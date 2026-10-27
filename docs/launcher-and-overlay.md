# Launcher and overlay

Two ways to change settings without editing `settings.ini`. Both show the same screen, both save as you
change things, and both are usable with a mouse, the keyboard or a gamepad. They are on Windows and, since
v0.6.2, macOS and Linux; the screen is the same, and the few differences are marked **macOS** below
(Linux, the Steam Deck included, behaves as macOS does, with `defjam_recomp --launcher` from a terminal).

## The launcher

A window that appears **every time you start the game**, before the game itself loads. It has the settings
and two buttons: **Play** starts the game, **Quit** closes it. Changes that are read only at start-up (render
scale, who plays, how many controllers, the pad backend) take effect for this very launch, because the game
has not started yet.

- **Skip it:** tick *Skip the launcher at start* at the bottom of the launcher, or in the overlay's
  General page (`[launcher] skip` in `settings.ini`).
- **Bring it back when skipped:** hold **Shift** while the game starts, or start `defjam_recomp.exe` with
  `--launcher`. `--no-launcher` skips it for one launch.
  **macOS:** hold Shift while you open `Def Jam Recompiled.app`, or from Terminal
  `open "Def Jam Recompiled.app" --args --launcher` (`--no-launcher` the same way). Those two
  options are the only ones the app hands on to the game.
- Test and scripted runs (the regression harness, `RECOMP_SETTINGS=none`) never show it, and neither does
  a run with no display (`RECOMP_HEADLESS=1`).
- **macOS:** the launcher is a window of its own, drawn through Metal, that goes away before the game's
  window opens. Closing it (the red button, Cmd+Q) quits without starting the game, as **Quit** does.

## The overlay

Press **F1** in the game (or the pad's guide button, or hold both stick clicks for half a second) to open it
over the picture; the same keys close it, and so do Esc and the pad's B button. While it is open the game is
polled at rest, as if every pad and the keyboard were untouched, but it **keeps running**: it does not pause.
Close it before the next hit. **macOS:** on a Mac keyboard F1 is often the brightness key; hold **fn**, or
choose another key in the General page (the key, a pad button or both stick clicks open it too).
The pointer shows while the overlay is open, also in full screen.

Full screen, window size, picture shape, filter and vsync apply as you change them. Render scale and the
brightness curve are marked *(restart)* and take effect the next time the game starts; a banner at the bottom
lists the changes that are waiting for a restart. Keys: `[ui] overlay_key` and `overlay_pad` in
`settings.ini`, or the General page.

## The pages

| Page | What it has |
|---|---|
| Display | Full screen, window size, picture shape, scaling filter, vsync, render scale, the brightness curve |
| Controllers | Which controller is in which player, with a live view of each pad's sticks and buttons as the game will see them (after dead zones and mapping) and a rumble test; who plays (keyboard player, controller count, backend); dead zones and thresholds; rumble; the pointer |
| Pad buttons | Which physical button acts as each Xbox control. Choose from a list, or press **Detect** and then the button on your pad |
| Keyboard & mouse | The keys and mouse inputs for each Xbox control. Press **Add**, then a key, a side or middle mouse button or a wheel notch (Esc cancels); the left and right mouse buttons have buttons of their own while you are adding. **Clear** and **Default** per control |
| General | Skip the launcher, the overlay's key and pad buttons, text size, a button that shows `settings.ini` in Explorer (Finder on macOS), reset every setting to default, and the version |

## Using a gamepad in the menus

The d-pad or left stick moves, **A** (the bottom face button) activates, **B** goes back or closes, the shoulder
buttons switch pages, and the triggers change a slider faster. This works with any pad the game reads, with
the first pad to press a button, and independently of the game's own button mapping.

## The online screens' look

The online screens (`src/hooks/online_screen.cpp`, opened from ONLINE in the game's main menu) wear the
launcher's look: its background picture, blurred (`ffny-branding/launcher-bg-blur.jpg`), behind every page,
and the launcher's glass for every block of content (a dark translucent fill over the picture, a light line
along the top, a faint border, corners that scale with the menu). Each page frames and lights the picture
its own way: the hill red rising from the lower right, the room crimson, watching a cold broadcast light,
the stats gold, the players and voice chat violet, and so on; the framing eases from page to page and drifts
very slowly. A vignette, a little film grain and a few of the launcher's embers finish it. The light, the
vignette and the grain are small textures made once; nothing is blurred while the game runs.

The pages that used to sit beside the game's own door (the hub, the room list, the code, a new room's
settings, voice chat, connecting) now cover the whole window as well, and the door's text sits on a glass
panel where the door was. Keeping the game's door was tried: its lit, yellow-green wall beside the warm
launcher picture looked like two screens at once. The hub shows its own A SELECT / B BACK, since the game's
are covered.

Without `ffny-branding` (a source tree without it, a damaged installation) the pages keep the earlier plain
near-black background, with the game's door beside the hub, and the panels are the same dark glass over it.
The game finds the folder as the launcher does: in its working directory or up to four folders above the
executable.

### The versus screen

