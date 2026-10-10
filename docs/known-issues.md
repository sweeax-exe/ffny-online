# Known issues and validation limits

Playable Windows x64 was accepted by the maintainer on October 4, 2026. Test
routes cover menus, setup, a fight, Story intro/creator, crib/gym and Unlock
Fighters selection. They do not cover every mode or a full Story campaign.

## Remaining investigations

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
characters only), and Intel Macs. The overlay's Linux build compiles from the same sources but has not been run.

## Future features

MSVC is the supported compiler. Clang is not a validated build path: expanded CI
exposed a 32-bit NEG flag test failure under Clang optimization and a switch
fixture compilation issue. The Release gate selects MSVC for the shared CPU
fixture; it does not establish Clang or native Linux correctness.

A settings file, borderless full screen, a resizable window and render-scale choice
arrived in v0.2.0. Exclusive full screen is not offered; the flip-model borderless window replaces it. Vsync
paces the game only on displays whose refresh rate is a multiple of 60. Changing `render_scale` needs
a restart. The launcher and in-game settings are available on Windows and, since v0.6.2, macOS, and v0.6.0 added optional
HD texture packs, on macOS too since v0.6.1 ([guide](hd-textures.md)). True 16:9 remains planned ([plan](07-m6-plan.md));
music replacement and frame rates above 60 are not planned for M6. macOS on Apple Silicon is
playable since v0.5.0, with the launcher and overlay since v0.6.2
([guide](build-macos-linux.md)). Proton, Steam Deck and a native Linux game build are
unvalidated. Network play is not supported.

See the [improvement backlog](04-improvement-backlog.md) and
[rebase acceptance](research/toolkit-rebase-acceptance.md) for details.
