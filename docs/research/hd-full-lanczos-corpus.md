# Full Lanczos texture corpus and local review gallery

Owner decision, 2026-10-08: after reviewing classical SwinIR against the same
40 samples, Lanczos looks best. Upscale the static texture corpus and provide
a gallery so the owner can flag exceptions. This completes that processing and
review tool; full-pack runtime certification remains a separate step.

## Result and scope

18,971 unique supported SHPX textures were decoded and upscaled at 4x, preserving
24,266 archive/image aliases. One unsupported 32x32 image remains an explicit
Unresolved gallery row: `assets/misc.viv::C_ENV00A.xsh`, index0, `ec1 `, type0x65.
Its observed 16-bit Xbox payload has no validated decoder. No guessed image
was substituted. Maximum output size is4096x2048.

The final batch took257.395s (4min17s), with six CPU workers using Pillow12.3.0
and NumPy. It needs no neural model or GPU. The56,913 original/upscaled/thumbnail
files total6,817,272,766bytes (~6.35GiB), excluding metadata/records.

This is the bounded static-image census, not every visual primitive on disc.
24 original font entries (10TTF/14XFN),94MAD movie entries, UI vector/timeline
primitives and dynamically generated textures are separate workflows. Seven
top-level audio archives exceed the128MiB traversal budget: music0/music1/music2,
sfx1/sfx2,speech1/speech2.viv. Their directories have not been certified image-free.
No movie upscaling or font rasterization is claimed by this batch.

## Recipe and identity

RGB and alpha are resized independently;8source-pixel edge padding is removed
after4x Lanczos. UI/HUD/loading emblems spread visible RGB into zero-alpha pixels
before resizing. World RGB skips that treatment. Effects use nearest alpha;
other images use Lanczos alpha. PNG compression3 is lossless. Atlases retain
their full relative layout and aspect ratio. All edges currently clamp; actual
material wrap axes, masks, blend modes and atlas gutters still need game review.

The fresh census is `<hd-data>/hd-work/full-lanczos-census/inventory.json`.
The earlier `hd-work/inventory.json` used an older four-field identity descriptor;
the current five-field descriptor includes row stride. Do not use those legacy
IDs as current runtime keys. The full batch verifies canonical source identities
against the fresh census and keeps every alias after deduplication. Source IDs
still require runtime capture/correlation before installation in a pack.

All40 original and Lanczos4x decoded pixels match the approved representative
corpus exactly. The first full comparison caught one shop loading logo that had
missed UI RGB spread; classification was fixed and the entire final batch rebuilt.
Final recipe ID: `81ddfce230cae3c81100c324d2b0f552bd6668a8b534e84c57677b717c48a0e3`.
Per-image records hold dimensions, decoder/source/pixel identity, treatment and
file hashes. Recipe stores census/decoder/runner hashes and Pillow version.

## Gallery and local flags

Open http://127.0.0.1:8767/ . ServerPID49036, bound only to127.0.0.1; output root
`<hd-data>/hd-work/full-lanczos`. Search name, archive alias
or texture ID; filter category/review status; browse48 lazy thumbnails per page.
Open an asset for original versus Lanczos4x, shared pan/zoom, checker/black/white,
RGBA/trueRGB/alpha views and original/full PNG links. Category counts may overlap
when one identical texture has aliases in several groups.

Choose Flag concern, a topic and a note; Looksgood and Unreviewed are also available.
Flags save atomically to the data-root `reviews.json` and survive reload/restart.
Export flags and notes provides a portable JSON copy; no export is required for
the next local agent to read reviews. Owner flags are review records, not automatic
pack exclusions or new processing settings. All original art/output stays local.

The review store was empty during validation. Before the final recipe ID update,
its exact earlier copy was retained as `reviews-before-final-recipe.json`; IDs
were checked and all review records preserved. Browser test removed only its
own temporary fixture review; final store matched initial reviews.

| Category | Unique images in filter |
|---|---:|
| Animated textures | 47 |
| Cinematic characters | 55 |
| Cinematic environments | 1,055 |
| Crowds | 212 |
| Customization | 4,988 |
| Effects and masks | 8 |
| Environment maps | 215 |
| Fighters | 213 |
| HUD and tutorials | 58 |
| Loading emblems | 6 |
| Other textures | 89 |
| Props | 296 |
| UI and menus | 11,261 |
| Unresolved | 1 |
| Venues | 1,585 |

These are provenance-based review groups, not proof of each texture's channel
semantics or material use. See `hd-full-corpus-categories.md` for the underlying
archive/XML investigation; its proposed alias table predates final classifications.

## Reproduction and verification

Run from the HD lane; `$hdData` is the lane's own data root and `$hdPython` is
`$hdData/tools/swinir-python/Scripts/python.exe` (Pillow/NumPy are used, not Torch).

```powershell
& $hdPython scripts/hd_corpus.py build --root "$hdData/extracted" --inventory "$hdData/hd-work/full-lanczos-census/inventory.json" --selection "$hdData/hd-work/representative-40/selection.json" --output "$hdData/hd-work/full-lanczos" --workers 6
& $hdPython scripts/hd_corpus.py serve --output "$hdData/hd-work/full-lanczos" --port 8767
```

Do not start a second server whilePID49036 owns the port. Resume verifies recipe
and all three file hashes per image. A different recipe requires explicit review
store migration preserving existing flags; the server refuses mismatched IDs.

Verification:56,913SHA/dimension checks,18,971original decoded pixel hashes,
40original+Lanczos pixel comparisons pass (`full-verification.json`). Headless
Edge passes full count/pagination/category/search, channels/zoom, saved flags/notes
after reload, JSON export, invalid ID rejection and the unresolved row, with no
JavaScript errors (`browser-verification.json`, `gallery-preview.png`).
Accelerated Morton decoding matched70 synthetic reference cases; padding matched
six RGB/L/RGBA clamp/wrap cases. Focused synthetic unit suite18passed
(`logs/hd-full-corpus-unit.txt`), including UI bleed, independent alpha, source
preservation, classification and corrupted-resume repair. Generation log:
data-root `hd-work/full-lanczos-build-final.log`.

No native/runtime/game/build/save changes or source publication occurred in this
batch. Existing26-entry Lanczos in-game pilot remains separate; the18,971 files
are not automatically installed or game-tested. Retain earlier quick4/5
(pack-off fight43presents/2s below110), full11/14, Debug/perf/cache/memory/mask/wrap
gates and public nativev0.5/toolkit8f08c4f reconciliation before HDv0.6. Widescreen,
fonts and HD movie playback remain follow-ups.


## Owner acceptance, 2026-10-08 (D91)

Owner accepts the full static Lanczos artwork as a great upgrade. A further
Customization/fighter pass is deferred. Correct install-size distinction: only
textures/ is6.212GB (5.786GiB);6.817GB previously quoted includes originals and
thumbs, while the complete review folder adds records/root metadata (~6.86GB).
See hd-texture-release-packaging.md and ../hd-texture-workflow.md. No full-pack
runtime certification, merge or release publication is implied.
