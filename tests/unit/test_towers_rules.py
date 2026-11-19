"""Towers (T0): the seeded order of opponents and venues, the difficulty curve, the health rule between fights and
the run file (src/hooks/towers_rules.c, compiled natively), plus the wiring the game side needs (the hooks in
openworld.c, automatch.c and pc_ui.cpp, the hub entry). Needs MSVC (`cl` on PATH) for the native part."""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "towers_rules.h"

static TowerRun a, b;
static char text[65536];

static void print_run(const char *tag, const TowerRun *r)
{
    int i;
    printf("%s|%d|%u|%d|%d|%u|", tag, r->tower, r->seed, r->floors, r->venue_n, r->roster_hash);
    for (i = 0; i < r->floors; i++) printf("%s%d", i ? "," : "", r->roster[i]);
    printf("|");
    for (i = 1; i <= r->floors; i++) printf("%s%d", i > 1 ? "," : "", towers_venue(r, i));
    printf("\n");
}

int main(void)
{
    int pool[80], venues[40], n = 0, vn = 0, i, f, t;
    for (i = 0; i < 60; i++) pool[n++] = i * 3 + 1;       /* 60 stock-like IDs */
    pool[n++] = 55;                                       /* the climber's own pick */
    pool[n++] = 4;                                        /* a duplicate */
    for (i = 1; i <= 27; i++) venues[vn++] = i;           /* standard venues */
    venues[vn++] = 30; venues[vn++] = 41;                 /* two community venues */
    towers_new_run(&a, TOWER_UPTOWN, 1234u, 55, 0, pool, n, venues, vn);
    print_run("run", &a);
    towers_new_run(&b, TOWER_UPTOWN, 1234u, 55, 0, pool, n, venues, vn);
    print_run("same", &b);
    towers_new_run(&b, TOWER_UPTOWN, 99u, 55, 0, pool, n, venues, vn);
    print_run("other", &b);
    {   /* the pool in another order: the same run */
        int rev[80];
        for (i = 0; i < n; i++) rev[i] = pool[n - 1 - i];
        towers_new_run(&b, TOWER_UPTOWN, 1234u, 55, 0, rev, n, venues, vn);
        print_run("reversed", &b);
    }
    towers_new_run(&b, TOWER_FIVE_BOROUGHS, 1234u, 55, 0, pool, n, venues, vn);
    print_run("t2", &b);
    printf("empty|%d|%d\n", towers_new_run(&b, TOWER_UPTOWN, 1u, 55, 0, pool, 0, venues, vn),
           towers_new_run(&b, TOWER_UPTOWN, 1u, 55, 0, pool, n, venues, 0));
    for (t = 1; t <= 2; t++)
        for (f = 1; f <= 130; f++) {
            TowerFloorRules r;
            int floors = t == 1 ? 60 : 130;
            if (t == 1 && f > floors) break;
            towers_floor_rules(t, f, floors, &r);
            printf("curve|%d|%d|%d|%.4f|%.4f|%.4f|%d|%d\n", t, f, r.cpu_level, r.cpu_damage, r.cpu_momentum,
                   r.cpu_health, r.boss, towers_is_save_point(t, f, floors, TOWER_CHAPTER));
        }
    printf("health|%d|%d|%d|%d|%d|%d\n", towers_health_after_win(TOWER_HEALTH_FULL, 30, 12),
           towers_health_after_win(TOWER_HEALTH_CARRY, 30, 12), towers_health_after_win(TOWER_HEALTH_REFILL, 30, 12),
           towers_health_after_win(TOWER_HEALTH_REFILL, 30, 90), towers_health_after_win(TOWER_HEALTH_CARRY, 0, 0),
           towers_health_after_win(7, 30, 40));
    {   /* save and restore */
        const TowerRun *runs[TOWER_COUNT] = { &a, NULL, NULL };
        TowerRun back[TOWER_COUNT];
        int have[TOWER_COUNT], best[TOWER_COUNT] = { 17, 3, 0 }, best2[TOWER_COUNT], len, ok;
        a.floor = 13; a.health_pct = 64; a.climb_ms = 777000u; a.interrupted = 1; a.in_fight = 13; a.draws = 2; a.skipped = 1;
        a.started = 1795000000LL; a.skin = 2;
        len = towers_file_write(runs, best, text, (int)sizeof text);
        {   /* one line for the test: the file's line breaks as spaces */
            static char flat[65536];
            for (i = 0; text[i]; i++) flat[i] = text[i] == '\n' ? ' ' : text[i];
            flat[i] = 0;
            printf("file|%d|%s\n", len, flat);
        }
        ok = towers_file_read(text, back, have, best2);
        printf("read|%d|%d|%d|%d|%d|%d|%d|%d|%d|%u|%d|%d|%d|%lld|%d|%d\n", ok, have[0], have[1], have[2], best2[0], best2[1],
               back[0].floor, back[0].health_pct, back[0].skin, back[0].climb_ms, back[0].interrupted, back[0].draws,
               back[0].skipped, back[0].started, memcmp(back[0].roster, a.roster, sizeof(int) * (size_t)a.floors) == 0,
               back[0].in_fight);
        print_run("back", &back[0]);
        /* a damaged roster (one ID changed: the hash no longer matches) is dropped */
        {
            char *p = strstr(text, "\"roster\":[");
            p += 10;
            *p = *p == '9' ? '8' : '9';
            ok = towers_file_read(text, back, have, best2);
            printf("damaged|%d|%d|%d\n", ok, have[0], best2[0]);
        }
        printf("notafile|%d\n", towers_file_read("hello", back, have, best2));
        len = towers_file_write(runs, best, text, 20);
        printf("small|%d\n", len);
    }
    {
        uint32_t s = 7u, x = towers_next32(&s), y = towers_next32(&s);
        printf("gen|%u|%u|%u\n", x, y, s);
    }
    {   /* T1: the account service's vectors (server relay/accounts/towers_test.go): the same run on both sides */
        int p10[10], v5[5] = { 1, 3, 5, 8, 30 };
        uint32_t s = 0x4F2A91C3u, x = towers_next32(&s), y = towers_next32(&s);
        for (i = 0; i < 10; i++) p10[i] = i + 1;
        towers_new_run(&b, TOWER_UPTOWN, 0x4F2A91C3u, 3, 0, p10, 10, v5, 5);
        printf("srvgen|%08X|%08X|%08X\n", x, y, b.roster_hash);
        printf("srvorder|");
        for (i = 0; i < b.floors; i++) printf("%s%d", i ? "," : "", b.roster[i]);
        printf("|");
        for (i = 1; i <= 5; i++) printf("%s%d", i > 1 ? "," : "", towers_venue(&b, i));
        printf("|");
        for (i = 1; i <= 9; i++) { TowerFloorRules r; towers_floor_rules(TOWER_UPTOWN, i, 9, &r); printf("%s%d", i > 1 ? "," : "", r.cpu_level); }
        printf("\n");
    }
    {   /* T1: a draw is fought again once per floor; a fight left open is a loss at the next start */
        int beaten = -1, open;
        printf("refight|%d|%d|%d\n", towers_draw_refights(0), towers_draw_refights(1), towers_draw_refights(2));
        a.in_fight = 13;
        open = towers_pending_is_loss(&a, &beaten);
        printf("pending|%d|%d|", open, beaten);
        a.in_fight = 0;
        printf("%d\n", towers_pending_is_loss(&a, &beaten));
    }
    {   /* T1: the server's run id, the board flag and the floor's refights survive the file */
        const TowerRun *runs[TOWER_COUNT] = { &a, NULL, NULL };
        TowerRun back[TOWER_COUNT];
        int have[TOWER_COUNT], best[TOWER_COUNT] = { 1, 0, 0 };
        snprintf(a.run_id, sizeof a.run_id, "9f3c0a17be42d6e1");
        a.board = 1;
        a.refights = 1;
        towers_file_write(runs, best, text, (int)sizeof text);
        towers_file_read(text, back, have, best);
        printf("net|%d|%s|%d|%d\n", have[0], back[0].run_id, back[0].board, back[0].refights);
        snprintf(a.run_id, sizeof a.run_id, "bad\"id");     /* a quote cannot break the file */
        towers_file_write(runs, best, text, (int)sizeof text);
        printf("netbad|%d\n", towers_file_read(text, back, have, best) && have[0]);
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def out():
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = Path(tempfile.mkdtemp(prefix='towers'))
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'towers.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(HOOKS),
                            str(folder / 'driver.c'), str(HOOKS / 'towers_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8').splitlines()


def row(out, tag):
    return next(line.split('|')[1:] for line in out if line.startswith(tag + '|'))


def rows(out, tag):
    return [line.split('|')[1:] for line in out if line.startswith(tag + '|')]


def ids(s):
    return [int(x) for x in s.split(',')] if s else []


def test_the_order_is_a_seeded_shuffle_of_the_roster(out):
    tower, seed, floors, venue_n, h, roster, venues = row(out, 'run')
    roster = ids(roster)
    expected = sorted(set(i * 3 + 1 for i in range(60)) | {4})
    expected.remove(55)                                  # the climber never meets himself
    assert int(floors) == len(roster) == len(expected)
    assert sorted(roster) == expected                    # every fighter once, the duplicate once
    assert roster != expected                            # shuffled
    assert int(venue_n) == 27                            # Uptown: standard venues only
    assert all(1 <= v <= 27 for v in ids(venues))
    assert row(out, 'same') == row(out, 'run')           # the same seed: the same run, floor by floor
    assert row(out, 'reversed') == row(out, 'run')       # the pool's order does not matter
    other = row(out, 'other')
    assert ids(other[5]) != roster and sorted(ids(other[5])) == expected and other[4] == h   # same roster, new order


def test_venues_change_every_floor(out):
    venues = ids(row(out, 'run')[6])
    assert all(a != b for a, b in zip(venues, venues[1:]))   # never the same venue twice in a row
    assert len(set(venues)) > 10                              # spread over the pool


def test_five_boroughs_takes_community_venues(out):
    t2 = row(out, 't2')
    assert int(t2[3]) == 29
    assert set(ids(t2[6])) & {30, 41}
    assert row(out, 'empty') == ['0', '0']               # no opponents or no venues: no run


def test_the_curve_climbs_and_stays_mild(out):
    curve = rows(out, 'curve')
    t1 = [c for c in curve if c[0] == '1']
    levels = [int(c[2]) for c in t1]
    assert levels == sorted(levels)                       # never easier further up
    assert levels[0] == 0 and 1 in levels and levels[-1] == 2
    assert [int(c[2]) for c in t1[:8]] == [0] * 8         # p < 0.15 of 60 floors: Easy
    for c in t1:
        f, dmg, mom, hp, boss = int(c[1]), float(c[3]), float(c[4]), float(c[5]), c[6] == '1'
        assert 1.0 <= dmg <= 1.4001 and 0 <= mom <= 0.7501 and 1.0 <= hp <= 1.4376
        assert boss == (f == 60)                          # Uptown: the top floor is the boss
        if not boss:
            assert dmg <= 1.3001 and mom <= 0.5001 and hp <= 1.1501
        assert c[7] == '1'                                # Uptown saves after every floor
    top = t1[-1]
    assert (float(top[3]), float(top[4]), float(top[5])) == pytest.approx((1.40, 0.75, 1.4375))
    t2 = [c for c in curve if c[0] == '2']
    assert [int(c[1]) for c in t2 if c[6] == '1'] == list(range(10, 131, 10))   # a boss closes each chapter
    assert [int(c[1]) for c in t2 if c[7] == '1'] == list(range(10, 131, 10))   # ... and is its save point
    endless = [c for c in t2 if 100 <= int(c[1]) and c[6] == '0']
    assert endless and all(c[3:6] == endless[0][3:6] for c in endless)      # past floor 100 the top row stays


def test_health_rules(out):
    assert row(out, 'health') == ['100', '12', '42', '100', '1', '100']


def test_the_run_file_round_trips(out):
    file_row = next(line for line in out if line.startswith('file|'))
    text = file_row.split('|', 2)[2]
    data = json.loads(text)                               # real JSON
    assert data['version'] == 1 and data['best'] == [17, 3, 0]
    run = data['runs'][0]
    assert run['tower'] == 1 and run['floor'] == 13 and len(run['roster']) == run['floors']
    r = row(out, 'read')
    assert r == ['1', '1', '0', '0', '17', '3', '13', '64', '2', '777000', '1', '2', '1', '1795000000', '1', '13']
    assert row(out, 'back')[:6] == row(out, 'run')[:6] or row(out, 'back')[4] == row(out, 'run')[4]
    assert row(out, 'damaged') == ['1', '0', '17']        # the run is dropped, the best floors stay
    assert row(out, 'notafile') == ['0']
    assert row(out, 'small') == ['0']


def test_generator_is_splitmix32(out):
    def mix(state):
        state = (state + 0x9E3779B9) & 0xFFFFFFFF
        z = state
        z = ((z ^ (z >> 16)) * 0x85EBCA6B) & 0xFFFFFFFF
        z = ((z ^ (z >> 13)) * 0xC2B2AE35) & 0xFFFFFFFF
        return state, z ^ (z >> 16)
    s, x = mix(7)
    s, y = mix(s)
    assert row(out, 'gen') == [str(x), str(y), str(s)]


def test_the_server_computes_the_same_run(out):
    # the vectors the account service's tests pin (server relay/accounts/towers_test.go): seed 0x4F2A91C3, pool 1..10,
    # climber 3, venues 1,3,5,8,30 in Uptown (30 is a community venue: dropped)
    assert row(out, 'srvgen') == ['AFAAAE8E', '4365412F', '68BD538D']
    order, venues, levels = row(out, 'srvorder')
    assert ids(order) == [8, 6, 5, 7, 4, 2, 1, 10, 9]
    assert ids(venues) == [3, 8, 1, 1, 3]       # floors 3 and 4: the rule looks at the floor below's own draw
    assert ids(levels) == [0, 1, 1, 2, 2, 2, 2, 2, 2]


def test_draws_and_fights_left_open(out):
    assert row(out, 'refight') == ['1', '0', '0']   # one refight per floor, then a draw is a loss
    assert row(out, 'pending') == ['1', '12', '0']  # left in floor 13's fight: 12 beaten, the run is over


def test_the_server_run_survives_the_file(out):
    assert row(out, 'net') == ['1', '9f3c0a17be42d6e1', '1', '1']
    assert row(out, 'netbad') == ['1']


def test_the_game_side_is_wired():
    ow = (HOOKS / 'openworld.c').read_text(encoding='utf-8')
    assert 'towers_match_start();' in ow and 'towers_step();' in ow and 'towers_health_change();' in ow
    am = (HOOKS / 'automatch.c').read_text(encoding='utf-8')
    assert 'towers_on_funccall(text);' in am and 'towers_rematch_request()' in am
    ui = (HOOKS / 'pc_ui.cpp').read_text(encoding='utf-8')
    assert 'take_towers_request' in ui and 'towers_card_wanted' in ui
    hub = (HOOKS / 'online_screen.cpp').read_text(encoding='utf-8')
    assert '#include "towers_ui.inc"' in hub and 'HUB_TOWERS' in hub
    towers = (HOOKS / 'towers.c').read_text(encoding='utf-8')
    assert 'netplay_active()' in towers      # never online
