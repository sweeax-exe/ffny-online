"""The fight overlays' rules (src/hooks/fight_hud_rules.c, build 76), compiled natively: the PERFECT
tracker fed health samples step by step (a clean win, a hit, health won back, a Team side, a draw,
steps run again by rollback that take a loss or a decision back) and the name tags' places on the
HUD (2 to 4 fighters, 4:3, stretched, widescreen at the edges and in the centred 16:9 box, any
window size) and their text. Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1); skips without it."""
import re
import shutil
import subprocess

import pytest

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "fight_hud_rules.h"

static FightPerfect P;

/* one step: the health of four slots, the decision */
static void step(unsigned n, unsigned active, float h0, float h1, float h2, float h3, int decided, unsigned winners)
{
    FightSample s;
    memset(&s, 0, sizeof(s));
    s.step = n; s.active = active;
    s.health[0] = h0; s.health[1] = h1; s.health[2] = h2; s.health[3] = h3;
    s.decided = decided; s.winners = winners;
    fight_perfect_sample(&P, &s);
}

static void tag(const char *label, float w, float h, float wide, int stretch, int centred, int fighters, int slot)
{
    FightPicture pic;
    FightTag t;
    int ok;
    pic.w = w; pic.h = h; pic.wide = wide; pic.stretch = stretch; pic.hud_centred = centred;
    memset(&t, 0, sizeof(t));
    ok = fight_tag_place(&pic, fighters, slot, &t);
    printf("tag|%s|%d|%d|%.3f|%.3f|%d|%.3f|%.3f\n", label, slot, ok, t.x, t.y, t.right, t.size, t.width);
}

