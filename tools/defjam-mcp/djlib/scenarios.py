"""High-level scenarios, each ending in one compact JSON result (the main token saver).

    boot      one copy to the main menu: boot time, faults, a picture
    offline   one copy, a match started without the menus (automatch), fought to the summary
    online    a local relay and 2..4 copies in one quick-match room, N room matches in a row
    gallery   one copy, the online screens' sample pages captured one by one

Every scenario runs under the machine lock on disposable data roots (copies.Session) and returns a
dict whose 'ok' is False on any failure; dj.py exits non-zero then. format_result() prints it with
one line per match, so a result stays under ~40 lines.
"""
import glob
import html.parser
import importlib.util
import json
import re
import os
import threading
import time

from . import copies, logparse, machine, nav

_harness = None


def harness():
    """scripts/harness.py, for its pad-script builder and the fight presses (FIGHT_TAIL)."""
    global _harness
    if _harness is None:
        spec = importlib.util.spec_from_file_location("harness_dj", os.path.join(machine.REPO, "scripts", "harness.py"))
        _harness = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_harness)
    return _harness


MAIN_MENU = "Control.GetScreenInfo(intMain/mainMenu"
HANG_MARK = "interrupts masked for 2 s"     # [OHCI0]: the game thread has stopped polling the pad


def capture_hang(s, c):
    """A copy whose game thread stopped: where its threads are, before anything kills it
    (scripts/sample-threads.py, guest-stack.py, native-stacks.py into hang-copyN.txt)."""
    import subprocess, sys
    out = os.path.join(s.dir, "hang-copy%d.txt" % c.index)
    pid = str(c.proc.pid)
    cmds = [["sample-threads.py", "--preset", "win-x64-release", "--pid", pid, "--samples", "6"],
            ["guest-stack.py", "--preset", "win-x64-release", "--pid", pid],
            ["native-stacks.py", "--pid", pid, "--depth", "40"],
            ["icall-window.py", "--preset", "win-x64-release", "--pid", pid, "--samples", "2"],
            # the USB host controller driver's extension (OHCI deferred routine context; its pending
            # URB list head/tail at +0x41C/+0x420) and the pad's interrupt URB, TDs around it
            ["peek-guest.py", "--preset", "win-x64-release", "--pid", pid, "0x80000784", "0x4A0"],
            ["peek-guest.py", "--preset", "win-x64-release", "--pid", pid, "0x80000784", "0x4A0", "--diff", "3"],
            ["peek-guest.py", "--preset", "win-x64-release", "--pid", pid, "0x80002800", "0x400"],
            ["peek-guest.py", "--preset", "win-x64-release", "--pid", pid, "0x80003E00", "0x200"],
            ["peek-guest.py", "--preset", "win-x64-release", "--pid", pid, "0x003CC800", "0x300"],
            # the TD pool's counters (0x3CD0A0..) and the four pads' rumble blocks (XINPUT_FEEDBACK at
            # 0x3C92E0 + 0x90 * port, their request block at +0x10)
            ["peek-guest.py", "--preset", "win-x64-release", "--pid", pid, "0x003CD080", "0x60"],
            ["peek-guest.py", "--preset", "win-x64-release", "--pid", pid, "0x003C92B0", "0x250"]]
    with open(out, "w", encoding="utf-8") as f:
        for cmd in cmds:
            f.write("==== %s\n" % " ".join(cmd))
            f.flush()
            try:
                subprocess.run([sys.executable, os.path.join(machine.REPO, "scripts", cmd[0])] + cmd[1:],
                               stdout=f, stderr=subprocess.STDOUT, timeout=180)
            except Exception as e:      # a diagnostic must never end the test
                f.write("failed: %r\n" % (e,))
    return out


TITLE_ANCHOR = "feflow.xml#2"
VIRTUAL_PAD_ENV = {"RECOMP_INPUT_HOST": "1", "RECOMP_INPUT_IGNORE_FOCUS": "1", "RECOMP_PAD_HOST": "1",
                   "RECOMP_INPUT_NO_PADS": "1", "RECOMP_PAD_SCRIPT_VIRTUAL": "1"}
# Faster tests that keep the simulation honest: no flip pacing and no vsync wait. The fight itself
# runs on the guest's PTIMER (real time), so its length is set by the knockout step, not by these.
TURBO_ENV = {"RECOMP_FLIP_HZ": "0", "RECOMP_PRESENT_VSYNC": "0"}

MODES = {   # name: (match type for the quick match, summaries per match = fighters in it)
    "1v1": (0, 2), "team": (1, 4), "ffa": (3, 4), "koth": (64, 2),
    # FFA rotation (build 66): a room with a line, up to four fight a Free for All, the winner stays on
    "ffarot": (0x42, 4),
}

PAD_SCRIPT_MAX = 4095   # src/hooks: a longer RECOMP_PAD_SCRIPT is ignored ("[PAD] script: ... ignored")


def _koth_script(matches, per, p2):
    """A hill copy's script: presses for up to `matches` + 4 fights (a king fights more than the count),
    as many as fit the game's script limit."""
    best = None
    for n in range(matches, matches + 5):
        script = harness().build_pad_script(["getscreeninfo(intmain/mainmenu=lt@0.5"], online_tail(n, per, p2))
        if len(script) > PAD_SCRIPT_MAX:
            break
        best = script
    if best is None:
        raise ValueError("the pad script for %d hill fights is over %d characters" % (matches, PAD_SCRIPT_MAX))
    return best


def _rb_env(rollback):
    """RECOMP_ROLLBACK for a player copy: on, off, or (mandatory) "server": the audio arena without an opt-in."""
    return {"on": "on", "mandatory": "server"}.get(rollback, "off")


def _rb_relay_extra(rollback, modes=None):
    """The relay's rollback-everywhere switch for tests (-rollback-modes), with --rollback mandatory."""
    return ["-rollback-modes", modes or "all"] if rollback == "mandatory" else []


NOTE_SENT = "[REPORT] note after the match for report "     # ... sent: HTTP <status> (bug_report.c, the note after the match)


def _bug_reports(folder):
    """The local account service's reports (online --reports): folder, files, the note.txt added after the match."""
    out = []
    for name in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        d = os.path.join(folder, name)
        if not os.path.isdir(d):
            continue
        item = {"folder": name, "files": sorted(f for f in os.listdir(d) if not f.startswith("."))}
        note = os.path.join(d, "note.txt")
        if os.path.isfile(note):
            item["note_after_match"] = open(note, encoding="utf-8", errors="replace").read().strip()
        out.append(item)
    return out


WATCH = ["[CRASH]", "[WATCHDOG]", "[NETPLAY] desync in", "[NETPLAY] gave up", " starts in ",
         "[FUNCCALL] Game.UpdateMatchRewardsPoints()", "[FUNCCALL] Game.GetMatchSummary(", MAIN_MENU,
         "[FUNCCALL] Game.GetMatchSummary(0)",
         '"event":"match_start"', '"event":"result"', '"event":"match_over"', "[AUTOMATCH] Game.StartGame()",
         "[NETPLAY] START:", "[TEST-KO] match", HANG_MARK]


def compact_json(obj):
    """JSON with one top-level key per line and one line per list item: short and greppable."""
    out = ["{"]
    items = list(obj.items())
    for n, (k, v) in enumerate(items):
        comma = "," if n < len(items) - 1 else ""
        if isinstance(v, list) and v and all(isinstance(x, (dict, list, str)) for x in v):
            out.append('  "%s": [' % k)
            for m, x in enumerate(v):
                out.append("    " + json.dumps(x, separators=(",", ":")) + ("," if m < len(v) - 1 else ""))
            out.append("  ]" + comma)
        else:
            out.append('  "%s": %s%s' % (k, json.dumps(v, separators=(",", ":")), comma))
    out.append("}")
    return "\n".join(out)


def _bot_env(bot):
    """--bot: True/'' -> aggression 60; a number -> that aggression; 'p1=..,p2=..' -> per slot (the
    behaviours land with the bot's playbook; until then each name maps to an aggression)."""
    if bot in (None, False, 0, "0", "off"):
        return {}
    if bot is True or bot == "" or bot == "on":
        return {"RECOMP_TEST_BOT": "1,60"}
    s = str(bot)
    if s.isdigit():
        return {"RECOMP_TEST_BOT": "1,%d" % max(0, min(100, int(s)))}
    return {"RECOMP_TEST_BOT": "1,60", "RECOMP_TEST_BOT_PLAN": s}   # src/hooks/test_bot.c behaviours


def _png(session, i, name, shots):
    path = os.path.join(session.dir, name + ".png")
    got, err = session.screenshot(i, path)
    if got:
        shots.append(os.path.relpath(path, machine.REPO))
    return got


def _relpath(p):
    try:
        return os.path.relpath(p, machine.REPO)
    except ValueError:
        return p


def _finish(session, result, t0):
    closed = session.close()
    result["elapsed_s"] = round(time.time() - t0, 1)
    result["lock_wait_s"] = session.lock.waited_s
    if session.dir:
        result["dir"] = _relpath(session.dir)
    return result


def _faults(text):
    return [c[0] for _, c in ((i, logparse.condense_crash(b)) for i, b in logparse.crash_blocks(text.splitlines()))]


# ---- boot -----------------------------------------------------------------------------------------

EXIT_OPEN, EXIT_NO, EXIT_YES = "[UI] exit prompt opened", "[UI] exit prompt: no", "[UI] exit prompt: yes"
EXIT_CHIP, WINDOW_CLOSED, PRESTART = "[UI] exit chip at ", "[TRANS] window closed by the user", "intMain/preStart"
EXIT_WATCH = [EXIT_OPEN, EXIT_NO, EXIT_YES, EXIT_CHIP, WINDOW_CLOSED, PRESTART]


def _wait_count(s, c, text, n, secs):
    deadline = time.time() + secs
    while time.time() < deadline:
        s.poll()
        if c.watch.count(text) >= n:
            return True
        if not c.alive():
            return False
        time.sleep(0.25)
    return False


