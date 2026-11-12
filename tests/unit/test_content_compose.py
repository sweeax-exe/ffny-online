"""FFNY Online's composed packs (setup/content_compose.py): fighter bodies, clothing and venues built from the
player's own files plus each pack's deltas and ops. Synthetic files only: every archive, database, parts list,
name table and venue list is built here (the layouts follow FFNY Studio's fixtures)."""
import hashlib
import shutil
import json
import os
from pathlib import Path
import struct
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'setup'))
import content_compose as cc  # noqa: E402
from ffnycontent import clothing, cpparts, delta, loc, newvenue, sod, sodwrite, viv  # noqa: E402
sys.path.pop(0)


# -- synthetic game files -------------------------------------------------------------------------------------

def build_sod(rows, strings, bundle_items=(), venues=()):
    """A SODB laid out as the game's: Clothing (class block 0 by name hash, class block 1 by type, a row list), the
    bundle table with PlayerDefault and, with ``venues``, a Venue table. ``rows``: [(name, num, group,
    [(colour string, Number)], type)]; ``venues``: [(row name, display, filename, fe_id)]."""
    out = bytearray(24)

    def put(data):
        while len(out) % 4:
            out.append(0)
        off = len(out)
        out.extend(data)
        return off

    strings = list(strings) + [s for v in venues for s in (v[0], v[1], v[2])]
    sidx = {s: i for i, s in enumerate(strings)}
    skma = put(struct.pack("<4sII", b"SKMA", 0, 2))
    rows = sorted(rows, key=lambda r: sod.name_hash(r[0]))
    variants = put(struct.pack("<I", 1) + struct.pack("<I", 0))
    vel = put(struct.pack("<3I", 0, 0, 0x400))
    struct.pack_into("<I", out, variants + 4, vel)
    offsets = []
    for name, num, group, colours, typ in rows:
        elems = [put(struct.pack("<3I", 0, sidx[d], n)) for d, n in colours]
        clist = put(struct.pack("<I%dI" % len(elems), len(elems), *elems))
        offsets.append(put(struct.pack("<9I", sidx[name], num, sidx[group], clist, 45, 0, variants, 300, typ)))

    def block(tag, keys, vals, stride=1):
        return put(struct.pack("<4sII", tag, len(keys), stride) + struct.pack("<%dI" % len(keys), *keys)
                   + (struct.pack("<%dI" % len(vals), *vals) if stride else b""))

    names = [sod.name_hash(r[0]) for r in rows]
    by_type = sorted(zip([r[4] for r in rows], offsets))
    b0 = block(struct.pack("<I", sod.name_hash("Clothing")), names, offsets)
    b1 = block(b"TYPE", [t for t, _ in by_type], [o for _, o in by_type])
    rl = block(b"LIST", offsets, [], stride=0)
    cloth = len(out) + (-len(out)) % 4
    put(struct.pack("<4I", sod.name_hash("Clothing"), cloth + 0x10, cloth + 0x28, len(rows))
        + struct.pack("<2I2I2I", 2, 1, sod.name_hash("Clothing"), 0x9F135B1A, b0, b1)
        + struct.pack("<4I", 1, 1, 0x594E1CFC, rl))
    elems = [put(struct.pack("<3I", 0, i, sodwrite.ITEM_FLAGS)) for i in bundle_items]
    blist = put(struct.pack("<I%dI" % len(elems), len(elems), *elems))
    brow = put(struct.pack("<4I", sidx["PlayerDefault"], 0, blist, 0))
    bb = block(b"BUND", [sod.name_hash("PlayerDefault")], [brow])
    bundles = len(out) + (-len(out)) % 4
    put(struct.pack("<4I", sodwrite.BUNDLES, 0, 0, 1) + struct.pack("<4I", 1, 1, sodwrite.BUNDLES, bb))
    hashes, tables = [sod.name_hash("Clothing"), sodwrite.BUNDLES], [cloth, bundles]
    if venues:
        vrows = []
        for name, display, filename, fe_id in sorted(venues, key=lambda v: sod.name_hash(v[0])):
            words = [0] * 21
            words[sod.VENUE_NAME], words[sod.VENUE_DISPLAY] = sidx[name], sidx[display]
            words[sod.VENUE_FE_ID], words[sod.VENUE_CORE_ID], words[sod.VENUE_MODES] = fe_id, 2, 0xFF
            words[sod.VENUE_FILENAME] = sidx[filename]
            vrows.append((sod.name_hash(name), put(struct.pack("<21I", *words))))
        vb = block(b"VENU", [k for k, _ in vrows], [p for _, p in vrows])
        vt = len(out) + (-len(out)) % 4
        put(struct.pack("<4I", sod.VENUE, 0, 0, len(vrows)) + struct.pack("<4I", 1, 1, sod.VENUE, vb))
        hashes.append(sod.VENUE)
        tables.append(vt)
    data = put(struct.pack("<4sII", b"DATA", len(hashes), 1) + struct.pack("<%dI" % len(hashes), *hashes)
               + struct.pack("<%dI" % len(tables), *tables))
    strt = len(out) + (-len(out)) % 4
    base = strt + 8 + 8 * len(strings)
    pool, entries = bytearray(), []
    for s in strings:
        if s.startswith("#"):
            entries.append((int(s[1:], 16), 0))
        else:
            entries.append((sod.name_hash(s), base + len(pool)))
            pool += s.encode() + b"\0"
    put(struct.pack("<4sI", b"STRT", len(strings)) + b"".join(struct.pack("<II", *e) for e in entries) + pool)
    struct.pack_into("<4sII3I", out, 0, b"SODB", 2, 0x86F322AC, skma, data, strt)
    return bytes(out)


