"""Build 83: the game shows why community content is missing. The launcher's sync result (sync-result.ini) and a
failed news check (news.ini error=) reach the start log, the F9 report and the online hub (pc_content.c,
online_screen.cpp); the relay's STATUS Same (build 83 relays: players with exactly this PC's fingerprint) lets the
empty room list say that most players have other content; PLAY retries a failed sync once."""
import re
from pathlib import Path

import pytest

from server_src import RELAY as RELAY_SRC, SKIP

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'
read = lambda name: (HOOKS / name).read_text(encoding='utf-8')
CONTENT_C, CONTENT_H = read('pc_content.c'), read('pc_content.h')
SCREEN, SERVERS_C, SERVERS_H = read('online_screen.cpp'), read('netplay_servers.c'), read('netplay_servers.h')
LAUNCHER, SETTINGS_C, REPORT_C = read('pc_launcher_content.inc'), read('pc_settings.c'), read('bug_report.c')
MODS = (ROOT / 'setup/mods.py').read_text(encoding='utf-8')


def test_the_game_reads_the_files_mods_py_writes():
    for key in ('result', 'kind', 'reason', 'time', 'ids', 'skipped', 'revision', 'packs'):
        assert '%s=' % key in MODS.split('def write_sync_result')[1].split('def ')[0]
        assert '!strcmp(k, "%s")' % key in CONTENT_C
    assert 'read_ini_section("sync-result.ini", "sync"' in CONTENT_C
    assert 'read_ini_section("news.ini", "news"' in CONTENT_C
    for key in ('kind', 'error'):
        assert '%s=' % key in MODS.split('def write_news_failure')[1].split('\n    def ')[0]


def test_start_log_and_f9_report_carry_the_content_state():
    assert 'pc_content_log_header();' in SETTINGS_C
    assert SETTINGS_C.index('graphics_mod_log_header();') < SETTINGS_C.index('pc_content_log_header();') < \
        SETTINGS_C.index('pc_content_start(')
    # build 90: the first line says the community-content-in-offline-play setting
    assert '"[CONTENT] community content in offline play: %s\\n[CONTENT] %s\\n[CONTENT] installed: %s\\n"' in CONTENT_C
    assert 'last sync ok, revision %d, %d pack%s' in CONTENT_C and 'last sync %s: %s' in CONTENT_C
    assert '"FAILED"' in CONTENT_C
    # the F9 report (this session's log): the state at the moment F9 was pressed, from the send thread, which
    # names the print taken at the start rather than touching pc_content_print's static buffer
    assert 'pc_content_report_line();' in REPORT_C and '[REPORT] content: %s; %s; check: %s' in CONTENT_C
    state = CONTENT_C.split('static void content_state')[1].split('\n}\n')[0]
    assert 'pc_content_print()' not in state and 'g_print_at_start' in state and 'malloc(' in state


def test_the_hub_line_texts():
    assert PC_LINE('PC_CONTENT_FIX_LINE') == 'Close the game and start it from the launcher, or check your internet and clock.'
    assert PC_LINE('PC_CONTENT_SPLIT_LINE') == ("Players with different community content can't see your rooms: "
                                                'close the game and start it from the launcher to update')
    assert '"Community content could not be %s%s%s. %s"' in CONTENT_C
    assert 'content_bad && signed_in' in SCREEN and 'COMMUNITY CONTENT NOT UPDATED' in SCREEN
    assert 'bool split = !n && content_split_shown();' in SCREEN       # FIND A ROOM
    assert 'n <= 1 && content_split_shown()' in SCREEN                 # PLAYERS ONLINE
    for page in ('"hub-sync-failed"', '"rooms-split"', '"rooms-sync-failed"'):
        assert SCREEN.count(page) >= 2      # in the gallery's page list and set up


def PC_LINE(name):
    m = re.search(r'#define %s "([^"]*)"' % name, CONTENT_H)
    assert m, name
    return m.group(1)


def test_status_same_is_read_only_from_a_relay_that_says_so():
    assert '#define NETSERVER_SAME     8' in SERVERS_H
    assert 'P.s[who].same = (b[11] & NETSERVER_SAME) ? rd16(b + 24) : -1;' in SERVERS_C
    assert '!(sv[i].flags & NETSERVER_SAME)' in SCREEN
    assert '#define STATUS_LEN    26' in SERVERS_C     # the answer is exactly as long as the request


@pytest.mark.skipif(RELAY_SRC is None, reason=SKIP)
def test_relay_agrees_on_the_flag_and_the_place():
    status = (RELAY_SRC / 'protocol_status.go').read_text(encoding='utf-8')
    if 'StatusSame' not in status:
        pytest.skip('the relay checkout is before feat/content-print-visibility')
    assert re.search(r'StatusSame\s+uint8 = 1 << 3', status)
    assert 'return binary.LittleEndian.AppendUint16(b, m.Same), nil' in status


def test_play_retries_a_failed_sync_once():
    gate = LAUNCHER.split('static bool content_play_gate')[1].split('\n}\n')[0]
    assert 'static bool retried_failed;' in gate and '!strcmp(last.result, "failed")' in gate
    assert 'failed_retry ||' in gate


def test_a_sync_never_reports_ok_after_a_failed_refresh():
    sync = MODS.split('    def sync(self')[1].split('\n    def ')[0]
    assert "self.write_sync_result('failed' if problems else 'ok'" in sync
    assert "self.write_sync_result('failed', [reason])" in sync and "self.write_sync_result('cancelled'" in sync


def test_installed_but_not_served_is_said_plainly():
    """Build 83's data-folder bug (letters outside the ANSI code page): the sync was fine, the game served 0
    community files and went online with another content set. If that ever shows again, the log, F9 and the hub
    say so."""
    start = CONTENT_C.split('void pc_content_start(')[1].split('\n}\n')[0]
    assert 'composed && !g_file_count' in start and 'last.packs > 0' in start
    assert '[CONTENT] PROBLEM: %d composed pack(s) but the game serves 0 community files' in start
    assert '[CONTENT] PROBLEM: the sync installed %d pack(s)' in start
    problem = CONTENT_C.split('int pc_content_problem(char *out, int size)')[1].split('\n}\n')[0]
    assert problem.index('g_load_problem[0]') < problem.index('pc_content_sync_result')     # it comes first
    assert 'Community content is installed but the game could not load it (%s). %s' in problem
    assert PC_LINE('PC_CONTENT_LOAD_LINE') == 'Close the game and start it from the launcher; if it stays, press F9 to send a report.'
    # every new file read opens wide (UTF-8 paths), as build 83 made the rest of pc_content.c
    reader = CONTENT_C.split('static int read_ini_section')[1].split('\n}\n')[0]
    assert 'content_fopen(path, "rb")' in reader and ' fopen(' not in reader
