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


def make_pack(folder, model, name, fe_id=None):
    """A new-fighter pack as FFNY Studio writes it: the template's model files under the new name (symbols
    renamed) as add entries, and the op."""
    folder = Path(folder)
    (folder / "deltas").mkdir(parents=True)
    arc = viv.Archive(str(GAME / "assets" / "v2ip.viv"))
    originals = {s: arc.read(TEMPLATE_MODEL + s) for s in nf.MODEL_SUFFIXES if arc.has(TEMPLATE_MODEL + s)}
    entries = []
    for entry, base_entry, base, result in nf.model_entries(TEMPLATE_MODEL, model, originals):
        rel = "deltas/%s.ffdelta" % entry
        (folder / rel).write_bytes(delta.encode(base, result))
        entries.append({"archive": nf.MODEL_ARCHIVE, "entry": entry, "mode": "add", "base_archive": nf.MODEL_ARCHIVE,
                        "base_entry": base_entry, "delta": rel,
                        "original_sha256": hashlib.sha256(base).hexdigest(),
                        "result_sha256": hashlib.sha256(result).hexdigest(), "result_size": len(result)})
    info = {"schema": 1, "type": "new-fighter", "entries": entries,
            "ops": [nf.make_op(TEMPLATE, TEMPLATE_MODEL, model, name, fe_id)]}
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
    nis = viv.Archive(str(composed(data, nf.NIS_ARCHIVE)))
    entry = next(n for n in nis.names() if n.lower() == nf.INTRO_EXTRO.lower())
    assert b"V2IP_7K3A" in nis.read(entry) and b"CI_7K3A" in nis.read(entry)
    assert b"V2IP_7K3A" in composed(data, nf.ENTITY_XML).read_bytes()
    screens = viv.Archive(str(composed(data, nf.SCREENS_ARCHIVE)))
    assert screens.has("screens/portrait/p_CF/p_cf_102_2p.big")     # p_ families are feID + 1


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
    for bad in (dict(base, fighter_fe_id=128), dict(base, fighter_fe_id="100"), dict(base, type="venue"),
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
