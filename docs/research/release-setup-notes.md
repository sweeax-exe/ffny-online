# Release setup implementation and validation

2026-10-08. Feature lane `feat/release-setup`, based on public macOS/Linux merge
`5cc5b3b0b6812ec039822cd2e80a7c7e3b2a38b5`. Toolkit remains the accepted pin
`8f08c4f99b7b6e8401033fe43a6be09e5679f5c4`. No dependency pin changes.
This lane records its work here; integration should fold the results into PROGRESS.

## Decisions and behavior

- Windows x64 first. macOS arm64 and Linux/Steam Deck x64 are named to-dos in
  `setup/platforms.json`. Reuse the merged Python pipeline and POSIX/Vulkan/SDL3
  implementation when those installers are taken up.
- Standalone native Windows controls avoid needing a renderer, generated code or
  a Python installation just to open the wizard. Embedded Python runs one shared
  engine for the wizard and silent mode.
- Choose separate installation and data directories. Copy the user's dump to the
  private data directory; preserve the original, saves and settings. Paths longer
  than 140 characters are rejected early while runtime/toolchain limits remain.
- Setup contains reviewed source, pinned toolkit, embedded Python, hashed Python
  wheels and offline SDL3/ImGui source. SHA-256 resource and full inventory checks
  precede execution. No game bytes or generated guest code enter its payload.
- Reuse the pinned toolkit's XDVDFS extraction code, with path, bounds and cycle
  guards. No separate extract-xiso dependency or executable is needed.
- Missing compiler installation requires explicit opt-in and Microsoft's signed
  bootstrapper. Setup itself stays per-user; elevation belongs to Microsoft.
- OS locks guard setup and launcher lifetime. Versioned source/builds and atomic
  launch receipts keep the previous installation active until success. Reusable
  analysis/lift artifacts must pass incoming pipeline certification; compiler
  objects are not copied between versions.
- The tagged release workflow attaches only the inspected setup, checksum and
  provenance to a draft. No release/tag was created by this implementation.

## Evidence

- 40 installer boundary/recovery tests pass, including valid synthetic XISO,
  malicious disc paths, tampered inventories, extraction cancellation, source
  repair, changed data roots, locks, and uninstall preserving saves/unrelated files.
- Native standalone launcher and setup build with static MSVC runtime. Hidden
  native wizard control smoke returns 0. Payload inspection imports the actual
  embedded Python dependencies and validates its matching PE resource.
- Packaged silent invalid-input run returns 2 without copying game data.
  Evidence: `logs/setup-e2e/invalid-result.json`.
- Packaged silent installation from an extracted verified dump returns 0 and
  certifies the Release game build. It uses its own copied dump/data/saves and
  source/build under `logs/setup-e2e`, leaving the usual developer data untouched.
  Evidence: `logs/setup-e2e/retry-result.json`, `retry.log`.
- First real attempt found an omitted toolkit `tests/d3d8_smoke` source dependency
  required by its unconditional CMake subdirectory. Added precisely that source
  directory; retry reused certified analysis/lift and compiled successfully.
- Installed game checks: all four pass (m2 frame, m3 title, m4a menu and fight).
  Fight reached 90 seconds of observed combat, median 120 presents per two seconds.
  Report: installed source `logs/regress-20261008-143602.txt`.
- All 213 project unit tests pass under VS 2019/MSVC; the three corrected fixture
  files separately pass all 10 tests. Packaged repair during the installed game
  run returns the documented busy exit 5 (`logs/setup-e2e/busy-result.json`).
- Installed native launcher succeeds from an unrelated working directory, with
  a deliberately incorrect inherited data environment; the receipt supplies both
  the source directory and correct data. `logs/setup-e2e/launcher-result.json`.
- First repair test caught CMD rejecting backslash-escaped nested quotes in the
  toolchain handoff (exit 4, executable unchanged). Replaced it with a constant
  command expanding one quoted environment variable, without CALL's second
  expansion. Its native regression covers spaces, ampersands and literal percent.
- Fresh-process update then exposed CreateProcess ignoring the child environment's
  PATH for locating bare executable names. Resolve every bare tool name against
  the imported compiler PATH before starting it; captured environment output is
  excluded from logs. The earlier failure left the active receipt and synthetic
  save/settings files unchanged (`logs/setup-e2e/update-result.json`).
- Corrected production-format installer update succeeds from a fresh process
  without developer compiler paths, reuses certified analysis/lift, rebuilds in
  its own version root and activates the new receipt. Both synthetic save and
  settings files remain byte-identical. Evidence: `logs/setup-e2e/update-fixed.log`
  and `update-fixed-result.json`. Packaged source identity is
  `ee27538d0bc181c0ea2cfde9f5cdc7da1bf263ce`; this is a local unsigned artifact,
  not a published release.
- Same-release packaged repair succeeds and reuses certified extraction, analysis,
  lift and build. The game executable plus both save/settings fixtures remain
  byte-identical (`logs/setup-e2e/repair-fixed-result.json`, three files checked).

## Review handoff

Branch `feat/release-setup` contains only public-history feature commits, based on
the accepted public merge. The original private integration checkout and its
unrelated changes remain separate. Setup assets are ignored local build output
under `build/setup-assets`; no game executable/data is staged or exported.

Public branch push was rejected by automatic approval review because the current
session lacks explicit authorization to export these source commits to
`vyanhursky/DefJamFFNY-recomp`. No push, PR, tag or release was created. Obtain owner
approval for that exact repository and source-only payload, then push the feature
branch, create a draft PR, attach it to this chat, and inspect remote CI. Use the
explicit public URL; this worktree's inherited origin remains the private repo.
- Merged port exposed missing `src` include paths in three native Python fixtures;
  fixed fixture compile commands without changing runtime behavior.

## Remaining acceptance gates

- Clean Windows 11 VM with no compiler/Git/Python, VS 2022 provisioning and restart
  behavior, independent real ISO/XISO, Unicode paths, physical-machine wizard and
  gameplay acceptance, peak disk use/timings, code signing/SmartScreen decision.
  Hyper-V enumeration on this host is unavailable with current account permissions.
- Update across changed toolkit/lifter inputs and interrupted compiler installation.
  The tested source-commit update keeps the existing toolkit pin and runtime inputs.
- Show source/toolkit identities in the game's About UI (currently in install logs,
  JSON provenance/receipt and installed source).
- macOS installer: arm64 toolchain/provisioning, MoltenVK/shaderc, bundle/signing/
  notarization, install/data conventions and native graphics/gameplay acceptance.
- Linux/Steam Deck installer: distro/toolchain/runtime dependencies, Vulkan/SDL3,
  desktop/Steam launch integration, writable locations, controller/gameplay tests.
  The accepted port's hardware-free checks alone cannot establish these.
