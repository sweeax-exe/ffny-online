# Controllers, keyboard and mouse

Since v0.3.0 the game reads gamepads through SDL3 and the keyboard and mouse as a player of
their own. Everything below is a setting in `settings.ini` (written beside your `save` folder
on first start). The launcher and in-game overlay edit the same values. [settings.ini and keybinds](settings-reference.md) lists them all.

## What works

| | |
|---|---|
| Gamepads | Xbox, DualSense, DualShock 4, Switch Pro and most DirectInput pads, through SDL3's gamepad layer. Bluetooth or wired. `backend = xinput` reads Xbox-style pads only, with no extra library. |
| Players | Up to four. Each pad takes the lowest free player in the order it appeared. A pad plugged in while the game runs, even at the player-select screen, becomes a new controller on the console's hub, which the game sees as an ordinary hot-plug; one unplugged leaves its player at rest. |
| Keyboard and mouse | A separate player by default, after the pads: with a pad as player 1 the keyboard is player 2, and with no pad it is player 1. Choose another player, merge it into player 1's pad, or turn it off. |
| Rumble | The game's own rumble reaches the pad of the player who is hit. The game's pulses are brief and faint, so `rumble_floor` and `rumble_min_ms` make them felt; `rumble` sets the strength and 0 turns it off. A pad buzzes for a moment when it connects (`rumble_on_connect`). |
| Remapping | Any physical pad control can act as any Xbox control; sticks can be swapped or switched off; stick deadzones and the trigger threshold are settings. |
| Focus | Nothing is read while the game window is in the background, and held keys are released when it loses focus. |

## Keys, mouse and settings

The default keys, every key name, and every `[input]`, `[gamepad]` and `[keyboard]` setting are in
[settings.ini and keybinds](settings-reference.md). In short: W A S D is the left stick, the arrow keys
are the right stick (the game uses it for Blazin' moves), I J K L are the face buttons in the pad's
diamond, Enter is Start. In the menus W A S D move the cursor, K or Space confirms.

The mouse buttons and wheel are bindable like keys. The pointer hides over the game when it has not
moved for two seconds (always in full screen), and in full screen it cannot leave the window
(`hide_cursor`, `confine_cursor`). There is no pointer-driven menu, because the game's menus have no
notion of a pointer.

## Troubleshooting

- **A pad is not seen.** Look for `[INPUT] ... pad(s) found` in `logs/run-*.log.err`. A pad that
  SDL does not know as a gamepad is not used; Steam's own controller configuration can hide a
  pad from other programs, so close Steam or its controller layer while testing.
- **The keyboard player is an extra controller.** By default the game sees one more controller
  for the keyboard. If you only use pads and that gets in the way, set `keyboard_player = off`,
  or `merge` to type into player 1's pad.
- **No rumble.** A pad buzzes for a moment when it connects. If it does not, the pad's rumble does not work through SDL
  (try `backend = xinput`, or another cable or mode). If it buzzes but the game does not rumble it, look in
  `logs/run-*.log.err` for `[INPUT] rumble ... sent` lines after a hit: they name the pad and say whether SDL accepted the
  command; none means the game did not ask (check its Options for vibration). The game's own pulses are brief and
  faint, so `rumble_floor` and `rumble_min_ms` make them stronger and longer; raise `rumble_min_ms` if it is still too
  subtle.
- **A pad plugged in later is not seen.** It should be: the game sees it as a hot-plug within a couple of seconds.
  The log shows `[INPUT] ... is player N` and `[OHCI0] pad N arriving`. With four controllers already present there is
  no free player.
- **A stick drifts.** Raise `left_deadzone` / `right_deadzone`.
- **Keys do nothing.** The window must have the focus; click it. A key held while the window lost
  the focus is released.

## Diagnostics (maintainers)

These are for unattended runs and are not player settings:

| Variable | Effect |
|---|---|
| `RECOMP_INPUT_HOST=1` | Start the host input layer in a scripted or `RECOMP_SETTINGS=none` run, where it is otherwise off so the test harness keeps its one scripted pad. `0` turns it off everywhere and leaves the toolkit's old XInput path. |
| `RECOMP_INPUT_NO_PADS=1` | Open no pad, whatever is plugged in. |
| `RECOMP_INPUT_IGNORE_FOCUS=1` | Read devices without the window's focus. |
| `RECOMP_PAD_SCRIPT_KEYS=1` | The scripted presses of the player the keyboard plays as press that player's first bound key instead, so a scripted match drives the whole keyboard path. |
| `RECOMP_INPUT_VIRTUAL_PADS=n` with `RECOMP_PAD_SCRIPT_VIRTUAL=1` | Attach `n` SDL virtual gamepads and send the scripted presses of that player to the pad's buttons, so a scripted match goes through the real SDL path. `RECOMP_INPUT_VIRTUAL_LATE_MS=t` plugs one more in `t` ms after start (hot-plug); `RECOMP_INPUT_VIRTUAL_CHORD_MS=t` makes virtual pad 1 hold both stick clicks 1.8 s at `t` ms and then press B (opens and closes the overlay). |
| `RECOMP_RUMBLE_LOG=1` | Log the rumble values the game sends, when they change. |
| `RECOMP_USB_PADS=n` | Number of controllers on the hub. Set by the game from the settings unless already set. |

## How it fits together

`src/hooks/pc_input.c` owns the settings and the window's messages. The toolkit does the rest:
`src/input/input_map.c` (key names, binding lists, deadzones, remapping; plain C, unit tested),
`src/input/input_host.c` (the devices, slots, focus and rumble; tested against SDL's virtual
gamepads) and `src/usb/usb_gamepad.c` (the emulated Controller S that carries the report to the
game and the rumble report back). The title never learns that the pad is not an Xbox one.

SDL3 is zlib licensed; the toolkit downloads release 3.4.18 at configure time, checks its hash and
builds it statically. On Windows, `-DDEFJAM_SDL3=OFF` selects the XInput fallback.
POSIX hosts also use SDL for window/audio integration; this is not a promise of an XInput fallback there.
