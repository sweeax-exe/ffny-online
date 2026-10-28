# Approved skyline and remaining pilot background drafts

2026-10-17, D128-D129. The owner approved the full-height v3 isolated skyline and
asked to move to the others, documenting everything used. This pass pins that
approval, makes the workflow reproducible, and produces initial art candidates
for the remaining title and mode-select scenery. No game/build, runtime change,
pack installation, source-core change or live workshop write occurred.

## Approved artifact, exact scope

`seam-v3/title-skyline-wide-master-v3.png`,5120×1536, SHA-256
`683e5481d3bef12a27be00a1eda0afbbcfcefdbc3583be862f00ee21b809f2f0`.
The private `skyline-approval.json` pins this and all five aspect crops with the
owner's exact wording. Its bytes are unchanged by this batch. Approval covers
isolated skyline artwork; it does not approve the earlier separate source-palette
tile trials or new city/tower/mode art, and is not runtime acceptance.

Complete self-contained package:
`<data>/scratch/menu-ui-approved-skyline-v3-20261017/`. It retains the v1/v2/v3
chain in one tree, all prompts/raw outputs, source/faithful joins, masks/guides,
coordinate transforms, processors/receipts, approval and hash manifest. Earlier
incremental archives remain untouched. Source-only production guide is
`/Users/vlad/Code/DefJam-Modding-Tools/docs/menu-art-workflow.md`, alongside the
prompt scaffold and seam-review guide. It records the original color failure,
regenerated-context cloud mismatch, missed lower sky and actual accepted recipe.

## Geometry before generation

Two bounded offline follow-ups validate source bytes against the corpus and
derive active source rectangles, transforms and alpha/padding behavior:
[remaining title layers](menu-title-remaining-layers-2026-10-17.md) and
[remaining mode-menu layers](menu-mode-remaining-layers-2026-10-17.md).

- City names6|5:640×512 stored,640×310 active, settled stage offset(0,102).
- Tower/roof names11|10:640×512 stored,640×446 active, offset(0,16).
- Bottom padding in both title sheets is opaque. Clip active height before
  compositing; preserve the entire sheet for source-shaped texture restoration.
- Lit wall14|13:320×480 active; its mapping differs from dark319.5width/0.5px
  overlap. Share structural detail, retain each original lighting and mapping.
- Shutter18 above17:230×354 active within256×384 source join. Do not widen the
  animated shutter or its controls merely to fill a widescreen frame.
- Surround53|52:512.5×480 active, second tile at448.5; source53 contributes449
  columns rather than its stored512. Preserve the doorway alpha, including the
  hidden bright-green RGB. It is an alpha opening inside sampled geometry.
- Surround already extends to stage x828.63. The16:9 right side can therefore
  expose original art; only further areas require invented scenery.

## New drafts and exact preservation

Four built-in ImageGen restorations: city, tower/roof, rolling shutter and door
surround. The lit poster wall is a deterministic sibling using the earlier dark
trial's fine detail with its own original lit chroma/illumination, not an unrelated
generated layout. Five new restrained derivatives have byte-exact baseline alpha,
protected graphics, silhouette bands, seams and padding. Median Lab drift0 for
each; absolute mean chroma changes<0.004Lab. Fine L* is25% of the raw difference,
bounded±1, radius8HD pixels at4×. Raw generations remain for honest comparison.

Three outpaint proposals use the FINAL actual centers/context and explicit region
guides: city, tower/roof and mode environment. Protected original context is copied
back before QA. Initial proposals still had a roof-contour step and shifted mode
posters outside the center. Two focused built-in bridge edits address these. All
new context/proposal/raw dimensions, normalization and hashes are retained.

The rigid roof plane needed an additional technical registration: the generated
right texture is vertically sampled onto the alpha contour extrapolated from the
last80source-edge columns. It continues the measured slope, naturally leaving
the active layer through its bottom at very wide aspects. Color continuity alone
could not remove that shape mismatch. Original center/alpha remain exact. New
side alpha is a draft interpretation requiring review, not recovered source data.

Bridge assembly uses premultiplied color/alpha and the retained harmonic solver.
For patches reaching the canvas top/bottom, corner conditions must agree with the
original-facing edge; freezing an incompatible old edge row retains a small cut.
The mode bridge returned1254square for a1024square target. Its full-canvas aspect,
50%join line and24%poster-top landmark match, so uniform resolution normalization
is explicit. Reject crop/registration drift; tool pixel dimensions alone are not
a mask or a placement guarantee. Raw outputs and actual transforms are recorded.

