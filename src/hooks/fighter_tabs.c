/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* CHOOSE FIGHTER's tabs and style filter; see fighter_tabs.h. */
#include "fighter_tabs.h"

#include <ctype.h>
#include <stddef.h>

int fighter_tab_step(int cur, int dir, const int offered[FTAB_COUNT])
{
    int t;
    if (dir == 0) return cur;
    for (t = cur + (dir > 0 ? 1 : -1); t >= 0 && t < FTAB_COUNT; t += dir > 0 ? 1 : -1)
        if (offered[t]) return t;
    return cur;
}

int fighter_tab_shown(int saved, const int offered[FTAB_COUNT])
{
    int t;
    if (saved >= 0 && saved < FTAB_COUNT && offered[saved]) return saved;
    if (offered[FTAB_FFNY]) return FTAB_FFNY;
    for (t = 0; t < FTAB_COUNT; t++) if (offered[t]) return t;
    return FTAB_FFNY;
}

/* The letters of s from `from`, lower case: the next one, or 0 at the end. */
static int next_letter(const char **s)
{
    while (**s && !isalpha((unsigned char)**s)) (*s)++;
    if (!**s) return 0;
    return tolower((unsigned char)*(*s)++);
}

int fighter_style_matches(const char *style, const char *filter)
{
    const char *start;
    const char *f = filter;
    if (!filter || !next_letter(&f)) return 1;     /* no filter: every style */
    if (!style) return 0;
    for (start = style; *start; start++) {
        const char *s = start, *q = filter;
        int a, b;
        if (!isalpha((unsigned char)*start)) continue;
        for (;;) {
            b = next_letter(&q);
            if (!b) return 1;
            a = next_letter(&s);
            if (a != b) break;
        }
    }
    return 0;
}

int fighter_style_next(int cur, int count)
{
    if (count <= 0 || cur < 0 || cur >= count) return 0;
    return cur + 1;
}
