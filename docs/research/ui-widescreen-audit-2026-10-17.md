# Menu restoration and widescreen: runtime audit, 2026-10-17

This is a read-only source/archive audit for the UI restoration planning session. No code was changed, no build was made, and no game was run. This report is the only file written by this audit. Asset names, counts, dimensions and structure come from the user's own dump; no artwork or game bytes are reproduced here.

## Evidence and source identity

- Parent checkout: `/Users/vlad/.codex/worktrees/01c5/DefJamFFNY-recomp`. Read `AGENTS.md`, `PROGRESS.md`, `src/patches/widescreen.{c,h}`, settings/capture/harness code, and the previous `docs/widescreen/` and research reports.
- Its `tools/xboxrecomp` directory is uninitialized. The parent gitlink is `4a6c88e841dd74d3c279f15b8f8e50110b0df70d`.
- The **exact pinned toolkit commit**, with a clean working tree, is available locally at `/Users/vlad/Code/xboxrecomp-audio-fix`. Renderer and pack-loader conclusions below were checked there. The older `/Users/vlad/Code/DefJamFFNY-recomp/tools/xboxrecomp` is at `837e2c7`; it was useful for initial exploration but does not supersede the exact-pin evidence.
- Local dump: `/Users/vlad/Code/defjam-data/extracted/screens/{screens.viv,feflow.xml}`. Used the repository's APT and RefPack parsers in memory to inspect `prestart.big`, `demoStrt.big` and `mainMenu.big`; no extraction was written.

## What exists today

The port already separates a wider render canvas from the console's 640x480 UI coordinates. `ws_init()` in `src/patches/widescreen.c:50` fixes the aspect for a boot, configures the renderer, and widens full-screen 3D views. The picture-shape choices are 4:3, stretch, auto, 16:9, 16:10, experimental 21:9 and experimental 32:9. `RECOMP_WS_ASPECT` overrides the choice with a positive number. Auto snaps near 16:9/16:10 and caps ultrawide discovery at 16:9; wider shapes require explicit selection.

| Shape | `k = (4/3)/aspect` | Logical canvas width at height 480 | Present menu behavior |
|---|---:|---:|---|
| 4:3 | 1 | 640 | Original geometry |
| 16:10 | 0.833333 | 768 | 4:3 menu centered, sides available |
| 16:9 | 0.75 | 853.333 (rounded target) | Same |
| 21:9 | 0.571429 | 1120 | Same; experimental |
| 32:9 | 0.375 | 1706.667 (rounded target) | Same; experimental |

The exact toolkit's `src/kernel/nv2a_pb_exec.c:805`, `ws_place_batch()`, is the decisive 2D implementation. It examines transformed vertices, classifies constant-depth/w=1 draws as 2D, then applies per-draw placement:

- Large full-screen pictures stay centered with their authored proportions. They are recognized by approximately full 640x480 coverage and a source texture larger than 128 texels.
- Full-screen untextured/small-texture fills stretch across the canvas: fades, tints and similar overlays.
- Other 2D draws are centered; fight HUD strips can be left/right anchored while the match heartbeat is active.
- Centered and anchored draws receive a correspondingly narrowed scissor rectangle, deliberately hiding off-stage slide-ins.
- This decision uses geometry and original texture dimensions, not an art pack's intended aspect or a menu-background role.

There is **no implemented menu-background-specific stretch/cover policy, aspect variant selector, or outpaint metadata** in the pinned path. A source-wide setting that removes all 2D squeezing would stretch fonts, logos, posters and prompts too. It would not be a correct widescreen restoration.

`ws_end_frame()` requests ambient sides only when the frame contains a centered full-screen picture and no 3D. D3D11 implements the blurred fill. The exact toolkit's `src/d3d/d3d8_vk.c:1536` explicitly makes `d3d8_RequestSideFill()` a no-op: Vulkan/MoltenVK/Deck currently retain black sides. `src/hooks/pc_settings.c:41` exposes only `blur` and `black`. The `art` setting and `mods/<pack>/widescreen/<screen>.png` in the design document are a **proposal**, not an existing feature.

## The first screens are layered animation, not screenshots

### Retail Press Start: `screens/intMain/prestart.big`

`feflow.xml` names `intMain/preStart` as the initial interactive screen and defines its transition to `intMain/mainMenu`. This is the retail pilot target. The title golden describes the skyline, logo, Press START and copyright. The similarly named `demoStrt` is a different movie with demo-specific content and should not define the retail restoration.

Fresh APT read: 640x480; 165 main-timeline frames; 42 character records (1 Movie, 15 Images, 10 Shapes, 14 Sprites, 1 Font, 1 Text). The SHPX contains 15 texture records. Important background image pairs are:

| Logical background layer | SHPX texture names / APT Image IDs | Source dimensions | Main placement |
|---|---|---|---|
| First city layer | `1` + `2` | 128x512 + 512x512 | Sprite 4, y=29 |
| Second city layer | `5` + `6` | 128x512 + 512x512 | Sprite 8, y=102 |
| Third city layer | `10` + `11` | 128x512 + 512x512 | Sprite 13, y=16 |

