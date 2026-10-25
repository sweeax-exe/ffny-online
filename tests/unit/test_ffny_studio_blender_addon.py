"""The FFNY Studio Blender add-on against a stub ``bpy`` (Blender is not needed, and not exercised).

Catches syntax and structure errors (imports, register/unregister, the operator glue, the zip layout) and runs
the thin bpy layer against MagicMocks so a typo in it fails here. Whether the Blender API calls are right is
only shown by running the add-on in Blender itself.
"""
import importlib
import re
import sys
import types
import zipfile
from pathlib import Path
from unittest import mock

import pytest

ROOT = Path(__file__).parents[2]
STUDIO = ROOT / "tools" / "ffny-studio"
sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(STUDIO))
from ffny_studio_fixtures import game_folder  # noqa: E402


class _Menu:
    def __init__(self):
        self.funcs = []

    def append(self, func):
        self.funcs.append(func)

    def remove(self, func):
        self.funcs.remove(func)


def fake_bpy():
    bpy = types.ModuleType("bpy")
    bpy.types = types.SimpleNamespace(AddonPreferences=type("AddonPreferences", (), {}),
                                      Operator=type("Operator", (), {}), Panel=type("Panel", (), {}),
                                      TOPBAR_MT_file_import=_Menu())
    props = types.ModuleType("bpy.props")
    for name in ("StringProperty", "EnumProperty", "FloatProperty", "BoolProperty", "IntProperty"):
        setattr(props, name, (lambda n: lambda **kw: (n, kw))(name))
    bpy.props = props
    registered = []
    bpy.utils = types.SimpleNamespace(register_class=registered.append, unregister_class=registered.remove,
                                      registered=registered)
    bpy.path = types.SimpleNamespace(abspath=lambda p: p)
    bpy.data = mock.MagicMock(name="bpy.data")
    bpy.ops = mock.MagicMock(name="bpy.ops")
    return bpy


@pytest.fixture()
def addon(monkeypatch):
    bpy = fake_bpy()
    monkeypatch.setitem(sys.modules, "bpy", bpy)
    monkeypatch.setitem(sys.modules, "bpy.props", bpy.props)
    monkeypatch.syspath_prepend(str(STUDIO / "blender"))
    for name in [n for n in sys.modules if n == "ffny_studio" or n.startswith("ffny_studio.")]:
        monkeypatch.delitem(sys.modules, name)
    module = importlib.import_module("ffny_studio")
    yield module, bpy
    for name in [n for n in sys.modules if n == "ffny_studio" or n.startswith("ffny_studio.")]:
        del sys.modules[name]


def context_for(module, folder):
    prefs = types.SimpleNamespace(game_folder=str(folder))
    addons = {module.preferences.ADDON: types.SimpleNamespace(preferences=prefs)}
    context = mock.MagicMock(name="context")
    context.preferences.addons = addons
    context.mode = "OBJECT"
    context.view_layer.objects.__iter__.side_effect = lambda: iter([])
    return context


def test_register_and_unregister(addon):
    module, bpy = addon
    assert module.bl_info["blender"] >= (4, 2, 0) and module.bl_info["category"] == "Import-Export"
    module.register()
    names = [c.__name__ for c in bpy.utils.registered]
    assert names == ["FFNYStudioPreferences", "IMPORT_SCENE_OT_ffny_fighter", "FFNY_OT_import_part",
                     "FFNY_OT_refresh", "VIEW3D_PT_ffny_studio"]
    assert bpy.types.TOPBAR_MT_file_import.funcs == [module.operators.menu_import]
    assert module.operators.IMPORT_SCENE_OT_ffny_fighter.bl_idname == "import_scene.ffny_fighter"
    assert module.preferences.FFNYStudioPreferences.bl_idname == "ffny_studio"
    layout = mock.MagicMock()
    module.operators.menu_import(types.SimpleNamespace(layout=layout), None)
    layout.operator.assert_called_once_with("import_scene.ffny_fighter", text="FFNY fighter (from your game)")
    module.unregister()
    assert bpy.utils.registered == [] and bpy.types.TOPBAR_MT_file_import.funcs == []


def test_manifest_matches_bl_info(addon):
    module, _ = addon
    text = (STUDIO / "blender" / "ffny_studio" / "blender_manifest.toml").read_text(encoding="utf-8")
    version = re.search(r'^version = "([^"]+)"', text, re.MULTILINE).group(1)
    assert version == ".".join(map(str, module.bl_info["version"]))
    assert 'blender_version_min = "4.2.0"' in text and 'type = "add-on"' in text
    try:
        import tomllib
    except ImportError:
        return
    manifest = tomllib.loads(text)
    assert manifest["id"] == "ffny_studio" and len(manifest["tagline"]) <= 64
    assert not manifest["tagline"].endswith(".")


def test_catalogue_glue(addon, tmp_path):
    module, _ = addon
    context = context_for(module, game_folder(tmp_path / "dump"))
    items = module.preferences.fighter_items(None, context)
    assert [i[0] for i in items] == ["V2IP_900A", "V2IP_901A", "V2IP_900B"]
    assert items[0][1] == "Abe (MAR) - V2IP_900A"
    assert module.preferences.part_search(None, context, "0002") == ["V2CP_110_0002_C1"]
    missing = context_for(module, tmp_path / "nowhere")
    module.preferences.reset()
    assert module.preferences.fighter_items(None, missing)[0][0] == "NONE"
    assert module.preferences.part_search(None, missing, "") == []


