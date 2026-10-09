# HD texture support and local asset upscaling plan

Initial investigation 2026-10-08 14:19. This section records the original review, not release acceptance. Subsequent prototype work is recorded in the feature lane docs/research/hd-textures-notes.md; read that for current implementation and evidence. Reviewed private integration HEAD `1e5ace3`, pinned toolkit `c979c091`, public checkout/tag v0.4.1 `a13286f`, M6 plan, handover, current PROGRESS, test plan, renderer and metadata from the owner's dump. Existing integration edits were preserved. No game run, build, asset export, model installation, upscale, commit or publication was performed.

Owner choices in this chat: **faithful cleanup**, preserving faces, logos and the original art style; **static assets first**, with video extraction/workflow planning now and HD video playback in a follow-up. Follow-up D84 moves HD ahead of widescreen. D85 refresh found public native v0.5.0 already released; HD is proposed as v0.6.0 and widescreen remains unscheduled ToDo. C: is approved for the corpus and scratch, under `C:/Users/Vlad/code/defjam-hd-data`.

## Findings that affect implementation

- `docs/07-m6-plan.md` defines v0.6.0 as content-hash dumps and PNG/DDS replacements under `mods/<pack>/textures/`, with a manifest and explicit precedence. The initial local M6 snapshot had accepted v0.4.0/v0.4.1. Owner subsequently prioritized HD; refreshed public state includes native macOS/Linux v0.5.0. `docs/research/m6-handover.md` is partly historical: use current PROGRESS for toolkit pins, branches and playable build locations.
- `tools/xboxrecomp/src/kernel/nv2a_pb_exec.c:440` records diagnostic stage-0 texture use; `:1263` writes a 24-bit BMP, compositing alpha over grey, named by sequence/address. It records at most 64 first-use textures; periodic diagnostics cap at 40 additional images. **This cannot be the source for a faithful RGBA pack.** `RECOMP_PAL_DUMP` is another diagnostic, not a complete exporter.
- `bind_texture_ready` at `:2717` handles pitch and palette expansion. Palette identity and content matter: the game redraws text into indexed textures in place. Stages 1–3 bind through the same function at `:2880` onward, so replacement must cover all four stages.
- `tools/xboxrecomp/src/nv2a/nv2a_pgraph_d3d11.c:800` has a 256-entry LRU cache keyed by address/format/dimensions. `ready_tex_sig` at `:836` samples every 61st word; that is a change heuristic, **not a stable content hash**. `pgraph_d3d11_replace_texture_ready` at `:913` forces a refill; its name does not imply existing mod support.
- Cache uploads create one mip level (`:1029`); texture filtering explicitly omits mip filtering (`:1091`). HD world textures need mip upload and sampler handling, not just larger PNG loading.
- Linear coordinates are normalized using original guest dimensions/pitch (`nv2a_pb_exec.c:3160`, `:3191`, `:3235`). Keep that logical geometry separate from larger host replacement dimensions. Rewriting guest texture dimensions would break UI/atlas coordinates and potentially game memory assumptions. For a padded linear row, cropping to visible width requires an explicit UV correction or matching deterministic host padding: retaining the old pitch-based normalization alone would sample the wrong region. Include a width-versus-row-stride fixture in the marker tests.
- Existing surface tracking is a diagnostic heuristic, not sufficient proof that a resource is immutable. Render targets, MAD video surfaces, generated text/profile thumbnails and other writable resources require explicit exclusions or dedicated support.
- Backend work overlaps [upstream PR #162](https://github.com/sp00nznet/xboxrecomp/pull/162), still open when reviewed. Refresh upstream and the native-port branch before implementation; keep generic hooks small. First target is the accepted Windows D3D11 release, with backend-independent identity/manifest/recipes so later Vulkan work can consume them.

## Asset census and categories

Metadata reads confirmed 10 original TrueType fonts in `fonts/fonts.viv`, 14 `.xfn` entries in `fonts/befonts.viv`, 87 packed `.mad` clips plus 7 loose clips, and 8,795 outer screen entries (8,794 `.big` plus XML). A read-only recursive screen pass found 8,693 XSH entries / 8,391 distinct complete XSH payloads; direct VIV directory entries across the disc include another 5,607 XSH entries. **These are container/entry counts, not a unique texture count.** A per-image SHPX decoder is needed for the full census.

Actual paths provide useful initial rules: `v2bg.viv` for venue candidates; `v2cp_0..3.viv` for character/customization candidates; nested `props.viv`, `shops.viv`, cinema archives and `texanim.viv`. Confirm categories against XML and observed game use. `.asf` can be EA audio (`SCHl`), so inspect signatures rather than treating the extension as Windows video.

| Category | Proposed processing | Acceptance focus |
|---|---|---|
| UI panels, HUD, icons, button legends | 4× conservative colour, separate opacity, edge padding; isolate atlas regions when metadata allows | Exact layout, no halos or neighbouring sprite bleed |
| Logos, wordmarks, graphic lettering | Original vector artwork where available; otherwise conservative 4× and manual correction | Exact spelling, shape and colour |
| Fonts and glyphs | Original TTF outlines first; investigate FntX separately; tracing only for bitmap glyphs without outlines | Baseline, advances, kerning, punctuation, counters |
| Fighter faces, skin, hair, clothes, tattoos, accessories | Compare 2× and 4×; conservative model, no automatic face enhancement | Identity, tattoo/clothing fidelity, hair/cutout coverage |
| Venues, floors, walls, props, crowds | 2× initial candidate, selected 4×; wrap padding only on repeating axes; full mips | Tiling seams, distance shimmer, memory |
| Decals, particles, effects, cutouts | Separate alpha-test/additive/opacity presets | Silhouette, intensity, blend behaviour |
| Texture animation | One consistent recipe for every frame | Stable timing, no frame-to-frame detail changes |
| Data/mask textures, packed channels, normal maps if found | Deterministic channel-aware processing; preserve or pass through | Preserve channel meaning and alpha coverage |
| MAD video | Separate decode/upscale/encode workflow; playback follows later | Temporal quality, aspect, cadence and audio |
| Runtime-generated surfaces | Account for, exclude from static packs initially | Original text, thumbnails and movies keep updating |

APT UI "movies" are Flash-style timelines/display lists in `.apt/.const/.o/.xsh` screen bundles. They belong to UI texture coverage; preserve scripts and geometry. They are distinct from prerecorded MAD FMV.

## Recommended local toolchain

Use [chaiNNer](https://chainner.app/) with its CUDA/PyTorch backend to audition models and save visual category recipes. Its node workflow supports batches and multiple inference backends. The [download documentation](https://chainner.app/download/) says it manages its own Python environment: keep this isolated from the repo's Python 3.13. Use a manifest-driven Python/PowerShell runner for reproducible unattended pack builds; do not assume the GUI has a supported headless invocation until checked.

The detected GPU is an RTX 4070 Ti SUPER, 16,376 MiB VRAM, driver 591.86. Start with one GPU worker, FP16 where supported, bounded inference tiles, and an out-of-memory retry that lowers tile size. Measure seconds/image, peak VRAM and quality on small and large textures. Do not run inference during game performance gates.

[Real-ESRGAN NCNN Vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan) is a portable fallback/prototype requiring no Python/CUDA setup. It supports folder input, tile sizing and GPU/thread controls, but offers less flexibility for arbitrary PyTorch models and custom preprocessing.

Audition a conventional resampling baseline, `RealESRNet_x4plus`, and `RealESRGAN_x4plus` on the same 30–50 images. The [official model descriptions](https://github.com/xinntao/Real-ESRGAN/blob/master/docs/model_zoo.md) identify RealESRNet as the MSE-trained option and warn of smoothing. Select the winner per category from the comparisons; none is a guaranteed best model for FFNY. Consider general-v3 denoise controls or texture-specific models only after the pilot. Prefer faithful reconstruction over diffusion or face-restoration changes.

Use [DirectXTex texconv](https://github.com/microsoft/DirectXTex/wiki/Texconv) for validated mip generation and optional DDS packaging. Start runtime DDS support with formats the upload path can deliberately support, such as BC1/BC2/BC3. BC7 needs explicit host-format support; D3D11 capability alone does not make it available through the current Xbox-format wrapper. Preserve the game's colour/gamma behaviour; do not silently switch textures to sRGB sampling.

## Automated pipeline

1. **Inventory:** bounded recursive BIGF/BIG4 and RefPack traversal, then per-image SHPX parsing. Reuse `scripts/big-entry.py`, `refpack.py` and `apt-dump.py` knowledge, adding strict bounds/decompression limits rather than treating the diagnostic parsers as production extractors.
2. **Export/map:** lossless RGBA with transparent RGB intact; collect runtime observations using the same identity scheme as the loader. Retain archive and entry aliases, original dimensions/format/palette, atlas regions and observed sampler/blend use. Offline export supplies coverage; runtime export validates actual uploads.
3. **Classify:** JSON rules plus per-asset overrides, including alpha meaning and wrap/mirror/clamp per axis. Ambiguous assets go to review/passthrough. Identical pixels used as different materials may need different recipes; do not merge them solely on decoded appearance.
4. **Prepare:** split RGB and alpha; bleed RGB into transparent pixels only for verified opacity art. Use edge padding for clamped images and periodic padding for confirmed wrapping axes. Isolate atlas sprites/glyphs before inference where possible, then rebuild the proportionally scaled atlas without changing relative placements.
5. **Infer:** pinned model/weights hash/backend/version and scale; resume from successful unchanged jobs. Compare 8/16/32 source-pixel context on seam-sensitive samples. Distinguish external texture padding from overlap between inference tiles.
6. **Finish:** crop padding exactly (8 source pixels becomes 32 output pixels at 4×); separately resample alpha, recombine into the declared straight/premultiplied convention, generate category-appropriate mips, and encode PNG/DDS. Preserve alpha-test coverage; avoid premultiplying data channels. Validate tile repeats and atlas gutters at each mip.
7. **Review/package:** contact sheets, black/white/coloured alpha composites, tiled previews and in-game comparisons. Record source identity, recipe identity, output hash, review state and failures. A complete inventory must account for every image as replaced, intentional passthrough, excluded dynamic data or unresolved—not silently drop difficult assets.

The Discord workflow is a good starting point. Change three rules: alpha treatment follows material use rather than UI/world labels; wrapping follows actual sampler axes rather than all world textures; 8px is an experiment rather than a universal safe margin. Its timing and 3,876-texture count do not describe this game/hardware. The [Real-ESRGAN implementation](https://github.com/xinntao/Real-ESRGAN/blob/master/realesrgan/utils.py) already distinguishes colour and alpha processing, but its defaults should not substitute for per-material choices.

Suggested local layout under a chosen external data root:

```text
hd-work/inventory.json
hd-work/originals/<identity>.png
hd-work/recipes/*.json
hd-work/review/                  # contact sheets and measurements
hd-work/video-scratch/           # bounded, one clip/chunk at a time
mods/faithful-hd/manifest.ini
mods/faithful-hd/textures/<identity>.png or .dds
```

Extracted/upscaled artwork, fonts, clips, proof sheets and model weights stay local and outside tracked source. Ship tooling, recipes, manifests without embedded artwork, and documentation under the existing source-release policy.

## Font handling

First trace which game text uses the original TTF files, FntX bitmap fonts, baked image lettering or generated indexed textures. Original TTF presence does not establish that every displayed glyph can be replaced through a static pack. Rasterizing vector glyphs at higher resolution may require a dedicated host atlas/text hook while retaining guest metrics.

For glyphs that only exist as bitmaps, audition the proposed 8× smoothing → threshold → [Potrace](https://potrace.sourceforge.net/potrace.1.html) → 4× [Cairo](https://www.cairographics.org/manual/cairo-Image-Surfaces.html) rasterization. Thresholding/tracing can change holes, punctuation and thin strokes; preserve original metrics and review a full alphabet. Cairo ARGB surfaces use premultiplied alpha, so explicitly convert for a straight-alpha PNG pipeline. Tracing is a fallback, not the default for existing vectors. Keep generated text updating normally; do not replace a dynamic text line with a fixed bitmap.

## Runtime replacement design

- Version the source identity before generating a large pack. Proposal: SHA-256 over an explicit serialized descriptor (format, logical dimensions, relevant mip/layout metadata) plus complete source texel/block bytes and palette contents where applicable. Exclude guest addresses and uninitialized pitch padding; define row canonicalization, byte order and palette length precisely. Keep a separate decoded-pixel digest for workflow deduplication. Dump and load must share one implementation and known-answer fixtures.
- Keep source-resource state and host replacement state separate. Content reused at a different address still resolves to the same pack entry; changed source bytes or palette invalidate the match. Do not reuse the sampled cache signature as the pack ID. Full hashing must be tied to a reliable source-change check, not file lookup or image decode on every draw. Benchmark the first full-check implementation; page-write tracking is a possible later optimization, not an existing capability to assume.
- Bind larger host textures while retaining original guest dimensions and UV normalization. Support static formats discovered in the census, including indexed/palette and swizzled assets, across stages 0–3. Preserve address modes, filtering and blending.
- Validate manifest versions, dimensions/scales, alpha conventions, paths and DDS payloads; malformed or missing entries log a bounded diagnostic and fall back to the original. Explicit enabled-pack ordering determines conflicts, e.g. later listed pack wins. Cache index misses and successful loads; avoid repeated disk probes in the render loop.
- PNG first for proof and lossless QA, then DDS/mip upload. Use a byte-budgeted replacement cache, initially auditioning a 512 MiB budget, with LRU eviction that protects textures bound on other stages. 4× width/height costs 16× uncompressed pixels; a 256-slot count alone cannot bound memory. Include retained CPU buffers and queued uploads in limits.
- Add texture-pack enable/selection/dump controls through the existing settings core and both launcher/overlay. Mark selection as restart-required initially. Keep diagnostic overrides and an explicit pack-disabled harness mode.
- Conservative defaults exclude render targets, movie frames and generated/writable resources. Static source allowlists plus resource provenance/observations are preferable to excluding every linear texture, because legitimate static images can be linear too.

## Implementation sequence and evidence gates

**A. Identity and lossless export.** Create an isolated feature lane and toolkit worktree using `docs/parallel-work.md`, starting from the current accepted pin. Use the lane's own small fixture save/data root. Implement stable identity and RGBA dump; validate formats/palettes/pitch and collect a representative census. No need to upscale the whole game yet.

**B. Prove replacement with unmistakable markers.** Load a handful of hand-generated diagnostic textures, including larger dimensions and alpha, to prove mapping, geometry and fallback independently of AI quality. Verify enabled appearance, disabled/removal restoration, all stages, palette variations and address reuse. All game captures stay local.

**C. Build the faithful pilot.** Audition 30–50 assets including logo, opaque/alpha UI, atlas, fighter face/tattoo/clothes, repeating venue, palette image, cutout, particle and largest common texture. Compare 2×/4× world and 4× UI. Select category recipes from proof sheets and in-game views.

**D. Production runtime.** Complete DDS/mips/filtering, pack precedence/settings, cache budget, fallback and dynamic exclusions. Test eviction while bound on another stage, tiny source/palette changes outside the old sampled signature, asset reloads and pack changes/restarts. Focused synthetic fixtures may be committed; real game bytes may not.

**E. Game acceptance.** Run appropriate unit/native fixtures plus quick and full existing regressions on Release and Debug, serially in the lane. Packs explicitly off must retain existing 4:3 goldens. Pack-on captures get separate local review evidence; do not overwrite baseline goldens to conceal regressions. Cover loading/title/menu, creator/name entry, in-fight HUD, multiple fighters/venues, Story routes and a full movie/skip transition. Record frame pacing, warm/cold loading, replacement hits/misses, CPU/GPU memory, and a multi-fight soak. Agree performance limits against a solo baseline before release. Owner playtest remains the final visual acceptance.

**F. Expand to the full static census.** Run resumable category jobs, review exceptions and publish source tooling/docs after acceptance. Full-game art coverage and renderer feature support are distinct acceptance claims. v0.6.0 is not done merely because one texture appeared: propose the above gates when implementation updates `docs/02-test-plan.md`.

## Video companion workflow (playback deferred)

FFmpeg has an [EA MAD decoder](https://ffmpeg.org/doxygen/trunk/eamad_8c.html), making it a sensible extraction/decode candidate. Actual FFNY clips have not been probed/decoded in this session; verify the local build's decoder and demuxer on the real samples. `ffmpeg`/`ffprobe` were not on PATH during inspection.

Inventory all 94 clip entries, deduplicate them, and use ffprobe to record codec, dimensions, display aspect, frame rate/time base, timestamps, field order and audio. Try a 10–30-second motion-heavy excerpt and a graphic/logo clip. Preserve original cadence/audio; deinterlace only if needed. Compare conventional scaling with frame-based SR; inspect pans/fades/cuts for flicker. [Video2X](https://github.com/k4yt3x/video2x) provides a local Vulkan/Real-ESRGAN video processing candidate without materializing every frame. Use upscaling only, preserving original cadence/audio; frame-based models still need flicker review. A temporal model such as [RealBasicVSR](https://github.com/ckkelvinchan/RealBasicVSR) is an optional feasibility experiment with additional dependency/setup work.

Do not assume an upscaled MP4 can be renamed to `.mad` and played. The game currently decodes original MAD into changing guest surfaces. A later HD path must identify each clip and present host-decoded frames tied to the original game clock, retaining skip, looping, transitions and audio synchronization. Consider leaving original audio/guest control active while replacing video presentation; prove clock alignment before designing the full playback integration. Static texture lookup must continue to exclude these frames.

Process bounded chunks or streams rather than exporting all clips at once. One minute of 1920×1080 RGB24 at 30fps is 10.4 GiB uncompressed; PNG size varies. Inspection found about 134.8 GiB free on C:, which is adequate for a pilot but not a reason to materialize every full-resolution movie. Choose a scratch budget and another drive if available before full video processing. No local throughput estimate exists yet.

## Remaining input and next handoff

- Release sequence answered (D84/D85): HD first, proposed v0.6.0 after native v0.5.0; widescreen remains unscheduled ToDo. Feature lane `C:/Users/Vlad/code/defjam-hd-textures`, branch `feat/hd-textures`, toolkit lane branch `defjam/hd-textures`.
- Storage answered (D84): use C: for originals, outputs, models and video scratch. Data root `C:/Users/Vlad/code/defjam-hd-data`; keep scratch bounded and report measured disk pressure if it occurs. The owner can free space when needed.
- Recommend deciding 2× versus 4× world from the pilot rather than asking for a blind global choice. UI/logo/font candidates start at 4× where the source path allows it. Manual logo/font reconstruction and remaining dynamic-font coverage should be chosen from concrete examples.

Prototype follow-up: isolated Windows lane, versioned full-content identity, lossless export, PNG/DDS mip loader, settings, synthetic fixtures, local GPU jobs and visible marker/pilot proof now exist. Full Release checks are running there. Reconcile public v0.5.0/toolkit8f08c4f, expand the representative pilot, finish performance/dynamic/Debug/platform gates and owner acceptance before release. See feature-lane docs/research/hd-textures-notes.md. The owner has selected appearance and video scope; no additional visual-style question is needed. No upstream PR or release publication is authorized by this planning report. Keep the integration playtest build and real save root untouched.
