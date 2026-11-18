"""Fast scrolling in long lists (src/hooks/list_nav.c), compiled natively.

Held up/down: one step at the press, the first repeat 0.3 s later, then 8 steps a second rising to 30 a
second one second after that; a hitch does not jump the list. A single press wraps where the list wraps;
repeats and pages stop at the ends. A 240-row list is walked at 60 frames a second.
Needs MSVC (`cl` on PATH) on Windows or `cc` elsewhere; skips without it.
"""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "list_nav.h"

/* hold: dir seconds fps -> the cursor in a 240-row list after each tenth of a second */
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "hold")) {
        int dir = atoi(argv[2]), frames = (int)(atof(argv[3]) * atoi(argv[4])), fps = atoi(argv[4]), sel = dir > 0 ? 0 : 239, i;
        ListRepeat r;
        memset(&r, 0, sizeof r);
        for (i = 0; i <= frames; i++) {
            int d = list_repeat_steps(&r, dir, 10.0 + (double)i / fps);
            sel = list_nav_move(sel, 240, d, 0);
            if (i % (fps / 10) == 0) printf("%d ", sel);
        }
        list_repeat_steps(&r, 0, 10.0 + (double)(frames + 1) / fps);
        printf("| %d\n", list_repeat_steps(&r, dir, 20.0));       /* a new press: one step */
        return 0;
    }
    if (!strcmp(argv[1], "hitch")) {       /* held 2 s, then a 5 s frame: at most a tenth of a second's steps */
        ListRepeat r;
        int i, total = 0;
        memset(&r, 0, sizeof r);
        for (i = 0; i <= 120; i++) list_repeat_steps(&r, 1, (double)i / 60);
        total = list_repeat_steps(&r, 1, 7.0);
        printf("%d\n", total);
        return 0;
    }
    if (!strcmp(argv[1], "rate")) {
        printf("%.1f %.1f %.1f %.1f %.1f\n", list_repeat_rate(0.1), list_repeat_rate(0.3), list_repeat_rate(0.8),
               list_repeat_rate(1.3), list_repeat_rate(5));
        return 0;
    }
    if (!strcmp(argv[1], "move")) {        /* sel n delta wrap */
        printf("%d\n", list_nav_move(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5])));
        return 0;
    }
    return 2;
}
'''


@pytest.fixture(scope='module')
def exe(tmp_path_factory):
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('the native fixture needs a C compiler (MSVC on Windows)')
    folder = tmp_path_factory.mktemp('listnav')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    src = str(ROOT / 'src/hooks/list_nav.c')
    if os.name == 'nt':
        out = folder / 'listnav.exe'
        cmd = [compiler, '/nologo', '/O2', '/TC', '/I' + str(ROOT / 'src/hooks'), str(folder / 'driver.c'), src,
               '/Fe:' + str(out)]
    else:
        out = folder / 'listnav'
        cmd = [compiler, '-O2', '-std=c11', '-I' + str(ROOT / 'src/hooks'), str(folder / 'driver.c'), src, '-o', str(out)]
    built = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def run(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True).stdout.strip()


def test_rate_rises_from_8_to_30(exe):
    assert run(exe, 'rate').split() == ['0.0', '8.0', '19.0', '30.0', '30.0']


def test_hold_down_accelerates_through_a_long_list(exe):
    out, again = run(exe, 'hold', 1, 4, 60).split('|')
    at = [int(x) for x in out.split()]          # every 0.1 s
    assert at[0] == 1 and at[1] == 1 and at[2] == 1, at       # the press, then nothing until 0.3 s
    assert 1 < at[3] <= 3, at                                # the first repeat at 0.3 s
    first_second = at[13] - at[3]                            # 0.3 .. 1.3 s: 8 rising to 30 a second, about 19
    last_second = at[40] - at[30]                            # at the top rate
    assert 15 <= first_second <= 23, at
    assert 28 <= last_second <= 32, at
    assert 96 <= at[-1] <= 106, at      # 1 + 1 + about 19 + 2.7 s x 30: about 102 rows in 4 s
    assert int(again) == 1                                   # a new press is one step again


def test_hold_up_to_the_top_stops_there(exe):
    out, _ = run(exe, 'hold', -1, 12, 60).split('|')
    at = [int(x) for x in out.split()]
    assert at[0] == 238 and at[-1] == 0, at


def test_same_speed_at_another_frame_rate(exe):
    a = [int(x) for x in run(exe, 'hold', 1, 3, 60).split('|')[0].split()]
    b = [int(x) for x in run(exe, 'hold', 1, 3, 144).split('|')[0].split()]
    assert abs(a[-1] - b[-1]) <= 2, (a, b)


def test_a_hitch_does_not_jump(exe):
    assert int(run(exe, 'hitch')) <= 4


def test_move_wraps_only_single_steps(exe):
    assert run(exe, 'move', 0, 240, -1, 1) == '239'
    assert run(exe, 'move', 239, 240, 1, 1) == '0'
    assert run(exe, 'move', 239, 240, 1, 0) == '239'
    assert run(exe, 'move', 235, 240, 8, 1) == '239'        # a page stops at the end
    assert run(exe, 'move', 100, 240, -1000000, 0) == '0'   # Home / a held trigger
    assert run(exe, 'move', 100, 240, 1000000, 0) == '239'
    assert run(exe, 'move', 5, 0, 1, 1) == '0'


def test_hub_players_online_scrolls_fast():
    # build 96: the hub's PLAYERS ONLINE rows (fix/watch-any's cursor) use the helper too; a single press wraps
    src = (ROOT / 'src' / 'hooks' / 'online_screen.cpp').read_text(encoding='utf-8')
    body = src[src.index('void draw_players(ImDrawList *dl'):]
    body = body[:body.index('\n}\n')]
    assert 'list_nav_delta(nav, pad.up, pad.down, nk, 20, &single)' in body
    assert 'list_nav_move(sel, n, delta, single ? 1 : 0)' in body
    assert 'if (pad.up) sel' not in body and 'if (pad.down) sel' not in body