The original game shows the fighters and the venue before a fight. Online, the room's START goes from the
room straight into the load (`src/hooks/automatch.c` drives the game's own selection screens), so the port
draws its own versus screen while an online match loads: each fighter's portrait (the online art), the
player's name (`CPU` for a CPU fill), the fighter and the fighting style, each player's ping, `VS` in the
game's stencil over a soft red light, the venue's photo and name banner (the vote's art), the mode's logo, the
room and the server. In King of the Hill the king stands on the right with the crown and "N WINS IN A ROW".
One on One has two large cards with the venue between them; 3-Way and Free for All a row of cards with the
venue below. The cards slide in, `VS` and the venue fade in; nothing flashes.

It shows from the START (it replaces the old MATCH FOUND card) until the fight's first steps have run on
every PC: the step after the input delay, which waits for the slowest game to finish loading (at least
2.5 s after the START when a load is instant), then fades out in 0.4 s. For the first 1.8 s (or until this
PC's fight is set up) it is the online screens' page, as MATCH FOUND was; then the online screens close, the
controller belongs to the game again, and the overlay keeps drawing the same page without taking any input
over the game's loading screen. It is drawing only: `netplay_versus` copies what the START set up and
nothing about the match, its inputs or its timing changes. Never offline, in Story, after a rematch from the
results (no new START) or for a King of the Hill watcher; a King of the Hill fight that waits for the king's
results screen shows it once the king is back in the menus.

The music under it is the game's own. Game.StartGame stops the menu music, and the game itself streams
the loading part of the fight's track (`music\<track>\loading0.mus`) while the venue loads, then the
fight's music; the versus screen does not touch the audio. Starting the venue's music any other way would
go through the music director, which draws from the match's shared generator, or would need a decoder of
the game's music streams of our own; neither is needed while the game already plays the track.

## Where it comes from

The screen is [Dear ImGui](https://github.com/ocornut/imgui) (MIT; release 1.92.9b, fetched at build time at
a pinned hash, licence in `thirdparty/imgui-LICENSE.txt`). The overlay is drawn in the presentation step after
the game's picture has been scaled into the window, so it is in window pixels and never in a test capture.
Source: `src/hooks/pc_ui.cpp` (the screen and the overlay), `src/hooks/pc_launcher.cpp` (the Windows launcher
window), `src/hooks/pc_launcher_sdl.cpp` (the macOS one), `src/hooks/pc_ui_settings.c` (the settings and the rule
for when the launcher shows).

| | Windows | macOS (and the Linux build) |
|---|---|---|
| Launcher window | Win32 window, Direct3D 11 | SDL3 window and renderer (Metal), on the process's first thread |
| Overlay | Direct3D 11, in the present step | Vulkan (MoltenVK): a render pass the toolkit's Vulkan present step opens on the window's image (`d3d8_vk_set_overlay`) |
| Keyboard and mouse | the window's messages | SDL events, fed in by `src/host_posix.c` |

## Looking at it without a screen

Test hooks, all off unless set. They exist because the picture cannot be looked at on a machine with
nobody at it (a locked screen, a remote run):

| Variable | What it does |
|---|---|
| `RECOMP_WINDOW_SHOT=<file.bmp>` | Writes what the launcher (first frame) or the game window (the overlay, after `RECOMP_WINDOW_SHOT_FRAME` overlay frames, 20 by default) shows, as a 32-bit BMP. macOS and Linux |
| `RECOMP_LAUNCHER_SCRIPT` | The launcher's input: `<ms>:click:<x>:<y>`, `<ms>:move:<x>:<y>`, `<ms>:shot:<file>`, `<ms>:play`, `<ms>:quit`, separated by `;`, times from the window's first frame and positions in window points |
| `RECOMP_HOST_SCRIPT` | The game window's input: `<ms>:key:<SDL key name>` (`F1`, `Escape`), `<ms>:click:<x>:<y>`, `<ms>:move:<x>:<y>`, `<ms>:text:<chars>`, times from the window's opening |
| `RECOMP_OVERLAY_SHOT_DIR=<folder>`, `RECOMP_OVERLAY_SHOT_MS` | Windows: the finished picture with the overlay, every `RECOMP_OVERLAY_SHOT_MS` (2000 by default) while the overlay draws, as `<ms>.bmp` |
| `RECOMP_TEST_ONLINE_GALLERY=<ms>` | Windows, with `RECOMP_TEST_OPEN_ONLINE=1`: the online screens show every page in turn, `<ms>` each, with sample players, rooms, a hill and statistics, take no input and connect to nothing; the overlay captures are then named `<ms>-<page>.bmp`. For comparing a design change page by page from one copy. `RECOMP_TEST_ONLINE_GALLERY_ONLY=<prefix>` shows only the pages whose names start with it (`versus`: the versus screen for One on One, 3-Way, Free for All and King of the Hill, the entrance replaying on each page) |
| `RECOMP_MODS_PAGE=1` | The Textures page becomes upstream's Mods page (installed packs, load order, the signed project catalogue through the mod helper). Upstream's mod support is beta and not wired into FFNY Online's launcher and updater yet, so it is off by default |
| `RECOMP_TEST_UPDATE_PROGRESS=<step>[,popup]` | Windows: a made-up update in progress at step 1 (the download) to 8 (finishing up), with no network and no setup, for pictures of the GAME card; `,popup` opens the "Update available" window first, which then shows the same progress |
| `RECOMP_INPUT_VIRTUAL_PADS`, `RECOMP_INPUT_VIRTUAL_CHORD_MS` | An SDL virtual gamepad, which also opens and closes the overlay with its stick clicks and B ([input guide](10-input.md)) |

For example, `RECOMP_HOST_SCRIPT="14000:key:F1;15000:click:659:126" RECOMP_WINDOW_SHOT=/tmp/overlay.bmp
RECOMP_WINDOW_SHOT_FRAME=90` opens the overlay fourteen seconds in, clicks its General tab and writes the
picture; add `--no-launcher` or turn the launcher off first.
