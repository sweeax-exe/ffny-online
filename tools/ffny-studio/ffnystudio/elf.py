"""32-bit little-endian ELF relocatable objects, as EAGL writes them, with a byte-exact writer.

EAGL objects (``e_machine`` 8, MIPS; PS2 tool flags) hold one ``.data`` section, ``.shstrtab``, ``.strtab``,
``.symtab`` and ``.rel.data`` (``R_MIPS_32``: the word at ``r_offset`` gets the symbol's address added). There is
no ``.text`` and no program header.

The writer lays the sections and the section header table out in their original file order. Each keeps the
alignment its original offset implies (``sh_addralign`` when that reproduces the gap before it, as it does
for every section in the dump; older tool versions put the header table mid-file, aligned 4, newer ones at
the end, aligned 16) and the padding
bytes it had while the gap length is unchanged. So ``parse -> write`` is the identity for every object in the
dump, and an edited object comes out with consistent offsets.
"""

import struct
from dataclasses import dataclass, field

EHDR = struct.Struct("<16sHHIIIIIHHHHHH")
SHDR = struct.Struct("<10I")
SYM = struct.Struct("<IIIBBH")
REL = struct.Struct("<II")

SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_NOBITS, SHT_REL = 0, 1, 2, 3, 4, 8, 9
R_MIPS_32 = 2
SHDR_KEY = -1

STB = {0: "LOCAL", 1: "GLOBAL", 2: "WEAK"}
STT = {0: "NOTYPE", 1: "OBJECT", 2: "FUNC", 3: "SECTION", 4: "FILE"}


@dataclass
class Section:
    name: str
    type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    addralign: int
    entsize: int
    name_offset: int
    data: bytes = b""
    file_align: int = 0      # 0: use addralign
    pad: bytes = b""         # original padding before the section


@dataclass
class Symbol:
    name: str
    value: int
    size: int
    info: int
    other: int
    shndx: int
    name_offset: int = 0

    @property
    def bind(self):
        return self.info >> 4

    @property
    def type(self):
        return self.info & 0xF


@dataclass
class Relocation:
    offset: int
    sym: int
    type: int


@dataclass
class ElfObject:
    ident: bytes
    e_type: int
    machine: int
    version: int
    entry: int
    phoff: int
    flags: int
    ehsize: int
    phentsize: int
    phnum: int
    shentsize: int
    shstrndx: int
    sections: list = field(default_factory=list)
    shoff: int = 0
    shdr_align: int = 16
    shdr_pad: bytes = b""
    trailer: bytes = b""     # bytes after the last item (none in the dump)

    # -- lookup ---------------------------------------------------------------------------------------------

    def section(self, name):
        for index, section in enumerate(self.sections):
            if section.name == name:
                return index, section
        raise KeyError(name)

    def strtab_name(self, strtab_index, offset):
        data = self.sections[strtab_index].data
        end = data.index(b"\0", offset)
        return data[offset:end].decode("latin-1")

    def symbols(self):
        """The .symtab entries, names resolved."""
        index, symtab = self.section(".symtab")
        out = []
        for pos in range(0, len(symtab.data), SYM.size):
            name_off, value, size, info, other, shndx = SYM.unpack_from(symtab.data, pos)
            out.append(Symbol(self.strtab_name(symtab.link, name_off), value, size, info, other, shndx, name_off))
        return out

    def relocations(self, target=".data"):
        """REL entries that apply to section ``target``."""
        target_index = self.section(target)[0]
        out = []
        for section in self.sections:
            if section.type == SHT_REL and section.info == target_index:
                for pos in range(0, len(section.data), REL.size):
                    offset, info = REL.unpack_from(section.data, pos)
                    out.append(Relocation(offset, info >> 8, info & 0xFF))
        return out

    # -- writing --------------------------------------------------------------------------------------------

    def _items(self):
        """(key, offset) in file order: section indices and SHDR_KEY for the header table."""
        items = [(i, s.offset) for i, s in enumerate(self.sections) if s.type != SHT_NULL]
        items.append((SHDR_KEY, self.shoff))
        return sorted(items, key=lambda item: (item[1], item[0] == SHDR_KEY, item[0]))

    def layout(self):
        """Assign file offsets in the current file order; returns the file length."""
        pos = EHDR.size
        self._pads = {}
        for key, _ in self._items():
            if key == SHDR_KEY:
                align, pad, length = self.shdr_align, self.shdr_pad, len(self.sections) * SHDR.size
            else:
                section = self.sections[key]
                if section.type != SHT_NOBITS:
                    section.size = len(section.data)
                align = section.file_align or max(section.addralign, 1)
                pad = section.pad
                length = 0 if section.type == SHT_NOBITS else section.size
            start = (pos + align - 1) // align * align
            gap = start - pos
            self._pads[key] = pad if len(pad) == gap else bytes(gap)
            if key == SHDR_KEY:
                self.shoff = start
            else:
                self.sections[key].offset = start
            pos = start + length
        return pos + len(self.trailer)

    def to_bytes(self):
        total = self.layout()
        out = bytearray(total)
        for key, _ in self._items():
            start = self.shoff if key == SHDR_KEY else self.sections[key].offset
            pad = self._pads[key]
            out[start - len(pad):start] = pad
        out[total - len(self.trailer):] = self.trailer
        EHDR.pack_into(out, 0, self.ident, self.e_type, self.machine, self.version, self.entry, self.phoff,
                       self.shoff, self.flags, self.ehsize, self.phentsize, self.phnum, self.shentsize,
                       len(self.sections), self.shstrndx)
        for index, section in enumerate(self.sections):
            if section.type not in (SHT_NULL, SHT_NOBITS):
                out[section.offset:section.offset + len(section.data)] = section.data
            SHDR.pack_into(out, self.shoff + index * SHDR.size, section.name_offset, section.type,
                           section.flags, section.addr, section.offset if section.type != SHT_NULL else 0,
                           section.size, section.link, section.info, section.addralign, section.entsize)
        return bytes(out)


