/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The room streak and the recent form's text (hud_stats_rules.h). */
#include "hud_stats_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hud_streaks_reset(HudStreaks *t)
{
    if (t) memset(t, 0, sizeof(*t));
}

static int find(const HudStreaks *t, const char *name)
{
    int i;
    for (i = 0; i < HUD_STREAK_PLAYERS; i++)
        if (t->name[i][0] && !strncmp(t->name[i], name, HUD_STREAK_NAME - 1)) return i;
    return -1;
}

void hud_streaks_result(HudStreaks *t, const char *room, const char *const *names, int count, unsigned winners)
{
    int p;
    if (!t || !names || count < 1) return;
    if (!room) room = "";
    if (strncmp(t->room, room, sizeof(t->room) - 1)) {
        hud_streaks_reset(t);
        snprintf(t->room, sizeof(t->room), "%s", room);
    }
    t->decisions++;
    for (p = 0; p < count && p < 32; p++) {
        const char *name = names[p];
        int i, won = (winners >> p) & 1u;
        if (!name || !*name) continue;
        i = find(t, name);
        if (i < 0) {
            /* a free entry, else the one untouched the longest */
            int k, oldest = 0;
            for (k = 0; k < HUD_STREAK_PLAYERS; k++) {
                if (!t->name[k][0]) { oldest = k; break; }
                if (t->age[k] < t->age[oldest]) oldest = k;
            }
            i = oldest;
            snprintf(t->name[i], HUD_STREAK_NAME, "%s", name);
            t->wins[i] = 0;
        }
        t->wins[i] = won ? t->wins[i] + 1 : 0;
        t->age[i] = t->decisions;
    }
}

int hud_streaks_get(const HudStreaks *t, const char *room, const char *name)
{
    int i;
    if (!t || !name || !*name || !t->room[0] || strncmp(t->room, room ? room : "", sizeof(t->room) - 1)) return 0;
    i = find(t, name);
    return i < 0 ? 0 : t->wins[i];
}

int hud_streak_label(int wins, char *out, int size)
{
    int n;
    if (!out || size < 1) return 0;
    out[0] = 0;
    if (wins < HUD_STREAK_SHOWN) return 0;
    n = snprintf(out, (size_t)size, "(%d WINS)", wins);
    return n < 0 ? 0 : n >= size ? size - 1 : n;
}

/* the next '|' field of *p as an int; -1 when it is not a number */
static int field_int(const char **p)
{
    char *end;
    long v;
    if (**p == '|') (*p)++;
    v = strtol(*p, &end, 10);
    if (end == *p || v < 0 || v > 1000) return -1;
    *p = end;
    return (int)v;
}

int hud_recent_parse(const char *text, HudRecent *out, int max, int *n)
{
    int got = 0;
    const char *line = text;
    if (n) *n = 10;
    while (line && *line) {
        const char *eol = strchr(line, '\n'), *end = eol ? eol : line + strlen(line);
        if (n && !strncmp(line, "n=", 2)) {
            int v = atoi(line + 2);
            if (v > 0 && v <= 100) *n = v;
        } else if (!strncmp(line, "recent=", 7) && got < max && out) {
            const char *p = line + 7, *bar = memchr(p, '|', (size_t)(end - p));
            HudRecent r;
            memset(&r, 0, sizeof(r));
            if (bar && bar > p && bar - p < HUD_STREAK_NAME) {
                memcpy(r.name, p, (size_t)(bar - p));
                p = bar;
                r.wins = field_int(&p);
                r.losses = field_int(&p);
                r.draws = field_int(&p);
                r.counted = field_int(&p);
                if (r.wins >= 0 && r.losses >= 0 && r.draws >= 0 && r.counted >= 0 && p <= end) out[got++] = r;
            }
        }
        line = eol ? eol + 1 : NULL;
    }
    return got;
}

int hud_recent_label(const HudRecent *r, int n, char *out, int size)
{
    int k, last;
    if (!out || size < 1) return 0;
    out[0] = 0;
    if (!r || r->counted <= 0) return 0;
    last = r->counted < n ? r->counted : n;
    k = r->draws > 0 ? snprintf(out, (size_t)size, "LAST %d: %dW %dL %dD", last, r->wins, r->losses, r->draws)
                     : snprintf(out, (size_t)size, "LAST %d: %dW %dL", last, r->wins, r->losses);
    return k < 0 ? 0 : k >= size ? size - 1 : k;
}
