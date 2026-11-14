"""Build 83: the season countdown and the hub fighter search (src/hooks/season_search.c), compiled natively, plus
the wiring in the hub pages. Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1) for the native part; skips
without it."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include "season_search.h"

int main(void)
{
    char out[96];
    const long long now = 1000000;
    const long long ends[] = { 0, -5, now - 10, now, now + 59, now + 3599, now + 3600, now + 7200 + 5,
                               now + 86399, now + 86400, now + 86400 * 2 - 1, now + 86400 * 48 + 3600 };
    const char *names[] = { "Blaze", "D-Mob", "Crow", "Ice-T", "Method Man", "Busta Rhymes", "Carmen Electra", "" };
    const char *queries[] = { "", "  ", "bla", "BLA", "dmob", "d mob", "-mob", "ice t", "man", "zz", "meth man" };
    unsigned i, j;
    for (i = 0; i < sizeof(ends) / sizeof(ends[0]); i++) {
        int n = season_countdown_text("SEASON 1", ends[i], now, out, sizeof(out));
        printf("cd|%lld|%d|%s\n", ends[i] - now, n, out);
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        for (j = 0; j < sizeof(queries) / sizeof(queries[0]); j++)
            printf("m|%s|%s|%d\n", names[i], queries[j], fighter_name_matches(names[i], queries[j]));
    for (i = 0; i < 8; i++)
        printf("j|%u|%d|%d\n", i, fighter_letter_jump(names, 8, (int)i, 1), fighter_letter_jump(names, 8, (int)i, -1));
    {
        const char *one[] = { "Ann", "Abe" };
        printf("j1|%d|%d\n", fighter_letter_jump(one, 2, 1, 1), fighter_letter_jump(one, 2, 0, -1));
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('season_search')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'season_search.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(HOOKS),
                            str(folder / 'driver.c'), str(HOOKS / 'season_search.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return [line.split('|') for line in out.splitlines()]


def test_countdown_wording(rows):
    got = {int(r[1]): r[3] for r in rows if r[0] == 'cd'}
    assert got[-1000000] == ''                     # no end (0): nothing
    assert got[-1000005] == ''                         # a negative end: nothing
    assert got[-10] == 'SEASON 1 ENDS TODAY'           # passed, not rolled over yet
    assert got[0] == got[59] == got[3599] == 'SEASON 1 ENDS TODAY'
    assert got[3600] == 'SEASON 1 ENDS IN 1 HOUR'
    assert got[7205] == 'SEASON 1 ENDS IN 2 HOURS'
    assert got[86399] == 'SEASON 1 ENDS IN 23 HOURS'   # hours on the last day
    assert got[86400] == 'SEASON 1 ENDS IN 1 DAY'
    assert got[2 * 86400 - 1] == 'SEASON 1 ENDS IN 1 DAY'
    assert got[48 * 86400 + 3600] == 'SEASON 1 ENDS IN 48 DAYS'


def test_name_matching(rows):
    got = {(r[1], r[2]): r[3] == '1' for r in rows if r[0] == 'm'}
    assert got[('Blaze', '')] and got[('Blaze', '  ')]                 # nothing typed: every fighter
    assert got[('Blaze', 'bla')] and got[('Blaze', 'BLA')]             # case-insensitive
    assert got[('D-Mob', 'dmob')] and got[('D-Mob', 'd mob')] and got[('D-Mob', '-mob')]   # punctuation ignored
    assert got[('Ice-T', 'ice t')]
    assert got[('Method Man', 'man')] and not got[('Method Man', 'meth man')]   # in a row
    assert not any(v for (n, q), v in got.items() if q == 'zz')
    assert not got[('', 'bla')]


def test_letter_jump(rows):
    # names: 0 Blaze, 1 D-Mob, 2 Crow, 3 Ice-T, 4 Method Man, 5 Busta Rhymes, 6 Carmen Electra, 7 "" (RANDOM)
    got = {int(r[1]): (int(r[2]), int(r[3])) for r in rows if r[0] == 'j'}
    assert got[0] == (2, 4)     # B -> C (Crow, the first C in grid order); back wraps to M
    assert got[2] == (1, 0)     # C -> D; back to B (Blaze, the first B)
    assert got[4] == (0, 3)     # M -> wraps to B; back to I
    assert got[5] == (2, 4)     # the second B behaves as the first
    one = [r for r in rows if r[0] == 'j1'][0]
    assert one[1:] == ['1', '0']   # one letter only: stays


def test_hub_wiring():
    screen = (HOOKS / 'online_screen.cpp').read_text(encoding='utf-8')
    ranked = (HOOKS / 'ranked_ui.inc').read_text(encoding='utf-8')
    board = (HOOKS / 'ranked_board.inc').read_text(encoding='utf-8')
    turf = (HOOKS / 'turf_ui.inc').read_text(encoding='utf-8')
    # the countdown: from the season line's end, nothing for a preseason or without an end
    body = ranked.split('std::string season_countdown()', 1)[1].split('\n}\n', 1)[0]
    assert 'si.pre' in body and 'si.end <= 0' in body and 'season_countdown_text' in body
    assert 'season_countdown()' in board.split('page_title(dl, f, "RANKED BOARD")', 1)[1]
    stats = screen.split('void draw_stats(', 1)[1].split('draw_season_rewards', 1)[0]
    assert 'season_countdown()' in stats
    hub = screen.split('void draw_hub(', 1)[1].split('// the items on one glass panel', 1)[0]
    assert 'season_countdown()' in hub and 'turf_season_countdown()' in hub
    assert 'season_countdown_text("TURF WAR"' in turf
    # the search: typing only while it is open and the page has the keyboard, closing it does not leave the page
    assert '!(S.page == FIGHTERS && S.fsearch_on)' in screen
    fighters = screen.split('void draw_fighters(', 1)[1].split('// ---- the room ----', 1)[0]
    assert 'S.fsearch_on && pad.keys' in fighters and 'fighter_name_matches' in fighters
    assert 'fighter_letter_jump' in fighters and 'pad.lt || pad.rt' in fighters
    assert "const_cast<Pad &>(pad).b = false" in fighters