def test_thin_bpy_layer_runs_on_mocks(addon, tmp_path):
    module, bpy = addon
    context = context_for(module, game_folder(tmp_path / "dump"))
    game, error = module.preferences.get_game(context)
    assert not error
    asset = game.load_fighter("V2IP_900A")
    plan = module._lib.blender_data.build_import(asset.obj, asset.name, asset.images)
    bsdf = mock.MagicMock(type="BSDF_PRINCIPLED", location=(0.0, 0.0))
    material = bpy.data.materials.new.return_value
    material.node_tree.nodes.__iter__.side_effect = lambda: iter([bsdf])
    mesh = bpy.data.meshes.new.return_value
    mesh.loops.__len__.return_value = 6
    mesh.polygons.__len__.return_value = 2

    holder = module.build.import_fighter(context, plan, "Abe (MAR)", scale=0.5)
    armature = bpy.data.objects.new.return_value
    assert holder is armature
    bpy.data.images.new.assert_called_once_with("V2IP_900A_T001", 2, 2, alpha=True)
    pixels = bpy.data.images.new.return_value.pixels.foreach_set.call_args[0][0]
    assert len(pixels) == 16 and list(pixels[:4]) == [1.0, 0.0, 0.0, 1.0]
    edit_bones = bpy.data.armatures.new.return_value.edit_bones
    assert [c.args[0] for c in edit_bones.new.call_args_list] == ["root", "L_arm", "R_arm"]
    mesh.from_pydata.assert_called_once_with(plan.meshes[0]["positions"], [], plan.meshes[0]["faces"])
    uv = mesh.uv_layers.new.return_value.data.foreach_set.call_args[0]
    assert uv[0] == "uv" and len(uv[1]) == 12
    mesh.normals_split_custom_set_from_vertices.assert_called_once_with(plan.meshes[0]["normals"])
    groups = armature.vertex_groups.new
    assert sorted(c.kwargs["name"] for c in groups.call_args_list) == ["L_arm", "R_arm", "root"]
    assert groups.return_value.add.call_count == 8
    assert bsdf.inputs.__getitem__.call_args_list[0].args == ("Metallic",)
    assert material.surface_render_method == "BLENDED"
    assert armature.scale == (0.5, 0.5, 0.5)

    part_asset = game.load_part("V2CP_110_0001_C")
    part = module._lib.blender_data.build_import(part_asset.obj, part_asset.name, part_asset.images,
                                                 fallback_texture=part_asset.fallback_texture)
    armature.data.bones.keys.return_value = ["root", "L_arm"]
    armature.get.return_value = ""
    objects, missing = module.build.import_part(context, part, armature)
    assert len(objects) == 1 and missing == ["R_arm"]


def test_addon_zip_bundles_the_library(tmp_path, monkeypatch):
    build_addon = importlib.import_module("build_addon")
    out, version = build_addon.build(tmp_path / "addon.zip")
    with zipfile.ZipFile(out) as zf:
        names = zf.namelist()
        zf.extractall(tmp_path / "addons")
    assert "ffny_studio/__init__.py" in names and "ffny_studio/blender_manifest.toml" in names
    assert "ffny_studio/ffnystudio/texture.py" in names and "ffny_studio/ffnystudio/_hd_assets.py" in names
    assert "ffny_studio/ffnystudio/__main__.py" not in names
    assert all(n.startswith("ffny_studio/") and n.endswith((".py", ".toml")) for n in names)
    assert not any("__pycache__" in n for n in names)
    assert build_addon.build(tmp_path / "again.zip")[0].read_bytes() == out.read_bytes()

    # The extracted add-on uses its own bundled library, RefPack decoder included.
    bpy = fake_bpy()
    monkeypatch.setitem(sys.modules, "bpy", bpy)
    monkeypatch.setitem(sys.modules, "bpy.props", bpy.props)
    monkeypatch.syspath_prepend(str(tmp_path / "addons"))
    monkeypatch.delenv("FFNY_HD_ASSETS", raising=False)
    for name in [n for n in sys.modules if n == "ffny_studio" or n.startswith("ffny_studio.")]:
        monkeypatch.delitem(sys.modules, name)
    try:
        module = importlib.import_module("ffny_studio")
        assert module._lib.game.__name__ == "ffny_studio.ffnystudio.game"
        game = module._lib.game.Game(str(game_folder(tmp_path / "dump")))
        assert [f.model for f in game.fighters()][0] == "V2IP_900A"
        assert game.load_fighter("V2IP_900A").images["T001"].width == 2
        repo = sys.modules["ffny_studio.ffnystudio._repo"]
        assert Path(repo._cache["hd"].__file__).name == "_hd_assets.py"
    finally:
        for name in [n for n in sys.modules if n == "ffny_studio" or n.startswith("ffny_studio.")]:
            del sys.modules[name]
