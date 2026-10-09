# Setup installers

Setup contains tools and source and compiles your own USA Xbox dump locally. It
exists for Windows x64 and, since v0.6.1, macOS on Apple Silicon (see
[macOS setup](#macos-setup-apple-silicon) below). The Windows sections come first;
Linux setup is still a to-do and reuses the same engine.

## Wizard and destinations

Choose an ISO/XISO or extracted folder, then choose **Install location** and
**Data location** independently. Defaults are under your LocalAppData folder.
Install, data and original dump locations must be separate and non-nested.
Choose an empty install folder or an existing recognized Def Jam installation.
Setup copies the dump into the data location and leaves the original untouched.
Saves/settings stay in the data location through updates and uninstall.

Folders must be writable and at most 140 characters because the game/toolchain
still have path-length limits. Allow 12 GiB build headroom plus extracted dump
storage and compiler disk space. Exact clean-machine peak usage is still to be measured.

**Install missing Microsoft Build Tools** allows Microsoft's publisher-verified
installer to request consent/UAC. Without that option, missing tools stop setup.
Setup runs as your user. Python, dependencies and source/toolkit are bundled;
no Git or system Python installation is needed. Missing compiler installation
requires internet; SDL3/ImGui source is packaged for offline configuration.

Install / Repair runs verification, extraction, analysis, lift and compile.
Status shows the stage; **Open logs** reveals detailed output. **Cancel** stops
the process tree; rerun to resume verified completed work. Changed/corrupt dump
contents are refused rather than merged into an existing extraction.
**Create a desktop shortcut** is checked by default; clear it to create only a
Start-menu shortcut. Shortcuts launch through the stable native launcher.
After setup, select **Play**, use the shortcut, or double-click
`<install>\DefJamLauncher.exe`. The wizard displays the exact launcher path in a
selectable field. It supplies the correct data location and working directory.
There is no automatic game launch.

## Optional HD textures (v0.6.0)

**Apply HD texture upscale during install (increases install time)** is unchecked
by default. It generates Lanczos4x from your verified dump locally after the game
build, verifies the PNGs, then enables the generated texture packs for first launch.
No GPU/AI model is needed. Expect about6.2GB of HD PNGs plus~0.65GB reusable cache;
Setup reserves15GiB extra working headroom, including a copy fallback. This adds
about four minutes on the developer's fast PC; other machines vary.

Status shows texture counts. Cancel/failure does not enable unfinished packs;
rerun resumes matching verified work. With the option unchecked, existing HD
settings survive updates. To return to originals, disable packs on the launcher's
Textures page and restart. Old recipe caches/pack versions remain in the data
folder; uninstallation preserves them with the rest of the player's data.

The packaged v0.6.0 installer was run end to end with this option from a real dump,
and the installed game loaded the generated packs; see
[release validation](research/hd-v060-release-validation.md) for what was and was
not exercised. See also [HD setup integration](research/hd-setup-integration.md).

## Silent mode

```powershell
DefJamSetup.exe --silent --dump "D:\Dumps\DefJam.iso" --install-dir "D:\Games\DefJamApp" --data-dir "D:\Games\DefJamData" --log "D:\SetupLogs\DefJam.log"
```

Add `--install-prerequisites` to allow compiler installation explicitly. Silent
mode suppresses the wizard, not Windows elevation or Microsoft's consent UI.
For unattended operation, pre-provision the compiler, SDK, CMake and Ninja.
Add `--hd-textures` to generate and enable HD textures locally.
Add `--no-shortcuts` to suppress desktop and Start-menu shortcuts.
Add `--no-desktop-shortcut` to create only the Start-menu shortcut.

Exit codes: 0 success; 2 invalid input; 3 prerequisites missing; 4 failed stage;
5 concurrent setup/game; 6 cancelled; 3010 restart required. Logs default to
`%LOCALAPPDATA%\DefJamSetup\logs\<session>.log` for the wizard/packaged setup,
including failures before destination validation. **Open logs** opens the current
file directly, and error dialogs show its full path. `--log` overrides the path;
review personal paths before sharing.
No dump, game data or generated code is uploaded.

## Update, repair and uninstall

Rerun the newer setup against the same install and data folders. Source/builds
live in versioned directories, and the active launch receipt changes only after
certification. Failed updates leave the previous game usable. Analysis/lift
outputs can be copied from the previous version, but every incoming pipeline
fingerprint is checked before reuse. Compiler objects are rebuilt. Close the
game before updating; launcher lifetime locks and direct-process checks guard this.

```powershell
DefJamSetup.exe --silent --uninstall --install-dir "D:\Games\DefJamApp"
```

Removal deletes recognized version directories, launcher/receipts and matching
shortcuts. Dump, extracted data, settings, saves, logs and Microsoft Build Tools
remain. No arbitrary destination folder is recursively removed.

## Build and publication

Use `scripts/build-setup.ps1` from a clean recursive Windows checkout, or add
`-Development` for local feature prototypes. Production validation rejects
development payloads. Setup is a standalone native-controls wizard, statically
linked to its C++ runtime and independent of generated code/game renderer.

The executable contains a SHA-256-checked ZIP resource. After unpacking in a unique
local folder, the engine checks its complete file inventory before running stages.
It uses the pinned toolkit XDVDFS reader with additional path/bounds/cycle checks.
Python, pyxbe, capstone, Pillow, NumPy, SDL3 and ImGui inputs have pinned checksums and notices.

CI checks synthetic failures/recovery, embedded Python imports, the setup build
and a packaged silent invalid-input run. Tagged releases attach exactly the setup,
checksum and provenance to a draft. Source hygiene still bans tracked binaries.

The initial setup release is unsigned; Windows may display a SmartScreen prompt.
Clean Windows 11 VM setup with no
tools, VS 2022, independent ISO/XISO input, Unicode paths and physical-machine
gameplay acceptance remain to be validated; CI is not a substitute.

## macOS setup (Apple Silicon)

`DefJamSetup-<version>-macos-arm64.dmg` holds `Def Jam Setup.app`, a short
"READ ME FIRST" and the third-party licenses. It needs an Apple Silicon Mac on macOS 11
or newer, the **Xcode Command Line Tools** and about 12 GiB free plus the dump copy. It
needs no internet connection and no Terminal, Homebrew, CMake or Python of your own.

**First open.** The setup is not signed with an Apple Developer ID, so macOS blocks it
the first time. Choose **Done** (not Move to Trash), then open System Settings →
Privacy & Security, scroll to Security, and choose **Open Anyway** beside the message
about Def Jam Setup, then confirm with your password or Touch ID. You do this once.

**Command Line Tools.** Setup uses Apple's compiler, which cannot be redistributed.
If it is missing, setup stops with exit 3 and a message linking to
[Apple's installation instructions](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools/);
install them, then run setup again. Intel Macs stop the same way.

The wizard has the same controls as the Windows one: your ISO/XISO or extracted dump,
separate **Install location** and **Data location** (defaults under
`~/Library/Application Support/DefJamRecompiled`), a desktop-shortcut choice,
**Install / Repair**, **Open logs**, **Play** and **Cancel**. Rules for the folders,
the dump copy, resuming after cancel and refusing a changed dump are the Windows ones.
There is no "install build tools" checkbox (see Command Line Tools above). The
**Apply HD texture upscale during install** option is the Windows one: unchecked by
default, about 6.2 GB of generated textures plus 15 GiB of working headroom, no GPU needed
([HD textures](hd-textures.md)).

Setup writes **Def Jam Recompiled.app** into the install folder, a copy in
`~/Applications` and, unless you clear the choice, an alias on the Desktop. Open any of
them or select **Play**. The app is made on your Mac, never downloaded, takes the play
lock (an update refuses to run while the game does), supplies the data folder and
working directory and keeps the ten newest play logs in `~/Library/Logs/DefJamRecompiled`
(`.log` and `.log.err`; attach the `.err` to a problem report after reviewing it for
personal paths). The launcher and in-game overlay are not on macOS yet; settings are in
`settings.ini` ([reference](settings-reference.md)).

Setup logs are in `~/Library/Logs/DefJamSetup`; **Open logs** opens the current one.

### Silent mode

```bash
"/Applications/Def Jam Setup.app/Contents/MacOS/DefJamSetup" --silent --dump "$HOME/Dumps/DefJam.iso" \
  --install-dir "$HOME/Games/DefJamApp" --data-dir "$HOME/Games/DefJamData" --log "$HOME/DefJamSetup.log"
```

`--hd-textures` generates and enables the HD packs. `--no-shortcuts` creates neither the `~/Applications` copy nor the Desktop alias;
`--no-desktop-shortcut` skips only the alias. `--silent --uninstall --install-dir PATH`
removes the app, the versions and the shortcuts it made, and keeps the dump copy, saves,
settings and logs. Exit codes: 0 success; 2 invalid input; 3 prerequisites missing (Command
Line Tools, or an Intel Mac); 4 failed stage; 5 another setup or the game is running;
6 cancelled.

### What the payload carries

Reviewed source, the pinned toolkit, Python 3.13 with `pyxbe`, `capstone`, Pillow and
NumPy, CMake and Ninja, and three libraries the game links: SDL3 and shaderc, built
from pinned source, and MoltenVK, linked directly with no Vulkan loader. Everything is
pinned by SHA-256 or git commit in `setup/dependencies-macos.json` (SDL3 by the
toolkit's pin), arm64 only, and ad-hoc signed. The build looks nowhere else:
`/opt/homebrew`, `/usr/local` and package-manager variables are ignored, so a Mac
without Homebrew builds exactly what a Mac with it does.

Build the setup with `scripts/build-setup.sh` (`--development` for a local prototype
from a dirty checkout). CI builds it on a macOS arm64 runner, checks that the wizard
opens and that a silent run with a missing dump exits 2, and a tagged release attaches
exactly the dmg, its SHA-256 and provenance.

### Validation

Checked on arm64 macOS 26 with `/opt/homebrew` and `/usr/local` made unreadable: a full
silent install from an XISO, the launcher starting the game with the receipt's data
folder, a repair while the game runs (exit 5), shortcut creation and removal, and the
installed build against the title, menu and fight checks. Not yet checked: a Mac with
nothing installed, older macOS versions and Gatekeeper's first-open prompt on a
downloaded copy.
