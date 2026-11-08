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


# CMakePresets.json version 2 and cmake_minimum_required in CMakeLists.txt.
CMAKE_MINIMUM = (3, 20)


def cmake_version(text):
    """(major, minor) from `cmake --version` output ("cmake version 3.18.20081302-MSVC_2"), or None."""
    match = re.search(r'cmake version (\d+)\.(\d+)', text or '')
    return (int(match.group(1)), int(match.group(2))) if match else None


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


def replace_shown(temporary, path, attempts=STATUS_ATTEMPTS):
    """Publish one of the files the wizard reads while setup runs (status, progress), best effort.
    On Windows the rename fails with "Access is denied" while the wizard has the old file open, and
    a player's setup stopped there (2026-10-25; upstream issue #40). It is retried a few times with
    a growing pause and then given up: these files only feed the wizard's display, so a missed
    update must not stop the install. Returns whether the file was published."""
    for attempt in range(attempts):
        try:
            os.replace(temporary, path)
            return True
        except OSError:
            if attempt + 1 < attempts:
                time.sleep(0.01 * 2 ** attempt)
    with contextlib.suppress(OSError):
        os.remove(temporary)
    return False


def write_status(path, message):
    """One progress line for the wizard, best effort: never raises, returns whether it was shown."""
    path = Path(path)
    temporary = path.with_suffix('.pending')
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary.write_text(message, encoding='utf-8')
    except OSError:
        return False
    return replace_shown(temporary, path)


def write_progress(status_path, permille):
    """Overall progress, 0-1000, beside the status file (<status>.progress). The Windows
    wizard draws it as a bar; a front end that does not read it loses nothing."""
    path = Path(str(status_path) + '.progress')
    temporary = Path(str(status_path) + '.progress-pending')
    try:
        temporary.write_text(str(max(0, min(1000, int(permille)))), encoding='ascii')
    except OSError:
        return False
    return replace_shown(temporary, path)


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


# Build headroom plus extracted files: players needed about 16 GB in practice (2026-10-28 reports;
# with less the build stopped part-way), so it is asked for up front. HD textures need 15 GB more
# while they are made. The Windows wizard's folder check uses the same numbers (plan_folder).
BUILD_SPACE = 16 * 1024**3
HD_SPACE = 15 * 1024**3


def dump_bytes(dump):
    dump = Path(dump)
    return sum(p.stat().st_size for p in dump.rglob('*') if p.is_file()) if dump.is_dir() else dump.stat().st_size