def is_elf(blob):
    return blob[:4] == b"\x7fELF"


def parse(blob):
    if not is_elf(blob):
        raise ValueError("not an ELF object")
    if blob[4] != 1 or blob[5] != 1:
        raise ValueError("only 32-bit little-endian ELF is supported")
    (ident, e_type, machine, version, entry, phoff, shoff, flags, ehsize, phentsize, phnum, shentsize, shnum,
     shstrndx) = EHDR.unpack_from(blob, 0)
    if shentsize != SHDR.size or shoff + shnum * SHDR.size > len(blob):
        raise ValueError("bad section header table")
    obj = ElfObject(ident, e_type, machine, version, entry, phoff, flags, ehsize, phentsize, phnum, shentsize,
                    shstrndx, shoff=shoff)
    for index in range(shnum):
        name_off, typ, sflags, addr, offset, size, link, info, align, entsize = \
            SHDR.unpack_from(blob, shoff + index * SHDR.size)
        data = b""
        if typ not in (SHT_NULL, SHT_NOBITS):
            if offset + size > len(blob):
                raise ValueError("section %d runs past the file" % index)
            data = bytes(blob[offset:offset + size])
        obj.sections.append(Section("", typ, sflags, addr, offset, size, link, info, align, entsize, name_off, data))
    _infer_layout(obj, blob)
    names = obj.sections[shstrndx].data
    for section in obj.sections:
        end = names.find(b"\0", section.name_offset)
        section.name = names[section.name_offset:end].decode("latin-1")
    return obj


def _infer_layout(obj, blob):
    """Record each item's alignment and padding so that ``layout`` reproduces the original offsets."""
    pos = EHDR.size
    for key, offset in obj._items():
        if key == SHDR_KEY:
            length = len(obj.sections) * SHDR.size
        else:
            section = obj.sections[key]
            length = 0 if section.type == SHT_NOBITS else section.size
        if offset < pos:
            raise ValueError("overlapping sections are not supported")
        natural = 4 if key == SHDR_KEY else max(obj.sections[key].addralign, 1)
        for align in [natural, 4, 16] + [1 << b for b in range(17)]:
            if (pos + align - 1) // align * align == offset:
                break
        else:
            raise ValueError("unsupported gap before offset 0x%x" % offset)
        pad = bytes(blob[pos:offset])
        if key == SHDR_KEY:
            obj.shdr_align, obj.shdr_pad = align, pad
        else:
            section = obj.sections[key]
            section.file_align = 0 if align == max(section.addralign, 1) else align
            section.pad = pad
        pos = offset + length
    obj.trailer = bytes(blob[pos:])


EAGL_FLAGS = 0x20924000


def build_object(data, symbols, relocations, flags=EAGL_FLAGS, machine=8):
    """A new EAGL-style object: sections NULL, .data, .shstrtab, .strtab, .symtab, .rel.data, header table last.

    ``symbols``: [(name, value, size, info, shndx)] after the null and .data section symbols, locals first
    (``info`` = bind << 4 | type; shndx 1 is .data, 0 undefined). ``relocations``: [(offset, symbol index,
    type)] where symbol index counts the null symbol as 0 and the section symbol as 1."""
    shstr = b"\0.data\0.shstrtab\0.strtab\0.symtab\0.rel.data\0"
    names = {n: shstr.index(n.encode() + b"\0") for n in (".data", ".shstrtab", ".strtab", ".symtab", ".rel.data")}
    strtab = bytearray(b"\0")
    syms = bytearray(SYM.size)                                   # null symbol
    syms += SYM.pack(0, 0, 0, 3, 0, 1)                           # .data section symbol (LOCAL SECTION)
    first_global = 2
    for i, (name, value, size, info, shndx) in enumerate(symbols):
        offset = len(strtab)
        strtab += name.encode("latin-1") + b"\0"
        syms += SYM.pack(offset, value, size, info, 0, shndx)
        if info >> 4 == 0:
            first_global = i + 3
    rel = b"".join(REL.pack(off, (sym << 8) | typ) for off, sym, typ in relocations)
    obj = ElfObject(b"\x7fELF\x01\x01\x01" + bytes(9), 1, machine, 1, 0, 0, flags, EHDR.size, 0, 0, SHDR.size, 2)
    obj.sections = [
        Section("", SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        Section(".data", SHT_PROGBITS, 3, 0, 1, 0, 0, 0, 16, 0, names[".data"], bytes(data)),
        Section(".shstrtab", SHT_STRTAB, 0, 0, 2, 0, 0, 0, 1, 0, names[".shstrtab"], shstr),
        Section(".strtab", SHT_STRTAB, 0, 0, 3, 0, 0, 0, 1, 0, names[".strtab"], bytes(strtab)),
        Section(".symtab", SHT_SYMTAB, 0, 0, 4, 0, 3, first_global, 4, SYM.size, names[".symtab"], bytes(syms)),
        Section(".rel.data", SHT_REL, 0, 0, 5, 0, 4, 1, 4, REL.size, names[".rel.data"], rel),
    ]
    obj.shoff = 6
    return obj


def round_trips(blob):
    """True when ``parse(blob).to_bytes() == blob``."""
    return parse(blob).to_bytes() == bytes(blob)
