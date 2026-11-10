# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""FFDL v1 binary deltas against the player's own files (FFNY Studio ``ffnystudio/delta.py``).

Format (little-endian)::

    "FFDL" u32 version (1)
    32 bytes SHA-256 of the original, 32 bytes SHA-256 of the result, u32 result size
    ops until the end: 0x01 u32 offset u32 length  (copy from the original)
                       0x02 u32 length <bytes>     (literal)
"""

import hashlib
import struct

MAGIC = b"FFDL"
VERSION = 1
BLOCK = 32
COPY, LITERAL = 1, 2


class DeltaError(ValueError):
    pass


def encode(original, new, block=BLOCK):
    """The delta that turns ``original`` into ``new`` (tests and tools; Studio writes the packs' deltas)."""
    original, new = bytes(original), bytes(new)
    index = {}
    for off in range(0, max(0, len(original) - block + 1)):
        index.setdefault(original[off:off + block], off)
    ops = bytearray()
    literal = bytearray()

    def flush():
        if literal:
            ops.extend(struct.pack("<BI", LITERAL, len(literal)))
            ops.extend(literal)
            literal.clear()

    i, n = 0, len(new)
    while i < n:
        off = index.get(new[i:i + block]) if i + block <= n else None
        if off is None:
            literal.append(new[i])
            i += 1
            continue
        length = block
        while i + length < n and off + length < len(original) and new[i + length] == original[off + length]:
            length += 1
        flush()
        ops.extend(struct.pack("<BII", COPY, off, length))
        i += length
    flush()
    head = MAGIC + struct.pack("<I", VERSION) + hashlib.sha256(original).digest() + hashlib.sha256(new).digest() \
        + struct.pack("<I", len(new))
    return head + bytes(ops)


def parse(delta):
    """(original sha256 hex, result sha256 hex, result size, [(op, a, b)]) after checking the structure."""
    d = bytes(delta)
    if len(d) < 76 or d[:4] != MAGIC:
        raise DeltaError("not an FFNY delta")
    if struct.unpack_from("<I", d, 4)[0] != VERSION:
        raise DeltaError("unknown delta version")
    src_hash, dst_hash = d[8:40].hex(), d[40:72].hex()
    size = struct.unpack_from("<I", d, 72)[0]
    ops, pos, total = [], 76, 0
    while pos < len(d):
        op = d[pos]
        if op == COPY and pos + 9 <= len(d):
            off, length = struct.unpack_from("<II", d, pos + 1)
            ops.append((COPY, off, length))
            pos += 9
        elif op == LITERAL and pos + 5 <= len(d):
            length = struct.unpack_from("<I", d, pos + 1)[0]
            if pos + 5 + length > len(d):
                raise DeltaError("literal runs past the end")
            ops.append((LITERAL, pos + 5, length))
            pos += 5 + length
        else:
            raise DeltaError("bad op at %d" % pos)
        total += length
        if total > size:
            raise DeltaError("ops make more than the header's %d bytes" % size)
    if total != size:
        raise DeltaError("ops make %d bytes, the header says %d" % (total, size))
    return src_hash, dst_hash, size, ops


def apply(original, delta):
    """The result of ``delta`` on the player's ``original``; refuses another original or a wrong result."""
    original, delta = bytes(original), bytes(delta)
    src_hash, dst_hash, size, ops = parse(delta)
    if hashlib.sha256(original).hexdigest() != src_hash:
        raise DeltaError("this delta is for another version of the file")
    out = bytearray()
    for op, a, b in ops:
        if op == COPY:
            if a + b > len(original):
                raise DeltaError("copy past the end of the original")
            out += original[a:a + b]
        else:
            out += delta[a:a + b]
    if len(out) != size or hashlib.sha256(out).hexdigest() != dst_hash:
        raise DeltaError("the result does not match the delta's hash")
    return bytes(out)
