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
         "[NETPLAY] START:", "[TEST-KO] match"]


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
    (the Nth GetMatchSummary call: `per` of them a match), the last match's summary included."""
    h = harness()
    fight = h.FIGHT_TAIL.replace("right:", "left:") if p2 else h.FIGHT_TAIL
    parts = [fight]
    for m in range(1, matches + 1):
        nth = "#%d" % (per * (m - 1) + 1) if m > 1 else ""
        parts.append(h.raw_stages(["game.getmatchsummary(%s=a@4+3x12" % nth]))
        if m < matches:
            parts.append(fight.replace("@game.startgame(", "@game.startgame(#%d" % (m + 1)))
    return ",".join(parts)


def online(matches=3, mode="1v1", players=2, fighters=None, rollback="off", fast_ko=600, ko_winner=None,
           bot=None, timeout=None, relay_port=27900, base_core=2, keep_going=False, extra_env=None,
           lock_timeout=None, log=None, require_hits=False, stall=300, keep_roots=False, quick=None):
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
                         "rollback": rollback, "fast_ko": fast_ko, "bot": bot}}
    shots, problems = [], []
    stop_reason = None
    logs = {}
    try:
        s.relay(relay_port, rollback=(rollback == "on"))
        for i in range(players):
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
                                                                online_tail(matches, per, i % 2 == 1)),
            })
            if fast_ko:
                env["RECOMP_TEST_KO_STEP"] = "%d,%s" % (int(fast_ko), winner)
            env.update(_bot_env(bot))
            env.update(extra_env or {})
            s.launch(i, env=env, core=base_core + 2 * i, watch=WATCH)
            time.sleep(3)           # the first copy opens the room and is player 1
        deadline = time.time() + timeout
        shot_due = None
        while time.time() < deadline:
            s.poll()
            cs = list(s.copies.values())
            done = all(c.watch.count("[FUNCCALL] Game.UpdateMatchRewardsPoints()") >= matches for c in cs)
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
            if not keep_going and any(c.watch.count("[NETPLAY] desync in") or c.watch.count("[NETPLAY] gave up") for c in cs):
                time.sleep(5)
                stop_reason = "desync or gave up"
                break
            if shot_due is None and cs[0].watch.count(" starts in "):
                shot_due = time.time() + 8
            if shot_due and time.time() >= shot_due and not shots:
                _png(s, 0, "fight-m1-copy0", shots)
            if stall and time.time() - max(c.watch.last_change for c in cs) > stall:
                stop_reason = "stalled: no front-end, netplay or telemetry line for %d s" % stall
                break
            time.sleep(1.0)
        else:
            stop_reason = "timeout after %d s" % timeout
        if stop_reason != "done":
            for c in s.copies.values():
                if c.alive():
                    st = s.control(c.index, "state")
                    problems.append("copy %d at stop: screen %s, %s" % (c.index, st.get("screen", "?"),
                                                                        (st.get("online") or {}).get("status", st.get("error", ""))[:80]))
                    _png(s, c.index, "stop-copy%d" % c.index, shots)
        s.poll()
        logs = {i: c.log for i, c in s.copies.items()}
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


def online_rows(analyses, matches, require_hits=False, problems=None):
    """The per-match rows of an online result from each copy's analyze_copy(); appends problems."""
    problems = problems if problems is not None else []
    rows = []
    for i, a in analyses.items():
        for cr in a["crashes"]:
            problems.append("copy %d: %s" % (i, cr[1][0]))
    n = matches
    for k in range(n):
        per = {i: a["started"][k] for i, a in analyses.items() if k < len(a["started"])}
        row = {"n": k + 1}
        if len(per) < len(analyses):
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
    more = max([0] + [len(a["started"]) - matches for a in analyses.values()])
    if more > 0 and rows:
        rows[-1]["then"] = "%d more match(es) started (not checked)" % more
    return rows, not problems


def format_result(r):
    return compact_json(r)


# ---- gallery --------------------------------------------------------------------------------------

def gallery(page_ms=1500, timeout=300, lock_timeout=None, log=None, keep_roots=False):
    """The online screens' sample pages (RECOMP_TEST_ONLINE_GALLERY), one PNG each."""
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
