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
int automatch_queue_fe(const char *command) { (void)command; return 0; }   /* tests' user ID load */

/* the setting online.created_names: STUB_NAMES, and what a set writes */
static char g_names[CP_NAMES_TEXT_MAX];
static int g_names_read;
void cp_names_setting_get(char *out, int size)
{
    const char *v = getenv("STUB_NAMES");
    if (!g_names_read) { snprintf(g_names, sizeof(g_names), "%s", v ? v : ""); g_names_read = 1; }
    snprintf(out, (size_t)size, "%s", g_names);
}
void cp_names_setting_set(const char *text)
{
    snprintf(g_names, sizeof(g_names), "%s", text);
    printf("names|%s\n", text);
}

static void dump(void)
{
    static CpLocal l[CP_LOCAL_MAX];
    uint8_t rec[CP_REC_LEN];
    char name[CP_NAME_MAX + 1];
    int n = cp_local_list(l, CP_LOCAL_MAX), i;
    printf("count|%d|%d\n", n, cp_local_list(NULL, 0));
    for (i = 0; i < n; i++)
        printf("entry|%d|%s|%s|%d|%08X|%s|%s\n", i, l[i].name, l[i].key, l[i].saved[0] != 0, cp_record_crc(l[i].rec),
               l[i].user_id, l[i].online);
    printf("pick|%d\n", cp_local_pick());
    if (cp_local_record(rec)) {
        cp_record_name(rec, name, sizeof(name));
        printf("record|%s|%d\n", name, cp_record_sealed(rec));
    } else printf("record|-|0\n");
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "clean")) {     /* cp_name_clean of argv[2] */
        char out[CP_NAME_MAX + 1];
        int n = cp_name_clean(argv[2], out, sizeof(out));
        printf("clean|%d|[%s]\n", n, out);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "sanitize")) {  /* a record named argv[2] (escapes \\xNN), checked */
        static uint8_t p[0x5950];
        uint8_t rec[CP_REC_LEN], ok[CP_REC_LEN];
        char name[CP_NAME_MAX + 1];
        const char *s = argv[2];
        int k = 0;
        while (*s && k < 0x20) {
            unsigned v;
            if (s[0] == '\\' && s[1] == 'x' && sscanf(s + 2, "%2x", &v) == 1) { p[0xC + k++] = (uint8_t)v; s += 4; }
            else p[0xC + k++] = (uint8_t)*s++;
        }
        p[0x143] = 98; p[0x140] = 1; p[0x141] = p[0x142] = 0xFF; p[0x13C] = 3; p[0x4D8A] = 1;
        memset(p + 0x5564, 0xFF, 0x98);
        cp_record_from_profile(p, rec);
        if (!cp_record_sanitize(rec, ok)) { printf("sanitize|0\n"); return 0; }
        cp_record_name(ok, name, sizeof(name));
        printf("sanitize|1|[%s]|%d\n", name, (int)strlen((const char *)ok + 1));
        return 0;
    }
    if (argc > 13 && !strcmp(argv[1], "stats")) {   /* base[0..5] bonus[0..5] (argv[2..13]), checked */
        static uint8_t p[0x5950];
        uint8_t rec[CP_REC_LEN], ok[CP_REC_LEN];
        int i;
        memcpy(p + 0xC, "STATS", 5);
        p[0x143] = 98; p[0x140] = 1; p[0x141] = p[0x142] = 0xFF; p[0x13C] = 3; p[0x4D8A] = 1;
        memset(p + 0x5564, 0xFF, 0x98);
        for (i = 0; i < 12; i++) {
            unsigned v = (unsigned)strtol(argv[2 + i], NULL, 10);
            memcpy(p + 0xFC + 4 * i, &v, 4);
        }
        cp_record_from_profile(p, rec);
        if (!cp_record_sanitize(rec, ok)) { printf("stats|0\n"); return 0; }
        printf("stats|1");
        for (i = 0; i < 12; i++) { int v; memcpy(&v, ok + 1 + 0x20 + 4 * i, 4); printf("|%d", v); }
        printf("\n");
        return 0;
    }
    dump();
    if (argc > 2 && !strcmp(argv[1], "name")) {      /* online name argv[3] for entry argv[2] */
        printf("named|%d\n", cp_local_set_name(atoi(argv[2]), argc > 3 ? argv[3] : ""));
        dump();
    } else if (argc > 1) {
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
           if k not in ('RECOMP_CP_RECORD', 'RECOMP_CP_PICK', 'STUB_PICK_KEY', 'STUB_NAMES', 'DEFJAM_DATA')}
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


