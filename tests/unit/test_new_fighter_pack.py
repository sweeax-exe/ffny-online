"""New-fighter packs on the player's PC (setup/content_compose.py with setup/ffnycontent/newfighter.py): the op
adds the Character row, the entity, the IntroExtro entry, the camera banks and the menu pictures; the place
(front-end ID) comes from the signed catalogue (``fighter_fe_id``), so two players with the same packs build the
same files; a synced pack without a place is skipped. Needs the user's own dump ($DEFJAM_DATA/extracted or
game/); skipped without it."""
import hashlib
import json
import os
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))

import content_compose  # noqa: E402
import mod_catalogue  # noqa: E402
from ffnycontent import delta, newfighter as nf, viv  # noqa: E402


def _game():
    for base in (os.environ.get("DEFJAM_DATA"), str(ROOT / "game" / "..")):
        if base and (Path(base) / "extracted" / "assets" / "djv2.sod").is_file():
            return Path(base) / "extracted"
    if (ROOT / "game" / "assets" / "djv2.sod").is_file():
        return ROOT / "game"
    return None


GAME = _game()
pytestmark = pytest.mark.skipif(GAME is None, reason="needs the user's own dump")
TEMPLATE, TEMPLATE_MODEL = 55, "V2IP_109A"


def make_pack(folder, model, name, fe_id=None, gameplay=None, results=None):
    """A new-fighter pack as FFNY Studio writes it: the template's model files under the new name (symbols
    renamed) as add entries, and the op. ``results`` {suffix: bytes}: the maker's own files (painted textures in the
    .xsh, an imported body), as ``Project.build_new_fighter`` passes them."""
    folder = Path(folder)
    (folder / "deltas").mkdir(parents=True)
    arc = viv.Archive(str(GAME / "assets" / "v2ip.viv"))
    originals = {s: arc.read(TEMPLATE_MODEL + s) for s in nf.MODEL_SUFFIXES if arc.has(TEMPLATE_MODEL + s)}
    entries = []
    for entry, base_entry, base, result in nf.model_entries(TEMPLATE_MODEL, model, originals, results):
        rel = "deltas/%s.ffdelta" % entry
        (folder / rel).write_bytes(delta.encode(base, result))
        entries.append({"archive": nf.MODEL_ARCHIVE, "entry": entry, "mode": "add", "base_archive": nf.MODEL_ARCHIVE,
                        "base_entry": base_entry, "delta": rel,
                        "original_sha256": hashlib.sha256(base).hexdigest(),
                        "result_sha256": hashlib.sha256(result).hexdigest(), "result_size": len(result)})
    info = {"schema": 1, "type": "new-fighter", "entries": entries,
            "ops": [nf.make_op(TEMPLATE, TEMPLATE_MODEL, model, name, fe_id, gameplay=gameplay)]}
    (folder / "install.json").write_text(json.dumps(info), encoding="utf-8")
    return folder


def record(folder, pack_id, order, place=None):
    return {"id": pack_id, "version": "1.0.0", "content_sha256": "0" * 64, "kind": "content", "order": order,
            "folder": folder, "fighter_fe_id": place or 0}


def composed(data, rel):
    return Path(data) / "mod-manager" / "content-files" / rel


