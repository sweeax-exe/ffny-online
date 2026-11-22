"""A roster fighter's piece as a created-fighter item, made at compose time from the player's own files (build 108:
the Eyepatch; setup/ffnycontent/rosteritem.py through content_compose). Synthetic objects, textures and archives
built here; with the user's own dump (game/ or $DEFJAM_DATA/extracted; skipped without it) the Eyepatch itself,
whose files must be the bytes FFNY Studio makes (the same hashes are pinned in Studio's tests)."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'setup'))
import content_compose as cc  # noqa: E402
from ffnycontent import cpparts, rosteritem as ri, sod, viv  # noqa: E402
sys.path.pop(0)
sys.path.insert(0, str(HERE))
from test_content_compose import (  # noqa: E402,F401  (the synthetic game folder and pack helpers)
    NAMES, PARTS, ROWS, STRINGS, build_sod, clothing_floor, compose, data, game, item_op, make_pack, out)
sys.path.pop(0)


# -- synthetic EAGL objects (the layout of FFNY Studio's tests/ffny_studio_fixtures.py) -----------------------------

POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 2.0, 0.0), (1.0, 2.0, 0.5), (0.5, 3.0, 0.0)]
UNIT_UVS = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0), (0.5, 0.5)]
NORMALS = [(0.0, 0.0, 1.0), (0.0, 0.0, -1.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.6, 0.8, 0.0)]
SKIN = [-96, -96, -92, -92, -92]
INDICES = [0, 1, 2, 3, 3, 4]
GEO = ("__EAGL::GeoPrimState:::RUNTIME_ALLOC::UID=1;SetTransparencyMethod=EAGL::TM_ALPHA;"
       "SetPrimitiveType=EAGL::PT_TRIANGLESTRIP;")
PART_TAR = ("__EAGL::TAR:::RUNTIME_ALLOC::UID=7;SHAPENAME=cp_c,1;XBOXEXTOBJ_SetClampU=EAGL::XBOXCM_CLAMP;"
            "XBOXEXTOBJ_SetClampV=EAGL::XBOXCM_CLAMP;")
ROSTER_TAR = "__EAGL::TAR:::RUNTIME_ALLOC::UID=3;SHAPENAME=A130,1;"


class Blob:
    """A .data section with internal pointers (section-symbol relocations) and imports."""

    def __init__(self):
        self.data, self.relocs, self.defs, self.imports = bytearray(), [], [], []

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

    def build(self):
        """An ELF object: NULL, .data, .shstrtab, .strtab, .symtab, .rel.data, the header table last."""
        shstr = b"\0.data\0.shstrtab\0.strtab\0.symtab\0.rel.data\0"
        strtab, syms = bytearray(b"\0"), bytearray(16) + struct.pack("<IIIBBH", 0, 0, 0, 3, 0, 1)
        symbols = [(n, off, 4, 0x11, 1) for n, off in self.defs] + [(n, 0, 0, 0x10, 0) for n in self.imports]
        for name, value, size, info, shndx in symbols:
            syms += struct.pack("<IIIBBH", len(strtab), value, size, info, 0, shndx)
            strtab += name.encode("latin-1") + b"\0"
        index = {n: 2 + len(self.defs) + i for i, n in enumerate(self.imports)}
        rel = b"".join(struct.pack("<II", off, ((1 if key is None else index[key]) << 8) | 2)
                       for off, key in self.relocs)
        bodies = [bytes(self.data), shstr, bytes(strtab), bytes(syms), rel]
        at = {n: shstr.index(n.encode() + b"\0") for n in (".data", ".shstrtab", ".strtab", ".symtab", ".rel.data")}
        heads = [(at[".data"], 1, 3, 0, 0, 16, 0), (at[".shstrtab"], 3, 0, 0, 0, 1, 0), (at[".strtab"], 3, 0, 0, 0, 1, 0),
                 (at[".symtab"], 2, 0, 3, 2, 4, 16), (at[".rel.data"], 9, 0, 4, 1, 4, 8)]
        out = bytearray(52)
        offsets = []
        for (_, _, _, _, _, align, _), body in zip(heads, bodies):
            while len(out) % align:
                out.append(0)
            offsets.append(len(out))
            out += body
        while len(out) % 4:
            out.append(0)
        shoff = len(out)
        out += bytes(40)
        for (name, typ, flags, link, info, align, entsize), off, body in zip(heads, offsets, bodies):
            out += struct.pack("<10I", name, typ, flags, 0, off, len(body), link, info, align, entsize)
        struct.pack_into("<16sHHIIIIIHHHHHH", out, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 1, 8, 1, 0, 0, shoff,
                         0x20924000, 52, 0, 0, 40, 6, 2)
        return bytes(out)


def eagl_object(model, material, uvs, tar):
    """One model, one material, one skinned draw (five vertices, one strip) whose texture is ``tar``."""
    b = Blob()
    b.words(0x10000, 0, 0, 0)
    weights = b.here()
    b.floats(1.0, 0.0, 0.0, 0.0)
    b.floats(0.5, 0.5, 0.0, float(0x01020000))
    b.words(0, 0, 0, 0)
    lengths = b.here()
    b.words(len(INDICES), 0, 0, 0)
    indices = b.here()
    b.data += struct.pack("<%dH" % len(INDICES), *INDICES)
    b.align()
    streams = [(0, 0x32, 12, b"".join(struct.pack("<3f", *p) for p in POSITIONS)),
               (1, 0x22, 8, b"".join(struct.pack("<2f", *uv) for uv in uvs)),
               (2, 0x40, 4, bytes([0x10, 0x20, 0x30, 0xFF]) * 5),
               (3, 0x16, 4, b"".join(struct.pack("<I", ri.encode_normal(n)) for n in NORMALS)),
               (4, 0x15, 2, struct.pack("<5h", *SKIN))]
    data_at = {}
    for stream, _, stride, payload in streams:
        b.define("__VB:::__GPvb_%d_0_%s" % (15 + stream, model))
        desc = b.here()
        b.words(0, len(payload), stride, 1)
        b.ptr(desc + 20)
        data_at[stream] = b.here()
        b.data += payload + b"\xbb" * 4
        struct.pack_into("<I", b.data, desc, b.here())
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
    for stream, _, stride, _ in streams:
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
    b.data += ("%s_0_RM = LitTest\0" % model).encode()
    b.align(4)
    b.define("__RM:::__GP_RM_%s_0" % model)
    rm = b.here()
    b.ptr(descriptor); b.words(1); b.ptr(decl); b.ptr(shaders); b.words(0); b.ptr(shaders); b.words(0)
    b.ext("ParentRM_LitTest"); b.words(0, 0, 0xFFFFFFFF); b.ext("ParentRM_LitTest"); b.ptr(label)
    b.words(0xDDDDDDDD)
    instance = b.here()
    b.ptr(rm)
    b.words(1); b.ext(tar)
    b.words(1); b.ext(GEO)
    b.words(2); b.ptr(weights)
    b.words(0x28); b.ext("__const MATRIX4:::EAGLAnimationBuffer")
    b.words(1); b.ptr(lengths)
    b.words(len(INDICES)); b.ptr(indices)
    for stream, _, _, _ in streams:
        b.words(len(POSITIONS)); b.ptr(data_at[stream])
    b.align()
    b.define("__BBOX:::%s" % model)
    b.floats(0.0, 0.0, 0.0, 1.0, 3.0, 0.5)
    b.words(0, 0)
    name = b.here()
    b.data += material.encode() + b"\0"
    b.align(4)
    materials = b.here()
    b.ptr(name)
    lod = b.here()
    b.words(0, 1); b.ptr(instance)
    b.align()
    b.define("__Model:::%s" % model)
    m = b.here()
    b.data += bytes(0xD0)
    struct.pack_into("<I", b.data, m + 0x9C, 1)
    struct.pack_into("<I", b.data, m + 0xA0, materials); b.relocs.append((m + 0xA0, None))
    struct.pack_into("<I", b.data, m + 0xCC, lod); b.relocs.append((m + 0xCC, None))
    return b.build()


# the template part's UVs sit in the eyewear's 144x32 place at (0, 32) of the 256x256 composite
PART_UVS = [((2 + 138 * u) / 256, (34 + 28 * v) / 256) for u, v in UNIT_UVS]
# the roster draw covers the middle of its 8x8 atlas
ROSTER_UVS = [(0.25 + 0.5 * u, 0.25 + 0.5 * v) for u, v in UNIT_UVS]


# -- synthetic textures and thumbnails ------------------------------------------------------------------------------

def shpx(records):
    """An SHPX file of [(name, record)], pixels on 128-byte boundaries."""
    out = bytearray(b"SHPX" + bytes(8) + b"G355")
    for name, _ in records:
        out += name.encode().ljust(4, b"\0")[:4] + bytes(4)
    out += b"Buy ERTS"
    offsets = []
    for _, record in records:
        out += bytes((-(len(out) + 16)) % 128)
        offsets.append(len(out))
        out += record
    struct.pack_into("<II", out, 4, len(out), len(records))
    for i, off in enumerate(offsets):
        struct.pack_into("<I", out, 20 + 8 * i, off)
    return bytes(out)


def p8_record(name, w, h, indices, palette, swizzled=False):
    """A P8 record: header, pixels, the gap to 64 bytes, a 256-colour palette attachment and a name block."""
    gap = (-(w * h + 16)) % 64
    head = bytearray(16)
    head[0] = 0x7B
    head[1:4] = (16 + w * h + gap).to_bytes(3, "little")
    struct.pack_into("<HHI", head, 4, w, h, 0x2000 if swizzled else 0)
    pixels = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            at = morton(x, y) if swizzled else y * w + x
            pixels[at] = indices[y * w + x]
    pal = bytearray(16)
    pal[0] = 0x2A
    pal[1:4] = (16 + 1024).to_bytes(3, "little")
    struct.pack_into("<HHH", pal, 4, 256, 1, 256)
    for r, g, b, a in palette:
        pal += bytes((b, g, r, a))
    pal += bytes(16 + 1024 - len(pal))
    name_block = bytes([0x70, 0, 0, 0]) + name.encode()[:4].ljust(4, b"\0") + bytes(8)
    return bytes(head) + bytes(pixels) + bytes(gap) + bytes(pal) + name_block


def morton(x, y):
    out = 0
    for bit in range(8):
        out |= ((x >> bit) & 1) << (2 * bit) | ((y >> bit) & 1) << (2 * bit + 1)
    return out


ATLAS_PALETTE = [(200, 20, 20, 255), (20, 200, 20, 255), (240, 240, 250, 128), (0, 0, 0, 0)]
ATLAS = shpx([("A130", p8_record("A130", 8, 8, [(x // 2 + y // 4) % 4 for y in range(8) for x in range(8)],
                                 ATLAS_PALETTE, swizzled=True))])
TEMPLATE_TEX = shpx([("0000", p8_record("0000", 144, 32, bytes(144 * 32), [(90, 90, 90, 255)]))])


def thumb_big(base, w, h):
    """A shop thumbnail: the Apt movie's four files, the picture a plain DXT5 image."""
    record = bytes([0x62, 0, 0, 0]) + struct.pack("<HHII", w, h, 0, 0) + b"\x11" * (w * h)
    return viv.build_archive([(base + ".apt", b"apt movie " + base.encode()), (base + ".const", bytes(32)),
                              (base + ".o", b"\x7fELF" + bytes(60)), (base + ".xsh", shpx([("0000", record)]))],
                             align=128)


