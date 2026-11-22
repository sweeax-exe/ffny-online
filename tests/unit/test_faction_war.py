"""Faction War (build 108): D-Mob's Team against Crow's Team. The season rewards' plain part (src/hooks/tower_rewards.c,
compiled natively: the fw-* item ids, their rarity, effects and ribbon) and the wiring the screens need: the hub tile,
the picker, the page, the queue's legs, the service routes the game calls, the bosses' art and the test scenario.
Needs MSVC (`cl` on PATH) for the native part."""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include "tower_rewards.h"

int main(void)
{
    static const char *const ids[] = { "fw-winner-dmob-2026-11", "fw-winner-crow-2027-01", "fw-top-dmob-2026-12",
        "fw-top-crow-2026-11", "fw-winner-season", "fw-winner-blaze-2026-11", "fw-top-dmob-2026-13", "fw-winner-dmob-2026-11x",
        "fw-nope", "tw-uptown", "tw-legend-1-2026-11" };
    char rib[80];
    int i;
    for (i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++) {
        int t, y, m, k = tower_reward_kind(ids[i], &t, &y, &m);
        tower_reward_ribbon(ids[i], rib, (int)sizeof(rib));
        printf("id|%s|%d|%d|%d|%d|%d|%u|%d|%s\n", ids[i], k, t, y, m, tower_reward_rarity(k), tower_reward_effects(k),
               tower_reward_effect_count(k), rib);
    }
    for (i = 0; i < 4; i++) {
        static const int sides[] = { 1, 2, 0, 9 };
        int t = 0, k;
        tower_reward_set_side(sides[i]);
        k = tower_reward_kind("fw-winner-season", &t, NULL, NULL);
        tower_reward_ribbon("fw-winner-season", rib, (int)sizeof(rib));
        printf("side%d|%d|%d|%s\n", sides[i], k, t, rib);
    }
    {   /* a held reward is unaffected by the side */
        int t = 0, k;
        tower_reward_set_side(2);
        k = tower_reward_kind("fw-winner-dmob-2026-11", &t, NULL, NULL);
        printf("held|%d|%d\n", k, t);
    }
    printf("kinds|%d|%d|%d|%d|%d\n", TRW_FW_DMOB, TRW_FW_CROW, TRW_FW_TOP_DMOB, TRW_FW_TOP_CROW, TRW_KINDS);
    return 0;
}
'''


@pytest.fixture(scope='module')
def native():
    if not shutil.which('cl'):
        pytest.skip('MSVC cl not on PATH')
    with tempfile.TemporaryDirectory() as d:
        drv = Path(d) / 'drv.c'
        drv.write_text(DRIVER)
        exe = Path(d) / 'drv.exe'
        subprocess.run(['cl', '/nologo', '/W3', '/I', str(HOOKS), str(drv), str(HOOKS / 'tower_rewards.c'), '/Fe' + str(exe),
                        '/Fo' + d + '\\'], check=True, capture_output=True, cwd=d)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True, encoding='utf-8').stdout
    rows = {}
    for line in out.splitlines():
        p = line.split('|')
        rows[p[1] if p[0] == 'id' else p[0]] = p[2:] if p[0] == 'id' else p[1:]
    return rows


def test_faction_reward_ids(native):
    rows = native
    dmob, crow, top_dmob, top_crow, kinds = (int(v) for v in rows['kinds'])
    assert (dmob, crow, top_dmob, top_crow) == (8, 9, 10, 11) and kinds == 19
    kind = {i: int(r[0]) for i, r in rows.items() if i.startswith(('fw-', 'tw-'))}
    assert kind['fw-winner-dmob-2026-11'] == dmob and kind['fw-winner-crow-2027-01'] == crow
    assert kind['fw-top-dmob-2026-12'] == top_dmob and kind['fw-top-crow-2026-11'] == top_crow
    assert kind['fw-winner-season'] == dmob          # the Locker's locked row shows a winner's frame (no side: D-Mob's)
    for bad in ('fw-winner-blaze-2026-11', 'fw-top-dmob-2026-13', 'fw-winner-dmob-2026-11x', 'fw-nope'):
        assert kind[bad] == 0, bad
    assert rows['fw-winner-crow-2027-01'][1:4] == ['2', '2027', '1']       # the side (2 Crow), the year, the month
    # the tower rewards are unchanged
    assert kind['tw-uptown'] == 3 and kind['tw-legend-1-2026-11'] == 7


def test_faction_reward_rarity_effects_ribbon(native):
    rows = native
    rarity = {i: int(r[4]) for i, r in rows.items() if i.startswith(('fw-', 'tw-'))}
    assert rarity['fw-winner-dmob-2026-11'] == rarity['fw-winner-crow-2027-01'] == 3     # LEGENDARY
    assert rarity['fw-top-dmob-2026-12'] == rarity['fw-top-crow-2026-11'] == 4           # MYTHIC
    count = {i: int(r[6]) for i, r in rows.items() if i.startswith(('fw-', 'tw-'))}
    assert count['fw-winner-dmob-2026-11'] > 2 and count['fw-top-crow-2026-11'] > count['fw-winner-dmob-2026-11']
    assert rows['fw-winner-dmob-2026-11'][7] == "D-MOB'S TEAM WON · NOV 2026"
    assert rows['fw-top-crow-2026-11'][7] == "#1 OF CROW'S TEAM · NOV 2026"
    assert rows['fw-winner-season'][7] == "SEASON WINNER FRAME"     # locked: no "WON"
    # the viewer's side picks the locked row's frame and ribbon
    assert rows['side1'] == ['8', '1', "D-MOB'S TEAM · WINNER FRAME"]
    assert rows['side2'] == ['9', '2', "CROW'S TEAM · WINNER FRAME"]
    assert rows['side0'] == ['8', '1', 'SEASON WINNER FRAME'] and rows['side9'] == ['8', '1', 'SEASON WINNER FRAME']
    assert rows['held'] == ['8', '1']       # a held reward ignores the viewer's side


def read(name):
    return (HOOKS / name).read_text(encoding='utf-8')


def test_locked_row_follows_viewers_side():
    frames = read('tower_frames.inc')
    assert frames.count('tower_reward_set_side(fw_my_side())') == 2         # the frame and the title
    assert 'int fw_my_side()' in read('faction_ui.inc') and 'int fw_my_side();' in read('online_screen.cpp')


def test_hub_tile_and_pages_wired():
    src = read('online_screen.cpp')
    assert '#include "faction_ui.inc"' in src
    assert src.index('#include "hub_tiles.inc"') < src.index('#include "faction_ui.inc"')
    assert 'HUB_FACTION' in src and 'case HUB_FACTION: open_factions(); break;' in src
    assert '(i != HUB_FACTION || faction_shown())' in src           # only while the service runs it
    assert 'FACTIONS, FACTION_PICK };' in src
    assert 'fw_match_tick(room);' in src and 'fw_hub_auto();' in src
    tiles = read('hub_tiles.inc')
    assert '{ HUB_RANKED, HUB_FACTION,' in tiles                    # beside RANKED MATCH
    # the items, subs and descs arrays reach the new id 14 (MY MUSIC, a chip only, is 15 now)
    assert '"TOWERS", "FACTION WAR" };' in src and 'HUB_TOWERS = 13, HUB_FACTION = 14;' in src and 'const int HUB_MUSIC = 15;' in src


def test_service_routes_and_pad():
    ui = read('faction_ui.inc')
    for route in ('/api/factions/live?format=text', '/api/factions/me?format=text', '/api/factions/pick?format=text',
                  '/api/factions/queue?format=text', '/api/factions/room?format=text', '/api/factions/leave?format=text',
                  '/api/factions/board?format=text'):
        assert route in ui, route
    # the picker: left / right, A then the confirm step, B back; the locked side is refused
    assert 'if (pad.left) FU.sel = FW_DMOB;' in ui and 'if (pad.right) FU.sel = FW_CROW;' in ui
    assert 'FU.confirm = true;' in ui and 'You can switch next season.' in ui
    assert 'is closed for new picks' in ui
    # the select moment and the banner, skippable after 0.8 s
    assert '"YOU RIDE WITH "' in ui and 'st > 0.8f && (pad.a || pad.b)' in ui
    # the live numbers count up and pulse; the bar is eased
    assert 'struct FwCounter' in ui and 'pulse_at = now' in ui
    # the queue's legs: the host opens a private room of the mode, the others join it by code on its relay
    assert 's.is_public = 0;' in ui and 'netservers_join_on(at, q.code.c_str()' in ui


def test_boss_art_from_the_players_dump():
    art = (ROOT / 'scripts/online-art.py').read_text(encoding='utf-8')
    assert 'FACTION_RENDERS = [("dmob", 59), ("crow", 58)]' in art
    assert '"screens/portrait/%d_2p.big" % (fid + 1)' in art
    assert re.search(r'^VERSION = 8\b', art, re.M)
    assert 'tex(std::string("factions/") + fw_id(fac) + ".png")' in read('faction_ui.inc')


def test_scenario_registered():
    dj = (ROOT / 'tools/defjam-mcp/dj.py').read_text(encoding='utf-8')
    sc = (ROOT / 'tools/defjam-mcp/djlib/scenarios.py').read_text(encoding='utf-8')
    assert '"factionwar"' in dj and 'def faction_war(' in sc
    assert '"-factions"' in sc and 'RECOMP_NETPLAY": "faction"' in sc
    net = read('netplay.c')
    assert '!strcmp(mode, "faction")' in net and 'strcmp(mode, "tset") && strcmp(mode, "faction")' in net   # the autostart gate
