# HD detail preservation assessment — 2026-10-08

## Owner feedback

After reviewing the 40-image corpus, Vlad prefers Lanczos and RealESRNet over
RealESRGAN, which looks generated/fake. RealESRNet can look cleaner at extreme
zoom but removes Blaze's jersey perforations, changes denim, and smooths
weathered wood. Lanczos is his suggested first approach. This is a direction
for further work, not approval of every asset, a global scale, or the release.

## Findings and recommendation

The exact source crops reproduce the concern. The official model zoo explicitly
describes RealESRNet_x4plus as MSE-oriented with over-smoothing effects:
https://github.com/xinntao/Real-ESRGAN/blob/master/docs/model_zoo.md
Interpreting the weave/grain as unwanted high-frequency variation is a plausible
explanation, not a measured statement about the model's internal decisions.

Use Lanczos as the proposed faithful initial pack baseline. It interpolates
existing artwork rather than recovering authentic missing high-resolution art.
Inspect haloing, alpha edges, mip transitions and material appearance in-game;
an enlarged image alone does not establish the benefit or correct scale.

Keep neural processing selective. Build Lanczos and neural branches independently
from the original, then blend outputs or use reviewed material-region masks.
One fighter atlas contains skin, jersey, denim, text and jewellery: whole-image
or category-level selection cannot express all of these differences. Preserve
jersey holes, denim weave, grain, lettering and identity on the source-based
branch. Feather masks carefully and check UV borders/mips in-game. Do not
cascade another model after RealESRNet expecting it to restore deleted detail.
That second prediction cannot reliably reconstruct the original pattern.

## Local four-region experiment

Scratch generator: C:/Users/Vlad/code/defjam-hd-data/hd-work/detail-probe.py.
Artifacts: hd-work/representative-40/experiments/detail-preservation/index.html,
comparison.png and verification.json, served at
http://127.0.0.1:8766/experiments/detail-preservation/index.html.
The existing 40-image gallery, review identity and stored choices are untouched.

Four 64x64 source regions: Blaze jersey, Blaze denim, weathered wood, concrete.
Five columns: original nearest-display enlargement, Lanczos 4x, RealESRNet 4x,
75% Lanczos + 25% RealESRNet, and a fine-detail-preserving experiment:

    F = L + 0.25 * (blur(N) - blur(L))

Here L/N are independent 4x outputs; Gaussian radius is 3.2 output pixels
(0.8 source pixels). Computation uses 32 output pixels of surrounding context,
8-bit display RGB, one final rounding/clipping, and unchanged Lanczos alpha.
This is a proof of concept, not the final production colour pipeline. Input and
output hashes, dimensions and byte-identical derived alpha are verified.
The manifest counts clipped channels over each padded context; these are not
quality scores. No new inference or model installation was needed.

Visual inspection: the 25% parallel blend retains much more weave/perforation
than pure Net; the final column keeps still more and is quite close to Lanczos.
It also retains original noise/dither. Small visual benefit and possible clipping
mean this recipe is an optional candidate, not an automatic improvement or proof
that every source detail is exactly preserved. No in-game hybrid validation yet.

## Additional candidates, if the hybrid does not justify itself

- SwinIR's classical SR checkpoint, rather than its real-world GAN checkpoint,
  is a reasonable independent candidate to audition on these same regions.
  Not installed, benchmarked or demonstrated superior here:
  https://github.com/JingyunLiang/SwinIR
- Real-ESRGAN's Python runner supports adjustable denoising only for
  realesr-general-x4v3. It is not a RealESRNet_x4plus strength knob and is not
  established as supported by the installed 20220424 NCNN executable:
  https://github.com/xinntao/Real-ESRGAN/blob/master/inference_realesrgan.py

Next comparison should use normal viewing scale and in-game cameras/mips as
well as extreme zoom. Retain separate experimental neural/hybrid recipes and
alpha policies; no face restoration. Model exploration must not become a gate
on implementing reliable HD replacement support. Release gates from
hd-textures-notes.md remain pending; this turn adds no native code/build claims.
