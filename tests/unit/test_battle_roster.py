"""The online roster may hold only fighters Battle mode can load: front-end IDs with a Character
record in the game's database (assets/djv2.sod). A fighter.xml ID without one leaves the match slot
empty and crashes the load (docs/research/crash-001B2480.md, section 11): Teck, Santos, O.E and
Eric Sermon under their fighter.xml IDs (builds 13/14), and Gwen (39) as the CPU that filled a free
place of a live Free for All (build 31). The game (src/hooks/online_data.c) and the relay
(netplay/relay/protocol_lobby.go) keep the same list; with the user's dump present it is compared
with the database itself and with what the game would offer from fighter.xml."""
import importlib.util
import os
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
ONLINE_DATA = (ROOT / 'src/hooks/online_data.c').read_text(encoding='utf-8')
RELAY = (ROOT / 'netplay/relay/protocol_lobby.go').read_text(encoding='utf-8')

# The 75 Character records of the reference dump's djv2.sod, by front-end ID (numbers only).
SOD_FE_IDS = [
    0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 30, 31, 32, 33, 34, 35, 36, 38, 43, 44, 45, 46, 47, 48, 50, 51, 54, 55, 56, 57, 58,
    59, 60, 61, 62, 64, 65, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 80, 81, 82, 83, 84, 85,
    91, 92, 93, 98,
]
# fighter.xml ID -> front-end ID for the four whose IDs differ
OLD_IDS = {40: 85, 41: 93, 52: 92, 78: 91}


def ints(text):
    return [int(x) for x in re.findall(r'\d+', text)]


def c_sod_fighters():
    m = re.search(r'k_sod_fighters\[\]\s*=\s*\{(.*?)\};', ONLINE_DATA, re.S)
    assert m
    return ints(m.group(1))


def c_battle_ids():
    m = re.search(r'k_battle_ids\[\]\s*=\s*\{(.*?)\n\};', ONLINE_DATA, re.S)
    assert m
    return {a: b for a, b in re.findall(r'\{\s*(\d+),\s*(\d+)\s*\}', m.group(1))}


def go_valid_fighters():
    m = re.search(r'var ValidFighters = .*?\[\]uint8\{(.*?)\}', RELAY, re.S)
    assert m
    return ints(m.group(1))


def go_battle_ids():
    m = re.search(r'var BattleIDs = map\[uint8\]uint8\{(.*?)\}', RELAY)
    assert m
    return {int(a): int(b) for a, b in re.findall(r'(\d+):\s*(\d+)', m.group(1))}


def test_game_and_relay_hold_the_database_ids():
    assert c_sod_fighters() == SOD_FE_IDS
    assert go_valid_fighters() == SOD_FE_IDS
    assert len(set(SOD_FE_IDS)) == 75


def test_old_ids_translate_to_records_the_same_way_everywhere():
    assert {int(a): int(b) for a, b in c_battle_ids().items()} == OLD_IDS
    assert go_battle_ids() == OLD_IDS
    for old, new in OLD_IDS.items():
        assert old not in SOD_FE_IDS and new in SOD_FE_IDS


def test_gwen_has_no_record():
    assert 39 not in SOD_FE_IDS     # the build-31 Free for All crash (bug report 2026-10-27)


def _sod_module():
    spec = importlib.util.spec_from_file_location('sod_characters', ROOT / 'scripts/sod-characters.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _dump():
    mod = _sod_module()
    return mod, mod.sod_path()


@pytest.mark.skipif(_dump()[1] is None, reason="no game data (set DEFJAM_DATA)")
def test_list_is_the_dumps_character_table():
    mod, path = _dump()
    recs = mod.characters(path.read_bytes())
    assert [fe for fe, _, _ in recs] == SOD_FE_IDS
    core = {fe: c for fe, c, _ in recs}
    assert (core[85], core[93], core[92], core[91]) == (70, 73, 12, 42)    # Teck, Santos, O.E, Eric Sermon


@pytest.mark.skipif(_dump()[1] is None, reason="no game data (set DEFJAM_DATA)")
def test_roster_from_fighter_xml_loads():
    """What online_data.c offers (entries with a fighting style, and Lauren), mapped and filtered as
    there: every one has a record, the four are in, Gwen is out."""
    _, path = _dump()
    xml = (path.parent.parent / 'fighter.xml').read_text(encoding='latin-1')
    offered = []
    for body in re.findall(r'<fighter>(.*?)</fighter>', xml, re.S):
        disp = re.search(r'<dispname>\s*(.*?)\s*<', body).group(1)
        xml_id = int(re.search(r'<ID>\s*(.*?)\s*<', body).group(1))
        style = re.search(r'\((\w{3})\)$', disp)
        if not ((style and style.group(1) in ('STF', 'MAR', 'KIC', 'WRE', 'SUB')) or disp == 'Lauren'):
            continue
        if xml_id in OLD_IDS.values():
            continue
        fe = OLD_IDS.get(xml_id, xml_id)
        if fe in SOD_FE_IDS:
            offered.append(fe)
    assert {85, 91, 92, 93} <= set(offered)
    assert 39 not in offered
    assert len(offered) == len(set(offered)) == 73
