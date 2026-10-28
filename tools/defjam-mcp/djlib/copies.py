"""A test session: the machine lock, managed data roots, a local relay and 1..4 game copies.

Session.open() takes the lock (waiting for other agents and for the owner's game to close) and
Session.close() undoes everything it did, in this order: the copies and relays it started (by PID,
nothing else), its data roots (junctions first), the lock. close() also runs at exit.
"""
import atexit
import base64
import ctypes
import io
import json
import os
import socket
import subprocess
import time

from . import machine, roots

LOGS = os.path.join(machine.REPO, "logs", "dj")


class LogWatch:
    """Reads a growing log incrementally; remembers when each watched text first appeared and how
    often (host seconds since the session start), so load and boot times come for free."""

    def __init__(self, path, t0):
        self.path, self.t0, self.pos, self.buf = path, t0, 0, b""
        self.lines = 0
        self.counts = {}
        self.first = {}
        self.last_change = time.time()
        self.watch = []

    def poll(self):
        try:
            with open(self.path, "rb") as f:
                f.seek(self.pos)
                data = f.read()
        except OSError:
            return
        if not data:
            return
        self.pos += len(data)
        self.buf += data
        *done, self.buf = self.buf.split(b"\n")
        now = round(time.time() - self.t0, 1)
        for raw in done:
            l = raw.decode("utf-8", "replace")
            self.lines += 1
            if l.startswith("[NETPLAY]") or l.startswith("[FUNCCALL]") or l.startswith("[TEST-EVENT]") or l.startswith("[AUTOMATCH]"):
                self.last_change = time.time()
            for w in self.watch:
                if w in l:
                    self.counts[w] = self.counts.get(w, 0) + 1
                    self.first.setdefault(w, now)
                    self.first[w + "#%d" % self.counts[w]] = now

    def count(self, text):
        return self.counts.get(text, 0)


class KillOnClose:
    """A Windows job object with KILL_ON_JOB_CLOSE: when this Python process dies -- however -- the
    copies and relays it started die with it, so a crashed test never leaves a game running."""

    def __init__(self):
        self.h = None
        if not machine.WINDOWS:
            return
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateJobObjectW.restype = ctypes.c_void_p
        h = k32.CreateJobObjectW(None, None)
        if not h:
            return

        class BASIC(ctypes.Structure):
            _fields_ = [("a", ctypes.c_int64), ("b", ctypes.c_int64), ("LimitFlags", ctypes.c_uint32),
                        ("c", ctypes.c_size_t), ("d", ctypes.c_size_t), ("e", ctypes.c_uint32),
                        ("f", ctypes.c_size_t), ("g", ctypes.c_uint32), ("h", ctypes.c_uint32)]

        class IOC(ctypes.Structure):
            _fields_ = [(n, ctypes.c_uint64) for n in ("r", "w", "o", "rt", "wt", "ot")]

        class EXT(ctypes.Structure):
            _fields_ = [("basic", BASIC), ("io", IOC), ("pm", ctypes.c_size_t), ("jm", ctypes.c_size_t),
                        ("ppm", ctypes.c_size_t), ("pjm", ctypes.c_size_t)]
        info = EXT()
        info.basic.LimitFlags = 0x2000          # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if k32.SetInformationJobObject(ctypes.c_void_p(h), 9, ctypes.byref(info), ctypes.sizeof(info)):
            self.h = h
        self.k32 = k32

    def add(self, proc):
        if not self.h:
            return False
        k32 = self.k32
        k32.OpenProcess.restype = ctypes.c_void_p
        ph = k32.OpenProcess(0x0101 | 0x1000, False, proc.pid)   # SET_QUOTA | TERMINATE | QUERY_LIMITED
        ok = bool(ph) and bool(k32.AssignProcessToJobObject(ctypes.c_void_p(self.h), ctypes.c_void_p(ph)))
        if ph:
            k32.CloseHandle(ctypes.c_void_p(ph))
        return ok


class Copy:
    def __init__(self, index, proc, port, root, log, watch):
        self.index, self.proc, self.port, self.root, self.log, self.watch = index, proc, port, root, log, watch
        self.sock, self.buf = None, b""

    def alive(self):
        return self.proc.poll() is None