def test_new_fighter_is_composed_from_the_catalogue_place(tmp_path):
    pack = make_pack(tmp_path / "packs" / "doc-two", "V2IP_7K3A", "Doc Two")
    data = tmp_path / "data"
    got = content_compose.compose(data, GAME, [record(pack, "doc-two", 1, 101)], off=(), log=lambda t: None,
                                  languages=[])
    assert got["applied"] == ["doc-two"] and not got["skipped"]
    rows = nf.characters(composed(data, nf.SOD_PATH).read_bytes())
    new = rows[-1]
    assert (new["fe_id"], new["display"], new["model"], new["core_id"]) == (101, "Doc Two", "V2IP_7K3A", 10)
    v2ip = viv.Archive(str(composed(data, nf.MODEL_ARCHIVE)))
    assert all(v2ip.has("V2IP_7K3A" + s) for s in (".o", ".xsh"))
    assert b"V2IP_109A" not in v2ip.read("V2IP_7K3A.o") and b"V2IP_7K3A" in v2ip.read("V2IP_7K3A.o")
    v2ta = viv.Archive(str(composed(data, nf.BANK_ARCHIVE)))
    assert v2ta.has("CE_7K3A.viv") and v2ta.has("CI_7K3A.viv")
    for bank in ("CE_7K3A.viv", "CI_7K3A.viv"):          # the retail layout: 128-byte entries, the L231 trailer
        inner = viv.Archive(v2ta.read(bank))
        assert all(e.offset % 128 == 0 for e in inner.entries)
        assert v2ta.read(bank)[inner.header_size - 8:inner.header_size] == b"L231\x00\x00\x00\x02"
    nis = viv.Archive(str(composed(data, nf.NIS_ARCHIVE)))
    entry = next(n for n in nis.names() if n.lower() == nf.INTRO_EXTRO.lower())
    assert b"V2IP_7K3A" in nis.read(entry) and b"CI_7K3A" in nis.read(entry)
    assert b"V2IP_7K3A" in composed(data, nf.ENTITY_XML).read_bytes()
    screens = viv.Archive(str(composed(data, nf.SCREENS_ARCHIVE)))
    assert screens.has("screens/portrait/p_CF/p_cf_102_2p.big")     # p_ families are feID + 1


def test_new_fighter_gameplay_is_written_into_its_row(tmp_path):
    """The op's gameplay block lands in the row's words 3 and 11 (what the game applies), its bars and its style
    bits; the composed row reads back as the same block."""
    g = {"version": 1, "stats": dict(zip(nf.STAT_KEYS, (12, 9, 4, 6, 15, 0, 4))), "styles": ["W", "B"],
         "blazins": [74, 44, 16, 65]}
    pack = make_pack(tmp_path / "packs" / "doc-gp", "V2IP_7G1A", "Doc Gp", gameplay=g)
    data = tmp_path / "data"
    got = content_compose.compose(data, GAME, [record(pack, "doc-gp", 1, 100)], off=(), log=lambda t: None,
                                  languages=[])
    assert got["applied"] == ["doc-gp"]
    new = nf.characters(composed(data, nf.SOD_PATH).read_bytes())[-1]
    assert new["fe_id"] == 100 and new["core_id"] == 10
    assert nf.gameplay_from_words(new["words"]) == g
    assert [new["words"][w] for w in nf.BAR_WORDS] == [nf.stat_bar(v) for v in (12, 9, 4, 6, 15, 4)]
    assert new["words"][nf.W_STYLE_BITS] == (1 << nf.STYLE_BITS["W"]) | (1 << nf.STYLE_BITS["B"])
    # a block out of range is refused at install as the server refuses it at submit
    bad = make_pack(tmp_path / "packs" / "doc-bad", "V2IP_7G2A", "Doc Bad",
                    gameplay=dict(g, stats=dict(g["stats"], health=0)))
    got = content_compose.compose(tmp_path / "data2", GAME, [record(bad, "doc-bad", 1, 100)], off=(),
                                  log=lambda t: None, languages=[])
    assert got["applied"] == [] and got["skipped"]