STRINGS = ["SW_Generic Hoody", "A01", "T_Plain Tee", "B01", "PlayerDefault", "#11110001", "#11110002"]
ROWS = [("SW_Generic Hoody", 5, "A01", [("#11110001", 1), ("#11110002", 2)], 3),
        ("T_Plain Tee", 17, "B01", [("#11110001", 1)], 4)]
NAMES = [(0x11110001, "White"), (0x11110002, "Black")]
VENUES = [("ARENA_BG03A", "The Power Plant", "V2BG_03A", 3)]

PARTS = """<partslist>
\t<?TypeBody = "Ubody"?>
\t<head>1.1.X</head>
\t<parts>
\t\t<contents>
\t\t\t<count>1</count>
\t\t\t<model>
\t\t\t\t<name>V2CP_114_0005_C</name>
\t\t\t\t<count>1</count>
\t\t\t\t<texture>
\t\t\t\t\t<name>V2CP_114_0005_A01_01</name>
\t\t\t\t\t<count>1</count>
\t\t\t\t\t<colour>
\t\t\t\t\t\t<name>V2CP_114_0005_A01_02</name>
\t\t\t\t\t</colour>
\t\t\t\t</texture>
\t\t\t</model>
\t\t</contents>
\t</parts>
</partslist>
""".replace("\n", "\r\n")

VENUE_XML = """<?xml version="1.0" encoding="utf-8" ?>\r
\r
<venuelist>\r
\r
\t<venue>\r
\t\t<name>ARENA_BG03A</name>\r
\t\t<filename>V2BG_03A</filename>\r
\t\t<dispname>03A The Power Plant</dispname>\r
\t\t<ID>4</ID>\r
\t\t<coreID>2</coreID>\r
\t\t<feID>3</feID>\r
\t</venue>\r
\r
</venuelist>\r
"""

ENTITY_XML = """<entitylist>\r
\t<entity>\r
\t\t<name>V2BG_03A</name>\r
\t\t<model><name>V2BG_03A</name></model>\r
\t\t<model><name>V2BG_03A_A</name></model>\r
\t\t<xml>V2BG_03A</xml>\r
\t</entity>\r
</entitylist>\r
"""

BODY_O = b"\x7fELF" + bytes(range(256)) * 4
BODY_XSH = b"SHPX" + bytes(200)
TEX_A = b"SHPX" + bytes([1]) * 300
TEX_B = b"SHPX" + bytes([2]) * 300


@pytest.fixture
def game(tmp_path):
    root = tmp_path / "game"
    (root / "assets").mkdir(parents=True)
    hoody = sod.Sod(build_sod(ROWS, STRINGS)).find_clothing("SW_Generic Hoody").id
    (root / "assets" / "djv2.sod").write_bytes(build_sod(ROWS, STRINGS, [hoody], VENUES))
    (root / "assets" / "v2ip.viv").write_bytes(viv.build_archive(
        [("V2IP_109A.o", viv.refpack_store(BODY_O)), ("V2IP_109A.xsh", BODY_XSH)], align=128,
        trailer=b"L231\0\0\0\x05"))
    (root / "assets" / "v2cp_0.viv").write_bytes(viv.build_archive(
        [("createplayer_parts.xml", viv.refpack_store(PARTS.encode("latin-1")))], align=128))
    (root / "assets" / "v2cp_1.viv").write_bytes(viv.build_archive(
        [("V2CP_114_0005_C.o", viv.refpack_store(b"\x7fELF" + bytes(100))),
         ("V2CP_114_0005_A01_01.xsh", viv.refpack_store(TEX_A)),
         ("V2CP_114_0005_A01_02.xsh", viv.refpack_store(TEX_B))], align=128))
    names = loc.build(NAMES)
    (root / "assets" / "misc.viv").write_bytes(viv.build_archive(
        [("cmusen.loc", viv.refpack_store(names)), ("cmuken.loc", viv.refpack_store(names))], magic=b"BIG4",
        align=128))
    (root / "venue.xml").write_bytes(VENUE_XML.encode("latin-1"))
    (root / "assets" / "entity.xml").write_bytes(ENTITY_XML.encode("latin-1"))
    (root / "assets" / "v2bg.viv").write_bytes(viv.build_archive(
        [("V2BG_03A.o", viv.refpack_store(b"\x7fELF" + bytes(300))),
         ("V2BG_03A.xml", viv.refpack_store(b"<stage><name>V2BG_03A</name></stage>"))], align=128))
    (root / "assets" / "nis.viv").write_bytes(viv.build_archive(
        [("assets/FlyIn_03A.xml", viv.refpack_store(b"<fly_in><venue>V2BG_03A</venue></fly_in>"))], align=128))
    return root


@pytest.fixture
def data(tmp_path):
    d = tmp_path / "data"
    (d / "mods").mkdir(parents=True)
    return d


def make_pack(data, pack_id, type_, entries=(), ops=(), kind="content", order=1, version="1.0.0"):
    """A synced pack folder: install.json and its deltas. ``entries``: (archive, entry, mode, base_archive,
    base_entry, base bytes, result bytes)."""
    folder = data / "mods" / pack_id
    (folder / "deltas").mkdir(parents=True)
    rows = []
    for k, (archive, entry, mode, base_archive, base_entry, base, result) in enumerate(entries):
        rel = "deltas/%d_%s.ffdelta" % (k, (entry or archive).split("/")[-1])
        (folder / rel).write_bytes(delta.encode(base, result))
        rows.append({"archive": archive, "entry": entry, "mode": mode, "base_archive": base_archive,
                     "base_entry": base_entry, "delta": rel, "original_sha256": hashlib.sha256(base).hexdigest(),
                     "result_sha256": hashlib.sha256(result).hexdigest(), "result_size": len(result)})
    (folder / "install.json").write_text(json.dumps({"schema": 1, "type": type_, "entries": rows,
                                                     "ops": list(ops)}), encoding="utf-8")
    return {"id": pack_id, "version": version, "content_sha256": hashlib.sha256(pack_id.encode()).hexdigest(),
            "kind": kind, "order": order, "folder": folder}


