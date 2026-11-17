"""Build 90, community content in offline play (settings.ini [content] community_offline, default on).

Off, a run leaves FFNY Online's synced community packs out (the game starts with the original data and the player's
own packs; online is refused until a restart with the content on, for that run only). The rules are in
src/hooks/content_update_logic.h, the state in src/hooks/pc_content.c, the restart in content_update.c, the prompt in
online_screen.cpp and the launcher's switch in pc_ui.cpp / pc_launcher_content.inc. Native parts need MSVC.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

RULES = r'''
#include <stdio.h>
#include <string.h>
#include "content_update_logic.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void)
{
    /* the decision: the setting is on by default; off leaves the community content out unless the run was started for online */
    CHECK(!cu_community_off(1, 0));
    CHECK(!cu_community_off(1, 1));
    CHECK(cu_community_off(0, 0));
    CHECK(!cu_community_off(0, 1));         /* the restart for online: on for this run only */
    CHECK(cu_online_allowed(0) && !cu_online_allowed(1));
    /* the restart for online needs no news, but never in a fight or a match */
    CuGate g; memset(&g, 0, sizeof g);
    CHECK(cu_gate(&g) == CU_NO_NEWS);
    g.force = 1; CHECK(cu_gate(&g) == CU_OK);
    g.in_fight = 1; CHECK(cu_gate(&g) == CU_IN_FIGHT); g.in_fight = 0;
    g.match_locked = 1; CHECK(cu_gate(&g) == CU_IN_MATCH); g.match_locked = 0;
    g.already = 1; CHECK(cu_gate(&g) == CU_BUSY); g.already = 0;
    CHECK(strstr(CU_ONLINE_PROMPT_TEXT, "Online needs FFNY Online's community content") != NULL);
    CHECK(strstr(CU_ONLINE_PROMPT_TEXT, "Restart with it now?") != NULL);
    printf(fails ? "FAILED %d\n" : "OK\n", fails);
    return fails != 0;
}
'''

# pc_content.c natively (the app flavour: no game redirect), with the real [content] settings
CONTENT = r'''
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "recomp_settings.h"
#include "pc_content.h"
#include "pc_language.h"
int main(int argc, char **argv)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    static RecompSetting settings[7];
    char names[256];
    int n, i, forced = argc > 1 && atoi(argv[1]);
    n = (int)pc_language_settings(settings, 7);
    recomp_settings_register(settings, (size_t)n);
    printf("registered %d\n", n);
    printf("before %d\n", pc_content_community_off());          /* the launcher never decides */
    n = pc_content_list(packs, PC_CONTENT_MAX);
    printf("launcher %d\n", n);
    pc_content_session_online(forced);
    pc_content_decide();
    printf("off %d allowed %d\n", pc_content_community_off(), pc_content_online_allowed());
    n = pc_content_list(packs, PC_CONTENT_MAX);
    printf("count %d\n", n);
    for (i = 0; i < n; i++)
        printf("%s|local=%d|applied=%d\n", packs[i].id, packs[i].local, packs[i].applied);
    printf("print %08X\n", (unsigned)pc_content_print());
    printf("news %d\n", pc_content_news(names, sizeof names));
    return 0;
}
'''

CONTENT_INI = """; FFNY Online community packs, written by setup/mods.py sync. Do not edit.
[content]
revision=5
[pack.studio-d-mob-2]
name=D-Mob 2
kind=content
type=new-fighter
version=0.1.0
content_sha256=aa
order=1
fighter_fe_id=101
composed=1
[pack.hd-textures]
name=HD textures
kind=replacement
type=fighter-texture
version=1.0.0
content_sha256=bb
order=2
[pack.deutsch]
name=Deutsch
kind=replacement
type=language
version=1.0.0
language=de
content_sha256=cc
order=3
[pack.my-venue]
name=My venue
kind=content
type=venue
version=0.0.1
content_sha256=dd
order=900
local=1
online_ok=0
composed=1
"""

NEWS_INI = "[news]\nrevision=9\npending=2\nnames=Doc Nine | The Block\n"


def files_ini(applied):
    return '[content-files]\nschema=1\napplied=%s\nfile=data/venue.xml\n' % ','.join(applied)


@pytest.fixture(scope='module')
def tools(tmp_path_factory):
    compiler = shutil.which('cl')
    if sys.platform != 'win32' or not compiler:
        pytest.skip('the native fixtures need MSVC on Windows')
    settings_c = ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c'
    if not settings_c.is_file():
        pytest.skip('tools/xboxrecomp is not checked out')
    out = tmp_path_factory.mktemp('contentoffline')
    (out / 'rules.c').write_text(RULES, encoding='utf-8')
    (out / 'content.c').write_text(CONTENT, encoding='utf-8')
    built = subprocess.run([compiler, '/nologo', '/W4', '/WX', '/TC', '/I' + str(HOOKS), str(out / 'rules.c'),
                            str(HOOKS / 'content_update_logic.c'), '/Fe:' + str(out / 'rules.exe')],
                           cwd=out, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    built = subprocess.run([compiler, '/nologo', '/O2', '/TC', '/DDEFJAM_APP=1', '/I' + str(HOOKS),
                            '/I' + str(settings_c.parent), str(out / 'content.c'), str(HOOKS / 'pc_content.c'),
                            str(HOOKS / 'pc_language_settings.c'), str(settings_c), '/Fe:' + str(out / 'content.exe')],
                           cwd=out, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def test_rules(tools):
    r = subprocess.run([str(tools / 'rules.exe')], capture_output=True, text=True)
    assert r.returncode == 0 and r.stdout.strip() == 'OK', r.stdout


def make_data(data):
    (data / 'mod-manager' / 'content-files').mkdir(parents=True)
    (data / 'mod-manager' / 'own-packs' / 'content-files').mkdir(parents=True)
    for pack in ('studio-d-mob-2', 'hd-textures', 'deutsch', 'my-venue'):
        (data / 'mods' / pack).mkdir(parents=True)
    (data / 'mod-manager' / 'content.ini').write_text(CONTENT_INI, encoding='utf-8')
    (data / 'mod-manager' / 'news.ini').write_text(NEWS_INI, encoding='utf-8')
    # the community composition holds the community fighter and the player's venue; the own one only the venue
    (data / 'mod-manager' / 'content-files' / 'files.ini').write_text(
        files_ini(['studio-d-mob-2', 'my-venue']), encoding='utf-8')
    (data / 'mod-manager' / 'own-packs' / 'content-files' / 'files.ini').write_text(
        files_ini(['my-venue']), encoding='utf-8')


def run(tools, data, setting=None, forced=0):
    env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    env['DEFJAM_DATA'] = str(data)
    env['RECOMP_SETTINGS'] = 'none'
    if setting is not None:
        env['RECOMP_CONTENT_OFFLINE'] = str(setting)
    got = subprocess.run([str(tools / 'content.exe'), str(forced)], capture_output=True, text=True, env=env)
    assert got.returncode == 0, got.stderr
    return {line.split(' ', 1)[0]: line.split(' ', 1)[1] if ' ' in line else '' for line in got.stdout.splitlines()
            if ' ' in line or '|' not in line}, got.stdout.splitlines()


def test_default_is_on_and_unchanged(tools, tmp_path):
    make_data(tmp_path / 'data')
    info, lines = run(tools, tmp_path / 'data')
    assert info['registered'] == '7'
    assert info['before'] == '0' and info['launcher'] == '4'
    assert info['off'] == '0 allowed 1'
    assert info['count'] == '4'
    assert 'studio-d-mob-2|local=0|applied=1' in lines
    assert info['print'] != '00000000'       # a community content print (online)
    assert info['news'] == '2'


def test_off_leaves_the_community_packs_out(tools, tmp_path):
    make_data(tmp_path / 'data')
    info, lines = run(tools, tmp_path / 'data', setting=0)
    assert info['launcher'] == '4'            # the launcher's Content page still lists everything
    assert info['off'] == '1 allowed 0'
    # only the player's own pack and the language pack remain; no community fighter, no retexture
    assert info['count'] == '2'
    assert 'deutsch|local=0|applied=0' in lines
    assert 'my-venue|local=1|applied=1' in lines     # applied from the own composition
    assert not any(l.startswith(('studio-d-mob-2', 'hd-textures')) for l in lines)
    assert info['print'] == '00000000'        # no community content print: never matching a room
    assert info['news'] == '0'                # nothing to update while it is off


def test_off_without_the_own_composition_applies_nothing_of_the_players(tools, tmp_path):
    make_data(tmp_path / 'data')
    (tmp_path / 'data' / 'mod-manager' / 'own-packs' / 'content-files' / 'files.ini').unlink()
    info, lines = run(tools, tmp_path / 'data', setting=0)
    assert 'my-venue|local=1|applied=0' in lines


def test_restart_for_online_turns_it_on_for_that_run_only(tools, tmp_path):
    make_data(tmp_path / 'data')
    info, lines = run(tools, tmp_path / 'data', setting=0, forced=1)
    assert info['off'] == '0 allowed 1'
    assert info['count'] == '4' and info['print'] != '00000000'


def src(name):
    return (ROOT / name).read_text(encoding='utf-8')


def test_setting_is_registered_default_on():
    s = src('src/hooks/pc_language_settings.c')
    assert 'ADD("content", "community_offline", RECOMP_SETTING_BOOL, 1,' in s
    assert 'RECOMP_CONTENT_OFFLINE' in s
    assert 'pc_language_settings(g_settings + g_setting_count, 7)' in src('src/hooks/pc_settings.c')


def test_online_is_refused_without_the_content():
    n = src('src/hooks/netplay.c')
    start = n[n.index('static int start('):]
    assert 'pc_content_online_allowed()' in start[:start.index('InitOnceExecuteOnce') + 1500]
    # the single entry of every room, hub and match connection; the check sits before the server address is used
    assert start.index('pc_content_online_allowed()') < start.index('N.server_text')
    o = src('src/hooks/online_screen.cpp')
    take = o[o.index('bool take_open_request()'):]
    assert 'pc_content_online_allowed()' in take[:take.index('connect_if_needed')]
    assert 'g_content_prompt_req = 1' in take[:take.index('connect_if_needed')]


def test_prompt_and_restart_are_wired():
    assert 'CU_ONLINE_PROMPT_TEXT' in src('src/hooks/online_screen.cpp')
    assert 'content_update_request_online(UP.where' in src('src/hooks/online_screen.cpp')
    ui = src('src/hooks/pc_ui.cpp')
    assert 'take_content_prompt_request()' in ui and 'update_prompt_begin_online(' in ui
    c = src('src/hooks/content_update.c')
    assert 'g->force = force;' in c and 'request(where, why, size, 1)' in c
    # a run started with --content-update has the content on, whatever the setting
    assert '--content-update' in src('src/hooks/pc_content.c') and 'g_session_on |= started_for_online()' in src('src/hooks/pc_content.c')


def test_report_and_log_header_say_the_setting():
    c = src('src/hooks/pc_content.c')
    assert 'community content in offline play: %s' in c
    assert c.count('mode_text()') >= 2          # the [CONTENT] header and the [REPORT] line of F9


def test_launcher_has_the_switch():
    assert '"Community content in offline play"' in src('src/hooks/pc_ui.cpp')
    assert 'settings_group("content", { "community_offline" })' in src('src/hooks/pc_ui.cpp')
    assert '##content_offline_switch' in src('src/hooks/pc_launcher_content.inc')


def test_own_packs_are_composed_apart():
    m = src('setup/mods.py')
    assert 'self.compose_own(off)' in m and "OWN_DIR = 'own-packs'" in m
    assert 'COMPOSED_OWN_DIR "mod-manager" HOST_SEP "own-packs" HOST_SEP "content-files"' in src('src/hooks/pc_content.c')