def space_needed(dump_size, hd, same_drive):
    """(free space the install drive needs, free space the data drive needs)."""
    data = dump_size + 1024**3 + (HD_SPACE if hd else 0) + (BUILD_SPACE if same_drive else 0)
    return BUILD_SPACE, data


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
            try:
                child = subprocess.Popen(list(map(str, argv)), cwd=cwd, env=child_env,
                                         stdout=output, stderr=subprocess.STDOUT,
                                         **({'creationflags': subprocess.CREATE_NO_WINDOW} if IS_WINDOWS
                                            else {'start_new_session': True}))
            except OSError as ex:
                if getattr(ex, 'winerror', None) == 740:
                    # A player's PC asked for elevation to start vswhere.exe (a compatibility setting).
                    raise SetupError('Windows asks for administrator rights to start ' + Path(str(argv[0])).name +
                                     '. Right-click the setup, choose "Run as administrator" and try again.') from ex
                raise
            with child:
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
                    if self.log and not log_output:
                        # Output kept out of the log (it can hold the environment) is still the only
                        # place the reason is: a failed step ends before anything secret is printed.
                        output.seek(0)
                        tail = output.read().decode('utf-8', errors='replace').splitlines()[-30:]
                        self.log.write('Output of the failed step (last lines):\n' + '\n'.join(tail) + '\n')
                        self.log.flush()
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
        if previous.is_file():
            old = Path(json.loads(previous.read_text(encoding='utf-8'))['source']).resolve()
        else:
            # Reuse only saves time, so an earlier folder that cannot be read is skipped. On
            # Windows a half-deleted failed attempt answers access denied (WinError 5), and
            # Python 3.13's is_file() raises that instead of returning False (upstream D199).
            candidates = []
            for p in (self.install / 'versions').glob('*/source'):
                try:
                    if p != self.source and (p / 'src/recomp/gen/lift-state.json').is_file():
                        candidates.append((p.stat().st_mtime, p))
                except OSError as ex:
                    self.event('Skipped an unreadable earlier build folder: ' + str(ex))
            if not candidates:
                return
            old = max(candidates)[1].resolve()
        versions = (self.install / 'versions').resolve()
        if versions not in old.parents or old == self.source:
            return
        self.previous_source = old
        # Reuse only local analysis/lift artifacts; the incoming pipeline must
        # validate all inputs/outputs before skipping a stage. Never reuse objects.
        for relative in ('src/recomp/gen', 'tools/xboxrecomp/game_files',
                         'tools/xboxrecomp/tools/disasm/output', 'tools/xboxrecomp/tools/func_id/output',
                         'tools/xboxrecomp/tools/abi_analysis/output'):
            src, dst = old / relative, self.source / relative
            try:
                if src.is_dir() and (not dst.exists() or (relative == 'src/recomp/gen' and
                                                          not (dst / 'lift-state.json').exists())):
                    shutil.copytree(src, dst, dirs_exist_ok=True)
            except OSError as ex:
                # A partial copy is discarded so that stage starts from nothing.
                shutil.rmtree(dst, ignore_errors=True)
                self.event('Earlier ' + relative + ' not reused: ' + str(ex))
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
                raise SetupError('Visual Studio Build Tools are missing; turn on "Missing Windows parts" in setup (or use --install-prerequisites)', 3)
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
        # A stray double quote in PATH (a player's had 'C:\Program Files\dotnet"') turns the ')' of
        # '(x86)' into the end of a block in Microsoft's vcvars.bat: "\Microsoft was unexpected".
        self.env['PATH'] = self.env.get('PATH', '').replace('"', '')
        lines = self.run(['cmd.exe', '/d', '/c', '%DEFJAM_SETUP_ENV_SCRIPT%'], capture=True,
                         log_output=False)  # SET includes inherited environment secrets.
        for line in lines.splitlines():
            key, equal, value = line.partition('=')
            if equal and key and not key.startswith('='):
                self.env[key] = value
        includes = [d for d in self.env.get('INCLUDE', '').split(';') if d]
        if not any((Path(d) / 'stdio.h').is_file() for d in includes):
            # A player's Windows SDK lost its Universal CRT part: every C file failed on stdio.h.
            raise SetupError('The Windows SDK is incomplete: the C runtime headers (stdio.h) are missing. Open '
                             'Visual Studio Installer, choose Modify, tick "Windows Universal CRT SDK" and a '
                             '"Windows 11 SDK" under Individual components (or use Repair), then run setup again.', 3)
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
                hint = (' (the "C++ CMake tools for Windows" part of Visual Studio). Turn on "Missing Windows parts" in '
                        'setup and install again') if executable in ('cmake.exe', 'ninja.exe') else ''
                raise SetupError('Missing toolchain component: ' + executable + hint, 3)
        version = cmake_version(self.run(['cmake', '--version'], capture=True))
        if version and version < CMAKE_MINIMUM:
            # A player's Visual Studio 2019 brought CMake 3.18, which does not know --preset and took the
            # preset's name for the source folder ('The source directory ".../win-x64-release" does not
            # exist', "Stage failed with exit 1"). Newer Build Tools sort first in vswhere once installed.
            have = '.'.join(map(str, version))
            if self.args.install_prerequisites and not provisioned:
                self.event(f'Visual Studio CMake {have} is too old (needs {CMAKE_MINIMUM[0]}.{CMAKE_MINIMUM[1]}+)')
                self.install_prerequisites()
                return self.toolchain(provisioned=True)
            raise SetupError(f'Your Visual Studio ({vs.name}) brings CMake {have}; setup needs CMake '
                             f'{CMAKE_MINIMUM[0]}.{CMAKE_MINIMUM[1]} or newer. Turn on "Missing Windows parts" in '
                             'setup to add Build Tools for Visual Studio 2022, or install them yourself (with '
                             '"Desktop development with C++"), then run setup again.', 3)
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
            if action == 'recomp':
                self.keep_generated_times()

    def keep_generated_times(self):
        """After a new lift, a generated file that came out byte for byte as the previous
        version's takes that file's modification time, like keep_unchanged_times does for the
        shipped source: an override in recomp_manual.c changes one or two of the 21 files, and
        Ninja then recompiles those instead of all the generated code (build 37 to 38)."""
        old = getattr(self, 'previous_source', None)
        if old is None:
            return
        gen, old_gen = self.source / 'src/recomp/gen', old / 'src/recomp/gen'
        kept = 0
        for new in gen.iterdir():
            prev = old_gen / new.name
            try:
                if (new.suffix in ('.c', '.h') and prev.is_file() and prev.stat().st_size == new.stat().st_size
                        and digest(prev) == digest(new)):
                    st = prev.stat()
                    os.utime(new, ns=(st.st_atime_ns, st.st_mtime_ns))
                    kept += 1
            except OSError:
                pass
        self.event('Generated code: ' + str(kept) + ' files unchanged since the previous version')

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
                if self.dump is None:
                    dump_size = 0       # an update reuses the extracted files already on the data drive
                elif not self.dump.exists():
                    raise SetupError('Dump does not exist', 2)
                else:
                    dump_size = dump_bytes(self.dump)
                if IS_WINDOWS:
                    same_drive = self.install.anchor.casefold() == self.data.anchor.casefold()
                else:
                    same_drive = os.stat(nearest_existing(self.install)).st_dev == os.stat(nearest_existing(self.data)).st_dev
                required, data_required = space_needed(dump_size, getattr(self.args, 'hd_textures', False), same_drive)
                if shutil.disk_usage(nearest_existing(self.install)).free < required:
                    raise SetupError('At least 16 GB free space is required on the install drive (the game is compiled on your PC); free some space and run setup again', 2)
                if shutil.disk_usage(nearest_existing(self.data)).free < data_required:
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


# ---------------------------------------------------------------------------------------------
# The Windows wizard's questions: which game file is this (--identify), where are the player's
# copies (--find-games) and can the game go into this folder (--plan-folder). Each answer is one
# JSON file (--out); nothing is written anywhere else. What is told apart, and how sure it is:
#   - the USA Xbox game: default.xbe byte for byte as config/dump-manifest.json records it, every
#     required path present, at least the expected number of files, and (for an image) every file
#     inside the image's length. That is what the install itself checks (validate_dump), so the
#     wizard never accepts a copy the install refuses. Other files' contents are not hashed.
#   - another Xbox game, region or release: read from default.xbe's certificate (title ID, name,
#     region flags). A copy of this game whose default.xbe differs but whose regions include North
#     America is only called "not the USA version" (it may be another release, modified or damaged).
#   - PlayStation (ISO 9660 system identifier "PLAYSTATION", SYSTEM.CNF BOOT2 = PS2), GameCube and
#     Wii (their disc header magic numbers), Dolphin's compressed formats and ZIP/7z/RAR archives
#     (their signatures). Anything else is "not the USA Xbox version", without a guess.