The layer labels should be tied to the visual inventory/contact sheet; these IDs are the unambiguous archive identity. The SHPX directory ordering differs from APT numeric IDs: e.g. name `1` is SHPX index 0, name `2` is index 5, name `5` is index 13 and name `6` is index 14. Always distinguish `index` from `name` in briefs, manifests and review notes.

The three pairs have a combined 640-pixel texture width, but their authored shape bounds and UVs still need to be respected. Texture height is 512 while the stage is 480; do not blindly treat the whole tile rectangle as the displayed image. Alpha, crop/padding, vertical offsets and the separately drawn shield/effect, logo and prompt must remain separate. The timeline animates layer color/position and the transition off this screen. Restoring an assembled layer and splitting it back at its original boundary prevents the image generator inventing incompatible details on adjacent tiles.

The movie's strings include the copyright and localized start-sticker logic, a delay before the first prompt, `MemCard.QueryBootupCheck`, `MemCard.BootupLoad`, inactivity handling and `Game.StartMainMenu`. This supports preserving the original prompt/text as UI rather than baking it into an AI-generated scene.

### Demo title: `screens/intMain/demoStrt.big`

Fresh read: 640x480, 380 main-timeline frames, 33 character records, 8 SHPX images. Its three city layers use `1`, `4`, `8`, each 1024x512; other images are `11` 512x128, `15` 256x64, `19` 256x128, `26`/`28` 256x64. APT includes `demoSticker`, `esrbClip`, `startSticker`, and a demo inactivity timer. Wider raw dimensions do not establish widescreen-ready artwork; the separate visual inventory identifies padded/streaked regions. Reusing these layers requires explicit alignment and fidelity review against retail, not a filename substitution.

### Mode select: `screens/intMain/mainMenu.big`

Fresh read: 640x480; four main-timeline frames; 227 character records (1 Movie, 40 Sprites, 65 Shapes, 120 Images, 1 Morph); 120 SHPX textures. This is not 120 unique full-screen backgrounds.

Main frame 0 places Sprite 222 as `mainMenu` at (316.2,239.9). That sprite has a 73-frame timeline. Its first frame places separate `spotMover`, nested `mainMenu`, `door`, `doorText`, and another art sprite. Initial child offsets include nested mainMenu x=-162.5, door=(140.6,28.5), and doorText=(143,81). Later frames pan and zoom those objects independently. ActionScript moves `spotlight`/`spotlightRim`, selects the poster, switches the door text and invokes Story/Battle/Options/etc. transitions. The nested poster menu has its own long animation timeline.

The SHPX holds large background pieces, small posters/buttons and many language/title/subtitle fragments. Several larger image dimensions are narrow strips paired with larger tiles: `4` 64x512 + `5` 256x512; `13` 64x512 + `14` 256x512; `52` 64x512 + `53` 512x512. Their precise logical grouping should be confirmed from `.o` shape geometry and the visual inventory before editing. Dimensions alone do not tell us which portion is safe to extend or whether a given image is reused in an animated object.

Preserving live menu operation means keeping these layers, masks, lighting/spotlight and text separate. A single flat regenerated menu image would lose selection state, animation and localization.

## Why a wider PNG alone will not work

The exact toolkit's `src/nv2a/texture_pack.c:339`, `valid_scale()`, accepts only 1..8 times the original width **and the same integer times its height**. Both PNG preparation and DDS validation enforce this. A differently shaped widescreen replacement is rejected by today's loader. Merely relaxing validation would still leave the following unresolved:

1. The APT triangles/UVs still draw the original shape, including split textures, crops and padding.
2. The 2D placement classifier still squeezes/centers the batch and narrows its scissor.
3. One source ID selects one replacement; there is no aspect-specific branch in `manifest.ini` resolution.
4. Background masks, transitions, color animation and spotlight may need corresponding side extensions.

Same-shape HD restoration can use the pack path immediately. True widescreen art needs an explicit draw/UV/placement policy in addition to replacement images.

`config/hd-runtime-aliases.json` contains only 13 exact RGBA-matched identities from a bounded previous pilot. It is not a complete menu map, and archive names/hashes are not automatically the runtime identities: archive palettes/swizzles can be transformed before binding. The current `RECOMP_TEXTURE_DUMP_DIR` plus `textures.jsonl`, decoded pixel matching and hash aliases remain the right correlation path. New identities should be demonstrated rather than guessed.

## Proposed implementation sequence

