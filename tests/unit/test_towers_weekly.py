"""The Weekly (tower 3, build 108): the account service's tower of the week as the game reads and plays it
(src/hooks/towers_rules.c, compiled natively), against the service's own answers (server relay/accounts/
towers_weekly.go WeeklyText, recorded for the weeks 2026-W42..W47: one of each twist). The order, the spare, the
venue of every floor and the roster hash must be the service's, or the service freezes the run ("order"). Needs MSVC
(`cl` on PATH) for the native part."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "towers_rules.h"
#include "towers_ai_rules.h"

static char text[65536];
static TowerRun run;

static int load(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return 0;
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    return 1;
}

int main(int argc, char **argv)
{
    int a, f, i;
    for (a = 1; a < argc; a++) {
        TowerWeekly w;
        int climber = 55, ok;
        if (!load(argv[a])) return 2;
        ok = towers_weekly_parse(text, &w);
        printf("week|%s|%d|%s|%s|%d|%d|%u|%d|%lld|%lld|%08X|%d|", w.week, ok, w.twist_key, w.title, w.twist, w.rules, w.seed,
               w.floors, w.starts, w.ends, w.roster_hash, w.spare);
        for (i = 0; i < w.venue_n; i++) printf("%s%d", i ? "," : "", w.venues[i]);
        printf("\n");
        {   /* the service's roster hash: over the order and the spare */
            int ids[TOWER_WEEKLY_MAX_FLOORS + 1];
            for (i = 0; i < w.floors; i++) ids[i] = w.opponent[i];
            ids[w.floors] = w.spare;
            printf("hash|%s|%08X\n", w.week, towers_roster_hash(ids, w.floors + 1));
        }
        for (i = 0; i < 2; i++) {   /* climber 55, then the climber of floor 5 (the spare stands in there) */
            int c = i ? w.opponent[4] : climber, n = towers_new_weekly_run(&run, &w, c, 0);
            printf("run|%s|%d|%d|%d|%d|%s|%lld|", w.week, c, n, run.twist, run.floor, run.week, run.week_end);
            for (f = 1; f <= run.floors; f++) printf("%s%d", f > 1 ? "," : "", towers_opponent(&run, f));
            printf("|");
            for (f = 1; f <= run.floors; f++) printf("%s%d", f > 1 ? "," : "", towers_venue(&run, f) == w.venue[f - 1]);
            printf("\n");
        }
        towers_new_weekly_run(&run, &w, climber, 0);
        for (f = 1; f <= run.floors; f++) {
            TowerFloorRules r;
            TowerAiFloor ai;
            towers_run_floor_rules(&run, f, &r);
            towers_ai_floor_ex(TOWER_WEEKLY, f, run.floors, run.twist == TOWER_TWIST_ALL_BOSSES, &ai);
            printf("rules|%s|%d|%d|%d|%.2f|%.2f|%.2f|%.2f|%d|%d|%d|%d|%d|%d\n", w.week, f, r.cpu_level, r.boss, r.cpu_damage,
                   r.cpu_health, r.hit_scale, r.max_health, r.momentum_hold, r.carry_health, towers_run_is_boss(&run, f),
                   w.level[f - 1], ai.boss, ai.on);
        }
        {   /* the run file keeps the twist and the week */
            const TowerRun *runs[TOWER_COUNT] = { NULL, NULL, &run, NULL, NULL };
            TowerRun back[TOWER_COUNT];
            int have[TOWER_COUNT], best[TOWER_COUNT] = { 0, 0, 7, 0, 0 }, best2[TOWER_COUNT];
            run.floor = 6;
            run.health_pct = 41;
            towers_file_write(runs, best, text, (int)sizeof text);
            towers_file_read(text, back, have, best2);
            printf("file|%s|%d|%d|%s|%lld|%d|%d|%d\n", w.week, have[2], back[2].twist, back[2].week, back[2].week_end,
                   back[2].floor, back[2].health_pct, best2[2]);
        }
    }
    {   /* what the game refuses or cannot play */
        TowerWeekly w;
        printf("bad|%d|%d|%d\n", towers_weekly_parse("enabled=0\nweek=2026-W47\n", &w), towers_weekly_parse("", &w),
               towers_weekly_parse("enabled=1\nweek=2026-W47\nseed=1\nfloors=2\nvenues=1\nfloor=1|3|1|0|0\n", &w));
        towers_weekly_parse("enabled=1\nweek=2026-W47\ntwist=moon_gravity|MOON\nrules=1\nseed=1\nfloors=2\nvenues=1\n"
                            "floor=1|3|1|0|0\nfloor=2|58|1|0|1\n", &w);
        printf("unknown|%d|%d|%d|", w.valid, w.twist, towers_new_weekly_run(&run, &w, 55, 0));
        w.twist = TOWER_TWIST_DOUBLE_DAMAGE;
        w.rules = 2;
        printf("%d|", towers_new_weekly_run(&run, &w, 55, 0));
        w.rules = 1;
        printf("%d\n", towers_new_weekly_run(&run, &w, 55, 0));
    }
    {   /* a Weekly run without its week is dropped (written by a build that did not know it) */
        TowerRun back[TOWER_COUNT];
        int have[TOWER_COUNT], best[TOWER_COUNT];
        char *p;
        towers_file_write((const TowerRun *const[TOWER_COUNT]){ NULL, NULL, &run, NULL, NULL }, (int[TOWER_COUNT]){ 0 }, text,
                          (int)sizeof text);
        p = strstr(text, "\"week\":\"");
        if (p) p[8] = '"';      /* "week":"" */
        towers_file_read(text, back, have, best);
        printf("noweek|%d\n", have[2]);
    }
    for (i = 1; i < TOWER_TWIST_COUNT; i++)
        printf("twist|%d|%s|%d|%d\n", i, towers_twist_key(i), towers_twist_id(towers_twist_key(i)), towers_twist_text(i)[0] != 0);
    {
        char a1[16], a2[16], a3[16], a4[16];
        towers_iso_week(1794787200LL, a1, 16);                  /* Monday 2026-11-16 00:00 UTC */
        towers_iso_week(1794787200LL - 7 * 86400LL, a2, 16);
        towers_iso_week(1609459200LL, a3, 16);                  /* 2021-01-01: ISO 2020-W53 */
        towers_iso_week(1798675199LL, a4, 16);                  /* 2026-12-31 23:59:59: 2026-W53 */
        printf("iso|%s|%s|%s|%s\n", a1, a2, a3, a4);
    }
    printf("climber|%d|%d|%d\n", towers_climber_ok(TOWER_WEEKLY, 55, 0), towers_climber_ok(TOWER_WEEKLY, TOWER_CREATED_FIGHTER, 0),
           towers_climber_ok(TOWER_WEEKLY, 1000, 0));
    {   /* the other towers keep their rules: no twist fields set */
        TowerFloorRules r;
        TowerAiFloor x, y;
        towers_floor_rules(TOWER_UPTOWN, 3, 72, &r);
        towers_ai_floor(TOWER_UPTOWN, 3, 72, &x);
        towers_ai_floor_ex(TOWER_UPTOWN, 3, 72, 0, &y);
        printf("others|%.2f|%.2f|%d|%d|%d|%d\n", r.hit_scale, r.max_health, r.momentum_hold, r.carry_health,
               memcmp(&x, &y, sizeof x) == 0, towers_is_boss(TOWER_WEEKLY, 20, 20) + towers_is_boss(TOWER_WEEKLY, 19, 20));
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def out():
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = Path(tempfile.mkdtemp(prefix='weekly'))
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'weekly.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(HOOKS), str(folder / 'driver.c'),
                            str(HOOKS / 'towers_rules.c'), str(HOOKS / 'towers_ai_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    files = []
    for week, body in WEEKS.items():
        p = folder / (week + '.txt')
        p.write_text(''.join(body) + 'now=1795295421\n', encoding='utf-8', newline='\n')
        files.append(str(p))
    return subprocess.run([str(exe)] + files, capture_output=True, check=True).stdout.decode('utf-8').splitlines()


def rows(out, tag, week=None):
    return [line.split('|')[1:] for line in out if line.startswith(tag + '|') and (week is None or line.split('|')[1] == week)]


def row(out, tag, week=None):
    return rows(out, tag, week)[0]


def order_of(week):
    return [int(l.split('|')[1]) for l in WEEKS[week].splitlines() if l.startswith('floor=')]


def field(week, key):
    return next(l.split('=', 1)[1].strip() for l in WEEKS[week].splitlines() if l.startswith(key + '='))


def test_every_week_reads_as_the_service_wrote_it(out):
    twists = {}
    for week in WEEKS:
        w = row(out, 'week', week)
        assert w[1] == '1', week
        assert w[2] == field(week, 'twist').split('|')[0] and w[3] == field(week, 'twist').split('|')[1]
        assert w[5] == '1' and w[6] == field(week, 'seed') and w[7] == '20'
        assert w[10] == field(week, 'roster') and w[11] == field(week, 'spare')
        assert w[12] == field(week, 'venues').replace('|', ',')
        assert row(out, 'hash', week)[1] == field(week, 'roster')     # the order and the spare, as the service hashes them
        twists[w[2]] = int(w[4])
    assert sorted(twists) == sorted(['low_health', 'double_damage', 'cpu_momentum', 'glass_jaw', 'all_bosses', 'one_venue'])
    assert all(v > 0 for v in twists.values())


def test_the_run_is_the_weeks_order_and_venues(out):
    for week in WEEKS:
        order = order_of(week)
        spare = int(field(week, 'spare'))
        for r in rows(out, 'run', week):
            climber, n, twist, floor = int(r[1]), int(r[2]), int(r[3]), int(r[4])
            assert n == 20 and floor == 1 and twist > 0 and r[5] == week and r[6] == field(week, 'ends')
            opp = [int(x) for x in r[7].split(',')]
            want = [spare if x == climber else x for x in order]    # the service's OrderFor
            assert opp == want, (week, climber)
            assert climber not in opp
            assert r[8].split(',') == ['1'] * 20, (week, 'a floor venue differs from the service')
            assert opp[-1] == 58 or climber == 58               # Crow at the top
    # 2026-W42 has fighter 55 on floor 10: the default climber meets the spare there
    r = rows(out, 'run', '2026-W42')[0]
    assert int(r[7].split(',')[9]) == int(field('2026-W42', 'spare'))


def test_the_twists_in_the_fight_rules(out):
    def rules(week):
        return {int(r[1]): r for r in rows(out, 'rules', week)}
    for week in WEEKS:
        tw = field(week, 'twist').split('|')[0]
        for f, r in rules(week).items():
            level, boss, hit, maxh, hold, carry, run_boss, srv_level, ai_boss, ai_on = (r[2], r[3], r[6], r[7], r[8], r[9], r[10],
                                                                                        r[11], r[12], r[13])
            assert hit == ('2.00' if tw == 'double_damage' else '1.00')
            assert maxh == ('0.50' if tw == 'glass_jaw' else '1.00')
            assert hold == ('1' if tw == 'cpu_momentum' else '0')
            assert carry == ('1' if tw == 'low_health' else '0')
            want_boss = tw == 'all_bosses' or f == 20
            assert boss == run_boss == ai_boss == ('1' if want_boss else '0'), (week, f)
            if want_boss:
                assert level == '2' and ai_on == '1'            # the boss AI level and the boss extras
            else:
                assert level == srv_level, (week, f)            # the service's TowerCPULevel otherwise
    assert rules('2026-W46')[1][2] == '0'                       # Home Turf: an ordinary floor 1
    assert rules('2026-W45')[1][4] != '1.00'                    # Boss Rush: the boss extras from floor 1


def test_the_run_file_keeps_the_week(out):
    for week in WEEKS:
        have, twist, wk, end, floor, health, best = row(out, 'file', week)[1:]
        assert (have, wk, end, floor, health, best) == ('1', week, field(week, 'ends'), '6', '41', '7')
        assert int(twist) > 0
    assert row(out, 'noweek') == ['0']


def test_what_the_game_cannot_play(out):
    assert row(out, 'bad') == ['0', '0', '0']
    assert row(out, 'unknown') == ['1', '0', '0', '0', '2']      # an unknown twist, another rules version: no climb


def test_twists_weeks_and_climbers(out):
    tw = rows(out, 'twist')
    assert len(tw) == 6 and all(r[2] == r[0] and r[3] == '1' for r in tw)
    assert row(out, 'iso') == ['2026-W47', '2026-W46', '2020-W53', '2026-W53']
    assert row(out, 'climber') == ['1', '0', '0']               # stock climbers only, as the service's board
    assert row(out, 'others') == ['1.00', '1.00', '0', '0', '1', '1']


def test_the_game_side_is_wired():
    tw = (HOOKS / 'towers.c').read_text(encoding='utf-8')
    assert '"/api/towers/weekly?format=text"' in tw and 'TOWER_WEEKLY_RULES, g_start_week' in tw
    assert 'towers_run_floor_rules(&g_run, g_run.floor, &g_rules)' in tw
    assert 'towers_ai_boss_all(g_run.tower == TOWER_WEEKLY && g_run.twist == TOWER_TWIST_ALL_BOSSES)' in tw
    assert 'g_rules.hit_scale' in tw and 'g_rules.max_health' in tw and 'g_rules.momentum_hold' in tw
    assert 'RECOMP_TOWERS_OFFLINE' in tw[tw.index('static int weekly_allowed'):tw.index('static int weekly_fetch')]
    ui = (HOOKS / 'towers_ui.inc').read_text(encoding='utf-8')
    assert 'tw_weekly_refresh();' in ui and 'COMING SOON' not in ui
    assert 'tower=3' in ui      # the Weekly's board by its number (the all-boards answer leaves it out)


WEEKS = {
    '2026-W42': (
        'enabled=1\n'
        'week=2026-W42\n'
        'name=The Weekly 2026-W42: HEAVY HANDS\n'
        'twist=double_damage|HEAVY HANDS\n'
        'rules=1\n'
        'seed=33094566\n'
        'floors=20\n'
        'starts=1791763200\n'
        'ends=1792368000\n'
        'roster=C3BBACB2\n'
        'spare=22\n'
        'venues=1|3|10|11|14|17\n'
        'floor=1|91|11|0|0\n'
        'floor=2|7|3|0|0\n'
        'floor=3|24|10|1|0\n'
        'floor=4|77|17|1|0\n'
        'floor=5|57|3|1|0\n'
        'floor=6|50|17|1|0\n'
        'floor=7|74|3|2|0\n'
        'floor=8|67|14|2|0\n'
        'floor=9|14|11|2|0\n'
        'floor=10|55|10|2|0\n'
        'floor=11|2|3|2|0\n'
        'floor=12|21|14|2|0\n'
        'floor=13|10|3|2|0\n'
        'floor=14|76|1|2|0\n'
        'floor=15|20|14|2|0\n'
        'floor=16|26|10|2|0\n'
        'floor=17|93|1|2|0\n'
        'floor=18|16|3|2|0\n'
        'floor=19|75|3|2|0\n'
        'floor=20|58|17|2|1\n'
    ),
    '2026-W43': (
        'enabled=1\n'
        'week=2026-W43\n'
        "name=The Weekly 2026-W43: BLAZIN' HOUR\n"
        "twist=cpu_momentum|BLAZIN' HOUR\n"
        'rules=1\n'
        'seed=49872185\n'
        'floors=20\n'
        'starts=1792368000\n'
        'ends=1792972800\n'
        'roster=3A17B127\n'
        'spare=33\n'
        'venues=8|9|10|11|17|21\n'
        'floor=1|2|17|0|0\n'
        'floor=2|85|21|0|0\n'
        'floor=3|19|8|1|0\n'
        'floor=4|11|17|1|0\n'
        'floor=5|10|21|1|0\n'
        'floor=6|23|8|1|0\n'
        'floor=7|22|9|2|0\n'
        'floor=8|21|10|2|0\n'
        'floor=9|67|11|2|0\n'
        'floor=10|18|21|2|0\n'
        'floor=11|69|8|2|0\n'
        'floor=12|61|17|2|0\n'
        'floor=13|62|21|2|0\n'
        'floor=14|44|17|2|0\n'
        'floor=15|7|9|2|0\n'
        'floor=16|93|10|2|0\n'
        'floor=17|72|21|2|0\n'
        'floor=18|0|9|2|0\n'
        'floor=19|1|10|2|0\n'
        'floor=20|58|11|2|1\n'
    ),
    '2026-W44': (
        'enabled=1\n'
        'week=2026-W44\n'
        'name=The Weekly 2026-W44: GLASS JAW\n'
        'twist=glass_jaw|GLASS JAW\n'
        'rules=1\n'
        'seed=66649804\n'
        'floors=20\n'
        'starts=1792972800\n'
        'ends=1793577600\n'
        'roster=112CBA62\n'
        'spare=64\n'
        'venues=3|4|15|18|24|25\n'
        'floor=1|2|25|0|0\n'
        'floor=2|46|24|0|0\n'
        'floor=3|67|15|1|0\n'
        'floor=4|47|25|1|0\n'
        'floor=5|11|18|1|0\n'
        'floor=6|76|15|1|0\n'
        'floor=7|35|24|2|0\n'
        'floor=8|93|25|2|0\n'
        'floor=9|59|3|2|0\n'
        'floor=10|25|18|2|0\n'
        'floor=11|27|15|2|0\n'
        'floor=12|70|3|2|0\n'
        'floor=13|68|4|2|0\n'
        'floor=14|22|15|2|0\n'
        'floor=15|77|18|2|0\n'
        'floor=16|92|3|2|0\n'
        'floor=17|69|4|2|0\n'
        'floor=18|20|4|2|0\n'
        'floor=19|73|24|2|0\n'
        'floor=20|58|4|2|1\n'
    ),
    '2026-W45': (
        'enabled=1\n'
        'week=2026-W45\n'
        'name=The Weekly 2026-W45: BOSS RUSH\n'
        'twist=all_bosses|BOSS RUSH\n'
        'rules=1\n'
        'seed=83427423\n'
        'floors=20\n'
        'starts=1793577600\n'
        'ends=1794182400\n'
        'roster=F18783C0\n'
        'spare=47\n'
        'venues=5|8|10|11|15|23\n'
        'floor=1|14|23|0|1\n'
        'floor=2|45|8|0|1\n'
        'floor=3|57|10|1|1\n'
        'floor=4|54|23|1|1\n'
        'floor=5|43|5|1|1\n'
        'floor=6|62|8|1|1\n'
        'floor=7|34|11|2|1\n'
        'floor=8|48|10|2|1\n'
        'floor=9|4|8|2|1\n'
        'floor=10|23|23|2|1\n'
        'floor=11|38|8|2|1\n'
        'floor=12|21|23|2|1\n'
        'floor=13|92|11|2|1\n'
        'floor=14|76|15|2|1\n'
        'floor=15|31|10|2|1\n'
        'floor=16|16|11|2|1\n'
        'floor=17|70|23|2|1\n'
        'floor=18|8|15|2|1\n'
        'floor=19|10|8|2|1\n'
        'floor=20|58|10|2|1\n'
    ),
    '2026-W46': (
        'enabled=1\n'
        'week=2026-W46\n'
        'name=The Weekly 2026-W46: HOME TURF\n'
        'twist=one_venue|HOME TURF\n'
        'rules=1\n'
        'seed=100205042\n'
        'floors=20\n'
        'starts=1794182400\n'
        'ends=1794787200\n'
        'roster=E197CA9E\n'
        'spare=59\n'
        'venues=5\n'
        'floor=1|2|5|0|0\n'
        'floor=2|36|5|0|0\n'
        'floor=3|38|5|1|0\n'
        'floor=4|23|5|1|0\n'
        'floor=5|27|5|1|0\n'
        'floor=6|68|5|1|0\n'
        'floor=7|19|5|2|0\n'
        'floor=8|33|5|2|0\n'
        'floor=9|46|5|2|0\n'
        'floor=10|74|5|2|0\n'
        'floor=11|34|5|2|0\n'
        'floor=12|47|5|2|0\n'
        'floor=13|31|5|2|0\n'
        'floor=14|77|5|2|0\n'
        'floor=15|12|5|2|0\n'
        'floor=16|54|5|2|0\n'
        'floor=17|28|5|2|0\n'
        'floor=18|26|5|2|0\n'
        'floor=19|7|5|2|0\n'
        'floor=20|58|5|2|1\n'
    ),
    '2026-W47': (
        'enabled=1\n'
        'week=2026-W47\n'
        'name=The Weekly 2026-W47: IRON MAN\n'
        'twist=low_health|IRON MAN\n'
        'rules=1\n'
        'seed=116982661\n'
        'floors=20\n'
        'starts=1794787200\n'
        'ends=1795392000\n'
        'roster=6E685675\n'
        'spare=45\n'
        'venues=4|8|9|18|21|27\n'
        'floor=1|64|27|0|0\n'
        'floor=2|14|9|0|0\n'
        'floor=3|56|18|1|0\n'
        'floor=4|57|21|1|0\n'
        'floor=5|23|9|1|0\n'
        'floor=6|6|4|1|0\n'
        'floor=7|28|8|2|0\n'
        'floor=8|61|9|2|0\n'
        'floor=9|22|18|2|0\n'
        'floor=10|36|9|2|0\n'
        'floor=11|44|27|2|0\n'
        'floor=12|7|9|2|0\n'
        'floor=13|54|18|2|0\n'
        'floor=14|33|4|2|0\n'
        'floor=15|72|27|2|0\n'
        'floor=16|8|18|2|0\n'
        'floor=17|70|27|2|0\n'
        'floor=18|85|21|2|0\n'
        'floor=19|21|4|2|0\n'
        'floor=20|58|9|2|1\n'
    ),
}