def test_at_most_sixty_four_newest_kept(exe, tmp_path):
    users = tmp_path / 'data/save/UserData/45410049'
    now = time.time()
    for i in range(67):
        d = users / ('%012X' % (i + 1))
        d.mkdir(parents=True)
        f = d / ('F%02d' % i)
        f.write_bytes(save_file('F%02d' % i))
        os.utime(f, (now - 1000 + 60 * i, now - 1000 + 60 * i))     # F66 newest
    rows = run(exe, tmp_path, {'DEFJAM_DATA': str(tmp_path / 'data')})
    assert rows['count'] == [['64', '64']]
    assert names(rows) == ['F%02d' % i for i in range(66, 2, -1)]


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


# ---- online names (build 39): up to 16 characters in the record, the save keeps its user ID ----

def test_record_name_up_to_sixteen(exe, tmp_path):
    rows = run(exe, tmp_path, {}, 'sanitize', 'Brooklyn Bruiser')
    assert rows['sanitize'] == [['1', '[Brooklyn Bruiser]', '16']]
    rows = run(exe, tmp_path, {}, 'sanitize', 'Brooklyn Bruisers')       # 17: cut to 16, the rest zero
    assert rows['sanitize'] == [['1', '[Brooklyn Bruiser]', '16']]
    rows = run(exe, tmp_path, {}, 'sanitize', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ012345')    # the whole field
    assert rows['sanitize'] == [['1', '[ABCDEFGHIJKLMNOP]', '16']]
    rows = run(exe, tmp_path, {}, 'sanitize', 'AW5J3555')
    assert rows['sanitize'] == [['1', '[AW5J3555]', '8']]
    rows = run(exe, tmp_path, {}, 'sanitize', r'AB\x07CD\xC9')               # not printable: '?'
    assert rows['sanitize'] == [['1', '[AB?CD?]', '6']]


# ---- stats (build 47): the game's clamp is the relay's limit (server relay/record_stats.go) ----

def stats(exe, tmp_path, base, bonus):
    rows = run(exe, tmp_path, {}, 'stats', *base, *bonus)
    row = rows['stats'][0]
    assert row[0] == '1'
    v = [int(x) for x in row[1:]]
    return v[:6], v[6:]


def test_stats_within_limits_unchanged(exe, tmp_path):
    base, bonus = stats(exe, tmp_path, (50, 50, 50, 50, 50, 50), (8, 2, 0, 4, 5, 5))
    assert base == [50] * 6 and bonus == [8, 2, 0, 4, 5, 5]      # total 24: the most the game makes


def test_stats_clamped_to_relay_limits(exe, tmp_path):
    base, bonus = stats(exe, tmp_path, (99, 0, 51, 50, 7, 60), (38, 9, -3, 8, 1, 6))
    assert base == [50, 0, 50, 50, 7, 50]
    # each stat at 8, bonus[5] at 5 (8, 8, 0, 8, 1, 5: total 30), then a point off the largest at a time
    assert bonus == [6, 6, 0, 6, 1, 5]
    # still above the total: a point off the largest (the first of equal ones) until 24
    base, bonus = stats(exe, tmp_path, (0,) * 6, (38, 38, 38, 38, 38, 38))
    assert bonus == [4, 4, 4, 4, 4, 4]
    base, bonus = stats(exe, tmp_path, (0,) * 6, (8, 8, 8, 1, 0, 5))
    assert bonus == [6, 6, 6, 1, 0, 5]


def test_name_clean(exe, tmp_path):
    for given, want in (('  Ann  ', 'Ann'), ('Brooklyn Bruiser', 'Brooklyn Bruiser'),
                        ('Brooklyn Bruisers', 'Brooklyn Bruiser'), ('ABCDEFGHIJKLMNO Q', 'ABCDEFGHIJKLMNO'),
                        ('A\tB', 'AB'), ('   ', ''), ('a;b=c%d', 'a;b=c%d')):
        rows = run(exe, tmp_path, {}, 'clean', given)
        assert rows['clean'] == [[str(len(want)), '[%s]' % want]], given


def test_online_name_from_setting(exe, save_area):
    env = {'DEFJAM_DATA': str(save_area / 'data')}
    plain = run(exe, save_area, env)
    env.update(STUB_NAMES='9999/GONE=Old one;3C3C3C3C3C3C/NIGHTOWL=Night Owl Rising',
               STUB_PICK_KEY='3C3C3C3C3C3C/NIGHTOWL')
    rows = run(exe, save_area, env)
    owl = rows['entry'][2]
    assert owl[1] == 'Night Owl Rising' and owl[5] == 'NIGHTOWL' and owl[6] == 'Night Owl Rising'
    assert owl[4] != plain['entry'][2][4]                       # another record: the name is in it
    others = [e[4] for i, e in enumerate(rows['entry']) if i != 2]
    assert others == [e[4] for i, e in enumerate(plain['entry']) if i != 2]
    assert [e[1] for e in rows['entry']] == ['LATEST', 'CPTWO', 'Night Owl Rising', 'AW5J3555']
    assert all(e[6] == '' for i, e in enumerate(rows['entry']) if i != 2)
    assert rows['record'] == [['Night Owl Rising', '1']]       # the relay gets the online name
    # a name longer than 16 in the file is cut as the editor cuts it
    env['STUB_NAMES'] = '3C3C3C3C3C3C/NIGHTOWL=  Night Owl Rising Again  '
    assert run(exe, save_area, env)['entry'][2][1] == 'Night Owl Rising'


def test_set_and_clear_online_name(exe, save_area):
    env = {'DEFJAM_DATA': str(save_area / 'data')}
    save = save_area / 'data/save/UserData/45410049/1A3F1B2E4ADD/CPTWO'
    before = save.read_bytes()
    rows = run(exe, save_area, env, 'name', 1, 'Brooklyn Bruiser')
    assert rows['named'] == [['1']]
    assert rows['names'] == [['1A3F1B2E4ADD/CPTWO=Brooklyn Bruiser']]
    first, second = rows['entry'][:4], rows['entry'][4:]
    assert first[1][1] == 'CPTWO' and second[1][1] == 'Brooklyn Bruiser' and second[1][5] == 'CPTWO'
    assert second[1][4] != first[1][4]
    assert save.read_bytes() == before                         # the Story save keeps its user ID
    # read back from the setting: the same record
    env['STUB_NAMES'] = rows['names'][0][0]
    again = run(exe, save_area, env)
    assert again['entry'][1][1] == 'Brooklyn Bruiser' and again['entry'][1][4] == second[1][4]
    # cleared: the entry leaves the setting, the record is the user ID's again (the same bytes)
    rows = run(exe, save_area, env, 'name', 1, '')
    assert rows['named'] == [['1']] and rows['names'] == [['']]
    assert rows['entry'][5][1] == 'CPTWO' and rows['entry'][5][6] == '' and rows['entry'][5][4] == first[1][4]
    # no such entry
    assert run(exe, save_area, env, 'name', 7, 'X')['named'] == [['0']]


def test_online_name_escaped_in_setting(exe, save_area):
    env = {'DEFJAM_DATA': str(save_area / 'data'), 'STUB_NAMES': '0C6F8520AAF5/AW5J3555=Ace'}
    rows = run(exe, save_area, env, 'name', 0, 'a;b=c%d')
    assert rows['names'] == [['0C6F8520AAF5/AW5J3555=Ace;4D4D4D4D4D4D/LATEST=a%3Bb%3Dc%25d']]
    env['STUB_NAMES'] = rows['names'][0][0]
    rows = run(exe, save_area, env)
    assert rows['entry'][0][1] == 'a;b=c%d' and rows['entry'][3][1] == 'Ace'


def test_profiles_put_aside_are_listed_under_their_game_folder(exe, save_area):
    """<save>/aside (the launcher's Put aside, save_manager.h): the game never reads it, the list does,
    keyed by the game folder each profile came from ("<folder>~2" is a second one of that folder), so
    its online name and the pick stay with it. A key already listed from the game's folder wins; an
    unfinished move (.moving-*) is not read."""
    aside = save_area / 'data/save/aside'
    now = time.time()
    for stash, name, age in (('5E5E5E5E5E5E', 'BENCHED', 10), ('6F6F6F6F6F6F~2', 'SECONDOF', 400),
                             ('4D4D4D4D4D4D~2', 'LATEST', 5), ('.moving-7A7A7A7A7A7A', 'HALFWAY', 1)):
        d = aside / stash
        d.mkdir(parents=True)
        f = d / name
        f.write_bytes(save_file(name))
        os.utime(f, (now - age, now - age))
    env = {'DEFJAM_DATA': str(save_area / 'data'), 'STUB_NAMES': '5E5E5E5E5E5E/BENCHED=Bench Boss'}
    rows = run(exe, save_area, env)
    assert names(rows) == ['Bench Boss', 'LATEST', 'CPTWO', 'NIGHTOWL', 'AW5J3555', 'SECONDOF']
    keys = [e[2] for e in rows['entry']]
    assert keys[0] == '5E5E5E5E5E5E/BENCHED' and keys[-1] == '6F6F6F6F6F6F/SECONDOF'
    assert keys.count('4D4D4D4D4D4D/LATEST') == 1 and 'HALFWAY' not in ''.join(keys)
    assert rows['entry'][0][5] == 'BENCHED' and rows['entry'][0][6] == 'Bench Boss'
    assert rows['pick'] == [['0']] and rows['record'] == [['Bench Boss', '1']]
    env['STUB_PICK_KEY'] = '6F6F6F6F6F6F/SECONDOF'
    rows = run(exe, save_area, env)
    assert rows['pick'] == [['5']] and rows['record'] == [['SECONDOF', '1']]

