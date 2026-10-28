# Remaining main-menu environmental layers — 2026-10-17

Bounded offline metadata audit of the owner's dump for the menu art batch.
Targets: lit poster wall Shape15 (Image names14/13), rolling shutter Shape19
(18/17), and door surround Shape54 (53/52). Dark wall Shape6 is referenced from
the existing audit and was not decoded again. No source/runtime edits, builds,
game runs, image generation/export, or workshop writer8768 changes occurred.

## Method and evidence

Used the existing ELF/APT decoding helpers in
`logs/ui-restoration-20261017/uv-audit/audit_pilot_uv.py` and the existing
`/Users/vlad/Code/DefJamFFNY-recomp/scripts/apt-dump.py`. Read only the
`screens/intMain/mainMenu.big` embedded `.o`, `.apt` and related constants, plus
the retained original PNGs in the private faithful corpus.

Resource object SHA-256:
`3141dbf8c8d8663e585c855362733e80540af0302c5335cc96ecbd313bb05df9`.
APT SHA-256:
`d1183e6673b8f3eac688c4df0d4bdf366dc99fee01c8ae3b8464ba91c8320549`.

For all three Shapes, the APT rectangle exactly equals the ELF bounding box's
XY bounds. Each of the two texture parts has four compressed XYZ vertices,
24bytes at stride6; its material texture relocation identifies the Image name.
The inferred compressed position formula is the existing audit's
`(signed_component + 16384) * model_scale2 / 16384`. All texture matrices here
are identity plus translation; UV pixel coordinates are local XY plus that
translation. Geometry quantization is generally a few hundredths of a pixel.
The ideal authored bounds below are preferable to rounding every encoded
vertex into a new, independently distorted rectangle.

Ignored metadata evidence (no saved source bytes or PNGs):

- `logs/ui-restoration-20261017/batch2/mode-layer-metadata.json`
- `logs/ui-restoration-20261017/batch2/mode-pixel-metadata.json`
- `logs/ui-restoration-20261017/batch2/mode-actions-metadata.json`
- `logs/ui-restoration-20261017/batch2/mode-placement-metadata.json`

## Join orientation and sampled bounds

Image names are SHPX four-character names, not directory indices. “Visible”
below means sampled by this Shape before APT clipping/occlusion. It does not
mean every texel is visible in the final 640×480 frame.

| Shape / source | Source image | Sampled source rectangle, nominal pixels | Placement in assembled source plane |
|---|---|---|---|
|15 part0, name14|256×512|x0..256,y0..480|x0,y0|
|15 part1, name13|64×512|x0..64,y0..480|x256,y0|
|19 part0, name18|256×256|x0..230,y0..256|x0,y0|
|19 part1, name17|256×128|x0..230,y0..98|x0,y256|
|54 part0, name53|512×512|x0..449,y0..480|x0,y0|
|54 part1, name52|64×512|x0..64,y0..480|x448.5,y0|

**Lit wall:** join14→13 horizontally, source plane320×480. Shape bounds are
`[-153,-254]..[167,226]`; matrices translate by `(153,254)` for14 and
`(-103,254)` for13. Both image bottoms y480..512 are outside the quad.
Actual inferred seam x=103.0092 in Shape coordinates; the two parts share it.

**Shutter:** join18→17 vertically, source plane230×354. Shape bounds are
`[-114.5,-172.5]..[115.5,181.5]`; matrices translate by `(114.5,172.5)` and
`(114.5,-83.5)`. The join is local y83.5 (inferred83.5051). Columns230..256 of
both images and rows98..128 of17 are outside the quad. A naïve256×384 join
would include26unused columns and30unused bottom rows and misstate the frame.

**Surround:** join53→52 horizontally, source plane512.5×480, with a half-pixel
overlap. Shape bounds are `[-233.5,-240]..[279,240]`; matrices translate by
`(233.5,240)` and `(-215,240)`. The first part's inferred right edge is
215.5166; second part's left is215.0057. Thus source53 contributes only about
449columns even though stored512wide; source52 starts at common x448.5, not512.
At4x the intended half-pixel offset is exactly2output pixels. Keep the original
part mapping/overlap instead of stretching a naïve576wide contact-sheet join.
Both images' bottom32rows are outside the quad.

The source IDs are:

| Image name | Source ID |
|---|---|
|14|`44ac477e5bbedc831511b189da8e581c4beb01646ea12f119719fceb02482693`|
|13|`2fca375e9466d772de13fa5cc53013e90d063ba8aa87c20d16a052b437aeccbb`|
|18|`5e6f2fec0b51311c14bea415934548dd946081c270abdfb4834746ffe633abff`|
|17|`de1deb64a25f6ae1821fa0837bf2dd452b4dc27fd0a51bcadcaa28d3ea214260`|
|53|`d543121c2fd1cada431cf25345d9d8efa1f25a02992f19cf49b49fa66f15fee6`|
|52|`832e9d0b1495f399e2d53408d3292b603a591ec6c927abd4b0bb1c9a00e07be4`|

## Stable APT placement

