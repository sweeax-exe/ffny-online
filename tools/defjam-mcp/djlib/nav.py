"""Navigation by name: the main menu's entries reached through the cursor the menu reports.

The patched main menu (scripts/apt-patch.py) reports its cursor as `Online.Focus(n)` front-end
calls, and the control channel's `state` lists the last calls and the current screen. So goto()
moves the cursor one step at a time until the menu says it is on the wanted entry, presses A and
waits for the next screen's GetScreenInfo -- no blind press timings. Works on any copy with a
virtual pad (launch's default) and the control channel.

The menu is a 3-column grid of 9 entries (docs/research/frontend-menu-integration.md); the ones
whose place is known are named below, any other is reachable as 'slot<N>'.
"""
import re
import time

ENTRIES = {"story": 0, "battle": 1, "rewards": 2, "online": 6}
COLUMNS = 3
FOCUS_RE = re.compile(r"Online\.Focus\((\d+)\)")
# after the entry: what the next screen is, and presses that take a profile prompt
NEXT = {"story": "options/userID", "battle": "battle/cmtype", "rewards": "battle/buyrewards", "online": "Online.Open"}


def focus_of(state):
    """The menu cursor from a state's last front-end calls (None when the menu has not said)."""
    for call in reversed(state.get("calls") or []):
        m = FOCUS_RE.search(call)
        if m:
            return int(m.group(1))
    return None


def path(src, dst, columns=COLUMNS):
    """Pad directions from grid entry src to dst."""
    (r0, c0), (r1, c1) = divmod(src, columns), divmod(dst, columns)
    steps = ["dpad_down" if r1 > r0 else "dpad_up"] * abs(r1 - r0)
    steps += ["dpad_right" if c1 > c0 else "dpad_left"] * abs(c1 - c0)
    return steps


def target_slot(name):
    name = name.lower().strip()
    if name in ENTRIES:
        return ENTRIES[name]
    m = re.match(r"slot(\d)$", name)
    if m:
        return int(m.group(1))
    raise ValueError("goto: menu, %s or slot0..slot8" % ", ".join(sorted(ENTRIES)))


def to_menu(control, copy=0, timeout=180):
    deadline = time.time() + timeout
    k = 0
    while time.time() < deadline:
        s = control(copy, "state")
        if "error" not in s:
            if "mainmenu" in s.get("screen", "").lower():
                return {"ok": True, "screen": s["screen"]}
            control(copy, "press 0 %s 200" % ("start" if k % 3 == 0 else "a"))
            k += 1
        time.sleep(1.2)
    return {"ok": False, "error": "not at the main menu after %ss" % timeout}


def goto(control, target, copy=0, timeout=120):
    """Main menu -> the named entry. Returns {'ok', 'screen', 'presses'}."""
    t = target.lower().strip()
    r = to_menu(control, copy, timeout=min(timeout, 180))
    if not r.get("ok") or t == "menu":
        return r
    want = target_slot(t)
    deadline = time.time() + timeout
    presses = []
    time.sleep(2.0)             # the menu takes presses once its music runs
    while time.time() < deadline:
        s = control(copy, "state")
        cur = focus_of(s)
        if cur is None:
            cur = 0             # the menu opens on its first entry and says nothing until it moves
        if cur == want:
            break
        step = path(cur, want)[0]
        control(copy, "press 0 %s 150" % step)
        presses.append(step)
        time.sleep(0.6)
        if len(presses) > 12:
            return {"ok": False, "error": "the cursor did not reach entry %d (at %s)" % (want, cur), "presses": presses}
    before = control(copy, "state").get("calls_total", 0)
    control(copy, "press 0 a 150")
    presses.append("a")
    expect = NEXT.get(t, "")
    while time.time() < deadline:
        s = control(copy, "state")
        calls = s.get("calls") or []
        screen = s.get("screen", "")
        if expect and (expect.lower() in screen.lower() or any(expect.lower() in c.lower() for c in calls)):
            return {"ok": True, "screen": screen, "presses": presses}
        if not expect and s.get("calls_total", 0) > before and "mainmenu" not in screen.lower():
            return {"ok": True, "screen": screen, "presses": presses}
        time.sleep(0.5)
    return {"ok": False, "error": "no %s after the press" % (expect or "new screen"), "presses": presses}
