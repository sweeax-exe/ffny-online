"""FFNY Studio ELF container: parse, byte-exact re-emit and relayout, on synthetic objects (no game bytes)."""
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[2] / "tools" / "ffny-studio"))
from ffnystudio import elf  # noqa: E402

GLOBAL_OBJECT = (1 << 4) | 1
GLOBAL_NOTYPE = 1 << 4


def sample():
    data = struct.pack("<4I", 0x10000, 0, 8, 0) + b"payload!"
    symbols = [("__Thing:::a", 0, 4, GLOBAL_OBJECT, 1), ("ParentRM_Import", 0, 0, GLOBAL_NOTYPE, 0)]
    relocations = [(8, 1, elf.R_MIPS_32), (12, 3, elf.R_MIPS_32)]
    return elf.build_object(data, symbols, relocations)


def test_built_object_parses_back():
    blob = sample().to_bytes()
    obj = elf.parse(blob)
    assert [s.name for s in obj.sections] == ["", ".data", ".shstrtab", ".strtab", ".symtab", ".rel.data"]
    assert obj.machine == 8 and obj.flags == elf.EAGL_FLAGS
    assert obj.section(".data")[1].offset == 64           # header 52, aligned 16
    assert obj.shoff % 16 == 0 and obj.shoff + 6 * 40 == len(blob)
    names = [s.name for s in obj.symbols()]
    assert names == ["", "", "__Thing:::a", "ParentRM_Import"]
    rels = obj.relocations()
    assert [(r.offset, r.sym, r.type) for r in rels] == [(8, 1, 2), (12, 3, 2)]
    assert elf.round_trips(blob)


def test_mid_file_header_table_and_padding_survive():
    """Older EAGL tools put the section header table between sections (aligned 4) and pad oddly."""
    obj = sample()
    blob = bytearray(obj.to_bytes())
    parsed = elf.parse(bytes(blob))
    # Move the header table to just after .shstrtab and the remaining sections after it, by hand.
    data, shstr, strtab, symtab, rel = (parsed.sections[i] for i in range(1, 6))
    out = bytearray(blob[:52]) + b"\xab" * 12                # non-zero padding before .data
    out += data.data
    shstr_off = len(out); out += shstr.data
    while len(out) % 4:
        out.append(0)
    shoff = len(out); out += bytes(6 * 40)
    offsets = {}
    for section in (rel, symtab, strtab):
        while len(out) % section.addralign:
            out.append(0)
        offsets[section.name] = len(out); out += section.data
    out += b"\x01\x02\x03"                                     # trailing bytes after the last section
    struct.pack_into("<I", out, 32, shoff)
    table = {".data": 64, ".shstrtab": shstr_off, **offsets}
    for index, section in enumerate(parsed.sections):
        off = table.get(section.name, 0)
        struct.pack_into("<10I", out, shoff + index * 40, section.name_offset, section.type, section.flags,
                         section.addr, off, len(section.data) if index else 0, section.link, section.info,
                         section.addralign, section.entsize)
    again = elf.parse(bytes(out))
    assert again.shdr_align == 4 and again.trailer == b"\x01\x02\x03"
    assert again.to_bytes() == bytes(out)


def test_growing_data_relays_out_consistently():
    obj = elf.parse(sample().to_bytes())
    index, data = obj.section(".data")
    data.data = data.data + b"more bytes, 17 by"
    blob = obj.to_bytes()
    again = elf.parse(blob)
    assert again.section(".data")[1].data.endswith(b"17 by")
    assert again.section(".symtab")[1].offset % 4 == 0
    assert [s.name for s in again.symbols()][2] == "__Thing:::a"
    assert elf.round_trips(blob)


def test_rejects_non_elf_and_big_endian():
    with pytest.raises(ValueError):
        elf.parse(b"not an elf")
    blob = bytearray(sample().to_bytes())
    blob[5] = 2
    with pytest.raises(ValueError):
        elf.parse(bytes(blob))
