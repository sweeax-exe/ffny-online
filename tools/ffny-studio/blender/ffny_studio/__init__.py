"""FFNY Studio for Blender: import Def Jam: Fight for NY fighters and clothing from the user's own game copy.

Stage 1 is the importer: File > Import > "FFNY fighter (from your game)" and the "FFNY Studio" sidebar tab.
The add-on contains no game data; it reads only the game folder set in its preferences. Build the
installable zip with tools/ffny-studio/build_addon.py, which bundles the ffnystudio library.
"""

bl_info = {
    "name": "FFNY Studio",
    "author": "FFNY Online contributors",
    "version": (0, 1, 0),
    "blender": (4, 2, 0),
    "location": "File > Import > FFNY fighter (from your game); 3D View > Sidebar > FFNY Studio",
    "description": "Import Def Jam: Fight for NY fighters and clothing from your own copy of the game",
    "category": "Import-Export",
}

import bpy  # noqa: E402

from . import operators, preferences, ui  # noqa: E402

CLASSES = (preferences.FFNYStudioPreferences,) + operators.CLASSES + ui.CLASSES


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_import.append(operators.menu_import)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(operators.menu_import)
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)
    preferences.reset()
