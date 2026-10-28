# Jukebox approved; Options reference door v3 — 2026-10-18

Vlad explicitly approves current Jukebox v2 (D140). Private approval manifest
`pilot-v1/jukebox-approval-20261018.json` pins11PNGs:master,5aspect crops,2join
proofs,source-shaped backdrop restoration and2source-tile splits. No original
references,previous variants,live bottom strip/mask/lighting or game acceptance
are silently approved. Jukebox artwork is unchanged this turn.

Options v2 is still too wide/plain. Vlad supplies a screenshot of the Mode Select
door and asks to use its width and panel texture (D141). New Options v3 changes
only master x[3520,5120),y[0,1440);the accepted left side and all original4:3
pixels stay exact. It remains a review draft. The reference and game source are
private,not source repository assets.

## Reference-driven layout and material

Private evidence: `logs/ui-restoration-20261017/pilot-v1/options-door-v3/`.
The screenshot is retained exactly as owner-door-reference.png with hashes in
generations.json. The source-proportioned-door.png is a730×1110crop of the actual
Mode lit known3× layout (x1020..1750,y270..1380). It is pasted at natural scale
into an EDITABLE layout scaffold:target local x570,y265,not stretched wider.
The real Options source is restored left of local x720 before generation.
Approximate right jamb x1280 gives~710width/~1110height,ratio~0.64. The earlier
wide door instead occupied most of the extension. A large wall field now sits
beyond the narrow doorway. Reference dimensions are visual/layout constraints,
not a claim that this future scene is native game geometry.

Two built-in imagegen edits,56pilot calls total. The first uses the owner
screenshot,real source doorway,layout target and guide. It restores closely
ribbed,stained/painted worn panels rather than broad plain panels. Existing
source orange/silver hardware is completed locally as short hardware;it no
longer extends as long bars across the door. No reference lamp,emblem poster
or menu composition is copied. Source-derived original lighting remains fixed;
new scenery follows the reference's olive material and stays subject to color
review. No global LUT/warming adjustment was added.

A focused second edit bridges the new material to actual original pixels in
master x3520..3776,full height. Both flanks and correct narrow doorframe are fixed
context. Source center and outside bridge exact;only generated-side residual
color/grain matching follows. The fullheight transition can still be judged in
the before/after pane;pixel-preservation checks do not prove visual seamlessness.
The reference wear is a generated interpretation,not a pixel-identical copy.

Raw full-side1591×989 normalizes to2320×1440;raw bridge1295×1215 normalizes to
1536×1440. Full-canvas aspect/landmarks agree within raster rounding. There is
no horizontal squeeze of the old wide door. The natural-width scaffold supplies
geometry before generation. Exact2prompts,inputs/raws/hashes,normalization,
prebridge history and assembly scripts are retained. Run assemble.py then retain
its output as options-prebridge.png,finish-bridge.py,verify.py,update-review.py.
Actual generated outputs are retained rather than reproducible by a seed.

## Review and checks

Review http://127.0.0.1:51738/review.html defaults to current Options v3 and
approved Jukebox v2. Previous Options v2/v1 and Jukebox v1 remain selectable;
redundant v2 option disabled for Jukebox. Status label follows selected art.
Original comparisons,wipe/center,5aspects,PNG links,fullheight before/after,
complete-door and owner screenshot links remain. Options joins compare v2/v3;
Jukebox joins retain v1/v2 comparison. The individual source restoration section
retains its historical generic draft labels;Jukebox approval is authoritative
in the main scene status/manifest.

verification.json:371previous PNG hashes unchanged,11approved Jukebox pins exact,
Options accepted entire left+center exact,5crops+original centers exact.
browser-verification.json:25menu/revision/aspect combinations×4images and4join
pairs×2images load;wipe50%/center lock pass;zero console warnings/errors.
Complete door,original right-boundary close-up and5aspect contact sheet inspected.

Separate private overlay `<data>/scratch/menu-ui-options-door-v3-20261018/` retains
new art/refs/prompts/recipes,approved Jukebox files and source/toolchain snapshots.
Apply only to COPY of previous pilot tree;old sections depend on older files.
Previous archives remain immutable. Authoring workflow/prompt and both handoffs
updated. No source/runtime processor adopted,workshop8768/17candidate changes,
new import,selection,installation,game/build,commit or push. Options independent
scroll extrema,source-shaped UV mapping,live lighting/transitions andall5ratio
game validation remain pending. Battle remains next authorized group.
