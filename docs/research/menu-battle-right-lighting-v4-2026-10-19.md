# Battle setup upper-right lighting v4 — 2026-10-19

D148 records Vlad’s feedback that v3 brick texture is better and the remaining
issue is the upper-right lighting at the original 4:3 boundary. This is a local
lighting refinement, still awaiting artwork review. It does not approve setup
artwork or change the 39 approved fighter-background files.

Private artifacts: `logs/ui-restoration-20261017/pilot-v1/battle-right-light-v4/`.
Final master `cmtype-wide-v4.png`; five `cmtype-<ratio>-v4.png` centered crops,
two join proofs, two RGBA extension layers, and upper-right before/after proofs.
Review http://127.0.0.1:51738/review.html defaults to v4 and retains v3/v2/v1.
The new “upper-right lighting before / after” disclosure compares v3 and v4.

## Lighting correction

One built-in imagegen edit (85 pilot calls total), no CLI fallback. The target
is master x3040..4640, y0..720, including 480px of authoritative original context.
The prompt asks only for cool ceiling/wall illumination continuity, retaining
texture, lines, geometry and the original color temperature. Exact prompt,
reference hash, raw output and adoption notes are in `generations.json`.
The raw proposal is normalized uniformly to 1600×720. Its painted pixels are
not directly used: only a Gaussian32px low-frequency luminance guide, with gain
clipped to 0.85..1.15, is transferred to the existing v3 RGB. Thus the model’s
fine texture/geometry changes cannot enter the final master.

Native adjacent 24px bands control the illumination at the join, Gaussian12px
row profiles and gain bounded 0.55..1.5, decaying horizontally over 480px.
A smooth outer fade covers x1120..1600 in the extension. The wall adjustment
fades vertically from y256 to640. A separate broad ceiling correction carries
the native x3360..3520 row illumination into the added beam using a Gaussian48px
field, gain0.6..1.4, horizontal onset0..96 and vertical fade128..224. This removes
the unsupported brighter ceiling patch beyond the immediate seam.

All corrections multiply R/G/B by the same local scalar, preserving their cool
channel ratios to rounding. No source pixels are graded; no geometry is moved,
no detail is blurred or repainted. Only x3520..5120,y0..640 may change. The whole
v3 left and native 4:3 center, plus the right below640, are byte-identical.
`assemble.py` and `assembly-receipt.json` document exact parameters and hashes.
Matching profiles supports review; it is not a claim of seamless visual acceptance.

## Verification and handoff

1019 prior PNGs and 39 approved fighter file pins unchanged. All five crop
centers match exactly; left/center and right below640 exact. Browser checks:
five current ratios × six comparison/join images load; three older setup
revision paths and three approved fighter paths load, with approval labels and
revision lock retained. Two lighting comparison images load. Wipe50%, center
1920/aspect lock and no horizontal overflow verified; console warnings/errors
empty. Review screenshot retained as `review-proof.jpg`. No game/runtime test
is required for this private artwork-only revision; actual game checks remain.

New private archive overlay: `<data>/scratch/menu-ui-battle-right-light-v4-20261019/`.
Apply to a copy of the earlier pilot. Raw, target, prompt, recipes, input v3,
review history and state records retained; earlier archives remain immutable.
Authoring workflow/state synchronized. Next: owner visual review of v4, then
native UV/scissor/portraits/lighting/transitions and five-ratio in-game checks,
Options scrolling and conditional Mode Select right-border darkness.
Writer8768, 17 workshop candidates and Trejo are untouched. No import, install,
game run, build, runtime/core edit, commit or push.