def test_new_fighter_at_a_low_place(tmp_path):
    """Build 85: a place below 98 (newfighter.LOW_PLACES). Place 37 has an unused Crow 2's pictures in the retail
    screens (thumb37, thumb37off, l_MS_37_*, 38_2p/3p): the new fighter's copies replace them; place 5 has none."""
    pack = make_pack(tmp_path / "packs" / "doc-low", "V2IP_7L5A", "Doc Low")
    other = make_pack(tmp_path / "packs" / "doc-five", "V2IP_7L6A", "Doc Five")
    data = tmp_path / "data"
    got = content_compose.compose(data, GAME, [record(pack, "doc-low", 1, 37), record(other, "doc-five", 2, 5)],
                                  off=(), log=lambda t: None, languages=[])
    assert got["applied"] == ["doc-low", "doc-five"] and not got["skipped"]
    rows = nf.characters(composed(data, nf.SOD_PATH).read_bytes())
    assert [(r["fe_id"], r["display"], r["core_id"], r["main"]) for r in rows[-2:]] == [(37, "Doc Low", 10, 1),
                                                                                      (5, "Doc Five", 10, 1)]
    assert sorted(r["fe_id"] for r in rows) == sorted([r["fe_id"] for r in rows[:-2]] + [37, 5])
    stock = viv.Archive(str(GAME / nf.SCREENS_ARCHIVE))
    screens = viv.Archive(str(composed(data, nf.SCREENS_ARCHIVE)))
    for name in ("screens/thumbs/thumb37.big", "screens/thumbs/thumb37off.big", "screens/portrait/38_2p.big",
                 "screens/portrait/l_MS_37_2p.big"):
        assert stock.has(name) and screens.has(name)
        assert screens.read(name) != stock.read(name), name      # the new fighter's, not Crow 2's
        inner = viv.Archive(viv.unwrap(screens.read(name)))
        assert all(e.name.lower().startswith(name.rsplit("/", 1)[1][:-4].lower()) for e in inner.entries)
    assert screens.has("screens/thumbs/thumb5.big") and screens.has("screens/portrait/p_cf/p_cf_6_2p.big")
    assert not stock.has("screens/thumbs/thumb5.big")


def test_a_place_outside_the_list_is_refused(tmp_path):
    for place in (98, 99, 40, 128, 4):
        pack = make_pack(tmp_path / "packs" / ("p%d" % place), "V2IP_7P%dA" % (place % 10), "Doc P")
        got = content_compose.compose(tmp_path / ("data%d" % place), GAME, [record(pack, "p", 1, place)], off=(),
                                      log=lambda t: None, languages=[])
        assert got["applied"] == [] and got["skipped"], place


def test_same_packs_same_files_whatever_the_order_of_places(tmp_path):
    a = make_pack(tmp_path / "packs" / "doc-two", "V2IP_7K3A", "Doc Two")
    b = make_pack(tmp_path / "packs" / "blaze-x", "V2IP_7Q1A", "Doc Three")
    digests = []
    for player in ("one", "two"):
        data = tmp_path / player
        got = content_compose.compose(data, GAME, [record(a, "doc-two", 1, 104), record(b, "blaze-x", 2, 100)],
                                      off=(), log=lambda t: None, languages=[])
        assert got["applied"] == ["doc-two", "blaze-x"]
        sod = composed(data, nf.SOD_PATH).read_bytes()
        assert [r["fe_id"] for r in nf.characters(sod)[-2:]] == [104, 100]
        digests.append({rel: hashlib.sha256(composed(data, rel).read_bytes()).hexdigest() for rel in got["files"]})
    assert digests[0] == digests[1]


def test_synced_pack_without_a_place_or_taken_place_is_skipped(tmp_path):
    a = make_pack(tmp_path / "packs" / "a", "V2IP_7K3A", "Doc Two")
    b = make_pack(tmp_path / "packs" / "b", "V2IP_7Q1A", "Doc Three")
    c = make_pack(tmp_path / "packs" / "c", "V2IP_7Z9A", "Doc Four", fe_id=127)     # installed by hand
    got = content_compose.compose(tmp_path / "data", GAME,
                                  [record(a, "a", 1, 100), record(b, "b", 2, 100), record(c, "c", 3)],
                                  off=(), log=lambda t: None, languages=[])
    assert got["applied"] == ["a", "c"]
    assert got["skipped"][0][0] == "b" and "100" in got["skipped"][0][1]
    nope = make_pack(tmp_path / "packs" / "d", "V2IP_7AAA", "Doc Five")
    got = content_compose.compose(tmp_path / "data2", GAME, [record(nope, "d", 1)], off=(), log=lambda t: None,
                                  languages=[])
    assert got["applied"] == [] and "no place" in got["skipped"][0][1]


