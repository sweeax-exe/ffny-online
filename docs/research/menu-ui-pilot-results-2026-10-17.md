# Menu/UI artwork pilot — first drafts, 2026-10-17

Owner direction D124: restrained cleanup, original color with no added warmth or
yellow cast, original composition/controls centered, and21:9/32:9 scenery in the
first pass. This report records an initial art audition, not full pilot/menu
coverage or runtime acceptance. No game build, launch, installation, approval,
selection, export or publication occurred.

## Deliverables

The read-only private review is http://127.0.0.1:51738/review.html. Its temporary
server is exec session54187, bound only to127.0.0.1; it serves
`logs/ui-restoration-20261017/pilot-v1/`. The existing authoring writer remains
http://127.0.0.1:8768; it was not restarted or replaced.

- Two joined source/faithful reference pairs: retail title skyline640x512 and
  mode-select dark poster wall320x512. These are texture canvases, not screenshots.
- Six built-in imagegen outputs: two initial restorations, two color-only
  revisions, and left/right skyline extensions. Exact submitted prompts, input
  hashes and raw output hashes are in `generations.json`; no CLI/API fallback.
- Two conservative palette-constrained technical trials, split into four
  source-mapped4x tiles and imported through the existing workshop writer.
- Skyline texture master5120x1536 and layer-only stage previews at4:3,16:10,16:9,
  21:9,32:9, all with the same faithful center at3x preview scale. Five center
  equality checks pass. Near-city/tower/logo/prompt layers are not included yet.
- Read-only review page with ratio selection, original4:3 bounds, original versus
  constrained/raw comparison, measurements, PNGs and provenance. No review store
  or approval buttons are added to this temporary page.

All artwork and private receipts remain ignored under the pilot folder. A stable
mirror is retained outside Git at `<data>/scratch/menu-ui-pilot-20261017-v1/`;
the local data root is `/Users/vlad/Code/defjam-data`. The mirror is outside the
live workshop's writer-owned directory. Runtime/authoring source code is unchanged.

## Geometry verified before generation

See [the UV/layout audit](menu-pilot-uv-layout-2026-10-17.md). The original skyline
joins name2 on the left and name1 on the right, at source x0/512. The wall joins
name5 then name4 at x0/256. Retain full original padding and dimensions; the wall
has an authored half-texel geometry overlap that should not be corrected by
stretching artwork. Nominal visible heights are450 and480 respectively.

For settled retail title frame134, the skyline's source-to-stage transform is
`stage_x=source_x`, `stage_y=source_y-134`. The master preview therefore uses
source rows134..450 and leaves the rest for the other layers. This avoids
presenting a raw512-high atlas as if it were the displayed480-high screen.

## Raw generation did not establish palette fidelity

Comparing corresponding source pixels with maxRGB>16 (after reducing each raw
generation to source dimensions), median CIELAB deltas were:

| Draft | ΔL* | Δa* | Δb* |
|---|---:|---:|---:|
| Skyline initial | +2.97 | -0.96 | +1.93 |
| Skyline color revision | +2.09 | -0.85 | +1.66 |
| Wall initial | +4.64 | +0.68 | -0.04 |
| Wall color revision | +0.10 | +1.08 | -0.64 |

Positive b* means yellower, positive L* brighter. Shape changes influence these
unweighted comparisons; the scores do not prove fidelity. They still demonstrate
why a prompt alone is insufficient for D124. The skyline stayed brighter/yellower
after a targeted revision. Raw generated lettering and geometry are unapproved.
These raw images were not installed or imported as usable replacement tiles.

## Conservative source-color technical assembly

`palette-proof.py` is a private experimental processor, not a new production core
or a change to the workshop import contract. Its file hash, parameters, masks,
input/output hashes and invariants are retained in `palette-proof-receipt.json`.

It uses the accepted faithful4x image as its base, copies its spatial Lab a*/b*
values and broad lighting, and permits only a bounded fine-luminance difference
from the generated revision:25% strength, maximum1L*, radius8output pixels.
Dark/empty pixels, alpha, full source padding, outer/seam guards and the poster
graphics region are technically protected. Gamut-clipped pixels fall back to
faithful. This deliberately produces a subtle trial rather than adopting the
generated broad contrast, tint, invented lettering or shifted silhouettes.

Both assembled trials have median ΔLab=(0,0,0) against faithful4x. Mean added
red/yellow shifts are each below0.01Lab; quantized individual pixels can differ
slightly in chroma when luminance changes. Protected pixels and alpha are
byte-identical. This is evidence for the implemented constraints, not proof that
the generated detail is authentic or visibly preferable to faithful HD.

