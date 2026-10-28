"""Pure log parsing: the digest of a game log and the per-match analysis of a test.

Nothing here touches the game or the disk except read_text() and load_call_sites(); everything
else takes text or lines, so tests/unit/test_defjam_mcp_v2.py runs it on synthetic logs.

digest(text)          the key events of any log (a test copy's, a player's F9 log.txt) in <= ~60
                      lines: settings / GPU / input, one line per online match, waits and gave-ups,
                      desync detail, the crash block with its sub_ and filtered callers, [MEM],
                      [INPUT] removed/arrived, [REPORT]
parse_matches(lines)  one record per online match (START .. next START), from the [NETPLAY] lines
analyze_copy(text)    parse_matches plus the front-end order, telemetry hits, results and summaries
"""
import json
import os
import re

# ---- reading -----------------------------------------------------------------------------------


def read_text(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def load_call_sites(repo):
    """{return address: (function, callee)} from the lifted C (scripts/guest-stack.py's cache), or {}."""
    import importlib.util
    script = os.path.join(repo, "scripts", "guest-stack.py")
    gens = [os.path.join(repo, "src", "recomp", "gen")]
    main = main_checkout(repo)
    if main:
        gens.append(os.path.join(main, "src", "recomp", "gen"))
    for gen in gens:
        if os.path.isdir(gen) and any(n.endswith(".c") for n in os.listdir(gen)):
            try:
                spec = importlib.util.spec_from_file_location("guest_stack_dj", script)
                mod = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mod)
                cache = os.path.join(os.path.dirname(gen), "..", "..", "build", "win-x64-release", "call-sites.json")
                return mod.build_call_sites(gen, os.path.abspath(cache))
            except Exception:   # noqa: BLE001 -- a digest without callers is still a digest
                return {}
    return {}


def main_checkout(repo):
    """The main checkout of a worktree (its .git is a file naming .git/worktrees/<name>)."""
    g = os.path.join(repo, ".git")
    if os.path.isfile(g):
        with open(g, encoding="utf-8") as f:
            m = re.match(r"gitdir:\s*(.+)", f.read().strip())
        if m:
            d = os.path.abspath(m.group(1).strip())
            parts = d.replace("\\", "/").split("/")
            if ".git" in parts:
                return "/".join(parts[:parts.index(".git")]).replace("/", os.sep)
    return None


# ---- the crash / watchdog block ----------------------------------------------------------------

SCAN_RE = re.compile(r"^\s+\[esp\+\s*(\d+)\s*\]\s+0x([0-9A-Fa-f]{8})")
NATIVE_RE = re.compile(r"^\s+\[(?:rip|\d+)\]\s+0x([0-9A-Fa-f]+)")
IN_SUB_RE = re.compile(r"^\s+in (\S+)")
REG_RE = re.compile(r"^\s+Xbox regs: (.*)")


def crash_blocks(lines):
    """[(line index, [block lines])] of every [CRASH] / [WATCHDOG] block (header + indented lines)."""
    out = []
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith("[CRASH]") or l.startswith("[WATCHDOG]"):
            j = i + 1
            while j < len(lines) and (lines[j].startswith(" ") or lines[j].startswith("\t")) and j - i < 400:
                j += 1
            out.append((i, lines[i:j]))
            i = j
        else:
            i += 1
    return out


