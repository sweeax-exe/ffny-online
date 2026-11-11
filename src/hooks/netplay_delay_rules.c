/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The online input delay and its line (build 76); see netplay_delay_rules.h. */
#include "netplay_delay_rules.h"
#include <stdio.h>

void netdelay_top2(const int *pings, int n, int *a, int *b)
{
    int i;
    *a = *b = 0;
    for (i = 0; pings && i < n; i++) {
        int r = pings[i] < 0 ? 0 : pings[i];
        if (r > *a) { *b = *a; *a = r; } else if (r > *b) *b = r;
    }
}

int netdelay_path_ms(int a_ms, int b_ms)
{
    if (a_ms < 0) a_ms = 0;
    if (b_ms < 0) b_ms = 0;
    return (a_ms + b_ms) / 2;
}

int netdelay_steps(int a_ms, int b_ms, int rollback, int multi_margin)
{
    /* ms to 1/60 s steps, rounded up, + 1 (the formula of builds 19-75, unchanged) */
    int d = (netdelay_path_ms(a_ms, b_ms) * 3 + 49) / 50 + 1;
    if (rollback) d -= NETDELAY_RB_LESS;         /* what the delay does not cover, a rollback does */
    else if (multi_margin > 0) d += multi_margin;
    return d < NETDELAY_MIN ? NETDELAY_MIN : d > NETDELAY_MAX ? NETDELAY_MAX : d;
}

int netdelay_line(char *out, int size, int rollback, int frames, int path_ms, int estimate)
{
    int n;
    const char *about = estimate ? "about " : "";
    if (!out || size <= 0) return 0;
    if (rollback)
        n = snprintf(out, (size_t)size, "Rollback \xC2\xB7 %s%d frame%s", about, frames, frames == 1 ? "" : "s");
    else
        n = snprintf(out, (size_t)size, "No rollback (path %d ms) \xC2\xB7 %s%d frame%s", path_ms < 0 ? 0 : path_ms,
                     about, frames, frames == 1 ? "" : "s");
    return n < 0 ? 0 : n >= size ? size - 1 : n;
}
