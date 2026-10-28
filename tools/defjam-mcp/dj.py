#!/usr/bin/env python3
"""dj: the game-test CLI (defjam-mcp v2 from Bash; the MCP server offers the same).

Every game launch waits for the machine lock (D:\\DefJam\\test-lock.txt and test-priority.txt, and no
other defjam_recomp.exe running), runs on disposable data roots and prints one compact JSON result;
the exit code is 1 on any failure.

    python tools/defjam-mcp/dj.py scenario online --matches 3 [--mode 1v1|ffa|team|koth] [--players 2]
           [--fighters 55,22] [--rollback on|off] [--fast-ko 600|0] [--bot [60|p1=aggressive,p2=counter]]
    python tools/defjam-mcp/dj.py scenario offline [--type 0] [--venue 1] [--f0 55] [--f1 22] [--human]
           [--bot] [--fast-ko 600] [--turbo] [--fixture <save folder>]
    python tools/defjam-mcp/dj.py scenario boot [--goto battle|story|rewards|online|slotN]
    python tools/defjam-mcp/dj.py scenario gallery [--page-ms 1500]
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


def cmd_scenario(a):
    common = {"lock_timeout": a.lock_timeout, "log": say, "keep_roots": a.keep_roots}
    env = dict(e.split("=", 1) for e in a.env)
    if a.kind == "online":
        r = scenarios.online(matches=a.matches, mode=a.mode, players=a.players,
                             fighters=ints(a.fighters) if a.fighters else None, rollback=a.rollback,
                             fast_ko=a.fast_ko, ko_winner=a.ko_winner, bot=a.bot, timeout=a.timeout,
                             relay_port=a.relay_port, keep_going=a.keep_going, extra_env=env,
                             require_hits=a.require_hits, quick=a.quick, **common)
    elif a.kind == "offline":
        r = scenarios.offline(type=a.type, venue=a.venue, f0=a.f0, f1=a.f1, cpu=not a.human, bot=a.bot,
                              fast_ko=a.fast_ko, turbo=a.turbo, timeout=a.timeout or 420,
                              extra_env=env, fixture=a.fixture, **common)
    elif a.kind == "boot":
        r = scenarios.boot(turbo=a.turbo, timeout=a.timeout or 240, extra_env=env, goto=a.goto, **common)
    else:
        r = scenarios.gallery(page_ms=a.page_ms, timeout=a.timeout or 300, **common)
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sc = sub.add_parser("scenario")
    sc.add_argument("kind", choices=["online", "offline", "boot", "gallery"])
    sc.add_argument("--matches", type=int, default=3)
    sc.add_argument("--mode", default="1v1", choices=sorted(scenarios.MODES))
    sc.add_argument("--players", type=int, default=2)
    sc.add_argument("--fighters")
    sc.add_argument("--rollback", default="off", choices=["on", "off"])
    sc.add_argument("--fast-ko", type=int, default=600, help="knockout at this fight step (0: off)")
    sc.add_argument("--ko-winner", help="slot or alt (default alt; koth 0)")
    sc.add_argument("--bot", nargs="?", const=True, default=None,
                    help="the state-reading bot fights (aggression 0..100, or p1=..,p2=.. behaviours)")
    sc.add_argument("--timeout", type=int)
    sc.add_argument("--relay-port", type=int, default=27900)
    sc.add_argument("--keep-going", action="store_true", help="online: do not stop at the first desync")
    sc.add_argument("--require-hits", action="store_true", help="online: a match where a slot landed no hit fails")
    sc.add_argument("--quick", help="online: raw RECOMP_NETPLAY_QUICK 'mode,players'")
    sc.add_argument("--type", type=int, default=0)
    sc.add_argument("--venue", type=int, default=1)
    sc.add_argument("--f0", type=int, default=55)
    sc.add_argument("--f1", type=int, default=22)
    sc.add_argument("--human", action="store_true", help="offline: the second fighter is a human slot, not a CPU")
    sc.add_argument("--turbo", action="store_true", help="no flip pacing or vsync wait (measured: no gain, the fight runs on the guest timer)")
    sc.add_argument("--fixture", help="a folder of save files copied over the copy's saves")
    sc.add_argument("--page-ms", type=int, default=1500)
    sc.add_argument("--goto", help="boot: then this main-menu entry by name (story, battle, rewards, online, slotN)")
    sc.add_argument("--env", action="append", default=[], help="extra NAME=VALUE (checked against the machine rules)")
    sc.add_argument("--lock-timeout", type=float, help="give up waiting for the machine after this many s")
    sc.add_argument("--keep-roots", action="store_true", help="leave the data roots for a look (dj roots clean)")
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