def condense_crash(block, sites=None, callers=6):
    """A [CRASH]/[WATCHDOG] block in a few lines: what, where (sub_), registers, the real callers."""
    out = [block[0].strip()[:200]]
    where, regs, scan, native = None, [], [], 0
    for l in block[1:]:
        m = IN_SUB_RE.match(l)
        if m and where is None and not l.strip().startswith("in sub") is False:
            where = m.group(1)
        m = REG_RE.match(l)
        if m:
            regs.append(m.group(1).strip())
        m = SCAN_RE.match(l)
        if m:
            scan.append(int(m.group(2), 16))
        elif NATIVE_RE.match(l):
            native += 1
    if where or regs:
        out.append("  in %s | %s" % (where or "?", " ".join(regs)[:150]))
    if scan:
        if sites:
            real = []
            for w in scan:
                if w in sites:
                    fn, tgt = sites[w]
                    real.append("%s->%s" % (fn, tgt.replace("(indirect)", "icall")))
            out.append("  callers (filtered, innermost first): " + (" | ".join(real[:callers]) or "none of the %d words" % len(scan)))
        else:
            out.append("  stack scan (unfiltered: no lifted C here): " + " ".join("%08X" % w for w in scan[:8]))
    if native:
        out.append("  native: %d words (scripts/symbolize-host.py <log> names them for this build)" % native)
    return out


# ---- online matches ----------------------------------------------------------------------------

START_RE = re.compile(r"\[NETPLAY\] START: seed (\w+), type (\d+), rules (\d+), venue (\d+), (\d+) players, "
                      r"this PC is player (\d+)(.*)")
STARTS_RE = re.compile(r"\[NETPLAY\] match (\w+) starts in (\w+) \((\d+) players, delay (\d+)\); loaded in ([\d.]+) s")
CHECKS_RE = re.compile(r"\[NETPLAY\] in step: (\d+) checksums compared and equal \(match (\w+), step (\d+)\)")
DESYNC_RE = re.compile(r"\[NETPLAY\] desync in match (\w+) at step (\d+) \(local (\w+), remote (\w+)\)")
DETAIL_RE = re.compile(r"\[NETPLAY\] desync detail: (.*)")
FIELDS_RE = re.compile(r"\[NETPLAY\] desync fields: (.*)")      # build 35 on: the raw words that differ
GAVEUP_RE = re.compile(r"\[NETPLAY\] gave up at step (\d+)(.*)")
WAITED_RE = re.compile(r"\[NETPLAY\] waited ([\d.]+) s at the fight's start for player (\d+)")
DELAY_RE = re.compile(r"\[NETPLAY\] input delay (\d+) steps \(pings ([^)]*)\)")
RB_ON_RE = re.compile(r"\[NETPLAY\] rollback on for this match")
RB_OFF_RE = re.compile(r"\[NETPLAY\] rollback (?:off for this match|is switched off)[^:]*:?\s*(.*)")
RESULT_RE = re.compile(r"\[NETPLAY\] match result: (.*?);")
OVER_RE = re.compile(r"\[NETPLAY\] (Match over|left the match before it was decided|a player left)(.*)")
RB_SUM_RE = re.compile(r"\[ROLLBACK\] (\d+) rollbacks, (\d+) steps run again, deepest (\d+)")
UNPLUG_RE = re.compile(r"\[NETPLAY\] (the game sees ports \w+ unplugged.*|this PC's last controller disconnected.*)")
REFUSED_RE = re.compile(r"\[NETPLAY\] (refused: .*|player \d+'s game files differ.*)")


