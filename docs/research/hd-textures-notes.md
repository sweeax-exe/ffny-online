# HD feature lane: implementation and handover

2026-10-08. Owner: faithful cleanup, static assets first; video extraction/planning now, HD playback later;
HD before widescreen; C: corpus/scratch. Initial plan: `hd-texture-plan.md`. This lane does not edit integration
PROGRESS/worklogs. Do not call the prototype an accepted/released milestone.

## Source lanes and release drift

- Parent `C:/Users/Vlad/code/defjam-hd-textures`, `feat/hd-textures`, base private main `1e5ace3`.
- Toolkit `tools/xboxrecomp`, `defjam/hd-textures`, base accepted Windows v0.4.1 pin `c979c091`.
- Own data `C:/Users/Vlad/code/defjam-hd-data`; extracted is a read-only junction. Analysis and generated C
  were freshly regenerated in the lane after detecting a stale copied analysis/tool-file hash. Own build,
  logs and small `vy2-hour-v1` fixture/save; owner saves and playtest builds untouched.
- Refresh revealed public PR #1 merged 2026-10-08 21:05 UTC, native v0.5.0 published 21:19 UTC. Current public
  main `3b5c94c5`, toolkit `8f08c4f99b7b6e8401033fe43a6be09e5679f5c4`. HD's proposed release becomes v0.6.0;
  widescreen remains unscheduled ToDo. Before final source commits/publication, reconcile these changes in
  a public-history feature lane, replay the toolkit topic over that pin, regenerate/certify and rerun gates.
  Never push private history to the public game repo. Public v0.5.0 validation does not certify these HD changes.
