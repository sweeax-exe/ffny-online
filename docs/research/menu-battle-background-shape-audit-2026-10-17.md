# Battle menu background geometry — 2026-10-17

Bounded offline audit of the owner's own dump: `screens/battle/cmtype.big`
Image names2/1 and fighter selection `chooseF2.big`338/337, with corresponding
`chooseF3.big`343/342 and `chooseF4.big`344/343. No portraits, text or wider
Battle UI inventory was audited. No production code/assets were edited, no
assets generated/exported, no build/game run and no workshop-writer changes.

## Evidence and method

Read the embedded APT and ELF objects through the existing `apt-dump.py`,
`hd_assets.py` and the `uv-audit/audit_pilot_uv.py` ELF helper. Native corpus PNGs
were inspected read-only. The8target originals match their corpus PNG SHA-256
pins; source IDs/aliases come from the corpus already matched to the full fresh
SHPX census earlier in this session. Metadata and replay script are ignored:

- `logs/ui-restoration-20261017/next-scope/battle-background-geometry.json`
- `logs/ui-restoration-20261017/next-scope/audit-battle-backgrounds.py`

JSON includes exact resource hashes, original paths/PNG hashes, source IDs,
all corpus aliases, bbox, material matrices, sampled UVs, packed-geometry decode,
root placement/depth events, alpha ranges and excluded small material records.
It contains metadata, not artwork or binary payloads.

| Resource | APT SHA-256 | ELF object SHA-256 |
|---|---|---|
|cmtype|`9f6b0f14e379c213a0146f5e9ac7aae590c27d5ef2d41cef9a0307ef1931774e`|`7ead458f694d88336fa1fd5f2e15b6346d47033f98f3a76e0384ece477327d69`|
|chooseF2|`cee0019857c8d19602f7aa2422e35633ee15feebe58331aae3319d702658ebe0`|`bb9f1082f3115e6f4a68b0229b36b54196a7f06365f726175908dc4520731e6e`|
|chooseF3|`066828bef2df3b809d99252df15b5faf4b27b6b4a47bcf593e937992c2eaacd7`|`2f7ae152ac917a663218660e5b0e794fb739c8215deafc6eaf81b4c004d06868`|
|chooseF4|`a23b71d752873f4e9fb9809be25a78fe76f5091760d17a9c561753462ac6448c`|`77fd4000c19f4cd9c2e941023924905b9d6f4f723c6d2bb740aadc46aa551e35`|

## Identical background geometry, different source art

All four backgrounds have APT bounds`[0,0,640,480]` and are placed directly in
the640×480 root with identity matrix. Their two background submeshes have the
same decoded vertices/material-coordinate matrices. Image names are local to
each resource, not global identities; notably chooseF3 name343 is512wide while
chooseF4 name343 is128wide.

| Movie | Background Shape | Parts / image order left→right | Root depth | Root timeline frames |
|---|---:|---|---:|---:|
|cmtype|3|part0:name2; part1:name1|2|67|
|chooseF2|341|part2:name338; part3:name337|4|2|
|chooseF3|344|part0:name343; part1:name342|2|2|
|chooseF4|345|part0:name344; part1:name343|2|2|

For all4pairs:

| Tile | Stored size | Nominal active source rectangle | Nominal assembled origin | Material matrix translation |
|---|---|---|---|---|
|left|512×512|`[0,0,512,480]`|(0,0)|(0,0)|
|right|128×512|`[0,0,128,480]`|(512,0)|(−512,0)|

Join horizontally without flipping/rotation at source x512, retaining the full
640×512 authoring canvas. Source rows480..512 are outside the nominal quad;
they are padding, not additional visible floor or wall. Do not stretch512rows
into480. After restoration split exactly at512into original512square and
128×512canvases, preserving padding. The source640×480 crop maps directly to
the authored640×480 stage.

Using the existing inferred packed-vertex formula, all4pairs share the exact
same seam x512.03125 and y0..480. LeftUVx0..512.03125 and rightUVx0.03125..128;
the two quads share their seam vertices, with no inferred gap or half-pixel
overlap. These fractions reflect quantization; use the nominal source split and
original geometry rather than independently resampling the two sides. This is
an offline material/geometry inference, not proof of bit-identical GPU sampling.

