"""The EAGL object model on top of an ELF relocatable: symbols, pointers and typed views.

An EAGL ``.o`` is one ``.data`` blob. Pointers inside it are stored as addends and fixed up by ``R_MIPS_32``
relocations: against the section symbol for an internal pointer (the stored word is the target offset), or
against an undefined symbol for something the engine supplies at load time (render methods, shader
microcode, texture slots ``__EAGL::TAR:::...``, ``EAGLAnimationBuffer``). Typed objects are named by
symbols ``__<Type>:::<name>``. See docs/research/ffny-studio-eagl.md for the layouts decoded here.

Nothing is modified on parse; ``EaglObject.to_bytes()`` re-emits the object byte for byte.
"""

import math
import struct
from dataclasses import dataclass, field

from . import elf

# -- render-model descriptor entry types (hi16 of the entry's first word; lo16 is its length in words) ------
P_COMP_INDEX = 0x03     # ComputationIndex (consumes a parameter)
P_STATE_BEGIN = 0x04    # no parameter
P_GEO_PRIM_STATE = 0x10
P_TEXTURE = 0x0F        # __EAGL::TAR:::... texture slot
P_WEIGHTS = 0x1F        # skin weight palette: {w0, w1, w2, packed bone bytes as float} per entry
P_MATRIX_PALETTE = 0x1D # EAGLAnimationBuffer (the bone matrices)
P_PRIM_LENGTHS = 0x02   # u32 per primitive run (one triangle strip per render model in the fighters)
P_INDICES = 0x0E        # u16 vertex indices
P_MATRIX = 0x14         # a view-port matrix bound to a constant register
P_CONST = 0x05
P_CONST_BLOCK = 0x06
P_CONST_23 = 0x23
P_VERTEX_STREAM = 0x09  # entry words {0, stream, stride}
P_DRAW_INDEXED = 0x1A   # no parameter; entry words {0, stream count}
P_DRAW_1 = 0x01         # no parameter (point sprites)
P_DRAW_ARRAYS = 0x19    # no parameter (point sprites)
NO_PARAMETER = {P_STATE_BEGIN, P_DRAW_INDEXED, P_DRAW_1, P_DRAW_ARRAYS}

PARAM_NAMES = {
    P_COMP_INDEX: "comp_index", P_STATE_BEGIN: "state_begin", P_GEO_PRIM_STATE: "geo_prim_state",
    P_TEXTURE: "texture", P_WEIGHTS: "weights", P_MATRIX_PALETTE: "matrix_palette",
    P_PRIM_LENGTHS: "prim_lengths", P_INDICES: "indices", P_MATRIX: "matrix", P_CONST: "const",
    P_CONST_BLOCK: "const_block", P_CONST_23: "const_23", P_VERTEX_STREAM: "vertex_stream",
    P_DRAW_INDEXED: "draw_indexed", P_DRAW_1: "draw_1", P_DRAW_ARRAYS: "draw_arrays",
}

# -- NV2A vertex attribute formats (vertex declaration: low nibble type, high nibble component count) --------
NV_UB_D3D, NV_S1, NV_F, NV_UB_OGL, NV_S32K, NV_CMP = 0, 1, 2, 4, 5, 6

VB_SENTINEL = b"\xbb\xbb\xbb\xbb"
SLOT_REGISTERS = 4       # NV2A constant registers per matrix-palette slot


@dataclass
class Pointer:
    """A relocated word: ``local`` targets .data at ``offset``; otherwise ``symbol`` names an import."""
    offset: int
    symbol: str = ""
    addend: int = 0

    @property
    def local(self):
        return not self.symbol


@dataclass
class Bone:
    index: int
    name: str
    parent: int
    mirror: int                 # the left/right counterpart (itself for centre bones)
    scale: tuple
    rotation: tuple             # quaternion x, y, z, w (local, relative to the parent)
    translation: tuple          # local, relative to the parent
    inverse_bind: tuple         # 16 floats, row-major, row vectors (D3D): world -> bone space


@dataclass
class Skeleton:
    name: str
    offset: int
    hash: int
    bones: list


@dataclass
class VertexBuffer:
    name: str
    offset: int                 # descriptor offset
    trailer: int
    byte_length: int
    stride: int
    data_offset: int

    @property
    def count(self):
        return self.byte_length // self.stride if self.stride else 0


