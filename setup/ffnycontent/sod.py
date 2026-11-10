# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
# Ported from FFNY Studio (ffnystudio/sod.py) for the player-side install.
"""The game's item database ``assets/djv2.sod`` (``SODB``), read far enough for clothing.

Layout (little-endian dwords; every pointer is an absolute file offset, the loader adds its base):

* header: ``SODB``, version 2, a hash, then the offsets of the schema (``SKMA``), the tables (``DATA``) and the
  string table (``STRT``). The game checks only the magic and the version.
* a block is ``tag, count, stride, keys[count], values[count * stride]``. ``DATA`` maps table-name hashes
  (sorted) to table offsets; the table index is the top byte of an item ID.
* a table: name hash, two pointers, row count, class count, a word, the class hashes, then one block offset per
  class. Class block 0 holds the rows: keys are the row-name hashes **sorted**, values the row pointers. A row's
  index there is its ID (``table << 24 | row``), and a saved fighter's recipe holds such IDs (with the colour in
  bits 16-23), so rows can only be appended, and only with a name hash above every existing one.
* ``STRT``: ``count`` pairs (name hash, pool offset), then the pool; rows refer to strings by index. Offset 0
  is a hash-only string (the text comes from the localisation).
* names hash as ``h = 0x2b5a5; h = (h + byte) * 33`` (32 bits), case-sensitive.

A Clothing row is nine dwords: name (string), model number, texture group (string, "A01"), colour list, cost,
display name (string), variant list, store order, type (0-15). A list is ``count, pointers to elements``;
a colour element is ``(0, description string, Number)``, a variant element ``(0, number delta, body-type
flags)``. The game builds part names from them (``sub_0001FE70``): model ``V2CP_<cat>_<num + delta>_C`` and
texture ``V2CP_<cat>_<num>_<group>_<Number>`` with ``cat`` from the type (:data:`CATEGORIES`), then selects the
part by those names in ``createplayer_parts.xml``. The fighter's default outfit is looked up by row name
(``SW_Generic Hoody``, ``Jeans_Baggy``, ``Shoes_Hiking``).

Reverse engineered on 2026-10-26 (FFNY Studio ``docs/ffny-studio-export.md``, "New parts"); standard library only.
"""

import struct
from dataclasses import dataclass, field

MAGIC = b"SODB"
CLOTHING = "Clothing"
CATEGORIES = (111, 112, 113, 114, 115, 121, 122, 123, 131, 132, 133, 134, 141, 142, 143, 144)
TYPE_NAMES = ("jackets", "jerseys", "shirts", "sweaters", "t-shirts", "jeans", "pants", "shorts",
              "athletic", "casual", "boots", "shoes (11)", "headwear", "armbands", "gloves", "eyewear")


def name_hash(text):
    h = 0x2B5A5
    for c in text.encode("latin-1"):
        h = ((h + c) * 33) & 0xFFFFFFFF
    return h


@dataclass
class ClothingRow:
    index: int
    id: int
    name: str
    num: int
    group: str
    colours: list            # [(description string index, Number)]
    cost: int
    display: str
    variants: list           # [(number delta, body-type flags)]
    order: int
    type: int
    offset: int              # file offset of the row
    colour_offsets: list = field(default_factory=list)   # file offset of each colour's Number

    @property
    def category(self):
        return CATEGORIES[self.type]

    def model_name(self, variant=0):
        return "V2CP_%03d_%04d_C" % (self.category, self.num + self.variants[variant][0])

    def texture_name(self, colour=0):
        return "V2CP_%03d_%04d_%s_%02d" % (self.category, self.num, self.group, self.colours[colour][1])

    @property
    def num_offset(self):
        return self.offset + 4


