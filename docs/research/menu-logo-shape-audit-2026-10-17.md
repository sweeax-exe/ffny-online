# Retail title logo and Press Start shape audit — 2026-10-17

Read-only audit of `screens/intMain/prestart.big` in the owner's own
`screens/screens.viv`, plus retained corpus originals. No game run, build,
asset write, workshop operation or art generation. Only this report and local
metadata/scripts were written. All eight retained PNG file hashes match the
corpus, and their decoded RGBA bytes independently match fresh SHPX decoding.
Evidence/replay: ignored `logs/ui-restoration-20261017/logo-audit/audit.py`
and `metadata.json`. The existing pilot ELF reader supplies `object_info`.

## Main findings

The actual title wordmark is **raster image name19**, drawn by shape20 as a
four-vertex textured triangle strip. Its APT `Shape` type does not mean the
letters are vector outlines. The retail resource contains only `.apt`,
`.const`, `.o` and `.xsh`; the `.o` contains that quad and its material/UV
references, not editable glyph curves. No `.svg`, `.ai`, `.eps` or `.pdf`
assets were found among visible files in this recomp checkout or the authoring
tools checkout. This bounded search does not establish whether original EA
production files exist elsewhere.

`branding/masters/wordmark.png` is a separate raster derivative of
`misc.viv::nis_logo.xsh` (see `branding/README.md` and
`scripts/make-branding.py:97`), with transparent lettering intentionally filled
for launcher/Steam readability. It is **not** an editable vector source or a
pixel-faithful replacement for retail prestart name19. Do not import its fill
changes into the game logo by accident.

## Encoding and alpha

Every audited image uses SHPX type98 (`0x62`, DXT5), NV2A identity format15
(`0x0f`), unswizzled. These are colored RGBA images with independent, graded
alpha—not monochrome RGB plus a glyph-only alpha mask.

| Image name | SHPX index | Stored size | Nonzero-alpha bbox (exclusive right/bottom) | Alpha zero / opaque / partial | Distinct visible RGB colors |
|---|---:|---|---|---|---:|
| 19 | 4 | 512×128 | [1,1,507,88] | 27402 / 16163 / 21971 | 1435 |
| 27 | 6 | 8×64 | [0,8,5,40] | 382 / 34 / 96 | 30 |
| 28 | 7 | 128×64 | [3,0,128,40] | 3679 / 3656 / 857 | 531 |
| 30 | 8 | 64×64 | [0,0,38,40] | 2761 / 1033 / 302 | 233 |
| 31 | 9 | 128×64 | [0,0,128,40] | 3561 / 3823 / 808 | 601 |
| 34 | 10 | 64×64 | [0,0,64,40] | 2113 / 1582 / 401 | 320 |
| 35 | 11 | 128×64 | [0,0,128,40] | 3532 / 3819 / 841 | 611 |
| 37 | 12 | 256×64 | [0,0,244,40] | 7506 / 7341 / 1537 | 1010 |

Logo19 uses every alpha level0–255. Its visible RGB includes authored white,
gray and dark brown; preserving the original color means retaining those
values rather than neutralizing them. Transparent padding in several files
contains saturated green/red/yellow RGB (e.g. 24,336 green RGB texels in logo19,
all absent from its visible RGB top colors). These colors are filler, not a
chroma-key instruction. Materials explicitly specify SRCALPHA/INVSRCALPHA
blending and clamp U/V.

Preserve the independent original alpha and RGB design. Resize in
premultiplied RGBA (RGB×alpha and alpha with the same filter, then guarded
unpremultiplication), or use the established faithful baseline. Straight-RGB
filtering would pull transparent filler into edges; thresholding alpha would
remove authored antialiasing, distress and translucent marks. Use source alpha
as the authority when constraining any generated interior detail. Do not let
an RGB-only generation repaint glyph silhouettes or assume all lettering is
represented solely by alpha. Keep the actual warm outline values, but do not
apply a global warm color shift.

## Sampled rectangles and joins

