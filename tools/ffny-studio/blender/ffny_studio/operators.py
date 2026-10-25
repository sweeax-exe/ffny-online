"""The import operators: a fighter (File > Import and the side panel) and a clothing part onto its armature."""

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, StringProperty

from . import build, preferences
from ._lib import blender_data

DEFAULT_SCALE = 0.085      # a fighter is about 21.5 game units tall; this makes it about 1.8 m


def ffny_armature(context):
    """The FFNY armature of the active object (itself or its parent), or None."""
    obj = context.active_object
    for candidate in (obj, obj.parent if obj else None):
        if candidate is not None and candidate.type == "ARMATURE" and "ffny_model" in candidate:
            return candidate
    return None


def _load_part(context, name, textures):
    game, error = preferences.get_game(context)
    if not game:
        raise ValueError(error)
    asset = game.load_part(name)
    return asset, blender_data.build_import(asset.obj, asset.name, asset.images if textures else {},
                                            fallback_texture=asset.fallback_texture if textures else "")


class IMPORT_SCENE_OT_ffny_fighter(bpy.types.Operator):
    """Import a fighter from your own copy of the game: armature, meshes, skin weights and textures"""
    bl_idname = "import_scene.ffny_fighter"
    bl_label = "Import FFNY Fighter"
    bl_options = {"REGISTER", "UNDO"}

    fighter: EnumProperty(name="Fighter", items=preferences.fighter_items,
                          description="fighter.xml entries, then the unlisted models of v2ip.viv")
    part: StringProperty(name="Clothing part", search=preferences.part_search,
                         description="Optional: a V2CP_* part to add onto the same armature")
    scale: FloatProperty(name="Scale", default=DEFAULT_SCALE, min=0.0001, max=100.0,
                         description="Object scale of the armature; the mesh data stays in game units")
    textures: BoolProperty(name="Textures", default=True, description="Decode and attach the SHPX textures")
    helpers: BoolProperty(name="Facing arrow", default=False,
                          description="Also import the ground arrow that marks the facing (not part of the body)")

    def invoke(self, context, event):
        game, error = preferences.get_game(context)
        if not game:
            self.report({"ERROR"}, error or "Set the game folder in the add-on preferences")
            return {"CANCELLED"}
        return context.window_manager.invoke_props_dialog(self, width=460)

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "fighter")
        layout.prop(self, "part")
        row = layout.row()
        row.prop(self, "scale")
        row.prop(self, "textures")
        layout.prop(self, "helpers")

    def execute(self, context):
        game, error = preferences.get_game(context)
        if not game or self.fighter in ("", "NONE"):
            self.report({"ERROR"}, error or "No fighter selected")
            return {"CANCELLED"}
        fighter = preferences.fighter_by_model(context, self.fighter)
        label = fighter.label if fighter else self.fighter
        try:
            asset = game.load_fighter(self.fighter)
            plan = blender_data.build_import(asset.obj, asset.name, asset.images if self.textures else {},
                                             include_helpers=self.helpers)
        except (ValueError, KeyError, OSError) as error:
            self.report({"ERROR"}, "Could not read %s: %s" % (self.fighter, error))
            return {"CANCELLED"}
        holder = build.import_fighter(context, plan, label, scale=self.scale)
        s = plan.stats
        self.report({"INFO"}, "Imported %s: %d bones, %d meshes, %d vertices, %d textures" % (
            label, s["bones"], s["meshes"], s["vertices"], s["textures"]))
        for problem in asset.texture_errors:
            self.report({"WARNING"}, "Texture skipped: " + problem)
        if self.part.strip() and holder is not None and holder.type == "ARMATURE":
            return _attach_part(self, context, holder, self.part, self.textures)
        return {"FINISHED"}


def _attach_part(op, context, armature, name, textures):
    try:
        asset, plan = _load_part(context, name, textures)
    except (ValueError, KeyError, OSError) as error:
        op.report({"ERROR"}, "Could not read part %s: %s" % (name, error))
        return {"CANCELLED"}
    objects, missing = build.import_part(context, plan, armature)
    op.report({"INFO"}, "Added %s: %d meshes" % (plan.name, len(objects)))
    if missing:
        op.report({"WARNING"}, "Bones not in %s: %s" % (armature.name, ", ".join(missing[:8])))
    return {"FINISHED"}


class FFNY_OT_import_part(bpy.types.Operator):
    """Add a clothing part (V2CP_*) to the selected FFNY fighter's armature"""
    bl_idname = "ffny.import_part"
    bl_label = "Add Clothing Part"
    bl_options = {"REGISTER", "UNDO"}

    part: StringProperty(name="Clothing part", search=preferences.part_search,
                         description="A V2CP_* part from v2cp_0..3.viv")
    textures: BoolProperty(name="Textures", default=True)

    @classmethod
    def poll(cls, context):
        return ffny_armature(context) is not None

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self, width=400)

    def execute(self, context):
        armature = ffny_armature(context)
        if armature is None or not self.part.strip():
            self.report({"ERROR"}, "Select an imported FFNY fighter and name a part")
            return {"CANCELLED"}
        return _attach_part(self, context, armature, self.part, self.textures)


class FFNY_OT_refresh(bpy.types.Operator):
    """Read the game folder again (after changing it or the files in it)"""
    bl_idname = "ffny.refresh"
    bl_label = "Refresh Game Folder"

    def execute(self, context):
        preferences.reset()
        game, error = preferences.get_game(context)
        if game:
            self.report({"INFO"}, "%d fighters in %s" % (len(preferences.fighters(context)), game.root))
        else:
            self.report({"WARNING"}, error)
        return {"FINISHED"}


def menu_import(self, context):
    self.layout.operator(IMPORT_SCENE_OT_ffny_fighter.bl_idname, text="FFNY fighter (from your game)")


CLASSES = (IMPORT_SCENE_OT_ffny_fighter, FFNY_OT_import_part, FFNY_OT_refresh)
