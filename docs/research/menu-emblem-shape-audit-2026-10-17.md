# Main-menu emblem shape audit — 2026-10-17

Offline bounded read of the owner's dump. This report maps the actual selected
mode artwork attached to the rolling shutter, rather than treating the six
support-resource icons as the complete set. No production code, build, game run,
image generation/export or workshop writer change occurred. Existing native
corpus PNGs were read for channel statistics and visual inspection.

## Evidence

Read `screens/intMain/mainMenu.big` and
`screens/intMain/support/mainMenuLogosIcons.big` from
`<data>/extracted/screens/screens.viv`. Used existing `hd_assets.py`,
`apt-dump.py`, and `uv-audit/audit_pilot_uv.py`'s ELF decoder. Ignored evidence:
`logs/ui-restoration-20261017/emblem-audit/audit.py` and `metadata.json`.
Metadata contains the full source IDs, original paths, matrices, decoded
positions, UV coordinates and alpha counts; no game bytes or images are embedded.

| Resource | SHA-256 of embedded object / APT |
|---|---|
| mainMenu.o | `3141dbf8c8d8663e585c855362733e80540af0302c5335cc96ecbd313bb05df9` |
| mainMenu.apt | `d1183e6673b8f3eac688c4df0d4bdf366dc99fee01c8ae3b8464ba91c8320549` |
| mainMenuLogosIcons.o | `b18e4ee301751955e3698e7922497f158bf20e334c915d5fc57c10d3f94487d1` |
| mainMenuLogosIcons.apt | `705da9f6fcd16ad30ffeacf6d5645e76e248235f13eaf923fa126a11ecd31fc0` |

APT Shape bounds and ELF material mapping supply the nominal dimensions. Packed
quad decoding uses the previously documented inferred formula
`(signed_component+16384)*model_scale2/16384`; results agree with nominal bounds
and seams to about0.013pixels. Texture matrices are identity plus translation,
so UVpixel=localXY+translation. Fractional quantization is not a reason to resize
or round each fragment independently. This audit does not claim a GPU raster or
runtime shader/blending validation.

## Actual eight mode states

Sprite222's `door` is Sprite51. Sprite51 frame0 places Sprite50 at depth4,
translation(-15,-86), named `doorLogos`. Sprite50 places the following Shapes
with identity matrices. These are Shapes directly, not intermediate Sprites.
All frame numbers below are zero-based.

| Frame / label | Shape / depth | Source Image names by part0,1,2 | APT bounds xmin,ymin,xmax,ymax | Active width×height |
|---|---:|---|---|---|
| 0 mmStory |24 /3|23,22,21|−68,−68,99,89|167×157|
| 1 mmBattle |28 /3|27,26,25|−78,−70,89,102|167×172|
| 2 mmOnline |none|none|removes depth3|none|
| 3 mmOptions |32 /3|31,30,29|−35.8,−69,103.2,70|139×139|
| 4 mmJukebox |35 /2|34,33|−61,−69,67,79|128×148|
| 5 mmExtras |38 /2|37,36|−64,−62,108,62|172×124|
| 6 mmHighScores |41 /2|40,39|−14,−76,114,99|128×175|
| 7 mmCreatePlayer |none|none|removes depth2|none|
| 8 mmRewards |45 /3|44,43,42|−52.75,−67.4,106.25,83.6|159×151|
| 9 mmCredits |49 /3|48,47,46|−64.8,−72,92.2,98|157×170|

These10labels reference8actual artworks /21bitmap fragments. Online and
CreatePlayer are not additional blank textures to restore, and the labels do
not prove retail feature availability. In Sprite50, frames1/5/6/9 replace the
prior Shape at an existing depth without supplying a new matrix, retaining the
identity transform. Frames2/4/7 explicitly remove the previous depth where
appropriate. Door animation moves the emblem independently upward along with
the shutter; its initial(-15,-86) is not a permanent screen coordinate.

## Source joins and active rectangles

Origin is the top-left of each assembled artwork, equivalent to subtracting
that Shape's APT xmin/ymin from local positions. Source rectangles use exclusive
right/bottom bounds and nominal pixel units. Every rectangle starts source(0,0).
The bottom-right region of three-piece joins has no submesh and must remain
transparent in an authoring sheet. Preserve full stored tile canvases when
splitting replacements; do not stretch active content into padding.

