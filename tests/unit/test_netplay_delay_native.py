"""The online input delay and its line (src/hooks/netplay_delay_rules.c, build 76), compiled natively.

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
        assert got == old_delay(a, b, rb, mm), (a, b, rb, mm)


def test_known_values(rows):
    table = {(int(a), int(b), int(rb), int(mm)): int(d) for a, b, rb, mm, d in by(rows, 'steps')}
    assert table[(-1, -1, 0, 0)] == 2           # local test copies: the minimum
    assert table[(110, 110, 0, 0)] == 8         # two players at 110 ms, lockstep
    assert table[(110, 110, 1, 0)] == 4         # the same with rollback
    assert table[(398, 397, 0, 0)] == 10        # the maximum


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
