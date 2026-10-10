"""Local-only setup engine used by the Windows wizard and silent entry point.

Exit codes: 0 success; 2 invalid input; 3 prerequisites missing; 4 stage failure;
5 another setup is running; 6 cancelled; 3010 prerequisites require a reboot.
Nothing from a player's dump is uploaded. All executable source comes from the
inventory verified by the packaged bootstrap, never from a moving branch.
"""
import argparse
import contextlib
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request

IS_WINDOWS = os.name == 'nt'
IS_MACOS = sys.platform == 'darwin'
# The payload manifest names the platform it was built for; a payload for
# another one is refused before anything in it runs.
HOST_PLATFORM = 'windows-x64' if IS_WINDOWS else 'macos-arm64' if IS_MACOS else 'linux-x64'
PRESET = 'win-x64-release' if IS_WINDOWS else 'posix-release'
HD_SUPPORTED = IS_WINDOWS or IS_MACOS  # drawn by the Direct3D 11 renderer and, since v0.6.1, the macOS Vulkan one
LAUNCHER_BUNDLE = 'Def Jam Recompiled.app'
RUNTIME_FOLDERS = ('source/', 'python/') if IS_WINDOWS else ('source/', 'python/', 'tools/', 'deps/')
APPLE_COMMAND_LINE_TOOLS_URL = 'https://developer.apple.com/documentation/xcode/installing-the-command-line-tools/'


class SetupError(Exception):
    def __init__(self, message, code=4):
        super().__init__(message)
        self.code = code


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.pending')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def write_status(path, message):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.pending')
    temporary.write_text(message, encoding='utf-8')
    os.replace(temporary, path)


def safe_relative(name):
    path = PurePosixPath(name)
    if (not name or '\\' in name or ':' in name or path.is_absolute()
            or any(p in {'', '.', '..'} or p.rstrip(' .') != p for p in name.split('/'))):
        raise SetupError('Unsafe payload or disc path: ' + name, 2)
    if any(re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?', p, re.I)
           for p in path.parts):
        raise SetupError('Reserved Windows filename: ' + name, 2)
    return Path(*path.parts)