SPEC = {"model": "V2IP_130A", "material": "emc3_jewelry-eye_w", "bone": 58}
EYEWEAR = ("Eyewear_Test Frames", 1, "A01", [("#11110002", 1)], 15)


def eyewear_game(game):
    """The synthetic game with an eyewear row (type 15, part 144_0001), its part, texture and thumbnails, and a
    roster model V2IP_130A whose first material is the eyepatch."""
    strings = STRINGS + ["Eyewear_Test Frames"]
    (game / "assets" / "djv2.sod").write_bytes(build_sod(ROWS + [EYEWEAR], strings))
    eyew = PARTS.replace('"Ubody"', '"Eyew"').replace("V2CP_114_0005", "V2CP_144_0001")
    parts = PARTS.replace("</partslist>\r\n", "") + eyew.split("<partslist>\r\n", 1)[1]
    (game / "assets" / "v2cp_0.viv").write_bytes(viv.build_archive(
        [("createplayer_parts.xml", viv.refpack_store(parts.encode("latin-1")))], align=128))
    part = eagl_object("V2CP_144_0001_C_alpha", "emc3_jewelry~DJIrradTextureEnvmap_Skin3", PART_UVS, PART_TAR)
    (game / "assets" / "v2cp_3.viv").write_bytes(viv.build_archive(
        [("V2CP_144_0001_C.o", viv.refpack_store(part)),
         ("V2CP_144_0001_A01_01.xsh", viv.refpack_store(TEMPLATE_TEX))], align=128))
    roster = eagl_object("V2IP_130A_alpha", "emc3_jewelry-eye_w~DJIrradTextureEnvmap_Skin3", ROSTER_UVS, ROSTER_TAR)
    (game / "assets" / "v2ip.viv").write_bytes(viv.build_archive(
        [("V2IP_130A.o", viv.refpack_store(roster)), ("V2IP_130A.xsh", ATLAS)], align=128))
    (game / "screens").mkdir(exist_ok=True)
    (game / "screens" / "screens.viv").write_bytes(viv.build_archive(
        [("screens/shopThumbs/100/V2TN_144_0001_A01_01.big", viv.refpack_store(
            thumb_big("V2TN_144_0001_A01_01", 16, 8))),
         ("screens/shopThumbs/100/V2TN_144_0001_A01_01_t.big", thumb_big("V2TN_144_0001_A01_01_t", 8, 4))],
        align=128))
    return game


