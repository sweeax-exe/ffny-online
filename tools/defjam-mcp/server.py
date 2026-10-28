#!/usr/bin/env python3
"""defjam-mcp: drive test copies of the game live, as an MCP server over stdio.

Claude Code starts it from the repository's .mcp.json. Tools:

  launch      start 1..4 copies (optionally online: a local relay, quick match), each with the
              live control channel (src/hooks/test_control.c) and virtual pads
  state       one copy's screen, last front-end calls, match (phase, clock, fighters' health) and
              online status
  press       press / hold / release controls on a copy's virtual pad
  screenshot  the copy's window as a picture
  log         lines of a copy's log, filtered
  wait_for    wait until a copy's log has a line containing a text
  to_menu     from wherever the copy is (title, legal screens) to the main menu
  wait_state  wait until a state field meets a condition (e.g. match.phase == 1)
  inject      pad bits for a fighter on exact fight steps (offline; frame-accurate tests)
  match       start any match at once (mode, venue, fighters, CPUs) from the main menu
  fe          run a front-end command (Game.*, Controller.*, ...) on the game thread
  peek/poke   read / write guest memory
  close       stop every copy and the relay this server started

Copies never touch the player's save: each runs on its own data folder beside the main one
(<data>-mcp<N>: the dump and the generated folders as junctions, a fresh copy of the saves), and
never reports to the live account service. Standard library only, plus Pillow for pictures.
"""
import base64
import ctypes
import io
import json
import os
import shutil
import socket
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.environ.get("DEFJAM_DATA") or r"D:\DefJam\data"
EXE = os.path.join(REPO, "build", "win-x64-release", "defjam_recomp.exe")
RELAY = os.path.join(REPO, "netplay", "relay", "dist", "relay.exe")
LOGS = os.path.join(REPO, "logs", "mcp")
BASE_PORT = 4900
INTRO_MOVIES = r"movies\eagames.mad;movies\thx.mad;movies\intro.mad"
LINKED = ("extracted", "override", "online-art", "mods")

COPIES = {}         # index -> dict(proc, port, log, sock)
RELAY_PROC = None


# ---- the copies -----------------------------------------------------------------------------

def prepare_root(i):
    root = DATA.rstrip("\\/") + "-mcp%d" % i
    os.makedirs(root, exist_ok=True)
    for name in LINKED:
        src, dst = os.path.join(DATA, name), os.path.join(root, name)
        if os.path.isdir(src) and not os.path.exists(dst):
            subprocess.run(["cmd", "/c", "mklink", "/J", dst, src], capture_output=True)
    save = os.path.join(root, "save")
    if os.path.isdir(save):
        shutil.rmtree(save)
    if os.path.isdir(os.path.join(DATA, "save")):
        shutil.copytree(os.path.join(DATA, "save"), save)
    return root


