# Pilot texture layout and UV evidence — 2026-10-17

The source supports the proposed **skyline order `2 | 1`** and **dark poster-wall order `5 | 4`**, with no flip or rotation. A 640×512 skyline join and a 320×512 poster-wall join are appropriate authoring references. Preserve their full source canvases, existing filler and exact baked lettering. There is a half-pixel authored overlap at the poster seam; retain the original split and let the original geometry handle it.

This was an offline metadata audit of the user’s own dump. No production source edit, build, game run, asset export or image edit was performed. The approved visual goal remains restrained cleanup, unchanged original color, centered content and side scenery, including 21:9 and 32:9 in the first pass.

## Evidence and confidence

**Direct observations:** APT bounds, ELF bounding boxes, the texture IDs referenced by each material relocation, each material’s 4×4 coordinate matrix, four packed vertex positions per submesh, and their relative edge ordering. The APT and ELF bounds agree exactly for both Shapes.

**Interpretation supported by independent consistency checks:** the coordinate matrices map Shape positions to pixel-space texture coordinates; their translations identify each tile’s source origin. An inferred unpacking formula also reproduces all bounds and seams to within 0.032 pixels, consistent with quantized source vertices. The packed-vertex formula has not been traced through a running renderer or its full guest shader path. The tile order and relative seam position do not depend on that formula: they are established by the material references, matrices and packed-edge equality/order.

No exact game screenshot or complete APT compositor was produced or claimed.

## Press Start skyline: Shape 3

Source container: `screens/screens.viv::screens/intMain/prestart.big`.

| Part | Bitmap name | SHPX directory index | Original canvas | Matrix translation | Nominal local origin |
|---:|---:|---:|---|---|---|
| 0 | 2 | 5 | 512×512 | `(0,163,0)` | `(0,-163)` |
| 1 | 1 | 0 | 128×512 | `(-512,163,0)` | `(512,-163)` |

Both matrices have identity linear components. Shape 3’s APT bounds are `(xmin=0,ymin=-163,xmax=640,ymax=287)`. Its ELF `__BBOX:::3` repeats the same bounds (z is zero).

The first quad’s right edge and the second quad’s left edge have **the identical packed x value, -9830**. This establishes a shared vertical seam. Both quads have identical packed top/bottom y values, -21036 and -8192. The left quad starts at packed x -16384; the right quad ends at -8192. Those values establish left/right order independently of image appearance.

Read the matrices as `u = local_x + translation_x`, `v = local_y + translation_y`: name 2 starts at local x 0; name 1 starts at local x 512; both start at local y -163. The full authored span is therefore 640×450 pixels. Nominal texture v runs from 0 to 450; each source canvas is 512 pixels high.

**Authoring reference:** paste the existing 512×512 image named 2 at `(0,0)` and the existing 128×512 image named 1 at `(512,0)` into a 640×512 canvas, without resizing, recoloring, alpha conversion or blending. This reference preserves all original source pixels. Mark rows 450–511 as source padding outside the nominal geometry; keep them available when splitting replacements. Rows 0–449 include authored black regions as well as scenery; those black regions must not be mistaken for automatic permission to repaint the center.

For the original root placement `(0,29)`, the layer’s nominal stage rectangle is x 0–640, y -134–316. This confirms that the texture canvas is not a 640×480 screenshot. The other title layers and animation complete the scene.

## Settled skyline: source pixels to the authored 640×480 stage

The composed **authored-coordinate transform is exactly `stage_x = source_x`, `stage_y = source_y - 134`**, where source x/y refer to the assembled 640×512 sheet (`2 | 1`). This is the prestart movie’s own stage, before any loader transition or host output fitting; it is a layer placement, not a full menu screenshot.

| Transform link | Scale/rotation | Translation |
|---|---|---|
| Name 2 source pixels → Shape 3 | identity | `(0,-163)` |
| Name 1 source pixels → Shape 3 | identity | `(512,-163)`; its pixels are already shifted +512 in the assembled sheet |
| Shape 3 → Sprite 4 | identity | `(0,0)` |
| Sprite 4 → prestart root stage | identity | `(0,29)` |
| Assembled sheet → prestart stage | identity | `(0,-134)` |

