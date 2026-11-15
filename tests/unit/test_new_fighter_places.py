"""The 48 places of new fighters (build 85): 100..127, then the 20 front-end IDs below 98 that no stock fighter has
and nothing else uses (docs/research/new-fighters.md section 5). Every list of them is the same list, in the same
order (the place index numbers a fighter's own voice and orders the select grid's community row): the game's
src/hooks/new_fighter_places.h, setup/ffnycontent/newfighter.py PLACES, setup/mod_catalogue.py FIGHTER_FE_IDS,
scripts/apt-patch.py, scripts/online-art.py, the harness's rules check, and (with the private server checked out)
the relay's and the account service's NewFighterPlaces. With the user's own dump, the places are also checked
against the database, fighter.xml and the speech archive."""
import importlib.util
import os
import re
import sys
from pathlib import Path

import pytest

from server_src import RELAY

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))

from ffnycontent import fightervoice as fv  # noqa: E402
from ffnycontent import newfighter as nf  # noqa: E402
import mod_catalogue  # noqa: E402

LOW = [5, 15, 29, 37, 39, 42, 49, 53, 63, 66, 79, 86, 87, 88, 89, 90, 94, 95, 96, 97]
PLACES = list(range(100, 128)) + LOW
# the 75 Character rows of the retail database (test_battle_roster.py), the old fighter.xml IDs the hub and the
# relay translate (40 Teck, 41 Santos, 52 O.E, 78 Erick Sermon) and a created fighter's byte
STOCK = [0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 32,
         33, 34, 35, 36, 38, 43, 44, 45, 46, 47, 48, 50, 51, 54, 55, 56, 57, 58, 59, 60, 61, 62, 64, 65, 67, 68, 69, 70,
         71, 72, 73, 74, 75, 76, 77, 80, 81, 82, 83, 84, 85, 91, 92, 93, 98]
OLD_IDS = [40, 41, 52, 78]
CREATED = 99


def ints(text):
    return [int(x) for x in re.findall(r"\d+", text)]


def _module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_the_places():
    assert len(PLACES) == 48 and len(set(PLACES)) == 48 and max(PLACES) == 127      # 128 crashes the load
    assert not set(PLACES) & (set(STOCK) | set(OLD_IDS) | {CREATED})
    # every ID below 128 is a stock row, an old ID, the created fighter's byte or a place
    assert set(range(128)) == set(STOCK) | set(OLD_IDS) | {CREATED} | set(PLACES)
    # the game's own list array holds 128 rows: 75 stock + 48 fit
    assert len(STOCK) + len(PLACES) <= 128


def test_the_game_header():
    hdr = (ROOT / "src/hooks/new_fighter_places.h").read_text(encoding="utf-8")
    m = re.search(r"k_places\[NEW_FIGHTER_MAX\]\s*=\s*\{(.*?)\};", hdr, re.S)
    assert m and ints(m.group(1)) == PLACES
    assert "#define NEW_FIGHTER_MAX 48" in hdr and "#define NEW_FIGHTER_HIGH 28" in hdr


def test_the_python_lists():
    assert list(nf.PLACES) == PLACES and list(nf.LOW_PLACES) == LOW
    assert list(mod_catalogue.FIGHTER_FE_IDS) == PLACES
    assert [nf.place_index(p) for p in PLACES] == list(range(48))
    # the order places are given in (the account service, Studio's test place): 100..123, the low ones, 124..127
    assert list(nf.PLACE_ORDER) == list(range(100, 124)) + LOW + [124, 125, 126, 127]
    assert all(nf.place_index(x) is None for x in STOCK + OLD_IDS + [CREATED, 128, -1, True])
    assert list(_module("apt_patch", ROOT / "scripts/apt-patch.py").CHOOSEF_PLACES) == PLACES
    assert list(_module("online_art", ROOT / "scripts/online-art.py").NEW_FIGHTER_PLACES) == PLACES
    roots = (ROOT / "tools/defjam-mcp/djlib/roots.py").read_text(encoding="utf-8")
    m = re.search(r"NEW_FIGHTER_PLACES = tuple\(range\(100, 128\)\) \+ \(([\d,\s]+)\)", roots)
    assert m and ints(m.group(1)) == LOW
    scen = (ROOT / "tools/defjam-mcp/djlib/scenarios.py").read_text(encoding="utf-8")
    m = re.search(r"blocked\.update\(list\(range\(100, 128\)\) \+ \[([\d,\s]+)\]\)", scen)
    assert m and ints(m.group(1)) == LOW