def launch(copies=1, online=False, quick="0,2", fighters=None, pads=1, fast_ko=0, env=None, visible=False,
           ko_step=0, ko_winner="alt", bot=0, relay_port=27777, base_core=2):
    global RELAY_PROC
    close()
    if not os.path.isfile(EXE):
        return {"error": "not built: " + EXE}
    os.makedirs(LOGS, exist_ok=True)
    copies = max(1, min(4, int(copies)))
    fighters = fighters or [55, 22, 9, 31]
    if online:
        RELAY_PROC = subprocess.Popen([RELAY, "-listen", "127.0.0.1:%d" % int(relay_port), "-log-interval", "30s"],
                                      stdout=open(os.path.join(LOGS, "relay.out"), "wb"),
                                      stderr=open(os.path.join(LOGS, "relay.err"), "wb"),
                                      creationflags=subprocess.CREATE_NO_WINDOW)
        time.sleep(0.5)
    started = []
    for i in range(copies):
        e = {k: v for k, v in os.environ.items() if not k.upper().startswith("RECOMP_")}
        e.update({
            "RECOMP_VBLANK": "1", "RECOMP_PB_EXEC": "1", "RECOMP_PB_D3D11": "1", "RECOMP_USB": "1",
            "RECOMP_SETTINGS": "none", "RECOMP_SKIP_FILES": INTRO_MOVIES,
            "RECOMP_TEST_OBSERVATIONS": "1", "RECOMP_ACCOUNT_SERVER": "127.0.0.1:9",
            "RECOMP_INPUT_HOST": "1", "RECOMP_INPUT_IGNORE_FOCUS": "1", "RECOMP_PAD_HOST": "1",
            "RECOMP_INPUT_NO_PADS": "1", "RECOMP_INPUT_VIRTUAL_PADS": str(max(1, min(4, int(pads)))),
            "RECOMP_CONTROL_PORT": str(BASE_PORT + i),
            "DEFJAM_DATA": prepare_root(i),
        })
        if not visible:
            e["RECOMP_WINDOW_BACKGROUND"] = "1"
        if i:
            e["RECOMP_AUDIO_MUTE"] = "1"
        # each copy's guest threads on a CPU of their own (2, 4, ...), as on a player's PC: two copies
        # on CPU 2 starve each other, and spreading them (RECOMP_GUEST_CORES=all) runs the game thread
        # beside the mixer, which rollback's page attribution assumes cannot happen
        e["RECOMP_GUEST_CORE"] = str(int(base_core) + 2 * i)
        if fast_ko:
            e["RECOMP_TEST_FAST_KO"] = str(int(fast_ko))
        if ko_step:     # the game knocks the loser out at that fight step (src/hooks/test_telemetry.c)
            e["RECOMP_TEST_KO_STEP"] = "%d,%s" % (int(ko_step), ko_winner)
        if bot:         # the state-reading bot drives the copy's fighter (src/hooks/test_bot.c)
            e["RECOMP_TEST_BOT"] = "1,%d" % max(0, min(100, int(bot)))
        if online:
            e.update({"RECOMP_NETPLAY": "quick", "RECOMP_NETPLAY_SERVER": "127.0.0.1:%d" % int(relay_port),
                      "RECOMP_NETPLAY_QUICK": quick, "RECOMP_NETPLAY_FIGHTER": str(fighters[i % len(fighters)])})
        for k, v in (env or {}).items():
            e[str(k)] = str(v)
        log = os.path.join(LOGS, "copy%d.log.err" % i)
        p = subprocess.Popen([EXE], cwd=REPO, env=e, stdout=open(os.path.join(LOGS, "copy%d.log" % i), "wb"),
                             stderr=open(log, "wb"))
        COPIES[i] = {"proc": p, "port": BASE_PORT + i, "log": log, "sock": None}
        started.append({"copy": i, "pid": p.pid, "port": BASE_PORT + i, "log": log})
        if online and i == 0:
            time.sleep(3)       # the first copy opens the room and is player 1
    return {"started": started, "online": bool(online)}


def close():
    global RELAY_PROC
    stopped = []
    for i, c in list(COPIES.items()):
        try:
            if c["sock"]:
                c["sock"].close()
        except OSError:
            pass
        if c["proc"].poll() is None:
            c["proc"].kill()
            stopped.append(i)
        del COPIES[i]
    if RELAY_PROC and RELAY_PROC.poll() is None:
        RELAY_PROC.kill()
    RELAY_PROC = None
    return {"stopped": stopped}


def control(i, line, timeout=5.0):
    c = COPIES.get(int(i))
    if not c:
        return {"error": "no copy %s (launch first)" % i}
    if c["proc"].poll() is not None:
        return {"error": "copy %s exited with %s" % (i, c["proc"].returncode)}
    for attempt in range(2):
        try:
            if not c["sock"]:
                c["sock"] = socket.create_connection(("127.0.0.1", c["port"]), timeout=timeout)
                c["buf"] = b""
            c["sock"].sendall((line + "\n").encode())
            deadline = time.time() + timeout
            while b"\n" not in c["buf"] and time.time() < deadline:
                chunk = c["sock"].recv(65536)
                if not chunk:
                    raise OSError("closed")
                c["buf"] += chunk
            text, _, c["buf"] = c["buf"].partition(b"\n")
            return json.loads(text.decode("utf-8", "replace"))
        except (OSError, ValueError) as ex:
            try:
                if c["sock"]:
                    c["sock"].close()
            except OSError:
                pass
            c["sock"] = None
            if attempt:
                return {"error": "control channel not ready on port %d (%s); the copy may still be starting"
                        % (c["port"], ex)}
            time.sleep(0.3)


