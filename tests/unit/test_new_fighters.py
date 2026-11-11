"""New fighters as Character rows (setup/ffnycontent/newfighter.py): the row goes last in both class blocks and in
its place in the feID list, no existing row moves or changes its ID, and the select-screen hook's C side reads
the same layout. Needs the user's own dump (game/assets/djv2.sod or $DEFJAM_DATA/extracted); skipped without it."""
import os
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))

from ffnycontent import newfighter as nf  # noqa: E402
from ffnycontent.sod import Sod, name_hash  # noqa: E402


def _dump():
    for base in (os.environ.get("DEFJAM_DATA"), str(ROOT / "game" / "..")):
        if not base:
            continue
        for p in (Path(base) / "extracted" / "assets" / "djv2.sod", ROOT / "game" / "assets" / "djv2.sod"):
            if p.is_file():
                return p.read_bytes()
    return None


SOD = _dump()
pytestmark = pytest.mark.skipif(SOD is None, reason="needs the user's own dump (djv2.sod)")


def _blocks(blob):
    s = Sod(blob)
    off = s.table_offsets[s.table_hashes.index(nf.CHARACTER)]
    u = lambda o: struct.unpack_from("<I", blob, o)[0]
    classes = u(off + 0x10)
    out = []
    for c in range(classes):
        b = u(off + 0x18 + 4 * classes + 4 * c)
        n = u(b + 4)
        out.append((u(off + 0x18 + 4 * c), list(struct.unpack_from("<%dI" % n, blob, b + 12)),
                    list(struct.unpack_from("<%dI" % n, blob, b + 12 + 4 * n))))
    lists = u(off + 8)
    m = u(lists)
    plain = []
    for i in range(m):
        b = u(lists + 8 + 4 * m + 4 * i)
        n = u(b + 4)
        plain.append(list(struct.unpack_from("<%dI" % n, blob, b + 12)))
    return u(off + 0xC), out, plain


def test_append_keeps_every_row_and_goes_last():
    before = nf.characters(SOD)
    out, row_name, item = nf.append_character(SOD, 55, 100, "Doc 2", "Doc 2")
    after = nf.characters(out)
    assert len(after) == len(before) + 1
    for a, b in zip(before, after):
        assert (a["index"], a["fe_id"], a["name"], a["words"]) == (b["index"], b["fe_id"], b["name"], b["words"])
    new = after[-1]
    assert item == (7 << 24) | len(before)
    assert (new["fe_id"], new["core_id"], new["display"], new["main"]) == (100, 10, "Doc 2", 1)
    assert new["name"] == row_name and row_name.startswith("Doc 2")
    count, classes, plain = _blocks(out)
    assert count == len(after)
    for class_hash, keys, vals in classes:
        assert keys == sorted(keys) and len(keys) == count
        assert vals[-1] == new["offset"]
        if class_hash == nf.CHARACTER:
            assert keys[-1] == name_hash(row_name)
    for ptrs in plain:     # the feID list: sorted by feID
        fes = [struct.unpack_from("<I", out, p + 4 * nf.W_FE_ID)[0] for p in ptrs]
        assert fes == sorted(fes) and 100 in fes


def test_several_and_refusals():
    out, _, _ = nf.append_character(SOD, 55, 101, "Doc 3", "Doc 3", model="V2IP_901A", edits={12: 10})
    out, _, item = nf.append_character(out, 56, 100, "Blaze 4", "Blaze 4")
    rows = nf.characters(out)
    assert [r["fe_id"] for r in rows[-2:]] == [101, 100]
    assert rows[-2]["model"] == "V2IP_901A" and rows[-2]["words"][12] == 10
    _, _, plain = _blocks(out)
    fes = [struct.unpack_from("<I", out, p + 4 * nf.W_FE_ID)[0] for p in plain[0]]
    assert fes == sorted(fes)
    with pytest.raises(nf.FighterError):
        nf.append_character(out, 55, 100, "Again", "Again")
    with pytest.raises(nf.FighterError):
        nf.append_character(SOD, 55, 255, "Too far", "Too far")
    with pytest.raises(nf.FighterError):
        nf.append_character(SOD, 55, 100, "x", "x", edits={nf.W_CORE_ID: 1})


def test_c_reader_matches_the_layout():
    src = (ROOT / "src/hooks/new_fighters.c").read_text(encoding="utf-8")
    for name, value in (("W_DISPLAY", nf.W_DISPLAY), ("W_FE_ID", nf.W_FE_ID), ("W_CORE_ID", nf.W_CORE_ID),
                        ("W_MAIN", nf.W_MAIN), ("ROW_WORDS", nf.ROW_WORDS)):
        assert "#define %s %d" % (name, value) in src
    hdr = (ROOT / "src/hooks/new_fighters.h").read_text(encoding="utf-8")
    assert "#define NEW_FIGHTER_FIRST %d" % nf.FIRST_NEW_FE_ID in hdr
    assert "#define NEW_FIGHTER_LAST %d" % nf.LAST_NEW_FE_ID in hdr
