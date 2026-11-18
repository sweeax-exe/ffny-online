"""Build 94, the master switch with hundreds of own packs (src/hooks/pc_content.c, setup/content_compose.py).

A setting holds 1023 characters (about 60 pack ids), so the own packs that are switched off live in
<data>/mod-manager/own-off.txt and the master switch's backup in mod-manager/mods-backup.txt. This runs the real
pc_content.c with 300 own packs: the list is no longer cut at 64, the switches round-trip through the file, a huge
state file is written whole, and the compose step (Python) reads the same ids. Native part needs MSVC.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'
sys.path.insert(0, str(ROOT / 'setup'))
import content_compose  # noqa: E402

COUNT = 300

DRIVER = r'''
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "recomp_settings.h"
#include "pc_content.h"
#include "pc_language.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    static RecompSetting settings[7];
    static char csv[60000], big[60000], back[60000], ids[60000];
    int n, i, off_count = 0, len;
    char id[64];
    n = (int)pc_language_settings(settings, 7);
    recomp_settings_register(settings, (size_t)n);
    /* every one of the 300 own packs is listed (the list was cut at 64 before build 94) */
    n = pc_content_list(packs, PC_CONTENT_MAX);
    CHECK(n == 300);
    CHECK(PC_CONTENT_MAX >= 300);
    for (i = 0; i < n; i++) off_count += packs[i].off;
    CHECK(off_count == 0);
    /* all 300 off in one write: far more than a setting holds */
    ids[0] = 0;
    for (i = 0; i < 300; i++) { snprintf(id, sizeof id, "own-fighter-pack-%03d", i); strcat(ids, i ? "," : ""); strcat(ids, id); }
    CHECK(strlen(ids) > 6000);
    CHECK(pc_content_own_off_put(ids) == 1);
    len = pc_content_own_off_csv(csv, sizeof csv);
    CHECK(len == (int)strlen(ids) && !strcmp(csv, ids));
    CHECK(pc_content_own_is_off("own-fighter-pack-299") && pc_content_own_is_off("own-fighter-pack-000"));
    n = pc_content_list(packs, PC_CONTENT_MAX);
    off_count = 0;
    for (i = 0; i < n; i++) off_count += packs[i].off;
    CHECK(n == 300 && off_count == 300);
    /* one back on, one more off (a pack that is not listed: kept) */
    CHECK(pc_content_own_set_off("own-fighter-pack-007", 0) == 1);
    CHECK(!pc_content_own_is_off("own-fighter-pack-007") && pc_content_own_is_off("own-fighter-pack-008"));
    CHECK(pc_content_own_set_off("gone-pack", 1) == 1 && pc_content_own_is_off("gone-pack"));
    /* an invalid id is refused, not written */
    CHECK(pc_content_own_set_off("Bad Id", 1) == 0);
    /* every one on again */
    CHECK(pc_content_own_off_put("") == 1);
    CHECK(pc_content_own_off_csv(csv, sizeof csv) == 0 && !pc_content_own_is_off("own-fighter-pack-008"));
    /* all off again for the Python side; then a state file of 45 000 bytes round-trips whole */
    CHECK(pc_content_own_off_put(ids) == 1);
    memset(big, 'x', 45000); big[45000] = 0;
    CHECK(pc_content_state_write("mods-backup.txt", big) == 1);
    CHECK(pc_content_state_read("mods-backup.txt", back, sizeof back) == 45000 && !strcmp(back, big));
    CHECK(pc_content_state_read("mods-backup.txt", back, 100) == -1);       /* too small: not half-read */
    CHECK(pc_content_state_write("mods-backup.txt", "") == 1);              /* empty removes it */
    CHECK(pc_content_state_read("mods-backup.txt", back, sizeof back) == 0);
    CHECK(pc_content_state_write("../escape.txt", "x") == 0);               /* a name, not a path */
    printf(fails ? "FAILED %d\n" : "OK\n", fails);
    return fails != 0;
}
'''


def content_ini():
    lines = ['; test', '[content]', 'revision=1']
    for i in range(COUNT):
        lines += ['[pack.own-fighter-pack-%03d]' % i, 'name=Own %d' % i, 'kind=content', 'type=new-fighter',
                  'version=0.0.1', 'content_sha256=aa', 'order=%d' % (900 + i), 'local=1', 'online_ok=0', 'composed=1']
    return '\n'.join(lines) + '\n'


@pytest.fixture(scope='module')
def driver(tmp_path_factory):
    compiler = shutil.which('cl')
    if sys.platform != 'win32' or not compiler:
        pytest.skip('the native fixture needs MSVC on Windows')
    settings_c = ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c'
    if not settings_c.is_file():
        pytest.skip('tools/xboxrecomp is not checked out')
    out = tmp_path_factory.mktemp('mods_master_files')
    (out / 'driver.c').write_text(DRIVER, encoding='utf-8')
    built = subprocess.run([compiler, '/nologo', '/O2', '/TC', '/DDEFJAM_APP=1', '/I' + str(HOOKS),
                            '/I' + str(settings_c.parent), str(out / 'driver.c'), str(HOOKS / 'pc_content.c'),
                            str(HOOKS / 'pc_language_settings.c'), str(settings_c), '/Fe:' + str(out / 'driver.exe')],
                           cwd=out, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out / 'driver.exe'


def test_hundreds_of_own_packs(driver, tmp_path):
    data = tmp_path / 'data'
    (data / 'mod-manager').mkdir(parents=True)
    (data / 'mod-manager' / 'content.ini').write_text(content_ini(), encoding='utf-8')
    for i in range(COUNT):
        (data / 'mods' / ('own-fighter-pack-%03d' % i)).mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    env.update(DEFJAM_DATA=str(data), RECOMP_SETTINGS='none')
    got = subprocess.run([str(driver)], capture_output=True, text=True, env=env)
    assert got.returncode == 0 and got.stdout.strip() == 'OK', got.stdout + got.stderr
    # the compose step reads the same ids: all 300 are off (the driver left the full list in place)
    off = content_compose.read_own_off(data)
    assert len(off) == COUNT and 'own-fighter-pack-123' in off and 'gone-pack' not in off


def test_compose_reads_nothing_without_the_file(tmp_path):
    assert content_compose.read_own_off(tmp_path) == set()
    (tmp_path / 'mod-manager').mkdir()
    (tmp_path / 'mod-manager' / 'own-off.txt').write_text('\ufeffa-pack\n\n  b-pack  \n', encoding='utf-8')
    assert content_compose.read_own_off(tmp_path) == {'a-pack', 'b-pack'}
