"""The one-FFNY-Online-at-a-time mutex is per data folder (src/hooks/pc_settings.c, build 83).

On 2026-11-13 an agent's manual test copy held the one session-wide mutex "Local\\FFNY-Online-running" and
the owner's launcher exited with "already running". The name now carries a hash of the data folder, so a copy
with another data root never blocks the player's; two starts of the same install (any spelling of its path)
still meet. Needs MSVC (`cl` on PATH) for the native part; Windows only.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SETTINGS = ROOT / 'src' / 'hooks' / 'pc_settings.c'

DRIVER = r'''
#include <windows.h>
#include <stdio.h>
#include <string.h>
%s
int main(int argc, char **argv)
{
    WCHAR name[96];
    int i;
    for (i = 1; i < argc; i++) {
        instance_mutex_name(argv[i][0] == '-' ? NULL : argv[i], name, sizeof name / sizeof name[0]);
        wprintf(L"%%ls\n", name);
    }
    return 0;
}
'''


def source():
    text = SETTINGS.read_text(encoding='utf-8')
    start = text.index('static void instance_mutex_name(')
    return text[start:text.index('\n}', start) + 2]


def test_mutex_is_named_per_data_folder():
    text = SETTINGS.read_text(encoding='utf-8')
    assert 'CreateMutexW(NULL, FALSE, L"Local\\\\FFNY-Online-running")' not in text
    assert 'instance_mutex_name(root, name' in text
    assert re.search(r'one_instance_only\(have_root \? root : NULL\)', text)


@pytest.fixture(scope='module')
def names(tmp_path_factory):
    compiler = shutil.which('cl')
    if sys.platform != 'win32' or not compiler:
        pytest.skip('the native fixture needs MSVC on Windows')
    out = tmp_path_factory.mktemp('mutex')
    (out / 'driver.c').write_text(DRIVER % source(), encoding='utf-8')
    built = subprocess.run([compiler, '/nologo', '/W4', '/WX', '/TC', 'driver.c'], cwd=out,
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(out / 'driver.exe'), r'D:\DefJam\data', r'd:/defjam/DATA/', r'D:\DefJam\fresh82\data',
                          r'C:\Users\x\AppData\Local\FFNY Online', '-'], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    return run.stdout.split()


def test_same_folder_same_name_other_folder_other_name(names):
    data, data_again, test_copy, player, program = names
    assert all(re.fullmatch(r'Local\\FFNY-Online-running-[0-9a-f]{16}', n) for n in names)
    assert data == data_again                     # case, slashes and a trailing separator do not matter
    assert len({data, test_copy, player, program}) == 4
