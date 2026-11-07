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
| `skip` | `false` | Skip the launcher and start the game at once. The launcher shows on every launch unless this is on. Hold **Shift** while the game starts, or start it with `--launcher`, to see it anyway; turn this off in the overlay's Interface page or the launcher's own checkbox. Env `RECOMP_NO_LAUNCHER=1` skips it for one run. |
| `check_updates` | `true` | The app (`DefJamRecompiled`) checks GitHub for a newer release when it starts, at most every twelve hours, and Home offers the update. Off: no check. The game itself ignores it. Shown in the app's Advanced > Interface > Start-up as "Check for updates when the app starts". |

## [ui]

| Key | Default | Meaning |
|---|---|---|
| `overlay_key` | `F1` | The key that opens and closes the in-game overlay: one key name from the list below. |
| `overlay_pad` | `guide, left_stick_click+right_stick_click` | Pad buttons that open and close the overlay, any pad. Alternatives are separated by commas; buttons joined with `+` must all be held for half a second. Button names are the physical names used in `[gamepad]`. Empty for none. |
| `scale` | `100` | Size of the launcher's and overlay's text and controls in percent, 60-250, on top of your display's scaling (full screen on a large monitor is scaled up automatically). |

## [debug]

| Key | Default | Meaning |
|---|---|---|
| `unlock_all` | `false` | Every fighter and venue is available on any profile, through the developers' own unlock routine in the game. For testing: it changes the profile in memory and asks the game to save it, so keep it off for a save you care about. Restart. Shown in the launcher and the overlay's Diagnostics page as "Unlock all fighters and venues". Env `RECOMP_UNLOCK_ALL` (`1` on, `0` off). The test harness turns it on for its runs. [How it works](research/unlock-everything.md). |
| `skip_intro` | `false` | Skip the startup legal screen and EA/THX/intro movies and start at **Press Start**. A brief resource-loading screen and the title's own animation still run. Story cutscenes and idle attract playback keep working. Choose **Interface → Skip startup movies and legal screen** before Play, or restart after changing it in the overlay. Env `RECOMP_SKIP_INTRO` (`1` on, `0` off) overrides the file. [How it works](research/startup-skip-flow.md). |

### Recorder keys

The play-test recorder: see [Play-test recorder](playtest-recorder.md). Off by default; `--record` on the command
line turns it on for one run. Every key here is read at start-up.

| Key | Default | Meaning |
|---|---|---|
| `record` | `false` | Keep a session under `logs/sessions/` for every run: frame times, events, the sound and the last seconds of pictures. Env `RECOMP_RECORD`. |
| `mark_key` | `F9` | While recording, the key that saves a bug bundle of the seconds before it. |
| `mark_pad` | `back+left_stick_click` | While recording, pad buttons that do the same (on a Steam Deck: hold **View** and press the **left stick** for half a second). Alternatives separated by commas, a combination joined with `+`, button names as in `[gamepad]`; empty for none. |
| `record_fps` | `10` | Pictures a second kept for bundles, 0-30. Each is a read-back from the graphics card. Env `RECOMP_RECORD_FPS`. |
| `record_seconds` | `30` | Seconds before a mark a bundle holds, 5-120. Env `RECOMP_RECORD_SECONDS`. |
| `record_audio` | `true` | Keep the mixed sound (about 11 MB a minute). |
| `record_game_state` | `true` | Log fighters, health and match events (read-only). |
| `auto_bundle_hitch_ms` | `0` | A frame at least this long saves a bundle by itself (five a run at most); 0 is off. Env `RECOMP_RECORD_AUTO_HITCH_MS`. |
| `hang_seconds` | `15` | No frame for this many seconds saves a hang bundle; 0 is off. Env `RECOMP_RECORD_HANG_SECS`. |
| `keep_sessions` | `20` | Sessions kept whole; older ones keep their bundles and events only. |
| `record_guest_ram` | `false` | Crash and hang bundles also hold the game's 64 MB of memory. |

## [cheats]

