# Reusing the HD texture workflow and review tools

## Accepted approach and preserved experiments

Owner accepts Lanczos4x for the first static HD release (D91). Preserve faces,
logos, lettering, jersey perforations, denim weave and wood grain. No face
restoration. RealESRGAN was rejected as generated-looking; RealESRNet smoothed
intentional detail. Classical SwinIR sharpened but changed linework/patterns.
Selective neural blends/material masks remain future research, not the default.
Customization and possibly fighter textures can receive another pass later.

Lanczos uses separate RGB/alpha,8source-pixel clamp padding, transparent colour
spread for UI/HUD/loading logos, nearest alpha for effects and Lanczos alpha
otherwise. Whole-atlas proportions are preserved. Actual wrap axes, alpha/mask
semantics, mips and in-game seams require material-specific review. See
[the full-corpus report](research/hd-full-lanczos-corpus.md).

The tools live in `scripts/`. No
generated artwork, HTML corpus output, checkpoints, original archive bytes or
captures belong in source control.

| Source tool | Purpose / boundary |
|---|---|
| `hd_assets.py` | Bounded archive/SHPX census, decode/export, selection, runtime-capture batch, fingerprints, alpha/edge helpers. Runtime correlation is separate from an archive ID. |
| `build-hd-pack.py` | Own-dump census/generation, checked pack assembly, immutable recipe caches and runtime aliases for Setup. Final game/payload certification pending. |
| `hd_corpus.py` + `hd_corpus.html` | Resumable deduplicated full Lanczos4x corpus, provenance/category gallery, local review HTTP server. No install manifest. Requires Pillow/NumPy only. |
| `hd_review.py` |40-source Lanczos/RealESRNet/RealESRGAN2x/4x audition. prepare/generate/render; neural generation requires the local NCNN tool. |
| `hd_swinir_review.py` | Pinned official classical SwinIR4x audition, render-only reuse, separate alpha. CUDA generation needs isolated Torch/timm/checkpoint/source.2x is reduced from4x. |
| `config/hd-recipes.example.json` | Early three-UI-asset runtime batch example; not the accepted full4x recipe. Several categories are unapproved/2x/excluded. Do not use unchanged for the release corpus. |
| `tests/unit/test_hd_assets.py`, `test_hd_corpus.py` | Synthetic hash/archive/alpha/reference/bleed/resume checks; no artwork. |

## Existing local data and galleries

`<hd-data>` below is a data folder of your own choosing (its own link to the extracted
dump, separate save/build/tool environments). Preserve it while the feature is in use.

| Data path below root | Use |
|---|---|
| `hd-work/full-lanczos-census/inventory.json` | Current five-field ID census; legacy `hd-work/inventory.json` IDs are obsolete. |
| `hd-work/full-lanczos/` | Accepted18,971 originals/4xPNG/thumbs/records, recipe, corpus, validation and local reviews. |
| `hd-work/full-lanczos/reviews.json` | Durable gallery flags/notes; global acceptance is D91, not19k artificial individual good votes. Store was empty at acceptance. |
| `hd-work/representative-40/selection.json` | Named40-asset selection, provenance and original paths used for all model comparisons. |
| `hd-work/representative-40/experiments/swinir-classical/` | SwinIR80variants and focused tattoo/logo/material comparison. |
| `hd-work/representative-40/lanczos-in-game/` | Actual26-entry pilot game evidence (not gallery-only mockups). |
| `mods/lanczos-test/` | Existing26-entry installed prototype pack; full corpus is separate. |
| `tools/swinir-python/` | LocalPython3.13/Pillow12.3.0/NumPy2.5.2 plus optional Torch for the audition; Lanczos does not import Torch. |

Full gallery: http://127.0.0.1:8767/ (PID49036 at handover).
Earlier gallery server8766 serves `/review.html`, `/lanczos-in-game/index.html`
and `/experiments/swinir-classical/index.html` / `details.html`.
The full server binds only127.0.0.1; close/restart only the owned process when
needed. It must be running for autosaved flags and RGB/alpha channel routes.

## Reopen and reproduce

Run from the HD lane. Reopening requires only existing corpus+Pillow; no rebuild:

```powershell
$hdData = 'D:\Games\DefJam\HdData'   # your own folder
$hdPython = "$hdData\tools\swinir-python\Scripts\python.exe"
& $hdPython scripts/hd_corpus.py serve --output "$hdData/hd-work/full-lanczos" --port 8767
```

