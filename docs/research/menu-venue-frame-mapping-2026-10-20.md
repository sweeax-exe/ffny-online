# Venue-selection Polaroid mapping — 2026-10-20

Bounded static read of the user's own dump: `screens/battle/chsVenue.big`, its APT, embedded `chsVenue.o`, and the existing HD source corpus/alias CSV. No code, runtime, or game changes; no build, game run, or image generation. The scratch decoder and structured metadata are `logs/ui-restoration-20261020/audit-venue-frame-20261020.py` and `venue-frame-metadata-20261020.json`.

## Finding

Names 143, 145, and 150 are **not three adjoining/croppable tiles of a single frame**. The APT uses them as separate full-square shapes inside an animated Polaroid/card composition. Name 145 is an independent black-RGB shadow mask behind the right-hand card. Names 143 and 150 are two different paper/photo-frame artwork layers. The composition has a third card (name 157) and a second animated shadow (name 152); these are the only adjacent texture names confirmed in the same Polaroid group. Do not flatten the group or combine the originals into one inferred montage.

## Source and exact geometry

All five pieces below are unique to `chsVenue.xsh` in the retained alias census. Every shape is one quad with APT bounds `(0,0)-(256,256)`, full source UV `(0,0)-(256,256)`, identity texture matrix, and no texture crop, rotation, or multi-part join. The vertices land on `(0,0),(0,256),(256,0),(256,256)`. Full-square geometry does not mean every source pixel is visible: alpha supplies the silhouette and soft edge.

| SHPX name | Source ID prefix | APT Shape / local role | APT hierarchy and local placement | RGBA facts |
|---:|---|---|---|---|
|143|`67f3756e55402d`|Shape 144, paper/photo panel for right card|Shape 144 at (0,0) in Sprite 149; sibling live `polaroid` clip at (0,0), and shadow Sprite 148 at (0,0). Sprite 149 is `polaroidR`, depth 3 at (40,0) in Sprite 160.|21,488 alpha-0 / 2,841 partial / 41,207 opaque; mottled gray photo/paper print and soft distressed perimeter.|
|145|`9d9501265c23d1`|Shape 146, right-card shadow mask|Shape 146 in Sprite 147 → animated Sprite 148 (`shadow`) in Sprite 149; aligned at (0,0), behind the card.|RGB is exactly black (one RGB color); 22,882 alpha-0 / 1,430 partial / 41,224 opaque. Preserve as black-with-alpha, not a keyed or recolored image.|
|150|`c2599d14439cfa`|Shape 151, alternate/left blank Polaroid paper frame|Shape 151 in Sprite 156 with the live `polaroid` clip and Sprite 155 shadow. Sprite 156 is `polaroidL`, depth 8 at (0,10) in Sprite 160.|22,015 alpha-0 / 2,161 partial / 41,360 opaque; pale frame, inset bevel, surface marks and soft gray edge shading.|
|152|`f574d22093f056`|Shape 153, left-card shadow mask|Shape 153 in Sprite 154 → animated Sprite 155 (`shadow`) in Sprite 156; aligned at (0,0), behind Shape 151.|RGB is exactly black (one RGB color); 22,768 alpha-0 / 1,467 partial / 41,301 opaque. Same independent black-alpha shadow treatment as 145.|
|157|`8b094269e1717f`|Shape 158, third paper card in the group|Shape 158 in Sprite 159, placed at depth 13 and (20,10) in Sprite 160.|19,460 alpha-0 / 4,309 partial / 41,767 opaque; separate pale distressed card/frame texture.|

The parent is Sprite 160 (`polaroids`), itself at (10,0) in Sprite 182 (`display`); the root places `display` at (335,72), with y scale 0.9998627. Sprite 160 has 70 frames, and the two shadow Sprites each have 39 frames. Thus the independent offsets and animated placements are authored parts of the card motion. At its initial placement, the three paper quads sit at display-local `(50,0)` right, `(10,10)` left, and `(30,10)` third-card; the exact source-local transforms above are the stable reference. Do not use these initial screen coordinates as permanent positions.

Sprite 131, named `polaroid`, is an empty APT sprite reused inside these assemblies and also by the venue map clips. Its empty static timeline is consistent with the photo/card content being filled dynamically; these five SHPX textures are the paper and shadow surround, not a self-contained set of venue photos. The display also contains the map Shape 142, which is sourced by names 140 and 141 and is a separate map object, not another paper tile.

## Source-exact art constraints

Keep each original 256×256 RGBA tile and its identity, source alpha, RGB, and pixel registration intact. Preserve the gray distressed printing, stains, inset line/bevel, rounded card perimeter and soft edge shading on 143/150/157. Preserve the exact alpha silhouettes and partial-alpha falloff in shadow masks 145/152; their black RGB is intentional mask data. Do not replace black with a key color, discard hidden RGB, turn either shadow opaque, recolor/grade the paper, sharpen or add/remove blur, or independently resize/crop/rotate any of these layers. Their native APT placement and moving card/shadow relationships carry the composition. A restoration can extend material only if the original 4:3 texels, RGBA, UVs, pivots, and moving overlap remain source-exact; this audit does not establish a native wide-layout registration.

The nearby names 140/141 map to Shape 142 in the map path (`mapClip`) and do not belong to these frame/paper assemblies. Names 152 and 157 are included because the object graph proves they are the other shadow/card surfaces in Sprite 160, rather than by numeric proximity alone.

## Evidence limits

APT and object hashes, SHPX name-to-source-ID mapping, matrices, quantized vertices, UVs, bounds, and source file hashes are in the private JSON. Geometry uses the same inferred packed-vertex decode documented in `menu-emblem-shape-audit-2026-10-17.md`; this is static APT/object evidence, not a rasterizer or live-screen validation. No claim is made about which venue types populate each dynamic card at runtime or exact visibility during every transition frame.