int main(void)
{
    char out[64];
    float nan_value;
    unsigned i;
    {
        unsigned bits = 0x7FC00000u;
        memcpy(&nan_value, &bits, sizeof(nan_value));
    }

    /* a clean win: slot 0 never hit, slot 1 knocked out */
    fight_perfect_reset(&P);
    printf("clean|%u", fight_perfect_mask(&P));
    step(10, 3, 100, 100, 0, 0, 0, 0);
    for (i = 11; i < 50; i++) step(i, 3, 100, 100.0f - (float)(i - 10) * 2.5f, 0, 0, 0, 0);
    printf("|%u", fight_perfect_mask(&P));                  /* not decided yet */
    step(50, 3, 100, 0, 0, 0, 1, 1);
    printf("|%u|%d|%d\n", fight_perfect_mask(&P), (int)P.lost_step[0], (int)P.lost_step[1]);

    /* the winner took one hit: no perfect, also when the health came back */
    fight_perfect_reset(&P);
    step(1, 3, 100, 100, 0, 0, 0, 0);
    step(2, 3, 99.5f, 100, 0, 0, 0, 0);
    step(3, 3, 100, 100, 0, 0, 0, 0);
    step(4, 3, 100, 0, 0, 0, 1, 1);
    printf("hit|%u|%d\n", fight_perfect_mask(&P), (int)P.lost_step[0]);

    /* the loser wins nothing; the other slot's perfect when it is the winner */
    fight_perfect_reset(&P);
    step(1, 3, 80, 120, 0, 0, 0, 0);                        /* unequal start values (a handicap) */
    step(2, 3, 40, 120, 0, 0, 0, 0);
    step(3, 3, 0, 120, 0, 0, 1, 2);
    printf("other|%u|%d|%d\n", fight_perfect_mask(&P), fight_perfect_under(&P, 1, 120), fight_perfect_under(&P, 1, 119));

    /* health above the start is no loss; a NaN is */
    fight_perfect_reset(&P);
    step(1, 3, 100, 100, 0, 0, 0, 0);
    step(2, 3, 130, 100, 0, 0, 0, 0);
    step(3, 3, 130, 0, 0, 0, 1, 1);
    printf("above|%u", fight_perfect_mask(&P));
    fight_perfect_reset(&P);
    step(1, 3, 100, 100, 0, 0, 0, 0);
    step(2, 3, nan_value, 100, 0, 0, 0, 0);
    step(3, 3, 100, 0, 0, 0, 1, 1);
    printf("|%u|%d\n", fight_perfect_mask(&P), fight_perfect_under(&P, 0, nan_value));

    /* a draw, and a winner that is not in the fight */
    fight_perfect_reset(&P);
    step(1, 3, 100, 100, 0, 0, 0, 0);
    step(2, 3, 100, 100, 0, 0, 1, 0);
    printf("draw|%u", fight_perfect_mask(&P));
    fight_perfect_reset(&P);
    step(1, 3, 100, 100, 0, 0, 0, 0);
    step(2, 3, 100, 100, 0, 0, 1, 4);
    printf("|%u\n", fight_perfect_mask(&P));

    /* Team: both of the winning side clean; then the partner hit */
    fight_perfect_reset(&P);
    step(1, 15, 100, 100, 100, 100, 0, 0);
    step(2, 15, 100, 100, 50, 20, 0, 0);
    step(3, 15, 100, 100, 0, 0, 1, fight_winner_mask(0, 1, 15));
    printf("team|%u", fight_perfect_mask(&P));
    fight_perfect_reset(&P);
    step(1, 15, 100, 100, 100, 100, 0, 0);
    step(2, 15, 100, 90, 50, 20, 0, 0);
    step(3, 15, 100, 90, 0, 0, 1, fight_winner_mask(0, 1, 15));
    printf("|%u\n", fight_perfect_mask(&P));
    printf("winners|%u|%u|%u|%u|%u|%u|%u\n", fight_winner_mask(0, 0, 3), fight_winner_mask(1, 0, 3),
           fight_winner_mask(2, 1, 15), fight_winner_mask(1, 1, 15), fight_winner_mask(3, 0, 15),
           fight_winner_mask(2, 0, 3), fight_winner_mask(-1, 0, 15));

    /* rollback: a mispredicted step shows the winner hit; the steps run again show it was not */
    fight_perfect_reset(&P);
    step(1, 3, 100, 100, 0, 0, 0, 0);
    step(2, 3, 100, 100, 0, 0, 0, 0);
    step(3, 3, 90, 100, 0, 0, 0, 0);                        /* predicted: a hit on slot 0 */
    step(4, 3, 90, 100, 0, 0, 0, 0);
    printf("rollback|%d", (int)P.lost_step[0]);
    step(3, 3, 100, 100, 0, 0, 0, 0);                       /* run again: blocked */
    step(4, 3, 100, 60, 0, 0, 0, 0);
    step(5, 3, 100, 0, 0, 0, 1, 1);
    printf("|%d|%u\n", (int)P.lost_step[0], fight_perfect_mask(&P));

    /* rollback the other way: the hit only shows in the steps run again, and earlier */
    fight_perfect_reset(&P);
    step(1, 3, 100, 100, 0, 0, 0, 0);
    step(2, 3, 100, 100, 0, 0, 0, 0);
    step(3, 3, 100, 100, 0, 0, 0, 0);
    step(2, 3, 95, 100, 0, 0, 0, 0);
    step(3, 3, 95, 100, 0, 0, 0, 0);
    step(4, 3, 95, 0, 0, 0, 1, 1);
    printf("rollback2|%d|%u\n", (int)P.lost_step[0], fight_perfect_mask(&P));

    /* rollback takes a decision back, and the fight is decided later for the other fighter */
    fight_perfect_reset(&P);
    step(1, 3, 100, 100, 0, 0, 0, 0);
    step(2, 3, 100, 0, 0, 0, 1, 1);
    printf("undecide|%u", fight_perfect_mask(&P));
    step(2, 3, 100, 100, 0, 0, 0, 0);
    printf("|%u|%d", fight_perfect_mask(&P), (int)P.decided_step);
    step(3, 3, 0, 100, 0, 0, 1, 2);
    step(4, 3, 0, 100, 0, 0, 1, 2);
    printf("|%u|%d\n", fight_perfect_mask(&P), (int)P.decided_step);

    /* rollback to the round's first step takes the start values again */
    fight_perfect_reset(&P);
    step(5, 3, 50, 100, 0, 0, 0, 0);
    step(6, 3, 50, 100, 0, 0, 0, 0);
    step(5, 3, 100, 100, 0, 0, 0, 0);
    step(6, 3, 100, 100, 0, 0, 0, 0);
    step(7, 3, 100, 0, 0, 0, 1, 1);
    printf("restart|%.0f|%u\n", P.start[0], fight_perfect_mask(&P));

    /* name tags: 640x480 and twice that; stretched; 16:9 at the edges; 21:9 centred and at the edges;
       a 4:3 picture in a 16:9 window; three and four fighters */
    for (i = 0; i < 2; i++) tag("4:3", 640, 480, 0, 0, 0, 2, (int)i);
    for (i = 0; i < 2; i++) tag("4:3x2", 1280, 960, 0, 0, 0, 2, (int)i);
    for (i = 0; i < 2; i++) tag("pillar", 1920, 1080, 0, 0, 0, 2, (int)i);
    for (i = 0; i < 2; i++) tag("stretch", 1920, 1080, 0, 1, 0, 2, (int)i);
    for (i = 0; i < 2; i++) tag("16:9", 1920, 1080, 16.0f / 9.0f, 0, 0, 2, (int)i);
    for (i = 0; i < 2; i++) tag("21:9c", 2560, 1080, 2560.0f / 1080.0f, 0, 1, 2, (int)i);
    for (i = 0; i < 2; i++) tag("21:9e", 2560, 1080, 2560.0f / 1080.0f, 0, 0, 2, (int)i);
    for (i = 0; i < 2; i++) tag("16:9c", 1920, 1080, 16.0f / 9.0f, 0, 1, 2, (int)i);
    for (i = 0; i < 2; i++) tag("letter", 1280, 1024, 16.0f / 9.0f, 0, 0, 2, (int)i);
    for (i = 0; i < 4; i++) tag("four", 640, 480, 0, 0, 0, 4, (int)i);
    for (i = 0; i < 4; i++) tag("three", 640, 480, 0, 0, 0, 3, (int)i);
    tag("two", 640, 480, 0, 0, 0, 2, 2);
    tag("none", 0, 0, 0, 0, 0, 2, 0);

    printf("text|%d|%s", fight_tag_text("Quel", 24, out, sizeof(out)), out);
    printf("|%d|%s", fight_tag_text("  AVeryLongOnlineNameOfThirtyChars  ", 12, out, sizeof(out)), out);
    printf("|%d|%s", fight_tag_text("a\tb\x01" "c", 24, out, sizeof(out)), out);
    printf("|%d", fight_tag_text("", 24, out, sizeof(out)));
    printf("|%d|%s", fight_tag_text("J\xC3\xBCrgen\xC3", 24, out, sizeof(out)), out);
    printf("|%d|%s", fight_tag_text("\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F" "abc", 4, out, sizeof(out)), out);
    printf("|%d|%s\n", fight_tag_text("abcdefghij", 24, out, 8), out);
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('fighthud')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'fighthud.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/fight_hud_rules.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return [line.split('|') for line in out.splitlines()]


def by(rows, kind):
    return [r[1:] for r in rows if r[0] == kind]


def tags(rows, label):
    """slot -> (ok, x, y, right, size, width)"""
    return {int(r[1]): (int(r[2]), float(r[3]), float(r[4]), int(r[5]), float(r[6]), float(r[7]))
            for r in by(rows, 'tag') if r[0] == label}


def test_a_round_won_without_losing_health_is_a_perfect(rows):
    # nothing before the decision; then the winner's bit; the loser's loss began at step 11
    assert by(rows, 'clean') == [['0', '0', '1', '-1', '11']]
    assert by(rows, 'other') == [['2', '0', '1']]
    assert by(rows, 'above') == [['1', '0', '1']]       # more health is fine; a NaN never is


def test_any_health_lost_by_the_winner_is_no_perfect(rows):
    assert by(rows, 'hit') == [['0', '2']]              # also with the health back at the start value
    assert by(rows, 'draw') == [['0', '0']]             # a draw; a winner that is not in the fight


def test_team_needs_both_of_the_winning_side(rows):
    assert by(rows, 'team') == [['3', '0']]
    # one on one, Team (slots 0, 1 against 2, 3), four-way, a slot not in the fight, no slot
    assert by(rows, 'winners') == [['1', '2', '12', '3', '8', '0', '0']]


def test_steps_run_again_by_rollback_replace_what_was_seen(rows):
    assert by(rows, 'rollback') == [['3', '-1', '1']]   # a predicted hit that never was: still a perfect
    assert by(rows, 'rollback2') == [['2', '0']]        # a hit only the second run shows: none
    assert by(rows, 'undecide') == [['1', '0', '-1', '2', '3']]   # later, the other fighter's clean win
    assert by(rows, 'restart') == [['100', '1']]


def test_tags_sit_over_the_fighter_names(rows):
    t = tags(rows, '4:3')
    assert t[0] == (1, 53.0, 24.0, 0, 11.0, 214.0)
    assert t[1] == (1, 589.0, 24.0, 1, 11.0, 214.0)
    t2 = tags(rows, '4:3x2')                            # everything scales with the window
    assert t2[0] == (1, 106.0, 48.0, 0, 22.0, 428.0) and t2[1][1] == 1178.0


def test_tags_follow_the_picture(rows):
    # a 4:3 picture in a 16:9 window: bars at the sides (1440 wide from x 240), the scale 2.25
    p = tags(rows, 'pillar')
    assert p[0][1] == pytest.approx(240 + 53 * 2.25) and p[1][1] == pytest.approx(240 + 589 * 2.25)
    assert p[0][2] == pytest.approx(24 * 2.25) and p[0][4] == pytest.approx(11 * 2.25)
    # stretched: the same layout over the whole width, the height's scale for the text
    s = tags(rows, 'stretch')
    assert s[0][1] == pytest.approx(53 * 3.0) and s[1][1] == pytest.approx(589 * 3.0)
    assert s[0][4] == pytest.approx(11 * 2.25) and s[0][5] == pytest.approx(214 * 3.0)
    # widescreen, HUD at the edges: the bars keep their 4:3 size at the picture's corners
    w = tags(rows, '16:9')
    assert w[0][1] == pytest.approx(53 * 2.25) and w[1][1] == pytest.approx(1920 - 51 * 2.25)
    assert w[0][5] == pytest.approx(214 * 2.25)
    assert tags(rows, '16:9c') == w                     # "centred" only matters for wider pictures
    # 21:9: in the centred 16:9 box (1920 wide from x 320), or at the edges
    c, e = tags(rows, '21:9c'), tags(rows, '21:9e')
    assert c[0][1] == pytest.approx(320 + 53 * 2.25) and c[1][1] == pytest.approx(2240 - 51 * 2.25)
    assert e[0][1] == pytest.approx(53 * 2.25) and e[1][1] == pytest.approx(2560 - 51 * 2.25)
    # a 16:9 picture letterboxed in a 5:4 window: 1280x720 from y 152
    lb = tags(rows, 'letter')
    assert lb[0][2] == pytest.approx(152 + 24 * 1.5) and lb[1][1] == pytest.approx(1280 - 51 * 1.5)


def test_three_and_four_fighters_use_the_bottom_row(rows):
    four = tags(rows, 'four')
    assert [four[i][0] for i in range(4)] == [1, 1, 1, 1]
    assert four[0][2] == four[1][2] and four[2][2] == four[3][2] > four[0][2]
    assert (four[2][1], four[2][3]) == (four[0][1], 0) and (four[3][1], four[3][3]) == (four[1][1], 1)
    three = tags(rows, 'three')
    assert [three[i][0] for i in range(4)] == [1, 1, 1, 0]
    assert tags(rows, 'two')[2][0] == 0 and tags(rows, 'none')[0][0] == 0


def test_tag_text(rows):
    assert by(rows, 'text') == [['4', 'Quel', '14', 'AVeryLongOnl..', '3', 'abc', '0', '7', 'Jürgen',
                                 '10', 'äöüß..', '6', 'abcd..']]


def test_wiring():
    """The overlays only read: the hooks, the settings and the report's field are where the docs say."""
    netplay = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8', errors='replace')
    # after netplay's own step work (a rollback done there), on every step, offline too
    assert re.search(r'void netplay_on_step\(void\)\s*\{\s*step_body\(\);\s*fight_hud_step\(\);\s*\}', netplay)
    assert re.search(r'void netplay_on_match_start\(void\)\s*\{\s*match_start_body\(\);\s*fight_hud_match_start\(\);\s*\}', netplay)
    glue = (ROOT / 'src/hooks/fight_hud.c').read_text(encoding='utf-8')
    # no write to guest memory: every MEM access is a read (no assignment, no address taken)
    assert not re.search(r'MEM(8|16|32)\([^;]*\)\s*(=[^=]|\+=|-=|\|=|&=|\+\+|--)', glue)
    assert '&MEM' not in glue and 'rand' not in glue
    settings = (ROOT / 'src/hooks/pc_settings.c').read_text(encoding='utf-8')
    for key, env in (('perfect_banner', 'RECOMP_PERFECT_BANNER'), ('name_tags', 'RECOMP_NAME_TAGS')):
        assert re.search(r'\{ "online", "%s", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "%s", 0,' % (key, env), settings)
        assert ('`%s`' % key) in (ROOT / 'docs/settings-reference.md').read_text(encoding='utf-8')
    ui = (ROOT / 'src/hooks/pc_ui.cpp').read_text(encoding='utf-8', errors='replace')
    assert ui.count('settings_group("online", { "hill_watch", "name_tags", "perfect_banner" });') == 2
    assert '!fighthud' in ui and 'online_screen::draw_fight_hud(' in ui
    assert 'fight_hud_report_perfect(winners)' in netplay and '\\"perfect\\":[' in netplay
