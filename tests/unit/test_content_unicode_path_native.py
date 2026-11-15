"""Community content under a data folder outside the ANSI code page (src/hooks/pc_content.c, build 83).

A player's launcher said "Community content up to date: 1 pack(s)" while its Content page showed "No community
packs yet": pc_content.c read DEFJAM_DATA from the narrow environment and opened content.ini with fopen(), both in
the ANSI code page, so a data folder with, say, a Cyrillic user name on a Western Windows was not found (and the
game served none of the composed files, so it went online with another content print). The folder is now read wide
and files open wide. Also checks build 83's template_fe_id/template_name (where a new fighter is). Needs MSVC.
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
int main(void)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    int n, i;
    recomp_settings_register(NULL, 0);
    n = pc_content_list(packs, PC_CONTENT_MAX);
    printf("count %d\n", n);
    for (i = 0; i < n; i++)
        printf("%s|%s|%s|%d|%s\n", packs[i].id, packs[i].name, packs[i].type, packs[i].template_fe_id,
               packs[i].template_name);
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
fighter_fe_id=101
composed=1
template_fe_id=59
template_name=D-Mob
"""


@pytest.fixture(scope='module')
def driver(tmp_path_factory):
    compiler = shutil.which('cl')
    if sys.platform != 'win32' or not compiler:
        pytest.skip('the native fixture needs MSVC on Windows')
    out = tmp_path_factory.mktemp('contentpath')
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


def run(driver, data):
    (data / 'mod-manager').mkdir(parents=True)
    (data / 'mods' / 'studio-d-mob-2').mkdir(parents=True)
    (data / 'mod-manager' / 'content.ini').write_text(CONTENT_INI, encoding='utf-8')
    env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    env['DEFJAM_DATA'] = str(data)
    env['RECOMP_SETTINGS'] = 'none'
    got = subprocess.run([str(driver)], capture_output=True, text=True, env=env)
    assert got.returncode == 0, got.stderr
    return got.stdout.split('\n')


def test_plain_folder(driver, tmp_path):
    lines = run(driver, tmp_path / 'data')
    assert lines[0] == 'count 1' and lines[1] == 'studio-d-mob-2|D-Mob 2|new-fighter|59|D-Mob'


def test_folder_outside_the_ansi_code_page(driver, tmp_path):
    lines = run(driver, tmp_path / 'Ждан δεδομένα')
    assert lines[0] == 'count 1' and lines[1].startswith('studio-d-mob-2|D-Mob 2|new-fighter|59|')
