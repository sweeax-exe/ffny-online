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


def replace_shown(temporary, path):
    """os.replace for the files the wizard reads while setup runs (status, progress). On Windows the
    rename fails with "Access is denied" while the wizard has the old file open, and a player's setup
    stopped there (2026-10-25). It is retried for about a second, and then given up: these files only
    feed the wizard's display, so a missed update must not stop the install."""
    for attempt in range(20):
        try:
            os.replace(temporary, path)
            return
        except PermissionError:
            time.sleep(0.05)
    with contextlib.suppress(OSError):
        os.remove(temporary)


def write_status(path, message):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.pending')
    temporary.write_text(message, encoding='utf-8')
    replace_shown(temporary, path)


def write_progress(status_path, permille):
    """Overall progress, 0-1000, beside the status file (<status>.progress). The Windows
    wizard draws it as a bar; a front end that does not read it loses nothing."""
    path = Path(str(status_path) + '.progress')
    temporary = Path(str(status_path) + '.progress-pending')
    temporary.write_text(str(max(0, min(1000, int(permille)))), encoding='ascii')
    replace_shown(temporary, path)


def write_stage(status_path, index, total, key, name):
    """The step being worked on, beside the status file (<status>.stage), one line:
    "<index> <total> <key> <name>", e.g. "5 7 lift Translating the game code". The launcher's
    updater shows it as "Step 5 of 7"; a front end that does not read it loses nothing, and the
    status and progress files keep their formats."""
    path = Path(str(status_path) + '.stage')
    temporary = Path(str(status_path) + '.stage-pending')
    temporary.write_text(f'{int(index)} {int(total)} {key} {name}', encoding='utf-8')
    replace_shown(temporary, path)


# Share of the whole run each stage is given in the progress bar (permille, cumulative
# starts). The build is most of an install; with HD textures their upscale takes a slice of it.
PROGRESS_STAGES = {
    'source': (0, 20), 'extract': (20, 90), 'toolchain': (90, 130), 'analysis': (130, 170),
    'lift': (170, 260), 'build': (260, 950), 'hd': (950, 950), 'activate': (950, 1000),
}
PROGRESS_STAGES_HD = dict(PROGRESS_STAGES, build=(260, 800), hd=(800, 960), activate=(960, 1000))
# What each stage is called on screen (the launcher's update card), in run order. 'hd' only
# counts as a step when HD textures are built.
STAGE_NAMES = {
    'source': 'Getting the new version', 'extract': 'Checking your game files',
    'toolchain': 'Checking the build tools', 'analysis': 'Preparing the game code',
    'lift': 'Translating the game code', 'build': 'Building the game (the longest step)',
    'hd': 'HD textures', 'activate': 'Finishing up',
}


def stage_steps(hd):
    """The stages that count as steps, in order."""
    return [key for key in STAGE_NAMES if hd or key != 'hd']


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


def volume_filesystem(path):
    """The file system of the drive holding path ('NTFS', 'exFAT', ...), None when unknown or not Windows."""
    if not IS_WINDOWS:
        return None
    name = ctypes.create_unicode_buffer(64)
    root = Path(path).resolve().anchor
    ok = ctypes.windll.kernel32.GetVolumeInformationW(ctypes.c_wchar_p(root), None, 0, None, None, None, name, 64)
    return name.value if ok and name.value else None


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


def locate_dump(dump):
    """A chosen folder without default.xbe at its top is often the folder around the dump: the one the
    disc image was saved in, or one level above the extracted files (players picked those). Use the
    one image or the one game folder it holds; otherwise say what to choose."""
    if not dump.is_dir() or (dump / 'default.xbe').is_file():
        return dump
    try:
        images = [p for p in dump.iterdir() if p.is_file() and p.suffix.lower() in ('.iso', '.xiso')]
        folders = [p for p in dump.iterdir() if p.is_dir() and (p / 'default.xbe').is_file()]
        folders += [q for p in dump.iterdir() if p.is_dir() for q in p.iterdir()
                    if q.is_dir() and (q / 'default.xbe').is_file()]
    except OSError:
        return dump
    found = images + folders
    if len(found) == 1:
        return found[0].resolve()
    raise SetupError('The chosen dump folder has no default.xbe' + (
        ' and holds more than one possible dump' if found else '') + '. Choose the Xbox disc image (.iso) '
        'itself, or the folder that contains default.xbe.', 2)


