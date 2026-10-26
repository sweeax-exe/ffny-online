# v0.12.1: Sketch Artist eye-color preview refresh

The reported green-swatch/brown-eye symptom is independently reproduced with
no texture packs on Windows D3D11, 16:9, render scale 3. The input callback
updates the selection and the correct eye image reaches guest memory, but the
host continues showing an earlier color. This is separate from loading-card
clipping (#108) and the HUD flag merge (#112).

## Cause and correction

The preview is a layered 2D image. All 64 original eye images have unique
complete payload hashes, but the host cache's every-61st-word signature
collides for all eight colors within each eye shape. The changed compressed
color words fall outside its sampled positions.

A native read-only trace confirms address reuse: the first eye image binds at
`0x82366300`, then all seven later choices reuse `0x82365C80`. Their complete
payload hashes match the corresponding original images. Reusing that cache
key with the same sparse signature prevents a new host upload.

Published fork commit `9a30c25` hashes every byte for textures up to 64 KiB.
It preserves the eight-millisecond recheck interval and the existing sampled
path for larger scene/video surfaces. This bounds the added work to small
textures; it does not claim complete change detection for larger atlases.
The internal helper is shared with a hardware-free synthetic fixture.

## Validation and limits

- The fixture demonstrates the old sparse collision and detects localized
  edits at every one of 8,192 word positions in a 32 KiB texture. Byte tails,
  unaligned input and the 64 KiB boundary are checked. Release CTest passes.
- Windows Release build passes with the changed runtime source.
- Native `eyes-cache-fixed` cycles all eight colors, then reverses two steps.
  Green and blue now render correctly; the reversed selections reproduce the
  same iris RGB measurements as their earlier visits.
- Representative iris-region mean RGB changes from brown `(40.7,29.5,21.6)`
  to green `(14.6,67.1,21.7)` and blue `(20.9,42.7,74.9)`. These are measurements
  of the local captures, not substituted artwork.
- No crashes, watchdogs, truncated batches or unresolved calls. Median 120
  presents per two seconds; low counts during the introductory movies are not
  a gameplay performance comparison. Saves restore identically; owner saves
  remain unchanged.

Local evidence: `logs/v0121-ui/eyes-cache-fixed.json`,
`logs/run-20261025-171718.log.err`, `eyes-cache-fixed-memory/textures.json`,
`ec-source-hashes.json`, `ec-sparse-signatures.json`, and
`eye-cache-iris-colors.json` under `logs/v0121-ui`. All captures and source
images remain local and ignored. Game unit/regression and CI results are
recorded in the PR as they complete.

This correction restores the visible preview; it does not change the stored
eye selection or claim a separately tested persistence fix. The small
created-fighter grid thumbnail remains blank in the subsequent normal-route
test and is tracked independently in #113. No Power Plant performance fix is
claimed. Use the v0.12.1 release train, with candidate platform checks and
owner acceptance still due.