def roster_op(game, **extra):
    op = item_op("Eyepatch", "Y01", clothing_floor(game))
    op.update({"template": "Eyewear_Test Frames", "section": "Eyew", "bundle": "", "price": 0, "variants": [],
               "model": {"num": 10, "name": "V2CP_144_0010_C"},
               "colours": [{"number": 1, "label": "Diamond", "texture": "V2CP_144_0010_Y01_01"}],
               "roster_part": dict(SPEC)})
    op.update(extra)
    return op


BUILT = ["V2CP_144_0010_C.o", "V2CP_144_0010_Y01_01.xsh", "screens/shopThumbs/100/V2TN_144_0010_Y01_01.big",
         "screens/shopThumbs/100/V2TN_144_0010_Y01_01_t.big"]


# -- tests ------------------------------------------------------------------------------------------------------------

def test_a_roster_piece_is_made_from_the_players_files(game, data):
    eyewear_game(game)
    pack = make_pack(data, "eyepatch", "clothing", [], [roster_op(game)])
    assert json.loads((pack["folder"] / "install.json").read_text())["entries"] == []
    result = compose(data, game, [pack])
    assert result["applied"] == ["eyepatch"], result
    assert set(result["files"]) == {"assets/djv2.sod", "assets/v2cp_0.viv", "assets/v2cp_3.viv", "assets/misc.viv",
                                    "screens/screens.viv"}
    db = sod.Sod(out(data, "assets/djv2.sod").read_bytes())
    new, = [r for r in db.clothing() if r.name.startswith("FFNY_")]
    assert new.num == 10 and new.type == 15 and new.texture_name() == "V2CP_144_0010_Y01_01"
    parts = viv.Archive(out(data, "assets/v2cp_0.viv")).read("createplayer_parts.xml").decode("latin-1")
    assert cpparts.check(parts) == []
    assert cpparts.models(parts, "Eyew")[-1] == ("V2CP_144_0010_C", [("V2CP_144_0010_Y01_01", [])])
    arch = viv.Archive(out(data, "assets/v2cp_3.viv"))
    # the part: the roster draw as the template's only draw, every vertex on the head bone, the template's texture
    # reference kept, its name the new part's
    blob = arch.read("V2CP_144_0010_C.o")
    obj = ri._Eagl(blob)
    model, = obj.models()
    assert model.name == "V2CP_144_0010_C_alpha" and len(model.groups) == 1 and len(model.groups[0]) == 1
    assert b"V2CP_144_0001_C" not in blob
    inst = model.groups[0][0]
    mesh = obj.mesh(inst)
    assert mesh.positions == POSITIONS and mesh.indices == INDICES
    weights = inst.first(ri.P_WEIGHTS)
    assert weights.count == 1 and struct.unpack_from("<4f", obj.data, weights.target[0]) == (1.0, 0.0, 0.0,
                                                                                              float(58 << 24))
    tars = [p.target[1] for p in inst.params if p.type == ri.P_TEXTURE]
    assert tars == [PART_TAR] and [n for n in obj.imports() if "SHAPENAME" in n] == [PART_TAR]
    # the UVs: the cut's (a 2-pixel margin around the draw's 4x4 texels of the 8x8 atlas: all of it) moved into the
    # eyewear place of the composite
    for (u, v), (ru, rv) in zip(mesh.uvs, ROSTER_UVS):
        assert abs(u - 144 * ru / 256) < 1e-6 and abs(v - (32 + 32 * rv) / 256) < 1e-6
    # the texture: the template's layout, the atlas window scaled to 144x32
    xsh = arch.read("V2CP_144_0010_Y01_01.xsh")
    (name, off), = ri.directory(xsh)
    assert name == "0000" and xsh[off] == 0x7B and struct.unpack_from("<HH", xsh, off + 4) == (144, 32)
    assert xsh[12:16] == b"G355" and xsh != TEMPLATE_TEX
    # the thumbnails: entries renamed, the picture painted, the rest byte for byte
    screens = viv.Archive(out(data, "screens/screens.viv"))
    for entry, base in ((BUILT[2], "V2TN_144_0010_Y01_01"), (BUILT[3], "V2TN_144_0010_Y01_01_t")):
        inner = viv.Archive(screens.read(entry))
        assert inner.names() == [base + s for s in (".apt", ".const", ".o", ".xsh")]
        assert inner.read(base + ".apt").startswith(b"apt movie V2TN_144_0001")
        assert b"\x11" * 16 not in inner.read(base + ".xsh")


