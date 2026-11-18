"""The content print read on several threads at once (src/hooks/pc_content.c, build 94).

Build 93's release test: a two-copy quick match once never paired. Both copies said the same content print in
their HELLO (the relay logged it), yet the relay opened a second room for the second copy's QUICK instead of
putting it in the first one's: their build prints differed. The build print (netplay_build_fingerprint) hashes
pc_content_print(), which listed the packs into one static array (and read files.ini's applied list into one
static buffer) on whatever thread called it: the netplay start, its network thread's HELLO and the main thread's
[CONTENT] header ran it at the same moment at start-up, one thread zeroed entries the other was hashing, and the
start got the print of another content set. The relay keeps players of different build prints apart (rooms,
QUICK, room lists), so that player met nobody until a restart. Every caller now lists into its own buffer.
Needs MSVC.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "recomp_settings.h"
#include "pc_content.h"
static uint32_t g_want;
static volatile LONG g_bad, g_calls;
static DWORD WINAPI worker(LPVOID arg)
{
    int i, kind = (int)(INT_PTR)arg;
    for (i = 0; i < 400; i++) {
        if (kind == 3) {    /* the other callers of the shared list in build 93 */
            PcContentSkin skins[8];
            char names[256];
            pc_content_skins(skins, 8);
            pc_content_local_offline(names, sizeof names);
            pc_content_language_blocked("studio-pack-3");
            continue;
        }
        if (pc_content_print() != g_want) InterlockedIncrement(&g_bad);
        InterlockedIncrement(&g_calls);
    }
    return 0;
}
int main(void)
{
    HANDLE t[6];
    int i;
    recomp_settings_register(NULL, 0);
    g_want = pc_content_print();
    for (i = 0; i < 6; i++) t[i] = CreateThread(NULL, 0, worker, (LPVOID)(INT_PTR)(i == 5 ? 3 : 0), 0, NULL);
    WaitForMultipleObjects(6, t, TRUE, INFINITE);
    printf("print %08X\ncalls %ld\nbad %ld\n", g_want, g_calls, g_bad);
    return 0;
}
'''


def content_ini(n):
    out = ['[content]', 'revision=47']
    for i in range(n):
        out += ['[pack.studio-pack-%d]' % i, 'name=Pack %d' % i, 'kind=content', 'type=new-fighter',
                'version=0.1.%d' % i, 'content_sha256=%064x' % (i + 1), 'fighter_fe_id=%d' % (100 + i), 'composed=1',
                'order=%d' % i]
    return '\n'.join(out) + '\n'


@pytest.fixture(scope='module')
def driver(tmp_path_factory):
    compiler = shutil.which('cl')
    if sys.platform != 'win32' or not compiler:
        pytest.skip('the native fixture needs MSVC on Windows')
    out = tmp_path_factory.mktemp('contentthreads')
    (out / 'driver.c').write_text(DRIVER, encoding='utf-8')
    sources = [out / 'driver.c', ROOT / 'src/hooks/pc_content.c',
               ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c']
    if not sources[-1].is_file():
        pytest.skip('tools/xboxrecomp is not checked out')
    includes = [ROOT / 'src/hooks', ROOT / 'tools/xboxrecomp/src/settings']
    built = subprocess.run([compiler, '/nologo', '/O2', '/TC', '/DDEFJAM_APP=1',
                            *['/I' + str(p) for p in includes], *map(str, sources), '/Fe:' + str(out / 'driver.exe')],
                           cwd=out, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out / 'driver.exe'


def test_the_print_is_the_same_on_every_thread(driver, tmp_path):
    packs = 29      # as many as the release test's install
    data = tmp_path / 'data'
    (data / 'mod-manager' / 'content-files').mkdir(parents=True)
    for i in range(packs):
        (data / 'mods' / ('studio-pack-%d' % i)).mkdir(parents=True)
    (data / 'mod-manager' / 'content.ini').write_text(content_ini(packs), encoding='utf-8')
    applied = ','.join('studio-pack-%d' % i for i in range(packs))
    (data / 'mod-manager' / 'content-files' / 'files.ini').write_text(
        '[content-files]\nschema=1\napplied=%s\n' % applied, encoding='utf-8')
    env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    env['DEFJAM_DATA'] = str(data)
    env['RECOMP_SETTINGS'] = 'none'
    got = subprocess.run([str(driver)], capture_output=True, text=True, env=env, timeout=300)
    assert got.returncode == 0, got.stderr
    rows = dict(line.split(' ', 1) for line in got.stdout.splitlines())
    assert rows['print'] != '00000000'
    assert int(rows['calls']) == 5 * 400
    assert rows['bad'] == '0', got.stdout


def test_a_long_applied_line_is_read_whole(driver, tmp_path):
    """300 packs: the applied line is far over 1200 characters. A pack at its end must count (build 94)."""
    packs = 300

    def run(name, applied_count):
        data = tmp_path / name
        (data / 'mod-manager' / 'content-files').mkdir(parents=True)
        for i in range(packs):
            (data / 'mods' / ('studio-pack-%d' % i)).mkdir(parents=True)
        (data / 'mod-manager' / 'content.ini').write_text(content_ini(packs), encoding='utf-8')
        applied = ','.join('studio-pack-%d' % i for i in range(applied_count))
        (data / 'mod-manager' / 'content-files' / 'files.ini').write_text(
            '[content-files]\nschema=1\napplied=%s\n' % applied, encoding='utf-8')
        env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
        env['DEFJAM_DATA'] = str(data)
        env['RECOMP_SETTINGS'] = 'none'
        got = subprocess.run([str(driver)], capture_output=True, text=True, env=env, timeout=300)
        assert got.returncode == 0, got.stderr
        rows = dict(line.split(' ', 1) for line in got.stdout.splitlines())
        assert rows['bad'] == '0', got.stdout
        return rows['print']

    assert run('all', packs) != run('last-missing', packs - 1)
