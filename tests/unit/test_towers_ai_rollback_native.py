# SPDX-License-Identifier: GPL-3.0-or-later
"""Team Tower Online with rollback (build 111): the tower AI ramp (src/hooks/towers_ai.c) is deterministic and
idempotent per step. A synthetic 2 vs 2 fight on floor 30 (skill/tendency ramp, hold cap and the repeat counter all on)
is run straight, and again with a rollback every few steps: a save (the AI contexts, towers_ai_state_save, plus guest
memory), steps run ahead on wrong guesses of the players' input, a load and a quiet re-run on the right input. Both
must end with the same AI hash at every step and the same guest memory, with the writes and reads really done.
Compiled natively (needs MSVC: `cl` on PATH; skips without it)."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / "src" / "hooks"

STUB_TYPES = r'''#include <stdint.h>
extern unsigned char memory[];
#define MEM32(a) (*(uint32_t *)(memory + (a)))
#define MEM16(a) (*(uint16_t *)(memory + (a)))
#define MEM8(a)  (*(uint8_t *)(memory + (a)))
#define MEMF(a)  (*(float *)(memory + (a)))
'''

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "recomp/gen/recomp_types.h"
#include "towers_ai.h"
#include "towers_rules.h"

#define MEMSIZE 0x400000u
unsigned char memory[MEMSIZE];
#define AI_BLOCK(s)  (0x003773A8u + 0x890u * (uint32_t)(s))
#define ACTORS       0x003B92E0u
#define STRIDE       0x12E8u
#define BRAIN(s)     (0x003BF990u + 0xB8u * (uint32_t)(s))
#define CPU_FLAG(s)  (0x003B9088u + 4u * (uint32_t)(s))
#define GAME_RNG     0x003C1000u        /* the synthetic game's own state, in guest memory (saved with it) */

static uint32_t mix(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }

/* the players' input of a step (wrong: another guess, as a prediction that missed) */
static uint32_t input(unsigned step, int who, int wrong) { return mix(step * 2u + (uint32_t)who + (wrong ? 0x9E3779B9u : 0u)); }

static void setup(void)
{
    int s, set, k;
    memset(memory, 0, MEMSIZE);
    for (s = 0; s < 4; s++) {
        MEM32(ACTORS + STRIDE * s + 0x28u) = (uint32_t)s;          /* the record's slot */
        MEM32(ACTORS + STRIDE * s + 0x30u) = 1u;                   /* standing */
        MEM32(CPU_FLAG(s)) = s >= 2;
        MEM32(AI_BLOCK(s) + 0x7CCu) = 40u;                         /* the loaded block: skill, tendencies */
        for (set = 0; set < 4; set++)
            for (k = 0; k < 24; k++) MEM32(AI_BLOCK(s) + 0x60u * set + 4u * k) = (uint32_t)(1 + (s + set + k) % 5);
    }
    MEM32(GAME_RNG) = 12345u;
}

/* one step of the synthetic game after the AI's own pass: the players act on their input (a small set of attacks, so
 * the counter sees repeats), the CPUs' brains count down and pick new holds, a hit lands now and then */
static void game_step(unsigned step, int wrong)
{
    int who, c;
    uint32_t r = mix(MEM32(GAME_RNG) + step);
    MEM32(GAME_RNG) = r;
    for (who = 0; who < 2; who++) {
        uint32_t in = input(step, who, wrong), rec = ACTORS + STRIDE * (uint32_t)who;
        if ((in & 7u) == 0) {
            MEM32(rec + 0x30u) = 0x45u + (in >> 3) % 3u;
            MEM32(rec + 0xF50u) = 0x100u + (in >> 8) % 2u;
        } else if ((in & 7u) < 4) MEM32(rec + 0x30u) = 1u;
    }
    for (c = 2; c < 4; c++) {
        uint32_t b = BRAIN(c);
        int hold = (int)MEM32(b + 0x34u), think = (int)MEM32(b + 0x38u);
        if (hold > 0) hold--; else hold = (int)(mix(r + (uint32_t)c) % 200u);
        if (think > 0) think--; else think = (int)(mix(r ^ (uint32_t)c) % 90u);
        MEM32(b + 0x34u) = (uint32_t)hold;
        MEM32(b + 0x38u) = (uint32_t)think;
        MEM32(ACTORS + STRIDE * (uint32_t)c + 0x30u) = MEM32(AI_BLOCK(c) + 0x7CCu) > 60u ? 0x46u : 2u;
    }
    if (r % 37u == 0) towers_ai_health(ACTORS + STRIDE * (r % 4u), -5.0f);
}

#define STEPS 3000u
static uint32_t straight[STEPS + 1];

int main(int argc, char **argv)
{
    static unsigned char save_mem[MEMSIZE];
    static unsigned char save_ai[16384];
    unsigned step, every = argc > 1 ? (unsigned)atoi(argv[1]) : 7u, depth = argc > 2 ? (unsigned)atoi(argv[2]) : 5u;
    uint32_t mem_hash_a = 2166136261u, mem_hash_b = 2166136261u, i, final_a;
    unsigned mismatches = 0, rollbacks = 0;
    if (towers_ai_state_size() > sizeof save_ai) { printf("state too big %u\n", towers_ai_state_size()); return 2; }
    /* run A: straight, one pass per step */
    setup();
    towers_ai_set_online(1);
    towers_ai_begin_team(TOWER_TEAM, 30, 35);
    for (step = 1; step <= STEPS; step++) {
        towers_ai_step(step, 0);
        game_step(step, 0);
        straight[step] = towers_ai_hash();
    }
    towers_ai_end(0, STEPS);
    final_a = towers_ai_hash();
    for (i = 0x370000u; i < MEMSIZE; i++) mem_hash_a = (mem_hash_a ^ memory[i]) * 16777619u;
    /* run B: every `every` steps a save, `depth` steps ahead on wrong input, the load and a quiet re-run */
    setup();
    towers_ai_begin_team(TOWER_TEAM, 30, 35);
    for (step = 1; step <= STEPS; step++) {
        if (step % every == 0 && step + depth <= STEPS) {
            unsigned j;
            memcpy(save_mem, memory, MEMSIZE);
            towers_ai_state_save(save_ai);
            for (j = 0; j < depth; j++) { towers_ai_step(step + j, 0); game_step(step + j, 1); }
            memcpy(memory, save_mem, MEMSIZE);
            towers_ai_state_load(save_ai);
            towers_ai_quiet(1);
            for (j = 0; j < depth; j++) {
                towers_ai_step(step + j, 0);
                game_step(step + j, 0);
                if (towers_ai_hash() != straight[step + j]) mismatches++;
            }
            towers_ai_quiet(0);
            rollbacks++;
            step += depth - 1;
            continue;
        }
        towers_ai_step(step, 0);
        game_step(step, 0);
        if (towers_ai_hash() != straight[step]) mismatches++;
    }
    towers_ai_end(0, STEPS);
    for (i = 0x370000u; i < MEMSIZE; i++) mem_hash_b = (mem_hash_b ^ memory[i]) * 16777619u;
    printf("mismatches %u rollbacks %u memory %s final %08X %08X\n", mismatches, rollbacks,
           mem_hash_a == mem_hash_b ? "same" : "differs", final_a, towers_ai_hash());
    return 0;
}
'''


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    cl = shutil.which("cl")
    if not cl:
        pytest.skip("the native tower AI rollback test needs MSVC")
    tmp = tmp_path_factory.mktemp("towersai_rb")
    (tmp / "recomp" / "gen").mkdir(parents=True)
    (tmp / "recomp" / "gen" / "recomp_types.h").write_text(STUB_TYPES, encoding="utf-8")
    (tmp / "driver.c").write_text(DRIVER, encoding="utf-8")
    out = tmp / "towersai_rb.exe"
    r = subprocess.run([cl, "/nologo", "/O2", "/TC", "/I" + str(tmp), "/I" + str(HOOKS), "/I" + str(ROOT / "src"),
                        str(tmp / "driver.c"), str(HOOKS / "towers_ai.c"), str(HOOKS / "towers_ai_rules.c"),
                        str(HOOKS / "towers_rules.c"), "/Fe:" + str(out)], cwd=tmp, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    return out


@pytest.mark.parametrize("every,depth", [(7, 5), (3, 8), (11, 1)])
def test_rollback_reruns_match_the_straight_run(exe, every, depth):
    r = subprocess.run([str(exe), str(every), str(depth)], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stdout + r.stderr
    last = r.stdout.strip().splitlines()[-1]
    assert "mismatches 0 " in last and "memory same" in last, last
    a, b = last.split("final ")[1].split()
    assert a == b
    log = r.stdout + r.stderr
    # the ramp really ran on floor 30: the block was taken, words written, holds cut, attacks read
    assert "skill 40 ->" in log and "cut to" in log and "is read" in log, log[-2000:]


def test_wiring():
    """towers.c runs the floor's step in re-runs too and saves/loads its state with each rollback save (netplay.c)."""
    tw = (HOOKS / "towers.c").read_text(encoding="utf-8")
    net = (HOOKS / "netplay.c").read_text(encoding="utf-8")
    assert "if (g_net.on) { net_step(); return; }" in tw
    assert "towers_online_state_save(N.snap[slot].tw);" in net and "towers_online_state_load(N.snap[slot].tw);" in net
    assert "if (N.tower_match) return 0;" not in net