def test_two_pcs_compose_the_same_bytes(game, data, tmp_path):
    eyewear_game(game)
    other = tmp_path / "data2"
    (other / "mods").mkdir(parents=True)
    results = []
    for d in (data, other):
        result = compose(d, game, [make_pack(d, "eyepatch", "clothing", [], [roster_op(game)])])
        assert result["applied"] == ["eyepatch"]
        results.append({rel: out(d, rel).read_bytes() for rel in result["files"]})
    assert results[0] == results[1]


def test_the_override_copy_is_the_base(game, data):
    """The roster model is read like a delta's base: <data>/override first."""
    eyewear_game(game)
    (data / "override" / "assets").mkdir(parents=True)
    moved = [(x, y + 0.25) for x, y in ROSTER_UVS]
    roster = eagl_object("V2IP_130A_alpha", "emc3_jewelry-eye_w~X", moved, ROSTER_TAR)
    (data / "override" / "assets" / "v2ip.viv").write_bytes(viv.build_archive(
        [("V2IP_130A.o", roster), ("V2IP_130A.xsh", ATLAS)], align=128))
    assert compose(data, game, [make_pack(data, "eyepatch", "clothing", [], [roster_op(game)])])["applied"]
    obj = ri._Eagl(viv.Archive(out(data, "assets/v2cp_3.viv")).read("V2CP_144_0010_C.o"))
    uvs = obj.mesh(obj.models()[0].groups[0][0]).uvs
    assert max(v for _, v in uvs) * 256 > 63      # the cut's window moved: the UVs are the override model's


