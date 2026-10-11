Def Jam Recompiled Setup for Linux (x86-64, including the Steam Deck)
=====================================================================

This setup builds Def Jam: Fight for NY for this computer from your own copy of
the USA Xbox game. It contains no game files and downloads none; you need your
own disc image (ISO/XISO) or extracted game folder.

Start it
--------
Open DefJamSetup.sh (on the Steam Deck, in Desktop Mode: double-click it in
Dolphin and choose Execute). Choose your game copy, an install location and a
data location, then Install / Repair. The first install takes a while.

What it needs
-------------
- A Vulkan-capable GPU, SDL3 3.2 or newer, the Vulkan loader and shaderc.
  SteamOS 3.8 or newer has all of them.
- About 12 GB free plus room for a copy of the game files.
- A compiler, or podman to fetch a build container (about 1 GB, once).
  SteamOS has podman and no compiler, so on the Deck the container is used and
  needs an internet connection the first time.

Afterwards
----------
Start "Def Jam Recompiled" from the application menu, the desktop shortcut or,
if you asked for it, Steam (Gaming Mode works). Saves and settings live in the
data location; settings.ini there is described in the project's settings
reference. There is no launcher window or in-game overlay on Linux yet.

Play logs are kept in ~/.local/state/DefJamRecompiled/logs and setup logs in
~/.local/state/DefJamSetup/logs. Review them for personal paths before sharing.

Silent install and removal
--------------------------
./DefJamSetup.sh --silent --dump ~/Dumps/DefJam.iso \
    --install-dir ~/Games/DefJamRecompiled/App --data-dir ~/Games/DefJamRecompiled/Data
    [--steam-shortcut] [--no-desktop-shortcut | --no-shortcuts] [--toolchain host|container]
./DefJamSetup.sh --uninstall --install-dir ~/Games/DefJamRecompiled/App

Exit codes: 0 success; 2 invalid input; 3 prerequisites missing; 4 a stage
failed; 5 another setup or the game is running; 6 cancelled.
Uninstalling keeps your dump copy, saves, settings and logs. The build
container stays in podman's store; remove it with
"podman rmi $(podman images -q localhost/defjam-recompiled-build)".
