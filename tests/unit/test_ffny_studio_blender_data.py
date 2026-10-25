"""FFNY Studio's bpy-free importer layer: the game folder catalogue and the plain data the add-on turns into
Blender objects. Synthetic game folder and EAGL object only (tests/unit/ffny_studio_fixtures.py)."""
import math
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parents[2] / "tools" / "ffny-studio"))
from ffnystudio import blender_data, eagl, game  # noqa: E402
from ffny_studio_fixtures import FIGHTER_XML, fighter, game_folder  # noqa: E402


@pytest.fixture(scope="module")
def obj():
    return eagl.EaglObject(fighter(), "TEST")


@pytest.fixture()
def folder(tmp_path):
    return game_folder(tmp_path / "dump")


# -- catalogue -----------------------------------------------------------------------------------------------

def test_fighter_xml_and_list():
    records = game.parse_fighter_xml(FIGHTER_XML)
    assert [r["model"] for r in records] == ["V2IP_901A", "V2IP_900A", "V2IP_999A", "NULL"]
    assert records[3]["dispname"] == "Created &"
    models = game.fighter_models(["V2IP_900A.o", "V2IP_900A_F.o", "V2IP_900A.xsh", "v2ip_901a.o", "V2IP_900B.o"])
    assert models == ["V2IP_900A", "v2ip_901a", "V2IP_900B"]
    fighters = game.fighter_list(records, models)
    assert [(f.model, f.display, f.id, f.listed) for f in fighters] == [
        ("V2IP_900A", "Abe (MAR)", 0, True), ("v2ip_901a", "Zed (STF)", 1, True),
        ("V2IP_900B", "V2IP_900B", -1, False)]
    assert fighters[0].label == "Abe (MAR) - V2IP_900A" and fighters[2].label == "V2IP_900B"


def test_part_names_and_textures():
    names = ["V2CP_110_0001_C.o", "V2CP_110_0001_C1.o", "V2CP_110_0001_A.o", "V2CP_532_0001_C_F.o",
             "V2CP_110_0001_A01_02.xsh", "V2CP_110_0001_A01_01.xsh", "V2CP_A_A.xsh", "V2CP_Template_Long.o"]
    assert game.part_names(names) == ["V2CP_110_0001_C", "V2CP_110_0001_C1"]
    assert game.part_texture("V2CP_110_0001_C", names) == "V2CP_110_0001_A01_01.xsh"
    assert game.part_texture("V2CP_110_0001_C1", names) == "V2CP_110_0001_A01_01.xsh"
    assert game.part_texture("V2CP_110_0002_C", names) == ""


def test_resolve_game_dir(folder):
    assert game.resolve_game_dir(folder) == folder
    assert game.resolve_game_dir(folder / "assets") == folder
    with pytest.raises(ValueError, match="game folder"):
        game.resolve_game_dir("")
    with pytest.raises(ValueError, match="fighter.xml"):
        game.resolve_game_dir(folder.parent)
    extracted = folder.parent / "data"
    (extracted).mkdir()
    folder.rename(extracted / "extracted")
    assert game.resolve_game_dir(extracted) == extracted / "extracted"


def test_game_loads_fighters_and_parts(folder):
    g = game.Game(str(folder))
    assert [f.model for f in g.fighters()] == ["V2IP_900A", "V2IP_901A", "V2IP_900B"]
    asset = g.load_fighter("V2IP_900A")
    assert asset.texture_file == "V2IP_900A.xsh" and list(asset.images) == ["T001"]
    assert asset.images["T001"].pixel(0, 0) == (255, 0, 0, 255) and asset.fallback_texture == ""
    assert g.load_fighter("V2IP_901A").images == {}
    assert [(p.name, p.archive) for p in g.parts()] == [("V2CP_110_0001_C", "assets/v2cp_1.viv"),
                                                        ("V2CP_110_0002_C1", "assets/v2cp_1.viv")]
    part = g.load_part("v2cp_110_0001_c.o")
    assert part.texture_file == "V2CP_110_0001_A01_01.xsh" and part.fallback_texture == "0000"
    assert part.images["0000"].pixel(1, 1) == (0, 0, 255, 128)
    with pytest.raises(KeyError):
        g.load_part("V2CP_999_0001_C")


# -- plain data ----------------------------------------------------------------------------------------------

def test_axis_conversion_is_a_proper_rotation():
    x, y, z = (blender_data.to_blender(v) for v in [(1, 0, 0), (0, 1, 0), (0, 0, 1)])
    assert (x, y, z) == ((1, 0, 0), (0, 0, 1), (0, -1, 0))      # game up -> Blender up, game front -> -Y
    det = (x[0] * (y[1] * z[2] - y[2] * z[1]) - x[1] * (y[0] * z[2] - y[2] * z[0])
           + x[2] * (y[0] * z[1] - y[1] * z[0]))
    assert det == 1