def test_a_pack_that_does_not_fit_is_skipped(game, data):
    eyewear_game(game)
    packs = [make_pack(data, "no-material", "clothing", [], [roster_op(game, roster_part=dict(SPEC, material="x"))]),
             make_pack(data, "no-model", "clothing", [], [roster_op(game, roster_part=dict(SPEC, model="V2IP_131A"))],
                       order=2)]
    result = compose(data, game, packs)
    assert result["applied"] == []
    why = dict(result["skipped"])
    assert "no material x" in why["no-material"] and "V2IP_131A.o is not in your game" in why["no-model"]


@pytest.mark.parametrize("change, message", [
    (lambda op: op["roster_part"].update(extra=1), "roster_part needs exactly"),
    (lambda op: op["roster_part"].pop("bone"), "roster_part needs exactly"),
    (lambda op: op["roster_part"].update(bone=256), "bone must be"),
    (lambda op: op["roster_part"].update(bone=True), "bone must be"),
    (lambda op: op["roster_part"].update(model="V2IP_7AAA"), "not a roster model"),
    (lambda op: op["roster_part"].update(material="a b"), "not a material name"),
    (lambda op: op.pop("model"), "needs the item's model"),
    (lambda op: op["colours"].append(dict(op["colours"][0], number=2)), "exactly one colour"),
    (lambda op: op["colours"][0].update(texture="V2CP_144_0011_Y01_01"), "is not the part"),
    (lambda op: op.update(variants=[{"model": "V2CP_144_0010_C", "textures": ["V2CP_144_0010_Y01_01"]}]),
     "no variants"),
    (lambda op: op.update(colour_of="x"), "unknown field colour_of"),
])
def test_install_json_refuses_a_wrong_roster_op(game, change, message):
    op = roster_op(game)
    change(op)
    with pytest.raises(cc.ComposeError, match=message):
        cc.check_install({"schema": 1, "type": "clothing", "entries": [], "ops": [op]})