Use a zero-based frame index when comparing with parser output. The outer movie
frame1 bytecode explicitly calls `mainMenu.mainMenu.gotoAndStop(2)` and then
plays `mainMenu`. These are Sprite16's lit-wall frame1 and Sprite222's advancing
scene. Sprite222 frame1 updates selection text/emblem and spotlight motion,
then stops (`0x119F3`). Its frame0 also stops (`0x1193C`). Sprite51's closed-door
frame0 stops (`0x10AF8`). These give an authored idle arrangement; they are
offline bytecode evidence, not an observed runtime frame.

Outer Sprite222 transform is
`[sx=0.9998626708984375, sy=1, tx=316.2000122, ty=239.8999939]`.
The initial transforms persist through the early idle frames:

| Layer path | Translations before the outer transform |
|---|---|
|Sprite222 depth4 Sprite16 → depth6 Shape15|(-162.5,0), then(-3.5,14.5), retained when frame1 replaces Shape6 with15|
|Sprite222 depth30 Sprite51 → depth1 Sprite20 → depth2 Shape19|(140.6,28.5), then(2,1.5), thenidentity|
|Sprite222 depth35 Sprite55 → depth2 Shape54|(233.5,0), thenidentity|

For each assembled source plane above, the stable ideal mapping is:

| Layer | Stage x | Stage y | Stage bounds before 640×480 clipping |
|---|---|---|---|
|Lit wall|0.999862671·source_x−2.75618|source_y+0.39999|x−2.756..317.200,y0.400..480.400|
|Closed shutter|0.999862671·source_x+344.29615|source_y+97.39999|x344.296..574.265,y97.400..451.400|
|Surround|0.999862671·source_x+316.20001|source_y−0.10001|x316.200..828.630,y−0.100..479.900|

An important widening opportunity is already in the original: the surround's
geometry/art extends to stage x828.63. Its second tile starts at stage x764.64,
beyond the original right edge640. Some right-side art need not be invented.
For example a centered16:9 logical canvas extends to x746.67; the existing
surround geometrically reaches that far. This is not a claim that current
scissoring will expose it, that the offstage region has passed visual review,
or that later animation frames remain covered. The source beyond the current
4:3 crop should be inspected before generating extra material.

The scene is not static when activated: root depth35 is replaced with a0.5px
x adjustment at frame9; depths4/30/35 then translate and later scale during the
transition, all removed at frame67. The parent's frame5 activates Sprite51's
separate door animation; its shutter eventually moves up to local
y−355.5 and stops onframe79. Preserve those pivots and the alpha-hole alignment.
Do not bake the stable composition into a full-screen texture.

## Bright-green and transparent filler

Every inspected environmental tile except53 has alpha255 throughout.
Name53 has47,339fully transparent pixels, bounding box
`[56,163,225,444)` in its original512square. It also has26,836partial-alpha
pixels (alpha1..219), bounding box`[37,99,252,444)`. A broad bright-green test
(G>150,R/B<70) finds47,304pixels with alpha0..3, primarily exactRGBA
`(0,255,0,0)` (47,144pixels).

This doorway lies **inside** Shape54's sampled quad. It is not excluded by
vertex geometry: the source alpha encodes the opening through which the shutter
can show. Preserve its complete alpha and the semi-transparent boundary. Do not
paint the green as visible architecture, flatten it onto the shutter, crop out
its rectangle, or invent a green-key rule. The actual material/blending path
still needs runtime acceptance. For authoring, compare RGBA and trueRGB and keep
approved alpha separate; opacity-aware mips/transparent RGB handling matter.

Unused source borders listed above are a different category from the alpha
hole. They may be held exact while restoring visible art. They are not evidence
of drawable extra content unless UV/geometry is deliberately changed.

## Safe shared lit/dark structure

The source pairs14/13 and5/4 are the same poster-wall composition under
different lighting. The eight baked mode names/art, brick arrangement and paper
edges should come from one structural restoration. Do not generate an unrelated
lit wall: the moving spotlight would reveal different words, edges or brick
positions when it exposes Shape15 over the dark Shape6.

Use the original lit/dark relationship to derive a deterministic light treatment
of the approved structural base, preserving source-specific lettering and
lighting differences. A source-calibrated illumination/residual field is a
candidate approach; it still requires a visual trial and clipping/color checks.
Do not declare a guessed gain curve faithful without comparing original source
and both outputs. Keep alpha255 and protect the baked mode posters/words.

The shapes are closely registered, but not identical rectangles: lit width320
versus the already documented dark width319.5, dark second-tile origin255.5
versus lit256, and lit depth6 is0.5px lower than base depth2. Preserve both
original mappings. Share restored structure with explicit source-coordinate
sampling; do not force one entire assembled rectangle to the other's width.

## Useful previews and remaining boundary

The evidence supports isolated source-layer previews, sampled-rectangle crops,
alpha-hole overlays, original/restored blink comparisons, and an **authored
idle-layout approximation** using the matrices above. The preview can expose
offstage source art with a clear overlay of the original640×480 crop and future
aspect crops. It can also show closed/open shutter offsets as separate studies.

A true game raster additionally depends on the APT mask at depth3 with clip
depth7, spotlight/rim transforms, selected emblem/text, stage clipping,
subpixel texture sampling, material alpha/color transforms and the active
timeline/bytecode state. Drawing the lit wall full-strength everywhere is only
a lighting-envelope preview; it does not reproduce the moving spotlight.
Keep that distinction visible in any batch review page. Final validation remains
the running game at4:3,16:9,16:10,21:9 and32:9 with selection and transitions.
