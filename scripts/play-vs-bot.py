#!/usr/bin/env python3
"""Play an online match against a test bot on this PC, through a local relay with simulated ping.

Window 1 is yours: your controller and keyboard, a normal game window. Window 2 is the bot: a virtual
pad that presses random fight inputs and confirms the menus after a match. Both run the current
build (build/win-x64-release) from throw-away data folders (tools/defjam-mcp prepares them), never
report to the live leaderboard, and hold every arriving packet for the given ping, so the input
delay and rollback behave as over the internet.

    python scripts/play-vs-bot.py                 # 60 ms each way, 10 ms jitter
    python scripts/play-vs-bot.py --lag 90,20
    python scripts/play-vs-bot.py --no-rollback   # the same without rollback (the default of build 17)

Press START in your window; the room match starts by itself. Close your window to end; the bot and
the relay stop with it. Logs: logs/mcp/copy0.log.err (yours), copy1.log.err (the bot).
"""
import argparse
import os
import random
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "defjam-mcp"))
import server as m  # noqa: E402

MOVES = ["80", "40", "20", "10", "4", "8", "1", "2", "2000", "1000", "84", "24"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--lag", default="60,10", help="ms each way[,jitter ms]")
    ap.add_argument("--no-rollback", action="store_true")
    ap.add_argument("--fighter", type=int, default=55, help="the bot's fighter id")
    a = ap.parse_args()
    if not os.path.isfile(m.EXE):
        sys.exit("not built: " + m.EXE)
    os.makedirs(m.LOGS, exist_ok=True)
    m.close()
    m.RELAY_PROC = subprocess.Popen([m.RELAY, "-listen", "127.0.0.1:27777", "-log-interval", "30s"],
                                    stdout=open(os.path.join(m.LOGS, "relay.out"), "wb"),
                                    stderr=open(os.path.join(m.LOGS, "relay.err"), "wb"),
                                    creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(0.5)
    base = {k: v for k, v in os.environ.items() if not k.upper().startswith("RECOMP_")}
    common = {
        "RECOMP_VBLANK": "1", "RECOMP_PB_EXEC": "1", "RECOMP_PB_D3D11": "1", "RECOMP_USB": "1",
        "RECOMP_SETTINGS": "none", "RECOMP_SKIP_FILES": m.INTRO_MOVIES,
        "RECOMP_TEST_OBSERVATIONS": "1", "RECOMP_ACCOUNT_SERVER": "127.0.0.1:9",
        "RECOMP_INPUT_HOST": "1", "RECOMP_PAD_HOST": "1",
        "RECOMP_NETPLAY": "quick", "RECOMP_NETPLAY_SERVER": "127.0.0.1", "RECOMP_NETPLAY_QUICK": "0,2",
        "RECOMP_NETPLAY_LAG_MS": a.lag,
    }
    common["RECOMP_ROLLBACK"] = "off" if a.no_rollback else "on"
    for i in (0, 1):
        e = dict(base)
        e.update(common)
        e.update({"RECOMP_CONTROL_PORT": str(m.BASE_PORT + i), "DEFJAM_DATA": m.prepare_root(i)})
        if i == 0:
            e["RECOMP_NETPLAY_FIGHTER"] = "22"
        else:      # the bot: a virtual pad, no real ones, works without the focus, silent
            e.update({"RECOMP_INPUT_NO_PADS": "1", "RECOMP_INPUT_VIRTUAL_PADS": "1", "RECOMP_INPUT_IGNORE_FOCUS": "1",
                      "RECOMP_AUDIO_MUTE": "1", "RECOMP_NETPLAY_FIGHTER": str(a.fighter)})
        log = os.path.join(m.LOGS, "copy%d.log.err" % i)
        p = subprocess.Popen([m.EXE], cwd=m.REPO, env=e, stdout=open(os.path.join(m.LOGS, "copy%d.log" % i), "wb"),
                             stderr=open(log, "wb"))
        m.COPIES[i] = {"proc": p, "port": m.BASE_PORT + i, "log": log, "sock": None}
        if i == 0:
            time.sleep(3)       # the first copy opens the room
    print("Your window is the first one. Press START there; the match starts by itself.", flush=True)
    rng = random.Random()
    menu_press = 0.0
    try:
        print(m.to_menu(1), flush=True)
        while m.COPIES[0]["proc"].poll() is None:
            try:
                st = m.control(1, "state", timeout=2)
                match = st.get("match") or {}
                step = match.get("step")
                if isinstance(step, int) and step >= 0 and match.get("phase") == 1:
                    for _ in range(2):
                        m.control(1, "inject 1 %d %d %s" % (step + rng.randint(1, 25), rng.randint(1, 10),
                                                           rng.choice(MOVES)), timeout=2)
                elif isinstance(step, int) and step >= 0 and match.get("phase") in (2, 3) \
                        and time.time() - menu_press > 2.5:
                    m.control(1, "press 0 a 120", timeout=2)       # the result screens, the rematch
                    menu_press = time.time()
            except Exception:
                pass
            time.sleep(0.4)
    finally:
        m.close()
    print("Done. Logs: logs/mcp/copy0.log.err (yours), copy1.log.err (the bot).")


if __name__ == "__main__":
    main()
