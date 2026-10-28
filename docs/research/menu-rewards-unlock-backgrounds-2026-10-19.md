# Battle approval and Rewards/Unlock wall draft — 2026-10-19

Vlad approves Battle setup v5 and asks to move on. D150 pins exactly13 files:
the v5master/fivecrops/twojoins/twoextensions and unchanged source-shaped mosaic/
twotiles. `battle-setup-approval-20261019.json` records owner words and hashes.
Together with D145’s39fighter files, all52 Battle background files are frozen.
Only artwork is accepted; native integration/game validation remains separate.
Review51738 marks setupv5 approved and retains all previous revisions.

D151 continues the authorized menu-background work with the shared Rewards,
Unlock Fighters and Unlock Venues brick wall. New art remains unapproved.
Private artifacts: `logs/ui-restoration-20261017/pilot-v1/rewards-v1/`.
Current master `rewards-shared-world-v1.png` (5256×1440),15 previews for3screen
labels×5ratios, two source-shaped tile restorations, full-height join proofs,
original/alpha comparisons and separate extension layers. Each screen can be
selected in the review’s first panel. These are background-only studies;
portrait frames, prices, menu controls and live effects are not flattened in.

## Structural audit and shared coverage

Corpus alias/ID and actual APT/object reads confirm the same two source IDs
(`fc7e7ea76a80…`,512×512; `6cd17deca345…`,256×512) in all3resources:
`battle/buyrewards` names2/1, `battle/unlockchar` and `battle/unlockven` names11/10.
This is one wall, not three independent paintings. Geometry uses512px from
part0 and188px from part1, with a1px overlap at511:699×480active,699×512stored.
Rewards places the wall atx−29 (root321 pluslocal−350); unlocks atx−50.4.
The actual640×480stage therefore crops sourcex29..669 vs50.4..690.4.
Unlock shapes include six additional foreground/frame parts; these remain
separate and unmodified. New audit and metadata are in private `next-scope/`.
The3xpreview rounds50.4×3 to151px (−0.0667nativepixel); runtime mapping still
requires exact authored coordinates. Unlock Fighters/Venues have identical
background layout but distinct foreground art; do not call them different walls.

## Vignette and source-alpha policy

The original wall includes BOTH baked RGB darkening and an alpha edge fade
(alpha52..255). Source-shaped exports preserve original alpha, stored padding,
edge vignette and tile boundary. Native4:3previews retain those original edges.
A seamless wide wall cannot carry the old fade as a dark vertical stripe inside
the expanded scenery. Widescreen studies keep the trusted native interior
x40..659 (619px) and generate peripheral coverage; a96pxfade moves to each final
view’s outer edge. The full original4:3RGBAcenter is therefore NOT byte-identical
in wide compositions; the peripheral vignette is deliberately replaced. This
exception is shown in review copy, receipts and checks rather than hidden behind
a center-preservation claim. It is a background-specific art study, not a relaxed
alpha policy for title/emblem/portrait transparency or the texture loader.

Common world origin isx−512native, width1752 (5256at3x). Trusted interior is
worldpixels1656..3513; original source RGB there is protected after restrained
restoration. Crop origins preserve each screen’s native wall offset and centered
640×480controls. `wide-receipt.json` records every crop and exception.

## Generated and deterministic work

Six built-in imagegen calls (92pilot total), no CLI fallback:
1restoration,1rejected left extension,1source-only left retry,1right extension,
2full-height join refinements. The first extension used the full-wall supporting
reference as a composition and repeated original bricks; rejected and retained.
The successful retry references only the narrow native-context blank target.
Prompts explicitly protect course heights, grain/material/softness/illumination,
original brown/olive/gray palette, and forbid yellowing/warming, raised coarse
mortar, new corners/floors/objects and an interior edge vignette.

Restoration adopts only raw high-frequency LabL brightness detail: Gaussian4px,
clamp±1.5L,strength0.35. Existing source Lab chroma retained. Alpha exact; padding
below1920px exact at4x; edges and tile seam protected. Wide sides normalized to
full target aspect/height and native-density sampling, then source-adjacent
Labmean/contrast profiles Gaussian64px, gains0.6..1.4. Final256pxside bridges
discard all generated source-half pixels, transfer smooth original color field,
condition16pxedge and blend64pxinto the older new side. No vertical course warp.
All exact jobs, input/raw hashes/sizes, adoption/rejection notes and processors
are saved (`generations.json`, `restore-and-targets.py`, `assemble.py`).

## Validation and next steps

1051 prior PNGs unchanged, all52 approved Battle pins exact. New source-shaped
alpha/padding and trusted interior exact; full wide native center equality
explicitly false because of the documented vignette relocation. Browser15cases×
6comparison/joinimages and2alpha images load; wipe50%, nativecenter1920/aspectlock,
4:3bounds75%at16:9, draft-only/side controls, approvedBattle labels and nooverflow
pass. Console warnings/errors empty; screenshot `review-proof.jpg` retained.
Source-publication checkers and instruction twins pass; no runtime tests.

Private archive `<data>/scratch/menu-ui-rewards-v1-battle-approved-20261019/`
retains this batch, approval pins/approved Battle files, audit metadata, recipes,
review/history and authoring state. Apply to a COPY of priorpilot; earlierarchives
immutable. Next owner review of Rewards/Unlock wall (especially both joins and
wide edgefade). Native title/menu/Battle integration, source identities/scissor/
UV/alpha/lighting/transitions and five-ratio game checks remain outstanding,
including Options scrolling and conditionalMode right-border darkness.
Writer8768/17candidates/Trejo untouched. No import/install/game/build/runtime/
core edit/commit/push.
