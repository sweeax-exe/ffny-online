# Remaining Press Start background layers — 2026-10-17

For the settled retail Press Start title at **zero-based frame 134**, the near-city sheet is `6 | 5` with **stage `(x,y)=(source_x,source_y+102)`**. The foreground tower/roof sheet is `11 | 10` with **stage `(x,y)=(source_x,source_y+16)`**. Both authoring sheets are 640×512, but their nominal geometry heights are **310** and **446**, respectively. Their unused bottom rows are opaque filler: clip to the nominal heights before compositing a preview.

This is a read-only metadata investigation of the user’s own dump. No artwork was generated, transformed or exported; no production code edit, build or game run occurred. The approved skyline master was not modified or re-audited. This report extends the coordinate method in `menu-pilot-uv-layout-2026-10-17.md` to the two remaining image layers.

## Verified data and interpretation

Directly observed facts are the APT Shapes and PlaceObjects, frame timelines and color transforms; matching ELF bounding boxes; image names referenced by ELF material relocations; identity linear coordinate matrices and their translations; and the equality/order of each pair’s packed vertex edges. The APT and ELF bounds agree exactly for both Shapes. All four original PNG hashes match the existing corpus, and their complete RGBA pixels match a fresh in-memory decode of the raw SHPX entries byte for byte. No decoded image was saved.

The material coordinate interpretation follows the pilot audit: `u=local_x+matrix_tx`, `v=local_y+matrix_ty`, in source pixel units. The established packed-vertex unpacking formula remains an inference, not a traced runtime specification. It predicts x 512.03125 at the shared seam versus the nominal x 512, explaining tiny quantization error; it is not a reason to resample the sources. The tile order and offsets do not rely on that formula alone.

The stage coordinates below are the prestart movie’s own 640×480 authored stage before loader transitions, output fitting or any new widescreen integration. They describe individual image layers, not a complete menu capture.

## Near city: Shape 7, Sprite 8, root depth 4

| Material part | Bitmap name | SHPX index | Original canvas | Coordinate-matrix translation | Nominal local origin |
|---:|---:|---:|---|---|---|
| 0 | 6 | 14 | 512×512 | `(0,0,0)` | `(0,0)` |
| 1 | 5 | 13 | 128×512 | `(-512,0,0)` | `(512,0)` |

Both coordinate matrices have identity linear components. Shape 7 has APT bounds `(0,0,640,310)`; `__BBOX:::7` contains `(0,0,0,640,310,0)`. The left quad’s right-edge packed x and right quad’s left-edge packed x are the same value, **-9830**. Their packed y range is **-16384 to -8192**. There is a shared vertical seam with no evidence of a poster-wall-style half-pixel overlap, flip, rotation or scale.

Authoring reference: name 6 at `(0,0)` plus name 5 at `(512,0)` in a 640×512 sheet. Retain the original source pixels, alpha and bottom padding. Nominal active geometry is sheet rectangle **`(0,0,640,310)`**. The remaining **202 rows, y 310–511**, are outside the nominal quad.

Sprite 8 has a single frame: Shape 7 at depth 2 with matrix `(1,0,0,1,0,0)`, no clip action and no additional transform. The root places Sprite 8 at depth 4 with matrix `(1,0,0,1,0,102)`. At frame 134 its color multiplier is `(255,255,255,255)`, with `(0,0,0,0)` addition. Therefore:

```
source sheet → Shape 7:  identity
Shape 7 → Sprite 8:     identity
Sprite 8 → stage:       translate(0,102)
stage_x = source_x
stage_y = source_y + 102
```

The settled nominal stage rectangle is **x `[0,640]`, y `[102,412]`**. For a registered scenery-layer preview, crop the assembled sheet to `(0,0,640,310)`, preserving RGBA, then place it at `(0,102)` over the approved skyline layer. Do not stretch its 310-pixel height to fill the stage. Other authored nonimage/foreground layers may occlude parts of this rectangle in the complete title.

Timeline: the root’s RGB multiplier fades from zero with alpha multiplier held at 255. Full `(255,255,255,255)` multiplication is reached at **zero-based frame 34**, with no geometric change during that entrance. Matrix `(0,102)` remains in force through frame 141. Exit movement begins at frame 142 with y ≈102.45, increases through 304 at frame 162, and then remains there in the authored timeline. At frame 134 the original settled placement is unchanged.

## Foreground tower and roof: Shape 12, Sprite 13, root depth 8

| Material part | Bitmap name | SHPX index | Original canvas | Coordinate-matrix translation | Nominal local origin |
|---:|---:|---:|---|---|---|
| 0 | 11 | 2 | 512×512 | `(0,0,0)` | `(0,0)` |
| 1 | 10 | 1 | 128×512 | `(-512,0,0)` | `(512,0)` |

Both coordinate matrices have identity linear components. Shape 12 has APT bounds `(0,0,640,446)`; `__BBOX:::12` contains `(0,0,0,640,446,0)`. The seam again shares packed x **-9830** across both quads, with the same y range **-16384 to -8192**. There is no extra rotation, flip or authored half-pixel overlap.

Authoring reference: name 11 at `(0,0)` plus name 10 at `(512,0)` in a 640×512 sheet. Nominal active geometry is **`(0,0,640,446)`**. Retain the **66 bottom padding rows, y 446–511**, in texture source/export canvases, but exclude them from the preview compositor.

Sprite 13 has a single frame: Shape 12 at depth 2 with matrix `(1,0,0,1,0,0)`, no clip action and no additional transform. The root places Sprite 13 at depth 8 with matrix `(1,0,0,1,0,16)`. Its frame-134 color multiplier is `(255,255,255,255)`, with `(0,0,0,0)` addition. Therefore:

```
source sheet → Shape 12: identity
Shape 12 → Sprite 13:   identity
Sprite 13 → stage:      translate(0,16)
stage_x = source_x
stage_y = source_y + 16
```

The settled nominal stage rectangle is **x `[0,640]`, y `[16,462]`**. For a layered preview, crop to `(0,0,640,446)`, preserving RGBA, then place at `(0,16)` above the city/skyline according to root depth. It is not a stage-filling rectangle; transparent regions must show the lower layers.

Timeline: full RGB multiplication is reached at **zero-based frame 29**. There is no root matrix update or removal at this depth anywhere in the 165-frame movie after the initial placement, and the child sprite has no independent timeline motion. Thus its local stage placement is fixed for the entire authored movie, including frame 134 and the root’s later exit frames. This statement does not rule out movement of the whole movie by its external loader.

## Alpha and filler: preserve silhouettes, clip geometry

The originals use real alpha with partial edge coverage; they are not opaque paintings. Counts below cover each **whole 512-high source canvas**, including opaque padding.

| Bitmap | Alpha 0 pixels | Partial-alpha pixels | Alpha 255 pixels | Nonzero-alpha bounds inside nominal geometry |
|---:|---:|---:|---:|---|
| 6 | 22,062 | 1,806 | 238,276 | `(0,2,512,310)` |
| 5 | 4,494 | 452 | 60,590 | `(0,21,128,310)` |
| 11 | 119,023 | 1,580 | 141,541 | `(0,1,512,446)` |
| 10 | 46,490 | 201 | 18,845 | `(0,340,128,446)` |

Bounds use the usual left/top inclusive, right/bottom exclusive image convention. The tower’s narrow right strip is transparent above y 340; at settled placement its first nonzero-alpha content begins at stage y 356. It must not be filled with a guessed sky or background color merely because the individual PNG appears mostly empty.

Every padding pixel in all four images has alpha **255**:

- City name 6, y 310–511: a repeated source row containing only RGBA `(5,2,0,255)` and `(2,1,0,255)`; it is also identical to row 309.
- City name 5, y 310–511: solid RGBA `(2,1,0,255)`, identical to row 309.
- Tower name 11, y 446–511: solid RGBA `(0,0,0,255)`, identical to row 445.
- Tower name 10, y 446–511: almost entirely `(0,0,0,255)`, with 128 pixels `(10,5,0,255)` and 4 pixels `(11,5,2,255)` across the padding. Preserve these source bytes; they are not scene content to expose.

Do not alpha-composite complete 640×512 sheets onto the stage: opaque filler would cover the lower image layers beyond the authored quad. Keep full sheets for generation provenance/splitting and enforce the 310/446 geometry crops in the compositor. Preserve existing center alpha exactly for a faithful-center audition, including partial edge coverage, and require generated side scenery to retain an appropriate independent alpha silhouette. Do not treat the transparent regions as black paint.

For a 4× reference compositor, the corresponding crops and placements are city **2560×1240 at `(0,408)`**, tower **2560×1784 at `(0,64)`**, on a 2560×1920 original-aspect stage. A wider stage adds the same horizontal centering offset to both layers; their vertical coordinates and scales stay registered. The original 640×480 center should not be stretched across the wider viewport.

## Provenance and reproducible local metadata

All inspected resources are from `screens/screens.viv::screens/intMain/prestart.big`. ELF offsets below are relative to its `.data` section, which starts at file offset 0x40.

| Resource | APT/ELF shape bbox | Model | Materials 0 / 1 | Matrices 0 / 1 | Vertex descriptors 0 / 1 | Vertex data 0 / 1 |
|---|---|---|---|---|---|---|
| Shape 7 | APT `(0,0,640,310)` / ELF `0x34D0` | `0x369C` | `0x320C / 0x343C` | `0x3070 / 0x32A0` | `0x3110 / 0x3340` | `0x3124 / 0x3354` |
| Shape 12 | APT `(0,0,640,446)` / ELF `0x460` | `0x630` | `0x1A0 / 0x3D0` | `0x0 / 0x230` | `0xA0 / 0x2D0` | `0xB4 / 0x2E4` |

The vertex vectors at Model +0x5C are `(1280,620,0,0)` and `(1280,892,0,0)`, respectively. Under the previously documented unpacking inference, they yield nominal y 0–310 and 0–446 exactly, while the shared quantized x seam is 512.03125. Exact geometry/UV matrices retain their original subpixel behavior; the authoring references use the original integer x 512 split.

Source IDs:

- Name 6: `fdc3666e5aec15824bd2cb3fb8598f2f7047980c696c494035d019e7036680ba`
- Name 5: `99aa2410ae53d3edc92c89fbecf01d1adf5eab12b2d613338de66f0f03a92a33`
- Name 11: `427424b7b670739f1a493dff7e53725299f72c718c7680aca3314b76247ca5ad`
- Name 10: `682f298f59c1680b2bc7118a4768d685fa41dd16eb684b67cbfbf9077036c4e4`

Local scratch probe: `logs/ui-restoration-20261017/batch2/title_layer_metadata.py`. Metadata-only receipt: `title-layer-metadata.json` beside it. Run with the existing standalone tools Python at `/Users/vlad/Code/DefJam-Modding-Tools/.venv/bin/python`. It reuses the previous parser, asserts APT/ELF bbox agreement and exact shared packed seams, verifies source PNG hashes, compares decoded SHPX RGBA bytes, and records all layer timeline updates plus the effective frame-134 state. It writes no artwork. Both scratch files remain ignored and local.

These findings are sufficient for registered layered scenery previews and faithful source splitting. They do not establish a complete APT visual recreation, wide-geometry integration, or runtime acceptance. Those remain the main agent’s subsequent implementation and game-validation work.