- Upstream `193e2995...main` identical when refreshed; [PR #162](https://github.com/sp00nznet/xboxrecomp/pull/162)
  remains open/rebased and changes executor/backend interfaces. No upstream submission made or authorized here.

## Implemented prototype

- Generic `texture_pack.c/h`: schema-1 INI packs, full CNG SHA-256 source IDs, WIC straight-alpha PNG capture/load,
  supplied classic BC1/BC2/BC3 DDS mips, PNG opacity mips, palette identity, original logical dimensions/row padding,
  last-listed precedence, 512 MiB default CPU+GPU texture budget, protected bound stages and LRU eviction,
  negative lookup cache via manifest, bounded diagnostics/fallback, temporary pressure retry.
- Thin executor hook before original palette expansion; stages 0–3 use the same bind. Static colour format whitelist,
  bounds checks; cube, movie/YUV/depth and tracked surface exclusions. Source hashes recheck at most every 8 ms;
  no write tracking. Surface provenance is a heuristic, so dynamic/generated assets must remain unapproved.
- Windows settings file/launcher/overlay Textures page; restart semantics, explicit diagnostic env precedence.
  WIC/CNG are Windows-specific; non-Windows loader stubs currently retain original rendering.
- `scripts/hd_assets.py`: bounded BIG/RefPack/SHPX census, DXT and linear/swizzled indexed/BGRA exports, provisional
  archive IDs and decoded correlation IDs; resumable RGB/separate-alpha/padding GPU jobs, category/override batch
  runner with review defaults; bounded raw MAD/font exports. Source/model/executable/recipe/runner/output hashes.
- `config/hd-recipes.example.json`: three observed pilot assets only. No original/upscaled artwork, models, captures,
  lifted code or binaries are tracked. Source changes remain uncommitted while acceptance/reconciliation is pending.
- Read-only XML follow-up verified V2IP=named fighter atlases (House003A, SeanPaul125A, Blaze113A, D-Mob014A/B),
  V2CP=create-player/customization (createplayer_parts.xml); its shared cp_a atlas is not proven to be a face.
  Crowd sources are icrowd/cincrowd/V2CM; V2BG covers venues and shop/Story interiors. Classifier now separates
  these, cinematic variants and effects; screen-container provenance takes precedence over embedded names.

## Measurements and local evidence

- RTX 4070 Ti SUPER 16,376 MiB; official NCNN job log confirms this NVIDIA device and FP16 support.
- Census: 24,267 image directory entries, 18,971 provisional unique source IDs under the first descriptor, one
  unsupported SHPX 0x65 entry, 24 font entries (10 TTF/14 XFN), 94 movie entries. Seven large top-level audio VIVs
  exceeded the 128 MiB traversal budget; retain these exceptions. Forty selected originals exported locally.
  Rebuild inventory with the final five-field descriptor before treating IDs/unique counts as final.
- Isolated original Release build passed 5/5 quick checks: 173 unit tests, m2/m3/m4a goldens, two-minute fight,
  median 120 presents per 2 s/minimum107. `logs/regress-20261008-144436.txt`.
- Lossless dump enabled: m2 unchanged, 13 observed assets captured with alpha. `logs/hd-dump-m2.txt`.
- Native mock-resource fixture passes real WIC/SHA tests: known vector shared with Python, full-byte/palette changes,
  address independence, ignored row padding, malformed source/path/DDS, PNG alpha/mips/physical row width,
  supplied DDS mips, precedence, bound-stage protection and resource release. `logs/hd-native-fixture.txt`.
- Ten synthetic Python asset tests pass with the isolated Pillow environment, `logs/hd-tools-tests.txt`.
  The project suite passed 180 tests before the three added batch/aux/category fixtures (rerun final suite before committing).
- Three 4x UI images: RealESRNet 3.259/1.963/2.067 s; RealESRGAN and Lanczos comparison also generated.
  These tiny samples do not establish full-corpus throughput or peak VRAM. `logs/hd-gpu-*.txt`.
- Serial original/marker/RealESRNet boot probes: no harness faults, saved roots restored. Frame40/70 marker
  changes bbox(257,178,383,272), pilot changes bbox(258,178,383,268). Frame150/220 no differences because the
  sampled assets have left that screen. The coarse m2 golden still passed the first marker run; image pixel
  differences and explicit load logs supply the positive proof instead. `logs/hd-pack-probe.txt`.
- Local proof sheet: `<data>/hd-work/ui-model-comparison.png`; marker/actual views in lane `logs/shots/hd-*`.
  Both neural models smooth intentional logo wear. Current faithful pilot uses Lanczos for the two logos,
  conservative RealESRNet for loading lettering; three entries, ten unapproved observations in review.json.
- Packs-off Release regression on the v0.4.1-based prototype finished at15:50:
  **11/14 passed in54min**, `logs/regress-20261008-155040.txt` / `logs/hd-regress-release.txt`.
  Unit180, three goldens, fight/FFA/Terrordome, combat, crib/gym and three boot soak pass.
  Versus failed audio.dry_queue (602 dry, no dropped buffers); replay produced40 rather than80
  golden records; intro reported one indirect call to non-code. The host also ran installer
  extraction and another game instance, so performance/determinism are not certified and
  causality is unproven. Preserve reports and rerun failed gates without overlap after port
  reconciliation; do not change thresholds/goldens to make them pass.
  The focused HD Python suite is now12 passing tests, including exact selection/path safety
  and skipping unrelated compressed archive members.

## Tool reproduction

Portable [official 20220424 Windows bundle](https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip)
SHA256 `abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d`.
It has RealESRGAN, not RealESRNet. The RealESRNet pair was copied from the
[official 20211212 bundle](https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.3.0/realesrgan-ncnn-vulkan-20211212-windows.zip),
SHA256 `caf96d62999e741194a28b514eb6202c09a39edcd9ced730e3f784c424cc0653`.
Weights and executable hashes are retained in each local job build.json. Pillow12.3.0 is isolated in tools/python.

The [official model descriptions](https://github.com/xinntao/Real-ESRGAN/blob/master/docs/model_zoo.md) warn that
RealESRNet smooths; observed logo results agree. [chaiNNer](https://chainner.app/) remains an optional GUI;
CLI automation is the tested pilot. [FFmpeg's MAD decoder](https://ffmpeg.org/doxygen/8.0/eamad_8c_source.html)
and [Video2X](https://github.com/k4yt3x/video2x) are video workflow candidates; no local decoder throughput has
been measured. See `../hd-textures.md` for recipe and extraction commands.

## Remaining gates / next actions

### Representative review ready,16:00

Owner liked the first3-image UI comparison and requested30–50 representative assets.
The40-image set is ready at `<data>/hd-work/representative-40/review.html`
(local URL `http://127.0.0.1:8766/review.html`). Coverage:6 fighters,6 customization,
8 venues,4 props,2 logos,4 UI,4 HUD,4 effects,2 crowds. Original plus three methods
at2x/4x give240 candidates. Original hashes,120 4x output hashes, all dimensions,
alpha equality and image paths verified; all40/browser controls/export/error guard
pass in isolated headless Edge. No general model/category acceptance yet.
Neural models sharpen lettering/contours and also smooth intentional grit; inspect
faces, branded lettering, wood/concrete and alpha semantics before choosing.

`scripts/hd_review.py` creates the gallery/sheets; exact selection/provenance is in
the data-root `requested.json`/`selection.json`; recipes and hashes in per-job build.json.
No artwork/model/HTML outputs in source. See `hd-representative-review.md` for
commands, benchmark and test limitations. RealESRNet/GAN40 jobs total86.62/89.71s;
native directory-input UI benchmark10 images2.322s vs18.474s per-image, identical
decoded RGB. Use bounded same-recipe directory batching for production after
world throughput/restart checks; current runner remains per-image/resumable.

1. Investigate/rerun the three recorded regression failures without host overlap; reconcile public v0.5.0/toolkit8f08c4f, preserving Windows behavior and new platform
   builds. Rebuild/re-lift/certify in isolated public-history checkout; no private-history publication.
2. Fix/add any remaining focused cases: avalanche the source-cache address key so aligned guest allocations do not cluster; transient corrupt/OOM errors, schema/fallback and DDS limits, source
   change caching, and alpha-test/additive/channel-aware mip recipes. Reviewed atlas isolation is not automated.
3. Owner reviews the ready40-image corpus at2x/4x. Capture title/menu/fight/creator/Story
   with new metadata and correlate source/decoded IDs with runtime IDs; the offline40
   samples are not already installed game replacements. Complete the updated census.
4. Prove in-game world and palette replacements/all stages, disabled/removal restoration, tiny-budget eviction,
   movie/name-entry liveness, and warm/cold/frame-pacing/memory behavior. Dump and GPU inference off for perf.
5. Run final Release and Debug quick/full gates, multi-match soak and fresh-checkout CI/source hygiene.
   Existing 4:3 goldens remain unchanged when packs off; local pack-on review evidence stays separate.
6. Owner visually accepts concrete sample pack and gameplay build, then source-only toolkit publication,
   parent gitlink, integration and release. No packaged game art or executable publication is permitted by the
   standing rules. Font host integration and HD video playback remain follow-ups; do not claim full coverage.


## Owner detail-preservation feedback, 2026-10-08

Owner prefers Lanczos as the initial approach; RealESRNet selectively, with jersey
perforations/denim weave/wood grain preserved. RealESRGAN looks generated/fake.
Four local crop experiments support a source-based baseline and independent
output blending/material masks. See hd-detail-preservation-assessment.md.
This is a direction, not asset/scale/release acceptance; native gates remain.


## Lanczos in-game test, 2026-10-08

Owner requests Lanczos tests now; defer deeper model research. Local26-entry pack:
Blaze,22 opaque Foundation textures,3 loading assets; runtime correlation320 captures.
Game/palette/world replacement and jersey detail visible in1280x960/2560x1920 captures;
all runs restore fixture saves. New optional independent-channel PNG mips, cache-address
mixing, source readability on refresh/capture and native-resolution diagnostic captures.
Native fixture passes and project unit185pass; performance remains open (~21fps
observed with both packs off/on). Local preview uses separate persistent playtest saves.
See hd-lanczos-game-pilot.md and http://127.0.0.1:8766/lanczos-in-game/index.html.
No release acceptance/publication; older11/14 full run does not certify these new edits.

Final pilot: quick4/5 (185unit+3goldens pass; pack-off fight43/2s below110); logs/regress-20261008-202746.txt. Viewer8images/4times/native-fit/Edge checks pass. Interactive preview launched20:28 PID12956 with26entries, keyboard player1 and separate playtest saves. Leave running for owner; no rebuild/second game until closed. Full gates/public port/performance remain open.

## SwinIR classical audition, 2026-10-08

Owner finds Lanczos in-game decent and requests official classical SwinIR on the same40 before whole-corpus choice.40 native4x+40 reduced2x candidates; FP32 CUDA whole-image generation28.1748s, alpha byte-identical to Lanczos.80 hashes/dimensions and40-asset Edge controls/reviews/export pass;3 samples exactly match official inference functions. Six focused tattoo/logo/material regions; earlier reviews/pack intact. Details docs/research/hd-swinir-audition.md. SwinIR changes markings/patterns despite sharpness; owner decision pending. Preview12956 gracefully closed before inference; no runtime/build/release gate changes.


## Full Lanczos corpus ready, 2026-10-08 (D90)

Owner reviewed SwinIR and selects Lanczos for the wider static corpus, with an
all-texture gallery for concern flags. Final18,971unique4x outputs/24,266aliases,
one explicit unknown0x65; six CPU workers257.395s. Fresh canonical-ID census
replaces legacy IDs; loading-logo classification fixed and all40approved Lanczos
pixel comparisons pass.56,913files/hash/dimension checks and18,971original pixel
hashes pass, focused unit18pass. Gallery http://127.0.0.1:8767/ (PID49036) has
category/search/pagination, original/LC/channel/zoom and durable flags+notes at
own data `hd-work/full-lanczos/reviews.json`; Edge checks pass, temporary flag
removed. Report hd-full-lanczos-corpus.md holds commands/provenance/coverage.
This supersedes earlier pending model choice and running-preview instructions:
preview12956 is closed. Full corpus is not installed; earlier26-entry pack remains.
Native/perf/full/Debug/public-port/mask/wrap certification and font/video follow-ups
remain open. No game/native build/save/publication changes in this batch.


## Static artwork accepted, 2026-10-08 (D91)

Owner reviewed the full static gallery and considers it a great upgrade; accepts
Lanczos for now. Customization and possibly fighter-model refinement are future
release items. HD PNGs6,212,168,677bytes (6.212GB/5.786GiB); review folder~6.86GB.
Packaging explanation: hd-texture-release-packaging.md; reusable tools guide:
../hd-texture-workflow.md. Recommend source-only local generation under existing
artwork policy; runtime assembler/correlation/full-pack gates still required.
Owner reserves merge/release preparation for a later request; none performed.


## Setup checkbox/main rebase, 2026-10-08 (D92)

Rebased HD parent to publicmain1eb4c55(v0.5.1) and toolkit8f08c4f, preserving
unpublished sources in stashes. Implemented optional unchecked --hd-textures
checkbox, CPU generator/owned assembler, pinned Pillow/NumPy, progress/disk checks
and settings activation. Full proof18,971images/18,984keys/13runtime aliases,
238.017s generation, all accepted PNG hashes match. Unit211pass/31skip, UI probe,
embeddedPython and Release HDfixture1/1pass. Shared-link repair and INI section
semantics protected by tests. See hd-setup-integration.md. No full-game/actual
installer run, source commit/merge/release; prior release gates remain open.