**chooseF2 qualification:** Shape341 also references64×32 images340/339 in
parts0/1. Their decoded quad/source extent is approximately38.046875×26.015625.
They are not either of the two512/128wall tiles and remain outside this batch.
Do not replace the whole Shape341 with a flattened background or assume every
material in that Shape is background. Their metadata is retained under
`other_shape_materials_not_in_scope` solely so the source mapping can exclude
and protect them. Their final visible role was not investigated here.

## No source-ID sharing across the variants

| Movie / image | Source ID |
|---|---|
|cmtype2|`06830e776d6cc6bb56c52925f42cf6ca19c72444049477f1d594cfedac4d7edd`|
|cmtype1|`d0e7ae35466ded72d9b99139b6684bfbd83e2967fdfd2139b224ca8f694ed524`|
|chooseF2 338|`c49727c8650aaf13bb5d1a0aee11b3fc466d607a657b77a35b86bda6b99aa9e7`|
|chooseF2 337|`39406336dd035b74ae57e953a386c49500392da9f566bb6ed8c5fab56c8010c4`|
|chooseF3 343|`d628474a2f54474db0057e938f1c6b6aeb89d6003da7d1c6c79f76410d3725f1`|
|chooseF3 342|`96e7606df178d45d402e060a32e9d6f9033cc247cf86c02bc336aea7db0f3571`|
|chooseF4 344|`82ec4995c69b506a022b461def716702ac593e21887a8a6ce853a91ffcc80dfb`|
|chooseF4 343|`02f7125c71c22c6db9425f25763ef553904bad5a9a6f1e49d7a477f8da6ad119`|

These8IDs are distinct. Each has only its corresponding resource alias in the
corpus. An installed replacement for chooseF2's background would therefore not
improve chooseF3/chooseF4 automatically by identity.

Visual inspection agrees that these are distinct layouts: chooseF2's wall has
two large worn paper/photo frames; chooseF3 has three narrower frames; chooseF4
has four. The paper edges, frame silhouettes and dark inset shading are baked
into each background pair. Preserve each layout separately and avoid producing
a third/fourth frame by stretching or copying the two-player result. Portraits,
names, selections, stickers and stats are separate live UI content outside this
audit and must stay registered to their own backdrop frames.

cmtype is a different scene: blue-green plaster/brick with pipe, fluorescent
light, posters, graffiti and floor. Its lettering/posters require protected
source-aligned treatment. Similar material/palette does not make it the same
image as the fighter-selection wall.

## Timeline persistence and runtime limits

Each background Shape is placed at root frame0. Exhaustive root display-list
checks find no later Move, replacement or Remove at that depth: cmtype persists
through all67authored frames; fighter backgrounds persist across their2frames.
cmtype root labels0/1/2 are `screenpreload`, `screenwait`, `screenrestart`;
none changes the background mapping. This establishes static timeline geometry
for these targets, not a claim that gameplay/UI bytecode can never change or
cover the screen.

The root placements have no clip-depth mask of their own. The fighter UIs retain
separate dynamic layers; chooseF2/chooseF3 explicitly place a `lightingClip`
above the background. Keep such layers separate and validate their effect in
game. Final composition also depends on parent loader transforms/scissoring,
active UI state, masks and live portraits. Offline flattened previews remain
layout approximations, not captured game screens.

Alpha should remain source-specific: fighter-selection background tiles are
fully opaque255, whereas cmtype's512tile spans alpha243..255 and128tile spans
246..255. Do not silently force cmtype to opaque. Original palette and dark
lighting remain the restoration baseline; do not warm/yellow the paper or turn
the blue-green room neutral gray.

## Implication for the next scenery batch

Four independent backgrounds /8source tiles are prepared for mapping-aware
restoration. The shared geometry makes a common assembly/splitting routine
possible, while the art and framed layouts require separate source constraints.
Widen wall/floor scenery around a centered unchanged640×480layout; retain all
2/3/4player frame positions and baked design. Additional scenery requires actual
wide layer/UV/scissor integration, because these quads stop at640rather than
providing original offstage art. Validate all three player-count screens in game
at4:3,16:10,16:9,21:9,32:9; a successful two-player view does not validate the
three/four-player portrait alignment or their separate source art.
