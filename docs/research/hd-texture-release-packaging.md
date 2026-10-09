# HD texture acceptance, storage and release packaging

Owner feedback, 2026-10-08 (D91): the complete static Lanczos gallery is a great
upgrade and acceptable for the first HD release. A further pass on Customization
and possibly fighter models is a future release item. This accepts the artwork;
runtime/performance/platform/release certification remains open. The owner asks
for this explanation and reusable notes now, and will request merge/release
preparation separately. No merge, release draft, tag or asset upload is performed.

## Measured local storage

18,971 unique images retain24,266 supported archive aliases. The pack is already
deduplicated. Measurements count file contents, not NTFS allocation or other
experiments/dependencies/dumps elsewhere in the data root.

| Content | Exact bytes | Decimal GB | Binary GiB |
|---|---:|---:|---:|
| 4x HD PNGs only | 6,212,168,677 | 6.212 | 5.786 |
| Decoded original PNGs | 525,848,032 | 0.526 | 0.490 |
| Gallery thumbnails | 79,256,057 | 0.079 | 0.074 |
| Per-texture recipe/hash records | 20,331,619 | 0.020 | 0.019 |
| Gallery metadata, reviews and validation previews | 25,188,620 | 0.025 | 0.023 |
| Entire review folder | 6,862,793,005 | 6.863 | 6.391 |

The install payload would contain the6.212GB of HD PNGs plus small runtime
manifests and pack metadata. Originals, thumbnails, reviews, inference models
and research captures are not needed by the game. Keep them separately for
future editing/review. A copy-based assembler needs another~6.3GB while retaining
the gallery; a future builder that writes directly to the installed pack avoids
that second HD copy. Reserve~8GB for a direct installation or~15GB for a fresh
review corpus plus a separate install copy, excluding the game and dependencies.
These are working-space allowances, not measured compressed download sizes.

6.212GB is on-disk PNG storage, not the game's VRAM requirement. Textures expand
when decoded and acquire mips. The prototype uses a512MiB replacement cache
budget accounting for retained upload data and GPU bytes, with fallback/retry;
whole-pack frame pacing, eviction and memory behavior still need certification.

## Recommended way to ship under the existing project policy

Publish the HD loader, source-only Lanczos builder, recipe, hash/provenance
manifest and instructions alongside the source release. Players supply their own
supported dump and generate the HD PNGs locally. No GPU, Torch, AI checkpoint or
neural model is needed for Lanczos. The full corpus took4min17s on this developer
machine with six CPU workers; this is not a timing promise for other PCs.

This follows `AGENTS.md` section3 and `docs/releasing.md`: game files/derived
artwork are not published; the two fingerprinted README visuals are a narrow
exception. Current publicv0.5.1 distributes a narrowly approved game-free Windows Setup,
checksum and provenance; it includes no game-data assets or game executable. Upscaling does not make the game artwork part of our MIT source
license. This is the repository's distribution policy, not a new legal conclusion.

The proposed release user flow is:

1. Obtain/build the HD-capable version using the normal supported own-dump setup.
2. Run an optional **Build Faithful HD textures** source tool. Validate the dump,
   install pinned Pillow/NumPy in an isolated environment, inventory, upscale,
   verify and assemble the final manifest. Expose progress/resume and free-space
   checks. These convenience/assembly steps still need implementation.
3. Leave an unpacked pack under `<data>/mods/faithful-hd/` (or separate sibling
   packs if reviewed mip policies differ). Keep original game archives untouched.
4. Open the launcher/overlay **Textures** page, enable **Use HD texture packs**,
   set **Pack folders** to the generated folder names and restart. Multiple names
   use semicolons; later packs override matching keys from earlier packs.
5. To revert, disable HD packs and restart. Removing only the generated pack is
   also possible; original assets remain available. No original archive rewrite.

## Runtime package contract and unfinished conversion

The current Windows prototype reads unpacked files, not ZIP/7z archives:

```text
<data>/mods/faithful-hd/manifest.ini
<data>/mods/faithful-hd/textures/<corpus-source-id>.png
```