@dataclass
class DescriptorEntry:
    type: int
    words: tuple                # the entry's words after the type word

    @property
    def name(self):
        return PARAM_NAMES.get(self.type, "type_%02x" % self.type)


@dataclass
class RenderModel:
    """``__RM:::`` : the parameter layout and shaders of one draw."""
    name: str
    offset: int
    descriptor: list
    vertex_decl: list           # [(stream, nv2a format byte, attribute word)]
    passes: int
    parent: str                 # ParentRM_* render method
    label: str


@dataclass
class Parameter:
    type: int
    entry: DescriptorEntry
    count: int
    target: Pointer

    @property
    def name(self):
        return self.entry.name


@dataclass
class Instance:
    """One draw of a model: a render model plus its parameter values."""
    offset: int
    rm: RenderModel
    params: list

    def first(self, ptype):
        for param in self.params:
            if param.type == ptype:
                return param
        return None

    def all(self, ptype):
        return [p for p in self.params if p.type == ptype]


@dataclass
class Model:
    name: str
    offset: int
    materials: list             # material names
    textures: list              # SHPX image names the model lists
    bbox: tuple                 # (min xyz, max xyz) from __BBOX when present
    groups: list = field(default_factory=list)   # per material: [Instance]


@dataclass
class Mesh:
    """Decoded geometry of one instance."""
    name: str
    positions: list
    normals: list = None
    uvs: list = None
    colors: list = None
    skin: list = None           # raw int16 per vertex
    indices: list = None
    prim_lengths: list = None
    prim_type: str = "TRIANGLESTRIP"
    weights: list = None        # [(w0, w1, w2), (b0, b1, b2)] per weight entry
    skin_slots: list = None     # skin stream value (constant register) of each weight entry, in order
    texture: str = ""
    material: str = ""
    render_method: str = ""
    state: str = ""             # the GeoPrimState in effect (EAGL state setters)

    @property
    def blended(self):
        return "ABM_BLEND" in self.state or "TM_ALPHA" in self.state

    def triangles(self):
        return strip_triangles(self.indices, self.prim_lengths) if self.prim_type == "TRIANGLESTRIP" \
            else list_triangles(self.indices)

    def joints_weights(self):
        """Per vertex ([j0..j3], [w0..w3]) from the skin stream and the weight palette, or None."""
        if self.skin is None or not self.weights or not self.skin_slots:
            return None
        slot_of = {register: i for i, register in enumerate(self.skin_slots)}
        joints, weights = [], []
        for value in self.skin:
            entry = slot_of.get(value, -1)
            if not 0 <= entry < len(self.weights):
                joints.append((0, 0, 0, 0)); weights.append((0.0, 0.0, 0.0, 0.0))
                continue
            w, b = self.weights[entry]
            joints.append((b[0], b[1], b[2], 0))
            weights.append((w[0], w[1], w[2], 0.0))
        return joints, weights


def strip_triangles(indices, lengths=None):
    """Triangle strip(s) -> counter-clockwise triangles (glTF front faces), dropping degenerates.

    The data is authored for D3D's clockwise front faces: strip triangle i is (s[i], s[i+1], s[i+2]) for even i,
    (s[i+1], s[i], s[i+2]) for odd i, clockwise as seen from outside (checked against the vertex normals),
    so each is emitted reversed. ``lengths`` splits ``indices`` into separate strips (EAGL stores one length
    per strip; the fighters join their pieces with repeated indices into one strip per render model)."""
    out = []
    runs = lengths or [len(indices)]
    start = 0
    for length in runs:
        strip = indices[start:start + length]
        start += length
        for i in range(len(strip) - 2):
            a, b, c = strip[i], strip[i + 1], strip[i + 2]
            if a == b or b == c or a == c:
                continue
            out.append((a, c, b) if i % 2 == 0 else (a, b, c))
    return out


def list_triangles(indices):
    return [(indices[i], indices[i + 2], indices[i + 1]) for i in range(0, len(indices) - 2, 3)]


def decode_normal_cmp(word):
    """NV2A NORMPACKED3 (CMP): x 11 bits, y 11 bits, z 10 bits, two's complement, normalised."""
    x = word & 0x7FF
    y = (word >> 11) & 0x7FF
    z = (word >> 22) & 0x3FF
    x = x - 0x800 if x & 0x400 else x
    y = y - 0x800 if y & 0x400 else y
    z = z - 0x400 if z & 0x200 else z
    return (x / 1023.0, y / 1023.0, z / 511.0)


