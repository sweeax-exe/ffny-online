"""Towers: the CPU past Hard (src/hooks/towers_ai_rules.c, compiled natively): the floor ramp of the skill and the
tendencies, plus the gating the game side needs (towers_ai.c is called by towers.c only in a live tower fight
offline, for the CPU slot only; online rooms with CPUs never reach it). Needs MSVC (`cl` on PATH) for the native
part."""
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
#include "towers_ai_rules.h"
#include "towers_rules.h"

int main(void)
{
    int f, k, own;
    for (f = 1; f <= 72; f++) {
        TowerAiFloor a;
        towers_ai_floor(TOWER_UPTOWN, f, 72, &a);
        printf("u|%d|%d|%.4f|%d|%d|%d|%d\n", f, a.on, a.t, a.boss, towers_ai_skill(&a, 65), towers_ai_skill(&a, 20),
               towers_ai_skill(&a, 100));
    }
    for (f = 1; f <= 60; f++) {
        TowerAiFloor a;
        towers_ai_floor(TOWER_FIVE_BOROUGHS, f, 130, &a);
        printf("b|%d|%d|%.4f|%d|%d\n", f, a.on, a.t, a.boss, towers_ai_skill(&a, 65));
    }
    {   /* every tendency from every own value at four points of the ramp */
        static const int floors[] = { 6, 7, 20, 40 };
        int i;
        for (i = 0; i < 4; i++) {
            TowerAiFloor a;
            towers_ai_floor(TOWER_UPTOWN, floors[i], 72, &a);
            for (k = 0; k < TOWER_AI_TENDENCIES; k++)
                for (own = 0; own <= 6; own++)
                    printf("t|%d|%d|%d|%d\n", floors[i], k, own, towers_ai_tendency(&a, k, own));
        }
    }
    printf("defence|%d\n", TOWER_AI_DEFENCE_BEST);
    for (f = 1; f <= 72; f++) {
        TowerAiFloor a;
        towers_ai_floor(TOWER_UPTOWN, f, 72, &a);
        printf("cap|%d|%d\n", f, a.hold_cap);
    }
    {
        TowerAiFloor a;
        towers_ai_floor(TOWER_FIVE_BOROUGHS, 40, 130, &a);
        printf("bosscap|%d\n", a.hold_cap);
    }
    printf("hold|%d|%d|%d|%d|%d|%d|%d\n", towers_ai_hold(0, 40, 5), towers_ai_hold(0, 20, 5), towers_ai_hold(0, 16, 5),
           towers_ai_hold(5, 4, 5), towers_ai_hold(30, 29, 5), towers_ai_hold(0, 40, 0), towers_ai_hold(4, 40, 12));
    return 0;
}
'''


@pytest.fixture(scope='module')
def out():
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = Path(tempfile.mkdtemp(prefix='towersai'))
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'towersai.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(HOOKS), str(folder / 'driver.c'),
                            str(HOOKS / 'towers_ai_rules.c'), str(HOOKS / 'towers_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8').splitlines()


def rows(out, tag):
    return [line.split('|')[1:] for line in out if line.startswith(tag + '|')]


def test_the_ramp_starts_at_floor_7_and_tops_out_at_40(out):
    up = {int(r[0]): r for r in rows(out, 'u')}
    for f in range(1, 7):
        assert up[f][1] == '0' and up[f][4] == '65'          # floors 1..6: the game's own data
    assert up[7][1] == '1' and float(up[7][2]) == 0 and up[7][4] == '65'   # floor 7: on, at the bottom (own Hard)
    skills = [int(up[f][4]) for f in range(7, 72)]
    assert skills == sorted(skills)                           # never easier further up
    assert int(up[20][4]) in range(72, 80)                    # about 40 % of the way from 65 to 100
    assert all(int(up[f][4]) == 100 for f in range(40, 72))   # at the top from floor 40
    assert float(up[40][2]) == pytest.approx(1.0)
    for f in range(7, 72):
        assert int(up[f][5]) >= 20 and int(up[f][4]) >= 65 and up[f][6] == '100'   # never below own, never past 100


def test_bosses_take_the_top_of_their_floor_plus_extra(out):
    up = {int(r[0]): r for r in rows(out, 'u')}
    assert [f for f in up if up[f][3] == '1'] == [72]          # Uptown: the top floor
    b = {int(r[0]): r for r in rows(out, 'b')}
    assert [f for f in b if b[f][3] == '1'] == [10, 20, 30, 40, 50, 60]
    for f in (10, 20, 30):
        plain = (f - 7) / 33
        assert float(b[f][2]) == pytest.approx(min(1.0, plain + 0.25), abs=1e-4)
        assert int(b[f][4]) > int(b[f - 1][4])                 # a boss is harder than the floor before
    assert int(b[10][4]) >= 65 + 10                            # + the boss's skill extra


def test_tendencies_move_toward_their_strongest_values(out):
    t = {(int(r[0]), int(r[1]), int(r[2])): int(r[3]) for r in rows(out, 't')}
    strongest = {8: 5, 23: 5, 15: 5, 20: 5, 10: 5, 11: 5, 12: 5, 22: 5, 18: 5, 19: 1}
    defence_best = int(rows(out, 'defence')[0][0])
    assert defence_best in (1, 5)
    strongest[16] = strongest[17] = defence_best
    for k in range(24):
        for own in range(0, 7):
            assert t[(6, k, own)] == own                        # below floor 7: untouched
            assert t[(7, k, own)] == own                        # floor 7: the bottom of the ramp, own values
            if own < 1 or own > 5:
                assert t[(40, k, own)] == own                   # a value outside 1..5 is never touched
            elif k in strongest:
                assert t[(40, k, own)] == strongest[k]          # the top: the strongest value
                lo, hi = sorted((own, strongest[k]))
                assert lo <= t[(20, k, own)] <= hi              # on the way, between own and the target
            else:
                assert t[(40, k, own)] == own                   # style tendencies stay the fighter's own


def test_the_ai_is_gated_on_a_live_offline_tower_fight_and_the_cpu_slot():
    towers = (HOOKS / 'towers.c').read_text(encoding='utf-8')
    ai = (HOOKS / 'towers_ai.c').read_text(encoding='utf-8')
    # towers.c is the only caller, and its step/match-start hooks return while netplay is active or no fight is on
    step = towers[towers.index('void towers_step(void)'):]
    step = step[:step.index('\n}\n')]
    assert 'if (g_state != TOWERS_FIGHTING || netplay_active() || netplay_rerunning()) return;' in step
    assert step.index('return;') < step.index('towers_ai_step(')
    start = towers[towers.index('void towers_match_start(void)'):]
    start = start[:start.index('\n}\n')]
    assert start.index('if (netplay_active()) return;') < start.index('towers_ai_begin(')
    assert 'g_state == TOWERS_ARMED' in start                # only the floor's own fight
    for other in HOOKS.glob('*.c*'):
        if other.name not in ('towers.c', 'towers_ai.c'):
            text = other.read_text(encoding='utf-8', errors='replace')
            assert not re.search(r'\btowers_ai_(begin|step|end|health)\(', text), other.name
    # only the CPU slot's block, and only while that slot is a CPU and the climber's is not
    assert '#define CPU                 1' in ai
    assert 'if (!g_on || !MEM32(G_CPU_FLAG(CPU)) || MEM32(G_CPU_FLAG(ME))) return;' in ai
    assert re.findall(r'MEM32\(AI_(?:SKILL|TEND)\((\w+)', ai) and set(re.findall(r'MEM32\(AI_(?:SKILL|TEND)\((\w+)', ai)) == {'CPU'}
    # the difficulty word is never 3: the slot words come from towers_floor_rules (0..2)
    assert 'MEM32(G_SLOT_LEVEL + 4u) = (uint32_t)g_rules.cpu_level;' in towers
    assert 'G_SLOT_LEVEL' not in ai and '0x003B90A8' not in ai


def test_no_ai_data_files_are_touched():
    ai = (HOOKS / 'towers_ai.c').read_text(encoding='utf-8')
    assert 'ai.viv' not in ai.replace('ai.viv, so nothing', '') and 'override' not in ai.lower()


def test_the_hold_cap_cuts_only_fresh_long_holds(out):
    caps = {int(r[0]): int(r[1]) for r in rows(out, 'cap')}
    assert all(caps[f] == 0 for f in range(1, 8))             # floors 1..7: nothing is cut
    on = [caps[f] for f in range(8, 72)]
    assert all(c > 0 for c in on) and on == sorted(on, reverse=True)   # tighter further up
    assert caps[20] in range(20, 30) and caps[40] == 5 and caps[71] == 5
    assert caps[72] == 4                                        # the boss: one step less
    assert int(rows(out, 'bosscap')[0][0]) == 4
    # a fresh 40 and a fresh 20 are cut; a fresh 16 (an attack's own hold), a countdown going down and no cap are not
    assert rows(out, 'hold')[0] == ['5', '5', '16', '4', '29', '40', '12']
