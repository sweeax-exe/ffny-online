# Widescreen combo call-out (#196)

In a widescreen fight, the "1ST COMBO MOVE" call-out came apart: the "ST" sat far to the left of the "1" and
"COMBO MOVE". Reproduced on macOS (M1 Pro) at 16:9 with no texture packs: `RECOMP_TEST_RNG_SEED=20261006`, One on One
with the default fighters, `--step-input default`, simulation step 1100.

## Cause

The renderer places each 2D batch (`ws_place_batch`, toolkit `nv2a_pb_exec.c`). During a fight, a batch wholly on one
side of the middle is anchored to that side only inside a HUD strip: top `y1 <= 130`, or bottom `y0 >= 350` with 3-4
fighters. Anything else is centred. The call-out is three batches, and they straddled those limits.

Guest-pixel extents over 1v1 and four-fighter fights at 16:9, including the text's animation:

| Piece | Texture | Top strip (P1, P2) | Bottom strip (P3, P4) |
|---|---|---|---|
| Digit | 128x64 | y 106.5..142.4 | y 338.5..370.4 |
| Suffix ("ST", "ND") | 64x32 | y 108.6..128.1 | y 340.6..356.1 |
| Text ("COMBO MOVE") | 128x128 | y 120.5..140.5 | y 352.5..368.5 |

So the top strip anchored only the suffix, and the bottom strip anchored only the text.

## Correction

The limits become `y1 <= 146` and `y0 >= 336`, which take in the whole call-out. Other fight text keeps its place:
"CASH MONEY", "2 HIT COMBO" and the "+points" text already lay inside the strips.

Moving these limits is safe only while nothing else one-sided falls between the old and new values. A batch trace of
those fights and of a paused 1v1 found one nearby piece that is not part of the call-out, the pause panel's at
x 146..268, y 330..346. Its `y0` is below 336, so it stays centred. The toolkit fixture `nv2a_ws_left_tag` pins these
extents at 16:9 and 21:9. It fails on either old limit and on a limit loose enough to catch the panel piece.

At 4:3 (`k = 1`) the classifier returns before placing anything, so 4:3 cannot change.

## Not covered

- "3RD" did not appear in these runs. "1ST" and "2ND" did, and the digit and suffix use the same textures and layout.
- A tutorial prompt was not traced. One would move only if it lay wholly on one side and between the old and new limits.
- A paused four-fighter match was not traced. The panel's position does not depend on the number of fighters.
- Windows D3D11 and Steam Deck were not run. The change is in the shared executor, not a backend.
