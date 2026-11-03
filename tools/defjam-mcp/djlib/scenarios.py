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
import importlib.util
import json
import os
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
}

WATCH = ["[CRASH]", "[WATCHDOG]", "[NETPLAY] desync in", "[NETPLAY] gave up", " starts in ",
         "[FUNCCALL] Game.UpdateMatchRewardsPoints()", "[FUNCCALL] Game.GetMatchSummary(", MAIN_MENU,
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

def boot(timeout=240, turbo=False, extra_env=None, lock_timeout=None, log=None, keep_roots=False, goto=None):
    """To the main menu; with goto (story, battle, rewards, online, slotN) on to that entry by name."""
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
        env.update(extra_env or {})
        c = s.launch(0, env=env, watch=WATCH)
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
            if goto:
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
    # copy 0: asks copy 1 to be friends, writes to it, then a line in global chat
    "wait:4;friend:Copy 1;dm:Copy 1:hey Copy 1, run it back?;wait:6;global:eu:anyone on Europe for a quick one?;"
    "wait:42;log",
    # copy 1: accepts once the request has been on screen a moment, answers, then a global line
    "wait:9;accept;wait:3;dm:Copy 0:yo, best of 3?;wait:3;global:eu:gg all, rematch later;wait:40;log",
]
# The window's pages for the captures, then the real input paths: copy 1 opens a room (its friends see
# it), types on the keyboard (ENTER opens, text, ENTER sends, ESC closes); copy 0 uses the pad (BACK
# opens, Y the on-screen keyboard, A types K, left and A types J, START sends "kj", B closes) and then
# joins copy 1's room from FRIENDS (A on JOIN ROOM).
SOCIAL_UI = [
    "3:chat=friends;19:dm=Copy 1;25:chat=global;30:keyboard=yeah lets run it bac;35:close;40:chat=friends;44:chat=crew;"
    "46:chat=global;47:type=;48:close;50:pad=back;51:pad=y;52:pad=a;53:pad=left;54:pad=a;55:pad=start;57:pad=b;58:chat=friends;"
    "59:pad=a;64:chat=room;67:close",
    "5:chat=friends;16:dm=Copy 0;22:chat=global;27:friend=Tavo;32:close;33:room=create;36:chat=room;40:chat=global;41:close;"
    "44:key=enter;45:text=typed on a keyboard;46:key=enter;47:key=escape",
]


