"""A new fighter's own gameplay (stats, styles, Blazins): the installer's encoding into Character row words 3 and
11 (setup/ffnycontent/newfighter.py) and the game's decoder and clamp (src/hooks/fighter_gameplay.c) agree, and
both refuse the same blocks as a whole. The C side is compiled natively (needs MSVC, `cl` on PATH; skips
without it). docs/research/new-fighter-gameplay.md."""
import random
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))

from ffnycontent import newfighter as nf  # noqa: E402


def block(stats=(12, 9, 10, 8, 9, 5, 6), styles=("K", "S", "M"), blazins=(34, 7, 51, 12)):
    g = {"version": 1, "stats": dict(zip(nf.STAT_KEYS, stats)), "blazins": list(blazins)}
    if styles is not None:
        g["styles"] = list(styles)
    return g


def test_constants_match_the_game():
    h = (ROOT / "src/hooks/fighter_gameplay.h").read_text(encoding="utf-8")
    val = lambda name: int(re.search(r"#define %s (\d+)" % name, h).group(1))
    assert val("FGP_STAT_BUDGET") == nf.STAT_BUDGET
    assert val("FGP_STATS") == len(nf.STAT_KEYS)
    assert val("FGP_BLAZIN_MAX") == nf.BLAZIN_MAX
    assert val("FGP_MOVESET_MAX") == len(nf.archetypes())
    assert val("FGP_ARCHETYPE_BASE") + 1 == nf.ARCHETYPE_FIRST


def test_archetypes_follow_fighter_xml_order():
    a = nf.archetypes()
    assert len(a) == 55 and a[0] == (86, ("B",)) and a[-1] == (140, ("M", "W"))
    names = [nf.archetype_name(c) for _, c in a]
    assert names[:11] == ["CP B__", "CP BK_", "CP BKS", "CP BKW", "CP BKM", "CP BS_", "CP BSW", "CP BSM", "CP BW_",
                          "CP BWM", "CP BM_"]
    assert names[11:14] == ["CP K__", "CP KB_", "CP KBS"]
    assert nf.moveset_of(["K", "M", "S"]) == nf.moveset_of(["K", "S", "M"]) == 104 - 85
    assert nf.styles_of(nf.moveset_of(["W", "B"])) == ["W", "B"]
    assert nf.moveset_of(None) == 0 and nf.styles_of(0) is None


def test_archetypes_match_the_dump():
    for base in (ROOT / "game", Path("D:/DefJam/data/extracted")):
        if (base / "fighter.xml").is_file():
            text = (base / "fighter.xml").read_text(encoding="latin-1")
            break
    else:
        pytest.skip("needs the user's own dump (fighter.xml)")
    found = {int(i): d for d, i in re.findall(r"<dispname>([^<]*)</dispname>\s*<ID>\s*(\d+)", text)}
    for aid, combo in nf.archetypes():
        assert found[aid] == nf.archetype_name(combo)


def test_round_trip_and_row_words():
    g = block()
    w = nf.gameplay_words(g)
    assert nf.gameplay_from_words(w) == g
    assert w[nf.W_STYLE_BITS] == (1 << 0) | (1 << 4) | (1 << 2)       # K S M on the select screen
    assert [w[x] for x in nf.BAR_WORDS] == [nf.stat_bar(v) for v in (12, 9, 10, 8, 9, 6)]
    t = block(styles=None)
    wt = nf.gameplay_words(t)
    assert nf.gameplay_from_words(wt) == t and nf.W_STYLE_BITS not in wt


def test_stock_row_has_no_gameplay():
    assert nf.gameplay_from_words({3: 0, 11: 50}) is None


def test_stock_like_block():
    """Doc's own values as the game decodes them: grapple 0, his Blazin in all four keys."""
    g = block((9, 6, 10, 5, 10, 0, 10), None, (4, 4, 4, 4))
    assert nf.gameplay_problems(g) == []
    assert nf.gameplay_from_words(nf.gameplay_words(g)) == g


@pytest.mark.parametrize("edit,why", [
    (lambda g: g["stats"].update(health=0), "each stat"),
    (lambda g: g["stats"].update(upper=16), "each stat"),
    (lambda g: g["stats"].update({k: 10 for k in nf.STAT_KEYS}), "over the budget"),
    (lambda g: g.update(styles=[]), "styles"),
    (lambda g: g.update(styles=["K", "K"]), "styles"),
    (lambda g: g.update(styles=["B", "K", "S", "W"]), "styles"),
    (lambda g: g.update(blazins=[1, 2, 3, 82]), "Blazin ids"),
    (lambda g: g.update(voice={"mode": "own"}), "voice"),
    (lambda g: g.update(moves="x"), "unknown key"),
    (lambda g: g.update(version=2), "version"),
])
def test_problems(edit, why):
    g = block()
    edit(g)
    problems = nf.gameplay_problems(g)
    assert problems and any(why in p for p in problems)
    with pytest.raises(nf.FighterError):
        nf.gameplay_words(g)


def test_op_with_gameplay():
    op = nf.make_op(55, "V2IP_109A", "V2IP_7K3A", "Doc Two", gameplay=block())
    assert nf.op_problems(op) == []
    op["gameplay"]["blazins"] = [1, 1, 2, 82]
    assert any(p.startswith("gameplay: ") for p in nf.op_problems(op))


DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include "fighter_gameplay.h"
int main(int argc, char **argv)
{
    int i, k;
    for (i = 1; i + 1 < argc; i += 2) {
        FighterGameplay g;
        unsigned long w3 = strtoul(argv[i], NULL, 16), w11 = strtoul(argv[i + 1], NULL, 16);
        if (!fighter_gameplay_decode((uint32_t)w3, (uint32_t)w11, &g)) { printf("-\n"); continue; }
        printf("%d", g.moveset);
        for (k = 0; k < FGP_STATS; k++) printf(" %d", g.stats[k]);
        for (k = 0; k < FGP_BLAZINS; k++) printf(" %d", g.blazins[k]);
        printf(" %d\n", fighter_gameplay_char_index(&g));
    }
    return 0;
}
'''


@pytest.fixture(scope="module")
def decoder(tmp_path_factory):
    compiler = shutil.which("cl")
    if not compiler:
        pytest.skip("needs MSVC (cl on PATH)")
    folder = tmp_path_factory.mktemp("fgp")
    (folder / "driver.c").write_text(DRIVER)
    exe = folder / "fgp.exe"
    built = subprocess.run([compiler, "/nologo", "/O2", "/TC", "/I" + str(ROOT / "src/hooks"), str(folder / "driver.c"),
                            str(ROOT / "src/hooks/fighter_gameplay.c"), "/Fe:" + str(exe)], cwd=folder,
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr

    def run(pairs):
        args = []
        for w3, w11 in pairs:
            args += ["%08x" % w3, "%08x" % w11]
        return subprocess.run([str(exe)] + args, capture_output=True, text=True, check=True).stdout.splitlines()
    return run


def test_c_decoder_agrees(decoder):
    rnd = random.Random(7)
    cases, want = [], []
    for _ in range(300):
        stats = [rnd.randint(0, 15) for _ in range(7)]
        if rnd.random() < 0.5:
            stats = [rnd.randint(1, 9) for _ in range(5)] + [rnd.randint(0, 3), rnd.randint(1, 9)]
        styles = rnd.choice([None] + [list(c) for _, c in nf.archetypes()])
        blazins = [rnd.randint(0, 85) for _ in range(4)]
        g = block(stats, styles, blazins)
        ok = not nf.gameplay_problems(g)
        if ok:
            w = nf.gameplay_words(g)
            w3, w11 = w[3], w[11]
        else:       # what an out-of-range block would encode to (the server never lets one through)
            m = nf.moveset_of(styles) if styles else 0
            w11 = 1 << 28 | ((m >> 4) & 3) << 30 | sum((s & 15) << (4 * k) for k, s in enumerate(stats))
            w3 = (m & 15) << 28 | sum((b & 127) << (7 * k) for k, b in enumerate(blazins))
        back = nf.gameplay_from_words({3: w3, 11: w11})
        cases.append((w3, w11))
        if back is None:
            want.append("-")
        else:
            m = nf.moveset_of(back.get("styles"))
            want.append(" ".join(map(str, [m] + [back["stats"][k] for k in nf.STAT_KEYS] + back["blazins"]
                                     + [85 + m if m else -1])))
    cases += [(0, 50), (0xFFFFFFFF, 0xFFFFFFFF), (0, 0)]
    want += ["-", "-", "-"]
    assert decoder(cases) == want
    assert any(x != "-" for x in want) and any(x == "-" for x in want)


# The stats, Blazins and max health the game decoded for these stock fighters (RECOMP_NF_STATS_DUMP=1,
# logs/dj/20261112-153657-offline and 20261112-171413-offline): U L S T H 6th C, the four Blazin keys.
MEASURED = {55: ((9, 6, 10, 5, 10, 0, 10), 4, 304.0), 56: ((10, 9, 5, 10, 7, 0, 9), 2, 265.6),
            58: ((7, 9, 10, 9, 9, 0, 10), 0, 291.2), 60: ((10, 10, 10, 10, 10, 0, 10), 56, 304.0)}


def test_template_gameplay_matches_the_game():
    sod = None
    for base in (ROOT / "game", Path("D:/DefJam/data/extracted")):
        if (base / "assets" / "djv2.sod").is_file():
            sod = (base / "assets" / "djv2.sod").read_bytes()
            break
    if sod is None:
        pytest.skip("needs the user's own dump (djv2.sod)")
    for fe, (stats, blazin, health) in MEASURED.items():
        g = nf.template_gameplay(sod, fe)
        assert tuple(g["stats"][k] for k in nf.STAT_KEYS) == stats
        assert g["blazins"] == [blazin] * 4
        assert abs(176 + 12.8 * stats[4] - health) < 0.01
    assert max(sum(s) for s, _, _ in MEASURED.values()) == nf.STAT_BUDGET
    # every template's own block is a valid one, inside the budget
    for r in nf.characters(sod):
        if r["main"] == 1 and r["fe_id"] < nf.FIRST_NEW_FE_ID:
            assert nf.gameplay_problems(nf.template_gameplay(sod, r["fe_id"])) == []
