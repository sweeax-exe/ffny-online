"""The online input delay and its line (src/hooks/netplay_delay_rules.c, build 76), compiled natively.

Build 78: netdelay_steps_r0, the hybrid delay of rollback matches (rollback-high-ping-study.md 3a), against
the study's formula and its table over the ping grid; lockstep and r0 <= 0 stay netdelay_steps.

netdelay_steps must give exactly the delay builds 19-75 agreed (the old inline formula of agreed_delay,
written out again below), so a match plays with the same delay as before and the line says what it plays
with. Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1); skips without it."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "netplay_delay_rules.h"

int main(void)
{
    int a, b, rb, mm;
    char line[96];
    for (a = -1; a <= 400; a += 3)
        for (b = -1; b <= a; b += 7)
            for (rb = 0; rb < 2; rb++)
                for (mm = 0; mm < 2; mm++)
                    printf("steps|%d|%d|%d|%d|%d\n", a, b, rb, mm, netdelay_steps(a, b, rb, mm));
    printf("steps|110|110|0|0|%d\n", netdelay_steps(110, 110, 0, 0));
    printf("steps|110|110|1|0|%d\n", netdelay_steps(110, 110, 1, 0));
    printf("steps|398|397|0|0|%d\n", netdelay_steps(398, 397, 0, 0));
    for (a = -1; a <= 400; a += 3)
        for (b = -1; b <= a; b += 7)
            for (rb = 0; rb < 2; rb++)
                for (mm = 0; mm < 2; mm++) {
                    int r0;
                    for (r0 = -1; r0 <= 9; r0++)
                        printf("hyb|%d|%d|%d|%d|%d|%d\n", a, b, rb, mm, r0, netdelay_steps_r0(a, b, rb, mm, r0));
                }
    for (a = 0; a <= 300; a += 10)
        printf("hybgrid|%d|%d\n", a, netdelay_steps_r0(a, a, 1, 0, 2));
    {
        int p[6] = { 40, -1, 120, 90, 120, 10 }, x, y;
        netdelay_top2(p, 6, &x, &y);
        printf("top2|%d|%d\n", x, y);
        netdelay_top2(p, 0, &x, &y);
        printf("top2|%d|%d\n", x, y);
    }
    printf("path|%d\n", netdelay_path_ms(150, 131));
    netdelay_line(line, (int)sizeof(line), 1, 3, 40, 0);   printf("line|%s\n", line);
    netdelay_line(line, (int)sizeof(line), 0, 10, 140, 0); printf("line|%s\n", line);
    netdelay_line(line, (int)sizeof(line), 1, 1, 0, 1);    printf("line|%s\n", line);
    netdelay_line(line, (int)sizeof(line), 0, 7, -5, 1);   printf("line|%s\n", line);
    printf("short|%d\n", netdelay_line(line, 8, 0, 10, 140, 0));
    return 0;
}
'''


def old_delay(a, b, rb, players_3plus):
    """agreed_delay as builds 19-75 had it inline."""
    a, b = max(a, 0), max(b, 0)
    d = ((a + b) // 2 * 3 + 49) // 50 + 1
    if rb:
        d -= 4
    if not rb and players_3plus:
        d += 1
    return 2 if d < 2 else 10 if d > 10 else d


def delay_761(a, b, rb, players_3plus):
    """Build 76.1: lockstep adds one step after the clamp (build 75's slack), rollback unchanged."""
    return old_delay(a, b, rb, players_3plus) + (0 if rb else 1)


def hybrid_delay(a, b, rb, mm, r0):
    """the study's formula: Lf = path in steps rounded up, R = R0 + max(0, (Lf-3)//3), d = clamp(Lf+1-R)"""
    if not rb or r0 <= 0:
        return delay_761(a, b, rb, mm)
    r0 = min(r0, 8)
    lf = ((max(a, 0) + max(b, 0)) // 2 * 3 + 49) // 50
    d = lf + 1 - (r0 + max(0, (lf - 3) // 3))
    return 2 if d < 2 else 10 if d > 10 else d


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('delay')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'delay.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/utf-8', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/netplay_delay_rules.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return [line.split('|') for line in out.splitlines()]


def by(rows, kind):
    return [r[1:] for r in rows if r[0] == kind]


def test_same_delay_as_before(rows):
    steps = by(rows, 'steps')
    assert len(steps) > 1000
    for a, b, rb, mm, got in steps:
        a, b, rb, mm, got = int(a), int(b), int(rb), int(mm), int(got)
        assert got == delay_761(a, b, rb, mm), (a, b, rb, mm)


def test_known_values(rows):
    table = {(int(a), int(b), int(rb), int(mm)): int(d) for a, b, rb, mm, d in by(rows, 'steps')}
    assert table[(-1, -1, 0, 0)] == 3           # local test copies: the minimum (+1 lockstep, 76.1)
    assert table[(110, 110, 0, 0)] == 9         # two players at 110 ms, lockstep (+1, 76.1)
    assert table[(110, 110, 1, 0)] == 4         # the same with rollback
    assert table[(398, 397, 0, 0)] == 11        # the maximum (10 + 1 in lockstep, 76.1)


def test_top_two_and_path(rows):
    assert by(rows, 'top2') == [['120', '120'], ['0', '0']]
    assert by(rows, 'path') == [['140']]


def test_line_words(rows):
    dot = '·'
    assert by(rows, 'line') == [
        [f'Rollback {dot} 3 frames'],
        [f'No rollback (path 140 ms) {dot} 10 frames'],
        [f'Rollback {dot} about 1 frame'],
        [f'No rollback (path 0 ms) {dot} about 7 frames'],
    ]
    assert by(rows, 'short') == [['7']]


def test_hybrid_formula(rows):
    hyb = by(rows, 'hyb')
    assert len(hyb) > 10000
    for a, b, rb, mm, r0, got in hyb:
        a, b, rb, mm, r0, got = int(a), int(b), int(rb), int(mm), int(r0), int(got)
        assert got == hybrid_delay(a, b, rb, mm, r0), (a, b, rb, mm, r0)
        if not rb:
            assert got == delay_761(a, b, 0, mm)     # lockstep (76.1) never changes, whatever R0 says


def test_hybrid_study_table(rows):
    """the study's table (section 3a, R0 = 2): path ms -> delay (two players with the same ping: path = ping)"""
    grid = {int(p): int(d) for p, d in by(rows, 'hybgrid')}
    want = {20: 2, 40: 2, 60: 3, 80: 4, 100: 4, 120: 6, 140: 6, 160: 7, 180: 8, 200: 8}
    for path, d in want.items():
        assert grid[path] == d, path
    assert all(2 <= d <= 10 for d in grid.values())
    assert [grid[p] for p in sorted(grid)] == sorted(grid[p] for p in sorted(grid))     # never less delay for more ping
