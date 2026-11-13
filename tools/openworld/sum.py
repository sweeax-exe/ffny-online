# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""One-line summary of an open-world phase-0 run log (fight window: match_start .. result).
usage: python sum.py LOG [LOG...]"""
import re, statistics as st, sys

PROF = re.compile(r"\[OWPROF\] planes (\d+) \| (\d+) steps: step ([\d.]+) us avg, ([\d.]+) max \| sweeps ([\d.]+)/step, "
                  r"sweep ([\d.]+) us/step \(([\d.]+) us each\) \| plane tests (\d+)/step \| re-run (\d+) steps: step "
                  r"([\d.]+) us avg, ([\d.]+) max, sweep ([\d.]+) us/step, tests (\d+)/step(?: \| contacts (\d+), "
                  r"index>=256 (\d+), highest (\d+))?")
EXEC = re.compile(r"\[EXECTIME\] (\d+) frames, (\d+) with no sleep left: interval ([\d.]+)/([\d.]+)/([\d.]+), busy "
                  r"([\d.]+)/([\d.]+)/([\d.]+), batch ([\d.]+)/([\d.]+)/([\d.]+).*?sleep median ([\d.]+) min ([\d.]+) ms; "
                  r"(\d+) batches")
WAIT = re.compile(r"\[EXECTIME\] waiting for the title ([\d.]+)/([\d.]+)/([\d.]+) ms")
D3D = re.compile(r"\[D3D\] ([\d.]+)s: (\d+) present \(([\d.]+) fps\), \d+ begin, \d+ end, \d+ clear, (\d+) draw")
RB = re.compile(r"\[ROLLBACK\] match \d+ \w+: (\d+) rollbacks, (\d+) steps run again, deepest (\d+), ([\d.]+) ms average, "
                r"([\d.]+) ms longest, (\d+) slow")

for path in sys.argv[1:]:
    fight = False
    prof, ex, wt, d3, rb = [], [], [], [], []
    crash = []
    for line in open(path, encoding="utf-8", errors="replace"):
        if '"event":"match_start"' in line:
            fight = True
        elif '"event":"result"' in line:
            fight = False
        if "[CRASH]" in line or "[WATCHDOG]" in line:
            crash.append(line.strip()[:160])
        m = RB.search(line)
        if m:
            rb.append([float(x) for x in m.groups()])
        if not fight:
            continue
        m = PROF.search(line)
        if m:
            prof.append([float(x) if x is not None else 0.0 for x in m.groups()])
        m = EXEC.search(line)
        if m:
            ex.append([float(x) for x in m.groups()])
        m = WAIT.search(line)
        if m:
            wt.append([float(x) for x in m.groups()])
        m = D3D.search(line)
        if m:
            d3.append([float(x) for x in m.groups()])
    out = [path.replace("\\", "/").split("/")[-2]]
    if prof:
        n = sum(p[1] for p in prof)
        w = lambda i: sum(p[i] * p[1] for p in prof) / n
        rn = sum(p[8] for p in prof)
        rw = lambda i: (sum(p[i] * p[8] for p in prof) / rn) if rn else 0.0
        out.append("planes %d: step %.0f us (max %.0f), sweeps %.2f/step %.0f us/step (%.2f us each), tests %.0f/step"
                   % (prof[0][0], w(2), max(p[3] for p in prof), w(4), w(5), w(6), w(7)))
        if rn:
            out.append("re-run steps %d: step %.0f us (max %.0f), sweep %.0f us/step" % (rn, rw(9), max(p[10] for p in prof), rw(11)))
        out.append("contacts %d, idx>=256 %d, highest %d" % (sum(p[13] for p in prof), sum(p[14] for p in prof),
                                                             max(p[15] for p in prof)))
    if ex:
        out.append("GPU thread busy %.2f ms med (p95 %.2f), batch %.2f ms, sleep med %.2f (min %.2f), no-sleep frames %d/%d, "
                   "%.0f batches/frame" % (st.median(e[5] for e in ex), st.median(e[6] for e in ex), st.median(e[8] for e in ex),
                                           st.median(e[11] for e in ex), min(e[12] for e in ex), sum(e[1] for e in ex),
                                           sum(e[0] for e in ex), st.median(e[13] for e in ex)))
    if wt:
        out.append("GPU waits for title %.2f ms med" % st.median(w[0] for w in wt))
    if d3:
        out.append("fps %.1f (min %.1f), draws/frame %.0f" % (st.mean(d[2] for d in d3), min(d[2] for d in d3),
                                                             st.mean(d[3] / max(1, d[1]) for d in d3)))
    if rb:
        r = rb[-1]
        out.append("rollback: %d rollbacks, %d steps again, deepest %d, %.2f ms avg, %.2f ms max, %d slow" % tuple(r))
    if crash:
        out.append("CRASH/WATCHDOG: " + crash[0])
    print(" | ".join(out))
