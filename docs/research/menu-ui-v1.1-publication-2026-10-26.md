# HD Widescreen Menus 1.1.0 publication — 2026-10-26

The owner approved Battle/User ID v4, Choose Teams v2 and Choose Venue map v6,
selected the Stronger treatment for six heading/fighter-banner assets, and
explicitly requested Cloudflare upload and publication of the updated mod register.
Every PR for this work targets milestone v0.12.2. Artwork stays outside Git.

## Final artifact

`config/menu-ui-v1.1-release.json` pins the stable artifact. ZIP 536,557,056 bytes;
unpacked 538,706,855 bytes; 404 files; 180 native textures and 80 wide/stretch
entries. Internal root `menu-ui-hd-wide-v1-1-0`.

SHA-256: `95200fe8a01402a1b50695135102e6349309342c22132d0300aa62a92c79e724`.

[Cloudflare download](https://pub-9167b11f83f24f2a9e21af7f0e225057.r2.dev/menu-ui-hd-wide/1.1.0/95200fe8a01402a1b50695135102e6349309342c22132d0300aa62a92c79e724/menu-ui-hd-wide-v1.1.0.zip).
The same immutable hash prefix contains `.zip.sha256` and `RELEASE-NOTES.txt`.
All three objects were checked through R2 metadata and complete public GETs;
the ZIP also passes a byte-exact 206 range request. Local receipt:
`logs/menu-ui-v1.1/publication/upload-receipt.json`.

The render-tested local RC1 in `config/menu-ui-v1.1-local-candidate.json` is
superseded, not published. Final installer validation exposed 17 wide anchors
without required native texture entries. The final pack adds unchanged original
art at the corresponding 4x dimensions; Faithful remains optional and unbundled.
A dotted archive root was also rejected and replaced by the hyphenated slug
above. Neither invalid private export was uploaded. Prior released objects,
version records, artwork approvals and immutable checkpoints remain unchanged.
The final private pack receipt records all 17 original/output fingerprints.

## Compatibility and verification

Game v0.12.1 can install and use this artwork without reinstalling the game.
Both capabilities (`textures-v1`, `wide-v1`) already ship in that release.
`setup/mods.py` is byte-identical to tag v0.12.1; the tag and this branch pin
the same toolkit `7a0f2f60ac2f8671e6f34ce4ff61a77759ec8a7f`.
The exact final ZIP passes that installer helper and the catalogue strict package
validator: 180 textures, 80 wide entries, correct file count/size/hash/root.

The anchor-complete artwork passed a serial 32:9 Battle/fighter/directional-map
route with `RECOMP_MENU_TRANSITION_REPAIR=0`, simulating the released game’s
absence of the new repair. Log `logs/run-20261026-154533.log.err`: 46 two-second
present intervals, median120/min84, no crashes/watchdogs/unresolved calls,
debug-layer errors or truncated batches. Complete disposable saves restore
identically. The same ten inherited optional-file/kernel omissions remain;
this is not a claim of zero diagnostics. Earlier approved-art and integrated
1440p/32:9 checks are in the join report.

The brief fire-escape black rectangle is a separate original geometry defect.
Tag v0.12.1 lacks the new `menu_geometry_register` repair. Updating artwork alone
cannot fix it on v0.12.1; the source repair is queued for game v0.12.2.
It is fingerprint-gated, works with no texture mod, and has native guard tests,
five-aspect transition captures and quick regression 5/5 (`regress-20261026-110132.txt`),
including 635 unit passes and three golden matches. No toolkit change or game
release is part of this mod publication. Native artwork checks here are macOS;
Windows/Deck runtime acceptance belongs to the game release candidate.

## Catalogue

[Catalogue PR #4](https://github.com/vyanhursky/DefJamFFNY-mods/pull/4), milestone
v0.12.2, adds stable version 1.1.0 with minimum game 0.12.1 and preserves older
published version records. Fourteen tests and metadata validation pass; trusted
revision3 signature and append-only history were checked locally. CI passes and
the PR is merged as `2b4e76bd89fa1d4e5a53f029536262983d585f25`.

[Protected publication run](https://github.com/vyanhursky/DefJamFFNY-mods/actions/runs/40415849187)
uses the existing verification, signing and Pages workflow with `bootstrap=false`.
Both verification and publish jobs succeeded. The normal required-reviewer gate
was approved as authenticated owner `vyanhursky` under the explicit publication
request; self-review is permitted by the existing environment. No protection,
workflow or signing-key change or bypass was made.

The live signed catalogue is now revision4. The exact released v0.12.1 helper
verified its pinned Ed25519 signature, selected stable1.1.0, downloaded the public
ZIP, and performed a real managed update over signed1.0.0 in a disposable data
root. All404 installed files match the receipt; previous1.0.0 remains available
for rollback and the isolated save marker is unchanged. Result:
`logs/menu-ui-v1.1/publication/released-helper-update-result.json`.
Live index/signature copies and the workflow result remain beside it.

Player action: open launcher Mods, refresh the catalogue, update HD Widescreen
Menus to1.1.0, ensure the pack is enabled with its intended load order, and
restart the game. No game reinstall is needed. The separate game repair is in
[game PR #131](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/131), milestone
v0.12.2, for normal release-train integration rather than direct game publication.

## Hand-off

Private publication workspace: `logs/menu-ui-v1.1/publication/`. It holds final
ZIP, strict installer validation, immutable R2 receipt, signed-history evidence
and the disposable released-helper managed-update test. Authoring assets and
old candidate evidence are preserved separately in the approved checkpoint and
revision3 checkpoint listed in the join report. Do not redistribute local RC1
or the invalid dotted-root intermediate. Feature branch does not edit PROGRESS.md.
