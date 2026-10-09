# settings.ini and keybinds

The game writes `settings.ini` the first time it starts, in the data folder (beside `extracted` and
`save`; with no data folder, beside the executable). Every setting is in it with a comment, so the file
is a complete, editable copy of this page. Edit it with the game closed. A line you delete comes back
with its default; a line the game does not know is kept.

- A name is case-insensitive. Values are `true` / `false` (also `yes`/`no`, `on`/`off`, `1`/`0`),
  whole numbers (out-of-range numbers are clamped to the limits below), or one of the listed words.
  A value the game cannot read is replaced by the default, and `logs/run-*.log.err` says which.
- `RECOMP_SETTINGS=<path>` makes the game use another file; `RECOMP_SETTINGS=none` uses the defaults
  and writes nothing (this is how the test harness runs).
- An environment variable named in the tables below overrides the file for one run and is never saved.
- "Restart" means the setting is read at start-up only.

You do not have to edit the file by hand: the launcher (shown before the game starts) and the in-game overlay (F1)
change the same settings and save as you go. See [Launcher and overlay](launcher-and-overlay.md). For how input works
and what to do when something misbehaves, see [Controllers, keyboard and mouse](10-input.md).

## [launcher]

| Key | Default | Meaning |
|---|---|---|
| `skip` | `false` | Skip the launcher and start the game at once. The launcher shows on every launch unless this is on. Hold **Shift** while the game starts, or start it with `--launcher`, to see it anyway; turn this off in the overlay's General page or the launcher's own checkbox. Env `RECOMP_NO_LAUNCHER=1` skips it for one run. |

## [ui]

| Key | Default | Meaning |
|---|---|---|
| `overlay_key` | `F1` | The key that opens and closes the in-game overlay: one key name from the list below. |
| `overlay_pad` | `guide, left_stick_click+right_stick_click` | Pad buttons that open and close the overlay, any pad. Alternatives are separated by commas; buttons joined with `+` must all be held for half a second. Button names are the physical names used in `[gamepad]`. Empty for none. |
| `scale` | `100` | Size of the launcher's and overlay's text and controls in percent, 60-250, on top of your display's scaling (full screen on a large monitor is scaled up automatically). |

## [display]

| Key | Default | Meaning |
|---|---|---|
| `fullscreen` | `false` | Borderless full screen. **Alt+Enter** or **F11** switches while playing, and the choice is saved. |
| `window_width`, `window_height` | `1280`, `960` | Window size when not full screen (320-16384 by 240-16384). Resizing the window updates both. |
| `aspect` | `4:3` | `4:3` keeps the console's picture shape with black bars; `stretch` fills the window. |
| `render_scale` | `2` | Internal resolution as a multiple of the console's 640x480, 1-4. Higher is sharper and costs GPU time. Restart. Env `RECOMP_RENDER_SCALE`. |
| `filter` | `smooth` | How the picture is scaled to the window: `smooth` or `sharp`. |
| `vsync` | `true` | Pace frames on the display. Used when the refresh rate is a multiple of 60 Hz; other displays use the game's own 60 fps timer. Env `RECOMP_PRESENT_VSYNC`. |
| `gamma` | `true` | Apply the game's own brightness curve, as the console does. Restart. Env `RECOMP_GAMMA`. |

## [textures]

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Use local HD texture packs. Restart. |
| `packs` | `faithful-hd` | Folder names beneath `<data>/mods`, separated by semicolons. Later packs win. Restart. An explicit `RECOMP_TEXTURE_PACKS` enables those packs for one run. |
| `cache_mb` | `512` | Replacement memory budget in MiB (32–4096), accounting for GPU resources and retained upload data. Restart. Env `RECOMP_TEXTURE_CACHE_MB`. |
| `dump` | `false` | Write lossless source PNGs and texture identities to `<data>/hd-work/runtime`. Restart. Env `RECOMP_TEXTURE_DUMP_DIR` can select a different destination. |
| `dump_limit` | `2000` | Maximum new images captured per run (0–65536). Restart. Env `RECOMP_TEXTURE_DUMP_LIMIT`. |

See [HD textures](hd-textures.md) for the pack format and local workflow.