Nine new built-in calls,18total pilot calls. No new workshop requests were
fabricated;17workshop candidates and existing reviews/session remain untouched.
New art remains unapproved. No automatic selection/export/installation occurred.

## Review and limits

Same read-only page: http://127.0.0.1:51738/review.html. New top section offers:

1. Title skyline+city+tower composition, and each new layer over the approved sky.
2. Mode-select dark and lit lighting-envelope studies sharing extended structure.
3. All five aspect crops per study, original4:3area overlay and full-height joins.
4. Original/faithful/new restrained/raw comparisons for all five new derivatives.
5. Approved skyline and previous iterations, approval/recipe/provenance links.

Title preview clips active layers and uses settled transforms at3×. It omits the
shield, wordmark and Press Start sticker. Mode preview uses nominal idle APT
transforms rounded to nearest4×pixel, then3×art scale. It is an authored layout
approximation with full-strength dark/lit wall envelopes; spotlight mask/rim,
selected emblem/text, precise rasterization and timeline behavior are not shown.
Do not ship the flattened study as a replacement for those animated layers.

Evidence: ignored `logs/ui-restoration-20261017/pilot-v1/batch2/`, including all
source/faithful/raw/protected images, exact prompts/inputs, `generations.json`,
`references.json`, `restoration-receipt.json`, `wide-receipt.json`, processor code,
five view groups×five crops and full-height joins. Stable private copy:
`<data>/scratch/menu-ui-second-batch-20261017/`, with a hash manifest.

Final independent verification (`batch2/verification.json`): approved master and
five crop hashes exact;5/5restoration alpha/protected comparisons exact;
3/3wide protected contexts and all pixels outside allowed domains exact;
25/25aspect centers exact. Browser loads all25study images and five restoration
pairs, switches raw history, and toggles the37.5%original-area overlay at32:9;
zero console errors. Live workshop remains17candidates, with reviews, requests,
session, prior candidate records and the four imported menu files unchanged.
Both repositories pass whitespace, AGENTS/CLAUDE equality and source hygiene
(324tracked source/0publication failures;66source/0policy failures). Runtime/core
regressions were not run or claimed for private art and documentation changes.

Next: owner review the new surfaces, roof extension/side alpha, brick courses and
local lighting. Preserve the approved skyline. Reviewed restoration trials can
then be split/imported with source IDs; wide art still needs explicit per-layer
geometry/UV/scissor/loading integration and real five-aspect game captures.


## Original comparison follow-up — 2026-10-17 (D130)

Vlad requested comparisons with the originals. The review page now pairs all
five study groups with native-source versions in all five aspects. These use
only the seven original decoded texture sheets, linearly enlarged in the same
active-bounds/layer-layout approximation. Widescreen originals retain black
side margins beyond the original 4:3 display. They contain no Faithful HD or
generated pixels. These are authored texture layouts, not captured game frames.

Controls offer side-by-side, draggable wipe and draft-only views; shared aspect
selection; an original 4:3 center view at matching scale; bounds overlays; and
links to both full PNGs. The individual texture section now defaults to native
originals and can switch to the Faithful HD baseline.

Evidence: `logs/ui-restoration-20261017/pilot-v1/comparison-v1/`, including
`build-originals.py`, `update-review.py`, `receipt.json` and `verification.json`.
All 25 source centers/black margins and input/output hashes verified. Browser:
25 matching pairs/four images each and 14 native/faithful texture pairs load;
all view/framing/bounds controls and wipe endpoints work; zero console errors.
All 25 current drafts and the approved skyline master retain their hashes.
No new generation or workshop import. Existing art approvals are unchanged.

Separate private overlay: `<data>/scratch/menu-ui-original-comparison-20261017/`.
Apply its `pilot-v1/` files to a COPY of the second-batch archive; previous
approved and second-batch archives remain immutable.


## Owner approval follow-up — 2026-10-17 (D131-D132)

Water-tower/roof artwork is approved. Mode Select is conditionally approved,
with uneven darkness at the right original4:3 boundary reserved for an in-game
check; keep current art unchanged. City/middle art remains awaiting approval.
Exact versions are pinned in private `pilot-v1/approvals-v1/approval.json`.
See `menu-ui-next-scope-2026-10-17.md` for inspected next-screen recommendations
and the conditional acceptance plan. Earlier unapproved labels above describe
the batch at creation time; these decisions supersede them for the named art.
