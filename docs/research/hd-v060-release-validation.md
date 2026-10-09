# v0.6.0 candidate validation

Owner authorization: 2026-10-08, prepare v0.6.0. The accepted artwork is faithful
Lanczos 4x; widescreen, font-outline integration, HD movie playback and another
customization/fighter artwork pass remain follow-ups.

## Source and data boundaries

- Parent candidate starts from public main/v0.5.1 `1eb4c55048afb84f078828ac72bafbb825eb8be4`.
- Toolkit candidate starts from the published Windows/native-port pin
  `8f08c4f99b7b6e8401033fe43a6be09e5679f5c4` on the separate HD branch.
- Own dump, saves, generated C, binaries, model weights, PNGs, captures and logs
  remain local/ignored. The release installer generates the player's artwork
  locally; it contains no texture pack or compiled game.
- All game runs use the HD lane's disposable data/save root and existing complete
  save guards. Original user saves and unrelated checkouts remain untouched.

## Completed focused validation

- Fresh local analysis and lift on the latest public base: 17,896 identified
  functions, 17,882 ABI entries, 20 generated C files. Release build succeeds.
- Project unit suite: **246 passed**, including HD setup final activation success,
  cancellation, shortcut failure and receipt failure; prior settings and receipts
  remain byte-identical on failure. `logs/hd-v060-unit.txt`.
- Release native texture-pack fixture: **1/1 passed**, real WIC/CNG and synthetic
  images with mock D3D8 resources. Covers content/palette/stride IDs, unreadable
  source memory, PNG alpha/channel mips, odd extents, row padding, DDS validation,
  allocation retry, precedence and four-stage-safe eviction/resource lifetime.
  `logs/hd-v060-fixture.txt`.
- Actual embedded Python negative control reproduces the missing sibling-script
  import; adding `../source/scripts` fixes `build-hd-pack.py --help`.
  Final payload checks require all HD entry-point sources and execute this probe.
  `logs/hd-v060-embedded-path.txt`.
- Earlier whole-corpus proof generated **18,971 images / 18,984 manifest keys**,
  13 independently correlated runtime aliases, one unsupported static encoding.
  All output PNG hashes match the owner-accepted gallery; generation 238.017 s.
  This proof precedes cache-publication fixes, which preserve pixel processing.

Read-only reviews: [installer](hd-v060-installer-review.md) and
[runtime](hd-v060-runtime-review.md). Their findings are reviewed evidence;
source inspection alone does not replace real installation/game gates.

## Game regressions (2026-10-09)

Full `scripts/regress.py`, 14 checks each, on the candidate with the HD runtime built in:
**Release 14 of 14 passed in 55 min**
(`logs/regress-20261009-001522.txt`, 246 unit tests) and **Debug 14 of 14 passed
in 57 min** (`logs/regress-20261009-012256.txt`, 250 unit tests). Both cover the
three still-frame goldens, fight, free-for-all and Terrordome, combat (27 of 27
assertions), versus (26 of 26), the simulation replay hash (80 records over 1,800
steps), Story intro, crib, Learn Moves and a three-boot soak. Source-tree policy:
384 tracked files, 0 failures.

## Release procedure and remaining gates

The toolkit change is one commit (`4bff257`) on the fork branch `defjam/hd-textures`,
published before the parent pin. Source tag, version, notes, main ancestry and
hygiene are re-checked by `scripts/check-release.py`; hosted CI and the Setup asset
inventory run on the tag before the release draft is created.

Not recorded in this lane: logs of the packaged Setup run through the selected,
unselected, update, repair and cancel scenarios, and a cold/warm full-pack
performance sweep. The owner reported testing complete on 2026-10-09; hosted CI
on the pull request and the tag provide the clean-checkout evidence.