def compose(data, game, packs, off=()):
    return cc.compose(data, game, packs, off=off, log=lambda text: None)


def out(data, rel=""):
    return data / "mod-manager" / "content-files" / rel


def files_ini(data):
    return cc.read_files_ini(data / "mod-manager")


def body_pack(data, pack_id, new, order=1, kind="replacement"):
    return make_pack(data, pack_id, "fighter-body", [("assets/v2ip.viv", "V2IP_109A.o", "replace", "assets/v2ip.viv",
                                                      "V2IP_109A.o", BODY_O, new)], kind=kind, order=order)


def item_op(name, group, row_name_floor):
    return {"op": "item", "template": "SW_Generic Hoody", "name": name, "price": 1200, "bundle": "PlayerDefault",
            "group": group, "section": "Ubody",
            "colours": [{"number": 1, "label": "Gold", "texture": "V2CP_114_0005_%s_01" % group}],
            "variants": [{"model": "V2CP_114_0005_C", "textures": ["V2CP_114_0005_%s_01" % group]}],
            "row_name": sodwrite.name_above("FFNY_%s" % name.replace(" ", "")[:18], row_name_floor)}


def clothing_floor(game):
    db = sod.Sod((game / "assets" / "djv2.sod").read_bytes())
    return max(db.rows(db.table_index(sod.CLOTHING))[0])


# -- tests ----------------------------------------------------------------------------------------------------

def test_fighter_body_replace_composes_the_archive(game, data):
    new = BODY_O[:500] + b"modder mesh" * 40 + BODY_O[600:]
    dump = hashlib.sha256((game / "assets" / "v2ip.viv").read_bytes()).hexdigest()
    result = compose(data, game, [body_pack(data, "body-one", new)])
    assert result["applied"] == ["body-one"] and result["files"] == ["assets/v2ip.viv"]
    composed = viv.Archive(out(data, "assets/v2ip.viv"))
    assert composed.read("V2IP_109A.o") == new and viv.is_refpack(composed.raw("V2IP_109A.o"))
    assert composed.read("V2IP_109A.xsh") == BODY_XSH
    ini = files_ini(data)
    assert ini["schema"] == "1" and ini["applied"] == "body-one" and ini["skipped"] == ""
    assert ini["files"] == ["assets/v2ip.viv"] and len(ini["set"]) == 64
    assert hashlib.sha256((game / "assets" / "v2ip.viv").read_bytes()).hexdigest() == dump   # never written


def test_base_is_the_override_copy_when_there_is_one(game, data):
    (data / "override" / "assets").mkdir(parents=True)
    patched = viv.build_archive([("V2IP_109A.o", viv.refpack_store(BODY_O)), ("V2IP_109A.xsh", BODY_XSH),
                                 ("PORT_EXTRA.o", b"port")], align=128)
    (data / "override" / "assets" / "v2ip.viv").write_bytes(patched)
    compose(data, game, [body_pack(data, "body-one", BODY_O + b"x")])
    assert viv.Archive(out(data, "assets/v2ip.viv")).read("PORT_EXTRA.o") == b"port"


def test_clothing_adds_and_ops(game, data):
    colour = {"op": "colour", "item": "SW_Generic Hoody", "item_id": 0, "number": 3, "label": "Studio red",
              "section": "Ubody", "textures": [{"model": "V2CP_114_0005_C", "texture": "V2CP_114_0005_A01_03",
                                                "first_texture": "V2CP_114_0005_A01_01"}]}
    item = item_op("Brick City Hoody", "Z01", clothing_floor(game))
    pack = make_pack(data, "hoodies", "clothing", [
        ("assets/v2cp_1.viv", "V2CP_114_0005_A01_03.xsh", "add", "assets/v2cp_1.viv", "V2CP_114_0005_A01_01.xsh",
         TEX_A, TEX_A[:100] + b"red" * 60),
        ("assets/v2cp_1.viv", "V2CP_114_0005_Z01_01.xsh", "add", "assets/v2cp_1.viv", "V2CP_114_0005_A01_01.xsh",
         TEX_A, TEX_A[:50] + b"gold" * 60)], [colour, item])
    result = compose(data, game, [pack])
    assert result["applied"] == ["hoodies"], result
    assert set(result["files"]) == {"assets/djv2.sod", "assets/v2cp_0.viv", "assets/v2cp_1.viv", "assets/misc.viv"}
    db = sod.Sod(out(data, "assets/djv2.sod").read_bytes())
    hoody = db.find_clothing("SW_Generic Hoody")
    assert [n for _, n in hoody.colours] == [1, 2, 3]
    new = [r for r in db.clothing() if r.name.startswith("FFNY_")]
    assert len(new) == 1 and new[0].cost == 1200 and new[0].group == "Z01"
    assert new[0].id in [i for i, _ in sodwrite.bundles(db)["PlayerDefault"][1]]
    parts = viv.Archive(out(data, "assets/v2cp_0.viv")).read("createplayer_parts.xml").decode("latin-1")
    assert cpparts.check(parts) == [] and "V2CP_114_0005_A01_03" in parts and "V2CP_114_0005_Z01_01" in parts
    names = loc.Loc(viv.Archive(out(data, "assets/misc.viv")).read("cmusen.loc"))
    assert names.get(clothing.text_hash("colour", "Studio red", "V2CP_114_0005_A01_03")) == "Studio red"
    assert names.get(0x11110001) == "White"
    tex = viv.Archive(out(data, "assets/v2cp_1.viv"))
    assert tex.read("V2CP_114_0005_Z01_01.xsh").endswith(b"gold") and tex.read("V2CP_114_0005_A01_01.xsh") == TEX_A