def validate_locations(install, data, dump):
    """dump is None for an update that reuses the installed game files (--reuse-extracted)."""
    install, data = Path(install).resolve(), Path(data).resolve()
    dump = locate_dump(Path(dump).resolve()) if dump is not None else None
    named = (('install', install), ('data', data)) + ((('dump', dump),) if dump is not None else ())
    for i, (first_name, first) in enumerate(named):
        for second_name, second in named[i + 1:]:
            if first == second or first in second.parents or second in first.parents:
                raise SetupError('Install, data and dump locations must be separate, non-nested folders: the '
                                 + first_name + ' location (' + str(first) + ') and the ' + second_name
                                 + ' location (' + str(second) + ') overlap. Keep the dump outside the install'
                                 ' and data folders, and the two of them apart.', 2)
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
        self.install, self.data, self.dump = validate_locations(
            args.install_dir, args.data_dir, None if getattr(args, 'reuse_extracted', False) else args.dump)
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
        self.container_image = None     # Linux: the build container, when the host has no toolchain
        self.log = None

    def event(self, message):
        print(message, flush=True)
        if self.log:
            self.log.write(message + '\n')
            self.log.flush()
        if self.args.status_file:
            write_status(self.args.status_file, message)

    def stage(self, name):
        """Enter a progress stage: the bar jumps to its start, and steps reported while it
        runs (run(progress=True)) fill it towards its end."""
        hd = getattr(self.args, 'hd_textures', False)
        stages = PROGRESS_STAGES_HD if hd else PROGRESS_STAGES
        self.progress_span = stages[name]
        steps = stage_steps(hd)
        if self.args.status_file and name in steps:
            with contextlib.suppress(OSError):
                write_stage(self.args.status_file, steps.index(name) + 1, len(steps), name, STAGE_NAMES[name])
        self.progress(0, 1)

    def progress(self, done, total):
        lo, hi = getattr(self, 'progress_span', (0, 0))
        if self.args.status_file and total > 0:
            with contextlib.suppress(OSError):
                write_progress(self.args.status_file, lo + (hi - lo) * min(done, total) / total)

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
                                    self.progress(int(match[1]), int(match[2]))
                                    continue
                                # Ninja's "[12/352] Building C object ...": the build's own count.
                                match = re.match(r'\s*\[(\d+)/(\d+)\]', line)
                                if match:
                                    self.progress(int(match[1]), int(match[2]))
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
            self.keep_unchanged_times(old.parent)

    def keep_unchanged_times(self, old_root):
        """A file the new version ships byte for byte as the old one had it takes the old
        file's modification time. Windows builds share one build folder across versions
        (pipeline): Ninja then recompiles only what really changed -- usually a few
        files, where every update used to rebuild all 92 MB of generated code."""
        kept = 0
        for name, sha in self.manifest['files'].items():
            if not name.startswith(RUNTIME_FOLDERS):
                continue
            new, old = self.version_root / safe_relative(name), old_root / safe_relative(name)
            try:
                if old.is_file() and old.stat().st_size == new.stat().st_size and digest(old) == sha:
                    st = old.stat()
                    os.utime(new, ns=(st.st_atime_ns, st.st_mtime_ns))
                    kept += 1
            except OSError:
                pass
        self.event('Reusing the previous build where nothing changed (' + str(kept) + ' files unchanged)')

    def junction(self, link, target):
        """A directory junction (Windows): link -> target, replacing an old junction."""
        link, target = Path(link), Path(target)
        if os.path.lexists(link):
            if not (getattr(os.path, 'isjunction', lambda p: False)(link) or os.path.islink(link)):
                raise SetupError('Expected a link but found a folder: ' + str(link), 2)
            os.rmdir(link)              # removes the junction only, never its target's contents
        self.env['DEFJAM_SETUP_LINK'] = str(link)
        self.env['DEFJAM_SETUP_TARGET'] = str(target)
        self.run(['powershell.exe', '-NoProfile', '-Command',
                  "New-Item -ItemType Junction -Path $env:DEFJAM_SETUP_LINK -Target $env:DEFJAM_SETUP_TARGET -ErrorAction Stop | Out-Null"])

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
        if getattr(self.args, 'allow_running', False) and Path(receipt['executable']).parent.name == 'bin':
            # An update from the game's own launcher: the running copy is the old version's own
            # executable (place_executable), which this update neither writes nor removes.
            return
        self.env['DEFJAM_SETUP_INSTALLED_EXE'] = receipt['executable']
        result = self.run(['powershell.exe', '-NoProfile', '-Command',
            "if (Get-Process defjam_recomp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $env:DEFJAM_SETUP_INSTALLED_EXE }) { Write-Output 'GAME_RUNNING' }"], capture=True)
        if 'GAME_RUNNING' in result:
            raise SetupError('Close the installed game before updating or repairing it', 5)

    def extract(self):
        if self.dump is None:
            # An update from the launcher: the game files this installation already extracted.
            extracted = self.data / 'extracted'
            self.event('Checking the installed game files')
            if not extracted.is_dir():
                raise SetupError('This installation has no game files to reuse; run setup with your dump', 2)
            validate_dump(extracted, self.source)
            self.event('Installed game files reused')
            return
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
            found = shutil.which(executable, path=self.env['PATH'])
            if found and executable in ('cmake.exe', 'ninja.exe') and vs.resolve() not in Path(found).resolve().parents:
                # Only Visual Studio's own CMake and Ninja: another CMake on PATH (a player's had one from a
                # third-party toolchain without cmcldeps) built the resource file as "RC ..." and stopped
                # with RC1107. Without Visual Studio's, add that part as for a missing one.
                found = None
            if not found:
                if self.args.install_prerequisites and not provisioned:
                    # Build Tools are there but lack a part (CMake and Ninja, usually): add it to
                    # that installation; a fresh install would leave it as it is.
                    self.install_prerequisites(modify=vs)
                    return self.toolchain(provisioned=True)
                hint = (' (the "C++ CMake tools for Windows" part of Visual Studio). Tick "Install missing Microsoft Build '
                        'Tools" and select Install again') if executable in ('cmake.exe', 'ninja.exe') else ''
                raise SetupError('Missing toolchain component: ' + executable + hint, 3)
        self.run(['cmake', '--version'])
        self.run(['ninja', '--version'])

    def install_prerequisites(self, modify=None):
        if modify:
            self.event('Adding CMake and Ninja to your Visual Studio Build Tools; Windows may request administrator consent')
            self.env['DEFJAM_SETUP_VS_MODIFY'] = str(modify)
        else:
            self.event('Installing Microsoft Build Tools; Windows may request administrator consent')
            self.env.pop('DEFJAM_SETUP_VS_MODIFY', None)
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
        if os.path.lexists(game):
            # A link left by an earlier attempt whose target is gone: New-Item refused to replace it
            # ("Stage failed with exit 1" after a New-Item error on ...\source\game).
            with contextlib.suppress(OSError):
                os.rmdir(game) if IS_WINDOWS else game.unlink()
        if not IS_WINDOWS:
            game.symlink_to(self.data / 'extracted', target_is_directory=True)
            return
        filesystem = volume_filesystem(self.source)
        if filesystem and filesystem.upper() not in ('NTFS', 'REFS'):
            raise SetupError(f'{self.source.anchor} is formatted as {filesystem}, which cannot hold the folder link the '
                             'game needs. Choose an install folder on an NTFS drive (for example on C:).', 2)
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
            self.stage({'analyze': 'analysis', 'recomp': 'lift', 'build': 'build'}[action])
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
                if IS_WINDOWS:
                    # The shared build folder must be in place before anything writes into
                    # <source>\build (begin-build below creates it otherwise).
                    cache = self.install / 'build-cache'
                    cache.mkdir(exist_ok=True)
                    link = self.source / 'build'
                    is_link = getattr(os.path, 'isjunction', lambda p: False)(link) or os.path.islink(link)
                    if os.path.lexists(link) and not is_link:
                        # A plain folder from an earlier run of this version: set it aside.
                        os.replace(link, self.source / ('build.old-' + str(int(time.time()))))
                    if not os.path.lexists(link):
                        self.junction(link, cache)
                # Populate FetchContent from the inventoried, offline dependency source.
                self.run([self.python, self.source / 'scripts/pipeline-state.py', 'begin-build',
                          '--preset', PRESET], cwd=self.source)
                if IS_LINUX:
                    # The system's (or the build container's) SDL3, Vulkan and shaderc; Dear
                    # ImGui from the payload, since the container builds without a network.
                    overrides = [self.imgui_override()]
                elif IS_WINDOWS:
                    # One build folder for every version, reached through one stable path, so CMake
                    # and Ninja see the same tree from update to update and rebuild only what
                    # changed (keep_unchanged_times). <install>\work points at this version's
                    # source; its build folder points at <install>\build-cache.
                    work = self.install / 'work'
                    self.junction(work, self.source)
                    build_dir = work
                    deps = json.loads((self.payload / 'dependencies.json').read_text())
                    overrides = [f'-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={work / item["source_dir"]}'
                                 for name, item in zip(('sdl3', 'imgui'), [d for d in deps if 'source_dir' in d])]
                else:
                    # SDL3, MoltenVK and shaderc come prebuilt with the setup; nothing is
                    # looked for in /opt/homebrew, /usr/local or any other prefix.
                    prefix = self.version_root / 'deps'
                    overrides = [self.imgui_override(),
                                 f'-DCMAKE_PREFIX_PATH={prefix}', f'-DVulkan_LIBRARY={prefix}/lib/libMoltenVK.dylib',
                                 f'-DVulkan_INCLUDE_DIR={prefix}/include', '-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF',
                                 '-DCMAKE_OSX_ARCHITECTURES=arm64']
                build_cwd = build_dir if IS_WINDOWS else self.source
                self.run(self.in_container(['cmake', '--preset', PRESET, '-DDEFJAM_BUILD_GAME=ON', *overrides],
                                           self.source), cwd=build_cwd)
                self.run(self.in_container(['cmake', '--build', '--preset', PRESET, '--target', 'defjam_recomp'],
                                           self.source), cwd=build_cwd, progress=True)
                self.run([self.python, self.source / 'scripts/pipeline-state.py', 'record-build',
                          '--preset', PRESET], cwd=self.source)
                verify()
                if IS_WINDOWS:
                    self.place_executable()
                if IS_LINUX:
                    self.check_host_runtime()
                continue
            self.run(argv, cwd=self.source)
            verify()

    def place_executable(self):
        """Windows: this version's own copy of the game, <source>\\bin, which the receipt names.
        The shared build folder's executable is overwritten by the next update's build; a copy
        per version lets the old version keep running (an update from its launcher) and stay
        playable if the new build fails."""
        built = self.source / 'build' / PRESET
        bin_dir = self.source / 'bin'
        bin_dir.mkdir(parents=True, exist_ok=True)
        for name in ('defjam_recomp.exe', 'defjam_recomp.pdb'):
            if (built / name).is_file():
                pending = bin_dir / (name + '.new')
                shutil.copy2(built / name, pending)
                os.replace(pending, bin_dir / name)
        if not (bin_dir / 'defjam_recomp.exe').is_file():
            raise SetupError('The build produced no game executable', 4)

    def executable(self):
        """The receipt's executable: this version's own copy when the build placed one (Windows),
        otherwise the build folder's, as before."""
        name = 'defjam_recomp.exe' if IS_WINDOWS else 'defjam_recomp'
        if IS_WINDOWS and (self.source / 'bin' / name).is_file():
            return self.source / 'bin' / name
        return self.source / 'build' / PRESET / name

    def prepare_online(self):
        """The main menu's ONLINE entry and the online screens' art, made from the player's own
        files: scripts/apt-patch.py writes <data>/override/screens/screens.viv, and
        scripts/online-art.py <data>/online-art. A dump they do not recognise keeps the game
        without the in-game online screens, so a failure here is reported, not fatal."""
        for script, what in (('apt-patch.py', 'the ONLINE entry in the main menu'),
                             ('online-art.py', "the online screens' art")):
            try:
                self.run([self.python, self.source / 'scripts' / script, '--data', self.data])
            except SetupError as error:
                self.event('Could not prepare ' + what + ' (' + str(error) + '); online play stays in the launcher')

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

    def install_launcher_linux(self, receipt):
        """The launcher script, a menu entry, a desktop shortcut and, if asked, a Steam shortcut."""
        template = (self.payload / 'launcher/launcher.sh').read_text(encoding='utf-8')
        script = template
        for key in ('source', 'data', 'executable'):
            if any(c in receipt[key] for c in '\r\n'):
                raise SetupError('Install paths cannot contain line breaks', 2)
            script = script.replace('@' + key.upper() + '@', shlex.quote(receipt[key]))
        launcher = self.install / LINUX_LAUNCHER
        pending = launcher.with_name(launcher.name + '.new')
        pending.write_text(script, encoding='utf-8')
        pending.chmod(0o755)
        os.replace(pending, launcher)
        icon = self.install / (LINUX_LAUNCHER + '.png')
        icon_source = self.source / 'branding/icon/png/defjam-256.png'
        if icon_source.is_file():
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
            owned = json.loads(previous.read_text(encoding='utf-8')).get('steam_art', []) if previous.is_file() else []
            receipt['steam_art'] = place_steam_art(self.source, launcher, owned, self.event)
        except SetupError as ex:
            if ex.code == 6:
                raise
            self.event('Steam did not take the shortcut; add ' + str(launcher) + ' with "Add a Non-Steam Game"')

    def activate(self):
        self.event('Creating launch shortcuts and install receipt')
        receipt = {key: self.manifest[key] for key in ('version', 'source_commit', 'toolkit_commit')}
        previous = self.install / 'installed.json'
        receipt['hd_packs'] = getattr(self, 'hd_packs', json.loads(previous.read_text()).get('hd_packs', []) if previous.exists() else [])
        receipt.update(schema=1, source=str(self.source), data=str(self.data), executable=str(self.executable()))
        if IS_WINDOWS:
            # Stable native launcher reads this atomic receipt and supplies the data env.
            shutil.copyfile(self.payload / 'DefJamLauncher.exe', self.install / 'DefJamLauncher.exe.new')
            target = self.install / 'DefJamLauncher.exe'
            for stale in self.install.glob('DefJamLauncher.exe.old-*'):
                with contextlib.suppress(OSError):
                    stale.unlink()      # left by an earlier update while it ran
            try:
                os.replace(self.install / 'DefJamLauncher.exe.new', target)
            except PermissionError:
                # It runs (it waits for the game that started this update): Windows lets a running
                # executable be renamed, not replaced. Move it aside, then put the new one in place.
                os.replace(target, self.install / ('DefJamLauncher.exe.old-' + str(int(time.time()))))
                os.replace(self.install / 'DefJamLauncher.exe.new', target)
            launcher = self.install / 'DefJamLauncher.exe'
            self.env['DEFJAM_SETUP_LAUNCHER'] = str(launcher)
            self.env['DEFJAM_SETUP_ROOT'] = str(self.install)
            self.env['DEFJAM_SETUP_DESKTOP_SHORTCUT'] = '0' if self.args.no_desktop_shortcut else '1'
            if not self.args.no_shortcuts:
                self.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                          self.payload / 'engine/shortcuts.ps1'])
        elif IS_LINUX:
            launcher = self.install_launcher_linux(receipt)
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
        self.prune_versions(before[previous])
        self.event('Setup complete. Select Play or double-click: ' + str(launcher))

    def prune_versions(self, previous_receipt):
        """Every update builds a new versions/<version>-<source>-<toolkit> folder (about 0.5 GB) and
        none was ever removed: a player's install grew by one per build. Keep this one and the one
        installed before it (it may still run while this update finishes); remove older ones."""
        if getattr(self, 'version_root', None) is None:
            return
        keep = {self.version_root.resolve()}
        with contextlib.suppress(ValueError, KeyError, TypeError, OSError):
            keep.add(Path(json.loads(previous_receipt)['source']).resolve().parent)
        for folder in (self.install / 'versions').glob('*'):
            if (folder.is_dir() and not folder.is_symlink() and folder.resolve() not in keep
                    and re.fullmatch(r'\d+\.\d+\.\d+-[0-9a-f]{12}-[0-9a-f]{12}', folder.name)):
                shutil.rmtree(folder, ignore_errors=True)
                if not folder.exists():
                    self.event('Removed an old version: ' + folder.name)

    def execute(self):
        if getattr(self.args, 'hd_textures', False) and not HD_SUPPORTED:
            raise SetupError('HD textures are not supported on this platform yet', 2)
        self.data.mkdir(parents=True, exist_ok=True)
        # From the running game's launcher (--allow-running, Windows) the game holds play.lock; the
        # update builds a new version beside it and touches nothing the running one uses.
        playing = (contextlib.nullcontext() if IS_WINDOWS and getattr(self.args, 'allow_running', False)
                   else game_lock(self.install))
        with install_lock(self.install), install_lock(self.data), playing:
            log_path = Path(self.args.log) if self.args.log else self.install / 'logs' / time.strftime('setup-%Y%m%d-%H%M%S.log')
            log_path.parent.mkdir(parents=True, exist_ok=True)
            with log_path.open('a', encoding='utf-8') as log:
                self.log = log
                self.event('Setup ' + json.dumps({k: self.manifest[k] for k in ('version', 'source_commit', 'toolkit_commit')}))
                self.check_install_destination()
                self.check_existing_install()
                # Build headroom plus extracted files: players needed about 16 GB in practice (2026-10-28 reports;
                # with less the build stopped part-way), so ask for that up front.
                required = 16 * 1024**3
                if shutil.disk_usage(nearest_existing(self.install)).free < required:
                    raise SetupError('At least 16 GB free space is required on the install drive (the game is compiled on your PC); free some space and run setup again', 2)
                if self.dump is None:
                    dump_size = 0       # an update reuses the extracted files already on the data drive
                elif not self.dump.exists():
                    raise SetupError('Dump does not exist', 2)
                else:
                    dump_size = sum(p.stat().st_size for p in self.dump.rglob('*') if p.is_file()) if self.dump.is_dir() else self.dump.stat().st_size
                if IS_WINDOWS:
                    same_drive = self.install.anchor.casefold() == self.data.anchor.casefold()
                else:
                    same_drive = os.stat(nearest_existing(self.install)).st_dev == os.stat(nearest_existing(self.data)).st_dev
                hd_space = 15 * 1024**3 if getattr(self.args, 'hd_textures', False) else 0
                if shutil.disk_usage(nearest_existing(self.data)).free < dump_size + 1024**3 + hd_space + (required if same_drive else 0):
                    raise SetupError('Insufficient free space on the data drive', 2)
                self.stage('source')
                self.prepare_source()
                self.stage('extract')
                self.extract()
                self.stage('toolchain')
                self.toolchain()
                self.link_game()
                self.pipeline()
                self.prepare_online()
                self.stage('hd')
                self.build_hd_textures()
                self.stage('activate')
                self.activate()
                self.progress(1, 1)


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
    with install_lock(root), game_lock(root):
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
                if art.parent.name == 'grid' and art.is_file() and not art.is_symlink():
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
    parser.add_argument('--reuse-extracted', action='store_true',
                        help='Update: build from the game files the installation already extracted (no --dump)')
    parser.add_argument('--allow-running', action='store_true',
                        help='Update from the running game\'s launcher (Windows; the old version keeps its own copy)')
    parser.add_argument('--install-dir', required=True)
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
    try:
        if args.uninstall:
            verify_payload(Path(args.payload))
            uninstall(args.install_dir)
            return 0
        if args.steam_art:
            return apply_steam_art(args.install_dir)
        if (not args.dump and not args.reuse_extracted) or not args.data_dir:
            raise SetupError('--dump (or --reuse-extracted) and --data-dir are required for installation', 2)
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