XDVDFS_MAGIC = b'MICROSOFT*XBOX*MEDIA'
# Where a game partition starts in the image layouts players have (the toolkit's xdvdfs.py has the
# same list): bare XISO, rebuilt, redump with the video partition first, full disc layouts.
XDVDFS_BASES = (0x00000000, 0x0000FD90, 0x00030600, 0x0FD90000, 0x18300000)
DISC_SECTOR = 2048
XBE_REGION_NA, XBE_REGION_JAPAN, XBE_REGION_PAL = 0x1, 0x2, 0x4
MIN_IMAGE_BYTES = 100 * 1024**2

_SAME_GAME = "FFNY Online needs the USA Xbox version. Everyone online plays the same game data, so other versions can't connect."
_PICK_USA = 'Pick the USA Xbox version if you have it.'
GAME_TEXTS = {
    'ok': ('DEF JAM: FIGHT FOR NY', 'Xbox · USA · complete and undamaged', ''),
    'pal': ('THIS IS THE EUROPEAN (PAL) VERSION', _SAME_GAME, _PICK_USA),
    'japan': ('THIS IS THE JAPANESE VERSION', _SAME_GAME, _PICK_USA),
    'region': ("THIS ISN'T THE USA XBOX VERSION", _SAME_GAME, _PICK_USA),
    'changed': ("THIS ISN'T THE USA XBOX VERSION",
                "Its main game file (default.xbe) is not the one from the USA release FFNY Online is built from. "
                "It may be another release, a modified copy or a damaged one.", _PICK_USA),
    'other-game': ('THIS IS A DIFFERENT XBOX GAME', 'This disc image holds {title}, not Def Jam: Fight for NY.',
                   'Choose your copy of Def Jam: Fight for NY (Xbox, USA).'),
    'incomplete': ('THIS COPY IS INCOMPLETE OR DAMAGED', 'It is the right game, but {problem}.',
                   'Copy or dump the game again, then choose the new file.'),
    'cut': ('THIS DISC IMAGE IS INCOMPLETE', "The file is cut short, so setup can't read the game in it.",
            'Copy or download it again, then choose the new file.'),
    'ps2': ('THIS IS A PLAYSTATION 2 DISC IMAGE', 'FFNY Online is built from the Xbox version (USA) only.', _PICK_USA),
    'playstation': ('THIS IS A PLAYSTATION DISC IMAGE', 'FFNY Online is built from the Xbox version (USA) only.', _PICK_USA),
    'gamecube': ('THIS IS THE GAMECUBE VERSION', 'FFNY Online is built from the Xbox version (USA) only.', _PICK_USA),
    'gamecube-other': ('THIS IS A GAMECUBE GAME', 'This disc image holds {title}. FFNY Online is built from the '
                       'Xbox version (USA) of Def Jam: Fight for NY.', _PICK_USA),
    'wii': ('THIS IS A WII GAME', 'FFNY Online is built from the Xbox version (USA) only.', _PICK_USA),
    'dolphin': ('THIS IS A GAMECUBE OR WII IMAGE', 'It is in a compressed format of the Dolphin emulator. '
                'FFNY Online is built from the Xbox version (USA) only.', _PICK_USA),
    'archive': ('THIS FILE IS STILL PACKED', 'It is a {title} archive, not a disc image.',
                'Unpack it first (for example with 7-Zip), then choose the .iso inside.'),
    'no-game-folder': ('NO GAME IN THIS FOLDER', 'This folder has no Xbox game in it: no default.xbe and no .iso file.',
                       'Choose the .iso file itself, or the folder that holds default.xbe.'),
    'many-games': ('MORE THAN ONE GAME IN THIS FOLDER', 'This folder holds more than one disc image or game folder.',
                   'Choose the .iso file itself, or the folder that holds default.xbe.'),
    'missing': ("SETUP CAN'T OPEN THIS", 'It was moved, renamed or cannot be read.',
                'Choose the file again from where it is now.'),
    'unknown': ("THIS ISN'T THE USA XBOX VERSION",
                "Setup couldn't recognise it as the USA Xbox version of Def Jam: Fight for NY.",
                'Pick the USA Xbox version: an .iso file, or the folder with the extracted game.'),
}
# Kinds that are this game in another version or state: the search lists them so a player sees
# why the copy they have does not work.
SAME_TITLE_KINDS = {'ok', 'pal', 'japan', 'region', 'changed', 'incomplete', 'gamecube'}


def game_verdict(kind, chosen, path=None, title='', problem='', size=None, **extra):
    headline, message, advice = GAME_TEXTS[kind]
    shown_title = '“' + title + '”' if title else 'another game'
    return dict(ok=kind == 'ok', kind=kind, headline=headline,
                message=message.format(title=shown_title if kind != 'archive' else title, problem=problem),
                advice=advice, chosen=str(chosen), path=str(path or chosen), title=title, size=size, **extra)


def load_dump_manifest(source):
    return json.loads((Path(source) / 'config/dump-manifest.json').read_text(encoding='utf-8'))


