# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
# Ported from FFNY Studio (ffnystudio/sodwrite.py) for the player-side install.
"""The item database (``djv2.sod``) beyond Clothing rows: availability bundles, and appending to it.

Reverse engineered on 2026-11-10 from the player's own dump (see FFNY Studio ``docs/ffny-studio-clothing.md``):

* A table header is ``hash, ptr classes, ptr lists, row count`` followed by two maps of the block form
  ``count, stride, keys[count], values[count * stride]``: the classes (class hash -> class block) and the plain
  row lists (list hash -> list block). Class block 0 holds the rows keyed by row-name hash (sorted; a row's
  position is its ID). For Clothing, class block 1 is an index by type (keys 0..15, sorted), and one plain list
  (stride 0) holds every row pointer once.
* The bundle table (hash ``0x51060F0D``) holds rewards and shipments. A bundle row is ``name, cash, list of
  elements (0, item ID, flags), ...``; ``PlayerDefault`` is what a new fighter owns, ``Ship_Clothes_001..010`` are
  the clothing shop's (SUS, Snowman's) shipments, ``Ship_Jewels_*`` Jacob's, ``Ship_Tats_*`` Manny's,
  ``Ship_Hair_*`` Stingray's.
* The game keeps the file as loaded and adds its base whenever it follows an offset (``sub_000BE560``), so new
  data may sit anywhere; the editor appends and re-points.

Standard library only.
"""

import struct

from .sod import Sod, name_hash

BUNDLES = 0x51060F0D        # table hash: reward and shipment bundles
DEFAULT_BUNDLE = "PlayerDefault"
ITEM_FLAGS = 0x1F           # the flags word almost every bundle element carries
MAX_COLOURS = 16            # the created-fighter record check allows colour indices 0..15


def table_by_hash(db, table_hash):
    return db.table_hashes.index(table_hash)


def bundles(db):
    """{bundle name: (row offset, [(item ID, flags)])} of the bundle table."""
    try:
        table = table_by_hash(db, BUNDLES)
    except ValueError:
        return {}
    _, ptrs = db.rows(table)
    out = {}
    for p in ptrs:
        name = db.string(db.u32(p)) or "#%d" % db.u32(p)
        items = [struct.unpack_from("<II", db.blob, e + 4) for e in db._list(db.u32(p + 8))]
        out[name] = (p, items)
    return out


def availability(db):
    """{clothing item ID (colour bits clear): bundle name}: where an item comes from. ``PlayerDefault`` first
    (owned from the start), then the other bundles in name order."""
    out = {}
    bs = bundles(db)
    for name in [DEFAULT_BUNDLE] + sorted(n for n in bs if n != DEFAULT_BUNDLE):
        for item, _ in bs.get(name, (0, []))[1]:
            out.setdefault(item & 0xFF00FFFF, name)
    return out


def name_above(base, floor, limit=500000):
    """``base`` or ``base`` + a short suffix whose hash is above ``floor``: new rows can only be appended in
    hash order, so their names must hash above every existing row of the table."""
    if name_hash(base) > floor:
        return base
    # each character multiplies the hash by 33, so a short suffix only reaches the low bits: seven letters, the
    # fastest-changing first, reach all 32 (33**7 > 2**32)
    letters = "abcdefghijklmnopqrstuvwxyz"
    for i in range(1, limit):
        suffix, n = "", i
        while n:
            n, k = divmod(n, 26)
            suffix += letters[k]
        name = "%s_%s" % (base, suffix.ljust(7, "a"))
        if name_hash(name) > floor:
            return name
    raise ValueError("no name above %08x found for %r" % (floor, base))