| Mode | Image name: stored size → active source size at assembled origin |
|---|---|
| Story |23:128×128→128×128 at(0,0);22:64×128→39×128 at(128,0);21:128×32→128×29 at(0,128)|
| Battle |27:128×128→128×128 at(0,0);26:64×128→39×128 at(128,0);25:128×64→128×44 at(0,128)|
| Options |31:128×128→128×128 at(0,0);30:16×128→11×128 at(128,0);29:128×16→128×11 at(0,128)|
| Jukebox |34:128×128→128×128 at(0,0);33:128×32→128×20 at(0,128)|
| Extras |37:128×128→128×124 at(0,0);36:64×128→44×124 at(128,0)|
| High Scores |40:128×128→128×128 at(0,0);39:128×64→128×47 at(0,128)|
| Rewards |44:128×128→128×128 at(0,0);43:32×128→31×128 at(128,0);42:128×32→128×23 at(0,128)|
| Credits |48:128×128→128×128 at(0,0);47:32×128→29×128 at(128,0);46:128×64→128×42 at(0,128)|

Source-preserving integer authoring canvases may be192×160,192×192,144×144,
128×160,192×128,128×192,160×160,160×192 respectively, with unused texels retained
and active rectangles overlaid. Such padded authoring sheets are not the active
Shape rectangles. For production, keep each original tile's dimensions and
identity, and retain exact split coordinates128. No overlap or rotation was
found in these joins.

## Six support icons are separate art

The support movie is800×600, one frame, presenting multiple independently
placed icons. Six bitmap Shapes have a single256×256 source each. Their IDs
are all disjoint from the21actual door-emblem fragment IDs. The native images
are not higher-resolution copies of the assembled door art: the support set
uses mostly flat white distressed symbols; actual door art is shaded metal-like
illustration with different silhouettes/compositions. Shared motifs such as VIP,
fist and disc do not establish texture reuse or coordinate compatibility.

| Support Image / Shape | Sampled source width×height | Source-ID prefix | Visual motif |
|---|---|---|---|
|11 /12|180×171|`e9ad5bba3390`|VIP triangle|
|14 /15|152×139|`14c02269ec35`|clenched fist|
|17 /18|191×203|`a6e7c14ecee4`|fist with lightning bolt|
|20 /21|150×164|`af5cb7be674f`|rectangular divided paper/panel|
|23 /24|184×139|`0024896ced50`|record/disc with projecting tab|
|26 /27|165×157|`9f93608748ab`|exclamation triangle|

Their entire256square is not sampled by these Shapes. Name11 alpha never
exceeds212; the others reach255. Do not normalize11to opaque. This bounded
audit does not establish which retail screens import every support icon;
restoring them would not replace any actual Sprite50 door emblem by identity.
The current batch should prioritize8actual emblems. Six support icons can be
reviewed as separate optional candidates without claiming active-path coverage.

## Alpha/RGB encoding and restoration implications

All21actual fragments and6support bitmaps are SHPX type0x62, NV2Aformat0x0F,
DXT5, non-swizzled in the fresh census. They decode to ordinary RGBA. Actual
fragments collectively contain100,410alpha-zero,48,942partial-alpha and
55,448fully opaque pixels; every fragment spans alpha0..255. 33,327partial-alpha
pixels have at least oneRGB channel greater than alpha, so these decoded PNGs
are not premultiplied RGB data. Preserve straight RGB plus independent alpha;
do not unpremultiply the originals or treat luminance as coverage.

Many source canvases contain strong green RGB where alpha is zero or very low.
The actual-emblem green test(G>150,R/B<70) spans alpha0..19; support green spans
alpha0..29. Transparent source RGB is neither universally black nor uniformly
one green value. This is not permission to key green: DXT5 already carries the
coverage, and keying would change soft edges/intentional distress. RGB-only
inspection exaggerates filler. For authoring, show true-alpha composites against
several backgrounds as well as rawRGB/alpha separately. Filter/resize in
premultiplied space or use the existing opacity-aware bleed path, then retain
the intended separate alpha; do not expose hidden green through naïve RGB-only
interpolation.

Preserve local palette, existing gray/cream shading, antialiased silhouette,
internal cutouts and distressed paint. The visible art is not exactly grayscale;
do not force white or tint it warm. Use source-aligned restoration with protected
shape/text boundaries, then split at original coordinates. Any generated detail
should be constrained to safe interior tonal variation and judged against the
assembled original before being propagated to all fragments. Wider scenery
should not horizontally stretch these menu symbols: retain authored logo scale,
pivot, placement and animation at4:3/16:10/16:9/21:9/32:9.
