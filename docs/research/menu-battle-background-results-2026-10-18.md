# Battle menu background drafts — 2026-10-18

D144 records Vlad’s request to start Battle backgrounds, carry forward his color
and border feedback, and add drafts to the existing review page for feedback.
Four independent scenes are now prepared: match setup (`cmtype`) and fighter
selection for two, three and four fighters (`chooseF2/3/4`). Eight original IDs
remain distinct. Review: http://127.0.0.1:51738/review.html, new first section.
These are unapproved artwork studies. No game import, install or native rendering
claim is made; the workshop writer and its 17 candidates remain unchanged.

## Source fidelity and restoration

The preceding [geometry audit](menu-battle-background-shape-audit-2026-10-17.md)
controls tile order, stage size, unused rows and independent live UI content.
Eight source PNG hashes match that audit. Each stored mosaic is640×512 with
640×480 active pixels; the left/right split is512/128. Source-shaped4x tiles are
2048×2048 and512×2048. Their original alpha and bottom128 enlarged padding rows
are restored from each tile’s own faithful reference. The active tile RGB
reassembles exactly to the restored mosaic. cmtype retains partial alpha;
fighter-select backdrops retain opaque coverage. The RGB review is flattened,
so use the stored RGBA/alpha/checker comparisons for transparency inspection.

Four built-in imagegen restoration proposals were generated separately. Final
assembly retains faithful spatial color, original chroma and broad lighting;
only bounded fine lightness (strength0.25, maximum1Lab L before quantization)
is adopted outside protected areas. Frame/paper/inset regions are conservatively
protected, as are setup posters/lettering, pipes/light, texture seams, near-black
pixels and unused rows. Each original frame count, silhouette and registration
is preserved; AI never adds portraits, controls or new lettering. Median ΔLab
is0/0/0 for each final mosaic. Raw AI color/geometry/alpha drift remains visible
as history, not the chosen restoration. The final source center is1920×1440.

## Widescreen and both-border lessons

There are four5120×1440 masters and twenty centered crops:4:3,16:10,16:9,21:9,
32:9. Every crop retains its corresponding restored4:3center exactly. Side
scenery is stored separately as eight RGBA extension layers for future mapping.

Eight full-height side generations use blank outpaint targets with fixed source
context, plus guides and full-center references. Prompts explicitly require
original cool local colors, no warming/yellowing or exposure/contrast changes,
no duplicated portrait frames, and continuity of lines/materials at every height.
Source softness and diffuse lighting are part of the prompt, not an afterthought.

The first setup-left proposal incorrectly extended plaster and introduced a
second corner; it is rejected. A narrower brick-only context produced a complete
brick slab. Initial reflected material support in assembly produced visible
vertical bands and was also rejected. Eight blank-gap bridge generations repair
both original boundaries on all four scenes; reflected/edge-held strips are not
in the adopted repair. Final assembly adopts only the generated middle512px,
restores the original context exactly, applies modest measured softness, and
reconciles boundary RGB on generated pixels only. Outer generated sides have
Gaussian2px softness at1440high. Source context itself is never blurred/graded.

The setup-left brick join received one further26-course-guided retry. Mortar
valleys in the generated strip are registered within18px of source-measured
course guides, easing to identity toward the outer retained scenery. No original
brickwork is warped. `course-retry-receipt.json` retains every anchor. These
checks improve joins; material/grain authenticity and the remaining visible
transition quality still need owner review, especially at full-height close-up.
Pixel invariants alone do not establish seamless artwork.

22new built-in imagegen calls (79pilot total):4restorations,8sides,1brick-only
retry,8bridges,1course-guided retry. No CLI/API fallback. Every exact prompt,
input/output hash, raw file, target, guide, rejected pass and assembly script is
retained in private `logs/ui-restoration-20261017/pilot-v1/battle-v1/`.
`generations.json` is the prompt/input/raw manifest. Final processing order:
prepare.py → make-restoration-jobs.py → built-in calls → restore-and-targets.py
→ built-in side/brick calls → assemble.py → prepare-bridges.py → built-in calls
→ finish-bridges.py → prepare-course-retry.py → built-in call →
finish-course-retry.py → verify.py → update-review.py. Job/result JSON files
record the generated paths; use the preserved outputs to replay without new calls.

## Review and verification

The existing51738 review now begins with Battle. It provides four scene choices,
five ratios, native-original comparisons, wipe, center lock, original bounds,
both full-height join proofs, full PNGs and stored texture/raw/alpha comparisons.
Previous approved art and its controls/history remain present. A browser pass
loaded20scene/aspect cases×4comparison images plus8join images, and32stored
texture cases×2images. Wipe25%, center1920, disabled aspect during center-lock,
boundary toggle and default setup16:9 work; no console warnings/errors and no
horizontal page overflow at398px. All756prior PNG fingerprints are unchanged.
Original IDs/alpha/padding,4protected mosaics,8active splits and20crop centers
pass private verification. These are artifact checks, not game tests.

The private immutable overlay is
`<data>/scratch/menu-ui-battle-backgrounds-v1-20261018/`; apply it to a COPY of the
previous pilot. It preserves the new batch, updated page, toolchain snapshot and
source workflow records without changing earlier archives. Exact archive hashes
are in its manifest and coordinating `battle-v1/archive-receipt.json`.

Next: owner feedback on the four drafts and both joins. After artwork acceptance,
map source tiles and wide layers into the native UI; check independent lighting,
portrait/name alignment for every fighter count, menus/transitions, all5ratios,
Options scrolling and Mode Select’s conditional right-border darkness in game.