class SodEditor:
    """Appends to an item database without moving anything: new rows, lists, blocks and strings go after the last
    byte, and the few words that point at grown structures are re-pointed. Existing IDs (row positions) and string
    indices never change."""

    def __init__(self, blob):
        self.out = bytearray(blob)
        self.base = Sod(blob)
        self.new_strings = []       # (hash, text or None)

    def u32(self, off):
        return struct.unpack_from("<I", self.out, off)[0]

    def put(self, off, value):
        struct.pack_into("<I", self.out, off, value)

    def append(self, data):
        while len(self.out) % 4:
            self.out.append(0)
        off = len(self.out)
        self.out += data
        return off

    def db(self):
        return Sod(bytes(self.out))

    # -- strings ------------------------------------------------------------------------------------------

    def add_string(self, text=None, text_hash=None):
        """A new string index: ``text`` stored in the pool, or a hash-only string (``text_hash``, its text in
        the .loc files)."""
        h = name_hash(text) if text is not None else text_hash
        self.new_strings.append((h, text))
        return len(self.base.strings) + len(self.new_strings) - 1

    def string_index(self, text):
        """The index of a string with this text (existing or added), else a new one."""
        for i in range(len(self.base.strings)):
            if self.base.string(i) == text:
                return i
        for k, (_, t) in enumerate(self.new_strings):
            if t == text:
                return len(self.base.strings) + k
        return self.add_string(text)

    # -- lists --------------------------------------------------------------------------------------------

    def _grow_list(self, holder, extra):
        """Re-point the list pointer at ``holder`` to a copy of the list with ``extra`` element offsets added.
        Returns the new length."""
        old = self.u32(holder)
        n = self.u32(old)
        elems = list(struct.unpack_from("<%dI" % n, self.out, old + 4)) + list(extra)
        self.put(holder, self.append(struct.pack("<I%dI" % len(elems), len(elems), *elems)))
        return len(elems)

    def add_colour(self, row_offset, number, description):
        """Add a colour (description string index, texture Number) to the Clothing row at ``row_offset``.
        Returns its index (the colour bits of the item ID)."""
        if self.u32(self.u32(row_offset + 12)) >= MAX_COLOURS:
            raise ValueError("an item can have at most %d colours" % MAX_COLOURS)
        elem = self.append(struct.pack("<3I", 0, description, number))
        return self._grow_list(row_offset + 12, [elem]) - 1

    def new_colour_list(self, colours):
        """A fresh colour list [(description string index, Number)]; returns its offset."""
        elems = [self.append(struct.pack("<3I", 0, d, n)) for d, n in colours]
        return self.append(struct.pack("<I%dI" % len(elems), len(elems), *elems))

    def add_bundle_item(self, bundle, item_id, flags=ITEM_FLAGS):
        """List ``item_id`` in a bundle (``PlayerDefault``: owned from the start; ``Ship_Clothes_00n``: sold
        from that shipment on)."""
        found = bundles(self.db()).get(bundle)
        if found is None:
            raise KeyError("no bundle %r" % bundle)
        elem = self.append(struct.pack("<3I", 0, item_id, flags))
        self._grow_list(found[0] + 8, [elem])

    # -- rows ---------------------------------------------------------------------------------------------

    def add_row(self, table, words, key_hash, template_offset):
        """Append a row (``words``: its dwords) to ``table``; its ID is the next row index, so ``key_hash`` must
        be above every existing key. Every index of the table gets the row: class block 0 at its end, the other
        class blocks next to the template row's entries, the plain row lists at their end. Returns the item ID."""
        db = self.db()
        off = db.table_offsets[table]
        rows = self.u32(off + 12)
        classes = self.u32(off + 0x10)
        row = self.append(struct.pack("<%dI" % len(words), *words))
        for c in range(classes):
            at = off + 0x18 + 4 * classes + 4 * c
            block = self.u32(at)
            tag, count, stride = struct.unpack_from("<4sII", self.out, block)
            if stride != 1:
                raise ValueError("class block %d of table %d has stride %d" % (c, table, stride))
            keys = list(struct.unpack_from("<%dI" % count, self.out, block + 12))
            vals = list(struct.unpack_from("<%dI" % count, self.out, block + 12 + 4 * count))
            if c == 0:
                if keys and key_hash <= max(keys):
                    raise ValueError("a new row's name hash must be above %08x" % max(keys))
                keys.append(key_hash)
                vals.append(row)
            else:
                if template_offset in vals:
                    k = keys[vals.index(template_offset)]
                    pos = max(i for i, key in enumerate(keys) if key == k) + 1
                else:
                    k, pos = 0, len(keys)
                keys.insert(pos, k)
                vals.insert(pos, row)
            self.put(at, self.append(struct.pack("<4sII", tag, len(keys), 1)
                                     + struct.pack("<%dI" % len(keys), *keys)
                                     + struct.pack("<%dI" % len(vals), *vals)))
        lists = self.u32(off + 8)
        if 0 < lists < len(self.out) - 8:
            m = self.u32(lists)
            for i in range(m):
                at = lists + 8 + 4 * m + 4 * i
                block = self.u32(at)
                if not 0 < block < len(self.out) - 12:
                    continue
                tag, count, stride = struct.unpack_from("<4sII", self.out, block)
                if stride == 0 and count == rows:
                    ptrs = list(struct.unpack_from("<%dI" % count, self.out, block + 12)) + [row]
                    self.put(at, self.append(struct.pack("<4sII", tag, len(ptrs), 0)
                                             + struct.pack("<%dI" % len(ptrs), *ptrs)))
        self.put(off + 12, rows + 1)
        return (table << 24) | rows

    def finish(self):
        """The edited file. With new strings the string table is copied to the end with them added; the old pool
        stays where it is (pool offsets are absolute)."""
        if self.new_strings:
            old = self.base.strings
            while len(self.out) % 4:
                self.out.append(0)
            start = len(self.out)
            table_size = 8 + 8 * (len(old) + len(self.new_strings))
            pool, entries = bytearray(), []
            for h, text in self.new_strings:
                if text is None:
                    entries.append((h, 0))
                else:
                    entries.append((h, start + table_size + len(pool)))
                    pool += text.encode("latin-1") + b"\0"
            blob = struct.pack("<4sI", b"STRT", len(old) + len(entries))
            blob += b"".join(struct.pack("<II", h, o) for h, o in list(old) + entries) + pool
            self.append(blob)
            self.put(20, start)
            self.base = Sod(bytes(self.out))
            self.new_strings = []
        return bytes(self.out)
