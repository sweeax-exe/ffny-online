# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Unlock everything (src/hooks/save_unlock.cpp), compiled on its own with save_manager.cpp and fed a
synthetic database, profile and Options file: which bits change and which do not, the sorted fighter
and venue sets, the songs, the cash box, the backup first, the signatures, Undo byte for byte, and the
refusals. With the user's own dump present, the item counts the game builds from it."""
from pathlib import Path
import hashlib
import hmac
import os
import random
import re
import shutil
import struct
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include "save_unlock.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace saves;
static Bytes slurp(const char *p) {
    std::ifstream f(fs::u8path(p), std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
static void spit(const char *p, const Bytes &b) {
    std::ofstream(fs::u8path(p), std::ios::binary).write((const char *)b.data(), b.size());
}
static void counts(const UnlockCounts &c) {
    std::cout << "items";
    for (int n : c.items) std::cout << " " << n;
    std::cout << "\nfighters " << c.fighters << "\nvenues " << c.venues << "\nsongs " << c.songs << "\ncash " << c.cash
              << "\ndev " << c.dev_points << "\n";
}
int main(int argc, char **argv) {
    std::string cmd = argc > 1 ? argv[1] : "", error;
    UnlockLayout l;
    if (cmd == "layout") {
        if (!unlock_layout(slurp(argv[2]), l, &error)) { std::cout << "error " << error << "\n"; return 1; }
        std::cout << "items";
        for (auto n : l.items) std::cout << " " << n;
        std::cout << "\nrows";
        for (auto n : l.rows) std::cout << " " << n;
        std::cout << "\ntypes";
        for (auto n : l.clothing_type_rows) std::cout << " " << n;
        std::cout << "\ncharacters " << l.character_table << " " << l.characters << "\nvenues " << l.venue_table << " "
                  << l.venues << "\n";
        return 0;
    }
    if (cmd == "profile" || cmd == "options") {     // <sod> <in> <out> [cash]
        if (!unlock_layout(slurp(argv[2]), l, &error)) { std::cout << "error " << error << "\n"; return 1; }
        Bytes b = slurp(argv[3]);
        UnlockCounts c;
        bool ok = cmd == "profile" ? unlock_profile_bytes(b, l, argc > 5 && atoi(argv[5]), c, &error)
                                   : unlock_options_bytes(b, l, c, &error);
        if (!ok) { std::cout << "error " << error << "\n"; return 1; }
        spit(argv[4], b);
        counts(c);
        return 0;
    }
    if (cmd == "unlock") {                          // <save> <backups> <folder> <sod> <cash> <now>
        UnlockResult r = unlock_everything(fs::u8path(argv[2]), fs::u8path(argv[3]), argv[4], slurp(argv[5]),
                                           atoi(argv[6]) != 0, atoll(argv[7]));
        std::cout << "ok " << r.ok << "\nbackup " << r.backup << "\nmessage " << r.message << "\n";
        counts(r.counts);
        return r.ok ? 0 : 1;
    }
    if (cmd == "restore") {
        std::string message;
        bool ok = restore(fs::u8path(argv[2]), fs::u8path(argv[3]), argv[4], atoll(argv[5]), &message);
        std::cout << ok << " " << message << "\n";
        return ok ? 0 : 1;
    }
    if (cmd == "backups") {
        for (auto &b : list_backups(fs::u8path(argv[2]))) std::cout << b.name << " " << b.reason << "\n";
        return 0;
    }
    return 2;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    folder = tmp_path_factory.mktemp('save-unlock-native')
    (folder / 'driver.cpp').write_text(DRIVER, encoding='utf-8')
    sources = [str(ROOT / 'src/hooks/save_manager.cpp'), str(ROOT / 'src/hooks/save_unlock.cpp')]
    include = ROOT / 'src/hooks'
    compiler = shutil.which('cl')
    if compiler:
        exe = folder / 'save_unlock.exe'
        cmd = [compiler, '/nologo', '/std:c++17', '/EHsc', '/O2', '/W3', '/I' + str(include),
               str(folder / 'driver.cpp'), *sources, '/Fe:' + str(exe)]
    else:
        compiler = shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            pytest.skip('C++ compiler required')
        exe = folder / 'save_unlock'
        cmd = [compiler, '-std=c++17', '-O2', '-I' + str(include), str(folder / 'driver.cpp'), *sources, '-o', str(exe)]
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def run(tool, *args, check=True):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True)
    if check:
        assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def parse(out):
    return dict(line.split(' ', 1) for line in out.strip().splitlines() if ' ' in line)


# ---- a synthetic database --------------------------------------------------------------------------------

def name_hash(text):
    h = 0x2B5A5
    for c in text.encode('latin-1'):
        h = ((h + c) * 33) & 0xFFFFFFFF
    return h


HAIR, TATTOO, JEWEL, CLOTH, BLAZIN, STYLE = 0x7450591A, 0x30948EA0, 0x426B8667, 0x912016DD, 0x47D305FD, 0x906455C6
CHAR, VENUE, SONG = 0x4CE5B1B2, 0xD7DCA628, 0x0E1BB13C
COLOURS, VARIANTS, TYPE = 0xAC3E66A4, 0xC2E12E98, 0x0E334DC7


def make_sod(tables):
    """tables: [(hash, [row lists])]; a row is a list of list lengths for word 3 (or None for no list)."""
    out = bytearray(24)
    def at():
        return len(out)
    def put(words):
        o = at()
        out.extend(struct.pack('<%dI' % len(words), *words))
        return o
    # schema: the two list fields at word 3
    keys = [CLOTH ^ COLOURS, JEWEL ^ VARIANTS, CLOTH ^ TYPE, 0x1234, BLAZIN ^ BLAZIN_COST]
    schema = put([0x414D4B53, len(keys), 2] + keys + [3, 0x3000000, 3, 0x3000000, 4, 0, 1, 0, 4, 0])
    offs = []
    for index, (th, rows) in enumerate(tables):
        ptrs = []
        for row, r in enumerate(rows):
            lst = 0
            if r is not None:
                lst = put([r] + [0] * r)
            ptrs.append(put([index, 0, 0, lst, row % 3]))       # word 4: a clothing row's type
        block = put([0x41544144, len(rows), 1] + list(range(len(rows))) + ptrs)
        offs.append(put([th, 0, 0, len(rows), 1, 0, th, block]))
    data = put([0x41544144, len(tables), 1] + [t for t, _ in tables] + offs)
    struct.pack_into('<4sIIIII', out, 0, b'SODB', 2, 0, schema, data, 0)
    return bytes(out)


# Song, (1-3 unused), Tattoo at 4, Blazin 6, Character 7, HairStyle 14, FightingStyle 15, Clothing 16,
# Venue 23, Jewelry 24: the real indices for the ones that have them
def sample_tables():
    t = [(0x100 + i, [None]) for i in range(25)]
    t[0] = (SONG, [None] * 5)
    t[4] = (TATTOO, [None] * 30)
    t[6] = (BLAZIN, [None] * 82)
    t[7] = (CHAR, [None] * 75)
    t[14] = (HAIR, [None] * 9)
    t[15] = (STYLE, [None] * 5)
    t[16] = (CLOTH, [3, 0, 5, 2])           # 10 items in 3 rows (row 1 has no colour): types 0, 2, 0
    t[23] = (VENUE, [None] * 24)
    t[24] = (JEWEL, [0, 2, 0, 4])           # 1 + 2 + 1 + 4 = 8 items
    return t


# ---- synthetic save files ----------------------------------------------------------------------------------

KEY = bytes(16)


def sign(blob):
    b = bytearray(blob)
    b[0x10:0x24] = hmac.new(KEY, bytes(b[0x4C:0x8C]), hashlib.sha1).digest()
    b[0x24:0x38] = hmac.new(KEY, bytes(b[0x8C:]), hashlib.sha1).digest()
    b[0x38:0x4C] = hmac.new(KEY, bytes(b[:0x38]), hashlib.sha1).digest()
    return bytes(b)


def save_file(size, identity, body):
    b = bytearray(size)
    struct.pack_into('<4sIII', b, 0, b'20CM', size, 0x40, size - 0x8C)
    b[0x4C:0x4C + len(identity)] = identity
    b[0x8C:] = body[0x8C:]
    return sign(bytes(b))


def profile_blob(seed=1, cash=500):
    rnd = random.Random(seed)
    body = bytearray(rnd.getrandbits(8) for _ in range(0x5990))
    states = 0x80 + 0x4DC4
    body[states:states + 0x400] = bytes(0x400)
    body[states] = 0b10_00_00_10        # hair 0 owned-not-unlocked (2), hair 3 owned (2)
    body[states + 1] = 0b00_00_11_00    # hair 5 owned (3)
    struct.pack_into('<I', body, 0x80 + 0x20, cash)
    struct.pack_into('<6H', body, 0x80 + 0xA8, 2, 0, 1, 0, 0, 7)       # hair, tattoo, cloth, jewel, Blazin, owned Blazins
    struct.pack_into('<16I', body, 0x80 + 0xB8, *([1] + [0] * 15))
    return save_file(0x5990, b'DJFFNY::User 17 5', body)


def options_blob(seed=2):
    rnd = random.Random(seed)
    body = bytearray(rnd.getrandbits(8) for _ in range(0x151C))
    g = 0x3C
    struct.pack_into('<II', body, g + 0x1264, 2, 3)                     # venues, fighters
    struct.pack_into('<30I', body, g + 0x126C, *([0x17000004, 0x17000009] + [0xFFFFFFFF] * 28))
    struct.pack_into('<80I', body, g + 0x12E4, *([0x07000008, 0x07800010, 0x07000040] + [0xFFFFFFFF] * 77))
    body[g + 0x1424:g + 0x1448] = bytes([0x05, 0, 0x09, 0, 0] + [0] * 31)  # songs 0 and 2 unlocked (orders 1, 2)
    struct.pack_into('<36I', body, g + 0x1448, *(list(range(5)) + [0xFFFFFFFF] * 31))
    struct.pack_into('<I', body, g + 0x14D8, 3)
    return save_file(0x151C, b'DJFFNY::Global 17 -2030886228', body)


def write_area(root, profile=None, options=None, name='PLAYER'):
    title = root / 'save' / 'UserData' / '45410049'
    title.mkdir(parents=True, exist_ok=True)
    (title / 'TitleMeta.xbx').write_bytes(b'meta')
    meta = lambda n: b'\xff\xfe' + ('Name=%s\r\n' % n).encode('utf-16-le')
    if options is not None:
        (title / 'OPTFOLDER').mkdir(exist_ok=True)
        (title / 'OPTFOLDER' / 'SaveMeta.xbx').write_bytes(meta('Options'))
        (title / 'OPTFOLDER' / 'Options').write_bytes(options)
    if profile is not None:
        (title / 'PROFFOLDER').mkdir(exist_ok=True)
        (title / 'PROFFOLDER' / 'SaveMeta.xbx').write_bytes(meta(name))
        (title / 'PROFFOLDER' / name).write_bytes(profile)
        (title / 'PROFFOLDER' / 'saveimage.xbx').write_bytes(b'IMG')
    return title


def tree(path):
    return {str(p.relative_to(path)): p.read_bytes() for p in sorted(path.rglob('*')) if p.is_file()}


@pytest.fixture
def sod(tmp_path):
    p = tmp_path / 'djv2.sod'
    p.write_bytes(make_sod(sample_tables()))
    return p


def test_layout_counts_items_as_the_game_builds_them(tool, sod):
    out = parse(run(tool, 'layout', sod))
    assert out['items'] == '9 30 8 10 82 5'
    assert out['rows'] == '9 30 4 3 82 5' and out['types'] == '2 0 1' + ' 0' * 13
    assert out['characters'] == '7 75' and out['venues'] == '23 24'


def test_profile_unlocks_every_item_and_nothing_else(tool, sod, tmp_path):
    src, dst = tmp_path / 'p', tmp_path / 'q'
    before = profile_blob()
    src.write_bytes(before)
    out = parse(run(tool, 'profile', sod, src, dst, 0))
    after = dst.read_bytes()
    states = 0x80 + 0x4DC4
    sizes = [9, 30, 8, 10, 82, 5]
    end = states + sum(n // 4 + 1 for n in sizes)
    changed = [i for i in range(len(before)) if before[i] != after[i]]
    counters = range(0x80 + 0xA8, 0x80 + 0xF8)
    assert changed and all(states <= i < end or i in counters for i in changed)    # header unsigned, cash, stats untouched
    assert struct.unpack_from('<6H', after, 0x80 + 0xA8) == (9, 30, 3, 4, 82, 7)     # rows; owned Blazins kept
    assert struct.unpack_from('<16I', after, 0x80 + 0xB8) == (2, 0, 1) + (0,) * 13
    at = states
    for n in sizes:
        for i in range(n):
            assert (after[at + i // 4] >> (i % 4 * 2)) & 1, (n, i)
        at += n // 4 + 1
    assert (after[states] & 3) == 3 and (after[states] >> 6) == 3 and (after[states + 1] >> 2 & 3) == 3   # owned stays
    assert out['items'] == '6 30 8 10 82 5' and out['cash'] == '0'
    # padding past each kind's last item is left alone: hair has 9 items, so byte 2 keeps bits 2..7
    assert after[states + 2] & 0xFC == 0


def test_cash_box_raises_only(tool, sod, tmp_path):
    src, dst = tmp_path / 'p', tmp_path / 'q'
    src.write_bytes(profile_blob(cash=500))
    assert parse(run(tool, 'profile', sod, src, dst, 1))['cash'] == '1'
    assert struct.unpack_from('<I', dst.read_bytes(), 0xA0)[0] == 999999
    src.write_bytes(profile_blob(cash=2000000))
    assert parse(run(tool, 'profile', sod, src, dst, 1))['cash'] == '0'
    assert struct.unpack_from('<I', dst.read_bytes(), 0xA0)[0] == 2000000


GYM_COST = (5, 25, 50, 100, 225, 500, 1000, 1500, 2500, 5000)      # the game's table at 0x2DBB70
GYM_CAP = 50


BLAZIN_COST = 0x0CFA401E        # "Cost": in the sample database a Blazin row costs row % 3 (its word 4)
STYLE_PRICE = 2500
ALL_MOVES = sum(r % 3 for r in range(82))       # the sample profile owns no Blazin move


def training_cost(stats):
    return sum(GYM_COST[v // 5] for s in stats for v in range(s, GYM_CAP))


def stats_blob(stats, dev, cash=500, styles=(1, 2, 3), owned_moves=()):
    blob = bytearray(profile_blob(cash=cash))
    struct.pack_into('<5I', blob, 0x80 + 0xFC, *stats)
    blob[0x80 + 0x140:0x80 + 0x143] = bytes(styles)
    blazin = 0x80 + 0x4DC4 + sum(n // 4 + 1 for n in (9, 30, 8, 10))
    for i, state in owned_moves:
        blob[blazin + i // 4] |= state << (i % 4 * 2)
    struct.pack_into('<I', blob, 0x80 + 0x28, dev)
    return bytes(blob)      # unsigned is fine: the 'profile' command edits the bytes only


def test_development_points_cover_training_to_the_cap_and_no_more(tool, sod, tmp_path):
    """With the cash box: the points to train the five stats from where they are to 50, never lowered."""
    src, dst = tmp_path / 'p', tmp_path / 'q'
    assert training_cost([0] * 5) == 272625 and training_cost([0]) == 54525
    for stats, dev in (((0, 5, 5, 0, 0), 400), ((0, 0, 0, 0, 0), 0), ((50, 49, 12, 30, 7), 10)):
        before = stats_blob(stats, dev)
        src.write_bytes(before)
        out = parse(run(tool, 'profile', sod, src, dst, 1))
        after = dst.read_bytes()
        want = training_cost(stats) + ALL_MOVES
        assert out['dev'] == str(want) and struct.unpack_from('<I', after, 0x80 + 0x28)[0] == want
        assert after[0x80 + 0xFC:0x80 + 0x144] == before[0x80 + 0xFC:0x80 + 0x144]     # stats, bonus, moves, styles
        # spending them the gym's way reaches the cap exactly: nothing is left to go past it
        left, trained = want - ALL_MOVES, list(stats)
        for i in range(5):
            while trained[i] < GYM_CAP:
                left -= GYM_COST[trained[i] // 5]
                trained[i] += 1
        assert left == 0 and trained == [GYM_CAP] * 5
        # again: nothing more
        src.write_bytes(after)
        assert parse(run(tool, 'profile', sod, src, dst, 1))['dev'] == '0'
        assert dst.read_bytes() == after


def test_development_points_also_cover_the_moves_and_styles_not_owned(tool, sod, tmp_path):
    """The gym sells Blazin moves (the row's Cost) and two more styles (2,500 each) for the same points."""
    src, dst = tmp_path / 'p', tmp_path / 'q'
    full = (50, 50, 50, 50, 50)
    cases = [
        (dict(), ALL_MOVES),                                                    # no move owned, three styles
        (dict(owned_moves=[(1, 2), (2, 3), (4, 1)]), ALL_MOVES - 1 - 2),        # owned (2, 3) are not paid again; unlocked (1) is
        (dict(styles=(0, 0xFF, 0xFF)), ALL_MOVES + 2 * STYLE_PRICE),            # two empty style slots
        (dict(styles=(4, 2, 0xFF), owned_moves=[(80, 2)]), ALL_MOVES - 2 + STYLE_PRICE),
    ]
    for kw, want in cases:
        before = stats_blob(full, 0, **kw)
        src.write_bytes(before)
        assert parse(run(tool, 'profile', sod, src, dst, 1))['dev'] == str(want), kw
        after = dst.read_bytes()
        assert struct.unpack_from('<I', after, 0x80 + 0x28)[0] == want
        assert after[0x80 + 0xFC:0x80 + 0x144] == before[0x80 + 0xFC:0x80 + 0x144]     # stats and styles untouched
    src.write_bytes(stats_blob((0, 0, 0, 0, 0), 0, styles=(0, 0xFF, 0xFF)))    # everything at once
    assert parse(run(tool, 'profile', sod, src, dst, 1))['dev'] == str(272625 + ALL_MOVES + 2 * STYLE_PRICE)


def test_development_points_are_never_lowered_or_given_without_the_box(tool, sod, tmp_path):
    src, dst = tmp_path / 'p', tmp_path / 'q'
    src.write_bytes(stats_blob((0, 0, 0, 0, 0), 300000))               # more than the training costs: kept
    assert parse(run(tool, 'profile', sod, src, dst, 1))['dev'] == '0'
    assert struct.unpack_from('<I', dst.read_bytes(), 0x80 + 0x28)[0] == 300000
    src.write_bytes(stats_blob((50, 50, 50, 50, 50), ALL_MOVES + 7))   # fully trained, the moves covered: nothing to give
    assert parse(run(tool, 'profile', sod, src, dst, 1))['dev'] == '0'
    assert struct.unpack_from('<I', dst.read_bytes(), 0x80 + 0x28)[0] == ALL_MOVES + 7
    src.write_bytes(stats_blob((60, 0xFFFFFFFF, 50, 50, 50), 7))       # an edited save above the cap: no training cost
    assert parse(run(tool, 'profile', sod, src, dst, 1))['dev'] == str(ALL_MOVES)
    src.write_bytes(stats_blob((0, 5, 5, 0, 0), 400))                  # box not ticked: untouched
    assert parse(run(tool, 'profile', sod, src, dst, 0))['dev'] == '0'
    assert struct.unpack_from('<I', dst.read_bytes(), 0x80 + 0x28)[0] == 400


def test_a_fully_trained_fighter_passes_the_online_stat_limits():
    """The cap the points are sized for is the one the clients clamp to and the relay refuses beyond
    (created_fighter.c MAX_BASE; the relay's MaxBaseStat is the same 50, with no cap on the sum)."""
    hooks = ROOT / "src" / "hooks"
    client = (hooks / 'created_fighter.c').read_text(encoding='utf-8')
    header = (hooks / 'save_unlock.h').read_text(encoding='utf-8')
    assert re.search(r'#define MAX_BASE\s+50\b', client)
    assert 'No cap on the sum of the base stats' in client
    assert re.search(r'kGymStatCap = 50;', header)
    assert 'kGymCost[10] = {5, 25, 50, 100, 225, 500, 1000, 1500, 2500, 5000}' in header


def test_options_get_every_fighter_venue_and_song(tool, sod, tmp_path):
    src, dst = tmp_path / 'o', tmp_path / 'r'
    before = options_blob()
    src.write_bytes(before)
    out = parse(run(tool, 'options', sod, src, dst))
    after = dst.read_bytes()
    g = 0x3C
    venues, fighters = struct.unpack_from('<II', after, g + 0x1264)
    assert (venues, fighters) == (24, 75)
    assert list(struct.unpack_from('<30I', after, g + 0x126C)) == [0x17000000 | r for r in range(24)] + [0xFFFFFFFF] * 6
    ids = list(struct.unpack_from('<80I', after, g + 0x12E4))
    assert [i & 0xFF7FFFFF for i in ids[:75]] == [0x07000000 | r for r in range(75)] and ids[75:] == [0xFFFFFFFF] * 5
    assert ids[0x10] == 0x07800010                      # its own flag kept
    assert list(after[g + 0x1424:g + 0x1429]) == [0x05, 1 | 3 << 2, 0x09, 1 | 4 << 2, 1 | 5 << 2]
    assert struct.unpack_from('<I', after, g + 0x14D8)[0] == 6
    assert out['fighters'] == '72' and out['venues'] == '22' and out['songs'] == '3'
    changed = [i for i in range(len(before)) if before[i] != after[i]]
    assert min(changed) >= g + 0x1264 and max(changed) < g + 0x14DC


def test_unlock_backs_up_signs_and_undo_is_byte_identical(tool, sod, tmp_path):
    title = write_area(tmp_path, profile_blob(), options_blob())
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    original = tree(save / 'UserData')
    out = parse(run(tool, 'unlock', save, backups, 'PROFFOLDER', sod, 1, 1_800_000_000))
    assert out['ok'] == '1' and out['backup'] and 'everything is unlocked' in out['message']
    assert tree(backups / out['backup'] / 'UserData') == original
    assert '%s unlock' % out['backup'] in run(tool, 'backups', backups)
    for f in (title / 'PROFFOLDER' / 'PLAYER', title / 'OPTFOLDER' / 'Options'):
        b = f.read_bytes()
        assert sign(b) == b                              # all three signatures valid with the port's key
    assert not list(title.rglob('*.unlocking'))
    assert tree(save / 'UserData') != original
    run(tool, 'restore', save, backups, out['backup'], 1_800_000_100)
    assert tree(save / 'UserData') == original


def test_unlock_twice_changes_nothing_more(tool, sod, tmp_path):
    title = write_area(tmp_path, profile_blob(), options_blob())
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    run(tool, 'unlock', save, backups, 'PROFFOLDER', sod, 0, 1_800_000_000)
    once = tree(save / 'UserData')
    out = parse(run(tool, 'unlock', save, backups, 'PROFFOLDER', sod, 0, 1_800_000_200))
    assert tree(save / 'UserData') == once
    assert out['items'] == '0 0 0 0 0 0' and out['fighters'] == '0' and out['songs'] == '0'


@pytest.mark.parametrize('case', ['no-options', 'foreign-key', 'not-user', 'wrong-folder', 'full-set'])
def test_refusals_change_nothing(tool, tmp_path, case):
    tables = sample_tables()
    profile, options = profile_blob(), options_blob()
    if case == 'no-options':
        options = None
    elif case == 'foreign-key':
        profile = profile[:0x10] + bytes(20) + profile[0x24:]
    elif case == 'not-user':
        b = bytearray(profile)
        b[0x4C:0x50] = b'XXXX'
        profile = sign(bytes(b))
    elif case == 'full-set':
        tables[23] = (VENUE, [None] * 31)               # more venues than the game's set holds
    sod = tmp_path / 'djv2.sod'
    sod.write_bytes(make_sod(tables))
    write_area(tmp_path, profile, options)
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    before = tree(save)
    out = parse(run(tool, 'unlock', save, backups, 'NOPE' if case == 'wrong-folder' else 'PROFFOLDER', sod, 1, 1_800_000_000,
                    check=False))
    assert out['ok'] == '0' and out['message'].startswith('Nothing was changed')
    assert tree(save) == before and not backups.exists()


def test_the_dumps_item_counts(tool):
    """The user's own dump, when it is there: the counts the game's own code builds (93 hair styles, 30
    tattoos, 405 jewellery, 1454 clothing, 82 Blazins, 5 styles; 75 fighters, 24 venues)."""
    roots = [Path(os.environ['DEFJAM_DATA']) / 'extracted'] if os.environ.get('DEFJAM_DATA') else []
    roots.append(ROOT / 'game')
    sod = next((r / 'assets' / 'djv2.sod' for r in roots if (r / 'assets' / 'djv2.sod').is_file()), None)
    if not sod:
        pytest.skip('no dump')
    out = parse(run(tool, 'layout', sod))
    assert out['items'] == '93 30 405 1454 82 5'
    assert out['rows'] == '93 30 120 326 82 5'
    # what a finished profile (a converted PS2 sample) holds in its per-type counters
    assert out['types'] == '40 15 21 11 36 23 15 19 18 7 6 0 73 13 7 22'
    assert out['characters'] == '7 75' and out['venues'] == '23 24'
