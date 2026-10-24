# Title logo / emblem transparency repair — 2026-10-18

Vlad liked the sharper generated artwork but found missing see-through regions
on checkerboard. The raw AI variants had interpreted authored openings and
translucent ink as opaque black. The existing restrained/source-shaped variants
already retained Faithful HD alpha exactly; substituting those would not repair
the particular artwork he liked.

Private evidence: `logs/ui-restoration-20261017/pilot-v1/transparency-v2/`.
The read-only review remains `http://127.0.0.1:51738/review.html`.

## Coverage is independent of color

The [source probe audit](menu-branding-transparency-audit-2026-10-18.md) pins
29 semantic probes. Native title F/I/G/H/T interiors have alpha129/255; the D
counter is graded. Some dark distressed material is fully opaque. Green, black
and pink RGB can all occur at alpha0. Neither a black key nor a green key is a
valid substitute for the actual alpha plane. The initial title prompt wrongly
described the large lettering as black; that wording is superseded, while the
exact historical prompt remains unchanged in provenance.

## Retry and final technical assembly

Nine new built-in ImageGen calls used `transparent_background=true`, the liked
raw draft as edit target, a magenta-backed original coverage reference, and a
grayscale alpha guide. Exact prompts, input/output hashes and untouched outputs
are in `generations.json` and individual prompt/edit PNG files. Pilot count is
now36. No API/CLI fallback was used.

The retry improved some areas but still left the speaker's dark center filled,
retained a trophy halo, and changed RGB/detail despite the alpha-only request.
Those outputs remain evidence, not the final repair. A prompt is not an alpha
lock. Direct affine source-alpha copying was also rejected: slight differences
in generated contours left visible ghosts. Diagnostic proofs are retained.

Final drafts retain the previous raw RGB **byte for byte**, at its original
returned canvas size. Original coverage is registered locally to that artwork:

1. `prepare-guides.py` places the Faithful HD alpha>=64 bounding box into the
   target's bright-material box. This is an initial reference transform only.
2. `register-alpha.py` refines source-to-target structure by local block matching
   at maximum dimensions96/192/384, three iterations each, radius3 offset search,
   radius5 window cost, radius3 displacement smoothing and .0007 offset penalty.
   Registration uses source RGB multiplied by independent coverage, excluding
   transparent filler. Full-resolution backward displacement fields are retained
   in nine NPZ files. Registered coverage uses bilinear sampling; RGB is not warped.
3. `assemble-revisions.py` copies the liked raw RGB and uses
   `min(previous_raw_alpha, registered_original_alpha)`. This restores missing
   coverage while protecting previously clear areas. These are technical
   derivatives of AI artwork, not raw ImageGen outputs.

This is an art audition on the generated canvas. It does not establish native
UV geometry or replace source-shaped runtime assets. Source/Faithful/restrained
references and the21 earlier source-shaped split tiles remain unchanged.

## Verification and review

All nine RGB planes and alpha recipes pass exact comparison. All27 prior
source/Faithful/restrained hashes and41 approved/conditional scenery pins match.
Transformed probes check partial title lettering, clear openings, opaque metal,
dark distress and padding.28 semantic classes pass; one documented cap exception
keeps an already-clear disc-center feather at0 rather than adding the registered
original's alpha8 (native alpha5). There are no unexplained probe failures.
Probe positions and subpixel inversion errors are saved in `verification.json`;
they do not imply exact native-coordinate equivalence or perfect edge fidelity.

The review defaults to Transparency repair v2 on checkerboard. Original,
Faithful HD and previous generated draft are selectable left references;
restrained and old raw candidates remain available. All54 repaired asset ×
reference × channel comparisons load; dark/light/checker controls work and the
browser reports zero warnings/errors. Nine individual checkerboard previews and
light/dark proofs were inspected. The halos and major filled openings are
corrected; final aesthetic acceptance and runtime alpha/geometry remain review
steps. New nine candidates remain unapproved.

Reusable prompts must distinguish clear holes, partial ink and opaque distress,
supply the actual independent alpha, forbid baked backgrounds, and require
checker/light/dark plus alpha inspection before review. Preserve every retry and
technical transform. Do not describe a flat dark preview's black areas as ink.
For future runtime assets, preserve source-shaped alpha exactly after registering
RGB; this raw-canvas audition must not become an automatic export preset.

New private overlay: `<data>/scratch/menu-ui-transparency-20261018/`, with its own
SHA256 manifest and the current review HTML. Earlier archives remain immutable.
The workshop writer/session,17 existing candidates, approvals, runtime and game
installation were not changed. No build, game launch, commit or push.