def xbe_identity(xbe):
    """Title ID, title name and region flags from an XBE's certificate, or None if it is not one."""
    if len(xbe) < 0x178 or xbe[:4] != b'XBEH':
        return None
    base, certificate = struct.unpack_from('<I', xbe, 0x104)[0], struct.unpack_from('<I', xbe, 0x118)[0]
    offset = certificate - base
    if offset < 0x178 or offset + 0xA4 > len(xbe):
        return None
    title_id = struct.unpack_from('<I', xbe, offset + 8)[0]
    name = xbe[offset + 0x0C:offset + 0x5C].decode('utf-16-le', errors='replace').split('\0')[0]
    name = ''.join(c for c in name if c.isprintable() and c != '�').strip()[:40]
    return {'title_id': title_id, 'title': name, 'region': struct.unpack_from('<I', xbe, offset + 0xA0)[0]}


def classify_xbe(xbe, manifest):
    """'match' for the manifest's default.xbe, else the kind of difference."""
    expected = manifest['xbe']
    if len(xbe) == expected['size'] and hashlib.sha256(xbe).hexdigest() == expected['sha256']:
        return 'match', None
    identity = xbe_identity(xbe)
    if identity is None:
        return 'unknown', None
    same = identity['title_id'] == int(str(manifest.get('title_id', '0')), 16) or \
        'fight for ny' in identity['title'].casefold()
    if not same:
        return 'other-game', identity
    region = identity['region'] & 7
    if region & XBE_REGION_NA:
        return 'changed', identity
    return {XBE_REGION_PAL: 'pal', XBE_REGION_JAPAN: 'japan'}.get(region, 'region'), identity


def completeness_problem(names, count, manifest, truncated=False):
    """The install's completeness rule (scripts/verify-dump.py), as a phrase, or '' when complete."""
    if truncated:
        return 'the file is cut short: part of the image is missing'
    present = {n.casefold() for n in names}
    if any(p.casefold() not in present for p in manifest['required_paths']) or count < manifest['expected_file_count']:
        return 'some of its files are missing'
    return ''


class DiscImage:
    def __init__(self, path):
        self.path = Path(path)
        self.stream = self.path.open('rb')
        self.size = os.fstat(self.stream.fileno()).st_size

    def read(self, offset, size):
        if offset < 0 or offset >= self.size:
            return b''
        self.stream.seek(offset)
        return self.stream.read(size)

    def close(self):
        self.stream.close()


def xdvdfs_base(image):
    for base in XDVDFS_BASES:
        if image.read(base + 32 * DISC_SECTOR, len(XDVDFS_MAGIC)) == XDVDFS_MAGIC:
            return base
    return None


def xdvdfs_tree(image, base):
    """{top-level-relative path: (sector, size, is_dir)} of an Xbox image, and whether any of it
    lies past the end of the file. Bounded like the install's extraction: no cycles, at most
    100000 entries and 16 MB per directory."""
    descriptor = image.read(base + 32 * DISC_SECTOR, 0x1C)
    if len(descriptor) < 0x1C:
        return {}, True
    root_sector, root_size = struct.unpack_from('<II', descriptor, 0x14)
    entries, truncated, seen = {}, False, set()
    pending = [('', root_sector, root_size)]
    while pending:
        folder, sector, size = pending.pop()
        if (sector, size) in seen or size > 16 * 1024 * 1024 or len(entries) > 100000:
            break
        seen.add((sector, size))
        offset = base + sector * DISC_SECTOR
        if offset + size > image.size:
            truncated = True
            continue
        data = image.read(offset, size)
        for start in range(0, len(data), DISC_SECTOR):
            chunk, at = data[start:start + DISC_SECTOR], 0
            while at + 0x0E <= len(chunk):
                left, right, entry_sector, entry_size, attributes, length = struct.unpack_from('<HHIIBB', chunk, at)
                if (left == 0xFFFF and right == 0xFFFF) or length == 0 or at + 0x0E + length > len(chunk):
                    break
                try:
                    name = chunk[at + 0x0E:at + 0x0E + length].decode('ascii')
                except UnicodeDecodeError:
                    break
                path = (folder + '/' + name).lstrip('/')
                is_dir = bool(attributes & 0x10)
                entries[path] = (entry_sector, entry_size, is_dir)
                if is_dir:
                    pending.append((path, entry_sector, entry_size))
                elif base + entry_sector * DISC_SECTOR + entry_size > image.size:
                    truncated = True
                at += (0x0E + length + 3) & ~3
    return entries, truncated


