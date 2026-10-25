"""Add-on preferences (the game folder) and the cached catalogue of that folder."""

import bpy
from bpy.props import StringProperty

from . import _lib

ADDON = __package__
_state = {"path": None, "game": None, "error": "", "fighters": [], "items": []}


def _changed(self, context):
    reset()


class FFNYStudioPreferences(bpy.types.AddonPreferences):
    bl_idname = ADDON

    game_folder: StringProperty(
        name="Game folder",
        description="Your own extracted copy of the game: the folder with fighter.xml and assets/",
        subtype="DIR_PATH",
        update=_changed,
    )

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "game_folder")
        game, error = get_game(context)
        if game:
            layout.label(text="Found %d fighters in %s" % (len(fighters(context)), game.root), icon="CHECKMARK")
        else:
            layout.label(text=error, icon="ERROR")
        layout.label(text="FFNY Studio reads only this folder. It contains no game data; mods must not "
                          "redistribute EA's assets.", icon="INFO")


def prefs(context):
    addon = context.preferences.addons.get(ADDON)
    return addon.preferences if addon else None


def reset():
    _state.update(path=None, game=None, error="", fighters=[], items=[])


def get_game(context):
    """(game.Game, "") or (None, reason). Cached until the folder setting changes."""
    p = prefs(context)
    path = bpy.path.abspath(p.game_folder) if p and p.game_folder else ""
    if path != _state["path"]:
        reset()
        _state["path"] = path
        try:
            _state["game"] = _lib.game.Game(path)
        except (ValueError, OSError) as error:
            _state["error"] = str(error)
    return _state["game"], _state["error"]


def fighters(context):
    game, _ = get_game(context)
    if game and not _state["fighters"]:
        try:
            _state["fighters"] = game.fighters()
        except (ValueError, OSError) as error:
            _state["error"] = str(error)
    return _state["fighters"]


def fighter_items(self, context):
    """EnumProperty items. Kept in module state: Blender needs the strings to stay alive."""
    found = fighters(context)
    if not found:
        _, error = get_game(context)
        _state["items"] = [("NONE", "No fighters", error or "Set the game folder in the add-on preferences")]
    elif len(_state["items"]) != len(found):
        _state["items"] = [(f.model, f.label, "fighter.xml ID %d" % f.id if f.listed else "not in fighter.xml")
                           for f in found]
    return _state["items"]


def fighter_by_model(context, model):
    for f in fighters(context):
        if f.model == model:
            return f
    return None


def part_search(self, context, edit_text):
    """StringProperty search: clothing parts whose name contains the typed text."""
    game, _ = get_game(context)
    if not game:
        return []
    key = (edit_text or "").lower()
    try:
        names = [p.name for p in game.parts()]
    except (ValueError, OSError):
        return []
    return [n for n in names if key in n.lower()][:500]