def verify_payload(payload, platform=None):
    payload = Path(payload).resolve()
    manifest = json.loads((payload / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('schema') != 1 or manifest.get('platform') != (platform or HOST_PLATFORM):
        raise SetupError('Unsupported setup payload', 2)
    if not re.fullmatch(r'\d+\.\d+\.\d+', manifest.get('version', '')):
        raise SetupError('Invalid release version', 2)
    for key in ('source_commit', 'toolkit_commit'):
        if not re.fullmatch('[0-9a-f]{40}', manifest.get(key, '')):
            raise SetupError('Invalid release identity: ' + key, 2)
    expected = manifest['files']
    for name, sha in expected.items():
        path = payload / safe_relative(name)
        if path.is_symlink() or not path.is_file() or digest(path) != sha:
            raise SetupError('Payload verification failed: ' + name, 2)
    actual = {p.relative_to(payload).as_posix() for p in payload.rglob('*') if p.is_file()}
    if actual != set(expected) | {'manifest.json'}:
        raise SetupError('Unexpected files in setup payload', 2)
    return manifest


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_dump(folder, source):
    module = load_module('setup_verify_dump', source / 'scripts/verify-dump.py')
    manifest = json.loads((source / 'config/dump-manifest.json').read_text(encoding='utf-8'))
    problems = module.verify(str(folder), manifest)
    if problems:
        raise SetupError('Dump verification failed: ' + '; '.join(problems), 2)


def nearest_existing(path):
    path = Path(path)
    while not path.exists():
        path = path.parent
    return path


def validate_locations(install, data, dump):
    install, data, dump = (Path(p).resolve() for p in (install, data, dump))
    for first, second in ((install, data), (install, dump), (data, dump)):
        if first == second or first in second.parents or second in first.parents:
            raise SetupError('Install, data and dump locations must be separate, non-nested folders', 2)
    for target in (install, data):
        if any(c in str(target) for c in ('\r', '\n')):
            raise SetupError('Destination paths cannot contain line breaks', 2)
        if target == Path(target.anchor) or len(str(target)) > 140:
            raise SetupError('Choose a shorter writable folder (at most 140 characters)', 2)
        if target.exists() and not target.is_dir():
            raise SetupError('Destination is not a directory', 2)
    return install, data, dump


@contextlib.contextmanager
def install_lock(root):
    """OS-held lock: released even after a crash; a stale file is harmless."""
    root.mkdir(parents=True, exist_ok=True)
    stream = (root / 'setup.lock').open('a+b')
    try:
        stream.seek(0)
        if os.name == 'nt':
            import msvcrt
            if (root / 'setup.lock').stat().st_size == 0:
                stream.write(b'0')
                stream.flush()
            stream.seek(0)
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as ex:
                raise SetupError('Another setup is using this destination', 5) from ex
        else:
            import fcntl
            try:
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as ex:
                raise SetupError('Another setup is using this destination', 5) from ex
        yield
    finally:
        stream.close()


@contextlib.contextmanager
def game_lock(root):
    if not IS_WINDOWS:
        # The launcher holds an flock on play.lock for as long as the game runs
        # (it execs the game with the descriptor open), so this fails while the
        # game is up and succeeds again the moment it exits, even after a crash.
        root.mkdir(parents=True, exist_ok=True)
        import fcntl
        stream = (root / 'play.lock').open('a+b')
        try:
            try:
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as ex:
                raise SetupError('Close the installed game before updating or repairing it', 5) from ex
            yield
        finally:
            stream.close()
        return
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                  ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.CreateFileW(str(root / 'play.lock'), 0xC0000000, 0, None, 4, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise SetupError('Close the installed game before updating or repairing it', 5)
    try:
        yield
    finally:
        kernel.CloseHandle(handle)


class Engine:
    def __init__(self, args):
        self.args = args
        self.payload = Path(args.payload).resolve()
        self.manifest = verify_payload(self.payload)
        self.install, self.data, self.dump = validate_locations(args.install_dir, args.data_dir, args.dump)
        identity = self.manifest['source_commit'][:12] + '-' + self.manifest['toolkit_commit'][:12]
        self.version_root = self.install / 'versions' / (self.manifest['version'] + '-' + identity)
        self.source = self.version_root / 'source'
        self.env = os.environ.copy()
        # Do not inherit the developer's test switches or another installation's data.
        for key in list(self.env):
            if key.startswith('RECOMP_') or key in {'PYTHONHOME', 'PYTHONPATH', 'DEFJAM_DATA'}:
                self.env.pop(key)
        self.env['DEFJAM_DATA'] = str(self.data)
        self.env['PYTHONUTF8'] = '1'
        if IS_WINDOWS:
            self.env['PATH'] = str(self.payload / 'python') + os.pathsep + self.env.get('PATH', '')
            self.python = self.version_root / 'python/python.exe'
        else:
            # A build must not pick up a package manager's compiler, headers or
            # libraries: the toolchain is the system's, everything else is bundled.
            for key in list(self.env):
                if key.startswith(('DYLD_', 'HOMEBREW_', 'CMAKE_', 'PKG_CONFIG', 'CONDA', 'VIRTUAL_ENV')) or key in {
                        'CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'CPPFLAGS', 'CPATH', 'LIBRARY_PATH',
                        'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET', 'VK_ICD_FILENAMES', 'VK_DRIVER_FILES',
                        'SDL_VULKAN_LIBRARY', 'PYTHONSTARTUP', 'PYTHONUSERBASE'}:
                    self.env.pop(key)
            self.env['PATH'] = os.pathsep.join(
                [str(self.version_root / 'python/bin'), str(self.version_root / 'tools/bin'),
                 '/usr/bin', '/bin', '/usr/sbin', '/sbin'])
            self.python = self.version_root / 'python/bin/python3'
        self.log = None

    def event(self, message):
        print(message, flush=True)
        if self.log:
            self.log.write(message + '\n')
            self.log.flush()
        if self.args.status_file:
            write_status(self.args.status_file, message)

    def cancelled(self):
        if self.args.cancel_file and Path(self.args.cancel_file).exists():
            raise SetupError('Setup cancelled; run again to resume completed work', 6)

    def run(self, argv, cwd=None, env=None, capture=False, log_output=True, progress=False):
        self.cancelled()
        argv = list(argv)
        if Path(argv[0]) == self.python:
            argv.insert(1, '-B')
        child_env = env or self.env
        # Windows CreateProcess searches the parent's PATH, ignoring a custom
        # child environment. Resolve tools against the imported VS environment.
        if Path(argv[0]).name == str(argv[0]):
            argv[0] = shutil.which(str(argv[0]), path=child_env.get('PATH', '')) or argv[0]
        if self.log:
            self.log.write('Running: ' + (subprocess.list2cmdline if IS_WINDOWS else shlex.join)(list(map(str, argv))) + '\n')
            self.log.flush()
        # Redirect to disk, not a pipe: compiler bursts cannot deadlock the UI.
        with tempfile.TemporaryFile() as output:
            with subprocess.Popen(list(map(str, argv)), cwd=cwd, env=child_env,
                                  stdout=output, stderr=subprocess.STDOUT,
                                  **({'creationflags': subprocess.CREATE_NO_WINDOW} if IS_WINDOWS
                                     else {'start_new_session': True})) as child:
                position = 0
                progress_text = ''
                try:
                    while child.poll() is None:
                        self.cancelled()
                        time.sleep(.2)
                        output.seek(position)
                        chunk = output.read()
                        position += len(chunk)
                        if chunk and progress and self.args.status_file:
                            progress_text += chunk.decode('utf-8', errors='replace')
                            lines = progress_text.split('\n')
                            progress_text = lines.pop()
                            for line in lines:
                                match = re.match(r'PROGRESS\s+(\d+)\s*/\s*(\d+)', line)
                                if match:
                                    write_status(self.args.status_file, 'Upscaling HD textures: ' + match[1] + ' / ' + match[2])
                        if chunk and self.log and log_output:
                            self.log.write(chunk.decode('utf-8', errors='replace'))
                            self.log.flush()
                except SetupError:
                    if IS_WINDOWS:
                        subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'],
                                       capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
                    else:
                        # The child leads its own session, so this reaches the whole
                        # compiler tree and not only the first process.
                        for sig in (signal.SIGTERM, signal.SIGKILL):
                            with contextlib.suppress(ProcessLookupError, PermissionError):
                                os.killpg(child.pid, sig)
                            try:
                                child.wait(timeout=5)
                                break
                            except subprocess.TimeoutExpired:
                                continue
                    child.wait()
                    raise
                output.seek(position)
                if self.log and log_output:
                    self.log.write(output.read().decode('utf-8', errors='replace'))
                    self.log.flush()
                if child.returncode:
                    raise SetupError(f'Stage failed with exit {child.returncode}; see setup log')
            output.seek(0)
            return output.read().decode('utf-8', errors='replace') if capture else ''

    def prepare_source(self):
        self.event('Preparing verified release source')
        # Resume generated/build artifacts while rechecking every distributed source file.
        self.source.mkdir(parents=True, exist_ok=True)
        for name, sha in self.manifest['files'].items():
            if not name.startswith(RUNTIME_FOLDERS):
                continue
            destination = self.version_root / safe_relative(name)
            if not destination.is_file() or digest(destination) != sha:
                destination.parent.mkdir(parents=True, exist_ok=True)
                # copy, not copyfile: the bundled interpreter and tools keep their execute bit.
                shutil.copy(self.payload / name, destination)
        previous = self.install / 'installed.json'
        candidates = [p for p in (self.install / 'versions').glob('*/source')
                      if p != self.source and (p / 'src/recomp/gen/lift-state.json').is_file()]
        if previous.is_file() or candidates:
            old = (Path(json.loads(previous.read_text(encoding='utf-8'))['source']).resolve()
                   if previous.is_file() else max(candidates, key=lambda p: p.stat().st_mtime).resolve())
            versions = (self.install / 'versions').resolve()
            if versions not in old.parents or old == self.source:
                return
            # Reuse only local analysis/lift artifacts; the incoming pipeline must
            # validate all inputs/outputs before skipping a stage. Never reuse objects.
            for relative in ('src/recomp/gen', 'tools/xboxrecomp/game_files',
                             'tools/xboxrecomp/tools/disasm/output', 'tools/xboxrecomp/tools/func_id/output',
                             'tools/xboxrecomp/tools/abi_analysis/output'):
                src, dst = old / relative, self.source / relative
                if src.is_dir() and (not dst.exists() or (relative == 'src/recomp/gen' and
                                                          not (dst / 'lift-state.json').exists())):
                    shutil.copytree(src, dst, dirs_exist_ok=True)

    def check_install_destination(self):
        marker = self.install / 'setup-owner.json'
        identity = {'schema': 1, 'product': 'DefJamRecompiled'}
        if marker.exists():
            if json.loads(marker.read_text(encoding='utf-8')) != identity:
                raise SetupError('Destination has a different setup ownership marker', 2)
            return
        receipt_file = self.install / 'installed.json'
        if receipt_file.exists():
            receipt = json.loads(receipt_file.read_text(encoding='utf-8'))
            if receipt.get('schema') != 1 or (self.install / 'versions') not in Path(receipt['source']).resolve().parents:
                raise SetupError('Destination has an invalid installation receipt', 2)
        elif any(p.name not in {'logs', 'setup.lock', 'play.lock', '.DS_Store'} for p in self.install.iterdir()):
            raise SetupError('Choose an empty install folder or a recognized Def Jam installation', 2)
        write_json(marker, identity)

    def check_existing_install(self):
        receipt_path = self.install / 'installed.json'
        if not receipt_path.exists():
            return
        receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
        if Path(receipt['data']).resolve() != self.data:
            raise SetupError('Updates must use the installed data folder: ' + receipt['data'], 2)
        if not IS_WINDOWS:
            # game_lock covers the launcher; this also catches the executable started by hand.
            listing = self.run(['ps', '-axo', 'comm='], capture=True, log_output=False)
            if any(line.strip() == receipt['executable'] for line in listing.splitlines()):
                raise SetupError('Close the installed game before updating or repairing it', 5)
            return
        self.env['DEFJAM_SETUP_INSTALLED_EXE'] = receipt['executable']
        result = self.run(['powershell.exe', '-NoProfile', '-Command',
            "if (Get-Process defjam_recomp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $env:DEFJAM_SETUP_INSTALLED_EXE }) { Write-Output 'GAME_RUNNING' }"], capture=True)
        if 'GAME_RUNNING' in result:
            raise SetupError('Close the installed game before updating or repairing it', 5)

    def extract(self):
        self.event('Verifying and preparing your dump')
        if not self.dump.exists():
            raise SetupError('Dump does not exist', 2)
        extracted = self.data / 'extracted'
        state = self.data / 'extraction-state.json'
        if self.dump.is_dir():
            validate_dump(self.dump, self.source)
            # Copy once, never merge arbitrary user data into an existing extraction.
            files = sorted(p for p in self.dump.rglob('*') if p.is_file())
            if any(p.is_symlink() for p in self.dump.rglob('*')):
                raise SetupError('Dump folder must not contain symlinks', 2)
            fingerprint = {p.relative_to(self.dump).as_posix(): digest(p) for p in files}
        else:
            fingerprint = {'image': digest(self.dump)}
        if state.is_file() and extracted.is_dir():
            recorded = json.loads(state.read_text(encoding='utf-8'))
            current = {p.relative_to(extracted).as_posix(): digest(p)
                       for p in extracted.rglob('*') if p.is_file()}
            if recorded.get('input') == fingerprint and recorded.get('outputs') == current:
                validate_dump(extracted, self.source)
                self.event('Verified extraction reused')
                return
        if extracted.exists():
            raise SetupError('Existing data/extracted differs from this dump; choose a new data folder', 2)
        staging = Path(tempfile.mkdtemp(prefix='extract-pending-', dir=self.data))
        try:
            if self.dump.is_dir():
                for p in files:
                    self.cancelled()
                    destination = staging / safe_relative(p.relative_to(self.dump).as_posix())
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(p, destination)
            else:
                # Use the pinned toolkit reader, but validate all names/bounds/cycles here.
                sys.path.insert(0, str(self.source / 'tools/xboxrecomp'))
                try:
                    from tools.xiso.xdvdfs import Xiso, XisoError, SECTOR
                    with Xiso(str(self.dump)) as iso:
                        seen_dirs, seen_files = set(), set()
                        pending = [('', iso.root_sector, iso.root_size)]
                        total_size = self.dump.stat().st_size
                        while pending:
                            directory, sector, size = pending.pop()
                            if (sector, size) in seen_dirs or size > 16 * 1024 * 1024:
                                raise SetupError('Malformed disc directory', 2)
                            seen_dirs.add((sector, size))
                            if iso.base + sector * SECTOR + size > total_size:
                                raise SetupError('Truncated disc directory', 2)
                            for entry in iso._read_dir(sector, size):
                                self.cancelled()
                                name = (directory + '/' + entry.name).lstrip('/')
                                destination = staging / safe_relative(name)
                                folded = name.casefold()
                                if folded in seen_files or len(seen_files) >= 100000:
                                    raise SetupError('Duplicate or excessive disc entries', 2)
                                seen_files.add(folded)
                                if iso.base + entry.sector * SECTOR + entry.size > total_size:
                                    raise SetupError('Truncated disc entry', 2)
                                if entry.is_dir:
                                    pending.append((name, entry.sector, entry.size))
                                else:
                                    iso.extract(entry, str(destination))
                except XisoError as ex:
                    raise SetupError('Invalid Xbox disc image: ' + str(ex), 2) from ex
                finally:
                    sys.path.pop(0)
            validate_dump(staging, self.source)
            outputs = {p.relative_to(staging).as_posix(): digest(p)
                       for p in staging.rglob('*') if p.is_file()}
            write_json(state, {'input': fingerprint, 'outputs': outputs})
            staging.rename(extracted)
        finally:
            # Only this fresh, owned staging directory may be removed.
            if staging.exists():
                shutil.rmtree(staging)

    def toolchain_macos(self):
        self.event('Checking the Xcode Command Line Tools, CMake and Ninja')
        if not IS_MACOS:
            raise SetupError('Setup for this operating system is not available yet', 3)
        if platform.machine() != 'arm64':
            raise SetupError('This setup supports Apple Silicon Macs only', 3)
        advice = ('Install the Xcode Command Line Tools (instructions: ' + APPLE_COMMAND_LINE_TOOLS_URL
                  + '), then run setup again.')
        selected = subprocess.run(['/usr/bin/xcode-select', '-p'], capture_output=True, text=True)
        if selected.returncode or not Path(selected.stdout.strip()).is_dir():
            if getattr(self.args, 'install_prerequisites', False):
                # Apple's own installer window; nothing is installed by setup itself.
                self.run(['/usr/bin/xcode-select', '--install'])
                raise SetupError('Finish the Command Line Tools installer that just opened, then run setup again.', 3)
            raise SetupError('The Xcode Command Line Tools are not installed. ' + advice, 3)
        try:
            sdk = self.run(['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path'], capture=True).strip()
            probe = self.version_root / 'toolchain-check'
            probe.mkdir(parents=True, exist_ok=True)
            (probe / 'check.c').write_text('int main(void) { return 0; }\n', encoding='utf-8')
            self.run(['/usr/bin/xcrun', '--sdk', 'macosx', 'clang', '-arch', 'arm64',
                      '-o', probe / 'check', probe / 'check.c'])
        except SetupError as ex:
            if ex.code == 6:
                raise
            raise SetupError('The Xcode Command Line Tools could not build a test program. ' + advice, 3) from ex
        self.env['SDKROOT'] = sdk
        for executable in ('cmake', 'ninja'):
            if not shutil.which(executable, path=self.env['PATH']):
                raise SetupError('Missing bundled build tool: ' + executable, 3)
            self.run([executable, '--version'])

    def toolchain(self, provisioned=False):
        if not IS_WINDOWS:
            return self.toolchain_macos()
        self.event('Checking C++ compiler, Windows SDK, CMake and Ninja')
        vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        if not vswhere.is_file():
            if not self.args.install_prerequisites or provisioned:
                raise SetupError('Visual Studio Build Tools missing; use --install-prerequisites or the wizard checkbox', 3)
            self.install_prerequisites()
            return self.toolchain(provisioned=True)
        query = [vswhere, '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                 '-sort', '-latest', '-property', 'installationPath']
        installation = self.run(query, capture=True).strip().splitlines()
        if not installation:
            if self.args.install_prerequisites and not provisioned:
                self.install_prerequisites()
                return self.toolchain(provisioned=True)
            raise SetupError('C++ Build Tools workload missing', 3)
        vs = Path(installation[0])
        # Import VsDevCmd through a fixed script with an environment-supplied path.
        devcmd = vs / 'Common7/Tools/VsDevCmd.bat'
        self.env['DEFJAM_SETUP_VSDEVCMD'] = str(devcmd)
        batch = self.version_root / 'toolchain.cmd'
        batch.write_text('@echo off\ncall "%DEFJAM_SETUP_VSDEVCMD%" -arch=amd64 -host_arch=amd64 -no_logo\nif errorlevel 1 exit /b %errorlevel%\nset\n', encoding='utf-8')
        # CMD does not understand list2cmdline's backslash-escaped inner quotes.
        # Expand one quoted environment value after parsing a constant command;
        # omit CALL, whose second expansion would reinterpret percent characters.
        self.env['DEFJAM_SETUP_ENV_SCRIPT'] = '"' + str(batch) + '"'
        lines = self.run(['cmd.exe', '/d', '/c', '%DEFJAM_SETUP_ENV_SCRIPT%'], capture=True,
                         log_output=False)  # SET includes inherited environment secrets.
        for line in lines.splitlines():
            key, equal, value = line.partition('=')
            if equal and key and not key.startswith('='):
                self.env[key] = value
        additions = [self.payload / 'python', vs / 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin',
                     vs / 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja']
        self.env['PATH'] = os.pathsep.join(map(str, additions)) + os.pathsep + self.env.get('PATH', '')
        for executable in ('cl.exe', 'cmake.exe', 'ninja.exe', 'rc.exe'):
            if not shutil.which(executable, path=self.env['PATH']):
                if self.args.install_prerequisites and not provisioned:
                    self.install_prerequisites()
                    return self.toolchain(provisioned=True)
                raise SetupError('Missing toolchain component: ' + executable, 3)
        self.run(['cmake', '--version'])
        self.run(['ninja', '--version'])

    def install_prerequisites(self):
        self.event('Installing Microsoft Build Tools; Windows may request administrator consent')
        cache = self.install / 'downloads'
        cache.mkdir(exist_ok=True)
        bootstrapper = cache / 'vs_buildtools.exe'
        urllib.request.urlretrieve('https://aka.ms/vs/17/release/vs_buildtools.exe', bootstrapper)
        self.env['DEFJAM_SETUP_BOOTSTRAPPER'] = str(bootstrapper)
        # Authenticode is checked before requesting elevation. No user strings in script.
        script = self.payload / 'engine/install-prerequisites.ps1'
        result = self.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script], capture=True)
        if 'REBOOT_REQUIRED' in result:
            raise SetupError('Build Tools installed; restart Windows and rerun setup', 3010)

    def link_game(self):
        game = self.source / 'game'
        if game.exists():
            if game.resolve() != (self.data / 'extracted').resolve():
                raise SetupError('Existing game link points to different data', 2)
            return
        if not IS_WINDOWS:
            game.symlink_to(self.data / 'extracted', target_is_directory=True)
            return
        self.env['DEFJAM_SETUP_LINK'] = str(game)
        self.env['DEFJAM_SETUP_TARGET'] = str(self.data / 'extracted')
        self.run(['powershell.exe', '-NoProfile', '-Command',
                  "New-Item -ItemType Junction -Path $env:DEFJAM_SETUP_LINK -Target $env:DEFJAM_SETUP_TARGET -ErrorAction Stop | Out-Null"])

    def pipeline(self):
        state = load_module('setup_pipeline_state', self.source / 'scripts/pipeline-state.py')
        tk = self.source / 'tools/xboxrecomp'
        for label, verify, action in (
            ('Analysis', lambda: state.verify_analysis(self.source, tk), 'analyze'),
            ('Lift', lambda: state.verify_lift(self.source, tk), 'recomp'),
            ('Build', lambda: state.verify_build(self.source, PRESET), 'build')):
            self.cancelled()
            try:
                verify()
                self.event(label + ' already verified; reused')
                continue
            except (OSError, ValueError, KeyError, json.JSONDecodeError):
                pass
            self.event(label + ' in progress (this may take several minutes)')
            argv = [self.python, self.source / 'scripts/pipeline.py', action]
            if action == 'analyze':
                argv += ['--data', self.data]
            elif action == 'build':
                # Populate FetchContent from the inventoried, offline dependency source.
                self.run([self.python, self.source / 'scripts/pipeline-state.py', 'begin-build',
                          '--preset', PRESET], cwd=self.source)
                if IS_WINDOWS:
                    deps = json.loads((self.payload / 'dependencies.json').read_text())
                    overrides = [f'-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={self.source / item["source_dir"]}'
                                 for name, item in zip(('sdl3', 'imgui'), [d for d in deps if 'source_dir' in d])]
                else:
                    # SDL3, MoltenVK and shaderc come prebuilt with the setup; nothing is
                    # looked for in /opt/homebrew, /usr/local or any other prefix.
                    prefix = self.version_root / 'deps'
                    # Dear ImGui (the launcher and overlay) is built from the inventoried source.
                    deps = json.loads((self.payload / 'dependencies.json').read_text())
                    imgui = [d for d in deps if '/imgui/' in d.get('source_dir', '').replace('\\', '/') + '/']
                    if len(imgui) != 1:
                        raise SetupError('The setup payload has no Dear ImGui source', 2)
                    overrides = [f'-DFETCHCONTENT_SOURCE_DIR_IMGUI={self.source / imgui[0]["source_dir"]}',
                                 f'-DCMAKE_PREFIX_PATH={prefix}', f'-DVulkan_LIBRARY={prefix}/lib/libMoltenVK.dylib',
                                 f'-DVulkan_INCLUDE_DIR={prefix}/include', '-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF',
                                 '-DCMAKE_OSX_ARCHITECTURES=arm64']
                self.run(['cmake', '--preset', PRESET, '-DDEFJAM_BUILD_GAME=ON', *overrides], cwd=self.source)
                self.run(['cmake', '--build', '--preset', PRESET, '--target', 'defjam_recomp'], cwd=self.source)
                self.run([self.python, self.source / 'scripts/pipeline-state.py', 'record-build',
                          '--preset', PRESET], cwd=self.source)
                verify()
                continue
            self.run(argv, cwd=self.source)
            verify()

    def build_hd_textures(self):
        if not getattr(self.args, 'hd_textures', False):
            return
        if not HD_SUPPORTED:
            raise SetupError('HD textures are not supported on this platform yet', 2)
        self.event('Upscaling HD textures with Lanczos 4x; several minutes, CPU only')
        receipt = self.version_root / 'hd-pack-build.json'
        self.run([self.python, self.source / 'scripts/build-hd-pack.py',
                  '--root', self.data / 'extracted', '--work', self.data / 'hd-work/setup-lanczos',
                  '--mods', self.data / 'mods', '--receipt', receipt,
                  '--aliases', self.source / 'config/hd-runtime-aliases.json'], progress=True)
        result = json.loads(receipt.read_text(encoding='utf-8'))
        self.hd_packs = result['packs']
        self.event('HD textures ready (' + str(result['images']) + ' images); awaiting install activation')

    def make_launcher_bundle(self, destination, root):
        """A tiny local app: it takes the play lock, sets the data folder and execs the
        game, so the game runs as this app. Made here, never downloaded, so Gatekeeper has
        nothing to say about it; the ad-hoc signature is what Apple Silicon needs to run it."""
        destination = Path(destination)
        pending = destination.with_name(destination.name + '.new')
        shutil.rmtree(pending, ignore_errors=True)
        shutil.copytree(self.payload / 'launcher' / LAUNCHER_BUNDLE, pending, symlinks=True)
        # The payload inventories files only, so an empty Resources folder is not in it.
        (pending / 'Contents/Resources').mkdir(parents=True, exist_ok=True)
        (pending / 'Contents/Resources/install-root.txt').write_text(str(root), encoding='utf-8')
        self.run(['/usr/bin/codesign', '--force', '--sign', '-', pending])
        if destination.is_dir() and not destination.is_symlink():
            shutil.rmtree(destination)
        else:
            destination.unlink(missing_ok=True)
        os.replace(pending, destination)

    def install_launcher_bundle(self, receipt):
        bundle = self.install / LAUNCHER_BUNDLE
        self.make_launcher_bundle(bundle, self.install)
        shortcuts = []
        if not self.args.no_shortcuts:
            try:
                applications = Path.home() / 'Applications'
                applications.mkdir(exist_ok=True)
                self.make_launcher_bundle(applications / LAUNCHER_BUNDLE, self.install)
                shortcuts.append(str(applications / LAUNCHER_BUNDLE))
                if not self.args.no_desktop_shortcut:
                    alias = Path.home() / 'Desktop' / 'Def Jam Recompiled'
                    alias.unlink(missing_ok=True)
                    self.run([bundle / 'Contents/MacOS/DefJamLauncher', '--make-alias', bundle, alias])
                    shortcuts.append(str(alias))
            except (OSError, SetupError) as ex:
                if isinstance(ex, SetupError) and ex.code == 6:
                    raise
                # A refused folder permission must not undo a finished build.
                self.event('Could not create every shortcut (' + str(ex) + '); open the app in the install folder instead')
        receipt['shortcuts'] = shortcuts
        return bundle

    def activate(self):
        self.event('Creating launch shortcuts and install receipt')
        receipt = {key: self.manifest[key] for key in ('version', 'source_commit', 'toolkit_commit')}
        previous = self.install / 'installed.json'
        receipt['hd_packs'] = getattr(self, 'hd_packs', json.loads(previous.read_text()).get('hd_packs', []) if previous.exists() else [])
        receipt.update(schema=1, source=str(self.source), data=str(self.data),
                       executable=str(self.source / 'build' / PRESET / ('defjam_recomp.exe' if IS_WINDOWS else 'defjam_recomp')))
        if IS_WINDOWS:
            # Stable native launcher reads this atomic receipt and supplies the data env.
            shutil.copyfile(self.payload / 'DefJamLauncher.exe', self.install / 'DefJamLauncher.exe.new')
            os.replace(self.install / 'DefJamLauncher.exe.new', self.install / 'DefJamLauncher.exe')
            launcher = self.install / 'DefJamLauncher.exe'
            self.env['DEFJAM_SETUP_LAUNCHER'] = str(launcher)
            self.env['DEFJAM_SETUP_ROOT'] = str(self.install)
            self.env['DEFJAM_SETUP_DESKTOP_SHORTCUT'] = '0' if self.args.no_desktop_shortcut else '1'
            if not self.args.no_shortcuts:
                self.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                          self.payload / 'engine/shortcuts.ps1'])
        else:
            launcher = self.install_launcher_bundle(receipt)
        self.cancelled()
        # Commit HD selection with the receipts only after cancellable work.
        # Restore exact prior bytes if any final publication fails.
        paths = [self.install / 'installed.json'] + ([self.install / 'installed.ini'] if IS_WINDOWS else [])
        if hasattr(self, 'hd_packs'):
            paths.append(self.data / 'settings.ini')
        before = {path: path.read_bytes() if path.exists() else None for path in paths}
        try:
            if hasattr(self, 'hd_packs'):
                old_packs = json.loads(before[previous]).get('hd_packs', []) if before[previous] else []
                enable_hd_packs(self.data, self.hd_packs, old_packs)
            write_json(self.install / 'installed.json', receipt)
            if IS_WINDOWS:
                temporary = self.install / 'installed.ini.pending'
                temporary.write_text('[install]\n' + ''.join(f'{key}={receipt[key]}\n' for key in
                                     ('source', 'data', 'executable')), encoding='utf-16')
                os.replace(temporary, self.install / 'installed.ini')
        except (OSError, ValueError, SetupError):
            for path, content in before.items():
                if content is None:
                    path.unlink(missing_ok=True)
                else:
                    pending = path.with_suffix('.rollback')
                    pending.write_bytes(content)
                    os.replace(pending, path)
            raise
        self.event('Setup complete. Select Play or double-click: ' + str(launcher))

    def execute(self):
        if getattr(self.args, 'hd_textures', False) and not HD_SUPPORTED:
            raise SetupError('HD textures are not supported on this platform yet', 2)
        self.data.mkdir(parents=True, exist_ok=True)
        with install_lock(self.install), install_lock(self.data), game_lock(self.install):
            log_path = Path(self.args.log) if self.args.log else self.install / 'logs' / time.strftime('setup-%Y%m%d-%H%M%S.log')
            log_path.parent.mkdir(parents=True, exist_ok=True)
            with log_path.open('a', encoding='utf-8') as log:
                self.log = log
                self.event('Setup ' + json.dumps({k: self.manifest[k] for k in ('version', 'source_commit', 'toolkit_commit')}))
                self.check_install_destination()
                self.check_existing_install()
                # At least 12 GiB build headroom plus extracted files; measured later on clean VM.
                required = 12 * 1024**3
                if shutil.disk_usage(nearest_existing(self.install)).free < required:
                    raise SetupError('At least 12 GiB free space is required on the install drive', 2)
                if not self.dump.exists():
                    raise SetupError('Dump does not exist', 2)
                dump_size = sum(p.stat().st_size for p in self.dump.rglob('*') if p.is_file()) if self.dump.is_dir() else self.dump.stat().st_size
                if IS_WINDOWS:
                    same_drive = self.install.anchor.casefold() == self.data.anchor.casefold()
                else:
                    same_drive = os.stat(nearest_existing(self.install)).st_dev == os.stat(nearest_existing(self.data)).st_dev
                hd_space = 15 * 1024**3 if getattr(self.args, 'hd_textures', False) else 0
                if shutil.disk_usage(nearest_existing(self.data)).free < dump_size + 1024**3 + hd_space + (required if same_drive else 0):
                    raise SetupError('Insufficient free space on the data drive', 2)
                self.prepare_source()
                self.extract()
                self.toolchain()
                self.link_game()
                self.pipeline()
                self.build_hd_textures()
                self.activate()


def enable_hd_packs(data, packs, previous=()):
    """Change only texture enabled/pack keys; preserve other settings and comments."""
    if not packs or any(not re.fullmatch(r'faithful-hd-[0-9a-f]{12}-(opacity|channels)', p) for p in packs):
        raise SetupError('Invalid generated HD pack names', 2)
    path = Path(data) / 'settings.ini'
    if path.is_symlink():
        raise SetupError('Settings must not be a symlink', 2)
    text = path.read_text(encoding='utf-8-sig') if path.exists() else ''
    sections = list(re.finditer(r'(?ims)^[ \t]*\[[ \t]*textures[ \t]*\][ \t]*(?:\r?\n|\Z)(.*?)(?=^[ \t]*\[[^\r\n]+\][ \t]*$|\Z)', text))
    old_values = [m for section in sections for m in re.finditer(r'(?im)^[ \t]*packs[ \t]*=([^\r\n]*)', section.group(1))]
    old = old_values[-1] if old_values else None
    retained = [p.strip() for p in old.group(1).split(';') if p.strip() and p.strip() not in previous and p.strip() not in packs] if old else []
    combined = ';'.join(retained + list(packs))
    if len(combined.encode('ascii')) >= 256:
        raise SetupError('Texture pack list is too long; shorten existing pack names', 2)
    def updated(body):
        for key, value in (('enabled','1'),('packs',combined)):
            pattern = r'(?im)^([ \t]*' + key + r'[ \t]*=)[^\r\n]*'
            if re.search(pattern,body):
                body = re.sub(pattern,lambda m:m.group(1)+value,body)
            else:
                body = body.rstrip('\r\n')+'\n'+key+'='+value+'\n'
        return body
    # Runtime accepts padded section names and later values win. Update every
    # matching section using the effective final pack list, preserving others.
    if sections:
        for section in reversed(sections):
            text = text[:section.start(1)]+updated(section.group(1))+text[section.end(1):]
    else:
        text = text.rstrip('\r\n')+'\n[textures]\n'+updated('')
    temporary=path.with_suffix('.pending')
    temporary.write_text(text,encoding='utf-8')
    os.replace(temporary,path)


def uninstall(install):
    root = Path(install).resolve()
    receipt_file = root / 'installed.json'
    if not receipt_file.is_file() or root == Path(root.anchor):
        raise SetupError('No recognized installation at this location', 2)
    receipt = json.loads(receipt_file.read_text(encoding='utf-8'))
    versions = root / 'versions'
    if receipt.get('schema') != 1 or versions.resolve() != versions or versions not in Path(receipt['source']).parents:
        raise SetupError('Invalid installation ownership receipt', 2)
    with install_lock(root), game_lock(root):
        # Python 3.13 rmtree does not traverse Windows junction contents. Data is
        # outside versions by contract; verify the final target before deletion.
        if versions.exists():
            shutil.rmtree(versions)
        for name in ('installed.ini', 'installed.json', 'DefJamLauncher.exe'):
            (root / name).unlink(missing_ok=True)
        if not IS_WINDOWS:
            bundle = root / LAUNCHER_BUNDLE
            if bundle.is_dir() and not bundle.is_symlink():
                shutil.rmtree(bundle)
            # Only the shortcuts this installation made and recorded, and only if
            # they still are what was made: a bundle that names this install, or an alias file.
            for name in receipt.get('shortcuts', []):
                shortcut = Path(name)
                marker = shortcut / 'Contents/Resources/install-root.txt'
                if shortcut.is_dir() and not shortcut.is_symlink() and marker.is_file() \
                        and marker.read_text(encoding='utf-8') == str(root):
                    shutil.rmtree(shortcut)
                elif shortcut.is_file() and not shortcut.is_symlink():
                    shortcut.unlink()
            print('Application removed. Dump, saves, settings and logs were preserved.', flush=True)
            return
        env = os.environ.copy()
        env['DEFJAM_SETUP_REMOVE_SHORTCUTS'] = '1'
        env['DEFJAM_SETUP_LAUNCHER'] = str(root / 'DefJamLauncher.exe')
        subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        Path(__file__).parent / 'shortcuts.ps1'], env=env, check=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    print('Application removed. Dump, saves, settings and logs were preserved.', flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload', required=True)
    parser.add_argument('--silent', action='store_true')
    parser.add_argument('--dump')
    parser.add_argument('--install-dir', required=True)
    parser.add_argument('--data-dir')
    parser.add_argument('--uninstall', action='store_true')
    parser.add_argument('--no-shortcuts', action='store_true', help='Do not create desktop/Start-menu shortcuts')
    parser.add_argument('--no-desktop-shortcut', action='store_true', help='Create only the Start-menu shortcut')
    parser.add_argument('--install-prerequisites', action='store_true')
    parser.add_argument('--hd-textures', action='store_true', help='Generate and enable Lanczos 4x HD textures locally (extra time/space)')
    parser.add_argument('--log')
    parser.add_argument('--status-file')
    parser.add_argument('--cancel-file')
    args = parser.parse_args(argv)
    engine = None
    try:
        if args.uninstall:
            verify_payload(Path(args.payload))
            uninstall(args.install_dir)
            return 0
        if not args.dump or not args.data_dir:
            raise SetupError('--dump and --data-dir are required for installation', 2)
        engine = Engine(args)
        engine.execute()
        return 0
    except (SetupError, OSError, ValueError, KeyError) as ex:
        message = 'Setup stopped: ' + str(ex)
        print(message, file=sys.stderr, flush=True)
        error_log = Path(engine.log.name) if engine and engine.log else Path(args.log) if args.log else None
        if error_log:
            error_log.parent.mkdir(parents=True, exist_ok=True)
            with error_log.open('a', encoding='utf-8') as log:
                log.write(message + '\n')
        if engine and args.status_file:
            write_status(args.status_file, message)
        return ex.code if isinstance(ex, SetupError) else 4


if __name__ == '__main__':
    sys.exit(main())