Sprite 4 has one frame, containing only a Shape 3 PlaceObject at depth 2 with matrix `(1,0,0,1,0,0)` and flags 6 (`HasCharacter|HasMatrix`). It adds no hidden pivot, scale, shear, rotation or translation. The root’s first frame places Sprite 4 at depth 1 with matrix `(1,0,0,1,0,29)`.

The root changes the skyline’s **color multiplier**, rather than its placement, during entrance: the initial multiplier is `(0,0,0,255)` with zero color addition; zero-based frame 27 reaches `(255,255,255,255)` with zero addition. The depth-1 Move records at frames 6–27 have flags 9 (`Move|HasColorTransform`), with no matrix present, so the original matrix is retained. No depth-1 geometry change occurs through zero-based frame 141. Thus frame 27 onward is suitable for the fully colored settled layer, including the normal idle Press Start state at frame 134 (one-based frame 135). Frame 133 stops for the title’s delay/timer; frame 134 introduces `startSticker` and ends in Stop. The skyline placement is identical in both states.

Exit motion starts at zero-based frame 142: root skyline y changes from 29 to about 29.55, then increases through 263 by frame 162. Do not use an exit frame as the placement reference. The title logo, foreground layers and sticker can have their own independent animations; their motion does not alter the skyline’s transform chain.

**Clipping for a settled preview:** nominal geometry is source x `[0,640]`, source y `[0,450]`, which lands at stage x `[0,640]`, y `[-134,316]`. Clipping to the 640×480 stage shows source rows `[134,450)` at stage rows `[0,316)`; an integer authoring-sheet crop `(left=0,top=134,right=640,bottom=450)` is therefore 640×316 and lands at stage `(0,0)`. Source rows 0–133 are above the stage at this settled placement; source rows 450–511 are outside the nominal quad, retained as padding. Stage rows 316–479 are not supplied by this skyline quad. Do not stretch the sheet or its visible portion to fill 480 stage pixels. Preserve the source canvas for restoration, and use the transform above for a scenery-layer preview.

These are exact APT/material matrix translations and timeline observations. Their pixel-center rasterization retains the tiny packed-geometry quantization already described below; this audit does not claim a bit-identical GPU raster or remove that quantization.

## Dark poster wall: Shape 6

Source container: `screens/screens.viv::screens/intMain/mainMenu.big`.

| Part | Bitmap name | SHPX directory index | Original canvas | Matrix translation | Nominal local origin |
|---:|---:|---:|---|---|---|
| 0 | 5 | 93 | 256×512 | `(153,254,0)` | `(-153,-254)` |
| 1 | 4 | 85 | 64×512 | `(-102.5,254,0)` | `(102.5,-254)` |

Both matrices have identity linear components. Shape 6’s APT bounds are `(xmin=-153,ymin=-254,xmax=166.5,ymax=226)`. The ELF `__BBOX:::6` agrees exactly.

The origin difference is **255.5 pixels**, rather than 256. The first quad’s packed right-edge x is -11316, while the second quad’s packed left-edge x is -11340; both share top/bottom y values -24576 and -9095. The second quad therefore starts slightly to the left of the first quad’s right edge: a small overlap, not a gap.

Interpreting the coordinate matrices gives first-tile source coverage at local x -153 through 103, and second-tile coverage at 102.5 through 166.5. Their nominal union is 319.5 pixels wide. Texture v runs from 0 through 480, with 32 extra source rows below that nominal geometry. Quantized vertices differ by tiny fractions of a pixel, so filtering at the boundary can involve adjacent source padding: preserve it.

**Authoring reference:** paste the existing 256×512 image named 5 at `(0,0)` and the existing 64×512 image named 4 at `(256,0)` into a 320×512 canvas. This integer-pixel join retains every source pixel and establishes the correct visual order. It is an authoring sheet, not an exact rasterization of the half-pixel-overlapped mesh. Do not rescale it to 319.5 pixels or shift the narrow strip by half a pixel and resample it. Split a restored reference back at x 256 into the original 256×512 and 64×512 canvases; unchanged runtime geometry preserves its authored seam behavior.

Keep the poster words, emblems and paper outlines registered to their original source pixels. The lit wall names 14/13 use a separate Shape; any restored counterpart should be derived from the same stable composition rather than generated independently. This bounded audit did not re-derive the lit pair’s geometry.

