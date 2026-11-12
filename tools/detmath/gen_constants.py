#!/usr/bin/env python3
"""Check the deterministic math layer's constants and generate its 2/pi table.

src/detmath/detmath.c follows the published fdlibm algorithms (Sun Microsystems, 1993, and the
FreeBSD msun revisions). This script recomputes, with Python integers only, every constant that
is a rounded or split value of pi, ln 2 or 1/ln 2, and checks each polynomial coefficient's
decimal spelling against its bit pattern. It also writes the bits of 2/pi that the
large-argument reduction reads (DM_TWO_OVER_PI in src/detmath/detmath_tables.h).

    python tools/detmath/gen_constants.py            # check, exit 1 on any mismatch
    python tools/detmath/gen_constants.py --write    # also rewrite src/detmath/detmath_tables.h

No third-party module; nothing is downloaded.
"""
import struct
import sys
from fractions import Fraction
from pathlib import Path

PREC = 2400  # bits of fixed point


def arctan_inv(n, bits):
    """atan(1/n) * 2^bits by its series, in integers."""
    x = (1 << bits) // n
    total, k, n2, sign = x, 1, n * n, -1
    while x:
        x //= n2
        total += sign * (x // (2 * k + 1))
        sign, k = -sign, k + 1
    return total


def pi_fixed(bits):
    g = 32
    return (16 * arctan_inv(5, bits + g) - 4 * arctan_inv(239, bits + g)) >> g


def ln2_fixed(bits):
    """ln 2 = 2 atanh(1/3)."""
    g = 32
    x = (1 << (bits + g)) // 3
    total, k = x, 1
    while x:
        x //= 9
        total += x // (2 * k + 1)
        k += 1
    return (2 * total) >> g


PI = Fraction(pi_fixed(PREC), 1 << PREC)
LN2 = Fraction(ln2_fixed(PREC), 1 << PREC)


def d2bits(d):
    return struct.unpack("<Q", struct.pack("<d", d))[0]


def bits2d(b):
    return struct.unpack("<d", struct.pack("<Q", b))[0]


def hexd(hi, lo):
    return bits2d((hi << 32) | lo)


def round53(fr):
    return float(fr)  # Fraction -> float is correctly rounded


def trunc_bits(fr, nbits):
    """fr truncated to nbits significant bits."""
    s = -1 if fr < 0 else 1
    fr = abs(fr)
    e = 0
    while fr >= 2:
        fr /= 2
        e += 1
    while fr < 1:
        fr *= 2
        e -= 1
    m = int(fr * (1 << (nbits - 1)))
    return s * Fraction(m, 1 << (nbits - 1)) * Fraction(2) ** e


problems = []
checked = [0]


def check(name, value, expect):
    hi, lo = expect
    checked[0] += 1
    if d2bits(value) != ((hi << 32) | lo):
        problems.append("%s: computed %r (0x%016x), table 0x%08x%08x" % (name, value, d2bits(value), hi, lo))
    return value


def atan_fixed(fr, bits):
    """atan(fr) for |fr| <= 2/3 by its series (fr a Fraction)."""
    x = (fr.numerator << bits) // fr.denominator
    x2 = (x * x) >> bits
    term, total, k, sign = x, x, 1, -1
    while term:
        term = (term * x2) >> bits
        total += sign * (term // (2 * k + 1))
        sign, k = -sign, k + 1
    return Fraction(total, 1 << bits)


def check_all():
    # splits of pi/2 (rem_pio2), pi/4 (tan), pi (atan2)
    pio2 = PI / 2
    p1 = trunc_bits(pio2, 33)
    check("pio2_1", float(p1), (0x3FF921FB, 0x54400000))
    check("pio2_1t", round53(pio2 - p1), (0x3DD0B461, 0x1A626331))
    p2 = trunc_bits(pio2 - p1, 33)
    check("pio2_2", float(p2), (0x3DD0B461, 0x1A600000))
    check("pio2_2t", round53(pio2 - p1 - p2), (0x3BA3198A, 0x2E037073))
    p3 = trunc_bits(pio2 - p1 - p2, 33)
    check("pio2_3", float(p3), (0x3BA3198A, 0x2E000000))
    check("pio2_3t", round53(pio2 - p1 - p2 - p3), (0x397B839A, 0x252049C1))
    check("invpio2", round53(2 / PI), (0x3FE45F30, 0x6DC9C883))
    check("pio4", round53(PI / 4), (0x3FE921FB, 0x54442D18))
    check("pio4lo", round53(PI / 4 - Fraction(round53(PI / 4))), (0x3C81A626, 0x33145C07))
    check("pi_o_2", round53(PI / 2), (0x3FF921FB, 0x54442D18))
    check("pi", round53(PI), (0x400921FB, 0x54442D18))
    check("pi_lo", round53(PI - Fraction(round53(PI))), (0x3CA1A626, 0x33145C07))
    # atan's table: atan(0.5), atan(1), atan(1.5), atan(inf), each with its tail
    at05 = atan_fixed(Fraction(1, 2), PREC)
    at15 = PI / 2 - atan_fixed(Fraction(2, 3), PREC)
    for name, val, hx, lx in (("atanhi[0]", at05, (0x3FDDAC67, 0x0561BB4F), (0x3C7A2B7F, 0x222F65E2)),
                              ("atanhi[1]", PI / 4, (0x3FE921FB, 0x54442D18), (0x3C81A626, 0x33145C07)),
                              ("atanhi[2]", at15, (0x3FEF730B, 0xD281F69B), (0x3C700788, 0x7AF0CBBD)),
                              ("atanhi[3]", PI / 2, (0x3FF921FB, 0x54442D18), (0x3C91A626, 0x33145C07))):
        h = check(name, round53(val), hx)
        check(name.replace("hi", "lo"), round53(val - Fraction(h)), lx)
    # ln 2 splits (expm1) and 1/ln 2 splits (log2)
    ln2_hi = Fraction(hexd(0x3FE62E42, 0xFEE00000))
    if not (0 < LN2 - ln2_hi < Fraction(1, 1 << 30)) or (d2bits(float(ln2_hi)) & 0x1FFFFF):
        problems.append("ln2_hi is not a 32-bit leading split of ln 2")
    check("ln2_lo", round53(LN2 - ln2_hi), (0x3DEA39EF, 0x35793C76))
    check("invln2", round53(1 / LN2), (0x3FF71547, 0x652B82FE))
    ivln2hi = Fraction(hexd(0x3FF71547, 0x65200000))
    if not (0 < 1 / LN2 - ivln2hi < Fraction(1, 1 << 30)):
        problems.append("ivln2hi is not a leading split of 1/ln 2")
    check("ivln2lo", round53(1 / LN2 - ivln2hi), (0x3DE705FC, 0x2EEFA200))
    check("ln2", round53(LN2), (0x3FE62E42, 0xFEFA39EF))
    for name, (dec, hi, lo) in COEFFS.items():
        check(name, float(dec), (hi, lo))


# polynomial coefficients (fdlibm k_sin, k_cos, k_tan, s_atan, k_log, s_expm1)
COEFFS = {
    "S1": ("-1.66666666666666324348e-01", 0xBFC55555, 0x55555549),
    "S2": ("8.33333333332248946124e-03", 0x3F811111, 0x1110F8A6),
    "S3": ("-1.98412698298579493134e-04", 0xBF2A01A0, 0x19C161D5),
    "S4": ("2.75573137070700676789e-06", 0x3EC71DE3, 0x57B1FE7D),
    "S5": ("-2.50507602534068634195e-08", 0xBE5AE5E6, 0x8A2B9CEB),
    "S6": ("1.58969099521155010221e-10", 0x3DE5D93A, 0x5ACFD57C),
    "C1": ("4.16666666666666019037e-02", 0x3FA55555, 0x5555554C),
    "C2": ("-1.38888888888741095749e-03", 0xBF56C16C, 0x16C15177),
    "C3": ("2.48015872894767294178e-05", 0x3EFA01A0, 0x19CB1590),
    "C4": ("-2.75573143513906633035e-07", 0xBE927E4F, 0x809C52AD),
    "C5": ("2.08757232129817482790e-09", 0x3E21EE9E, 0xBDB4B1C4),
    "C6": ("-1.13596475577881948265e-11", 0xBDA8FAE9, 0xBE8838D4),
    "T0": ("3.33333333333334091986e-01", 0x3FD55555, 0x55555563),
    "T1": ("1.33333333333201242699e-01", 0x3FC11111, 0x1110FE7A),
    "T2": ("5.39682539762260521377e-02", 0x3FABA1BA, 0x1BB341FE),
    "T3": ("2.18694882948595424599e-02", 0x3F9664F4, 0x8406D637),
    "T4": ("8.86323982359930005737e-03", 0x3F8226E3, 0xE96E8493),
    "T5": ("3.59207910759131235356e-03", 0x3F6D6D22, 0xC9560328),
    "T6": ("1.45620945432529025516e-03", 0x3F57DBC8, 0xFEE08315),
    "T7": ("5.88041240820264096874e-04", 0x3F4344D8, 0xF2F26501),
    "T8": ("2.46463134818469906812e-04", 0x3F3026F7, 0x1A8D1068),
    "T9": ("7.81794442939557092300e-05", 0x3F147E88, 0xA03792A6),
    "T10": ("7.14072491382608190305e-05", 0x3F12B80F, 0x32F0A7E9),
    "T11": ("-1.85586374855275456654e-05", 0xBEF375CB, 0xDB605373),
    "T12": ("2.59073051863633712884e-05", 0x3EFB2A70, 0x74BF7AD4),
    "aT0": ("3.33333333333329318027e-01", 0x3FD55555, 0x5555550D),
    "aT1": ("-1.99999999998764832476e-01", 0xBFC99999, 0x9998EBC4),
    "aT2": ("1.42857142725034663711e-01", 0x3FC24924, 0x920083FF),
    "aT3": ("-1.11111104054623557880e-01", 0xBFBC71C6, 0xFE231671),
    "aT4": ("9.09088713343650656196e-02", 0x3FB745CD, 0xC54C206E),
    "aT5": ("-7.69187620504482999495e-02", 0xBFB3B0F2, 0xAF749A6D),
    "aT6": ("6.66107313738753120669e-02", 0x3FB10D66, 0xA0D03D51),
    "aT7": ("-5.83357013379057348645e-02", 0xBFADDE2D, 0x52DEFD9A),
    "aT8": ("4.97687799461593236017e-02", 0x3FA97B4B, 0x24760DEB),
    "aT9": ("-3.65315727442169155270e-02", 0xBFA2B444, 0x2C6A6C2F),
    "aT10": ("1.62858201153657823623e-02", 0x3F90AD3A, 0xE322DA11),
    "Lg1": ("6.666666666666735130e-01", 0x3FE55555, 0x55555593),
    "Lg2": ("3.999999999940941908e-01", 0x3FD99999, 0x9997FA04),
    "Lg3": ("2.857142874366239149e-01", 0x3FD24924, 0x94229359),
    "Lg4": ("2.222219843214978396e-01", 0x3FCC71C5, 0x1D8E78AF),
    "Lg5": ("1.818357216161805012e-01", 0x3FC74664, 0x96CB03DE),
    "Lg6": ("1.531383769920937332e-01", 0x3FC39A09, 0xD078C69F),
    "Lg7": ("1.479819860511658591e-01", 0x3FC2F112, 0xDF3E5244),
    "Q1": ("-3.33333333333331316428e-02", 0xBFA11111, 0x111110F4),
    "Q2": ("1.58730158725481460165e-03", 0x3F5A01A0, 0x19FE5585),
    "Q3": ("-7.93650757867487942473e-05", 0xBF14CE19, 0x9EAADBB7),
    "Q4": ("4.00821782732936239552e-06", 0x3ED0CFCA, 0x86E65239),
    "Q5": ("-2.01099218183624371326e-07", 0xBE8AFDB7, 0x6E09C32D),
    "o_threshold": ("7.09782712893383973096e+02", 0x40862E42, 0xFEFA39EF),
}


def two_over_pi_words(nwords):
    """The bits of 2/pi after the binary point, 24 per entry (fdlibm's ipio2 layout)."""
    t = 2 / PI
    words = []
    for _ in range(nwords):
        t *= 1 << 24
        w = int(t)
        words.append(w)
        t -= w
    return words


def main():
    check_all()
    words = two_over_pi_words(66)
    print("2/pi words[0..5] = " + ", ".join("0x%06X" % w for w in words[:6]))
    if problems:
        print("CONSTANT PROBLEMS:")
        for p in problems:
            print("  " + p)
    else:
        print("all %d constants agree with pi and ln 2 computed here and with their decimal spellings" % checked[0])
    if "--write" in sys.argv:
        out = Path(__file__).resolve().parents[2] / "src" / "detmath" / "detmath_tables.h"
        lines = ["/* Generated by tools/detmath/gen_constants.py: do not edit.",
                 " * SPDX-License-Identifier: MIT",
                 " * The first 66 x 24 bits of 2/pi after the binary point, computed with Python integers",
                 " * (Machin's formula). Read by the large-argument reduction in detmath.c. */",
                 "#ifndef DETMATH_TABLES_H", "#define DETMATH_TABLES_H", "#define DM_TWO_OVER_PI_WORDS 66",
                 "static const unsigned int DM_TWO_OVER_PI[DM_TWO_OVER_PI_WORDS] = {"]
        for i in range(0, 66, 6):
            lines.append("    " + ", ".join("0x%06X" % w for w in words[i:i + 6]) + ",")
        lines += ["};", "#endif", ""]
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("\n".join(lines), encoding="ascii", newline="\n")
        print("wrote", out)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
