"""More than five new fighters at once (2026-11-15, build 83): the composer takes any number of them up to the 28
places, and the game must hold the result. The stock Character table has 75 rows and the game's unlocked-fighter
set 80 ids (sub_000144B0), so from the sixth new fighter on the rows are more than the set holds; with
[debug] unlock_all the developers' unlock routine inserted every row and the 81st id went over the song store and
into the save (crash at the main menu). The game refuses an insert into a full set and repairs a save that ran
over (src/recomp_manual.c, sub_00016810 and sub_00014540); scripts/many-fighters-boot.py boots such a set.
Needs the user's own dump; skipped without it."""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import content_compose  # noqa: E402
from ffnycontent import newfighter as nf, viv  # noqa: E402
from test_new_fighter_pack import GAME, composed, make_pack, record  # noqa: E402

pytestmark = pytest.mark.skipif(GAME is None, reason="needs the user's own dump")

SET_CAPACITY = 80       # the game's unlocked-fighter set (sub_000144B0: record + 0x1C = 0x50)


def test_eight_new_fighters_compose_into_one_set(tmp_path):
    models = ["V2IP_7%s%sA" % (a, b) for a, b in zip("ABCDEFGH", "12345678")]
    packs = [record(make_pack(tmp_path / "packs" / ("nf-%d" % i), m, "Doc X%d" % i), "nf-%d" % i, i + 1, 100 + 3 * i)
             for i, m in enumerate(models)]
    data = tmp_path / "data"
    got = content_compose.compose(data, GAME, packs, off=(), log=lambda t: None, languages=[])
    assert got["applied"] == [p["id"] for p in packs] and not got["skipped"]
    rows = nf.characters(composed(data, nf.SOD_PATH).read_bytes())
    assert len(rows) == nf.STOCK_ROWS + 8 > SET_CAPACITY       # the case the game has to hold
    assert [r["fe_id"] for r in rows[-8:]] == [100 + 3 * i for i in range(8)]
    assert len({r["model"] for r in rows}) == len(rows)
    v2ta = viv.Archive(str(composed(data, nf.BANK_ARCHIVE)))
    v2ip = viv.Archive(str(composed(data, nf.MODEL_ARCHIVE)))
    for m in models:
        code = m[5:]
        assert v2ta.has("CE_%s.viv" % code) and v2ta.has("CI_%s.viv" % code) and v2ip.has(m + ".o")


def test_the_game_keeps_the_sets_inside():
    """The guards stay in the game: an insert into a full set is refused, a save that ran over is taken back."""
    src = (ROOT / "src" / "recomp_manual.c").read_text(encoding="utf-8")
    assert "extern void sub_00016810_gen(void);" in src and "void sub_00016810(void)" in src
    assert "extern void sub_00014540_gen(void);" in src and "void sub_00014540(void)" in src
    assert "MEM32(set + 4) >= MEM32(set)" in src
