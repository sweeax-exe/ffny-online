# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""The game's string tables (``.loc``, magic ``LOCH``): look names up by hash and append new ones (FFNY Studio
``ffnystudio/loc.py``, the part install needs).

Layout: ``LOCH`` header with one section offset, a ``LOCT`` chunk of (key hash, string index) pairs sorted by
hash, then ``LOCL``: size, 0, string count, string offsets (from the LOCL start), UTF-16LE strings each ended
by two zero code units. The shops' item and colour names are found by hash; :meth:`Loc.with_texts` appends new
names without touching the old ones.
"""

import bisect
import struct


class LocError(ValueError):
    pass


class Loc:
    def __init__(self, blob):
        b = bytes(blob)
        if b[:4] != b"LOCH" or b[0x14:0x18] != b"LOCT":
            raise LocError("not a LOCH text table")
        self.blob = b
        self.locl = struct.unpack_from("<I", b, 0x10)[0]
        if b[self.locl:self.locl + 4] != b"LOCL":
            raise LocError("LOCL not where the header says")
        self.pairs_at = 0x14 + struct.unpack_from("<I", b, 0x18)[0]
        n = (self.locl - self.pairs_at) // 8
        flat = struct.unpack_from("<%dI" % (2 * n), b, self.pairs_at)
        self.pairs = list(zip(flat[0::2], flat[1::2]))
        self.count = struct.unpack_from("<I", b, self.locl + 12)[0]
        self.offsets = list(struct.unpack_from("<%dI" % self.count, b, self.locl + 16))
        self._by_hash = dict(self.pairs)

    def text_at(self, index):
        o = self.locl + self.offsets[index]
        e = o
        while self.blob[e:e + 2] != b"\0\0":
            e += 2
        return self.blob[o:e].decode("utf-16-le", "replace")

    def get(self, text_hash, default=None):
        index = self._by_hash.get(text_hash)
        return default if index is None else self.text_at(index)

    def __contains__(self, text_hash):
        return text_hash in self._by_hash

    def with_texts(self, new):
        """A new file with ``new`` [(hash, text)] added (hashes must be new). Existing indices stay."""
        pairs = list(self.pairs)
        texts = [self.text_at(i) for i in range(self.count)]
        for h, text in new:
            if h in self._by_hash or any(h == p[0] for p in pairs[len(self.pairs):]):
                raise LocError("text hash %08x is already used" % h)
            index = len(texts)
            texts.append(text)
            keys = [p[0] for p in pairs]
            pairs.insert(bisect.bisect(keys, h), (h, index))
        head = bytearray(self.blob[:self.pairs_at])
        table = b"".join(struct.pack("<II", h, i) for h, i in pairs)
        locl = len(head) + len(table)
        struct.pack_into("<I", head, 0x10, locl)
        # the original strings keep their bytes; the offset table grows, so every offset moves by the growth
        old_pool_start = 16 + 4 * self.count
        pool = bytearray(self.blob[self.locl + old_pool_start:self.locl + struct.unpack_from("<I", self.blob,
                                                                                             self.locl + 4)[0]])
        grow = 4 * (len(texts) - self.count)
        offsets = [o + grow for o in self.offsets]
        for text in texts[self.count:]:
            offsets.append(16 + 4 * len(texts) + len(pool))
            pool += text.encode("utf-16-le") + b"\0\0"
        while len(pool) % 4:
            pool += b"\0"
        body = struct.pack("<4sIII", b"LOCL", 16 + 4 * len(texts) + len(pool), 0, len(texts))
        body += struct.pack("<%dI" % len(offsets), *offsets) + pool
        out = bytes(head) + table + body
        tail = self.blob[self.locl + struct.unpack_from("<I", self.blob, self.locl + 4)[0]:]
        return out + tail


def build(texts, tag_hash=0x411F02F3):
    """A small LOCH file of [(hash, text)] for tests (no game bytes)."""
    pairs = sorted((h, i) for i, (h, _) in enumerate(texts))
    head = struct.pack("<4sIIII", b"LOCH", 0x14, 0, 1, 0) + struct.pack("<4sIII", b"LOCT", 0x10, tag_hash, 0)
    table = b"".join(struct.pack("<II", h, i) for h, i in pairs)
    pool, offsets = bytearray(), []
    for _, text in texts:
        offsets.append(16 + 4 * len(texts) + len(pool))
        pool += text.encode("utf-16-le") + b"\0\0"
    while len(pool) % 4:
        pool += b"\0"
    body = struct.pack("<4sIII", b"LOCL", 16 + 4 * len(texts) + len(pool), 0, len(texts))
    body += struct.pack("<%dI" % len(offsets), *offsets) + pool
    out = bytearray(head + table + body)
    struct.pack_into("<I", out, 0x10, len(head) + len(table))
    return bytes(out)