Imagegen returned near-original canvas aspects, not the exact requested pixel
sizes. This trial explicitly normalizes each full canvas before the bounded
detail assembly and records actual sizes (skyline1402x1122, wall992x1586).
Raw files are retained. The workshop's strict same-aspect validation was not
weakened; any future reusable normalizer needs explicit bounds/provenance/tests
in DefJam-Modding-Tools. The imported image is described as a technical derivative,
with raw input and recipe references, not falsely labelled the raw model output.

## Existing workshop imports and preservation

| Source | New unapproved revision |
|---|---|
| prestart name2,512x512 | `r-79d5b7230a144039` |
| prestart name1,128x512 | `r-a0377c04607f46ab` |
| mainMenu name5,256x512 | `r-3403319ed9c449ad` |
| mainMenu name4,64x512 | `r-2aec9aa2ab7b4144` |

Choose Full texture library in the workshop and search `prestart.xsh` or
`mainMenu.xsh`; the new source-palette trial is in the Rich Original Detail lane.
Original/faithful references remain unchanged. No trial is approved, selected or
game-verified. The review page offers joined comparisons because the workshop
still shows individual source textures and cannot display a wide master.

`workshop-before-menu-import.json` and `menu-import-receipt.json` verify that all
prior candidate records/files references, reviews, queued request records and
saved session were preserved. Candidates13→17; the13old request records remain,
with zero queued. The six new imagegen calls are retained in the private pilot
provenance, not counted as fabricated workshop queue requests. No second writer
was opened against the active workspace.

## Wide master and limits

Two overlapping outpaint panels supplied only the added left/right scenery.
The entire center was copied from faithful HD, uniformly resized to3x preview
scale. The master5120x1536 places that center at x1600..3520. Centered stage crops
at height1440 are1920,2304,2560,3360,5120wide. The master center and each preview's
4:3 center are byte-identical to their corresponding faithful reference.

The initial added sides had visible cloud/building/brightness discontinuities at
their joins; the cloud refinement below supersedes that preview. Exact original-center equality does not make
an outpaint seamless or validate new scenery color. The other title depth layers
and main-menu wide art remain unfinished first-pass work; ultrawide was not
downgraded to a fallback. Do not present this isolated skyline as a completed
Press Start restoration or an in-game widescreen test.

## Validation and next steps

- Four source-mapped tile dimensions are exactly4x; protected poster pixels,
  seams, padding and alpha invariants pass. All inputs/outputs are hashed.
- Five aspect preview center comparisons pass. Browser checks exercised all
  ratio buttons, source-bounds overlay and wall/title comparisons; all three
  displayed images loaded. Existing workshop imports succeeded through its API.
- Parent source hygiene and whitespace checks pass; no runtime tests were run
  because runtime code/builds were unchanged. Existing67tool tests were not
  rerun or claimed as new evidence for this scratch experiment.

Next art review: compare constrained cleanup against faithful at full size, and
review the invented side scenery/scale. Refine the skyline joins with source
palette constraints; then extend the near-city/water-tower layers and the
main-menu dark/lit wall, shutter and surround while keeping graphics live.
Carry21:9/32:9 through that first pass. Promote only a useful reviewed assembly
workflow into the standalone tools with focused tests. Approved art and verified
runtime identities precede opt-in geometry/UV/scissor/loading integration and
the real five-aspect, all-platform game test matrix. Prior decisions and the
[full inventory/plan](menu-ui-restoration-plan-2026-10-17.md) remain applicable.

## Cloud-join refinement after owner feedback

Vlad says color accuracy is good, but identifies visible original4:3 bounds from
cloud shapes that stop/restart at the joins. This accepts the color direction,
not the whole artwork. The [bounded seam audit](menu-ui-seam-audit-2026-10-17.md)
checks the cause: raw outpaint panels redrew the source cloud context, so copying
back the exact faithful center exposed a structural mismatch. The first left-side
prompt also called the adjoining boundary the wrong edge in one sentence.

Two additional built-in imagegen edits use crops of the FINAL assembled master,
with a guide marking generated-side edit areas and a wider reference. The prompts
trace each bright ridge/dark channel's height, tangent, thickness, curvature,
softness and lighting from the real source into retained outer scenery. This
repairs cloud paths before technical boundary matching. Raw1536x1024 outputs fit
their crops exactly; no normalization was applied. All eight pilot calls are
retained across the two provenance files; no workshop queue record was invented.

