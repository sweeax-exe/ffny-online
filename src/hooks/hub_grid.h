/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The online hub's tile layout (build 98, "tile hub"): one big tile (QUICK MATCH) on the left, the other modes
 * as tiles in up to three rows on its right, and a slim bar of chips under them (MY STATS ... LEAVE).
 * The pure part, in C, so tests/unit/test_hub_grid_native.py runs it natively: where every box goes
 * (hub_grid_layout) and where the stick or the D-pad goes from one box to the next (hub_grid_move).
 * The drawing is in hub_tiles.inc. Boxes are in the menu's 640x480 units. */
#ifndef DEFJAM_HUB_GRID_H
#define DEFJAM_HUB_GRID_H

#ifdef __cplusplus
extern "C" {
#endif

#define HUB_GRID_MAX 24

typedef struct HubRect { float x, y, w, h; } HubRect;

/* Lays out 1 big tile, `nright` tiles beside it and `nchips` chips in the bar at the foot of `area`:
 * out[0] the big tile, out[1 .. nright] the tiles beside it (row by row, left to right), then the chips.
 * The tiles beside it go into up to three rows, the extra ones in the lower rows (5: 1, 2, 2; 6: 2, 2, 2;
 * 7: 2, 2, 3), so the first of them (RANKED MATCH) gets a whole row when there is room. `big_w` is the big
 * tile's width (the whole width without tiles beside it), `bar_h` the bar's height, `gap` the space between
 * boxes. Returns the number of boxes (0 when they do not fit HUB_GRID_MAX). */
int hub_grid_layout(int nright, int nchips, HubRect area, float big_w, float bar_h, float gap, HubRect *out);

/* The box the stick or the D-pad goes to from `cur` (dx, dy: -1, 0 or 1, one of them non-zero): the nearest
 * box wholly on that side, those overlapping `cur` across the move first, then the one lined up best with
 * `cur`'s top edge (left, right) or left edge (up, down). No wraparound: `cur` itself when there is none. */
int hub_grid_move(const HubRect *r, int n, int cur, int dx, int dy);

#ifdef __cplusplus
}
#endif

#endif
