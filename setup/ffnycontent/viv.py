# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""BIGF/BIG4 (``.viv``) archives: read entries (RefPack removed) and compose a patched copy
(FFNY Studio ``ffnystudio/viv.py``).

Layout: magic ``BIGF`` or ``BIG4``; total size as little-endian u32 (big-endian accepted too); entry count BE;
header size BE; then per entry {offset BE, size BE, NUL-terminated name}. Entries are often RefPack-compressed
(``10 fb`` + 3-byte BE size). RefPack decoding is the bounded decoder in ``scripts/hd_assets.py``.
"""

import importlib.util
import io
import os
from pathlib import Path
import struct
from dataclasses import dataclass

MAGICS = (b"BIGF", b"BIG4")
_hd = None


def _hd_assets():
    """scripts/hd_assets.py of this source tree (an installed tree has scripts/ too)."""
    global _hd
    if _hd is None:
        path = Path(__file__).resolve().parents[2] / "scripts" / "hd_assets.py"
        spec = importlib.util.spec_from_file_location("ffny_content_hd_assets", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        _hd = module
    return _hd


@dataclass
class Entry:
    name: str
    offset: int
    size: int


def is_refpack(blob):
    return len(blob) >= 5 and int.from_bytes(blob[:2], "big") & 0x3EFF == 0x10FB


def refpack_decompress(blob):
    return _hd_assets().unpack_refpack(blob)


def refpack_store(data):
    """``data`` as RefPack with literal blocks only (valid, uncompressed)."""
    if len(data) >= 1 << 24:
        raise ValueError("stored RefPack needs a 3-byte size")
    out = bytearray(b"\x10\xfb" + len(data).to_bytes(3, "big"))
    pos = 0
    while len(data) - pos >= 4:
        run = min(112, (len(data) - pos) & ~3)
        out.append(0xE0 | ((run - 4) >> 2))
        out += data[pos:pos + run]
        pos += run
    tail = len(data) - pos
    out.append(0xFC | tail)
    out += data[pos:]
    return bytes(out)


def unwrap(blob):
    """Strip RefPack layers."""
    for _ in range(8):
        if not is_refpack(blob):
            return blob
        blob = refpack_decompress(blob)
    raise ValueError("too many RefPack layers")


def parse_directory(head):
    """(magic, total_size, header_size, [Entry]) from the first bytes of an archive."""
    if head[:4] not in MAGICS or len(head) < 16:
        raise ValueError("not a BIG archive: %r" % head[:4])
    total_le = struct.unpack_from("<I", head, 4)[0]
    count, header_size = struct.unpack_from(">II", head, 8)
    if count > 200000:
        raise ValueError("implausible entry count %d" % count)
    pos, entries = 16, []
    for _ in range(count):
        if pos + 8 > len(head):
            raise ValueError("truncated directory")
        offset, size = struct.unpack_from(">II", head, pos)
        end = head.find(b"\0", pos + 8, pos + 8 + 4096)
        if end < 0:
            raise ValueError("unterminated entry name")
        entries.append(Entry(head[pos + 8:end].decode("latin-1"), offset, size))
        pos = end + 1
    return head[:4], total_le, header_size, entries


class Archive:
    """A BIG archive backed by a file (read lazily) or by bytes."""

    def __init__(self, source, label=None):
        if isinstance(source, (bytes, bytearray, memoryview)):
            self._blob, self._path, self.length = bytes(source), None, len(source)
            head = self._blob[:4 << 20]
        else:
            self._blob, self._path = None, os.fspath(source)
            self.length = os.path.getsize(self._path)
            with open(self._path, "rb") as fh:
                head = fh.read(16)
                if head[:4] not in MAGICS:
                    raise ValueError("not a BIG archive: %r" % head[:4])
                header_size = struct.unpack_from(">I", head, 12)[0]
                fh.seek(0)
                head = fh.read(max(header_size, 16) + 16)
        self.label = label or (self._path or "<bytes>")
        self.magic, self.total_size, self.header_size, self.entries = parse_directory(head)
        self.size_le = self.total_size == self.length
        if not self.size_le and struct.unpack_from(">I", head, 4)[0] != self.length:
            self.size_le = None
        self._by_name = {e.name.lower(): e for e in self.entries}

    def names(self):
        return [e.name for e in self.entries]

    def has(self, name):
        return name.replace("\\", "/").lower() in self._by_name

    def find(self, name):
        entry = self._by_name.get(name.replace("\\", "/").lower())
        if entry is None:
            raise KeyError("no entry %r in %s" % (name, self.label))
        return entry

    def raw(self, name):
        """The stored bytes of an entry (possibly RefPack)."""
        entry = name if isinstance(name, Entry) else self.find(name)
        if entry.offset + entry.size > self.length:
            raise ValueError("entry %s runs past the archive" % entry.name)
        if self._blob is not None:
            return self._blob[entry.offset:entry.offset + entry.size]
        with open(self._path, "rb") as fh:
            fh.seek(entry.offset)
            return fh.read(entry.size)

    def read(self, name):
        """An entry's content, RefPack removed."""
        return unwrap(self.raw(name))


