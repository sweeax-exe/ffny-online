# Battle setup material and ceiling refinement — 2026-10-19

D145 records owner approval of the2-,3-,4-fighter selection backgrounds.
`battle-fighter-approval-20261019.json` pins39exact files (13each:restored mosaic,
2source-shaped splits,master,5aspect crops,2join proofs,2extension layers).
These pixels and all963prior PNGs remain unchanged. Native game validation is
separate. D146 records the requested setup corrections: existing left course
alignment is good but brick material differs; right wall is good but the ceiling
line has a small mismatch. Setup remains unapproved pending review of this v2.

Private outputs are under
`logs/ui-restoration-20261017/pilot-v1/battle-setup-v2/`.
Current master `cmtype-wide-v2.png`,5`cmtype-<aspect>-v2.png` crops,2join proofs,
2separate extension layers and focused material/ceiling comparisons are saved.
The review at http://127.0.0.1:51738/review.html defaults to setupv2 with previous
v1 available; fighter selections retain exact approvedv1paths and approval labels.
Original comparisons,wipe,center lock,both full-height joins and a focused
previous/current ceiling comparison remain available. The revision control is
bounded to pane width to prevent horizontal overflow on a narrow pane.

## Recipe and retained attempts

3built-in imagegen edits (82pilot total),noCLI fallback. Every exact prompt,
input hash,raw hash/size and adoption note is in `generations.json`;copies of all
references,guides,raws and rejected passes are saved. The first left filled-target
material edit copied existing vertical smear and was rejected. The adopted
left clean-gap retry uses original234×1440brick material reference and26mortar
height guides. Only generated x1088..1600,y78..1440 is adopted,with Gaussian1px
softness and one-sided boundary-color reconciliation. No vertical course remap,
source-reflection strip or edge-hold support is used. This removes the earlier
warp-driven smear rather than retaining it as surface texture. Brick courses are
guided,not authorized for redesign; original source art remains the authority.

The right ceiling edit sees a2080×720crop with480pxfixed source context.
Only master x3520..5120,y0..256 is adopted. A source/proposal vertical-gradient
comparison overy70..170 measured a1pxoffset; generated beam pixels are registered
up1px,easing the shift away below160px. A smooth bottom blend overy192..256
returns to the existing good wall. Original4:3center and ALLright pixels below256
are exact. Ceiling color reconciliation affects generated pixels only; no source
blur,grade or geometric change. Original cool dark palette/no warming remains
explicit in every prompt. Source contexts from generated outputs are discarded.

`assemble.py` is the final recipe. Run it with the existing Pillow/NumPy tool
venv;it reads preserved raw paths/results and the earlier v1master plus boundary
helper. `prepare.py` documents input/approval preparation;`update-review.py` reads
its immutable review-before snapshot. Do not rerun older builders over current
approval labels. Prior artwork and archives remain immutable.

## Validation and next step

39approved fighter pins and963prior PNG hashes exact;5aspect centers exact;
original center,good right wall below256 and all unrequested pixels exact.
Browser25scene/revision/aspect cases×6images load,including both setup versions
and all3approved fighter screens;2ceiling proofs load. Wipe50%,center1920,
disabled aspect while centered,approved labels/version lock and narrow-pane
layout pass;zero console warnings/errors. These checks establish preservation
and working comparisons,not owner acceptance of material authenticity.

A new private overlay
`<data>/scratch/menu-ui-battle-setup-v2-20261019/` preserves this revision,
39approved fighter files,the v1input/helper,updated page and source workflow.
Apply to a COPY of the earlier pilot;prior archives are unchanged.
`archive-receipt.json` identifies its exact manifest hash.

Next owner review of setupv2. Retain the approved2/3/4fighter artwork. Native
UV/scissor integration,portrait registration,lighting/transitions,all5aspect game
captures,Options scrolling and conditional Mode Select right-border darkness
remain later work. No workshop writer8768/17candidate/Trejo changes,imports,
installation,game run,build,runtime change,commit or push occurred.
