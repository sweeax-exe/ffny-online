"""Build 99: recording mode (hides the fight HUD and the overlay's fight drawings) and the offline slow-motion
switch (src/hooks/recording_mode_rules.c compiled natively), plus the wiring that keeps online matches as they
were: the HUD is skipped only at its draw, and slow motion is always the game's own while online.
Needs MSVC (`cl` on PATH) for the native part; skips without it."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "recording_mode.h"

int main(void)
{
    const float v[] = { 0.0f, 0.14f, 0.5f, 0.99f, 1.0f };
    char out[64];
    unsigned i;
    for (i = 0; i < 5; i++)
        printf("scale|%.2f|%.2f|%.2f\n", v[i], recording_slow_motion_scale(v[i], 1), recording_slow_motion_scale(v[i], 0));
    printf("allowed|%d|%d|%d|%d\n", recording_slow_motion_allowed(0, 0), recording_slow_motion_allowed(0, 1),
           recording_slow_motion_allowed(1, 0), recording_slow_motion_allowed(1, 1));
    printf("note|%d|", recording_mode_note_text(1, 1000, 2500, "F10", out, sizeof(out)));
    printf("%s\n", out);
    printf("note|%d|", recording_mode_note_text(0, 1000, 2999, "", out, sizeof(out)));
    printf("%s\n", out);
    printf("gone|%d|%d\n", recording_mode_note_text(1, 1000, 3000, "F10", out, sizeof(out)),
           recording_mode_note_text(1, 0, 10, "F10", out, sizeof(out)));
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('recmode')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'recmode.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/recording_mode_rules.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return [line.split('|') for line in out.splitlines()]


def test_slow_values_become_full_speed_only_when_not_allowed(rows):
    scale = [r[1:] for r in rows if r[0] == 'scale']
    assert scale == [['0.00', '0.00', '0.00'], ['0.14', '0.14', '1.00'], ['0.50', '0.50', '1.00'],
                     ['0.99', '0.99', '1.00'], ['1.00', '1.00', '1.00']]


def test_online_always_allows_slow_motion(rows):
    assert [r[1:] for r in rows if r[0] == 'allowed'] == [['0', '1', '1', '1']]


def test_note_shows_two_seconds(rows):
    notes = [r[1:] for r in rows if r[0] == 'note']
    assert notes == [['1', 'Recording mode on: F10'], ['1', 'Recording mode off: F10']]
    assert [r[1:] for r in rows if r[0] == 'gone'] == [['0', '0']]


def test_wiring():
    manual = (ROOT / 'src/recomp_manual.c').read_text(encoding='utf-8')
    # the HUD draw only (not its update sub_000B5710), skipped as a thiscall with no stack arguments
    assert re.search(r'void sub_000B5AD0\(void\)\s*\{\s*if \(recording_mode_on\(\)\) \{ g_esp \+= 4; return; \}', manual)
    assert 'void sub_000B5710(void)' not in manual
    assert 'recording_slow_motion_allowed_now()' in manual
    game = (ROOT / 'src/hooks/recording_mode.c').read_text(encoding='utf-8')
    assert 'ffny_online_locked()' in game
    settings = (ROOT / 'src/hooks/pc_settings.c').read_text(encoding='utf-8')
    assert re.search(r'\{\s*"gameplay",\s*"slow_motion",\s*RECOMP_SETTING_BOOL,\s*1', settings)
    assert re.search(r'"recording",\s*"key".*?"F10" \}', settings, re.S)
    ui = (ROOT / 'src/hooks/pc_ui.cpp').read_text(encoding='utf-8')
    assert 'if (rec) fightchat = hillhud = hillres = fighthud = delayhud = rtsworld = false;' in ui
    doc = (ROOT / 'docs/settings-reference.md').read_text(encoding='utf-8')
    assert '| `slow_motion` | `true` |' in doc
    assert '## [recording]' in doc