def test_build_armature_data(obj):
    bones = blender_data.build_armature_data(obj.skeleton())
    assert [(b["name"], b["parent"]) for b in bones] == [("root", None), ("L_arm", "root"), ("R_arm", "root")]
    root, left, right = bones
    assert root["head"] == pytest.approx((0, 0, 1)) and root["tail"] == pytest.approx((1, 0, 1))
    assert left["head"] == pytest.approx((1, 0, 1)) and left["tail"] == pytest.approx((1.5, 0, 1))
    assert right["head"] == pytest.approx((-1, 0, 1)) and right["tail"] == pytest.approx((-1.5, 0, 1))
    assert root["align_z"] == pytest.approx((0, 0, 1))
    assert left["props"]["ffny_mirror"] == "R_arm" and right["props"]["ffny_index"] == 2
    assert left["props"]["ffny_inverse_bind"][12:15] == pytest.approx([-1, -1, 0])
    for b in bones:
        assert math.dist(b["head"], b["tail"]) >= blender_data.MIN_BONE


def test_bone_tails_never_collapse():
    """A child sitting on its parent's head (EAGL has several) must not give a zero-length bone."""
    ident = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)
    bones = [eagl.Bone(0, "root", -1, 0, (1, 1, 1), (0, 0, 0, 1), (0, 0, 0), ident + (0, 0, 0, 1)),
             eagl.Bone(1, "same", 0, 1, (1, 1, 1), (0, 0, 0, 1), (0, 0, 0), ident + (0, 0, 0, 1)),
             eagl.Bone(2, "tip", 1, 2, (1, 1, 1), (0, 0, 0, 1), (0, 0, 0), ident + (0, -2, 0, 1))]
    data = blender_data.build_armature_data(eagl.Skeleton("root", 0, 0, bones))
    for b in data:
        assert math.dist(b["head"], b["tail"]) >= blender_data.MIN_BONE
    assert data[1]["tail"] == pytest.approx((0, 0, 2))           # "same" points at "tip"


def test_build_mesh_data(obj):
    (mesh,) = obj.meshes()
    data = blender_data.build_mesh_data(mesh, obj.skeleton())
    assert data["positions"][3] == pytest.approx((1.0, -0.5, 2.0))
    assert data["faces"] == [(0, 2, 1), (1, 2, 3)]
    assert data["uvs"][0] == (0.0, 1.0) and data["uvs"][3] == (1.0, 0.0)
    assert data["normals"][0] == pytest.approx((0, -1, 0), abs=2e-3)
    assert data["colors"][0] == pytest.approx((0x30 / 255, 0x20 / 255, 0x10 / 255, 1.0))
    assert data["groups"] == {"root": [(0, 1.0), (1, 1.0)], "L_arm": [(2, 0.5), (3, 0.5), (4, 0.5)],
                              "R_arm": [(2, 0.5), (3, 0.5), (4, 0.5)]}
    assert data["material"] == ("skin~LitTest", "T001", True)
    assert blender_data.vertex_groups(mesh, None) == {}


def test_faces_drop_degenerate_and_repeated_triangles(obj):
    (mesh,) = obj.meshes()
    mesh.indices = [0, 1, 2, 2, 1, 0, 0, 1, 2]
    mesh.prim_lengths = None
    assert blender_data.faces_of(mesh) == [(0, 2, 1)]


def test_build_import(obj):
    from ffnystudio import texture
    image = texture.Image("T001", 1, 1, b"\xff\x00\x00\xff")
    plan = blender_data.build_import(obj, "V2IP_900A", {"T001": image})
    assert plan.stats == {"bones": 3, "meshes": 1, "vertices": 5, "triangles": 2, "skinned": 1, "textures": 1}
    assert plan.meshes[0]["object_name"] == "V2IP_900A.00 skin T001 blend"
    assert plan.materials == [{"key": ("skin~LitTest", "T001", True), "name": "V2IP_900A skin T001 blend",
                               "texture": "T001", "image": "T001", "blended": True}]
    assert plan.images == {"T001": image}
    untextured = blender_data.build_import(obj, "X", {})
    assert untextured.materials[0]["image"] == "" and untextured.images == {}
    placeholder = blender_data.build_import(obj, "P", {"0000": image}, fallback_texture="0000")
    assert placeholder.materials[0]["image"] == "0000"
    assert blender_data.image_name("V2IP_900A", "T001") == "V2IP_900A_T001"
