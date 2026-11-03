"""Add the app to Steam as a non-Steam game, with its library artwork (design §7; Windows).

Linux asks the running Steam to add the shortcut (engine.py add_to_steam). Windows has no such
request, so this writes the shortcut into Steam's own file, as Steam ROM Manager and BoilR do:

- Steam rewrites userdata/<account>/config/shortcuts.vdf from memory when it exits, so the file is
  changed only while Steam is closed. With --close-steam, Steam is asked to exit first
  (steam.exe -shutdown) and started again afterwards.
- The file is edited without re-encoding what is already there: every other shortcut keeps its
  bytes exactly; only their numbering changes when one is removed. A copy of the previous file is
  kept beside it (shortcuts.vdf.defjam-backup).
- The shortcut's appid is chosen here (the usual crc32 of the target and name), so the artwork is
  named by it at once; an existing shortcut to the same program is kept, with its own appid.
- The artwork rules are the Linux setup's: art the player chose is never replaced, and only files
  this placed, still unchanged, are removed. What was placed is recorded in --steam-record.

DEFJAM_STEAM_ROOT stands in for Steam's folder in tests, so a test never reads or writes the
player's own Steam, and only a steam.exe in that folder counts as running.
"""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

IS_WINDOWS = os.name == 'nt'
APP_NAME = 'Def Jam Recompiled'
STEAMID64_BASE = 76561197960265728
# Grid file suffix -> artwork file. The "Fan Recomp" set in branding/fan/steam comes first; the plain
# set fills what it lacks (the hero, the icon). The icon is not a grid image Steam looks for by name:
# the shortcut's icon field names it, and keeping it in the grid folder keeps it with the rest.
ART = {'p': 'portrait.png', '': 'wide.png', '_hero': 'hero.png', '_logo': 'logo.png', '_icon': 'icon.png'}
BACKUP = 'shortcuts.vdf.defjam-backup'


class SteamError(Exception):
    def __init__(self, message, code=2):
        super().__init__(message)
        self.code = code


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def same_path(a, b):
    return os.path.normcase(os.path.abspath(str(a))) == os.path.normcase(os.path.abspath(str(b)))


# ---------------------------------------------------------------- shortcuts.vdf (binary VDF)

def _node(data, pos):
    """One map's fields from `pos` to its end marker: (dict, position after the marker)."""
    out = {}
    while True:
        if pos >= len(data):
            raise ValueError('unterminated VDF map')
        kind = data[pos]
        pos += 1
        if kind == 8:
            return out, pos
        end = data.index(b'\0', pos)
        key = data[pos:end].decode('utf-8', 'replace')
        pos = end + 1
        if kind == 0:
            out[key], pos = _node(data, pos)
        elif kind == 1:
            end = data.index(b'\0', pos)
            out[key] = data[pos:end].decode('utf-8', 'replace')
            pos = end + 1
        elif kind == 2:
            out[key] = struct.unpack_from('<I', data, pos)[0]
            pos += 4
        elif kind == 3:
            out[key] = struct.unpack_from('<f', data, pos)[0]
            pos += 4
        elif kind == 7:
            out[key] = struct.unpack_from('<Q', data, pos)[0]
            pos += 8
        else:
            raise ValueError('unsupported VDF value type %d' % kind)


def split_shortcuts(data):
    """(bodies, tail): each shortcut's map exactly as Steam wrote it (from after its key to its end
    marker), and what follows the shortcuts map. An empty file has none."""
    if not data:
        return [], b'\x08'
    head = b'\x00shortcuts\x00'
    if data[:len(head)].lower() != head:
        raise ValueError('not a shortcuts file')
    pos = len(head)
    bodies = []
    while True:
        if pos >= len(data):
            raise ValueError('unterminated shortcuts map')
        if data[pos] == 8:
            return bodies, data[pos + 1:]
        if data[pos] != 0:
            raise ValueError('a shortcut that is not a map')
        start = data.index(b'\0', pos + 1) + 1
        _, stop = _node(data, start)
        bodies.append(data[start:stop])
        pos = stop


def join_shortcuts(bodies, tail=b'\x08'):
    out = bytearray(b'\x00shortcuts\x00')
    for number, body in enumerate(bodies):
        out += b'\x00' + str(number).encode() + b'\x00' + body
    return bytes(out + b'\x08' + tail)


def fields(body):
    return _node(body, 0)[0]


def field(values, name, default=None):
    return next((v for k, v in values.items() if k.lower() == name.lower()), default)


def shortcut_appid(exe_field, name=APP_NAME):
    """The id Steam ROM Manager and BoilR give a shortcut (and Steam itself, for one added by hand)."""
    return zlib.crc32((exe_field + name).encode('utf-8')) | 0x80000000


