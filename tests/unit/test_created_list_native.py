"""The player's created fighters (src/hooks/created_fighter.c, cp_local_*), compiled natively: every
Story save of the save area with a created fighter is listed, newest first, at most CP_LOCAL_MAX, with
its name, its save ("<folder>/<file>") and its time; saves without a created fighter and files of
another size are left out; the pick is the save the setting names, the newest when the setting is
empty or names a save that is gone, and RECOMP_CP_PICK overrides it; picking writes the setting; the
record handed to the relay is the picked one; RECOMP_CP_RECORD lists several test records in the order
given. Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1); skips without it.

The game files the record check reads (djv2.sod, fighter.xml) are absent here, so the check keeps
every record whose shape is right and resets the move keys and recipe parts it cannot look up."""
import os
import shutil
import subprocess
import time
from pathlib import Path

import pytest

from native_hooks_fixtures import STUB_TYPES

ROOT = Path(__file__).resolve().parents[2]

PROFILE_SIZE = 0x5950
SAVE_SIZE = 0x5990

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "created_fighter.h"

unsigned char memory[0x400000];

/* the setting online.created (pc_settings.c in the game) */
void cp_pick_setting_get(char *out, int size)
{
    const char *v = getenv("STUB_PICK_KEY");
    snprintf(out, (size_t)size, "%s", v ? v : "");
}
void cp_pick_setting_set(const char *key) { printf("setting|%s\n", key); }

static void dump(void)
{
    static CpLocal l[CP_LOCAL_MAX];
    uint8_t rec[CP_REC_LEN];
    char name[16];
    int n = cp_local_list(l, CP_LOCAL_MAX), i;
    printf("count|%d|%d\n", n, cp_local_list(NULL, 0));
    for (i = 0; i < n; i++)
        printf("entry|%d|%s|%s|%d|%08X\n", i, l[i].name, l[i].key, l[i].saved[0] != 0, cp_record_crc(l[i].rec));
    printf("pick|%d\n", cp_local_pick());
    if (cp_local_record(rec)) {
        cp_record_name(rec, name, sizeof(name));
        printf("record|%s|%d\n", name, cp_record_sealed(rec));
    } else printf("record|-|0\n");
}

