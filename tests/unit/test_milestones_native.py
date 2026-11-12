"""Milestone progress (build 79, src/hooks/milestones_rules.c), compiled natively: the rows a profile slot's
bytes give (counters, thresholds and rewards as the game's 0x00014C20 and Game.GetMilestone have them), the
fraction, the number formatting, and the toast after a match. Plus the wiring: the panel reads a copy of the
profile and never writes guest memory, and the reward-pass hooks stay around the generated body.
Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1) for the native part; skips without it."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "milestones.h"

static unsigned char prof[MS_PROFILE_BYTES];

static void w32(unsigned o, unsigned v) { prof[o] = v & 255; prof[o + 1] = (v >> 8) & 255; prof[o + 2] = (v >> 16) & 255; prof[o + 3] = v >> 24; }
static void w16(unsigned o, unsigned v) { prof[o] = v & 255; prof[o + 1] = (v >> 8) & 255; }

static void dump(const char *tag, int fix, unsigned size)
{
    MilestoneRow r[MS_MAX_ROWS];
    int n = milestones_rows(prof, size, fix, r, MS_MAX_ROWS), i;
    char t[64];
    for (i = 0; i < n; i++) {
        milestones_progress_text(&r[i], t, sizeof(t));
        printf("%s|%d|%d|%s|%s|%u|%u|%u|%u|%d|%d|%d|%.3f|%s\n", tag, i, r[i].group, r[i].what, r[i].reward, r[i].count,
               r[i].target, r[i].count2, r[i].target2, r[i].reached, r[i].sure, r[i].note,
               milestones_fraction(&r[i]), t);
    }
    printf("%s-count|%d|%d\n", tag, n, milestones_reached(r, n));
}

int main(void)
{
    MilestoneRow a[MS_MAX_ROWS], b[MS_MAX_ROWS];
    int na, nb, k, i;
    char text[160], num[32];
    unsigned values[] = { 0, 7, 999, 1000, 3420, 100000000u, 4294967295u };
    for (i = 0; i < (int)(sizeof(values) / sizeof(values[0])); i++) {
        milestones_count_text(values[i], num, sizeof(num));
        printf("num|%u|%s\n", values[i], num);
    }
    memset(prof, 0, sizeof(prof));
    prof[0x140] = prof[0x141] = prof[0x142] = 0xFF;     /* a new profile: no style learned */
    dump("empty", 1, MS_PROFILE_BYTES);
    /* part of the way */
    w32(0x68, 3420); w32(0x6C, 5000); w16(0x70, 250); w16(0x72, 249); w16(0x74, 10); w16(0x76, 3);
    w16(0x80, 5); w32(0x78, 500); w32(0x7C, 99); w32(0x84, 25); w32(0x88, 251); w16(0x8C, 4); w16(0x82, 5);
    prof[0x8E] = 1;
    w32(0xFC, 60); w32(0x100, 50); w32(0x104, 50); w32(0x108, 50); w32(0x10C, 40); w32(0x110, (unsigned)-5);
    prof[0x140] = 0; prof[0x141] = 2; prof[0x142] = 4;
    for (i = 0; i < 38; i++) prof[0x4D8B + (i >> 3)] |= (unsigned char)(1u << (i & 7));
    prof[0x150] = 1;
    dump("some", 1, MS_PROFILE_BYTES);
    dump("retail", 0, MS_PROFILE_BYTES);
    dump("short", 1, 0x200);
    /* the toast: punches 99 -> 100, kicks 4999 -> 5000, wins 9 -> 10 */
    memset(prof, 0, sizeof(prof));
    w32(0x68, 99); w32(0x6C, 4999); w16(0x74, 9);
    for (i = 0; i < 39; i++) prof[0x4D8B + (i >> 3)] |= (unsigned char)(1u << (i & 7));
    nb = milestones_rows(prof, MS_PROFILE_BYTES, 1, b, MS_MAX_ROWS);
    w32(0x68, 100);
    na = milestones_rows(prof, MS_PROFILE_BYTES, 1, a, MS_MAX_ROWS);
    k = milestones_newly_reached(b, nb, a, na, text, sizeof(text));
    printf("toast1|%d|%s\n", k, text);
    w32(0x6C, 5000); w16(0x74, 10);
    na = milestones_rows(prof, MS_PROFILE_BYTES, 1, a, MS_MAX_ROWS);
    k = milestones_newly_reached(b, nb, a, na, text, sizeof(text));
    printf("toast3|%d|%s\n", k, text);
    k = milestones_newly_reached(a, na, a, na, text, sizeof(text));
    printf("toast0|%d|%s\n", k, text);
    return 0;
}
'''


@pytest.fixture(scope='module')
def out(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('milestones')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'milestones.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/milestones_rules.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    text = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return [line.split('|') for line in text.splitlines()]


def rows(out, tag):
    return [r for r in out if r[0] == tag]


def test_numbers_have_thousands_separators(out):
    nums = {int(r[1]): r[2] for r in out if r[0] == 'num'}
    assert nums == {0: '0', 7: '7', 999: '999', 1000: '1,000', 3420: '3,420', 100000000: '100,000,000',
                    4294967295: '4,294,967,295'}


def test_every_milestone_with_the_games_thresholds(out):
    r = rows(out, 'empty')
    # (group, target, reward) in order: 0x00014C20's fifteen stat tests, then Game.GetMilestone's four
    assert [(int(x[2]), int(x[6]), x[4]) for x in r] == [
        (0, 100, '+1 Upper Body'), (0, 5000, '+2 Upper Body'), (1, 100, '+1 Lower Body'), (1, 5000, '+2 Lower Body'),
        (2, 250, '+1 Speed'), (2, 250, '+1 Speed'), (3, 10, '+1 Toughness'), (3, 25, '+1 Toughness'),
        (4, 5, '+1 Health'), (4, 100, '+1 Health'),
        (5, 25, '+1 Charisma'), (5, 250, '+1 Charisma'), (5, 5, '+1 Charisma'), (5, 5, '+1 Charisma'),
        (5, 1, '+1 Charisma'),
        (6, 250, 'ID card badge'), (6, 3, 'ID card badge'), (6, 39, 'ID card badge'), (6, 1, 'ID card badge')]
    assert r[0][3] == 'Punches landed' and r[2][3] == 'Kicks landed'
    assert r[6][3] == 'Wins in fights with 3+ fighters'
    assert all(x[9] == '0' for x in r)                      # a fresh profile reaches nothing
    assert rows(out, 'empty-count')[0][1:] == ['19', '0']
    assert r[9][8] == '500'                                 # Health 2: two conditions, 100 and 500
    # the counters whose meaning is confirmed are marked sure (Speed, Health 2 and two Charisma rows confirmed by
    # players who know the retail rules, build 80); the inferred ones are not
    assert [x[10] for x in r[:15]] == ['1', '1', '1', '1', '1', '1', '1', '0', '1', '1', '1', '1', '0', '0', '0']


def test_progress_and_reached(out):
    r = rows(out, 'some')
    get = {int(x[1]): x for x in r}
    assert get[0][5] == '3420' and get[0][9] == '1' and get[1][9] == '0'           # 3,420 punches: 100 yes, 5000 no
    assert get[1][13] == '3,420 / 5,000' and abs(float(get[1][12]) - 0.684) < 0.001
    assert get[2][9] == '1' and get[3][9] == '1'                                   # 5000 kicks: both
    assert get[4][9] == '1' and get[5][9] == '0'                                   # speed 250 / 249
    assert get[6][9] == '1' and get[6][11] == '2'                                  # 10 wins, the fix's note
    assert get[9][9] == '0' and get[9][13] == '99 / 100  +  500 / 500'             # Health 2: 99 of 100 holds it back
    assert abs(float(get[9][12]) - 0.99) < 0.001
    assert get[8][9] == '1' and get[10][9] == '1' and get[11][9] == '1'
    assert get[12][9] == '0' and get[13][9] == '1' and get[14][9] == '1'
    # base stats: 60 counts as 50, -5 as 0: 50+50+50+50+40+0 = 240 of 250
    assert get[15][5] == '240' and get[15][9] == '0'
    assert get[16][5] == '3' and get[16][9] == '1'
    assert get[17][5] == '38' and get[17][9] == '0'
    assert get[18][9] == '1'
    assert rows(out, 'some-count')[0][1:] == ['19', '12']
    # the toughness fix off: the note says the game keeps only the last fight
    assert {int(x[1]): x for x in rows(out, 'retail')}[6][11] == '3'


def test_a_short_copy_gives_only_the_stat_rows(out):
    assert rows(out, 'short-count')[0][1] == '15'


def test_the_toast_names_what_this_match_reached(out):
    t1 = rows(out, 'toast1')[0]
    assert t1[1] == '1' and t1[2] == 'MILESTONE: Punches landed 100 - +1 Upper Body'
    t3 = rows(out, 'toast3')[0]
    assert t3[1] == '3' and t3[2] == 'MILESTONE: Punches landed 100 - +1 Upper Body  (+ 2 more)'
    t0 = rows(out, 'toast0')[0]
    assert t0[1] == '0' and t0[2] == ''                     # nothing new: no toast (and the ID card's badges never toast)


def test_the_game_side_only_reads():
    src = (ROOT / 'src/hooks/milestones.c').read_text(encoding='utf-8')
    body = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    # no store into guest memory: MEMn(...) never on the left of an assignment
    assert not re.search(r'MEM(8|16|32)\([^;]*\)\s*(=[^=]|\+=|-=|\|=|&=)', body)
    assert 'memcpy(buf, (const void *)XBOX_PTR(p)' in body
    ui = (ROOT / 'src/hooks/milestones_ui.inc').read_text(encoding='utf-8')
    assert 'MEM8(' not in ui and 'MEM32(' not in ui and 'XBOX_PTR' not in ui


def test_the_reward_pass_hooks():
    fix = (ROOT / 'src/patches/toughness_fix.c').read_text(encoding='utf-8')
    before = fix.index('void toughness_fix_before_rewards')
    after = fix.index('void toughness_fix_after_rewards')
    assert 'milestones_before_rewards(s_profile);' in fix[before:after]
    # after the fix's own add, so the toast sees the count the save will hold
    tail = fix[after:]
    assert tail.index('milestones_after_rewards(p);') > tail.index('MEM16(p + S_MULTI_WINS) = (uint16_t)sum;')


def test_the_page_is_offered_where_the_brief_says():
    src = (ROOT / 'src/hooks/online_screen.cpp').read_text(encoding='utf-8')
    assert 'options/battleID' in src and 'tory/crib' in src
    assert 'milestones_on_funccall(text);' in src
    ui = (ROOT / 'src/hooks/pc_ui.cpp').read_text(encoding='utf-8')
    assert 'online_screen::milestones_wanted()' in ui and 'online_screen::draw_milestones(io.DisplaySize)' in ui
    assert 'online_screen::milestone_toast(notice' in ui