def read_log(i, grep=None, last=40):
    c = COPIES.get(int(i))
    if not c:
        return {"error": "no copy %s" % i}
    with open(c["log"], "rb") as f:
        lines = f.read().decode("utf-8", "replace").splitlines()
    noise = ("[NV2A]", "[TRANS]", "[GPU]", "[READ]", "[KERNEL]", "[D3D]", "[KTRAIL]", "[OHCI")
    lines = [l for l in lines if not l.strip().startswith(noise)]
    if grep:
        lines = [l for l in lines if grep.lower() in l.lower()]
    return {"lines": lines[-int(last):], "total": len(lines)}


def wait_for(i, text, timeout=120):
    c = COPIES.get(int(i))
    if not c:
        return {"error": "no copy %s" % i}
    deadline = time.time() + float(timeout)
    needle = text.lower().encode()
    while time.time() < deadline:
        if c["proc"].poll() is not None:
            return {"found": False, "error": "copy exited with %s" % c["proc"].returncode}
        with open(c["log"], "rb") as f:
            data = f.read()
        k = data.lower().rfind(needle)
        if k >= 0:
            start = data.rfind(b"\n", 0, k) + 1
            end = data.find(b"\n", k)
            return {"found": True, "line": data[start:end if end > 0 else None].decode("utf-8", "replace"),
                    "after_s": round(float(timeout) - (deadline - time.time()), 1)}
        time.sleep(0.5)
    return {"found": False, "timeout_s": timeout}


def get_path(obj, path):
    for part in path.split("."):
        if isinstance(obj, list):
            obj = obj[int(part)] if int(part) < len(obj) else None
        elif isinstance(obj, dict):
            obj = obj.get(part)
        else:
            return None
        if obj is None:
            return None
    return obj


def compare(have, op, want):
    try:
        if isinstance(have, str) and have.lower().startswith("0x") and str(want).lower().startswith("0x"):
            have, want = int(have, 16), int(str(want), 16)
        elif not isinstance(have, str):
            want = type(have)(want) if not isinstance(have, bool) else str(want).lower() in ("1", "true")
    except (TypeError, ValueError):
        pass
    if op == "contains":
        return str(want).lower() in str(have).lower()
    return {"==": have == want, "!=": have != want, ">": have > want, ">=": have >= want,
            "<": have < want, "<=": have <= want}[op]


def wait_state(i, path, op, value, timeout=60):
    deadline = time.time() + float(timeout)
    have = None
    while time.time() < deadline:
        s = control(i, "state")
        if "error" in s:
            time.sleep(0.5)
            continue
        have = get_path(s, path)
        if have is not None and compare(have, op, value):
            return {"met": True, "value": have, "match": s.get("match")}
        time.sleep(0.25)
    return {"met": False, "last": have}


def to_menu(i, timeout=180):
    deadline = time.time() + float(timeout)
    k = 0
    while time.time() < deadline:
        s = control(i, "state")
        if "error" not in s:
            if "mainmenu" in s.get("screen", "").lower():
                return {"ok": True, "screen": s["screen"]}
            control(i, "press 0 %s 200" % ("start" if k % 3 == 0 else "a"))
            k += 1
        time.sleep(1.2)
    return {"ok": False, "error": "not at the main menu after %ss" % timeout}


# ---- pictures --------------------------------------------------------------------------------

def screenshot(i, scale=0.5):
    from PIL import Image
    c = COPIES.get(int(i))
    if not c:
        return None, "no copy %s" % i
    user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
    pid = c["proc"].pid
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
    out = io.BytesIO()
    img.save(out, "PNG")
    return base64.b64encode(out.getvalue()).decode(), None


# ---- MCP over stdio --------------------------------------------------------------------------