def social(timeout=300, relay_port=27900, off=False, lock_timeout=None, log=None, keep_roots=False, extra_env=None,
           out_dir=None):
    """Two copies at the online hub, signed in to a local account service with the social features
    on (accounts -social -test-accounts) and a local relay: copy 0 asks copy 1 to be friends, copy 1
    accepts, both write a private message and a global line (RECOMP_TEST_SOCIAL), and the chat
    window's tabs, the keyboard and the friends list are captured (RECOMP_TEST_SOCIAL_UI and the
    overlay's own captures). off: one copy against a service whose switch file is missing and without
    RECOMP_SOCIAL: nothing new may show. out_dir: the chosen captures as PNG there too."""
    t0 = time.time()
    s = copies.Session("social-off" if off else "social", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    result = {"scenario": "social", "ok": False, "params": {"off": off}}
    try:
        h = harness()
        acc_port = relay_port + 100
        n = 1 if off else 2
        names = ["Copy %d" % i for i in range(n)]
        flags = ["-social-switch", os.path.join(s.dir, "social-enabled")] if off else ["-social"]
        tokens = s.accounts(acc_port, names, extra=flags)
        s.relay(relay_port)
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
                env["RECOMP_SOCIAL"] = "on"
                env["RECOMP_TEST_SOCIAL"] = SOCIAL_NET[i]
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
            if off:
                menu = cs[0].watch.first.get(MAIN_MENU)
                if menu and s.elapsed() > menu + 30:
                    stop = "done"
                    break
            elif (all(c.watch.count("[SOCIAL-TEST] state:") for c in cs) and cs[0].watch.count("[SOCIAL-UI] close") >= 3
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
            checks = {
                "ready": has(0, "[SOCIAL] ready") and has(1, "[SOCIAL] ready"),
                "request_arrived": has(1, "friend request from Copy 0"),
                "friends_both": has(0, "friend added: Copy 1") and has(1, "friend added: Copy 0"),
                "dm_0_to_1": has(1, "dm from Copy 0") and has(0, "dm sent to"),
                "dm_1_to_0": has(0, "dm from Copy 1"),
                "global_sent": has(0, "sent to global eu") and has(1, "sent to global eu"),
                "global_seen": all("global 2" in state[i] or "global 3" in state[i] or "global 4" in state[i] for i in state),
                # both online in the menu at first; copy 1's room reaches copy 0 as a presence change
                "presence": "Copy 0=" in state[1] and "Copy 0=offline" not in state[1] and
                            any("[SOCIAL] presence" in l and ": room" in l for l in lines[0]) and "Copy 1=room" in state[0],
                "pad_back_opens": has(0, "chat opened (Back)"),
                "pad_keyboard_send": has(0, "sent to global eu: kj") and has(1, "global eu from Copy 0: kj"),
                "keyboard_enter_opens": has(1, "chat opened (Enter)"),
                "keyboard_typing_send": has(1, "sent to global eu: typed on a keyboard") and
                                        has(0, "global eu from Copy 1: typed on a keyboard"),
                "join_from_friends": has(0, "joining Copy 1's room"),
            }
            result["state"] = {i: state[i].split("state: ", 1)[-1] for i in state}
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


# ---- offline --------------------------------------------------------------------------------------

def offline(type=0, venue=1, f0=55, f1=22, cpu=True, bot=None, fast_ko=600, turbo=False, timeout=420,
            extra_env=None, lock_timeout=None, log=None, fixture=None, keep_roots=False, summary=True):
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

def online_tail(matches, per, p2):
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
        parts.append(h.raw_stages(["game.getmatchsummary(0%s=a@4+3x12" % nth]))
        if m < matches:
            parts.append(fight.replace("@game.startgame(", "@game.startgame(#%d" % (m + 1)))
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
           more_shots=False, spectators=0, copy_env=None, fixtures=None, ranked=None):
    """copy_env: {copy index: {NAME: VALUE}}, extra variables for one copy only; fixtures: per copy a save
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
    and every match is checked on them as on the players (checksums, winners, summary). ranked: "classic" or
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
    players = max(2, min(4 if mode != "koth" else 10, int(players)))
    fighters = list(fighters or [55, 22, 9, 31])
    timeout = timeout or (180 + 240 * matches * (1 if fast_ko or bot else 3))
    winner = ko_winner or ("0" if mode == "koth" else "alt")
    s = copies.Session("online", lock_timeout=lock_timeout, log=log, keep_roots=keep_roots).open()
    result = {"scenario": "online", "ok": False,
              "params": {"matches": matches, "mode": mode, "players": players, "fighters": fighters[:players],
                         "rollback": rollback, "fast_ko": fast_ko, "bot": bot, "spectators": spectators}}
    shots, problems = [], []
    stop_reason = None
    logs = {}
    try:
        relay_extra = (["-created"] if created else []) + (["-test-venues", str(venues)] if venues else [])
        tokens = {}
        if ranked:
            acc_port = relay_port + 100
            tokens = s.accounts(acc_port, ["Copy %d" % i for i in range(players)])
            relay_extra += ["-accounts-internal", "http://127.0.0.1:%d" % (acc_port + 1)]
            result["params"]["ranked"] = ranked
        s.relay(relay_port, rollback=(rollback == "on"), extra=relay_extra)
        before = {}
        koth = mode == "koth"
        draft = bool(ranked) and "draft" in ranked
        watch = WATCH + list(DRAFT_SHOTS) if draft else WATCH
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
                "RECOMP_ROLLBACK": "on" if rollback == "on" else "off",
                "RECOMP_SCRIPT_ANCHOR": TITLE_ANCHOR,
                "RECOMP_PAD_SCRIPT": harness().build_pad_script(["getscreeninfo(intmain/mainmenu=lt@0.5"],
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
            if spectators:      # the spectators watch from the first match on
                env["RECOMP_TEST_READY_SPECTATORS"] = str(spectators)
            if fast_ko:
                env["RECOMP_TEST_KO_STEP"] = "%d,%s" % (int(fast_ko), winner)
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
            for k, v in (extra_env or {}).items():     # NAME@N: only copy N gets it
                name, _, only = k.partition("@")
                if not only or (only.isdigit() and int(only) == i):
                    env[name] = v
            env.update((copy_env or {}).get(i, {}))
            fx = fixtures[i] if fixtures and i < len(fixtures) and fixtures[i] else None
            root = s.make_root(i, fixture=os.path.abspath(fx) if fx else None)
            before[i] = _save_files(root)
            s.launch(i, env=env, root=root, core=base_core + 2 * i, watch=watch)
            time.sleep(3)           # the first copy opens the room and is player 1
        for k in range(spectators):
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
            env.update(extra_env or {})
            root = s.make_root(i)
            before[i] = _save_files(root)
            s.launch(i, env=env, root=root, core=base_core + 2 * i, watch=WATCH)
        deadline = time.time() + timeout
        shot_due = None
        more_done = {}
        while time.time() < deadline:
            s.poll()
            cs = list(s.copies.values())
            rewards = [c.watch.count("[FUNCCALL] Game.UpdateMatchRewardsPoints()") for c in cs]
            if koth and len(cs) > 2:
                # the hill: copy 0 (the knockout winner's slot) fights every match, the others take turns
                # as the challenger, so the matches are done when the king's and the challengers' are
                done = rewards[0] >= matches and sum(rewards[1:]) >= matches
            else:
                done = all(n >= matches for n in rewards)
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
    rows, ok = online_rows(analyses, matches, require_hits, problems)
    if stop_reason != "done":
        problems.insert(0, "stopped: " + stop_reason)
        ok = False
    result.update({"ok": ok and not problems, "stop": stop_reason, "matches": rows, "problems": problems,
                   "logs": [_relpath(p) for p in logs.values()], "shots": shots})
    return result


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
    if len(analyses) > 2 and any(m.get("koth") for a in analyses.values() for m in a["started"]):
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