class Session:
    def __init__(self, name="session", preset="win-x64-release", keep_roots=False, lock_timeout=None, log=None,
                 agent=None):
        self.name = name
        self.preset = preset
        self.exe = machine.exe_path(preset)
        self.keep_roots = keep_roots
        self.lock = machine.MachineLock(note=name, log=log, agent=agent)
        self.lock_timeout = lock_timeout
        self.copies = {}
        self.relays = []
        self.roots = []
        self.t0 = time.time()
        self.dir = None
        self.log = log or (lambda m: None)
        self.closed = False
        self.job = None

    # ---- lifetime ----
    def open(self):
        if not self.lock.acquire(timeout=self.lock_timeout):
            raise TimeoutError("the machine stayed busy for %s s" % self.lock_timeout)
        atexit.register(self.close)
        self.job = KillOnClose()
        self.t0 = time.time()
        self.dir = os.path.join(LOGS, time.strftime("%Y%m%d-%H%M%S-") + self.name)
        os.makedirs(self.dir, exist_ok=True)
        return self

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc):
        self.close()
        return False

    def close(self):
        if self.closed:
            return {"stopped": []}
        stopped = []
        for i, c in list(self.copies.items()):
            try:
                if c.sock:
                    c.sock.close()
            except OSError:
                pass
            if c.alive():
                c.proc.kill()
                stopped.append(i)
            try:
                c.proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                pass
        for p in self.relays:
            if p.poll() is None:
                p.kill()
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                pass
        self.relays = []
        removed = []
        if not self.keep_roots:
            for r in self.roots:
                for attempt in range(5):
                    try:
                        roots.destroy(r)
                        removed.append(r)
                        break
                    except PermissionError:
                        break
                    except OSError:
                        time.sleep(1.0)     # a just-killed copy can hold files for a moment
            self.roots = []
        self.lock.release()
        self.closed = True
        return {"stopped": stopped, "roots_removed": len(removed)}

    def elapsed(self):
        return round(time.time() - self.t0, 1)

    # ---- the pieces ----
    def relay(self, port=27900, rollback=False, extra=()):
        if not os.path.isfile(machine.relay_path()):
            raise FileNotFoundError("no local relay: go build -o netplay/relay/dist/relay.exe ./cmd/relay (in netplay/relay)")
        if not machine.free_port(port):
            raise OSError("relay port %d is taken" % port)
        args = [machine.relay_path(), "-listen", "127.0.0.1:%d" % port, "-log-interval", "30s"]
        if rollback:
            args.append("-rollback")
        args += list(extra)
        tag = "relay%d" % len(self.relays)
        p = subprocess.Popen(args, stdout=open(os.path.join(self.dir, tag + ".out"), "wb"),
                             stderr=open(os.path.join(self.dir, tag + ".err"), "wb"),
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.relays.append(p)
        if self.job:
            self.job.add(p)
        time.sleep(0.5)
        return port

    def make_root(self, i, fixture=None, partitions=False):
        r = roots.create("%s%d" % (self.name[:12], i), fixture=fixture, partitions=partitions)
        self.roots.append(r)
        return r

    def launch(self, i, env=None, root=None, core=None, control_port=None, watch=(), fixture=None):
        """Start copy i on its own data root and CPU. Refuses before starting when anything is missing."""
        if self.closed or not self.lock.held:
            raise RuntimeError("open the session (take the lock) first")
        root = root or self.make_root(i, fixture=fixture)
        bad = machine.preflight(self.exe, root)
        if bad:
            raise FileNotFoundError("; ".join(bad))
        core = 2 + 2 * i if core is None else core
        if core >= (os.cpu_count() or 4):
            core = (os.cpu_count() or 4) - 1
        extra = dict(env or {})
        port = control_port or (4900 + i)
        extra.setdefault("RECOMP_CONTROL_PORT", str(port))
        if i:
            extra.setdefault("RECOMP_AUDIO_MUTE", "1")
        e = machine.safe_env(core, root, extra)
        log = os.path.join(self.dir, "copy%d.log.err" % i)
        with open(os.path.join(self.dir, "copy%d.env.json" % i), "w", encoding="utf-8") as f:
            json.dump({k: v for k, v in e.items() if k.startswith("RECOMP_") or k == "DEFJAM_DATA"}, f, indent=1)
        p = subprocess.Popen([self.exe], cwd=machine.REPO, env=e,
                             stdout=open(os.path.join(self.dir, "copy%d.log" % i), "wb"), stderr=open(log, "wb"))
        if self.job:
            self.job.add(p)
        w = LogWatch(log, self.t0)
        w.watch = list(watch)
        self.copies[i] = Copy(i, p, int(e["RECOMP_CONTROL_PORT"]), root, log, w)
        return self.copies[i]

    def poll(self):
        for c in self.copies.values():
            c.watch.poll()

    # ---- the control channel (src/hooks/test_control.c) ----
    def control(self, i, line, timeout=5.0):
        c = self.copies.get(int(i))
        if not c:
            return {"error": "no copy %s" % i}
        if not c.alive():
            return {"error": "copy %s exited with %s" % (i, c.proc.returncode)}
        for attempt in range(2):
            try:
                if not c.sock:
                    c.sock = socket.create_connection(("127.0.0.1", c.port), timeout=timeout)
                    c.buf = b""
                c.sock.sendall((line + "\n").encode())
                deadline = time.time() + timeout
                while b"\n" not in c.buf and time.time() < deadline:
                    chunk = c.sock.recv(65536)
                    if not chunk:
                        raise OSError("closed")
                    c.buf += chunk
                text, _, c.buf = c.buf.partition(b"\n")
                return json.loads(text.decode("utf-8", "replace"))
            except (OSError, ValueError) as ex:
                try:
                    if c.sock:
                        c.sock.close()
                except OSError:
                    pass
                c.sock = None
                if attempt:
                    return {"error": "control channel not ready on port %d (%s)" % (c.port, ex)}
                time.sleep(0.3)

    def screenshot(self, i, path=None, scale=0.5):
        """The copy's own window (PrintWindow on its HWND, never the desktop) as PNG. Returns (path or
        base64, error)."""
        c = self.copies.get(int(i))
        if not c:
            return None, "no copy %s" % i
        return window_png(c.proc.pid, path, scale)


def window_png(pid, path=None, scale=0.5):
    try:
        from PIL import Image
    except ImportError:
        return None, "Pillow missing"
    user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
    found = []

    class RECT(ctypes.Structure):
        _fields_ = [("l", ctypes.c_long), ("t", ctypes.c_long), ("r", ctypes.c_long), ("b", ctypes.c_long)]

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def each(hwnd, _):
        p = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(ctypes.c_void_p(hwnd), ctypes.byref(p))
        if p.value == pid:
            r = RECT()
            user32.GetClientRect(ctypes.c_void_p(hwnd), ctypes.byref(r))
            found.append((r.r * r.b, hwnd))       # the game's window is the largest of its windows
        return True
    user32.EnumWindows(each, 0)
    if not found:
        return None, "no window yet"
    hwnd = ctypes.c_void_p(max(found)[1])
    rc = RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rc))
    w, h = max(1, rc.r), max(1, rc.b)
    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 3)

    class BMI(ctypes.Structure):
        _fields_ = [("size", ctypes.c_uint32), ("w", ctypes.c_int32), ("h", ctypes.c_int32), ("planes", ctypes.c_uint16),
                    ("bits", ctypes.c_uint16), ("comp", ctypes.c_uint32), ("img", ctypes.c_uint32), ("xp", ctypes.c_int32),
                    ("yp", ctypes.c_int32), ("used", ctypes.c_uint32), ("imp", ctypes.c_uint32)]
    bmi = BMI(ctypes.sizeof(BMI), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    img = Image.frombuffer("RGBA", (w, h), buf.raw, "raw", "BGRA", 0, 1).convert("RGB")
    if scale and float(scale) != 1.0:
        img = img.resize((max(1, int(w * float(scale))), max(1, int(h * float(scale)))))
    if path:
        img.save(path, "PNG")
        return path, None
    out = io.BytesIO()
    img.save(out, "PNG")
    return base64.b64encode(out.getvalue()).decode(), None