@pytest.mark.parametrize("entry", BUILT)
def test_a_pack_must_not_carry_what_the_op_makes(game, entry):
    archive = "screens/screens.viv" if entry.startswith("screens/") else "assets/v2cp_3.viv"
    row = {"archive": archive, "entry": entry.lower(), "mode": "add", "base_archive": archive,
           "base_entry": "V2CP_144_0001_A01_01.xsh", "delta": "deltas/x.ffdelta", "original_sha256": "0" * 64,
           "result_sha256": "0" * 64, "result_size": 1}
    with pytest.raises(cc.ComposeError, match="made from your game files"):
        cc.check_install({"schema": 1, "type": "clothing", "entries": [row], "ops": [roster_op(game)]})


def test_validate_install_takes_a_roster_op_with_items_v1(tmp_path, game):
    sys.path.insert(0, str(HERE))
    import test_mod_manager  # noqa: E402  (the loaded mods module)
    sys.path.pop(0)
    folder = tmp_path / "pack"
    folder.mkdir()
    (folder / "install.json").write_text(json.dumps({"schema": 1, "type": "clothing", "entries": [],
                                                     "ops": [roster_op(game)]}), encoding="utf-8")
    assert test_mod_manager.mods.validate_install(folder)["capabilities"] == ["content-v1", "items-v1"]
    bad = roster_op(game)
    bad["roster_part"]["shape"] = "A130"
    (folder / "install.json").write_text(json.dumps({"schema": 1, "type": "clothing", "entries": [],
                                                     "ops": [bad]}), encoding="utf-8")
    with pytest.raises(test_mod_manager.mods.ModError, match="roster_part"):
        test_mod_manager.mods.validate_install(folder)


