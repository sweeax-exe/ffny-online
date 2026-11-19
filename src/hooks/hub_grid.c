/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The online hub's tile layout and grid navigation (hub_grid.h). */
#include "hub_grid.h"

#include <math.h>

int hub_grid_layout(int nright, int nchips, HubRect area, float big_w, float bar_h, float gap, HubRect *out)
{
    int n = 0, rows, r, k = 0;
    float tiles_h, row_h, rx, rw, y;
    if (nright < 0) nright = 0;
    if (nchips < 0) nchips = 0;
    if (1 + nright + nchips > HUB_GRID_MAX) return 0;
    tiles_h = nchips ? area.h - bar_h - gap : area.h;
    if (nright == 0) big_w = area.w;
    out[n].x = area.x; out[n].y = area.y; out[n].w = big_w; out[n].h = tiles_h; n++;
    rows = nright < 3 ? nright : 3;
    rx = area.x + big_w + gap;
    rw = area.x + area.w - rx;
    row_h = rows ? (tiles_h - gap * (rows - 1)) / rows : 0;
    for (r = 0, y = area.y; r < rows; r++, y += row_h + gap) {
        int base = nright / rows, rem = nright % rows;
        int cols = base + (r >= rows - rem ? 1 : 0), c;
        float cw = (rw - gap * (cols - 1)) / cols;
        for (c = 0; c < cols && k < nright; c++, k++) {
            out[n].x = rx + c * (cw + gap); out[n].y = y; out[n].w = cw; out[n].h = row_h; n++;
        }
    }
    if (nchips) {
        float cw = (area.w - gap * (nchips - 1)) / nchips, cy = area.y + area.h - bar_h;
        int c;
        for (c = 0; c < nchips; c++) {
            out[n].x = area.x + c * (cw + gap); out[n].y = cy; out[n].w = cw; out[n].h = bar_h; n++;
        }
    }
    return n;
}

static float overlap(float a0, float a1, float b0, float b1)
{
    float lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
    return hi - lo;
}

int hub_grid_move(const HubRect *r, int n, int cur, int dx, int dy)
{
    const float eps = 0.5f;
    int best = cur, i;
    float best_score = 0;
    if (cur < 0 || cur >= n || (!dx && !dy)) return cur;
    for (i = 0; i < n; i++) {
        const HubRect *a = &r[cur], *b = &r[i];
        float gap, ov, align, score;
        if (i == cur) continue;
        if (dx > 0) { if (b->x < a->x + a->w - eps) continue; gap = b->x - (a->x + a->w); }
        else if (dx < 0) { if (b->x + b->w > a->x + eps) continue; gap = a->x - (b->x + b->w); }
        else if (dy > 0) { if (b->y < a->y + a->h - eps) continue; gap = b->y - (a->y + a->h); }
        else { if (b->y + b->h > a->y + eps) continue; gap = a->y - (b->y + b->h); }
        if (dx) { ov = overlap(a->y, a->y + a->h, b->y, b->y + b->h); align = fabsf(b->y - a->y); }
        else { ov = overlap(a->x, a->x + a->w, b->x, b->x + b->w); align = fabsf(b->x - a->x); }
        if (gap < 0) gap = 0;
        /* a box across the move comes first; then the nearest; then the one lined up with cur */
        score = (ov > eps ? 0.0f : 10000.0f + (-ov) * 4.0f) + gap * 2.0f + align;
        if (best == cur || score < best_score) { best = i; best_score = score; }
    }
    return best;
}
