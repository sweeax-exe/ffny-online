# Title skyline: cloud continuity at the original-frame boundaries

Read-only follow-up, 2026-10-17. Read current `AGENTS.md`, `PROGRESS.md`, the pilot report, exact left/right prompts and assembly receipt; inspected the faithful center, both raw generated panels and the assembled master. No images were edited or written, no imports/game/build/server/core changes were made. Only this report was written.

## Finding

The original 4:3 region is visible because cloud structures do not continue across its sides. The accepted overall palette and exact original-center preservation are separate successes; neither establishes cloud continuity. The final master has abrupt changes in which bright/dark cloud masses occupy each height at x1600 and x3520. Blending only the immediate edge cannot create the missing cloud paths.

The main agent's proposed focused image-generation edits to the generated-side bridge bands are appropriate. The edit must be conditioned on the **final composited faithful center**, with enough sky context on both sides of the proposed repair. Every cloud ridge entering from the immutable center should continue smoothly into the edited side before merging with the retained outer scenery. Recheck the outer edge of each repair band as carefully as the center join.

## Evidence and bounded causes

Inputs are under `logs/ui-restoration-20261017/pilot-v1/`:

- `title-skyline-wide-master-draft.png`: 5120x1536, original center x1600..3519 inclusive.
- `title-center-faithful-preview-scale.png`: exact 1920x1536 center.
- `title-wide-left-raw-v1.png`, `title-wide-right-raw-v1.png`: both 1898x829, normalized to the requested 3520x1536 during prior assembly.
- `title-wide-left-prompt.txt`, `title-wide-right-prompt.txt`, `generations.json`, `wide-assembly-receipt.json`.

Verified causes/limitations:

1. **The model redrew the reference region.** Each raw output is a complete panel containing new scenery and a regenerated interpretation of the original center. In-memory comparison, after the documented Lanczos normalization, gives mean absolute RGB error about 20.36/255 (left) and 20.34/255 (right) in visible reference-region pixels within y396..959 (reference maxRGB>16). Only approximately 0.04%/0.03% of those pixels match exactly. This is not a valid matched overlap. Preserving the true center afterward is correct, but it replaces the cloud context on which those generated sides were visually conditioned.
2. **A textual location promise was used in place of an exact target canvas/mask.** The inputs were the standalone faithful/source images, while the prompt described where the model should place them inside a larger output. No supplied final composite made the immutable center and the seam location unambiguous visually. The raw output dimensions also differ from the requested dimensions, so numerical coordinates in the prompt cannot be assumed to have been followed precisely.
3. **The left prompt has a boundary ambiguity.** It correctly puts the source at x1600..3520 and new scenery at x0..1600, but then says the image's “left boundary” joins the source at x1600. The adjoining boundary is the new region's **right** edge. Remove this contradictory phrasing from reusable prompts.
4. **The prompt asks for general continuity, without enumerating the feature to continue.** “Existing smoky clouds,” palette, band height and building scale are useful, but permit a different arrangement of cloud masses. A cloud silhouette can stop and another can begin at the same average color/brightness. The repaired prompt should require continuing each arriving cloud mass, dark channel and luminous ridge at its existing height, thickness and tangent.
5. **There is no documented measured overlap registration or structural acceptance gate before the hard composite.** The assembly receipt proves exact center pixels and crop behavior. It does not show that the generated clouds agree in a contextual overlap with those source pixels. Replacing the generated center exposes any mismatch immediately.
6. **Independent side generations do not share a single global sky composition.** This can contribute different cloud frequency, direction or lighting falloff on opposite sides. It is secondary to the local mismatched context here, but should be constrained when extending other layers.

The broad aspect is nearly correct: 1898/829 differs from 3520/1536 by about 0.09%. That small aspect normalization alone is not a sufficient explanation for the large cloud-shape mismatch. The model's exact source placement and structural edits are the greater uncertainties. Do not describe the result as a mere one-pixel crop error without a registration measurement.

## Measurements of the draft joins

These are diagnostics of the old master, not acceptance thresholds for all backgrounds. Images were read into memory only. Cloud ROI y410..689 avoids the large black padding and much of the skyline. The source-to-stage vertical offset is -402 at this 3x preview scale, so this ROI is visible near the top of the settled stage.

| Boundary | Adjacent-column RGB difference at join, visible rows | Mean adjacent-column difference in local 256-wide strip, all ROI rows | Interpretation |
|---|---:|---:|---|
| Left, x1600 | 11.48/255 | 0.40/255 | Large artificial join compared with nearby variation |
| Right, x3520 | 19.84/255 | 0.69/255 | Stronger immediate mismatch |

The first column is evaluated only on rows where either seam pixel exceeds maxRGB 8; the local reference includes all rows, so the values are illustrative rather than a statistically normalized anomaly ratio. Both joins also show different broad vertical cloud profiles after smoothing. For example, on 32-pixel Gaussian-smoothed luminance, matching the two 32-wide side profiles improves markedly after vertical shifts of -34 pixels (left) or +14 pixels (right). These are **not instructions to shift artwork**: a single profile conflates natural slopes, cloud thickness and local lighting. They demonstrate that feature arrangement differs over a wider region than a narrow edge feather.

