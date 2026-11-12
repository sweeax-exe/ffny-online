"""The deterministic math layer's constants and 2/pi table (src/detmath, tools/detmath/gen_constants.py)."""
import importlib.util
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def load_gen():
    spec = importlib.util.spec_from_file_location("dm_gen", REPO / "tools/detmath/gen_constants.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_constants_match_pi_and_ln2():
    gen = load_gen()
    gen.check_all()
    assert gen.problems == []


def test_coefficients_in_c_match_the_checked_spellings():
    gen = load_gen()
    src = (REPO / "src/detmath/detmath.c").read_text()
    for name, (dec, _hi, _lo) in gen.COEFFS.items():
        assert dec.lstrip("-") in src, name


def test_two_over_pi_table_is_generated():
    gen = load_gen()
    words = gen.two_over_pi_words(66)
    table = (REPO / "src/detmath/detmath_tables.h").read_text()
    found = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{6})", table)]
    assert found == words


# The bit patterns gen_constants.py derives from pi and ln 2, as detmath.c spells them in decimal.
DERIVED = {
    "pio4": 0x3FE921FB54442D18, "pio4lo": 0x3C81A62633145C07, "invpio2": 0x3FE45F306DC9C883,
    "pio2_1": 0x3FF921FB54400000, "pio2_1t": 0x3DD0B4611A626331, "pio2_2": 0x3DD0B4611A600000,
    "pio2_2t": 0x3BA3198A2E037073, "pio2_3": 0x3BA3198A2E000000, "pio2_3t": 0x397B839A252049C1,
    "pio2_hi": 0x3FF921FB54442D18, "pio2_lo": 0x3C91A62633145C07, "pi": 0x400921FB54442D18,
    "pi_lo": 0x3CA1A62633145C07, "pi_o_2": 0x3FF921FB54442D18, "pi_o_4": 0x3FE921FB54442D18,
    "ln2_hi": 0x3FE62E42FEE00000, "ln2_lo": 0x3DEA39EF35793C76, "ivln2hi": 0x3FF7154765200000,
    "ivln2lo": 0x3DE705FC2EEFA200, "invln2": 0x3FF71547652B82FE,
}


def test_derived_constants_in_c_have_the_checked_bits():
    import struct
    src = (REPO / "src/detmath/detmath.c").read_text()
    for name, want in DERIVED.items():
        m = re.search(r"\b%s = ([-0-9.e+]+)" % name, src)
        assert m, name
        assert struct.unpack("<Q", struct.pack("<d", float(m.group(1))))[0] == want, name