def test_new_venue(game, data):
    op = {"op": "new-venue", "template": "V2BG_03A", "template_code": "03A", "template_fe_id": 3,
          "filename": "V2BG_28A", "code": "28A", "fe_id": 28, "title": "The Block", "dispname": "28A The Block",
          "drop_objects": True}
    v2bg = viv.Archive(game / "assets" / "v2bg.viv")
    stage = v2bg.read("V2BG_03A.xml")
    pack = make_pack(data, "the-block", "venue", [
        ("assets/v2bg.viv", "V2BG_28A.o", "add", "assets/v2bg.viv", "V2BG_03A.o", v2bg.read("V2BG_03A.o"),
         b"\x7fELF" + b"new venue" * 30),
        ("assets/v2bg.viv", "V2BG_28A.xml", "add", "assets/v2bg.viv", "V2BG_03A.xml", stage,
         stage.replace(b"V2BG_03A", b"V2BG_28A")),
        ("assets/nis.viv", "assets/FlyIn_28A.xml", "add", "assets/nis.viv", "assets/FlyIn_03A.xml",
         b"<fly_in><venue>V2BG_03A</venue></fly_in>", b"<fly_in><venue>V2BG_28A</venue></fly_in>")], [op])
    result = compose(data, game, [pack])
    assert result["applied"] == ["the-block"], result
    venue_xml = out(data, "venue.xml").read_text("latin-1")
    assert "<filename>V2BG_28A</filename>" in venue_xml and "<feID>28</feID>" in venue_xml
    assert "<ID>5</ID>" in venue_xml and "<name>ARENA_BG28A</name>" in venue_xml
    assert venue_xml.rstrip().endswith("</venuelist>")
    entity = out(data, "assets/entity.xml").read_text("latin-1")
    assert "<name>V2BG_28A</name>" in entity and "V2BG_03A_A" in entity.split("V2BG_28A", 1)[0]
    assert entity.count("V2BG_03A_A") == 1                      # drop_objects: the copy keeps only its main model
    rows = {v["fe_id"]: v for _, v in sod.venues(out(data, "assets/djv2.sod").read_bytes())}
    assert rows[28]["filename"] == "V2BG_28A" and rows[28]["display"] == "The Block" and rows[3]["filename"] == "V2BG_03A"
    assert viv.Archive(out(data, "assets/v2bg.viv")).read("V2BG_28A.xml") == b"<stage><name>V2BG_28A</name></stage>"
    assert viv.Archive(out(data, "assets/nis.viv")).has("assets/FlyIn_28A.xml")
    # the same venue again in another pack: feID taken -> skipped whole
    again = make_pack(data, "the-block-2", "venue", [], [op], order=2)
    result = compose(data, game, [pack, again])
    assert result["applied"] == ["the-block"] and result["skipped"][0][0] == "the-block-2"
    assert "feID 28 is taken" in result["skipped"][0][1]



LOCATOR = b"\x7fELF" + bytes(range(256)) * 40           # a stand-in locator file (10244 bytes)
CROWD_OP = {"op": "new-venue", "template": "V2BG_03A", "template_code": "03A", "template_fe_id": 3,
            "filename": "V2BG_28A", "code": "28A", "fe_id": 28, "title": "The Block", "dispname": "28A The Block",
            "drop_objects": False, "crowd_locator": "icl_28A"}


def crowd_game(game):
    """The test game with a far crowd: icrowd.viv holds icl_03A.o and the template's entity names it."""
    (game / "assets" / "icrowd.viv").write_bytes(viv.build_archive(
        [("icrowd01.o", viv.refpack_store(b"\x7fELF" + bytes(64))), ("icl_03A.o", viv.refpack_store(LOCATOR))],
        align=128))
    ent = ENTITY_XML.replace("\t\t<xml>V2BG_03A</xml>\r\n", "\t\t<xml>V2BG_03A</xml>\r\n\t\t<instancecrowd>\r\n"
                             "\t\t\t<instancecrowdxml>icrowd01</instancecrowdxml>\r\n"
                             "\t\t\t<instancecrowdlocator>icl_03A</instancecrowdlocator>\r\n\t\t</instancecrowd>\r\n")
    (game / "assets" / "entity.xml").write_bytes(ent.encode("latin-1"))
    return game


def crowd_entry(entry="icl_28A.o", mode="add", base_entry="icl_03A.o", result=None):
    moved = LOCATOR[:100] + b"\x41" * 12 + LOCATOR[112:]          # places moved: same size, same kind
    return ("assets/icrowd.viv", entry, mode, "assets/icrowd.viv", base_entry, LOCATOR,
            moved if result is None else result)


