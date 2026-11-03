# Texture Modding — Getting Started

Want to redraw a menu, improve a fighter's outfit, or replace a venue texture?
This guide takes you from finding an original image to loading your first texture
pack in Def Jam: Fight for NY Recompiled.

Start with **one texture and a small, obvious edit**. Once you can see it in the
game, add more images or higher-resolution artwork. You do not need an AI upscaler
or the full HD texture library to get started.

## How texture replacement works

The game reads the original assets from your own Xbox dump. A texture pack tells
the renderer, “when you encounter this original texture, use my image instead.”
It leaves the original game archives alone. Disable the pack to return to the
original appearance, or to the next enabled pack that supplies that texture.

Three things matter:

| Term | Meaning |
|---|---|
| Texture | An image used by a menu, character, object or effect. |
| Atlas | One image containing several pieces: for example a fighter's face, clothing and tattoos, or several UI elements. The model/menu samples particular areas of it. |
| Texture ID | A 64-character identifier for an original texture. The pack's manifest maps this ID to your replacement file. |

Texture packs change appearance. They do not add a fighter to the roster, change
a model's shape, or replace animations, music or game code. The tools' fighter
model viewer is a static preview; exporting a Blender model is not a working
new-fighter import path.

## Before you begin

You need a working installation from your supported Xbox dump, an image editor
that preserves PNG transparency, and a separate folder for your editing work.
GIMP, Krita, Photoshop or another editor you already use will do.

In the launcher, choose **Open Folder → Data** to find your data folder, or
**Open Folder → Mods** to go directly to its packs. The examples below call the
data folder `<data>`; replace it with your actual location. Setup lets you choose
this location independently of the installation folder, so use the launcher
instead of guessing a path under Program Files or your home directory.

Keep originals and editing projects outside the installed packs. Use a separate
test pack rather than editing Recomp HD, Faithful HD or a downloaded mod in place.
That makes comparisons and updates much easier.

## 1. Find and view the original texture

There are two useful routes. Capture from the game when you know which screen
or fighter you want to change. Extract from the archives when you want to browse
more broadly or inspect the named source resources.

### Capture the textures used on a screen

This is the shortest route to a first working replacement:

1. Open **Advanced → Diagnostics → Texture authoring** in the launcher.
2. Enable **Capture source textures**. Set **Maximum new captures** high enough
   for the screen you intend to visit.
3. Start the game and visit that screen, fighter or venue.
4. Close the game. Turn capture off before your next normal play session.
5. Open `<data>/hd-work/runtime/`.

This folder contains original texture PNGs named by ID, plus `textures.jsonl`,
which records their IDs, dimensions and other capture information. These are
individual source images, not screenshots. Open the PNGs in your image editor
or an image browser and find the one you want. A single screen may use many
layers; you may need to inspect several images.

Capture records the original source before replacement, even when HD packs are
enabled. It only captures supported textures the renderer encounters while you
play, up to the configured limit. Visiting a menu does not dump every texture
on the disc. If something is missing, visit the relevant state or increase the
capture limit and restart. Capture adds work, so leave it off for performance
comparisons.

### Extract and browse the game archives

