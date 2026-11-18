"""Build 96: character groups in CHOOSE FIGHTER's COMMUNITY tab (src/hooks/char_groups.c, compiled natively; needs
MSVC, e.g. after scripts/env.ps1, and skips without it), plus the wiring: content.ini's keys, the hub's one card a
group, the version window, the remembered choice, the search, RANDOM, and nothing of it online."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "char_groups.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void)
{
    char k[CG_NAME], a[CG_NAME], b[CG_NAME];
    /* the key: case, spacing and punctuation folded */
    char_group_key("Spider-Man", a, sizeof a);
    char_group_key("  spider man ", b, sizeof b);
    CHECK(!strcmp(a, "spiderman") && !strcmp(a, b));
    char_group_key("Lil' Wayne", k, sizeof k);
    CHECK(!strcmp(k, "lilwayne"));
    char_group_key("", k, sizeof k);
    CHECK(k[0] == 0);
    char_group_key(NULL, k, sizeof k);
    CHECK(k[0] == 0);
    char_group_key("J\xC3\xBCrgen", k, sizeof k);          /* UTF-8 letters kept */
    CHECK(!strcmp(k, "j\xC3\xBCrgen"));
    char_group_key("abcdefgh", k, 4);
    CHECK(!strcmp(k, "abc"));
    /* groups: 5 Spider-Man, 2 Lil Wayne, 1 Doc, 2 ungrouped */
    {
        const char *keys[10] = { "spiderman", "", "lilwayne", "spiderman", "doc", "spiderman", "lilwayne", "",
                                 "spiderman", "spiderman" };
        int leader[10], count[10];
        CHECK(char_groups_assign(keys, 10, leader, count) == 2);       /* the group of 1 is no group */
        CHECK(leader[0] == 0 && leader[3] == 0 && leader[9] == 0 && count[0] == 5 && count[5] == 5);
        CHECK(leader[2] == 2 && leader[6] == 2 && count[6] == 2);
        CHECK(leader[4] == 4 && count[4] == 1);
        CHECK(leader[1] == -1 && leader[7] == -1 && count[1] == 1);
        CHECK(char_groups_assign(keys, 0, leader, count) == 0);
    }
    /* the card's face: the remembered version, else the newest */
    {
        int orders[3] = { 4, 9, 7 };
        CHECK(char_group_face(orders, 3, -1) == 1);
        CHECK(char_group_face(orders, 3, 2) == 2);
        CHECK(char_group_face(orders, 3, 5) == 1);
        CHECK(char_group_face(orders, 0, -1) == -1);
        int same[2] = { 3, 3 };
        CHECK(char_group_face(same, 2, -1) == 0);
    }
    /* the version label */
    CHECK(!strcmp(char_group_version_label("Noir", "Spidey Noir", "Spidey"), "Noir"));
    CHECK(!strcmp(char_group_version_label("", "Spidey Noir", "Spidey"), "Spidey Noir"));
    CHECK(!strcmp(char_group_version_label(NULL, "", "Spidey"), "Spidey"));
    CHECK(!strcmp(char_group_version_label("", "", ""), "?"));
    /* character-choices.ini */
    {
        char key[CG_NAME], pack[96];
        CHECK(char_groups_parse_choice("group.spiderman=studio-spidey-noir\r\n", key, sizeof key, pack, sizeof pack));
        CHECK(!strcmp(key, "spiderman") && !strcmp(pack, "studio-spidey-noir"));
        CHECK(char_groups_parse_choice("group.lilwayne=", key, sizeof key, pack, sizeof pack) && pack[0] == 0);
        CHECK(!char_groups_parse_choice("; a comment", key, sizeof key, pack, sizeof pack));
        CHECK(!char_groups_parse_choice("group.=x", key, sizeof key, pack, sizeof pack));
        CHECK(!char_groups_parse_choice("fighter.5=x", key, sizeof key, pack, sizeof pack));
    }
    if (!fails) printf("OK\n");
    return fails ? 1 : 0;
}
'''


def test_char_groups_pure(tmp_path):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native character groups test needs MSVC')
    (tmp_path / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = tmp_path / 'cg.exe'
    r = subprocess.run([compiler, '/nologo', '/W4', '/WX', '/TC', '/I' + str(HOOKS), str(tmp_path / 'driver.c'),
                        str(HOOKS / 'char_groups.c'), '/Fe:' + str(exe)], cwd=tmp_path, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0 and run.stdout.strip() == 'OK', run.stdout


def src(name):
    return (ROOT / name).read_text(encoding='utf-8')


def test_content_ini_keys_reach_the_game():
    pc = src('src/hooks/pc_content.c')
    assert 'CONTENT_TEXT(character); CONTENT_TEXT(version_name);' in pc
    h = src('src/hooks/pc_content.h')
    assert 'char character[64];' in h and 'char version_name[64];' in h
    game = src('src/hooks/char_groups_game.c')
    assert 'p->skin' in game and 'strcmp(p->type, "new-fighter")' in game     # skins and other packs never group
    assert 'character-choices.ini' in game and 'RECOMP_TEST_CHAR_GROUPS' in game


def test_the_hub_shows_one_card_a_group_and_the_version_window():
    screen = src('src/hooks/online_screen.cpp')
    body = screen.split('void draw_fighters(', 1)[1].split('\n}\n', 1)[0]
    assert 'char_groups_find(list[i].id)' in body and 'members.size() < 2' in body     # a group of one is a plain card
    assert 'if (!hidden(i)) base.push_back(i);' in body
    assert 'for (int i : searching ? vis : pool)' in body       # RANDOM: every version, not one card a group
    assert 'group_matches(i)' in body and 'char_group_label(m)' in body     # the search finds groups and versions
    assert 'char_group_window(' in body and 'char_groups_remember(g.key.c_str(), m->id)' in body
    # what is picked is the concrete version's place or number, as from a single card (nothing new online)
    assert '*fighter = m->fe_id;' in body
    ui = src('src/hooks/char_groups_ui.inc')
    assert '"CHOOSE A VERSION"' in ui and 'm->credits' in ui and 'm->authors' in ui and 'portrait(dl, f, m->fe_id' in ui
    # numbered versions none of which is on the shelf still get their group's card
    grid = screen.split('const std::vector<OnlineFighter> &grid_fighters()', 1)[1].split('\n}\n', 1)[0]
    assert 'char_groups_fighters(&all)' in grid


def test_groups_stay_out_of_the_netplay_code():
    for name in ('netplay.c', 'netplay_protocol.h', 'automatch.c'):
        path = HOOKS / name
        if path.exists():
            assert 'char_group' not in path.read_text(encoding='utf-8', errors='replace'), name
    for c in re.findall(r'char_groups_\w+', src('src/hooks/pc_content.c')):
        pytest.fail('the content print must not know the groups: ' + c)
