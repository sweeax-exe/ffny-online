# Battle setup upper-left lighting v5 — 2026-10-19

D149 records the owner's correction: the remaining lighting issue is the LEFT
border, and the right border looks fine. V5 uses v3 as its base, restoring the
ENTIRE pre-v4 right side exactly. V4 remains clearly labelled reverted history.
The improved brick surface is retained; this pass changes only local upper-left
illumination. Setup as a whole remains a review draft; the 39 approved fighter
files remain frozen.

Private artifacts: `logs/ui-restoration-20261017/pilot-v1/battle-left-light-v5/`.
Final `cmtype-wide-v5.png`, five centered `cmtype-<ratio>-v5.png` crops,
left/right join proofs and extension layers, `left-light-before.png`,
`left-light-current.png`, `full-left-light-proof.png`, `review-proof.jpg`.
Review http://127.0.0.1:51738/review.html defaults v5, keeps v4/v3/v2/v1,
and adds “upper-left lighting before / after”. The right-lighting v4 panel is
explicitly labelled reverted history rather than the current repair.

## Prompt and assembly

One built-in imagegen lighting edit, 86 pilot calls total, no CLI fallback.
Target x640..2080,y0..720 includes the actual added brick at localx0..960 and
fixed ORIGINAL context at localx960..1440. The prompt explicitly names LEFT,
the x960 join, authoritative RIGHT480 context, darker native shadow continuity,
unchanged brick detail/mortar/beam/objects, and original cool gray/olive/teal
colors without yellowing or warmth. Exact prompt/reference/raw hashes and
adoption notes are saved in `generations.json` and `prompt.txt`.

Generated pixels are not directly pasted. The raw is uniformly normalized to
1440×720. Gaussian32 luminance fields supply a lighting-only gain bounded
0.55..1.15. Original adjacent brick x1600..1664 and generated x1536..1600,
Gaussian16 row profiles, control the join lighting; edge gain bounded0.55..1.4
decays320px into the added left. The adjustment begins smoothly atx640..800
and fades vertically fromy480..720. The same scalar multiplies existing R/G/B,
retaining cool channel ratios to rounding and all material positions/detail.
No new geometry, texture, alpha or source-center pixels are adopted from the
model. Only x640..1600,y0..720 may differ from v3. Right, originalcenter,
leftof640 and below720 remain byte-identical. `assemble.py` and
`assembly-receipt.json` pin the exact recipe and final master.

## Verification and next steps

1035 earlier PNGs and 39 approved fighter file pins unchanged. All five crop
centers exact; ENTIRE pre-v4 right, center and pixels outside repair exact.
Browser: five current ratios × six comparison/join images, four setup history
paths and three approved fighter paths load. Approval labels/revision lock,
two lighting proofs, wipe50%, center1920/aspect lock pass. No horizontal
overflow or console warnings/errors. Source-publication checkers and instruction
twins pass. No runtime tests: this is private artwork only.

A new private overlay `<data>/scratch/menu-ui-battle-left-light-v5-20261019/`
retains inputs, raw, recipe, history snapshot, current review and state records.
Apply to a COPY of the prior pilot; older archives remain immutable.
Authoring workflow/state synchronized. Owner visual review remains next.
Later native UV/scissor/portraits/lighting/transitions and all-five-ratio game
captures, Options scrolling and conditional Mode Select right-border darkness
remain pending. Writer8768/17candidates/Trejo unchanged; no import, install,
game run, build, runtime/core edit, commit or push.
