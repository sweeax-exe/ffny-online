# v0.12.1: Story shop LOADING flash on item changes (#119)

Changing an item in a Story shop flashed one frame of the game's LOADING
card. That frame is black over the centred 4:3 picture, with "LOADING" at the
lower right, and the shop scene still shows at the widescreen sides. The
preview came back on the next frame. Reproduced on macOS in the clothing shop
(16:9, no texture packs) with the `mac-profiles` fixture. The route is
crib → map → Shop District → SUS → Tops → Jackets, then choose items. The
owner's Windows session shows the same in the jeweller and clothing shops.

## Cause

Every item change ends with `CreatePlayer.ForceUpdate`. Its callback
(`0x0004A1A0`) reconciles the created player's parts; see the owner's static
trace in #119. The part loaders `0x000992E0`, `0x0009A050` and `0x0009AB90`
emit event `0xF0000038` before a blocking load. `sub_0005B770` registers
`0x0005AF80` for that event. It is a complete LOADING frame: it begins a frame
with a clear, draws `gLoadingIconQuad1` (the caption, guest x ≈ 460–480,
y 412–444), ends the frame, presents and returns. The two draws are the
2-draw frames in the recorder. Because the frame is 2D, the clear covers only
the centred box in widescreen. On the console the part came off the disc and
the card covered the wait. Here the load takes a few dozen milliseconds, so
the card is a one-frame flash.

## Correction (game code, `src/recomp_manual.c`)

The game's presents are counted at `sub_00018DF0` entry. The ForceUpdate
callback's entry records the count. `0x0005AF80` becomes a `_gen` wrapper
that skips the LOADING frame while no ordinary frame has been presented since
that ForceUpdate began. Otherwise it runs the original. A skipped observer
just returns: the dispatcher ignores its result and pops its arguments. The
load, the event and its other observer (`0x0005AF50`) still run. Shop entry
and every other load keep their card. `RECOMP_SHOP_LOADING_CARD=1` restores
the card.

## Evidence (local, ignored logs; same binary, the variable as the control)

Recorder sessions over 14 ForceUpdates each (9 load a part):

| Run | Session | After a refresh |
|---|---|---|
| Card kept (control) | `20261025-220746` | item frame → **2-draw LOADING frame** → 52–64 ms load frame, 9 times |
| Fix | `20261025-220954` | item frame → 53–68 ms load frame with the full UI (353–363 draws); 0 flashes |

Both runs also kept the shop's own entry card (7 frames) and had no audio
dropouts. A capture after a skipped card shows the new jacket on the
preview. No crashes, watchdogs, truncated batches or unresolved calls, and
the disposable saves were restored identically.

## The hitch

The load frame itself remains. On this Mac it is one 53–68 ms frame per
refresh, against 11–21 ms for an ordinary frame. It uploads only 1–2
textures (256–320 KB), so the time is the game's synchronous part load, not
the texture cache. The owner's Windows session, with `faithful-hd` and
`menu-ui-hd-wide` and the HD cache near its 512 MiB budget, measured
146–333 ms and real audio underruns in the clothing shop. No packs are
installed on this Mac, so the pack-dependent part is not measured here. The
next step is a Windows recorder run comparing no packs, Faithful only and
both packs.