1. **Restore the authored 4:3 artwork first.** Reassemble the three retail title background pairs into logical layers using original crop/UV geometry, generate restrained candidates, preserve original alpha and protected pixels deterministically, and split approved results back to the original tiles. Keep original logos/text, shield/effect, prompt language frames and animation. Export an ordinary pack at a uniform supported scale (4x is a practical first candidate, not a fixed requirement).
2. **Review the restoration in texture space and as a composed screen.** Existing texture comparison can compare each tile/candidate, but screen reconstruction, original tile-boundary overlays, per-layer toggles and a timeline/contact strip are required to judge the city composition. For the main menu, add a composition showing both idle and selected states before making major edits.
3. **Outpaint approved logical background layers.** Protect the approved center, allow new artwork only in the side areas, preserve layer alpha, and retain a master plus explicit source-center rectangle. One 16:9 master can supply a 16:10 crop and a 4:3 crop when composed at the same height. For example at 1440 high: central 4:3=1920 wide, 16:10=2304, 16:9=2560. These are art-master coordinates; actual per-tile exports must still meet the loader's supported scales. 21:9/32:9 need more horizontal art or an explicit fallback.
4. **Add an opt-in wide-background path.** Keep game-specific screen/layer IDs and policies in the parent project. Put generic variant loading/placement support in the toolkit fork. Metadata must identify the original center, target/master aspect, role/layer, crop, alpha and fallback. Identify actual draws from verified texture identity and shape/context; do not globally exempt every large 2D texture from centering. Apply a full-width background geometry/UV mapping and scissor while leaving foreground menu layouts and transitions correctly centered.
5. **Handle split art and motion deliberately.** Either provide a full logical-layer texture to a replacement layer draw, or extend edge geometry/UVs with verified per-shape mapping. Keep matching movement/opacity on the extended sides. The title's layers and the mode-select door/spotlight should be measured separately. A post-present side-only plate is a lower-effort prototype, but its seam/motion must be reviewed and it cannot be called equivalent to a correctly animated layer extension.
6. **Only then expand coverage to all menus.** Reused brick/paper/background motifs should share an approved restoration, while each composition still needs a clipping/transition check. Treat fonts, logos, button glyphs and localization fragments as a different restoration task from photographic/painted background art.

Recommended initial scope is retail title + mode-select background art at 4:3/16:9/16:10, central interactive UI retained. A full widescreen re-layout of menu controls is a separate design choice and is not necessary to obtain full-width backgrounds.

## Runtime verification plan and current tooling limits

Existing guards and captures are useful. `harness.py` has a `boot` route that reaches and idles at the main-menu anchor. The `m3` and `m4a` golden checks cover the retail title and main menu. Capture clocks can use `feflow.xml#2` for the title and `getscreeninfo(intmain/mainmenu` for the menu; use observed anchors rather than absolute boot time.

Use supported CLI syntax such as `--env RECOMP_WS_ASPECT=1.7777778` or `--env RECOMP_WS_ASPECT=1.6`, and `--env RECOMP_TRANS_SHOT_NATIVE=1` for detail inspection. `RECOMP_TRANS_SHOT_WIDE=1` preserves canvas aspect at the game's 480-row height. **Default captures squeeze a wide canvas into 640x480** and are unsuitable for judging proportions. `RECOMP_PB_BATCH_DUMP=shot` can pair each capture with batch extents/texture information; `RECOMP_TEXTURE_DUMP_DIR` can correlate real source hashes.

The old widescreen test document's `harness --widescreen` argument and `regress --only widescreen` check are planned interfaces, not implemented in these current scripts. Use actual `--env` support until a dedicated UI/aspect matrix is added.

For each pilot screen, compare original/restored at 4:3,16:10,16:9, then explicit ultrawide fallbacks; review native pixels and a fixed-size overview. Capture the settled screen plus entrance/exit and representative moving-light/parallax frames. Check tile seams, alpha fringes, crop/geometry, logos and prompts, language variants, corner scissor, fades, door zoom and transitions to the next screen. Verify UI inputs and selection remain live. Verify Windows D3D11 and Mac/Deck Vulkan separately, since the existing side-fill implementations differ. Use disposable guarded saves and restore them identically. Confirm unmodified pack-off 4:3 goldens; renderer/loader changes also require focused fixtures and the repository's required full regression before a runtime commit.

Existing documentation records successful original/faithful-HD widescreen runs, but that is evidence that ordinary HD replacement and centered widescreen UI coexist. It does not validate new generated artwork, outpaint seams, layer mapping, full-width menu geometry or every menu. No new runtime validation is claimed by this report.

## Decisions to obtain before artwork production

- Restoration strength: strict original aesthetic with cleaned detail, or a more visibly redrawn high-resolution interpretation? A conservative first candidate gives a useful comparison baseline.
- Widescreen UI: preserve centered menu controls and extend only background art, or also move controls/header/footer outward? The first is the smaller initial scope.
- Target ceiling: 16:9/16:10 first with black/current-fill fallback for ultrawide, or outpaint to 21:9/32:9 in the first master? More invented side scenery requires more review.
- Preserve exact original branding/text/prompt sprites, or include separately reviewed hand-restored replacements? Image generation should not be trusted to redraw their lettering consistently.

## Documentation corrections identified

- Retail `prestart` should be the title pilot rather than demo-specific `demoStrt` in the element catalog.
- The early frontend report's mode-1/mode-2 mix-up has an erratum; implementation is authoritative.
- Several design sections still show the superseded game-side-emitter architecture; design section 0 and exact renderer source describe current placement.
- `side_fill=art`, dedicated harness widescreen arguments and a widescreen regression check remain proposals. Do not present them as ready-made integration points.
