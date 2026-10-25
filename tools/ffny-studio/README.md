# FFNY Studio

Modding tools for Def Jam: Fight for NY: a Python library and command line (`ffnystudio`) that read the
game's archives, EAGL models and SHPX textures, and a Blender add-on that imports fighters and clothing.

> **Legal note.** FFNY Studio contains no game data. It reads only your own copy of the game, from the
> folder you point it at, and writes only where you tell it to. Models, textures and anything exported
> from them are EA's assets: keep them on your own computer. A mod must not redistribute EA's assets
> (no meshes, textures or archives taken from the game); share tools, patches and your own work only.

## Blender add-on (stage 1: import)

Blender 4.2 LTS or newer.

### Build and install

```
python tools/ffny-studio/build_addon.py
```

This writes `build/ffny-studio/ffny_studio-<version>.zip`: the add-on with the `ffnystudio` library bundled
(only Python sources, no game data). In Blender: **Edit > Preferences > Get Extensions**, the menu at the top
right, **Install from Disk...**, pick the zip. (It also has a `bl_info`, so the legacy add-on installer
accepts it too.)

### Set the game folder

In the add-on's preferences (**Edit > Preferences > Add-ons > FFNY Studio**), set **Game folder** to your
extracted copy of the game: the folder with `fighter.xml` and `assets/`. The `assets` folder itself, or a
folder holding `extracted/` or `game/`, also works. The preferences show how many fighters were found.

### Import

* **File > Import > FFNY fighter (from your game)**, or the **Import Fighter** button in the 3D view's
  sidebar (**N**), tab **FFNY Studio**.
* Pick a fighter. The list is `fighter.xml` (display name and model, e.g. *House (STF) - V2IP_003A*), then the
  models of `v2ip.viv` that no entry names (alternate outfits).
* Optional: a **Clothing part** (`V2CP_...`, type to search); it is added onto the same armature.
* **Scale** is the armature object's scale (default 0.085, about 1.8 m tall); the mesh data stays in game
  units. **Facing arrow** also imports the small ground arrow the game uses as a facing marker.

You get a collection named after the model with:

* an armature of the 94-bone skeleton, posed at the bind pose (from the inverse bind matrices), with the
  game's parents and names. Each bone keeps its inverse bind matrix, index, mirror bone and rest transform as
  custom properties (`ffny_*`);
* one mesh object per draw, with normals as custom split normals, UVs, vertex colours where they are not
  plain white, vertex groups from the skin weights and an Armature modifier;
* materials with the fighter's textures as images (packed into the .blend). The game's irradiance and
  environment-map shading is approximated by a plain Principled BSDF.

**Add Clothing Part** in the sidebar adds a part to the selected fighter later. Parts use a run-time
placeholder texture in the game; the importer uses the part's own first texture (`<part>_A01_01.xsh`) when it
has one. The sidebar also shows the selected fighter's model, bone, mesh, vertex and texture counts.

The game is +Y up and faces +Z; the importer turns that into Blender's +Z up, facing -Y (the Front view).

### Status

Stage 1 is import only. The add-on has unit tests for everything outside Blender (the decoding and the data it
builds) and a stub-`bpy` test of its structure, but **it has not yet been run in a real Blender**; expect
fixes after the first try. Export back to the game is the next stage.

### Developing

The add-on folder `tools/ffny-studio/blender/ffny_studio` also runs straight from a checkout (link it into
Blender's add-on folder): without the bundled copy it imports `tools/ffny-studio/ffnystudio` and
`scripts/hd_assets.py` from the checkout. Everything that does not need `bpy` lives in the library
(`ffnystudio/texture.py`, `game.py`, `blender_data.py`) and is tested with `python -m pytest -q tests/unit`.

## Command line

```
cd tools/ffny-studio
python -m ffnystudio list v2ip.viv
python -m ffnystudio info v2ip.viv V2IP_003A.o
python -m ffnystudio export-fighter V2IP_003A <out-dir>
```

The game is found through `$FFNY_GAME`, the checkout's `game` folder or `$DEFJAM_DATA/extracted`. The glTF
export needs Pillow for the textures. Formats: `docs/research/ffny-studio-formats.md` and
`docs/research/ffny-studio-eagl.md`.