def encode_normal_cmp(n):
    x = max(-1023, min(1023, int(round(n[0] * 1023)))) & 0x7FF
    y = max(-1023, min(1023, int(round(n[1] * 1023)))) & 0x7FF
    z = max(-511, min(511, int(round(n[2] * 511)))) & 0x3FF
    return x | (y << 11) | (z << 22)


def palette_slots(entry):
    """The matrix palette descriptor (type 0x1d) lists register ranges {start, length} after one word;
    each 4-register slot holds the blended matrix of one weight entry, in order. The fighters use
    {-96, 56} and {-36, 104}: 14 + 26 = 40 slots, skipping c[-40..-37]."""
    words = [w - (1 << 32) if w & 0x80000000 else w for w in entry.words[1:]]
    slots = []
    for start, count in zip(words[0::2], words[1::2]):
        slots.extend(range(start, start + count, SLOT_REGISTERS))
    return slots


def unpack_bones(packed):
    """The three bone indices a weight entry stores as an integer-valued float: bytes 3, 2, 1."""
    value = int(packed) & 0xFFFFFFFF
    return ((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF)


class EaglObject:
    def __init__(self, blob, name=""):
        self.name = name
        self.original = bytes(blob)
        self.elf = elf.parse(blob)
        self.data_index, self._data = self.elf.section(".data")
        self.data = self._data.data
        self.symbols = self.elf.symbols()
        self.relocs = {r.offset: r for r in self.elf.relocations(".data")}
        self.by_name = {s.name: s for s in self.symbols if s.name}
        self._rm_cache = {}

    # -- raw access ---------------------------------------------------------------------------------------

    def u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def i32(self, off):
        return struct.unpack_from("<i", self.data, off)[0]

    def f32(self, off, n=1):
        values = struct.unpack_from("<%df" % n, self.data, off)
        return values[0] if n == 1 else values

    def cstr(self, off):
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("latin-1")

    def pointer(self, off):
        """The pointer stored at ``off``, or None when the word is not relocated."""
        rel = self.relocs.get(off)
        if rel is None:
            return None
        sym = self.symbols[rel.sym]
        word = self.u32(off)
        if sym.name and sym.shndx == 0:
            return Pointer(word, sym.name, word)
        return Pointer(word + (sym.value if sym.name else 0))

    def typed(self, kind):
        """Symbols ``__<kind>:::name`` defined in .data, as (name, offset)."""
        prefix = "__%s:::" % kind
        return [(s.name[len(prefix):], s.value) for s in self.symbols
                if s.name.startswith(prefix) and s.shndx == self.data_index]

    def imports(self):
        return sorted(s.name for s in self.symbols if s.name and s.shndx == 0)

    def to_bytes(self):
        self._data.data = self.data
        return self.elf.to_bytes()

    # -- skeleton -----------------------------------------------------------------------------------------

    def bone_indices(self):
        """__Bone:::root.<name> records hold the bone's index: {index, 0, 0, 0}."""
        return {self.u32(off): name.split(".", 1)[-1] for name, off in self.typed("Bone")}

    def skeleton(self):
        found = self.typed("Skeleton")
        if not found:
            return None
        name, off = found[0]
        count = self.u32(off + 8)
        names = self.bone_indices()
        bones = []
        for i in range(count):
            r = off + 16 + i * 0x70
            bones.append(Bone(
                index=i, name=names.get(i, "bone%d" % i), parent=self.i32(r + 12), mirror=self.i32(r + 44),
                scale=self.f32(r, 3), rotation=self.f32(r + 16, 4), translation=self.f32(r + 32, 3),
                inverse_bind=self.f32(r + 48, 16)))
        return Skeleton(name, off, self.u32(off), bones)

    # -- vertex buffers -----------------------------------------------------------------------------------

    def vertex_buffers(self):
        out = []
        for name, off in self.typed("VB"):
            out.append(VertexBuffer(name, off, self.u32(off), self.u32(off + 4), self.u32(off + 8),
                                    self.u32(off + 16)))
        return out

    def check_vertex_buffer(self, vb):
        """True when the descriptor, data, sentinel and trailer agree."""
        end = vb.data_offset + vb.byte_length
        return (vb.data_offset == vb.offset + 20 and self.data[end:end + 4] == VB_SENTINEL
                and vb.trailer == end + 4 and self.u32(vb.trailer + 4) == vb.data_offset)

    # -- render models and instances ----------------------------------------------------------------------

    def render_model(self, off):
        if off in self._rm_cache:
            return self._rm_cache[off]
        names = {o: n for n, o in self.typed("RM")}
        desc_ptr = self.u32(off)
        entries, p = [], desc_ptr
        while self.u32(p) != 0:
            code = self.u32(p)
            length = code & 0xFFFF
            if not length or len(entries) > 256:
                raise ValueError("bad render model descriptor at 0x%x" % p)
            entries.append(DescriptorEntry(code >> 16, tuple(self.u32(p + 4 * i) for i in range(1, length))))
            p += 4 * length
        decl, q = [], self.u32(off + 8)
        while self.u32(q) != 0xFFFFFFFF and len(decl) < 16:
            first, second = self.u32(q), self.u32(q + 4)
            decl.append((first & 0xFFFF, (second >> 16) & 0xFF, second))
            q += 8
        parent = self.pointer(off + 28)
        label_ptr = self.pointer(off + 48)
        rm = RenderModel(names.get(off, "rm_%x" % off), off, entries, decl, self.u32(off + 4),
                         parent.symbol if parent else "", self.cstr(label_ptr.offset) if label_ptr else "")
        self._rm_cache[off] = rm
        return rm

    def instance(self, off):
        rm_ptr = self.pointer(off)
        if rm_ptr is None or not rm_ptr.local:
            raise ValueError("no render model pointer at 0x%x" % off)
        rm = self.render_model(rm_ptr.offset)
        params, p = [], off + 4
        for entry in rm.descriptor:
            if entry.type in NO_PARAMETER:
                continue
            params.append(Parameter(entry.type, entry, self.u32(p), self.pointer(p + 4)))
            p += 8
        return Instance(off, rm, params)

    def models(self):
        out = []
        bboxes = dict(self.typed("BBOX"))
        for name, off in self.typed("Model"):
            nmat = self.u32(off + 0x9C)
            mats_ptr = self.pointer(off + 0xA0)
            materials = []
            if mats_ptr:
                for i in range(nmat):
                    sp = self.pointer(mats_ptr.offset + 4 * i)
                    materials.append(self.cstr(sp.offset) if sp else "")
            textures = sorted({texture_shape(s) for s in self.imports() if texture_shape(s)})
            bbox = None
            if name in bboxes:
                values = self.f32(bboxes[name], 6)
                bbox = (values[:3], values[3:])
            model = Model(name, off, materials, textures, bbox)
            lod = self.pointer(off + 0xCC)
            if lod:
                p = lod.offset + 4
                for _ in range(nmat):
                    count = self.u32(p)
                    model.groups.append([self.instance(self.pointer(p + 4 + 4 * i).offset) for i in range(count)])
                    p += 4 + 4 * count
            out.append(model)
        return out

    # -- geometry -----------------------------------------------------------------------------------------

    def stream(self, ptr, count, fmt, stride):
        base = ptr.offset
        kind, size = fmt & 0xF, fmt >> 4
        out = []
        for i in range(count):
            o = base + i * stride
            if kind == NV_F:
                out.append(struct.unpack_from("<%df" % size, self.data, o))
            elif kind == NV_CMP:
                out.append(decode_normal_cmp(self.u32(o)))
            elif kind == NV_S32K:
                out.append(struct.unpack_from("<%dh" % size, self.data, o))
            elif kind == NV_S1:
                out.append(tuple(v / 32767.0 for v in struct.unpack_from("<%dh" % size, self.data, o)))
            elif kind == NV_UB_D3D:   # D3DCOLOR: B, G, R, A
                b, g, r, a = self.data[o:o + 4]
                out.append((r / 255.0, g / 255.0, b / 255.0, a / 255.0))
            elif kind == NV_UB_OGL:
                out.append(tuple(v / 255.0 for v in self.data[o:o + size]))
            else:
                raise ValueError("unsupported vertex format 0x%02x" % fmt)
        return out

    def mesh(self, inst, material="", state=None):
        """Decode one instance's geometry. ``state`` carries parameters inherited from the group's first draw."""
        state = dict(state or {})
        for param in inst.params:
            state[param.type] = param
            if param.type == P_TEXTURE and param.target and texture_shape(param.target.symbol):
                state.setdefault("texture", param)
        streams = [p for p in inst.params if p.type == P_VERTEX_STREAM]
        decl = {stream: fmt for stream, fmt, _ in inst.rm.vertex_decl}
        mesh = Mesh(inst.rm.name, [], material=material, render_method=inst.rm.parent)
        for param in streams:
            _, stream, stride = param.entry.words[:3]
            fmt = decl.get(stream)
            if fmt is None or param.target is None or not param.target.local:
                continue
            values = self.stream(param.target, param.count, fmt, stride)
            kind = fmt & 0xF
            if kind == NV_F and fmt >> 4 == 3 and not mesh.positions:
                mesh.positions = values
            elif kind == NV_F and fmt >> 4 == 2 and mesh.uvs is None:
                mesh.uvs = values
            elif kind == NV_CMP:
                mesh.normals = values
            elif kind == NV_UB_D3D:
                mesh.colors = values
            elif kind == NV_S32K and fmt >> 4 == 1:
                mesh.skin = [v[0] for v in values]
        idx = inst.first(P_INDICES)
        if idx:
            mesh.indices = list(struct.unpack_from("<%dH" % idx.count, self.data, idx.target.offset))
        lengths = inst.first(P_PRIM_LENGTHS)
        if lengths:
            mesh.prim_lengths = list(struct.unpack_from("<%dI" % lengths.count, self.data, lengths.target.offset))
        weights = inst.first(P_WEIGHTS)
        if weights and weights.target and weights.target.local:
            mesh.weights = []
            for i in range(weights.count):
                w = self.f32(weights.target.offset + 16 * i, 4)
                mesh.weights.append((w[:3], unpack_bones(w[3])))
        for entry in inst.rm.descriptor:
            if entry.type == P_MATRIX_PALETTE:
                mesh.skin_slots = palette_slots(entry)
        textures = [p for p in inst.params if p.type == P_TEXTURE and p.target
                    and texture_shape(p.target.symbol)]
        if not textures and state.get("texture"):
            textures = [state["texture"]]
        if textures:
            mesh.texture = texture_shape(textures[0].target.symbol)
        geo = state.get(P_GEO_PRIM_STATE)
        if geo and geo.target:
            mesh.state = geo.target.symbol
        if geo and geo.target and "PT_" in geo.target.symbol:
            mesh.prim_type = geo.target.symbol.split("SetPrimitiveType=EAGL::PT_")[1].split(";")[0]
        return mesh

    def meshes(self, model=None):
        """Every drawable mesh of a model (default: the first), in material order."""
        models = self.models()
        if not models:
            return []
        model = model or models[0]
        out = []
        for material, group in zip(model.materials, model.groups):
            state = {}
            for inst in group:
                mesh = self.mesh(inst, material, state)
                for param in inst.params:
                    state[param.type] = param
                    if param.type == P_TEXTURE and param.target and texture_shape(param.target.symbol):
                        state["texture"] = param
                if mesh.positions and mesh.indices:
                    out.append(mesh)
        return out


def texture_shape(tar_symbol):
    """``__EAGL::TAR:::RUNTIME_ALLOC::...SHAPENAME=A003,1;...`` -> ``A003`` (empty for runtime slots)."""
    if "SHAPENAME=" not in tar_symbol:
        return ""
    return tar_symbol.split("SHAPENAME=")[1].split(",")[0].split(";")[0]


def quat_to_matrix(q):
    x, y, z, w = q
    return ((1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)),
            (2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)),
            (2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)))


def bind_positions(skeleton):
    """World position of each bone from its inverse bind matrix (rigid: p = -t * R^T)."""
    out = []
    for bone in skeleton.bones:
        m = bone.inverse_bind
        t = m[12:15]
        rows = (m[0:3], m[4:7], m[8:11])
        out.append(tuple(-(rows[c][0] * t[0] + rows[c][1] * t[1] + rows[c][2] * t[2]) for c in range(3)))
    return out


def world_matrices(skeleton):
    """World matrices (row-major, row vectors) from the local TRS chain."""
    world = []
    for bone in skeleton.bones:
        r = quat_to_matrix(bone.rotation)
        s = bone.scale
        local = [[r[i][j] * s[i] for j in range(3)] + [0.0] for i in range(3)]
        local.append(list(bone.translation) + [1.0])
        if bone.parent >= 0:
            p = world[bone.parent]
            local = [[sum(local[i][k] * p[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
        world.append(local)
    return world


def length(v):
    return math.sqrt(sum(c * c for c in v))
