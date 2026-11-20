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
#include <string.h>
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
    {   /* the fixed bosses */
        static TowerRun r, plain;
        static int pool[200], venues[3] = { 1, 2, 3 };
        int n = 0, i, placed, climbers[2] = { 55, 58 }, c;
        for (i = 0; i <= 85; i++) pool[n++] = i;                 /* stock-like IDs, the story bosses among them */
        for (c = 0; c < 2; c++) {
            towers_new_run(&plain, TOWER_UPTOWN, 4242u, climbers[c], 0, pool, n, venues, 3);
            r = plain;
            placed = towers_place_bosses(&r);
            printf("up|%d|%d|%d|%d|%u|%u|", climbers[c], placed, r.floors, r.roster[r.floors - 1], r.roster_hash,
                   towers_roster_hash(r.roster, r.floors));
            for (i = 0; i < r.floors; i++) printf("%s%d", i ? "," : "", r.roster[i]);
            printf("|");
            for (i = 0; i < plain.floors; i++) printf("%s%d", i ? "," : "", plain.roster[i]);
            printf("\n");
        }
        for (i = 0; i < 60; i++) pool[n++] = 1000 + i;           /* community fighters: 145 floors, 14 chapters */
        towers_new_run(&r, TOWER_FIVE_BOROUGHS, 77u, 55, 0, pool, n, venues, 3);
        placed = towers_place_bosses(&r);
        printf("fb|%d|%d|", placed, r.floors);
        for (i = 10; i <= r.floors; i += 10) printf("%s%d", i > 10 ? "," : "", r.roster[i - 1]);
        printf("|%u|%u\n", r.roster_hash, towers_roster_hash(r.roster, r.floors));
        n = 0;
        for (i = 0; i < 25; i++) pool[n++] = 1000 + i;           /* no story boss: nothing placed, nothing moved */
        towers_new_run(&plain, TOWER_FIVE_BOROUGHS, 77u, 55, 0, pool, n, venues, 3);
        r = plain;
        printf("none|%d|%d\n", towers_place_bosses(&r), memcmp(r.roster, plain.roster, sizeof r.roster) == 0);
    }
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
    {   /* the repeat counter: key 7 three times in 12 s is read; key 9 spread out never is */
        static TowerAiAdapt ad;
        TowerAiFloor a;
        towers_ai_adapt_reset(&ad);
        static const uint32_t key[8] = { 7, 7, 7, 7, 7, 9, 9, 9 }, at[8] = { 100, 300, 700, 1800, 3100, 1000, 2000, 3000 };
        printf("adapt|");
        for (f = 0; f < 8; f++) printf("%d|", towers_ai_adapt_note(&ad, key[f], at[f]));     /* in this order */
        for (f = 0; f < 40; f++) towers_ai_adapt_note(&ad, 1000u + (uint32_t)f, 4000u + (uint32_t)f);   /* the table rolls */
        printf("%d|%d\n", ad.n, towers_ai_adapt_skill(60));
        for (f = 1; f <= 72; f++) {
            towers_ai_floor(TOWER_UPTOWN, f, 72, &a);
            printf("adapton|%d|%d\n", f, towers_ai_adapt_on(&a, f));
        }
        towers_ai_floor(TOWER_FIVE_BOROUGHS, 10, 130, &a);
        printf("adaptboss|%d\n", towers_ai_adapt_on(&a, 10));
    }
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


def test_the_repeat_counter_reads_a_move_used_three_times_in_a_short_window(out):
    # key 7 at steps 100, 300, 700: the third use in 12 s is read; again at 1800 (in its 20 s): still read; at 3100
    # (more than 20 s after the last use): no longer. Key 9 once every 1000 steps is never read. The table rolls.
    assert rows(out, 'adapt')[0] == ['0', '0', '1', '1', '0', '0', '0', '0', '24', '80']
    on = {int(r[0]): r[1] == '1' for r in rows(out, 'adapton')}
    assert not any(on[f] for f in range(1, 12)) and all(on[f] for f in range(12, 73))
    assert rows(out, 'adaptboss')[0] == ['1']                  # every boss, also below floor 12


def test_the_repeat_counter_reads_the_game_state_never_the_pad():
    ai = (HOOKS / 'towers_ai.c').read_text(encoding='utf-8')
    count = ai[ai.index('static int count_step('):]
    count = count[:count.index('\n}\n')]
    assert 'F_STATE' in count and 'F_MOVE' in count
    for pad in ('0x9E0', '0x9E4', '0x9F0', '0x9EC', 'F_PAD', '0x3B8F48', 'PAD_TABLE'):
        assert pad not in ai, pad                                 # no pad words, no pad table, no queue


def test_the_bosses_are_fixed_as_in_the_story(out):
    up = {int(r[0]): r for r in rows(out, 'up')}
    placed, floors, top, h1, h2, order, drawn = up[55][1:]
    assert placed == '1' and top == '58'                       # Uptown: Crow on the top floor
    assert h1 == h2                                            # the same roster set: the hash is unchanged
    order, drawn = [int(x) for x in order.split(',')], [int(x) for x in drawn.split(',')]
    assert sorted(order) == sorted(drawn)
    assert sum(a != b for a, b in zip(order, drawn)) <= 2       # one swap, nothing else moved
    assert up[58][3] == '82'                                   # a climb with Crow: the strongest other story boss
    fb = rows(out, 'fb')[0]
    bosses = [int(x) for x in fb[2].split(',')]
    assert fb[0] == '9' and fb[1] == '145'
    assert bosses[:8] == [59, 57, 56, 83, 80, 81, 84, 82]      # D-Mob, Sticky, Blaze, then their later versions
    assert bosses[13] == 58                                     # the last chapter's boss: Crow
    assert fb[3] == fb[4]
    assert rows(out, 'none')[0] == ['0', '1']


def test_fixed_bosses_wait_for_the_service_on_an_online_run():
    # the account service shuffles the order itself and freezes a run that meets another opponent (towers.go Fight):
    # an online run places the bosses only when the service says it does too (bosses=1)
    towers = (HOOKS / 'towers.c').read_text(encoding='utf-8')
    assert 'g_net_bosses = kv_int(reply, "bosses");' in towers
    assert 'if (!online || g_net_bosses) place_bosses();' in towers
    ui = (HOOKS / 'towers_ui.inc').read_text(encoding='utf-8')
    assert 'towers_boss_banner(dl, f, v, pt);' in ui and '"BOSS FIGHT"' in ui
    hud = (HOOKS / 'fight_hud_ui.inc').read_text(encoding='utf-8')
    assert 'FH.boss = hud_up && towers_boss_fight(&opp) && !netplay_active();' in hud


def test_a_read_attack_is_answered_within_a_few_steps_never_frame_perfect():
    # the study's "broken" list: no frame-perfect answers; a read attack cuts the CPU's hold to 4 steps at least
    rules = (HOOKS / 'towers_ai_rules.h').read_text(encoding='utf-8')
    hold = int(re.search(r'#define TOWER_AI_READ_HOLD\s+(\d+)', rules).group(1))
    top = int(re.search(r'#define TOWER_AI_HOLD_TOP\s+(\d+)', rules).group(1))
    assert hold >= 4 and top - 1 >= 4
    ai = (HOOKS / 'towers_ai.c').read_text(encoding='utf-8')
    cut = ai[ai.index('static void read_cut(void)'):]
    cut = cut[:cut.index('\n}\n')]
    assert 'now > TOWER_AI_READ_HOLD' in cut and '(uint32_t)TOWER_AI_READ_HOLD' in cut
