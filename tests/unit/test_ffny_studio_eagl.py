"""FFNY Studio EAGL views and glTF export, on a tiny synthetic EAGL object built here (no game bytes)."""
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parents[2] / "tools" / "ffny-studio"))
from ffnystudio import eagl, gltf  # noqa: E402
from ffny_studio_fixtures import INDICES, NORMALS, POSITIONS, UVS, fighter  # noqa: E402


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
