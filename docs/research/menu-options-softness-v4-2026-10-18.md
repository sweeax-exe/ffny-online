# Options right-side softness v4 — 2026-10-18

Vlad says v3 is almost perfect and requests only slight blur/depth of field on
the right to match the original door/brick (D142). New v4 is a filter-only child
of v3;Options remains pending final art review. Approved Jukebox is unchanged.

Private evidence: `logs/ui-restoration-20261017/pilot-v1/options-softness-v4/`.
One built-in imagegen edit supplies an optical-softness reference,57pilot calls
total. Exact prompt,target,guide,original soft reference,raw and hashes retained.
The model output is NOT adopted as replacement art:the final projects its blur
strength onto actual v3 RGB to retain the liked door width,wear and brick paths.
No repaint,new detail,global grade or geometry warp enters the final.

Final operation:Gaussian radius2 at5120×1440,only master x[3520,5120),full height.
Smoothstep0→1 over the first64generated pixels eases the onset;original4:3edge
and all pixels left of3520 are exact. The filter is spatial averaging of existing
RGB,not an exposure/color treatment. Mean right-sideRGB delta is
[-0.00271,-0.00218,-0.00847] on0..255. This is a baked texture defocus,not a new
runtime camera or renderer depth-of-field feature.

Radius2 selected from1..3.5trials using local wall high-frequencyRMS(radius2).
Raw softness-referenceRMS1.573,projected radius2RMS1.430,unblurred v3RMS4.348.
These guide a modest strength;visual judgment remains authoritative. Recipe,
input/output hashes and mask coordinates in assembly-receipt.json;assemble.py
reproduces final without generative repaint. Raw normalized to2080×1440 only for
the reference diagnostic,never pasted into final.

Full door,right boundary and5aspect contact sheet inspected. Verification:
395prior PNG hashes exact,11approvedJukeboxpins exact,accepted Optionsleft+
center exact,5aspect crops/centers exact,meanRGB drift below0.05each channel.
Browser30menu/revision/aspect combos×4images and4joinpairs×2images load;wipe50%
andcenter lock work,zero console warnings/errors. Review51738 defaults current
Optionsv4 andapprovedJukeboxv2;v3/v2/v1 history retained. Options before/after
is v3→v4. All prompts/recipes remain linked;no workshop writer changes.

Private overlay `<data>/scratch/menu-ui-options-softness-v4-20261018/` retains
new batch,review page,prior v3 input,source/toolchain snapshots and manifest.
Apply toCOPY of previous pilot tree;old sections depend on prior files. Old
archives immutable. Authoring workflow/prompt and both handoffs synchronized.
Native UV/independent Options scroll/lighting/transitions/all5ratio game checks
remain later work. Battle is next authorized group. Noimport,selection,install,
game/build/runtime/core change,commit orpush;writer8768/17candidates/Trejo intact.