## [input]

| Key | Default | Meaning |
|---|---|---|
| `keyboard_player` | `auto` | How the keyboard and mouse play. `auto`: their own player after the pads (player 1 when no pad is connected, player 2 with one pad, and so on; merged into player 1 if four pads are connected). `off`: not used. `merge`: added to player 1's pad. `1` to `4`: their own player in that slot. Restart. Env `RECOMP_KEYBOARD_PLAYER`. |
| `players` | `0` | How many controllers the game sees at start, 0-4. `0` means one per pad plus one for a separate keyboard player. A pad plugged in later is added as a new controller when the game is running, so this rarely needs raising. Restart. |
| `backend` | `sdl` | Where pads are read: `sdl` (Xbox, DualSense, DualShock 4, Switch Pro and most others) or `xinput` (Xbox-style pads only, no extra library). Restart. Env `RECOMP_INPUT_BACKEND`. |
| `rumble` | `100` | Rumble strength in percent, 0-100. `0` turns it off. |
| `rumble_floor` | `60` | The game's rumble pulses are brief and often faint (40% strength is common). A pulse weaker than this percent of full strength is raised to it. `0` passes the game's strength through. |
| `rumble_min_ms` | `200` | The game's pulses last 50-200 ms, which many pads do not make felt. A pulse is held at least this many milliseconds, 0-1000, before the game's stop takes effect. `0` passes the game's timing through. |
| `rumble_on_connect` | `true` | Buzz a pad for a quarter of a second when it takes a player, at start-up or when plugged in, so you can tell it works and which pad it is. |
| `hide_cursor` | `true` | Hide the mouse pointer over the game after two seconds without movement (always in full screen). |
| `confine_cursor` | `true` | In full screen, keep the pointer inside the window so a click cannot land on another monitor. |

## [gamepad]

Applies to every pad. Which physical control acts as which Xbox control, and the dead zones.

| Key | Default | Meaning |
|---|---|---|
| `left_deadzone`, `right_deadzone` | `15`, `15` | Dead zone of each stick in percent of its travel, 0-90. A stick inside it reads as centred; beyond it the range is stretched so full push is still full. Raise it if a stick drifts. |
| `trigger_threshold` | `5` | How far a trigger must be pulled before it counts, in percent, 0-90. |
| `button_threshold` | `50` | How far a trigger or stick direction must travel to count as a button, when one is mapped to a button control, 10-90. |
| `left_stick`, `right_stick` | `left`, `right` | Which physical stick feeds each Xbox stick: `left`, `right` or `none`. The game uses the right stick for Blazin' moves. |
| one line per Xbox control | the identity | The physical control that acts as that Xbox control; see below. |

The Xbox controls are `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right`, `start`, `back`, `left_thumb`,
`right_thumb` (the stick clicks), `a`, `b`, `x`, `y`, `black`, `white`, `left_trigger`, `right_trigger`.
The value is one physical control, using SDL's positional names:

| Name | What it is |
|---|---|
| `south`, `east`, `west`, `north` | The bottom, right, left and top face buttons (Xbox A, B, X, Y; PlayStation cross, circle, square, triangle) |
| `left_shoulder`, `right_shoulder` | The shoulder buttons (L1/R1, LB/RB) |
| `left_trigger`, `right_trigger` | The triggers (L2/R2, LT/RT) |
| `back`, `start`, `guide` | Back/Create/Share, Start/Options, the logo or PS button |
| `left_stick_click`, `right_stick_click` | The stick clicks (L3/R3) |
| `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right` | The D-pad |
| `left_stick_up`, `left_stick_down`, `left_stick_left`, `left_stick_right` and the same for `right_stick_...` | A stick pushed one way, usable as a button |
| `misc1`, `touchpad`, `paddle1` to `paddle4` | Extra buttons some pads have |
| `none` | Unbound |

The defaults are the identity: `a = south`, `b = east`, `x = west`, `y = north`, `black = left_shoulder`,
`white = right_shoulder`, `start = start`, `back = back`, `left_thumb = left_stick_click`, and so on.
Examples: swap A and B with `a = east` and `b = south`; put Start on the PlayStation touchpad with
`start = touchpad`; swap the sticks with `left_stick = right` and `right_stick = left`.

