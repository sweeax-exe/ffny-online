# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""A piece of a roster fighter's model as a created-fighter item, made on the player's PC from the player's own
files (build 108: Slick Rick's diamond eyepatch).

A clothing pack's item op may carry, beside its ``"model": {"num", "name"}`` (a part of its own) and its one colour::

    "roster_part": {"model": "V2IP_130A", "material": "emc3_jewelry-eye_w", "bone": 58}

and nothing else of the piece: the pack has no delta for the part, its texture or its shop thumbnails. The
composition makes them here, from the roster model ``assets/v2ip.viv`` ``<model>.o`` (its first draw of the material
``material``, the name before ``~``) and its texture atlas ``<model>.xsh``, and from the item op's template row
(its part ``V2CP_<cat>_<num+d>_C.o`` in the category's part archive, its texture ``V2CP_<cat>_<num>_<group>_01.xsh``
and that texture's two thumbnails in ``screens/screens.viv``):

* ``<model name>.o``: a clone of the template part whose only draw is the roster draw: its positions, normals and
  triangle strip as the roster model has them (roster and created fighters share the skeleton and bind pose),
  every vertex on ``bone`` with full weight (58 is the head, as the game's eyewear parts), the UVs moved onto the
  piece's own image and from there into that image's place in the created fighter's composite atlas (the
  template part's UV box, aligned down to the template image's size: the eyewear's 144x32 at (0, 32) of 256x256).
  Render model, shader, matrix palette and the texture reference (the ``cp_c`` TAR import the game binds the
  composite to) are the template's, byte for byte; the template's part name becomes the new one in every symbol
  (the same length, so nothing moves);
* ``<colour texture>.xsh``: the template texture's layout (every record, format, size, flags and attachment)
  with the piece's image (the draw's UV rectangle of the atlas plus 2 pixels) scaled to the template image's size
  as its main image, 8-bit paletted (median cut to 256 colours, nearest colour);
* the two thumbnails ``screens/shopThumbs/<n>00/V2TN_<texture>[_t].big``: the template's small Apt archives with
  their entries renamed and the DXT5 picture painted with the piece's image scaled to its size.

Scaling averages the colour multiplied by the alpha over each target pixel (``fighterart``'s integer resampler);
DXT5 is ``fighterart.encode_texture``; the floats of the geometry are IEEE operations without libm. So two PCs
with the same files make the same bytes (the composed files are part of the online fight-file print).

One implementation for both sides: FFNY Studio (``ffnystudio/rosteritem.py``) and the player's install
(``setup/ffnycontent/rosteritem.py`` of the game repository) hold the same text below their first comment lines.
Standard library and ``fighterart`` only.
"""

import math
import re
import struct

from . import fighterart

ROSTER_ARCHIVE = "assets/v2ip.viv"
SCREENS = "screens/screens.viv"
THUMBS = "screens/shopThumbs/%d00/"
KEY = "roster_part"
SPEC_KEYS = ("model", "material", "bone")
ROSTER_MODEL = re.compile(r"V2IP_[0-9]{3}[A-Z]")
MATERIAL = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}")
PART_MODEL = re.compile(r"V2CP_([0-9]{3})_([0-9]{4})_C")
PART_TEXTURE = re.compile(r"V2CP_[0-9]{3}_[0-9]{4}_\w{1,8}_[0-9]{2}")
MAX_BONE = 255
MARGIN = 2                      # pixels of the atlas around the draw's UV rectangle
COMPOSITE = 256                 # a created fighter's composite atlas (cp_c): 256x256, the part images placed in it


class RosterItemError(ValueError):
    pass


# -- the op ---------------------------------------------------------------------------------------------------------

def _int(v, lo, hi):
    return isinstance(v, int) and not isinstance(v, bool) and lo <= v <= hi


def op_problem(op):
    """Why the item op ``op`` with a ``roster_part`` is not one this module makes ("" when it is)."""
    spec = op.get(KEY)
    if not isinstance(spec, dict) or set(spec) != set(SPEC_KEYS):
        return "roster_part needs exactly %s" % ", ".join(SPEC_KEYS)
    if not (isinstance(spec["model"], str) and ROSTER_MODEL.fullmatch(spec["model"])):
        return "roster_part model %r is not a roster model V2IP_<nnn><letter>" % (spec["model"],)
    if not (isinstance(spec["material"], str) and MATERIAL.fullmatch(spec["material"])):
        return "roster_part material %r is not a material name" % (spec["material"],)
    if not _int(spec["bone"], 0, MAX_BONE):
        return "roster_part bone must be 0..%d" % MAX_BONE
    model = op.get("model")
    if not (isinstance(model, dict) and isinstance(model.get("name"), str) and PART_MODEL.fullmatch(model["name"])):
        return "a roster part needs the item's model {num, name}"
    colours = op.get("colours")
    if not (isinstance(colours, list) and len(colours) == 1 and isinstance(colours[0], dict)
            and isinstance(colours[0].get("texture"), str) and PART_TEXTURE.fullmatch(colours[0]["texture"])):
        return "a roster part's item has exactly one colour"
    if colours[0]["texture"][:14] != model["name"][:14]:
        return "the colour's texture %s is not the part %s's" % (colours[0]["texture"], model["name"])
    if op.get("variants") != []:
        return "a roster part's item has no variants"
    # cosmetic only (owner, build 108): the row's Cost (word 4, field 0x0CFA401E) is both the shop price and what
    # the profile's sixth base stat (Charisma, sub_00014C20: the worn parts' Cost / 5) counts, so it is 0
    if op.get("price") != 0 or isinstance(op.get("price"), bool):
        return "a roster part's item is cosmetic: its price (and so its Charisma) is 0"
    return ""


def part_archive(category):
    """The part archive of a category: 11x ``v2cp_1``, 12x ``v2cp_2``, hair and facial hair (2xx) ``v2cp_0``, the
    rest (13x, 14x, jewellery, tattoos) ``v2cp_3`` (FFNY Studio ``clothing.part_archive``)."""
    if category < 120:
        return "assets/v2cp_1.viv"
    if category < 130:
        return "assets/v2cp_2.viv"
    return "assets/v2cp_%d.viv" % (0 if 200 <= category < 300 else 3)


def thumb_entry(texture_name, small=False):
    """``V2CP_144_0010_Y01_01`` -> its shop thumbnail's screens.viv entry."""
    rest = texture_name[len("V2CP_"):]
    return THUMBS % int(rest[0]) + "V2TN_%s%s.big" % (rest, "_t" if small else "")


def built_entries(op):
    """[(archive, entry)] the composition makes for the item op ``op`` (a pack must have no entry for them)."""
    name, texture = op["model"]["name"], op["colours"][0]["texture"]
    archive = part_archive(int(name[5:8]))
    return [(archive, name + ".o"), (archive, texture + ".xsh"), (SCREENS, thumb_entry(texture)),
            (SCREENS, thumb_entry(texture, True))]


# -- ELF (32-bit little-endian relocatable, as EAGL writes it; written back byte for byte) ---------------------------

_EHDR = struct.Struct("<16sHHIIIIIHHHHHH")
_SHDR = struct.Struct("<10I")
_SYM = struct.Struct("<IIIBBH")
_REL = struct.Struct("<II")
SHT_NULL, SHT_SYMTAB, SHT_NOBITS, SHT_REL = 0, 2, 8, 9
R_MIPS_32 = 2
_SHDR_KEY = -1


class _Section:
    def __init__(self, fields, data):
        (self.name_offset, self.type, self.flags, self.addr, self.offset, self.size, self.link, self.info,
         self.addralign, self.entsize) = fields
        self.data, self.name, self.file_align, self.pad = data, "", 0, b""


class _Symbol:
    def __init__(self, name, value, size, info, other, shndx, name_offset=0):
        self.name, self.value, self.size, self.info, self.other, self.shndx = name, value, size, info, other, shndx
        self.name_offset = name_offset

    @property
    def type(self):
        return self.info & 0xF


class _Elf:
    """Sections laid out in their file order with the alignment and padding each had (``parse -> to_bytes`` is the
    identity for the game's objects; FFNY Studio ``elf.py``)."""

    def __init__(self, blob):
        if blob[:4] != b"\x7fELF" or len(blob) < _EHDR.size or blob[4] != 1 or blob[5] != 1:
            raise RosterItemError("not a 32-bit little-endian ELF object")
        (self.ident, self.e_type, self.machine, self.version, self.entry, self.phoff, self.shoff, self.flags,
         self.ehsize, self.phentsize, self.phnum, self.shentsize, shnum, self.shstrndx) = _EHDR.unpack_from(blob, 0)
        if self.shentsize != _SHDR.size or self.shoff + shnum * _SHDR.size > len(blob) or self.shstrndx >= shnum:
            raise RosterItemError("bad ELF section table")
        self.sections = []
        for i in range(shnum):
            fields = _SHDR.unpack_from(blob, self.shoff + i * _SHDR.size)
            typ, offset, size = fields[1], fields[4], fields[5]
            data = b""
            if typ not in (SHT_NULL, SHT_NOBITS):
                if offset + size > len(blob):
                    raise RosterItemError("an ELF section runs past the file")
                data = bytes(blob[offset:offset + size])
            self.sections.append(_Section(fields, data))
        pos = _EHDR.size
        for key, offset in self._items():
            if key == _SHDR_KEY:
                length, natural = len(self.sections) * _SHDR.size, 4
            else:
                s = self.sections[key]
                length, natural = (0 if s.type == SHT_NOBITS else s.size), max(s.addralign, 1)
            if offset < pos:
                raise RosterItemError("overlapping ELF sections")
            for align in [natural, 4, 16] + [1 << b for b in range(17)]:
                if (pos + align - 1) // align * align == offset:
                    break
            else:
                raise RosterItemError("unsupported ELF gap before 0x%x" % offset)
            pad = bytes(blob[pos:offset])
            if key == _SHDR_KEY:
                self.shdr_align, self.shdr_pad = align, pad
            else:
                s = self.sections[key]
                s.file_align, s.pad = (0 if align == max(s.addralign, 1) else align), pad
            pos = offset + length
        self.trailer = bytes(blob[pos:])
        names = self.sections[self.shstrndx].data
        for s in self.sections:
            s.name = names[s.name_offset:names.find(b"\0", s.name_offset)].decode("latin-1")

    def section(self, name):
        for i, s in enumerate(self.sections):
            if s.name == name:
                return i, s
        raise RosterItemError("the object has no %s" % name)

    def symbols(self):
        _, symtab = self.section(".symtab")
        strtab = self.sections[symtab.link].data
        out = []
        for pos in range(0, len(symtab.data), _SYM.size):
            name_off, value, size, info, other, shndx = _SYM.unpack_from(symtab.data, pos)
            name = strtab[name_off:strtab.index(b"\0", name_off)].decode("latin-1")
            out.append(_Symbol(name, value, size, info, other, shndx, name_off))
        return out

    def relocations(self, target):
        """{offset: (symbol index, type)} of the REL entries applying to section index ``target``."""
        out = {}
        for s in self.sections:
            if s.type == SHT_REL and s.info == target:
                for pos in range(0, len(s.data), _REL.size):
                    offset, info = _REL.unpack_from(s.data, pos)
                    out[offset] = (info >> 8, info & 0xFF)
        return out

    def _items(self):
        items = [(i, s.offset) for i, s in enumerate(self.sections) if s.type != SHT_NULL]
        items.append((_SHDR_KEY, self.shoff))
        return sorted(items, key=lambda item: (item[1], item[0] == _SHDR_KEY, item[0]))

    def to_bytes(self):
        pos, pads = _EHDR.size, {}
        for key, _ in self._items():
            if key == _SHDR_KEY:
                align, pad, length = self.shdr_align, self.shdr_pad, len(self.sections) * _SHDR.size
            else:
                s = self.sections[key]
                if s.type != SHT_NOBITS:
                    s.size = len(s.data)
                align, pad = s.file_align or max(s.addralign, 1), s.pad
                length = 0 if s.type == SHT_NOBITS else s.size
            start = (pos + align - 1) // align * align
            pads[key] = pad if len(pad) == start - pos else bytes(start - pos)
            if key == _SHDR_KEY:
                self.shoff = start
            else:
                self.sections[key].offset = start
            pos = start + length
        total = pos + len(self.trailer)
        out = bytearray(total)
        for key, _ in self._items():
            start = self.shoff if key == _SHDR_KEY else self.sections[key].offset
            out[start - len(pads[key]):start] = pads[key]
        out[total - len(self.trailer):] = self.trailer
        _EHDR.pack_into(out, 0, self.ident, self.e_type, self.machine, self.version, self.entry, self.phoff,
                        self.shoff, self.flags, self.ehsize, self.phentsize, self.phnum, self.shentsize,
                        len(self.sections), self.shstrndx)
        for i, s in enumerate(self.sections):
            if s.type not in (SHT_NULL, SHT_NOBITS):
                out[s.offset:s.offset + len(s.data)] = s.data
            _SHDR.pack_into(out, self.shoff + i * _SHDR.size, s.name_offset, s.type, s.flags, s.addr,
                            s.offset if s.type != SHT_NULL else 0, s.size, s.link, s.info, s.addralign, s.entsize)
        return bytes(out)


# -- EAGL objects (FFNY Studio ``eagl.py``: the parts a draw needs) ---------------------------------------------------

P_GEO_PRIM_STATE, P_TEXTURE, P_WEIGHTS, P_MATRIX_PALETTE = 0x10, 0x0F, 0x1F, 0x1D
P_PRIM_LENGTHS, P_INDICES, P_VERTEX_STREAM = 0x02, 0x0E, 0x09
NO_PARAMETER = {0x04, 0x1A, 0x01, 0x19}
NV_UB_D3D, NV_S1, NV_F, NV_UB_OGL, NV_S32K, NV_CMP = 0, 1, 2, 4, 5, 6
VB_SENTINEL = b"\xbb\xbb\xbb\xbb"
SLOT_REGISTERS = 4
STREAM_STRIDES = {0: 12, 1: 8, 2: 4, 3: 4, 4: 2}


def texture_shape(symbol):
    """``__EAGL::TAR:::...SHAPENAME=A130,1;...`` -> ``A130`` (empty for another symbol)."""
    if "SHAPENAME=" not in symbol:
        return ""
    return symbol.split("SHAPENAME=")[1].split(",")[0].split(";")[0]


def decode_normal(word):
    """NV2A NORMPACKED3: x 11 bits, y 11 bits, z 10 bits, two's complement."""
    x, y, z = word & 0x7FF, (word >> 11) & 0x7FF, (word >> 22) & 0x3FF
    x = x - 0x800 if x & 0x400 else x
    y = y - 0x800 if y & 0x400 else y
    z = z - 0x400 if z & 0x200 else z
    return (x / 1023.0, y / 1023.0, z / 511.0)


def encode_normal(n):
    x = max(-1023, min(1023, int(round(n[0] * 1023)))) & 0x7FF
    y = max(-1023, min(1023, int(round(n[1] * 1023)))) & 0x7FF
    z = max(-511, min(511, int(round(n[2] * 511)))) & 0x3FF
    return x | (y << 11) | (z << 22)


def palette_slots(words):
    """The matrix palette entry's register ranges {start, length} (after one word): one slot per 4 registers."""
    signed = [w - (1 << 32) if w & 0x80000000 else w for w in words[1:]]
    slots = []
    for start, count in zip(signed[0::2], signed[1::2]):
        slots.extend(range(start, start + count, SLOT_REGISTERS))
    return slots


class _RenderModel:
    def __init__(self, offset, descriptor, decl, passes, parent):
        self.offset, self.descriptor, self.decl, self.passes, self.parent = offset, descriptor, decl, passes, parent


class _Param:
    def __init__(self, type, words, count, target):
        self.type, self.words, self.count, self.target = type, words, count, target


class _Instance:
    def __init__(self, offset, rm, params):
        self.offset, self.rm, self.params = offset, rm, params

    def first(self, ptype):
        return next((p for p in self.params if p.type == ptype), None)


class _Model:
    def __init__(self, name, offset, materials, textures, groups):
        self.name, self.offset, self.materials, self.textures, self.groups = name, offset, materials, textures, groups


class _Mesh:
    def __init__(self):
        self.positions, self.normals, self.uvs, self.indices = [], None, None, None
        self.prim_lengths, self.skin_slots, self.prim_type = None, None, "TRIANGLESTRIP"


class _Eagl:
    def __init__(self, blob):
        self.elf = _Elf(blob)
        self.data_index, section = self.elf.section(".data")
        self.data = section.data
        self.symbols = self.elf.symbols()
        self.relocs = self.elf.relocations(self.data_index)

    def u32(self, off):
        if off < 0 or off + 4 > len(self.data):
            raise RosterItemError("the object's data is cut short")
        return struct.unpack_from("<I", self.data, off)[0]

    def cstr(self, off):
        return self.data[off:self.data.index(b"\0", off)].decode("latin-1")

    def pointer(self, off):
        """(.data offset, "") of a local pointer, (word, symbol) of an import, None for a plain word."""
        rel = self.relocs.get(off)
        if rel is None:
            return None
        sym = self.symbols[rel[0]]
        word = self.u32(off)
        if sym.name and sym.shndx == 0:
            return word, sym.name
        return word + (sym.value if sym.name else 0), ""

    def local(self, off):
        p = self.pointer(off)
        if p is None or p[1]:
            raise RosterItemError("no local pointer at 0x%x" % off)
        return p[0]

    def typed(self, kind):
        prefix = "__%s:::" % kind
        return [(s.name[len(prefix):], s.value) for s in self.symbols
                if s.name.startswith(prefix) and s.shndx == self.data_index]

    def imports(self):
        return sorted(s.name for s in self.symbols if s.name and s.shndx == 0)

    def render_model(self, off):
        entries, p = [], self.u32(off)
        while self.u32(p) != 0:
            code = self.u32(p)
            length = code & 0xFFFF
            if not length or len(entries) > 256:
                raise RosterItemError("bad render model descriptor at 0x%x" % p)
            entries.append((code >> 16, tuple(self.u32(p + 4 * i) for i in range(1, length))))
            p += 4 * length
        decl, q = [], self.u32(off + 8)
        while self.u32(q) != 0xFFFFFFFF and len(decl) < 16:
            decl.append((self.u32(q) & 0xFFFF, (self.u32(q + 4) >> 16) & 0xFF))
            q += 8
        parent = self.pointer(off + 28)
        return _RenderModel(off, entries, decl, self.u32(off + 4), parent[1] if parent else "")

    def instance(self, off):
        rm = self.render_model(self.local(off))
        params, p = [], off + 4
        for ptype, words in rm.descriptor:
            if ptype in NO_PARAMETER:
                continue
            params.append(_Param(ptype, words, self.u32(p), self.pointer(p + 4)))
            p += 8
        return _Instance(off, rm, params)

    def models(self):
        textures = sorted({texture_shape(s) for s in self.imports() if texture_shape(s)})
        out = []
        for name, off in self.typed("Model"):
            nmat = self.u32(off + 0x9C)
            if nmat > 256:
                raise RosterItemError("model %s has %d materials" % (name, nmat))
            materials, groups = [], []
            names = self.pointer(off + 0xA0)
            if names:
                for i in range(nmat):
                    sp = self.pointer(names[0] + 4 * i)
                    materials.append(self.cstr(sp[0]) if sp else "")
            lod = self.pointer(off + 0xCC)
            if lod:
                p = lod[0] + 4
                for _ in range(nmat):
                    count = self.u32(p)
                    if count > 4096:
                        raise RosterItemError("model %s has an unreadable draw list" % name)
                    groups.append([self.instance(self.local(p + 4 + 4 * i)) for i in range(count)])
                    p += 4 + 4 * count
            out.append(_Model(name, off, materials, textures, groups))
        if not out:
            raise RosterItemError("the object has no model")
        return out

    def _stream(self, at, count, fmt, stride):
        kind, size = fmt & 0xF, fmt >> 4
        if at < 0 or at + count * stride > len(self.data):
            raise RosterItemError("a vertex stream runs past the object")
        out = []
        for i in range(count):
            o = at + i * stride
            if kind == NV_F:
                out.append(struct.unpack_from("<%df" % size, self.data, o))
            elif kind == NV_CMP:
                out.append(decode_normal(self.u32(o)))
            elif kind == NV_S32K:
                out.append(struct.unpack_from("<%dh" % size, self.data, o))
            elif kind == NV_S1:
                out.append(tuple(v / 32767.0 for v in struct.unpack_from("<%dh" % size, self.data, o)))
            elif kind == NV_UB_D3D:
                b, g, r, a = self.data[o:o + 4]
                out.append((r / 255.0, g / 255.0, b / 255.0, a / 255.0))
            elif kind == NV_UB_OGL:
                out.append(tuple(v / 255.0 for v in self.data[o:o + size]))
            else:
                raise RosterItemError("unsupported vertex format 0x%02x" % fmt)
        return out

    def mesh(self, inst):
        """The geometry of one draw (the first draw of its material group: it carries the full state)."""
        mesh = _Mesh()
        decl = dict(inst.rm.decl)
        for param in inst.params:
            if param.type != P_VERTEX_STREAM:
                continue
            _, stream, stride = param.words[:3]
            fmt = decl.get(stream)
            if fmt is None or param.target is None or param.target[1]:
                continue
            values = self._stream(param.target[0], param.count, fmt, stride)
            kind = fmt & 0xF
            if kind == NV_F and fmt >> 4 == 3 and not mesh.positions:
                mesh.positions = values
            elif kind == NV_F and fmt >> 4 == 2 and mesh.uvs is None:
                mesh.uvs = values
            elif kind == NV_CMP:
                mesh.normals = values
            elif kind == NV_F and fmt >> 4 == 4 and mesh.normals is None:
                mesh.normals = [v[:3] for v in values]
        idx = inst.first(P_INDICES)
        if idx and idx.target and not idx.target[1]:
            if idx.target[0] + 2 * idx.count > len(self.data):
                raise RosterItemError("the draw's indices run past the object")
            mesh.indices = list(struct.unpack_from("<%dH" % idx.count, self.data, idx.target[0]))
        lengths = inst.first(P_PRIM_LENGTHS)
        if lengths and lengths.target and not lengths.target[1]:
            if lengths.target[0] + 4 * lengths.count > len(self.data):
                raise RosterItemError("the draw's strip lengths run past the object")
            mesh.prim_lengths = list(struct.unpack_from("<%dI" % lengths.count, self.data, lengths.target[0]))
        for ptype, words in inst.rm.descriptor:
            if ptype == P_MATRIX_PALETTE:
                mesh.skin_slots = palette_slots(words)
        geo = inst.first(P_GEO_PRIM_STATE)
        if geo and geo.target and "SetPrimitiveType=EAGL::PT_" in geo.target[1]:
            mesh.prim_type = geo.target[1].split("SetPrimitiveType=EAGL::PT_")[1].split(";")[0]
        return mesh


# -- the part writer (FFNY Studio ``eaglwrite.py``: one new draw cloned from the template's, appended to .data) --------

class _Appender:
    def __init__(self, obj):
        self.obj = obj
        self.data = bytearray(obj.data)
        self.relocs = dict(obj.relocs)
        self.symbols = list(obj.symbols)
        self.section_sym = next(i for i, s in enumerate(self.symbols) if s.type == 3 and s.shndx == obj.data_index)
        self.by_name = {s.name: i for i, s in enumerate(self.symbols) if s.name}

    def put(self, blob, align=4):
        while len(self.data) % align:
            self.data.append(0)
        off = len(self.data)
        self.data += blob
        return off

    def u32(self, off, value):
        struct.pack_into("<I", self.data, off, value & 0xFFFFFFFF)

    def pointer(self, off, target):
        self.u32(off, target)
        self.relocs[off] = (self.section_sym, R_MIPS_32)

    def import_pointer(self, off, name):
        self.u32(off, 0)
        if name not in self.by_name:
            self.symbols.append(_Symbol(name, 0, 0, 0x10, 0, 0))
            self.by_name[name] = len(self.symbols) - 1
        self.relocs[off] = (self.by_name[name], R_MIPS_32)

    def define(self, name, offset, size=4):
        if name in self.by_name:
            raise RosterItemError("symbol %s exists" % name)
        self.symbols.append(_Symbol(name, offset, size, 0x11, 0, self.obj.data_index))
        self.by_name[name] = len(self.symbols) - 1

    def copy(self, start, length, remap=None, align=4):
        """The object's bytes start..start+length again, with their relocations; local pointers into ``remap``
        (old offset -> new) redirected."""
        src = self.obj
        new = self.put(bytes(src.data[start:start + length]), align)
        for off in range(start, start + length, 4):
            rel = src.relocs.get(off)
            if rel is None:
                continue
            at = new + (off - start)
            sym = src.symbols[rel[0]]
            if sym.name and sym.shndx == 0:
                self.import_pointer(at, sym.name)
            else:
                target = src.u32(off) + (sym.value if sym.name else 0)
                self.pointer(at, (remap or {}).get(target, target))
        return new

    def to_bytes(self):
        e = self.obj.elf
        e.section(".data")[1].data = bytes(self.data)
        _, symtab = e.section(".symtab")
        strtab = e.sections[symtab.link]
        names = bytearray(strtab.data)
        packed = bytearray()
        for s in self.symbols:
            if s.name and s.name_offset == 0:
                s.name_offset = len(names)
                names += s.name.encode("latin-1") + b"\0"
            packed += _SYM.pack(s.name_offset if s.name else 0, s.value, s.size, s.info, s.other, s.shndx)
        strtab.data, symtab.data = bytes(names), bytes(packed)
        for s in e.sections:
            if s.type == SHT_REL and s.info == self.obj.data_index:
                s.data = b"".join(_REL.pack(off, (sym << 8) | typ) for off, (sym, typ) in sorted(self.relocs.items()))
        return e.to_bytes()


def _entry_words(bone):
    """The weight entry of one bone with full weight: {1, 0, 0, float(bone << 24)}."""
    return struct.pack("<4f", 1.0, 0.0, 0.0, float(bone << 24))


def _template_parts(obj, inst):
    rm = inst.rm.offset
    desc = obj.u32(rm)
    p = desc
    while obj.u32(p) != 0:
        p += 4 * (obj.u32(p) & 0xFFFF)
    decl = obj.u32(rm + 8)
    q = decl
    while obj.u32(q) != 0xFFFFFFFF:
        q += 8
    passes = obj.u32(rm + 4)
    return {"desc": (desc, p + 4 - desc), "decl": (decl, q + 4 - decl), "vs": (obj.u32(rm + 12), 4 * passes),
            "ps": (obj.u32(rm + 20), 4 * passes), "rm": (rm, 0x38), "block": (inst.offset, 4 + 8 * len(inst.params))}


def _bounds(points):
    if not points:
        return ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
    return (tuple(min(p[i] for p in points) for i in range(3)), tuple(max(p[i] for p in points) for i in range(3)))


def part_from_draw(template_blob, positions, normals, uvs, strip, bone, rename=None):
    """A part file: ``template_blob`` (a part of the same kind) with one draw (``positions``, ``normals`` or None,
    ``uvs``, one triangle ``strip``), every vertex on ``bone`` with full weight, as its only draw. It is a clone of
    the template's first draw: render model (descriptor, vertex declaration, shaders, a new label), parameter block
    with the template's targets and its texture reference kept, five new vertex buffers. ``rename`` (old, new):
    the template's part name in every symbol and label becomes the new part's (the same length)."""
    if not 0 < len(positions) <= 65535 or len(uvs) != len(positions) or (normals and len(normals) != len(positions)):
        raise RosterItemError("the draw has %d vertices and %d UVs" % (len(positions), len(uvs)))
    if len(strip) < 3 or max(strip) >= len(positions):
        raise RosterItemError("the draw's strip does not fit its vertices")
    if not _int(bone, 0, MAX_BONE):
        raise RosterItemError("bone %r" % (bone,))
    obj = _Eagl(template_blob)
    model = obj.models()[0]
    if not model.groups or not model.groups[0]:
        raise RosterItemError("the template part has no draw")
    first = model.groups[0][0]
    tmesh = obj.mesh(first)
    slots = tmesh.skin_slots
    if not slots:
        raise RosterItemError("the template part's draw has no matrix palette")
    normals = list(normals) if normals else [(0.0, 0.0, 1.0)] * len(positions)
    app = _Appender(obj)
    n = sum(len(g) for g in model.groups)
    weights = app.put(_entry_words(bone) + bytes(16), 16)
    lengths = app.put(struct.pack("<I", len(strip)), 16)
    indices = app.put(struct.pack("<%dH" % len(strip), *strip), 16)
    count = len(positions)
    streams = {
        0: b"".join(struct.pack("<3f", *p) for p in positions),
        1: b"".join(struct.pack("<2f", *uv) for uv in uvs),
        2: b"\xff\xff\xff\xff" * count,
        3: b"".join(struct.pack("<I", encode_normal(nm)) for nm in normals),
        4: struct.pack("<h", slots[0]) * count,
    }
    stream_data = {}
    for stream in sorted(streams):
        blob = streams[stream]
        desc = app.put(bytes(20), 16)
        data = app.put(blob, 4)
        app.put(VB_SENTINEL, 1)
        trailer = app.put(bytes(12), 1)
        app.pointer(desc, trailer)
        app.u32(desc + 4, len(blob))
        app.u32(desc + 8, STREAM_STRIDES[stream])
        app.u32(desc + 12, 1)
        app.pointer(desc + 16, data)
        app.u32(trailer, 1)
        app.pointer(trailer + 4, data)
        app.define("__VB:::__GPvb_%d_%d_%s" % (15 + stream, n, model.name), desc)
        stream_data[stream] = data
    parts = _template_parts(obj, first)
    remap = {}
    for key in ("desc", "decl", "vs", "ps"):
        start, length = parts[key]
        remap[start] = app.copy(start, length)
    label = ("%s_%d_RM = %s" % (model.name, n, first.rm.parent.replace("ParentRM_", ""))).encode("latin-1") + b"\0"
    label_off = app.put(label, 1)
    rm_start, rm_len = parts["rm"]
    rm = app.copy(rm_start, rm_len, remap, align=4)
    app.pointer(rm + 48, label_off)
    app.define("__RM:::__GP_RM_%s_%d" % (model.name, n), rm)
    block_start, block_len = parts["block"]
    block = app.copy(block_start, block_len, {rm_start: rm})
    p = block + 4
    for param in first.params:
        if param.type == P_WEIGHTS:
            app.u32(p, 1)
            app.pointer(p + 4, weights)
        elif param.type == P_PRIM_LENGTHS:
            app.u32(p, 1)
            app.pointer(p + 4, lengths)
        elif param.type == P_INDICES:
            app.u32(p, len(strip))
            app.pointer(p + 4, indices)
        elif param.type == P_VERTEX_STREAM:
            if param.words[1] not in stream_data:
                raise RosterItemError("the template draw reads vertex stream %d" % param.words[1])
            app.u32(p, count)
            app.pointer(p + 4, stream_data[param.words[1]])
        p += 8
    # the model: one material (the template's first), its one draw, its bounds; the texture list as it was
    m = model.offset
    lo, hi = _bounds(positions)
    names = app.put(bytes(4), 4)
    app.pointer(names, obj.local(obj.local(m + 0xA0)))
    app.u32(m + 0x9C, 1)
    app.pointer(m + 0xA0, names)
    lod = app.put(bytes(12), 16)
    app.u32(lod + 4, 1)
    app.pointer(lod + 8, block)
    app.pointer(m + 0xCC, lod)
    centre = tuple(0.5 * (a + b) for a, b in zip(lo, hi))
    radius = 0.5 * math.sqrt(sum((b - a) * (b - a) for a, b in zip(lo, hi)))
    bounds = struct.pack("<2I", 0x10000 + ((1 - 11) & 0xFFFF), 0x10000 + ((1 - 11 + 11) & 0xFFFF))
    bounds += struct.pack("<12f", lo[0], lo[1], lo[2], 1.0, hi[0], hi[1], hi[2], 1.0, centre[0], centre[1],
                          centre[2], radius)
    app.pointer(m + 0xC4, app.put(bounds, 4))
    old = obj.pointer(m + 0xBC)
    records, tag = [], 0x10001
    r = old[0] if old is not None and not old[1] else None
    while r is not None and obj.u32(r) != 0 and len(records) < 64:
        records.append((obj.local(r), obj.pointer(r + 8)[1]))
        tag = obj.u32(r + 12)
        r += 16
    tex = app.put(bytes(8 + 16 * len(records) + 4), 4)
    q = tex + 8
    for name_off, tar in records:
        app.pointer(q, name_off)
        app.u32(q + 4, 1)
        app.import_pointer(q + 8, tar)
        app.u32(q + 12, tag)
        q += 16
    app.pointer(m + 0xB8, tex)
    app.pointer(m + 0xBC, tex + 8)
    for name, off in obj.typed("BBOX"):
        if name == model.name:
            struct.pack_into("<6f", app.data, off, *(tuple(lo) + tuple(hi)))
    blob = app.to_bytes()
    if rename:
        old_name, new_name = (s.encode("latin-1") for s in rename)
        if len(old_name) != len(new_name):
            raise RosterItemError("%s and %s differ in length" % tuple(rename))
        blob = blob.replace(old_name, new_name)
    return blob


def find_draw(obj, material):
    """The first draw of the first material whose name (before ``~``) is ``material``."""
    m = obj.models()[0]
    for g, name in enumerate(m.materials):
        if name.split("~")[0] == material and g < len(m.groups) and m.groups[g]:
            return m.groups[g][0]
    raise RosterItemError("no material %s in %s (it has %s)" % (material, m.name, ", ".join(
        n.split("~")[0] for n in m.materials)))


def composite_rect(template_blob, size, composite=COMPOSITE):
    """(x, y, w, h) in pixels of the part image's place in the composite atlas: the template part's UV box, in
    texels, aligned down to the image size ``size`` (w, h)."""
    w, h = size
    obj = _Eagl(template_blob)
    us, vs = [], []
    for group in obj.models()[0].groups:
        for inst in group:
            mesh = obj.mesh(inst)
            if mesh.uvs:
                us += [u for u, _ in mesh.uvs]
                vs += [v for _, v in mesh.uvs]
    if not us or not w or not h:
        raise RosterItemError("the template part has no textured draw")
    x0 = int(min(us) * composite) // w * w
    y0 = int(min(vs) * composite) // h * h
    if (max(us) * composite > x0 + w + 0.5 or max(vs) * composite > y0 + h + 0.5 or x0 + w > composite
            or y0 + h > composite):
        raise RosterItemError("the template part's UVs do not sit in one %dx%d image of the composite" % (w, h))
    return x0, y0, w, h


def to_composite(uvs, rect, composite=COMPOSITE):
    """UVs on the part's own image (0..1) moved into its place ``rect`` in the composite atlas."""
    x0, y0, w, h = rect
    return [((x0 + u * w) / composite, (y0 + v * h) / composite) for u, v in uvs]


# -- SHPX textures -----------------------------------------------------------------------------------------------

TYPE_PALETTE, TYPE_P8, TYPE_BGRA = 0x2A, 0x7B, 0x7D
TYPE_DXT1, TYPE_DXT3, TYPE_DXT5 = 0x60, 0x61, 0x62
FLAG_SWIZZLED, FLAG_INDIRECT = 0x2000, 0x1000
IMAGE_TYPES = (TYPE_P8, TYPE_BGRA, TYPE_DXT1, TYPE_DXT3, TYPE_DXT5)
PIXEL_ALIGN, PALETTE_ALIGN = 128, 64


def directory(blob):
    """[(name, offset)] of an SHPX file."""
    if len(blob) < 16 or blob[:4] != b"SHPX":
        raise RosterItemError("not an SHPX texture file")
    size, count = struct.unpack_from("<II", blob, 4)
    if size != len(blob) or count > 4096 or 16 + count * 8 > size:
        raise RosterItemError("bad SHPX directory")
    out = []
    for i in range(count):
        name, off = struct.unpack_from("<4sI", blob, 16 + i * 8)
        if off + 16 > size:
            raise RosterItemError("bad SHPX record")
        out.append((name.decode("latin-1"), off))
    return out


def records(blob):
    """[(name, record bytes)]: each image from its header to the next image (or the end)."""
    entries = directory(blob)
    offsets = sorted(off for _, off in entries) + [len(blob)]
    return [(name, bytes(blob[off:offsets[offsets.index(off) + 1]])) for name, off in entries]


def morton_tables(w, h):
    """offset(x, y) = xs[x] | ys[y] of the NV2A swizzle: x and y bits interleaved, x first."""
    xs, ys = [0] * w, [0] * h
    target, bit = 0, 0
    while (1 << bit) < w or (1 << bit) < h:
        if (1 << bit) < w:
            for x in range(w):
                xs[x] |= ((x >> bit) & 1) << target
            target += 1
        if (1 << bit) < h:
            for y in range(h):
                ys[y] |= ((y >> bit) & 1) << target
            target += 1
        bit += 1
    return xs, ys


def _palette(blob, offset):
    """The BGRA palette a P8 image at ``offset`` reaches through its attachment chain."""
    pos, seen = offset, set()
    while pos not in seen:
        seen.add(pos)
        step = int.from_bytes(blob[pos + 1:pos + 4], "little")
        if not step:
            break
        pos += step
        if pos + 16 > len(blob) or len(seen) > 64:
            raise RosterItemError("bad attachment chain")
        if blob[pos] == TYPE_PALETTE:
            colours = struct.unpack_from("<H", blob, pos + 4)[0]
            if not 1 <= colours <= 256 or pos + 16 + colours * 4 > len(blob):
                raise RosterItemError("bad palette")
            return blob[pos + 16:pos + 16 + colours * 4]
    raise RosterItemError("no palette")


def p8_window(blob, offset, x0, y0, x1, y1):
    """(w, h, RGBA rows of the window x0..x1, y0..y1) of the 8-bit paletted image whose header is at ``offset``."""
    if blob[offset] != TYPE_P8:
        raise RosterItemError("the atlas is not an 8-bit paletted image")
    w, h = struct.unpack_from("<HH", blob, offset + 4)
    flags = struct.unpack_from("<I", blob, offset + 12)[0]
    if flags >> 28 or flags & FLAG_INDIRECT or not w or not h or offset + 16 + w * h > len(blob):
        raise RosterItemError("the atlas's layout is not a plain image")
    palette = _palette(blob, offset)
    lut = [bytes((palette[i * 4 + 2], palette[i * 4 + 1], palette[i * 4], palette[i * 4 + 3]))
           for i in range(len(palette) // 4)]
    if flags & FLAG_SWIZZLED:
        if w & (w - 1) or h & (h - 1):
            raise RosterItemError("a swizzled atlas that is not a power of two")
        xs, ys = morton_tables(w, h)
    else:
        xs, ys = list(range(w)), [y * w for y in range(h)]
    pixels = offset + 16
    out = bytearray()
    for y in range(y0, y1):
        row = ys[y]
        for x in range(x0, x1):
            k = blob[pixels + (row | xs[x] if flags & FLAG_SWIZZLED else row + x)]
            if k >= len(lut):
                raise RosterItemError("a palette index past the palette")
            out += lut[k]
    return w, h, bytes(out)


def cut_rect(w, h, uvs, margin=MARGIN):
    """(x0, y0, x1, y1): the pixels of the UV rectangle of ``uvs`` in a ``w`` x ``h`` image plus ``margin``."""
    us, vs = [u for u, _ in uvs], [v for _, v in uvs]
    x0 = max(0, int(min(us) * w) - margin)
    x1 = min(w, int(max(us) * w + 0.999) + margin)
    y0 = max(0, int(min(vs) * h) - margin)
    y1 = min(h, int(max(vs) * h + 0.999) + margin)
    if x1 <= x0 or y1 <= y0:
        raise RosterItemError("the draw's UVs cover no pixels of the atlas")
    return x0, y0, x1, y1


def cut_uvs(w, h, rect, uvs):
    """``uvs`` on a ``w`` x ``h`` image remapped onto its window ``rect``."""
    x0, y0, x1, y1 = rect
    cw, ch = x1 - x0, y1 - y0
    return [((u * w - x0) / cw, (v * h - y0) / ch) for u, v in uvs]


def resize(w, h, rgba, dw, dh):
    """RGBA ``w`` x ``h`` scaled to ``dw`` x ``dh``: per target pixel the average of the source span it covers
    (two-pixel blend when enlarging) of the colour multiplied by the alpha (``fighterart``, integer)."""
    if (w, h) == (dw, dh):
        return bytes(rgba)
    planes = fighterart._window(fighterart.Art(w, h, bytes(rgba)), dw, dh, (w, dw, 0), (h, dh, 0))
    out = bytearray(dw * dh * 4)
    for c in range(4):
        out[c::4] = planes[c]
    return bytes(out)


def quantise(rgba, colors=256):
    """(palette [(r, g, b, a)], index bytes) of RGBA pixels: the colours themselves when there are at most
    ``colors`` (in the order they first appear), else a median cut over the distinct colours (the box with the
    widest channel range times its pixel count is split at its pixel median; each box's mean colour) and every
    pixel the nearest palette colour (FFNY Studio ``shpxwrite.quantise``)."""
    counts = {}
    for i in range(0, len(rgba), 4):
        key = bytes(rgba[i:i + 4])
        counts[key] = counts.get(key, 0) + 1
    unique = list(counts)
    if len(unique) <= colors:
        lookup = {c: i for i, c in enumerate(unique)}
        return [tuple(c) for c in unique], bytes(lookup[bytes(rgba[i:i + 4])] for i in range(0, len(rgba), 4))

    def measure(box):
        return ([max(c[ch] for c in box) - min(c[ch] for c in box) for ch in range(4)],
                sum(counts[c] for c in box))

    boxes, sizes = [unique], [measure(unique)]
    while len(boxes) < colors:
        best, best_score = None, -1
        for k, box in enumerate(boxes):
            if len(box) < 2:
                continue
            score = max(sizes[k][0]) * sizes[k][1]
            if score > best_score:
                best, best_score = k, score
        if best is None:
            break
        box = boxes.pop(best)
        ranges, total = sizes.pop(best)
        ch = ranges.index(max(ranges))
        box.sort(key=lambda c: c[ch])
        acc, cut = 0, 1
        for i, c in enumerate(box):
            acc += counts[c]
            if acc * 2 >= total:
                cut = max(1, min(len(box) - 1, i + 1))
                break
        for part in (box[:cut], box[cut:]):
            boxes.append(part)
            sizes.append(measure(part))
    palette = []
    for box in boxes:
        n = sum(counts[c] for c in box)
        palette.append(tuple(int(round(sum(c[ch] * counts[c] for c in box) / n)) for ch in range(4)))
    cache = {}
    out = bytearray(len(rgba) // 4)
    for i in range(0, len(rgba), 4):
        key = bytes(rgba[i:i + 4])
        idx = cache.get(key)
        if idx is None:
            r, g, b, a = key
            idx = min(range(len(palette)), key=lambda k: (palette[k][0] - r) ** 2 + (palette[k][1] - g) ** 2
                      + (palette[k][2] - b) ** 2 + (palette[k][3] - a) ** 2)
            cache[key] = idx
        out[i // 4] = idx
    return palette, bytes(out)


def _attachment_chain(record):
    """The attachments of a P8 record (palette first) as bytes."""
    chain = record[int.from_bytes(record[1:4], "little"):]
    pos = 0
    for _ in range(16):
        if pos + 8 > len(chain):
            break
        step = int.from_bytes(chain[pos + 1:pos + 4], "little")
        if not step:
            size = 8 + struct.unpack_from("<I", chain, pos + 4)[0] if chain[pos] == 0x6F else 16
            end = pos + (size + 15) // 16 * 16
            if end > len(chain):
                raise RosterItemError("an attachment runs past the record")
            return bytes(chain[:end])
        pos += step
    raise RosterItemError("the texture's attachment chain is not one this can write")


def _name_block(chain):
    pos = 0
    for _ in range(16):
        if chain[pos] == 0x70:
            return pos
        step = int.from_bytes(chain[pos + 1:pos + 4], "little")
        if not step:
            return -1
        pos += step
    return -1


def p8_record(name, w, h, rgba, template_record):
    """An 8-bit paletted record of RGBA ``w`` x ``h`` in the layout of ``template_record`` (a P8 record: its flags,
    swizzled or linear, its attachments with the palette replaced by a 256-colour one and the name block named
    ``name``); the palette's colours start on a 64-byte boundary (FFNY Studio ``shpxwrite.p8_record``)."""
    flags = struct.unpack_from("<I", template_record, 12)[0]
    swizzled = bool(flags & FLAG_SWIZZLED)
    if swizzled and (w & (w - 1) or h & (h - 1)):
        raise RosterItemError("%s: %dx%d is not a power of two" % (name, w, h))
    palette, indices = quantise(rgba)
    chain = _attachment_chain(template_record)
    if chain[0] != TYPE_PALETTE:
        raise RosterItemError("the texture's first attachment is not a palette")
    step = int.from_bytes(chain[1:4], "little")
    if not (struct.unpack_from("<H", chain, 4)[0] == 256 and step == 16 + 1024):
        head = bytearray(chain[:16])
        head[1:4] = (16 + 1024).to_bytes(3, "little")
        struct.pack_into("<HHH", head, 4, 256, 1, 256)
        chain = bytes(head) + bytes(1024) + chain[step:]
    name_at = _name_block(chain)
    chain = bytearray(chain)
    pal = bytearray(1024)
    for i, (r, g, b, a) in enumerate(palette):
        pal[4 * i:4 * i + 4] = bytes((b, g, r, a))
    chain[16:16 + 1024] = pal
    if name_at >= 0:
        chain[name_at + 4:name_at + 8] = name.encode("latin-1")[:4].ljust(4, b"\0")
    gap = (-(w * h + 16)) % PALETTE_ALIGN
    header = bytearray(template_record[:16])
    header[0] = TYPE_P8
    header[1:4] = (16 + w * h + gap).to_bytes(3, "little")
    struct.pack_into("<HH", header, 4, w, h)
    if swizzled:
        struct.pack_into("<I", header, 12, FLAG_SWIZZLED)
        xs, ys = morton_tables(w, h)
        pixels = bytearray(w * h)
        for y in range(h):
            for x in range(w):
                pixels[ys[y] | xs[x]] = indices[y * w + x]
    else:
        struct.pack_into("<I", header, 12, flags & ~(FLAG_SWIZZLED | FLAG_INDIRECT))
        pixels = indices
    return bytes(header) + bytes(pixels) + bytes(gap) + bytes(chain)


def build_shpx(images):
    """An SHPX file of [(name, record bytes)], each record's pixels on a 128-byte boundary."""
    out = bytearray(b"SHPX" + bytes(8) + b"G359")
    for name, _ in images:
        out += name.encode("latin-1")[:4].ljust(4, b"\0") + bytes(4)
    out += b"Buy ERTS"
    offsets = []
    for _, record in images:
        out += bytes((-(len(out) + 16)) % PIXEL_ALIGN)
        offsets.append(len(out))
        out += record
    struct.pack_into("<II", out, 4, len(out), len(images))
    for i, off in enumerate(offsets):
        struct.pack_into("<I", out, 16 + 8 * i + 4, off)
    return bytes(out)


def image_size(xsh):
    """(w, h) of a part texture's first image."""
    for _, off in directory(xsh):
        if xsh[off] in IMAGE_TYPES:
            return struct.unpack_from("<HH", xsh, off + 4)
    raise RosterItemError("the texture holds no image")


def part_texture(template_xsh, w, h, rgba):
    """The template texture with RGBA ``w`` x ``h`` (scaled to the first image's size) as its first image, 8-bit
    paletted; its other records byte for byte, the file's version tag as the template has it."""
    recs = records(template_xsh)
    name, first = recs[0]
    flags = struct.unpack_from("<I", first, 12)[0]
    if first[0] != TYPE_P8 or flags >> 28 or flags & FLAG_INDIRECT:
        raise RosterItemError("the template texture is not a plain 8-bit paletted image")
    tw, th = struct.unpack_from("<HH", first, 4)
    image = resize(w, h, rgba, tw, th)
    palette, indices = quantise(image)
    flat = b"".join(bytes(palette[k]) for k in indices)
    built = bytearray(build_shpx([(name, p8_record(name, tw, th, flat, first))] + recs[1:]))
    built[12:16] = template_xsh[12:16]
    return bytes(built)


# -- the shop thumbnails (small BIG archives of an Apt movie with one DXT5 picture) ---------------------------------

def _big_entries(blob):
    if len(blob) < 16 or blob[:4] not in (b"BIGF", b"BIG4"):
        raise RosterItemError("the thumbnail is not a BIG archive")
    count, header_size = struct.unpack_from(">II", blob, 8)
    if count > 64:
        raise RosterItemError("the thumbnail has %d entries" % count)
    pos, out = 16, []
    for _ in range(count):
        offset, size = struct.unpack_from(">II", blob, pos)
        end = blob.find(b"\0", pos + 8, pos + 8 + 256)
        if end < 0 or offset + size > len(blob):
            raise RosterItemError("the thumbnail's directory is unreadable")
        out.append((pos, blob[pos + 8:end].decode("latin-1"), offset, size))
        pos = end + 1
    return header_size, out


def thumb_file(template_big, new_base, w, h, rgba):
    """The template thumbnail with its entries renamed to ``new_base`` (``V2TN_144_0010_Y01_01`` or ``..._t``) and
    its DXT5 picture painted with RGBA ``w`` x ``h`` scaled to its size; everything else byte for byte."""
    _, entries = _big_entries(template_big)
    if not entries:
        raise RosterItemError("the thumbnail is empty")
    old_base = entries[0][1].rsplit(".", 1)[0]
    if len(new_base) != len(old_base):
        raise RosterItemError("%s and %s differ in length" % (new_base, old_base))
    out = bytearray(template_big)
    for pos, name, offset, size in entries:
        if not name.startswith(old_base):
            raise RosterItemError("the thumbnail's entry %s is not named after %s" % (name, old_base))
        out[pos + 8:pos + 8 + len(name)] = (new_base + name[len(old_base):]).encode("latin-1")
        if name.lower().endswith(".xsh"):
            xsh = bytes(template_big[offset:offset + size])
            (_, off), = directory(xsh)
            tw, th = struct.unpack_from("<HH", xsh, off + 4)
            flags = struct.unpack_from("<I", xsh, off + 12)[0]
            if xsh[off] != TYPE_DXT5 or flags & 0xF0003000 or tw % 4 or th % 4 or off + 16 + tw * th > size:
                raise RosterItemError("the thumbnail's picture is not a plain DXT5 image")
            image = resize(w, h, rgba, tw, th)
            pixels = fighterart.encode_texture(image[0::4], image[1::4], image[2::4], image[3::4], tw, th,
                                               fighterart.DXT5)
            out[offset + off + 16:offset + off + 16 + len(pixels)] = pixels
    return bytes(out)


# -- the item's files ----------------------------------------------------------------------------------------------

class Piece:
    """The roster draw and its image: ``positions``, ``normals``, ``strip``, ``uvs`` (on the image), the image
    (``width`` x ``height`` RGBA ``rgba``) and the atlas size."""

    def __init__(self, positions, normals, strip, uvs, width, height, rgba, atlas_size):
        self.positions, self.normals, self.strip, self.uvs = positions, normals, strip, uvs
        self.width, self.height, self.rgba, self.atlas_size = width, height, rgba, atlas_size


def roster_piece(model_blob, atlas_xsh, material):
    """The first draw of ``material`` in a roster model (``V2IP_<n>A.o``) and its image cut out of the model's
    atlas (its ``.xsh``: the image the model names, else the first)."""
    obj = _Eagl(model_blob)
    inst = find_draw(obj, material)
    mesh = obj.mesh(inst)
    if mesh.prim_type != "TRIANGLESTRIP" or not mesh.indices:
        raise RosterItemError("the draw is not a triangle strip")
    if len(mesh.prim_lengths or [len(mesh.indices)]) != 1:
        raise RosterItemError("the draw has %d strips; one is supported" % len(mesh.prim_lengths))
    if not mesh.positions or not mesh.uvs or len(mesh.uvs) != len(mesh.positions):
        raise RosterItemError("the draw has no textured vertices")
    shapes = obj.models()[0].textures
    found = directory(atlas_xsh)
    offset = next((off for name, off in found if shapes and name == shapes[0]), None)
    if offset is None:
        offset = next((off for _, off in found if atlas_xsh[off] in IMAGE_TYPES), None)
    if offset is None:
        raise RosterItemError("the model's texture file holds no image")
    w, h = struct.unpack_from("<HH", atlas_xsh, offset + 4)
    rect = cut_rect(w, h, mesh.uvs)
    _, _, rgba = p8_window(atlas_xsh, offset, *rect)
    return Piece(mesh.positions, mesh.normals, mesh.indices, cut_uvs(w, h, rect, mesh.uvs), rect[2] - rect[0],
                 rect[3] - rect[1], rgba, (w, h))


def item_files(read, op, template_part, template_texture, report=None):
    """[(archive, entry, bytes)] of an item op with a ``roster_part`` (:func:`op_problem`): the part, its texture and
    the thumbnails the template texture has (:func:`built_entries` order). ``read(archive, entry)``: the player's
    own entry (RefPack removed; KeyError when there is none). ``template_part`` / ``template_texture``: the template
    row's part and first texture (``V2CP_144_0001_C``, ``V2CP_144_0001_A01_01``). ``report``: a list that gets
    lines saying what was made."""
    problem = op_problem(op)
    if problem:
        raise RosterItemError(problem)
    spec = op[KEY]
    name, texture = op["model"]["name"], op["colours"][0]["texture"]
    category = int(name[5:8])
    if not (PART_MODEL.fullmatch(template_part) and PART_TEXTURE.fullmatch(template_texture)
            and int(template_part[5:8]) == category and int(template_texture[5:8]) == category):
        raise RosterItemError("the template %s / %s is not of the part's category %03d" % (
            template_part, template_texture, category))
    archive = part_archive(category)

    def need(arch, entry):
        try:
            return read(arch, entry)
        except KeyError:
            raise RosterItemError("%s|%s is not in your game files" % (arch, entry)) from None

    piece = roster_piece(need(ROSTER_ARCHIVE, spec["model"] + ".o"), need(ROSTER_ARCHIVE, spec["model"] + ".xsh"),
                         spec["material"])
    template_blob = need(archive, template_part + ".o")
    template_xsh = need(archive, template_texture + ".xsh")
    uvs = to_composite(piece.uvs, composite_rect(template_blob, image_size(template_xsh)))
    out = [(archive, name + ".o", part_from_draw(template_blob, piece.positions, piece.normals, uvs, piece.strip,
                                                 spec["bone"], rename=(template_part, name))),
           (archive, texture + ".xsh", part_texture(template_xsh, piece.width, piece.height, piece.rgba))]
    for small in (False, True):
        try:
            base = read(SCREENS, thumb_entry(template_texture, small))
        except KeyError:
            continue
        out.append((SCREENS, thumb_entry(texture, small),
                    thumb_file(base, "V2TN_%s%s" % (texture[5:], "_t" if small else ""), piece.width, piece.height,
                               piece.rgba)))
    if report is not None:
        report.append("%s %s: %d vertices, strip %d, bone %d; image %dx%d cut from the %dx%d atlas; %s as %s" % (
            spec["model"], spec["material"], len(piece.positions), len(piece.strip), spec["bone"], piece.width,
            piece.height, piece.atlas_size[0], piece.atlas_size[1], template_part, name))
    return out
