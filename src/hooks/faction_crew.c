/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Faction War's crew-only fighters: see faction_crew.h. */
#include "faction_crew.h"

#define CREW_CREATED_FIGHTER 99     /* NETPLAY_CREATED_FIGHTER: never a crew member */

int faction_crew_has(const int *ids, int n, int id)
{
    int i;
    for (i = 0; ids && i < n; i++) if (ids[i] == id) return 1;
    return 0;
}

int faction_crew_parse(const char *list, int *out, int max)
{
    int n = 0;
    const char *p = list;
    while (p && *p && n < max) {
        int v = 0, digits = 0;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9' && digits < 6) { v = v * 10 + (*p - '0'); p++; digits++; }
        if (digits && (*p == ',' || *p == ' ' || !*p) && v <= 255 && v != CREW_CREATED_FIGHTER && !faction_crew_has(out, n, v))
            out[n++] = v;
        while (*p && *p != ',') p++;      /* to the next entry (a bad one is skipped whole) */
        if (*p == ',') p++;
    }
    return n;
}

int faction_crew_pairs(const char *list, int *a, int *b, int max)
{
    int n = 0;
    const char *p = list;
    while (p && *p && n < max) {
        int x = 0, y = 0, dx = 0, dy = 0;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9' && dx < 4) { x = x * 10 + (*p - '0'); p++; dx++; }
        if (dx && *p == ':') {
            p++;
            while (*p >= '0' && *p <= '9' && dy < 4) { y = y * 10 + (*p - '0'); p++; dy++; }
            if (dy && (*p == ',' || *p == ' ' || !*p) && x <= 255 && y <= 999) { a[n] = x; b[n] = y; n++; }
        }
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return n;
}

int faction_crew_filter(const int *crew, int n_crew, const int *offered, int n_offered, int *out, int max)
{
    int i, n = 0;
    for (i = 0; offered && i < n_offered && n < max; i++)
        if (faction_crew_has(crew, n_crew, offered[i])) out[n++] = offered[i];
    return n;
}

int faction_crew_random(const int *crew, int n_crew, const int *offered, int n_offered, unsigned r)
{
    int i, count = 0, k;
    for (i = 0; offered && i < n_offered; i++) count += faction_crew_has(crew, n_crew, offered[i]);
    if (!count) return -1;
    k = (int)(r % (unsigned)count);
    for (i = 0; i < n_offered; i++)
        if (faction_crew_has(crew, n_crew, offered[i]) && k-- == 0) return offered[i];
    return -1;
}

const char *faction_crew_line(int side)
{
    return side == 1 ? "FACTION WAR: D-MOB'S CREW ONLY" : side == 2 ? "FACTION WAR: CROW'S CREW ONLY" : "";
}