def parse_matches(lines):
    """One dict per online match (a START line, or a rematch's second 'starts in' line)."""
    matches = []
    cur = None
    last_checks = {}

    def new(i, start=None):
        m = {"line": i, "start": start, "id": None, "mode": None, "delay": None, "load_s": None,
             "input_delay": None, "pings": None, "rollback_off_why": None, "checks": 0, "desync": None,
             "desync_detail": None, "desync_fields": None, "gave_up": [], "waited": [], "result": None, "over": None,
             "rollbacks": None, "unplugged": [], "refused": [], "koth": bool(start and "King of the Hill" in start.get("note", ""))}
        matches.append(m)
        return m

    for i, l in enumerate(lines):
        if not l.startswith("[NETPLAY]") and not l.startswith("[ROLLBACK]"):
            continue
        m = START_RE.match(l)
        if m:
            cur = new(i, {"seed": m.group(1), "type": int(m.group(2)), "rules": int(m.group(3)),
                          "venue": int(m.group(4)), "players": int(m.group(5)), "me": int(m.group(6)),
                          "note": m.group(7).strip(" ()")})
            continue
        m = STARTS_RE.match(l)
        if m:
            if cur is None or cur["id"] is not None:
                cur = new(i)            # a rematch: no START of its own
            cur.update(id=m.group(1), delay=int(m.group(4)), load_s=float(m.group(5)), starts_line=i)
            if cur["mode"] is None:
                cur["mode"] = "rollback" if m.group(2) == "rollback" else "lockstep"
            continue
        if cur is None:
            continue
        m = DELAY_RE.match(l)
        if m:
            cur["input_delay"], cur["pings"] = int(m.group(1)), m.group(2)
        elif RB_ON_RE.match(l):
            cur["mode"] = "rollback"
        elif RB_OFF_RE.match(l):
            cur["mode"] = "lockstep"
            cur["rollback_off_why"] = RB_OFF_RE.match(l).group(1)[:60]
        elif CHECKS_RE.match(l):
            m = CHECKS_RE.match(l)
            last_checks[m.group(2)] = int(m.group(1))
        elif DESYNC_RE.match(l):
            m = DESYNC_RE.match(l)
            cur["desync"] = {"match": m.group(1), "step": int(m.group(2)), "local": m.group(3), "remote": m.group(4)}
        elif DETAIL_RE.match(l):
            cur["desync_detail"] = DETAIL_RE.match(l).group(1)[:200]
        elif FIELDS_RE.match(l):
            cur["desync_fields"] = FIELDS_RE.match(l).group(1)[:300]
        elif GAVEUP_RE.match(l):
            cur["gave_up"].append(l[len("[NETPLAY] "):][:160])
        elif WAITED_RE.match(l):
            m = WAITED_RE.match(l)
            cur["waited"].append((float(m.group(1)), int(m.group(2))))
        elif RESULT_RE.match(l):
            cur["result"] = RESULT_RE.match(l).group(1)
        elif OVER_RE.match(l):
            cur["over"] = OVER_RE.match(l).group(1)
        elif RB_SUM_RE.match(l):
            cur["rollbacks"] = l[len("[ROLLBACK] "):][:120]
        elif UNPLUG_RE.match(l):
            cur["unplugged"].append(UNPLUG_RE.match(l).group(1)[:120])
        elif REFUSED_RE.match(l):
            cur["refused"].append(REFUSED_RE.match(l).group(1)[:120])
    # checksums compared and equal, per match: the running count's growth over the match
    prev = 0
    for m in matches:
        if m["id"] and m["id"] in last_checks:
            m["checks"] = max(0, last_checks[m["id"]] - prev)
            prev = last_checks[m["id"]]
    return matches


def match_line(m):
    """One line for a match record."""
    s = m.get("start") or {}
    parts = []
    if s:
        parts.append("START type %d venue %d %dp me P%d%s" % (s["type"], s["venue"], s["players"], s["me"],
                                                            " KOTH" if m.get("koth") else ""))
    else:
        parts.append("rematch")
    if m["id"]:
        parts.append("%s %s d%s" % (m["id"], m["mode"] or "?", m["delay"]) + (" load %.1fs" % m["load_s"] if s else ""))
    else:
        parts.append("never started")
    if m["input_delay"] is not None:
        parts.append("pings " + m["pings"])
    if m["checks"]:
        parts.append("%d checks equal" % m["checks"])
    if m["rollbacks"]:
        parts.append(m["rollbacks"].split(",")[0])
    if m["result"]:
        parts.append(m["result"])
    if m["over"]:
        parts.append(m["over"])
    return "  match: " + ", ".join(parts)


# ---- the digest --------------------------------------------------------------------------------

