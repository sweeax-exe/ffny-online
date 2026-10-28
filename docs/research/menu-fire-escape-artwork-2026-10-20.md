# Fire-escape transition artwork — 2026-10-20

**Later same-day update (D166/UI):** Vlad says the ladder transition looks good,
with full approval pending a native playtest. Freeze v4's56current files/10IDs;
private `transition-conditional-approval-20261020.json`, catalogue333pins/90IDs.
No native/pan approval or geometry/registration fix is inferred. The report
below records its pre-approval production history; older revisions stay
unapproved. Move to [Story phone drafts](menu-story-phone-restoration-2026-10-20.md),
now first in review, and leave the running game untouched.


Vlad requested that the Press Start → Main Menu fire-escape pan receive the
same restoration and widescreen treatment as the settled menus. He explicitly
asked to leave the launcher-redesign game running and continue texture work
(D163/UI). No game was stopped, launched or modified in this art iteration.
The new transition artwork is **unapproved** and is not installed or registered
as a working wide transition.

## Extras approval carried forward

Vlad: “These Extras · paper and ribbon drafts all look good. Approved and good
to move on”. D164/UI freezes51files: six composites, six alpha proofs, nine
native-shaped tiles and30aspect placement drafts. The private record is
`extras-approval-20261020.json`. The tracked catalogue now has277unique pins,
80reviewed source IDs(72unconditional+8Modeconditional),19remaining IDs from the
original23-source shortlist. Five list/ribbon IDs were additional to that list.

`config/menu-ui-source-shaped-exports.json` maps the new nine tiles to a
separate `menu-ui-extras-test-v1` component. The existing exporter produced it
under `logs/ui-restoration-20261020/approved-pack-exports/`, all at uniform4x.
No active pack, launch setting or installed component was changed. Approval
covers artwork; live Extras text, state, alpha/UV and placement still need a
later guarded game check. Tests cover the updated table and ensure Extras
papers, like result papers, are not registered as widescreen scenery.

## Source reconstruction and runtime caveat

See [the original mapping](menu-fire-escape-transition-mapping-2026-10-20.md).
Retail `trans/t1` Shape11 has ten source bitmaps. A continuous **texel-space**
641×2533 art reconstruction uses rows:

| Native y | Left512px bitmap | Right129px bitmap |
|---:|---:|---:|
| 0 | 9 | 8 |
| 512 | 10 | 7 |
| 1024 | 6 | 5 |
| 1536 | 4 | 3 |
| 2048 | 2 | 1 |

The final row uses485rows; each right tile's unused stored127columns stays
protected. Source IDs, original hashes, matrices, used bounds and all decoded
positions are retained in `transition-v1-20261020/references.json`.

**Important correction to the simple scene assumption:** bitmap9/part1 has an
identity texture translation but its compressed geometry decodes to
x512..1024,y512.042..1024.084, rather than the top-left position its image
suggests. The existing baseline's `[WS-WIDEBATCH] 51ebe2dcf268` confirms
x512..1024,y991.9..1504.0 while the scene is below the viewport. This is evidence
of a placement exception, not proof of its cause or visibility during the pan.
The art study places its texels at top-left for image continuity; it does not
patch the runtime geometry or claim that this is the displayed game layout.
That tile stays Faithful-only. Other decoded rows accumulate about0.042native
pixel per512rows; previews use integer texels and retain the exact matrices
separately for subsequent integration.

Existing trace summaries are budgeted samples, not a complete pan measurement.
They cannot establish min/max motion, timing, visibility or lack of additional
draws. No new runtime measurement was taken while the other game was active.

## Artwork and retained attempts

The source is intentionally very dark olive/brown/black with optical/motion
softness. Preserve that original treatment, all stairs/rails/windows, source
alpha, stored padding and tile boundaries. No warmer/yellower grading or broad
relighting. Small panels stay centered; this background extends through32:9.

The first built-in full-height1707×2533 extension proposal preserved the broad
scene idea but invented coarse rounded masonry and sharper mortar. Both wide
sides were rejected after inspecting the final source-centered assembly, not
merely the raw generation. This first pass remains selectable as history.

A source-material retry uses three1707×1100 targets at native y0,716,1433,
overlapping384/383rows. Each request has the actual source center and enlarged
original wall-boundary references. Prompts explicitly forbid coarse stone,
extra mortar rows and crisp side texture. Generated centers are discarded.
The initial upper retry returned1620×971 (7.51% aspect drift): it was rejected
rather than warped into a structural repair. A targeted upper framing retry
uses only the exact original edit target. It still returns7.45%canvas-aspect
drift. Its pure wall material is uniformly scaled and center-cropped, with the
actual raw crop box recorded; all generated stairs/landings/center are discarded.
This material-only crop is not permission to warp structural seam repairs or
runtime geometry. No per-axis stretch is used for that upper proposal.
All attempts and their inputs remain
in immutable generation history; execution details are recorded in the private
`generations-v3.json`.