def test_new_venue_with_its_own_far_crowd(game, data):
    crowd_game(game)
    pack = make_pack(data, "far", "venue", [crowd_entry()], [CROWD_OP])
    result = compose(data, game, [pack])
    assert result["applied"] == ["far"], result
    icrowd = viv.Archive(out(data, "assets/icrowd.viv"))
    assert icrowd.read("icl_28A.o") == crowd_entry()[6] and icrowd.read("icl_03A.o") == LOCATOR
    entity = out(data, "assets/entity.xml").read_text("latin-1")
    template, new = entity.split("<name>V2BG_28A</name>", 1)
    assert "<instancecrowdlocator>icl_03A</instancecrowdlocator>" in template       # the standard venue's stays
    assert "<instancecrowdlocator>icl_28A</instancecrowdlocator>" in new


def test_new_venue_far_crowd_is_refused_unless_it_is_its_own_add(game, data):
    crowd_game(game)
    plain = {k: v for k, v in CROWD_OP.items() if k != "crowd_locator"}
    cases = {
        "retail": ([crowd_entry("icl_03A.o", "replace", "icl_03A.o")], CROWD_OP),      # replaces a retail locator
        "other": ([crowd_entry("icl_29A.o")], CROWD_OP),                              # not its own code
        "unnamed": ([crowd_entry()], plain),                                          # added, not named
        "missing": ([], CROWD_OP),                                                    # named, not added
        "foreign": ([crowd_entry()], dict(CROWD_OP, crowd_locator="icl_03A")),        # names a retail locator
        "base": ([crowd_entry(base_entry="icrowd01.o")], CROWD_OP),                   # not from a locator
        "notlocator": ([crowd_entry(result=b"<xml/>" + bytes(len(LOCATOR) - 6))], CROWD_OP),
        "size": ([crowd_entry(result=LOCATOR + bytes(64))], CROWD_OP),
    }
    for order, (pack_id, (entries, op)) in enumerate(sorted(cases.items()), 1):
        pack = make_pack(data, pack_id, "venue", entries, [op], order=order)
        result = compose(data, game, [pack])
        assert result["applied"] == [] and result["skipped"][0][0] == pack_id, (pack_id, result)
    # icl_28A.o in the game already (another mod's): the pack does not overwrite it
    (game / "assets" / "icrowd.viv").write_bytes(viv.build_archive(
        [("icl_03A.o", viv.refpack_store(LOCATOR)), ("icl_28A.o", viv.refpack_store(LOCATOR))], align=128))
    pack = make_pack(data, "taken", "venue", [crowd_entry()], [CROWD_OP], order=20)
    result = compose(data, game, [pack])
    assert result["applied"] == [] and "exists already" in result["skipped"][0][1]


def test_crowd_locator_only_changes_the_new_entity_s_locator(game, data):
    """With crowd_locator the new entity names its own far crowd; without it (a pack before the field, or a
    launcher before it, which reads only the op fields it knows) the copy shares the template's, as before."""
    crowd_game(game)
    op = dict(CROWD_OP)
    venue_xml, entity_xml, _ = newvenue.apply((game / "venue.xml").read_bytes(),
                                              (game / "assets" / "entity.xml").read_bytes(),
                                              (game / "assets" / "djv2.sod").read_bytes(), op)
    assert b"<instancecrowdlocator>icl_28A</instancecrowdlocator>" in entity_xml
    op.pop("crowd_locator")
    _, entity_xml, _ = newvenue.apply((game / "venue.xml").read_bytes(),
                                      (game / "assets" / "entity.xml").read_bytes(),
                                      (game / "assets" / "djv2.sod").read_bytes(), op)
    assert entity_xml.count(b"<instancecrowdlocator>icl_03A</instancecrowdlocator>") == 2


def test_two_packs_combine_in_order(game, data):
    floor = clothing_floor(game)
    a = make_pack(data, "pack-a", "clothing", [
        ("assets/v2cp_1.viv", "V2CP_114_0005_Y01_01.xsh", "add", "assets/v2cp_1.viv", "V2CP_114_0005_A01_01.xsh",
         TEX_A, TEX_A + b"a")], [item_op("Alpha Hoody", "Y01", floor)], order=2)
    b = make_pack(data, "pack-b", "clothing", [
        ("assets/v2cp_1.viv", "V2CP_114_0005_X01_01.xsh", "add", "assets/v2cp_1.viv", "V2CP_114_0005_A01_01.xsh",
         TEX_A, TEX_A + b"b")], [item_op("Alpha Hoody", "X01", floor)], order=1)   # same hint as pack-a
    result = compose(data, game, [a, b])
    assert result["applied"] == ["pack-b", "pack-a"]           # catalogue order, not the list's
    db = sod.Sod(out(data, "assets/djv2.sod").read_bytes())
    new = sorted((r for r in db.clothing() if r.name.startswith("FFNY_")), key=lambda r: r.index)
    assert [r.group for r in new] == ["X01", "Y01"] and new[0].name != new[1].name
    keys = db.rows(db.table_index(sod.CLOTHING))[0]
    assert keys == sorted(keys[:2]) + keys[2:] and keys[-1] > keys[-2] > floor
    # two replacements of one body entry: the later pack wins
    one = body_pack(data, "body-one", BODY_O + b"one", order=3)
    two = body_pack(data, "body-two", BODY_O + b"two", order=4)
    result = compose(data, game, [two, one])
    assert result["applied"] == ["body-one", "body-two"]
    assert viv.Archive(out(data, "assets/v2ip.viv")).read("V2IP_109A.o") == BODY_O + b"two"