def build_archive(files, magic=b"BIGF", align=1, size_le=True, trailer=b""):
    """Pack [(name, bytes)] into a BIG archive (tests)."""
    names = [n.encode("latin-1") + b"\0" for n, _ in files]
    header_size = 16 + sum(8 + len(n) for n in names) + len(trailer)
    offsets, pos = [], header_size
    for _, data in files:
        pos = (pos + align - 1) // align * align
        offsets.append(pos)
        pos += len(data)
    out = bytearray(magic)
    out += struct.pack("<I" if size_le else ">I", pos)
    out += struct.pack(">II", len(files), header_size)
    for (_, data), name, offset in zip(files, names, offsets):
        out += struct.pack(">II", offset, len(data)) + name
    out += trailer
    for (_, data), offset in zip(files, offsets):
        out += b"\0" * (offset - len(out))
        out += data
    return bytes(out)


def entry_alignment(entries, limit=2048):
    """The largest power of two (up to ``limit``) that divides every entry offset: 128 in the game's archives."""
    align = limit
    while align > 1 and any(e.offset % align for e in entries):
        align //= 2
    return align


def _source(archive):
    return open(archive._path, "rb") if archive._path else io.BytesIO(archive._blob)


def compose_archive(src, dst, replace=None, add=(), align=None, chunk=1 << 20):
    """Write a copy of archive ``src`` (a path or bytes) to the path ``dst`` with entries replaced and added.

    ``replace`` maps existing entry names to new stored bytes; ``add`` lists ``(name, stored bytes)``. The original
    entries keep their bytes and relative positions (one block, moved by a multiple of the alignment only when the
    grown directory needs room); new bytes are appended at the entry alignment; added records go at the directory's
    end; the directory trailer and the size field's byte order are kept. Written to ``dst + '.tmp'``, read back and
    checked, then renamed over ``dst``."""
    source = Archive(src)
    if not source.entries:
        raise ValueError("cannot compose an empty archive")
    known = {e.name.lower(): e for e in source.entries}
    replaced = {}
    for name, data in (replace or {}).items():
        entry = known.get(name.replace("\\", "/").lower())
        if entry is None:
            raise KeyError("no entry %r to replace in %s" % (name, source.label))
        replaced[entry.name] = bytes(data)
    added, seen = [], set(known)
    for name, data in add:
        if not name or "\0" in name:
            raise ValueError("invalid entry name %r" % name)
        name.encode("latin-1")
        if name.lower() in seen:
            raise ValueError("entry %r already exists (replace it instead)" % name)
        seen.add(name.lower())
        added.append((name, bytes(data)))
    align = align or entry_alignment(source.entries)
    with _source(source) as fh:
        head = fh.read(source.header_size)
    dir_end = 16 + sum(9 + len(e.name.encode("latin-1")) for e in source.entries)
    trailer = head[dir_end:source.header_size]
    first = min(e.offset for e in source.entries)
    if first < source.header_size:
        raise ValueError("an entry starts inside the header")
    header_size = dir_end + sum(9 + len(n.encode("latin-1")) for n, _ in added) + len(trailer)
    shift = -(-(header_size - first) // align) * align if header_size > first else 0

    pos, placed, appended = source.length + shift, {}, list(replaced.items()) + added
    for name, data in appended:
        pos = -(-pos // align) * align
        placed[name] = (pos, len(data))
        pos += len(data)
    total = pos
    records = [(e.name,) + placed.get(e.name, (e.offset + shift, e.size)) for e in source.entries]
    records += [(name,) + placed[name] for name, _ in added]

    out_head = bytearray(source.magic)
    out_head += struct.pack(">I" if source.size_le is False else "<I", total)
    out_head += struct.pack(">II", len(records), header_size)
    for name, off, size in records:
        out_head += struct.pack(">II", off, size) + name.encode("latin-1") + b"\0"
    out_head += trailer

    tmp = os.fspath(dst) + ".tmp"
    with _source(source) as fh, open(tmp, "wb") as out:
        out.write(out_head)
        if len(out_head) < first:
            fh.seek(len(out_head))
            out.write(fh.read(first - len(out_head)))
        out.write(b"\0" * (first + shift - max(len(out_head), first)))
        fh.seek(first)
        for block in iter(lambda: fh.read(chunk), b""):
            out.write(block)
        length = source.length + shift
        for name, data in appended:
            pad = placed[name][0] - length
            out.write(b"\0" * pad)
            out.write(data)
            length += pad + len(data)
    if length != total:
        raise ValueError("composed length %d != %d" % (length, total))
    _verify_composed(source, tmp, records, dict(appended), shift, chunk)
    os.replace(tmp, dst)
    return {"entries": len(records), "added": len(added), "replaced": len(replaced), "shift": shift,
            "size": total, "align": align}


def _verify_composed(source, path, records, new_data, shift, chunk):
    out = Archive(path)
    if [(e.name, e.offset, e.size) for e in out.entries] != records:
        raise ValueError("composed directory does not read back")
    if out.size_le is None:
        raise ValueError("composed size field does not match the file")
    ordered = sorted(out.entries, key=lambda e: e.offset)
    if ordered[0].offset < out.header_size:
        raise ValueError("a composed entry starts inside the header")
    for a, b in zip(ordered, ordered[1:]):
        if a.offset + a.size > b.offset:
            raise ValueError("composed entries %s and %s overlap" % (a.name, b.name))
    for e in out.entries:
        if e.name in new_data and out.raw(e) != new_data[e.name]:
            raise ValueError("new bytes of %s do not read back" % e.name)
    first = min(e.offset for e in source.entries)
    with open(path, "rb") as fo, _source(source) as fs:
        fo.seek(first + shift)
        fs.seek(first)
        remaining = source.length - first
        while remaining:
            n = min(remaining, chunk)
            if fo.read(n) != fs.read(n):
                raise ValueError("original entries changed while composing")
            remaining -= n
