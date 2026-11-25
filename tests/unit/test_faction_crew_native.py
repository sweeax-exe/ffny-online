"""Build 112: Faction War's crew-only fighters (src/hooks/faction_crew.c), compiled natively, plus the wiring in the
online screens. The crews are the account service's (server relay/accounts/faction_crews.go, sent as crew_dmob= and
crew_crow= on /api/factions/live and /me); the game carries no list of its own. Needs MSVC (`cl` on PATH, e.g. after
scripts/env.ps1) for the native part; skips without it."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

# The lists as the service sends them (the test's own sample of the answer, not a copy the game uses).
DMOB = "16,17,21,38,43,44,45,46,47,48,50,51,54,55,56,57,59,65,75,80,81,82,83,84,91"
CROW = "7,8,11,12,20,22,23,24,25,26,28,30,31,32,33,34,35,36,58,64,69,77,92"

DRIVER = r'''
#include <stdio.h>
#include "faction_crew.h"

static void show(const char *tag, const int *ids, int n)
{
    int i;
    printf("%s|%d|", tag, n);
    for (i = 0; i < n; i++) printf("%s%d", i ? "," : "", ids[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    int dmob[FACTION_CREW_MAX], crow[FACTION_CREW_MAX], out[FACTION_CREW_MAX], nd, nc, n, i;
    /* the select's offer: stock fighters, a created fighter (99), community places (100, 124), numbers (1000+),
     * RANDOM (-2), the MORE card (-3) */
    const int offered[] = { 55, 22, 59, 83, 84, 58, 9, 99, 100, 124, 1000, 1005, -2, -3, 56, 81, 82, 57, 80, 92, 60, 0 };
    const int no = (int)(sizeof offered / sizeof offered[0]);
    int seen[256] = { 0 };
    (void)argc;
    nd = faction_crew_parse(argv[1], dmob, FACTION_CREW_MAX);
    nc = faction_crew_parse(argv[2], crow, FACTION_CREW_MAX);
    show("dmob", dmob, nd);
    show("crow", crow, nc);
    {   /* a bad line: repeats, a created fighter, a number, junk, blanks */
        int bad[FACTION_CREW_MAX];
        n = faction_crew_parse(" 59, 59,99,1000,x7,7 ,,256,255", bad, FACTION_CREW_MAX);
        show("bad", bad, n);
        n = faction_crew_parse(NULL, bad, FACTION_CREW_MAX);
        printf("null|%d\n", n);
        n = faction_crew_parse("1,2,3,4", bad, 2);
        show("max", bad, n);
    }
    n = faction_crew_filter(dmob, nd, offered, no, out, FACTION_CREW_MAX);
    show("fdmob", out, n);
    n = faction_crew_filter(crow, nc, offered, no, out, FACTION_CREW_MAX);
    show("fcrow", out, n);
    for (i = 0; i < 2000; i++) {
        int r = faction_crew_random(dmob, nd, offered, no, (unsigned)i * 2654435761u);
        if (r >= 0 && r < 256) seen[r] = 1; else printf("rnd-out|%d\n", r);
    }
    printf("rnd|");
    for (i = 0; i < 256; i++) if (seen[i]) printf("%d,", i);
    printf("\n");
    {
        static const int others[] = { 9, 99, 1000 };
        printf("none|%d\n", faction_crew_random(dmob, nd, others, 3, 5));
    }
    printf("empty|%d\n", faction_crew_random(dmob, 0, offered, no, 5));
    printf("has|%d|%d|%d|%d\n", faction_crew_has(dmob, nd, 59), faction_crew_has(dmob, nd, 58), faction_crew_has(crow, nc, 58),
           faction_crew_has(crow, nc, 99));
    printf("line|%s|%s|%s\n", faction_crew_line(1), faction_crew_line(2), faction_crew_line(0));
    {   /* build 112, the variety rule's lines: crew_locked (fighter:fights left), crew_versions (version:character) */
        int a[FACTION_CREW_MAX], b[FACTION_CREW_MAX], k;
        n = faction_crew_pairs("59:3,83:3,84:3,55:2", a, b, FACTION_CREW_MAX);
        printf("pairs|%d|", n);
        for (k = 0; k < n; k++) printf("%d=%d,", a[k], b[k]);
        printf("\n");
        n = faction_crew_pairs(" 80:57, x:1,1000:2,5:,7:7777,81:56", a, b, FACTION_CREW_MAX);
        printf("badpairs|%d|", n);
        for (k = 0; k < n; k++) printf("%d=%d,", a[k], b[k]);
        printf("\n");
        printf("nopairs|%d|%d\n", faction_crew_pairs(NULL, a, b, 4), faction_crew_pairs("", a, b, 4));
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('faction_crew')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'faction_crew.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(HOOKS),
                            str(folder / 'driver.c'), str(HOOKS / 'faction_crew.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe), DMOB, CROW], capture_output=True, check=True).stdout.decode('utf-8')
    return {line.split('|', 1)[0]: line.split('|')[1:] for line in out.splitlines()}


def ids(row):
    return [int(x) for x in row[1].split(',') if x]


def test_parse_the_service_lines(rows):
    assert ids(rows['dmob']) == [int(x) for x in DMOB.split(',')]
    assert ids(rows['crow']) == [int(x) for x in CROW.split(',')]
    assert int(rows['dmob'][0]) == 25 and int(rows['crow'][0]) == 23


def test_parse_skips_what_is_no_stock_fighter(rows):
    assert ids(rows['bad']) == [59, 7, 255]        # no repeat, no 99, no 1000, no junk, no 256
    assert rows['null'] == ['0']
    assert ids(rows['max']) == [1, 2]


def test_filter_keeps_only_the_crew(rows):
    # D-Mob's: Doc, D-Mob in all three versions, Blaze 1-3, Sticky 1-2; never Crow's, a created or community fighter
    assert ids(rows['fdmob']) == [55, 59, 83, 84, 56, 81, 82, 57, 80]
    assert ids(rows['fcrow']) == [22, 58, 92]


def test_random_stays_within_the_crew(rows):
    assert 'rnd-out' not in rows
    drawn = {int(x) for x in rows['rnd'][0].split(',') if x}
    assert drawn == {55, 59, 83, 84, 56, 81, 82, 57, 80}     # every crew fighter offered, nothing else
    assert rows['none'] == ['-1'] and rows['empty'] == ['-1']


def test_has_and_line(rows):
    assert rows['has'] == ['1', '0', '1', '0']
    assert rows['line'] == ["FACTION WAR: D-MOB'S CREW ONLY", "FACTION WAR: CROW'S CREW ONLY", '']


def test_pairs(rows):
    assert rows['pairs'] == ['4', '59=3,83=3,84=3,55=2,']
    assert rows['badpairs'] == ['2', '80=57,81=56,']
    assert rows['nopairs'] == ['0', '0']


def test_variety_lock_wiring():
    ui = (HOOKS / 'faction_ui.inc').read_text(encoding='utf-8')
    screen = (HOOKS / 'online_screen.cpp').read_text(encoding='utf-8')
    # the service's locks, moved on by the fights started here that /me does not count yet
    assert 'k == "crew_locked"' in ui and 'k == "crew_versions"' in ui and 'k == "crew_progress"' in ui
    left = ui[ui.index('int fw_crew_locked_left(int id)'):]
    left = left[:left.index('\n}\n')]
    assert 'FN.me.crew_locked' in left and 'FN.pending' in left and 'FN.live.crew_lock' in left
    assert 'fw_crew_started(room.players[i].fighter)' in ui and 'm.fights > p.fights' in ui
    # a locked fighter is blocked (greyed, RANDOM skips it, the READY check), with its note
    blocked = screen[screen.index('bool fighter_blocked('):]
    blocked = blocked[:blocked.index('\n}\n')]
    assert 'fw_crew_locked_left(id) > 0' in blocked
    assert '"LOCKED: " + std::to_string(rest)' in screen and 'rest > 0 ? "LOCKED"' in screen
    # the page's progress
    assert '"%d OF %d CREW"' in ui


def test_screens_use_the_service_rule():
    ui = (HOOKS / 'faction_ui.inc').read_text(encoding='utf-8')
    screen = (HOOKS / 'online_screen.cpp').read_text(encoding='utf-8')
    # the rule and the lists come from the service's answers only
    assert 'k == "crew_rule"' in ui and 'k == "crew_dmob"' in ui and 'k == "crew_crow"' in ui
    side = ui[ui.index('int fw_crew_side('):]
    side = side[:side.index('\n}\n')]
    assert 'FN.live.crew_rule' in side and 'FN.room_code != r.code' in side and 'NETPLAY_SLOT_SPECTATOR' in side
    # no crew table in the game: no run of crew IDs in the sources
    for text in (ui, screen, (HOOKS / 'faction_crew.c').read_text(encoding='utf-8')):
        assert not re.search(r'\b59\s*,\s*83\s*,\s*84\b', text)
    # every pick path goes through fighter_blocked (greyed cards, the room's READY check, the version window)
    blocked = screen[screen.index('bool fighter_blocked('):]
    blocked = blocked[:blocked.index('\n}\n')]
    assert 'fw_crew_side(r)' in blocked and 'fw_crew_has(side, id)' in blocked
    # the grid shows the crew only (no created, community or MORE cards) and RANDOM draws from it
    grid = screen[screen.index('const std::vector<OnlineFighter> &grid_fighters()'):]
    grid = grid[:grid.index('\n}\n')]
    assert grid.index('fw_crew_side(r)') < grid.index('rnd.id = FIGHTER_RANDOM')
    assert 'g_grid_cp[i] < 0 && list[i].id != FB_MORE_ID && fw_crew_has(side, list[i].id)' in grid
    # the line on the select and the room, and no skins there
    assert screen.count('crew_plate();') == 2 and 'fw_crew_plate(dl, f, 48, 52, side' in screen
    assert 'crew_side;      // build 112' in screen
