# Representative HD texture audition

Owner liked the first three-image comparison and requested 30–50 representative
assets. This second set selects 40 distinct images with exact archive-chain and
SHPX-directory provenance. It is a visual audition, not an installed or accepted
game pack. Fonts and movies remain separate workflows.

## Coverage

| Category | Count | Examples |
| --- | ---: | --- |
| Fighters | 6 | House, D-Mob, Blaze, Sean Paul; main and secondary face/clothing atlases |
| Customization | 6 | Shared body atlases and three XML-referenced create-player upper-body variants |
| Venues | 8 | Vehicle atlases, concrete, wood, cracked windows and speakers |
| Props | 4 | Bat, champagne bottle, broken glass and door |
| Logos | 2 | Intact crown loading emblems |
| UI | 4 | Arrow, border, button legend and panel |
| HUD | 4 | Block/grapple buttons and left/right-stick legends |
| Effects | 4 | Decals with non-binary alpha/intensity values |
| Crowds | 2 | Shared and character atlases |

V2CP is customization, not crowd. Category selection uses archive provenance;
fighter names embedded in screen thumbnails do not imply a fighter atlas.
The initial two `load0.xsh` logo choices were fragments and were replaced after
contact-sheet inspection. Atlas labels describe provenance without claiming
all UV regions have been identified.

## Reproduction

Local media root:
`<hd-data>/hd-work/representative-40`.
The requested selection lives in `requested.json`; decoded provenance, dimensions,
palette/source IDs and original PNG hashes are in `selection.json`.

Use the isolated Pillow environment in `defjam-hd-data/tools/python/Scripts/python.exe`:

```powershell
python scripts/hd_assets.py select --root <data>/extracted --selection <review>/requested.json --output <review>
python scripts/hd_review.py prepare --root <review>
# Exclusive GPU use: finish all game/performance harnesses first.
python scripts/hd_review.py generate --root <review> --tool <data>/tools/realesrgan/realesrgan-ncnn-vulkan.exe
python scripts/hd_review.py render --root <review>
```

Each asset has Lanczos, RealESRNet x4plus and RealESRGAN x4plus candidates at 4x
and 2x: 240 candidate PNGs plus 40 originals. Neural colour always runs at 4x;
2x colour is reduced from that same output. This is not a native 2x model audition.
Alpha is always resampled independently from the original. Eight source pixels
of edge padding are cropped after processing. UI/logos use transparent RGB spread;
world/fighters/crowds/effects do not. Effects retain mask values using nearest
alpha; other alpha uses Lanczos. No face restoration or generative redraw.

Wrap axes are not yet verified from runtime sampler state. These audition images
use clamp padding; a reviewed repeat recipe must use wrap padding before final
production. Atlas gutters, channel semantics and in-game alpha tests also need
runtime acceptance. Offline SHPX source IDs must be correlated with runtime dump
IDs/decoded IDs before installing world replacements.

`jobs/<model>/<recipe>/build.json` records source/output hashes, model/runner/exe
hashes, dimensions, recipe and timing. Jobs resume only with matching provenance
and valid output hashes. RGB-only PNGs preserve colour even below zero alpha;
browser canvas alone would discard those hidden colours.

## Review artifacts

`review.html` provides all 40 assets, category filtering, original/Lanczos/Net/GAN
panes, shared pan, 2x/4x, fit/100%/200% zoom, RGB/alpha/composited views, and light,
dark or checkerboard backgrounds. Review choices and scale are saved locally;
export choices as JSON to share them. PNG links expose full resolution.
`sheets/comparison-01.jpg` through `comparison-08.jpg` provide five assets per
sheet. `overview.jpg` is a navigation contact sheet, not a quality verdict.

Keep art, model weights, binaries, screenshots and these HTML/media artifacts in
the local data root. Source documentation/scripts contain no game artwork.

## Verification and release limits

Generation finished15:53, gallery rendering15:54. Per-image jobs totalled5.69s
for Lanczos,86.62s for RealESRNet and89.71s for RealESRGAN (40 each). Median
neural job2.11/2.21s includes model startup, padding, cropping and PNG output.
Logs confirm RTX4070TiSUPER/FP16 inference. A separate native directory-input
benchmark processed the same10 padded UI RGB images in2.322s versus18.474s
with per-image calls; every decoded output pixel matched. This small UI benchmark
does not establish world/full-corpus throughput. Full production should prepare
bounded same-recipe directories and reuse model loading; the current audition
runner remains per-image for granular provenance/resume. Evidence lives in
`directory-benchmark/result.json` and `gpu.log`, not the source repo.

All40 original hashes and120 4x job hashes verified; all240 candidate dimensions
and gallery image paths verified. Independent alpha is identical across the three
models at each scale. Eight comparison sheets and an RGB-only face-crop detail
sheet were visually inspected. Neural processing cleans button lettering and
facial contours but reduces intentional noise/grit on vehicle/concrete/wood art;
no category or model is automatically accepted. Review originals/Lanczos too.

Exact selective decoding and path escape rejection bring the synthetic HD asset
suite to 12 passing tests. Python compilation and gallery JavaScript syntax also
pass. Output and interactive browser checks are recorded after generation.

Headless Edge loaded all40 assets and passed category filtering, 2x/4x independent
choices, true RGB/alpha paths, zoom/pan, exported JSON and failed-image protection,
with zero page script errors. An initial failure-injection test targeted the colour
file while alpha view was active; corrected the test to request colour explicitly.
The Codex CUA helper failed during sandbox initialization, so browser verification
used the installed Edge with a fresh isolated headless profile. The local HTTP
gallery is available at `http://127.0.0.1:8766/review.html`; the Codex panel opening
was queued for the current thread. Source/HTML gallery works without that helper.

The earlier packs-off Release regression remains separate: 11/14 gates passed
in54min (`logs/regress-20261008-155040.txt`). Two-player `audio.dry_queue` failed
(602 dry counts, zero dropped buffers); replay produced40 rather than80 golden
records; intro reported one indirect call to non-code. Story crib/gym and the
three-boot soak passed. Other work included installer extraction and another game
instance on the host; causality is unproven. Preserve reports and rerun the failed
gates without overlap before claiming a clean build. Do not alter thresholds or
golden data. GPU inference started only after that regression exited.
Public v0.5.0 reconciliation, Debug, performance, world replacement proof and owner
gameplay acceptance remain release gates; proposed HD version is v0.6.0.
