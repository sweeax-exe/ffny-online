#!/usr/bin/env python3
"""Why the deterministic layer exists: the host libms disagree.

Runs the functions the lifted game calls (sin, cos, tan, atan2, log2, expm1) through this
Python's math module, which is the platform C library (MSVC UCRT on Windows, glibc on Linux,
Apple libm, bionic), over a fixed input set, and writes the result bits to a file. Run it on two
platforms and compare:

    python tests/detmath/libm_compare.py run  OUT.bin      # on each platform
    python tests/detmath/libm_compare.py diff A.bin B.bin  # how many results differ, per function
"""
import math
import struct
import sys

N = 200000
FUNCS = ("sin", "cos", "tan", "atan2", "log2", "expm1")


def inputs():
    s = 0x9E3779B97F4A7C15
    mask = (1 << 64) - 1
    for _ in range(N):
        vals = []
        for _k in range(2):
            s ^= s >> 12
            s ^= (s << 25) & mask
            s ^= s >> 27
            r = (s * 0x2545F4914F6CDD1D) & mask
            vals.append(((r >> 11) / 9007199254740992.0) * 2000.0 - 1000.0)   # |x| < 1000, game-like
        yield vals


def run(path):
    out = []
    for a, b in inputs():
        out.append((math.sin(a), math.cos(a), math.tan(a), math.atan2(a, b),
                    math.log2(abs(a)) if a else 0.0, math.expm1(a / 2.0) if abs(a) < 1400 else 0.0))
    with open(path, "wb") as f:
        for row in out:
            f.write(struct.pack("<6d", *row))
    print("wrote %d rows to %s" % (len(out), path))


def diff(pa, pb):
    a = open(pa, "rb").read()
    b = open(pb, "rb").read()
    n = min(len(a), len(b)) // 48
    for k, name in enumerate(FUNCS):
        d, worst = 0, 0
        for i in range(n):
            x = struct.unpack_from("<q", a, i * 48 + k * 8)[0]
            y = struct.unpack_from("<q", b, i * 48 + k * 8)[0]
            if x != y:
                d += 1
                worst = max(worst, abs(x - y))
        print("%-6s %d of %d results differ (max %d ulp)" % (name, d, n, worst))


if __name__ == "__main__":
    if sys.argv[1] == "run":
        run(sys.argv[2])
    else:
        diff(sys.argv[2], sys.argv[3])
