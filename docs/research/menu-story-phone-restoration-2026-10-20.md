# Story message-center phone restoration — 2026-10-20

**Later same-day update (D168/UI):** Vlad approves phone v1. Freeze10files/two
source IDs; catalogue343pins/92reviewed IDs. Separate source4xexport prepared,
uninstalled; native validation pending. [Larger batch](menu-details-restoration-2026-10-20.md)
now first in review. Pre-approval production notes below remain historical.


Vlad conditionally accepted the ladder transition v4: “The ladder transition
looks good but needs to be playtested for a full approval. We can move on to
the other textures for now” (D166/UI). Its56current files/10source IDs are
frozen in `transition-conditional-approval-20261020.json`. The catalogue now
holds333unique pins/90reviewed IDs:72unconditional,8conditional Mode and
10conditional transition. No transition component or wide registration was
added. Full native moving-pan approval is still pending. Previous revisions
remain unapproved history. The active launcher-redesign game stays untouched.

## Phone draft and native mapping

The next study restores the Story message-center phone casing, not live screen
content. Read [the static mapping](menu-story-phone-mapping-2026-10-20.md).
Retail `screens/story/msgCentr.big::msgCentr.xsh` names110/111 are512×512 and
128×512, combined640×512storage with a640×480visible layer. Candidate exports
are2048×2048 and512×2048, uniform4x, joined into2560×1920for review.

Shape112 is at native(0,0),depth3. FractionalUV bounds/matrices are retained,
not rounded. A third part reuses110 outside the640wide APT bounds; do not fold
it into the review or assume it is invisible in wide runtime draws without a
live check. Foreground `headrAdjust` character128 at depth4,(115,109.5) remains
live. This metadata is not an inventory of dynamic screen strings or states.

Both source textures contain real alpha136..255, with no fully transparent
pixels. Black is not a transparency key. Native outer left/right RGBA row
profiles are exactly equal: black alpha136 in rows0..440 and187 in441..479.
The wider study extends only that matching profile beyond the centered640×480
viewport. The device keeps its original size, center, geometry and crop through
4:3,16:10,16:9,21:9,32:9. Reference layouts use the original native layer with
clear wider margins, composited over the APT's RGB153gray. This is a layout
study, not an installed backdrop or proof of actual wide clipping.

## Restoration and exact preservation

One built-in ImageGen call,118historical total. The exact prompt, source hash,
raw output/hash/dimensions and adoption policy are in `generations.json`.
The raw1448×1086proposal matches4:3aspect but repaints labels/coverage. It is
reference history only; its screen, lettering, silhouette and alpha are not
adopted. No new scene outpainting or generative widescreen join is needed.

Tile-aware4xLanczos is the authority. Use model fine brightness only in a
restricted source casing mask: neutral full-alpha interior, source L18..90,
RGBspread<45, alpha erosion17HDpx, source gradients>13expanded17HDpx excluded.
Permitted native casing bands are y114..326atx<108orx>=574, y44..75atx50..478,
and y366..396atx22..450. These exclusions keep LCD, status icons, both T-Mobile
Sidekick marks, MENU/CLOSE, keycaps/letters, grooves and source edges exact.
Four-native-pixel tile/outer guards and all stored rows480..512stay exact.

Adopt20% of sigma4 fine brightness, clamped±1.5L with the safe mean removed;
source broad illumination and Lab a/b retained. Final quantization changes
123,202pixels out of200,417safe pixels, maximum4RGB levels. Mean safe ΔLab is
(+0.005114,+0.000269,-0.001770), without broad warmth/exposure change. Source
Faithful alpha, protected screen/branding/keycap bands, tile joins and padding
are pixel-exact. This is intentionally subtle restoration; the sharper raw
proposal is not the final source-protected asset.

## Review, validation and handover

`http://127.0.0.1:51738/review.html` starts with the unapproved phone study.
Original/Faithful, RGBA/alpha, gray/checker/dark/light, side/wipe/draft and five
layout pairs are available; expand the layout disclosure for wide backdrop
comparison. Conditional transition follows, then approved Extras/history.

All333pins and1,725prior PNGs unchanged. Two source tiles retain exact alpha,
protected marks/seams/unused rows. All five stage pairs retain center and
native device scale. Browser controls exercised4reference/channel cases,
4backgrounds,3comparison modes,5aspect pairs and75%bounds at16:9; no horizontal
overflow at662px and no warnings/errors. Active phone images decoded; a few
rapidly switched comparison images were still loading on first observation,
so the record does not claim every historical image decoded in that instant.
Screenshot: `story-phone-v1-20261020/review-proof.png`. Existing22focused
menu/export/instruction tests pass. No runtime code, live pack/default, game
process,8768writer,17workshop candidates or Trejo was changed.

All phone art remains UNAPPROVED and absent from the catalogue/pack tables.
Next: owner phone feedback, then venue frames and remaining Start/support
art after static mapping. The19-source historical priority list still includes
the two phone IDs until approval. When serial game testing is available, check
live phone text, partial alpha/backdrop, fractional joins/offstagepart2 across
five aspects. Measure actual transition transforms/bitmap9 and register only
explicitly approved sparse/wide components. The4:3transition issue is not yet
proven fixed in game.

Private working batch: `logs/ui-restoration-20261017/pilot-v1/story-phone-v1-20261020/`.
New immutable additive archive: `<data>/scratch/menu-ui-story-phone-v1-transition-conditional-20261020/`.
Apply overlays to a COPY of the October19full snapshot and prior Extras and
transition overlays; never overwrite them. Source-only prompt/recipes/review
copies are in DefJam-Modding-Tools `research/menu-ui-story-phone-20261020/`.
No artwork, metadata arrays, captures or game bytes enter either repository.