# (priority, pattern): 3 always kept, 2 the run's story, 1 context (dropped first when over budget)
RULES = [
    (3, re.compile(r"^\[NETPLAY\] (desync|gave up|refused|player \d+'s game files differ|left the match|a player left|the game crashed)")),
    (3, re.compile(r"^\[ROLLBACK\] (at the crash|no save state|off for the rest|soak #)")),
    (3, re.compile(r"^\[ICALL\] Failed to resolve|^\[UNIMPL\] untranslated instruction REACHED|^\[ABI\]|^\[CONTROL\] could not")),
    (3, re.compile(r"^\[(HANG|STALL)\]")),
    (2.5, re.compile(r"^\[SETTINGS\] (display|data folder|.*value\(s\) read|defaults only|debug-env)")),
    (2.5, re.compile(r"^D3D8: graphics card")),
    (2.5, re.compile(r"^\[INPUT\] .*(removed from player| is player \d|\d+ pad\(s\) found)")),
    (2.5, re.compile(r"^\[UPDATE\]|^\[REPORT\]")),
    (2, re.compile(r"^\[NETPLAY\] (waited|the game sees ports|this PC's last controller)")),
    (2, re.compile(r"^\[MEM\] (guest address|contiguous memory at the match end)|^\[MEM\] (?!contiguous memory at the match start)")),
    (2, re.compile(r"^\[ACCOUNT\] (crash report)")),
    (2, re.compile(r"^\[ROLLBACK\] (slow:|the audio arena cannot grow|no room)")),
    (2, re.compile(r"^\[TEST-KO\] match|^\[TEST-BOT\] (summary|plan|unknown)")),
    (2, re.compile(r"^\[(UI|NETPLAY)\] .*(stuck|timed out|timeout|failed)", re.I)),
    (1, re.compile(r"^\[MEM\]")),
    (1, re.compile(r"^\[SETTINGS\]")),
    (1, re.compile(r"^D3D8: Device created")),
    (1, re.compile(r"^\[INPUT\] (\d+ pad\(s\), |keyboard and mouse)")),
    (1, re.compile(r"^\[(SERVERS\] (automatic|connecting)|ACCOUNT\] signed)")),
    (1, re.compile(r"^\[NETPLAY\] (Connecting|In the lobby|notice:|rematch:|tests:|first press from|the other player's controller is arriving)")),
    (1, re.compile(r"^\[AUTOMATCH\] (armed|sequence done)|^\[ACCOUNT\] match result")),
]
MATCH_FOLDED = re.compile(r"^\[NETPLAY\] (START:|match \w+ starts in|input delay|rollback (on|off|is switched)|in step: \d+ checksums"
                          r"|match result|Match over|sync point|Match found|crowd |hill: |King of the Hill: the fight)|^\[ROLLBACK\] \d+ rollbacks")
NUM_RE = re.compile(r"0x[0-9A-Fa-f]+|\b[0-9A-F]{8}\b|\d+(\.\d+)?")


def _key(text):
    return NUM_RE.sub("#", text)


