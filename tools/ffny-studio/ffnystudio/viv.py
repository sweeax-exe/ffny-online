"""BIGF/BIG4 (`.viv`) archives, the Xbox variant, with RefPack and nested archives.

Layout: magic ``BIGF`` or ``BIG4``; total size as **little-endian** u32 (big-endian on other platforms, accepted
too); entry count BE; header size BE; then per entry {offset BE, size BE, NUL-terminated name}. Entries are often
RefPack-compressed (``10 fb`` + 3-byte BE size) and may themselves be archives.

RefPack decoding is the bounded decoder in ``scripts/hd_assets.py``; this module adds lazy file access, nested
paths (``outer.viv::inner.big::file``) and a writer (stored RefPack, archive packing) for fixtures and, later, mods.
"""

import os
import struct
from dataclasses import dataclass

from . import _repo

MAGICS = (b"BIGF", b"BIG4")
SEP = "::"


@dataclass
class Entry:
    name: str
    offset: int
    size: int


def is_refpack(blob):
    return len(blob) >= 5 and int.from_bytes(blob[:2], "big") & 0x3EFF == 0x10FB


def refpack_size(blob):
    """The decompressed size a RefPack header advertises."""
    sig = int.from_bytes(blob[:2], "big")
    step = 4 if sig & 0x8000 else 3
    pos = 2 + (step if sig & 0x0100 else 0)
    return int.from_bytes(blob[pos:pos + step], "big")


def refpack_decompress(blob):
    return _repo.hd_assets().unpack_refpack(blob)


def refpack_store(data):
    """Encode ``data`` as RefPack using literal blocks only (valid, uncompressed). For fixtures and writers."""
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
            # Some archives store a size that does not match (nested copies); keep the value, flag it.
            self.size_le = None
        self._by_name = {e.name.lower(): e for e in self.entries}

    def names(self):
        return [e.name for e in self.entries]

    def find(self, name):
        key = name.replace("\\", "/").lower()
        entry = self._by_name.get(key)
        if entry is None:
            matches = [e for e in self.entries if e.name.lower().replace("\\", "/").endswith("/" + key)
                       or os.path.basename(e.name.lower()) == key]
            if len(matches) == 1:
                entry = matches[0]
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

    def open(self, name):
        """A nested archive."""
        entry = name if isinstance(name, Entry) else self.find(name)
        return Archive(self.read(entry), label=self.label + SEP + entry.name)

    def walk(self, depth=0):
        """(path, Entry, archive) for every entry, descending into nested archives."""
        for entry in self.entries:
            yield entry.name, entry, self
            if depth >= 6:
                continue
            try:
                blob = self.raw(entry)
                if is_refpack(blob):
                    blob = refpack_decompress(blob)
            except ValueError:
                continue
            if blob[:4] in MAGICS:
                inner = Archive(blob, label=self.label + SEP + entry.name)
                for path, sub, arch in inner.walk(depth + 1):
                    yield entry.name + SEP + path, sub, arch


def open_path(path):
    """``file.viv``, ``file.viv::entry``, ``file.viv::inner.big::entry`` -> (archive, entry name or None)."""
    parts = path.split(SEP)
    archive = Archive(parts[0])
    for part in parts[1:-1]:
        archive = archive.open(part)
    return archive, (parts[-1] if len(parts) > 1 else None)


def read_path(path):
    archive, name = open_path(path)
    if name is None:
        raise ValueError("path names an archive, not an entry: " + path)
    return archive.read(name)


def entry_kind(blob):
    """A short label for what an entry holds."""
    if blob[:4] in MAGICS:
        return "archive " + blob[:4].decode()
    if blob[:4] == b"\x7fELF":
        return "ELF object"
    if blob[:4] == b"SHPX":
        return "SHPX texture"
    if blob[:5] == b"<?xml" or blob[:1] == b"<":
        return "XML"
    return "data"


def build_archive(files, magic=b"BIGF", align=1, size_le=True):
    """Pack [(name, bytes)] into a BIG archive. Offsets follow the directory, aligned to ``align``."""
    names = [n.encode("latin-1") + b"\0" for n, _ in files]
    header_size = 16 + sum(8 + len(n) for n in names)
    offsets, pos = [], header_size
    for _, data in files:
        pos = (pos + align - 1) // align * align
        offsets.append(pos)
        pos += len(data)
    total = pos
    out = bytearray(magic)
    out += struct.pack("<I" if size_le else ">I", total)
    out += struct.pack(">II", len(files), header_size)
    for (_, data), name, offset in zip(files, names, offsets):
        out += struct.pack(">II", offset, len(data)) + name
    for (_, data), offset in zip(files, offsets):
        out += b"\0" * (offset - len(out))
        out += data
    return bytes(out)
