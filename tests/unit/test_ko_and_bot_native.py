"""The test-only knockout (RECOMP_TEST_KO_STEP, src/hooks/test_telemetry.c) and the test bot
(RECOMP_TEST_BOT, src/hooks/test_bot.c) against synthetic fighter records: when the knockout
fires, that a step run again (rollback) does the same again, the winner choice; and that the
bot drives only the fighters it should, the same way for the same state."""
import os
import subprocess

import pytest

import native_hooks_fixtures

DRIVER = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_observations.h"
unsigned char memory[0x400000];
static unsigned char saved[0x400000];
extern int stub_set_state_calls;
#define ACTOR(n) (0x3b92e0u + (n) * 0x12e8u)
#define PAD(n) (0x3b8f48u + (n) * 0x20u)
static void put32(uint32_t a, uint32_t v) { memcpy(memory + a, &v, 4); }
static void putf(uint32_t a, float v) { memcpy(memory + a, &v, 4); }
static uint32_t get32(uint32_t a) { uint32_t v; memcpy(&v, memory + a, 4); return v; }
static float getf(uint32_t a) { float v; memcpy(&v, memory + a, 4); return v; }

static void fighters(float distance)
{
    int n;
    for (n = 0; n < 4; n++) put32(ACTOR(n) + 0x28, 0xFFFFFFFFu);
    for (n = 0; n < 2; n++) {
        put32(ACTOR(n) + 0x28, (uint32_t)n);
        put32(ACTOR(n) + 0x30, 1);                                    /* standing */
        putf(ACTOR(n) + 0x44, n ? distance / 2 : -distance / 2);
        putf(ACTOR(n) + 0x350, 100.0f);
        putf(ACTOR(n) + 0x354, 100.0f);
        putf(ACTOR(n) + 0x35c, 1.0f);
    }
    put32(ACTOR(1) + 0x934, 0x01000000u);                             /* +0x937 bit 0: slot 1 is a CPU */
    put32(0x3b8f30, 1);                                               /* fighting */
    put32(0x3c1400, 12345);                                           /* the match generator's term */
}

static void line(int step)
{
    printf("%d %u %u %g %g %d\n", step, get32(ACTOR(0) + 0x30), get32(ACTOR(1) + 0x30),
           getf(ACTOR(0) + 0x350), getf(ACTOR(1) + 0x350), stub_set_state_calls);
}

