"""The machine: paths, the shared test lock, game processes and the safe environment.

One PC runs every agent's game tests, and two copies on the same CPUs starve each other (and once
froze the owner's PC). So every launch goes through MachineLock: it waits until the lock file
(D:\\DefJam\\test-lock.txt by default, DEFJAM_TEST_LOCK) is absent and no defjam_recomp.exe runs --
the owner's own game included -- then writes the lock (agent, time, pid) and removes it on close,
at exit, or when a later waiter finds the holder's process gone (stale-lock detection by PID and
process start time). A lock written by hand (no pid line) is never treated as stale.
"""
import atexit
import ctypes
import datetime
import os
import re
import socket
import sys
import time
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DATA = os.environ.get("DEFJAM_DATA") or r"D:\DefJam\data"
LOCK_PATH = os.environ.get("DEFJAM_TEST_LOCK") or os.path.join(os.path.dirname(DATA.rstrip("\\/")), "test-lock.txt")
GAME_IMAGE = "defjam_recomp.exe"
INTRO_MOVIES = r"movies\eagames.mad;movies\thx.mad;movies\intro.mad"
WINDOWS = os.name == "nt"


def exe_path(preset="win-x64-release"):
    return os.path.join(REPO, "build", preset, GAME_IMAGE)


def relay_path():
    return os.path.join(REPO, "netplay", "relay", "dist", "relay.exe" if WINDOWS else "relay")


def agent_id():
    """Who holds the lock: DEFJAM_AGENT, else the checkout's folder (a worktree's agent name)."""
    return os.environ.get("DEFJAM_AGENT") or os.path.basename(REPO)


def inside(path, root):
    """Whether path lies inside root (case-insensitive on Windows)."""
    if not path:
        return False
    p, r = os.path.normcase(os.path.abspath(path)), os.path.normcase(os.path.abspath(root))
    return p == r or p.startswith(r.rstrip("\\/") + os.sep)


# ---- processes ---------------------------------------------------------------------------------

if WINDOWS:
    from ctypes import wintypes as wt

    class _PE32(ctypes.Structure):
        _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                    ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", wt.DWORD),
                    ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD),
                    ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
                    ("szExeFile", ctypes.c_wchar * 260)]

    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
    _k32.OpenProcess.restype = wt.HANDLE
    _QUERY = 0x1000         # PROCESS_QUERY_LIMITED_INFORMATION
    _STILL_ACTIVE = 259


def processes(image=None):
    """[(pid, image name, full path or None)] of running processes, optionally by image name."""
    out = []
    if not WINDOWS:
        return out
    snap = _k32.CreateToolhelp32Snapshot(0x2, 0)
    if not snap or snap == wt.HANDLE(-1).value:
        return out
    try:
        e = _PE32()
        e.dwSize = ctypes.sizeof(_PE32)
        ok = _k32.Process32FirstW(snap, ctypes.byref(e))
        while ok:
            name = e.szExeFile
            if image is None or name.lower() == image.lower():
                out.append((e.th32ProcessID, name, process_path(e.th32ProcessID)))
            ok = _k32.Process32NextW(snap, ctypes.byref(e))
    finally:
        _k32.CloseHandle(snap)
    return out


def process_path(pid):
    if not WINDOWS:
        return None
    h = _k32.OpenProcess(_QUERY, False, int(pid))
    if not h:
        return None
    try:
        buf = ctypes.create_unicode_buffer(1024)
        n = wt.DWORD(len(buf))
        if _k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)):
            return buf.value
        return None
    finally:
        _k32.CloseHandle(h)


def process_started(pid):
    """The process's creation time (FILETIME as an int), or None when it is not running."""
    if not WINDOWS:
        try:
            os.kill(int(pid), 0)
            return 0
        except OSError:
            return None
    h = _k32.OpenProcess(_QUERY, False, int(pid))
    if not h:
        return None
    try:
        code = wt.DWORD()
        if _k32.GetExitCodeProcess(h, ctypes.byref(code)) and code.value != _STILL_ACTIVE:
            return None
        c, x, k, u = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
        if not _k32.GetProcessTimes(h, ctypes.byref(c), ctypes.byref(x), ctypes.byref(k), ctypes.byref(u)):
            return 0
        return (c.dwHighDateTime << 32) | c.dwLowDateTime
    finally:
        _k32.CloseHandle(h)


def games(exclude=()):
    """Running game copies [(pid, path)], all of them (the owner's included), minus `exclude` pids."""
    return [(pid, path) for pid, _, path in processes(GAME_IMAGE) if pid not in set(exclude)]