def iso9660_system_cnf(image):
    """SYSTEM.CNF's text from an ISO 9660 image's root directory, or None."""
    descriptor = image.read(16 * DISC_SECTOR, DISC_SECTOR)
    if len(descriptor) < 190:
        return None
    root_sector, root_size = struct.unpack_from('<I', descriptor, 156 + 2)[0], struct.unpack_from('<I', descriptor, 156 + 10)[0]
    data, at = image.read(root_sector * DISC_SECTOR, min(root_size, 64 * 1024)), 0
    while at < len(data):
        length = data[at]
        if length == 0:
            at = (at // DISC_SECTOR + 1) * DISC_SECTOR      # records do not cross sectors
            continue
        record = data[at:at + length]
        name = record[33:33 + record[32]].decode('ascii', errors='replace').split(';')[0].upper() if len(record) > 33 else ''
        if name == 'SYSTEM.CNF':
            sector, size = struct.unpack_from('<I', record, 2)[0], struct.unpack_from('<I', record, 10)[0]
            return image.read(sector * DISC_SECTOR, min(size, 4096)).decode('ascii', errors='replace')
        at += length
    return None


def identify_image(path, manifest, chosen=None):
    chosen = chosen or path
    try:
        image = DiscImage(path)
    except OSError:
        return game_verdict('missing', chosen)
    try:
        size = image.size
        base = xdvdfs_base(image)
        if base is not None:
            entries, truncated = xdvdfs_tree(image, base)
            xbe_entry = entries.get('default.xbe') or next(
                (v for k, v in entries.items() if k.casefold() == 'default.xbe'), None)
            if xbe_entry is None or xbe_entry[2] or xbe_entry[1] > 64 * 1024 * 1024:
                return game_verdict('cut' if truncated else 'unknown', chosen, path, size=size)
            xbe = image.read(base + xbe_entry[0] * DISC_SECTOR, xbe_entry[1])
            kind, identity = classify_xbe(xbe, manifest)
            if kind == 'unknown' and len(xbe) < xbe_entry[1]:
                return game_verdict('cut', chosen, path, size=size)
            if kind == 'match':
                files = [k for k, v in entries.items() if not v[2]]
                problem = completeness_problem({k.split('/')[0] for k in entries}, len(files), manifest, truncated)
                return game_verdict('incomplete' if problem else 'ok', chosen, path, problem=problem, size=size)
            if len(xbe) < xbe_entry[1] and identity and kind != 'other-game':
                return game_verdict('incomplete', chosen, path, problem='the file is cut short: part of the image is missing', size=size)
            return game_verdict(kind, chosen, path, title=identity['title'] if identity else '', size=size)
        head = image.read(0, 0x440)
        for signature, label in ((b'PK\x03\x04', 'ZIP'), (b'7z\xbc\xaf\x27\x1c', '7-Zip'), (b'Rar!\x1a\x07', 'RAR')):
            if head.startswith(signature):
                return game_verdict('archive', chosen, path, title=label, size=size)
        if head[:4] in (b'RVZ\x01', b'WIA\x01') or head[:4] == b'\x01\xc0\x0b\xb1' or head[:4] == b'CISO':
            return game_verdict('dolphin', chosen, path, size=size)
        if len(head) >= 0x40 and head[0x1C:0x20] == b'\xc2\x33\x9f\x3d':
            name = head[0x20:0x60].split(b'\0')[0].decode('ascii', errors='replace').strip()
            same = 'fight for ny' in name.casefold() or 'def jam' in name.casefold()
            return game_verdict('gamecube' if same else 'gamecube-other', chosen, path, title=name, size=size)
        if len(head) >= 0x20 and head[0x18:0x1C] == b'\x5d\x1c\x9e\xa3':
            return game_verdict('wii', chosen, path, size=size)
        descriptor = image.read(16 * DISC_SECTOR, 40)
        if descriptor[1:6] == b'CD001' and descriptor[8:40].strip().startswith(b'PLAYSTATION'):
            system = iso9660_system_cnf(image) or ''
            return game_verdict('ps2' if 'BOOT2' in system.upper() else 'playstation' if 'BOOT' in system.upper()
                                else 'playstation', chosen, path, size=size)
        return game_verdict('unknown', chosen, path, size=size)
    finally:
        image.close()


def identify_folder(folder, manifest, source, chosen=None):
    chosen = chosen or folder
    xbe_path = folder / 'default.xbe'
    try:
        xbe = xbe_path.read_bytes() if xbe_path.stat().st_size <= 64 * 1024 * 1024 else b''
    except OSError:
        return game_verdict('missing', chosen, folder)
    size = dump_bytes(folder)
    kind, identity = classify_xbe(xbe, manifest)
    if kind == 'match':
        problems = load_module('setup_verify_dump', Path(source) / 'scripts/verify-dump.py').verify(str(folder), manifest)
        problem = 'some of its files are missing' if problems else ''
        return game_verdict('incomplete' if problem else 'ok', chosen, folder, problem=problem, size=size)
    return game_verdict(kind, chosen, folder, title=identity['title'] if identity else '', size=size)


def identify_game(chosen, source):
    """What the chosen file or folder is, as the wizard shows it (game_verdict). source holds
    config/dump-manifest.json and scripts/verify-dump.py: the install's own rule."""
    manifest = load_dump_manifest(source)
    chosen = Path(chosen)
    try:
        path = chosen.resolve()
        exists = path.exists()
    except OSError:
        exists = False
    if not exists:
        return game_verdict('missing', chosen)
    if path.is_dir():
        try:
            located = locate_dump(path)
        except SetupError as ex:
            return game_verdict('many-games' if 'more than one' in str(ex) else 'no-game-folder', chosen, path)
        except OSError:
            return game_verdict('missing', chosen, path)
        if located.is_file():
            return identify_image(located, manifest, chosen)
        if not (located / 'default.xbe').is_file():
            if (located / 'SYSTEM.CNF').is_file():
                return game_verdict('ps2', chosen, located)
            if (located / 'sys/main.dol').is_file():
                return game_verdict('gamecube-other', chosen, located)
            return game_verdict('no-game-folder', chosen, located)
        return identify_folder(located, manifest, source, chosen)
    return identify_image(path, manifest, chosen)


# Folders a player keeps games in, besides Downloads, Desktop and Documents: these names are
# looked for at the top of every fixed drive.
GAME_FOLDER_NAMES = ('Games', 'Game', 'Spiele', 'Jeux', 'Juegos', 'ISO', 'ISOs', 'Roms', 'ROMs', 'Emulation',
                     'Emulators', 'Xbox', 'xemu', 'Downloads', 'Backups', 'Dumps')
SKIP_FOLDERS = {'windows', 'program files', 'program files (x86)', 'programdata', '$recycle.bin', 'appdata',
                'system volume information', 'recovery', 'node_modules', 'msocache', 'perflogs', 'windowsapps',
                'steamapps', 'onedrivetemp', 'config.msi'}
NAME_HINT = re.compile(r'def[\s._-]*jam|fight[\s._-]*for[\s._-]*n\.?y|ffny', re.I)


def known_folder(guid):
    """A Windows known folder (Downloads may live anywhere), or None."""
    if not IS_WINDOWS:
        return None

    class GUID(ctypes.Structure):
        _fields_ = [('a', ctypes.c_uint32), ('b', ctypes.c_uint16), ('c', ctypes.c_uint16), ('d', ctypes.c_ubyte * 8)]
    parts = guid.split('-')
    value = GUID(int(parts[0], 16), int(parts[1], 16), int(parts[2], 16),
                 (ctypes.c_ubyte * 8)(*bytes.fromhex(parts[3] + parts[4])))
    pointer = ctypes.c_wchar_p()
    shell = ctypes.windll.shell32
    shell.SHGetKnownFolderPath.argtypes = [ctypes.POINTER(GUID), ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_wchar_p)]
    if shell.SHGetKnownFolderPath(ctypes.byref(value), 0, None, ctypes.byref(pointer)) != 0:
        return None
    try:
        return Path(pointer.value)
    finally:
        ctypes.windll.ole32.CoTaskMemFree(pointer)


