#!/usr/bin/env python3
"""defjam-mcp v2: drive test copies of the game, as an MCP server over stdio.

Claude Code starts it from the repository's .mcp.json. The same features work from Bash through
tools/defjam-mcp/dj.py (subagents often have no MCP tools); both use djlib/.

High level (one compact JSON result each, the main token saver):
  scenario    online / offline / boot / gallery: runs the whole test under the machine lock and
              answers with one line per match (start, front-end order, hits, summary, checksums,
              desync detail, crash lines with sub_ names, load times, log and picture paths)
  digest      the key events of any game log (also a player's F9 log) in <= ~60 lines
  analyze     the online result of existing copy logs (e.g. a netplay-hub-test.ps1 run)
  lock_status who holds the machine (test-lock.txt, test-priority.txt, running copies)

Live driving (launch first; it waits for the machine lock and makes disposable data roots):
  launch, state, press, screenshot, log, wait_for, to_menu, wait_state, inject, match, fe,
  peek, poke, memdiff, save_state / load_state (the fight snapshot of the rollback code), close

Copies never touch the player's save or the dump (each runs on its own data root), never report to
the live account service or relays, and stop with this server. Standard library only (Pillow for
pictures).
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from djlib import copies as djcopies, logparse, machine, roots, scenarios  # noqa: E402

REPO = machine.REPO
DATA = machine.DATA
EXE = machine.exe_path()
RELAY = machine.relay_path()
LOGS = os.path.join(REPO, "logs", "mcp")
BASE_PORT = 4900
INTRO_MOVIES = machine.INTRO_MOVIES

SESSION = None      # djcopies.Session of the live tools
COPIES = {}         # index -> {"proc", "port", "log", "sock"} (also used by scripts/play-vs-bot.py)
RELAY_PROC = None   # scripts/play-vs-bot.py starts its own relay here


def log_stderr(msg):
    print(msg, file=sys.stderr, flush=True)


# ---- the copies -----------------------------------------------------------------------------

def prepare_root(i):
    """A managed data root for copy i (scripts/play-vs-bot.py)."""
    return roots.create("mcp%d" % i)


def launch(copies=1, online=False, quick="0,2", fighters=None, pads=1, fast_ko=0, env=None, visible=False,
           ko_step=0, ko_winner="alt", bot=0, relay_port=27777, base_core=2, rollback="off", lock_timeout=900,
           fixture=None):
    global SESSION
    close()
    copies = max(1, min(4, int(copies)))
    fighters = fighters or [55, 22, 9, 31]
    SESSION = djcopies.Session("mcp", lock_timeout=lock_timeout, log=log_stderr)
    try:
        SESSION.open()
    except TimeoutError as ex:
        SESSION = None
        return {"error": str(ex) + " (lock_status says who holds it)"}
    try:
        if online:
            SESSION.relay(int(relay_port), rollback=(rollback == "on"))
        started = []
        for i in range(copies):
            e = {"RECOMP_INPUT_HOST": "1", "RECOMP_INPUT_IGNORE_FOCUS": "1", "RECOMP_PAD_HOST": "1",
                 "RECOMP_INPUT_NO_PADS": "1", "RECOMP_INPUT_VIRTUAL_PADS": str(max(1, min(4, int(pads)))),
                 "RECOMP_CONTROL_PORT": str(BASE_PORT + i)}
            if visible:
                e["RECOMP_WINDOW_BACKGROUND"] = None
            if fast_ko:
                e["RECOMP_TEST_FAST_KO"] = str(int(fast_ko))
            if ko_step:
                e["RECOMP_TEST_KO_STEP"] = "%d,%s" % (int(ko_step), ko_winner)
            if bot:
                e["RECOMP_TEST_BOT"] = "1,%d" % max(0, min(100, int(bot)))
            if online:
                e.update({"RECOMP_NETPLAY": "quick", "RECOMP_NETPLAY_SERVER": "127.0.0.1:%d" % int(relay_port),
                          "RECOMP_NETPLAY_QUICK": quick, "RECOMP_NETPLAY_FIGHTER": str(fighters[i % len(fighters)]),
                          "RECOMP_ROLLBACK": "on" if rollback == "on" else "off"})
            e.update(env or {})
            c = SESSION.launch(i, env=e, core=int(base_core) + 2 * i, fixture=fixture)
            COPIES[i] = {"proc": c.proc, "port": c.port, "log": c.log, "sock": None}
            started.append({"copy": i, "pid": c.proc.pid, "port": c.port, "log": os.path.relpath(c.log, REPO)})
            if online and i == 0:
                time.sleep(3)       # the first copy opens the room and is player 1
        return {"started": started, "online": bool(online), "lock_wait_s": SESSION.lock.waited_s}
    except Exception as ex:     # noqa: BLE001 -- a failed launch leaves nothing behind
        close()
        return {"error": str(ex)}


def close():
    global SESSION, RELAY_PROC
    out = {"stopped": []}
    for i, c in list(COPIES.items()):
        try:
            if c.get("sock"):
                c["sock"].close()
        except OSError:
            pass
        if SESSION is None and c["proc"].poll() is None:     # started outside a session (play-vs-bot)
            c["proc"].kill()
            out["stopped"].append(i)
        del COPIES[i]
    if SESSION is not None:
        out = SESSION.close()
        SESSION = None
    if RELAY_PROC is not None and RELAY_PROC.poll() is None:
        RELAY_PROC.kill()
    RELAY_PROC = None
    return out


def control(i, line, timeout=5.0):
    """One control-channel command (src/hooks/test_control.c) to copy i, one JSON answer."""
    import socket
    c = COPIES.get(int(i))
    if not c:
        return {"error": "no copy %s (launch first)" % i}
    if c["proc"].poll() is not None:
        return {"error": "copy %s exited with %s" % (i, c["proc"].returncode)}
    for attempt in range(2):
        try:
            if not c.get("sock"):
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
                if c.get("sock"):
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
    lines = logparse.read_text(c["log"]).splitlines()
    noise = ("[NV2A]", "[TRANS]", "[GPU]", "[READ]", "[KERNEL]", "[D3D]", "[KTRAIL]", "[OHCI", "[TEST-STATE]")
    lines = [l for l in lines if not l.strip().startswith(noise)]
    if grep:
        lines = [l for l in lines if grep.lower() in l.lower()]
    return {"lines": [l[:220] for l in lines[-int(last):]], "total": len(lines)}


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
            return {"found": True, "line": data[start:end if end > 0 else None].decode("utf-8", "replace")[:220],
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


def screenshot(i, scale=0.5):
    c = COPIES.get(int(i))
    if not c:
        return None, "no copy %s" % i
    return djcopies.window_png(c["proc"].pid, None, scale)


# ---- MCP over stdio --------------------------------------------------------------------------

def _p(**props):
    return {"type": "object", "properties": props}


INT, NUM, STR, BOOL, ANY = {"type": "integer"}, {"type": "number"}, {"type": "string"}, {"type": "boolean"}, {}

TOOLS = [
    {"name": "scenario", "description": "Run a whole test under the machine lock and get one compact JSON result "
     "(ok false on any failure). kind: online (args: matches, mode 1v1|ffa|team|koth, players, fighters [ids], "
     "rollback on|off, fast_ko step (default 600, 0 off), ko_winner, bot (aggression or true), keep_going, "
     "require_hits, timeout), offline (type, venue, f0, f1, cpu (default true), bot, fast_ko, turbo, fixture), "
     "boot, gallery (page_ms). Online checks per match: started (lockstep/rollback, delay), the front-end order "
     "(the screen's ResetMatchData before StartBattleMode), hits landed per slot (telemetry), the summary "
     "accepted (UpdateMatchRewardsPoints, controller ids), checksums equal, desync detail, crashes with sub_ names, "
     "load times; plus log and picture paths. Takes minutes; waits for other agents' tests first.",
     "inputSchema": {"type": "object", "properties": {"kind": STR, "args": {"type": "object"}}, "required": ["kind"]}},
    {"name": "digest", "description": "The key events of a game log in <= ~60 lines: settings/GPU/input, the screens, "
     "one line per online match, waits and gave-ups, desync detail, the crash block with its sub_ and the real "
     "callers (lifted-C filtered), [MEM], [INPUT] removed, [REPORT]. path: a log, or a logs/bug-reports/<id> folder.",
     "inputSchema": {"type": "object", "properties": {"path": STR, "max": INT}, "required": ["path"]}},
    {"name": "analyze", "description": "The online scenario's per-match result for existing copy logs (one per copy, "
     "e.g. from scripts/netplay-hub-test.ps1).",
     "inputSchema": {"type": "object", "properties": {"logs": {"type": "array", "items": STR}, "matches": INT},
                     "required": ["logs"]}},
    {"name": "lock_status", "description": "Who holds the machine: test-lock.txt, test-priority.txt, running game copies.",
     "inputSchema": _p()},
    {"name": "launch", "description": "Start 1..4 live test copies (closes earlier ones). Waits for the machine lock "
     "(lock_timeout s, default 900) and makes a disposable data root per copy (fixture: a folder of save files copied "
     "over it). online=true adds a local relay and puts the copies into one quick-match room (quick='mode,players', "
     "e.g. '0,2' One on One, '64,2' King of the Hill; rollback on|off). pads = virtual pads per copy. ko_step = the "
     "game knocks the loser out at that fight step (ko_winner: a slot or 'alt'); fast_ko = health percent at the start; "
     "bot = the state-reading test bot fights (aggression 1..100). env = extra RECOMP_ variables (checked: local "
     "addresses only, never RECOMP_GUEST_CORES=all).",
     "inputSchema": _p(copies=INT, online=BOOL, quick=STR, fighters={"type": "array", "items": INT}, pads=INT,
                       fast_ko=INT, env={"type": "object"}, visible=BOOL, ko_step=INT, ko_winner=STR, bot=INT,
                       relay_port=INT, base_core=INT, rollback=STR, lock_timeout=NUM, fixture=STR)},
    {"name": "state", "description": "A copy's live state: current screen, the last front-end calls, the match (phase "
     "0 start/1 fighting/2 decided/3 done, clock, type, step, fighters with health, state, position) and online status.",
     "inputSchema": _p(copy=INT)},
    {"name": "press", "description": "Press controls on a copy's virtual pad. controls: names joined with + (a, b, x, y, "
     "start, back, black, white, left_trigger, right_trigger, dpad_up/down/left/right, left_stick_up/down/left/right, "
     "right_stick_..., left_thumb, right_thumb). mode: press (for ms, default 120), hold, release ('all' releases all).",
     "inputSchema": {"type": "object", "properties": {"copy": INT, "slot": INT, "controls": STR, "ms": INT, "mode": STR},
                     "required": ["controls"]}},
    {"name": "screenshot", "description": "The copy's own window as a PNG (scale 0.1..1, default 0.5).",
     "inputSchema": _p(copy=INT, scale=NUM)},
    {"name": "log", "description": "The last lines of a copy's log without GPU/kernel noise, optionally filtered by text. "
     "Prefer digest for an overview.",
     "inputSchema": _p(copy=INT, grep=STR, last=INT)},
    {"name": "wait_for", "description": "Wait until a copy's log has a line containing text (case-insensitive), e.g. "
     "'Match over', 'GetScreenInfo(intMain/mainMenu', '[CRASH]'. Returns that line.",
     "inputSchema": {"type": "object", "properties": {"copy": INT, "text": STR, "timeout": NUM}, "required": ["text"]}},
    {"name": "to_menu", "description": "Take a copy from the title / legal / memory-card screens to the main menu.",
     "inputSchema": _p(copy=INT, timeout=NUM)},
    {"name": "wait_state", "description": "Wait until a field of the copy's state meets a condition. path is dotted "
     "(match.phase, match.step, match.fighters.1.state, online.lockstep.step, screen), op one of == != > >= < <= "
     "contains, value compared as the field's type (hex strings like 0x37 compare as numbers).",
     "inputSchema": {"type": "object", "properties": {"copy": INT, "path": STR, "op": STR, "value": ANY, "timeout": NUM},
                     "required": ["path", "op", "value"]}},
    {"name": "inject", "description": "Frame-accurate input for offline tests: from `start` steps after the current fight "
     "step, for `steps` steps, fighter `slot` reads these pad-table bits instead of its pad (hex: 0x1 up, 0x2 down, "
     "0x4 left, 0x8 right; 0x80 0x40 strikes, 0x20 grab, 0x2000 block, 0x1000 modifier). clear=true drops every "
     "interval. Online matches use their own lockstep input; use press there.",
     "inputSchema": _p(copy=INT, slot=INT, start=INT, steps=INT, bits=STR, clear=BOOL)},
    {"name": "match", "description": "Start a match on a copy as soon as it is at the main menu, without the menus: "
     "spec like 'type=0,venue=6,f0=58,p0=0,f1=40,p1=-1' (type 0 One on One, 1 Tag, 2/3 FFA; venue = venue.xml feID; "
     "fN = fighter.xml ID; pN = controller port, -1 for the CPU; up to f3/p3).",
     "inputSchema": {"type": "object", "properties": {"copy": INT, "spec": STR}, "required": ["spec"]}},
    {"name": "fe", "description": "Run one front-end command on a copy's game thread at the front end's next call "
     "(e.g. 'Game.GetMatchInfo()'); the result is a [CONTROL] log line.",
     "inputSchema": {"type": "object", "properties": {"copy": INT, "command": STR}, "required": ["command"]}},
    {"name": "save_state", "description": "Snapshot the fight at the start of the next fight step (the rollback "
     "code's save; offline test copies only). load_state goes back to it, to replay a moment (e.g. before a desync).",
     "inputSchema": _p(copy=INT)},
    {"name": "load_state", "description": "Go back to the last save_state snapshot at the start of the next fight step.",
     "inputSchema": _p(copy=INT)},
    {"name": "peek", "description": "Read guest memory: address (e.g. 0x3B92E0), bytes up to 512. Returns hex.",
     "inputSchema": {"type": "object", "properties": {"copy": INT, "address": STR, "bytes": INT}, "required": ["address"]}},
    {"name": "memdiff", "description": "Which 4 KB pages of guest RAM change: call with phase 'begin', let the game "
     "run, then 'end'. Returns the changed ranges [address, KB] and the totals.",
     "inputSchema": {"type": "object", "properties": {"copy": INT, "phase": STR}, "required": ["phase"]}},
    {"name": "poke", "description": "Write one aligned 32-bit word of guest memory (tests only).",
     "inputSchema": {"type": "object", "properties": {"copy": INT, "address": STR, "value": STR},
                     "required": ["address", "value"]}},
    {"name": "close", "description": "Stop every copy and relay this server started, delete their data roots and give "
     "the machine lock back.", "inputSchema": _p()},
]


def run_scenario(kind, a):
    a = dict(a or {})
    a.setdefault("log", log_stderr)
    if kind == "online":
        r = scenarios.online(**a)
    elif kind == "offline":
        r = scenarios.offline(**a)
    elif kind == "boot":
        r = scenarios.boot(**a)
    elif kind == "gallery":
        r = scenarios.gallery(**a)
    else:
        raise ValueError("kind: online, offline, boot or gallery")
    return scenarios.format_result(r)


def call(name, a):
    copy = a.get("copy", 0)
    if name == "scenario":
        close()
        return [raw(run_scenario(a["kind"], a.get("args")))]
    if name == "digest":
        path = a["path"]
        if os.path.isdir(path):
            path = os.path.join(path, "log.txt")
        lines = logparse.digest(logparse.read_text(path), max_lines=int(a.get("max", 60)),
                                sites=logparse.load_call_sites(REPO), source=path)
        return [raw("\n".join(lines))]
    if name == "analyze":
        analyses = {i: logparse.analyze_copy(logparse.read_text(p)) for i, p in enumerate(a["logs"])}
        n = a.get("matches") or min(len(x["started"]) for x in analyses.values())
        problems = []
        rows, ok = scenarios.online_rows(analyses, n, False, problems)
        return [raw(scenarios.compact_json({"ok": ok and not problems, "matches": rows, "problems": problems}))]
    if name == "lock_status":
        lock = machine.MachineLock()
        text = lock.read()
        try:
            with open(lock.priority_path, encoding="utf-8", errors="replace") as f:
                prio = f.read().strip()
        except OSError:
            prio = None
        return [text_({"lock": machine.parse_lock(text)["who"] if text else None,
                       "priority": prio.splitlines()[0][:150] if prio else None,
                       "games": machine.games(), "this_agent": machine.agent_id(),
                       "held_here": bool(SESSION and SESSION.lock.held)})]
    if name == "launch":
        return [text_(launch(a.get("copies", 1), a.get("online", False), a.get("quick", "0,2"), a.get("fighters"),
                             a.get("pads", 1), a.get("fast_ko", 0), a.get("env"), a.get("visible", False),
                             a.get("ko_step", 0), a.get("ko_winner", "alt"), a.get("bot", 0),
                             a.get("relay_port", 27777), a.get("base_core", 2), a.get("rollback", "off"),
                             a.get("lock_timeout", 900), a.get("fixture")))]
    if name == "state":
        return [text_(control(copy, "state"))]
    if name == "press":
        mode = a.get("mode", "press")
        line = "%s %d %s" % (mode, int(a.get("slot", 0)), a.get("controls", "all"))
        if mode == "press":
            line += " %d" % int(a.get("ms", 120))
        return [text_(control(copy, line))]
    if name == "screenshot":
        data, err = screenshot(copy, a.get("scale", 0.5))
        if err:
            return [text_({"error": err})]
        return [{"type": "image", "data": data, "mimeType": "image/png"}]
    if name == "log":
        return [text_(read_log(copy, a.get("grep"), a.get("last", 40)))]
    if name == "wait_for":
        return [text_(wait_for(copy, a["text"], a.get("timeout", 120)))]
    if name == "to_menu":
        return [text_(to_menu(copy, a.get("timeout", 180)))]
    if name == "wait_state":
        return [text_(wait_state(copy, a["path"], a["op"], a["value"], a.get("timeout", 60)))]
    if name == "inject":
        if a.get("clear"):
            return [text_(control(copy, "inject clear"))]
        return [text_(control(copy, "inject %d %d %d %s" % (int(a.get("slot", 0)), int(a.get("start", 0)),
                                                           int(a.get("steps", 1)), a.get("bits", "0"))))]
    if name == "match":
        return [text_(control(copy, "match " + a["spec"]))]
    if name == "fe":
        return [text_(control(copy, "fe " + a["command"]))]
    if name == "save_state":
        return [text_(control(copy, "save"))]
    if name == "load_state":
        return [text_(control(copy, "load"))]
    if name == "peek":
        return [text_(control(copy, "peek %s %d" % (a["address"], int(a.get("bytes", 64)))))]
    if name == "memdiff":
        return [text_(control(copy, "memdiff " + a.get("phase", "begin"), timeout=30))]
    if name == "poke":
        return [text_(control(copy, "poke %s %s" % (a["address"], a["value"])))]
    if name == "close":
        return [text_(close())]
    raise ValueError("unknown tool " + name)


def text_(obj):
    return {"type": "text", "text": json.dumps(obj, ensure_ascii=False)}


def raw(s):
    return {"type": "text", "text": s}


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        msg = json.loads(line)
        mid, method = msg.get("id"), msg.get("method")
        if mid is None:
            continue        # notifications
        try:
            if method == "initialize":
                result = {"protocolVersion": msg.get("params", {}).get("protocolVersion", "2024-11-05"),
                          "capabilities": {"tools": {}}, "serverInfo": {"name": "defjam", "version": "2"}}
            elif method == "tools/list":
                result = {"tools": TOOLS}
            elif method == "tools/call":
                p = msg.get("params", {})
                try:
                    result = {"content": call(p.get("name"), p.get("arguments") or {})}
                except Exception as ex:     # a tool error goes back to the model, not over the protocol
                    result = {"content": [text_({"error": str(ex)})], "isError": True}
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
