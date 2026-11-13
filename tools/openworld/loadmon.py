# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Machine CPU load sampler (GetSystemTimes) every 5 s into OUT until STOPFILE exists; prints nothing.
usage: python loadmon.py OUT STOPFILE"""
import ctypes, os, sys, time
from ctypes import wintypes

k = ctypes.windll.kernel32


def times():
    i, kk, u = wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME()
    k.GetSystemTimes(ctypes.byref(i), ctypes.byref(kk), ctypes.byref(u))
    f = lambda t: (t.dwHighDateTime << 32) | t.dwLowDateTime
    return f(i), f(kk), f(u)


out, stop = sys.argv[1], sys.argv[2]
prev = times()
with open(out, "w") as fh:
    while not os.path.exists(stop):
        time.sleep(5)
        cur = times()
        idle, kern, user = (c - p for c, p in zip(cur, prev))
        total = kern + user
        fh.write("%.0f %.1f\n" % (time.time(), 100.0 * (total - idle) / total if total else 0))
        fh.flush()
        prev = cur