```ini
[pack]
schema=1
name=Faithful HD Lanczos 4x
png_mips=channels
[textures]
<verified-runtime-id>=textures/<corpus-source-id>.png
```

`png_mips=channels` above illustrates the existing pilot policy, not approval to
use it on every material. `opacity` is the default alpha-weighted colour mip
policy. This setting applies to the entire pack. Mixed opacity and material/data
alpha may need separate named packs or reviewed DDS mip chains. PNGs use straight
RGBA, uniform integer1x..8x, unchanged relative atlas layout/aspect and safe paths.
Original guest dimensions/UV coordinates remain intact. Missing or unsupported
replacements fall back. The prototype supports classicBC1/BC2/BC3DDS, notBC7;
lossy conversion is outside the accepted PNG visual baseline.

The18,971-file gallery is not yet an installable runtime mod. `hd_corpus.py`
intentionally writes no runtime manifest. Archive hashes are provisional until
matched to runtime captures: the guest can transform pixels, format, row width
or palettes. Exact decoded matching includes alpha. The current five-field ID
includes row stride; legacy four-field census IDs must not be used. Manifest keys
are authoritative runtime IDs, while paths may point to accepted corpus images.
One image can satisfy multiple verified runtime keys without duplicating its PNG.

A corpus-to-runtime assembler is still needed. Existing `hd_assets.py batch`
regenerates from captured inputs rather than packaging these accepted files;
`upscale` trusts input filenames and cannot establish runtime correlation.
Do not rename/copy18,971archive IDs blindly into a purported tested manifest.
The26-entry in-game pilot remains the only installed/tested pack so far.

## Prebuilt archives: technical option, outside current publication policy

For local backup/transfer, a pack needs its PNGs, manifest, pack version/recipe,
checksums and install/remove instructions. It does not need the review workspace.
Measure ZIP/7z output before reporting a download size; no full compression trial
was performed here. Lossless PNG recompression may reduce size, but would require
hash regeneration and decoded-pixel verification. Do not promise a reduction.

If distribution rights and the project's policy were separately resolved in the
future, publish a mod independently of the source/build, with compatible game
release and supported dump hashes, versioned checksums, and installation paths.
Never put game-derived PNGs in Git/Git LFS or automated source archives under the
current rules. External hosting does not change the artwork policy.

[GitHub's release documentation](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)
states each attached file must be under2GiB (checked2026-10-08). A pack archive
larger than that would need split volumes, independent parts with an installer,
or another appropriate delivery service. This size rule is separate from rights
and project policy, and is not a recommendation to upload game artwork.

## Release work reserved for the next request

Reconcile public nativev0.5/toolkit8f08c4f; decide supported HD platforms (current
loader is Windows-specific), assemble verified runtime mappings and material
policies, test full-pack coverage/performance/memory and fallback, run Release/
Debug/full/CI/fresh-checkout gates, then prepare source integration and release
instructions. Existing quick4/5 and full11/14 results are not release certification.
The unknown0x65 image remains original; fonts, UI vector/text primitives and94MAD
movies are separate follow-ups. Widescreen stays ToDo.

Reusable tools: `../hd-texture-workflow.md`. Corpus proof and exact provenance:
`hd-full-lanczos-corpus.md`. Local measured bytes: data-root
`hd-work/hd-texture-storage.json`. No pack compression, conversion, installation,
runtime tests, merge or publication occurred in this documentation iteration.


## Optional Setup generation implemented in the HD lane (D92)

Owner requests a checkbox for local upscale during installation and asks to rebase
on main. Lane now uses publicv0.5.1 main1eb4c55/toolkit8f08c4f, and the optional
unchecked checkbox plus --hd-textures, generator/assembler, dependency pins and
settings activation are implemented and locally verified. See hd-setup-integration.md.
The proposed standalone assembly gap above is now addressed by build-hd-pack.py;
full runtime coverage/material/platform/performance and final Setup gates remain.
Published Setup does not yet include the option. No game-derived PNGs are shipped.
