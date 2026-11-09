# Widescreen struggle meter (submissions)

In a widescreen fight, the struggle meter shown in a submission ("TAP ANY BUTTON" under the fighter's limb health)
sat about a side bar's width to the right of the limb health above it (owner play-test, 2026-11-07, 16:9).

## Cause

The renderer anchors a fight 2D batch to its side only inside the HUD's strips (`ws_place_batch`, toolkit
`nv2a_pb_exec.c`; [combo call-out](widescreen-combo-callout.md)): top `y1 <= 146`, bottom `y0 >= 336` with 3-4
fighters. The limb health ("HEAD HEALTH", y 105..136) lies inside the top strip and goes to the screen edge with the
health bars. The meter starts inside the strip but reaches below it, so it stayed centred. It was centred before the
call-out change (#198) as well: its frame ends at y 186.

## Measurements

An owner's run with a trace of every one-sided 2D batch near the strips (16:9, One on One, submissions on both
fighters, a pause during one):

| Piece | Texture | P1 | P2 |
|---|---|---|---|
| Meter frame | 256x64 | x 41..297, y 122..186 | x 343..599, y 121..185 |
| Fill bar (moves) | 128x8 | y 137..147 | y 136..146 |
| Text ("TAP ANY BUTTON") | 128x128 | x 46.5..218.5, y 146.5..162.5 | not drawn for the computer |
| Limb health label and bar | 128x128, 256x8 | y 105.5..136, already anchored | |
| Pause panel, highest pieces | 16x32, 128x100 | y0 161.5 and below | |

A second owner run (Free For All with four fighters, submissions on fighters 3 and 4, a pause) gave the bottom strip.
Fighters 3 and 4's meter is mirrored about their HUD at the bottom:

| Piece | Texture | P3 | P4 |
|---|---|---|---|
| Text ("TAP ANY BUTTON"), above the HUD | 128x128 | x 48.5..183.5, y 314..330 | x 465.5..590.5, y 314..330 |
| Meter frame | 256x64 | x 41..297, y 331..395 | x 343..599, y 331..395 |
| Fill bar (moves) | 128x8 | y 335..345 | y 335..345 |
| Limb health label and bar | 128x128, 256x8 | y 346..368, already anchored | |
| Pause panel, lowest pieces | 128x100 | x 146.5..267.5, y 330..346; x 190.5..223.5, y 300..316 | |

A scripted four-minute fight also drew the match summary's rows straight after the fight, while the HUD still
counts as live: rows starting at y 124..146 that are 23-26 tall (128x32, 256x32).

## Correction (toolkit)

A one-sided batch that starts at y 118..150, ends by y 190 and is 18 tall or less, or 60 or more, goes with the top
strip. That takes in the meter's three pieces and leaves the pause panel (starts lower) and the summary rows (their
height) centred. With 3-4 fighters, the bottom strip also takes each of the bottom meter's pieces by where it starts
and its height: a piece 60 or more tall starting at y 320..340 (frame), 12 or less starting at y 330..340 and ending by
350 (fill bar), 18 or less starting at 308 or lower down and ending at y 326..332 (text). The pause panel's lowest
piece (y 330..346, 16 tall) lies between them and matches none. Applied to both owner traces and to two scripted
four-minute fights, the rules move only the meter's pieces. The left-tag fixture pins these extents at 16:9 and 21:9 and fails without either rule.

## Not covered

- 4:3 is unchanged (nothing is placed).
- Windows D3D11 only in the trace; the classifier is shared by every backend.
