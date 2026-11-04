"""The launcher's update download guard (src/hooks/updater_guard.h, build 54), compiled natively: a second
launcher (another process) does not get the download while the first holds it, gets it once the first is
done or has closed without letting go (an abandoned mutex), and the download folder's path comes from
GetTempPathW (a temp folder with letters outside the ANSI code page). Needs MSVC (`cl` on PATH, e.g. after
scripts/env.ps1); skips without it."""
import os
import shutil
import subprocess
import sys

import pytest

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <string.h>
#include "updater_guard.h"

int wmain(int argc, wchar_t **argv)
{
    HANDLE held = NULL;
    if (argc > 1 && !wcscmp(argv[1], L"try")) {          /* a second launcher: 1 got it, 0 refused */
        int got = update_lock(&held);
        printf("try|%d\n", got);
        update_unlock(held);
        return 0;
    }
    if (argc > 1 && !wcscmp(argv[1], L"hold")) {         /* takes it and exits without letting go */
        update_lock(&held);
        printf("hold|%d\n", held != NULL);
        fflush(stdout);
        ExitProcess(0);
    }
    if (argc > 1 && !wcscmp(argv[1], L"path")) {
        wchar_t p[MAX_PATH * 2], d[MAX_PATH * 2];
        char u[MAX_PATH * 4];
        int ok = update_path(p, MAX_PATH * 2, L"DefJamSetup-54.exe") && update_path(d, MAX_PATH * 2, NULL);
        WideCharToMultiByte(CP_UTF8, 0, p, -1, u, (int)sizeof(u), NULL, NULL);
        printf("path|%d|%s|", ok, u);
        WideCharToMultiByte(CP_UTF8, 0, d, -1, u, (int)sizeof(u), NULL, NULL);
        printf("%s|%d\n", u, update_path(p, 8, L"DefJamSetup-54.exe"));
        return 0;
    }
    {   /* the first launcher: holds it while a second one tries, then lets go and the second gets it */
        wchar_t cmd[MAX_PATH * 2];
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        int got = update_lock(&held);
        printf("first|%d\n", got);
        fflush(stdout);
        _snwprintf(cmd, MAX_PATH * 2, L"\"%s\" try", argv[0]);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        update_unlock(held);
        printf("released|1\n");
        fflush(stdout);
        if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def exe(tmp_path_factory):
    if sys.platform != 'win32':
        pytest.skip('Windows only')
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('updguard')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    out = folder / 'updguard.exe'
    built = subprocess.run([compiler, '/nologo', '/W4', '/WX', '/TC', '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS',
                            '/I' + str(ROOT / 'src/hooks'), str(folder / 'driver.c'), '/Fe:' + str(out)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def run(exe, *args, env=None):
    out = subprocess.run([str(exe), *args], capture_output=True, text=True, encoding='utf-8', check=True, env=env,
                         timeout=60).stdout
    return [line.split('|') for line in out.splitlines()]


def test_second_launcher_waits_for_the_first(exe):
    assert run(exe) == [['first', '1'], ['try', '0'], ['released', '1'], ['try', '1']]


def test_a_closed_launcher_hands_it_over(exe):
    assert run(exe, 'hold') == [['hold', '1']]
    assert run(exe, 'try') == [['try', '1']]       # WAIT_ABANDONED counts as taken


def test_folder_from_the_wide_temp_path(exe, tmp_path):
    temp = tmp_path / 'Temp-éŁ漢'          # letters outside most ANSI code pages
    temp.mkdir()
    env = dict(os.environ, TMP=str(temp), TEMP=str(temp))
    ((tag, ok, setup, folder, short),) = run(exe, 'path', env=env)
    assert tag == 'path'
    assert ok == '1' and short == '0'
    assert setup == str(temp / 'DefJamUpdate' / 'DefJamSetup-54.exe')
    assert folder == str(temp / 'DefJamUpdate')