The exact-center array comparison passes. This should remain a separate invariant after repair.

## Seam quality: separate four questions

1. **Preservation:** are all protected center pixels and alpha identical to the approved faithful input? This is deterministic and should remain an exact test.
2. **Registration:** did the generated edit keep the supplied center, nearby horizon and layer coordinate system in the expected location? Check distinctive cloud/roof landmarks in unchanged context before cropping any output. Reject unrequested zoom, shift or aspect drift.
3. **Signal continuity:** are intensity and spatial gradients smooth across each compositing boundary? Test at several spatial scales, including broad cloud lighting; exclude black padding. A tiny edge delta is necessary but not sufficient.
4. **Structural continuity:** does the *same* luminous ridge/dark cloud channel entering a boundary continue with plausible thickness, curvature and direction? This is the owner's current issue. It needs a cross-boundary contextual view and feature-by-feature inspection; an image-wide mean color score cannot answer it.

Suggested diagnostic checks for future reusable tooling:

- Evaluate the cloud sky separately from buildings, alpha and unused padding. The black canvas must not dominate any average.
- At both original-center boundaries, review 128–512-pixel context on each side at native display scale, then at fit-to-screen size with the 4:3 outline hidden. A visible central rectangle at fit view is a fail even if native edge pixels blend.
- Measure seam-row luminance differences after small/medium/broad Gaussian smoothing. Compare to nearby interior candidate cut lines with similar brightness/edge density, not a fixed global threshold. This distinguishes a broad brightness band from natural textured cloud variation.
- Compare one-sided gradients perpendicular to the seam as well as value jumps. A forced edge-color match followed by a sudden slope change forms a visible halo or flat strip.
- Trace several bright/dark cloud ridges across a wide crop: endpoint height, tangent, thickness and order should remain coherent. Mark which ridge continues into which generated ridge. Flag cloud lobes abruptly truncated, doubled or redirected along the vertical seam. These semantic checks remain visual until a useful validated detector exists.
- Inspect at multiple blur/downsample levels. A rectangle surviving coarse viewing is low-frequency structural/lighting failure; sharpening or grain will not fix it.
- Check both boundaries of a bridge edit: at the immutable center and where the bridge meets retained outer-side artwork. A repair can merely move the seam outward.
- Inspect all four widescreen crops. At 16:10 the extension is narrow and the seam is near each edge, which makes a broad mismatched side band particularly conspicuous; ultrawide shows whether it flows into the larger side composition. The 4:3 crop must remain exactly unchanged.
- Preserve provenance for raw candidates, exact edit context, edit-mask/allowed-region coordinates, normalization, final composite and test measurements. Call a candidate seamless only after both signal and visual structural review.

## Reusable prompt invariants

Use these as an ordered compact block attached to the concrete image/mask, rather than relying on a long list of style negatives:

> Edit the supplied final composite in place. Its pixel coordinate system, crop, scale, horizon and camera remain fixed. The central approved original is immutable context; do not regenerate or reinterpret it. Change only the specified generated-side bridge region. At the original boundary, continue each existing cloud ridge, dark channel and luminous cloud mass at its current height, width, edge softness, direction and curvature; each shape crosses the boundary as one continuous shape. Continue those structures naturally through the bridge into the retained outer scenery. Avoid vertical composition boundaries, replacement cloud fields, duplicated cloud lobes, uniform blur bands and invented bright areas. Match the local source palette, broad lighting and softness already approved. Keep all source black padding and alpha behavior unchanged. No new objects or lettering. Preserve all unedited context exactly; return the full edit canvas without shifting, cropping or zooming.

For the current master, make the numeric immutable interval explicit as `1600 <= x < 3520`, width1920, and give actual bridge coordinates from the prepared edit target. Always identify the correct adjoining edge: left extension's **right edge** meets the center's left edge; right extension's **left edge** meets the center's right edge. If the tool cannot enforce a literal mask, deterministic final compositing must enforce the allowed region and exact center, followed by a new boundary review. That enforcement does not itself repair any mismatched generated features.

For a narrow bridge crop, specify the center-boundary x coordinate *in that crop*, its immutable side and the far-side continuity target; do not reuse full-master x values. Supply the already accepted outer scenery as context too. If the crop hides the trajectory of a large cloud, widen the context rather than merely widening the blurred blend.

## Advice for the current repair

Proceed with cloud-structure edits in generated-side bands, leaving the approved 4:3 pixels exact. Prefer a band wide enough to carry the source cloud into the retained side without an abrupt bend, and avoid a hard vertical feature cutoff at the far edge. Do not change the original center to hide the join, and do not use a global color correction as the primary response to this feedback. The user has accepted the overall colors and identified shape continuation as the problem. Local light differences can still be evaluated as consequences of mismatched clouds.

A soft composite ramp can remove residual pixel noise only after the cloud paths already align. It cannot replace the structural repair. The result remains an isolated skyline-layer artwork audition until the other title layers, animations and runtime rendering are integrated and checked.
