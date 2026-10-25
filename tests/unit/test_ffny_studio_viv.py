"""FFNY Studio archives: BIGF/BIG4 with RefPack and nesting, on synthetic fixtures only (no game bytes)."""
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[2] / "tools" / "ffny-studio"))
from ffnystudio import viv  # noqa: E402


def test_stored_refpack_round_trips_every_length():
    for n in (0, 1, 3, 4, 5, 111, 112, 113, 300):
        data = bytes((i * 7) & 0xFF for i in range(n))
        packed = viv.refpack_store(data)
        assert viv.is_refpack(packed) or n == 0
        assert viv.refpack_size(packed) == n
        assert viv.refpack_decompress(packed) == data


def test_refpack_backreference_is_decoded():
    # literal "abcd" (0xE0 block), then a short copy: 2 literals 'xy' + copy 3 from distance 6 -> "abc", end.
    stream = b"\x10\xfb\x00\x00\x09" + b"\xe0abcd" + bytes([0x02 | (0 << 2) | (0 << 5), 5]) + b"xy" + b"\xfc"
    assert viv.refpack_decompress(stream) == b"abcdxyabc"
    assert viv.unwrap(stream) == b"abcdxyabc"


def test_build_and_parse_little_endian_size():
    files = [("a/one.txt", b"hello"), ("two.bin", bytes(range(20)))]
    blob = viv.build_archive(files, magic=b"BIGF", align=16)
    assert struct.unpack_from("<I", blob, 4)[0] == len(blob)
    archive = viv.Archive(blob)
    assert archive.magic == b"BIGF" and archive.size_le is True
    assert archive.names() == ["a/one.txt", "two.bin"]
    assert all(e.offset % 16 == 0 for e in archive.entries)
    assert archive.read("one.txt") == b"hello"          # basename lookup
    assert archive.read("TWO.BIN") == bytes(range(20))  # case-insensitive
    with pytest.raises(KeyError):
        archive.find("missing")


def test_big_endian_size_and_big4_are_accepted():
    blob = viv.build_archive([("x", b"1234")], magic=b"BIG4", size_le=False)
    archive = viv.Archive(blob)
    assert archive.magic == b"BIG4" and archive.size_le is False
    assert archive.read("x") == b"1234"


def test_nested_refpacked_archive_from_file(tmp_path):
    inner = viv.build_archive([("deep/model.o", b"\x7fELF" + bytes(12)), ("tex.xsh", b"SHPX" + bytes(4))])
    outer = viv.build_archive([("screens/a.big", viv.refpack_store(inner)), ("plain.xml", b"<x/>")])
    path = tmp_path / "test.viv"
    path.write_bytes(outer)
    archive = viv.Archive(path)
    nested = archive.open("a.big")
    assert nested.names() == ["deep/model.o", "tex.xsh"]
    assert viv.read_path(str(path) + "::screens/a.big::model.o")[:4] == b"\x7fELF"
    walked = [p for p, _, _ in archive.walk()]
    assert walked == ["screens/a.big", "screens/a.big::deep/model.o", "screens/a.big::tex.xsh", "plain.xml"]
    assert viv.entry_kind(nested.read("tex.xsh")) == "SHPX texture"
    assert viv.entry_kind(archive.read("plain.xml")) == "XML"


def test_truncated_directory_is_rejected():
    blob = viv.build_archive([("name", b"data")])
    with pytest.raises(ValueError):
        viv.parse_directory(blob[:20])
    with pytest.raises(ValueError):
        viv.Archive(b"NOPE" + bytes(12))
