# Known issues and validation limits

Playable Windows x64 was accepted by the maintainer on October 4, 2026. Test
routes cover menus, setup, a fight, Story intro/creator, crib/gym and Unlock
Fighters selection. They do not cover every mode or a full Story campaign.

## Remaining investigations

- **v0.11.0: fighter HUD moves inward while paused ([#95](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/95)).** In widescreen fights, opening the in-game pause menu can move the health/Blazin bars, names and scores to the centered 4:3 positions. The pause panel stays centered. This is an accepted release limitation, not a fixed issue.
- **v0.11.0: fire-escape transition gap.** A black rectangle appears briefly above the leading left scenery tile during the title-to-main-menu pan. It also reproduces without texture packs. Windows draw traces establish an uncovered scene region; the underlying geometry/transform cause remains unresolved. Retained as a known limitation; see the [Windows report](research/windows-integrated-menu-mods-rc-2026-10-23.md).
- **HD Widescreen Menus coverage.** Select User ID, Choose Teams and Choose Venue still use their original backgrounds and side-fill behavior. Their widescreen artwork is deferred to modpack 1.1. The menu pack is a default-channel download (`1.0.0`, using the accepted RC2 archive), separate from generated Recomp HD textures. The owner accepted Steam Deck playtesting; final macOS validation remains unconfirmed.

- **Audio crackle (fixed in v0.9.3, [#30](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/30)).**
  Cutscenes, menus and matches crackled because the game's audio thread refilled its 50 ms
  mix buffer late and old audio played again. v0.9.3 fixes the causes in the port (a game
  clock that stood still for 12-18 ms at a time, late wake-ups and thread priority on macOS).
  On the Mac the opening Story scene went from about 10 holes a second to 5 in four minutes,
  confirmed by ear. One short skip remained as the car crash hits in the opening scene, the
  heaviest frame of the scene. v0.10.2's shader cache and macOS scheduling fix took that
  scene's late frames on the Mac from 2.8% to 0.1-0.2%, and the skip did not show in the
  measured runs since; it has not been re-checked by ear. On the Steam Deck the owner's
  play-test of v0.9.3 found the audio significantly improved. Windows has not been play-tested
  with the fix; the clock fix is shared code. Each part can be switched off to compare
  (`docs/03-workflows.md`).
- **Story scene performance (v0.10.2, [Mac](research/scene-perf-macos-2026-10-18.md),
  [Deck](research/scene-perf-deck-2026-10-18.md)).** On the Steam Deck, render scale 3 doubles
  the late frames of the heaviest scenes; at the default scale 2 the opening scene still drops
  about 11% of frames in its heaviest shots. Windows compiles each shader at first use in every
  launch (no Direct3D 11 shader cache yet).
- **Scene loads freeze the picture for up to 2 s (to do).** When a Story scene loads, one frame
  lasts 0.8-1.8 s on the Mac in eleven scenes (sc07a 1.8 s; sc00 and sc03 1.4 s; sc06, sc16, sc09,
  sc13, sc05, sc17, gf1a, sc10) and up to 2.1 s on the Deck; the rest load in 0.2-0.75 s. The picture
  stops for that long. It is not the disk (sc00 reads 10.6 MB in 8 KB reads, none slower
  than 6 ms) and not shader compiles (the v0.10.2 cache did not shorten these loads). A profile of
  sc00's load shows the game thread held in Direct3D's fence hold (`sub_0021EB90_enter` in
  `src/recomp_manual.c`), waiting for the push-buffer executor, and the executor sleeping in vsync
  pacing (`flip_pace`, `nv2a_pb_exec.c`) and, at times, stalled on a trapped diagnostic read of
  `PCRTC_START` that it makes on every loop (`xbox_memory_layout.c`, the NV2A trace that
  `RECOMP_PB_EXEC` turns on). Details: F2 in the [Mac report](research/scene-perf-macos-2026-10-18.md).
  **How to fix**, each behind a switch so it can be compared, cheapest first:
  1. read `PCRTC_START` only when `RECOMP_NV2A_TRACE` asks for the trace;
  2. do not pace the executor to vsync while no new picture is being presented (inside a load);
  3. replace the fence hold's `SwitchToThread` loop with a wait on an event the executor signals
     when it completes a fence (F5 in the Mac report);
  4. if a long load remains, keep presenting the last picture or a loading indicator meanwhile.

  Measure with `python3 scripts/scene-perf.py run sc00 sc03 sc06 sc07a sc16 sc09 --no-audio --every 0`,
  two interleaved passes per arm, and compare *load ms*; then `python scripts/regress.py --quick`.
- **Picture options (v0.10.3).** Brightness and the vignette options were measured on macOS in the
  default One on One venue and play-tested by the owner on the Steam Deck; Windows has not run
  them, and other venues and Story scenes were not toured one by one. Setting them back to
  100 / on / 100 restores the original picture.
- **Unresolved jump targets.** The translated code has about a hundred placeholders for
  jump targets the translator could not resolve; reaching one skips real code. v0.2.1
  replaced the seven that are simple function exits, one of which caused the Terrordome
  crash. Roughly seventy more decode as real code and need the translator's function
  extents fixed. `python scripts/stub-targets.py` lists them. None is known to be reached.
- **A rare crash in the sound library (guarded since v0.2.3).** One of three Debug runs of the
  scripted four-fighter Terrordome match crashed in the game's DirectSound code
  (`sub_002626B5`, on the audio interrupt's thread). The interrupt routine looks up the client
  of a hardware voice the audio chip reported idle and does not expect to find none; the chip
  model services that trap about a millisecond late and lets a later event overwrite the voice
  number, so the entry can be empty. The lookup (`sub_0025FB7C`) is now hand-written with the
  missing test and logs `[DSOUND] idle trap ... with no client` when it fires. It has not
  fired in the six Debug and Release runs since, so the guard is unproven against the real
  event; the model's trap handling is the underlying fault and is unchanged
  (`docs/research/dsound-voice-list-crash.md`).
- **A dropped audio buffer at a knockout.** Both scripted fights that were played to a
  knockout dropped exactly one audio buffer in the slow motion after it, and none before.
  Not investigated; it has not been noticed by ear.
- Input (v0.3.0): scripted matches cover the keyboard as player 1, as player 2 beside a scripted pad,
  and two pads (`versus`); SDL's virtual gamepads cover slot assignment, hot-plug, remapping, focus and
  rumble in a unit test. Real DualSense and Xbox pads, three or four pads, rumble feel, and the mouse
  buttons and wheel have been checked only by the owner's play-test (see the v0.3.0 notes), not by
  script. Pointer-driven menus are not offered. A pad plugged in after the game starts is added as a new controller
  (checked in the game with a virtual pad plugged in twenty seconds after launch); the keyboard counts as one
  controller. The game's own rumble pulses are brief and faint, so strength and length are raised by
  default (`rumble_floor`, `rumble_min_ms`); how that feels on a real pad is the owner's call.
- Long-session memory use over multiple fights remains to be measured.
- Earlier builds had a rare silent boot and a rare crash at guest `sub_001A3310`.
  The clean rebase soak does not prove those are eliminated.
- Previously recorded visual issues include a loading-bar glitch, black profile
  thumbnails, Blazin' film grain and half-pixel alignment. None was observed in
  the owner's accepted play-test; wider coverage remains backlog.
- Optional CPU vertex-program fallback performance is unmeasured after rebase.
- Audio counters do not prove fidelity of every track/effect; owner listening
  accepted the experience.

## Mac setup: "python3" Not Opened (fixed in v0.10.4)

A v0.10.3 or earlier Mac setup downloaded with a browser stops at "Preparing setup files" with
*"python3" Not Opened*, even after **Open Anyway** for Def Jam Setup. Setup unpacked its payload
with the download's quarantine still attached, so Gatekeeper refused the bundled Python, which is
signed ad hoc, not notarized
([#59](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/59)). The v0.10.4 setup unpacks
without the quarantine; use it. Workaround for an older setup: choose **Done**, drag Def Jam
Setup to Applications, run
`xattr -dr com.apple.quarantine "/Applications/Def Jam Setup.app"` in Terminal, then open it
from Applications.

## Windows setup: LNK1000 "Internal error during IncrCalcPtrs" (fixed in v0.10.7)

On some Windows PCs the setup's build stops at the last link step with
`defjam_recomp.rc.res : fatal error LNK1000: Internal error during IncrCalcPtrs`, an access
violation (C0000005) inside `link.exe`, and setup ends with "Stage failed". The build used
RelWithDebInfo, which CMake links with `/INCREMENTAL`, and the Microsoft linker's incremental
code can crash on an image as large as this one. The `.rc.res` file named in the message is only
the last input on the link line. The link now passes `/INCREMENTAL:NO` (nothing to reuse in a
from-scratch setup build). Workaround for an earlier setup: in an "x64 Native Tools Command
Prompt for VS", in the setup's source folder, delete `build\win-x64-release` and run
`cmake -S . -B build/win-x64-release -DCMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO="/debug /INCREMENTAL:NO"`,
then run setup again. The crash did not reproduce on a Windows 11 PC with VS 2019 Build Tools
(MSVC 14.29), where v0.10.6 and v0.10.7 both built, so the change rests on the cause analysis. The
reporter has not yet confirmed it.

## Windows: the game closes right after setup builds it, before the menu (fixed in v0.10.7, issue #72)

On some Windows PCs the game stopped at start-up with an access violation in `VCRUNTIME140.dll`,
before any window content. The start-up memory placement accepted a location for the 64 MB of
Xbox RAM without checking that the device memory areas it needs could be placed at the same
offset. When Windows refused one, audio start-up read memory that had never been mapped. The
code dates from v0.1.0 and depends on how Windows lays out the PC's address space. v0.10.7
checks the whole layout before booting, retries another location if any area is unavailable,
and stops with a message instead of continuing. Tested with the placement fixture on Windows;
the reporter has not yet confirmed it on their PC. If it still happens, attach the newest
`logs\run-*.log.err` (the `[CRASH]` block) to the issue.

## Mac diagnostics retained in v0.9.0 validation

At v0.9.0 the strict audio underrun check (`audio.dry_queue`) failed on the Mac, as in the
accepted pre-widescreen build, with gameplay and PCM checks passing. Since v0.9.3 the output
device runs dry far less: none in a four-minute, four-fighter Terrordome fight (10 before), the
`versus` check passes, and `combat` had one gap in the release regression. It is tracked in
[#30](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/30).
A historical stream callback target `0x001E3C14` appeared during an HD capture
and one widescreen-off crib boot. The same symptom was recorded before
widescreen; focused 16:10 and crib repeats passed, but the cause is unresolved.
Raw failures remain in the [Mac validation report](widescreen/09-macos-validation.md).
No callback suppression or weaker test threshold was added.

## Launcher and overlay (v0.4.0)

The launcher and overlay are checked by clicking through them in the real game and by scripted runs
(a virtual pad opens and closes the overlay during a fight); the regression harness never shows the
launcher. The overlay does not pause the game: it keeps running while the menu is open, with every
input held at rest. `render_scale` and the brightness curve still take effect at the next start (marked
in the screens). Not covered: very small windows (the overlay's panel fills the window), displays below
1280x720, and a pad that SDL does not know as a gamepad (it cannot navigate the menus).

On macOS (v0.6.2) the launcher and overlay were checked by scripted runs (a rendered frame read back, clicks and
keys played through the real event path, a virtual pad), not by a person at the keyboard; see
[Launcher and overlay](launcher-and-overlay.md#looking-at-it-without-a-screen). Not covered: a trackpad or
an external mouse's smooth scrolling in the overlay, text entry with an input method (the text boxes take typed
characters only), and Intel Macs. On Linux the launcher and overlay have shipped since v0.7.0 and were checked on a
Steam Deck by the same scripted `launcher` and `overlay` regression checks (2 of 2, see the v0.7.0 notes); a desktop
Linux distribution has not been tried.

## Future features

MSVC is the supported compiler. Clang is not a validated build path: expanded CI
exposed a 32-bit NEG flag test failure under Clang optimization and a switch
fixture compilation issue. The Release gate selects MSVC for the shared CPU
fixture; it does not establish Clang or native Linux correctness. Native Linux is built with Clang and checked by the
Deck regression runs described below.

A settings file, borderless full screen, a resizable window and render-scale choice
arrived in v0.2.0. Exclusive full screen is not offered; the flip-model borderless window replaces it. Vsync
paces the game only on displays whose refresh rate is a multiple of 60. Changing `render_scale` needs
a restart. The launcher and in-game settings are available on Windows and, since v0.6.2, macOS and Linux, and v0.6.0 added optional
HD texture packs, on macOS too since v0.6.1 and on Linux and the Steam Deck since v0.7.2 ([guide](hd-textures.md)). Widescreen is available as an opt-in beta since v0.9.0 ([guide](widescreen/README.md));
music replacement and frame rates above 60 are not planned for M6. macOS on Apple Silicon is
playable since v0.5.0, with the launcher and overlay since v0.6.2
([guide](build-macos-linux.md)). Native Linux, including the Steam Deck, builds with Clang
and passes most of the automated regression; Proton is untried. On the Steam Deck there was a
little audio clipping or distortion (the old #11). It was the same crackle as on the other
platforms, and the owner's play-test of v0.9.3 on the Deck found the audio significantly
improved; tracked in [#30](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/30). A game started from inside
a build container (podman or distrobox) has no sound, because the container has no PipeWire library;
start it from the host. On macOS and Linux, vsync is only used on a display refreshing at a
multiple of 60 Hz (60, 120, 180, 240), as on Windows: on a 75, 90, 100 or 144 Hz display the
game paces itself and the picture can tear. On Windows such a display does not tear, but a 60 fps frame stays up for an uneven number of refreshes (two, then three, on 144 Hz), so fights can look choppy while the frame counter says 60 ([#67](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/67)); setting the display to 120 or 60 Hz avoids it. The game log's `[DISPLAY]` line records the refresh rate, the window mode and whether vsync paces the game. Network play is not supported.

See the [improvement backlog](04-improvement-backlog.md) and
[rebase acceptance](research/toolkit-rebase-acceptance.md) for details.
