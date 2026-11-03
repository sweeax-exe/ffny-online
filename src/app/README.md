# The app

`defjam_app` is one app for setup, launcher and updates, in progress
([design](../../docs/research/unified-app-design-2026-10-26.md), steps 2 and 3). It holds no game
code: it sets up the game from the player's own dump, shows the launcher's Home and Settings, and
Play starts the game that setup built on this computer, with `--no-launcher`.

- **Setup inside the app** (`app_setup.cpp`). With nothing installed it opens on the first run:
  the dump (an image or an extracted folder), the install and data folders, and HD textures.
  Setup makes no shortcuts: the app the player downloaded is the only copy, and they move it
  wherever they like. Home's Update / Repair, and Advanced > Maintenance, open Maintenance:
  Repair and Clean Rebuild. Each runs `setup/engine.py` from the app's setup payload with that payload's Python, as
  the setup wizards do: the choices on its command line, its progress read from `--status-file`
  every frame and shown as a list of stages, Cancel through `--cancel-file`, and its exit code and
  last status line for how it ended. Closing the window during an operation asks it to cancel.
  On Windows the status file and setup's log are read with delete sharing, so the engine can
  replace the status file while the app reads it (#40), and the app first asks the engine whether
  Microsoft's C++ Build Tools are installed (`--check-toolchain`); when they are not, the first-run
  and Maintenance screens offer to install them with the setup (`--install-prerequisites`), and a
  setup that stopped for them (exit 3) offers **Install Build Tools**. Exit 3010 means they were
  installed and Windows must restart first.
- **Same screen, same file.** It draws `src/hooks/pc_ui.cpp` built with `DEFJAM_APP`, which leaves
  out the overlay and the Debug page's controls for a running game (the recorder's mark, Story
  scenes, EA's developer menu). It registers the same catalogue
  (`src/hooks/pc_settings_catalog.c`) and edits the same `settings.ini` as the game.
- **Like the play wrapper.** It holds the install's `play.lock` (letting it go while setup works,
  since setup takes it), reads `installed.json`, sets `DEFJAM_DATA`, `DEFJAM_INSTALL_DIR` and the
  working directory, and keeps the session log beside the wrapper's
  (`~/Library/Logs/DefJamRecompiled`, `~/.local/state/DefJamRecompiled/logs`, or
  `%LOCALAPPDATA%\DefJamRecompiled\logs`; setup's own log is `setup-<time>.log` there). Off
  Windows, Play then `exec`s the game: the lock lives as long as the game, and the app is gone when
  the game ends. Windows has no `exec`: the app closes its window, starts the game, holds the lock
  while it waits for it and then exits with the game's exit code, as `DefJamLauncher.exe` does. The
  game's own run logs stay in `logs\` under its folder there (`DEFJAM_LOG_DIR`).
- **When the window shows.** Same rule as the game's launcher (`pc_launcher_wanted`): skipped when
  "Skip launcher" is on, unless Shift is held or `--launcher` is given; always shown when mods need
  recovery or nothing is installed. `--play` always skips it.
- **System calls** are in `app_os.cpp` (POSIX and Windows halves), so the rest of the app is the
  same on every platform.

On every platform the window is SDL3's, drawn with SDL's renderer (Direct3D 11 on Windows, Metal on
a Mac). On Windows, the toolkit's own SDL3 is built for pads only, so the app is built on its own,
without the game: the `win-x64-app` preset fetches the same pinned SDL3 release with video, the
renderer and the file dialogs on. The program is `DefJamRecompiled.exe` there, with the shield
icon and a manifest that sets the UTF-8 code page, long paths and per-monitor DPI.

Nothing packages or installs the app yet.

```
# macOS, Linux
cmake -S . -B build/app -G Ninja -DDEFJAM_BUILD_GAME=OFF
cmake --build build/app --target defjam_app
build/app/defjam_app --payload <unpacked setup payload> [--install DIR] [--dump PATH] [--data DIR]

# Windows (after . .\scripts\env.ps1)
cmake --preset win-x64-app
cmake --build --preset win-x64-app
build\win-x64-app\DefJamRecompiled.exe --payload <unpacked setup payload> [--install DIR] [--dump PATH] [--data DIR]
```

- **Install folder.** `--install DIR`, else `DEFJAM_INSTALL_DIR`, else the nearest folder above the
  program that holds `installed.json`, else the usual place (`~/Library/Application
  Support/DefJamRecompiled/app`, `~/Games/DefJamRecompiled/App`, or
  `%LOCALAPPDATA%\DefJamRecompiled\app`, where the old Windows setup installed).
- **Setup payload.** `--payload DIR`, else `DEFJAM_SETUP_PAYLOAD`, else `payload` in the bundle's
  `Resources` or beside the program.
- **First-run choices.** `--dump` and `--data` fill them in.
- **Test hooks.** The launcher's work here too (`RECOMP_LAUNCHER_SCRIPT`, `RECOMP_WINDOW_SHOT`;
  see `src/hooks/pc_launcher_sdl.cpp`), with the setup screens' controls named `setup.*`.
  `RECOMP_APP_NO_DIALOG=1` reports a failure on stderr only. After a self-update the new app gets
  `RECOMP_LAUNCHER_SCRIPT_AFTER_UPDATE` as its script, or none.
