"""Synthetic EAGL objects for the FFNY Studio tests: built here byte by byte, no game data."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[2] / "tools" / "ffny-studio"))
from ffnystudio import eagl, elf  # noqa: E402

GLOBAL_OBJECT = (1 << 4) | 1
GLOBAL_NOTYPE = 1 << 4


class Blob:
    """Lays out a .data section with internal pointers (section-symbol relocations) and imports."""

    def __init__(self):
        self.data = bytearray()
        self.relocs = []          # (offset, symbol key)
        self.defs = []            # (name, offset)
        self.imports = []

    def here(self):
        return len(self.data)

    def align(self, n=16):
        while len(self.data) % n:
            self.data.append(0)

    def define(self, name):
        self.defs.append((name, self.here()))

    def words(self, *values):
        for v in values:
            self.data += struct.pack("<I", v & 0xFFFFFFFF)

    def floats(self, *values):
        self.data += struct.pack("<%df" % len(values), *values)

    def ptr(self, target):
        self.relocs.append((self.here(), None))
        self.words(target)

    def ext(self, name):
        if name not in self.imports:
            self.imports.append(name)
        self.relocs.append((self.here(), name))
        self.words(0)

    def patch(self, at, value):
        struct.pack_into("<I", self.data, at, value)

    def build(self):
        symbols = [(n, off, 4, GLOBAL_OBJECT, 1) for n, off in self.defs]
        symbols += [(n, 0, 0, GLOBAL_NOTYPE, 0) for n in self.imports]
        index = {n: 2 + len(self.defs) + i for i, n in enumerate(self.imports)}
        rels = [(off, 1 if key is None else index[key], elf.R_MIPS_32) for off, key in self.relocs]
        return elf.build_object(bytes(self.data), symbols, rels).to_bytes()


POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 2.0, 0.0), (1.0, 2.0, 0.5), (0.5, 3.0, 0.0)]
UVS = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0), (0.5, 0.5)]
NORMALS = [(0.0, 0.0, 1.0), (0.0, 0.0, -1.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.6, 0.8, 0.0)]
SKIN = [-96, -96, -92, -92, -92]
INDICES = [0, 1, 2, 3, 3, 4]
TAR = "__EAGL::TAR:::RUNTIME_ALLOC::UID=1;SHAPENAME=T001,1;"
GEO = "__EAGL::GeoPrimState:::RUNTIME_ALLOC::UID=1;SetTransparencyMethod=EAGL::TM_ALPHA;" \
      "SetPrimitiveType=EAGL::PT_TRIANGLESTRIP;"


def fighter():
    b = Blob()
    b.define("__EAGL_TOOLLIB_VERSION:::EAGL_TOOLLIB_VERSION-4")
    b.words(0x10000, 0, 0, 0)
    weights = b.here()
    b.floats(1.0, 0.0, 0.0, float(0x00000000))          # bone 0
    b.floats(0.5, 0.5, 0.0, float(0x01020000))          # bones 1 and 2
    b.words(0, 0, 0, 0)
    lengths = b.here()
    b.words(len(INDICES), 0, 0, 0)
    indices = b.here()
    b.data += struct.pack("<%dH" % len(INDICES), *INDICES)
    b.align()

    streams = []                                         # (stream, fmt, stride, payload)
    streams.append((0, 0x32, 12, b"".join(struct.pack("<3f", *p) for p in POSITIONS)))
    streams.append((1, 0x22, 8, b"".join(struct.pack("<2f", *uv) for uv in UVS)))
    streams.append((2, 0x40, 4, bytes([0x10, 0x20, 0x30, 0xFF]) * 5))    # B, G, R, A
    streams.append((3, 0x16, 4, b"".join(struct.pack("<I", eagl.encode_normal_cmp(n)) for n in NORMALS)))
    streams.append((4, 0x15, 2, struct.pack("<5h", *SKIN)))
    data_at = {}
    for stream, fmt, stride, payload in streams:
        b.define("__VB:::__GPvb_%d_0_TEST" % (15 + stream))
        desc = b.here()
        b.words(0, len(payload), stride, 1)
        b.ptr(desc + 20)
        data_at[stream] = b.here()
        b.data += payload
        b.data += b"\xbb" * 4
        b.patch(desc, b.here())
        b.relocs.append((desc, None))
        b.words(1)
        b.ptr(data_at[stream])
        b.words(0, 0)
        b.align()

    descriptor = b.here()
    b.words(0x000F0003, 0, 0x50)
    b.words(0x00100002, 0)
    b.words(0x001F0002, 0)
    b.words(0x001D0006, 0, -96, 8, -36, 8)
    b.words(0x00020002, 0)
    b.words(0x000E0002, 0)
    for stream, fmt, stride, _ in streams:
        b.words(0x00090004, 0, stream, stride)
    b.words(0x001A0003, 0, len(streams))
    b.words(0)
    decl = b.here()
    for stream, fmt, _, _ in streams:
        b.words(0x20000000 | stream, 0x40000000 | (fmt << 16) | stream)
    b.words(0xFFFFFFFF)
    shaders = b.here()
    b.ext("Test0__EAGLMicroCodeVS")
    label = b.here()
    b.data += b"TEST_0_RM = LitTest\0"
    b.align(4)
    b.define("__RM:::__GP_RM_TEST_0")
    rm = b.here()
    b.ptr(descriptor); b.words(1); b.ptr(decl); b.ptr(shaders); b.words(0); b.ptr(shaders); b.words(0)
    b.ext("ParentRM_LitTest"); b.words(0, 0, 0xFFFFFFFF); b.ext("ParentRM_LitTest"); b.ptr(label)
    b.words(0xDDDDDDDD)
    instance = b.here()
    b.ptr(rm)
    b.words(1); b.ext(TAR)
    b.words(1); b.ext(GEO)
    b.words(2); b.ptr(weights)
    b.words(0x28); b.ext("__const MATRIX4:::EAGLAnimationBuffer")
    b.words(1); b.ptr(lengths)
    b.words(len(INDICES)); b.ptr(indices)
    for stream, _, _, _ in streams:
        b.words(len(POSITIONS)); b.ptr(data_at[stream])
    b.align()

    b.define("__BBOX:::TEST_alpha")
    b.floats(0.0, 0.0, 0.0, 1.0, 3.0, 0.5)
    b.words(0, 0)
    bones = [("root", -1, 0, (0.0, 1.0, 0.0)), ("L_arm", 0, 2, (1.0, 0.0, 0.0)), ("R_arm", 0, 1, (-1.0, 0.0, 0.0))]
    for i, (name, _, _, _) in enumerate(bones):
        b.define("__Bone:::root." + name)
        b.words(i, 0, 0, 0)
    b.define("__Skeleton:::root")
    b.words(0x1234, 0, len(bones), 0)
    world = {}
    for i, (name, parent, mirror, t) in enumerate(bones):
        w = tuple(t[k] + (world[parent][k] if parent >= 0 else 0.0) for k in range(3))
        world[i] = w
        b.floats(1.0, 1.0, 1.0); b.words(parent)
        b.floats(0.0, 0.0, 0.0, 1.0)
        b.floats(*t); b.words(mirror)
        b.floats(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -w[0], -w[1], -w[2], 1)

    material = b.here()
    b.data += b"skin~LitTest\0"
    b.align(4)
    materials = b.here()
    b.ptr(material)
    lod = b.here()
    b.words(0, 1); b.ptr(instance)
    b.align()
    b.define("__Model:::TEST_alpha")
    model = b.here()
    b.data += bytes(0xD0)
    struct.pack_into("<I", b.data, model + 0x9C, 1)
    b.patch(model + 0xA0, materials); b.relocs.append((model + 0xA0, None))
    struct.pack_into("<I", b.data, model + 0xC8, 1)
    b.patch(model + 0xCC, lod); b.relocs.append((model + 0xCC, None))
    return b.build()


def bgra_shpx(images):
    """An SHPX file of 32-bit BGRA images: [(4-character name, width, height, BGRA bytes)]."""
    out = bytearray(b"SHPX" + bytes(8) + b"G354" + bytes(8 * len(images)))
    for i, (name, w, h, bgra) in enumerate(images):
        while len(out) % 16:
            out.append(0)
        struct.pack_into("<4sI", out, 16 + 8 * i, name.encode("latin-1"), len(out))
        head = bytearray(16)
        head[0] = 0x7D
        struct.pack_into("<HH", head, 4, w, h)
        out += head + bgra
    struct.pack_into("<II", out, 4, len(out), len(images))
    return bytes(out)


FIGHTER_XML = """<?xml version="1.0" encoding="utf-8" ?>
<fighterlist>
\t<fighter>
\t\t<name>ZED</name>
\t\t<model>V2IP_901A</model>
\t\t<dispname>Zed (STF)</dispname>
\t\t<ID>1</ID>
\t</fighter>
\t<fighter>
\t\t<name>ABE</name>
\t\t<model>V2IP_900A</model>
\t\t<dispname>Abe (MAR)</dispname>
\t\t<ID>0</ID>
\t</fighter>
\t<fighter>
\t\t<name>GONE</name>
\t\t<model>V2IP_999A</model>
\t\t<dispname>Gone (KIC)</dispname>
\t\t<ID>2</ID>
\t</fighter>
\t<fighter>
\t\t<name>SLOT</name>
\t\t<model>NULL</model>
\t\t<dispname>Created &</dispname>
\t\t<ID>3</ID>
\t</fighter>
</fighterlist>
"""


def game_folder(root):
    """A tiny game folder: fighter.xml, assets/v2ip.viv (two fighters, one alternate) and one clothing archive.
    Every object is the synthetic ``fighter()``; the textures are 2x2 BGRA images."""
    from ffnystudio import viv
    root.mkdir(parents=True, exist_ok=True)
    (root / "assets").mkdir(exist_ok=True)
    (root / "fighter.xml").write_text(FIGHTER_XML, encoding="latin-1")
    obj = viv.refpack_store(fighter())
    red = bytes((0, 0, 255, 255)) * 4
    blue = bytes((255, 0, 0, 128)) * 4
    (root / "assets" / "v2ip.viv").write_bytes(viv.build_archive([
        ("V2IP_900A.o", obj), ("V2IP_900A.xsh", viv.refpack_store(bgra_shpx([("T001", 2, 2, red)]))),
        ("V2IP_900A_F.o", b"face"), ("V2IP_901A.o", obj), ("V2IP_900B.o", obj)]))
    (root / "assets" / "v2cp_1.viv").write_bytes(viv.build_archive([
        ("createplayer_parts.xml", b"<parts/>"), ("V2CP_110_0001_C.o", obj), ("V2CP_110_0001_A.o", b"morph"),
        ("V2CP_110_0001_A01_01.xsh", bgra_shpx([("0000", 2, 2, blue)])), ("V2CP_110_0002_C1.o", obj)]))
    return root
