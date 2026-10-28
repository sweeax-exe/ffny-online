# v0.12.1: widescreen menu song notification (#118)

In widescreen the front end's song notification stayed at the centred 4:3
box's left edge. On the title screen the parked tab peeked in at the box
edge, inside the picture, and the card slid out from there over the main
menu. Reproduced on macOS at 16:9 (`RECOMP_WS_ASPECT=1.7778`, full-canvas
captures), no texture packs.

## The component

feloader, the persistent root APT movie, attaches the `chyron` clip to its
root at depth 7 during its first-frame set-up: `chyron._x = 0` (+20 on Xbox),
`chyron._y = 385`, `currentChyronY = chyron._y`. `LaunchChyron(sSong, sArtist,
sAlbum)` sets `chyron._x` from the label length and `chyron._y =
currentChyronY`, fills the three text fields and plays the clip's slide.
Between launches the clip stays parked with only its torn-paper tab on
screen. The GameCube branch is the only other user of `currentChyronY`.
The Beatbox branch fills its own screen's chyron instead.

Centred placement squeezes every menu batch into the 4:3 box and clips it to
that box. The renderer only anchors HUD strips to the edges during fights.
Its geometry cannot tell this clip from other lower-left menu art, and the
executor runs on its own thread, so a game-side on/off switch cannot mark
the clip's batches.

## Correction

- **Tag in the vertices (game, `src/patches/widescreen.c`).** With
  widescreen on, the literal 385 in the set-up becomes 385 + 8192. It is the
  operand of a PushShort in feloader.apt. Every launch reuses the stored y, so
  the tab, card, text and slide are all drawn 8192 pixels lower than
  authored. The patch is made in memory between the APT loader resolving
  feloader.apt (`sub_001810B0` entry: the name) and the next step of the load
  (`sub_00056F50` entry: the data, before any action runs). It checks the
  `Apt Data` magic and an FNV-1a hash of the 18-byte instruction window. A
  different build is left centred and logged; already-tagged data is left
  alone. No game bytes are in the source.
- **Undo and anchor (runtime, fork commit `7a0f2f6`).** The new
  `nv2a_ws_set_left_tag(8192)`: a 2D batch whose first vertex lands in
  [7936, 8960) takes the CPU vertex path, is moved back up, and is anchored
  left like the fight HUD (the screen edge, or the centred 16:9 box at
  ultrawide). It is clipped to the anchored box. It is off at 4:3 and by
  default.

## Evidence (local, ignored logs)

| Run | Log | Result |
|---|---|---|
| 16:9 | `run-20261025-211609.log.err` | tab at the physical left edge on the title screen; card slides out from x = 0 over the main menu, text intact |
| 16:10 | `run-20261025-211748.log.err` | same at the 16:10 edge |
| 21:9 | `run-20261025-211845.log.err` | anchored to the centred 16:9 box, like the fight HUD; a shorter label also placed correctly |

Each run logs `[WS] song notification: anchored left`. No crashes,
watchdogs, truncated batches or unresolved calls, and the disposable save
was restored identically.

Fixture `tests/nv2a_ws_left_tag` (toolkit) drives the real executor with six
2D triangles. A tagged one comes back up, anchored left. The same one
untagged stays centred, and one parked below the screen is untouched. Tag
off, 4:3 and ultrawide are also covered. The existing `nv2a_host_clip`
fixture still passes.

Not covered: Windows D3D11 (the change is backend-independent), the Beatbox
screen, and a label long enough to push the card past the left edge (that
part is clipped at the screen edge, as at 4:3).
