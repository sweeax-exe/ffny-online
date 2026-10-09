# Optional Lanczos generation in Windows Setup

2026-10-08, D92. Owner requests an optional **Apply HD texture upscale during
install (increases install time)** checkbox, and specifically asks to rebase onto
current main for the latest Setup implementation. Implemented in the active HD
feature lane; not merged, tagged or published. Artwork acceptance remains D91.

## Current base and preserved work

The HD parent branch now starts at public `main`1eb4c55048afb84f078828ac72bafbb825eb8be4
(publishedv0.5.1, Windows Setup PR4). Its independent toolkit lane now starts at
the public pin8f08c4f99b7b6e8401033fe43a6be09e5679f5c4. Both updates applied without
conflicts. Original HD changes are retained in recoverable stashes, not popped:
parentf072cd6737028df258f4b5a12b07448555583a4b; toolkit
5b9214f0b3c5524d6fc00dfb01afc550e998a630. These are local/private backups and must
not be pushed as public history. There were no feature commits to replay; source
changes were preserved, the lane branches rebased onto public tips, then restored.
The integration checkout remains its original private history plus documentation.

Latest Setup is already released; earlier planning-only notes describe historical
state. The approved game-free Setup binary is a narrow publication exception.
It does not permit a game executable, game artwork or HD PNG distribution.

## Installer behavior

`setup/main.cpp` adds an unchecked-by-default checkbox, explains disk/time cost,
passes `--hd-textures`, disables it during work and includes it in the UI smoke
probe. Silent installation accepts the same flag. The source-only builder uses
the verified installed dump after a successful game pipeline and before activation.
Cancel stops its process tree using the existing installer mechanism; a failure
does not enable the new packs. Unchecked means skip generation; on updates it
preserves existing texture preferences rather than switching off an existing mod.

`setup/engine.py` adds15GiB data-drive headroom for the selected option (in addition
to the existing game/extraction/build checks), reports texture counts, invokes
`scripts/build-hd-pack.py`, then enables the verified generated folders while
preserving unrelated settings/comments and custom packs. The installed receipt
records generated pack names so future checked updates can replace their active
names without accumulating old setup-generated entries. Old files remain local;
no automatic artwork deletion or cleanup was introduced. Padded/duplicate INI
section names follow runtime semantics; effective last values are retained.

The builder inventories the own dump, runs the accepted Lanczos4x processing,
uses immutable input/code recipe directories, validates output hashes/dimensions,
and creates owned pack folders with atomic manifests. It uses canonical source
keys plus13 exact full-RGBA decoded runtime aliases in the hash-only
`config/hd-runtime-aliases.json`. This mapping is bounded evidence, not exhaustive
runtime coverage. Missing/transformed/unresolved textures retain the original.

Opacity UI/fully opaque images and non-UI material/data alpha use separate pack
folders because `png_mips` is pack-wide. This preserves the available policies;
individual blend modes, alpha-test coverage, wrapping and seams remain game gates.
The full-generation proof creates18,984manifest entries for18,971unique PNGs.
The unknown0x65 image remains original. Fonts/movie playback are unchanged.

The named folders are `faithful-hd-<recipe-id-prefix>-opacity` and `...-channels`,
under `<data>/mods`. A final source revision changes their recipe prefix.
Setup enables both names in `[textures]`; players can later disable packs in
the launcher/overlay and restart. No original archive modification occurs.

## Payload and disk requirements

Pinned wheels: Pillow12.3.0 and NumPy2.5.2 for embeddedPython3.13 Windows x64,
with exact SHA-256 fingerprints in setup/dependencies.json. The payload packager
targets win_amd64/cp313 explicitly and the embedded-import check includes both.
No Torch, neural model, GPU dependency or artwork is bundled. Wheel licenses
remain in their unpacked metadata in the inventoried payload.

HD PNGs are6,212,168,677bytes (6.212GB/5.786GiB). The reusable corpus/cache adds
about0.65GB of originals/thumbnails/records/metadata. On local NTFS the generated
pack PNGs hard-link immutable corpus files, so those PNGs do not require a second
physical copy; links fall back to copying if unavailable. Selected setup reserves
15GiB additional working space to cover that fallback. Different recipe caches
and old pack versions are retained; future cleanup is a separate feature.

Corpus cache repairs publish new PNG paths atomically rather than overwrite a
shared hard-link inode. Edits to an installed pack survive regeneration and are
rejected by assembly, rather than silently replaced. Treat generated packs as
immutable; make a separate overriding pack for manual edits.

## Validation and remaining gates

- Complete isolated generation238.017s plus verification/assembly,18,971images,
  one unresolved; every upscaled file hash matches the owner-accepted gallery.
  No real install/game setting was changed by this proof run.
- Full project unit suite211passed/31skipped; synthetic HD setup checks cover
  optional no-op, failure before activation, independent mips/runtime aliases,
  hashes/ownership, modified linked PNG preservation, and duplicate/padded INI
  sections/custom settings. Logs/hd-main-rebase-unit.txt, hd-setup-unit.txt.
- Current UI builds and hidden `--ui-smoke` passes. Local probe embeds an older
  game-free payload only to exercise the UI; it is not a distributable HD installer.
- Pinned wheels verified inside the actual embeddedPython; imports, resize and
  NumPy operation pass. logs/hd-embedded-python.txt.
- Updated-toolkit Release fixture1/1passes. Initial Release fixture failed because
  NDEBUG removed assertion-contained setup calls; fixture now retains assertions
  in all configurations. No renderer behavior was changed for this test fix.

Generation log: lane logs/hd-setup-full-build.txt. Data proof receipt and validation:
hd-work/setup-hd-proof.json, setup-hd-verification.json; generated immutable corpus
under hd-work/setup-lanczos-proof. The sample proof used the pre-repair-publication
runner; later atomic PNG/INI fixes are covered by focused synthetic tests and do
not change image pixels. No repeated full generation was needed for those fixes.

Before release: publish/reconcile reviewed HD toolkit source and gitlink, assemble
the final clean payload from tracked source, verify embedded imports/asset hygiene,
run real Setup selected/unselected/cancel/repair/update scenarios, and certify
full-pack Windows game appearance/coverage/memory/frame pacing and Release/Debug/
full/multi-match gates. Vulkan/macOS/Linux HD paths remain unsupported. The earlier
quick4/5/full11/14 results do not certify this rebased runtime. Do not run its old
game executable as if it were rebuilt. Currentv0.5.1 Setup remains unchanged.

Public release preparation/merge remains reserved for the owner's next request.
No upload, draft release, commit or merge was performed in this iteration.