def digest(text, max_lines=60, sites=None, source=None):
    """The key events of a game log in at most ~max_lines lines (a list of strings)."""
    lines = text.splitlines()
    entries = []        # [priority, line index, text, repeat count]
    matches = {m["line"]: m for m in parse_matches(lines)}
    blocks = dict(crash_blocks(lines))
    skip_until = -1
    tags = {}
    seen = {}
    screens = []
    for i, l in enumerate(lines):
        if l.startswith("["):
            t = l[1:l.find("]")] if "]" in l else ""
            tags[t] = tags.get(t, 0) + 1
            if t == "FUNCCALL":
                m = SCREEN_RE.match(l)
                if m:
                    name = m.group(1).replace(".swf", "")
                    if screens and screens[-1][0] == name:
                        screens[-1][1] += 1
                    else:
                        screens.append([name, 1])
        if i < skip_until:
            continue
        if i in blocks:
            for k, c in enumerate(condense_crash(blocks[i], sites)):
                entries.append([3, i + k * 1e-3, c, 1])
            skip_until = i + len(blocks[i])
            continue
        if i in matches:
            m = matches[i]
            entries.append([2.5, i, match_line(m), 1])
            if m["desync_detail"]:
                entries.append([3, i + 1e-3, "  desync detail: " + m["desync_detail"], 1])
            if m["desync_fields"]:
                entries.append([3, i + 2e-3, "  desync fields: " + m["desync_fields"], 1])
            continue
        if MATCH_FOLDED.match(l):
            continue
        for prio, rx in RULES:
            if rx.search(l):
                text_l = l.strip()[:170]
                k = _key(text_l)
                # the same line again (numbers aside), straight after: keep the latest, count it
                if entries and entries[-1][0] == prio and _key(entries[-1][2]) == k:
                    entries[-1][2], entries[-1][3] = text_l, entries[-1][3] + 1
                elif prio < 3 and k in seen and seen[k][2] == text_l:
                    seen[k][3] += 1         # the very same line again later: counted on its first
                else:
                    entries.append([prio, i, text_l, 1])
                    seen.setdefault(k, entries[-1])
                break
    head = []
    if source:
        head.append("digest of %s" % source)
    crashes, watchdogs = tags.get("CRASH", 0), tags.get("WATCHDOG", 0)
    desyncs = sum(1 for l in lines if l.startswith("[NETPLAY] desync in"))
    head.append("%d lines; %d online matches; crash %d, watchdog %d, desync %d; tags: %s" % (
        len(lines), len(matches), crashes, watchdogs, desyncs,
        " ".join("%s %d" % kv for kv in sorted(tags.items(), key=lambda kv: -kv[1])[:8])))
    if screens:
        tail = screens[-14:]
        head.append("screens%s: %s" % (" (last %d of %d)" % (len(tail), len(screens)) if len(screens) > len(tail) else "",
                                       " > ".join(n.split("/")[-1] + ("x%d" % c if c > 1 else "") for n, c in tail)))
    budget = max(10, max_lines - len(head))
    # over budget: drop priority 1, then 2, from the middle out (the start and the end tell most)
    for prio in (1, 2, 2.5):
        while len(entries) > budget:
            idx = [k for k, e in enumerate(entries) if e[0] == prio]
            if not idx:
                break
            del entries[idx[len(idx) // 2]]
    out = list(head)
    last = None
    for e in sorted(entries, key=lambda e: e[1]):
        if last is not None and e[1] - last > 1 and (out and not out[-1].startswith("  ...")):
            pass
        out.append(e[2] + ("  (x%d)" % e[3] if e[3] > 1 else ""))
        last = e[1]
    return out


# ---- per-copy analysis of an online test -------------------------------------------------------

TEST_EVENT_RE = re.compile(r"^\[TEST-EVENT\] (\{.*\})\s*$")
SUMMARY_RE = re.compile(r"^\[FE-RESULT\] Game\.GetMatchSummary\((\d+)\) = (.*)")
BOT_RE = re.compile(r"^\[TEST-BOT\] summary slot (\d+) \((\w+)\): strikes (\d+), grabs (\d+), block steps (\d+), "
                    r"counters (\d+) of (\d+) wind-ups \((\d+) clean\), held (\d+) \(mashed (\d+), broke (\d+)\)")


def bot_line(l):
    """'[TEST-BOT] summary ...' -> a short text: what the bot tried and what came of it."""
    m = BOT_RE.match(l)
    if not m:
        return None
    g = [m.group(1), m.group(2)] + [int(x) for x in m.groups()[2:]]
    out = "s%s %s: %d strikes, %d grabs, %d block steps" % tuple(g[:5])
    if g[6] or g[5]:
        out += ", counters %d/%d wind-ups, %d clean" % (g[5], g[6], g[7])
    if g[8]:
        out += ", held %d broke %d" % (g[8], g[10])
    return out


SCREEN_RE = re.compile(r"^\[FUNCCALL\] Control\.GetScreenInfo\(([^)]*)\)")


def telemetry(lines):
    """[(line index, event dict)] of the [TEST-EVENT] lines."""
    out = []
    for i, l in enumerate(lines):
        if l.startswith("[TEST-EVENT]"):
            m = TEST_EVENT_RE.match(l)
            if m:
                try:
                    out.append((i, json.loads(m.group(1))))
                except ValueError:
                    pass
    return out


def hits_by_match(events):
    """{telemetry match number: {attacker slot: hits landed}} (an attack, then damage to its target
    within 40 steps), plus results {match: winners}."""
    hits, results, pending = {}, {}, []
    for _, e in events:
        k = e.get("match")
        if e.get("event") == "attack" and e.get("target_slot") is not None:
            pending.append(e)
            hits.setdefault(k, {})
        elif e.get("event") == "damage":
            for a in reversed(pending):
                if a.get("match") == k and a.get("target_slot") == e.get("slot") and 0 <= e.get("step", 0) - a.get("step", 0) <= 40:
                    hits.setdefault(k, {})
                    hits[k][a["slot"]] = hits[k].get(a["slot"], 0) + 1
                    pending.remove(a)
                    break
        elif e.get("event") == "result":
            results[k] = e.get("winners")
        elif e.get("event") == "match_start":
            hits.setdefault(k, {})
    return hits, results


def fe_sequences(lines):
    """Every automatch sequence: {'begin', 'done', 'screen', 'order'}.

    'order': 'menu' (started from the main menu), 'ok' (on battle/cmtype, after the screen's own
    Game.ResetMatchData()), or 'BAD: ...' when the screen's resets came after the sequence began
    (the build-31 loser-stuck-on-results bug, fixed by bcdab58)."""
    seqs = []
    screen, screen_line = "", -1
    begin = None
    for i, l in enumerate(lines):
        m = SCREEN_RE.match(l)
        if m:
            screen, screen_line = m.group(1), i
        elif l.startswith("[AUTOMATCH] Game.StartBattleMode()"):
            begin = {"begin": i, "screen": screen, "screen_line": screen_line, "done": None}
            seqs.append(begin)
        elif l.startswith("[AUTOMATCH] sequence done") and begin is not None:
            begin["done"] = i
            begin = None
    for k, s in enumerate(seqs):
        if "cmtype" not in s["screen"].lower():
            s["order"] = "menu" if "mainmenu" in s["screen"].lower() else "screen " + (s["screen"] or "?")
            continue
        before = any(lines[j].startswith("[FUNCCALL] Game.ResetMatchData()") for j in range(s["screen_line"], s["begin"]))
        end = seqs[k + 1]["begin"] if k + 1 < len(seqs) else len(lines)
        after = None
        if s["done"] is not None:
            for j in range(s["done"], end):
                if lines[j].startswith("[NETPLAY] match ") and " starts in " in lines[j]:
                    break
                if lines[j].startswith("[FUNCCALL] Game.ResetMatchData()") or lines[j].startswith("[FUNCCALL] Game.ResetMatchTypeList()"):
                    after = j
                    break
        if after is not None:
            s["order"] = "BAD: the screen's reset came after StartGame"
        elif not before:
            s["order"] = "BAD: StartBattleMode before the screen's ResetMatchData"
        else:
            s["order"] = "ok"
    return seqs


def analyze_copy(text):
    """Everything an online scenario checks, from one copy's log."""
    lines = text.splitlines()
    matches = [m for m in parse_matches(lines)]
    starts = [m.get("starts_line") for m in matches if m.get("starts_line") is not None]
    started = [m for m in matches if m.get("starts_line") is not None]

    def owner(i):
        """The started match a line after its 'starts in' line belongs to (index into started)."""
        k = -1
        for n, s in enumerate(starts):
            if s <= i:
                k = n
        return k

    def next_start(i):
        for n, s in enumerate(starts):
            if s > i:
                return n
        return -1

    per = [{"summary": {}, "rewards": False, "fe": None, "hits": {}, "winners": None, "ko": False}
           for _ in started]
    for i, l in enumerate(lines):
        if l.startswith("[FE-RESULT] Game.GetMatchSummary("):
            k = owner(i)
            m = SUMMARY_RE.match(l)
            if k >= 0 and m:
                cid = re.search(r"(?:^|&)cid=(-?\d+)", m.group(2))
                per[k]["summary"][int(m.group(1))] = int(cid.group(1)) if cid else None
        elif l.startswith("[FUNCCALL] Game.UpdateMatchRewardsPoints()"):
            k = owner(i)
            if k >= 0:
                per[k]["rewards"] = True
        elif l.startswith("[TEST-KO] match"):
            k = owner(i)
            if k >= 0:
                per[k]["ko"] = True
        elif l.startswith("[TEST-BOT] summary"):
            k = owner(i)
            b = bot_line(l)
            if k >= 0 and b:
                per[k].setdefault("bot", []).append(b)
    for s in fe_sequences(lines):
        k = next_start(s["begin"])
        if k >= 0 and per[k]["fe"] is None:
            per[k]["fe"] = s["order"]
    ev = telemetry(lines)
    hits, results = hits_by_match(ev)
    # telemetry numbers its matches from 1 in fight order; map them onto the started matches
    order = []
    for i, e in ev:
        if e.get("event") == "match_start" and e.get("match") not in order:
            order.append(e.get("match"))
    for n, tm in enumerate(order[:len(per)]):
        per[n]["hits"] = hits.get(tm, {})
        per[n]["winners"] = results.get(tm)
    for m, p in zip(started, per):
        m.update(p)
    crashes = [(i, condense_crash(b)) for i, b in crash_blocks(lines)]
    for i, c in crashes:
        k = owner(i)
        if 0 <= k < len(started):
            started[k].setdefault("crash", c[0])
    return {"matches": matches, "started": started, "crashes": crashes,
            "screens": [m.group(1) for m in (SCREEN_RE.match(l) for l in lines) if m][-3:],
            "lines": len(lines)}


def offline_summary(text):
    """One offline fight: telemetry start/result, hits, the summary, faults."""
    lines = text.splitlines()
    ev = telemetry(lines)
    hits, results = hits_by_match(ev)
    first = next((e for _, e in ev if e.get("event") == "match_start"), None)
    res = next((e for _, e in ev if e.get("event") == "result"), None)
    over = next((e for _, e in ev if e.get("event") == "match_over"), None)
    k = first.get("match") if first else None
    summary = {}
    for l in lines:
        m = SUMMARY_RE.match(l)
        if m:
            cid = re.search(r"(?:^|&)cid=(-?\d+)", m.group(2))
            summary[int(m.group(1))] = int(cid.group(1)) if cid else None
    return {
        "started": bool(first),
        "fighters": [f.get("character") for f in (first or {}).get("fighters", [])],
        "match_type": (first or {}).get("match_type"),
        "hits": hits.get(k, {}),
        "result": {"winners": res.get("winners"), "code": res.get("code"), "step": res.get("step"),
                   "time_up": res.get("time_up")} if res else None,
        "fight_s": round((res["host_ms"] - first["host_ms"]) / 1000.0, 1) if res and first and "host_ms" in res else None,
        "over": bool(over),
        "summary_cids": summary,
        "rewards": any(l.startswith("[FUNCCALL] Game.UpdateMatchRewardsPoints()") for l in lines),
        "ko": any(l.startswith("[TEST-KO] match") for l in lines),
        "bot": [b for b in (bot_line(l) for l in lines if l.startswith("[TEST-BOT] summary")) if b],
        "crashes": [condense_crash(b) for _, b in crash_blocks(lines)],
    }
