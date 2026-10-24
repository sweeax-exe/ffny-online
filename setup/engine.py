"""Local-only setup engine used by the Windows, macOS and Linux wizards and silent entry points.

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
import struct
import subprocess
import sys
import tempfile
import time
import uuid
import urllib.parse
import urllib.request

IS_WINDOWS = os.name == 'nt'
IS_MACOS = sys.platform == 'darwin'
IS_LINUX = sys.platform.startswith('linux')
# The payload manifest names the platform it was built for; a payload for
# another one is refused before anything in it runs.
HOST_PLATFORM = 'windows-x64' if IS_WINDOWS else 'macos-arm64' if IS_MACOS else 'linux-x64'
PRESET = 'win-x64-release' if IS_WINDOWS else 'posix-release'
HD_SUPPORTED = IS_WINDOWS or IS_MACOS or IS_LINUX  # Direct3D 11 on Windows; Vulkan on macOS (v0.6.1) and Linux (v0.7.2)
LAUNCHER_BUNDLE = 'Def Jam Recompiled.app'
RUNTIME_FOLDERS = ('source/', 'python/') if IS_WINDOWS or IS_LINUX else ('source/', 'python/', 'tools/', 'deps/')
# Linux: the launcher script setup writes into the install folder, and the
# desktop entries and Steam shortcut that point at it.
LINUX_LAUNCHER = 'Def Jam Recompiled'
LINUX_DESKTOP_ID = 'defjam-recompiled'
LINUX_MARKER = 'X-DefJamRecompiled-Install'
# Steam artwork for the non-Steam shortcut: grid file suffix -> file name. The "Fan Recomp" set in
# branding/fan/steam comes first; whatever it lacks (hero) comes from branding/steam.
STEAM_ART = {'p': 'portrait.png', '': 'wide.png', '_hero': 'hero.png', '_logo': 'logo.png'}
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


STATUS_ATTEMPTS = 5


def write_status(path, message):
    """Publish one progress line for the wizard, best effort (issue #40).

    The wizard polls this file while setup replaces it. Windows refuses the replace while any
    reader holds the file without delete sharing (an older wizard, a scanner), so a refusal is
    retried briefly and then the previous line is left. A progress label never stops setup;
    receipts and payloads keep the strict write_json and their own failures.
    """
    path = Path(path)
    temporary = path.with_suffix('.pending')
    for attempt in range(STATUS_ATTEMPTS):
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_text(message, encoding='utf-8')
            os.replace(temporary, path)
            return True
        except OSError:
            if attempt + 1 < STATUS_ATTEMPTS:
                time.sleep(0.01 * 2 ** attempt)
    return False


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
    if 'updater' in manifest and not releases.capable(manifest['updater']):
        raise SetupError('Unsupported updater protocol/data format; use a compatible newer setup', 2)
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
    install, data = (Path(p).resolve() for p in (install, data))
    dump = Path(dump).resolve() if dump else None
    pairs = [(install, data)] + ([(install, dump), (data, dump)] if dump else [])
    for first, second in pairs:
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
def install_lock(root, name='setup.lock'):
    """OS-held lock: released even after a crash; a stale file is harmless."""
    root.mkdir(parents=True, exist_ok=True)
    stream = (root / name).open('a+b')
    try:
        stream.seek(0)
        if os.name == 'nt':
            import msvcrt
            if (root / name).stat().st_size == 0:
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


releases = load_module('setup_releases', Path(__file__).with_name('releases.py'))
transaction = load_module('setup_transaction', Path(__file__).with_name('transaction.py'))


class Engine:
    def __init__(self, args):
        self.args = args
        self.payload = Path(args.payload).resolve()
        self.manifest = verify_payload(self.payload)
        self.install, self.data, self.dump = validate_locations(args.install_dir, args.data_dir, args.dump)
        identity = self.manifest['source_commit'][:12] + '-' + self.manifest['toolkit_commit'][:12]
        self.operation = getattr(args, 'operation', 'auto')
        suffix = ('-attempt-' + uuid.uuid4().hex[:8]) if hasattr(args, 'operation') else ''
        self.version_root = self.install / 'versions' / (self.manifest['version'] + '-' + identity + suffix)
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
        self.container_image = None     # Linux: the build container, when the host has no toolchain
        self.log = None

    def event(self, message):
        print(message, flush=True)
        if self.log:
            self.log.write(message + '\n')
            self.log.flush()
        if self.args.status_file:
            write_status(self.args.status_file, message)

    def cancelled(self):
        parent = getattr(self.args, 'handoff_parent', None)
        if parent and not IS_WINDOWS:
            try:
                os.kill(parent, 0)
            except ProcessLookupError:
                raise SetupError('Setup handoff parent exited; cancelling the build', 6)
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
        if self.operation in ('update', 'downgrade', 'rebuild'):
            return  # Fresh analysis, lift and objects are the clean operation contract.
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
        elif any(p.name not in {'logs', 'setup.lock', 'selection.lock', 'play.lock', '.DS_Store'} for p in self.install.iterdir()):
            raise SetupError('Choose an empty install folder or a recognized Def Jam installation', 2)
        write_json(marker, identity)

    def check_existing_install(self):
        receipt_path = self.install / 'installed.json'
        if not receipt_path.exists():
            return
        receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
        if Path(receipt['data']).resolve() != self.data:
            raise SetupError('Updates must use the installed data folder: ' + receipt['data'], 2)
        if IS_LINUX:
            # ps truncates comm to 15 characters there; /proc names the real file.
            if receipt['executable'] in running_executables():
                raise SetupError('Close the installed game before updating or repairing it', 5)
            return
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
        extracted = self.data / 'extracted'
        if self.dump is None:
            if not (self.install / 'installed.json').is_file():
                raise SetupError('Choose a dump for the first installation', 2)
            state = self.data / 'extraction-state.json'
            if not state.is_file() or not extracted.is_dir() or extracted.is_symlink():
                raise SetupError('Installed dump is unavailable; provide the original dump', 2)
            if any(p.is_symlink() for p in extracted.rglob('*')):
                raise SetupError('Installed dump must not contain symlinks', 2)
            recorded = json.loads(state.read_text(encoding='utf-8'))
            current = {p.relative_to(extracted).as_posix(): digest(p) for p in extracted.rglob('*') if p.is_file()}
            if recorded.get('outputs') != current:
                raise SetupError('Installed dump changed; provide a verified original in a new data location', 2)
            validate_dump(extracted, self.source)
            self.event('Verified installed dump reused; original image is not needed')
            return
        if not self.dump.exists():
            raise SetupError('Dump does not exist', 2)
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

    def host_toolchain_problem(self):
        """None if this machine can build the game itself, else what is missing."""
        compiler = shutil.which('clang', path=self.env['PATH']) or shutil.which('cc', path=self.env['PATH'])
        missing = [name for name in ('cmake', 'ninja') if not shutil.which(name, path=self.env['PATH'])]
        if not compiler:
            missing.insert(0, 'a C compiler (clang)')
        if missing:
            return 'missing ' + ', '.join(missing)
        # One probe for the three libraries the game links and their headers.
        probe = self.version_root / 'toolchain-check'
        probe.mkdir(parents=True, exist_ok=True)
        (probe / 'check.c').write_text(
            '#include <SDL3/SDL.h>\n#include <vulkan/vulkan.h>\n#include <shaderc/shaderc.h>\n'
            'int main(void) { SDL_GetVersion(); shaderc_compiler_release(0);\n'
            '  return (int)vkGetInstanceProcAddr(0, "vkCreateInstance") == 0; }\n', encoding='utf-8')
        try:
            self.run([compiler, '-o', probe / 'check', probe / 'check.c', '-lSDL3', '-lvulkan', '-lshaderc_shared'],
                     log_output=True)
        except SetupError as ex:
            if ex.code == 6:
                raise
            return 'the SDL3, Vulkan or shaderc development files are missing'
        self.env.setdefault('CC', compiler)
        return None

    def container_recipe(self):
        pins = json.loads((self.source / 'setup/dependencies-linux.json').read_text(encoding='utf-8'))['container']
        if not re.fullmatch(r'[\w./-]+@sha256:[0-9a-f]{64}', pins['image']) or \
                not re.fullmatch(r'\d{4}/\d{2}/\d{2}', pins['archive_date']) or \
                not all(re.fullmatch(r'[a-z0-9][a-z0-9+_.-]*', p) for p in pins['packages']):
            raise SetupError('Invalid build container pins', 2)
        stamp = pins['archive_date'].replace('/', '') + 'T120000'
        # The archive's packages are signed by keys some of which have expired since;
        # gpg judges them as of the archive date, so the signatures are still checked.
        return ('FROM ' + pins['image'] + '\n'
                "RUN echo 'Server = https://archive.archlinux.org/repos/" + pins['archive_date'] +
                "/$repo/os/$arch' > /etc/pacman.d/mirrorlist \\\n"
                " && echo 'faked-system-time " + stamp + "' >> /etc/pacman.d/gnupg/gpg.conf \\\n"
                ' && pacman -Syy --noconfirm --needed ' + ' '.join(pins['packages']) + ' \\\n'
                " && sed -i '/^faked-system-time/d' /etc/pacman.d/gnupg/gpg.conf \\\n"
                ' && pacman -Scc --noconfirm\n'
                'ENV CC=clang CXX=clang++\n')

    def toolchain_linux(self):
        choice = getattr(self.args, 'toolchain', 'auto') or 'auto'
        self.event('Checking the build toolchain')
        if platform.machine() not in ('x86_64', 'AMD64'):
            raise SetupError('This setup supports x86-64 Linux only', 3)
        problem = None
        if choice in ('auto', 'host'):
            problem = self.host_toolchain_problem()
            if problem is None:
                self.event('Building with this computer\'s own compiler and libraries')
                self.record_toolchain('host')
                return
            if choice == 'host':
                raise SetupError('This computer cannot build the game: ' + problem +
                                 '. Install clang, cmake, ninja and the SDL3, Vulkan and shaderc development '
                                 'packages, or run setup with the build container.', 3)
        podman = shutil.which('podman', path=self.env['PATH'])
        if not podman:
            raise SetupError('This computer cannot build the game (' + (problem or 'no toolchain') +
                             ') and has no podman for the build container. Install podman, or clang, cmake, '
                             'ninja and the SDL3, Vulkan and shaderc development packages.', 3)
        recipe = self.container_recipe()
        self.container_image = 'localhost/defjam-recompiled-build:' + hashlib.sha256(recipe.encode()).hexdigest()[:12]
        exists = subprocess.run([podman, 'image', 'exists', self.container_image],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not exists:
            self.event('Preparing the build container (a one-time download of about 1 GB)')
            folder = self.version_root / 'container'
            folder.mkdir(parents=True, exist_ok=True)
            (folder / 'Containerfile').write_text(recipe, encoding='utf-8')
            try:
                self.run([podman, 'build', '--pull=missing', '-t', self.container_image, folder])
            except SetupError as ex:
                if ex.code == 6:
                    raise
                raise SetupError('The build container could not be prepared; check the internet connection '
                                 'and the setup log, then run setup again.', 3) from ex
        self.run(self.in_container(['clang', '--version'], self.version_root))
        self.event('Building in the build container')
        self.record_toolchain('container:' + self.container_image)

    def record_toolchain(self, identity):
        """A build tree belongs to one compiler: switching between host and container starts it afresh."""
        note = self.version_root / 'toolchain.txt'
        if note.is_file() and note.read_text(encoding='utf-8') != identity:
            shutil.rmtree(self.source / 'build' / PRESET, ignore_errors=True)
        note.write_text(identity, encoding='utf-8')

    def in_container(self, argv, cwd):
        """argv run in the build container, with the install and data folders at their own paths."""
        if not self.container_image:
            return list(argv)
        mounts = []
        for folder in sorted({str(self.install), str(self.data)}):
            mounts += ['-v', folder + ':' + folder]
        return ['podman', 'run', '--rm', '--userns=keep-id', '--security-opt', 'label=disable', '--network=none',
                *mounts, '-w', str(cwd), '-e', 'DEFJAM_DATA=' + str(self.data), self.container_image,
                *map(str, argv)]

    def check_host_runtime(self):
        """The game runs on this computer's own SDL3, Vulkan loader and shaderc: say so now if one is missing."""
        executable = self.source / 'build' / PRESET / 'defjam_recomp'
        result = subprocess.run(['ldd', '-r', str(executable)], capture_output=True, text=True, env=self.env)
        problems = sorted({line.strip() for line in (result.stdout + result.stderr).splitlines()
                           if 'not found' in line or 'undefined symbol' in line})
        if result.returncode or problems:
            if self.log:
                self.log.write('Runtime check:\n' + result.stdout + result.stderr)
            raise SetupError('The game was built but cannot run on this computer: ' +
                             '; '.join(problems[:4] or ['ldd failed']) +
                             '. It needs SDL3 3.2 or newer, the Vulkan loader and shaderc.', 3)

    def toolchain(self, provisioned=False):
        if IS_LINUX:
            return self.toolchain_linux()
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

    def imgui_override(self):
        """Dear ImGui (the launcher and overlay) built from the payload's inventoried source."""
        deps = json.loads((self.payload / 'dependencies.json').read_text())
        imgui = [d for d in deps if '/imgui/' in d.get('source_dir', '').replace('\\', '/') + '/']
        if len(imgui) != 1:
            raise SetupError('The setup payload has no Dear ImGui source', 2)
        return f'-DFETCHCONTENT_SOURCE_DIR_IMGUI={self.source / imgui[0]["source_dir"]}'

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
                argv += ['--data', self.data, '--no-archive']
            elif action == 'build':
                # Populate FetchContent from the inventoried, offline dependency source.
                self.run([self.python, self.source / 'scripts/pipeline-state.py', 'begin-build',
                          '--preset', PRESET], cwd=self.source)
                if IS_LINUX:
                    # The system's (or the build container's) SDL3, Vulkan and shaderc; Dear
                    # ImGui from the payload, since the container builds without a network.
                    overrides = [self.imgui_override()]
                elif IS_WINDOWS:
                    deps = json.loads((self.payload / 'dependencies.json').read_text())
                    overrides = [f'-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={self.source / item["source_dir"]}'
                                 for name, item in zip(('sdl3', 'imgui'), [d for d in deps if 'source_dir' in d])]
                else:
                    # SDL3, MoltenVK and shaderc come prebuilt with the setup; nothing is
                    # looked for in /opt/homebrew, /usr/local or any other prefix.
                    prefix = self.version_root / 'deps'
                    overrides = [self.imgui_override(),
                                 f'-DCMAKE_PREFIX_PATH={prefix}', f'-DVulkan_LIBRARY={prefix}/lib/libMoltenVK.dylib',
                                 f'-DVulkan_INCLUDE_DIR={prefix}/include', '-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF',
                                 '-DCMAKE_OSX_ARCHITECTURES=arm64']
                self.run(self.in_container(['cmake', '--preset', PRESET, '-DDEFJAM_BUILD_GAME=ON', *overrides],
                                           self.source), cwd=self.source)
                self.run(self.in_container(['cmake', '--build', '--preset', PRESET, '--target', 'defjam_recomp'],
                                           self.source), cwd=self.source)
                self.run([self.python, self.source / 'scripts/pipeline-state.py', 'record-build',
                          '--preset', PRESET], cwd=self.source)
                verify()
                if IS_LINUX:
                    self.check_host_runtime()
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

    def install_launcher_bundle(self, receipt, core=True):
        bundle = self.install / LAUNCHER_BUNDLE
        if core:
            self.make_launcher_bundle(bundle, self.install)
        shortcuts = []
        if not self.args.no_shortcuts:
            try:
                applications = Path.home() / 'Applications'
                applications.mkdir(exist_ok=True)
                destination = applications / LAUNCHER_BUNDLE
                note = destination / 'Contents/Resources/install-root.txt'
                if destination.exists() and (not note.is_file() or note.read_text() != str(self.install)):
                    raise SetupError('Existing Applications app belongs to another installation', 2)
                self.make_launcher_bundle(destination, self.install)
                shortcuts.append(str(applications / LAUNCHER_BUNDLE))
                if not self.args.no_desktop_shortcut:
                    alias = Path.home() / 'Desktop' / 'Def Jam Recompiled'
                    expected = receipt.get('shortcut_hashes', {}).get(str(alias))
                    if alias.exists() and (alias.is_symlink() or not alias.is_file() or expected is None or digest(alias) != expected):
                        self.event('Left the existing desktop alias alone; its ownership/content is not verified')
                    else:
                        pending = alias.with_name(alias.name + '.new')
                        self.run([bundle / 'Contents/MacOS/DefJamLauncher', '--make-alias', bundle, pending])
                        if pending.is_file():
                            os.replace(pending, alias)
                            receipt.setdefault('shortcut_hashes', {})[str(alias)] = digest(alias)
                        shortcuts.append(str(alias))
            except (OSError, SetupError) as ex:
                if isinstance(ex, SetupError) and ex.code == 6:
                    raise
                # A refused folder permission must not undo a finished build.
                self.event('Could not create every shortcut (' + str(ex) + '); open the app in the install folder instead')
        receipt['shortcuts'] = shortcuts
        return bundle

    def install_launcher_linux(self, receipt, core=True):
        """The launcher script, a menu entry, a desktop shortcut and, if asked, a Steam shortcut."""
        template = (self.payload / 'launcher/launcher.sh').read_text(encoding='utf-8')
        script = template
        for key in ('source', 'data', 'executable'):
            if any(c in receipt[key] for c in '\r\n'):
                raise SetupError('Install paths cannot contain line breaks', 2)
            script = script.replace('@' + key.upper() + '@', shlex.quote(receipt[key]))
        launcher = self.install / LINUX_LAUNCHER
        pending = launcher.with_name(launcher.name + '.new')
        if core:
            pending.write_text(script, encoding='utf-8')
            pending.chmod(0o755)
            os.replace(pending, launcher)
        icon = self.install / (LINUX_LAUNCHER + '.png')
        icon_source = self.source / 'branding/icon/png/defjam-256.png'
        if icon_source.is_file():
            if core:
                shutil.copyfile(icon_source, icon)
        else:
            icon = None
        entry = desktop_entry(launcher, self.install, icon)
        shortcuts = []
        if not self.args.no_shortcuts:
            data_home = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
            places = [data_home / 'applications' / (LINUX_DESKTOP_ID + '.desktop')]
            if not self.args.no_desktop_shortcut:
                places.append(desktop_folder() / (LINUX_DESKTOP_ID + '.desktop'))
            for place in places:
                try:
                    if place.exists() and LINUX_MARKER not in place.read_text(encoding='utf-8', errors='replace'):
                        self.event('Left ' + str(place) + ' alone: it was not made by this setup')
                        continue
                    place.parent.mkdir(parents=True, exist_ok=True)
                    place.write_text(entry, encoding='utf-8')
                    place.chmod(0o755)        # a desktop file on the Desktop must be executable to be trusted
                    shortcuts.append(str(place))
                except OSError as ex:
                    self.event('Could not create ' + str(place) + ' (' + str(ex) + ')')
        if getattr(self.args, 'steam_shortcut', False):
            self.add_to_steam(launcher, receipt)
        receipt['shortcuts'] = shortcuts
        return launcher

    def add_to_steam(self, launcher, receipt):
        """Steam's own Add a Non-Steam Game, through the same steam:// request SteamOS's Dolphin uses."""
        helper = shutil.which('steamos-add-to-steam')
        steam = shutil.which('steam')
        if not (helper or steam) or subprocess.run(['pgrep', '-x', 'steam'], stdout=subprocess.DEVNULL).returncode:
            self.event('Steam is not running; add ' + str(launcher) + ' to Steam with "Add a Non-Steam Game"')
            return
        try:
            if find_steam_shortcut(launcher)[0] is not None:
                # A repair or update: Steam would add a second entry for the same launcher.
                self.event('The game is already in your Steam library')
            elif helper:
                self.run([helper, launcher])
                self.event('Asked Steam to add the game to your library')
            else:
                Path('/tmp/addnonsteamgamefile').touch()
                self.run([steam, 'steam://addnonsteamgame/' + urllib.parse.quote(str(launcher), safe='')])
                self.event('Asked Steam to add the game to your library')
            previous = self.install / 'installed.json'
            old = json.loads(previous.read_text(encoding='utf-8')) if previous.is_file() else {}
            owned = [name for name in old.get('steam_art', []) if Path(name).is_file() and not Path(name).is_symlink() and digest(name) == old.get('steam_art_hashes', {}).get(name)]
            receipt['steam_art'] = place_steam_art(self.source, launcher, owned, self.event)
            receipt['steam_art_hashes'] = {name: digest(name) for name in receipt['steam_art']}
        except SetupError as ex:
            if ex.code == 6:
                raise
            self.event('Steam did not take the shortcut; add ' + str(launcher) + ' with "Add a Non-Steam Game"')

    def activate(self):
        self.event('Activating the verified installation')
        previous = self.install / 'installed.json'
        old = json.loads(previous.read_text()) if previous.exists() else {}
        receipt = {key: self.manifest[key] for key in ('version', 'source_commit', 'toolkit_commit')}
        receipt['hd_packs'] = getattr(self, 'hd_packs', old.get('hd_packs', []))
        receipt['shortcuts'] = old.get('shortcuts', [])
        receipt['shortcut_hashes'] = old.get('shortcut_hashes', {})
        receipt['steam_art_hashes'] = old.get('steam_art_hashes', {})
        if 'steam_art' in old:
            receipt['steam_art'] = old['steam_art']
        receipt.update(schema=1, updater=releases.CONTRACT, source=str(self.source), data=str(self.data),
                       executable=str(self.source / 'build' / PRESET / ('defjam_recomp.exe' if IS_WINDOWS else 'defjam_recomp')))
        paths = [previous]
        if IS_WINDOWS:
            paths += [self.install / 'installed.ini', self.install / 'DefJamLauncher.exe']
        elif IS_LINUX:
            paths += [self.install / LINUX_LAUNCHER, self.install / (LINUX_LAUNCHER + '.png')]
        else:
            paths += [self.install / LAUNCHER_BUNDLE]
        if hasattr(self, 'hd_packs'):
            paths.append(self.data / 'settings.ini')
        journal = transaction.Activation(self.install, self.data)
        no_shortcuts = self.args.no_shortcuts
        try:
            journal.begin(paths)
            self.args.no_shortcuts = True
            if IS_WINDOWS:
                shutil.copyfile(self.payload / 'DefJamLauncher.exe', self.install / 'DefJamLauncher.exe.new')
                os.replace(self.install / 'DefJamLauncher.exe.new', self.install / 'DefJamLauncher.exe')
                launcher = self.install / 'DefJamLauncher.exe'
            elif IS_LINUX:
                launcher = self.install_launcher_linux(receipt)
            else:
                launcher = self.install_launcher_bundle(receipt)
            transaction.checkpoint('launcher')
            self.cancelled()
            if hasattr(self, 'hd_packs'):
                enable_hd_packs(self.data, self.hd_packs, old.get('hd_packs', []))
            transaction.checkpoint('settings')
            write_json(previous, receipt)
            transaction.checkpoint('receipt-json')
            if IS_WINDOWS:
                pending = self.install / 'installed.ini.pending'
                pending.write_text('[install]\n' + ''.join(f'{key}={receipt[key]}\n' for key in ('source', 'data', 'executable')), encoding='utf-16')
                os.replace(pending, self.install / 'installed.ini')
            transaction.checkpoint('receipt-ini')
            self.cancelled()
            for path in paths:
                if path.is_file():
                    transaction.flush_file(path)
                transaction.sync_directory(path.parent)
            journal.commit()
        except BaseException:
            journal.recover()
            raise
        finally:
            self.args.no_shortcuts = no_shortcuts
        # Core commit is final. Optional shortcuts cannot invalidate a verified game.
        if not no_shortcuts:
            try:
                if IS_WINDOWS:
                    self.env['DEFJAM_SETUP_LAUNCHER'] = str(launcher)
                    self.env['DEFJAM_SETUP_ROOT'] = str(self.install)
                    self.env['DEFJAM_SETUP_DESKTOP_SHORTCUT'] = '0' if self.args.no_desktop_shortcut else '1'
                    self.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', self.payload / 'engine/shortcuts.ps1'])
                elif IS_LINUX:
                    self.install_launcher_linux(receipt, core=False)
                else:
                    self.install_launcher_bundle(receipt, core=False)
                write_json(previous, receipt)
            except (OSError, ValueError, SetupError) as ex:
                self.event('Installation is ready; optional shortcuts need repair: ' + str(ex))
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
                transaction.Activation(self.install, self.data).recover()
                self.check_install_destination()
                self.check_existing_install()
                previous = self.install / 'installed.json'
                self.operation = releases.policy(self.operation, self.manifest, json.loads(previous.read_text()) if previous.exists() else None)
                # At least 12 GiB build headroom plus extracted files; measured later on clean VM.
                required = 12 * 1024**3
                if shutil.disk_usage(nearest_existing(self.install)).free < required:
                    raise SetupError('At least 12 GiB free space is required on the install drive', 2)
                if self.dump is not None and not self.dump.exists():
                    raise SetupError('Dump does not exist', 2)
                dump_size = 0 if self.dump is None or (self.data / 'extraction-state.json').is_file() else sum(p.stat().st_size for p in self.dump.rglob('*') if p.is_file()) if self.dump.is_dir() else self.dump.stat().st_size
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


TEXTURE_PACK_LIST_MAX = 1023


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
    # The game holds TEXTURE_PACK_LIST_MAX bytes (RECOMP_SETTING_TEXT_MAX less its terminator).
    if len(combined.encode('utf-8')) > TEXTURE_PACK_LIST_MAX:
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


def running_executables():
    """Linux: the executables of every process this user can see."""
    found = set()
    for entry in Path('/proc').iterdir():
        if entry.name.isdigit():
            with contextlib.suppress(OSError):
                found.add(os.readlink(entry / 'exe'))
    return found


def desktop_folder():
    """The XDG desktop folder (xdg-user-dirs), or ~/Desktop."""
    with contextlib.suppress(OSError, subprocess.SubprocessError):
        answer = subprocess.run(['xdg-user-dir', 'DESKTOP'], capture_output=True, text=True, timeout=5)
        if answer.returncode == 0 and answer.stdout.strip() and Path(answer.stdout.strip()) != Path.home():
            return Path(answer.stdout.strip())
    return Path.home() / 'Desktop'


def desktop_entry(launcher, install, icon=None):
    def quoted(path):
        # Desktop Entry Exec quoting: a double-quoted argument with " ` $ \ escaped.
        return '"' + re.sub(r'(["`$\\])', r'\\\1', str(path)) + '"'
    return ('[Desktop Entry]\nType=Application\nName=Def Jam Recompiled\n'
            'Comment=Def Jam: Fight for NY, built on this computer from your own copy\n'
            'Exec=' + quoted(launcher) + '\nIcon=' + (str(icon) if icon else 'applications-games')
            + '\nTerminal=false\nCategories=Game;\nStartupWMClass=' + LINUX_DESKTOP_ID + '\n'
            + LINUX_MARKER + '=' + str(install) + '\n')


def parse_binary_vdf(data):
    """Steam's binary VDF (shortcuts.vdf): nested maps of strings and integers."""
    def node(pos):
        out = {}
        while pos < len(data):
            kind = data[pos]
            pos += 1
            if kind == 8:
                return out, pos
            end = data.index(b'\0', pos)
            key = data[pos:end].decode('utf-8', 'replace')
            pos = end + 1
            if kind == 0:
                out[key], pos = node(pos)
            elif kind == 1:
                end = data.index(b'\0', pos)
                out[key] = data[pos:end].decode('utf-8', 'replace')
                pos = end + 1
            elif kind == 2:
                out[key] = struct.unpack_from('<I', data, pos)[0]
                pos += 4
            elif kind == 7:
                out[key] = struct.unpack_from('<Q', data, pos)[0]
                pos += 8
            else:
                raise ValueError('unsupported VDF value type %d' % kind)
        raise ValueError('unterminated VDF map')
    parsed, _ = node(0)
    return parsed


def steam_userdata_folders():
    """Every Steam user's folder on this account: the native install, the symlink and Flatpak Steam."""
    data_home = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
    found = []
    for root in (data_home / 'Steam', Path.home() / '.steam/steam',
                 Path.home() / '.var/app/com.valvesoftware.Steam/.local/share/Steam'):
        with contextlib.suppress(OSError):
            for entry in sorted((root / 'userdata').iterdir()):
                if entry.name.isdigit() and entry.name != '0' and entry.resolve() not in [f.resolve() for f in found]:
                    found.append(entry)
    return found


def find_steam_shortcut(launcher, folders=None):
    """(appid, user folder) of the non-Steam shortcut that runs `launcher`, or (None, None).
    The appid is read back from Steam's own file: sources disagree on how it is computed."""
    best = (None, None, -1)
    for folder in steam_userdata_folders() if folders is None else folders:
        vdf = Path(folder) / 'config/shortcuts.vdf'
        try:
            entries = parse_binary_vdf(vdf.read_bytes()).get('shortcuts', {})
            stamp = vdf.stat().st_mtime
        except (OSError, ValueError, struct.error):
            continue
        for entry in entries.values():
            exe = next((v for k, v in entry.items() if k.lower() == 'exe'), '')
            appid = next((v for k, v in entry.items() if k.lower() == 'appid'), None)
            if isinstance(exe, str) and exe.strip('"') == str(launcher) and isinstance(appid, int) and stamp > best[2]:
                best = (appid & 0xFFFFFFFF, Path(folder), stamp)
    return best[0], best[1]


def place_steam_art(source, launcher, owned=(), event=print, timeout=20.0, folders=None):
    """Copy the portrait and wide capsules, the hero and the logo into the shortcut's grid folder,
    named by its appid. Waits for Steam to write the shortcut; never overwrites art the player chose.
    Returns the files placed (or already ours)."""
    art_folders = [Path(source) / 'branding/fan/steam', Path(source) / 'branding/steam']
    deadline = time.monotonic() + timeout
    while True:
        appid, folder = find_steam_shortcut(launcher, folders)
        if appid is not None:
            break
        if time.monotonic() >= deadline:
            event('Steam has not written the shortcut yet, so its artwork was not placed; '
                  'run setup again with --steam-art once it shows in your library')
            return []
        time.sleep(1)
    grid = folder / 'config/grid'
    grid.mkdir(parents=True, exist_ok=True)
    placed = []
    for suffix, name in STEAM_ART.items():
        origin = next((f / name for f in art_folders if (f / name).is_file()), None)
        target = grid / (str(appid) + suffix + '.png')
        if origin is None:
            continue
        if target.exists() and str(target) not in owned and digest(target) != digest(origin):
            event('Left ' + str(target) + ' alone: it is your own artwork')
            continue
        pending = target.with_name(target.name + '.new')
        shutil.copyfile(origin, pending)
        os.replace(pending, target)
        placed.append(str(target))
    event('Steam artwork placed in ' + str(grid) + '; restart Steam, or switch to Gaming Mode, to see it')
    return placed


def apply_steam_art(install):
    """--steam-art: the artwork for a shortcut that was added after setup finished."""
    root = Path(install).resolve()
    receipt_file = root / 'installed.json'
    if not IS_LINUX or not receipt_file.is_file():
        raise SetupError('Steam artwork needs an installation made by the Linux setup', 2)
    receipt = json.loads(receipt_file.read_text(encoding='utf-8'))
    placed = place_steam_art(receipt['source'], root / LINUX_LAUNCHER, receipt.get('steam_art', []), timeout=3.0)
    if not placed:
        raise SetupError('No Steam shortcut for ' + str(root / LINUX_LAUNCHER) + ' was found; add it to Steam first', 2)
    receipt['steam_art'] = placed
    write_json(receipt_file, receipt)
    return 0


def uninstall(install):
    root = Path(install).resolve()
    receipt_file = root / 'installed.json'
    if not receipt_file.is_file() or root == Path(root.anchor):
        raise SetupError('No recognized installation at this location', 2)
    receipt = json.loads(receipt_file.read_text(encoding='utf-8'))
    versions = root / 'versions'
    if receipt.get('schema') != 1 or versions.resolve() != versions or versions not in Path(receipt['source']).parents:
        raise SetupError('Invalid installation ownership receipt', 2)
    with install_lock(root), install_lock(Path(receipt['data'])), game_lock(root):
        transaction.Activation(root, receipt['data']).recover()
        receipt = json.loads(receipt_file.read_text(encoding='utf-8'))
        # Python 3.13 rmtree does not traverse Windows junction contents. Data is
        # outside versions by contract; verify the final target before deletion.
        if versions.exists():
            shutil.rmtree(versions)
        for name in ('installed.ini', 'installed.json', 'DefJamLauncher.exe'):
            (root / name).unlink(missing_ok=True)
        if IS_LINUX:
            (root / LINUX_LAUNCHER).unlink(missing_ok=True)
            (root / (LINUX_LAUNCHER + '.png')).unlink(missing_ok=True)
            for name in receipt.get('steam_art', []):
                art = Path(name)
                if art.parent.name == 'grid' and art.is_file() and not art.is_symlink() and digest(art) == receipt.get('steam_art_hashes', {}).get(name):
                    art.unlink()   # the artwork this setup placed for its Steam shortcut
            # Only entries this installation made and recorded, and only if they still name it.
            for name in receipt.get('shortcuts', []):
                shortcut = Path(name)
                if shortcut.is_file() and not shortcut.is_symlink() and \
                        (LINUX_MARKER + '=' + str(root)) in shortcut.read_text(encoding='utf-8', errors='replace'):
                    shortcut.unlink()
            print('Application removed. Dump, saves, settings and logs were preserved. A Steam shortcut, '
                  'if you added one, stays in Steam until you remove it there.', flush=True)
            return
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
                elif shortcut.is_file() and not shortcut.is_symlink() and digest(shortcut) == receipt.get('shortcut_hashes', {}).get(name):
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


def discard_failed_attempt(engine):
    """Delete the unique version folder this run created, so a failed or cancelled attempt does not
    keep gigabytes. Never the folder the installed receipt names, nor anything while an activation is pending."""
    try:
        if not engine or '-attempt-' not in engine.version_root.name or (engine.install / '.setup-transaction').exists():
            return
        receipt = engine.install / 'installed.json'
        if receipt.is_file() and Path(json.loads(receipt.read_text(encoding='utf-8'))['source']).resolve().is_relative_to(engine.version_root.resolve()):
            return
        if engine.version_root.is_dir() and engine.version_root.parent == engine.install / 'versions':
            shutil.rmtree(engine.version_root, ignore_errors=True)
    except (OSError, ValueError, KeyError):
        pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload', required=True)
    parser.add_argument('--silent', action='store_true')
    parser.add_argument('--dump')
    parser.add_argument('--install-dir')
    parser.add_argument('--operation', choices=['auto', 'install', 'repair', 'update', 'downgrade', 'rebuild'], default='auto')
    parser.add_argument('--release', help='Exact published GitHub release tag, or latest')
    parser.add_argument('--bundled-only', action='store_true', help='Use this setup payload without fetching another release')
    parser.add_argument('--list-releases', action='store_true')
    parser.add_argument('--releases-file', help='Write the release catalog JSON for a native wizard')
    parser.add_argument('--download-cache')
    parser.add_argument('--handoff-parent', type=int, help=argparse.SUPPRESS)
    parser.add_argument('--data-dir')
    parser.add_argument('--uninstall', action='store_true')
    parser.add_argument('--no-shortcuts', action='store_true', help='Do not create desktop/Start-menu shortcuts')
    parser.add_argument('--no-desktop-shortcut', action='store_true', help='Create only the Start-menu shortcut')
    parser.add_argument('--install-prerequisites', action='store_true')
    parser.add_argument('--hd-textures', action='store_true', help='Generate and enable Lanczos 4x HD textures locally (extra time/space)')
    parser.add_argument('--toolchain', choices=['auto', 'host', 'container'], default='auto',
                        help='Linux: build with this computer\'s compiler, in the pinned build container, '
                             'or whichever works (the default)')
    parser.add_argument('--steam-shortcut', action='store_true', help='Linux: add the game to Steam')
    parser.add_argument('--steam-art', action='store_true',
                        help="Linux: place the Steam artwork for the installed game's existing shortcut")
    parser.add_argument('--log')
    parser.add_argument('--status-file')
    parser.add_argument('--cancel-file')
    args = parser.parse_args(argv)
    engine = None
    selection = None
    try:
        bootstrap = verify_payload(Path(args.payload))
        cache = Path(args.download_cache) if args.download_cache else releases.default_cache()
        def cancelled():
            if args.cancel_file and Path(args.cancel_file).exists():
                raise SetupError('Setup cancelled', 6)
        last_progress = [None]
        def progress(message):
            if message == last_progress[0]:
                return      # the download reports every 64 KiB; the label changes once a MiB
            last_progress[0] = message
            if args.status_file:
                write_status(args.status_file, message)
            if args.log:
                with Path(args.log).open('a', encoding='utf-8') as stream:
                    stream.write(message + '\n')
        if args.list_releases:
            rows = releases.catalog(HOST_PLATFORM, cache, cancelled, allow_cached=True)
            if args.install_dir and (Path(args.install_dir) / 'installed.json').is_file():
                # An existing installation can only move to a release whose setup carries the updater contract.
                for row in rows:
                    if row['available']:
                        row['updater'] = releases.updater_status(row, cache, cancelled)
            result = {'bundled': bootstrap['version'], 'releases': rows}
            if args.releases_file:
                write_json(Path(args.releases_file), result)
                updater = {True: '1', False: '0'}
                Path(args.releases_file + '.tags').write_text(''.join(
                    row['tag'] + '|' + ('1' if row['available'] else '0') + '|' + updater.get(row.get('updater'), '?') + '\n'
                    for row in rows), encoding='utf-8')
            else:
                print(json.dumps(result))
            return 0
        if not args.install_dir:
            raise SetupError('--install-dir is required', 2)
        if args.release and args.bundled_only:
            raise SetupError('--release and --bundled-only cannot be combined', 2)
        root = Path(args.install_dir).resolve()
        if root == Path(root.anchor) or len(str(root)) > 140:
            raise SetupError('Choose a shorter writable install folder', 2)
        if args.data_dir:
            validate_locations(root, args.data_dir, args.dump)
        if not args.handoff_parent:
            selection = install_lock(releases.default_cache().parent / 'locks' / hashlib.sha256(str(root).encode('utf-8')).hexdigest())
            try:
                selection.__enter__()
            except BaseException:
                selection = None
                raise
        recovery = root / transaction.NAME
        if recovery.exists():
            if recovery.is_symlink():
                raise SetupError('Unsafe recovery folder', 2)
            journal_file = recovery / 'journal.json'
            if not journal_file.is_file():
                if not args.data_dir:
                    raise SetupError('Choose the data folder to recover this interrupted setup', 2)
                data_root = Path(args.data_dir).resolve()
            else:
                journal = json.loads(journal_file.read_text())
                data_root = Path(journal['data']).resolve()
                if args.data_dir and Path(args.data_dir).resolve() != data_root:
                    raise SetupError('Recovery must use the previous data folder', 2)
            validate_locations(root, data_root, None)
            with install_lock(root), install_lock(data_root), game_lock(root):
                transaction.Activation(root, data_root).recover()
            args.data_dir = str(data_root)
        receipt_path = root / 'installed.json'
        previous = json.loads(receipt_path.read_text()) if receipt_path.is_file() else None
        if previous and not args.data_dir:
            args.data_dir = previous['data']
        if args.release:
            if args.uninstall or args.steam_art:
                raise SetupError('Release selection cannot be combined with removal or Steam-art repair', 2)
            if args.release == 'latest':
                args.release = releases.latest_tag(cancelled)
            scratch, selected = releases.select_payload(args.release, HOST_PLATFORM, cache, verify_payload, previous, args.operation, cancelled, progress)
            try:
                return releases.run_selected(args, HOST_PLATFORM, selected, cancelled)
            finally:
                shutil.rmtree(scratch)
        if args.uninstall:
            verify_payload(Path(args.payload))
            uninstall(args.install_dir)
            return 0
        if args.steam_art:
            return apply_steam_art(args.install_dir)
        if not args.data_dir or (not args.dump and previous is None):
            raise SetupError('--dump and --data-dir are required for the first installation', 2)
        engine = Engine(args)
        engine.execute()
        return 0
    except (SetupError, OSError, ValueError, KeyError, subprocess.SubprocessError) as ex:
        message = 'Setup stopped: ' + str(ex)
        print(message, file=sys.stderr, flush=True)
        error_log = Path(engine.log.name) if engine and engine.log else Path(args.log) if args.log else None
        if error_log:
            error_log.parent.mkdir(parents=True, exist_ok=True)
            with error_log.open('a', encoding='utf-8') as log:
                log.write(message + '\n')
        if args.status_file:
            write_status(args.status_file, message)
        discard_failed_attempt(engine)
        return ex.code if isinstance(ex, SetupError) else 4
    finally:
        if selection:
            selection.__exit__(None, None, None)


if __name__ == '__main__':
    sys.exit(main())