Coordinates below are nominal bounds independently agreeing between APT and
ELF `__BBOX` records. Packed vertex decoding introduces at most0.026pixel
rounding; preserve that mesh for runtime. Each part has four vertices/stride6.
The UV matrix is identity plus the stated translation; rectangles use original
source pixels before HD scaling.

| Shape | Nominal local rectangle | Ordered source parts | Nominal UV rectangle per part |
|---|---|---|---|
| 20, title wordmark | x−230.5..277.5, y−41..47 | 19 | [0,0,508,88] |
| 29, Xbox sticker variant1 | x0..134, y0..40 | 28 then27 | 28:[0,0,128,40];27:[0,0,6,40] |
| 32, Xbox sticker variant2 | x−12..155, y0..40 | 31 then30 | 31:[0,0,128,40];30:[0,0,39,40] |
| 36, PS2 sticker variant1 | x0..183, y0..40 | 35 then34 | 35:[0,0,128,40];34:[0,0,55,40] |
| 38, PS2 sticker variant2 | x−29..215, y0..40 | 37 | [0,0,244,40] |

Thus name27 is the **right-hand six-pixel tail**, not a left cap. Name30 is a
right tail at localx116; name34 is a right tail at localx128. Joining simply
by filename order or full stored widths would be wrong. Crop active UV height40
for every sticker; logo active height88, width508. Retain stored texture
canvases/padding in runtime replacements instead of changing original geometry.

## Stable placement and variant selection

All frame numbers here are **zero based**, with no runtime timing claim.

- Root places logo sprite22 at frame59, depth11, translation(203,129.5).
  Sprite21 contains shape20 at identity. In sprite22, the persistent main copy
  at depth3 settles at translation(99.95,42) by its frame43; extra skewed/scaled
  copies briefly appear at depth1 and are removed. Nominal settled main-logo
  stage rectangle is therefore approximately [72.45,130.5,580.45,218.5].
  Keep that authored horizontal placement; do not independently recenter its
  opaque bbox. Root frame142 begins alpha changes on this layer.
- Root places sprite41 named `startSticker` at frame134, depth15,
  translation(321,262). Sprite41 animates scale/rotation/jitter around sprite40,
  then has identity at its own frame29 and stops; it is not a permanently
  enlarged rectangle. The root also changes its alpha during transition.
- Sprite40 selects console-labelled frames `xbox`, `gc`, `ps2`. Xbox places
  sprite33 at(−65,−14); sprite33 uses shape29 in frame0 and shape32 in frame1,
  each with Stop. PS2 places sprite39 at(−93.5,−13); it analogously uses shapes36
  and38. Script checks `Game.GetLanguage()` and chooses the child sticker frame:
  language values1/2 use frame1, value3 uses frame2. No language names inferred.
- In the identity state, Xbox variant1 nominal rectangle is
  [256,248,390,288]; variant2 is [244,248,411,288]. PS2 counterparts are
  [227.5,249,410.5,289] and [198.5,249,442.5,289]. Preserve all variants and
  their original anchors; do not replace localized texture pairs with one new
  text rendering. Keeping logos/emblems in the original centered640×480 UI
  coordinate system avoids stretching them across widescreen scenery.

These placements are for source-aligned preview composition. They omit alpha
animation, ActionScript/runtime state and transient copies; validate actual
presentation in game before declaring runtime acceptance.

## Resource fingerprints

| Member | SHA-256 |
|---|---|
| prestart.apt | `6721681dd039100fd2fc33ede7427083d62eba5f2008481cb3b17d5108d81e55` |
| prestart.o | `b0c5ffff2e301ce78d8f9158ad93c3db47f6e63d08e1b6ef3d089324ba2a2be4` |
| prestart.xsh | `2bd96a20f2d4cddca1586f113501b5a02f45142c2eeb447878a96231b15977f2` |

All eight individual source IDs/PNG hashes, complete material matrices,
vertex/UV coordinates and alpha counts are in private `metadata.json`.
