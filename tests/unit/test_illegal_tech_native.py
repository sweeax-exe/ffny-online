"""The illegal-tech rules (src/hooks/illegal_tech_rules.c, build 112), compiled natively and fed fighter states step by
step, as the game's hooks feed them: each rule's legal and illegal sequence (from the mods' clips in #illegal-tech and
the [ITRACE] runs in docs/research/illegal-tech.md), the exceptions (Ice-T, Blaze, Comp), a save and load in the middle
of a combo coming to the same verdict at the same step (rollback), and the report's JSON. Needs MSVC (`cl` on PATH, e.g.
after scripts/env.ps1); skips without it."""
import json
import shutil
import subprocess

import pytest

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# A tiny interpreter: the scenarios below are scripts of lines, one command each.
#   o SLOT STATE [PHASE STUN CHAR REHIT MOVE BEARING]   the slot's record as the next step begins (hex numbers;
#                                               CHAR decimal, the fighter's ID)
#   h A V FLAGS                                 a hit lands (A on V, hit-class flags), before its body runs
#   s [N]                                       N fight steps (default 1) with the records as set
#   save / load                                 a rollback save, a load back to it
#   d                                           print the detections: det|step|rule|a|v|move|count
#   j                                           print the report's JSON
#   x                                           print the state's hash
DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "illegal_tech_rules.h"

static ItState S, saved;
static ItObs O[IT_SLOTS];

