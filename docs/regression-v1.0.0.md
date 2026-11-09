# v1.0.0 release candidate: regression and acceptance

This candidate includes the original 13 PRs from the renamed v1.0.0 milestone plus design #125 and launcher UX PR #179. Documentation cleanup #171 subsequently landed on main and is forward-merged to keep the candidate compatible. The release PR is the result tracker; do not merge individual PRs to main while testing. Main's v0.13.0 release remains the stable download. No full gameplay/platform regression has yet qualified this combined source tree.

## Identity and source coverage

Use the exact full head SHA shown by the release PR for every platform. Save `git rev-parse HEAD`, `git status --short`, and `git submodule status --recursive` with each result. A runtime/toolkit change requires a new candidate and affected-platform retests.

Toolkit pin: `d033837332a2c8ca8e59113edac17a474fb3a9c8`, fork branch `defjam/release-1.0.0`. Main `d569ca1` plus `c76b44d`→`124715b`, `d4f5890`→`82cb9d7`, `ebf6f12`→`d033837` (all cherry-picked with `-x`). Every original PR head and the refreshed heads below are ancestors of the release branch.

| PR | Change | Integrated head | Decisions |
|---|---|---|---|
| [#125](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/125) | docs: one app for setup, launcher and updates (design, D214/D215) | `ee3c2421d960757d5e9eb51a31bac83b6cd207d2` | D214, D215 |
| [#126](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/126) | feat(setup): Update reuses verified analysis; close stage-key gaps | `e1afe21ea82b494ee21c65885431a3c344f137c9` | D255, D256 |
| [#127](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/127) | feat(app): game-free app with the launcher's Home and Settings; Play starts the game | `00e1f7d8bb176b3990b2ec1b8ffdc38b6dd0147c` | D257, D258, D259, D260, D261 |
| [#128](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/128) | feat(app): setup inside the app: first run, Repair, Clean Rebuild | `7da10c32a9247371974eae25a0539e78d3aae7f6` | D262, D263, D264, D265, D266 |
| [#130](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/130) | feat(app): the app updates itself from GitHub and offers the update build | `fa901f0fe1ad5e78bff6121118cb2555c303a83e` | D267, D268, D269, D270, D271, D272, D273 |
| [#132](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/132) | feat(release): package the macOS and Windows apps and publish them with the setup | `45c3907376aed6bace3884b7aa72261a5370cf45` | D274, D275, D276, D277, D278, D279, D280 |
| [#144](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/144) | feat(app): Add to Steam with the library artwork on Windows | `85a5ef3601ef9aa07b3336761ea275ce8ae896b7` | D281, D282, D283, D284, D285, D286, D287, D288, D289, D290, D291 |
| [#168](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/168) | feat(app): the Linux app as an AppImage that installs, updates and adds itself to Steam (design §6, part A) | `c6d600df44660b31ad79cb92dc3f5b3eca86bf0d` | D292, D293, D294, D295, D296, D297 |
| [#170](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/170) | feat(setup): Linux builds the game with the payload's own Zig toolchain (design §6.3, part B) | `bbfcd12eecda0d2c96418c77904f426505932236` | D298, D299, D300 |
| [#151](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/151) | feat(display): texture levels, anisotropic filtering, MSAA, HQ downscaling, colour tint, film scratches | `bb61dc4d4e46046924ef68ccc9dd14e73f668d2c` | D301, D302, D303, D304, D305 |
| [#97](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/97) | feat: cheat menu for issue #71 | `08d33ff0906b8183be728a6091fa925eb705fcb0` | D306, D307 |
| [#146](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/146) | fix(test): isolate the pinned fight from the host's timing, --isolate-crowd (#98) | `0f858d37d805e46aab1e5d342bd44bd58fbb593a` | D308, D309 |
| [#164](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/164) | fix(cinema): prevent SC04 zero-step animation drift (#161) | `36d4e06b36f3861ce4253627499e200b258bb79e` | D310, D311, D312 |
| [#133](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/133) | feat(site): GitHub Pages project site, published with v0.13.0 | Removed: merge `38a7be9` reverted by `e2cd464` (owner, 2026-11-04) | D313, D314, D315, D316 |
| [#179](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/179) | Setup/launcher UX, presets, Mods tabs, updates/recovery, DPI layouts and approved typography A | `6859addd2120b227e81bd7eb25da26b88c9000eb` (merge `87fb38f`) | D318–D323 |

The app stack was refreshed by forward merges; its current Steam/uninstall work is retained by Linux and graphics. Cheats #97 depends on #151. The RC merges both Linux and graphics, then cheats, determinism, SC04 and site work. PROGRESS keeps main's landed records, restores reserved D214/D215, and numbers PR decisions once.

## Prepare each platform

Use a separate checkout and disposable test data/save copy. Read the repository's data verification instructions; game data, recordings, captures and generated guest code stay local. Keep game runs serial per machine. Keep macOS/Deck awake and plugged in for the full run. Use your existing verified dump and test fixture; record OS/GPU, compiler and any mod versions/receipts.

```sh
git clone --recursive --branch release/v1.0.0 https://github.com/vyanhursky/DefJamFFNY-recomp.git defjam-1.0-rc
cd defjam-1.0-rc
# Replace CANDIDATE_SHA with the exact release PR head on all three machines.
git checkout --detach CANDIDATE_SHA
git submodule update --init --recursive
git rev-parse HEAD
git submodule status --recursive
```

Set `DEFJAM_DATA` to a disposable test root containing the verified `extracted/` dump and copied saves; link `game` to its extracted dump as described in the platform build guide. Install the documented Python/native dependencies. Use a fresh analyze, lift and build: new guest hooks and toolkit changes require regenerated source.

macOS / Linux source-build lane:

```sh
python3 scripts/pipeline.py analyze --no-archive
python3 scripts/pipeline.py recomp
python3 scripts/pipeline.py build --preset posix-release
python3 scripts/regress.py --preset posix-release
python3 scripts/regress.py --preset posix-release --only repeat,replay --isolate-crowd
```

Windows (PowerShell, with the project environment initialized):

```powershell
. ./scripts/env.ps1
python scripts/pipeline.py analyze --no-archive
python scripts/pipeline.py recomp
./scripts/build.ps1 -Preset win-x64-release
python scripts/regress.py --preset win-x64-release
python scripts/regress.py --preset win-x64-release --only repeat,replay --isolate-crowd
```

The default full suite retains the plain replay golden. The extra isolated checks exercise #146 and retain its separate golden. Never use `--update-golden` to make a failure pass: #170 already reports 80 current-code records against #146's 79-record golden. First establish repeatability on the same source, then compare platform streams and investigate any changed expectation separately.

For the established HD widescreen lane, use `--release-profile` and, when testing full local Lanczos textures, `--lanczos-receipt PATH`. This explicitly verifies Menus1.4.0/Fighters1.0.0, records identities, uses 16:9/2048MiB, and preserves stock frame-golden runs. The setup, generated-pack and receipt commands remain in [the v0.13.0 reproduction guide](regression-v0.13.0.md); use `--runtime 1.0.0` for this candidate. Record your chosen lane; stock and HD results describe different configurations. Retain the original 64MiB memory-growth gate and all failures; POSIX RSS and Windows private bytes are distinct metrics (#166).

## Stable release result (2026-11-06)

The stable v1.0.0 is `main` after beta 2 (#186, #191, #193, #194) plus #190 and #198, toolkit `6c0bd55` (fork `defjam/release-1.0.0`), release PR #197. The Windows results below are on `4b73e78` (toolkit `351e28a`), before #198 landed; #198 was not regression-tested here (owner: no further full regression). The owner qualified it on Windows only (D340); macOS and Steam Deck were not rerun on this head. The table below keeps the candidate gates; their Windows column for the beta-1 candidate is in `PROGRESS.md` §6 (2026-11-04).

| Gate on head `4b73e78` | Windows result | Log |
|---|---|---|
| Full regression, stock lane | 17/17 pass, 4 skipped (launcher, overlay, unlockopt, picture) | `logs/regress-20261106-110329.txt` |
| Full regression, `--release-profile` (16:9, Menus 1.4.0, Fighters 1.0.0) | 16/17: **combat FAIL** `performance.memory_growth` 105.2 MB > 64 MB; rerun `--only combat` 99.2 MB, FAIL. Private bytes flat ~2,390 MB during play; the last sample is in the post-fight scene change (+~110 MB, same as the pre-fight change at ~80 s in every run incl. the beta-1 candidate's 23.2 MB pass). Not changed; #166. | `logs/regress-20261106-115208.txt`, `logs/regress-20261106-115740.txt` |
| #190 routes `story-map-shops`, `battle-map-edges` at 16:9 with Menus 1.4.0 | pass (captures reviewed) | `logs/run-20261106-095036.log.err`, `-095213` |
| Setup: v0.13.0 install, then Clean Update with CI run 42260186582's setup | pass: 400 s + 390 s, data folder 119 files identical | `C:\dj13\r1000\up\` (local) |
| Setup: fresh install with the same setup | pass: 426 s, exit 0 (installed games not started) | `C:\dj13\r1000\new\` (local) |
| macOS, Steam Deck full regression | not run on this head (D340) | |

## Acceptance beyond the full suite

| Gate | Windows | macOS | Steam Deck / Linux |
|---|---|---|---|
| Full default regression on exact candidate SHA | Pending | Pending | Pending |
| Isolated repeat/replay; compare streams across platforms | Pending | Pending | Pending |
| Graphics opt-in checks and visual review | Pending | Pending | Pending |
| SC04 dialogue timing and other scene/transition controls | Pending | Pending | Pending |
| Cheats: Max Blazin' player input, Story swap, confirmed one-shot actions | Pending | Pending | Pending |
| Install from candidate CI app/setup assets into scratch install | Pending | Pending | Pending |
| Clean Update from v0.13.0, Repair, restart, Play, cancellation | Pending | Pending | Pending |
| Steam shortcut, overlay and input; Deck Game Mode | Pending | N/A: Steam integration not implemented | Pending |
| Owner combined-candidate play-test | Pending | Pending | Pending |

Follow [graphics test plan](research/graphics-enhancements-test-plan.md), [SC04 evidence/limits](research/sc04-fix-validation.md) and [cheat coverage](research/cheat-menu.md). Existing installs retain their settings. Fresh setup now recommends Modern (widescreen/2x) with optional generated textures; Original is selectable. Test both preset contracts in [launcher UX acceptance](launcher-ux-candidate.md). Review the old Windows visual references rather than raising thresholds: thin background textures now draw after the DXT fix, while the crib ticker also needs inspection. Uninstall acceptance uses a scratch install and copied saves. #126's Windows analysis path and #170's bundled Zig build need acceptance on this combined candidate. The ordinary Linux source-build lane above uses the host toolchain; the CI AppImage installation is the bundled-toolchain lane. Repeat Deck scene-load measurements before treating #170's one-run 10–25% load-time difference as established.

App/setup assets are game-free and each needs its SHA-256 and provenance. CI builds are development candidates; stable-release provenance and a real GitHub-app self-update remain later release acceptance. The owner took #133 (project site) out of v1.0.0 on 2026-11-04; it stays open for a later release, which still needs Pages activation (API404) and a review of the app/install copy.

## Return evidence

Send the exact candidate/toolkit SHA, OS/GPU/toolchain, command and selected profile, complete `logs/regress-*.txt` table, save-restoration result, capture-review verdicts and every failing row's triage summary. Confirm failures with `--only NAME`. Keep raw logs, saves, generated guest code, images/audio and game executables local. Existing open issues #98/#141/#161/#162/#166 and prior release qualifications remain open; integrating a PR does not establish that its acceptance gates passed.

## Launcher UX follow-up (#179)

The owner explicitly assigned full launcher regression on Windows, macOS and Steam Deck/Linux to the platform agents. Use PR177's current head, not the preceding d79ca62 or a locally installed older app. Feature-head checks: native macOS app/legacy builds, six shared game/overlay objects, 67 focused tests / 1 platform skip and isolated UI checks; no complete game run or actual install/update/uninstall/mod operation was performed in the UX implementation pass.

In addition to the full suite, complete [the launcher acceptance checklist](launcher-ux-candidate.md): both fresh presets and existing-install preservation; optional Upscaled Original Textures and real downloadable texture/language packs; manual/automatic updates and two-phase restart/rebuild; backup/recovery/Steam behavior; keyboard/controller navigation and rebuilt overlay; Windows 4K at 300%, mixed DPI and large app text; Deck Game Mode. Typography A should retain normal-size title-case Home labels in a heavier UI face, with PLAY visually strongest. Record platform font availability if fallback occurs.