## Packed vertices: bounded consistency check

The vertex descriptor symbols are `__VB:::__GPvb_9_<part>_<shape>`. In all four descriptors, the fields at +4 and +8 contain 24 and 6. The pointer at +16 addresses a 24-byte block that fits four consecutive XYZ signed-16-bit triples with a 6-byte stride; z is zero. The field names themselves were not traced through the loader. These are quads in a triangle-strip material, not an arbitrary mesh.

Each `__Model:::<shape>` also contains vectors at offsets +0x4C and +0x5C. The first is twice the second in x/y:

| Shape | Vector at +0x4C | Vector at +0x5C |
|---|---|---|
| prestart 3 | `(2560,1148,0,1)` | `(1280,574,0,0)` |
| mainMenu 6 | `(666,1016,0,1)` | `(333,508,0,0)` |

The inference `position_axis = (packed_axis + 16384) × vector_at_0x5C_axis / 16384` yields:

| Quad | Inferred x min/max | Inferred y min/max | Matrix-derived pixel UV bounds |
|---|---|---|---|
| prestart name 2 | `0 / 512.031250` | `-162.979004 / 287` | u `0 / 512.031250`; v `0.020996 / 450` |
| prestart name 1 | `512.031250 / 640` | `-162.979004 / 287` | u `0.031250 / 128`; v `0.020996 / 450` |
| mainMenu name 5 | `-152.984070 / 103.005615` | `-254 / 226.001709` | u `0.015930 / 256.005615`; v `0 / 480.001709` |
| mainMenu name 4 | `102.517822 / 166.5` | `-254 / 226.001709` | u `0.017822 / 64`; v `0 / 480.001709` |

This formula explains the expected pixel coordinates and subpixel quantization. It is recorded as an inference, not a verified runtime specification. None of these fractional values is a reason to alter the source references before image generation.

## Byte-offset provenance

Offsets below are relative to the embedded ELF `.data` section, which begins at file offset 0x40 in both objects. They identify the metadata inspected without retaining any raw game bytes.

| Resource | Shape bbox | Model | Material 0 / 1 | Coordinate matrix 0 / 1 | Vertex descriptor 0 / 1 | Vertex data 0 / 1 |
|---|---|---|---|---|---|---|
| prestart | `0x1B40` | `0x1D0C` | `0x187C / 0x1AAC` | `0x16E0 / 0x1910` | `0x1780 / 0x19B0` | `0x1794 / 0x19C4` |
| mainMenu | `0x17B80` | `0x17D4C` | `0x178BC / 0x17AEC` | `0x17720 / 0x17950` | `0x177C0 / 0x179F0` | `0x177D4 / 0x17A04` |

Each material’s relocation at +0x48 names its image through `SHAPENAME=<name>,1`; its word at +0x58 points to the coordinate matrix. The original source Image/bitmap identities are also tabulated in `menu-asset-mapping-2026-10-17.md`.

- `prestart.o` SHA-256: `b0c5ffff2e301ce78d8f9158ad93c3db47f6e63d08e1b6ef3d089324ba2a2be4`
- `prestart.apt` SHA-256: `6721681dd039100fd2fc33ede7427083d62eba5f2008481cb3b17d5108d81e55`
- `mainMenu.o` SHA-256: `3141dbf8c8d8663e585c855362733e80540af0302c5335cc96ecbd313bb05df9`
- `mainMenu.apt` SHA-256: `d1183e6673b8f3eac688c4df0d4bdf366dc99fee01c8ae3b8464ba91c8320549`

Reproducible local scratch: `logs/ui-restoration-20261017/uv-audit/audit_pilot_uv.py`; its metadata-only output is `pilot-geometry-metadata.json` beside it. The script asserts agreement of APT and ELF bounds and the skyline’s shared seam. The logs directory remains local and ignored.

## What remains for later renderer validation

The joins are sufficient for faithful restoration references. They do not establish how to expose generated scenery outside original geometry at wider aspect ratios. That still requires the selected renderer/geometry approach, centered safe areas, masking of filler, and in-game checks at 4:3, 16:9, 16:10, 21:9 and 32:9. No new color treatment, center cropping or texture stretching is justified by this source-layout audit.
