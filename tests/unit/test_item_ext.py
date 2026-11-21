# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""New items without a new save layout (src/hooks/item_ext_logic.c, compiled on its own with a driver).

The game sorts each item container by ID and keeps an item's 2-bit state at its position; the six state arrays
of a profile slot follow one another, each sized from its container's count (docs/research/unlock-everything.md).
A database with new rows or colours therefore moves the states of retail items (community/clothing-packs-test.md).
These tests build the containers as the game does (one item per row, or per colour / jewellery entry, then sorted)
for a base database and for the same database with new items, and check that after item_ext's partition every
retail item has the position it has with the base, and that the slot layout from the retail counts is the base's:
so a profile's bytes mean the same with and without the new items. With the user's own dump present the same on
the real database plus an Eyepatch row (the official new item), a pack-style colour and enough items to cross
an array's padding."""
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys

import pytest

import test_save_unlock as su   # noqa: E402  (the synthetic database)

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'setup'))

DRIVER = r'''
#include "item_ext_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); unsigned char *d; long k;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); k = ftell(f); fseek(f, 0, SEEK_SET);
    d = (unsigned char *)malloc((size_t)k + 1); *n = fread(d, 1, (size_t)k, f); fclose(f); return d;
}
/* profile order: hair, tattoo, jewellery, clothing, Blazin, style */
static const unsigned kinds[6] = {0x7450591Au, 0x30948EA0u, 0x426B8667u, 0x912016DDu, 0x47D305FDu, 0x906455C6u};
static int cmp(const void *a, const void *b) {
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b; return x < y ? -1 : x > y;
}
/* the items of one kind as the game builds them: row-major, then qsort by ID (sub_000151C0, vtable +4) */
static unsigned build(const ItemExtBase *b, unsigned hash, unsigned *ids) {
    int t; unsigned r, c, n = 0;
    for (t = 0; t < b->tables; t++) if (b->hash[t] == hash) break;
    if (t == b->tables) return 0;
    for (r = 0; r < b->rows[t]; r++) {
        unsigned k = b->colours[t] ? b->colours[t][r] : 1;
        for (c = 0; c < k; c++) ids[n++] = (unsigned)t << 24 | c << 16 | r;
    }
    qsort(ids, n, sizeof(unsigned), cmp);
    return n;
}
int main(int argc, char **argv) {
    ItemExtBase base, cur; size_t n1 = 0, n2 = 0; unsigned char *d1, *d2; static unsigned ids[65536];
    unsigned k, i, counts[6], retail[6], offsets[6];
    if (argc < 3) return 2;
    d1 = slurp(argv[2], &n1);
    if (!item_ext_base_parse(d1, n1, &base)) { printf("error base\n"); return 1; }
    if (!strcmp(argv[1], "base")) {
        for (k = 0; k < 6; k++) { printf("%u", build(&base, kinds[k], ids)); putchar(k < 5 ? ' ' : '\n'); }
        return 0;
    }
    /* layout BASE CUR [plain]: each kind's items of CUR (partitioned with BASE unless plain) and the slot offsets */
    d2 = slurp(argv[3], &n2);
    if (!item_ext_base_parse(d2, n2, &cur)) { printf("error cur\n"); return 1; }
    for (k = 0; k < 6; k++) {
        counts[k] = build(&cur, kinds[k], ids);
        retail[k] = argc > 4 ? counts[k] : item_ext_partition(&base, ids, counts[k]);
        printf("kind %u count %u retail %u ids", k, counts[k], retail[k]);
        for (i = 0; i < counts[k]; i++) printf(" %08X", ids[i]);
        putchar('\n');
        for (i = 0; i < counts[k]; i++)
            if (item_ext_find(ids, counts[k], retail[k], ids[i]) != (int)i) { printf("find failed %08X\n", ids[i]); return 1; }
        if (item_ext_find(ids, counts[k], retail[k], 0xFFFFFFFFu) != -1) { printf("find absent failed\n"); return 1; }
    }
    item_ext_layout_offsets(retail, offsets);
    printf("offsets");
    for (k = 0; k < 6; k++) printf(" %u", offsets[k]);
    putchar('\n');
    return 0;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    folder = tmp_path_factory.mktemp('item-ext-native')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    sources = [str(ROOT / 'src/hooks/item_ext_logic.c')]
    include = ROOT / 'src/hooks'
    compiler = shutil.which('cl')
    if compiler:
        exe = folder / 'item_ext.exe'
        cmd = [compiler, '/nologo', '/O2', '/W3', '/I' + str(include), str(folder / 'driver.c'), *sources,
               '/Fe:' + str(exe)]
    else:
        compiler = shutil.which('clang') or shutil.which('gcc')
        if not compiler:
            pytest.skip('C compiler required')
        exe = folder / 'item_ext'
        cmd = [compiler, '-O2', '-I' + str(include), str(folder / 'driver.c'), *sources, '-o', str(exe)]
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def run(tool, *args):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def layout(tool, base, cur, plain=False):
    """{kind: (count, retail, [ids])}, offsets"""
    out = run(tool, 'layout', base, cur, *(['plain'] if plain else []))
    kinds, offsets = {}, None
    for line in out.splitlines():
        w = line.split()
        if w[0] == 'kind':
            kinds[int(w[1])] = (int(w[3]), int(w[5]), w[7:])
        elif w[0] == 'offsets':
            offsets = [int(x) for x in w[1:]]
    return kinds, offsets


def states_of(kinds, offsets, bits):
    """{id: state} as the game reads them from a profile slot's state bytes `bits` (from slot +0x4DC4)."""
    out = {}
    for k, (count, retail, ids) in kinds.items():
        for i, item in enumerate(ids[:retail]):
            byte = bits[offsets[k] + i // 4]
            out[item] = (byte >> (i % 4 * 2)) & 3
    return out


def write(tmp_path, name, blob):
    p = tmp_path / name
    p.write_bytes(blob)
    return p


def grown_tables(extra_rows=2, extra_colours=((0, 2),)):
    """sample_tables with new clothing rows (one colour each), more colours on retail rows and a jewellery row."""
    t = su.sample_tables()
    h, rows = t[16]
    rows = list(rows)
    for row, more in extra_colours:
        rows[row] += more
    rows += [1] * extra_rows
    t[16] = (h, rows)
    hj, jrows = t[24]
    t[24] = (hj, list(jrows) + [3])
    hh, hrows = t[14]
    t[14] = (hh, list(hrows) + [None] * 5)
    return t


def test_base_has_the_counts_the_game_builds(tool, tmp_path):
    base = write(tmp_path, 'base.sod', su.make_sod(su.sample_tables()))
    assert run(tool, 'base', base).split() == ['9', '30', '8', '10', '82', '5']


def test_no_new_items_changes_nothing(tool, tmp_path):
    base = write(tmp_path, 'base.sod', su.make_sod(su.sample_tables()))
    assert layout(tool, base, base) == layout(tool, base, base, plain=True)


def test_new_items_keep_every_retail_position_and_the_layout(tool, tmp_path):
    base = write(tmp_path, 'base.sod', su.make_sod(su.sample_tables()))
    cur = write(tmp_path, 'cur.sod', su.make_sod(grown_tables(extra_rows=9)))
    want, want_off = layout(tool, base, base)
    got, got_off = layout(tool, base, cur)
    plain, plain_off = layout(tool, base, cur, plain=True)
    assert got_off == want_off
    for k, (count, retail, ids) in want.items():
        g_count, g_retail, g_ids = got[k]
        assert g_retail == count and g_ids[:g_retail] == ids
    # what the game does without it: clothing positions move and the arrays after clothing move
    assert plain[3][2][:want[3][0]] != want[3][2]
    assert plain_off[4] != want_off[4]
    # and a profile written with the base reads the same with the new items
    rnd = random.Random(5)
    bits = bytes(rnd.getrandbits(8) for _ in range(64))
    assert states_of(got, got_off, bits) == states_of(want, want_off, bits)
    assert states_of(plain, plain_off, bits) != states_of(want, want_off, bits)


def test_new_items_sit_after_the_retail_ones_sorted(tool, tmp_path):
    base = write(tmp_path, 'base.sod', su.make_sod(su.sample_tables()))
    cur = write(tmp_path, 'cur.sod', su.make_sod(grown_tables()))
    got, _ = layout(tool, base, cur)
    count, retail, ids = got[3]
    new = ids[retail:]
    assert count == 10 + 2 + 2 and retail == 10
    assert new == sorted(new)
    # the two new rows (4, 5) and the two new colours of row 0 (colours 3, 4)
    assert sorted(new) == sorted(['10000004', '10000005', '10030000', '10040000'])


def test_unreadable_base_counts_everything_retail(tool, tmp_path):
    bad = write(tmp_path, 'bad.sod', b'not a database')
    result = subprocess.run([str(tool), 'base', str(bad)], capture_output=True, text=True)
    assert result.returncode == 1 and 'error base' in result.stdout


# ---- the user's own dump ------------------------------------------------------------------------------------

def dump_sod():
    for p in (ROOT / 'game' / 'assets' / 'djv2.sod',):
        if p.is_file():
            return p
    return None


def eyepatch_sod(blob):
    """The dump's database with the official Eyepatch row (an eyewear item, model 10) as setup composes it, one more
    colour on a retail eyewear row and four more new clothing items (enough to grow the clothing array)."""
    from ffnycontent import clothing, sod
    db = sod.Sod(blob)
    eyewear = [r for r in db.clothing() if r.type == 15]
    template = eyewear[0]
    ops = [{"op": "item", "template": template.name, "name": "Eyepatch", "price": 0, "bundle": "",
            "group": template.group, "row_name": "FFNY_Eyepatch", "model": {"num": 10, "name": "V2CP_144_0010_C"},
            "colours": [{"number": 1, "label": "Black", "texture": "V2CP_144_0010_%s_01" % template.group}],
            "variants": [], "section": "Eyew"}]
    for i in range(4):
        ops.append({"op": "item", "template": template.name, "name": "Test %d" % i, "price": 10, "bundle": "",
                    "group": template.group, "row_name": "FFNY_Test%d" % i,
                    "colours": [{"number": 1, "label": "C", "texture": "V2CP_144_0001_%s_01" % template.group}],
                    "variants": [], "section": "Eyew"})
    new_sod, _, _ = clothing.apply_ops(blob, None, {}, ops)
    return new_sod


@pytest.mark.skipif(dump_sod() is None, reason="the user's own dump (game/assets/djv2.sod) is not here")
def test_dump_with_the_eyepatch_keeps_the_retail_layout(tool, tmp_path):
    blob = dump_sod().read_bytes()
    base = write(tmp_path, 'base.sod', blob)
    cur = write(tmp_path, 'cur.sod', eyepatch_sod(blob))
    assert run(tool, 'base', base).split() == ['93', '30', '405', '1454', '82', '5']
    want, want_off = layout(tool, base, base)
    got, got_off = layout(tool, base, cur)
    plain, plain_off = layout(tool, base, cur, plain=True)
    assert got[3][0] == 1454 + 5 and got[3][1] == 1454
    assert got_off == want_off == [0, 24, 32, 134, 498, 519]       # hair .. style at slot +0x4DC4 + these
    assert all(got[k][2][:got[k][1]] == want[k][2] for k in want)
    assert plain_off[4] != want_off[4]          # without item_ext the Blazin array moves
    rnd = random.Random(7)
    bits = bytes(rnd.getrandbits(8) for _ in range(want_off[5] + 2))
    assert states_of(got, got_off, bits) == states_of(want, want_off, bits)


# ---- whole saves: a retail profile read and written back through item_ext ----------------------------------------
#
# A profile file (0x5990 bytes, 'DJFFNY::User ...') holds the slot from +0x80; its item states start at slot +0x4DC4
# (test_save_unlock.profile_blob). The game reads and writes a state through the container methods only
# (item_ext.h); these helpers do what item_ext's m_get / m_set do, on the file's bytes.

SLOT = 0x80
STATES = SLOT + 0x4DC4


def read_states(kinds, offsets, blob, new_state=2):
    """{id: state} of every item as an item_ext build reads it (new items: owned, no bits)."""
    out = {}
    for k, (count, retail, ids) in kinds.items():
        for i, item in enumerate(ids):
            out[item] = (blob[STATES + offsets[k] + i // 4] >> (i % 4 * 2)) & 3 if i < retail else new_state
    return out


def write_states(kinds, offsets, blob, states):
    """The bytes after an item_ext build set every item of `states` (m_set: mask, then OR; new items dropped)."""
    b = bytearray(blob)
    for k, (count, retail, ids) in kinds.items():
        for i, item in enumerate(ids[:retail]):
            if item in states:
                at, sh = STATES + offsets[k] + i // 4, i % 4 * 2
                b[at] = (b[at] & ~(3 << sh) & 0xFF) | (states[item] << sh)
    return bytes(b)


def synthetic_profiles():
    """test_save_unlock's profiles with random item states (every array, its padding bits too)."""
    out = []
    for seed in (1, 2, 3):
        b = bytearray(su.profile_blob(seed=seed))
        rnd = random.Random(seed)
        b[STATES:STATES + 0x220] = bytes(rnd.getrandbits(8) for _ in range(0x220))
        out.append(bytes(b))
    return out


def real_profiles():
    """Copies of real profiles (FFNY_TEST_SAVES: a folder; every 0x5990-byte 'DJFFNY::User' file below it). Never the
    player's own save area: point it at a copy (dj.py roots, a converted PS2 save)."""
    folder = os.environ.get('FFNY_TEST_SAVES')
    if not folder or not Path(folder).is_dir():
        return []
    out = []
    for p in sorted(Path(folder).rglob('*')):
        if p.is_file() and p.stat().st_size == 0x5990:
            blob = p.read_bytes()
            if blob[:4] == b'20CM' and blob[0x4C:0x58] == b'DJFFNY::User':
                out.append((p.name, blob))
    return out


def roundtrip_checks(tool, tmp_path, base_blob, cur_blob, profiles):
    base = write(tmp_path, 'base.sod', base_blob)
    cur = write(tmp_path, 'cur.sod', cur_blob)
    want, want_off = layout(tool, base, base)          # the retail game (and every build before item_ext)
    got, got_off = layout(tool, base, cur)             # an item_ext build with the new items
    new_ids = [i for k in got for i in got[k][2][got[k][1]:]]
    assert new_ids, 'the composed database has no new item'
    end = STATES + want_off[5] + want[5][0] // 4 + 1
    for name, blob in profiles:
        # 1. loaded and saved again, unchanged: byte for byte the file it was
        held = read_states(got, got_off, blob)
        assert write_states(got, got_off, blob, held) == blob, name
        # 2. every retail item reads in the item_ext build what the retail game reads; the new ones read as owned
        retail = read_states(want, want_off, blob)
        assert {i: s for i, s in held.items() if i in retail} == retail, name
        assert all(held[i] == 2 for i in new_ids), name
        # 3. the item_ext build buys a retail item and 'buys' the new ones: only that item's two bits change, the
        #    retail game reads the purchase, and reading back in the item_ext build gives what it held
        kinds3 = got[3][2][:got[3][1]]
        item = next(i for i in kinds3 if held[i] != 3)
        change = dict({n: 1 for n in new_ids}, **{item: 3})
        saved = write_states(got, got_off, blob, change)
        diff = [i for i in range(len(blob)) if saved[i] != blob[i]]
        assert len(diff) == 1 and STATES <= diff[0] < end, (name, diff)
        after = read_states(want, want_off, saved)
        assert after == dict(retail, **{item: 3}), name
        assert read_states(got, got_off, saved) == dict(held, **{item: 3}), name
        # 4. and without item_ext the same profile reads other states (what the test guards against)
        plain, plain_off = layout(tool, base, cur, plain=True)
        assert {i: s for i, s in read_states(plain, plain_off, blob).items() if i in retail} != retail, name


def test_retail_profiles_round_trip_byte_identical_with_new_items(tool, tmp_path):
    """Synthetic database and profiles: load + save is the identity, purchases touch only their bits."""
    roundtrip_checks(tool, tmp_path, su.make_sod(su.sample_tables()), su.make_sod(grown_tables(extra_rows=9)),
                     [('synthetic-%d' % k, b) for k, b in enumerate(synthetic_profiles())])


@pytest.mark.skipif(dump_sod() is None, reason="the user's own dump (game/assets/djv2.sod) is not here")
def test_dump_profiles_round_trip_byte_identical_with_the_eyepatch(tool, tmp_path):
    """The dump's database plus the Eyepatch (and four more new items); synthetic profiles, and with
    FFNY_TEST_SAVES copies of real ones."""
    blob = dump_sod().read_bytes()
    roundtrip_checks(tool, tmp_path, blob, eyepatch_sod(blob),
                     [('synthetic-%d' % k, b) for k, b in enumerate(synthetic_profiles())] + real_profiles())


@pytest.mark.skipif(dump_sod() is None or not real_profiles(), reason='needs the dump and FFNY_TEST_SAVES')
def test_real_profiles_are_found():
    assert real_profiles()
