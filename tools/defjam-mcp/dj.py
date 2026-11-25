#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""dj: the game-test CLI (defjam-mcp v2 from Bash; the MCP server offers the same).

Every game launch waits for the machine lock (D:\\DefJam\\test-lock.txt and test-priority.txt, and no
other defjam_recomp.exe running), runs on disposable data roots and prints one compact JSON result;
the exit code is 1 on any failure.

    python tools/defjam-mcp/dj.py scenario online --matches 3 [--mode 1v1|ffa|team|koth|ffarot] [--players 2]
           (ffarot: an FFA rotation room, 3 or 4 copies; checks the relay's calls, fights and line rotation)
           [--fighters 55,22] [--rollback on|off] [--fast-ko 600|0] [--bot [60|p1=aggressive,p2=counter]]
           [--spectators 1] [--room-venues 4,14,21]
    python tools/defjam-mcp/dj.py scenario offline [--type 0] [--venue 1] [--f0 55] [--f1 22] [--human]
           [--bot] [--fast-ko 600] [--turbo] [--fixture <save folder>]
    python tools/defjam-mcp/dj.py scenario boot [--goto battle|story|rewards|online|slotN|exit]
    python tools/defjam-mcp/dj.py scenario gallery [--page-ms 1500]
    python tools/defjam-mcp/dj.py scenario towers [--content-root DIR]   (Towers T0: climb, save and quit, resume, loss)
    python tools/defjam-mcp/dj.py scenario social [--off] [--out logs/social-2a]   (the chat window: two copies,
           a local account service with -social; --off: its switch off, one copy, nothing new may show;
           the quick-chat wheel in a room and on GLOBAL, a room report with the room's code)
    python tools/defjam-mcp/dj.py scenario socialonline [--out DIR]   (build 76: the chat window's ONLINE tab in a
           room and on the hub, the LEAVE CREW warning, the crew wait refusals with the time left; two copies)
    python tools/defjam-mcp/dj.py scenario roomchat [--out logs/social-2b]   (room chat through the relay: two
           copies and a spectator talk in the room; the spectator's line during the fight shows on the fighters;
           the spectator's quick-chat wheel in the fight reaches them, a fighter's stays shut)
    python tools/defjam-mcp/dj.py scenario mute [--via voice|chat] [--matches 2]   (build 92: two copies with accounts and the voice
           test tone; copy 0 mutes copy 1; its voice level at the speakers must be 0 in the room and in every match,
           copy 1's hearing of copy 0 unchanged; --via chat: the chat window's MUTE IN ROOM, also hides the lines)
    python tools/defjam-mcp/dj.py scenario crewbattle [--out logs/crew-battles]   (crew battles: a local account
           service and relay, four copies in two crews of two; challenge, accept, the battle, rating and ladder)
    python tools/defjam-mcp/dj.py scenario turf [--off] [--out logs/turf-e2e]   (Turf War: a local account service
           with turf on and a relay, four copies in two crews of two; the hub's TURF WAR, CLAIM on an open venue,
           the crew battle there, the new holder on the map; protection, a member's read-only page; --off: hidden)
    python tools/defjam-mcp/dj.py scenario factionwar [--size 1|2] [--matches N] [--out D:/DefJam/community/faction-war-shots]
           (build 108: Faction War on a local account service with -factions: the first-time picker with Crow closed by
           the balance rule, the FACTION WAR page's live totals, 1 ON 1 (or 2 ON 2 with four copies) and the board)
    python tools/defjam-mcp/dj.py scenario towersteam [--out D:/DefJam/community/team-tower-shots]   (build 106: the
           Team Tower, 2 vs 2 with two pads, signed in on a local account service: 3 floors won and saved, resumed and
           lost, a new climb on floor 1, the community Team Tower; build 108: player 2 signs in on the local
           service's /auth/pair page with the code off the screen, the start records them, the sign-in card's pictures;
           both boards with both names; FFNY_SERVER_DIR: a server checkout with pair.go)
    python tools/defjam-mcp/dj.py scenario towersteam-online [--variant climb|leave|desync|soak] [--rollback on]
           [--start-floor 30] [--floors N]   (build 111: towersonline with rollback on a lagged path and a high start
           floor; checksums compared on every floor on both copies, the AI ramp's numbers agree)
    python tools/defjam-mcp/dj.py scenario towersonline [--variant climb|leave|desync|soak] [--floors N]
           [--out D:/DefJam/community/team-tower-online-shots]   (Team Tower Online: two signed-in copies on a local
           account service and relay climb together; FFNY_SERVER_DIR: a server checkout with relay/tower.go)
    python tools/defjam-mcp/dj.py scenario towersweekly [--twists low_health,glass_jaw] [--out DIR]   (build 108: The
           Weekly, tower 3, on a local account service from source with its switch off, then on: no card while off;
           two floors of each twist, checked against the service's floor list and the twist's own effect; the
           page's Weekly card, rules and this week's board; pictures to D:/DefJam/community/weekly-tower-shots)
    python tools/defjam-mcp/dj.py digest <log> [<log> ...] [--max 60]     (a run log, or logs/bug-reports/*/log.txt)
    python tools/defjam-mcp/dj.py analyze <copy logs...> [--matches N]   (an online result from existing logs)
    python tools/defjam-mcp/dj.py lock status | run -- <command...>      (any other test under the lock)
    python tools/defjam-mcp/dj.py roots list | create NAME [--fixture F] | destroy PATH | clean
    python tools/defjam-mcp/dj.py cleanup                                (stray copies/relays of this checkout)
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from djlib import logparse, machine, roots, scenarios  # noqa: E402


def say(msg):
    print(msg, file=sys.stderr, flush=True)


def ints(text):
    return [int(x) for x in str(text).split(",") if x.strip()]


def copy_env(items):
    """["1:NAME=VALUE", ...] -> {1: {NAME: VALUE}}"""
    out = {}
    for e in items or []:
        idx, kv = e.split(":", 1)
        k, v = kv.split("=", 1)
        out.setdefault(int(idx), {})[k] = v
    return out


def cmd_scenario(a):
    common = {"lock_timeout": a.lock_timeout, "log": say, "keep_roots": a.keep_roots}
    if getattr(a, "override", None):    # FFNY Studio: rebuilt archives on top of <data>/override in each root
        os.environ["DJ_OVERRIDE"] = os.path.abspath(a.override)
    if getattr(a, "content_root", None):    # community packs (setup/mods.py sync) in each disposable root
        os.environ["DJ_CONTENT"] = a.content_root
    env = dict(e.split("=", 1) for e in a.env)
    if getattr(a, "pick_random", False):    # build 67: every copy picks RANDOM (the hub's RANDOM card)
        env.setdefault("RECOMP_NETPLAY_FIGHTER", "random")
    if getattr(a, "vote_random", False):    # build 67: every copy votes RANDOM where the relay offers it
        env.setdefault("RECOMP_NETPLAY_TEST_VOTE", "random")
    if a.kind == "online":
        r = scenarios.online(matches=a.matches, mode=a.mode, players=a.players,
                             fighters=ints(a.fighters) if a.fighters else None, rollback=a.rollback,
                             fast_ko=a.fast_ko, ko_winner=a.ko_winner, bot=a.bot, timeout=a.timeout,
                             relay_port=a.relay_port, keep_going=a.keep_going, extra_env=env,
                             require_hits=a.require_hits, quick=a.quick, created=a.created,
                             cp_records=a.cp_records.split(",") if a.cp_records else None,
                             cp_pick=a.cp_pick.split(",") if a.cp_pick else None,
                             cp_names=a.cp_names.split(",") if a.cp_names else None, more_shots=a.more_shots,
                             rematch=a.rematch, venues=a.venues, spectators=a.spectators, copy_env=copy_env(a.copy_env),
                             fixtures=a.fixtures.split(",") if a.fixtures else None, ranked=a.ranked,
                             rollback_modes=a.rollback_modes, rollback_max_ping=a.rollback_max_ping, leave=a.leave,
                             spectate_after=a.spectate_after, spectate_delay=a.spectate_delay,
                             maintenance=a.maintenance, cpu_level=a.cpu_level, linger=a.linger, reports=a.reports,
                             fighter_rules=a.fighter_rules, max_ping=a.max_ping, room_menu=a.room_menu,
                             results_leave=a.results_leave, hill_pick=a.hill_pick, hill_idle=a.hill_idle,
                             password=a.password, drop_downlink=a.drop_downlink, cut=a.cut, cut_expect=a.cut_expect,
                             room_venues=a.room_venues,
                             banned_tier=[int(x) for x in a.banned_tier.split(",") if x.strip()] if a.banned_tier is not None else None,
                             **common)
    elif a.kind == "offline":
        r = scenarios.offline(type=a.type, venue=a.venue, f0=a.f0, f1=a.f1, cpu=not a.human, bot=a.bot,
                              fast_ko=a.fast_ko, turbo=a.turbo, timeout=a.timeout or 420,
                              extra_env=env, fixture=a.fixture, shots_every=a.shots_every,
                              shots_from=a.shots_from, **common)
    elif a.kind == "boot":
        r = scenarios.boot(turbo=a.turbo, timeout=a.timeout or 240, extra_env=env, goto=a.goto, fixture=a.fixture,
                           **common)
    elif a.kind == "roomchat":
        r = scenarios.room_chat(timeout=a.timeout, relay_port=a.relay_port, extra_env=env, out_dir=a.out,
                                rollback=a.rollback, **common)
    elif a.kind == "mute":
        r = scenarios.mute(timeout=a.timeout, relay_port=a.relay_port, extra_env=env, via=a.via, matches=a.matches,
                           rollback=a.rollback, **common)
    elif a.kind == "crewbattle":
        r = scenarios.crew_battle(timeout=a.timeout, relay_port=a.relay_port, extra_env=env, out_dir=a.out,
                                  fast_ko=a.fast_ko, rollback=a.rollback, rollback_modes=a.rollback_modes, **common)
    elif a.kind == "turf":
        r = scenarios.turf(timeout=a.timeout, relay_port=a.relay_port, off=a.off, extra_env=env, out_dir=a.out,
                           fast_ko=a.fast_ko, rollback=a.rollback, rollback_modes=a.rollback_modes, **common)
    elif a.kind == "tset":
        r = scenarios.tset(timeout=a.timeout, relay_port=a.relay_port, extra_env=env, out_dir=a.out, fast_ko=a.fast_ko,
                           rollback=a.rollback, rollback_modes=a.rollback_modes, **common)
    elif a.kind == "lobby97":
        r = scenarios.lobby97(case=a.case, timeout=a.timeout, relay_port=a.relay_port, extra_env=env, **common)
    elif a.kind == "overlayroom":
        r = scenarios.overlay_room(timeout=a.timeout or 150, relay_port=a.relay_port, extra_env=env, **common)
    elif a.kind == "socialonline":
        r = scenarios.social_online(timeout=a.timeout or 200, relay_port=a.relay_port, extra_env=env, out_dir=a.out, **common)
    elif a.kind == "social":
        r = scenarios.social(timeout=a.timeout or 300, relay_port=a.relay_port, off=a.off, extra_env=env,
                             out_dir=a.out, stress=a.stress, **common)
    elif a.kind == "towers":
        r = scenarios.towers(timeout=a.timeout, extra_env=env, **common)
    elif a.kind == "towersteam":
        r = scenarios.towers_team(timeout=a.timeout, extra_env=env, out_dir=a.out, **common)
    elif a.kind in ("towersonline", "towersteam-online"):
        # towersteam-online (build 111): the same runs, with --rollback on (the floors roll back on a lagged path) and
        # --start-floor N (the service starts the climb on floor N: the AI ramp's writes), knockouts at step 900
        online_name = a.kind.replace("-", "_") if a.kind != "towersonline" else "towersonline"
        r = scenarios.towers_online(variant=a.variant, floors=a.floors, timeout=a.timeout, relay_port=a.relay_port,
                                    extra_env=env, out_dir=a.out,
                                    fast_ko=a.fast_ko if (a.kind == "towersonline" or a.fast_ko != 600) else 900,
                                    rollback=a.rollback == "on", start_floor=a.start_floor, lag=a.lag, name=online_name,
                                    **common)
    elif a.kind == "factionwar":
        r = scenarios.faction_war(size=a.size, matches=a.matches if a.matches != 3 else (2 if a.size == 1 else 1),
                                  timeout=a.timeout, relay_port=a.relay_port, extra_env=env,
                                  out_dir=a.out or scenarios.FACTION_SHOTS, fast_ko=a.fast_ko, **common)
    elif a.kind == "towersweekly":
        r = scenarios.towers_weekly(timeout=a.timeout, extra_env=env, out_dir=a.out,
                                    twists=[t for t in (a.twists or "").split(",") if t] or None, **common)
    elif a.kind == "towerrewards":
        r = scenarios.tower_rewards(timeout=a.timeout or 150, relay_port=a.relay_port, extra_env=env, out_dir=a.out, **common)
    else:
        r = scenarios.gallery(page_ms=a.page_ms, timeout=a.timeout or 300, extra_env=env, **common)
    print(scenarios.format_result(r))
    return 0 if r.get("ok") else 1


def cmd_digest(a):
    sites = {} if a.no_callers else logparse.load_call_sites(machine.REPO)
    for path in a.logs:
        if os.path.isdir(path):
            path = os.path.join(path, "log.txt")
        info = os.path.join(os.path.dirname(path), "info.txt")
        if os.path.isfile(info):      # a player's F9 report: who, which build, what kind
            print("report: " + "; ".join(l.strip() for l in logparse.read_text(info).splitlines() if l.strip()))
        for line in logparse.digest(logparse.read_text(path), max_lines=a.max, sites=sites,
                                    source=os.path.relpath(path) if not os.path.isabs(path) else path):
            print(line)
    return 0


def cmd_analyze(a):
    analyses = {i: logparse.analyze_copy(logparse.read_text(p)) for i, p in enumerate(a.logs)}
    n = a.matches or min(len(x["started"]) for x in analyses.values())
    problems = []
    rows, ok = scenarios.online_rows(analyses, n, a.require_hits, problems)
    print(scenarios.compact_json({"analyze": len(a.logs), "ok": ok and not problems, "matches": rows,
                                  "problems": problems}))
    return 0 if ok and not problems else 1


def cmd_lock(a):
    lock = machine.MachineLock(note=" ".join(a.command)[:60] if a.command else "dj lock", log=say)
    if a.action == "status":
        text = lock.read()
        try:
            with open(lock.priority_path, encoding="utf-8", errors="replace") as f:
                prio = f.read().strip()
        except OSError:
            prio = None
        info = machine.parse_lock(text) if text else None
        print("lock: %s" % ("free" if not text else info["who"] + (" (stale)" if machine.lock_is_stale(info) else "")))
        print("priority: %s" % (prio.splitlines()[0][:150] if prio else "none"))
        print("games: %s" % (machine.games() or "none"))
        print("this agent: %s" % machine.agent_id())
        return 0
    if not a.command:
        say("dj lock run -- <command ...>")
        return 2
    if not lock.acquire(timeout=a.lock_timeout):
        say("the machine stayed busy")
        return 3
    try:
        return subprocess.call(a.command)
    finally:
        lock.release()


def cmd_roots(a):
    if a.action == "list":
        for p, info in roots.listing():
            print(p, info.get("created", ""), "pid", info.get("pid", "?"))
    elif a.action == "create":
        print(roots.create(a.name, fixture=a.fixture, partitions=a.partitions))
    elif a.action == "destroy":
        print("removed", roots.destroy(a.name))
    elif a.action == "clean":
        for p, info in roots.listing():
            if info.get("pid") and machine.process_started(info["pid"]) is not None and info["pid"] != os.getpid():
                print("in use (pid %s): %s" % (info["pid"], p))
                continue
            print("removed", roots.destroy(p))
    return 0


def cmd_cleanup(a):
    stray = machine.stray_own_processes()
    for pid, path in stray:
        machine.kill_pid(pid)
        print("killed %d %s" % (pid, path))
    if not stray:
        print("no stray copies or relays of this checkout")
    return cmd_roots(argparse.Namespace(action="clean"))


def safe_output(streams=None):
    """Logs carry player names with emoji and other scripts; a cp1252 console (or a pipe without
    PYTHONIOENCODING) cannot encode them and print() raised UnicodeEncodeError halfway through a
    digest. Such streams escape what they cannot encode (\\U0001f525) instead; UTF-8 ones stay as
    they are."""
    for s in streams or (sys.stdout, sys.stderr):
        enc = (getattr(s, "encoding", None) or "").lower().replace("-", "").replace("_", "")
        if enc != "utf8" and hasattr(s, "reconfigure"):
            try:
                s.reconfigure(errors="backslashreplace")
            except (ValueError, OSError):
                pass


def main(argv=None):
    safe_output()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sc = sub.add_parser("scenario")
    sc.add_argument("kind", choices=["online", "offline", "boot", "gallery", "social", "roomchat",
                                              "crewbattle", "turf", "tset", "socialonline", "mute", "lobby97", "towers",
                                              "towerrewards", "towersteam", "factionwar", "towersweekly", "towersonline",
                                              "towersteam-online", "overlayroom"])
    sc.add_argument("--variant", default="climb", choices=["climb", "leave", "desync", "soak", "quit"],
                    help="towersonline: climb (public room, list join, resume, loss, new climb), leave (away and back, "
                         "then the 3-minute save), desync (a forced void), soak (10 floors, unequal loads), quit (the partner leaves "
                         "the room in floor 1's fight: the floor is lost, the climb ends)")
    sc.add_argument("--floors", type=int, default=0, help="towersonline: floors to climb (default 3; soak 10)")
    sc.add_argument("--start-floor", type=int, default=0, help="towersteam-online: the service starts each new climb on this "
                    "floor (-towers-test-team-floor; off the board): 30 runs the AI ramp, its hold cap and repeat counter")
    sc.add_argument("--lag", help="towersteam-online with --rollback on: RECOMP_NETPLAY_LAG_MS on both copies (default 40,20)")
    sc.add_argument("--size", type=int, default=1, choices=[1, 2], help="factionwar: 1 ON 1 (with the observer, the picker "
                    "and the lock) or 2 ON 2 (four fighters)")
    sc.add_argument("--twists", help="towersweekly: the twists to climb (comma list; default all six)")
    sc.add_argument("--case", default="kick", help="lobby97 (build 97): kick, quick, quickmax or afk")
    sc.add_argument("--via", default="voice", choices=["voice", "chat"], help="mute: the VOICE CHAT page's call, or the chat window's MUTE IN ROOM")
    sc.add_argument("--off", action="store_true", help="social: the service's switch off, no RECOMP_SOCIAL (nothing may show)")
    sc.add_argument("--stress", action="store_true", help="social: more accounts with emoji, Arabic, combining-mark "
                    "and very long names and lines in every tab")
    sc.add_argument("--out", help="social, roomchat, crewbattle: also write the chosen captures (PNG) to this folder")
    sc.add_argument("--matches", type=int, default=3)
    sc.add_argument("--mode", default="1v1", choices=sorted(scenarios.MODES))
    sc.add_argument("--players", type=int, default=2)
    sc.add_argument("--fighters")
    sc.add_argument("--rollback", default="off", choices=["on", "off", "mandatory"],
                    help="mandatory: the relay's rollback-everywhere switch (-rollback-modes), the copies without an opt-in")
    sc.add_argument("--rollback-modes", help="with --rollback mandatory: the relay's mode list (default all), "
                                             "e.g. '1v1 team ffa koth crew ranked'")
    sc.add_argument("--rollback-max-ping", type=int, help="online: the relay's rollback ping limit in ms (-rollback-max-ping; "
                                                          "default none, the game's own 120)")
    sc.add_argument("--drop-downlink", help="online (build 109): SLOT:STEP:DURATION, the relay cuts every datagram to "
                    "the copy in relay slot SLOT for DURATION once the fight's STEPS reach STEP (relay -test-drop-downlink, e.g. 1:900:4s); "
                    "the resend window must close the gap: no copy gives up, the stall notice shows")
    sc.add_argument("--cut", help="online (build 110, the fight resume): N@STEP:MS[:newport][:in|:out][,STEP:MS...], copy N's "
                    "connection is cut (every datagram in and out; in/out one way) from fight step STEP for MS "
                    "(RECOMP_TEST_NET_CUT; newport: a new socket after it); up to four cuts")
    sc.add_argument("--cut-expect", choices=["resume", "end", "limit", "none", "stop"], default="resume",
                    help="online, with --cut: resume (the fight goes on in sync), end (the hold runs out: today's end), "
                    "limit (the third cut is not held), none (no hold: an older copy, after the KO), stop (the other "
                    "copy stops waiting with START: RECOMP_TEST_RESUME_STOP_MS on it)")
    sc.add_argument("--fast-ko", type=int, default=600, help="knockout at this fight step (0: off)")
    sc.add_argument("--ko-winner", help="slot, alt or humans (only the human fighters; default alt; koth 0)")
    sc.add_argument("--cpu-level", choices=["easy", "normal", "hard"],
                    help="online (3-Way, FFA): the host sets its room's CPU level (RECOMP_TEST_CPU_LEVEL)")
    sc.add_argument("--fighter-rules", choices=["all", "nobanned", "nocreated", "stock", "standard", "stock+standard"],
                    help="online: the host sets its room's FIGHTER RULES (RECOMP_TEST_FIGHTER_RULES, build 68; "
                         "standard: STANDARD ONLY, community content off, build 82)")
    sc.add_argument("--max-ping", type=int, help="online: the host sets its room's MAX PING in ms (RECOMP_TEST_MAX_PING)")
    sc.add_argument("--banned-tier", help="online: fighter IDs, comma-separated, for the relay's banned-tier.txt ('' empty)")
    sc.add_argument("--bot", nargs="?", const=True, default=None,
                    help="the state-reading bot fights (aggression 0..100, or p1=..,p2=.. behaviours)")
    sc.add_argument("--timeout", type=int)
    sc.add_argument("--relay-port", type=int, default=27900)
    sc.add_argument("--keep-going", action="store_true", help="online: do not stop at the first desync")
    sc.add_argument("--require-hits", action="store_true", help="online: a match where a slot landed no hit fails")
    sc.add_argument("--quick", help="online: raw RECOMP_NETPLAY_QUICK 'mode,players'")
    sc.add_argument("--created", action="store_true", help="online: the relay plays created fighters (fighter 99)")
    sc.add_argument("--cp-records", help="online: per copy the profile dumps or user ID saves (RECOMP_CP_RECORD), comma-separated "
                                         "per copy, ';' between a copy's several created fighters (newest first), '' for none")
    sc.add_argument("--cp-pick", help="online: per copy the index of the created fighter it plays (RECOMP_CP_PICK), comma-separated")
    sc.add_argument("--cp-names", help="online: per copy the online names of its --cp-records (RECOMP_CP_NAMES), comma-separated "
                                       "per copy, ';' between a copy's records in the same order, '' the user ID")
    sc.add_argument("--more-shots", action="store_true", help="online: also each copy's versus and results screens of match 1")
    sc.add_argument("--pick-random", action="store_true", help="online: every copy picks a RANDOM fighter (RECOMP_NETPLAY_FIGHTER=random)")
    sc.add_argument("--vote-random", action="store_true", help="online: every copy votes RANDOM for the venue (RECOMP_NETPLAY_TEST_VOTE=random)")
    sc.add_argument("--venues", help="online: the venue feIDs every vote offers in turn (relay -test-venues), e.g. 26")
    sc.add_argument("--room-venues", help="online (build 110): copy 0 opens a private room on this CUSTOM venue list, e.g. 4,14,21, "
                    "the others join it by code; every vote and match must stay on the list")
    sc.add_argument("--rematch", action="store_true", help="online: matches after the first are Rematch from the results menu")
    sc.add_argument("--ranked", help="online: a ranked set (classic, open, classic,open or draft) against a local account service "
                                     "with test accounts (accounts -ranked -test-accounts)")
    sc.add_argument("--password", action="store_true", help="online: sign-in without Discord; a local account "
                    "service with -password, every copy signs in with a username and password (copy 0 makes its "
                    "account in the game) and the matches count for those accounts")
    sc.add_argument("--reports", action="store_true", help="online: a local account service (test accounts, not "
                    "ranked) keeps the copies' F9 reports in <session>/reports (RECOMP_TEST_PICTURE_REPORT_STEP, "
                    "RECOMP_TEST_REPORT_NOTE); the result lists them")
    sc.add_argument("--leave", help="online: N@STEP, copy N leaves the room at fight step STEP of the first match "
                    "(RECOMP_TEST_LEAVE_STEP) and is left out of the checks")
    sc.add_argument("--spectators", type=int, default=0, help="online: this many more copies watch the room (spectator mode)")
    sc.add_argument("--spectate-after", type=int, default=0, help="online: the spectators start once match N is over "
                    "(they come while the match runs, e.g. on its results before a --rematch), and must watch a later fight")
    sc.add_argument("--spectate-delay", type=float, default=0, help="online: with --spectate-after, that many seconds later "
                    "(e.g. into the next fight)")
    sc.add_argument("--maintenance", type=int, default=0, help="online: the relay's restart countdown (SECS) holds the "
                    "fights from the start until the file goes SECS + 5 s later; copy 0's online screens are captured")
    sc.add_argument("--room-menu", help="online, koth/ffarot: COPY@K:F, copy COPY presses START on the results of its K-th "
                    "fight (the room screen), picks fighter F there (default 7) and fights the next one with it")
    sc.add_argument("--results-leave", help="online, koth/ffarot: COPY@K, copy COPY holds START on the results of its K-th "
                    "fight and leaves the room; the others fight on")
    sc.add_argument("--hill-pick", help="online, koth/ffarot (build 76): COPY@K:F, copy COPY picks fighter F on the room "
                    "screen it reaches after its K-th fight (RECOMP_TEST_HILL_PICK, no START press) and must fight the next one with it")
    sc.add_argument("--hill-idle", help="online, koth/ffarot (build 76): COPY@K, copy COPY never readies from its K-th call on "
                    "(RECOMP_TEST_HILL_IDLE): the relay must skip it after the ready time")
    sc.add_argument("--linger", type=int, help="online, koth/ffarot: copy N presses nothing after the main menu and sits on "
                    "each fight's results (the game must take it off them; result hill_gaps)")
    sc.add_argument("--type", type=int, default=0)
    sc.add_argument("--venue", type=int, default=1)
    sc.add_argument("--f0", type=int, default=55)
    sc.add_argument("--f1", type=int, default=22)
    sc.add_argument("--human", action="store_true", help="offline: the second fighter is a human slot, not a CPU")
    sc.add_argument("--turbo", action="store_true", help="no flip pacing or vsync wait (measured: no gain, the fight runs on the guest timer)")
    sc.add_argument("--fixture", help="a folder of save files copied over the copy's saves")
    sc.add_argument("--page-ms", type=int, default=1500)
    sc.add_argument("--shots-every", type=float, default=0, help="offline: also a capture every N s through the fight")
    sc.add_argument("--shots-from", type=float, default=6, help="offline: the first fight capture N s after the "
                    "match starts (default 6; 0.5 with --shots-every 0.4 catches the start countdown)")
    sc.add_argument("--goto", help="boot: then this main-menu entry by name (story, battle, rewards, online, slotN), or exit: the EXIT prompt, NO three ways, then YES (the game must quit with code 0)")
    sc.add_argument("--env", action="append", default=[], help="extra NAME=VALUE (checked against the machine rules); online: NAME@N=VALUE for copy N only")
    sc.add_argument("--copy-env", action="append", default=[],
                    help="online: COPY:NAME=VALUE, an extra variable for one copy only (checked as --env)")
    sc.add_argument("--fixtures", help="online: per copy a save fixture folder, comma-separated ('' none)")
    sc.add_argument("--lock-timeout", type=float, help="give up waiting for the machine after this many s")
    sc.add_argument("--keep-roots", action="store_true", help="leave the data roots for a look (dj roots clean)")
    sc.add_argument("--content-root", help="a data folder filled by setup/mods.py sync (mods/, mod-manager/): its "
                    "community packs in each disposable root (DJ_CONTENT); online: one per copy, comma-separated, "
                    "'' for none (docs/content-packs.md)")
    sc.add_argument("--override", help="a folder of rebuilt game files (FFNY Studio, e.g. assets/v2ip.viv) laid over "
                    "<data>/override in each disposable root (DJ_OVERRIDE); <data> itself is not touched")
    sc.set_defaults(fn=cmd_scenario)

    dg = sub.add_parser("digest")
    dg.add_argument("logs", nargs="+")
    dg.add_argument("--max", type=int, default=60)
    dg.add_argument("--no-callers", action="store_true", help="skip the lifted-C caller filter (faster)")
    dg.set_defaults(fn=cmd_digest)

    an = sub.add_parser("analyze")
    an.add_argument("logs", nargs="+")
    an.add_argument("--matches", type=int)
    an.add_argument("--require-hits", action="store_true")
    an.set_defaults(fn=cmd_analyze)

    lk = sub.add_parser("lock")
    lk.add_argument("action", choices=["status", "run"])
    lk.add_argument("command", nargs=argparse.REMAINDER)
    lk.add_argument("--lock-timeout", type=float)
    lk.set_defaults(fn=cmd_lock)

    rt = sub.add_parser("roots")
    rt.add_argument("action", choices=["list", "create", "destroy", "clean"])
    rt.add_argument("name", nargs="?")
    rt.add_argument("--fixture")
    rt.add_argument("--partitions", action="store_true")
    rt.set_defaults(fn=cmd_roots)

    cu = sub.add_parser("cleanup")
    cu.set_defaults(fn=cmd_cleanup)

    a = ap.parse_args(argv)
    if getattr(a, "command", None) and a.command[:1] == ["--"]:
        a.command = a.command[1:]
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