int main(void)
{
    char line[256], cmd[16];
    unsigned a, b, c, d, e, f, g;
    int i;
    it_reset(&S);
    memset(O, 0, sizeof(O));
    for (i = 0; i < 2; i++) { O[i].present = 1; O[i].state = IT_ST_FREE; O[i].character = i ? 22 : 55; }
    while (fgets(line, sizeof(line), stdin)) {
        int n;
        if (sscanf(line, "%15s", cmd) != 1) continue;
        if (!strcmp(cmd, "o")) {
            unsigned slot;
            n = sscanf(line + 1, "%u %x %x %x %u %x %x %x", &slot, &a, &b, &c, &d, &e, &f, &g);
            if (n < 2 || slot >= IT_SLOTS) return 2;
            O[slot].present = 1;
            O[slot].state = a;
            if (n >= 3) O[slot].phase = b;
            if (n >= 4) O[slot].stun = c;
            if (n >= 5) O[slot].character = d;
            if (n >= 6) O[slot].rehit = e;
            if (n >= 7) O[slot].move = f;
            if (n >= 8) O[slot].bearing = g;
        } else if (!strcmp(cmd, "h")) {
            if (sscanf(line + 1, "%u %u %x", &a, &b, &c) != 3) return 3;
            it_hit(&S, (int)a, (int)b, c, &O[a], &O[b]);
        } else if (!strcmp(cmd, "s")) {
            g = 1;
            sscanf(line + 1, "%u", &g);
            while (g--) it_step(&S, O);
        } else if (!strcmp(cmd, "save")) {
            saved = S;
        } else if (!strcmp(cmd, "load")) {
            S = saved;
        } else if (!strcmp(cmd, "d")) {
            for (a = 0; a < S.det_n; a++)
                printf("det|%u|%s|%u|%u|%08X|%u\n", S.det[a].step, it_rule_id(S.det[a].rule), S.det[a].attacker,
                       S.det[a].victim, S.det[a].move, S.det[a].count);
            printf("total|%u\n", S.det_total);
        } else if (!strcmp(cmd, "j")) {
            char json[4096] = "{\"x\":1";
            int len = it_json(&S, json, (int)sizeof(json) - 1, (int)strlen(json), 1);
            json[len] = '}'; json[len + 1] = 0;
            printf("json|%s\n", json);
        } else if (!strcmp(cmd, "x")) {
            printf("hash|%08X\n", it_hash(&S));
        }
    }
    (void)d; (void)e; (void)f;
    return 0;
}
'''


@pytest.fixture(scope='module')
def run(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    work = tmp_path_factory.mktemp('it')
    (work / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = work / 'it.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(ROOT / 'src/hooks'),
                            str(work / 'driver.c'), str(ROOT / 'src/hooks/illegal_tech_rules.c'), '/Fe' + str(exe),
                            '/Fo' + str(work) + '\\'], capture_output=True, text=True, cwd=work)
    assert built.returncode == 0, built.stdout + built.stderr

    def go(script):
        out = subprocess.run([str(exe)], input=script, capture_output=True, text=True, check=True).stdout
        return [line.split('|') for line in out.splitlines()]
    return go


def dets(rows):
    return [(int(r[1]), r[2], int(r[3]), int(r[4]), int(r[6])) for r in rows if r[0] == 'det']


def rules(rows):
    return [r[2] for r in rows if r[0] == 'det']


# ---- building blocks (fighter 0 attacks fighter 1 unless named) ----

def strike(a=0, v=1, after='0E', move='47', flags='1', astate='45'):
    """One strike string hit that lands: the attacker in an attack state, the hit, the victim's reaction next step."""
    return f'o {a} {astate} 0 0 55 0 {move}\nh {a} {v} {flags}\no {v} {after}\ns\n'


def block_cancel(a=0, v=1):
    """The attacker blocks for a moment while the victim is still in its reaction (the combo stays open)."""
    return f'o {a} 2\ns 3\n'


def free(slot):
    return f'o {slot} 1\ns\n'


WALL_STICK = 'o 1 11\ns\no 1 37 1\ns 2\n'           # a stagger into the wall: pinned (0x37)
SLUMP_DOWN = 'o 1 15 2\ns 2\n'                       # the pinned victim slumps to the ground
PICK_UP = 'o 0 1E\ns\no 0 34\no 1 35\ns 3\n'          # a pick-up: the attacker's grab, the victim held


def test_one_block_cancel_is_legal_the_second_is_an_infinite(run):
    # morrowbound's Doc example: punch punch > block cancel > punch punch > grab (legal)
    legal = strike() + strike(move='48') + block_cancel() + strike() + strike(move='48') + 'd\n'
    assert dets(run(legal)) == []
    # punch punch > block cancel > punch punch > block cancel > punch punch (illegal at the second cancel's hit)
    illegal = legal.replace('d\n', '') + block_cancel() + strike() + strike(move='48') + 'd\n'
    found = dets(run(illegal))
    assert [d[1] for d in found] == ['infinite'] and found[0][2:4] == (0, 1) and found[0][4] == 2


def test_ice_t_back_loop_is_exempt(run):
    def hit(bearing, achar):
        return f'o 0 45 0 0 {achar} 0 47\nh 0 1 1\no 1 0E 0 0 22 0 0 {bearing}\ns\n'
    def loop(bearing, achar):
        return hit(bearing, achar) + ''.join(block_cancel() + hit(bearing, achar) for _ in range(3)) + 'd\n'
    assert dets(run(loop('80', 8))) == []                       # Ice-T, from behind: his back submission loop
    assert [d[1] for d in dets(run(loop('8', 8)))] == ['infinite']      # Ice-T from the front: not exempt
    assert [d[1] for d in dets(run(loop('80', 55)))] == ['infinite']    # Doc from behind: a back loop, counted


def test_block_cancels_in_separate_combos_are_legal(run):
    one = strike() + block_cancel() + strike()
    script = one + free(1) + one + free(1) + one + 'd\n'
    assert dets(run(script)) == []


def test_a_blocked_hit_ends_the_combo(run):
    script = strike() + block_cancel() + strike() + 'o 1 2\ns\n' + strike(after='03') + 'o 1 1\ns\n' + \
        strike() + block_cancel() + strike() + 'd\n'
    assert dets(run(script)) == []


def test_one_restick_is_legal_two_are_illegal(run):
    # "One Wall restick": combo > wall hit > ground > pick up > second wall hit with strikes > throw
    one = strike(after='11') + WALL_STICK + strike(after='37') + SLUMP_DOWN + PICK_UP + 'o 0 4A\no 1 11\ns\n' + WALL_STICK + \
        strike(after='37') + strike(after='37') + 'd\n'
    assert dets(run(one)) == []
    # "Two wall resticks": ... wall stick > 3 punches > wall stick (the third time on the wall)
    two = one.replace('d\n', '') + SLUMP_DOWN + PICK_UP + 'o 0 4A\no 1 11\ns\n' + WALL_STICK + 'd\n'
    found = dets(run(two))
    assert [d[1] for d in found] == ['restick'] and found[0][4] == 2


def test_strikes_on_a_pinned_victim_are_no_restick(run):
    script = strike(after='11') + WALL_STICK + ''.join(strike(after='37') for _ in range(8)) + 'd\n'
    assert dets(run(script)) == []


def test_grab_on_a_victim_coming_off_the_wall(run):
    off = strike(after='11') + WALL_STICK + 'o 1 10 0 1\ns 4\n'      # off the wall in the long dizzy
    assert dets(run(off + PICK_UP + 'd\n'))[0][1] == 'wall_grab'
    # a grab while the victim is still pinned is legal (the legal restick line ends with one)
    assert dets(run(strike(after='11') + WALL_STICK + PICK_UP + 'd\n')) == []
    # the victim got to the ground first: picking it up is legal
    assert dets(run(strike(after='11') + WALL_STICK + SLUMP_DOWN + PICK_UP + 'd\n')) == []
    # grabbed on the wall (live: the grabber 0x1E > 0x67, the pinned victim 0x37 > 0x68): legal
    assert dets(run(strike(after='11') + WALL_STICK + 'o 0 1E\ns\no 0 67\no 1 68\ns 3\no 1 15\ns\nd\n')) == []
    # the same wall grab once it has come off the wall dizzy: a grab off the wall
    assert rules(run(off + 'o 0 1E\ns\no 0 67\no 1 68\ns 3\nd\n')) == ['wall_grab']
    # coming off the wall stays "coming off" while it is dizzy, past the 150 steps
    assert rules(run(off + 'o 1 16 0 1\ns 300\n' + PICK_UP + 'd\n')) == ['wall_grab']
    # not dizzy and long gone from the wall: no longer coming off it
    assert rules(run(strike(after='11') + WALL_STICK + 'o 1 11\ns 200\n' + PICK_UP + 'd\n')) == []


def test_wall_redaze(run):
    # live (Ice-T on Bless, 2026-11-25): the third strike on the pinned victim fills the stun meter and pops it off
    # the wall into a big stagger with the stun bit set (0x37 > 0x10, +0x927 bit 2)
    off = strike(after='11') + WALL_STICK + strike(after='37') + strike(after='10') + 'o 1 10 0 1\ns 4\n'
    # coming off the wall in the long dizzy is legal by itself
    assert dets(run(off + 'o 1 15 0 1\ns 30\no 1 15 0 0\ns\nd\n')) == []
    # a hit that keeps it standing in the dizzy: a redaze
    found = dets(run(off + strike(after='10') + 'd\n'))
    assert [d[1] for d in found] == ['wall_redaze'] and found[0][2:4] == (0, 1)
    # a hit that puts the dizzy victim down is legal
    assert dets(run(off + strike(after='15') + 'd\n')) == []
    # a dizzy without a wall before it is no redaze
    assert rules(run(strike(after='11') + 'o 1 10 0 1\ns 3\n' + strike(after='10') + 'd\n')) == []
    # off the wall but not dizzy: strikes are fine
    assert rules(run(strike(after='11') + WALL_STICK + 'o 1 10 0 0\ns 3\n' + strike(after='10') + 'd\n')) == []


def chain_stun(move, a=0, v=1, achar=55):
    """A grab, then a hold move `move` that leaves the victim in the long dizzy (0x16)."""
    return (f'o {a} 1E 0 0 {achar}\ns\no {a} 34 0 0 {achar} 0 {move}\no {v} 35\ns 2\n'
            f'o {a} 1 0 0 {achar} 0 36\no {v} 36\ns 2\no {v} 16 0 1\ns 3\n')


def test_the_same_chain_stun_twice_is_illegal_different_ones_are_fine(run):
    assert dets(run(chain_stun('2730') + 'o 1 10 0 0\ns\n' + chain_stun('2731') + 'd\n')) == []
    found = dets(run(chain_stun('2730') + 'o 1 10 0 0\ns\n' + chain_stun('2730') + 'd\n'))
    assert [d[1] for d in found] == ['chain_stun'] and found[0][4] == 2
    # back to neutral in between: a new combo, legal
    assert dets(run(chain_stun('2730') + free(1) + chain_stun('2730') + 'd\n')) == []


def test_blaze_and_comp_chain_stuns_are_exempt(run):
    for ch in (56, 81, 82, 46):
        assert dets(run(chain_stun('2730', achar=ch) + 'o 1 10 0 0\ns\n' + chain_stun('2730', achar=ch) + 'd\n')) == []


def test_one_detection_per_rule_and_combo(run):
    combo = strike() + ''.join(block_cancel() + strike() for _ in range(5))
    rows = run(combo + 'd\n')
    assert rules(rows) == ['infinite'] and ['total', '1'] in rows


def test_a_load_back_into_a_combo_comes_to_the_same_verdict(run):
    # rollback: save mid-combo, run on to the detection, load, run the same steps again
    first = strike() + block_cancel() + strike() + 'save\n'
    rest = block_cancel() + strike() + 'd\nx\n'
    once = run(first + rest)
    twice = run(first + rest + 'load\n' + rest)
    d1 = [r for r in once if r[0] in ('det', 'hash')]
    d2 = [r for r in twice if r[0] in ('det', 'hash')]
    assert d1 and d2[-len(d1):] == d1
    # a wrong prediction (no block cancel) then the load and the right input: the verdict follows the right one
    wrong = run(first + strike() + strike() + 'load\n' + rest)
    assert [r for r in wrong if r[0] in ('det', 'hash')] == d1


def test_report_json(run):
    rows = run(strike() + block_cancel() + strike() + block_cancel() + strike() + 'j\n')
    doc = json.loads([r for r in rows if r[0] == 'json'][0][1])
    assert doc['it']['v'] == 1 and doc['it']['n'] == 1
    d = doc['it']['d'][0]
    assert d['r'] == 'infinite' and d['a'] == 0 and d['v'] == 1 and d['af'] == 55 and d['vf'] == 22 and d['c'] == 2
    empty = json.loads([r for r in run('j\n') if r[0] == 'json'][0][1])
    assert empty['it'] == {'v': 1, 'n': 0, 'd': []}


def test_wiring():
    """The hooks reach the detector in every fight, its state rides in every rollback save and the checksum, and
    only a ranked or tournament set game reports it."""
    combo = (ROOT / 'src/hooks/combo_log.c').read_text(encoding='utf-8')
    assert 'illegal_tech_hit(attacker, defender, flags);' in combo and 'illegal_tech_step();' in combo
    assert 'illegal_tech_match_start();' in combo
    header = (ROOT / 'src/hooks/combo_log.h').read_text(encoding='utf-8')
    assert 'static inline int combo_log_on(void)\n{\n    return 1;\n}' in header
    net = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')
    assert 'illegal_tech_state_save(N.snap[slot].it);' in net and 'illegal_tech_state_load(N.snap[slot].it);' in net
    assert 'N.hash_at[k] = (N.hash_at[k] ^ illegal_tech_hash()) * 16777619u;' in net
    assert 'illegal_tech_set_reported(N.set_match && !N.tower_match);' in net
    assert 'n = illegal_tech_json(json, size - 2, n);' in net
