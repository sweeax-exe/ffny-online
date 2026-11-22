"""Towers rewards (build 104): the card frames and titles players earn in the Towers. The plain part
(src/hooks/tower_rewards.c, compiled natively: item ids, rarity, effects, the legend's ribbon, the effects' timing) and
the wiring the game needs: the looks and the Locker read the service's new fields, the Locker lists and equips the
tower items the existing way, the room, watch and hill cards draw the worn frame and title, and the drawing stays
visual only and allocation free. Needs MSVC (`cl` on PATH) for the native part."""
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
    static const char *const ids[] = { "tw-floor25", "tw-floor50", "tw-uptown", "tw-untouchable", "tw-weekly",
        "tw-boroughs", "tw-legend-1-2026-11", "tw-legend-2-2027-01", "tw-legend-month", "tw-legend-3-2026-11",
        "tw-legend-1-2026-13", "tw-legend-1-2026-11x", "s1-classic-3", "", "tw-nope",
        "tw-team-floor25", "tw-team-untouchable", "tw-team-champions", "tw-team-legend-4-2026-11",
        "tw-team-legend-5-2027-02", "tw-team-legend-month", "tw-team-legend-1-2026-11", "tw-team-legend-4-2026-11x",
        "tw-team-floor50", "tw-legend-4-2026-11" };
    char rib[64];
    int i;
    for (i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++) {
        int t, y, m, k = tower_reward_kind(ids[i], &t, &y, &m);
        tower_reward_ribbon(ids[i], rib, (int)sizeof(rib));
        printf("id|%s|%d|%d|%d|%d|%d|%u|%d|%s\n", ids[i], k, t, y, m, tower_reward_rarity(k), tower_reward_effects(k),
               tower_reward_effect_count(k), rib);
    }
    printf("rarity|%s|%s|%s|%s|%s\n", tower_reward_rarity_name(1), tower_reward_rarity_name(2), tower_reward_rarity_name(3),
           tower_reward_rarity_name(4), tower_reward_rarity_name(9));
    for (i = 0; i <= 40; i++) {
        float t = i * 0.1f, x, rise, a;
        tower_fx_ember(i % TRW_EMBERS, t, &x, &rise, &a);
        printf("fx|%.1f|%.4f|%.4f|%.3f|%.3f|%.3f|%.3f|%.3f\n", t, tower_fx_sweep(t, 3.2f), tower_fx_pulse(t, 2.4f),
               tower_fx_flicker(t), x, rise, a, tower_fx_sweep(t, 2.4f));
    }
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
    fx = []
    for line in out.splitlines():
        p = line.split('|')
        if p[0] == 'id':
            rows[p[1]] = p[2:]
        elif p[0] == 'fx':
            fx.append([float(v) for v in p[1:]])
        else:
            rows[p[0]] = p[1:]
    return rows, fx


def test_ids_rarity_ribbon(native):
    rows, _ = native
    kinds = {i: int(r[0]) for i, r in rows.items() if i != 'rarity'}
    assert [kinds[i] for i in ('tw-floor25', 'tw-floor50', 'tw-uptown', 'tw-untouchable', 'tw-weekly', 'tw-boroughs')] == \
        [1, 2, 3, 4, 5, 6]
    assert kinds['tw-legend-1-2026-11'] == kinds['tw-legend-2-2027-01'] == kinds['tw-legend-month'] == 7
    for bad in ('tw-legend-3-2026-11', 'tw-legend-1-2026-13', 'tw-legend-1-2026-11x', 's1-classic-3', '', 'tw-nope'):
        assert kinds[bad] == 0, bad
    assert rows['tw-legend-2-2027-01'][1:4] == ['2', '2027', '1']
    rarity = {i: int(r[4]) for i, r in rows.items() if i != 'rarity'}
    assert rarity['tw-floor25'] == rarity['tw-floor50'] == 1
    assert rarity['tw-uptown'] == rarity['tw-untouchable'] == rarity['tw-weekly'] == 2
    assert rarity['tw-boroughs'] == 3 and rarity['tw-legend-1-2026-11'] == 4
    assert rows['tw-legend-1-2026-11'][7] == '#1 UPTOWN · NOV 2026'
    assert rows['tw-legend-2-2027-01'][7] == '#1 FIVE BOROUGHS · JAN 2027'
    assert rows['tw-uptown'][7] == '' and rows['tw-legend-month'][7] == ''
    assert rows['rarity'] == ['RARE', 'EPIC', 'LEGENDARY', 'MYTHIC', '']