def test_conflicts_skip_the_whole_pack(game, data):
    entry = ("assets/v2cp_1.viv", "V2CP_114_0005_W01_01.xsh", "add", "assets/v2cp_1.viv",
             "V2CP_114_0005_A01_01.xsh", TEX_A, TEX_A + b"first")
    first = make_pack(data, "first", "clothing", [entry], order=1)
    clash = make_pack(data, "clash", "clothing", [entry[:6] + (TEX_A + b"second",),
                                                  ("assets/v2cp_1.viv", "V2CP_114_0005_V01_01.xsh", "add",
                                                   "assets/v2cp_1.viv", "V2CP_114_0005_A01_01.xsh", TEX_A, TEX_A)],
                      order=2)
    exists = make_pack(data, "exists", "clothing", [("assets/v2cp_1.viv", "V2CP_114_0005_A01_02.xsh", "add",
                                                     "assets/v2cp_1.viv", "V2CP_114_0005_A01_01.xsh", TEX_A, TEX_A)],
                       order=3)
    stale = make_pack(data, "stale", "fighter-body", [("assets/v2ip.viv", "V2IP_109A.o", "replace",
                                                       "assets/v2ip.viv", "V2IP_109A.o", BODY_O + b"old", BODY_O)],
                      order=4)
    result = compose(data, game, [first, clash, exists, stale])
    assert result["applied"] == ["first"]
    reasons = dict(result["skipped"])
    assert "exists already" in reasons["clash"] and "exists already" in reasons["exists"]
    assert "not the version" in reasons["stale"]
    tex = viv.Archive(out(data, "assets/v2cp_1.viv"))
    assert tex.read("V2CP_114_0005_W01_01.xsh") == TEX_A + b"first"
    assert not tex.has("V2CP_114_0005_V01_01.xsh")          # nothing of the skipped pack, not even its other entry
    assert files_ini(data)["skipped"].startswith("clash: ")
    assert not out(data, "assets/v2ip.viv").exists()


def test_off_switch_removal_and_unchanged_skip(game, data, monkeypatch):
    body = body_pack(data, "body-one", BODY_O + b"mine")
    first = compose(data, game, [body])
    assert first["applied"] == ["body-one"] and not first["unchanged"]
    assert compose(data, game, [body])["unchanged"]                       # same packs, same bases
    stamp = out(data, "files.ini").stat().st_mtime_ns
    assert compose(data, game, [body], off=["some-texture-pack"])["unchanged"]   # an unrelated switch
    assert out(data, "files.ini").stat().st_mtime_ns == stamp
    # the player switched it off ([content] off in the settings file): the originals again, folder gone
    settings = data / "settings.ini"
    settings.write_text("[launcher]\nskip=0\n[content]\noff=other, body-one\n", encoding="utf-8")
    monkeypatch.setenv("RECOMP_SETTINGS", str(settings))
    result = cc.compose(data, game, [body], log=lambda t: None)
    assert result["applied"] == [] and not out(data).exists()
    monkeypatch.setenv("RECOMP_SETTINGS", "none")
    assert compose(data, game, [body])["applied"] == ["body-one"] and out(data, "files.ini").is_file()
    # a content pack cannot be switched off
    content = body_pack(data, "body-two", BODY_O + b"content", order=2, kind="content")
    assert compose(data, game, [content], off=["body-two"])["applied"] == ["body-two"]
    # removed from the catalogue: nothing applies, the folder goes
    assert compose(data, game, [])["applied"] == [] and not out(data).exists()
    assert compose(data, game, [])["unchanged"]
    # a changed base (size or mtime) composes again
    compose(data, game, [body])
    path = game / "assets" / "v2ip.viv"
    os.utime(path, ns=(path.stat().st_atime_ns, path.stat().st_mtime_ns + 10**9))
    assert not compose(data, game, [body])["unchanged"]
    # a deleted output file composes again
    out(data, "assets/v2ip.viv").unlink()
    again = compose(data, game, [body])
    assert not again["unchanged"] and out(data, "assets/v2ip.viv").is_file()


def test_set_hash_and_files_ini_fields(game, data):
    body = body_pack(data, "body-one", BODY_O + b"x")
    compose(data, game, [body], off=[])
    expected = hashlib.sha256(("body-one\x001.0.0\x00%s\n" % body["content_sha256"]).encode() + b"off:").hexdigest()
    assert files_ini(data)["set"] == expected
    text = out(data, "files.ini").read_text(encoding="utf-8").splitlines()
    assert text[:5] == ["[content-files]", "schema=1", "set=" + expected, "applied=body-one", "skipped="]
    assert text[5:] == ["file=assets/v2ip.viv"]


def test_interrupted_swap_keeps_the_complete_folder(game, data):
    body = body_pack(data, "body-one", BODY_O + b"x")
    compose(data, game, [body])
    root = data / "mod-manager"
    os.replace(root / "content-files", root / "content-files.old")
    (root / "content-files.new").mkdir()
    cc.recover(root)
    assert (root / "content-files" / "files.ini").is_file() and not (root / "content-files.new").exists()


# -- with a language pack (docs/language-packs.md): the same packs once more on top of its translation -------------

def language_pack(game, data, tmp_path, pack_id="xx-test", names=((0x11110001, "Blanco"), (0x11110002, "Negro")),
                  original=None):
    """A language pack made by scripts/langpack.py from a translated copy of the fixture: the colour names in
    misc.viv's name tables. ``original``: the dump the pack is made against (another than ``game``: a pack for a
    different dump)."""
    lp = cc.langpack()
    translated = tmp_path / ("translated-" + pack_id)
    shutil.copytree(original or game, translated)
    table = loc.build(list(names))
    (translated / "assets" / "misc.viv").write_bytes(viv.build_archive(
        [("cmusen.loc", viv.refpack_store(table)), ("cmuken.loc", viv.refpack_store(table))], magic=b"BIG4",
        align=128))
    folder = data / "mods" / pack_id
    assert lp.main(["make", str(original or game), str(translated), "--out", str(folder)]) == 0
    return folder