def fixed_drives():
    """Fixed drives' roots (Windows), else the home folder."""
    if not IS_WINDOWS:
        return [Path.home()]
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    drives = []
    for index in range(26):
        if mask & (1 << index):
            root = chr(ord('A') + index) + ':\\'
            if ctypes.windll.kernel32.GetDriveTypeW(ctypes.c_wchar_p(root)) == 3:     # DRIVE_FIXED
                drives.append(Path(root))
    return drives


def default_search_roots():
    """(folder, depth) pairs: the player's own folders deep, each fixed drive's top shallow, and
    the usual game folders on each drive deep. xemu's last disc is looked at too."""
    roots = []
    home = Path.home()
    for guid, fallback in (('374DE290-123F-4565-9164-39C4925E467B', 'Downloads'),
                           ('B4BFCC3A-DB2C-424C-B029-7FE99A87C641', 'Desktop'),
                           ('FDD39AD0-238F-46AF-ADB4-6C85480369C7', 'Documents')):
        folder = None
        with contextlib.suppress(OSError, ValueError, AttributeError):
            folder = known_folder(guid)
        roots.append((folder or home / fallback, 3))
    for drive in fixed_drives():
        roots.append((drive, 1))
        for name in GAME_FOLDER_NAMES:
            roots.append((drive / name, 3))
    appdata = os.environ.get('APPDATA')
    if appdata:
        with contextlib.suppress(OSError):
            text = (Path(appdata) / 'xemu/xemu/xemu.toml').read_text(encoding='utf-8', errors='replace')
            match = re.search(r'''dvd_path\s*=\s*['"]([^'"]+)['"]''', text)
            if match:
                roots.append((Path(match[1]).parent, 1))
    return roots


def search_candidates(roots, deadline, limit=40, max_entries=60000):
    """Disc images (.iso/.xiso, at least 100 MB) and folders holding default.xbe under roots."""
    found, seen, visited, scanned = [], set(), set(), 0
    queue = [(Path(root), depth) for root, depth in roots]
    while queue and len(found) < limit and scanned < max_entries and time.monotonic() < deadline:
        folder, depth = queue.pop(0)
        try:
            key = os.path.normcase(str(folder.resolve()))
            if key in visited or not folder.is_dir():
                continue
            visited.add(key)
            entries = list(os.scandir(folder))
        except OSError:
            continue
        scanned += len(entries)
        if any(e.name.casefold() == 'default.xbe' and e.is_file() for e in entries if not e.is_symlink()):
            if key not in seen:
                seen.add(key)
                found.append(folder)
            continue
        for entry in entries:
            try:
                if entry.is_symlink():
                    continue
                if entry.is_file() and Path(entry.name).suffix.casefold() in ('.iso', '.xiso'):
                    if entry.stat().st_size >= MIN_IMAGE_BYTES:
                        path_key = os.path.normcase(entry.path)
                        if path_key not in seen:
                            seen.add(path_key)
                            found.append(Path(entry.path))
                elif depth > 0 and entry.is_dir() and entry.name.casefold() not in SKIP_FOLDERS \
                        and not entry.name.startswith(('.', '$')):
                    queue.append((Path(entry.path), depth - 1))
            except OSError:
                continue
    return found


