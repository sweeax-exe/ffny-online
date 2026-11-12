#!/usr/bin/env python3
"""Floating-point inventory of the lifted code (src/recomp/gen), for the deterministic math layer.

Counts every call the lifted chunks make into floating-point helpers and the C library, sorts them
into the three classes of docs/research/deterministic-math.md and exits 1 when a call of the third
class (platform-dependent and not routed through src/detmath) appears, so a re-lift that starts
emitting, say, rsqrtss or exp2 is caught before it reaches an online build.

    python tests/detmath/inventory.py [--gen src/recomp/gen]
"""
import argparse
import re
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# exact everywhere: IEEE-754 / C define the result to the bit (with the guest rounding modes the
# toolkit applies itself), or integer-only helpers
EXACT = {"sqrt", "sqrtf", "fabs", "fabsf", "floor", "ceil", "trunc", "fmod", "remquo", "frexp", "ldexp", "scalbn",
         "copysign", "copysignf", "isfinite", "isnan", "isinf", "signbit", "nearbyint",
         "recomp_fp_round24", "recomp_frndint", "recomp_fist", "recomp_cvt_si32", "recomp_fscale", "recomp_fprem",
         "recomp_f80_load", "recomp_f80_store", "recomp_fxam", "recomp_fp_trig_in_range"}
# platform libm, but redirected to src/detmath by detmath_redirect.h (directly or inside recomp_x87_*)
ROUTED = {"sin", "cos", "tan", "atan2", "log2", "log1p", "expm1",
          "recomp_x87_sin", "recomp_x87_cos", "recomp_x87_sincos", "recomp_x87_tan"}
# platform-dependent and NOT routed: must stay at zero
UNSAFE = {"exp2", "exp", "log", "log10", "pow", "atan", "asin", "acos", "sinh", "cosh", "tanh", "hypot", "cbrt",
          "sinf", "cosf", "tanf", "atan2f", "expf", "logf", "log2f", "powf", "fma", "fmaf",
          "XMM_RSQRT", "XMM_RCP", "recomp_rsqrtss", "recomp_rcpss"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", default=str(REPO / "src/recomp/gen"))
    a = ap.parse_args()
    files = sorted(Path(a.gen).glob("recomp_[0-9]*.c"))
    if not files:
        print("no lifted chunks in", a.gen)
        return 2
    names = EXACT | ROUTED | UNSAFE
    pat = re.compile(r"\b(" + "|".join(sorted(names, key=len, reverse=True)) + r")\s*\(")
    seen = Counter()
    for f in files:
        seen.update(pat.findall(f.read_text(errors="replace")))
    for title, group in (("exact (IEEE / C define the bits)", EXACT), ("routed to src/detmath", ROUTED),
                         ("platform-dependent, not routed", UNSAFE)):
        rows = sorted(((n, seen[n]) for n in group if seen[n]), key=lambda r: -r[1])
        print("%s: %d calls" % (title, sum(c for _, c in rows)))
        for n, c in rows:
            print("    %-24s %6d" % (n, c))
    bad = sum(seen[n] for n in UNSAFE)
    if bad:
        print("FAIL: %d platform-dependent calls are not routed through src/detmath" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
