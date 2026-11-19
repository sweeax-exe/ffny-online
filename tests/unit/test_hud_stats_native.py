"""Build 98's room streaks and recent form (src/hooks/hud_stats_rules.c), compiled natively: wins in a row per
player in a room (a win adds one, a loss or a draw ends it, sitting out changes nothing, a new room starts over,
CPUs never count), the HUD's "(4 WINS)" from 2 on, the account service's text answer and the versus card's
"LAST 10: 7W 3L". Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1); skips without it."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "hud_stats_rules.h"

static HudStreaks T;

static void result(const char *room, const char *a, const char *b, const char *c, unsigned winners)
{
    const char *names[3];
    names[0] = a; names[1] = b; names[2] = c;
    hud_streaks_result(&T, room, names, c ? 3 : 2, winners);
}

int main(void)
{
    char out[64];
    HudRecent r[4];
    int n, k, i;
    hud_streaks_reset(&T);
    result("R1 Room", "Slaw", "Ann", NULL, 1);
    printf("one|%d|%d\n", hud_streaks_get(&T, "R1 Room", "Slaw"), hud_streaks_get(&T, "R1 Room", "Ann"));
    result("R1 Room", "Slaw", "Ann", NULL, 1);
    result("R1 Room", "Ann", "Slaw", NULL, 2);       /* positions swap: names count */
    result("R1 Room", "Slaw", "Ann", NULL, 1);
    printf("four|%d|%d\n", hud_streaks_get(&T, "R1 Room", "Slaw"), hud_streaks_get(&T, "R1 Room", "Ann"));
    result("R1 Room", "Bo", "Ann", NULL, 1);          /* Slaw sat this one out */
    printf("sat|%d|%d|%d\n", hud_streaks_get(&T, "R1 Room", "Slaw"), hud_streaks_get(&T, "R1 Room", "Bo"),
           hud_streaks_get(&T, "R1 Room", "Ann"));
    result("R1 Room", "Slaw", "Bo", NULL, 0);         /* a draw ends both */
    printf("draw|%d|%d\n", hud_streaks_get(&T, "R1 Room", "Slaw"), hud_streaks_get(&T, "R1 Room", "Bo"));
    result("R1 Room", "Slaw", "", NULL, 1);           /* against the CPU */
    result("R1 Room", "Slaw", NULL, NULL, 1);
    printf("cpu|%d|%d\n", hud_streaks_get(&T, "R1 Room", "Slaw"), hud_streaks_get(&T, "R1 Room", ""));
    printf("other|%d\n", hud_streaks_get(&T, "R2 Room", "Slaw"));
    result("R2 Room", "Slaw", "Ann", NULL, 2);        /* a new room starts over */
    printf("new|%d|%d\n", hud_streaks_get(&T, "R2 Room", "Slaw"), hud_streaks_get(&T, "R2 Room", "Ann"));
    result("R2 Room", "A", "B", "C", 4);               /* free for all */
    printf("ffa|%d|%d|%d\n", hud_streaks_get(&T, "R2 Room", "A"), hud_streaks_get(&T, "R2 Room", "C"),
           hud_streaks_get(&T, "R2 Room", "Ann"));
    for (i = 0; i < 40; i++) {                         /* more players than entries: the oldest go */
        char x[8], y[8];
        snprintf(x, sizeof(x), "P%d", i);
        snprintf(y, sizeof(y), "Q%d", i);
        result("R2 Room", x, y, NULL, 1);
    }
    printf("full|%d|%d\n", hud_streaks_get(&T, "R2 Room", "P39"), hud_streaks_get(&T, "R2 Room", "P0"));

    for (i = 0; i <= 4; i++) { hud_streak_label(i, out, sizeof(out)); printf("label|%d|%s\n", i, out); }
    hud_streak_label(12, out, 6);
    printf("label|cut|%s\n", out);

    k = hud_recent_parse("n=10\nrecent=SlawPro|7|3|0|10\nrecent=bad|x|1|1|1\nrecent=Ann|4|5|1|10\n"
                         "other=1\nrecent=Short|1|1|0|2", r, 4, &n);
    printf("parse|%d|%d", k, n);
    for (i = 0; i < k; i++) {
        hud_recent_label(&r[i], n, out, sizeof(out));
        printf("|%s=%s", r[i].name, out);
    }
    printf("\n");
    k = hud_recent_parse("", r, 4, &n);
    printf("empty|%d|%d\n", k, n);
    memset(&r[0], 0, sizeof(r[0]));
    printf("none|%d\n", hud_recent_label(&r[0], 10, out, sizeof(out)));
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('hudstats')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'hudstats.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/hud_stats_rules.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return {r[0] + ('|' + r[1] if r[0] == 'label' else ''): r[1:] for r in (line.split('|') for line in out.splitlines())}


def test_wins_in_a_row_per_player(rows):
    assert rows['one'] == ['1', '0']
    assert rows['four'] == ['4', '0']
    assert rows['sat'] == ['4', '1', '0']
    assert rows['draw'] == ['0', '0']


def test_cpus_rooms_and_free_for_all(rows):
    assert rows['cpu'] == ['2', '0']
    assert rows['other'] == ['0']
    assert rows['new'] == ['0', '1']
    assert rows['ffa'] == ['0', '1', '1']
    assert rows['full'] == ['1', '0']


def test_hud_label_from_two_wins(rows):
    assert [rows['label|%d' % i][1] for i in range(5)] == ['', '', '(2 WINS)', '(3 WINS)', '(4 WINS)']
    assert rows['label|cut'] == ['cut', '(12 W']


def test_recent_answer_and_card_line(rows):
    assert rows['parse'] == ['3', '10', 'SlawPro=LAST 10: 7W 3L', 'Ann=LAST 10: 4W 5L 1D', 'Short=LAST 2: 1W 1L']
    assert rows['empty'] == ['0', '10']
    assert rows['none'] == ['0']


def test_wiring():
    """Display only, and where the docs say: the result hook counts once per match, START asks for the form,
    the HUD and the versus card draw them; nothing here writes the game or goes into a match's input."""
    netplay = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8', errors='replace')
    assert 'hud_stats_result(key, list, n, (code & 4u) ? 0u : winners);' in netplay
    assert 'N.streak_counted = 1;' in netplay and netplay.count('N.streak_counted = 0;') == 2
    assert 'hud_stats_recent_request(list,' in netplay
    glue = (ROOT / 'src/hooks/hud_stats.c').read_text(encoding='utf-8')
    assert 'MEM' not in glue and 'account_fetch("' not in glue and '/api/players/recent?format=text' in glue
    assert 'account_call' not in glue      # public route, no session: an answer can never sign the game out
    hud = (ROOT / 'src/hooks/fight_hud_ui.inc').read_text(encoding='utf-8')
    assert 'hud_stats_streak(FH.match.room_key, FH.match.name[slot])' in hud
    ui = (ROOT / 'src/hooks/online_screen.cpp').read_text(encoding='utf-8', errors='replace')
    assert re.search(r'hud_stats_recent_label\(v\.name\[i\], form, sizeof\(form\)\)', ui)
