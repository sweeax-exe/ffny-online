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
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request


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


def verify_payload(payload):
    payload = Path(payload).resolve()
    manifest = json.loads((payload / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('schema') != 1 or manifest.get('platform') != 'windows-x64':
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
        self.env['PATH'] = str(self.payload / 'python') + os.pathsep + self.env.get('PATH', '')
        self.python = self.version_root / 'python/python.exe'
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

    def run(self, argv, cwd=None, env=None, capture=False):
        self.cancelled()
        argv = list(argv)
        if Path(argv[0]) == self.python:
            argv.insert(1, '-B')
        if self.log:
            self.log.write('Running: ' + subprocess.list2cmdline(list(map(str, argv))) + '\n')
            self.log.flush()
        # Redirect to disk, not a pipe: compiler bursts cannot deadlock the UI.
        with tempfile.TemporaryFile() as output:
            with subprocess.Popen(list(map(str, argv)), cwd=cwd, env=env or self.env,
                                  stdout=output, stderr=subprocess.STDOUT,
                                  creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0) as child:
                position = 0
                try:
                    while child.poll() is None:
                        self.cancelled()
                        time.sleep(.2)
                        output.seek(position)
                        chunk = output.read()
                        position += len(chunk)
                        if chunk and self.log:
                            self.log.write(chunk.decode('utf-8', errors='replace'))
                            self.log.flush()
                except SetupError:
                    if os.name == 'nt':
                        subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'],
                                       capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
                    else:
                        child.terminate()
                    child.wait()
                    raise
                output.seek(position)
                if self.log:
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
            if not name.startswith(('source/', 'python/')):
                continue
            destination = self.version_root / safe_relative(name)
            if not destination.is_file() or digest(destination) != sha:
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(self.payload / name, destination)
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
        elif any(p.name not in {'logs', 'setup.lock', 'play.lock'} for p in self.install.iterdir()):
            raise SetupError('Choose an empty install folder or a recognized Def Jam installation', 2)
        write_json(marker, identity)

    def check_existing_install(self):
        receipt_path = self.install / 'installed.json'
        if not receipt_path.exists():
            return
        receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
        if Path(receipt['data']).resolve() != self.data:
            raise SetupError('Updates must use the installed data folder: ' + receipt['data'], 2)
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

    def toolchain(self, provisioned=False):
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
        lines = self.run(['cmd.exe', '/d', '/c', '%DEFJAM_SETUP_ENV_SCRIPT%'], capture=True)
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
            ('Build', lambda: state.verify_build(self.source, 'win-x64-release'), 'build')):
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
                          '--preset', 'win-x64-release'], cwd=self.source)
                deps = json.loads((self.payload / 'dependencies.json').read_text())
                overrides = [f'-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={self.source / item["source_dir"]}'
                             for name, item in zip(('sdl3', 'imgui'), [d for d in deps if 'source_dir' in d])]
                self.run(['cmake', '--preset', 'win-x64-release', '-DDEFJAM_BUILD_GAME=ON', *overrides], cwd=self.source)
                self.run(['cmake', '--build', '--preset', 'win-x64-release', '--target', 'defjam_recomp'], cwd=self.source)
                self.run([self.python, self.source / 'scripts/pipeline-state.py', 'record-build',
                          '--preset', 'win-x64-release'], cwd=self.source)
                verify()
                continue
            self.run(argv, cwd=self.source)
            verify()

    def activate(self):
        self.event('Creating launch shortcuts and install receipt')
        receipt = {key: self.manifest[key] for key in ('version', 'source_commit', 'toolkit_commit')}
        receipt.update(schema=1, source=str(self.source), data=str(self.data),
                       executable=str(self.source / 'build/win-x64-release/defjam_recomp.exe'))
        # Stable native launcher reads this atomic receipt and supplies the data env.
        shutil.copyfile(self.payload / 'DefJamLauncher.exe', self.install / 'DefJamLauncher.exe.new')
        os.replace(self.install / 'DefJamLauncher.exe.new', self.install / 'DefJamLauncher.exe')
        self.env['DEFJAM_SETUP_LAUNCHER'] = str(self.install / 'DefJamLauncher.exe')
        self.env['DEFJAM_SETUP_ROOT'] = str(self.install)
        if not self.args.no_shortcuts:
            self.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                      self.payload / 'engine/shortcuts.ps1'])
        write_json(self.install / 'installed.json', receipt)
        temporary = self.install / 'installed.ini.pending'
        temporary.write_text('[install]\n' + ''.join(f'{key}={receipt[key]}\n' for key in
                             ('source', 'data', 'executable')), encoding='utf-16')
        os.replace(temporary, self.install / 'installed.ini')
        self.event('Setup complete. Your game is ready to launch.')

    def execute(self):
        if os.name != 'nt':
            raise SetupError('macOS and Linux setup configurations remain to-dos', 2)
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
                same_drive = self.install.anchor.casefold() == self.data.anchor.casefold()
                if shutil.disk_usage(nearest_existing(self.data)).free < dump_size + 1024**3 + (required if same_drive else 0):
                    raise SetupError('Insufficient free space on the data drive', 2)
                self.prepare_source()
                self.extract()
                self.toolchain()
                self.link_game()
                self.pipeline()
                self.activate()


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
    parser.add_argument('--install-prerequisites', action='store_true')
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