class Sod:
    def __init__(self, blob):
        self.blob = bytes(blob)
        if self.blob[:4] != MAGIC or self.u32(4) != 2:
            raise ValueError("not a version 2 SODB file")
        self.schema_offset, self.data_offset, self.strings_offset = (self.u32(12), self.u32(16), self.u32(20))
        _, count, stride = self._block(self.data_offset)
        if stride != 1:
            raise ValueError("unexpected DATA stride")
        self.table_hashes = list(struct.unpack_from("<%dI" % count, self.blob, self.data_offset + 12))
        self.table_offsets = list(struct.unpack_from("<%dI" % count, self.blob, self.data_offset + 12 + 4 * count))
        n = self.u32(self.strings_offset + 4)
        self.strings = [struct.unpack_from("<II", self.blob, self.strings_offset + 8 + 8 * i) for i in range(n)]

    def u32(self, off):
        return struct.unpack_from("<I", self.blob, off)[0]

    def _block(self, off):
        return struct.unpack_from("<4sII", self.blob, off)

    def string(self, index):
        """The text of string ``index``; None for a hash-only string or an index out of range."""
        if not 0 <= index < len(self.strings):
            return None
        _, off = self.strings[index]
        if not off:
            return None
        return self.blob[off:self.blob.index(b"\0", off)].decode("latin-1")

    def table_index(self, name):
        return self.table_hashes.index(name_hash(name))

    def rows(self, table):
        """(row-name hashes, row offsets) of class block 0 of a table, in ID order."""
        off = self.table_offsets[table]
        classes = self.u32(off + 0x10)
        block = self.u32(off + 0x18 + 4 * classes)
        _, count, stride = self._block(block)
        keys = list(struct.unpack_from("<%dI" % count, self.blob, block + 12))
        ptrs = list(struct.unpack_from("<%dI" % (count * stride), self.blob, block + 12 + 4 * count))
        return keys, ptrs[::stride]

    def _list(self, ptr):
        n = self.u32(ptr)
        return [self.u32(ptr + 4 + 4 * i) for i in range(n)]

    def clothing(self):
        table = self.table_index(CLOTHING)
        _, ptrs = self.rows(table)
        out = []
        for index, p in enumerate(ptrs):
            w = struct.unpack_from("<9I", self.blob, p)
            elems = self._list(w[3])
            out.append(ClothingRow(
                index=index, id=(table << 24) | index, name=self.string(w[0]) or "#%d" % w[0], num=w[1],
                group=self.string(w[2]) or "", colours=[(self.u32(e + 4), self.u32(e + 8)) for e in elems],
                cost=w[4], display=self.string(w[5]) or "", order=w[7], type=w[8], offset=p,
                variants=[(self.u32(e + 4), self.u32(e + 8)) for e in self._list(w[6])],
                colour_offsets=[e + 8 for e in elems]))
        return out

    def find_clothing(self, name):
        for row in self.clothing():
            if row.name == name:
                return row
        raise KeyError("no clothing row %r" % name)


def patch_u32(blob, offset, value):
    """Return ``blob`` with one dword replaced: the in-place edits (a model number, a colour Number) need no
    layout change."""
    out = bytearray(blob)
    struct.pack_into("<I", out, offset, value)
    return bytes(out)


# -- venues ---------------------------------------------------------------------------------------------------------

VENUE = 0xD7DCA628          # the Venue table's name hash (the text is not in the file)
VENUE_FE_ID, VENUE_CORE_ID, VENUE_MODES, VENUE_FILENAME = 10, 11, 12, 13   # row words
VENUE_NAME, VENUE_DISPLAY = 0, 9


def venues(blob):
    """[(row index, {name, display, fe_id, core_id, modes, filename, offset})] of the Venue table: the list
    Battle mode offers (``GetNextVenue`` keeps the rows whose ``modes`` bit for the match type is set) and the
    record ``Game.StartGame`` loads for the chosen feID. ``venue.xml`` is not read by either."""
    s = Sod(blob)
    t = s.table_hashes.index(VENUE)
    _, ptrs = s.rows(t)
    out = []
    for i, p in enumerate(ptrs):
        w = struct.unpack_from("<14I", s.blob, p)
        out.append((i, {"name": s.string(w[VENUE_NAME]), "display": s.string(w[VENUE_DISPLAY]),
                        "fe_id": w[VENUE_FE_ID], "core_id": w[VENUE_CORE_ID], "modes": w[VENUE_MODES],
                        "filename": s.string(w[VENUE_FILENAME]), "offset": p}))
    return out