def test_install_json_rules():
    op = nf.make_op(TEMPLATE, TEMPLATE_MODEL, "V2IP_7K3A", "Doc Two")
    content_compose.check_install({"schema": 1, "type": "new-fighter", "entries": [], "ops": [op]})
    for bad in ([], [op, op], [dict(op, name="Doctor Octopus")], [dict(op, template_fe_id=81)],
                [dict(op, model="V2IP_109A")], [{"op": "new-venue"}]):
        with pytest.raises(content_compose.ComposeError):
            content_compose.check_install({"schema": 1, "type": "new-fighter", "entries": [], "ops": bad})


def test_catalogue_place_field():
    base = {"id": "doc-two", "name": "Doc Two", "description": "x", "authors": ["a"], "class": "original",
            "licence": "x", "homepage": "https://play.defjam.online", "revoked_versions": [],
            "versions": [{"version": "1.0.0", "channel": "stable", "min_recomp": "0.1.0", "capabilities": [],
                          "reviewed": "2026-11-12", "depends": [], "conflicts": [], "load_after": [],
                          "archive": {"urls": ["https://play.defjam.online/x.zip"], "size": 1, "unpacked_size": 1,
                                      "file_count": 1, "sha256": "0" * 64, "root": "doc-two"}}],
            "kind": "content", "type": "new-fighter", "order": 3, "fighter_fe_id": 100}
    mod_catalogue.validate_mod(dict(base))
    for good in (5, 37, 97, 127):
        mod_catalogue.validate_mod(dict(base, fighter_fe_id=good))
    for bad in (dict(base, fighter_fe_id=128), dict(base, fighter_fe_id="100"), dict(base, type="venue"),
                dict(base, fighter_fe_id=98), dict(base, fighter_fe_id=40), dict(base, fighter_fe_id=4),
                {k: v for k, v in base.items() if k != "fighter_fe_id"}):
        with pytest.raises(ValueError):
            mod_catalogue.validate_mod(bad)
    with pytest.raises(ValueError):
        mod_catalogue.build([dict(base), dict(base, id="other")], 1, "2026-11-12T00:00:00Z", "2026-12-12T00:00:00Z")


def test_names():
    assert nf.name_problems("Doc Two") == []
    assert "at most 11" in nf.name_problems("Doctor Octopus")[0]
    assert nf.name_problems("doc") and nf.name_problems("") and nf.name_problems(" Doc 2")
    assert nf.NEW_MODEL.match(nf.model_for("studio-doc-two"))


def test_own_fighter_beside_a_community_one_plays_by_its_own_number(tmp_path):
    """A player's own new fighter (FFNY Studio's "Use in my game") whose old place a community fighter has (jackboy66,
    2026-11-15): from build 98 it plays by its own number through the banks, beside the community fighter at 117."""
    from ffnycontent import unlimited
    community = make_pack(tmp_path / "packs" / "studio-naruto", "V2IP_7N1A", "Naruto")
    local = make_pack(tmp_path / "packs" / "my-studio-naruto", "V2IP_7N2A", "Naruto Two")
    data = tmp_path / "data"
    own = dict(record(local, "my-studio-naruto", 1 << 20, 117), local=True)
    got = content_compose.compose(data, GAME, [record(community, "studio-naruto", 1, 117), own], off=(),
                                  log=lambda t: None, languages=[], reserved={117})
    assert got["applied"] == ["studio-naruto", "my-studio-naruto"] and not got["skipped"]
    rows = nf.characters(composed(data, nf.SOD_PATH).read_bytes())
    fes = [r["fe_id"] for r in rows]
    assert len(fes) == len(set(fes)) and 117 in fes                   # never two fighters on one feID
    assert own["fighter_fe_id"] == unlimited.own_number("my-studio-naruto")
    side = unlimited.read_side(composed(data, unlimited.SIDE_FILE).read_bytes(),
                               composed(data, nf.SOD_PATH).read_bytes())
    assert [(f["id"], f["name"]) for f in side["fighters"]] == [(own["fighter_fe_id"], "Naruto Two")]


