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


def _old_name_above(base, floor):
    """Build 84's name_above (the first suffix whose hash is above ``floor``, anywhere up to 2**32): the row-name
    hints of clothing packs that older Studio betas made."""
    from ffnycontent import sod
    for i in range(1, 500000):
        suffix, n = "", i
        while n:
            n, k = divmod(n, 26)
            suffix += "abcdefghijklmnopqrstuvwxyz"[k]
        name = "%s_%s" % (base, suffix.ljust(7, "a"))
        if sod.name_hash(name) > floor:
            return name


def _clothing_pack(folder, items, hint_floor, name_above=None):
    """A clothing pack of ``items`` new hoodies (ops only), row-name hints made against ``hint_floor``."""
    import json
    from ffnycontent import sodwrite
    name_above = name_above or sodwrite.name_above
    ops = []
    for name, group in items:
        ops.append({"op": "item", "template": "SW_Generic Hoody", "name": name, "price": 1200,
                    "bundle": "PlayerDefault", "group": group, "section": "Ubody",
                    "colours": [{"number": 1, "label": "Gold", "texture": "V2CP_114_0005_%s_01" % group}],
                    "variants": [{"model": "V2CP_114_0005_C", "textures": ["V2CP_114_0005_%s_01" % group]}],
                    "row_name": name_above("FFNY_%s" % name.replace(" ", "")[:18], hint_floor)})
    folder.mkdir(parents=True)
    (folder / "install.json").write_text(json.dumps({"schema": 1, "type": "clothing", "entries": [], "ops": ops}),
                                         encoding="utf-8")
    return folder


def test_48_fighters_15_skins_and_clothing_all_apply_and_compose_the_same_everywhere(tmp_path):
    """Build 85 fix (2026-11-16, "no name above ffffffff found for 'Incredible'"): every new Character and Clothing
    row name hashes just above the table's top, so all 48 places, the skins and the clothing apply, the floor moves
    by at most NAME_WINDOW per row, and two PCs with the same set (other folders, other list order) compose the same
    bytes."""
    import random
    import shutil
    from ffnycontent import sod, sodwrite
    from test_new_fighter_pack import skin_record
    code = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    model = lambda i: "V2IP_7%s%sA" % (code[i // 36], code[i % 36])      # noqa: E731
    src = tmp_path / "a"
    names = ["Incredible", "Mafia Mo", "Mojack", "Robert Parr", "Royalty"] + ["Nf %02d" % i for i in range(43)]
    packs = [record(make_pack(src / ("nf-%02d" % i), model(i), n), "nf-%02d" % i, i + 1, place)
             for i, (n, place) in enumerate(zip(names, nf.PLACES))]
    packs += [skin_record(make_pack(src / ("skin-%02d" % i), model(48 + i), "Skin %02d" % i), "skin-%02d" % i, 100 + i)
              for i in range(15)]
    stock = sod.Sod((GAME / nf.SOD_PATH).read_bytes())
    top = max(stock.rows(stock.table_index(sod.CLOTHING))[0])
    hoodies = [("Hoody %d" % i, "Z%02d" % (i + 1)) for i in range(6)]
    packs.append(record(_clothing_pack(src / "hoodies-new", hoodies[:3], top), "hoodies-new", 200))
    # an older Studio's hints: far up the range (the old name_above took the first suffix above the top)
    packs.append(record(_clothing_pack(src / "hoodies-old", hoodies[3:], top, _old_name_above), "hoodies-old", 201))
    for p in packs[-2:]:
        p["type"] = "clothing"

    def compose(data, packs):
        got = content_compose.compose(data, GAME, packs, off=(), log=lambda t: None, languages=[])
        assert not got["skipped"], got["skipped"]
        return got

    got = compose(tmp_path / "data-a", packs)
    assert len(got["applied"]) == 48 + 15 + 2
    blob = composed(tmp_path / "data-a", nf.SOD_PATH).read_bytes()
    rows = nf.characters(blob)
    assert len(rows) == nf.STOCK_ROWS + 48 and [r["display"] for r in rows[-48:]] == names
    db = sod.Sod(blob)
    stock_rows = nf.characters((GAME / nf.SOD_PATH).read_bytes())
    for before, after in (([r["name"] for r in stock_rows], [r["name"] for r in rows]),
                          ([r.name for r in stock.clothing()], [r.name for r in db.clothing()])):
        floor = max(map(sod.name_hash, before))
        assert after[:len(before)] == before and len(after) > len(before)
        for k in map(sod.name_hash, after[len(before):]):          # in ID order, each just above the last
            assert floor < k <= floor + sodwrite.NAME_WINDOW
            floor = k
    assert len([r for r in db.clothing() if r.name.startswith("FFNY_Hoody")]) == 6

    # another PC: the packs in other folders, the list in another order
    other = tmp_path / "b"
    shutil.copytree(src, other)
    moved = [dict(p, folder=other / Path(p["folder"]).name) for p in packs]
    random.Random(85).shuffle(moved)
    compose(tmp_path / "data-b", moved)
    a, b = composed(tmp_path / "data-a", ""), composed(tmp_path / "data-b", "")
    files = sorted(str(f.relative_to(a)) for f in a.rglob("*") if f.is_file())
    assert files == sorted(str(f.relative_to(b)) for f in b.rglob("*") if f.is_file())
    for rel in files:
        if rel != "files.ini":
            assert (a / rel).read_bytes() == (b / rel).read_bytes(), rel
