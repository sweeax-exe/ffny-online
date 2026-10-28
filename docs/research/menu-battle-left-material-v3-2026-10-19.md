# Battle left brick material v3 — 2026-10-19

D147 records Vlad’s rejection of the remaining material difference in setupv2.
This pass replaces the complete generated left brick surface rather than another
narrow strip. It is a review draft,not approved artwork. The original4:3center,
ENTIREright half of the v2scene and retained top78rows remain byte-identical.
The39approved fighter-background files and991prior PNGs remain unchanged.

Private artifact root:
`logs/ui-restoration-20261017/pilot-v1/battle-left-v3/`.
Final master `cmtype-wide-v3.png`;five`cmtype-<ratio>-v3.png` crops,two join
proofs,two separate extension layers,`full-left-proof.png`,and full-height
`left-before.png`/`left-current.png` comparisons. The51738review defaults to v3;
v2/v1history,original/wipe/center/bounds and approved fighter paths are retained.
A new leftmaterial detail panel shows before/after at the same scale.

## Why the approach changed

The v2patch left brighter/coarser generated material beside the original and
retained the older material farther left. Source color alone and matching a
single edge did not establish matching brick surface appearance. The new target
contains only native brick context at right208px and blank1600px to the left;
no old rejected AIbrickwork is supplied as a material reference. Course-height
guides and originalbrick references control the request. Prompts explicitly
exclude raised/chalky mortar,coarse porous masonry,warming/yellowing,new corners
and additional objects. Existing sourcepixels remain authoritative.

Two built-in imagegen calls (84pilot total),noCLI fallback. First source-only
outpaint is adopted through material processing. A native-game-style retry
continued to emphasize porous masonry and added a bottom floor horizon;rejected.
All exact prompts,inputs/raw hashes,sizes and adoption notes are preserved in
`generations.json`,with raw/reference/guide files beside it.

Several technical assemblies were inspected and rejected: height-window
lightness quantiles with a wide harmonic correction introduced a bright band;
global context transfer and power-spectrum matching alone left the material
mismatch visible. Scripts and images remain clearly named studies/history.
They are not final candidates. Do not repeat the wide additive correction merely
because its pixel-preservation tests pass.

## Adopted material processing

`assemble.py` copies/normalizes the selected raw then invokes
`edge-material-study.py` (despite the retained study filename,this is the adopted
processor pinned by the final receipt). Generatedbrick sampling is normalized to
nativepixel density:534×480then1600×1440. The actual original strip adjacent to
the border (masterx1600..1696) controls the material. Lab means/contrasts from that
96pxsource band and the512pxgenerated neighbor are measured as rowprofiles,
smoothed with Gaussianσ64px. The affine material response transfers these native
color/illumination/contrast statistics over the ENTIREnewleft;gain clipped0.3..1.6.
No sourcepixels are blurred,graded,repeated or warped. Generated phase/structure
is retained;there is no vertical course remap or copied source-material strip.
An8pxadditive seam condition meets the sourceedge;first16generatedbrick rows
blend to the retained beam. Exact profiles/gains are in
`edge-material-study.json`. This targets both native sampling and material
contrast/shading,not only a single border pixel or global color mean.

Only masterx0..1600,y78..1440 changes. The originalcenter,complete v2rightside,
ceiling repair,all other textures and prior revisions stay exact. Material
texture authenticity and join quality still require owner visual review;
statistical matching is supporting evidence,not a seamlessness guarantee.

## Verification and retention

991prior PNGs,39fighter pins,originalcenter/fullright/top78 and all5cropcenters
pass exact preservation checks. Browser30scene/revision/aspect cases×6images
load;the2left before/after proofs load. Wipe50%,center1920/aspectlock,approval
labels and narrow-pane layout pass;no console warnings/errors or overflow.
No game/runtime tests were run for this private artwork-only revision.

New private overlay:
`<data>/scratch/menu-ui-battle-left-v3-20261019/`.
Apply to a COPY of the previous pilot. It retains final/rejected attempts,
processing tools,v2input,updated page and source workflow records;earlier archives
are immutable. `archive-receipt.json` pins its manifest. Authoring workflow/state
are synced. Workshop8768/17candidates/Trejo remain unchanged;no import,install,
game run,build,runtime/core edit,commit or push. Next ownerreview v3leftmaterial;
retain approvedfighter art and v2right. Native integration/game checks remain later.
