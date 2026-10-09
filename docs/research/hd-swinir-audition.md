# SwinIR classical 40-texture audition — 2026-10-08

Vlad finds the Lanczos game pilot decent and is willing to try it across textures,
but requests a SwinIR classical audition before choosing the wider workflow. Small
tattoos/logos are the concern. This turn completes that audition; no whole-corpus
job, pack conversion or model acceptance is inferred from the provisional wording.

## Reproduction and provenance

Source runner: scripts/hd_swinir_review.py in the HD lane. Same 40 sample IDs and
byte-identical original PNGs as the earlier representative audition. Local root:
C:/Users/Vlad/code/defjam-hd-data/hd-work/representative-40.

Official [SwinIR repository](https://github.com/JingyunLiang/SwinIR), commit
6545850fbf8df298df73d81f3e8cba638787c8bd. Checkpoint:
001_classicalSR_DIV2K_s48w8_SwinIR-M_x4.pth from the author's
[v0.0 release](https://github.com/JingyunLiang/SwinIR/releases/tag/v0.0),
59,611,499 bytes; SHA-256
129dc773ba2d4c07f3eb0bb116fbe692011b7cc072d9ca12797cd3748198610a.
This is the classical task, with strict params loading, not the real-world GAN
checkpoint. Architecture/reference details: hd-swinir-classical-research.md.

Isolated tools/swinir-python environment: Python3.13, torch2.8.0+cu128,
torchvision0.23.0+cu128, timm0.9.16, NumPy2.5.2, Pillow12.3.0. CUDA12.8 on
RTX4070TiSUPER16GB. Full dependency lock: tools/swinir-requirements-lock.txt.
Official source/weights remain under tools/swinir, outside source repositories.
The previous owned game preview PID12956 was gracefully closed and absence of
other game processes confirmed before inference. No game build or save was changed.

```powershell
$python = 'C:/Users/Vlad/code/defjam-hd-data/tools/swinir-python/Scripts/python.exe'
$review = 'C:/Users/Vlad/code/defjam-hd-data/hd-work/representative-40'
$swinir = 'C:/Users/Vlad/code/defjam-hd-data/tools/swinir'
& $python scripts/hd_swinir_review.py generate --root $review --swinir $swinir --checkpoint "$swinir/model_zoo/001_classicalSR_DIV2K_s48w8_SwinIR-M_x4.pth"
& $python scripts/hd_swinir_review.py render --root $review
```

FP32, TF32 disabled, whole-image inference; no tiles, ensemble, face restoration
or extra sharpening. Eight pixels of external clamped padding; existing UI/logo
RGB bleed, no world bleed. Official flipped window extension, including an extra
window for aligned sizes, then exact crop. RGB-only neural processing. Effects
retain nearest alpha; others use Lanczos alpha, exactly matching earlier Lanczos
candidates. All atlas dimensions/aspect ratios retained. Wrap axes remain unverified.

## Outputs and checks

40 native4x outputs plus40 reduced2x outputs. 2x is Lanczos reduction from the same
4x colour inference, not a native2x checkpoint test. Generation28.1748s excluding
installation/model initialization. Peak allocated GPU tensor memory3,105,167,360
bytes (not total driver/process VRAM). Full recipe/software/runner/checkpoint/
source/output hashes and per-image timings: experiments/swinir-classical/build.json.
Local run log: hd-work/swinir-generation.log. Reuse/resume validates recipe, original
fingerprint and both candidate fingerprints; originals are never regenerated.

All40 originals and80 candidate hashes/dimensions verified. Every alpha plane at
both scales byte-matches the corresponding Lanczos plane. Edge checked all40
assets, category filters,2x/4x,RGB/alpha/RGBA,zoom, independent review choices,
JSON export and24 close-up images; zero script errors. Verification.json records
those checks. Python compilation and source whitespace checks pass. No native
runtime changed, so the existing quick4/5 and older full11/14 limitations remain.

Unexpectedly hard detail was checked using AST-loaded **unmodified official**
define_model()/test() functions with the same prepared RGB and reference padding.
House(S01), Blaze(S03) and crown(S25) all reproduce our4x RGB bytes exactly,
maximum channel difference0. Evidence: official-inference-check.json and scratch
hd-work/swinir-official-check.py. This validates the adapter/checkpoint operation;
it does not establish genuine recovered details or visual fidelity. No invented
ground truth or PSNR-based quality verdict was used.

## Review and observations

[Full gallery](http://127.0.0.1:8766/experiments/swinir-classical/index.html):
Original, Lanczos, RealESRNet and SwinIR classical, same40 assets and existing
controls. Separate review fingerprint/storage; earlier gallery, reviews and
in-game Lanczos pack untouched. Opening in the Codex panel returned queued.

[Six focused regions](http://127.0.0.1:8766/experiments/swinir-classical/details.html):
House sun tattoo/graffiti logo, crown loading emblem, Blaze jersey/denim and weathered
wood. Exact source boxes and input/output fingerprints in details.json. RGB shown
independently of alpha; use full gallery RGBA view for actual logo transparency.
64px crops show8x source magnification/2x output pixels;128px crown shows4x source/
1x output pixels. Details-sheet.png is a visually inspected navigation sheet;
its crown was reduced for that sheet only, with full-size crops unchanged.

Primary visual impression: SwinIR increases edge definition but changes tattoo
line thickness/connections, makes jersey perforations strongly outlined, retains/
emphasizes coloured speckling and turns wood noise into conspicuous patterns.
It is not an obvious faithful whole-pack improvement over Lanczos. These are
observations from the sampled art, not claims that every output fails or that
sharper shapes are genuine restored detail. A mismatch between bicubic-photo
training and these decoded game textures is a plausible explanation, not a proved
cause. Owner's comparison/decision remains pending; selective use is still possible.

Next: owner review, then choose Lanczos globally or category/asset exceptions before
full corpus generation. Retain runtime correlation, atlas/mask/wrap/mip/cache/
performance/public-port/Debug/full gates. SwinIR has not been installed in-game.
No runtime build, source commit, publication or HD release acceptance this turn.