def clothing_pack(game, data, pack_id="hoodies", order=1):
    item = item_op("Brick City Hoody", "Z01", clothing_floor(game))
    return make_pack(data, pack_id, "clothing", [
        ("assets/v2cp_1.viv", "V2CP_114_0005_Z01_01.xsh", "add", "assets/v2cp_1.viv", "V2CP_114_0005_A01_01.xsh",
         TEX_A, TEX_A[:50] + b"gold" * 60)], [item], order=order)


def names_in(path):
    return loc.Loc(viv.Archive(path).read("cmusen.loc"))


def test_language_variant_keeps_the_translation_and_the_packs(game, data, tmp_path):
    pack = clothing_pack(game, data)
    language_pack(game, data, tmp_path)
    result = compose(data, game, [pack])
    assert result["applied"] == ["hoodies"] and result["lang_skipped"] == []
    assert result["lang_files"] == [("xx-test", "assets/misc.viv")]       # the one file both change
    assert not out(data, "lang/xx-test/assets/djv2.sod").exists()
    text = out(data, "files.ini").read_text(encoding="utf-8").splitlines()
    assert "lang=xx-test|assets/misc.viv" in text and not any(t.startswith("language-skipped") for t in text)
    assert files_ini(data)["lang"] == [("xx-test", "assets/misc.viv")]
    plain, variant = names_in(out(data, "assets/misc.viv")), names_in(out(data, "lang/xx-test/assets/misc.viv"))
    texts = lambda table: {table.text_at(i) for i in range(table.count)}
    assert plain.get(0x11110001) == "White" and "Brick City Hoody" in texts(plain)
    assert variant.get(0x11110001) == "Blanco" and "Brick City Hoody" in texts(variant)
    # the translation's entries are what the game's own rebuild makes (langpack.py is its tested reference)
    reference = viv.Archive(cc.langpack().apply_pack(str(data / "mods" / "xx-test"), str(game))["assets/misc.viv"])
    assert loc.Loc(reference.read("cmusen.loc")).get(0x11110002) == "Negro" == variant.get(0x11110002)
    # the same pack's row ids: the variant's database is the plain one (not written twice)
    db = sod.Sod(out(data, "assets/djv2.sod").read_bytes())
    assert [r for r in db.clothing() if r.name.startswith("FFNY_")]


def test_language_variant_is_deterministic_and_tracked(game, data, tmp_path):
    pack = clothing_pack(game, data)
    language_pack(game, data, tmp_path)
    compose(data, game, [pack])
    first = out(data, "lang/xx-test/assets/misc.viv").read_bytes()
    # another PC: the same packs and language pack, a different data folder
    other = tmp_path / "other"
    (other / "mods").mkdir(parents=True)
    shutil.copytree(data / "mods" / "hoodies", other / "mods" / "hoodies")
    shutil.copytree(data / "mods" / "xx-test", other / "mods" / "xx-test")
    again = compose(other, game, [dict(pack, folder=other / "mods" / "hoodies")])
    assert again["lang_files"] == [("xx-test", "assets/misc.viv")]
    assert (other / "mod-manager" / "content-files" / "lang" / "xx-test" / "assets" / "misc.viv").read_bytes() == first
    assert (other / "mod-manager" / "content-files" / "assets" / "misc.viv").read_bytes() == out(data, "assets/misc.viv").read_bytes()
    assert files_ini(other)["set"] == files_ini(data)["set"]           # the online print's set is the packs alone
    # unchanged: the same inputs; changed: the language pack's deltas, or the pack gone
    assert compose(data, game, [pack])["unchanged"]
    ini = data / "mods" / "xx-test" / "language.ini"
    ini.write_text(ini.read_text(encoding="utf-8") + "\n; a new line\n", encoding="utf-8")
    assert not compose(data, game, [pack])["unchanged"]
    out(data, "lang/xx-test/assets/misc.viv").unlink()
    assert not compose(data, game, [pack])["unchanged"] and out(data, "lang/xx-test/assets/misc.viv").is_file()
    shutil.rmtree(data / "mods" / "xx-test")
    result = compose(data, game, [pack])
    assert not result["unchanged"] and result["lang_files"] == [] and not out(data, "lang").exists()
    assert "lang=" not in out(data, "files.ini").read_text(encoding="utf-8")


def test_language_pack_without_overlap_off_or_for_another_dump(game, data, tmp_path):
    body = body_pack(data, "body-one", BODY_O + b"mine")
    language_pack(game, data, tmp_path)
    result = compose(data, game, [body])         # the language pack changes misc.viv, the body pack v2ip.viv
    assert result["applied"] == ["body-one"] and result["lang_files"] == [] and result["lang_skipped"] == []
    assert not out(data, "lang").exists()
    # switched off on the Content page: not composed with
    pack = clothing_pack(game, data, order=2)
    assert compose(data, game, [body, pack], off=["xx-test"])["lang_files"] == []
    assert compose(data, game, [body, pack])["lang_files"] == [("xx-test", "assets/misc.viv")]
    # a pack made for another dump does not rebuild here: left out, reported, the plain files stay
    other = tmp_path / "other-dump"
    shutil.copytree(game, other)
    table = loc.build([(0x11110001, "Weiss"), (0x11110002, "Schwarz")])
    (other / "assets" / "misc.viv").write_bytes(viv.build_archive(
        [("cmusen.loc", viv.refpack_store(table)), ("cmuken.loc", viv.refpack_store(table))], magic=b"BIG4",
        align=128))
    language_pack(game, data, tmp_path, pack_id="yy-other", original=other)
    result = compose(data, game, [body, pack])
    assert result["lang_files"] == [("xx-test", "assets/misc.viv")]
    assert [i for i, _ in result["lang_skipped"]] == ["yy-other"]
    assert "language-skipped=yy-other: " in out(data, "files.ini").read_text(encoding="utf-8")
    assert not out(data, "lang/yy-other").exists() and out(data, "assets/misc.viv").is_file()


