# Windows setup installer

Implementation in progress. Setup contains tools and source and compiles your own
USA Xbox dump locally. Windows x64 is first; macOS/Linux setup are to-dos that
reuse the merged port's Python pipeline and Vulkan/SDL3 backend.

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

## Silent mode

```powershell
DefJamSetup.exe --silent --dump "D:\Dumps\DefJam.iso" --install-dir "D:\Games\DefJamApp" --data-dir "D:\Games\DefJamData" --log "D:\SetupLogs\DefJam.log"
```

Add `--install-prerequisites` to allow compiler installation explicitly. Silent
mode suppresses the wizard, not Windows elevation or Microsoft's consent UI.
For unattended operation, pre-provision the compiler, SDK, CMake and Ninja.
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
Python, pyxbe, capstone, SDL3 and ImGui inputs have pinned checksums and notices.

CI checks synthetic failures/recovery, embedded Python imports, the setup build
and a packaged silent invalid-input run. Tagged releases attach exactly the setup,
checksum and provenance to a draft. Source hygiene still bans tracked binaries.

The initial setup release is unsigned; Windows may display a SmartScreen prompt.
Clean Windows 11 VM setup with no
tools, VS 2022, independent ISO/XISO input, Unicode paths and physical-machine
gameplay acceptance remain to be validated; CI is not a substitute.
