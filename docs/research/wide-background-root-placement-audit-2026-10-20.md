# Wide-background APT root placements — 2026-10-20

This bounded offline follow-up traces seven mapped scenery shapes through
their APT root and sprite parents. It records authored frame, depth, local
matrix and composed 640×480-stage placement. It does not infer runtime loader
transforms, clipping outside the APT stage, active screen state, or visible
exposure in-game.

The replay script is
`logs/ui-restoration-20261020/audit-wide-background-root-placements.py`; its
coordinates-only metadata output is
`logs/ui-restoration-20261020/wide-background-root-placements.json`. It uses
the existing `apt.load_movie` decoder against the seven named embedded APTs.
The JSON preserves all root `PlaceObject`/`RemoveObject` events, frame-0 root
display lists, target root-depth state across authored frames, and the exact
sprite-frame events along each target path. Matrix order is parent × child.

| Resource / target | Authored placement chain | Composed matrix / authored bounds |
|---|---|---|
| `story/choosGF2`, shape 17 | root frame 0, depth 1, character 17, identity | identity; local shape bounds `[0,0,512,480]` |
| `story/choosGF2`, shape 31 | root frame 0, depth 12, character 31, identity | identity; local shape bounds `[512,0,640,480]` |
| `story/choosGF4`, shape 3 | root frame 0, depth 2, character 3, identity | identity; local bounds `[0,0,640,480]` |
| `story/choosPart`, shape 3 | root frame 0, depth 2, character 3, identity; remains at this depth in both authored root frames | identity; local bounds `[0,0,640,480]` |
| `story/charDec`, shape 3 | root frame 0, depth 2, character 3, identity; remains at this depth in all three authored root frames | identity; local bounds `[0,0,640,480]` |
| `options/cribOptions`, shape 5 door | root frame 0 depth 1 `bkgWall` sprite 6: translate `(469.3999938964844,0)`; sprite 6 frame 0 depth 1: shape 5 identity | composed translate `(469.3999938964844,0)`; local bounds `[0,0,171,487]`, so authored placement is approximately `x=469.4..640.4`, `y=0..487` before stage clipping |
| `options/cribOptions`, shape 9 wall | root frame 0 depth 3 `menu` sprite 58: identity; sprite 58 frame 0 depth 1 `menu` sprite 57: identity; sprite 57 frame 0 depth 2: shape 9 identity | composed identity; local bounds `[0,0,571,480]` |
| `battle/matchLoa`, shape 2 plate | root frame 0, depth 1, character 2, identity; remains at that depth through all 120 authored root frames | identity; local bounds `[0,0,640,480]` |
| `battle/matchLoa`, shape 10 overlay | root frame 0, depth 17, character 10, identity; remains at that depth through all 120 authored root frames | identity; local bounds `[0,0,640,480]` |
| `battle/tips`, shape 3 wall | root frame 0, depth 2, character 3, identity; remains at that depth in both authored root frames | identity; local bounds `[0,0,640,480]` |

## Crib Options placement matters

Do not place shape 5 at its local origin when assembling the `cribOptions`
background. It lives under the `bkgWall` root object, whose x translation is
469.4 pixels. Its 171-pixel-wide, 487-pixel-high door therefore occupies the
right edge and extends just past the 640×480 stage; the stage/runtime clip was
not resolved by this offline audit.

Shape 9 is the other wall layer. It is a 571×480 shape under the root `menu`
sprite chain, with identity transforms at every parent. Its x range overlaps
the door's translated range from about469.4 to571, while the door continues to
about640.4. Root depth1 `bkgWall` is drawn before root depth3 `menu`, placing
the door behind the wall where those shapes overlap. Preserve both placements
and their authored order when restoring the composition. Root depth5 also
contains a separate character61 object scaled `[2.5, 0, 0, 1.875, 0, 0]`; it
is not either audited scenic shape and remains a separate foreground layer.

Outside Crib Options, the other eight target shapes use identity root/parent transforms. In
`choosGF2`, the two side-by-side wall sections are separate root objects at
depths1 and12, rather than children of a translated container. `matchLoa`
keeps its full-stage overlay at depth17 above the opaque plate at depth1. These
are authored APT placement facts only; real loader positioning, masking and
aspect-ratio exposure still require runtime inspection.