def shortcut_body(appid, exe, start_dir, icon, launch_options, name=APP_NAME):
    """A shortcut map with the fields Steam writes for one added through "Add a Non-Steam Game"."""
    def text(key, value):
        return b'\x01' + key.encode() + b'\0' + value.encode('utf-8') + b'\0'

    def number(key, value):
        return b'\x02' + key.encode() + b'\0' + struct.pack('<I', value & 0xFFFFFFFF)

    return (number('appid', appid) + text('AppName', name) + text('Exe', '"%s"' % exe)
            + text('StartDir', '"%s"' % start_dir) + text('icon', icon) + text('ShortcutPath', '')
            + text('LaunchOptions', launch_options) + number('IsHidden', 0) + number('AllowDesktopConfig', 1)
            + number('AllowOverlay', 1) + number('OpenVR', 0) + number('Devkit', 0) + text('DevkitGameID', '')
            + number('DevkitOverrideAppID', 0) + number('LastPlayTime', 0) + text('FlatpakAppID', '')
            + b'\x00tags\x00\x08' + b'\x08')


def entry_appid(values):
    """A shortcut's appid; one without the field (an older Steam's) has the id Steam gives it."""
    appid = field(values, 'appid')
    if isinstance(appid, int):
        return appid & 0xFFFFFFFF
    return shortcut_appid(str(field(values, 'exe', '')), str(field(values, 'appname', '')))


def runs(values, program):
    exe = field(values, 'exe', '')
    return isinstance(exe, str) and bool(exe.strip()) and same_path(exe.strip().strip('"'), program)


# ---------------------------------------------------------------- Steam and its accounts

def steam_root():
    """Steam's folder, or None when Steam is not installed."""
    override = os.environ.get('DEFJAM_STEAM_ROOT')
    if override:
        return Path(override)
    if IS_WINDOWS:
        import winreg
        with contextlib.suppress(OSError):
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as key:
                path = winreg.QueryValueEx(key, 'SteamPath')[0]
            if path and (Path(path) / 'steam.exe').is_file():
                return Path(os.path.normpath(path))
        fallback = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Steam'
        return fallback if (fallback / 'steam.exe').is_file() else None
    if sys.platform == 'darwin':
        path = Path.home() / 'Library/Application Support/Steam'
        return path if path.is_dir() else None
    return None


def parse_text_vdf(text):
    """Steam's text VDF (loginusers.vdf): nested maps of strings."""
    tokens = re.findall(r'"((?:[^"\\]|\\.)*)"|([{}])', text)
    stack = [{}]
    key = None
    for quoted, brace in tokens:
        if brace == '{':
            if key is None:
                raise ValueError('a map with no name')
            stack[-1][key] = {}
            stack.append(stack[-1][key])
            key = None
        elif brace == '}':
            if len(stack) == 1:
                raise ValueError('unbalanced VDF')
            stack.pop()
        elif key is None:
            key = quoted
        else:
            stack[-1][key] = quoted.replace('\\"', '"').replace('\\\\', '\\')
            key = None
    return stack[0]


def steam_user(root):
    """(userdata folder, name) of the account to add to: the one that signed in last, else the only one."""
    users = {}
    with contextlib.suppress(OSError, ValueError):
        users = field(parse_text_vdf((Path(root) / 'config/loginusers.vdf').read_text(encoding='utf-8', errors='replace')),
                      'users', {})
    best = None
    for steamid, info in users.items():
        if not steamid.isdigit() or int(steamid) <= STEAMID64_BASE or not isinstance(info, dict):
            continue
        timestamp = field(info, 'Timestamp', '0')
        rank = (field(info, 'MostRecent', '0') == '1', int(timestamp) if timestamp.isdigit() else 0)
        name = field(info, 'PersonaName') or field(info, 'AccountName') or steamid
        if best is None or rank > best[0]:
            best = (rank, Path(root) / 'userdata' / str(int(steamid) - STEAMID64_BASE), name)
    if best:
        return best[1], best[2]
    folders = []
    with contextlib.suppress(OSError):
        folders = [f for f in sorted((Path(root) / 'userdata').iterdir()) if f.name.isdigit() and f.name != '0']
    if len(folders) == 1:
        return folders[0], folders[0].name
    if not folders:
        raise SteamError('Sign in to Steam once on this computer, then try again')
    raise SteamError('Several Steam accounts use this computer and none is marked as the last one; '
                     'sign in to the one to add the game to, then try again')


