"""Build 110: how a run of the game ended (src/hooks/run_end.h): the file beside the logs and the message the next
launcher start sends (run_end_rules.c, compiled natively; needs MSVC and skips without it), plus the wiring."""
import json
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "run_end.h"
int main(int argc, char **argv)
{
    char text[4096], out[400];
    size_t n;
    RunEnd r;
    FILE *f = fopen(argv[1], "rb");
    if (argc > 2) {     /* the question: mode, times asked */
        printf("%d\n", run_end_ask_due(atoi(argv[1]), atoi(argv[2])));
        return 0;
    }
    n = fread(text, 1, sizeof text - 1, f);
    text[n] = 0;
    fclose(f);
    if (!run_end_parse(text, &r)) { printf("none\n"); return 0; }
    if (!run_end_json(&r, 1000100, out, sizeof out)) { printf("too long\n"); return 1; }
    printf("%s\n", out);
    return 0;
}
'''

BEGIN = 'begin build=109 cl=194435207 at=1000000\r\n'
EXIT = 'exit code=%s at=1000090\r\n'
CRASH = 'crash code=C0000005 at=VCRUNTIME140.dll+1DA93 from=1A045\r\n'


@pytest.fixture(scope='module')
def ends(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native run end test needs MSVC')
    tmp = tmp_path_factory.mktemp('runend')
    (tmp / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = tmp / 're.exe'
    r = subprocess.run([compiler, '/nologo', '/W4', '/WX', '/wd4996', '/TC', '/I' + str(HOOKS), str(tmp / 'driver.c'),
                        str(HOOKS / 'run_end_rules.c'), '/Fe:' + str(exe)], cwd=tmp, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr

    ends_exe = str(exe)

    def ends_(text):
        (tmp / 'run-end.txt').write_bytes(text.encode('utf-8'))
        out = subprocess.run([str(exe), str(tmp / 'run-end.txt')], capture_output=True, text=True).stdout.strip()
        return None if out == 'none' else json.loads(out)
    ends_.exe = ends_exe
    return ends_


def test_the_ways_a_run_ends(ends):
    assert ends('') is None and ends('exit code=00000000 at=5\r\n') is None
    r = ends(BEGIN + 'closed\r\n' + EXIT % '00000000')
    assert r == {'build': 109, 'cl': 194435207, 'end': 'closed', 'hung': 0, 'code': '00000000', 'at': '', 'from': '', 'ago': 10}
    assert ends('begin build=109 at=1000000\r\n')['cl'] == 0
    assert ends(BEGIN + 'quit\r\n' + EXIT % '00000000')['end'] == 'quit'
    assert ends(BEGIN + 'update\r\n' + EXIT % '0000002A')['end'] == 'update'
    assert ends(BEGIN + EXIT % '00000000')['end'] == 'quit'             # the title left by itself
    r = ends(BEGIN + CRASH + EXIT % 'C0000005')
    assert (r['end'], r['code'], r['at'], r['from']) == ('crash', 'C0000005', 'VCRUNTIME140.dll+1DA93', '1A045')
    r = ends(BEGIN + CRASH)             # the launcher that waited was gone too
    assert (r['end'], r['code'], r['ago']) == ('crash', 'C0000005', 100)
    r = ends(BEGIN + EXIT % '00000001')     # ended from outside (the Task Manager), or a fault nothing saw
    assert (r['end'], r['code']) == ('exit', '00000001')
    assert ends(BEGIN)['end'] == 'unknown' and ends(BEGIN)['code'] == ''


def test_a_fault_that_was_handled_is_not_the_end(ends):
    r = ends(BEGIN + CRASH + 'closed\r\n' + EXIT % '00000000')
    assert (r['end'], r['at'], r['from']) == ('closed', '', '')
    assert ends(BEGIN + CRASH + EXIT % '00000000')['end'] == 'quit'
    r = ends(BEGIN + CRASH + 'crash code=C0000094 at=defjam_recomp.exe+81170\r\n' + EXIT % 'C0000094')
    assert (r['at'], r['from'], r['code']) == ('defjam_recomp.exe+81170', '', 'C0000094')


def test_a_frozen_game_that_was_closed(ends):
    assert ends(BEGIN + 'hung\r\nclosed\r\n')['hung'] == 1
    assert ends(BEGIN + 'hung\r\nmoved\r\nclosed\r\n')['hung'] == 0


def test_only_plain_characters_leave(ends):
    r = ends(BEGIN + 'crash code=C0000005 at=od"d\ä.dll+10 from=1\r\n')
    assert r['at'].endswith('.dll+10') and '"' not in r['at'] and r['at'].isascii()
    assert ends(BEGIN + 'crash code=C0000005 at=' + 'x' * 500 + '+10\r\n')['end'] == 'crash'


def test_asked_twice_at_most(ends):
    due = lambda mode, asked: int(subprocess.run([ends.exe, str(mode), str(asked)], capture_output=True, text=True).stdout)
    ask, on, off = 0, 1, 2
    assert [due(ask, n) for n in (0, 1, 2)] == [1, 1, 2]        # ask, ask once more, then no
    assert [due(m, n) for m in (on, off) for n in (0, 1, 2)] == [0] * 6


def src(name):
    return (ROOT / name).read_text(encoding='utf-8')


def test_wiring():
    main = src('src/main.c')
    assert main.index('pc_settings_init())') < main.index('run_end_begin(')      # the game's process, not the launcher's
    assert main.count('run_end_crash(') == 3
    crash = main.split('run_end_crash(ep);', 1)[1]
    assert crash.index('netplay_on_crash();') < 400                 # before the online match's report
    trans = src('src/hooks/d3d11_translator.c')
    close = trans.split('if (msg == WM_CLOSE) {', 1)[1].split('TerminateProcess', 1)[0]
    assert 'run_end_closing();' in close
    ui = src('src/hooks/pc_ui.cpp')
    assert ui.index('run_end_quit();') < ui.index('PostMessageW(g_window, WM_CLOSE')
    settings = src('src/hooks/pc_settings.c')
    assert settings.count('run_end_exit_code(code);') == 2
    again = settings.split('static void start_game_process(int reopen_launcher)', 1)[1].split('\n}\n', 1)[0]
    assert again.rindex('run_end_exit_code(code);') < again.index('"[UI] the game closed%s')
    assert 'run_end_send();' in settings.split('static void run_launcher_if_wanted(void)', 1)[1]
    # only with a sign-in, and only what the header says: no log, no path
    send = src('src/hooks/run_end.c')
    assert 'if (account_state() == ACCOUNT_IN && allowed()) status = account_send_run_end(json);' in send
    # ask by default; nothing is written or sent before a yes, off deletes what is pending
    assert '{ "online", "run_end_reports", RECOMP_SETTING_ENUM, 0, 0, 0, k_run_end,' in settings
    assert 'k_run_end[] = { "ask", "on", "off", NULL }' in settings
    begin = send.split('void run_end_begin(int build)', 1)[1].split('\n}\n', 1)[0]
    assert begin.index('DeleteFileA(RUN_END_FILE);') < begin.index('if (!allowed()) return;') < begin.index('CreateFileA(')
    start = send.split('void run_end_send(void)', 1)[1].split('\n}\n', 1)[0]
    assert start.index('if (!allowed()) {') < start.index('DeleteFileA(') < start.index('CreateFileA(')
    ask = src('src/hooks/pc_launcher_run_end.inc')
    assert ask.count('run_end_answer(RUN_END_ON') == 0 and 'run_end_answer(choice == 0 ? RUN_END_ON : RUN_END_OFF)' in ask
    assert 'if (i == 2) ImGui::SetItemDefaultFocus();' in ask and 'k_red' not in ask        # Yes is not picked for the player
    assert 'account_state() == ACCOUNT_IN && notice_may_open()' in ask
    assert ui.count('    run_end_setting();\n') == 2
    assert 'run-*.log' not in send and 'strrchr(path,' in send
