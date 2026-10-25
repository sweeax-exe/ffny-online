"""The "FFNY Studio" tab in the 3D view's sidebar (N panel)."""

import bpy

from . import operators, preferences

INFO_FIELDS = (("Model", "ffny_model"), ("Bones", "ffny_bones"), ("Meshes", "ffny_meshes"),
               ("Vertices", "ffny_vertices"), ("Triangles", "ffny_triangles"), ("Textures", "ffny_images"),
               ("Scale", "ffny_scale"), ("Parts", "ffny_parts"))


def info_rows(obj):
    """(label, text) rows describing an imported fighter (the custom properties the importer set)."""
    rows = []
    for label, key in INFO_FIELDS:
        if key in obj:
            value = obj[key]
            text = "%.4g" % value if isinstance(value, float) else str(value)
            rows.append((label, text or "-"))
    return rows


class VIEW3D_PT_ffny_studio(bpy.types.Panel):
    bl_label = "FFNY Studio"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "FFNY Studio"

    def draw(self, context):
        layout = self.layout
        game, error = preferences.get_game(context)
        box = layout.box()
        if game:
            box.label(text="Game: %d fighters" % len(preferences.fighters(context)), icon="CHECKMARK")
            box.label(text=str(game.root))
        else:
            box.label(text="Game folder not set", icon="ERROR")
            box.label(text=error[:80] if error else "")
        row = box.row(align=True)
        row.operator("preferences.addon_show", text="Settings", icon="PREFERENCES").module = preferences.ADDON
        row.operator(operators.FFNY_OT_refresh.bl_idname, text="", icon="FILE_REFRESH")

        col = layout.column(align=True)
        col.enabled = game is not None
        col.operator(operators.IMPORT_SCENE_OT_ffny_fighter.bl_idname, text="Import Fighter", icon="IMPORT")
        col.operator(operators.FFNY_OT_import_part.bl_idname, text="Add Clothing Part", icon="MOD_CLOTH")

        armature = operators.ffny_armature(context)
        if armature is not None:
            box = layout.box()
            box.label(text=str(armature.get("ffny_display", armature.name)), icon="ARMATURE_DATA")
            for label, text in info_rows(armature):
                row = box.row()
                row.label(text=label)
                row.label(text=text)
        else:
            layout.label(text="Select an imported fighter for details", icon="INFO")


CLASSES = (VIEW3D_PT_ffny_studio,)
