"""The start countdown (build 77, src/hooks/start_countdown_rules.c), compiled natively: which steps of a
fight hold the fighters' input at rest, what the overlay shows at each step, and the switch. Plus the
wiring that keeps it deterministic online: the hold is by step number (never the clock), applied to the
pad words the fighters read after the agreed entry is in place, and to the entries this PC sends.
Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1) for the native part; skips without it."""
import re
import shutil
import subprocess

import pytest

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "start_countdown.h"

int main(void)
{
    unsigned s;
    float t;
    const char *values[] = { 0, "", "1", "on", "0", "off", "false", "no", "yes" };
    unsigned i;
    printf("parse");
    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) printf("|%d", start_countdown_parse(values[i]));
    printf("\n");
    {
        int e, o, x;
        for (e = 0; e < 2; e++) for (o = 0; o < 2; o++) for (x = 0; x < 2; x++)
            printf("applies|%d|%d|%d|%d\n", e, o, x, start_countdown_applies(e, o, x));
    }
    for (s = 0; s <= 260; s++) {
        int n = start_countdown_shown(1, s, &t);
        printf("step|%u|%d|%d|%.4f|%d|%d\n", s, start_countdown_held_at(1, s), n, t,
               start_countdown_held_at(0, s), start_countdown_shown(0, s, 0));
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('countdown')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'countdown.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/start_countdown_rules.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return [line.split('|') for line in out.splitlines()]


def steps(rows):
    return {int(r[1]): (int(r[2]), int(r[3]), float(r[4]), int(r[5]), int(r[6])) for r in rows if r[0] == 'step'}


def test_the_switch(rows):
    # unset, empty and anything else: on; 0 / off / false / no: off
    assert [r for r in rows if r[0] == 'parse'][0][1:] == ['1', '1', '1', '1', '0', '0', '0', '0', '1']


def test_online_always_offline_by_the_setting(rows):
    # build 81: (switch, online, [gameplay] offline_countdown) -> the fight has the countdown
    got = {(int(r[1]), int(r[2]), int(r[3])): int(r[4]) for r in rows if r[0] == 'applies'}
    assert got == {
        (1, 1, 0): 1, (1, 1, 1): 1,     # online: always, whatever the setting says (every PC alike)
        (1, 0, 0): 0, (1, 0, 1): 1,     # offline: only with the setting on (default off)
        (0, 1, 0): 0, (0, 1, 1): 0, (0, 0, 0): 0, (0, 0, 1): 0,     # RECOMP_START_COUNTDOWN=0: never
    }


def test_offline_countdown_setting_is_off_by_default_and_shown():
    settings = (ROOT / 'src/hooks/pc_settings.c').read_text(encoding='utf-8')
    assert re.search(r'\{\s*"gameplay",\s*"offline_countdown",\s*RECOMP_SETTING_BOOL,\s*0,', settings)
    ui = (ROOT / 'src/hooks/pc_ui.cpp').read_text(encoding='utf-8')
    assert '{"gameplay", "offline_countdown", "Start countdown in offline fights"}' in ui
    assert ui.count('offline_countdown_setting();') == 2     # launcher Basics and Advanced > General (overlay)
    doc = (ROOT / 'docs/settings-reference.md').read_text(encoding='utf-8')
    assert '| `offline_countdown` | `false` |' in doc
    glue = (ROOT / 'src/hooks/start_countdown.c').read_text(encoding='utf-8')
    # read once at each fight's setup, never mid-fight
    setup = glue[glue.index('void start_countdown_match_start(void)'):glue.index('int start_countdown_active(int online)')]
    assert 'recomp_settings_get("gameplay", "offline_countdown", 0)' in setup
    assert glue.count('recomp_settings_get(') == 1


def test_input_is_held_for_the_first_180_steps(rows):
    s = steps(rows)
    assert s[0][0] == 0                                     # before the first step (intros): nothing held
    assert all(s[i][0] == 1 for i in range(1, 181))
    assert all(s[i][0] == 0 for i in range(181, 261))       # FIGHT: free from step 181 on
    assert all(s[i][3] == 0 and s[i][4] == -1 for i in s)   # off: never held, nothing shown


def test_three_two_one_fight(rows):
    s = steps(rows)
    shown = [s[i][1] for i in range(0, 261)]
    assert shown[0] == -1
    assert shown[1:61] == [3] * 60 and shown[61:121] == [2] * 60 and shown[121:181] == [1] * 60
    assert shown[181:241] == [0] * 60 and shown[241:] == [-1] * 20
    # how far into each number: 0 at its first step, 1 at its last
    assert s[1][2] == 0 and s[60][2] == 1 and s[61][2] == 0 and s[180][2] == 1
    assert s[181][2] == 0 and s[240][2] == 1


def test_wiring_is_by_step_and_in_the_simulated_input():
    netplay = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8', errors='replace')
    # after the agreed entry is written, every fighter's pad words at rest by the round's step
    assert re.search(r'void netplay_on_pad_read\(uint32_t actor\)\s*\{\s*pad_read_body\(actor\);\s*'
                     r'(/\*.*?\*/\s*)?start_countdown_hold\(actor, round_step\(\), fight_online\(\)\);\s*\}', netplay, re.S)
    # online: the lockstep (rollback too, a spectator's) or the soak; the setting is never asked online
    assert re.search(r'static int fight_online\(void\)\s*\{\s*return N\.lockstep \|\| soak_depth\(\);\s*\}', netplay)
    assert 'int netplay_countdown_active(void) { return start_countdown_active(fight_online()); }' in netplay
    # the lockstep's agreed step (rollback's re-runs repeat it); this PC's own count only offline
    assert re.search(r'return N\.lockstep \|\| soak_depth\(\) \? N\.step : start_countdown_offline_step\(\);', netplay)
    # the entries this PC sends for the held steps are at rest: the relay's recording and spectators see them
    assert 'if (start_countdown_enabled() && start_countdown_held_at(1, t)) memset(N.in[N.pos][k], 0, ENTRY);' in netplay
    assert 'start_countdown_match_start();' in netplay and 'start_countdown_step(N.resim);' in netplay
    glue = (ROOT / 'src/hooks/start_countdown.c').read_text(encoding='utf-8')
    rules = (ROOT / 'src/hooks/start_countdown_rules.c').read_text(encoding='utf-8')
    for text in (glue, rules):     # never the clock
        assert not re.search(r'GetTickCount|QueryPerformance|time\(|clock\(|rand', text)
    assert 'RECOMP_START_COUNTDOWN' in glue
    ui = (ROOT / 'src/hooks/fight_hud_ui.inc').read_text(encoding='utf-8')
    assert 'start_countdown_shown(netplay_countdown_active(), FH.view.step' in ui
