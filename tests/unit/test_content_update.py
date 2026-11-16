"""Build 86, UPDATE NOW for new community content: the rules (src/hooks/content_update_logic.c, built natively with
MSVC; skipped without it) and the wiring in the game, the launcher and the launcher's background watcher."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "content_update_logic.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define MIN 60000ull
int main(void)
{
    /* the gate: never in a fight, a match or a room's countdown */
    CuGate g; memset(&g, 0, sizeof g); g.news = 2;
    CHECK(cu_gate(&g) == CU_OK);
    g.in_fight = 1; CHECK(cu_gate(&g) == CU_IN_FIGHT); g.in_fight = 0;
    g.match_locked = 1; CHECK(cu_gate(&g) == CU_IN_MATCH); g.match_locked = 0;
    g.lockstep = 1; CHECK(cu_gate(&g) == CU_IN_MATCH); g.lockstep = 0;
    g.room_phase = 2; CHECK(cu_gate(&g) == CU_IN_MATCH);
    g.room_phase = 1; CHECK(cu_gate(&g) == CU_COUNTDOWN); g.room_phase = 0;
    g.hill_busy = 1; CHECK(cu_gate(&g) == CU_COUNTDOWN); g.hill_busy = 0;
    g.already = 1; CHECK(cu_gate(&g) == CU_BUSY); g.already = 0;
    g.news = 0; CHECK(cu_gate(&g) == CU_NO_NEWS);
    g.news = 0; g.in_fight = 1; CHECK(cu_gate(&g) == CU_IN_FIGHT);   /* a fight first, whatever the news */
    CHECK(strlen(cu_refusal_text(CU_IN_FIGHT)) > 0 && strlen(cu_refusal_text(CU_OK)) == 0);

    /* the popup: once per news on the main menu, again after a fight; LATER snoozes 30 minutes */
    CuOffer o; memset(&o, 0, sizeof o);
    unsigned long long t = 1000;
    CHECK(!cu_offer_popup_due(&o, 0, 1, t));
    CHECK(!cu_offer_popup_due(&o, 2, 0, t));                 /* not off the main menu */
    CHECK(cu_offer_popup_due(&o, 2, 1, t));                  /* news it has not asked about */
    cu_offer_answered(&o, 2, 1, t);                          /* LATER */
    CHECK(!cu_offer_popup_due(&o, 2, 1, t + 1));
    cu_offer_frame(&o, 1);                                   /* a fight during the snooze */
    CHECK(!cu_offer_popup_due(&o, 2, 1, t + 10 * MIN));      /* back on the main menu: still snoozed */
    CHECK(!cu_offer_popup_due(&o, 2, 1, t + 31 * MIN));      /* that fight was in the snooze: not asked */
    cu_offer_frame(&o, 1);                                   /* a fight after it */
    CHECK(!cu_offer_popup_due(&o, 2, 0, t + 40 * MIN));      /* still in the fight's menus */
    CHECK(cu_offer_popup_due(&o, 2, 1, t + 41 * MIN));       /* back on the main menu after a fight */
    cu_offer_answered(&o, 2, 0, t + 41 * MIN);               /* answered (not LATER) */
    CHECK(!cu_offer_popup_due(&o, 2, 1, t + 42 * MIN));      /* one time */
    CHECK(cu_offer_popup_due(&o, 3, 1, t + 43 * MIN));       /* a newer set: asked again */
    cu_offer_answered(&o, 3, 1, t + 43 * MIN);
    cu_offer_frame(&o, 1);
    CHECK(!cu_offer_popup_due(&o, 3, 1, t + 50 * MIN));      /* a fight in the snooze */

    /* the corner notice: once per news, in a menu, CU_TOAST_MS long; again when a snooze ran out */
    CuOffer c; memset(&c, 0, sizeof c);
    CHECK(!cu_offer_toast(&c, 0, 1, t));
    CHECK(!cu_offer_toast(&c, 2, 0, t));                     /* a fight: nothing, and nothing used up */
    CHECK(cu_offer_toast(&c, 2, 1, t));
    CHECK(cu_offer_toast(&c, 2, 1, t + 5000));
    CHECK(!cu_offer_toast(&c, 2, 0, t + 5000));              /* hidden in a fight */
    CHECK(!cu_offer_toast(&c, 2, 1, t + CU_TOAST_MS + 1));   /* its time is up */
    CHECK(!cu_offer_toast(&c, 2, 1, t + 5 * MIN));           /* the same news: once */
    CHECK(cu_offer_toast(&c, 4, 1, t + 6 * MIN));            /* more news */
    cu_offer_answered(&c, 4, 1, t + 6 * MIN);                /* LATER on the popup */
    CHECK(!cu_offer_toast(&c, 4, 1, t + 7 * MIN));
    CHECK(!cu_offer_toast(&c, 5, 1, t + 20 * MIN));          /* even newer news waits for the snooze */
    CHECK(cu_offer_toast(&c, 5, 1, t + 37 * MIN));           /* the snooze ran out */
    CHECK(!cu_offer_toast(&c, 5, 1, t + 50 * MIN));

    /* the check: every 10 minutes, in any menu when signed in, never in a fight */
    CHECK(cu_check_due(0, t, 0, 1, 0));
    CHECK(!cu_check_due(0, t, 1, 1, 1));
    CHECK(!cu_check_due(0, t, 0, 0, 0));
    CHECK(cu_check_due(0, t, 0, 0, 1));                      /* the online screens, as before */
    CHECK(!cu_check_due(t, t + 9 * MIN, 0, 1, 0));
    CHECK(cu_check_due(t, t + 10 * MIN, 0, 1, 0));

    /* the hand-off */
    CHECK(cu_after_child_exit(CU_EXIT_CODE) && !cu_after_child_exit(0) && !cu_after_child_exit(1));
    wchar_t out[512];
    CHECK(cu_launcher_command(L"\"C:\\FFNY\\defjam_recomp.exe\" --no-launcher --launcher-child", out, 512));
    CHECK(!wcscmp(out, L"\"C:\\FFNY\\defjam_recomp.exe\" --launcher --content-update"));
    CHECK(cu_launcher_command(L"\"C:\\x y\\defjam_recomp.exe\" --launcher --content-update --no-launcher --launcher-child", out, 512));
    CHECK(!wcscmp(out, L"\"C:\\x y\\defjam_recomp.exe\" --launcher --content-update"));
    CHECK(cu_launcher_command(L"defjam_recomp.exe", out, 512) && !wcscmp(out, L"defjam_recomp.exe --launcher --content-update"));
    /* a quoted argument with the flag's text in it stays */
    CHECK(cu_launcher_command(L"a.exe \"--launcher\" --launcherx", out, 512) &&
          !wcscmp(out, L"a.exe \"--launcher\" --launcherx --launcher --content-update"));
    CHECK(cu_reopen_command(L"a.exe --launcher --content-update", out, 512) && !wcscmp(out, L"a.exe --launcher --launcher"));
    CHECK(!cu_launcher_command(L"a.exe", out, 10));                       /* too small: refused, not cut */
    wchar_t line[64] = L"--content-update a.exe --content-update";
    CHECK(cu_strip_flag(line, L"--content-update") == 2 && !wcscmp(line, L"a.exe"));
    printf(fails ? "FAILED %d\n" : "OK\n", fails);
    return fails != 0;
}
'''


@pytest.fixture(scope='module')
def logic_exe(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native content update test needs MSVC')
    folder = tmp_path_factory.mktemp('cu')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'cu.exe'
    r = subprocess.run([compiler, '/nologo', '/W4', '/WX', '/TC', '/I' + str(HOOKS), str(folder / 'driver.c'),
                        str(HOOKS / 'content_update_logic.c'), '/Fe:' + str(exe)],
                       cwd=folder, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    return exe


def test_rules(logic_exe):
    r = subprocess.run([str(logic_exe)], capture_output=True, text=True)
    assert r.returncode == 0 and r.stdout.strip() == 'OK', r.stdout


def src(name):
    return (ROOT / name).read_text(encoding='utf-8')


def test_launcher_watcher_restarts_in_update_mode():
    s = src('src/hooks/pc_settings.c')
    # the launcher's background watcher: the game's UPDATE NOW code opens the launcher with --content-update
    assert 'cu_after_child_exit(code)' in s and 'cu_launcher_command(' in s and 'cu_reopen_command(' in s
    # a launcher in update mode waits for the closing game's one-copy lock instead of giving up
    assert 'CU_FLAG' in s and 'one_instance_wait' in s
    # the restarted game skips the intro movies
    assert 'content_update_restarted()' in src('src/patches/skip_intro.c')


def test_launcher_presses_play_through_the_sync_gate():
    s = src('src/hooks/pc_ui.cpp')
    # the automatic press goes through content_play_gate (it waits for the sync and its compose)
    assert 'content_update_auto_play()' in s
    assert 'content_play_gate(pressed)' in s


def test_never_in_a_match_and_the_save_first():
    s = src('src/hooks/content_update.c')
    assert 'netplay_in_fight()' in s and 'netplay_match_locked()' in s and 'netplay_active()' in s
    assert 'memcard_writing()' in s           # a save being written finishes first
    assert 'TerminateProcess(GetCurrentProcess(), child ? CU_EXIT_CODE : 0)' in s
    m = src('src/hooks/memcard_fast.c')
    assert 'int memcard_writing(void)' in m


def test_the_news_shows_with_update_now_everywhere():
    screen = src('src/hooks/online_screen.cpp')
    assert '"UPDATE NOW"' in screen and 'update_prompt_begin' in screen
    ui = src('src/hooks/pc_ui.cpp')
    assert 'content_update_toast(' in ui and 'content_update_offer_due(' in ui and 'content_update_frame(' in ui
    assert 'New community content is out' in src('src/hooks/content_update.c')