def _row_name(base, above):
    """``base`` or ``base`` + a suffix whose name hash is above ``above`` (rows are found by a sorted hash, and
    a new row must sort last so the existing rows keep their index, which is their saved ID)."""
    if name_hash(base) > above:
        return base
    for n in range(1, 1 << 20):
        name = "%s %d" % (base, n)
        if name_hash(name) > above:
            return name
    raise ValueError("no row name found")


def append_venue(blob, template_fe_id, fe_id, filename, display, name=""):
    """``djv2.sod`` with a new Venue row copied from the row of ``template_fe_id`` (its coreID, match types,
    sounds and lists), with feID ``fe_id``, file name ``filename`` and display name ``display``. Returns
    (bytes, row name).

    Everything new goes at the end of the file (every pointer is an absolute file offset): the row (a copy of
    the template's words), the class block with one more key and row pointer (the new key sorts last), and a
    new string table with three more strings (the old pool stays where it is). The table header's row count
    and class block pointer and the file header's string-table offset are updated in place."""
    s = Sod(blob)
    t = s.table_hashes.index(VENUE)
    off = s.table_offsets[t]
    classes = s.u32(off + 0x10)
    block_ptr_at = off + 0x18 + 4 * classes
    block = s.u32(block_ptr_at)
    tag, count, stride = s._block(block)
    keys = list(struct.unpack_from("<%dI" % count, s.blob, block + 12))
    ptrs = list(struct.unpack_from("<%dI" % (count * stride), s.blob, block + 12 + 4 * count))
    rows = {struct.unpack_from("<I", s.blob, p + 4 * VENUE_FE_ID)[0]: p for p in ptrs[::stride]}
    if fe_id in rows:
        raise ValueError("feID %d is in the Venue table already" % fe_id)
    if template_fe_id not in rows:
        raise KeyError("no Venue row with feID %d" % template_fe_id)
    src = rows[template_fe_id]
    after = sorted(p for p in ptrs[::stride] if p > src)
    length = (after[0] - src) if after else 21 * 4
    name = _row_name(name or "Studio %s" % filename, max(keys))
    out = bytearray(s.blob)

    def pad16():
        while len(out) % 16:
            out.append(0)

    # strings: a new table (old pairs + three) and the new texts
    texts = [name, display, filename]
    pad16()
    pool = []
    for text in texts:
        pool.append(len(out))
        out += text.encode("latin-1") + b"\0"
    pad16()
    first = len(s.strings)
    strt = len(out)
    tag_strt = s.blob[s.strings_offset:s.strings_offset + 4]
    out += tag_strt + struct.pack("<I", first + len(texts))
    out += s.blob[s.strings_offset + 8:s.strings_offset + 8 + 8 * first]
    for text, at in zip(texts, pool):
        out += struct.pack("<II", name_hash(text), at)
    pad16()
    # the row
    row = bytearray(s.blob[src:src + length])
    struct.pack_into("<I", row, 4 * VENUE_NAME, first)
    struct.pack_into("<I", row, 4 * VENUE_DISPLAY, first + 1)
    struct.pack_into("<I", row, 4 * VENUE_FILENAME, first + 2)
    struct.pack_into("<I", row, 4 * VENUE_FE_ID, fe_id)
    row_at = len(out)
    out += row
    pad16()
    # the class block with the new row last
    new_block = len(out)
    out += tag + struct.pack("<II", count + 1, stride)
    out += struct.pack("<%dI" % (count + 1), *(keys + [name_hash(name)]))
    out += struct.pack("<%dI" % ((count + 1) * stride), *(ptrs + [row_at] + [0] * (stride - 1)))
    struct.pack_into("<I", out, block_ptr_at, new_block)
    if s.u32(off + 0xC) == count:
        struct.pack_into("<I", out, off + 0xC, count + 1)
    struct.pack_into("<I", out, 20, strt)
    return bytes(out), name