def exit_check(s, c, shots):
    """The main menu's EXIT (online_screen.cpp): VIEW opens the prompt and B says NO, ESC opens and
    closes it, a click on the chip opens it and B closes it; the menu stays where it was through all of
    it (no title screen, the same cursor). Then VIEW, YES: the process ends with code 0 through the
    window's own close, with no crash and no dump."""
    WM_KEYDOWN, WM_KEYUP, WM_LBUTTONDOWN, WM_LBUTTONUP, VK_ESCAPE = 0x100, 0x101, 0x201, 0x202, 0x1B
    steps = []

    def step(name, ok):
        steps.append(name + (" ok" if ok else " FAILED"))
        return ok

    def key(vk):
        s.post(0, WM_KEYDOWN, vk, 1)
        time.sleep(0.12)
        s.post(0, WM_KEYUP, vk, 1 | (1 << 30) | (1 << 31))

    r = nav.to_menu(s.control, 0, timeout=60)
    if not r.get("ok"):
        return {"ok": False, "error": r.get("error"), "steps": steps}
    time.sleep(3.0)                 # the menu takes presses once its music runs; the chip is drawn
    before = s.control(0, "state")
    s.poll()
    focus0, prestart0 = nav.focus_of(before), c.watch.count(PRESTART)
    ok = step("chip drawn", c.watch.count(EXIT_CHIP) > 0)
    # 1. VIEW opens, B closes
    s.control(0, "press 0 back 150")
    ok &= step("view opens", _wait_count(s, c, EXIT_OPEN, 1, 6))
    time.sleep(0.8)
    _png(s, 0, "exit-prompt", shots)
    s.control(0, "press 0 b 150")
    ok &= step("b says no", _wait_count(s, c, EXIT_NO, 1, 6))
    time.sleep(2.0)
    # 2. ESC opens, ESC closes
    key(VK_ESCAPE)
    ok &= step("esc opens", _wait_count(s, c, EXIT_OPEN, 2, 6))
    time.sleep(0.8)
    key(VK_ESCAPE)
    ok &= step("esc says no", _wait_count(s, c, EXIT_NO, 2, 6))
    time.sleep(2.0)
    # 3. a click on the chip opens, B closes
    chip = None
    for line in logparse.read_text(c.log).splitlines():
        if line.startswith(EXIT_CHIP):
            a, b = line[len(EXIT_CHIP):].split("-")
            (x0, y0), (x1, y1) = (map(int, a.split(",")), map(int, b.split(",")))
            chip = ((x0 + x1) // 2, (y0 + y1) // 2)
    if chip:
        lp = (chip[1] << 16) | chip[0]
        s.post(0, WM_LBUTTONDOWN, 1, lp)
        time.sleep(0.12)
        s.post(0, WM_LBUTTONUP, 0, lp)
        ok &= step("click opens", _wait_count(s, c, EXIT_OPEN, 3, 6))
        time.sleep(0.8)
        s.control(0, "press 0 b 150")
        ok &= step("b says no", _wait_count(s, c, EXIT_NO, 3, 6))
    else:
        ok &= step("click opens (no chip line)", False)
    time.sleep(2.5)
    after = s.control(0, "state")
    _png(s, 0, "exit-no", shots)
    same = "mainmenu" in after.get("screen", "").lower() and c.watch.count(PRESTART) == prestart0 and         nav.focus_of(after) == focus0
    ok &= step("menu unchanged", same)
    # 4. VIEW, left to YES, A: the game ends
    s.control(0, "press 0 back 150")
    ok &= step("view opens", _wait_count(s, c, EXIT_OPEN, 4, 6))
    time.sleep(0.8)
    s.control(0, "press 0 dpad_left 150")
    time.sleep(0.6)
    s.control(0, "press 0 a 150")
    ok &= step("yes", _wait_count(s, c, EXIT_YES, 1, 6))
    deadline = time.time() + 20
    while c.alive() and time.time() < deadline:
        time.sleep(0.25)
    s.poll()
    code = None if c.alive() else c.proc.returncode
    # a crash writes process.dmp into its bug bundle (src/hooks/recorder.c, logs/sessions/<start>/)
    dumps = [_relpath(p) for p in glob.glob(os.path.join(machine.REPO, "logs", "sessions", "*", "*.dmp"))
             if os.path.getmtime(p) >= s.t0]
    crashed = c.watch.count("[CRASH]") > 0
    ok &= step("exit code 0", code == 0)
    ok &= step("window close path", c.watch.count(WINDOW_CLOSED) > 0)
    ok &= step("no crash, no dump", not dumps and not crashed)
    return {"ok": bool(ok), "exit_code": code, "dumps": dumps, "steps": steps}


def boot(timeout=240, turbo=False, extra_env=None, lock_timeout=None, log=None, keep_roots=False, goto=None):
    """To the main menu; with goto (story, battle, rewards, online, slotN) on to that entry by name, or
    with goto 'exit' through the menu's EXIT prompt (exit_check) until the game quits."""
    t0 = time.time()
    s = copies.Session("boot", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    result = {"scenario": "boot", "ok": False}
    shots = []
    try:
        h = harness()
        env = {"RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
               "RECOMP_PAD_SCRIPT": h.build_pad_script(["getscreeninfo(intmain/mainmenu=back@9999"])}
        if turbo:
            env.update(TURBO_ENV)
        if goto:
            env.update(VIRTUAL_PAD_ENV)
            env["RECOMP_INPUT_VIRTUAL_PADS"] = "1"
        if goto == "exit":
            # the title presses go into the report, not onto the virtual pad: there they would undo the
            # control channel's presses, and the overlay reads VIEW from the pad itself
            env.pop("RECOMP_PAD_SCRIPT_VIRTUAL", None)
        env.update(extra_env or {})
        c = s.launch(0, env=env, watch=WATCH + (EXIT_WATCH if goto == "exit" else []))
        deadline = time.time() + timeout
        while time.time() < deadline and c.alive():
            s.poll()
            if c.watch.count(MAIN_MENU) or c.watch.count("[CRASH]") or c.watch.count("[WATCHDOG]"):
                break
            time.sleep(0.5)
        nav_result = None
        if c.watch.count(MAIN_MENU):
            time.sleep(3)
            _png(s, 0, "menu", shots)
            if goto == "exit":
                nav_result = exit_check(s, c, shots)
                result["goto"] = nav_result
            elif goto:
                nav_result = nav.goto(s.control, goto, 0, timeout=90)
                time.sleep(2)
                _png(s, 0, "goto-" + goto, shots)
                result["goto"] = nav_result
        s.poll()
        text = logparse.read_text(c.log)
        result.update({"ok": bool(c.watch.count(MAIN_MENU)) and not _faults(text) and (not goto or bool(nav_result and nav_result.get("ok"))),
                       "menu_s": c.watch.first.get(MAIN_MENU), "exited": None if c.alive() else c.proc.returncode,
                       "faults": _faults(text), "gpu": next((l[7:90] for l in text.splitlines() if l.startswith("D3D8: graphics card")), None),
                       "log": _relpath(c.log), "shots": shots})
    finally:
        _finish(s, result, t0)
    return result


# ---- social (the chat window, Social Update stage 2a) ----------------------------------------------

SOCIAL_NET = [
    # copy 0: asks copy 1 to be friends, writes to it, then a line in global chat; stage 2c: once copy 1
    # is in its crew (made and invited through the window, SOCIAL_UI) makes it a Lieutenant and reports
    # its global line
    "wait:4;friend:Copy 1;dm:Copy 1:hey Copy 1, run it back?;wait:6;global:eu:anyone on Europe for a quick one?;"
    "wait:42;log;wait:56;crewrole:Copy 1:lieutenant;wait:3;report:Copy 1;wait:3;log;wait:24;log",
    # copy 1: accepts once the request has been on screen a moment, answers, then a global line; stage
    # 2c: joins copy 0's crew, lets everyone message it, blocks copy 0 and lifts the block again
    "wait:9;accept;wait:3;dm:Copy 0:yo, best of 3?;wait:3;global:eu:gg all, rematch later;wait:40;log;"
    "crewjoin;wait:8;dms:everyone;wait:3;log;wait:10;block:Copy 0;wait:5;log;unblock:Copy 0;wait:3;log",
]
# The window's pages for the captures, then the real input paths: copy 1 opens a room (its friends see
# it), types on the keyboard (ENTER opens, text, ENTER sends, ESC closes); copy 0 uses the pad (BACK
# opens, Y the on-screen keyboard, A types K, left and A types J, START sends "kj", B closes) and then
# joins copy 1's room from FRIENDS (A on JOIN ROOM).
SOCIAL_UI = [
    "3:chat=friends;19:dm=Copy 1;25:chat=global;30:keyboard=yeah lets run it bac;35:close;40:chat=friends;44:chat=crew;"
    "46:chat=global;47:type=;48:close;50:pad=back;51:pad=y;52:pad=a;53:pad=left;54:pad=a;55:pad=start;57:pad=b;58:chat=friends;"
    "59:pad=a;64:chat=room;66:type=hello room from copy 0;67:send;76:mute=Copy 1;92:close;"
    "94:chat=crew;95:crewcreate=Bronx Kings:BXK;100:crewinvite=Copy 1;113:chat=crew;117:menu=Copy 1;119:close;"
    "120:chat=global;121:menu=Copy 1;123:close;124:chat=messages;128:close",
    "5:chat=friends;12:quickedit=8:See you next round;16:dm=Copy 0;22:chat=global;27:friend=Tavo;32:close;33:room=create;36:chat=room;40:chat=global;41:close;"
    "44:key=enter;45:text=typed on a keyboard;46:key=enter;47:key=escape;62:chat=room;65:type=hey copy 0, gl;66:send;"
    "70:reportroom=Copy 0;"
    "82:type=copy 0 should not see this;83:send;90:close;104:chat=crew;110:chat=messages;114:close;116:chat=friends;"
    "118:chat=global;124:chat=friends;126:chat=friends;130:close",
]
# The quick-chat wheel (RECOMP_TEST_QUICK_CHAT, the pad's L3 through the overlay's pad input): copy 0 a
# second after it came into copy 1's room (the window shut), line 2 "Run it back?" (hold, point, let go);
# copy 1 on GLOBAL (open from 118), line 4 "Nice!" (a tap, point, A).
SOCIAL_QUICK = ["room:1:2", "chat:119:4:a"]
# --stress: more accounts (no game) whose names and lines are what the live chat has: flag emoji
# (regional-indicator pairs, 4-byte UTF-8), Arabic, combining marks, a ZWJ family, a very long word, format
# characters, near-empty text. They become friends of both copies (copy 0 also keeps two requests open),
# write to both copies (both let everyone message them) and to global chat, before the copies start and
# again while they run (the long-poll's events). Build 52's chat closed the game (2026-11-04).
STRESS_NAMES = ["faroukleboucher69\U0001F1EB\U0001F1F7\U0001F1E9\U0001F1FF", "محمد الفارس",
                "Z̴̡̛͓a̷͚͐l̶̰̓g̸̱̈́o̵̘͠",
                "Averyveryveryveryveryveryverylongnamewithoutanyspaces",
                "\U0001F468‍\U0001F469‍\U0001F467‍\U0001F466 família"]
STRESS_TEXTS = ["\U0001F1EB\U0001F1F7\U0001F1E9\U0001F1FF\U0001F1EB\U0001F1F7 allez \U0001F525\U0001F525\U0001F525",
                "مرحبا بالجميع، من يريد القتال؟",
                "Z̴̡̛͓a̷͚͐l̶̰̓g̸̱̈́o̵̘͠ t̸e̵x̷t̶",
                "A" * 260, "\U0001F600" * 70, "100% %s %n %d {x} \\ \" \t tab", "a​‎‮ reversed ‬ end",
                "é́́́ \U0001F44D\U0001F3FD \U0001F3F3️‍\U0001F308", " ", "x"]


def _stress_social(port, tokens, copy_names, rnd):
    """One round of the --stress accounts' lines (rnd 0: also the friendships and the settings)."""
    import urllib.request

    def call(name, method, path, body=None):
        req = urllib.request.Request("http://127.0.0.1:%d%s" % (port, path), method=method,
                                     data=None if body is None else json.dumps(body).encode("utf-8"),
                                     headers={"Authorization": "Bearer " + tokens[name], "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status, json.loads(r.read() or b"null")
        except Exception as e:      # a rejected line (empty, too fast) is part of the test
            return getattr(e, "code", 0), None

    ids = {}
    if rnd == 0:
        for c in copy_names:
            call(c, "POST", "/api/social/settings", {"dms": "everyone"})
        for k, w in enumerate(STRESS_NAMES):
            for c in copy_names:
                if c == copy_names[-1] and k >= 3:
                    continue        # copy 1 accepts its first request in the script: none of these may be first
                st, out = call(w, "POST", "/api/social/friends/request", {"name": c})
                if out and out.get("account"):
                    ids[c] = out["account"]
                if k < 3:
                    call(c, "POST", "/api/social/friends/accept", {"name": w})
        _stress_social.ids = ids
    ids = getattr(_stress_social, "ids", {})
    sent = 0
    for k, w in enumerate(STRESS_NAMES):
        t = STRESS_TEXTS[(k + rnd * 3) % len(STRESS_TEXTS)]
        st, _ = call(w, "POST", "/api/social/global", {"region": "eu", "text": t + ("" if rnd == 0 else " r%d" % rnd)})
        sent += st // 100 == 2
        for c, cid in ids.items():
            st, _ = call(w, "POST", "/api/social/messages/%d" % cid, {"text": STRESS_TEXTS[(k + rnd) % len(STRESS_TEXTS)] + " %d" % rnd})
            sent += st // 100 == 2
    return sent


def social(timeout=300, relay_port=27900, off=False, lock_timeout=None, log=None, keep_roots=False, extra_env=None,
           out_dir=None, stress=False):
    """Two copies at the online hub, signed in to a local account service with the social features
    on (accounts -social -test-accounts) and a local relay: copy 0 asks copy 1 to be friends, copy 1
    accepts, both write a private message and a global line (RECOMP_TEST_SOCIAL), and the chat
    window's tabs, the keyboard and the friends list are captured (RECOMP_TEST_SOCIAL_UI and the
    overlay's own captures). off: one copy against a service whose switch file is missing and without
    RECOMP_SOCIAL: nothing new may show. out_dir: the chosen captures as PNG there too."""
    t0 = time.time()
    s = copies.Session("social-off" if off else "social", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    result = {"scenario": "social", "ok": False, "params": {"off": off, "stress": stress}}
    try:
        h = harness()
        acc_port = relay_port + 100
        n = 1 if off else 2
        names = ["Copy %d" % i for i in range(n)]
        flags = ["-social-switch", os.path.join(s.dir, "social-enabled")] if off else ["-social"]
        # avatars (stage 2c): a fake picture source, never Discord; Copy 0 (account 1) has a picture,
        # Copy 1 none (its initials show)
        avatars = os.path.join(s.dir, "avatar-source")
        os.makedirs(avatars, exist_ok=True)
        try:
            from PIL import Image, ImageDraw
            im = Image.new("RGB", (128, 128), (40, 90, 200))
            d = ImageDraw.Draw(im)
            d.ellipse((24, 14, 104, 94), fill=(250, 205, 60))
            d.rectangle((14, 96, 114, 128), fill=(200, 16, 46))
            im.save(os.path.join(avatars, "1.png"))
        except ImportError:
            pass
        flags += ["-avatar-source", avatars]
        tokens = s.accounts(acc_port, names + (STRESS_NAMES if stress and not off else []), extra=flags)
        # DJ_RENAME="Copy 0=New Name[,Copy 1=...]": the FFNY name (POST /api/me/name) before the copies start;
        # the room and the chats then show the new name (the checks below still look for "Copy N")
        for pair in filter(None, os.environ.get("DJ_RENAME", "").split(",")):
            who, new = pair.split("=", 1)
            import urllib.request
            req = urllib.request.Request("http://127.0.0.1:%d/api/me/name" % acc_port, method="POST",
                                         data=json.dumps({"name": new}).encode(),
                                         headers={"Authorization": "Bearer " + tokens[who], "Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=5) as r:
                result.setdefault("renamed", {})[who] = json.loads(r.read())["account"]["name"]
        stress_sent, stress_rounds = [], [60, 120]
        if stress and not off:
            stress_sent.append(_stress_social(acc_port, tokens, names, 0))
        # the relay asks the account service who writes in a room (room chat's IDENT, stage 2b)
        s.relay(relay_port, extra=["-accounts-internal", "http://127.0.0.1:%d" % (acc_port + 1)])
        watch = WATCH + ["[SOCIAL-TEST] state:", "[SOCIAL] ready", "[SOCIAL-UI] close", "[SOCIAL-UI] key=escape",
                         "[SOCIAL] the service has the social"]
        shots = {}
        for i in range(n):
            shots[i] = os.path.join(s.dir, "shots%d" % i)
            os.makedirs(shots[i], exist_ok=True)
            env = dict(VIRTUAL_PAD_ENV)
            env.update({
                "RECOMP_INPUT_VIRTUAL_PADS": "1",
                "RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
                "RECOMP_PAD_SCRIPT": h.build_pad_script(["getscreeninfo(intmain/mainmenu=back@9999"]),
                "RECOMP_TEST_OPEN_ONLINE": "1",
                # the local relay stands in for Europe (a copy beside another needs local addresses only)
                "RECOMP_NETPLAY_SERVER": "127.0.0.1:%d" % relay_port, "RECOMP_NETPLAY_SERVER_EU": "127.0.0.1:%d" % relay_port,
                "RECOMP_NETPLAY_NAME": names[i],
                "RECOMP_ACCOUNT_SERVER": "127.0.0.1:%d" % acc_port,
                "RECOMP_ACCOUNT_TEST_TOKEN": tokens[names[i]],
                "RECOMP_OVERLAY_SHOT_DIR": shots[i], "RECOMP_OVERLAY_SHOT_MS": "1000",
                "RECOMP_TEST_SOCIAL_UI": "3:chat=global;6:chat=friends" if off else SOCIAL_UI[i],
            })
            if not off:
                env["RECOMP_TEST_QUICK_CHAT"] = SOCIAL_QUICK[i]
            if not off:
                env["RECOMP_SOCIAL"] = "on"
                env["RECOMP_TEST_SOCIAL"] = SOCIAL_NET[i]
            env.update(extra_env or {})
            s.launch(i, env=env, core=2 + 2 * i, watch=watch)
            time.sleep(3)
        deadline = time.time() + timeout
        stop = None
        while time.time() < deadline:
            s.poll()
            if stress and not off and stress_rounds and s.elapsed() > stress_rounds[0]:
                stress_rounds.pop(0)
                stress_sent.append(_stress_social(acc_port, tokens, names, len(stress_sent)))
            cs = list(s.copies.values())
            if any(c.watch.count("[CRASH]") or c.watch.count("[WATCHDOG]") for c in cs) or any(not c.alive() for c in cs):
                stop = "a copy crashed or exited"
                break
            if off:
                menu = cs[0].watch.first.get(MAIN_MENU)
                if menu and s.elapsed() > menu + 30:
                    stop = "done"
                    break
            elif (cs[0].watch.count("[SOCIAL-TEST] state:") >= 3 and cs[1].watch.count("[SOCIAL-TEST] state:") >= 4
                  and cs[0].watch.count("[SOCIAL-UI] close") >= 6 and cs[1].watch.count("[SOCIAL-UI] close") >= 4
                  and cs[1].watch.count("[SOCIAL-UI] key=escape")):
                stop = "done"
                break
            time.sleep(1)
        s.poll()
        time.sleep(2)
        texts = {i: logparse.read_text(c.log) for i, c in s.copies.items()}
        lines = {i: [l for l in t.splitlines() if "[SOCIAL" in l] for i, t in texts.items()}

        def has(i, text):
            return any(text in l for l in lines[i])
        checks = {}
        if off:
            checks = {"service_said_off": has(0, "the service has the social features off"),
                      "no_ready": not has(0, "[SOCIAL] ready"), "no_chat_ui": not has(0, "[SOCIAL-UI]"),
                      "no_social_captures": not glob.glob(os.path.join(shots[0], "*-social-*.bmp"))}
        else:
            state = {i: next((l for l in reversed(lines[i]) if "[SOCIAL-TEST] state:" in l), "") for i in lines}
            first = {i: next((l for l in lines[i] if "[SOCIAL-TEST] state:" in l), "") for i in lines}
            checks = {
                "ready": has(0, "[SOCIAL] ready") and has(1, "[SOCIAL] ready"),
                "request_arrived": has(1, "friend request from Copy 0"),
                "friends_both": has(0, "friend added: Copy 1") and has(1, "friend added: Copy 0"),
                "dm_0_to_1": has(1, "dm from Copy 0") and has(0, "dm sent to"),
                "dm_1_to_0": has(0, "dm from Copy 1"),
                "global_sent": has(0, "sent to global eu") and has(1, "sent to global eu"),
                "global_seen": all(any("global %d" % n in state[i] for n in (2, 3, 4, 5)) for i in state),
                # both online in the menu at first; copy 1's room reaches copy 0 as a presence change
                # (the first state: stage 2c's block ends the friendship before the last one)
                "presence": "Copy 0=" in first[1] and "Copy 0=offline" not in first[1] and
                            any("[SOCIAL] presence" in l and ": room" in l for l in lines[0]) and "Copy 1=room" in first[0],
                "pad_back_opens": has(0, "chat opened (Back)"),
                "pad_keyboard_send": has(0, "sent to global eu: kj") and has(1, "global eu from Copy 0: kj"),
                "keyboard_enter_opens": has(1, "chat opened (Enter)"),
                "keyboard_typing_send": has(1, "sent to global eu: typed on a keyboard") and
                                        has(0, "global eu from Copy 1: typed on a keyboard"),
                "join_from_friends": has(0, "joining Copy 1's room"),
                # room chat (stage 2b): through the relay, which knows the accounts (IDENT)
                "room_ident": all("room chat: the relay knows this account" in texts[i] for i in texts),
                "room_0_to_1": "room chat from Copy 0: hello room from copy 0" in texts[1],
                "room_1_to_0": "room chat from Copy 1: hey copy 0, gl" in texts[0],
                "room_mute": has(0, "room chat: muted Copy 1") and has(0, "hidden (muted): Copy 1"),
                # stage 2c: avatars from the service (never Discord), initials without one
                "avatar_picture": has(1, "avatar 1: picture") and has(0, "avatar 1: picture"),
                "avatar_initials": has(0, "avatar 2: none, initials"),
                # crews through the window: create (two keyboards), invite, join, role
                "crew_created": has(0, "crew: create Bronx Kings [BXK]") and has(0, "crew: created"),
                "crew_invited": has(0, "crew: invite Copy 1") and has(0, "crew: invite sent"),
                "crew_joined": any("[BXK] member" in l or "[BXK] lieutenant" in l for l in lines[1]
                                   if "[SOCIAL-TEST] state:" in l),
                "crew_role_seen": "[BXK] lieutenant" in state[1],
                "crew_role": any("Copy 1=lieutenant" in l for l in lines[0] if "[SOCIAL-TEST] state:" in l),
                "report_sent": has(0, "reported 2 ("),
                # the quick-chat wheel: copy 0's pad in the room reaches copy 1; copy 1's on GLOBAL goes out
                # a room line's report carries the room's code
                "report_room_code": any("reported 1 (room, message 0," in l and ", room " in l for l in lines[1]),
                "quick_room_wheel_opened": has(0, "quick chat: wheel opened (room)"),
                "quick_room_sent": has(0, "quick chat 2 sent to room: Run it back?"),
                "quick_room_received": "room chat from Copy 0: Run it back?" in texts[1],
                "quick_global_sent": has(1, "quick chat 4 sent to global: Nice!") and has(1, "sent to global eu: Nice!"),
                # a line changed with the keyboard (the QUICK CHAT page's) is kept in the settings
                "quick_line_edited": has(1, "quick chat line 8 set: See you next round") and
                                     has(1, "quick chat line 8 is now: See you next round"),
                "dms_everyone": has(1, "messages from: everyone") and any("dms everyone" in l for l in lines[1]),
                "block_both_ways": has(1, "blocked 1") and any("blocked [Copy 0 ]" in l for l in lines[1]) and
                                   "Copy 1=" not in state[0].split("friends [", 1)[-1].split("]", 1)[0],
                "unblock": has(1, "unblocked 1") and "blocked []" in state[1],
            }
            result["state"] = {i: state[i].split("state: ", 1)[-1] for i in state}
        if stress and not off:
            result["stress_sent"] = stress_sent
            checks["stress_lines_sent"] = len(stress_sent) >= 2 and all(n > 0 for n in stress_sent)
            checks["no_caught_exception"] = not any("[SOCIAL] caught:" in t for t in texts.values())
            checks["stress_names_shown"] = all(any("faroukleboucher69" in l for l in lines[i]) for i in lines)
        result["checks"] = checks
        # the captures: per copy and label the last one (the most filled in) and the first (a request, a
        # toast), as PNG
        picked = []
        try:
            from PIL import Image
        except ImportError:
            Image = None
        for i, d in shots.items():
            files = sorted(glob.glob(os.path.join(d, "*.bmp")))
            last, first = {}, {}
            for p in files:
                base = os.path.splitext(os.path.basename(p))[0]
                label = base.split("-", 1)[1] if "-" in base else "plain"
                last[label] = p
                first.setdefault(label + "-first", p)
            last.update({k: v for k, v in first.items() if k != "plain-first" and v != last[k[:-6]]})
            for label, p in sorted(last.items()):
                name = "copy%d-%s.png" % (i, label)
                if Image:
                    for target in [os.path.join(s.dir, name)] + ([os.path.join(out_dir, name)] if out_dir else []):
                        os.makedirs(os.path.dirname(target), exist_ok=True)
                        Image.open(p).convert("RGB").save(target)
                picked.append(name)
        result["captures"] = picked
        result["stop"] = stop
        result["social_lines"] = {i: len(v) for i, v in lines.items()}
        result["faults"] = {i: _faults(t) for i, t in texts.items() if _faults(t)}
        result["logs"] = [_relpath(c.log) for c in s.copies.values()]
        result["ok"] = stop == "done" and all(checks.values()) and not result["faults"]
    finally:
        _finish(s, result, t0)
    return result


# ---- room chat (social stage 2b) ------------------------------------------------------------------

def room_chat(timeout=None, relay_port=27900, lock_timeout=None, log=None, keep_roots=False, extra_env=None,
              out_dir=None, rollback="off"):
    """Room chat through the relay, no account service (the social features off: room chat is the
    relay's own): a quick match of two copies watched by a third (spectator mode, scenario online with
    one match and one spectator). In the room each copy writes a line (RECOMP_TEST_ROOM_CHAT); during
    the fight the spectator opens its chat bar, types and sends a line (RECOMP_TEST_FIGHT_CHAT), which
    must show on both fighters' overlay while the fight runs. The match's checks are scenario online's
    (checksums equal, winners, summary); the fighters never open the bar. out_dir: the overlay's
    captures of the fight with the line (PNG)."""
    shots = {}
    copy_env = {}
    session_dir = {}
    # copy 1 joins copy 0's room about 3 s after it opened it, the spectator 3 s later, and the match
    # starts as soon as the spectator is in: room lines before that, "fight" lines once START came
    # (the match loads; every copy is in the room)
    # stage 2c: copy 0's first line comes before anyone else is in: copy 1 and the spectator get it
    # from the relay's room memory when they come in (flag 8, "(earlier)"), once each
    # (copy 1's "still here" comes into the fight: copy 0's overlay draws while it shows, so copy 0's
    # quick-chat press there is seen, and refused)
    say = {0: "room:1:first line before anyone;room:5:ready when you are;fight:2:good luck",
           1: "fight:3:lets go;fight:24:still here",
           2: "room:1:hi from the stands"}
    import tempfile
    # under logs/dj (git-ignored), not the system temp folder, which other cleanups empty
    os.makedirs(os.path.join(machine.REPO, "logs", "dj"), exist_ok=True)
    base = tempfile.mkdtemp(prefix="roomchat-shots-", dir=os.path.join(machine.REPO, "logs", "dj"))
    for i in range(3):
        shots[i] = os.path.join(base, "shots%d" % i)
        os.makedirs(shots[i], exist_ok=True)
        copy_env[i] = {"RECOMP_TEST_ROOM_CHAT": say[i], "RECOMP_OVERLAY_SHOT_DIR": shots[i],
                       "RECOMP_OVERLAY_SHOT_MS": "700"}
    copy_env[2]["RECOMP_TEST_FIGHT_CHAT"] = "3:nice from the stands"
    # stage 2c: the spectator opens the bar with the pad's BACK; the on-screen keyboard comes up and sends
    copy_env[2]["RECOMP_TEST_FIGHT_CHAT_PAD"] = "1"
    # the spectator's overlay draws through the whole match (its ENTER opens the bar): fewer captures,
    # each one reads the picture back and slows that copy (it lags the players already)
    copy_env[2]["RECOMP_OVERLAY_SHOT_MS"] = "1500"
    # the quick-chat wheel (the pad's L3): the spectator's during the fight, line 2 "Run it back?" (a tap,
    # point, A); copy 0, a fighter, presses it in its fight: the wheel must stay shut
    copy_env[2]["RECOMP_TEST_QUICK_CHAT"] = "fight:5.5:2:a"
    copy_env[2]["RECOMP_TEST_QUICK_CHAT_HOLD_MS"] = "4000"     # the spectator lags: long enough for a capture
    # (copy 0 presses while copy 1's line "lets go" shows, so its overlay runs and sees the press)
    copy_env[0]["RECOMP_TEST_QUICK_CHAT"] = "fight:3:3;fight:6:3"
    r = online(matches=1, spectators=1, timeout=timeout, relay_port=relay_port, lock_timeout=lock_timeout, log=log,
               keep_roots=keep_roots, extra_env=extra_env, copy_env=copy_env, rollback=rollback)
    texts = {}
    for i, p in enumerate(r.get("logs", [])):
        path = p if os.path.isabs(p) else os.path.join(machine.REPO, p)
        texts[i] = logparse.read_text(path) if os.path.isfile(path) else ""

    def lines(i, mark):
        return [l for l in texts.get(i, "").splitlines() if mark in l]

    def during_fight(i, mark):
        """mark is logged between the copy's fight start and its result."""
        t = texts.get(i, "").splitlines()
        start = next((k for k, l in enumerate(t) if '"event":"match_start"' in l), None)
        end = next((k for k, l in enumerate(t) if start is not None and k > start and
                    ('"event":"result"' in l or '"event":"match_over"' in l)), None)
        hit = next((k for k, l in enumerate(t) if mark in l), None)
        return start is not None and hit is not None and hit > start and (end is None or hit < end)
    checks = {
        "relay_has_chat": all(lines(i, "(room chat)") for i in range(3)),
        "no_service_ident": all(lines(i, "this relay checks no accounts") for i in range(3)),
        "room_0_to_1": bool(lines(1, "room chat from Copy 0: ready when you are")),
        "players_to_spectator": bool(lines(2, "room chat from Copy 0: good luck")) and bool(lines(2, "room chat from Copy 1: lets go")),
        "player_to_player": bool(lines(0, "room chat from Copy 1: lets go")) and bool(lines(1, "room chat from Copy 0: good luck")),
        "room_spectator_to_players": all(lines(i, "room chat from Spectator 0 (spectator): hi from the stands") for i in (0, 1)),
        "echo_to_sender": bool(lines(0, "room chat from Copy 0 (this PC): ready when you are")),
        "spectator_bar_opened": bool(lines(2, "[UI] fight chat: typing, game input held")),
        "spectator_sent_in_fight": bool(lines(2, "fight chat sent: nice from the stands")),
        "fighters_got_it_in_fight": all(during_fight(i, "room chat from Spectator 0 (spectator): nice from the stands")
                                        for i in (0, 1)),
        "fighters_never_typed": not any(lines(i, "fight chat: typing") for i in (0, 1)),
        # stage 2c: the room's earlier lines for who comes later, once each (a return to the room after
        # the match brings them again: the game drops the repeats)
        "history_late_joiner": len(lines(1, "room chat (earlier) from Copy 0: first line before anyone")) == 1,
        "history_spectator": len(lines(2, "room chat (earlier) from Copy 0: first line before anyone")) == 1,
        "history_not_to_sayer": not lines(0, "(earlier) from Copy 0: first line before anyone"),
        "spectator_pad_keyboard": bool(lines(2, "bar opened (spectator, pad: on-screen keyboard)")),
        "quick_spectator_wheel": bool(lines(2, "quick chat: wheel opened (over the fight)")),
        "quick_spectator_sent": bool(lines(2, "quick chat 2 sent to room: Run it back?")),
        "quick_fighters_got_it": all(during_fight(i, "room chat from Spectator 0 (spectator): Run it back?") for i in (0, 1)),
        "quick_fighter_refused": bool(lines(0, "quick chat: not in a fighter's own fight")) and
                                 not any(lines(i, "quick chat: wheel opened") for i in (0, 1)) and
                                 not any(lines(i, "quick chat 3 sent") for i in (0, 1)),
    }
    picked = []
    try:
        from PIL import Image
    except ImportError:
        Image = None
    for i, d in shots.items():
        files = sorted(glob.glob(os.path.join(d, "*-fightchat*.bmp")))
        if not files or not Image:
            continue
        wheel = sorted(glob.glob(os.path.join(d, "*-fightchat-quickchat.bmp")))
        if wheel:       # the wheel over the fight (the spectator's)
            name = "copy%d-fightchat-quickchat.png" % i
            if out_dir:
                os.makedirs(out_dir, exist_ok=True)
                Image.open(wheel[len(wheel) // 2]).convert("RGB").save(os.path.join(out_dir, name))
            picked.append(name)
        # the capture where the spectator's line shows: the first after it arrived is enough; keep two
        for k, p in enumerate([files[0], files[len(files) // 2], files[-1]]):
            name = "copy%d-fightchat-%d.png" % (i, k)
            if out_dir:
                os.makedirs(out_dir, exist_ok=True)
                Image.open(p).convert("RGB").save(os.path.join(out_dir, name))
            picked.append(name)
    r["room_chat"] = {"checks": checks, "captures": picked, "shots_dir": base}
    r["ok"] = bool(r.get("ok")) and all(checks.values())
    return r


# ---- crew battles (social stage 3) ----------------------------------------------------------------

# Copies 0 and 1 are Bronx Kings [BXK] (copy 0 the Boss), copies 2 and 3 Red Hook Riders [RHR] (copy 2 the
# Boss). Copy 0 challenges RHR to a 2 v 2 (a test-only format, -social-test-battles) once RHR has two
# members; copy 2 accepts. Every copy then joins the battle (RECOMP_NETPLAY=crew: the room's CREWJOIN once the
# social features show the battle accepted) and its fights run as on the hill, the knockout winner the lower
# room slot. Each copy logs its state when the battle is over and loads the ladder.
CREW_NET = [
    "wait:3;crew:Bronx Kings:BXK;wait:4;crewinvite:Copy 1;wait:8;challenge:RHR:2;log;battledone;wait:3;"
    "ui:crewpage;wait:4;ui:crewladder;wait:4;ui:crewpicker=RHR;wait:4;ui:close;log",
    "crewjoin;log;battledone;wait:4;log",
    "wait:3;crew:Red Hook Riders:RHR;wait:4;crewinvite:Copy 3;battlewait;ui:crewpage;wait:6;battleaccept;log;battledone;wait:3;"
    "ui:crewpage;wait:4;ui:crewladder;wait:4;ui:close;log",
    "crewjoin;log;battledone;wait:4;log",
]
# copy 0's chat window on the crew page while it challenges; copy 2's crew page opens when the challenge is
# there (ui:crewpage, its first capture: ACCEPT CHALLENGE / DECLINE); after the battle the ui: steps show the
# crew page, the ladder and CHALLENGE A CREW's format picker
CREW_UI = {
    0: "8:chat=crew;40:close",
}


def crew_battle(timeout=None, relay_port=27900, lock_timeout=None, log=None, keep_roots=False, extra_env=None,
                out_dir=None, fast_ko=600, rollback="off", rollback_modes=None):
    """rollback: as online() ("mandatory": the relay's rollback-everywhere switch). Crew battles end to end (social stage 3): a local account service (-social -social-test-battles) and a
    relay that asks it (-accounts-internal), four copies in two crews of two: crews made and joined
    (RECOMP_TEST_SOCIAL), the challenge and its acceptance, every copy into the battle's room (CREWJOIN), the
    fights (knockout at a step, the lower room slot wins) until a crew has nobody left; then the relay's
    result, the rating, W/L, XP and the ladder must show on the service and in the games. out_dir: the chosen
    captures (the crew page, the challenge picker, the ladder, the battle screen, the result) as PNG."""
    t0 = time.time()
    n = 4
    timeout = timeout or 1200
    s = copies.Session("crewbattle", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    result = {"scenario": "crewbattle", "ok": False}
    shots = {}
    try:
        h = harness()
        acc_port = relay_port + 100
        names = ["Copy %d" % i for i in range(n)]
        tokens = s.accounts(acc_port, names, extra=["-social", "-social-test-battles"])
        s.relay(relay_port, rollback=(rollback != "off"),
                extra=["-accounts-internal", "http://127.0.0.1:%d" % (acc_port + 1)] + _rb_relay_extra(rollback, rollback_modes))
        watch = WATCH + ["[SOCIAL-TEST] step", "[SOCIAL] ready", "[NETPLAY] crew battle", "[SOCIAL] crew battle",
                         "[SOCIAL] crew battle 1: proposed"]
        for i in range(n):
            shots[i] = os.path.join(s.dir, "shots%d" % i)
            os.makedirs(shots[i], exist_ok=True)
            p2 = i >= 2     # the challenged crew comes in later: mostly the higher room slots, on the right
            env = dict(VIRTUAL_PAD_ENV)
            env.update({
                "RECOMP_INPUT_VIRTUAL_PADS": "1",
                "RECOMP_NETPLAY": "crew", "RECOMP_NETPLAY_SERVER": "127.0.0.1:%d" % relay_port,
                "RECOMP_NETPLAY_SERVER_EU": "127.0.0.1:%d" % relay_port,
                "RECOMP_NETPLAY_FIGHTER": str([55, 22, 9, 31][i]),
                "RECOMP_NETPLAY_NAME": names[i],
                "RECOMP_ROLLBACK": _rb_env(rollback),
                "RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
                "RECOMP_PAD_SCRIPT": h.build_pad_script(["getscreeninfo(intmain/mainmenu=lt@0.5"], online_tail(2, 2, p2)),
                "RECOMP_ACCOUNT_SERVER": "127.0.0.1:%d" % acc_port,
                "RECOMP_ACCOUNT_TEST_TOKEN": tokens[names[i]],
                "RECOMP_SOCIAL": "on",
                "RECOMP_TEST_OPEN_ONLINE": "1",     # the online screens (the chat window, the battle screen)
                "RECOMP_TEST_SOCIAL": CREW_NET[i],
                "RECOMP_OVERLAY_SHOT_DIR": shots[i], "RECOMP_OVERLAY_SHOT_MS": "1500",
            })
            if i in CREW_UI:
                env["RECOMP_TEST_SOCIAL_UI"] = CREW_UI[i]
            if fast_ko:
                env["RECOMP_TEST_KO_STEP"] = "%d,0" % int(fast_ko)
            env.update(extra_env or {})
            s.launch(i, env=env, core=2 + 2 * i, watch=watch)
            time.sleep(3)
        deadline = time.time() + timeout
        stop = None
        while time.time() < deadline:
            s.poll()
            cs = list(s.copies.values())
            if any(c.watch.count("[CRASH]") or c.watch.count("[WATCHDOG]") for c in cs) or any(not c.alive() for c in cs):
                stop = "a copy crashed or exited"
                break
            if any(c.watch.count("[NETPLAY] desync in") or c.watch.count("[NETPLAY] gave up") for c in cs):
                time.sleep(5)
                stop = "desync or gave up"
                break
            # each copy's last step (the log after battledone) written
            if all(c.watch.count("[SOCIAL-TEST] step") >= len(CREW_NET[c.index].split(";")) for c in cs):
                stop = "done"
                break
            time.sleep(1)
        else:
            stop = "timeout after %d s" % timeout
        time.sleep(4)       # the result screen a moment longer for the captures
        s.poll()
        texts = {i: logparse.read_text(c.log) for i, c in s.copies.items()}

        def lines(i, mark):
            return [l for l in texts[i].splitlines() if mark in l]
        last_state = {i: (lines(i, "[SOCIAL-TEST] crew:") or [""])[-1] for i in texts}
        results = {i: (lines(i, "[SOCIAL] crew battle") and [l for l in lines(i, "result: winner")] or [""])[-1] for i in texts}
        crew_lines = {i: lines(i, "[NETPLAY] crew battle") for i in texts}
        ladder = {i: (lines(i, "[SOCIAL] crew ladder:") or [""])[-1] for i in texts}
        # the service's own view: the ladder through the bot's internal route
        service = {}
        try:
            import urllib.request
            with urllib.request.urlopen("http://127.0.0.1:%d/internal/social/ladder?n=5" % (acc_port + 1), timeout=5) as r:
                service = json.loads(r.read().decode())
        except Exception as e:      # noqa: BLE001
            service = {"error": str(e)}
        crews = {c.get("tag"): c for c in service.get("crews") or []}
        winner_tag = None
        if crews.get("BXK") and crews.get("RHR"):
            winner_tag = "BXK" if crews["BXK"].get("wins") else "RHR" if crews["RHR"].get("wins") else None
        checks = {
            "crews_made": all("[BXK]" in l or "[RHR]" in l for l in [" ".join(lines(i, "[SOCIAL-TEST] state:")) for i in texts]),
            "challenged": bool(lines(0, "crew battle: challenged crew")) or bool(lines(0, "crew battle 1: proposed")),
            "accepted": bool(lines(2, "crew battle 1: accept")) or bool(lines(2, "[SOCIAL] crew battle 1: accepted")),
            "all_notified": all(lines(i, "[SOCIAL] crew battle (") for i in texts),
            "relay_caps": all(lines(i, "(crew battles)") for i in texts),
            "all_joined": all(lines(i, "[NETPLAY] crew battle 1: joining its room") for i in texts),
            "battle_ran": all(any("state 1" in l for l in crew_lines[i]) for i in texts),
            "battle_over": all(any("state 2" in l for l in crew_lines[i]) for i in texts),
            "result_in_games": all("result: winner" in results[i] for i in texts),
            "rated": all("rating +" in results[i] or "rating -" in results[i] for i in texts) and
                     not any("not rated" in results[i] for i in texts),
            "service_ladder": winner_tag is not None and len(crews) == 2 and
                              crews[winner_tag].get("rating", 0) > 1300 and
                              crews["RHR" if winner_tag == "BXK" else "BXK"].get("rating", 9999) < 1300 and
                              crews[winner_tag].get("place") == 1,
            "xp_and_wl": all(("%d-%d" % (1, 0)) in last_state[i] or ("%d-%d" % (0, 1)) in last_state[i] for i in texts) and
                         all("xp 150" in last_state[i] or "xp 50" in last_state[i] for i in texts),
            "ladder_in_games": all(ladder[i] and "2 crews" in ladder[i] for i in texts),
        }
        result["checks"] = checks
        result["service_ladder"] = [{k: c.get(k) for k in ("place", "tag", "rating", "wins", "losses", "league")}
                                    for c in service.get("crews") or []] or service
        result["results"] = {i: results[i].split("] ", 1)[-1][:160] for i in results}
        result["state"] = {i: last_state[i].split("crew: ", 1)[-1][:200] for i in last_state}
        # the captures: per copy and label the last one, the battle screen's first and last
        picked = []
        try:
            from PIL import Image
        except ImportError:
            Image = None
        want = ("social-crew", "social-crewchallenge", "social-crewladder", "crewbattle", "crewbattle-forming", "crewbattle-over")
        for i, d in shots.items():
            files = sorted(glob.glob(os.path.join(d, "*.bmp")))
            last = {}
            for p in files:
                base = os.path.splitext(os.path.basename(p))[0]
                label = base.split("-", 1)[1] if "-" in base else "plain"
                if label in want:
                    last[label] = p
                    if label in ("crewbattle", "social-crew"):
                        last.setdefault(label + "-first", p)
            for label, p in sorted(last.items()):
                name = "copy%d-%s.png" % (i, label)
                if Image:
                    for target in [os.path.join(s.dir, name)] + ([os.path.join(out_dir, name)] if out_dir else []):
                        os.makedirs(os.path.dirname(target), exist_ok=True)
                        Image.open(p).convert("RGB").save(target)
                picked.append(name)
        result["captures"] = picked
        result["stop"] = stop
        result["faults"] = {i: _faults(t) for i, t in texts.items() if _faults(t)}
        result["logs"] = [_relpath(c.log) for c in s.copies.values()]
        result["ok"] = stop == "done" and all(checks.values()) and not result["faults"]
    finally:
        _finish(s, result, t0)
    return result


# ---- tournament set ---------------------------------------------------------------------------------

class _FormReader(html.parser.HTMLParser):
    """The forms of a page as a browser sees them: action, and each field's name and value (checked
    boxes, selected options, text), plus every select's options (value, text)."""

    def __init__(self):
        super().__init__()
        self.forms, self._form, self._select, self._option, self._text = [], None, None, None, None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "form":
            self._form = {"action": a.get("action") or "", "fields": [], "options": {}}
            self.forms.append(self._form)
        if self._form is None:
            return
        if tag == "input" and a.get("name"):
            kind = (a.get("type") or "text").lower()
            if kind in ("checkbox", "radio") and "checked" not in a:
                return
            if kind in ("submit", "button", "image", "file"):
                return
            self._form["fields"].append((a["name"], a.get("value") or ("on" if kind == "checkbox" else "")))
        elif tag == "select" and a.get("name"):
            self._select = {"name": a["name"], "chosen": None, "first": None}
            self._form["options"][a["name"]] = []
        elif tag == "option" and self._select is not None:
            self._option = [a.get("value") or "", "", "selected" in a]
        elif tag == "textarea" and a.get("name"):
            self._text = [a["name"], ""]

    def handle_data(self, data):
        if self._option is not None:
            self._option[1] += data
        if self._text is not None:
            self._text[1] += data

    def handle_endtag(self, tag):
        if tag == "option" and self._option is not None and self._select is not None:
            v, t, sel = self._option
            self._form["options"][self._select["name"]].append((v, t.strip()))
            if self._select["first"] is None:
                self._select["first"] = v
            if sel:
                self._select["chosen"] = v
            self._option = None
        elif tag == "select" and self._select is not None and self._form is not None:
            v = self._select["chosen"] if self._select["chosen"] is not None else self._select["first"]
            if v is not None:
                self._form["fields"].append((self._select["name"], v))
            self._select = None
        elif tag == "textarea" and self._text is not None and self._form is not None:
            self._form["fields"].append(tuple(self._text))
            self._text = None
        elif tag == "form":
            self._form = None


def _web(base, method, path, cookie, form=None):
    """(status, location, text) of a request to the account service's pages as a signed-in browser
    (the game's session token as the session cookie; no redirects followed)."""
    import urllib.error
    import urllib.parse
    import urllib.request

    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *a, **k):
            return None
    data = urllib.parse.urlencode(form, doseq=True).encode() if form is not None else None
    req = urllib.request.Request(base + path, method=method, data=data)
    req.add_header("Cookie", "ffny_session=" + cookie)
    if data is not None:
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
        req.add_header("Origin", "null")
    opener = urllib.request.build_opener(NoRedirect)
    try:
        with opener.open(req, timeout=10) as r:
            return r.status, r.headers.get("Location") or "", r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.headers.get("Location") or "", e.read().decode("utf-8", "replace")


def _web_form(base, cookie, page, action_end, set_fields=None):
    """Posts the form on `page` whose action ends with action_end, as a browser would (its own fields,
    its CSRF token among them), with set_fields replacing values. Returns (status, location, text, form)."""
    st, _, text = _web(base, "GET", page, cookie)
    rd = _FormReader()
    rd.feed(text)
    form = next((f for f in rd.forms if f["action"].endswith(action_end)), None)
    if not form:
        return st, "", "no form %s on %s (%d)" % (action_end, page, st), None
    fields = [(k, v) for k, v in form["fields"] if k not in (set_fields or {})]
    for k, v in (set_fields or {}).items():
        for one in (v if isinstance(v, list) else [v]):
            fields.append((k, one))
    st, loc, text = _web(base, "POST", form["action"], cookie, fields)
    return st, loc, text, form


TSET_PLAYERS = ["Copy 0", "Copy 1"]


def tset(timeout=None, relay_port=27900, lock_timeout=None, log=None, keep_roots=False, extra_env=None, out_dir=None,
         fast_ko=600, rollback="off", rollback_modes=None, picks="7;8"):
    """A tournament set end to end: a local account service (tournaments on) and a relay that asks it; the
    host (test account "Host", the service's admin) makes a tournament with the TYF preset through its pages,
    both players sign up and the host makes their match (best of 3). Both copies enter it from the game
    (RECOMP_NETPLAY=tset: TJOIN 0), play with knockouts at a step that alternate the winner (slot 0, 1, 0),
    and the game-2 and game-3 losers pick their stage (RECOMP_TEST_TSET_PICK, `picks` per copy). Checked:
    both seated with TSET, a TPICK taken, Dave's rule (game 3's choices lack the stage its picker won), the
    set over 2-1 in both games, and the result reported on the service."""
    t0 = time.time()
    timeout = timeout or 900
    s = copies.Session("tset", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    result = {"scenario": "tset", "ok": False}
    shots = {}
    try:
        h = harness()
        acc_port = relay_port + 100
        base = "http://127.0.0.1:%d" % acc_port
        names = ["Host"] + TSET_PLAYERS
        tokens = s.accounts(acc_port, names, extra=["-tournaments", "-social"])
        s.relay(relay_port, rollback=(rollback != "off"),
                extra=["-accounts-internal", "http://127.0.0.1:%d" % (acc_port + 1)] + _rb_relay_extra(rollback, rollback_modes))
        steps = {}
        # the host's tournament: the TYF preset's form, a name and a start in two hours
        import datetime
        start = (datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(hours=2)).strftime("%Y-%m-%dT%H:%M")
        st, loc, text, form = _web_form(base, tokens["Host"], "/tournaments/new?preset=tyf", "/tournaments/new",
                                        {"name": "Local Test Cup", "start": start, "region": "eu"})
        steps["create"] = "%d %s" % (st, loc or text[:200])
        tid = loc.rsplit("/", 1)[-1] if st == 303 and loc.startswith("/tournaments/") else ""
        if not tid.isdigit():
            raise RuntimeError("the tournament was not made: " + steps["create"])
        page = "/tournaments/" + tid
        for n in TSET_PLAYERS:
            st, loc, text, _ = _web_form(base, tokens[n], page, "/signup")
            steps["signup " + n] = "%d %s" % (st, loc or text[:160])
        # the host accepts both sign-ups (the preset's sign-up asks the host), then makes the match
        st, _, text = _web(base, "GET", page, tokens["Host"])
        rd = _FormReader()
        rd.feed(text)
        for act in sorted({f["action"] for f in rd.forms if "/entrants/" in f["action"]}):
            st, loc, text, _ = _web_form(base, tokens["Host"], page, act, {"status": "accepted"})
            steps["accept " + act.rsplit("/", 1)[-1]] = "%d %s" % (st, loc or text[:120])
        # the host's match form: the two players by name, round, kind set
        st, _, text = _web(base, "GET", page, tokens["Host"])
        rd = _FormReader()
        rd.feed(text)
        mf = next((f for f in rd.forms if f["action"].endswith("/matches")), None)
        ids = {}
        for opt in (mf or {}).get("options", {}).get("p1", []):
            ids[opt[1]] = opt[0]
        p1 = next((v for k, v in ids.items() if TSET_PLAYERS[0] in k), None)
        p2 = next((v for k, v in ids.items() if TSET_PLAYERS[1] in k), None)
        st, loc, text, _ = _web_form(base, tokens["Host"], page, "/matches",
                                     {"p1": p1 or "", "p2": p2 or "", "round": "Winners round 1", "kind": "set"})
        steps["match"] = "%d %s" % (st, loc or text[:160])
        result["setup"] = steps
        if "error=" in loc or st != 303:
            raise RuntimeError("the match was not made: " + steps["match"])

        watch = WATCH + ["[NETPLAY] tournament", "over, winner slot"]
        pick_list = (picks or "7;8").split(";")
        for i in range(2):
            shots[i] = os.path.join(s.dir, "shots%d" % i)
            os.makedirs(shots[i], exist_ok=True)
            env = dict(VIRTUAL_PAD_ENV)
            env.update({
                "RECOMP_INPUT_VIRTUAL_PADS": "1",
                "RECOMP_NETPLAY": "tset", "RECOMP_NETPLAY_SERVER": "127.0.0.1:%d" % relay_port,
                "RECOMP_NETPLAY_SERVER_EU": "127.0.0.1:%d" % relay_port,
                "RECOMP_NETPLAY_FIGHTER": str([55, 22][i]),
                "RECOMP_NETPLAY_NAME": TSET_PLAYERS[i],
                "RECOMP_ROLLBACK": _rb_env(rollback),
                "RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
                "RECOMP_PAD_SCRIPT": h.build_pad_script(["getscreeninfo(intmain/mainmenu=lt@0.5"], online_tail(3, 2, i == 1)),
                "RECOMP_ACCOUNT_SERVER": "127.0.0.1:%d" % acc_port,
                "RECOMP_ACCOUNT_TEST_TOKEN": tokens[TSET_PLAYERS[i]],
                "RECOMP_TEST_OPEN_ONLINE": "1",     # the online screens: the set page in the captures
                "RECOMP_TEST_TSET_PICK": pick_list[i % len(pick_list)] + "," + pick_list[(i + 1) % len(pick_list)],
                "RECOMP_OVERLAY_SHOT_DIR": shots[i], "RECOMP_OVERLAY_SHOT_MS": "1500",
            })
            if fast_ko:
                env["RECOMP_TEST_KO_STEP"] = "%d,alt" % int(fast_ko)
            env.update(extra_env or {})
            s.launch(i, env=env, core=2 + 2 * i, watch=watch)
            time.sleep(3)
        deadline = time.time() + timeout
        stop = None
        while time.time() < deadline:
            s.poll()
            cs = list(s.copies.values())
            if any(c.watch.count("[CRASH]") or c.watch.count("[WATCHDOG]") for c in cs) or any(not c.alive() for c in cs):
                stop = "a copy crashed or exited"
                break
            if any(c.watch.count("[NETPLAY] desync in") or c.watch.count("[NETPLAY] gave up") for c in cs):
                time.sleep(5)
                stop = "desync or gave up"
                break
            if all(c.watch.count("over, winner slot") for c in cs):
                stop = "done"
                break
            time.sleep(1)
        else:
            stop = "timeout after %d s" % timeout
        time.sleep(5)       # the set page a moment longer for the captures, the report on its way
        s.poll()
        texts = {i: logparse.read_text(c.log) for i, c in s.copies.items()}

        def lines(i, mark):
            return [l for l in texts[i].splitlines() if mark in l]
        tsets = {i: lines(i, "[NETPLAY] tournament set ") for i in texts}
        picks_made = {i: lines(i, "[NETPLAY] tournament set: picking venue") for i in texts}     # the page's A or the tests' pick
        over = {i: (lines(i, "[NETPLAY] tests: tournament set") and [l for l in lines(i, "[NETPLAY] tests: tournament set") if " over" in l] or [""])[-1]
                for i in texts}
        # Dave's rule: the game-3 pick offered no stage its picker had won (game 1's stage, 10, the start)
        dave = False
        for i in texts:
            for l in tsets[i]:
                if "game 3" in l and "state 2" in l and "choices [" in l:
                    ch = l.split("choices [", 1)[1].split("]", 1)[0].split(",")
                    dave = dave or (len(ch) > 1 and "10" not in ch)
        service = {}
        st, _, text = _web(base, "GET", "/api/tournaments/" + tid, tokens["Host"])
        try:
            service = json.loads(text)
        except ValueError:
            service = {"error": "%d %s" % (st, text[:200])}
        matches = service.get("matches") or []
        m0 = matches[0] if matches else {}
        checks = {
            "relay_caps": all(lines(i, "the relay offers tournament sets") for i in texts),
            "joined": all(lines(i, "joining its room") for i in texts),
            "tset_shown": all(tsets[i] for i in texts),
            "picked": sum(len(picks_made[i]) for i in texts) >= 2,      # games 2 and 3
            "daves_rule": dave,
            "set_over": all(" over, winner slot " in over[i] for i in texts),
            "reported": m0.get("state") in ("reported", "confirmed"),
        }
        result["checks"] = checks
        result["service_match"] = {k: m0.get(k) for k in ("id", "round", "state", "winner", "score", "best_of")} if m0 else service
        result["tset_last"] = {i: (tsets[i] or [""])[-1].split("] ", 1)[-1][:260] for i in texts}
        result["picks"] = {i: [l.split("] ", 1)[-1][:100] for l in picks_made[i]] for i in texts}
        picked = []
        try:
            from PIL import Image
        except ImportError:
            Image = None
        for i, d in shots.items():
            files = sorted(glob.glob(os.path.join(d, "*.bmp")))
            keep = {}
            for p in files:
                base_name = os.path.splitext(os.path.basename(p))[0]
                label = base_name.split("-", 1)[1] if "-" in base_name else "plain"
                keep.setdefault(label, []).append(p)
            for label, ps in sorted(keep.items()):
                for k, p in enumerate([ps[0], ps[-1]] if len(ps) > 1 else ps):
                    name = "copy%d-%s-%d.png" % (i, label, k)
                    if Image:
                        for target in [os.path.join(s.dir, name)] + ([os.path.join(out_dir, name)] if out_dir else []):
                            os.makedirs(os.path.dirname(target), exist_ok=True)
                            Image.open(p).convert("RGB").save(target)
                    picked.append(name)
        result["captures"] = picked[:40]
        result["stop"] = stop
        result["faults"] = {i: _faults(t) for i, t in texts.items() if _faults(t)}
        result["logs"] = [_relpath(c.log) for c in s.copies.values()]
        result["ok"] = stop == "done" and all(checks.values()) and not result["faults"]
    except Exception as e:      # noqa: BLE001
        result["error"] = str(e)[:400]
    finally:
        _finish(s, result, t0)
    return result


# ---- turf -----------------------------------------------------------------------------------------

TURF_VENUE = 6      # The Terror Dome (the Bronx): open at the start, a plain venue of the relay's pool
# after the battle: out of its room, then the TURF page with the new holder. The second leaveroom must find no
# room ("room_stays_left"): on 2026-11-08 the ROOM answering the last fight's OVER reached the fighters after their
# LEAVE and brought the room (and the battle screen) back. The fighters' LEAVE reaches the relay some 20-25 s after
# the knockout because their game is on the fight's results and Rematch until then (the steps wait for the
# online screens), not because the LEAVE is late.
_TURF_AFTER = "wait:5;ui:leaveroom;wait:6;ui:leaveroom;wait:4;ui:turf;wait:3;ui:turfsel=%d;wait:4;ui:turflog=%d;wait:3;log" % (TURF_VENUE, TURF_VENUE)
TURF_NET = [
    # the BXK Boss: both crews full (the ladder), then the hub's TURF WAR with A, the venue, CLAIM, RHR, A
    "wait:3;crew:Bronx Kings:BXK;wait:4;crewinvite:Copy 1;ladderwait:RHR:2;ladderwait:BXK:2;ui:close;ui:turfhub;wait:1;"
    "ui:pad=a;wait:4;ui:turfsel=%d;wait:3;ui:turflog=%d;ui:pad=a;wait:3;ui:turfpick=RHR;wait:2;ui:pad=a;wait:4;"
    "ui:turflog=%d;log;battledone;" % (TURF_VENUE, TURF_VENUE, TURF_VENUE) + _TURF_AFTER,
    "crewjoin;log;battledone;" + _TURF_AFTER,
    # the RHR Boss accepts the claim battle; afterwards its map must say why it may not challenge (protection)
    "wait:3;crew:Red Hook Riders:RHR;wait:4;crewinvite:Copy 3;battlewait;battleaccept;log;battledone;" + _TURF_AFTER,
    # an RHR member (copy 1 the BXK one): the page read-only; the losing crew's Boss: protection
    "crewjoin;log;battledone;" + _TURF_AFTER,
]
TURF_OFF_NET = "wait:10;ui:close;ui:turfhub;wait:3;log"


def _http(method, url, token=None, body=None):
    """(status, text) of a request to the local account service."""
    import urllib.error
    import urllib.request
    req = urllib.request.Request(url, method=method, data=body.encode() if body is not None else None)
    if token:
        req.add_header("Authorization", "Bearer " + token)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:      # noqa: BLE001
        return 0, str(e)


def _turf_venue(text, venue):
    """The fields of the map text's venue=<venue>|... line (docs/turf-war.md section 4), [] none."""
    for line in (text or "").splitlines():
        if line.startswith("venue=%d|" % venue):
            return line.split("=", 1)[1].split("|")
    return []


def turf(timeout=None, relay_port=27900, off=False, lock_timeout=None, log=None, keep_roots=False, extra_env=None,
         out_dir=None, fast_ko=600, rollback="off", rollback_modes=None):
    """Turf War end to end (social stage 4): a local account service with Turf War on (-turf-switch with
    fresh_hours=0, -social -social-test-battles: crews of two play 2 v 2) and a relay that asks it; four copies
    in two crews of two (BXK: copies 0 and 1, RHR: 2 and 3, the first of each the Boss). Copy 0 opens TURF WAR
    from the hub with A, picks The Terror Dome (open), CLAIM, RHR, A (the page's POST); copy 2 accepts the crew
    battle, every copy joins it and fights it at that venue (knockout at a step); the relay reports it and the
    winner holds the venue: the map text (GET /api/turf?format=text), the turf events and each copy's TURF
    page after it. Then: RHR's Boss may not challenge again (why_not on its page and a 409 on the POST), RHR's
    member sees the page read-only. off: Turf War off, one copy: the hub hides TURF WAR, /api/turf is 404.
    out_dir: the chosen captures (the TURF page before and after, the crew picker, the battle) as PNG."""
    t0 = time.time()
    n = 1 if off else 4
    timeout = timeout or (300 if off else 1200)
    s = copies.Session("turf", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    result = {"scenario": "turf" + (" off" if off else ""), "ok": False}
    shots = {}
    steps = [TURF_OFF_NET] if off else TURF_NET
    try:
        h = harness()
        acc_port = relay_port + 100
        names = ["Copy %d" % i for i in range(n)]
        flags = ["-social", "-social-test-battles"]
        if not off:
            switch = os.path.join(s.dir, "turf-enabled")
            with open(switch, "w") as f:
                f.write("fresh_hours=0\n")      # crews made a minute ago: their battle still counts
            flags += ["-turf-switch", switch]
        tokens = s.accounts(acc_port, names, extra=flags)
        s.relay(relay_port, rollback=(rollback != "off"),
                extra=["-accounts-internal", "http://127.0.0.1:%d" % (acc_port + 1)] + _rb_relay_extra(rollback, rollback_modes))
        watch = WATCH + ["[SOCIAL-TEST] step", "[SOCIAL-UI] turf", "[ONLINE] turf", "[NETPLAY] crew battle"]
        for i in range(n):
            shots[i] = os.path.join(s.dir, "shots%d" % i)
            os.makedirs(shots[i], exist_ok=True)
            env = dict(VIRTUAL_PAD_ENV)
            env.update({
                "RECOMP_INPUT_VIRTUAL_PADS": "1",
                "RECOMP_NETPLAY": "crew", "RECOMP_NETPLAY_SERVER": "127.0.0.1:%d" % relay_port,
                "RECOMP_NETPLAY_SERVER_EU": "127.0.0.1:%d" % relay_port,
                "RECOMP_NETPLAY_FIGHTER": str([55, 22, 9, 31][i]),
                "RECOMP_NETPLAY_NAME": names[i],
                "RECOMP_ROLLBACK": _rb_env(rollback),
                "RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
                "RECOMP_PAD_SCRIPT": h.build_pad_script(["getscreeninfo(intmain/mainmenu=lt@0.5"], crew_tail(3, i >= 2)),
                "RECOMP_ACCOUNT_SERVER": "127.0.0.1:%d" % acc_port,
                "RECOMP_ACCOUNT_TEST_TOKEN": tokens[names[i]],
                "RECOMP_SOCIAL": "on",
                "RECOMP_TEST_OPEN_ONLINE": "1",
                "RECOMP_TEST_SOCIAL": steps[i],
                "RECOMP_OVERLAY_SHOT_DIR": shots[i], "RECOMP_OVERLAY_SHOT_MS": "1500",
            })
            if fast_ko:
                env["RECOMP_TEST_KO_STEP"] = "%d,0" % int(fast_ko)
            env.update(extra_env or {})
            s.launch(i, env=env, core=2 + 2 * i, watch=watch)
            time.sleep(3)
        deadline = time.time() + timeout
        stop = None
        while time.time() < deadline:
            s.poll()
            cs = list(s.copies.values())
            if any(c.watch.count("[CRASH]") or c.watch.count("[WATCHDOG]") for c in cs) or any(not c.alive() for c in cs):
                stop = "a copy crashed or exited"
                break
            if any(c.watch.count("[NETPLAY] desync in") or c.watch.count("[NETPLAY] gave up") for c in cs):
                time.sleep(5)
                stop = "desync or gave up"
                break
            if all(c.watch.count("[SOCIAL-TEST] step") >= len(steps[c.index].split(";")) for c in cs):
                stop = "done"
                break
            time.sleep(1)
        else:
            stop = "timeout after %d s" % timeout
        time.sleep(3)
        s.poll()
        texts = {i: logparse.read_text(c.log) for i, c in s.copies.items()}

        def lines(i, mark):
            return [l for l in texts[i].splitlines() if mark in l]

        def venue_log(i):       # the copy's last turflog line for the venue
            return (lines(i, "[SOCIAL-UI] turf venue %d " % TURF_VENUE) or [""])[-1]
        api = "http://127.0.0.1:%d" % acc_port
        if off:
            status, body = _http("GET", api + "/api/turf?format=text")
            checks = {
                "service_404": status == 404,
                "game_off": bool(lines(0, "[ONLINE] turf map: off (HTTP 404)")),
                "hub_hidden": bool(lines(0, "[SOCIAL-UI] turf: hub entry hidden")) and not lines(0, "hub entry shown"),
            }
            result["api"] = "HTTP %d" % status
        else:
            status, public = _http("GET", api + "/api/turf?format=text")
            pub = _turf_venue(public, TURF_VENUE)
            winner = pub[5] if len(pub) > 5 else ""
            lb, lm = (0, 1) if winner == "RHR" else (2, 3)     # the losing crew's Boss and member
            _, rhr_map = _http("GET", api + "/api/turf?format=text", tokens[names[lb]])
            rhr = _turf_venue(rhr_map, TURF_VENUE)
            post_status, post_body = _http("POST", api + "/api/turf/challenge", tokens[names[lb]], json.dumps({"venue": TURF_VENUE}))
            _, ev = _http("GET", "http://127.0.0.1:%d/internal/turf/events?after=0&n=50" % (acc_port + 1))
            try:
                events = json.loads(ev).get("events") or []
            except ValueError:
                events = []
            kinds = [e.get("kind") for e in events]
            before = lines(0, "[SOCIAL-UI] turf venue %d " % TURF_VENUE)[:1]
            results = {i: (lines(i, "result: winner") or [""])[-1] for i in texts}
            checks = {
                "hub_entry": bool(lines(0, "[SOCIAL-UI] turf: hub entry shown")),
                "page_map": bool(lines(0, "[ONLINE] turf map: season")) and bool(before) and "owner 0 []" in before[0]
                            and "officer 1" in before[0] and "CLAIM: PICK A CREW" in before[0],
                "picker_rhr": bool(lines(0, "[SOCIAL-UI] turf: picker up, crews [RHR")),
                "claim_posted": any('/api/turf/challenge {"venue":%d,"crew":' % TURF_VENUE in l and "-> HTTP 200" in l
                                    for l in lines(0, "[ONLINE] turf: POST")),
                "all_joined": all(lines(i, "joining its room") for i in texts),
                "battle_over": all(any("state 2" in l for l in lines(i, "[NETPLAY] crew battle")) for i in texts),
                "result_in_games": all("result: winner" in results[i] for i in texts),
                "map_holder": status == 200 and winner in ("BXK", "RHR") and len(pub) > 8 and pub[8] != "0",
                "claimed_event": "claimed" in kinds,
                # every copy still on the online screens after the battle (some go on into the game's own menus
                # with the pad script, where the ui: steps time out) shows the holder; both Bosses always do
                "pages_show_holder": bool(winner) and all(("[%s]" % winner) in venue_log(i) for i in range(4) if venue_log(i))
                                     and all(venue_log(i) for i in (0, 2)),
                "protection_why": len(rhr) > 19 and rhr[18] == "0" and ("protected" in rhr[19] or "again from" in rhr[19]) and "can 0" in venue_log(lb)
                                  and "actions []" in venue_log(lb) and rhr[19].lower() in venue_log(lb).lower(),
                "protection_post": post_status == 409,
                # left once, out for good: every copy's later leaveroom finds no room (no stale ROOM brought it back)
                "room_stays_left": all(len(lines(i, "[SOCIAL-UI] leaveroom: ")) >= 2 and
                                       lines(i, "[SOCIAL-UI] leaveroom: ")[-1].endswith("not in a room") for i in texts),
                "member_readonly": any(venue_log(i) for i in (1, 3)) and
                                   all("officer 0" in venue_log(i) and "actions []" in venue_log(i) for i in (1, 3) if venue_log(i))
                                   and ("Boss or a Lieutenant" in venue_log(lm) or not venue_log(lm)),
            }
            result["map"] = "|".join(pub[:11])
            result["loser_boss"] = "copy %d" % lb
            result["loser_why"] = rhr[19] if len(rhr) > 19 else None
            result["loser_post"] = "HTTP %d %s" % (post_status, post_body.strip()[:140])
            result["events"] = [e.get("text", "")[:140] for e in events]
            result["results"] = {i: results[i].split("] ", 1)[-1][:200] for i in results}
            result["venue_logs"] = {i: venue_log(i).split("] ", 1)[-1][:260] for i in range(4)}
        result["checks"] = checks
        picked = []
        try:
            from PIL import Image
        except ImportError:
            Image = None
        want = ("turf", "turf-claim", "crewbattle", "crewbattle-over", "plain")
        for i, d in shots.items():
            last = {}
            for p in sorted(glob.glob(os.path.join(d, "*.bmp"))):
                base = os.path.splitext(os.path.basename(p))[0]
                label = base.split("-", 1)[1] if "-" in base else "plain"
                if label in want and (label != "plain" or off):
                    last[label] = p
                    if label == "turf":
                        last.setdefault("turf-first", p)
            for label, p in sorted(last.items()):
                name = "copy%d-%s.png" % (i, label)
                if Image:
                    for target in [os.path.join(s.dir, name)] + ([os.path.join(out_dir, name)] if out_dir else []):
                        os.makedirs(os.path.dirname(target), exist_ok=True)
                        Image.open(p).convert("RGB").save(target)
                picked.append(name)
        if not off:     # the TURF page captured after the battle (the new holder), on the Boss and on a member
            def after(i):
                names = [os.path.basename(p) for p in sorted(glob.glob(os.path.join(shots[i], "*.bmp")))]
                fight = [k for k, b in enumerate(names) if "crewbattle" in b or "fightchat" in b or "versus" in b]
                return any(b.endswith("-turf.bmp") and k > (fight[-1] if fight else -1) for k, b in enumerate(names))
            result["turf_page_after"] = {i: after(i) for i in range(4)}
            checks["turf_page_after"] = any(result["turf_page_after"].values())
        result["captures"] = picked
        result["stop"] = stop
        result["faults"] = {i: _faults(t) for i, t in texts.items() if _faults(t)}
        result["logs"] = [_relpath(c.log) for c in s.copies.values()]
        result["ok"] = stop == "done" and all(checks.values()) and not result["faults"]
    finally:
        _finish(s, result, t0)
    return result


# ---- offline --------------------------------------------------------------------------------------

def offline(type=0, venue=1, f0=55, f1=22, cpu=True, bot=None, fast_ko=600, turbo=False, timeout=420,
            extra_env=None, lock_timeout=None, log=None, fixture=None, keep_roots=False, summary=True,
            shots_every=0):
    """One match from the main menu by automatch: type (0 One on One, 1 Tag, 2/3 FFA), venue (feID),
    fighters f0/f1 (fighter.xml IDs), the second one a CPU unless cpu=False (then the bot or a second
    virtual pad plays it). fast_ko: the knockout step (0: fight it out). Ends after the summary."""
    t0 = time.time()
    s = copies.Session("offline", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    result = {"scenario": "offline", "ok": False,
              "params": {"type": type, "venue": venue, "f0": f0, "f1": f1, "cpu": cpu, "bot": bot,
                         "fast_ko": fast_ko, "turbo": turbo}}
    shots = []
    try:
        h = harness()
        spec = "type=%d,venue=%d,f0=%d,p0=0,f1=%d,p1=%d" % (type, venue, f0, f1, -1 if cpu else 1)
        stages = ["getscreeninfo(intmain/mainmenu=lt@0.5"]
        tail = [] if bot else [h.FIGHT_TAIL]
        if summary:
            tail.append(h.raw_stages(["game.getmatchsummary(=a@4+3x12"]))
        env = {"RECOMP_AUTOMATCH": spec, "RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
               "RECOMP_PAD_SCRIPT": h.build_pad_script(stages, ",".join(tail))}
        if not cpu:
            env.update(VIRTUAL_PAD_ENV)
            env["RECOMP_INPUT_VIRTUAL_PADS"] = "2"
            if bot:
                env["RECOMP_TEST_BOT_SLOTS"] = "0,1"
        if fast_ko:
            env["RECOMP_TEST_KO_STEP"] = "%d,0" % int(fast_ko)
        env.update(_bot_env(bot))
        if turbo:
            env.update(TURBO_ENV)
        env.update(extra_env or {})
        c = s.launch(0, env=env, watch=WATCH, fixture=fixture)
        deadline = time.time() + timeout
        shot_at = None
        done_at = None
        while time.time() < deadline and c.alive():
            s.poll()
            w = c.watch
            if w.count("[CRASH]") or w.count("[WATCHDOG]"):
                time.sleep(2)
                break
            if shot_at is None and w.count('"event":"match_start"'):
                shot_at = time.time() + 6
            if shot_at and time.time() >= shot_at and not shots:
                _png(s, 0, "fight", shots)
                if shots_every:
                    shot_at = time.time() + shots_every
            elif shots_every and shot_at and time.time() >= shot_at and done_at is None:
                _png(s, 0, "fight-%02d" % len(shots), shots)     # FFNY Studio: a series through the fight
                shot_at = time.time() + shots_every
            if done_at is None and (w.count("[FUNCCALL] Game.UpdateMatchRewardsPoints()") or
                                    (not summary and w.count('"event":"match_over"'))):
                done_at = time.time() + 2
            if done_at and time.time() >= done_at:
                break
            time.sleep(0.5)
        if c.alive():
            _png(s, 0, "end", shots)
        s.poll()
        w = c.watch
        text = logparse.read_text(c.log)
        o = logparse.offline_summary(text)
        problems = []
        if not o["started"]:
            problems.append("the match never started (screens: %s)" % ", ".join(
                logparse.analyze_copy(text)["screens"]))
        elif not o["result"]:
            problems.append("no result")
        if summary and o["result"] and not o["rewards"]:
            problems.append("summary not accepted (no UpdateMatchRewardsPoints)")
        if o["crashes"]:
            problems += [cr[0] for cr in o["crashes"]]
        if not c.alive() and c.proc.returncode not in (None, 1) and not o["crashes"]:
            problems.append("exited with %s" % c.proc.returncode)
        hits = {str(k): v for k, v in sorted(o["hits"].items())}
        result.update({
            "ok": not problems,
            "times_s": {"menu": w.first.get(MAIN_MENU), "start_game": w.first.get("[AUTOMATCH] Game.StartGame()"),
                        "fight": w.first.get('"event":"match_start"'), "result": w.first.get('"event":"result"'),
                        "summary_done": w.first.get("[FUNCCALL] Game.UpdateMatchRewardsPoints()")},
            "fight": {"characters": o["fighters"], "hits": hits, "result": o["result"], "fight_s": o["fight_s"],
                      "ko": o["ko"], "summary_cids": o["summary_cids"], "rewards": o["rewards"], "bot": o["bot"]},
            "problems": problems, "log": _relpath(c.log), "shots": shots,
        })
    finally:
        _finish(s, result, t0)
    return result


# ---- online ---------------------------------------------------------------------------------------

def online_tail(matches, per, p2, last_presses=12):
    """Pad presses for N room matches: fight presses from each StartGame, A through each summary
    (anchored on the Nth GetMatchSummary(0): the summary asks once per fighter, `per` calls a match in
    One on One and King of the Hill, three or four in 3-Way, Free for All and Team, and only the one
    for fighter 0 counts the matches whatever the room's size), the last match's summary included."""
    del per     # kept for callers; the fighter-0 anchor needs no count of fighters
    h = harness()
    fight = h.FIGHT_TAIL.replace("right:", "left:") if p2 else h.FIGHT_TAIL
    parts = [fight]
    for m in range(1, matches + 1):
        nth = "#%d" % m if m > 1 else ""
        # The last match's presses can be fewer (turf: 12 ran on past the results into the hub's QUICK MATCH).
        presses = last_presses if m == matches else 12
        parts.append(h.raw_stages(["game.getmatchsummary(0%s=a@4+3x%d" % (nth, presses)]))
        if m < matches:
            parts.append(fight.replace("@game.startgame(", "@game.startgame(#%d" % (m + 1)))
    return ",".join(parts)


def crew_tail(fights, p2):
    """Pad presses for a crew battle's fights (turf): crew battles run King of the Hill style, so a copy fights
    0, 1 or 2 times and online_tail's count of matches is off. After the Nth fight an A every 3 s on each of
    its screens, anchored on the screen (the summary, the rewards, the save, the tips): an entry is over once
    any later anchor is reached (usb_gamepad.c), so the next screen ends the presses of the one before, and the
    Nth Game.Quit (the tips' A back to the online screens, after every fight, the King's too) those of the
    tips. A copy out of the battle never reaches the next StartGame, so nothing presses after its Quit.
    The first Quit closing everything left the King on its second summary (20261108-085153-turf). online_tail's 12 timed A's
    after the summary ran on past the Quit into the hub's QUICK MATCH and a quick room
    (logs/dj/20261108-070806-turf: room_stays_left, pages_show_holder).
    Build 68 takes a King who sits on the Rematch menu off it after 15 s (netplay.c hill_results_tick) through the
    Rematch path, with no tips and no Game.Quit() the script sees, so the Nth tips and Quit anchors of every later
    fight came one fight late, and the last fight's presses, ended by anchors that never came, ran on past the room
    into the hub (QUICK MATCH rooms in logs/dj/20261109-231233-turf, the match-type menu and a room again in
    20261109-233542-turf: room_stays_left). Now each fight's presses after its summary are one A every 3 s,
    from the Nth summary (every fought fight has one, whichever way it is left) to the Nth
    "netplay.hill-back" event, which netplay.c notes once per fought fight when the game is back from its
    results: no A reaches the online screens after a fight."""
    h = harness()
    fight = h.FIGHT_TAIL.replace("right:", "left:") if p2 else h.FIGHT_TAIL
    parts = []
    for k in range(1, fights + 1):
        nth = "#%d" % k if k > 1 else ""
        parts.append(fight.replace("@game.startgame(", "@game.startgame(%s" % nth))
        parts.append("@game.getmatchsummary(0%s,a:4:120:3:0.3" % nth)
        parts.append("@netplay.hill-back%s" % nth)
    return ",".join(parts)


def _save_files(root):
    """{relative path: SHA-256} of a data root's save area, without the cache partitions' images."""
    import hashlib
    out = {}
    save = os.path.join(root, "save")
    for d, _, files in os.walk(save):
        for f in files:
            p = os.path.join(d, f)
            if f.lower().startswith("partition") and os.path.getsize(p) > 1 << 20:
                continue
            with open(p, "rb") as fh:
                out[os.path.relpath(p, save)] = hashlib.sha256(fh.read()).hexdigest()
    return out


# Draft sets: the log line of a step (netplay.c on_draft) -> the name of its capture
DRAFT_SHOTS = {"[NETPLAY] draft: game 1 step 1 turn": "draft-ban", "[NETPLAY] draft: game 1 step 2 turn": "draft-strike",
               "[NETPLAY] draft: game 1 step 3 turn": "draft-blind", "[NETPLAY] draft: game 1 step 8 turn": "draft-reveal",
               "[NETPLAY] draft: game 2 step 4 turn": "draft-venue-ban", "[NETPLAY] draft: game 2 step 7 turn": "draft-counterpick"}


def online(matches=3, mode="1v1", players=2, fighters=None, rollback="off", fast_ko=600, ko_winner=None,
           bot=None, timeout=None, relay_port=27900, base_core=2, keep_going=False, extra_env=None,
           lock_timeout=None, log=None, require_hits=False, stall=300, keep_roots=False, quick=None,
           created=False, cp_records=None, rematch=False, venues=None, cp_pick=None, cp_names=None,
           more_shots=False, spectators=0, copy_env=None, fixtures=None, ranked=None, rollback_modes=None,
           rollback_max_ping=None, leave=None, spectate_after=0, spectate_delay=0, maintenance=0, cpu_level=None,
           linger=None, reports=False, fighter_rules=None, max_ping=None, banned_tier=None):
    """reports (build 68): a local account service with test accounts (not ranked) keeps the copies' F9 reports in
    <session>/reports; the result's `bug_reports` lists them (files, note.txt). With RECOMP_TEST_REPORT_NOTE in a
    copy's environment the run waits, after the matches, up to 45 s for that copy's note after the match to be sent.
    linger: King of the Hill or FFA rotation, the index of a copy with no presses after the main menu: it sits on
    each fight's results (summary page 1) until the game takes it off them (netplay.c hill_results_tick). The
    result's `hill_gaps` lists per copy the seconds from its last fight's decision to the next one under way.
    fighter_rules: "all", "nobanned", "nocreated" or "stock" (build 68): the room's host sets its FIGHTER RULES
    (RECOMP_TEST_FIGHTER_RULES); max_ping: MS, its MAX PING (RECOMP_TEST_MAX_PING); banned_tier: fighter IDs, the
    relay's <updates-dir>/banned-tier.txt. The result's `rules` lists, per copy, the fighters its STARTs named and
    the fighters the room blocked; a START naming a blocked fighter is a problem.
    cpu_level: "easy", "normal" or "hard" (build 67): the room's host sets its CPU level (RECOMP_TEST_CPU_LEVEL);
    the result's `cpu` says, per copy, the level its STARTs named and how often a fight ended with only CPU
    fighters left. maintenance: SECS, the relay's restart countdown from the start (<updates-dir>/maintenance "SECS Server
    update"): the relay holds every new fight; the file goes SECS + 5 s later (the restart that never comes),
    and the matches then run. Every copy must log the countdown, and copy 0 opens its online screens
    (RECOMP_TEST_OPEN_ONLINE) and captures them every 3 s into <session>/maint (the banner, the vote's HELD).
    leave: "N@STEP": copy N leaves the room at fight step STEP of the first match (RECOMP_TEST_LEAVE_STEP) and
    plays on offline; the run is done once the other copies are, it is left out of the match checks, and
    each other copy must go on in lockstep with its port at rest (P_GONE) when two or more stay.
    rollback: "off", "on" (every copy opts in, the relay allows it) or "mandatory" (build 52, rollback
    everywhere: the relay -rollback -rollback-modes <rollback_modes or all>, the copies RECOMP_ROLLBACK=server,
    so only the relay's mode mask makes a match roll back). copy_env: {copy index: {NAME: VALUE}}, extra variables for one copy only; fixtures: per copy a save
    fixture folder ('' none), so the copies' save areas can differ as two players' PCs do.
    created: the relay plays created fighters (relay -created); cp_records: per copy the profile dumps or
    user ID saves (RECOMP_CP_RECORD; several separated by ';', newest first; '' none), for fighter 99;
    cp_pick: per copy which of them it plays (RECOMP_CP_PICK, an index; '' the newest); cp_names: per copy
    the online names of its records, ';' between them in the same order ('' the user ID; RECOMP_CP_NAMES).
    more_shots: besides each copy's fight in match 1, its versus screen and its results screen. The result's `save_written` lists, per copy,
    the save files a run changed (the cache partitions left out). rematch: every match after the first is
    a Rematch from the results menu instead of a new room match. venues: "26" or "1,6": every vote
    offers these in turn (relay -test-venues). spectators: that many more copies watch the players' room
    (RECOMP_NETPLAY=spectate, docs/research/spectator-mode.md); the players ready only once they are in it,
    and every match is checked on them as on the players (checksums, winners, summary). spectate_after: N,
    the spectators start only once copy 0 has finished match N (its rewards), so they come while the
    players' match runs (with rematch: on the results or in the next fight) and the players do not wait
    for them; each must then watch at least one later fight, checked against the players' fight with the
    same id (late_spectators in the result); spectate_delay: that many seconds later still (into the
    next fight). ranked: "classic" or
    "open" (or "classic,open"): a ranked set instead of a quick match, against a local account service
    (accounts -ranked -test-accounts) the relay asks (-accounts-internal); the copies sign in with its test
    sessions, queue (RECOMP_NETPLAY=ranked, RECOMP_TEST_RANKED_MODES), accept and play the set's games.
    ranked "draft": a Draft set; the copies make legal choices in its pick and ban (RECOMP_TEST_DRAFT=auto),
    and each copy's draft pages are captured (game 1's bans, its blind pick, game 2's venue ban and picks)."""
    t0 = time.time()
    mode = mode.lower()
    if mode not in MODES:
        raise ValueError("mode: " + ", ".join(MODES))
    qtype, per = MODES[mode]
    # FFA rotation: three to eight copies; with five or more four fight and the others wait their turn
    players = max(2, min(10 if mode == "koth" else 8 if mode == "ffarot" else 4, int(players)))
    if mode == "ffarot":
        players = max(3, players)
    # a copy that sits out a fight counts its matches apart from the others: the test knockout's
    # alternating winner would differ between the fighters, so slot 0 (the first fighter) wins
    rot_wait = mode == "ffarot" and players > 4
    leaver = None
    if leave:
        who, _, step = str(leave).partition("@")
        leaver = (int(who), int(step or 300))
    fighters = list(fighters or [55, 22, 9, 31])
    timeout = timeout or (180 + 240 * matches * (1 if fast_ko or bot else 3))
    winner = ko_winner or ("0" if mode == "koth" or rot_wait else "alt")
    s = copies.Session("online", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    rot_relay_log = None
    result = {"scenario": "online", "ok": False,
              "params": {"matches": matches, "mode": mode, "players": players, "fighters": fighters[:players],
                         "rollback": rollback, "fast_ko": fast_ko, "bot": bot, "spectators": spectators,
                         "spectate_after": spectate_after, "spectate_delay": spectate_delay}}
    shots, problems = [], []
    stop_reason = None
    logs = {}
    try:
        relay_extra = (["-created"] if created else []) + (["-test-venues", str(venues)] if venues else [])
        if rollback_max_ping:   # build 54: the relay's rollback ping limit (else the game's default)
            relay_extra += ["-rollback-max-ping", str(int(rollback_max_ping))]
            result["params"]["rollback_max_ping"] = int(rollback_max_ping)
        tokens = {}
        note_copies = []
        if ranked or reports:
            acc_port = relay_port + 100
            tokens = s.accounts(acc_port, ["Copy %d" % i for i in range(players)])
            relay_extra += ["-accounts-internal", "http://127.0.0.1:%d" % (acc_port + 1)]
            if ranked:
                result["params"]["ranked"] = ranked
            else:
                result["params"]["reports"] = True
        maint_timer = None
        if banned_tier is not None:     # build 68: the relay's banned tier (room FIGHTER RULES)
            updates = os.path.join(s.dir, "updates")
            os.makedirs(updates, exist_ok=True)
            with open(os.path.join(updates, "banned-tier.txt"), "w") as f:
                f.write("".join("%d\n" % int(x) for x in banned_tier))
            relay_extra += ["-updates-dir", updates]
            result["params"]["banned_tier"] = [int(x) for x in banned_tier]
        if maintenance:     # build 64: the relay's restart countdown holds every new fight until the file goes
            updates = os.path.join(s.dir, "updates")
            os.makedirs(updates, exist_ok=True)
            maint_file = os.path.join(updates, "maintenance")
            with open(maint_file, "w") as f:
                f.write("%d Server update" % int(maintenance))
            if "-updates-dir" not in relay_extra:
                relay_extra += ["-updates-dir", updates]
            result["params"]["maintenance"] = int(maintenance)
            def end_maintenance():
                try:
                    os.remove(maint_file)
                except OSError:
                    pass
            maint_timer = threading.Timer(int(maintenance) + 5, end_maintenance)
            maint_timer.daemon = True
            maint_timer.start()
            timeout += int(maintenance) + 5
        s.relay(relay_port, rollback=(rollback != "off"), extra=relay_extra + _rb_relay_extra(rollback, rollback_modes))
        rot_relay_log = os.path.join(s.dir, "relay%d.err" % (len(s.relays) - 1))
        before = {}
        koth = mode == "koth"
        draft = bool(ranked) and "draft" in ranked
        watch = WATCH + list(DRAFT_SHOTS) if draft else WATCH
        if reports:
            watch = watch + [NOTE_SENT]
        for i in range(players):
            # which way the copy's fighter approaches: Team puts room slots 0 and 1 on the left and 2 and
            # 3 on the right; on the hill (the KO winner is slot 0) copy 0 stays king on the left and
            # the others come in as player 2 on the right
            p2 = i >= 2 if mode == "team" else i >= 1 if koth else i % 2 == 1
            env = dict(VIRTUAL_PAD_ENV)
            env.update({
                "RECOMP_INPUT_VIRTUAL_PADS": "1",
                "RECOMP_NETPLAY": "quick", "RECOMP_NETPLAY_SERVER": "127.0.0.1:%d" % relay_port,
                "RECOMP_NETPLAY_QUICK": quick or "%d,%d" % (qtype, players),
                "RECOMP_NETPLAY_FIGHTER": str(fighters[i % len(fighters)]),
                "RECOMP_NETPLAY_NAME": "Copy %d" % i,
                "RECOMP_ROLLBACK": _rb_env(rollback),
                "RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
                # on the hill a copy that keeps winning (or keeps being called) fights more than `matches`
                # fights before the others reach that count: presses for a few more, or it sat on its last
                # results and the other fighter gave up waiting (a test artefact, 2026-11-03); the game
                # ignores a script over 4095 characters, so no more than that
                "RECOMP_PAD_SCRIPT": harness().build_pad_script(["getscreeninfo(intmain/mainmenu=lt@0.5"])
                                     if linger is not None and i == int(linger) else
                                     _koth_script(matches, per, p2) if koth or rot_wait else
                                     harness().build_pad_script(["getscreeninfo(intmain/mainmenu=lt@0.5"],
                                                                online_tail(matches, per, p2)),
            })
            if ranked:
                env.update({"RECOMP_NETPLAY": "ranked", "RECOMP_RANKED": "1", "RECOMP_TEST_RANKED_MODES": ranked,
                            "RECOMP_ACCOUNT_SERVER": "127.0.0.1:%d" % (relay_port + 100),
                            "RECOMP_ACCOUNT_TEST_TOKEN": tokens["Copy %d" % i]})
                if draft:
                    env["RECOMP_TEST_DRAFT"] = "auto"
            if players > 2:
                # ready only once every copy is in the room: the relay starts a vote (or calls the hill's
                # first pair) as soon as two are there and ready, and a later copy then opened a room of
                # its own (src/hooks/netplay.c autostart_thread)
                env["RECOMP_TEST_READY_PLAYERS"] = str(players)
            if spectators and not spectate_after:   # the spectators watch from the first match on
                env["RECOMP_TEST_READY_SPECTATORS"] = str(spectators)
            if fast_ko:
                env["RECOMP_TEST_KO_STEP"] = "%d,%s" % (int(fast_ko), winner)
            if cpu_level:
                env["RECOMP_TEST_CPU_LEVEL"] = cpu_level
            if fighter_rules:
                env["RECOMP_TEST_FIGHTER_RULES"] = fighter_rules
            if max_ping:
                env["RECOMP_TEST_MAX_PING"] = str(int(max_ping))
            env.update(_bot_env(bot))
            if cp_records and i < len(cp_records) and cp_records[i]:
                recs = [os.path.abspath(r) for r in cp_records[i].split(";") if r]
                env["RECOMP_CP_RECORD"] = ";".join(recs)
                names = cp_names[i].split(";") if cp_names and i < len(cp_names) and cp_names[i] else []
                if any(names):      # the setting online.created_names, keyed by each record's path
                    esc = lambda s: s.replace("%", "%25").replace(";", "%3B").replace("=", "%3D")
                    env["RECOMP_CP_NAMES"] = ";".join("%s=%s" % (esc(r), esc(n)) for r, n in zip(recs, names) if n)
            if cp_pick and i < len(cp_pick) and str(cp_pick[i]).strip():
                env["RECOMP_CP_PICK"] = str(int(cp_pick[i]))
            if rematch:
                env["RECOMP_PAD_SCRIPT"] = harness().build_pad_script(["getscreeninfo(intmain/mainmenu=lt@0.5"],
                                                                       rematch_tail(matches, p2))
            if reports and not ranked:
                env.update({"RECOMP_ACCOUNT_SERVER": "127.0.0.1:%d" % (relay_port + 100),
                            "RECOMP_ACCOUNT_TEST_TOKEN": tokens["Copy %d" % i]})
            for k, v in (extra_env or {}).items():     # NAME@N: only copy N gets it
                name, _, only = k.partition("@")
                if not only or (only.isdigit() and int(only) == i):
                    env[name] = v
            env.update((copy_env or {}).get(i, {}))
            if reports and env.get("RECOMP_TEST_REPORT_NOTE"):
                note_copies.append(i)
            if maintenance and i == 0:
                env.update({"RECOMP_OVERLAY_SHOT_DIR": os.path.join(s.dir, "maint"), "RECOMP_OVERLAY_SHOT_MS": "3000",
                            "RECOMP_TEST_OPEN_ONLINE": "1"})     # the online screens up: the room, the vote, the banner
                os.makedirs(os.path.join(s.dir, "maint"), exist_ok=True)
            if leaver and i == leaver[0]:
                env["RECOMP_TEST_LEAVE_STEP"] = str(leaver[1])
            fx = fixtures[i] if fixtures and i < len(fixtures) and fixtures[i] else None
            root = s.make_root(i, fixture=os.path.abspath(fx) if fx else None)
            before[i] = _save_files(root)
            s.launch(i, env=env, root=root, core=base_core + 2 * i, watch=watch)
            time.sleep(3)           # the first copy opens the room and is player 1
        def launch_spectator(k):
            # a spectator: no fighter, no presses after the main menu (its game goes through the results by
            # itself); the same test knockout as the players, since that is applied on every PC
            i = players + k
            env = dict(VIRTUAL_PAD_ENV)
            env.update({
                "RECOMP_INPUT_VIRTUAL_PADS": "1",
                "RECOMP_NETPLAY": "spectate", "RECOMP_NETPLAY_SERVER": "127.0.0.1:%d" % relay_port,
                "RECOMP_NETPLAY_NAME": "Spectator %d" % k,
                "RECOMP_ROLLBACK": "off",
                "RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
                "RECOMP_PAD_SCRIPT": harness().build_pad_script(["getscreeninfo(intmain/mainmenu=lt@0.5"]),
            })
            if fast_ko:
                env["RECOMP_TEST_KO_STEP"] = "%d,%s" % (int(fast_ko), winner)
            for k, v in (extra_env or {}).items():     # NAME@N: only copy N gets it
                name, _, only = k.partition("@")
                if not only or (only.isdigit() and int(only) == i):
                    env[name] = v
            env.update((copy_env or {}).get(i, {}))
            root = s.make_root(i)
            before[i] = _save_files(root)
            s.launch(i, env=env, root=root, core=base_core + 2 * i, watch=WATCH)
        late = set(range(players, players + spectators)) if spectate_after else set()
        spectate_due = None
        if not spectate_after:
            for k in range(spectators):
                launch_spectator(k)
        deadline = time.time() + timeout
        shot_due = None
        more_done = {}
        while time.time() < deadline:
            s.poll()
            cs = list(s.copies.values())
            rewards = [c.watch.count("[FUNCCALL] Game.UpdateMatchRewardsPoints()") for c in cs]
            if linger is not None:
                # a copy taken off its results by the game may skip the rewards (and the lingering one always
                # does): each fight's summary counts instead
                rewards = [c.watch.count("[FUNCCALL] Game.GetMatchSummary(0)") for c in cs]
            if spectate_after and len(cs) == players and rewards[0] >= spectate_after and not spectate_due:
                spectate_due = time.time() + float(spectate_delay or 0)
            if spectate_due and len(cs) == players and time.time() >= spectate_due:
                log("copy 0 finished match %d: the spectators start" % spectate_after) if log else None
                for k in range(spectators):
                    launch_spectator(k)
                continue
            if spectate_after and len(cs) == players:
                done = False
            elif spectate_after:    # the players' matches, and a watched fight on every late spectator
                done = all(n >= matches for c, n in zip(cs, rewards) if c.index not in late) and \
                    all(n >= 1 for c, n in zip(cs, rewards) if c.index in late)
            elif koth and len(cs) > 2:
                # the hill: copy 0 (the knockout winner's slot) fights every match, the others take turns
                # as the challenger, so the matches are done when the king's and the challengers' are
                done = rewards[0] >= matches and sum(rewards[1:]) >= matches
            elif rot_wait:
                # FFA rotation with copies waiting: four fight each match, so the matches are done once
                # the copies' summaries add up to four a match and every copy has fought
                done = sum(rewards) >= 4 * matches and min(rewards) >= 1
            else:
                done = all(n >= matches for c, n in zip(cs, rewards) if not (leaver and c.index == leaver[0]))
            if done:
                time.sleep(3)
                stop_reason = "done"
                break
            dead = [c for c in cs if not c.alive()]
            crashed = [c for c in cs if c.watch.count("[CRASH]") or c.watch.count("[WATCHDOG]")]
            if dead or crashed:
                time.sleep(3)
                stop_reason = "copy %s %s" % ((dead or crashed)[0].index, "exited" if dead else "crashed")
                break
            for c in cs:      # before the stop below: the other copy gives up on a hung one
                if c.watch.count(HANG_MARK) >= 2 and not getattr(c, "hang_captured", False):
                    c.hang_captured = True
                    problems.append("copy %d hung; threads in %s" % (c.index, _relpath(capture_hang(s, c))))
            if not keep_going and any(c.watch.count("[NETPLAY] desync in") or c.watch.count("[NETPLAY] gave up") for c in cs):
                time.sleep(5)
                stop_reason = "desync or gave up"
                break
            if draft:       # the draft's pages, a second into each captured step
                for mark, name in DRAFT_SHOTS.items():
                    if cs[0].watch.count(mark) and ("draft", name) not in more_done:
                        more_done[("draft", name)] = time.time() + 1.0
                    due = more_done.get(("draft", name))
                    if isinstance(due, float) and time.time() >= due:
                        more_done[("draft", name)] = True
                        for k in range(len(cs)):
                            _png(s, k, "%s-copy%d" % (name, k), shots)
            if shot_due is None and cs[0].watch.count(" starts in "):
                shot_due = time.time() + 8
            if more_shots and shot_due and not more_done.get("versus") and time.time() >= shot_due - 7:
                more_done["versus"] = True      # the versus screen while the match loads
                for k in range(len(cs)):
                    _png(s, k, "versus-m1-copy%d" % k, shots)
            if shot_due and time.time() >= shot_due and not any("fight-m1" in p for p in shots):
                for k in range(len(cs)):
                    _png(s, k, "fight-m1-copy%d" % k, shots)
            if more_shots:      # the results: three seconds after each copy's match summary is asked for
                for k, c in enumerate(cs):
                    if c.watch.count("[FUNCCALL] Game.GetMatchSummary(") and ("summary", k) not in more_done:
                        more_done[("summary", k)] = time.time() + 3
                    due = more_done.get(("summary", k))
                    if isinstance(due, float) and time.time() >= due:
                        more_done[("summary", k)] = True
                        _png(s, k, "summary-m1-copy%d" % k, shots)
            if stall and time.time() - max(c.watch.last_change for c in cs) > stall:
                stop_reason = "stalled: no front-end, netplay or telemetry line for %d s" % stall
                break
            time.sleep(1.0)
        else:
            stop_reason = "timeout after %d s" % timeout
        if note_copies and stop_reason == "done":
            # the note offer comes once the room's match is over: give the copies time to type and send it
            until = time.time() + 45
            while time.time() < until and not all(s.copies[i].watch.count(NOTE_SENT) for i in note_copies):
                s.poll()
                time.sleep(1)
            if not all(s.copies[i].watch.count(NOTE_SENT) for i in note_copies):
                problems.append("no note after the match was sent within 45 s (copies %s)" % note_copies)
        if reports:
            result["bug_reports"] = _bug_reports(os.path.join(s.dir, "reports"))
        if ranked and stop_reason == "done":
            # the set is rated once the last game's reports are in (the account service's report window,
            # 45 s): wait for ranked-sets.jsonl, then each copy's results page and its MY STATS tab
            sets = os.path.join(s.dir, "ranked-sets.jsonl")
            until = time.time() + 150
            while time.time() < until and not (os.path.isfile(sets) and os.path.getsize(sets) > 0):
                s.poll()
                time.sleep(2)
            rated = os.path.isfile(sets) and os.path.getsize(sets) > 0
            result["ranked_sets"] = open(sets, encoding="utf-8").read().strip().splitlines() if rated else []
            if not rated:
                problems.append("the set was not rated within 150 s of its last game")
            time.sleep(8)       # the results page asks the account service again every 3 s
            for k in range(len(s.copies)):
                _png(s, k, "results-copy%d" % k, shots)
        if stop_reason != "done":
            for c in s.copies.values():
                if c.alive():
                    st = s.control(c.index, "state")
                    problems.append("copy %d at stop: screen %s, %s" % (c.index, st.get("screen", "?"),
                                                                        (st.get("online") or {}).get("status", st.get("error", ""))[:80]))
                    _png(s, c.index, "stop-copy%d" % c.index, shots)
        s.poll()
        logs = {i: c.log for i, c in s.copies.items()}
        for c in s.copies.values():
            if c.alive():
                c.proc.kill()
                c.proc.wait(60)
        written = {}
        for i, c in s.copies.items():
            after = _save_files(c.root)
            changed = sorted(k for k in set(after) | set(before.get(i, {})) if after.get(k) != before.get(i, {}).get(k))
            written[str(i)] = changed
        result["save_written"] = written
    finally:
        _finish(s, result, t0)
    analyses = {i: logparse.analyze_copy(logparse.read_text(p)) for i, p in logs.items()}
    if spectate_after:
        result["late_spectators"] = late_spectator_check(analyses, players, spectators, problems)
    if leaver:
        result["leave"] = leave_check(logs, leaver, players, problems)
        analyses.pop(leaver[0], None)
    rows, ok = online_rows(analyses, matches, require_hits, problems)
    if cpu_level or winner == "humans":
        result["cpu"] = cpu_check(logs, cpu_level, winner == "humans", leaver, problems, players)
    if fighter_rules or max_ping or banned_tier is not None:
        result["rules"] = rules_check(logs, fighter_rules, banned_tier, problems)
        ok = ok and not any(p.startswith("cpu:") for p in problems)
    if mode in ("koth", "ffarot"):
        result["hill_gaps"], result["hill_forced"], result["hill_gaps_watched"] = hill_gap_check(logs, linger, problems)
        if linger is not None:      # taken off the summary by the game: no rewards step (expected)
            problems[:] = [p for p in problems if not re.match(r"m\d+ copy \d+: summary not accepted", p)]
            ok = not problems       # online_rows failed on those alone otherwise
    if mode == "ffarot":
        result["rotation"] = rotation_check(logs, rot_relay_log, matches, players, problems, leaver)
    if maintenance:
        told = [i for i, p in logs.items() if "the relay restarts in" in logparse.read_text(p)]
        result["maintenance_told"] = told
        if len(told) < len(logs):
            problems.append("copies %s never logged the restart countdown" % sorted(set(logs) - set(told)))
        shots_dir = os.path.join(s.dir, "maint")
        result["maint_shots"] = _relpath(shots_dir) if os.path.isdir(shots_dir) else None
    if stop_reason != "done":
        problems.insert(0, "stopped: " + stop_reason)
        ok = False
    result.update({"ok": ok and not problems, "stop": stop_reason, "matches": rows, "problems": problems,
                   "logs": [_relpath(p) for p in logs.values()], "shots": shots})
    return result


def cpu_check(logs, level, humans_out, leaver, problems, players=2):
    """Build 67: per copy the CPU level its STARTs named, the slots' difficulty words it set, and the fights
    that ended with only CPU fighters left (with the --ko-winner humans knockout: every fight must)."""
    out = {}
    want = (level or "hard").capitalize()
    for i, path in logs.items():
        if leaver and i == leaver[0]:
            continue
        text = logparse.read_text(path)
        levels = re.findall(r"START: \d+ CPU fighters at level (\w+)", text)
        words = re.findall(r"CPU level \w+: the slots' difficulty words are now ([\d ]+)", text)
        ends = re.findall(r"only CPU fighters are left: fighter (\d+) knocked out at step (\d+); fighter (\d+)", text)
        results = [int(w, 16) for w in re.findall(r"match result: winners 0x([0-9A-Fa-f]+)", text)]
        humans = len(re.findall(r"\[TEST-KO\] match \d+ step \d+: slot \d+ \(human\) knocked out", text))
        out[str(i)] = {"levels": sorted(set(levels)), "difficulty_words": sorted(set(w.strip() for w in words)),
                       "cpu_end_knockouts": len(ends), "winners": sorted(set(int(e[2]) for e in ends)),
                       "humans_knocked_out": humans, "reported_winners": results}
        if level and (not levels or set(levels) != {want}):
            problems.append("cpu: copy %d's STARTs named levels %s, not %s" % (i, sorted(set(levels)), want))
        if level and level != "hard" and not words:
            problems.append("cpu: copy %d never set the CPU slots' difficulty" % i)
        if humans_out and not ends:
            problems.append("cpu: copy %d never ended a fight of CPU fighters only" % i)
        if humans_out and any(w & ((1 << players) - 1) or not w for w in results):
            problems.append("cpu: copy %d reported a fight won by a human or nobody: %s" % (i, results))
    return out


HILL_GAP_LIMIT = 35.0     # s from a fight's decision to the next one under way, with one copy idle on its results


def hill_gap_check(logs, linger, problems):
    """King of the Hill, FFA rotation: per copy the seconds from its last fight's decision to its next fight under
    way (netplay.c), and how often its results were left by themselves. With a lingering copy, a gap of that copy
    over HILL_GAP_LIMIT is a problem (the cap did not hold), and so is a lingering copy never taken off its results.
    (Only that copy's.) A gap with a watched fight in it (WATCH between the decision and the next fight: on the hill a
    challenger who lost waits through the next pair's fight) does not measure the cap and goes to `watched` instead
    of the gaps: build 68's release run counted 39.3 and 40.9 s that were a 34 s watched fight, then the copy taken
    off its results at once when its START came and the fight under way 5.4 s later."""
    import re
    gaps, forced, watched = {}, {}, {}
    for i, p in sorted(logs.items()):
        text = logparse.read_text(p)
        gaps[i], watched[i], saw_watch = [], [], False
        for m in re.finditer(r"\[NETPLAY\] (?:\S.*?: the fight is decided|WATCH: seed|hill: this fight is under way ([\d.]+) s after)",
                             text):
            line = m.group(0)
            if "the fight is decided" in line:
                saw_watch = False
            elif "WATCH: seed" in line:
                saw_watch = True
            else:
                (watched if saw_watch else gaps)[i].append(float(m.group(1)))
        forced[i] = len(re.findall(r"hill: still on the results", text))
    if linger is not None:
        worst = max(gaps.get(int(linger)) or [0.0])
        if worst > HILL_GAP_LIMIT:
            problems.append("hill: %.1f s between a fight's decision and the next fight (limit %.0f)" % (worst, HILL_GAP_LIMIT))
        if not forced.get(int(linger)):
            problems.append("hill: copy %s was never taken off its results" % linger)
    return gaps, forced, watched


def rules_check(logs, fighter_rules, banned_tier, problems):
    """Build 68: per copy the room's rules as its host set them, the fighters its STARTs named, the RANDOM
    re-draws and MAX PING refusals; a START naming a fighter the rules leave out is a problem."""
    bits = {"all": 0, "nobanned": 1, "nocreated": 2, "stock": 3}.get(fighter_rules or "all", 0)
    blocked = set(int(x) for x in (banned_tier or [])) if bits & 1 else set()
    if bits & 2:
        blocked.add(99)
    out = {}
    for i, path in logs.items():
        text = logparse.read_text(path)
        starts = [[int(x) for x in m.split()] for m in re.findall(r"\[NETPLAY\] START: fighters ([\d ]+)", text)]
        named = sorted(set(x for st in starts for x in st))
        out[str(i)] = {"start_fighters": starts, "set": re.findall(r"tests: the room's fighter rules are (\d+), its max ping (\d+)", text)[:1],
                       "random_redraws": re.findall(r"RANDOM fighter again \(the room blocks \d+ fighters\): (\d+)", text),
                       "refused": re.findall(r"refused: (Your ping[^\n]*)", text)[:3]}
        bad = sorted(set(named) & blocked)
        if bad:
            problems.append("rules: copy %d's STARTs named blocked fighters %s" % (i, bad))
    return out


def rotation_check(logs, relay_log, matches, players, problems, leaver=None):
    """FFA rotation (mode ffarot): every copy fought its fights as FFA rotation fights, sent its RESULT
    with the finishing order and saw it in (the relay's HILL moved on); the relay started each fight with
    up to four copies (3-Way or 4-Way), ended it with a winner, rotated the line and called the next ones.
    With five or more copies the ones waiting must come in: every copy fights within the first two fights,
    and the winner of each fight is in the next one. With a leaver (--leave COPY@STEP, in the first
    fight) its copy is not counted and the fights after the first have one fighter fewer."""
    out = {"copies": {}, "relay": {}}
    fighting = min(players, 4)
    left = leaver[0] if leaver else None
    for i, p in logs.items():
        text = logparse.read_text(p)
        c = {"rot_starts": text.count("(an FFA rotation fight)"),
             "decided": text.count("[NETPLAY] FFA rotation: the fight is decided"),
             "result_in": text.count("[NETPLAY] hill: the result is in"),
             "auto_ready": text.count("[NETPLAY] hill: called again straight after the fight")}
        out["copies"][str(i)] = c
        if i == left:
            continue
        want = matches if players <= 4 else 1
        if c["rot_starts"] < want:
            problems.append("copy %d: %d FFA rotation STARTs, want %d" % (i, c["rot_starts"], want))
        if c["decided"] < want:
            problems.append("copy %d: %d FFA rotation fights decided, want %d" % (i, c["decided"], want))
    rtext = logparse.read_text(relay_log) if relay_log and os.path.isfile(relay_log) else ""
    fights = re.findall(r"koth: fight venue=\d+ mode=(\d+) slots=\[([\d ]+)\]", rtext)
    overs = re.findall(r"koth: fight over how=(\d+) winner=(\d+) loser=(\d+) streak=(\d+)", rtext)
    calls = re.findall(r"koth: calling ((?:c=\S+(?: and )?)+)", rtext)
    want_mode = "3" if fighting == 4 else "2"
    out["relay"] = {"fights": ["mode %s slots %s" % f for f in fights],
                    "over": ["how %s winner %s streak %s" % (o[0], o[1], o[3]) for o in overs],
                    "calls": len(calls)}
    if len(fights) < matches:
        problems.append("relay: %d FFA rotation fights started, want %d" % (len(fights), matches))
    for k, (mode, slots) in enumerate(fights[:matches]):
        n = fighting if k == 0 or left is None else min(players - 1, 4)
        wm = want_mode if n == fighting else ("3" if n == 4 else "2" if n == 3 else "0")
        if mode != wm or len(slots.split()) != n:
            problems.append("relay: fight %d mode %s slots [%s], want mode %s with %d" % (k + 1, mode, slots, wm, n))
    if len(overs) < matches:
        problems.append("relay: %d fights over, want %d" % (len(overs), matches))
    for k, o in enumerate(overs[:matches]):
        if o[0] != "1":
            problems.append("relay: fight %d ended how=%s (want 1, won)" % (k + 1, o[0]))
        elif k + 1 < len(fights) and o[1] not in fights[k + 1][1].split():
            problems.append("relay: fight %d's winner (slot %s) is not in fight %d" % (k + 1, o[1], k + 2))
    if players > 4 and len(fights) >= 2:
        seen = set(fights[0][1].split()) | set(fights[1][1].split())
        out["relay"]["fought_in_two"] = sorted(seen, key=int)
        if len(seen) < players:
            problems.append("relay: only slots %s fought in the first two fights of %d copies" % (sorted(seen), players))
    return out


def late_spectator_check(analyses, players, spectators, problems):
    """Spectators that came while the players' match ran (spectate_after): each watched fight against the
    players' fight with the same id (checksums compared and equal, the same winners, no desync); the
    spectators are taken out of the per-match rows."""
    out = {}
    ref = analyses.get(0, {}).get("started", [])
    for i in range(players, players + spectators):
        a = analyses.pop(i, None)
        if a is None:
            problems.append("late spectator copy %d: no log" % i)
            continue
        rows = []
        for m in a["started"]:
            p = next((x for x in ref if x.get("id") == m.get("id")), None)
            row = {"id": m.get("id"), "players_match": ref.index(p) + 1 if p else None, "checks": m.get("checks", 0),
                   "winners": m.get("winners"), "desync": (m.get("desync") or {}).get("step")}
            rows.append(row)
            tag = "late spectator copy %d, fight %s: " % (i, m.get("id"))
            if p is None:
                problems.append(tag + "no player fight has its id")
            elif p.get("winners") != m.get("winners"):
                problems.append(tag + "winners %s, the players' %s" % (m.get("winners"), p.get("winners")))
            if m.get("desync"):
                problems.append(tag + "desync at step %d (%s)" % (m["desync"]["step"], (m.get("desync_detail") or "")[:110]))
            if not m.get("checks"):
                problems.append(tag + "no checksum compared")
            for g in m.get("gave_up", []):
                problems.append(tag + g[:120])
        if not rows:
            problems.append("late spectator copy %d watched no fight" % i)
        out[str(i)] = rows
    return out


def leave_check(logs, leaver, players, problems):
    """A run with a leaver: what each copy that stayed did after the leave (netplay.c P_GONE)."""
    import re
    out = {"copy": leaver[0], "step": leaver[1], "copies": {}}
    rest = set()
    for i, p in sorted(logs.items()):
        text = logparse.read_text(p)
        if i == leaver[0]:
            out["left"] = "RECOMP_TEST_LEAVE_STEP" in text
            continue
        offline = text.count("it finishes offline") + text.count("[NETPLAY] gave up")
        m = re.search(r"\[NETPLAY\] player (\d+)'s port rests from step (\d+)", text)
        out["copies"][i] = {"offline": offline, "gone": text.count("[NETPLAY] player %d is gone" % (leaver[0] + 1)),
                            "rests_from": int(m.group(2)) if m else None,
                            "desync": text.count("[NETPLAY] desync in")}
        if m:
            rest.add(int(m.group(2)))
        if players - 1 >= 2:
            if offline:
                problems.append("copy %d went offline after the leave" % i)
            if not m:
                problems.append("copy %d never agreed the leaver's rest step" % i)
    if len(rest) > 1:
        problems.append("the copies agreed different rest steps: %s" % sorted(rest))
    if not out.get("left"):
        problems.append("copy %d never left" % leaver[0])
    return out


def rematch_tail(matches, p2):
    """The fights of an online rematch series: the first match, then Rematch from the results menu
    for each further one. The menu (battle/rematch) comes after the reward screen's save
    (MemCard.Save(battle/rewardun.swf)) with No chosen; left moves to Rematch (the menu reads left and right, not up and down). Player 1 chooses
    at 5 s, player 2 at 9 s, so player 1's choice is held and starts by itself once player 2's arrives
    (netplay_rematch_tick). The presses are anchored on the screen they are for and held 0.05 s: a
    0.3 s A on an unpaced copy (RECOMP_FLIP_HZ=0) went through two or three screens and chose No, and
    the old route's downs never moved the menu (2026-11-02)."""
    h = harness()
    tail = h.FIGHT_TAIL.replace("right:", "left:") if p2 else h.FIGHT_TAIL
    parts = [tail]
    for k in range(2, matches + 1):
        nth = "#%d" % (2 * k - 3) if k > 2 else ""
        prev = "#%d" % (k - 1) if k > 2 else ""
        choose = "left:3:3.3,a:9:9.05" if p2 else "left:3:3.3,a:5:5.05"
        parts.append("@game.getmatchsummary(%s,a:4:4.05,a:7:7.05" % nth)
        parts.append("@game.updatematchrewardspoints(%s,a:3:3.05" % prev)
        parts.append("@memcard.save( battle/rewardun%s,%s" % (prev, choose))
        parts.append(tail.replace("@game.startgame(", "@game.startgame(#%d" % k))
    if matches > 1:     # the last match's summary too (the run is done once every copy accepted it)
        parts.append("@game.getmatchsummary(#%d,a:4:4.05,a:7:7.05,a:10:10.05" % (2 * matches - 1))
    return ",".join(parts)


def online_rows(analyses, matches, require_hits=False, problems=None):
    """The per-match rows of an online result from each copy's analyze_copy(); appends problems."""
    problems = problems if problems is not None else []
    rows = []
    for i, a in analyses.items():
        for cr in a["crashes"]:
            problems.append("copy %d: %s" % (i, cr[1][0]))
    n = matches
    # King of the Hill with more than two copies: each match is fought by two of them (the others
    # watch), so a copy's k-th match is not the room's k-th; the matches go by their START seed, in
    # the order the copies saw them, each checked on the copies that fought it.
    by_seed = None
    if len(analyses) > 2 and any(m.get("koth") or (m.get("rot") and len(analyses) > 4)
                                 for a in analyses.values() for m in a["started"]):
        by_seed = []
        for i in sorted(analyses):
            for m in analyses[i]["started"]:
                seed = (m.get("start") or {}).get("seed")
                if seed and seed not in by_seed:
                    by_seed.append(seed)
    for k in range(n):
        if by_seed is not None:
            if k >= len(by_seed):
                problems.append("m%d: never started" % (k + 1))
                rows.append({"n": k + 1, "started": False})
                continue
            per = {i: m for i, a in analyses.items() for m in a["started"]
                   if (m.get("start") or {}).get("seed") == by_seed[k]}
            row = {"n": k + 1, "copies": sorted(per)}
            if len(per) < 2:
                problems.append("m%d: started on copy %s only" % (k + 1, ",".join(map(str, sorted(per)))))
        else:
            per = {i: a["started"][k] for i, a in analyses.items() if k < len(a["started"])}
            row = {"n": k + 1}
        if by_seed is None and len(per) < len(analyses):
            missing = [i for i in analyses if i not in per]
            problems.append("m%d: never started on copy %s" % (k + 1, ",".join(map(str, missing))))
            row["started"] = False
            if not per:
                rows.append(row)
                continue
        m0 = per[min(per)]
        st = m0.get("start") or {}
        row["start"] = "%s d%s%s" % (m0.get("mode") or "?", m0.get("delay"), " rematch" if not st else "")
        if st:
            row["venue"] = st.get("venue")
        row["load_s"] = [m.get("load_s") for m in per.values()]
        row["fe"] = [m.get("fe") or "?" for m in per.values()]
        hits = m0.get("hits") or {}
        row["hits"] = [hits.get(sl, 0) for sl in range(max([1] + [x + 1 for x in hits]))] if hits else []
        row["winners"] = m0.get("winners")
        row["ko"] = m0.get("ko")
        row["checks"] = min(m.get("checks", 0) for m in per.values())
        bots = [b for m in per.values() for b in m.get("bot", [])]
        if bots:
            row["bot"] = bots
        row["summary"] = ["%s%s" % ("cid " + ",".join(str(v) for _, v in sorted(m["summary"].items())) if m.get("summary") else "none",
                                    "" if m.get("rewards") else " NOT accepted") for m in per.values()]
        for i, m in per.items():
            tag = "m%d copy %d: " % (k + 1, i)
            if m.get("desync"):
                d = m["desync"]
                problems.append(tag + "desync at step %d (%s)" % (d["step"], (m.get("desync_detail") or "no detail")[:110]))
                if m.get("desync_fields"):
                    problems.append(tag + "desync fields: " + m["desync_fields"][:200])
                row["desync"] = d["step"]
            for g in m.get("gave_up", []):
                problems.append(tag + g[:120])
            if (m.get("fe") or "").startswith("BAD"):
                problems.append(tag + "front-end order " + m["fe"])
            if k < matches and not m.get("rewards"):
                problems.append(tag + "summary not accepted (no UpdateMatchRewardsPoints)")
            if any(v is None or v <= -99 for v in (m.get("summary") or {}).values()):
                problems.append(tag + "summary controller ids %s" % m["summary"])
            if m.get("crash"):
                row["crash"] = m["crash"][:100]
            for u in m.get("unplugged", []) + [r for r in m.get("refused", []) if "game files differ" in r]:
                problems.append(tag + u[:120])
            if any("game files differ" not in r for r in m.get("refused", [])):
                row["refused"] = row.get("refused", 0) + 1
        wins = {json.dumps(m.get("winners")) for m in per.values() if m.get("winners") is not None}
        if len(wins) > 1:
            problems.append("m%d: the copies disagree on the winners: %s" % (k + 1, ", ".join(sorted(wins))))
        # what each game reports to the accounts service ("match result: winners 0x..."): the same on
        # every copy, and in Team the whole winning side
        reported = {json.dumps(m.get("reported")) for m in per.values() if m.get("reported") is not None}
        if reported:
            row["reported"] = m0.get("reported") if m0.get("reported") is not None else json.loads(sorted(reported)[0])
        if len(reported) > 1:
            problems.append("m%d: the copies report different winners: %s" % (k + 1, ", ".join(sorted(reported))))
        if (st.get("type") == 1 and st.get("players") == 4 and row.get("reported") is not None
                and row["reported"] not in ([0, 1], [2, 3]) and not any(m.get("desync") for m in per.values())):
            problems.append("m%d: a Team result names %s, not a whole side" % (k + 1, row["reported"]))
        landed = [x for x in row["hits"] if x]
        if not landed:
            msg = "m%d: no hits landed (telemetry)" % (k + 1)
            if require_hits:
                problems.append(msg)
            else:
                row["note"] = "no hits landed"
        elif len(landed) < min(2, len(row["hits"])):
            row["note"] = "one slot landed no hits"
            if require_hits:
                problems.append("m%d: only one slot landed hits" % (k + 1))
        rows.append(row)
    more = (len(by_seed) - matches if by_seed is not None
            else max([0] + [len(a["started"]) - matches for a in analyses.values()]))
    if more > 0 and rows:
        rows[-1]["then"] = "%d more match(es) started (not checked)" % more
    return rows, not problems


def format_result(r):
    return compact_json(r)


# ---- gallery --------------------------------------------------------------------------------------

def gallery(page_ms=1500, timeout=300, lock_timeout=None, log=None, keep_roots=False, extra_env=None):
    """The online screens' sample pages (RECOMP_TEST_ONLINE_GALLERY), one PNG each. extra_env adds
    switches, e.g. RECOMP_TEST_ONLINE_GALLERY_ONLY=stats or RECOMP_BUTTON_ICONS=playstation."""
    t0 = time.time()
    s = copies.Session("gallery", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    result = {"scenario": "gallery", "ok": False}
    try:
        out = os.path.join(s.dir, "gallery")
        os.makedirs(out, exist_ok=True)
        h = harness()
        env = {"RECOMP_TEST_OPEN_ONLINE": "1", "RECOMP_TEST_ONLINE_GALLERY": str(int(page_ms)),
               "RECOMP_OVERLAY_SHOT_DIR": out, "RECOMP_OVERLAY_SHOT_MS": str(int(page_ms)),
               "RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
               "RECOMP_PAD_SCRIPT": h.build_pad_script(["getscreeninfo(intmain/mainmenu=back@9999"])}
        env.update(extra_env or {})
        c = s.launch(0, env=env, watch=WATCH)
        deadline = time.time() + timeout
        last_n, last_t = 0, time.time()
        while time.time() < deadline and c.alive():
            n = len(glob.glob(os.path.join(out, "*.bmp")))
            if n != last_n:
                last_n, last_t = n, time.time()
            elif n and time.time() - last_t > max(6.0, 4 * page_ms / 1000.0):
                break
            time.sleep(1.0)
        s.poll()
        pngs = []
        try:
            from PIL import Image
            for b in sorted(glob.glob(os.path.join(out, "*.bmp"))):
                p = b[:-4] + ".png"
                Image.open(b).convert("RGB").save(p)
                os.remove(b)
                pngs.append(os.path.basename(p))
        except ImportError:
            pngs = [os.path.basename(b) for b in sorted(glob.glob(os.path.join(out, "*.bmp")))]
        text = logparse.read_text(c.log)
        result.update({"ok": bool(pngs) and not _faults(text), "pages": len(pngs), "dir": _relpath(out),
                       "first": pngs[:3], "faults": _faults(text), "log": _relpath(c.log)})
    finally:
        _finish(s, result, t0)
    return result