def test_sixty_own_fighters_with_live_community_ones(tmp_path):
    """Build 98: 60 own new fighters (more than the 48 places) compose beside community fighters at places and by
    number; every one is in the side file under its own number, none is left out."""
    from ffnycontent import unlimited
    data = tmp_path / "data"
    packs = [record(make_pack(tmp_path / "packs" / "c-place", "V2IP_7C1A", "Com Place"), "c-place", 1, 101),
             record(make_pack(tmp_path / "packs" / "c-num", "V2IP_7C2A", "Com Num"), "c-num", 2, 1000),
             record(make_pack(tmp_path / "packs" / "c-num2", "V2IP_7C3A", "Com Num2"), "c-num2", 3, 1001)]
    alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    own = []
    for i in range(60):
        folder = make_pack(tmp_path / "packs" / ("my-own-%02d" % i), "V2IP_7%s%sA" % ("XYZ"[i // 36], alphabet[i % 36]),
                           "Own %d" % i)
        own.append(dict(record(folder, "my-own-%02d" % i, (1 << 20) + i), local=True))
    got = content_compose.compose(data, GAME, packs + own, off=(), log=lambda t: None, languages=[],
                                  reserved={101, 1000, 1001, 1002})
    assert not got["skipped"] and len(got["applied"]) == 63
    numbers = [p["fighter_fe_id"] for p in own]
    assert all(unlimited.is_own_number(n) for n in numbers) and len(set(numbers)) == 60
    side = unlimited.read_side(composed(data, unlimited.SIDE_FILE).read_bytes(),
                               composed(data, nf.SOD_PATH).read_bytes())
    assert sorted(f["id"] for f in side["fighters"]) == sorted([1000, 1001] + numbers)
    assert 101 in [r["fe_id"] for r in nf.characters(composed(data, nf.SOD_PATH).read_bytes())]
    assert content_compose.not_in_game(data / "mod-manager") == {}
    # the next launch: the same numbers, nothing composed again
    again = [dict(p) for p in own]
    got = content_compose.compose(data, GAME, packs + again, off=(), log=lambda t: None, languages=[],
                                  reserved={101, 1000, 1001, 1002})
    assert got["unchanged"] and [p["fighter_fe_id"] for p in again] == numbers


# -- build 85: skins (a new-fighter pack the team accepted as an extra outfit of its template) ----------------------

def skin_record(folder, pack_id, order, skin_of=TEMPLATE):
    return dict(record(folder, pack_id, order), fighter_mode="skin", skin_of=skin_of)


def test_skin_is_composed_without_a_row_or_a_place(tmp_path):
    pack = make_pack(tmp_path / "packs" / "doc-red", "V2IP_7R1A", "Red Doc")
    other = make_pack(tmp_path / "packs" / "doc-blue", "V2IP_7B1A", "Blue Doc")
    data = tmp_path / "data"
    got = content_compose.compose(data, GAME, [skin_record(pack, "doc-red", 1), skin_record(other, "doc-blue", 2)],
                                  off=(), log=lambda t: None, languages=[])
    assert got["applied"] == ["doc-red", "doc-blue"] and not got["skipped"]
    assert not composed(data, nf.SOD_PATH).exists()            # no Character row: djv2.sod stays the game's
    assert not composed(data, nf.SCREENS_ARCHIVE).exists()     # and no menu pictures (no select cell)
    v2ip = viv.Archive(str(composed(data, nf.MODEL_ARCHIVE)))
    assert v2ip.has("V2IP_7R1A.o") and v2ip.has("V2IP_7B1A.xsh")
    v2ta = viv.Archive(str(composed(data, nf.BANK_ARCHIVE)))
    assert v2ta.has("CE_7R1A.viv") and v2ta.has("CI_7B1A.viv")
    nis = viv.Archive(str(composed(data, nf.NIS_ARCHIVE)))
    entry = next(n for n in nis.names() if n.lower() == nf.INTRO_EXTRO.lower())
    assert b"V2IP_7R1A" in nis.read(entry) and b"V2IP_7B1A" in nis.read(entry)
    ini = composed(data, "files.ini").read_text(encoding="utf-8")
    # the game numbers a template's skins in this (catalogue) order: 1 Red Doc, 2 Blue Doc
    assert ini.index("skin=doc-red|55|V2IP_7R1A|Red Doc") < ini.index("skin=doc-blue|55|V2IP_7B1A|Blue Doc")


def test_the_same_pack_as_a_fighter_or_as_a_skin(tmp_path):
    """The team decides the mode; the pack is the same bytes either way."""
    pack = make_pack(tmp_path / "packs" / "doc-two", "V2IP_7K3A", "Doc Two")
    as_fighter, as_skin = tmp_path / "f", tmp_path / "s"
    content_compose.compose(as_fighter, GAME, [record(pack, "doc-two", 1, 110)], off=(), log=lambda t: None, languages=[])
    content_compose.compose(as_skin, GAME, [skin_record(pack, "doc-two", 1)], off=(), log=lambda t: None, languages=[])
    assert nf.characters(composed(as_fighter, nf.SOD_PATH).read_bytes())[-1]["fe_id"] == 110
    assert not composed(as_skin, nf.SOD_PATH).exists()
    assert "skin=" not in composed(as_fighter, "files.ini").read_text(encoding="utf-8")
    assert "skin=doc-two|55|" in composed(as_skin, "files.ini").read_text(encoding="utf-8")
    key = lambda p: content_compose.inputs_key(GAME, [p], [])
    assert key(record(pack, "doc-two", 1)) != key(skin_record(pack, "doc-two", 1))


def test_skin_of_another_template_is_skipped(tmp_path):
    pack = make_pack(tmp_path / "packs" / "doc-red", "V2IP_7R1A", "Red Doc")
    got = content_compose.compose(tmp_path / "data", GAME, [skin_record(pack, "doc-red", 1, skin_of=7)], off=(),
                                  log=lambda t: None, languages=[])
    assert got["applied"] == [] and got["skipped"][0][0] == "doc-red"


def test_catalogue_skin_fields():
    base = {"id": "doc-red", "name": "Red Doc", "description": "x", "authors": ["a"], "class": "original",
            "licence": "x", "homepage": "https://play.defjam.online", "revoked_versions": [],
            "versions": [{"version": "1.0.0", "channel": "stable", "min_recomp": "0.1.0", "capabilities": [],
                          "reviewed": "2026-11-15", "depends": [], "conflicts": [], "load_after": [],
                          "archive": {"urls": ["https://play.defjam.online/x.zip"], "size": 1, "unpacked_size": 1,
                                      "file_count": 1, "sha256": "0" * 64, "root": "doc-red"}}],
            "kind": "content", "type": "new-fighter", "order": 3, "fighter_mode": "skin", "skin_of": 7}
    mod_catalogue.validate_mod(dict(base))
    mod_catalogue.validate_mod(dict(base, skin_of=0))          # House is a template too
    for bad in (dict(base, fighter_fe_id=100), dict(base, skin_of=81), dict(base, skin_of="7"),
                dict(base, fighter_mode="fighter"), dict(base, type="venue"),
                {k: v for k, v in base.items() if k != "skin_of"}):
        with pytest.raises(ValueError):
            mod_catalogue.validate_mod(bad)
    # skins hold no place: two of them and a fighter in one catalogue
    fighter = {k: v for k, v in dict(base, id="doc-two", fighter_fe_id=100).items() if k not in ("fighter_mode", "skin_of")}
    mod_catalogue.build([dict(base), dict(base, id="doc-blue"), fighter], 1, "2026-11-15T00:00:00Z",
                        "2026-12-15T00:00:00Z")


# -- skins with the template's look or their own (2026-11-16: "skins show the original, not the edited one") ----------
# The live skins of 2026-11-16 are three kinds of Studio packs: painted textures in the model's own .xsh (Bo 2, Z. Trejo,
# OG Crack, Sticky LS at twice the texture size), an imported model (.o and .xsh of its own, as Kratos or cj v2 would
# be as skins), and the template's files with only the model code renamed (Scorcha, Cyborg Crow: Studio 0.2.0 built
# them while its viewport still showed an earlier texture project's paint). A skin's composed model files are exactly
# the pack's: the first two look like their maker made them, the third like the template (the pack has no look).

def template_files():
    arc = viv.Archive(str(GAME / "assets" / "v2ip.viv"))
    return {s: arc.read(TEMPLATE_MODEL + s) for s in nf.MODEL_SUFFIXES if arc.has(TEMPLATE_MODEL + s)}


def painted(xsh, scale=1):
    """The template's .xsh with its texel bytes changed past the header (``scale`` 2: grown, as a texture painted
    at twice the size makes it), standing in for Studio's shpxwrite.swap_images."""
    head, body = xsh[:4096], bytes(b ^ 0x5A for b in xsh[4096:])
    return head + body * scale


def skin_model(data, model):
    v2ip = viv.Archive(str(composed(data, nf.MODEL_ARCHIVE)))
    return {s: v2ip.read(model + s) for s in nf.MODEL_SUFFIXES if v2ip.has(model + s)}


def expected(model, results=None):
    return {entry[len(model):]: result
            for entry, _, _, result in nf.model_entries(TEMPLATE_MODEL, model, template_files(), results)}


@pytest.mark.parametrize("kind", ["painted", "painted-2x", "imported"])
def test_a_skin_gets_the_packs_own_look(tmp_path, kind):
    tpl = template_files()
    results = {"painted": {".xsh": painted(tpl[".xsh"])},
               "painted-2x": {".xsh": painted(tpl[".xsh"], 2)},
               "imported": {".o": tpl[".o"] + bytes(range(256)) * 64, ".xsh": painted(tpl[".xsh"], 3)}}[kind]
    pack = make_pack(tmp_path / "packs" / "doc-look", "V2IP_7L1A", "Doc Look", results=results)
    data = tmp_path / "data"
    got = content_compose.compose(data, GAME, [skin_record(pack, "doc-look", 1)], off=(), log=lambda t: None,
                                  languages=[])
    assert got["applied"] == ["doc-look"] and not got["skipped"]
    model = skin_model(data, "V2IP_7L1A")
    want = expected("V2IP_7L1A", results)
    assert model == want
    plain = expected("V2IP_7L1A")
    changed = [s for s in results if model[s] != plain[s]]
    assert changed == list(results), "the skin's composed model lost the maker's %s" % kind
    # the model's own texture name, so the template's and the skin's never share one in a fight
    assert b"A7L1" in model[".xsh"] and b"A109" not in model[".xsh"]
    assert "skin=doc-look|55|V2IP_7L1A|Doc Look" in composed(data, "files.ini").read_text(encoding="utf-8")


def test_a_skin_without_a_look_of_its_own_is_the_template_renamed(tmp_path):
    """What studio-scorcha and studio-cyborg-crow are: in the game such a skin shows the template's look."""
    pack = make_pack(tmp_path / "packs" / "doc-plain", "V2IP_7P1A", "Plain Doc")
    data = tmp_path / "data"
    content_compose.compose(data, GAME, [skin_record(pack, "doc-plain", 1)], off=(), log=lambda t: None, languages=[])
    model, tpl = skin_model(data, "V2IP_7P1A"), template_files()
    assert model == expected("V2IP_7P1A")
    for s in (".o", ".xsh"):
        differing = sum(a != b for a, b in zip(model[s], tpl[s]))
        # the renamed model and texture names only: under 1 % (painting one texture changes 90 % of the .xsh)
        assert len(model[s]) == len(tpl[s]) and differing < len(tpl[s]) // 100, (s, differing)


LIVE = os.environ.get("FFNY_SKIN_PACKS")       # a folder of real packs' installed folders (mods/<id>), never committed


@pytest.mark.skipif(not LIVE, reason="FFNY_SKIN_PACKS names no folder of real packs")
def test_real_packs_as_skins_keep_their_files(tmp_path):
    """Real community packs (their installed folders, e.g. a scratch data root's mods/) composed as skins of their
    templates: every added model file is the pack's delta result, byte for byte."""
    folders = sorted(p for p in Path(LIVE).iterdir() if (p / "install.json").is_file())
    for n, folder in enumerate(folders):
        info = json.loads((folder / "install.json").read_text(encoding="utf-8"))
        op = next(o for o in info["ops"] if o["op"] == "new-fighter")
        data = tmp_path / folder.name
        got = content_compose.compose(data, GAME, [skin_record(folder, folder.name, 1, op["template_fe_id"])], off=(),
                                      log=lambda t: None, languages=[])
        assert got["applied"] == [folder.name], got["skipped"]
        v2ip = viv.Archive(str(composed(data, nf.MODEL_ARCHIVE)))
        for e in info["entries"]:
            if e["archive"] == nf.MODEL_ARCHIVE and e["mode"] == "add":
                assert hashlib.sha256(v2ip.read(e["entry"])).hexdigest() == e["result_sha256"], (folder.name, e["entry"])
        assert "skin=%s|%d|%s|" % (folder.name, op["template_fe_id"], op["model"]) in             composed(data, "files.ini").read_text(encoding="utf-8")


def test_a_place_fighters_own_voice_is_composed(tmp_path):
    """Build 86 (merge of the unlimited fighters): a new fighter at a place keeps its own voice (the speech archive's
    entries at 40 + its place index and the row's speech word); only a fighter beyond the places plays its
    template's. The phase-1 change had turned the own-voice step off for place fighters."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import test_fighter_voice as tfv
    from ffnycontent import fightervoice as fv
    sys.path.pop(0)
    files, lines = tfv.own_lines()
    g = {"version": 1, "stats": dict(zip(nf.STAT_KEYS, (12, 9, 4, 6, 15, 0, 4))), "styles": ["W", "B"],
         "blazins": [74, 44, 16, 65], "voice": {"mode": "own", "lines": lines}}
    pack = make_pack(tmp_path / "packs" / "doc-voice", "V2IP_7G3A", "Doc Voice", gameplay=g)
    for rel, raw in files.items():
        (pack / rel).parent.mkdir(parents=True, exist_ok=True)
        (pack / rel).write_bytes(raw)
    data = tmp_path / "data"
    got = content_compose.compose(data, GAME, [record(pack, "doc-voice", 1, 100)], off=(), log=lambda t: None,
                                  languages=[])
    assert got["applied"] == ["doc-voice"], got
    new = nf.characters(composed(data, nf.SOD_PATH).read_bytes())[-1]
    assert new["fe_id"] == 100 and new["words"][nf.W_SPEECH] == fv.voice_number(100)
    speech = viv.Archive(str(composed(data, fv.SPEECH_ARCHIVE)))
    assert all(speech.has(name) for name in fv.voice_entries(fv.voice_number(100)))
