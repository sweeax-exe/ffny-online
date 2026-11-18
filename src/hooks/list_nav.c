/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Fast scrolling in long lists: the repeat and the move (list_nav.h). */
#include "list_nav.h"

double list_repeat_rate(double held)
{
    double t = held - LIST_NAV_DELAY;
    if (t < 0) return 0;
    if (t > LIST_NAV_RAMP) t = LIST_NAV_RAMP;
    return LIST_NAV_RATE_START + (LIST_NAV_RATE_TOP - LIST_NAV_RATE_START) * t / LIST_NAV_RAMP;
}

int list_repeat_steps(ListRepeat *r, int dir, double now)
{
    int steps;
    r->first = 0;
    if (dir != r->dir) {            /* a press (or a release, or the other way) */
        r->dir = dir;
        r->since = r->last = now;
        r->acc = 0;
        r->first = dir != 0;
        return dir;
    }
    if (!dir) return 0;
    {
        double from = r->last - r->since, to = now - r->since, dt = now - r->last;
        if (dt > 0.1) {             /* a long frame (a hitch) does not jump the list */
            from = to - 0.1;
            dt = 0.1;
        }
        if (to > LIST_NAV_DELAY && dt > 0) {
            if (from < LIST_NAV_DELAY) {        /* the first repeat comes at the delay itself */
                if (r->acc < 1) r->acc = 1;
                from = LIST_NAV_DELAY;
            }
            /* the rate's mean over the frame (it rises linearly) */
            r->acc += (list_repeat_rate(from) + list_repeat_rate(to)) / 2 * (to - from);
        }
    }
    r->last = now;
    steps = (int)r->acc;
    r->acc -= steps;
    return steps * dir;
}

int list_nav_move(int sel, int n, int delta, int wrap)
{
    long long to;
    if (n <= 0) return 0;
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    if (!delta) return sel;
    if (wrap && (delta == 1 || delta == -1)) return (sel + delta + n) % n;
    to = (long long)sel + delta;
    if (to < 0) to = 0;
    if (to >= n) to = n - 1;
    return (int)to;
}
