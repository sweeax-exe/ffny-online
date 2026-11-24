/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Build 110: the room settings' CUSTOM venue list (netplay_room_venues.h). */
#include "netplay_room_venues.h"

#include <stdio.h>
#include <string.h>

int netroom_venues_has(const unsigned char *pool, int count, int id)
{
    int i;
    for (i = 0; i < count; i++) if (pool[i] == id) return 1;
    return 0;
}

int netroom_venues_parse(const char *text, unsigned char *out, int max)
{
    int n = 0;
    const char *p = text;
    if (!p || !out || max <= 0) return 0;
    while (*p && n < max) {
        int v = 0, digits = 0;
        while (*p && (*p < '0' || *p > '9')) p++;      /* commas, spaces, anything else */
        while (*p >= '0' && *p <= '9') {
            if (v < 1000) v = v * 10 + (*p - '0');
            digits++;
            p++;
        }
        if (digits && v >= 1 && v <= 255 && !netroom_venues_has(out, n, v)) out[n++] = (unsigned char)v;
    }
    return n;
}

int netroom_venues_format(const unsigned char *ids, int n, char *out, int size)
{
    int len = 0, i;
    if (!out || size <= 0) return 0;
    out[0] = 0;
    for (i = 0; ids && i < n; i++) {
        char one[8];
        int k = snprintf(one, sizeof(one), "%s%d", len ? "," : "", ids[i]);
        if (k <= 0 || len + k >= size) break;
        memcpy(out + len, one, (size_t)k + 1);
        len += k;
    }
    return len;
}

int netroom_venues_fit(const unsigned char *in, int n, const int *order, int order_n, unsigned char *out, int max)
{
    unsigned char keep[256] = { 0 };
    int i, m = 0;
    for (i = 0; in && i < n; i++) keep[in[i]] = 1;
    for (i = 0; order && i < order_n && m < max; i++)
        if (order[i] >= 1 && order[i] <= 255 && keep[order[i]] && !netroom_venues_has(out, m, order[i])) {
            /* out may be in: in's ids are all in keep[] already, so writing over them loses nothing */
            out[m++] = (unsigned char)order[i];
        }
    return m;
}

int netroom_venues_toggle(unsigned char *pool, int *count, int id, const int *order, int order_n, int max)
{
    unsigned char keep[256] = { 0 };
    int i, m = 0, on, known = 0;
    if (!pool || !count) return -2;
    for (i = 0; i < order_n; i++) if (order[i] == id) known = 1;
    if (!known || id < 1 || id > 255) return -2;
    on = !netroom_venues_has(pool, *count, id);
    if (on && *count >= max) return -1;
    for (i = 0; i < *count; i++) keep[pool[i]] = 1;
    keep[id] = (unsigned char)on;
    for (i = 0; i < order_n && m < max; i++)
        if (order[i] >= 1 && order[i] <= 255 && keep[order[i]]) {
            keep[order[i]] = 0;     /* once, if order repeats one */
            pool[m++] = (unsigned char)order[i];
        }
    *count = m;
    return on;
}

int netroom_venues_merge(unsigned char *saved, int n, const unsigned char *pool, int count, const int *order, int order_n, int max)
{
    unsigned char mine[256] = { 0 };
    int i, m = 0;
    for (i = 0; i < order_n; i++) if (order[i] >= 1 && order[i] <= 255) mine[order[i]] = 1;
    for (i = 0; i < n; i++) if (!mine[saved[i]] && !netroom_venues_has(saved, m, saved[i])) saved[m++] = saved[i];
    for (i = 0; i < count && m < max; i++) if (pool[i] && !netroom_venues_has(saved, m, pool[i])) saved[m++] = pool[i];
    return m;
}

int netroom_venues_all(unsigned char *pool, const int *order, int order_n, int max)
{
    int i, m = 0;
    for (i = 0; pool && order && i < order_n && m < max; i++)
        if (order[i] >= 1 && order[i] <= 255 && !netroom_venues_has(pool, m, order[i])) pool[m++] = (unsigned char)order[i];
    return m;
}
