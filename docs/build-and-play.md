# Build and play on Windows

This guide covers the Windows setup installer and a manual source build. Both
build the game locally from your own supported USA Xbox dump. No precompiled
game or game assets are distributed. Windows 11 x64 is the tested environment.

## Install with the Windows wizard

Download `DefJamSetup-<version>-windows-x64.exe` from a release that includes
setup on the [release page](https://github.com/vyanhursky/DefJamFFNY-recomp/releases).
The commands below use the v0.5.1 filename. For local testing, run the supplied
preview build.

1. Run the setup executable and select your ISO/XISO or extracted dump folder.
2. Choose separate sibling folders, such as `D:\Games\DefJam\App` for the
   installation and `D:\Games\DefJam\Data` for the dump copy, settings and saves.
   Data must not be inside the install folder. Keep 12 GiB build headroom plus
   space for the copied dump and compiler.
3. Choose **Create a desktop shortcut** if wanted. Clearing it keeps the
   Start-menu shortcut. Enable **Install missing Microsoft Build Tools** only
   to permit Microsoft's installer to request consent/UAC for missing tools.
4. Select **Install / Repair**. Setup bundles Python and pinned build sources,
   verifies the dump, then analyzes, lifts and compiles it locally. Git, system
   Python and a separate extract-xiso installation are not needed for setup.
5. After completion, select **Play**, use the shortcut, or double-click
   `D:\Games\DefJam\App\DefJamLauncher.exe`. The wizard displays your exact
   launcher path; it supplies the correct data location and working directory.

**Open logs** opens the current session file. Packaged setup logs default to
`%LOCALAPPDATA%\DefJamSetup\logs`, including failures before installation starts.
Close the game and rerun setup with the same destinations to repair/update;
saves/settings stay in the data folder.

For silent mode in PowerShell:

```powershell
$setupPath = (Resolve-Path .\DefJamSetup-0.5.1-windows-x64.exe).Path
$setupArguments = '--silent --dump "D:\Dumps\DefJam.iso" --install-dir "D:\Games\DefJam\App" --data-dir "D:\Games\DefJam\Data"'
$setupResult = Start-Process -FilePath $setupPath -ArgumentList $setupArguments -Wait -PassThru
$setupResult.ExitCode
```

Exit 0 means success. Add `--no-desktop-shortcut` for Start-menu only,
`--no-shortcuts` for neither shortcut, or `--log "D:\Logs\DefJam-setup.log"`
for a custom log. See [Windows setup](setup-installer.md) for prerequisites,
exit codes, removal, unsigned-build treatment and remaining acceptance limits.

## Manual build prerequisites

- Git for Windows.
- Python 3.12 or newer as `python` on PATH. Local development used 3.13; CI uses 3.12.
- Visual Studio Build Tools with **Desktop development with C++**, an x64 MSVC
  toolset, a Windows SDK and the CMake/Ninja tools. VS 2019 built the tested game;
  hosted CI uses the Windows runner's toolchain.
- PowerShell 5.1 or newer, a Direct3D 11 GPU, and a gamepad or a keyboard. The first configure downloads
  SDL3 (see [Controllers, keyboard and mouse](10-input.md)), so it needs network access once.
- Your own **USA Xbox Def Jam: Fight for NY** dump.
- [extract-xiso](https://github.com/XboxDev/extract-xiso/releases) for ISO extraction;
  7-Zip is optional for an image inside an archive.

Supported `default.xbe` SHA-256:
`31cc0d11f7c656b1a6f337fdf7d9bbaac9df11c324a2fb881f7eb639d4be00c2`.
The [manifest](../config/dump-manifest.json) records the other checks.

## Clone and select data storage

```powershell
git clone --recursive https://github.com/vyanhursky/DefJamFFNY-recomp.git
cd DefJamFFNY-recomp
python -m pip install pyxbe capstone pytest
$env:DEFJAM_DATA = 'C:\Games\DJFFNY-data'
```

Use a folder outside the repository. Set `DEFJAM_DATA` in each new shell before
running the pipeline or game. Without it, historical scripts use the development
machine's default path; avoid that default on another machine.

If you cloned without `--recursive`, run `git submodule update --init --recursive`.
Do not update the toolkit to another branch or replay `patches/xboxrecomp`.

## Extract and verify

Place `extract-xiso.exe` at
`$env:DEFJAM_DATA\tools\extract-xiso\artifacts\extract-xiso.exe`, then run:

```powershell
.\scripts\extract-disc.ps1 -Image 'D:\MyDumps\DefJam.xiso.iso' -DataDir $env:DEFJAM_DATA
python .\scripts\verify-dump.py "$env:DEFJAM_DATA\extracted"
if ($LASTEXITCODE) { throw 'Dump verification failed' }
```

If already extracted, put the files under `$env:DEFJAM_DATA\extracted` and run
the verifier. Other regions/revisions and other console versions are unsupported.

Create the `game` junction once:

```powershell
$discFolder = Join-Path $env:DEFJAM_DATA 'extracted'
New-Item -ItemType Junction -Path .\game -Target $discFolder
```

If `game` exists, inspect its target first; do not remove a real data directory.

## Analyze, recompile and build

Run each step only after the previous step succeeds:

```powershell
.\scripts\analyze.ps1
.\scripts\recomp.ps1
.\scripts\build.ps1 -Preset win-x64-release
```

Build scripts load the Visual Studio toolchain automatically. The first complete
pipeline may take tens of minutes. The XBE working copy and generated C remain
ignored local files. Do not share them or the resulting executable.

Output: `build/win-x64-release/defjam_recomp.exe`. Release uses optimized
`RelWithDebInfo`, retaining symbols for diagnostics. Input fingerprints reject
stale analysis, generated code and builds; follow the [workflow guide](03-workflows.md)
after source or toolkit changes.

## Launch and saves

From the repository root, with the same `DEFJAM_DATA` selected:

```powershell
.\scripts\run.ps1 -Preset win-x64-release
```

Or start `build/win-x64-release/defjam_recomp.exe` directly (double-click it or
make a shortcut): it finds the `game` folder in the repository root, supplies the
runtime switches itself and writes its log to `logs/`. `run.ps1` additionally
checks that the build is current and prints the log path when the game closes.

Play with a gamepad (Xbox, DualSense, Switch Pro and most others), the keyboard and mouse, or both:
the keyboard is its own player. [Controllers, keyboard and mouse](10-input.md) has the default keys and
the `[input]`, `[gamepad]` and `[keyboard]` settings for remapping, deadzones and rumble.

## Display settings

Every setting, including these, is listed in [settings.ini and keybinds](settings-reference.md).

The first thing you see is the launcher: the settings and a **Play** button (see
[Launcher and overlay](launcher-and-overlay.md); tick *Skip the launcher* to go straight to the game). In the game, **F1**
opens the same settings as an overlay.

Press **Alt+Enter** or **F11** to switch between a window and borderless full
screen. The window can be resized freely; the picture keeps the console's 4:3
shape with black bars.

Settings live in `settings.ini` in your data folder (`DEFJAM_DATA`, beside
`extracted` and `save`). The game writes the file with comments on first start;
edit it while the game is closed.

| `[display]` key | Default | Meaning |
|---|---|---|
| `fullscreen` | `false` | Start in borderless full screen. The hotkeys update it. |
| `window_width`, `window_height` | `1280`, `960` | Window size. Resizing the window updates them. |
| `aspect` | `4:3` | `4:3` keeps the picture shape; `stretch` fills the window. |
| `render_scale` | `2` | Internal resolution, 1-4 times 640x480. Try `3` or `4` on a 1440p or 4K display. |
| `filter` | `smooth` | `smooth` or `sharp` scaling to the window. |
| `vsync` | `true` | Pace frames on the display for even motion. Used on 60, 120, 180 and 240 Hz displays; others use the game's own 60 fps timer. |
| `gamma` | `true` | The game's own brightness curve, as on the console. |

`render_scale` and `gamma` take effect at the next start. A `RECOMP_*`
environment variable, where one exists, overrides the file for that run.
`RECOMP_SETTINGS=<path>` selects another file and `RECOMP_SETTINGS=none` ignores
the file, which is what the automated tests do.

Profiles and caches live under `DEFJAM_DATA/save`. Manual play writes normally,
without rollback. Back up this whole directory before trying another build.
Legacy project profile folders are accepted only after an exact metadata/name
match, without renaming or merging. Automated harness runs separately restore
the complete save root after testing.

## Troubleshooting

- Read the terminal error and `logs/run-*.log.err` if a process started.
- Missing `game/default.xbe`: check the junction and extraction.
- Freshness error: rerun the named analysis/lift/build step.
- Missing compiler: check the C++ workload and bundled CMake/Ninja tools.
- "The game files were not found": the executable looks for `game/default.xbe` in the
  current folder, beside itself and up to four folders above; check the junction.
- Saves missing after a direct launch: the data folder is found from `DEFJAM_DATA`, or
  from the `game` junction when it points at a folder named `extracted`. The first
  lines of `logs/run-*.log.err` say which folder and settings file were used.

Report your version/commit and reproduction steps. Review logs for personal
paths; do not upload game data, saves, generated source or memory dumps.