def steam_processes(root):
    """The running Steam client from `root` (Windows): process ids whose program is root\\steam.exe.
    Only that copy counts, so a test's stand-in never sees the player's own Steam."""
    if not IS_WINDOWS:
        return []
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel32.QueryFullProcessImageNameW.argtypes = (wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                    ctypes.POINTER(wintypes.DWORD))
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    target = Path(root) / 'steam.exe'
    count = 4096
    while True:
        pids = (wintypes.DWORD * count)()
        used = wintypes.DWORD()
        if not kernel32.K32EnumProcesses(pids, ctypes.sizeof(pids), ctypes.byref(used)):
            return []
        if used.value < ctypes.sizeof(pids):
            break
        count *= 2
    found = []
    name = ctypes.create_unicode_buffer(32768)
    for pid in pids[:used.value // ctypes.sizeof(wintypes.DWORD)]:
        handle = kernel32.OpenProcess(0x1000, False, pid)     # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            continue
        try:
            size = wintypes.DWORD(len(name))
            if kernel32.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size)) and same_path(name.value, target):
                found.append(pid)
        finally:
            kernel32.CloseHandle(handle)
    return found


def close_steam(root, running=steam_processes, timeout=90.0):
    """Ask Steam to exit, as its own menu's Exit does, and wait until it has (it writes its files first)."""
    flags = 0x08000000 if IS_WINDOWS else 0                        # CREATE_NO_WINDOW
    subprocess.Popen([str(Path(root) / 'steam.exe'), '-shutdown'], cwd=str(root), creationflags=flags,
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + timeout
    while running(root):
        if time.monotonic() >= deadline:
            raise SteamError('Steam did not close. Close it from its menu (Steam > Exit), then try again', 4)
        time.sleep(0.5)


def start_steam(root):
    flags = 0x00000008 | 0x00000200 if IS_WINDOWS else 0          # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    subprocess.Popen([str(Path(root) / 'steam.exe')], cwd=str(root), creationflags=flags, close_fds=True,
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ---------------------------------------------------------------- artwork

def art_origin(art_folders, name):
    return next((Path(f) / name for f in art_folders if (Path(f) / name).is_file()), None)


def place_art(grid, appid, art_folders, owned=(), event=print):
    """Copy the artwork into the grid folder under the shortcut's appid. Returns the files placed
    (or already there and the same); leaves art the player chose alone."""
    grid = Path(grid)
    grid.mkdir(parents=True, exist_ok=True)
    placed = []
    for suffix, name in ART.items():
        origin = art_origin(art_folders, name)
        if origin is None:
            continue
        target = grid / (str(appid) + suffix + '.png')
        if target.exists() and str(target) not in owned and digest(target) != digest(origin):
            event('Left ' + str(target) + ' alone: it is your own artwork')
            continue
        pending = target.with_name(target.name + '.new')
        shutil.copyfile(origin, pending)
        os.replace(pending, target)
        placed.append(str(target))
    return placed


def remove_art(grid, appid, art_folders, hashes):
    """Remove the artwork this placed for `appid`: files still the same as recorded, or as ours."""
    ours = {digest(o) for o in (art_origin(art_folders, n) for n in ART.values()) if o is not None}
    for suffix in ART:
        target = Path(grid) / (str(appid) + suffix + '.png')
        if target.is_file() and not target.is_symlink():
            sha = digest(target)
            if sha == hashes.get(str(target)) or sha in ours:
                target.unlink()


# ---------------------------------------------------------------- add, remove, status

def read_record(record):
    with contextlib.suppress(OSError, ValueError):
        value = json.loads(Path(record).read_text(encoding='utf-8'))
        if isinstance(value, dict):
            return value
    return {}


def write_atomically(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_name(path.name + '.defjam-new')
    pending.write_bytes(data)
    os.replace(pending, path)


def read_shortcuts(vdf):
    try:
        return split_shortcuts(vdf.read_bytes() if vdf.is_file() else b'')
    except (OSError, ValueError, struct.error) as ex:
        raise SteamError("Steam's list of non-Steam games (" + str(vdf) + ') could not be read (' + str(ex)
                         + '); nothing was changed')


def status(root, program, running=steam_processes):
    """What the app's Steam section shows."""
    result = {'steam': '', 'user': '', 'running': False, 'added': False}
    if root is None:
        result['message'] = 'Steam is not installed on this computer'
        return result
    result['steam'] = str(root)
    result['running'] = bool(running(root))
    try:
        folder, result['user'] = steam_user(root)
        bodies, _ = read_shortcuts(folder / 'config/shortcuts.vdf')
        result['added'] = bool(program) and any(runs(fields(b), program) for b in bodies)
    except SteamError as ex:
        result['message'] = str(ex)
    return result


@contextlib.contextmanager
def steam_closed(root, close, event, running, closer, starter):
    """Steam closed while the body runs: refused (exit 4) while it runs, unless asked to close it;
    then started again afterwards."""
    was_running = bool(running(root))
    if was_running:
        if not close:
            raise SteamError('Steam is running. It must be closed while the game is added, because it '
                             'rewrites its list of games when it exits', 4)
        event('Closing Steam')
        closer(root)
    try:
        yield
    finally:
        if was_running:
            starter(root)
            event('Started Steam again')


def add(root, program, install, art_folders, record, close=False, event=print,
        running=steam_processes, closer=close_steam, starter=start_steam):
    """Add the app to the account's Steam library, starting the game (--play), with its artwork."""
    if root is None:
        raise SteamError('Steam is not installed on this computer')
    program = Path(program)
    folder, user = steam_user(root)
    vdf = folder / 'config/shortcuts.vdf'
    read_shortcuts(vdf)                                       # refuse a damaged file before closing Steam
    with steam_closed(root, close, event, running, closer, starter):
        bodies, tail = read_shortcuts(vdf)
        appid = next((entry_appid(fields(b)) for b in bodies if runs(fields(b), program)), None)
        if appid is None:
            appid = shortcut_appid('"%s"' % program)
            grid = folder / 'config/grid'
            icon = str(grid / (str(appid) + '_icon.png')) if art_origin(art_folders, ART['_icon']) else str(program)
            options = '--play' + (' --install "%s"' % install if install else '')
            bodies.append(shortcut_body(appid, program, program.parent, icon, options))
            if vdf.is_file():
                shutil.copyfile(vdf, vdf.with_name(BACKUP))
            write_atomically(vdf, join_shortcuts(bodies, tail))
            event('Added Def Jam Recompiled to ' + user + "'s Steam library")
        else:
            event('Def Jam Recompiled is already in ' + user + "'s Steam library")
        old = read_record(record)
        hashes = old.get('art_hashes', {})
        owned = [n for n in old.get('art', []) if Path(n).is_file() and digest(n) == hashes.get(n)]
        placed = place_art(folder / 'config/grid', appid, art_folders, owned, event)
        write_atomically(record, json.dumps({'appid': appid, 'user_folder': str(folder), 'program': str(program),
                                             'art': placed, 'art_hashes': {n: digest(n) for n in placed}},
                                            indent=2).encode('utf-8'))
        event('Its library artwork is in place' if placed else 'Its library artwork was left as it was')
    return {'appid': appid, 'user_folder': str(folder), 'user': user}


def remove(root, program, art_folders, record, close=False, event=print,
           running=steam_processes, closer=close_steam, starter=start_steam):
    """Remove every shortcut to the app from the account's library, and the artwork this placed."""
    if root is None:
        raise SteamError('Steam is not installed on this computer')
    program = Path(program)
    old = read_record(record)
    folder = Path(old['user_folder']) if old.get('user_folder') and Path(old['user_folder']).is_dir() else steam_user(root)[0]
    vdf = folder / 'config/shortcuts.vdf'
    read_shortcuts(vdf)
    with steam_closed(root, close, event, running, closer, starter):
        bodies, tail = read_shortcuts(vdf)
        keep = [b for b in bodies if not runs(fields(b), program)]
        gone = [entry_appid(fields(b)) for b in bodies if runs(fields(b), program)]
        if gone:
            shutil.copyfile(vdf, vdf.with_name(BACKUP))
            write_atomically(vdf, join_shortcuts(keep, tail))
        for appid in set(gone) | ({old['appid']} if isinstance(old.get('appid'), int) else set()):
            remove_art(folder / 'config/grid', appid, art_folders, old.get('art_hashes', {}))
        with contextlib.suppress(OSError):
            Path(record).unlink()
        event('Removed Def Jam Recompiled from your Steam library' if gone else 'Def Jam Recompiled was not in your Steam library')
    return {'removed': len(gone)}


def command(action, payload, program, install, record, result_file, close=False):
    """engine.py --steam ACTION: the result, as JSON, in result_file. Exit 0, 2 (the message says
    why) or 4 (Steam is running; ask, then run again with --close-steam)."""
    messages = []
    root = steam_root()
    art_folders = [Path(payload) / 'source/branding/fan/steam', Path(payload) / 'source/branding/steam']
    code = 0
    try:
        if action != 'status' and not IS_WINDOWS and not os.environ.get('DEFJAM_STEAM_ROOT'):
            # macOS and Linux need their own way to tell whether Steam is running (design §7).
            raise SteamError('Add to Steam from the app is for Windows for now')
        if action != 'status' and not (program and record):
            raise SteamError('--app-program and --steam-record are required')
        if action == 'add':
            add(root, program, install, art_folders, record, close, messages.append)
        elif action == 'remove':
            remove(root, program, art_folders, record, close, messages.append)
    except SteamError as ex:
        messages.append(str(ex))
        code = ex.code
    except OSError as ex:
        messages.append('Steam could not be changed: ' + str(ex))
        code = 2
    result = status(root, program or '')
    if messages:
        result['message'] = messages[-1]
    result['messages'] = messages
    result['details'] = '\n'.join(messages)
    if result_file:
        write_atomically(result_file, json.dumps(result, indent=2).encode('utf-8'))
    else:
        print(json.dumps(result))
    return code
