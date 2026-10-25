"""FFNY Studio EAGL views and glTF export, on a tiny synthetic EAGL object built here (no game bytes)."""
import json
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[2] / "tools" / "ffny-studio"))
from ffnystudio import eagl, elf, gltf  # noqa: E402

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


@pytest.fixture(scope="module")
def obj():
    return eagl.EaglObject(fighter(), "TEST")


def test_round_trip_is_byte_exact(obj):
    assert obj.to_bytes() == obj.original


def test_skeleton(obj):
    sk = obj.skeleton()
    assert [b.name for b in sk.bones] == ["root", "L_arm", "R_arm"]
    assert [b.parent for b in sk.bones] == [-1, 0, 0]
    assert [b.mirror for b in sk.bones] == [0, 2, 1]
    pos = eagl.bind_positions(sk)
    assert pos[1] == pytest.approx((1.0, 1.0, 0.0)) and pos[2] == pytest.approx((-1.0, 1.0, 0.0))
    for w, bone in zip(eagl.world_matrices(sk), sk.bones):
        assert w[3][:3] == pytest.approx([-v for v in bone.inverse_bind[12:15]])


def test_vertex_buffers(obj):
    vbs = obj.vertex_buffers()
    assert len(vbs) == 5 and all(obj.check_vertex_buffer(v) for v in vbs)
    assert sorted(v.count for v in vbs) == [5] * 5


def test_model_and_mesh(obj):
    model = obj.models()[0]
    assert model.name == "TEST_alpha" and model.materials == ["skin~LitTest"]
    assert model.textures == ["T001"] and model.bbox == ((0.0, 0.0, 0.0), (1.0, 3.0, 0.5))
    inst = model.groups[0][0]
    assert inst.rm.name == "__GP_RM_TEST_0" and inst.rm.parent == "ParentRM_LitTest"
    assert inst.rm.label == "TEST_0_RM = LitTest"
    assert [p.name for p in inst.params][:3] == ["texture", "geo_prim_state", "weights"]
    (mesh,) = obj.meshes()
    assert mesh.positions == POSITIONS and mesh.uvs == UVS
    assert mesh.colors[0] == pytest.approx((0x30 / 255, 0x20 / 255, 0x10 / 255, 1.0))
    for got, want in zip(mesh.normals, NORMALS):
        assert got == pytest.approx(want, abs=2e-3)
    assert mesh.indices == INDICES and mesh.prim_lengths == [6]
    assert mesh.texture == "T001" and mesh.material == "skin~LitTest" and mesh.blended
    assert mesh.triangles() == [(0, 2, 1), (1, 2, 3)]
    assert mesh.skin_slots == [-96, -92, -36, -32]
    joints, weights = mesh.joints_weights()
    assert joints[0] == (0, 0, 0, 0) and weights[0] == (1.0, 0.0, 0.0, 0.0)
    assert joints[2] == (1, 2, 0, 0) and weights[2] == (0.5, 0.5, 0.0, 0.0)


def test_strip_helpers():
    assert eagl.strip_triangles([0, 1, 2, 3, 4], [3, 2]) == [(0, 2, 1)]
    assert eagl.strip_triangles([5, 6, 7, 8]) == [(5, 7, 6), (6, 7, 8)]
    assert eagl.unpack_bones(float(0x0C0D5800)) == (12, 13, 88)
    for n in [(1.0, 0.0, 0.0), (0.0, -1.0, 0.0), (0.0, 0.0, -1.0)]:
        assert eagl.decode_normal_cmp(eagl.encode_normal_cmp(n)) == pytest.approx(n, abs=2e-3)


def test_gltf_export_is_valid(obj, tmp_path):
    stats = gltf.write(obj, str(tmp_path), "TEST")
    assert gltf.validate(str(tmp_path / "TEST.gltf")) == []
    assert stats["vertices"] == 5 and stats["triangles"] == 2 and stats["skinned_vertices"] == 5
    doc = json.loads((tmp_path / "TEST.gltf").read_text())
    assert [n["name"] for n in doc["nodes"][:3]] == ["root", "L_arm", "R_arm"]
    assert doc["nodes"][0]["children"] == [1, 2]
    assert doc["nodes"][1]["translation"] == pytest.approx([1.0, 0.0, 0.0])
    prim = doc["meshes"][0]["primitives"][0]
    assert set(prim["attributes"]) == {"POSITION", "NORMAL", "TEXCOORD_0", "COLOR_0", "JOINTS_0", "WEIGHTS_0"}
    assert doc["skins"][0]["joints"] == [0, 1, 2]
    assert doc["materials"][0]["alphaMode"] == "BLEND"


def test_validate_reports_broken_references(obj, tmp_path):
    gltf.write(obj, str(tmp_path), "TEST")
    path = tmp_path / "TEST.gltf"
    doc = json.loads(path.read_text())
    doc["nodes"][0]["children"].append(99)
    doc["buffers"][0]["byteLength"] += 1000
    path.write_text(json.dumps(doc))
    problems = gltf.validate(str(path))
    assert any("child 99" in p for p in problems) and any("buffer 0" in p for p in problems)


def test_cli_on_a_synthetic_archive(tmp_path, capsys):
    from ffnystudio import __main__ as cli, viv
    archive = tmp_path / "v2ip.viv"
    archive.write_bytes(viv.build_archive([("TEST.o", viv.refpack_store(fighter()))]))
    assert cli.main(["list", str(archive)]) == 0
    assert "RefPack" in capsys.readouterr().out
    assert cli.main(["info", str(archive), "TEST.o"]) == 0
    out = capsys.readouterr().out
    assert "byte-exact re-emit: yes" in out and "3 bones" in out and "1 draws" in out
    assert cli.main(["export-fighter", "TEST", str(tmp_path / "out"), "--archive", str(archive),
                     "--no-texture"]) == 0
    out = capsys.readouterr().out
    assert "5 vertices (5 skinned), 2 triangles" in out and "validation: ok" in out