def find_games(source, roots=None, seconds=10.0):
    """The player's copies of the game (and other versions of it), best first, for the wizard's
    "Found on this PC" list. Each is identified as identify_game would."""
    deadline = time.monotonic() + seconds
    candidates = search_candidates(default_search_roots() if roots is None else roots, deadline)
    manifest = load_dump_manifest(source)
    games = []
    for candidate in candidates:
        if time.monotonic() > deadline + 5:
            break
        if candidate.is_dir() and (candidate.parent / 'extraction-state.json').is_file():
            continue        # an installation's own copy (<data>\\extracted): not a dump to install from
        try:
            verdict = identify_game(candidate, source) if candidate.is_file() else \
                identify_folder(candidate, manifest, source)
        except (OSError, ValueError, KeyError, struct.error):
            continue
        if verdict['kind'] in SAME_TITLE_KINDS or NAME_HINT.search(candidate.name):
            with contextlib.suppress(OSError):
                verdict['modified'] = candidate.stat().st_mtime
            verdict['name'] = candidate.name if candidate.is_file() else 'Game folder: ' + candidate.name
            verdict['is_folder'] = candidate.is_dir()
            verdict['folder'] = str(candidate.parent)
            games.append(verdict)
    games.sort(key=lambda g: (not g['ok'], g['kind'] not in SAME_TITLE_KINDS, g['is_folder'], -g.get('modified', 0)))
    return games


def format_bytes(size):
    gb = size / 1024**3
    return (f'{gb:.0f} GB' if gb >= 10 else f'{gb:.1f} GB') if gb >= 1 else f'{size / 1024**2:.0f} MB'


def inside_path(child, parent):
    child, parent = os.path.normcase(str(child)).rstrip('\\/'), os.path.normcase(str(parent)).rstrip('\\/')
    return child == parent or child.startswith(parent + os.sep)


def environment_folders(*names):
    return [Path(os.environ[n]) for n in names if os.environ.get(n)]


def folder_location_problem(folder):
    """(problem, fix) when a folder is somewhere the game should not go, else None."""
    parts = [p.casefold() for p in folder.parts]
    if any(inside_path(folder, f) for f in environment_folders('OneDrive', 'OneDriveConsumer', 'OneDriveCommercial')) \
            or any(p.startswith('onedrive') for p in parts):
        return 'This folder is in OneDrive, which would upload the 20 GB or more the build writes'
    if any(inside_path(folder, f) for f in environment_folders('ProgramFiles', 'ProgramFiles(x86)', 'ProgramW6432')):
        return 'This folder is in Program Files, where changes need admin rights'
    system = environment_folders('SystemRoot', 'windir', 'ProgramData', 'TEMP', 'TMP')
    users = environment_folders('USERPROFILE')
    if any(inside_path(folder, f) for f in system) or folder == Path(folder.anchor) or \
            any(folder == u or folder == u.parent for u in users):
        return 'This is a Windows or system folder'
    return None


UNSAFE_PATH_CHARACTERS = set(';%!^&\'`')


def path_problem(folder):
    text = str(folder)
    if len(str(folder / 'data')) > 140:
        return "This folder's path is too long for the build (at most about 130 characters)"
    if any(ord(c) > 126 or c in UNSAFE_PATH_CHARACTERS for c in text) or any(p != p.rstrip(' .') for p in folder.parts[1:]):
        return 'This path has characters the build tools may not handle (only plain English letters, numbers, spaces, - and _ are safe)'
    return None


def is_ffny_folder(folder):
    """An installation of this game (made by this wizard), an empty folder, or one only with its two parts."""
    try:
        names = {p.name.casefold() for p in folder.iterdir()}
    except OSError:
        return True
    return (folder / 'app/installed.json').is_file() or (folder / 'app/setup-owner.json').is_file() or \
        names <= {'app', 'data', 'desktop.ini'}


def existing_install(folder):
    """(install, data) of an earlier installation in folder, or None."""
    receipt = folder / 'app/installed.json'
    try:
        data = json.loads(receipt.read_text(encoding='utf-8'))['data']
        return folder / 'app', Path(data)
    except (OSError, ValueError, KeyError, TypeError):
        return None


def drive_free(path):
    with contextlib.suppress(OSError):
        return shutil.disk_usage(nearest_existing(path)).free
    return 0


def suggest_folder(needed):
    """An earlier installation's folder, or <the fixed NTFS drive with the most free space>:\\Games\\FFNY Online."""
    places = [Path(os.environ['LOCALAPPDATA']) / 'DefJamRecompiled'] if os.environ.get('LOCALAPPDATA') else []
    drives = fixed_drives()
    places += [d / 'Games/FFNY Online' for d in drives] + [d / 'FFNY Online' for d in drives]
    for place in places:
        if existing_install(place):
            return place
    usable = [d for d in drives if (volume_filesystem(d) or 'NTFS').upper() in ('NTFS', 'REFS')] or drives
    usable.sort(key=lambda d: (drive_free(d) < needed, -drive_free(d), str(d)))
    drive = usable[0] if usable else Path.home()
    games = drive / 'Games'
    folder = games / 'FFNY Online' if games.is_dir() or not games.exists() else drive / 'FFNY Online'
    if folder.exists() and not is_ffny_folder(folder):
        folder = folder.with_name('FFNY Online Setup')
    return folder


def writable(folder):
    """Whether this user can create folders where the game goes, without admin rights. Leaves nothing behind."""
    try:
        probe = Path(tempfile.mkdtemp(prefix='ffny-write-check-', dir=nearest_existing(folder)))
    except OSError:
        return False
    with contextlib.suppress(OSError):
        probe.rmdir()
    return True


