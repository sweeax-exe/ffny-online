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

- **Updates itself** (`app_update.cpp`). At start (unless `[launcher] check_updates` is off) the
  engine looks on GitHub for a newer app (`--check-app-update`); Update downloads and verifies it
  beside this one (`--stage-app`, `setup/releases.py` `select_app`), swaps it in and starts the new
  app, which always shows its window and offers to build the game it carries (a Clean Update from
  its own payload). On macOS the bundles are swapped. On Windows the app is a folder (the program
  and `payload\`), and a folder with a running program in it cannot be renamed: the program and
  `payload\` are renamed aside to `.old` one by one, each step undone if a later one fails, and the
  new copy removes the `.old` files when it starts. `select_app` reads the Windows program's
  built-in payload from its resource table (`pe_resource`, nothing is run) and checks it against
  the release's provenance.
- **Installs itself on Windows** (`app_install.cpp`). The released program,
  `DefJamRecompiled-<version>-windows-x64.exe`, carries the setup payload ZIP as resource 101 with
  its SHA-256 built in, as `DefJamSetup.exe` does. Opened from wherever it was downloaded, it asks
  where to install itself (`%LOCALAPPDATA%\Programs\DefJamRecompiled` unless the player chooses
  another folder; not Program Files, OneDrive, a network or FAT drive), copies itself there, unpacks
  its payload beside it, records the folder in `%LOCALAPPDATA%\DefJamRecompiled\location.json` and
  starts the installed copy. Opened again, the download hands over to the installed copy, first
  replacing it when the download is a newer release. No Start-menu or desktop shortcut is made
  (owner's choice, 2026-10-27). The installed copy unpacks its payload again when `payload\` is
  missing or from another build (`payload.sha256`).
- **The play wrapper forwards.** Whenever it loads an install, an installed app writes where it is
  to `<install>/app-location.txt` (the bundle on macOS, the program on Windows). The play wrapper
  setup keeps in the install folder (and in Applications, for installs an older setup made;
  `setup/macos/Launcher`), and `DefJamLauncher.exe` on Windows, which the old Start-menu and desktop
  shortcuts open, then open the app instead of the game (`--install <root>`; on macOS
  `--no-launcher` as `--play`). Without that file, or with the app gone, the wrapper plays the game
  as before.

On every platform the window is SDL3's, drawn with SDL's renderer (Direct3D 11 on Windows, Metal on
a Mac). On Windows, the toolkit's own SDL3 is built for pads only, so the app is built on its own,
without the game: the `win-x64-app` preset fetches the same pinned SDL3 release with video, the
renderer and the file dialogs on, and links everything with the static C runtime, so the app needs
nothing but Windows (it runs before any Build Tools or Visual C++ runtime are installed). The
program is `DefJamRecompiled.exe` there, with the shield icon and a manifest that sets the UTF-8
code page, long paths and per-monitor DPI.

The macOS and Windows apps are packaged and published; Linux (the AppImage) is not built yet.

## Packaging

**macOS.** `scripts/build-setup.sh` builds it with the setup, from the same payload: `defjam_app` is
compiled with the payload's own CMake, Ninja, SDL3 and Dear ImGui source (nothing else on the build
machine), `scripts/package-app.py` makes `Def Jam Recompiled.app` (the program, its Info.plist and
icon, and the payload in `Contents/Resources/payload`, whose SDL3 the program loads through its
rpath; signed ad hoc) and its ZIP, and `scripts/check-setup-assets.py --app` checks that archive
file by file against the inspected payload before writing `DefJamRecompiled-<version>-macos-arm64.zip`,
its `.sha256` and its `kind: app` provenance beside the setup's.

**Windows.** `scripts/build-setup.ps1` builds the payload and `DefJamSetup.exe`, then the app with
the preset `win-x64-app` and `-DDEFJAM_APP_PAYLOAD=<payload zip>`, which builds the payload in as
resource 101. `scripts/check-setup-assets.py --app` checks that the program's payload is the
inspected one byte for byte, that it imports only Windows' own libraries (no Visual C++ runtime) and
that it holds no lifted-code names, then writes `DefJamRecompiled-<version>-windows-x64.exe`, its
`.sha256` and its `kind: app` provenance (with `payload_sha256`) beside the setup's.

The release workflow attaches both with the setup assets (D215).

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
  see `src/hooks/pc_launcher_sdl.cpp`; `uiwait:<id>` waits up to half an hour for a control), with
  the setup screens' controls named `setup.*`, `update.*` and `install.*`. On Windows
  `DEFJAM_APP_USER_BASE` stands in for `%LOCALAPPDATA%` (the app's home, its location record, logs
  and cache), so a test never touches the player's own.
  `RECOMP_APP_NO_DIALOG=1` reports a failure on stderr only. After a self-update the new app gets
  `RECOMP_LAUNCHER_SCRIPT_AFTER_UPDATE` as its script, or none.