Private one-sided assembly applies the AI proposals only inside master rectangles
`[1216,380,384,324]` and `[3520,380,384,324]` (x,y,width,height). A harmonic correction
matches each proposal to the surrounding retained pixels and the source-side
normal gradient. It preserves the AI's interior gradients rather than blurring
across the source. The original center x1600..3520 and every pixel outside these
two domains remain byte-identical. This is a private technical experiment, not an
approved general processor. Local shading can change inside the repair domains.

The immediate-column RGB jump in master sky rows420..620 drops from6.37 to0.15
on the left and10.00 to0.145 on the right (out of255). These values partly reflect
the enforced boundary condition: they are diagnostics, not independent proof of
semantic cloud continuity. Native before/after crops and the assembled panorama
show the hard vertical cloud cuts substantially reduced. The solver separately
passes a known linear harmonic ramp, identity and residual checks (<1e-8).
Original-center comparisons pass for all five ratio crops.

The same read-only review51738 now defaults to **Refined cloud joins**, retains
**Initial extension**, and provides left/right close-ups. Browser verification:
all five revised PNGs load at their intended dimensions, both versions switch,
both join comparisons load, bounds toggle37.5% at32:9, previous restoration
comparisons remain available. The faithful4:3 crop is unchanged. Nothing was
imported into8768 during this refinement; the four previous trials remain there.
No approval, selection, export, game installation, game run or runtime changes.
Both repositories pass whitespace and AGENTS/CLAUDE equality checks; source
checkers report324tracked files/0publication failures and66source files/0policy
failures. No broad runtime or authoring-core regression was needed for these
private image derivatives and source-only prompt documentation.

Evidence: `logs/ui-restoration-20261017/pilot-v1/seam-v2/`, including exact prompts,
raw outputs, contexts/guides, `assemble-bridges.py`, `generations.json`,
`assembly-receipt.json`, both master versions and all aspect previews (the parent
master remains in pilot-v1). The refined master is
`seam-v2/title-skyline-wide-master-refined.png`; compare
`seam-v2/cloud-joins-before-after.png`. Stable33-file refinement overlay:
`/Users/vlad/Code/defjam-data/scratch/menu-ui-pilot-20261017-seam-v2/`, hash-verified;
original47-file v1 archive remains untouched. To reopen independently, copy v1
to a new directory, then overlay the new `seam-v2/` and `review.html` there.

Future background prompts use the source-only manual scaffold
`/Users/vlad/Code/DefJam-Modding-Tools/prompts/menu-widescreen-extension.txt` and
`docs/widescreen-backgrounds.md` in that authoring repository. It requires final
composite context, unambiguous side coordinates, material-specific feature
continuation, exact protected assembly and visual checks at BOTH repair-band
edges at native/coarse scales and every aspect. It is not automatically wired
into the workshop queue. Continue owner art review, then remaining title/menu
layers and the existing runtime integration plan.

## Right join, full-height follow-up

Vlad accepts the left repair but still sees the right bound. The previous right
domain ended at master y704: lower cloud streaks and horizon haze remained hard
composited below it. The earlier close-up ended at y800 and did not expose enough
of that lower join. Its upper-sky metric was insufficient for this whole-height
problem. Do not treat the earlier page/metric check as owner acceptance.

One additional built-in imagegen edit (nine pilot calls total) uses the final v2
crop with an expanded guide and explicit lower-cloud/horizon instruction. The
same harmonic boundary method now covers generated-side master rectangle
`[3520,380,512,580]`, down to y960. The entire accepted left side, original4:3
center and every pixel outside that right rectangle remain byte-identical to v2.
All five aspect centers pass. Full-height native crops and16:9/32:9 panoramas were
inspected. Lower join mean RGB jumps in y704..850/y850..960 fell34.59/20.59 to
1.92/0.00; boundary matching partly enforces this, so inspect structure as well.

Review51738 now defaults **Refined cloud joins · full height**, retains both
previous repair and initial extension, and shows768x674 close-ups through y1024.
All five current PNGs loaded at intended dimensions; previous/current switching
and both close-ups verified in browser. No workshop import or live8768 changes.
No selection, whole-texture approval, installation, game run or runtime changes.

Evidence: ignored `logs/ui-restoration-20261017/pilot-v1/seam-v3/`, including
`right-full-height-prompt.txt`, target/guide/raw, `assemble.py`, `receipt.json`,
`title-skyline-wide-master-v3.png`, all five previews and previous/current full
right close-ups. New stable private archive:
`/Users/vlad/Code/defjam-data/scratch/menu-ui-pilot-20261017-seam-v3/` (overlay
after v1 and seam-v2, old archives unchanged). Reusable authoring guidance now
requires full-height coverage and inspection down to the horizon and padding,
with separate lower-sky checks instead of one upper-sky metric.