TOOLS = [
    {"name": "launch", "description": "Start 1..4 test copies of the built game (closes earlier ones). online=true adds "
     "a local relay and puts the copies into one quick-match room (quick='mode,players', e.g. '0,2' One on One, "
     "'3,2' Tag Team with CPUs). pads = virtual pads per copy (2 for an offline versus). fast_ko = health percent "
     "every fighter starts a match with (e.g. 5); the fighter at zero health still has to be finished. ko_step = the game "
     "knocks the loser out at that fight step of every match (ko_winner: a slot, or 'alt' to alternate). bot = the "
     "state-reading test bot drives each copy's fighter, with that aggression 1..100. relay_port and base_core (copy i "
     "runs on CPU base_core + 2i) keep a second test apart from another one. env = extra RECOMP_ variables.",
     "inputSchema": {"type": "object", "properties": {
         "copies": {"type": "integer"}, "online": {"type": "boolean"}, "quick": {"type": "string"},
         "fighters": {"type": "array", "items": {"type": "integer"}}, "pads": {"type": "integer"},
         "fast_ko": {"type": "integer"}, "env": {"type": "object"}, "visible": {"type": "boolean"},
         "ko_step": {"type": "integer"}, "ko_winner": {"type": "string"}, "bot": {"type": "integer"},
         "relay_port": {"type": "integer"}, "base_core": {"type": "integer"}}}},
    {"name": "state", "description": "A copy's live state: current screen, the last front-end calls, the match (phase "
     "0 start/1 fighting/2 decided/3 done, clock, type, fighters with health) and the online status.",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}}}},
    {"name": "press", "description": "Press controls on a copy's virtual pad. controls: names joined with + (a, b, x, y, "
     "start, back, black, white, left_trigger, right_trigger, dpad_up/down/left/right, left_stick_up/down/left/right, "
     "right_stick_..., left_thumb, right_thumb). mode: press (for ms, default 120), hold, release ('all' releases all).",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "slot": {"type": "integer"},
                     "controls": {"type": "string"}, "ms": {"type": "integer"}, "mode": {"type": "string"}},
                     "required": ["controls"]}},
    {"name": "screenshot", "description": "The copy's window as a PNG (scale 0.1..1, default 0.5).",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "scale": {"type": "number"}}}},
    {"name": "log", "description": "The last lines of a copy's log without GPU/kernel noise, optionally filtered by text.",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "grep": {"type": "string"},
                     "last": {"type": "integer"}}}},
    {"name": "wait_for", "description": "Wait until a copy's log has a line containing text (case-insensitive), e.g. "
     "'Match over', 'GetScreenInfo(intMain/mainMenu', '[CRASH]'. Returns that line.",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "text": {"type": "string"},
                     "timeout": {"type": "number"}}, "required": ["text"]}},
    {"name": "to_menu", "description": "Take a copy from the title / legal / memory-card screens to the main menu.",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "timeout": {"type": "number"}}}},
    {"name": "wait_state", "description": "Wait until a field of the copy's state meets a condition. path is dotted "
     "(match.phase, match.step, match.fighters.1.state, online.lockstep.step, screen), op one of == != > >= < <= "
     "contains, value compared as the field's type (hex strings like 0x37 compare as numbers).",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "path": {"type": "string"},
                     "op": {"type": "string"}, "value": {}, "timeout": {"type": "number"}},
                     "required": ["path", "op", "value"]}},
    {"name": "inject", "description": "Frame-accurate input for offline tests: from `start` steps after the current fight "
     "step, for `steps` steps, fighter `slot` reads these pad-table bits instead of its pad (hex: 0x1 up? 0x8 right, 0x2, "
     "0x4 directions; 0x10 0x20 0x40 0x80 0x1000 0x2000 the six action buttons; 0x10000 left stick active). "
     "clear=true drops every interval. Online matches use their own lockstep input; use press there.",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "slot": {"type": "integer"},
                     "start": {"type": "integer"}, "steps": {"type": "integer"}, "bits": {"type": "string"},
                     "clear": {"type": "boolean"}}}},
    {"name": "match", "description": "Start a match on a copy as soon as it is at the main menu, without the menus: "
     "spec like 'type=0,venue=6,f0=58,p0=0,f1=40,p1=-1' (type 0 One on One, 3 Tag Team, ...; venue = venue.xml feID; "
     "fN = Battle-mode fighter ID (fighter.xml ID; Teck 85, Santos 93, O.E 92, Eric Sermon 91); pN = controller port, -1 for the CPU; up to f3/p3).",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "spec": {"type": "string"}},
                     "required": ["spec"]}},
    {"name": "fe", "description": "Run one front-end command on a copy's game thread at the front end's next call "
     "(e.g. 'Game.GetMatchInfo()', 'Control.GetScreenInfo(intMain/mainMenu.swf)'); the result is a [CONTROL] log line.",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "command": {"type": "string"}},
                     "required": ["command"]}},
    {"name": "peek", "description": "Read guest memory: address (e.g. 0x3B92E0), bytes up to 512. Returns hex.",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "address": {"type": "string"},
                     "bytes": {"type": "integer"}}, "required": ["address"]}},
    {"name": "memdiff", "description": "Which 4 KB pages of guest RAM change: call with phase 'begin', let the game "
     "run, then 'end'. Returns the changed ranges [address, KB] and the totals.",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "phase": {"type": "string"}},
                     "required": ["phase"]}},
    {"name": "poke", "description": "Write one aligned 32-bit word of guest memory (tests only).",
     "inputSchema": {"type": "object", "properties": {"copy": {"type": "integer"}, "address": {"type": "string"},
                     "value": {"type": "string"}}, "required": ["address", "value"]}},
    {"name": "close", "description": "Stop every copy and the relay this server started.",
     "inputSchema": {"type": "object", "properties": {}}},
]


