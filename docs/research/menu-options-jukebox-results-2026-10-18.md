# Options and Jukebox background drafts — 2026-10-18

Vlad approved all nine transparency-v2 logo/emblem textures (D137). Their exact
PNG hashes are pinned in private `pilot-v1/branding-approval-20261018.json`.
The approval covers those generated-canvas artworks; source-shaped mapping and
in-game acceptance remain pending. Earlier raw/restrained alternatives retain
their own history and are not silently approved.

The next authorized group (D134) is Options and Jukebox. New drafts are on the
read-only review at http://127.0.0.1:51738/review.html, above approved branding.
Both have original/current side-by-side, wipe, draft-only, original-center
framing, bounds, full-height joins and five aspect crops: 4:3,16:10,16:9,21:9,32:9.
The three individual background targets offer native/Faithful, raw/restrained,
RGBA/alpha and dark/light/checker references. New backgrounds remain unapproved.

## Source geometry and retained layers

Use the existing [shape audit](menu-options-jukebox-shape-audit-2026-10-17.md).
Xbox Options uses71|70 (576×512 stored,574×480 active) and separate scrolling
wall4 (256×512 stored,171×487 originally active). PS2/GC8|7 is reference only.
The wall's extra85 stored columns contain real artwork and are reused before
new scenery begins. Original wall sampling must retain those columns during
filtering: cropping to171 first creates a false translucent last stage column.
At3×, sample wall at fractional x1408.2 and clip the stage; the known context
is2176×1440. The original stage remains1920×1440 at master x1600.

Options studies show the TOP scroll state only. Panel and wall have independent,
opposite motion; these flat studies cannot validate their different extrema.
Jukebox20|19 forms640×512 stored/640×480 active. Bottom strip88|87, live controls,
list mask and console-dependent lighting remain separate source layers. Neither
study bakes live UI or animated lighting into the artwork. Side layers are
preview canvases; native scroll/UV/scissor/alpha/mip integration is still work.

## Restoration and generation history

Private evidence: `logs/ui-restoration-20261017/pilot-v1/menus-v1/`.
There are13 new built-in imagegen calls,49 pilot calls total:3 restoration,
2 rejected padded-wide proposals,4 full-height side proposals,4 focused bridges.
`generations.json` and13 prompt files retain exact inputs, raw outputs, hashes
and actual dimensions. No external image provider or new core preset was used.

The three restrained layers preserve source alpha, strong/protected graphics,
near-black texels, seams, edges and padding. Source Lab a/b and broad lightness
remain authoritative; only25% fine-L residual (radius8HD, bounded±1L) is allowed.
All three median Lab differences are0/0/0. Four split tiles retain native stored
canvases. Independent checks found unused bottom seam texels differed because
parent-vs-individual enlargement has different filtering support; splits now
copy each individual faithful tile's bottom32 rows exactly. Active reassembly
and alpha remain exact. This correction does not alter visible stage art.

Initial3072×1024 wide targets had864 active rows plus padding. The model filled
the padding and stretched scenery; those outputs are rejected. Full-height
left/right targets then retained real center context and material scale. Their
near-identical returned aspect/landmarks permit documented whole-canvas
resolution normalization. Never use the rejected vertically distorted art as
final: it supplied editable outer context that was subsequently replaced by
full-height outputs before final assembly.

Narrow bridges edit512HD columns between fixed original and fixed outer
contexts. Both flanks are supplied in1536×1440 targets; raw1295×1215 resolution
is recorded. After generation, local grain matching chooses Gaussian radii
1/4/1/0 for Options-left/right,Jukebox-left/right. One-sided boundary matching
is limited to generated pixels. `bridge-receipt.json` records domains, original
context preservation and numerical residuals. Reflected-material and clamped
support proofs were rejected because they produced repetition/streaks; retained
proofs are history, not the reviewed image.

## Quality assessment and verification

Final files use `*-wide-refined.png` and `*-RATIO-refined.png`. The original
center, extra known wall pixels, and pixels outside bridge bands are exact.
This does NOT establish invisible joins. Visual review still shows local
sharpness/material changes across some transitions, especially the Options
door. Both full-height join links expose this; do not describe the batch as
seamless or game-ready. Wider generated masonry also remains an interpretation,
subject to owner palette/material review. Exact source palette is enforced in
the restoration/center, not asserted for invented scenery.

`verification.json`:9 reference pairs,3 protected/alpha pairs,4 active split
reassemblies plus exact individual unused padding,10 aspect centers,2 known
contexts,50 prior approved/conditional art pins and27 prior branding-reference
hashes pass. `browser-verification.json`:10 scene combinations×4 images and24
texture combinations×2 images load;3 backdrops,3 wipe endpoints,center lock,
bounds,all9 approved branding images and prior title-study pair work; zero
console warnings/errors. UI locator mistakes during checks required exact
values/scoped bounds selector; they did not expose broken controls.

Prompts/workflow are synchronized to DefJam-Modding-Tools. A separate private
archive `<data>/scratch/menu-ui-options-jukebox-20261018/` retains the whole
new batch, approved nine, review page, helper/toolchain snapshots and hashes.
It is an overlay for a COPY of earlier pilot archives; original archives remain
immutable. The new menu section is self-contained, but old review sections
still depend on their earlier private pilot files.

Next: owner assessment/refinement of new art and both joins; then source-shaped
mapping and full Options scroll/transition testing before game validation.
Battle setup/selection remains the next authorized background group. Writer8768,
17 workshop candidates and Trejo review were untouched. No new import,selection,
installation,game/build/runtime change,commit or push.
