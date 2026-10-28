# Mod manager implementation and validation — 2026-10-21

## Implementation

Topic branch `codex/launcher-mod-manager-v1`, based on main `a0c9109`; toolkit stays
at published main pin `32aab4d`. The existing tested APFS save-copy fix is a separate
prerequisite commit `ee8be70` (cherry-picked from menu branch `69e0892`; main's
PROGRESS history retained rather than importing an older art-branch state).

- `setup/mods.py`: catalogue signature/expiry/revision verification, resumable
  exact-size/hash downloads, strict schema-1 pack validation, staging, managed
  ownership, update, prior-version rollback/removal and recovery. No settings edits.
- `setup/mod_catalogue.py`: strict versioned metadata contract, initially derived
  from the public catalogue foundation. Stable identity differs from export folder.
- `src/hooks/pc_mods.cpp/.h`: shell-free asynchronous helper process, bounded atomic
  INI view/progress reader, cancellation, lifetime game/read vs installer/write lock.
- Existing Mods page now separates installed packs and project catalogue, with one
  Load Order window and final-winner texture overlap descriptions. Helper mutation
  controls are launcher-only; environment locks and manual order editing remain.
- `pc_settings.c` refuses active/incomplete mutation even without the launcher,
  bypasses saved launcher-skip for recovery without changing that saved preference,
  and rechecks journal existence after shared-lock acquisition.
- Setup bundles cryptography50.0.2/cffi2.0.0/pycparser2.23 beside the four existing
  pinned wheels. All three platform filenames/SHA-256s were fetched from official
  PyPI metadata, resolved under the actual packager flags and checked against the
  downloaded wheels. Asset inspection requires the helper source/public pin and
  probes Ed25519 import in the embedded interpreter.

No game-art changes, image generation, runtime toolkit edits, disc modifications
or ordinary install/save/settings mutations. Current main cannot render `[wide]`;
CMake derives the `wide-v1` capability only from a toolkit with that loader. The
actual menu release still requires its separate PR62/PR73/native-acceptance work.

## Trust and publishing

