"""Build 90: CHOOSE FIGHTER's tabs (FFNY / COMMUNITY / CREATED on LT/RT) and fighting-style filter (Y)
(src/hooks/fighter_tabs.c, compiled natively; needs MSVC, e.g. after scripts/env.ps1, and skips without it), plus the
wiring in the online grid: styles from the game data, original fighters only, the remembered tab."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include "fighter_tabs.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void)
{
    const int all[3] = { 1, 1, 1 }, no_comm[3] = { 1, 0, 1 }, only_ffny[3] = { 1, 0, 0 }, no_ffny[3] = { 0, 1, 1 };
    /* LT/RT: the next offered tab that way, no wraparound */
    CHECK(fighter_tab_step(FTAB_FFNY, 1, all) == FTAB_COMMUNITY);
    CHECK(fighter_tab_step(FTAB_COMMUNITY, 1, all) == FTAB_CREATED);
    CHECK(fighter_tab_step(FTAB_CREATED, 1, all) == FTAB_CREATED);
    CHECK(fighter_tab_step(FTAB_FFNY, -1, all) == FTAB_FFNY);
    CHECK(fighter_tab_step(FTAB_CREATED, -1, all) == FTAB_COMMUNITY);
    /* COMMUNITY not offered (original fighters only): skipped both ways */
    CHECK(fighter_tab_step(FTAB_FFNY, 1, no_comm) == FTAB_CREATED);
    CHECK(fighter_tab_step(FTAB_CREATED, -1, no_comm) == FTAB_FFNY);
    CHECK(fighter_tab_step(FTAB_FFNY, 1, only_ffny) == FTAB_FFNY);
    CHECK(fighter_tab_step(FTAB_FFNY, 0, all) == FTAB_FFNY);
    /* the tab shown: the remembered one where offered, else FFNY */
    CHECK(fighter_tab_shown(FTAB_CREATED, all) == FTAB_CREATED);
    CHECK(fighter_tab_shown(FTAB_COMMUNITY, no_comm) == FTAB_FFNY);
    CHECK(fighter_tab_shown(FTAB_CREATED, only_ffny) == FTAB_FFNY);
    CHECK(fighter_tab_shown(7, all) == FTAB_FFNY && fighter_tab_shown(-1, all) == FTAB_FFNY);
    CHECK(fighter_tab_shown(FTAB_FFNY, no_ffny) == FTAB_COMMUNITY);
    /* styles: fighter.xml's names and a created fighter's two, letters only, any case */
    CHECK(fighter_style_matches("Street fighting", "Street fighting"));
    CHECK(fighter_style_matches("KICKBOXING / STREETFIGHTING", "Street fighting"));
    CHECK(fighter_style_matches("KICKBOXING / STREETFIGHTING", "Kickboxing"));
    CHECK(!fighter_style_matches("KICKBOXING / STREETFIGHTING", "Wrestling"));
    CHECK(fighter_style_matches("MARTIAL ARTS / KICKBOXING", "Martial arts"));
    CHECK(fighter_style_matches("Martial arts", "martial arts"));
    CHECK(!fighter_style_matches("Martial arts", "Submissions"));
    CHECK(!fighter_style_matches("WRESTLING / SUBMISSIONS", "Street fighting"));
    CHECK(!fighter_style_matches("", "Wrestling") && !fighter_style_matches(NULL, "Wrestling"));
    CHECK(fighter_style_matches("", NULL) && fighter_style_matches("Wrestling", "") && fighter_style_matches(NULL, " "));
    /* Y: All, each style, All */
    CHECK(fighter_style_next(0, 5) == 1 && fighter_style_next(4, 5) == 5 && fighter_style_next(5, 5) == 0);
    CHECK(fighter_style_next(0, 0) == 0 && fighter_style_next(9, 5) == 0);
    if (!fails) printf("OK\n");
    return fails ? 1 : 0;
}
'''


def test_tabs_and_styles(tmp_path):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fighter tabs test needs MSVC')
    (tmp_path / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = tmp_path / 'ft.exe'
    r = subprocess.run([compiler, '/nologo', '/W4', '/WX', '/TC', '/I' + str(HOOKS), str(tmp_path / 'driver.c'),
                        str(HOOKS / 'fighter_tabs.c'), '/Fe:' + str(exe)], cwd=tmp_path, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0 and run.stdout.strip() == 'OK', run.stdout


def src(name):
    return (ROOT / name).read_text(encoding='utf-8')


def test_styles_come_from_the_game_data():
    data = src('src/hooks/online_data.c')
    table = re.search(r'k_styles\[\]\[2\] = \{(.*?)\n\};', data, re.S).group(1)
    assert re.findall(r'\{ "([A-Z]{3})", "[^"]+" \}', table) == ['STF', 'KIC', 'MAR', 'WRE', 'SUB']
    styles = data.split('int online_styles(', 1)[1].split('\n}\n', 1)[0]
    assert 'g_fighters[k].style == k_styles[i][1]' in styles     # only the styles the dump's fighters have
    screen = src('src/hooks/online_screen.cpp')
    style = screen.split('std::string grid_style(', 1)[1].split('\n}\n', 1)[0]
    assert 'cards[cp].style' in style and 'e->template_fe_id' in style and 'list[i].style' in style
    assert 'Kickboxing' not in style and 'fighter_letter_jump' not in screen.split('void draw_fighters(', 1)[1]


def test_grid_wiring():
    screen = src('src/hooks/online_screen.cpp')
    fighters = screen.split('void draw_fighters(', 1)[1].split('// ---- the room ----', 1)[0]
    # LT/RT the tabs, Y the style, X a created card's online name (not Ctrl+F, the search)
    assert 'fighter_tab_step(tab, dir, offered)' in fighters and 'pad.lt || pad.rt' in fighters
    assert 'S.fstyle = fighter_style_next(S.fstyle, style_count)' in fighters
    assert 'pad.x && !ImGui::GetIO().KeyCtrl && cur_cp' in fighters and 'pad.y && cur_cp' not in fighters
    # original fighters only: COMMUNITY is not offered, nor found by the name search
    assert '!(t == FTAB_COMMUNITY && originals)' in fighters and 'offered[grid_tab(list, i)]' in fighters
    origin = screen.split('bool grid_originals_only(', 1)[1].split('\n}\n', 1)[0]
    assert 'standard_only(r.settings.fighter_rules)' in origin and 'search_original(NETPLAY_ORIGINAL_RANKED)' in origin
    # remembered, never by the gallery; RANDOM draws from the cards shown
    assert 'recomp_settings_set("online", "fighter_tab", to)' in fighters and '!gallery_on() &&' in fighters
    assert 'for (int i : vis) if (list[i].id != FIGHTER_RANDOM && !blocked(i)' in fighters
    read = screen.split('Pad read_pad()', 1)[1].split('\n}\n', 1)[0]
    assert 'ImGuiKey_GamepadL2, key(ImGuiKey_Z)' in read and 'ImGuiKey_GamepadR2, key(ImGuiKey_C)' in read
    assert '{ "online", "fighter_tab", RECOMP_SETTING_INT, 0, 0, 2,' in src('src/hooks/pc_settings.c')
    for page in ('fighters-tab-community', 'fighters-style', 'fighters-tab-created', 'fighters-tab-created-style',
                 'fighters-tab-original'):
        assert '"%s"' % page in screen
