"""The online hub's tile layout and grid navigation (build 98, src/hooks/hub_grid.c), compiled natively.

One big tile (QUICK MATCH) on the left, the other modes in up to three rows beside it, a bar of chips at the
foot. The stick or the D-pad moves in 2D between the boxes and into the bar, never wrapping round; every box
is reachable. The drawing (src/hooks/hub_tiles.inc) is checked for its wiring at the end.
Needs MSVC (`cl` on PATH) on Windows or `cc` elsewhere; skips without it.
"""
import os
import shutil
import subprocess
from collections import deque
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src' / 'hooks'

# the hub's own numbers (hub_tiles.inc HUB_TILE_X ...): the area 36,74 568x332, big tile 230 wide, bar 26, gap 8
DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include "hub_grid.h"

/* layout nright nchips -> every box; move nright nchips cur dx dy -> the new box */
int main(int argc, char **argv)
{
    HubRect area = { 36, 74, 568, 332 }, r[HUB_GRID_MAX];
    int n = hub_grid_layout(atoi(argv[2]), atoi(argv[3]), area, 230, 26, 8, r), i;
    if (argv[1][0] == 'l') {
        printf("%d\n", n);
        for (i = 0; i < n; i++) printf("%.1f %.1f %.1f %.1f\n", r[i].x, r[i].y, r[i].w, r[i].h);
        return 0;
    }
    printf("%d\n", hub_grid_move(r, n, atoi(argv[4]), atoi(argv[5]), atoi(argv[6])));
    return 0;
}
'''


@pytest.fixture(scope='module')
def exe(tmp_path_factory):
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('the native fixture needs a C compiler (MSVC on Windows)')
    folder = tmp_path_factory.mktemp('hubgrid')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    src = str(HOOKS / 'hub_grid.c')
    if os.name == 'nt':
        out = folder / 'hubgrid.exe'
        cmd = [compiler, '/nologo', '/O2', '/TC', '/I' + str(HOOKS), str(folder / 'driver.c'), src, '/Fe:' + str(out)]
    else:
        out = folder / 'hubgrid'
        cmd = [compiler, '-O2', '-std=c11', '-I' + str(HOOKS), str(folder / 'driver.c'), src, '-lm', '-o', str(out)]
    built = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def layout(exe, nright, nchips):
    out = subprocess.run([str(exe), 'l', str(nright), str(nchips)], capture_output=True, text=True, check=True).stdout.split('\n')
    n = int(out[0])
    return [tuple(float(v) for v in line.split()) for line in out[1:1 + n]]


def move(exe, nright, nchips, cur, dx, dy):
    return int(subprocess.run([str(exe), 'm', str(nright), str(nchips), str(cur), str(dx), str(dy)],
                              capture_output=True, text=True, check=True).stdout)


def test_layout_rows_and_bar(exe):
    # no RTS: RANKED alone on the first row, COMPLEX + TOWERS, FIND + CREATE; 6 chips
    r = layout(exe, 5, 6)
    assert len(r) == 12
    big = r[0]
    assert big[:2] == (36, 74) and big[2] == 230 and big[3] == 332 - 26 - 8
    rows = sorted({t[1] for t in r[1:6]})
    assert len(rows) == 3 and [sum(1 for t in r[1:6] if t[1] == y) for y in rows] == [1, 2, 2]
    assert r[1][2] > r[2][2]                                   # RANKED a whole row, wider than the others
    for t in r[6:]:
        assert t[1] == 74 + 332 - 26 and t[3] == 26          # the bar at the foot
    assert abs(r[-1][0] + r[-1][2] - 604) < 0.01              # ends at the area's right edge
    # with RTS: two in every row; with TOURNAMENT MATCH too: 2, 2, 3
    assert [t[1] for t in layout(exe, 6, 7)[1:7]].count(74) == 2
    seven = layout(exe, 7, 7)[1:8]
    assert sorted(t[1] for t in seven).count(max(t[1] for t in seven)) == 3


def test_no_box_overlaps_and_all_inside(exe):
    for nright in range(0, 8):
        for nchips in range(4, 8):
            r = layout(exe, nright, nchips)
            assert len(r) == 1 + nright + nchips
            for i, a in enumerate(r):
                assert a[0] >= 36 - 0.01 and a[0] + a[2] <= 604 + 0.01 and a[1] >= 74 and a[1] + a[3] <= 406 + 0.01
                for b in r[i + 1:]:
                    ox = min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0])
                    oy = min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1])
                    assert ox <= 0.01 or oy <= 0.01, (nright, nchips, a, b)


def test_grid_moves(exe):
    # 0 QUICK, 1 RANKED, 2 COMPLEX, 3 TOWERS, 4 FIND, 5 CREATE, 6.. chips (STATS, PLAYERS, VOICE, MUSIC, LEAVE)
    m = lambda cur, dx, dy: move(exe, 5, 5, cur, dx, dy)
    assert m(0, 1, 0) == 1          # right from QUICK MATCH: the top row (RANKED)
    assert m(1, 0, 1) == 2          # down from RANKED: COMPLEX, lined up on the left
    assert m(2, 1, 0) == 3 and m(3, -1, 0) == 2
    assert m(3, 0, -1) == 1         # up from TOWERS: RANKED (it spans the row)
    assert m(2, -1, 0) == 0 and m(4, -1, 0) == 0     # left from any first column: QUICK MATCH
    assert m(4, 0, 1) >= 6 and m(5, 0, 1) >= 6       # down from the last row: into the bar
    assert m(0, 0, 1) == 6          # down from QUICK MATCH: the first chip
    assert m(6, 1, 0) == 7 and m(7, -1, 0) == 6      # along the bar
    assert m(6, 0, -1) == 0         # up from the first chip: QUICK MATCH above it
    assert m(10, 0, -1) == 5        # up from the last chip (LEAVE): CREATE A ROOM above it
    # no wraparound at the edges
    assert m(0, -1, 0) == 0 and m(0, 0, -1) == 0 and m(1, 1, 0) == 1 and m(1, 0, -1) == 1
    assert m(6, -1, 0) == 6 and m(10, 1, 0) == 10 and m(8, 0, 1) == 8


def test_with_rts_the_top_row_has_two(exe):
    m = lambda cur, dx, dy: move(exe, 6, 6, cur, dx, dy)
    assert m(0, 1, 0) == 1 and m(1, 1, 0) == 2 and m(2, -1, 0) == 1 and m(2, 0, 1) == 4


def test_every_box_is_reachable(exe):
    for nright in range(0, 8):
        for nchips in (4, 6, 7):
            n = 1 + nright + nchips
            seen, todo = {0}, deque([0])
            while todo:
                cur = todo.popleft()
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nxt = move(exe, nright, nchips, cur, dx, dy)
                    if nxt not in seen:
                        seen.add(nxt)
                        todo.append(nxt)
            assert seen == set(range(n)), (nright, nchips, seen)


def test_hub_wiring():
    screen = (HOOKS / 'online_screen.cpp').read_text(encoding='utf-8')
    tiles = (HOOKS / 'hub_tiles.inc').read_text(encoding='utf-8')
    assert '#include "hub_tiles.inc"' in screen and '#include "hub_grid.h"' in screen
    hub = screen.split('void draw_hub(', 1)[1].split('\n}\n', 1)[0]
    # the tiles replace the list and the door; every destination stays in the same switch
    assert 'hub_tiles_on()' in hub and 'hub_tiles_draw(' in hub
    for case in ('case 2: S.page = ROOMS;', 'case HUB_RTS: open_rts();', 'case HUB_COMPLEX: open_complex(HUB, 0);',
                 'case HUB_TOWERS: open_towers();', 'case HUB_MUSIC: open_music();', 'case HUB_TSET: tset_join();'):
        assert case in hub, case
    # RUN THE STREETS only with access (the same check as the list), the grid in the C module
    assert 'rts_access()' in hub
    assert 'hub_grid_move(' in tiles and 'hub_grid_layout(' in tiles
    # LT/RT turn QUICK MATCH's mode on the tile (left/right move between tiles)
    assert 'pad.lt' in tiles and 'pad.rt' in tiles
