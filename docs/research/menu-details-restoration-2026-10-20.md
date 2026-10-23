# Larger Menu/UI detail batch and phone approval — 2026-10-20

**Later same-day direction(D170/UI):** Vlad says these look good, but explicitly
leaves all16forreview and asks to move on. No approval/pins/export inferred;
all19source candidates remain pending unchanged. [New popup/profilebatch](menu-popup-profile-restoration-2026-10-20.md)
now leads the review page.


Vlad approves the Story phone and requests larger batches for review (D168/UI).
Freeze10files/two source IDs in private `phone-approval-20261020.json`: two
source4x tiles, visible composite/alpha, native dark RGBArowprofile and five
wide studies. Catalogue343unique file pins/92reviewed IDs:74unconditional,
8Modeconditional and10transitionconditional. A separate `menu-ui-phone-test-v1`
two-tile sparse4xexport is prepared and pixel-identical to approved tiles,
but not installed/enabled. Native phone text/clipping/offstagepart2 checks and
moving ladder pan acceptance remain pending. No running game was touched.

## Batch scope

The next review groups16items over19unique source IDs, disjoint from approved
sources. It covers the17remaining IDs of the historical23-ID priority shortlist
and two proven venue companion layers. The shortlist stays17until owner review;
this is not an estimate of every unfinished menu painting or UI resource.

| Review group | Items | Source textures | Candidate policy |
|---|---:|---:|---|
|Venue Polaroid paper cards143/150/157|3|3|Restrained paper brightness detail|
|Four English/French Press Start variants|4|7|Restrained red-paper detail; all source lettering exact|
|Venue shadows145/152|2|2|Faithful4x; blackRGB/soft alpha retained|
|Title radial effect16|1|1|Faithful4x; original intentional softness|
|Independent support icons11/14/17/20/23/26|6|6|Faithful4x; original ink/partial alpha retained|
|**Total**|**16**|**19**|**Seven restored papers, nine Faithful-only choices**|

Read [venue geometry](menu-venue-frame-mapping-2026-10-20.md) and
[title/support mapping](menu-title-support-mapping-2026-10-20.md). Venue cards
are independent moving layers with live `polaroid` clips, not adjoining tiles.
Two shadows remain independent alpha masks; 152/157 join the scope because the
object graph proves them part of the same composition. All five use full256²
UVs/identity matrices. The adjacent map140/141is excluded.

The four sticker shapes are29(28+27,width134),32(31+30,width167,−12overhang),
36(35+34,width183) and38(37,width244,−29overhang), all40high. Integer texel
mosaics support review only; retain original fractional UVs, negative overhangs,
platform/language frame selection and transforms at export/runtime. Tiny8px
name27is a partial edge, not a standalone sticker. Effect16is the radial image,
not a shield; its source APT/object samples a640×480shape from256²texture,
so native sampling must be verified before any new wide-effect registration.

Six support-library icons are independent sources, with no paired APT/object
in that `.big`. They are not pixel-identical to mainMenu textures, and those
same-number mainMenu placements are not proof of support-library placements.
The shield's authored alpha max212is translucent ink. Other source RGB includes
green in alpha-zero texels: retain actual coverage, never chroma-key by RGB.

## Generation and protected assembly

Nine built-in ImageGen calls,127historical total: three card paper proposals,
two rejected support icon proposals, four sticker paper proposals. The icon
proposals invented dots/halftone grain, so no pixels from them enter candidates;
all six support icons keep Faithful4x. Raw failures/prompts/references remain
in immutable history. Final candidates do not use raw glyphs, silhouette or alpha.
No invented checker/dot texture is adopted. Intentionally soft masks/effects
need no generative redraw.

Original tile-aware4xLanczos is the authority. Paper candidates retain source
Labchroma and broad lighting, adopt sigma4 fine brightness at18%/clamp±1.4L
(cards) or7%/±0.45L(stickers), safe mean removed. Use original opaque interior,
alpha erosion9HDpx, source edge exclusion13HDpx, sourceL12..98, neutral card
paper or red-only sticker mask. Protect generous card photo-inset rectangles,
all non-red lettering/shadows, tilejoins,16HDpx edgeguards and unused rows/columns.
Sticker generation inputs are source-aligned1024×512padded targets; preserve
exact input rectangle, normalize only same2:1aspect outputs and crop recorded
bounds before taking fine brightness. Source-shaped exports retain full tile
canvases, including the8×64edge and transparent padding. Review composites are
reconstructed from those final guarded tile candidates, not a separate paint.

All19tile alpha/guards/padding and protected original graphics are exact.
Maximum RGB change is4for cards and2..3for stickers. Quantized mean safeΔLab:
cards L−0.014..−0.004,a/bwithin0.0004; stickers L+0.003..+0.043,a−0.100..−0.006,
b−0.087..+0.002. These tiny quantization/gamut effects are recorded, not claimed
as mathematically zero. No broad warm/yellow or exposure grading is introduced.
The French short sticker changes only186pixels after guards, deliberately
nearly Faithful4x. Numerical preservation does not confer owner approval.

## Review and validation

Review51738 now starts with the16-item batch, an overview disclosure and texture
selector. Original/Faithful, RGBA/alpha, checker/dark/light and side/wipe/candidate
views support comparison. Raw history is separately labeled. Five aspect pairs
per item retain original graphic size; centered isolated placements are explicitly
illustrative, not game screenshots or verified native positions. All80pairs
keep size/center exact; no scenery is invented behind small UI graphics.
Phone follows with APPROVED status, then conditional ladder and prior history.

Preservation:343approved/conditional pins and1,751prior PNGs exact. Browser64
reference/channel combinations and80aspect pairs all loaded,3backgrounds and
3comparison modes exercised,16:9bounds75%, no overflow at662px, zero console
warnings/errors. Proof `menu-details-v1-20261020/review-proof.png`. Existing22
focused menu/export/instruction tests pass; source policy checks cover both
repositories. No runtime/build/game/defaults/active-pack or8768writer change.

All16new candidates are UNAPPROVED, including Faithful-only recommendations,
and excluded from approval/export tables. Next: owner batch feedback; freeze
only the explicit accepted scope. Continue remaining UI classification beyond
the shortlist after that. When serial game testing is available, check native
phone/Extras and moving transition/venue clips, live text, actual platform frame,
alpha, fractional sampling, fade and joins across4:3,16:10,16:9,21:9,32:9.
The reported4:3transition is not yet proven fixed in game.

Working batch: `logs/ui-restoration-20261017/pilot-v1/menu-details-v1-20261020/`.
Private additive archive: `<data>/scratch/menu-ui-details-v1-phone-approved-20261020/`.
Apply to a COPY of prior complete/Extras/transition/phone snapshots. Earlier
archives stay immutable; images, captures, arrays and review records remain local.
Source-only prompt/recipes/review bundle is in DefJam-Modding-Tools
`research/menu-ui-details-20261020/`. Exact assembly/recipe hashes are in local
receipts. Existing approved art, live writer/17candidates and Trejo unchanged.