[Catalogue PR1](https://github.com/vyanhursky/DefJamFFNY-mods/pull/1) contains
append-only version/revocation checks, strict ZIP/image validation and ClamAV.
Archive checking runs in a job without signing credentials; the separate protected
job installs exact hash-pinned binary wheels and signs only metadata matching the
reviewed source and next revision. Missing published history fails closed except
for explicit first-empty-catalogue bootstrap. Every mirror must match exact bytes.

Vlad explicitly approved generating the Ed25519 key, retaining a private local
backup and setting the protected GitHub environment secret after automatic review
requested specific credential authorization. Secret provisioning succeeded; no
private value appeared in tool output or source. Public pins match across repos.
The durable backup is outside worktrees in `<data>/private-signing/mod-catalogue/`;
never add it to Git or package it. `catalogue-release` requires owner review and
main-only deployment. No merge, Pages publication, R2 account/bucket or artwork
upload has happened in this session. A separate owner's approved workflow run is
still necessary before a live signed catalogue exists.

## Synthetic and packaging checks

Acceptance tests cover tampered/untrusted/expired/replayed catalogue metadata,
SemVer ordering, revoked/incompatible versions, dependencies, malformed hashes, oversized receipt refusal before the filesystem switch,
resume and servers ignoring Range, bad range replies, ZIP traversal/links/device
paths/case collisions/unsupported files, missing images, exact expansion bounds,
live-overlay `stretch` anchors without replacement pixels, cancellation, collisions,
modified managed/rollback files, redirected state/mods roots (including Windows
junctions), and interrupted transactions before/after the filesystem switch.

Native C++ fixtures exercise asynchronous helper execution with spaces in paths,
Python/C++ lock interoperability and launch refusal for an incomplete journal.
A read-only review found and resolved unowned rollback cleanup, looser DDS checks,
directory durability, repeated failed recovery attempts, skip-preference handling,
missing-history reset and signer dependency verification. Unknown local edits are
preserved rather than repaired by deletion.

All nine downloaded verifier wheels match the platform dependency pins. The
actual retained macOS **CPython3.13.16** imports the unpacked pinned wheels and
successfully generates/verifies an ephemeral Ed25519 test signature with `-B`;
no existing Python installation was modified. The same retained interpreter also
validates HTTPS successfully against the public catalogue repository (HTTP200).
Dist-info/licence files remain in
wheel-unpacked payloads.

## Actual launcher UI

`scripts/test-mod-launcher.py` (retained from the local acceptance driver) uses a copied helper and an ephemeral
fixture public key, never the production private key. Two 8x8 synthetic packs
share a texture ID. Actual event targets:

1. Open Mods; show catalogue update; open shared order; move managed pack earlier;
   close order; Update through the helper; confirm version1.1.0, same enablement
   and changed precedence persisted. The verified download cache supplies bytes.
2. Resize to820x560 and set250% text; open/close order through real UI targets.
3. Leave a recognizable interrupted journal and saved launcher skip=true; launch
   normally, recover before Play, open Mods and order, quit; journal removed and
   skip preference still true.

All three native runs exit0, every scripted target resolves, and complete-save
restoration is byte-identical. Captures inspected locally, including the large
text window. `logs/mod-manager-native/launcher-receipts.json`; images and logs
remain local. The initial receipt assertion expected `skip=1` but the settings
writer correctly emitted `skip = true`; the assertion was corrected to parse
booleans and the full three-case tour was rerun successfully.

The original menu RC2 ZIP also validates/unpacks with the actual importer:
**157 texture mappings,62 wide entries,326files,421563157 compressed bytes and
423392524 unpacked bytes**, SHA256
`43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634`.
Only a temporary validation copy was written. This is format validation, not an
installation or additional approval of its native visuals. Receipt:
`logs/mod-manager-native/real-menu-validation.json`.

## Regression status

Full regression completed serially in a disposable APFS-cloned save root:
`logs/mod-manager-native/data`. **15 of18 passed in44minutes**; compact receipt
`logs/regress-20261021-112705.txt` and `/private/tmp/defjam-mod-manager-full.txt`.
Passes: units, three goldens, overlay, unlock option, picture settings, fight, FFA,
FFA Terrordome, combat27/27, versus26/26, deterministic replay80records/1800steps,
crib and soak3/3. Failures: launcher USB callback, intro duplicate-name fixture,
gym selecting clothing instead of athletics; diagnoses below. No full-green claim.

Final fast units after the Python-only durability/receipt/junction tests:
**569passed,32skipped,1slow deselected**,37.51s with required normal cache/process
access (`/private/tmp/defjam-mod-manager-final-units-unrestricted.txt`). Focused
helper/native bridge:37pass. Catalogue13tests/22subtests pass; Actions39623189224
overall conclusion success on3597c0e (the job-summary API briefly retained a stale
in_progress status although all steps and the run were complete).
Native C/C++ inputs remained frozen throughout the full run; verify-build passes.

The first ad-hoc build lacked pipeline certification and the harness correctly
refused game tests. Cached analysis/lift inputs were copied and verified exactly
with `pipeline-state.py verify-lift`, then the official build pipeline issued the
receipt. An earlier quick attempt was superseded after UI source changed; do not
count its stale-build refusal as game coverage. Final native sources are frozen
for the full run.

Current observations: three goldens pass; actual launcher controls and Play work,
but one regression row flags the documented intermittent USB callback0x80003344
(`run-20261021-104553.log.err`, triaged and bounded around its caller0x0027EC6E).
Overlay, unlock option, picture settings and a120-second fight pass. Do not hide
that callback under an exclusion or claim a fully green suite. Focused repeats are recorded separately; they do not erase full-run failures.

Windows/macOS/Deck release acceptance is not inferred from unit/CI builds; hosted
R2/GameBanana/Thunderstore downloads and wrapper import are not yet tested.


### Later diagnostics retained

The final fast-suite attempt inside the filesystem sandbox had567passes and two
Setup-test failures: access to the normal user-cache lock directory and process
listing (`ps`) was denied. Repeating with the normal required cache/process access passed all569tests;
production safeguards were not changed for the test environment.

The full intro route (`run-20261021-111344.log.err`) hit
`Loc.GetString(~USR_NAME_EXISTS_XBOX)` at22306: its scripted new-profile name already
exists in this copied fixture. Gym (`run-20261021-112016.log.err`) reached the crib
and entered `story/clothing.swf` after the map, so its fixed shop-selection route
does not fit this profile/map state. Presents continue at median120/2s; neither
log is evidence of a renderer hang. Both digests and bounded original slices were
read. Keep these full-run failures visible even if focused fixture checks pass.


### Final launcher-driver repeat

The retained `scripts/test-mod-launcher.py` was run after the last helper change.
All three cases (update/order,820x560 at250% text, interrupted recovery preserving
skip=true) pass again, exit0 with byte-identical full-save restoration. Captures
inspected locally. Receipt:
`logs/mod-manager-tour-20261021-112754/launcher-receipts.json`.
A separate fresh-save focused repeat passes **2/2**: launcher9s, intro155s
(cutscenes reach the creator, median120presents/2s, four captures). Receipt:
`logs/regress-20261021-113118.txt`, compact output
`/private/tmp/defjam-mod-manager-fresh-routes.txt`. This confirms the original
intro failure depends on the copied profile fixture, and launcher Play passes on
the focused repeat. Gym remains an uncalibrated fixture/map route; no claim of
covering it with the launcher tour. No game processes remain from these checks.