def kill_pid(pid):
    """Terminate one process by PID (only ever called for processes this tool started)."""
    if WINDOWS:
        h = _k32.OpenProcess(0x0001 | _QUERY, False, int(pid))     # PROCESS_TERMINATE
        if h:
            _k32.TerminateProcess(h, 1)
            _k32.CloseHandle(h)
    else:
        try:
            os.kill(int(pid), 9)
        except OSError:
            pass


def stray_own_processes():
    """Game or relay processes whose exe is inside this checkout (left by a crashed earlier run)."""
    out = []
    for pid, name, path in processes():
        if name.lower() in (GAME_IMAGE, "relay.exe") and inside(path, REPO):
            out.append((pid, path))
    return out


# ---- the lock ----------------------------------------------------------------------------------

LOCK_PID_RE = re.compile(r"^pid=(\d+)(?:\s+started=(\d+))?", re.M)
PRIORITY_PATH = os.environ.get("DEFJAM_TEST_PRIORITY") or os.path.join(os.path.dirname(LOCK_PATH), "test-priority.txt")


def priority_holder(text):
    """The agent a priority file names: the first word of its first line."""
    first = text.strip().splitlines()[0] if text.strip() else ""
    return first.split()[0] if first.split() else ""


def priority_blocks(text, agent):
    """Whether a priority file (test-priority.txt: '<agent> (why) ...') keeps `agent` from the lock."""
    if text is None:
        return False
    holder = priority_holder(text)
    return not holder or holder.lower() != agent.lower()


def parse_lock(text):
    """{'who': first line, 'pid': int|None, 'started': int|None} of a lock file's text."""
    first = text.strip().splitlines()[0] if text.strip() else ""
    m = LOCK_PID_RE.search(text)
    return {"who": first, "pid": int(m.group(1)) if m else None,
            "started": int(m.group(2)) if m and m.group(2) else None}


def lock_is_stale(info, started_of=process_started):
    """A lock is stale when it names a pid whose process is gone (or is another process now)."""
    if not info.get("pid"):
        return False            # written by hand: only its writer removes it
    now = started_of(info["pid"])
    if now is None:
        return True
    return bool(info.get("started")) and now not in (0, info["started"])


class MachineLock:
    """The machine-wide test lock. acquire() waits; release() runs at close and at exit."""

    def __init__(self, path=None, agent=None, note="", log=None, priority_path=None):
        self.path = path or LOCK_PATH
        self.priority_path = priority_path or (os.path.join(os.path.dirname(self.path), "test-priority.txt")
                                               if path else PRIORITY_PATH)
        self.agent = agent or agent_id()
        self.note = note
        self.token = uuid.uuid4().hex[:12]
        self.held = False
        self.log = log or (lambda msg: print(msg, file=sys.stderr, flush=True))
        self.waited_s = 0.0

    def text(self):
        stamp = datetime.datetime.now().isoformat(timespec="seconds")
        return ("%s %s %s\npid=%d started=%s token=%s host=%s\n"
                % (self.agent, stamp, self.note, os.getpid(), process_started(os.getpid()) or 0,
                   self.token, socket.gethostname()))

    def read(self):
        try:
            with open(self.path, "r", encoding="utf-8", errors="replace") as f:
                return f.read()
        except OSError:
            return None

    def acquire(self, timeout=None, poll=5.0, own_pids=()):
        """Wait for a free machine, then take the lock. Returns True, or False after `timeout` s."""
        if self.held:
            return True
        t0 = time.time()
        said = None
        while True:
            text = self.read()
            why = None
            try:
                with open(self.priority_path, "r", encoding="utf-8", errors="replace") as f:
                    prio = f.read()
            except OSError:
                prio = None
            if priority_blocks(prio, self.agent):
                why = "priority to %s (%s)" % (priority_holder(prio) or "?", os.path.basename(self.priority_path))
            elif text is not None:
                info = parse_lock(text)
                if lock_is_stale(info):
                    self.log("[lock] removing a stale lock (its process %s is gone): %s" % (info["pid"], info["who"]))
                    try:
                        os.remove(self.path)
                    except OSError:
                        pass
                    continue
                why = "lock held: " + info["who"][:120]
            else:
                running = games(exclude=own_pids)
                if running:
                    why = "another game copy runs: " + ", ".join("%d %s" % (p, path or "?") for p, path in running)[:200]
            if why is None:
                try:
                    fd = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                except FileExistsError:
                    continue
                with os.fdopen(fd, "w", encoding="utf-8") as f:
                    f.write(self.text())
                # a copy that started between the check and the lock: give the machine back
                if games(exclude=own_pids):
                    self._remove()
                    continue
                self.held = True
                self.waited_s = round(time.time() - t0, 1)
                atexit.register(self.release)
                if said:
                    self.log("[lock] taken after %.0f s" % self.waited_s)
                return True
            if why != said:
                self.log("[lock] waiting: " + why)
                said = why
            if timeout is not None and time.time() - t0 >= timeout:
                return False
            time.sleep(poll)

    def _remove(self):
        text = self.read()
        if text is not None and ("token=" + self.token) in text:
            try:
                os.remove(self.path)
            except OSError:
                pass

    def release(self):
        if self.held:
            self._remove()
            self.held = False