def test_voice_numbers():
    """A place's own voice is 40 + its index: 100..127 keep 040..067 (packs of builds 79..84), the low places get
    068..087; stock voices are 000..039 and 500..538."""
    assert [fv.voice_number(p) for p in PLACES] == list(range(40, 88))
    assert fv.voice_number(100) == 40 and fv.voice_number(127) == 67 and fv.voice_number(5) == 68
    assert fv.voice_number(97) == 87 and fv.voice_number(55) is None and fv.voice_number(128) is None


def test_the_relay_and_the_account_service():
    if RELAY is None:
        pytest.skip("the relay source (private server repository) is not checked out")
    relay = (RELAY / "protocol_lobby.go").read_text(encoding="utf-8")
    m = re.search(r"var NewFighterPlaces = \[\]uint8\{(.*?)\}", relay, re.S)
    assert m and ints(m.group(1)) == PLACES
    accounts = (RELAY / "accounts/packs_newcontent.go").read_text(encoding="utf-8")
    m = re.search(r"var NewFighterPlaces = \[\]int\{(.*?)\}", accounts, re.S)
    assert m and ints(m.group(1)) == PLACES
    # the order the account service gives them: 100..123, the low places, 124..127 last (kept free for later)
    # the account service builds NewFighterPlaceOrder as newfighter.PLACE_ORDER: 100..123, the low places, 124..127
    assert "NewFighterPlaces[:24]" in accounts and "NewFighterLowPlaces..." in accounts and "NewFighterPlaces[24:28]" in accounts


# -- with the user's own dump ---------------------------------------------------------------------------------------

def _data():
    for base in (os.environ.get("DEFJAM_DATA"), str(ROOT / "game" / "..")):
        if base and (Path(base) / "extracted" / "assets" / "djv2.sod").is_file():
            return Path(base) / "extracted"
    if (ROOT / "game" / "assets" / "djv2.sod").is_file():
        return ROOT / "game"
    return None


DATA = _data()
needs_dump = pytest.mark.skipif(DATA is None, reason="needs the user's own dump")


@needs_dump
def test_no_retail_row_has_a_place():
    rows = nf.characters((DATA / "assets/djv2.sod").read_bytes())
    assert sorted(r["fe_id"] for r in rows) == STOCK
    # a new row fits at a low place and goes in its feID place in the list
    out, _, _ = nf.append_character((DATA / "assets/djv2.sod").read_bytes(), 55, 37, "Doc 2", "Doc 2")
    assert [r["fe_id"] for r in nf.characters(out)][-1] == 37


@needs_dump
def test_fighter_xml_at_the_low_places_is_no_fighter():
    """fighter.xml has an entry for every ID up to 141, but at the low places only cut or placeholder ones
    (model EMPTY or NULL, or a created fighter's archetype); 37 is "Crow2 (NO PLAYABLE)", Crow's second model,
    which only the Story cinematics load by its model name (Story sets up a fight with a Character row's feID or
    99, sub_000C1240)."""
    text = (DATA / "fighter.xml").read_text(encoding="latin-1")
    by_id = {}
    for rec in re.findall(r"<fighter>(.*?)</fighter>", text, re.S):
        fid = int(re.search(r"<ID>\s*(\d+)\s*</ID>", rec).group(1))
        by_id[fid] = {k: v.strip() for k, v in re.findall(r"<(\w+)>([^<]*)</\1>", rec)}
    for p in LOW:
        model = by_id[p].get("model", "").upper()
        if p == 37:
            assert "NO PLAYABLE" in by_id[p].get("dispname", "").upper()
        else:
            assert model in ("EMPTY", "NULL") or not (DATA / "assets" / (model + ".viv")).exists(), p


@needs_dump
def test_speech_archive_has_no_new_voice():
    from ffnycontent import viv
    path = DATA / "audio" / "speech2.viv"
    if not path.is_file():
        pytest.skip("no audio/speech2.viv")
    names = {n.replace("/", "\\").lower() for n in viv.Archive(path.read_bytes()).names()}
    for v in range(40, 88):
        assert not [e for e in fv.voice_entries(v) if e.lower() in names], v