Cheats for issue [#71](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/71), on the **Cheats** page of
the launcher and the in-game overlay. All are off by default and apply as they change. None of them is ever
saved into a profile; the Story one-shots that do change the profile are buttons in the overlay, not
settings (see [Launcher and overlay](launcher-and-overlay.md#cheats)). [How they work](research/cheat-menu.md).

| Key | Default | Meaning |
|---|---|---|
| `infinite_health` | `off` | Fighters who lose no health: `off`, `player 1` (the first fighter on a pad or the keyboard), `every player` (every fighter not played by the computer) or `everyone`. Hits still land and react. Env `RECOMP_CHEAT_INFINITE_HEALTH` (the same words). |
| `max_blazin` | `off` | The momentum meter stays full and Blazin' is always ready for the same choice of fighters; start it as usual. It fills five seconds into a fight. Env `RECOMP_CHEAT_MAX_BLAZIN`. |
| `no_regen` | `false` | No fighter gets health back by itself during a fight. Env `RECOMP_CHEAT_NO_REGEN`. |
| `fighter_size` | `normal` | `giant` or `tiny` fighters. Only the picture changes: reach and collision stay the same. Env `RECOMP_CHEAT_FIGHTER_SIZE`. |
| `story_hero` | `-1` | In Story fights, play as another fighter, with that fighter's look and moves, instead of your created fighter: the fighter's `<ID>` from `fighter.xml` (the launcher lists them by name), `-1` off. Cutscenes, the crib and the shops still show your own fighter. Env `RECOMP_CHEAT_STORY_HERO`. |

## [display]

| Key | Default | Meaning |
|---|---|---|
| `fullscreen` | `false` | Borderless full screen. **Alt+Enter** or **F11** switches while playing, and the choice is saved. |
| `window_width`, `window_height` | `1920`, `1080` | Window size when not full screen (320-16384 by 240-16384). Resizing the window updates both. |
| `aspect` | `widescreen (auto)` | Picture shape. `4:3` keeps the console's picture shape with black bars; `stretch` fills the window; **widescreen (beta):** `widescreen (auto)` (the screen's shape: 16:9 or 16:10; wider screens get 16:9), `16:9`, `16:10`, `21:9 (experimental)`, `32:9 (experimental)` widen the 3D view (more of the arena at the sides, same vertical view) while menus, HUD and movies keep their shape. Widescreen starts with the game (4:3 and stretch apply at once). Env `RECOMP_ASPECT` (a choice name). See [widescreen](widescreen/README.md). |
| `widescreen_hud` | `auto` | Fight HUD in widescreen: `edges` (screen corners), `centered` (corners of a centred 16:9 box) or `auto` (edges up to 16:9, the box when wider). Restart. |
| `widescreen_cutscenes` | `authored` | Story cutscenes in widescreen: `authored` keeps the game's own 16:9 framing (full screen at 16:9, thin bars at 16:10); `fill` always fills the screen. Restart. |
| `widescreen_side_fill` | `blur` | What is beside 4:3 pictures (movies, menu backgrounds) in widescreen: `blur` (a soft blurred extension) or `black`. Restart. |
| `widescreen_fov_cap` | `150` | The widest horizontal field of view widescreen may use, 90-170 degrees. Restart. |
| `render_scale` | `2` | Internal render height: 1× = 480p, 2× = 960p, 3× = 1440p, 4× = 1920p; width follows Picture shape. For 1080p output, 2× balances sharpness and cost, or 3× renders above the output height for extra sharpness. For 1440p, 3× matches the height. Window size is separate; higher scales cost GPU time. Restart. Env `RECOMP_RENDER_SCALE`. |
| `filter` | `smooth` | How the picture is scaled to the window: `smooth`, `sharp` or `smooth (high quality)`. The last is `smooth` with area-averaged downscaling: when the render scale makes the picture larger than the window (4× in a 1080p window, 3× or 4× on a Steam Deck), it averages the extra pixels instead of skipping some, so edges and fine detail shimmer less. Same as `smooth` when the picture is not larger than the window. Applies at once. Env `RECOMP_PRESENT_FILTER` (a choice name). [How it works](research/graphics-enhancements.md). |
| `msaa` | `off` | Multisample anti-aliasing of the 3D picture: `off`, `2x`, `4x` or `8x`. Smooths the jagged edges of fighters and the arena, as the console's own anti-aliasing did; combines with render scale. Lowered to what the graphics card supports (the log says which). Costs GPU time and memory, more at higher render scales. Restart. Env `RECOMP_MSAA` (a choice name). |
| `texture_mips` | `false` | Use the smaller copies (mip levels) the game ships with each texture, and filter between them as the console does. Without it the port samples only the full-size copy, so distant floors, walls and crowds shimmer, most at render scale 1-2. Applies at once (textures already loaded keep their shape until next used). Env `RECOMP_TEXTURE_MIPS` (`1` on, `0` off). |
| `anisotropy` | `off` | Anisotropic filtering: `off`, `2x`, `4x`, `8x` or `16x`. Keeps floors and walls seen at a low angle sharp. Applies to textures the game filters smoothly; cut-out sprites are left as they are. Works best with `texture_mips` on. Applies at once. Env `RECOMP_ANISOTROPY` (a choice name). |
| `vsync` | `true` | Pace frames on the display. Used when the refresh rate is a multiple of 60 Hz; other displays use the game's own 60 fps timer, and on those (75, 100, 144, 165 Hz) motion can judder because each frame stays up for an uneven number of refreshes. The log's `[DISPLAY]` line names the refresh rate and which of the two paces the game. Env `RECOMP_PRESENT_VSYNC`. |
| `gamma` | `true` | Apply the game's own brightness curve, as the console does. Restart. Env `RECOMP_GAMMA`. |
| `brightness` | `100` | Picture brightness, 50-200, on top of the game's curve: a power curve that moves the mid-tones and keeps black and white where they are. 100 is the console's picture; higher is brighter. Applies at once. Env `RECOMP_BRIGHTNESS`. |
| `vignette` | `true` | The darkened screen edges that venues and some scenes draw (part of the game's post effects). `false` removes them. Applies at once. Env `RECOMP_VIGNETTE` (`1` on, `0` off). |
| `vignette_strength` | `100` | How dark the vignette is, 0-100 percent of the game's own; 0 is the same as off. Applies at once. Env `RECOMP_VIGNETTE_STRENGTH`. [How both work](research/picture-options.md). |
| `color_tint` | `100` | Strength of the colour tint venues and scenes put over the picture (the warm, sepia grade of most fights), 0-100 percent of the game's own; 0 shows the picture untinted. Applies at once. Env `RECOMP_COLOR_TINT`. |
| `scratches` | `true` | The film scratches some scenes draw over the picture. `false` removes them. Applies at once. Env `RECOMP_SCRATCHES` (`1` on, `0` off). |

## [language]

| Key | Default | Meaning |
|---|---|---|
| `pack` | *(empty)* | The language mod that is on: its folder in the mods folder (`<data>/mods`, or `RECOMP_TEXTURE_ROOT`); empty plays the game in its own English. The first start with a pack rebuilds its files from your dump into `<data>/cache/language`. Restart. Set by ticking the mod on the launcher's **Mods** page (one language at a time). Env `RECOMP_LANGUAGE_PACK`. [Language packs](language-packs.md). |
| `story_subtitles` | `auto` | Story cutscene subtitles: `auto` (what the language mod asks for; without one, as the game does), `all cutscenes` (every cutscene, English included: the game has English subtitles for all of them), `as the game does` (the opening cutscene only). Restart. Shown in the launcher as **Interface → Language and subtitles**. Env `RECOMP_STORY_SUBTITLES`. |
| `game` | `english` | The game's own language: `english`, or `french`, the translation built into the game (menus, memory-card messages and Story subtitles, which then show in every cutscene; the voices and the menu icon art stay English). Not used while a language mod is on. Restart. Shown in the launcher as **Interface → Language and subtitles**. Env `RECOMP_GAME_LANGUAGE`. [The game's own French](language-packs.md#the-games-own-french). |

## [songs]

| Key | Default | Meaning |
|---|---|---|
| `restored` | `true` | Restored songs: music cut from the release whose audio is still on the disc ("Holler If Ya Hear Me", 2Pac) is in the Beatbox and the playlist like the other songs. The game reads a copy of its song list with the extra rows from `<data>/cache/songs` (built on a language mod's copy when one is on). Restart. Env `RECOMP_SONGS_RESTORED`. [Research](research/custom-songs.md). |
| `custom` | `true` | Your own songs: MP3, FLAC, Ogg Vorbis and WAV files in `<data>/songs` (and its subfolders) and in `folders` are converted once into `<data>/cache/songs` and play in the Beatbox, the menus and fights like the game's songs, with their tags (or file name) in the song banner. The game holds 60 songs (27 of your own next to its 33); 35 can be on at once. Restart. Env `RECOMP_SONGS_CUSTOM`. [Custom songs](custom-songs.md). |
| `folders` | *(empty)* | More folders of your own songs, separated by semicolons; `<data>/songs` is always read. Restart. Env `RECOMP_SONGS_FOLDERS`. |
| `skip_pad` | `right_stick_click+dpad_right` | Pad buttons that skip to the next song, in the menus and in fights; a combination acts when its last button goes down (not while the left stick is also in: L3+R3 is the overlay's). The game sees the buttons too. Alternatives separated by commas; names as in `[gamepad]`; empty for none. Restart. [Custom songs](custom-songs.md#next-song-song-from-the-start). |
| `restart_pad` | `right_stick_click+dpad_left` | Pad buttons that play the current song from its start. Restart. |
| `skip_key` | `F8` | The key that skips to the next song (the game does not see it). Empty for none. Restart. |
| `restart_key` | `F7` | The key that plays the current song from its start. Empty for none. Restart. |

## [textures]

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Use local HD texture packs. Restart. |
| `packs` | `faithful-hd` | Folder names beneath `<data>/mods`, separated by semicolons. Later packs win. Restart. An explicit `RECOMP_TEXTURE_PACKS` enables those packs for one run. |
| `cache_mb` | `2048` | Replacement memory budget in MiB (32–4096), accounting for GPU resources and retained upload data. A budget smaller than a scene's textures makes the game evict and reload them during play. Try increasing it for texture pop-in, slow texture loading or repeated stutters with texture packs enabled. Higher values use more graphics/system memory; leave room for the game and other apps. Restart. Env `RECOMP_TEXTURE_CACHE_MB`. |
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


## Startup-skip application time

`debug.skip_intro` uses the same default < INI < environment boolean precedence as other registered settings, including explicit `false`/`0`. It is sampled after the launcher on the first screen selection, and that one-time decision is consumed even when the option is off or the initial screen is unexpected. Overlay edits therefore save the next-launch choice; they cannot begin skipping later front-end or Story screens in the current process. [Startup implementation and acceptance](research/startup-skip-flow.md) records this scope and the independent fast-start default used by automated tests.