Do not start this while the existing server owns8767. The gallery shows48items
per page; filters include category and concern/good/unreviewed status. Search
labels, any alias or identity. Open a card for original versus4x, shared pan/zoom,
RGBA/trueRGB/alpha and full PNG links. JPEG thumbnails are navigation previews;
judge full PNGs. Flags, topic and notes save to reviews.json; export is optional.
They do not automatically change processing or the game pack.

To reproduce the accepted recipe without replacing the reviewed directory:

```powershell
& $hdPython scripts/hd_corpus.py build --root "$hdData/extracted" --inventory "$hdData/hd-work/full-lanczos-census/inventory.json" --selection "$hdData/hd-work/representative-40/selection.json" --output "$hdData/hd-work/lanczos-rebuild" --workers 6
& $hdPython scripts/hd_corpus.py serve --output "$hdData/hd-work/lanczos-rebuild" --port 8769
```

Selection is optional for corpus build; it supplies familiar labels, not a40asset
limit. The corpus still processes the whole supported census. On another machine
create an isolated Python environment with pinned Pillow/NumPy and generate a
fresh census from its supported own dump. All current commands are development
tools, not a finished end-user installer. Keep output outside the repo.

For a metadata-only fresh census (no PNG selection), the current CLI can use:

```powershell
& $hdPython scripts/hd_assets.py inventory --root "$hdData/extracted" --output "$hdData/hd-work/fresh-census" --select __census_only_no_export__ --limit 1
```

This sentinel matches no asset; it controls exports, not census traversal. The
128MiB archive bound still applies. Preserve error rows, aliases, font/movie
counts and the unknown0x65 row; do not claim exhaustive disc visual coverage.

Each generated record stores source/decoded/pixel IDs, dimensions, alpha/bleed,
paths and hashes; recipe stores decoder/runner/inventory hashes and Pillow.
Unchanged recipe+allthree file hashes resume. Changed code invalidates the recipe
even if output pixels would stay the same. Preserve old outputs/reviews before
changing a recipe. The server rejects mismatched review corpus IDs. Validate
all surviving asset IDs and explicitly migrate reviews, rather than discard them.

## Future selective passes and acceptance checks

Use a new experiment/output directory for customization/fighter variations.
Keep canonical IDs and aliases so concern notes remain traceable. Start with
original+accepted Lanczos comparisons, then audition only the proposed changes.
Judge tattoos/logos/text at actual game size as well as enlarged crops; retain
whole-atlas output geometry and separate alpha. Review animated variants together.
Record model/source/checkpoint/dependency hashes, exact preprocessing and job time.
Do not regenerate or overwrite the accepted corpus simply to render a new page.

18focused unit tests passed; full corpus56,913SHA/dimension checks,
18,971source pixel hashes and all40original/LC pixel comparisons passed. Headless
Edge verified pagination/categories/search/channels/zoom/reload/export/error rows
and temporary-review cleanup. Evidence files are in `hd-work/full-lanczos/`.
Verification scratch runners live at `hd-work/verify-full-corpus.py` and
`hd-work/full-corpus-browser-check.cjs`; the latter mutates only a temporary
untouched review and refuses to overwrite an existing owner flag. These are local
QA helpers, not release requirements for users.

Further reports: [representative review](research/hd-representative-review.md),
[in-game Lanczos pilot](research/hd-lanczos-game-pilot.md) and
[release packaging](research/hd-texture-release-packaging.md). Retain full-pack runtime mapping/material/
performance and public-port gates. Fonts, HDvideo and widescreen are follow-ups.


## Setup reuse (D92)

The optional source-only Lanczos Setup step is implemented on rebased publicmain
1eb4c55/toolkit8f08c4f. See research/hd-setup-integration.md and setup-installer.md.
build-hd-pack.py exposes --root OWN_EXTRACTED_DUMP --work LOCAL_CACHE --mods LOCAL_MODS
--receipt LOCAL_JSON --aliases config/hd-runtime-aliases.json. Keep working data
outside the repo. Actual generation proof matches all18,971accepted PNG hashes.
13bounded runtime aliases are additional verified keys, not full game coverage.
The loader game fixture passes on the new toolkit base; rebuilt game, full-pack
performance/platform and final packaged-Setup gates remain. Original corpus/reviews
and gallery8767 are retained. Installed links and cached originals share storage
where supported; corrupt-cache repair preserves manual pack edits.