[Def Jam Modding Tools](https://github.com/vyanhursky/DefJam-Modding-Tools)
provides archive extraction, a searchable texture gallery and original/Faithful HD
comparisons. Its [README](https://github.com/vyanhursky/DefJam-Modding-Tools#install-and-resume)
explains installation with Python 3.11+ and a virtual environment. The commands
below use `defjam-tools` from that environment. If it is not on your PATH, replace
that name with `.\.venv\Scripts\defjam-tools.exe` on Windows or
`./.venv/bin/defjam-tools` on macOS/Linux, running from the tools repository.
Paths in quotes are examples to replace; use your actual extracted-game and work
folders. The argument syntax is the same in PowerShell and a macOS/Linux shell.

For an initial inventory and up to 20 decoded PNGs from matching `load0` resources:

```sh
defjam-tools assets inventory --root "/path/to/extracted" --output "/path/to/texture-work/inventory" --select load0 --limit 20
```

`--root` is the **extracted game folder**, not an ISO. See
[setup](setup-installer.md) if you have not extracted your dump yet. The command
writes `inventory.json` and selected PNGs beneath `originals/`. Open those PNGs
directly. `--select` matches archive/resource paths; remove it to export from any
supported resource. `--limit` bounds PNG exports, not the inventory scan.

For a full original/4× comparison gallery, build a corpus from that inventory:

```sh
defjam-tools corpus build --root "/path/to/extracted" --inventory "/path/to/texture-work/inventory/inventory.json" --output "/path/to/texture-work/corpus"
defjam-tools corpus serve --output "/path/to/texture-work/corpus" --port 8767
```

Then open [localhost:8767](http://127.0.0.1:8767/). This builds the **whole supported
inventory**, not just the 20 exported examples. Allow several GB of space: the
supported full corpus has about 19,000 images and roughly 6.2 GB of 4× PNGs alone.
Reuse an existing corpus if you have one; this step is optional for a first pack.
Use another free port if 8767 is already occupied.

To use revision tracking and side-by-side editing reviews, see the
[texture workshop guide](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/texture-workshop.md).
Its [model viewer guide](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/model-viewer.md)
covers static fighter previews. Keep corpus and review-workspace folders together
when backing up your work; neither belongs inside your shared mod ZIP.

### Which texture ID should I use?

For a captured texture, use the 64-character filename **without `.png`**, also
recorded as `id` in `textures.jsonl`. Copy it exactly, including lowercase letters.

**The ID identifies the original, not your edited PNG.** Do not calculate a new
ID from your replacement or invent a number. Editing your image does not change
the manifest key.

Archive inventory entries also have `source_id`, but those IDs can differ from
what the game actually uploads: the game may transform pixels or palettes.
Runtime capture is the authority for a replacement that will load. The same-looking
art can have multiple IDs or variants, and one ID can be reused in several places.
If an archive-derived replacement does not appear, capture the actual screen
and check its ID before changing your artwork.

## 2. Edit the image without breaking its layout

Save a working copy as a **PNG with transparency preserved**. For your first test,
keep the original dimensions and make a visible colour/detail change in an area
you can find in-game. Keep the original copy for comparison.

For higher resolution, multiply both dimensions by the same whole number from
1 through 8. For example, a 512×256 original can become 1024×512 at 2× or
2048×1024 at 4×. Upscaling both dimensions by 4 produces 16 times as many pixels;
larger packs also need more texture memory.

Keep every atlas piece in the same proportional position. Moving a sleeve or
cropping a menu tile makes the game sample the wrong part of the image. Preserve
alpha separately: black is not a substitute for transparency. Check see-through
holes and soft edges against a checkerboard. Some alpha channels contain material
data rather than transparency; see the [format reference](hd-textures.md#install-and-enable-a-pack)
before changing an unfamiliar mask or secondary map.

For restorations, compare with the original at game size as well as zoomed in.
Watch for colour shifts, rewritten lettering, skin seams and altered logos.
For tiled surfaces, check the repeating edges too.

## 3. Make a minimal texture pack

Create this folder under `<data>/mods/` while the game is closed:

```text
my-first-texture-pack/
├── manifest.ini
└── textures/
    └── edited-atlas.png
```

Save your edited PNG as `textures/edited-atlas.png`. Create `manifest.ini` with a
plain-text editor and save it as UTF-8. On Windows, check that the actual filename
is not `manifest.ini.txt`.

```ini
[pack]
schema=1
name=My First Texture Pack

[textures]
0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef=textures/edited-atlas.png
```

**Replace the example ID above with your captured texture's real ID.** That ID
is illustrative and will not make a replacement appear. The image filename can
be descriptive; it does not have to equal the ID. The manifest connects the two.

| Item | Required? | Purpose |
|---|---|---|
| `manifest.ini` | Yes | Declares schema 1 and maps original IDs to replacement files. |
| Every image referenced by the manifest | Yes | Supplies the replacement pixels. PNG is the easiest starting format. |
| `[pack] name` | Recommended | Identifies the pack; keep a stable, unique folder name too. |
| `README.txt` | Recommended for sharing | Author, version, description, compatible game version, installation/load order, credits and known limitations. |
| Editing projects, previews and original captures | No | Keep these in your authoring workspace. |

Add another line under `[textures]` for each additional original ID. A pack can
replace just one image; textures it omits continue to come from other packs or
the game. Use one mapping per ID within a manifest.

Use simple filenames with ASCII letters, numbers, hyphens and underscores, and
forward slashes in manifest paths. Paths are relative to the pack folder: avoid
absolute paths, `..`, spaces and platform-specific drive letters. Match filename
case exactly so the same pack works on Windows, macOS and Linux.

PNG replacements use straight alpha, not premultiplied alpha. Classic DDS with
BC1/DXT1, BC2/DXT3 or BC3/DXT5 and validated mips is also supported. JPEG, BC7 and
DX10 DDS are not replacement formats for this loader. Start with PNG and consult
the [technical reference](hd-textures.md) for compression and mip policies.

## 4. Enable it and choose which pack wins

1. Return to the launcher and open **Mods**.
2. Choose **Refresh local mods**. Your folder should appear as a local mod.
3. Enable **Use HD texture packs** and your new pack.
4. Open **Load Order…** and use **Higher** to put your pack above any pack that
   replaces the same texture, including **Recomp HD** or **HD Widescreen Menus**.
5. Start the game and revisit the texture you captured.

The current Load Order window shows **highest priority at the top**. A mod above
another wins their overlapping texture IDs. Generated Faithful components appear
together as Recomp HD; you can keep that baseline enabled beneath your custom pack.
The overlap information helps explain which pack is taking priority.

Changes take effect on the next game start. **Restart after changing image files,
manifests or load order**; refreshing the folder list is not a live texture reload.
Uncheck a pack to disable it without deleting it.

If you edit settings manually, the underlying INI list uses the opposite visual
direction: **the last folder in `packs` wins**. For example:

```ini
[textures]
enabled=true
packs=faithful-hd;my-first-texture-pack
```

Here `faithful-hd` is only an example baseline folder; use your installed folder
names. The launcher handles the stored list for you, so manual editing is optional.
See [mod management](mod-manager.md) and [settings](settings-reference.md#textures).

## 5. Test and share the pack

Compare your pack enabled and disabled. Check the intended screen, close-ups and
normal play distances. For UI artwork, also check transparency, transitions and
the aspect ratios you want to support. Fighter outfits may have separate Story,
cinematic or damaged variants; editing one texture does not establish coverage
of all of them.

Once the local folder works, ZIP the **pack folder itself**, keeping one top-level
folder in the archive:

```text
my-first-texture-pack-1.0.0.zip
└── my-first-texture-pack/
    ├── manifest.ini
    ├── README.txt
    └── textures/
        └── edited-atlas.png
```

The ZIP filename may include a version. Keep the installed folder stable across
updates. Do not add a second wrapping folder such as
`download/my-first-texture-pack/manifest.ini`.

There are two distribution routes:

- **Share a local pack ZIP.** Its recipient extracts that single pack folder into
  their data folder's `mods/`, refreshes local mods, enables it and sets priority.
  The runtime reads the installed folder, not the ZIP directly.
- **Publish through the project catalogue.** Players download/install in the
  launcher's Mods page, then enable the pack and set priority. Maintainers review
  the package and record its exact version, download URL, ZIP hash/size and
  compatibility in the [catalogue repository](https://github.com/vyanhursky/DefJamFFNY-mods).
  Making a ZIP does not automatically list it. See [catalogue operations](mod-manager.md#catalogue-operations).

The runtime requires `manifest.ini`; the signed catalogue is separate download
metadata. You do not need a signing key or a catalogue JSON file for a local pack.
Current catalogue ZIP validation allows `.ini`, `.png`, `.dds`, `.json` and `.txt`
files. Use `README.txt` in that ZIP; keep `.psd`, `.kra`, models and other editing
files outside it. Thunderstore's root `manifest.json` is another package format,
not a replacement for our manifest or a currently supported automatic import.

Include your replacement artwork and its credits/permissions, not the original
disc image, extracted archives or a built game executable. Keep source captures
and your editable project files backed up separately. For subsequent releases,
publish a new version rather than silently replacing a download already used by
the signed catalogue.

## Widescreen menu backgrounds need an extra step

A wider PNG alone will not make a menu widescreen. Ordinary replacements retain
the original texture shape and placement. The wide-menu loader uses additional
`[wide]` registrations to draw scenery outside the original 4:3 area.

Treat this as a follow-up after your first ordinary replacement works. Preserve
the original centre, identify the actual tiles/layers, and extend scenery across
both boundaries. Test joins, alpha, lighting, moving transitions and 4:3 as well as
wide views. Keep text and menu controls separate from background scenery.

See the tools' [widescreen scenery guide](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/widescreen-backgrounds.md)
and [menu art workflow](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/menu-art-workflow.md).
The [wide-menu implementation notes](research/menu-ui-playtest-pack-2026-10-19.md#widescreen-backgrounds-d160ui-toolkit-defjammenu-wide)
explain the slab/anchor format; those are dated engineering notes, so check the
current export scripts and loader before building a new wide pack.

## If something does not look right

| Symptom | First things to check |
|---|---|
| Pack is missing from Mods | Check `<data>/mods/<pack-folder>/manifest.ini`, avoid an extra nested folder, then refresh local mods. |
| Pack is enabled but nothing changes | Use the actual runtime ID, check that the edited image is referenced, move the pack higher, and restart. |
| Image falls back to the original | Check PNG/DDS support, integer scale, aspect/layout and path spelling. Search the game's logs for `[TEXPACK]`. |
| Black boxes or solid holes | Inspect alpha separately; ensure the editor did not flatten transparency onto black. |
| Wrong pieces appear on a character/menu | Restore the original atlas positions and proportions. |
| Seams, halos or shimmering | Check tile edges, UV padding, alpha and mip policy; compare in motion. |
| Hitches or repeated texture loading | Try fewer/smaller replacements, turn capture off, and check the texture-memory budget in Mods. |

The launcher provides **Open Logs** and the resolved data/mod paths. When asking
for help, include the game version, pack version, original texture ID, original
and replacement dimensions, enabled pack order, and a screenshot of the problem.

For more detail, use the [texture format reference](hd-textures.md),
[processing/review workflow](hd-texture-workflow.md), [launcher guide](launcher-and-overlay.md),
[mod-manager reference](mod-manager.md), or the
[Modding Tools documentation index](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/README.md).