def test_quantise_is_exact_up_to_256_colours_and_a_median_cut_above():
    few = b"".join(bytes((i, 255 - i, 7, 255)) for i in range(200)) * 2
    palette, indices = ri.quantise(few)
    assert len(palette) == 200 and b"".join(bytes(palette[k]) for k in indices) == few
    many = b"".join(bytes((i % 256, i // 256 * 40, 9, 255)) for i in range(1000))
    palette, indices = ri.quantise(many)
    assert len(palette) == 256 and ri.quantise(many) == (palette, indices)
    worst = max(abs(a - b) for k, i in enumerate(indices) for a, b in zip(palette[i], many[4 * k:4 * k + 4]))
    assert worst <= 40


def test_resize_averages_the_colour_by_its_alpha():
    # a clear red pixel next to an opaque green one: the clear one's colour does not bleed into the average
    rgba = bytes((255, 0, 0, 0, 0, 255, 0, 255))
    assert ri.resize(2, 1, rgba, 1, 1) == bytes((0, 255, 0, 128))
    assert ri.resize(2, 1, rgba, 2, 1) == rgba


# -- the Eyepatch from the user's own dump --------------------------------------------------------------------------

def _dump():
    for c in (ROOT / "game", Path(os.environ.get("DEFJAM_DATA", "/nonexistent")) / "extracted"):
        if (c / "assets" / "v2ip.viv").is_file() and (c / "screens" / "screens.viv").is_file():
            return c
    return None


DUMP = _dump()
# sha256 of what FFNY Studio's roster-item composes for the Eyepatch (tests/test_ffny_studio_rosteritem.py pins the
# same): the part is also the one the delta-built pack shipped, checked in the game on build 108's branch
EYEPATCH = {
    "V2CP_144_0010_C.o": "b34f357934b65fb5cee396c2d1651fa93a5bafb2774d12c361698767f3938f16",
    "V2CP_144_0010_Y01_01.xsh": "a884569a2dd92eacec063808b7d9bc258c87fb40a3207c253cc8dfad7955a420",
    "screens/shopThumbs/100/V2TN_144_0010_Y01_01.big":
        "b9c73d35cd6aecf420af6ea5d7880d4decc47a6c11f966def339135d92f45157",
    "screens/shopThumbs/100/V2TN_144_0010_Y01_01_t.big":
        "a55f5a02b6e405f4f6830a363604b90f6e598864b8df53dab94505cb904d5d84",
}


@pytest.mark.skipif(DUMP is None, reason="no game data (game/ or DEFJAM_DATA)")
def test_the_eyepatch_from_the_dump_is_studios_bytes():
    archives = {}

    def read(archive, entry):
        if archive not in archives:
            archives[archive] = viv.Archive(DUMP / archive)
        return archives[archive].read(entry)

    db = sod.Sod((DUMP / "assets" / "djv2.sod").read_bytes())
    row = db.find_clothing("Eyewear_Box Frames Tinted")
    assert (row.model_name(), row.category, row.num, row.group) == ("V2CP_144_0001_C", 144, 1, "A01")
    op = {"op": "item", "template": row.name, "name": "Eyepatch", "price": 0, "bundle": "", "group": "Y01",
          "colours": [{"number": 1, "label": "Diamond", "texture": "V2CP_144_0010_Y01_01"}], "variants": [],
          "section": "Eyew", "model": {"num": 10, "name": "V2CP_144_0010_C"}, "row_name": "FFNY_Eyepatch_cojwjcl",
          "roster_part": dict(SPEC)}
    report = []
    files = ri.item_files(read, op, row.model_name(), "V2CP_144_0001_A01_01", report)
    assert [e for _, e, _ in files] == list(EYEPATCH)
    assert {e: hashlib.sha256(d).hexdigest() for _, e, d in files} == EYEPATCH
    assert "134 vertices" in report[0] and "191x63 cut from the 512x512 atlas" in report[0]
