# Setup updater implementation: initial test results

Candidate: draft [PR #18](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/18),
`codex/setup-github-updates`, based on v0.7.2 `ce4d4ab`; development payload made
from implementation commit `f238afe` (development mode). Earlier evidence used
the dirty feature checkout at base commit `700e6c9`. Toolkit remains
`2992bd85277cd6a7a208158588c7d91917dc59aa`. No new release/version/tag was created.
The fixture labels 0.8.0 and 0.9.0 are local test identities, not published releases
or tests of those versions' game changes. The [73-family contract](setup-update-test-plan.md)
remains the release gate; this ledger does not mark whole families complete based
on a subset of their variants.

Host: Apple M1 Pro, macOS 26, arm64, APFS, Xcode Command Line Tools installed.
Python 3.13 isolated test environment; packaged Python/CMake/Ninja/runtime used
for native fixtures. Pinned third-party runtime build cache was reused; the new
payload inventory and embedded runtime imports were verified independently.
Local raw evidence is retained under `logs/setup-updates/2026-10-12-macos/` and
`/private/tmp/defjam-updater-dev/`, with the real disposable install at
`/private/tmp/dju-real/`. These directories are ignored and never published.

## Executed evidence

| Test / case variants | Result and limit | Evidence |
|---|---|---|
| Shared project unit suite; common policy/network/archive/cache/ownership/locks/handoff tests | 339 passed, 30 skipped on macOS. Skips include native Windows job test and platform-specific fixtures. Does not establish all common-family variants on every OS. | `unit-final.log`; `tests/unit/test_setup_updates.py` |
| OP01 / OP05 / OP07 intact / OP08 / OP11 B→A | Native synthetic install, clean update, repair, clean rebuild and future downgrade all pass. Real CMake/CLT compile, distinct attempt roots and ad-hoc signed stable launcher executed; version/data/CWD match; settings/save sentinels unchanged. Corrupt repair, skipped C variants and option permutations remain pending. | `native-results.json`, five positive cases plus final selected-engine update/downgrade; `scripts/test-setup-updates-macos.py` |
| TX01/03/05/06/08 subset; TX09/10 validation | Unit subprocess recovery: six checkpoints × three hard exits. Unsafe/tampered journals and interrupted recovery retry tested. Native signed-app engine also exits 91 at prepared/launcher/JSON receipt, recovers and executes prior launcher successfully (three cases). This is process termination, not reboot or power-loss evidence. | `test_setup_updates.py`; `native-results.json`, three fault cases |
| MAC07 controls/preparation subset | Packaged `--ui-smoke` and `--ui-preparation-smoke` exit 0. The preparation probe expects invalid nested-path rejection, a retained log and UI timer progress. | `packaged-smoke.json` |
| NET/HAND production endpoint smoke | Bundled Python fetched 17 public release entries through GitHub TLS. Actual v0.7.2 dmg download, SHA/provenance/inventory validation, read-only mount and historical own-engine handoff completed. Deliberately missing dump exits 2 as expected; this is not a completed remote game install. | `live-releases.json`; `historical-handoff.log` |
| Game-free dmg/package | Build, bundled imports and source/payload inventory pass: 4,867 files, 64,060,226 ZIP bytes in this local development build. No game/generated code/data in payload. | `build-final-head.log`; dmg SHA-256 below |
| Owner-dump fresh macOS install | Install succeeded using the owner's verified extracted dump and bundled tools; fresh analysis/lift/build (17,896 translated functions, five manual exclusions). Saves were copied into disposable data; original save root was never used by the game. | `real-install.log`; `/private/tmp/dju-real/app/installed.json` |
| Owner-dump local future clean update | Initial 0.7.2→local 0.8.0 update succeeds without original image, with a fresh candidate and retained old build. Of 120 non-extracted files, only the generated disassembly cache changed. This exposed data/analysis rewriting; the final implementation uses `pipeline.py analyze --no-archive` to preserve it, with a focused test. Final local 0.8.0→0.9.0 clean update passes with all 120 non-extracted files byte-identical, including the analysis cache; three builds retained. It first correctly refused insufficient space before staging, then passed after removal of obsolete synthetic test builds. | `real-update.log`; `real-update-preservation.json`; `real-update-final.log`; `real-update-final-preservation.json` |
| Installed game menu/fight and reviewed captures | M4a menu and full fight checks PASS; fight median 120 presents/2 s (60 fps), minimum 113. Initial M2/M3 runner failed because setup intentionally omits test/golden JSON metadata. Supplying reviewed hash-only metadata then rechecking existing loading/title captures passes (M2 frames 40/70/100/150; M3 title-5s). The original runner remains exit 1 / two of four checks PASS; do not call it quick 5/5 or a four-of-four runner pass. | installed source `logs/regress-20261012-125747.txt`, preserved initial run outputs; `golden-recheck.json` |

The clean-updated local 0.9.0 game subsequently passed a separate M2/M3 runner:
**two of two PASS** (`logs/regress-20261012-133441.txt`; `updated-game-smoke.out`).
Reviewed hash-only golden metadata was supplied before running; baseline values
were not changed. The earlier failed runner remains separate evidence.

First hosted implementation CI at `a43cdd6` (run 38304792498): macOS/Linux setup
lanes PASS; Windows had 29 failures /117 passes /21 skips. Failures exposed Windows
read-only-descriptor flush semantics, a raw ZIP-name normalization issue and an
open-cache-reader race; an uninstall fixture also lacked the required data path.
These were corrected with the checks retained. Final rerun and checksum/selected-handoff additions are tracked on PR18; do not count
this first failed lane as acceptance. `ci-first-failed.log` retains its trace.

Final local `f238afe` native execution: **ten of ten PASS**. This repeats the
original eight cases and adds selected-engine update and downgrade. Target bundled
Python/engine built and activated each selected release, then its signed stable
launcher reported the expected version/data/CWD. The report includes host, source,
toolkit and payload-manifest SHA-256: `native-final-head.json`; output
`native-final-head.log`. Live checksum-file cross-check passed against GitHub's
actual v0.7.2 Mac artifact (`live-checksum-smoke.log`). Obsolete synthetic build
folders were pruned for disk headroom; their reports remain retained.

[Corrected CI run ae2c890](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/38305557205)
passed all three setup lanes and the workflow. It includes the Windows startup-gate,
native job cleanup, raw ZIP and writable file-flush fixes. The checksum and two
extra selected-handoff cases are in final code `f238afe`; its hosted run is
[38306033576](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/38306033576).
**Final code CI: 25/25 jobs PASS**, including Windows/macOS/Linux setup lanes
and the ten-case native Mac fixture. JUnit and native game-free JSON/log artifacts
are retained even on failure. This evidence-only follow-up changes no tested code.

Local development dmg SHA-256: `d1a7027fc0ef0329eeb1d681aaa351a4bdc136bea895d7b81af99f702ca2b75f`.
It is not a tagged release artifact and is not attached to the PR.

## Pending acceptance and caveats

Windows and Steam Deck native owner-dump acceptance are **NOT RUN** in this
session. The three setup CI lanes now execute shared updater tests; macOS CI also
runs the eight native synthetic cases. Hosted green packaging/tests cannot replace
Windows standard-user/UAC/SmartScreen or physical SteamOS/Gaming Mode/Steam tests.
No desktop-Linux updater acceptance is claimed.

MAC01 minimum OS, compiler repair, quarantined browser-download/Open Anyway,
interactive release picker/cancel/close/accessibility, Finder/Dock launch, external
volumes/case-sensitive APFS and owned shortcut permutations remain pending.
The Mac was locked when interactive UI inspection was attempted. Offscreen PNG
creation is not counted as successful visual inspection. Local signed launcher
CLI tests and automated AppKit preparation probes passed.

The option manifest/pairwise matrix, real HD generation/HD on-off acceptance,
legacy→new rollback on each native host, skipped-version updates, corruption repair,
uninstall/reinstall, disk-full/permissions faults and reboot/power-loss TX12 still
need the planned variants. Real HD generation was not run because available disk
headroom was below its extra 15 GiB requirement. No full runtime/lifter/renderer
change was made; the toolkit pin and game code remain unchanged.

No published updater-capable pair exists. Historical downgrade is rejected;
future downgrade is tested with native game-free peers. First U0 release still
needs owner review and the pending acceptance gates. Old builds and failed attempts
are retained; repeated clean operations need disk headroom and time for fresh
analysis/lift/compile. Data relocation is not a supported update operation.

The selected Windows engine uses a kill-on-close process job, following
[Microsoft's job object contract](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects);
native Windows CI checks descendant cleanup. This adds no elevation or breakaway
policy. Process-kill recovery tests do not prove file-system durability after power
loss, especially on NTFS/removable volumes; those gates remain pending.