The final side assembly normalizes the near-matching middle/lower canvases
and uniformly crops the upper material proposal,
retains the real source/restored center, matches source row lighting/chroma and
native softness, and joins the overlapping proposals at measured low-error
rows with a narrow blend. It never copies/mirrors a source strip into the
extension. Exact parameters, input sizes/errors, chosen overlap rows and hashes
are in `assembly-v2-receipt.json`. Cross-overlap proofs are retained as well as
upper/middle/lower left/right border views.

V2 still showed material/course changes beside the original in the middle
close-ups, so it remains history. Six additional original-edge bridge targets
repair upper/middle/lower sections on BOTH sides. Each736×1104target has a
352-column gray gap,256columns of fixed original context, and128columns of
fixed outer wall. Only gap pixels enter v3. Native source core and outer
context stay exact; row endpoint palette/focus matching and recorded horizontal
overlaps keep the repairs bounded. All prompts stress native mortar phase,
original rectangular brick density, diffuse focus and unchanged color.
`assembly-v3-receipt.json` retains actual input sizes, gap rectangles, masks
by rectangle, chosen rows and processing hash. No new art approval is inferred.

A final v4 applies no new paint: the added wall's local L contrast is reduced
toward the source wall's measured row variance, using32native source columns,
generated columns8..72, sigma16 and gainclamp0.25..1. Original source/restored
center and generated chroma stay exact; this avoids making the source sharper
to match the sides. The active review selects v4, with v1–v3 retained.
The geometry/pan and remaining visible material continuity still require review;
none of these numerical checks grants artwork acceptance.

The ten source-shaped4x candidates keep original alpha, four-native-pixel tile
guards and unused storage exact. Tiny fine brightness detail is adopted only
inside the protected source shapes; broad light and Lab chroma remain source
controlled. Bitmap9 has no AI adoption. Their first-pass receipt records maximum
RGB differences0..5levels. These candidates are unapproved, distinct from
approved Extras and installed components.

## Review and limits

`http://127.0.0.1:51738/review.html` puts the fire-escape study first, followed by
approved Extras. The panorama is5120×7599 with the original640-wide viewport
at x1600..3520 and an extra original offstage column throughx3523. Five aspect
windows retain a common original center. Play/scrub moves a480native-pixel-high
window across the tall art, with original/draft, wipe and bounds views. The
loop is an illustrative slow art pan, not measured game timing or an in-game
capture. Six static pan positions ×fiveaspects accompany the continuous view.

Review BOTH full-height original borders, native grain/focus and all horizontal
proposal overlaps. Numerical center/edge equality is preservation evidence,
not proof of matching brick paths. New scenery remains subject to owner review;
do not automatically approve or install it because a numeric check passes.

Next: resolve art feedback, then when serial game testing is available measure
the actual transition crop/transforms and bitmap9 placement. Register approved
source tiles and side slabs with explicit measured anchors; verify4:3,16:10,
16:9,21:9,32:9, pan motion/fades and joins with existing save guards. The current
wide table still has no transition anchors. Do not call the reported4:3pan
fixed before that runtime validation.

## Validation and preservation

Eleven new built-in calls (117historical total) including retained rejected
attempts; v4 adds no call. All six bridge outputs have exact2:3aspect
(1024×1536 from736×1104targets). All277approved/conditional pins and1,445prior
PNGs remain exact. Ten native tile alpha/UV/edge guards, protected center,
and30phase/aspect centers pass. Extras' nine exported RGBA tiles match the
approved art exactly;22existing focused menu/export/instruction tests pass.

Browser:30pan/aspect states, three upper/middle/lower join pairs, active images
loaded, play/pause, wipe50%,16:9bounds75%, no horizontal overflow at the user's
662px viewport and zero console warnings/errors. Four history links resolve;
rapidly switched old master images were still loading on the first observation,
so no claim is made that all historical masters fully decoded in that pass.
The current v4 decoded fully. Proof is `transition-v1-20261020/review-proof.png`.

Private additive archive:
`<data>/scratch/menu-ui-transition-v4-extras-approved-20261020/`. It retains
the full transition batch, Extras approval/pinned images, sparse export, metadata,
recipes and review. Recover prior history by applying it to a COPY of the
October19 complete snapshot plus the October20 Extras overlay; earlier archives
are immutable. Source-only recipes/prompts/review code are preserved in
DefJam-Modding-Tools `research/menu-ui-transition-20261020/`. Art, captures,
arrays and review records remain local.

Archive verification:399files /378,712,798bytes, manifest SHA-256
`ba3990e4c9b6b9d2c7b82a3408b6e497ed637d37866c3bc4e5cd20ce53219de3`.