int main(int argc, char **argv)
{
    int step;
    const char *mode = argc > 1 ? argv[1] : "ko";
    if (!strcmp(mode, "ko")) {                  /* knockout at step 5; then step 5 run again */
        fighters(50.0f);
        defjam_test_match_start();
        for (step = 1; step <= 8; step++) {
            if (step == 5) memcpy(saved, memory, sizeof(memory));     /* the state before step 5 */
            defjam_test_step();
            line(step);
        }
        memcpy(memory, saved, sizeof(memory));                         /* a rollback to step 4's end */
        defjam_test_set_step(4);
        defjam_test_step();
        line(5);
    } else if (!strcmp(mode, "busy")) {         /* the winner is busy until step 20 */
        fighters(50.0f);
        put32(ACTOR(0) + 0x30, 0x45);
        defjam_test_match_start();
        for (step = 1; step <= 22; step++) {
            if (step == 21) put32(ACTOR(0) + 0x30, 1);
            defjam_test_step();
            line(step);
        }
    } else if (!strcmp(mode, "alt")) {          /* two matches, the winner alternates */
        int m;
        for (m = 0; m < 2; m++) {
            fighters(50.0f);
            defjam_test_match_start();
            for (step = 1; step <= 6; step++) defjam_test_step();
            line(m);
        }
    } else if (!strcmp(mode, "bot")) {          /* the bot's pad words, far apart and then close */
        fighters(50.0f);
        defjam_test_match_start();
        for (step = 1; step <= 240; step++) {
            if (step == 120) fighters(6.0f);
            put32(PAD(0), 0xAAAA); put32(PAD(1), 0xBBBB);
            defjam_test_step();
            defjam_test_input(ACTOR(0));
            defjam_test_input(ACTOR(1));
            printf("%d %x %x %x\n", step, get32(PAD(0)), get32(PAD(1)), get32(PAD(0) + 28));
        }
    } else if (!strcmp(mode, "plan")) {         /* the opponent winds up at 50, holds slot 0 at 100 */
        fighters(6.0f);
        defjam_test_match_start();
        for (step = 1; step <= 140; step++) {
            put32(ACTOR(1) + 0x30, step >= 50 && step < 56 ? 0x45 : 1);
            put32(ACTOR(0) + 0x30, step >= 100 && step < 110 ? 0x35 : 1);
            if (step == 140) put32(0x3b8f30, 2);                       /* decided */
            put32(PAD(0), 0xAAAA);
            defjam_test_step();
            defjam_test_input(ACTOR(0));
            printf("%d %x\n", step, get32(PAD(0)));
        }
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    return native_hooks_fixtures.build(tmp_path_factory, 'ko-bot-native', DRIVER)


def run(exe, mode, **env):
    base = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_') and k != 'STUB_LOCAL_SLOT'}
    result = subprocess.run([str(exe), mode], env=dict(base, **env), capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return [line.split() for line in result.stdout.splitlines()], result.stderr


def test_knockout_at_the_step_and_again_after_a_rollback(tool):
    rows, err = run(tool, 'ko', RECOMP_TEST_KO_STEP='5')
    by_step = {int(r[0]): r for r in rows[:8]}
    for step in range(1, 5):                    # nothing before the step
        assert by_step[step][1:] == ['1', '1', '100', '100', '0']
    # slot 0 wins by default: slot 1 gets zero health and the knockout state 0x18, once
    assert by_step[5][1:] == ['1', '24', '100', '0', '1']
    assert all(by_step[s][5] == '1' for s in (6, 7, 8))
    # the same step run again from the state before it: the same knockout again
    assert rows[8][:5] == ['5', '1', '24', '100', '0'] and rows[8][5] == '2'
    assert '[TEST-KO] match 1 step 5: slot 1 knocked out (winner slot 0)' in err


def test_knockout_waits_for_a_free_stance(tool):
    rows, _ = run(tool, 'busy', RECOMP_TEST_KO_STEP='5,0')
    fired = [int(r[0]) for r in rows if r[5] == '1' and r[2] == '24']
    assert fired and fired[0] == 21


def test_knockout_winner_alternates(tool):
    rows, _ = run(tool, 'alt', RECOMP_TEST_KO_STEP='3,alt')
    assert rows[0][1:3] == ['1', '24']          # match 1: slot 0 wins
    assert rows[1][1:3] == ['24', '1']          # match 2: slot 1 wins


def test_knockout_off_without_the_switch(tool):
    rows, err = run(tool, 'ko', RECOMP_TEST_OBSERVATIONS='1')
    assert all(r[5] == '0' and r[2] == '1' for r in rows)
    assert 'TEST-KO' not in err


def test_bot_drives_the_human_slot_and_repeats(tool):
    rows, err = run(tool, 'bot', RECOMP_TEST_BOT='1,60')
    again, _ = run(tool, 'bot', RECOMP_TEST_BOT='1,60')
    assert rows == again                        # the same state, the same presses
    assert all(r[2] == 'bbbb' for r in rows)    # the CPU fighter is the game's own
    assert all(r[3] == '0' for r in rows)       # a fresh poll for netplay to take
    far = [int(r[1], 16) for r in rows[:40]]
    close = [int(r[1], 16) for r in rows[130:]]
    assert far.count(0x8) >= 35                 # walks right, toward the opponent at +x
    # the synthetic fighters never move: after 45 steps without getting closer it tries a detour
    assert any(int(r[1], 16) in (0x1, 0x2, 0x4) for r in rows[45:119])
    actions = 0x80 | 0x40 | 0x20 | 0x2000
    assert sum(1 for b in close if b & actions) > 10
    assert '[TEST-BOT] on: aggression 60' in err


def test_bot_online_drives_only_this_pcs_player(tool):
    rows, _ = run(tool, 'bot', RECOMP_TEST_BOT='1', STUB_LOCAL_SLOT='1')
    assert all(r[1] == 'aaaa' for r in rows)
    assert any(r[2] != 'bbbb' for r in rows)


def test_bot_plan_counter_blocks_the_windup_and_mashes_out_of_a_hold(tool):
    rows, err = run(tool, 'plan', RECOMP_TEST_BOT='1', RECOMP_TEST_BOT_PLAN='p1=counter')
    pad = {int(r[0]): int(r[1], 16) for r in rows}
    assert '[TEST-BOT] plan: slot 0 counter' in err
    assert all(pad[s] == 0x2000 for s in range(50, 58))            # block from the wind-up's first step
    assert all(pad[s] & 0xC0 and not pad[s] & 0x2000 for s in range(100, 110))   # mashing strikes
    assert not any(pad[s] & 0xE0 for s in range(60, 99))           # waits in reach otherwise
    assert 'summary slot 0 (counter):' in err
    assert 'counters 1 of 1 wind-ups (1 clean), held 1 (mashed 10, broke 1)' in err


def test_bot_plan_idle_presses_nothing(tool):
    rows, err = run(tool, 'plan', RECOMP_TEST_BOT='1', RECOMP_TEST_BOT_PLAN='idle')
    assert all(r[1] == '0' for r in rows[:139])
    assert 'summary' not in err


def test_bot_off_without_the_switch(tool):
    rows, _ = run(tool, 'bot', RECOMP_TEST_OBSERVATIONS='1')
    assert all(r[1] == 'aaaa' and r[2] == 'bbbb' for r in rows)