def test_team_rewards(native):
    """Build 108: the Team Tower's own items: their kinds, rarity and effects as their solo looks, the team legend's
    tower (4 or 5) and ribbon; no team floor 50, no solo legend id for a team tower."""
    rows, _ = native
    kinds = {i: int(r[0]) for i, r in rows.items() if i != 'rarity'}
    assert [kinds[i] for i in ('tw-team-floor25', 'tw-team-untouchable', 'tw-team-champions', 'tw-team-legend-4-2026-11')] ==         [8, 9, 10, 11]
    assert kinds['tw-team-legend-5-2027-02'] == kinds['tw-team-legend-month'] == 11
    for bad in ('tw-team-legend-1-2026-11', 'tw-team-legend-4-2026-11x', 'tw-team-floor50', 'tw-legend-4-2026-11'):
        assert kinds[bad] == 0, bad
    rarity = {i: int(r[4]) for i, r in rows.items() if i != 'rarity'}
    assert rarity['tw-team-floor25'] == 1 and rarity['tw-team-untouchable'] == 2
    assert rarity['tw-team-champions'] == 3 and rarity['tw-team-legend-4-2026-11'] == 4
    fx = {i: int(r[5]) for i, r in rows.items() if i != 'rarity'}
    assert fx['tw-team-floor25'] == fx['tw-floor25'] and fx['tw-team-untouchable'] == fx['tw-untouchable']
    assert fx['tw-team-champions'] == fx['tw-boroughs'] and fx['tw-team-legend-4-2026-11'] == fx['tw-legend-1-2026-11']
    assert rows['tw-team-legend-4-2026-11'][1:4] == ['4', '2026', '11']
    assert rows['tw-team-legend-4-2026-11'][7] == '#1 TEAM TOWER · NOV 2026'
    assert rows['tw-team-legend-5-2027-02'][7] == '#1 TEAM TOWER COMMUNITY · FEB 2027'
    assert rows['tw-team-legend-month'][7] == ''
    # the frames draw the solo look with the TAG TEAM plate; the team legend's title; the Locker's sheet shows them
    frames = read('tower_frames.inc')
    assert 'int kind = tower_reward_kind(id, nullptr, nullptr, nullptr), base = tower_reward_base(kind);' in frames
    assert frames.count('if (team) trw_team_plate(') == 2 and '"TAG TEAM LEGEND"' in frames
    assert '"tw-team-champions", "Tag Team Champions"' in read('locker_ui.inc')
    # the server's catalogue has the same ids (relay/accounts/tower_rewards.go)
    srv = ROOT / 'server/relay/accounts/tower_rewards.go'
    if srv.is_file():
        go = srv.read_text(encoding='utf-8')
        for i in ('"tw-team-floor25"', '"tw-team-untouchable"', '"tw-team-champions"', '"tw-team-legend-"'):
            assert i in go, i


def test_rarer_means_more_effects(native):
    rows, _ = native
    by_rarity = {}
    for i, r in rows.items():
        if i == 'rarity' or int(r[0]) == 0:
            continue
        by_rarity.setdefault(int(r[4]), []).append(int(r[6]))
    # every tier shows more than the one below it: the fewest of a tier beat the most of the tier below,
    # except EPIC, whose three frames each have their own main effect (one or two)
    assert max(by_rarity[1]) == 0
    assert min(by_rarity[2]) >= 1
    assert min(by_rarity[3]) > max(by_rarity[2])
    assert min(by_rarity[4]) > max(by_rarity[3])
    fx = {i: int(r[5]) for i, r in rows.items() if i != 'rarity'}
    SWEEP, SHINE, GLINT, GLOW, FLICKER, EMBERS, PULSE = 1, 2, 4, 8, 16, 32, 64
    assert fx['tw-uptown'] == SWEEP | SHINE                     # gold sweep and shine
    assert fx['tw-untouchable'] == GLINT                        # chrome glint
    assert fx['tw-weekly'] == GLOW | FLICKER                    # neon glow and flicker
    assert fx['tw-boroughs'] == PULSE | SWEEP | SHINE           # crown pulse, sweep, shine
    assert fx['tw-legend-1-2026-11'] == EMBERS | PULSE | SWEEP | SHINE


def test_effect_timing(native):
    _, fx = native
    for t, sweep, pulse, flick, x, rise, a, glint in fx:
        assert sweep == -1 or 0 <= sweep <= 1
        assert 0 <= pulse <= 1 and flick in (1.0, 0.35)
        assert 0 <= x <= 1 and 0 <= rise < 1 and 0 <= a <= 1
    assert any(f[1] == -1 for f in fx) and any(0 < f[1] < 1 for f in fx)   # the sweep crosses, then rests
    assert any(f[3] == 0.35 for f in fx)                                    # 0.8 s is a flicker moment
    assert len({round(f[5], 2) for f in fx}) > 10                           # the embers rise


def read(name):
    return (HOOKS / name).read_text(encoding='utf-8')


def test_service_fields_read():
    src = read('social.cpp')
    for key in ('"frame_id"', '"title_id"', '"title_rarity"', 'e["tower"]', 'e["rarity"]'):
        assert key in src, key
    h = read('social.h')
    assert 'std::string frame_id, title_id;' in h and 'bool tower = false;' in h
    # equip goes out the existing way, the item id in the frame or title slot
    assert '"/api/social/locker/equip"' in src


def test_locker_lists_and_equips_tower_items():
    src = read('locker_ui.inc')
    assert 'g = "TOWER REWARDS";' in src and 'h.right = "KEPT FOREVER";' in src
    assert 'if (it.tower) return rarity_col(it.rarity);' in src
    assert 'l.frame_id = it->id' in src and 'l.title_id = it->id' in src      # the preview tries it on
    assert 'tower_card_frame(dl, f, x, vy, w, vh, l.frame_id.c_str()' in src
    # the EQUIP press is the existing one (no tower-only path)
    assert src.count('social::equip(k_slot_ids[LK.slot], selected->id);') == 1


def test_cards_draw_worn_frame_and_title():
    src = read('online_screen.cpp')
    assert '#include "tower_frames.inc"' in src
    assert src.index('#include "rank_art.inc"') < src.index('#include "tower_frames.inc"') < src.index('#include "locker_ui.inc"')
    assert src.count('look_card(dl, f, ') >= 4          # the room card, the two watch screens, the hill's king
    assert src.count('look_card_title(dl, f, ') >= 3
    assert 'card_border(dl, f, x, y, w, 270, 10, lk->frame);' not in src     # the room goes through look_card now


def test_drawing_is_visual_only_and_allocation_free():
    src = read('tower_frames.inc')
    body = re.sub(r'//[^\n]*', '', src)
    for bad in ('std::string', 'std::vector', 'new ', 'malloc', 'push_back', 'netplay_', 'rollback', 'g_rb'):
        assert bad not in body, bad
    c = read('tower_rewards.c')
    assert 'malloc' not in c and 'netplay' not in c
