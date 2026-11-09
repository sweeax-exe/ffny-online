# Widescreen song banner over side-filled menus

In widescreen without the HD Widescreen Menus mod, the song notification in the lower-left corner of the menus
looked cut off at the 4:3 picture's left edge (owner play-test, 2026-11-07, Battle menus at 16:9).

## Cause

Since #118 the notification is tagged by the front end (`src/patches/widescreen.c`: `chyron._y` moved into the
renderer's left-tag band) and the renderer anchors it to the screen's left edge, so it is not clipped at the 4:3 box.
That is right when the menu art fills the screen: over 3D, or with a pack's `[wide]` side slabs.

A menu background without `[wide]` art is a centred 4:3 picture. On a frame with such a picture and no 3D, the
renderer asks for the side fill (`ws_end_frame`, toolkit `nv2a_pb_exec.c`), which is painted over the areas beside the
4:3 box when the frame is presented, after everything the title drew. The left-anchored card reaches into that area,
so its left part was under the blur, and what showed began at the 4:3 edge.

## Correction (toolkit)

A left-tagged batch stays in the 4:3 box, where the title placed it, while the frames before it got the side fill; it
goes to the left edge otherwise. The placement changes only after 8 frames agree: with the mod installed, screen changes
have one to five frames without their `[wide]` art, which would otherwise move the card for those frames.

Checked at 16:9 (`RECOMP_WS_WIDE_TRACE` reports each change), Battle -> One on One menus to a fight:

| Data | Result |
|---|---|
| no mods | in the 4:3 box from the first menu on |
| `menu-ui-hd-wide` | in the box for the title screen's first frames, then at the left edge for the rest of the run |

The toolkit's left-tag fixture checks both placements, the 8-frame rule and the fill turned off.

## Not covered

- The side fill exists only in the Direct3D 11 presenter; the Vulkan one draws black beside a 4:3 picture. The rule
  uses the frame's request, so on Vulkan the card also stays in the 4:3 box over such a picture, which matches the
  picture there.
- `[display] widescreen_side_fill = black` never asks for the fill, so the card stays at the left edge, over black.
