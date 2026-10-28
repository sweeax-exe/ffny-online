# Title logo and actual menu emblems — restoration batch, 2026-10-17

Vlad approves the city layer (D133), completing approval of the three existing
title scenery layers, and authorizes logo/emblems FIRST, then Options/Jukebox,
then Battle setup/selection (D134). The current city layer/master/five previews
and five combined scenery studies are hash-pinned in private
`pilot-v1/branding-v1/city-approval.json`. Mode Select remains conditionally
approved under D132: right4:3-boundary lighting needs the running-game check.

## Completed artwork batch

Nine built-in imagegen calls (27pilot total): retail prestart wordmark and the
eight actual selected-mode door emblems: Story/book, Battle/fist, Options/speaker,
Jukebox/headphones, Extras/disc, HighScores/trophy, Rewards/open padlock,
Credits/filmstrip. These are unapproved art trials, not installed replacements.
Original image names are not SHPX directory indices. The eight door shapes use
21source fragments; six support icons are different artwork and do not replace
the eight. Native/faithful references for35textures were retained (logo,21door,
6support,7localized start fragments). Only logo/actual8emblems were generated.

Bounded independent audits then checked by the coordinating agent establish:

- Title19: sampled508×88 of512×128; real graded alpha and colored RGB, not a
  monochrome opacity mask. No editable authoritative vector logo was found.
- Emblems: active rectangles and source-preserving canvases are mapped for all8.
  Joins occur at source128; three-piece sheets have an empty bottom-right gap.
- All22actual logo/emblem fragments use DXT5/straightRGBA. Colored transparent
  filler is not visible art or a key-color instruction. Preserve the real alpha.
- Existing wordmark in branding/ is a different modified raster, not authority.

See `menu-logo-shape-audit-2026-10-17.md` and
`menu-emblem-shape-audit-2026-10-17.md` for exact metadata and source identities.

## Source-aligned restrained candidates (D135)

The raw title returned2172×724 despite the source4:1canvas; raw emblem outputs
also vary in resolution/framing and may invent cutouts, broaden outlines,
flatten shading, or introduce a faint backdrop/glow. Exact prompts and raws
are kept. Raw alpha/canvas/glyph shape is never accepted as runtime mapping.

Final candidates retain the accepted faithful4×alpha at every pixel, strong
source RGB/stroke/scuff edges, source tile joins and unused texels. Generated
visible bounds register ONLY the fine surface proposal to the baseline.
Source spatial Lab chroma and broad lighting remain authoritative. Apply35%
fine L* residual, bounded±2L, blur radius6 at4×, only inside unprotected opaque
source regions. Copy clipped/protected pixels back exactly. Bbox registration
is not evidence that generated lettering aligns; strong source edges stay held.
All geometry and alpha come from the baseline, not the raw proposal.

These candidates are deliberately subtle. Credits changes only160visible pixels
relative to FaithfulHD because thin strokes/alpha/seam protection occupy almost
its entire visible area; do not claim a meaningful recovered-detail gain there.
The other eight alter bounded interior surface detail while preserving shapes.
They remain source-shaped4×assets: wordmark2048×512,21split door tiles each4×
their original stored canvas. Same source-stage logo/emblem textures work across
aspect ratios; no logo stretching or per-aspect redesign is required.

## Review and verification

Read-only page http://127.0.0.1:51738/review.html now starts with the nine-asset
branding selector. It pairs native originals or FaithfulHD with restrained or
raw proposals, offers dark/light/checker backgrounds and RGBA/alpha inspection,
and links original/candidate PNGs, exact prompts, geometry and checks. Prior
scenery controls/comparisons and history remain. City status is now approved.

Independent checks:35original/faithful reference pairs unchanged;9candidate
alpha/protected comparisons exact;21split tiles reassemble exactly, including
stored padding and alpha. All previous approved hashes and12city approval hashes
remain exact. Browser:72asset/reference/version/channel combinations load,
three backdrops work, city/conditional-mode statuses correct,zero console errors.
No workshop8768writes/imports/approval-selection changes, game run, build,
installation, runtime/core edits, commit or push.

Private evidence `logs/ui-restoration-20261017/pilot-v1/branding-v1/`:35native/
faithful pairs, assembled references,9prompts/raws, `generations.json`, processors,
protected masks,9restrained candidates,21source-shaped splits, exact receipts,
alpha/dark/light previews and full original/restored contact sheet.
Stable new private overlay `<data>/scratch/menu-ui-logo-emblems-20261017/` keeps
this batch/current review/city-approved copies. Apply only to a COPY of earlier
second-batch/comparison/approval archives; those old archives remain immutable.

## Authorized order after this review batch

Continue Options/Jukebox, then Battle backgrounds under D134; do not ask again
whether those groups are in scope. Fresh preparation already maps the correct
Xbox Options71|70 (alternate8|7 is PS2/GC), its separately scrolling right wall,
Jukebox20|19 and independent bottom strip/mask/light layers. Battle match-type
and2/3/4fighter-selection backgrounds share640×480geometry but use eight distinct
source IDs/artwork and different portrait-frame counts. Do not stretch/reuse one
variant. Preserve cmtype alpha and chooseF2's small nonbackground materials.
See the new Options/Jukebox and Battle shape-audit reports for exact joins.
Actual five-aspect runtime integration/test remains pending, particularly Mode
Select's conditional right-edge lighting and live transitions.
