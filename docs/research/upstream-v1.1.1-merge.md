# Upstream v1.1.1 into FFNY Online: stage 1 (toolkit and game runtime)

Branch `merge/upstream-v1.1.1-stage1`, a real merge of upstream's tag `v1.1.1` (Def Jam Recompiled,
`origin/main`) into `remaster`, so later stages and the weekly merges have history.

## The merge base

`git merge-base` names ee439e9f (Oct 8), but that is wrong in content: build 44 already merged
upstream up to 2e6648c (v0.13.0 plus one commit). Our public history was rewritten on Nov 2, so our
copy of that commit is 86234fdd, with the identical tree (8de7c46b). The merge was made with that
base (`git merge-recursive 86234fdd -- HEAD v1.1.1`, parents remaster and v1.1.1): 236 upstream
commits, 25 conflicting files instead of 185. After this merge the next weekly merge finds v1.1.1 as
its base by itself.

## Toolkit

`tools/xboxrecomp` moves to `sweeax-exe/ffny-xboxrecomp` branch `ffny/v0.18`: our `ffny/v0.17`
(98c5294) with upstream's v1.1.1 pin 4801dc1 merged (ten commits on d569ca1, which v0.17 already
had). Conflicts: `d3d8_present.c` (our post-processing and upstream's filter mode 2 both kept) and
`nv2a_pgraph_d3d11.c` (our anisotropy setter next to upstream's mip-chain filter rules). All ten
commits are graphics (texture levels, anisotropy, MSAA, area downscaling, unaligned DXT, side
replay texture reuse, widescreen HUD placement); none touches the CPU, FPU, timing or input.

## Taken in (built)

- SC04 cinema zero-step fix: `src/patches/cinema_timing.c`, wrapper `sub_000766B0`.
- Widescreen: combo call-out, submission meter, venue map pins, Story map heading, song banner
  (`widescreen.c`, `story_map.c`, toolkit).
- Picture options `[display] msaa`, `texture_mips`, `color_tint`, `scratches`, filter
  `smooth (high quality)`; added to FFNY's own settings table, off/as before by default.
- Language packs on the French base, the Linux pack-name fix (`pc_language.c`).
- Test telemetry: isolated crowd (`RECOMP_TEST_ISOLATE_CROWD`), the logic generator seed hook
  (`sub_000C8820_enter`), damage reported before the health-line cap.
- Cheats (`cheats.c`) and the always-wrapped `Game.StartGame` (`dev_unlock.c`): built because
  `widescreen.c` and `dev_unlock.c` call them, but inert (the `[cheats]` table is not registered,
  so every cheat reads 0) and refused online (below).
- `CMakeLists.txt`: compiler cache option, `host_posix_keys.c`, `pc_language_settings.c`.

## FFNY Online additions in this stage

- `src/patches/online_guard.h`, `ffny_online_locked()`: while a session is open (any netplay state
  but off/failed, or lockstep), the cheat hooks, `RECOMP_UNLOCK_ALL`, `RECOMP_DEV_DEBUG` (its flag is
  cleared) and `RECOMP_START_VENUE`/`RECOMP_START_FIGHTERS` do nothing.
- `[display] frame_clock_lock` (default on, as upstream; `RECOMP_FRAME_CLOCK_LOCK=0` still works):
  the issue #140 steering, which build 44 took in, can now be switched off without a variable.
- `tools/defjam-mcp`: `DJ_COPY_EXE=1=<exe>` runs one copy of a scenario on another build (the
  mixed-build check).
- `project(VERSION)` stays 0.13.0: it is part of the online build fingerprint and the mods
  bridge's runtime version.

## Deferred (kept as ours, or in the tree but not built)

Files changed upstream between 2e6648c and v1.1.1 that this merge resolved as **ours** (their
upstream changes are not in the tree; the next stage re-applies them from
`git diff 2e6648c v1.1.1 -- <path>`):

- Stage 2, launcher and app: `src/hooks/pc_ui.cpp`, `pc_ui.h`, `pc_ui_internal.h`, `pc_ui_model.h`,
  `pc_ui_settings.c`, `pc_ui_theme.cpp/.h`, `pc_launcher.cpp`, `pc_launcher_home.cpp`,
  `pc_launcher_sdl.cpp`, `pc_mods.cpp`, `pc_debug.c/.h` (upstream moved `pc_debug_settings` into the
  catalogue), `src/hooks/pc_settings.c/.h` (upstream's settings catalogue refactor; ours keeps its
  table and gained only the picture options above), `src/hooks/README.md`; tests
  `test_launcher_model.py`, `test_mod_bridge.py`, `test_mod_manager.py`.
- Stage 3, setup, release and branding: all of `setup/`, `branding/`, `.github/`, `docs/media/`,
  `scripts/build-setup*.{ps1,sh}`, `check-setup-assets.py`, `check-release.py`, `linux_payload.py`,
  `make-branding.py`, `package-setup.py`, `check-source-tree.py`, `test-setup-updates-macos.py`,
  `test-hosted-menu-launcher.py` (upstream deleted it); tests `test_setup_engine.py`,
  `test_setup_updates.py`, `test_branding.py`. Upstream's new `.github/workflows/pages.yml` and new
  setup files (`setup/linux/glibc-symbols.h`, `setup/steam_shortcuts.py`) were left out.
- Prose: `AGENTS.md`, `PROGRESS.md`, `README.md`, `CONTRIBUTING.md`, `SECURITY.md`,
  `docs/03-workflows.md`, `docs/09-testing-harness.md`, `docs/launcher-and-overlay.md`,
  `docs/research/README.md`, `docs/settings-reference.md`, and the 36 docs upstream deleted in its
  cleanup (kept).

New upstream files **in the tree but not built** (`CMakeLists.txt` filters, `DEFJAM_BUILD_APP` off):

- Stage 2: `src/app/*`, `src/hooks/pc_settings_catalog.c/.h`, `src/patches/cheats_settings.c`
  (the `[cheats]` table: exposing cheats offline is a stage 2 decision), `scripts/package-app.py`,
  `package-appimage.py`, `build-app-linux.sh`; CMake preset `win-x64-app`.
- Stage 3: `scripts/build-toolchain-linux.sh`, `zig-glibc-symbols.py`, `build-site.py`, `site/`.
- Stage 4, songs: `src/hooks/pc_songs*.c`, `song_convert.c`, `song_decoders.c`, `song_library.c`,
  `src/patches/song_cursor.c`, `song_store.c`, `thirdparty/audio-decoders/`, and their hooks in
  `recomp_manual.c` (song store, playlist and jukebox wrappers, `sub_00131080_enter`,
  `sub_001350E0_enter`, the match-start and game-tick calls). The host shuffle in the
  `sub_00020D70` wrapper replaces a guest one when songs are added; that must be checked against
  lockstep before songs come in.
- `src/patches/options_volume.c`: not needed, FFNY's `src/hooks/options_audio.c` already hands the
  saved volumes to the mixer (the same bug, fixed independently).
- Unit tests for the parts above are skipped by `tests/unit/conftest.py` (`collect_ignore`).

Not needed at all: upstream's texture `cache_mb` fix (834d1264) repairs a regression of its own
settings-catalogue refactor, which FFNY never had.