int main(int argc, char **argv)
{
    dump();
    if (argc > 1) {
        int index = atoi(argv[1]);
        printf("set|%d\n", cp_local_set_pick(index));
    }
    return 0;
}
'''

STUB_HOST = '#include <windows.h>\n'


def profile(name, created=True, arch=98):
    """A profile image with the fields the record takes (docs/research/created-fighter-online.md §2)."""
    p = bytearray(PROFILE_SIZE)
    p[0xC:0xC + len(name)] = name.encode('ascii')
    for i, v in enumerate((0, 5, 5, 0, 0, 6)):
        p[0xFC + 4 * i:0x100 + 4 * i] = v.to_bytes(4, 'little')
    p[0x13C] = 3                        # voice
    p[0x140:0x143] = b'\x01\xff\xff'    # styles
    p[0x143] = arch
    p[0x4D8A] = 1 if created else 0
    p[0x5564:0x5564 + 0x98] = b'\xff' * 0x98
    return bytes(p)


def save_file(name, **kw):
    return b'\0' * 0x80 + profile(name, **kw)[:SAVE_SIZE - 0x80]


@pytest.fixture(scope='module')
def exe(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('cplist')
    (folder / 'recomp/gen').mkdir(parents=True)
    (folder / 'recomp/gen/recomp_types.h').write_text(STUB_TYPES)
    (folder / 'host.h').write_text(STUB_HOST)
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    out = folder / 'cplist.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/TC', '/I' + str(folder), '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/created_fighter.c'), '/Fe:' + str(out)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def run(exe, cwd, env_extra, *args):
    env = {k: v for k, v in os.environ.items()
           if k not in ('RECOMP_CP_RECORD', 'RECOMP_CP_PICK', 'STUB_PICK_KEY', 'DEFJAM_DATA')}
    env.update(env_extra)
    out = subprocess.run([str(exe), *map(str, args)], cwd=cwd, env=env, capture_output=True, text=True, check=True)
    rows = {}
    for line in out.stdout.splitlines():
        f = line.split('|')
        rows.setdefault(f[0], []).append(f[1:])
    return rows


@pytest.fixture()
def save_area(tmp_path):
    """Five user IDs: four with a created fighter (written a minute apart, the oldest first), one
    without, plus a file of another size and the options file."""
    users = tmp_path / 'data/save/UserData/45410049'
    now = time.time()
    made = [('0C6F8520AAF5', 'AW5J3555', True, 300), ('1A3F1B2E4ADD', 'CPTWO', True, 120),
            ('2B2B2B2B2B2B', 'NOFIGHT', False, 60), ('3C3C3C3C3C3C', 'NIGHTOWL', True, 240),
            ('4D4D4D4D4D4D', 'LATEST', True, 30)]
    for folder, name, created, age in made:
        d = users / folder
        d.mkdir(parents=True)
        f = d / name
        f.write_bytes(save_file(name, created=created))
        os.utime(f, (now - age, now - age))
    (users / '1A3F1B2E4ADD/Options').write_bytes(b'\0' * 0x200)
    (users / '0C6F8520AAF5/SHORT').write_bytes(save_file('SHORT')[:-1])
    return tmp_path


def names(rows):
    return [e[1] for e in rows['entry']]


def test_save_area_listed_newest_first(exe, save_area):
    rows = run(exe, save_area, {'DEFJAM_DATA': str(save_area / 'data')})
    assert rows['count'] == [['4', '4']]
    assert names(rows) == ['LATEST', 'CPTWO', 'NIGHTOWL', 'AW5J3555']
    assert [e[2] for e in rows['entry']] == ['4D4D4D4D4D4D/LATEST', '1A3F1B2E4ADD/CPTWO',
                                             '3C3C3C3C3C3C/NIGHTOWL', '0C6F8520AAF5/AW5J3555']
    assert all(e[3] == '1' for e in rows['entry'])      # each shows its save's time
    assert len({e[4] for e in rows['entry']}) == 4      # four different records
    assert rows['pick'] == [['0']] and rows['record'] == [['LATEST', '1']]    # no setting: the newest


def test_setting_names_the_pick(exe, save_area):
    env = {'DEFJAM_DATA': str(save_area / 'data'), 'STUB_PICK_KEY': '3C3C3C3C3C3C/NIGHTOWL'}
    rows = run(exe, save_area, env)
    assert rows['pick'] == [['2']] and rows['record'] == [['NIGHTOWL', '1']]
    # a save that is gone: the newest again
    env['STUB_PICK_KEY'] = '9999/GONE'
    rows = run(exe, save_area, env)
    assert rows['pick'] == [['0']] and rows['record'] == [['LATEST', '1']]
    # the test override wins over the setting
    env.update(STUB_PICK_KEY='3C3C3C3C3C3C/NIGHTOWL', RECOMP_CP_PICK='3')
    rows = run(exe, save_area, env)
    assert rows['pick'] == [['3']] and rows['record'] == [['AW5J3555', '1']]


def test_picking_writes_the_setting(exe, save_area):
    rows = run(exe, save_area, {'DEFJAM_DATA': str(save_area / 'data')}, 1)
    assert rows['set'] == [['1']] and rows['setting'] == [['1A3F1B2E4ADD/CPTWO']]
    rows = run(exe, save_area, {'DEFJAM_DATA': str(save_area / 'data')}, 7)
    assert rows['set'] == [['0']] and 'setting' not in rows


def test_at_most_eight_newest_kept(exe, tmp_path):
    users = tmp_path / 'data/save/UserData/45410049'
    now = time.time()
    for i in range(11):
        d = users / ('%012X' % (i + 1))
        d.mkdir(parents=True)
        f = d / ('F%02d' % i)
        f.write_bytes(save_file('F%02d' % i))
        os.utime(f, (now - 1000 + 60 * i, now - 1000 + 60 * i))     # F10 newest
    rows = run(exe, tmp_path, {'DEFJAM_DATA': str(tmp_path / 'data')})
    assert rows['count'] == [['8', '8']]
    assert names(rows) == ['F%02d' % i for i in range(10, 2, -1)]


def test_no_fighter(exe, tmp_path):
    rows = run(exe, tmp_path, {'DEFJAM_DATA': str(tmp_path / 'data')})
    assert rows['count'] == [['0', '0']] and rows['pick'] == [['-1']] and rows['record'] == [['-', '0']]


def test_test_records_in_given_order(exe, tmp_path):
    files = []
    for name, created in (('FIRST', True), ('BROKEN', False), ('SECOND', True), ('THIRD', True)):
        f = tmp_path / (name.lower() + '.bin')
        f.write_bytes(profile(name, created=created))
        files.append(str(f))
    env = {'RECOMP_CP_RECORD': ';'.join(files)}
    rows = run(exe, tmp_path, env)
    assert names(rows) == ['FIRST', 'SECOND', 'THIRD']
    assert [e[2] for e in rows['entry']] == [files[0], files[2], files[3]]
    assert all(e[3] == '0' for e in rows['entry'])      # test records have no save time
    env['RECOMP_CP_PICK'] = '2'
    rows = run(exe, tmp_path, env)
    assert rows['record'] == [['THIRD', '1']]
