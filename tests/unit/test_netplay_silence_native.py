"""Whom a fight's give-up names (src/hooks/netplay_check.h, netcheck_silent_cause), compiled natively.
When this PC hears nobody (no relay packet, or no STEPS from any of two or more other players) its own
connection dropped and no player is named (3-Way TMG7QT, 2026-11-02: "marcelo stopped answering" when
Cashmoney's own connection dropped). Needs MSVC (`cl` on PATH); skips without it."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')

DRIVER = r'''
#include <stdio.h>
#include "netplay_check.h"

int main(void)
{
    /* 3-Way, this PC is player 1 (index 0), waiting for player 2 (index 1) */
    uint32_t all_quiet[4] = { 0, 5200, 4800, 0 };
    uint32_t one_quiet[4] = { 0, 10000, 30, 0 };
    uint32_t duel[4] = { 0, 10000, 0, 0 };
    printf("3way all quiet %d\n", netcheck_silent_cause(3, 0, 1, all_quiet, 200, 4000));
    printf("3way one quiet %d\n", netcheck_silent_cause(3, 0, 1, one_quiet, 200, 4000));
    printf("duel relay heard %d\n", netcheck_silent_cause(2, 0, 1, duel, 200, 4000));
    printf("duel relay quiet %d\n", netcheck_silent_cause(2, 0, 1, duel, 9000, 4000));
    {   /* this PC is index 1: its own entry (0) is not counted, players 0 and 2 are quiet */
        uint32_t mine_heard[4] = { 5000, 0, 4800, 0 };
        printf("self ignored %d\n", netcheck_silent_cause(3, 1, 2, mine_heard, 200, 4000));
    }
    printf("least %u\n", netcheck_least_silence(3, 0, all_quiet, 200));
    printf("least alone %u\n", netcheck_least_silence(1, 0, all_quiet, 7000));
    return 0;
}
'''


@pytest.fixture(scope='module')
def output(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native give-up test needs MSVC')
    folder = tmp_path_factory.mktemp('netsilence')
    driver = folder / 'driver.c'
    driver.write_text(DRIVER, encoding='utf-8')
    exe = folder / 'netsilence.exe'
    r = subprocess.run([compiler, '/nologo', '/W3', '/TC', '/I' + str(ROOT / 'src/hooks'), str(driver), '/Fe:' + str(exe)],
                       cwd=folder, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    return run.stdout


def line(out, key):
    m = re.search(r'^%s (.*)$' % re.escape(key), out, re.M)
    assert m, key
    return m.group(1).strip()


def test_nobody_heard_names_nobody(output):
    assert line(output, '3way all quiet') == '-2'
    assert line(output, 'duel relay quiet') == '-2'


def test_the_others_heard_names_the_late_one(output):
    assert line(output, '3way one quiet') == '1'
    assert line(output, 'duel relay heard') == '1'


def test_this_pc_is_not_counted(output):
    # this PC is index 1 (its own entry is not counted); players 0 and 2 are quiet: own link
    assert line(output, 'self ignored') == '-2'


def test_the_notice_says_the_shortest_silence(output):
    assert line(output, 'least') == '4800'
    assert line(output, 'least alone') == '7000'


def test_the_give_up_uses_it():
    assert 'netcheck_silent_cause(N.players, N.pos, late, silent, relay, SILENT_OWN_MS) == NETCHECK_OWN_LINK' in NETPLAY_C
    assert 'Your connection is unstable: no data from the other players for %u s.' in NETPLAY_C
    assert 'g_steps_at[from] = GetTickCount64();' in NETPLAY_C