## [keyboard]

The keyboard and mouse are one player's pad. Each line names the keys that press one Xbox control:
up to four, separated by commas (`a = K, Space, Mouse1`). `none` or an empty line unbinds it. A name
that is not a key is ignored and reported in the log.

The 24 lines are named for the Xbox control: `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right`, `start`, `back`,
`left_thumb`, `right_thumb`, `a`, `b`, `x`, `y`, `black`, `white`, `left_trigger`, `right_trigger`, then the sticks as
keys: `left_stick_up`, `left_stick_down`, `left_stick_left`, `left_stick_right`, `right_stick_up`, `right_stick_down`,
`right_stick_left`, `right_stick_right`.

### Default keybinds

| Xbox control | Setting | Default keys |
|---|---|---|
| Left stick up, down, left, right | `left_stick_up` ... `left_stick_right` | `W`, `S`, `A`, `D` |
| Right stick up, down, left, right | `right_stick_up` ... `right_stick_right` | `Up`, `Down`, `Left`, `Right` (the arrow keys) |
| A | `a` | `K`, `Space`, `Mouse1` (left button) |
| B | `b` | `L`, `LShift`, `Mouse2` (right button) |
| X | `x` | `J` |
| Y | `y` | `I` |
| Black | `black` | `U` |
| White | `white` | `O` |
| Left trigger | `left_trigger` | `Q`, `Mouse4` |
| Right trigger | `right_trigger` | `E`, `Mouse5` |
| Start | `start` | `Enter` |
| Back | `back` | `Backspace` |
| Left stick click | `left_thumb` | `LCtrl` |
| Right stick click | `right_thumb` | `RCtrl` |
| D-pad up, down, left, right | `dpad_up` ... `dpad_right` | `Numpad8`, `Numpad2`, `Numpad4`, `Numpad6` |

The face buttons sit under the right hand in the shape of the pad's diamond (I top, J left, L right,
K bottom). The left hand has the left stick, the triggers and run. In the menus, W A S D move the
cursor, K or Space confirms and Enter is Start. Which action each Xbox control performs is the game's
own, as on the console.

### Key names

| Kind | Names |
|---|---|
| Letters and digits | `A` to `Z`, `0` to `9` |
| Function keys | `F1` to `F12` |
| Arrows and editing | `Up`, `Down`, `Left`, `Right`, `Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete` |
| Common | `Space`, `Enter` (or `Return`), `Backspace`, `Tab`, `Escape` (or `Esc`), `CapsLock`, `Pause` |
| Modifiers | `Shift`, `LShift`, `RShift`, `Ctrl` (or `Control`), `LCtrl`, `RCtrl`, `Alt`, `LAlt`, `RAlt` |
| Numeric pad | `Numpad0` to `Numpad9`, `NumpadAdd`, `NumpadSubtract`, `NumpadMultiply`, `NumpadDivide`, `NumpadDecimal` |
| Punctuation | `Comma`, `Period`, `Slash`, `Semicolon`, `Quote`, `Minus`, `Equals`, `Grave`, `LeftBracket`, `RightBracket`, `Backslash` |
| Mouse | `Mouse1` (left), `Mouse2` (right), `Mouse3` (middle), `Mouse4`, `Mouse5` (the side buttons) |
| Mouse wheel | `WheelUp`, `WheelDown`: each notch is a short press |

`Shift`, `Ctrl` and `Alt` are held while either side is; `LShift` and the others name one side.
Do not bind **F11** or **Alt+Enter**: they switch full screen. Nothing is read while the game window is
in the background.

### Examples

```ini
[keyboard]
; Arrow keys as the d-pad as well as the right stick
dpad_up = Numpad8, Up
; Mouse wheel as the triggers
left_trigger = Q, WheelDown
right_trigger = E, WheelUp
; Space to run instead of grapple
b = L, LShift, Space
a = K, Mouse1
```

To play the keyboard as part of player 1's pad (menus with the keyboard while a pad is connected), set
`keyboard_player = merge` in `[input]`. To use pads only and give the game no keyboard controller,
set it to `off`.
