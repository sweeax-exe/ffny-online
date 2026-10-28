# Branding transparency probes — 2026-10-18

Bounded read-only audit of the nine authentic assembled/native references under
ignored `pilot-v1/branding-v1/`. All nine source and faithful PNG hashes match
the existing reference receipts. The complete alpha channel of **every one of
the nine restrained candidates equals its faithful baseline exactly**.
No art, application, workshop, game or build changes were made by this audit.

Private metadata: `logs/ui-restoration-20261017/pilot-v1/transparency-v2/probes.json`.
It pins all27file hashes,29semantic probes, native RGBA values and3×3alpha
ranges. Source coordinates below are zero-based native assembled PNG texels,
not cropped visible bounds, normalized raw-image coordinates or game pixels.

## The title's dark letters are partially transparent, not fully empty

The native F/I/G/H/T stroke interiors all have alpha129/255 at these probes.
They are authored dark ink with roughly50.6%coverage. Making them fully opaque
black would lose the backdrop; erasing them to alpha0 would also change the
original design. Preserve that partial alpha, the original RGB and the subtle
D-counter gradient. The source deliberately contains both fully empty areas
and fully opaque dark distress detail.

| Native title probe | Coordinate | Authentic RGBA | Meaning |
|---|---|---|---|
| F stem | (135,50) | (16,12,0,129) | Partial dark lettering |
| I stem | (180,50) | (13,10,0,129) | Partial dark lettering |
| G curved stroke | (225,50) | (16,12,0,129) | Partial dark lettering |
| H stem | (285,50) | (16,10,0,129) | Partial dark lettering |
| T stem | (365,50) | (16,10,0,129) | Partial dark lettering |
| Def D counter | (30,30) | (10,10,0,112) | Authored graded opening, not a binary hole |
| Def/Jam negative gap | (80,35) | (16,10,0,129) | Partial dark field |
| Clear gap above Def | (50,10) | (0,219,0,0) | Entirely clear despite green RGB |
| Silver plaque | (156,40) | (214,215,214,255) | Opaque metal |
| Left plaque | (108,16) | (219,220,219,255) | Opaque metal |
| Dark distressed plaque | (356,71) | (79,75,65,255) | Opaque dark detail: retain |
| Lower empty padding | (5,100) | (0,255,0,0) | Entirely clear padding |
| Right empty padding | (510,10) | (0,255,0,0) | Entirely clear padding |

Native file:512×128, name19, SHA-256
`2617ecb8e7d11c0106ddf62ca97660eb3f442f7122002524a2a008a363918245`.
The actual sampled rectangle remains508×88. Probe coordinates must retain
that source registration when normalizing a generated proposal.

## Emblem openings versus opaque material

Two probes per native assembled emblem distinguish an opening from an opaque
material detail. “Dark” is a visual descriptor, not an instruction to zero
RGB values. Some material details are gray rather than black. A scan of all
nine sources finds **zero** fully opaque texels with every RGB channel≤32;
this does not justify keying dark pixels, because partially transparent ink
and fully opaque gray distress both remain authored content.

| Emblem and native size | Opening coordinate: RGBA | Opaque material coordinate: RGBA |
|---|---|---|
| Story192×160 | Book/spine gap(35,115):(0,0,0,0) | Book detail(113,29):(123,125,123,255) |
| Battle192×192 | Hand cutout(100,45):(0,255,0,0) | Metal edge(20,162):(90,89,82,255) |
| Options144×144 | Speaker sector(45,95):(0,255,0,0) | Frame(117,107):(140,142,132,255) |
| Jukebox128×160 | Ear/headband cavity(61,69):(0,255,0,0) | Headband(68,5):(74,77,66,255) |
| Extras192×128 | Disc opening feather(90,61):(104,182,88,5) | Disc reflection(108,16):(148,150,140,255) |
| High Scores128×192 | Lower statue gap(58,109):(255,32,82,0) | Statue edge(53,6):(90,93,90,255) |
| Rewards160×160 | Shackle interior(109,60):(0,0,0,0) | Shackle edge(132,9):(123,121,115,255) |
| Credits160×192 | Film frame(110,50):(0,255,0,0) | Film rail(57,154):(184,186,178,255) |

The Extras probe intentionally records alpha5 rather than rounding to0.
The high-scores hole contains pink RGB but is fully transparent. The Story
and Rewards openings contain black RGB and alpha0. These examples demonstrate
why neither “green means clear” nor “black means clear” is a valid decoder.
Use the independent source alpha channel.

## What the repair must verify

Retain the raw generated images and their native dimensions. Their different
canvases prevent comparing the same numeric coordinates directly against
source or faithful files. Explicit registration and a matching source-alpha
mask are needed before any probe comparison.

For a4×source-aligned output, the JSON samples faithful/candidate texels at
(4x+2,4y+2), retaining the actual interpolated RGBA; interpolation means those
values need not exactly equal the native pixel. Compare a repaired candidate
against **the faithful alpha at the same output coordinate**, not a nearest
native byte or a made-up binary mask. All29current restrained probe values
and all9complete restrained alpha planes pass that check already.

Check the edited raw proposal on checkerboard, light and dark backdrops.
Preserve clear holes, partial glyph coverage, opaque material, antialiased
edges and original active/padding geometry together. The raw variant is not
the source-protected candidate; an alpha failure in one must not silently
change approvals or substitute another variant's pixels.