def plan_folder(folder=None, dump=None, hd=False):
    """Where the wizard installs: the folder (suggested when None), its install and data folders,
    and each check with a plain reason and fix when it fails. Nothing is created."""
    dump = Path(dump).resolve() if dump else None
    dump_size = 0
    if dump is not None:
        with contextlib.suppress(OSError):
            dump_size = dump_bytes(dump)
    needed = space_needed(dump_size, hd, True)[1]
    suggested = folder is None
    folder = suggest_folder(needed) if folder is None else Path(folder)
    folder = Path(os.path.abspath(folder))
    adjusted = False
    if folder.name.casefold() == 'app' and (folder / 'installed.json').is_file():
        folder = folder.parent              # the app folder of an installation: the installation
    elif not suggested and folder.is_dir() and not is_ffny_folder(folder):
        folder, adjusted = folder / 'FFNY Online', True     # a folder with other things in it: a new one inside
    existing = existing_install(folder)
    install, data = existing if existing else (folder / 'app', folder / 'data')
    fallback = suggest_folder(needed)
    hint = '' if os.path.normcase(str(fallback)) == os.path.normcase(str(folder)) else ', for example ' + str(fallback)
    checks = []

    def check(key, ok, text, fix=''):
        checks.append({'id': key, 'ok': bool(ok), 'text': text, 'fix': '' if ok else fix})

    free = drive_free(folder)
    drive = folder.anchor.rstrip('\\/') or str(folder)
    if free >= needed:
        check('space', True, f'Enough free space on this drive ({format_bytes(free)} free, needs about {format_bytes(needed)})')
    else:
        without_hd = space_needed(dump_size, False, True)[1]
        fix = 'Free up space on ' + drive + ', or choose a folder on another drive' + hint + '.'
        if hd and free >= without_hd:
            fix = 'Turn off HD textures (you can add them later in the launcher), free up space, or choose another drive.'
        check('space', False, f'Not enough free space on {drive} ({format_bytes(free)} free, needs about {format_bytes(needed)})', fix)
    can_write = writable(folder)
    check('write', can_write, 'Setup can write here, no admin rights needed' if can_write else
          'Setup cannot write to this folder without admin rights', 'Choose a folder of your own' + hint + '.')
    location = folder_location_problem(folder)
    check('place', location is None, location or 'Not in OneDrive, Program Files or a system folder',
          'Choose a folder outside it' + hint + '.')
    if dump is not None:
        dump_inside = inside_path(dump, folder)
        nested = dump_inside or (dump.is_dir() and inside_path(folder, dump))
        check('dump', not nested, 'Your game file stays outside this folder' if not nested else
              'Your game file is inside this folder' if dump_inside else 'This folder is inside your game folder',
              'Choose a different folder' + hint + ', or move your game file somewhere else (for example Documents).')
    filesystem = volume_filesystem(folder) if IS_WINDOWS else None
    if filesystem and filesystem.upper() not in ('NTFS', 'REFS'):
        check('drive', False, f'{drive} uses {filesystem}, which cannot hold the folder link the game needs',
              'Choose a folder on an NTFS drive' + hint + '.')
    problem = path_problem(folder)
    if problem:
        check('path', False, problem, 'Choose a folder with a shorter, plain path' + hint + '.')
    if not existing and folder.is_dir() and not is_ffny_folder(folder):
        check('empty', False, 'This folder already has other files in it', 'Choose an empty folder' + hint + '.')
    if existing:
        check('existing', True, 'FFNY Online is already here: setup updates it and keeps your saves')
    return {'folder': str(folder), 'install_dir': str(install), 'data_dir': str(data), 'existing': bool(existing),
            'adjusted': adjusted, 'suggested': suggested, 'ok': all(c['ok'] for c in checks), 'checks': checks,
            'free': free, 'needed': needed}


def wizard_question(args):
    """--identify, --find-games or --plan-folder: answer into --out, exit 0 (2 for bad arguments)."""
    if not args.out:
        raise SetupError('--out is required', 2)
    source = Path(args.payload) / 'source'
    if args.source_root:
        source = Path(args.source_root)
    if args.identify:
        try:
            result = identify_game(args.identify, source)
        except (OSError, ValueError, struct.error):
            # Unreadable in a way the checks above did not expect: say only that it is not the game.
            result = game_verdict('unknown', args.identify)
    elif args.find_games:
        roots = [(Path(r), 3) for r in args.search_root] if args.search_root else None
        result = {'games': find_games(source, roots)}
    else:
        result = plan_folder(args.folder, args.dump, args.hd_textures)
    write_json(args.out, result)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload', required=True)
    parser.add_argument('--silent', action='store_true')
    parser.add_argument('--dump')
    parser.add_argument('--reuse-extracted', action='store_true',
                        help='Update: build from the game files the installation already extracted (no --dump)')
    parser.add_argument('--allow-running', action='store_true',
                        help='Update from the running game\'s launcher (Windows; the old version keeps its own copy)')
    parser.add_argument('--install-dir')
    parser.add_argument('--data-dir')
    # The Windows wizard's questions (wizard_question); each answers into --out and installs nothing.
    parser.add_argument('--identify', help='Wizard: what this game file or folder is')
    parser.add_argument('--find-games', action='store_true', help="Wizard: look for the player's copies")
    parser.add_argument('--search-root', action='append', help='Wizard: look only here (repeatable)')
    parser.add_argument('--plan-folder', action='store_true', help='Wizard: check (or suggest) an install folder')
    parser.add_argument('--folder', help='Wizard: the folder --plan-folder checks')
    parser.add_argument('--source-root', help=argparse.SUPPRESS)
    parser.add_argument('--out')
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
        if args.identify or args.find_games or args.plan_folder:
            return wizard_question(args)
        if not args.install_dir:
            raise SetupError('--install-dir is required', 2)
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
