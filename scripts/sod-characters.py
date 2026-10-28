#!/usr/bin/env python3
"""List the Character records of the game's database (assets/djv2.sod) in the user's own dump.

Battle mode does not take a fighter from fighter.xml: Game.StartGame (sub_00046E40) walks the
Character records of this database and copies the one whose front-end ID equals the fighter ID
Controller.ControllerSetup was given (docs/research/crash-001B2480.md, section 11). An ID without a
record leaves the match slot empty and the load crashes, so the IDs printed here are the only
fighters an online match may use (src/hooks/online_data.c, netplay/relay/protocol_lobby.go;
tests/unit/test_battle_roster.py compares them).

    python scripts/sod-characters.py [--data <DEFJAM_DATA>] [--ids]

Prints front-end ID, coreID and name per record, or with --ids only the IDs, comma-separated.
Reads metadata only and never writes game bytes anywhere.

The file (all little-endian): the header holds the offsets of the schema (+0xC), the tables
(+0x10) and the strings (+0x14). The schema maps (table key XOR field key) to (field index, type);
a table holds per class a block of record keys and record offsets; a record is an array of words,
the field index its word. Record keys and string keys are the same hashes, so a record's key
names it.
"""
import argparse
import os
import struct
import sys
from pathlib import Path

CHARACTER = 0x4CE5B1B2      # the Character table, and its record class
FE_ID = 0xB03E16FC          # field: front-end ID
CORE_ID = 0x6FC83815        # field: coreID


def _u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


def characters(data):
    """[(fe_id, core_id, name)] of every Character record of a djv2.sod image, by front-end ID."""
    if data[:4] != b'SODB':
        raise ValueError('not a SODB database: %r' % data[:4])
    schema, tables, strings = _u32(data, 0xC), _u32(data, 0x10), _u32(data, 0x14)
    n = _u32(data, schema + 4)
    skeys = struct.unpack_from('<%dI' % n, data, schema + 12)
    spairs = struct.unpack_from('<%dI' % (2 * n), data, schema + 12 + 4 * n)
    field = {k: spairs[2 * i] for i, k in enumerate(skeys)}
    nt = _u32(data, tables + 4)
    tkeys = struct.unpack_from('<%dI' % nt, data, tables + 12)
    toffs = struct.unpack_from('<%dI' % nt, data, tables + 12 + 4 * nt)
    names = {}
    for i in range(_u32(data, strings + 4)):
        h, o = struct.unpack_from('<II', data, strings + 8 + 8 * i)
        names[h] = data[o:data.index(b'\0', o)].decode('latin-1')
    t = toffs[tkeys.index(CHARACTER)]
    ncls = _u32(data, t + 0x10)
    classes = struct.unpack_from('<%dI' % ncls, data, t + 0x18)
    blocks = struct.unpack_from('<%dI' % ncls, data, t + 0x18 + 4 * ncls)
    block = blocks[classes.index(CHARACTER)]
    count, stride = _u32(data, block + 4), _u32(data, block + 8)
    keys = struct.unpack_from('<%dI' % count, data, block + 12)
    recs = struct.unpack_from('<%dI' % (count * stride), data, block + 12 + 4 * count)
    fe, core = field[CHARACTER ^ FE_ID], field[CHARACTER ^ CORE_ID]
    out = []
    for i in range(count):
        r = recs[i * stride]
        out.append((_u32(data, r + 4 * fe), _u32(data, r + 4 * core), names.get(keys[i], '#%08X' % keys[i])))
    return sorted(out)


def sod_path(data_root=None):
    """The dump's djv2.sod: <data>/extracted/assets, or the checkout's game junction; None if absent."""
    roots = []
    root = data_root or os.environ.get('DEFJAM_DATA')
    if root:
        roots.append(Path(root) / 'extracted')
    roots.append(Path(__file__).resolve().parents[1] / 'game')
    for r in roots:
        p = r / 'assets' / 'djv2.sod'
        if p.is_file():
            return p
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--data', help='the data root (default: $DEFJAM_DATA, then ./game)')
    ap.add_argument('--ids', action='store_true', help='only the front-end IDs, comma-separated')
    args = ap.parse_args()
    path = sod_path(args.data)
    if not path:
        sys.exit('assets/djv2.sod not found (set DEFJAM_DATA or pass --data)')
    recs = characters(path.read_bytes())
    if args.ids:
        print(','.join(str(fe) for fe, _, _ in recs))
    else:
        for fe, core, name in recs:
            print('%3d  core %3d  %s' % (fe, core, name))
        print('%d Character records' % len(recs))


if __name__ == '__main__':
    main()