def test_install_json_shape_is_checked(data):
    pack = body_pack(data, "body-one", BODY_O + b"x")
    info = json.loads((pack["folder"] / "install.json").read_text())
    cc.check_install(info)
    for bad in ({**info, "schema": 2}, {**info, "type": "language"},
                {**info, "ops": [{"op": "new-venue"}]},
                {**info, "entries": [{**info["entries"][0], "archive": "../x.viv"}]},
                {**info, "entries": [{**info["entries"][0], "mode": "delete"}]},
                {**info, "entries": info["entries"] * 2}):
        with pytest.raises(cc.ComposeError):
            cc.check_install(bad)


# -- packs of several mods ------------------------------------------------------------------------------------

def bundled(pack, bundle="winter-set", name="Winter set"):
    pack.update(bundle=bundle, bundle_name=name)
    return pack


def test_a_pack_of_several_mods_applies_all_or_none(game, data):
    single = body_pack(data, "body-one", BODY_O + b"single", order=1)
    good = bundled(make_pack(data, "set-hood", "clothing", [(
        "assets/v2cp_1.viv", "V2CP_114_0005_W01_01.xsh", "add", "assets/v2cp_1.viv", "V2CP_114_0005_A01_01.xsh",
        TEX_A, TEX_A + b"set")], order=2))
    stale = bundled(make_pack(data, "set-body", "fighter-body", [(
        "assets/v2ip.viv", "V2IP_109A.xsh", "replace", "assets/v2ip.viv", "V2IP_109A.xsh", BODY_XSH + b"old",
        BODY_XSH)], order=3))
    result = compose(data, game, [single, good, stale])
    assert result["applied"] == ["body-one"]
    reasons = dict(result["skipped"])
    assert [i for i, _ in result["skipped"]] == ["set-hood", "set-body"]       # catalogue order
    assert reasons["set-hood"] == "another mod of the pack Winter set could not be applied"
    assert "not the version" in reasons["set-body"]
    # nothing of the pack is in the files: its good mod's archive is not even written
    assert result["files"] == ["assets/v2ip.viv"] and not out(data, "assets/v2cp_1.viv").exists()
    assert viv.Archive(out(data, "assets/v2ip.viv")).read("V2IP_109A.o") == BODY_O + b"single"
    ini = files_ini(data)
    assert ini["applied"] == "body-one" and ini["skipped"].startswith("set-hood: another mod of the pack Winter set")
    # unchanged inputs: the same answer from the state file
    again = compose(data, game, [single, good, stale])
    assert again["unchanged"] and dict(again["skipped"]) == reasons
    # the whole pack applies once its mods all do
    fixed = bundled(make_pack(data, "set-body2", "fighter-body", [(
        "assets/v2ip.viv", "V2IP_109A.xsh", "replace", "assets/v2ip.viv", "V2IP_109A.xsh", BODY_XSH,
        BODY_XSH + b"new")], order=3))
    result = compose(data, game, [single, good, fixed])
    assert result["applied"] == ["body-one", "set-hood", "set-body2"] and result["skipped"] == []
    assert viv.Archive(out(data, "assets/v2cp_1.viv")).read("V2CP_114_0005_W01_01.xsh") == TEX_A + b"set"


def test_a_dropped_pack_frees_what_it_would_have_clashed_with(game, data):
    """The pack's mods come out of every round: a later single that clashed only with the pack's good mod
    applies, as if the pack were not installed."""
    entry = ("assets/v2cp_1.viv", "V2CP_114_0005_W01_01.xsh", "add", "assets/v2cp_1.viv",
             "V2CP_114_0005_A01_01.xsh", TEX_A, TEX_A + b"pack")
    good = bundled(make_pack(data, "set-hood", "clothing", [entry], order=1))
    stale = bundled(make_pack(data, "set-body", "fighter-body", [(
        "assets/v2ip.viv", "V2IP_109A.o", "replace", "assets/v2ip.viv", "V2IP_109A.o", BODY_O + b"old", BODY_O)],
        order=2))
    later = make_pack(data, "later-hood", "clothing", [entry[:6] + (TEX_A + b"single",)], order=3)
    result = compose(data, game, [good, stale, later])
    assert result["applied"] == ["later-hood"]
    assert viv.Archive(out(data, "assets/v2cp_1.viv")).read("V2CP_114_0005_W01_01.xsh") == TEX_A + b"single"
    assert set(dict(result["skipped"])) == {"set-hood", "set-body"}


def test_singles_compose_as_before_beside_packs(game, data):
    """Two singles where one fails: the same applied/skipped as without bundles (no second round)."""
    first = body_pack(data, "body-one", BODY_O + b"one", order=1)
    stale = make_pack(data, "stale", "fighter-body", [("assets/v2ip.viv", "V2IP_109A.o", "replace",
                                                       "assets/v2ip.viv", "V2IP_109A.o", BODY_O + b"old", BODY_O)],
                      order=2)
    result = compose(data, game, [first, stale])
    assert result["applied"] == ["body-one"] and [i for i, _ in result["skipped"]] == ["stale"]
    assert "another mod" not in dict(result["skipped"])["stale"]