def call(name, a):
    copy = a.get("copy", 0)
    if name == "launch":
        return [text(launch(a.get("copies", 1), a.get("online", False), a.get("quick", "0,2"), a.get("fighters"),
                            a.get("pads", 1), a.get("fast_ko", 0), a.get("env"), a.get("visible", False),
                            a.get("ko_step", 0), a.get("ko_winner", "alt"), a.get("bot", 0),
                            a.get("relay_port", 27777), a.get("base_core", 2)))]
    if name == "state":
        return [text(control(copy, "state"))]
    if name == "press":
        mode = a.get("mode", "press")
        line = "%s %d %s" % (mode, int(a.get("slot", 0)), a.get("controls", "all"))
        if mode == "press":
            line += " %d" % int(a.get("ms", 120))
        return [text(control(copy, line))]
    if name == "screenshot":
        data, err = screenshot(copy, a.get("scale", 0.5))
        if err:
            return [text({"error": err})]
        return [{"type": "image", "data": data, "mimeType": "image/png"}]
    if name == "log":
        return [text(read_log(copy, a.get("grep"), a.get("last", 40)))]
    if name == "wait_for":
        return [text(wait_for(copy, a["text"], a.get("timeout", 120)))]
    if name == "to_menu":
        return [text(to_menu(copy, a.get("timeout", 180)))]
    if name == "wait_state":
        return [text(wait_state(copy, a["path"], a["op"], a["value"], a.get("timeout", 60)))]
    if name == "inject":
        if a.get("clear"):
            return [text(control(copy, "inject clear"))]
        return [text(control(copy, "inject %d %d %d %s" % (int(a.get("slot", 0)), int(a.get("start", 0)),
                                                          int(a.get("steps", 1)), a.get("bits", "0"))))]
    if name == "match":
        return [text(control(copy, "match " + a["spec"]))]
    if name == "fe":
        return [text(control(copy, "fe " + a["command"]))]
    if name == "peek":
        return [text(control(copy, "peek %s %d" % (a["address"], int(a.get("bytes", 64)))))]
    if name == "memdiff":
        return [text(control(copy, "memdiff " + a.get("phase", "begin"), timeout=30))]
    if name == "poke":
        return [text(control(copy, "poke %s %s" % (a["address"], a["value"])))]
    if name == "close":
        return [text(close())]
    raise ValueError("unknown tool " + name)


def text(obj):
    return {"type": "text", "text": json.dumps(obj, ensure_ascii=False)}


def main():
    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        msg = json.loads(raw)
        mid, method = msg.get("id"), msg.get("method")
        if mid is None:
            continue        # notifications
        try:
            if method == "initialize":
                result = {"protocolVersion": msg.get("params", {}).get("protocolVersion", "2024-11-05"),
                          "capabilities": {"tools": {}}, "serverInfo": {"name": "defjam", "version": "1"}}
            elif method == "tools/list":
                result = {"tools": TOOLS}
            elif method == "tools/call":
                p = msg.get("params", {})
                try:
                    result = {"content": call(p.get("name"), p.get("arguments") or {})}
                except Exception as ex:     # a tool error goes back to the model, not over the protocol
                    result = {"content": [text({"error": str(ex)})], "isError": True}
            elif method == "ping":
                result = {}
            else:
                raise KeyError(method)
            reply = {"jsonrpc": "2.0", "id": mid, "result": result}
        except KeyError:
            reply = {"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": "unknown method"}}
        sys.stdout.write(json.dumps(reply) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    try:
        main()
    finally:
        close()