# ---- the safe environment ----------------------------------------------------------------------

BASE_ENV = {
    "RECOMP_VBLANK": "1", "RECOMP_PB_EXEC": "1", "RECOMP_PB_D3D11": "1", "RECOMP_USB": "1",
    "RECOMP_SETTINGS": "none",                  # a player's settings.ini never changes a test
    "RECOMP_SKIP_FILES": INTRO_MOVIES,          # the intro movies (80 s of every boot)
    "RECOMP_WINDOW_BACKGROUND": "1",            # off the desktop, no focus taken
    "RECOMP_TEST_OBSERVATIONS": "1",            # [TEST-EVENT] telemetry
    "RECOMP_FE_RESULTS": "1",                   # [FE-RESULT] lines: the summary's controller ids
    "RECOMP_ACCOUNT_SERVER": "127.0.0.1:9",     # never the live account service / leaderboard
    "RECOMP_NETPLAY_SERVER_EU": "127.0.0.1:9",  # never the live relays (also not their pings)
    "RECOMP_NETPLAY_SERVER_NA": "127.0.0.1:9",
    "RECOMP_NO_UPDATE_CHECK": "1",
}

LOCAL_HOSTS = ("127.0.0.1", "localhost", "::1", "[::1]")
ADDRESS_VARS = ("RECOMP_ACCOUNT_SERVER", "RECOMP_NETPLAY_SERVER", "RECOMP_NETPLAY_SERVER_EU", "RECOMP_NETPLAY_SERVER_NA")


def check_env(env):
    """Refuse what the machine rules forbid. Returns a list of problems (empty: fine)."""
    bad = []
    if str(env.get("RECOMP_GUEST_CORES", "")).strip().lower() == "all":
        bad.append("RECOMP_GUEST_CORES=all is not allowed (one guest CPU per copy)")
    for k in ADDRESS_VARS:
        v = str(env.get(k, "")).strip()
        if not v or v.lower() in ("auto", "eu", "na", "both"):
            continue
        host = v.rsplit(":", 1)[0] if v.count(":") == 1 else v
        if host.lower() not in LOCAL_HOSTS:
            bad.append("%s=%s is not a local address" % (k, v))
    if env.get("RECOMP_NETPLAY_SERVER", "").lower() in ("auto", "eu", "na", "both"):
        for k in ("RECOMP_NETPLAY_SERVER_EU", "RECOMP_NETPLAY_SERVER_NA"):
            if not env.get(k):
                bad.append("RECOMP_NETPLAY_SERVER=%s needs a local %s" % (env["RECOMP_NETPLAY_SERVER"], k))
    return bad


def safe_env(core, data_root, extra=None):
    """The environment of one test copy: no inherited RECOMP_ switches, the safe defaults, one guest
    CPU, its data root, then `extra` (checked)."""
    e = {k: v for k, v in os.environ.items() if not k.upper().startswith("RECOMP_")}
    e.update(BASE_ENV)
    e["RECOMP_GUEST_CORE"] = str(int(core))
    e["DEFJAM_DATA"] = data_root
    for k, v in (extra or {}).items():
        if v is None:
            e.pop(str(k), None)         # None takes a default out (e.g. a visible window)
        else:
            e[str(k)] = str(v)
    bad = check_env(e)
    if bad:
        raise ValueError("; ".join(bad))
    return e


def preflight(exe, data_root, cwd=REPO):
    """What would stop a copy from starting cleanly -- checked before it starts, so the game's own
    'game files were not found' dialog never appears. Returns a list of problems."""
    bad = []
    if not os.path.isfile(exe):
        bad.append("not built: %s (scripts/build.ps1 -Preset win-x64-release)" % exe)
    if not os.path.isfile(os.path.join(cwd, "game", "default.xbe")):
        bad.append("%s missing: make the 'game' junction to <data>\\extracted" % os.path.join(cwd, "game", "default.xbe"))
    if not os.path.isfile(os.path.join(data_root, "extracted", "default.xbe")):
        bad.append("data root %s has no extracted\\default.xbe" % data_root)
    if not os.path.isdir(os.path.join(data_root, "save")):
        bad.append("data root %s has no save folder" % data_root)
    return bad


def free_port(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind(("127.0.0.1", int(port)))
        return True
    except OSError:
        return False
    finally:
        s.close()
