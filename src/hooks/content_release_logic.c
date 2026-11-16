/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 87, the notice of content to come: the rules and the texts (content_release_logic.h). Nothing here reads
 * the game or the clock: the unit tests build this file alone (tests/unit/test_content_release.py). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "content_release_logic.h"

/* the service's words (server: relay/accounts/packs_release.go releaseKind), and ours for one and for several */
static const struct { const char *word, *one, *many; } k_kind[CR_KINDS] = {
    { "new fighter", "new fighter", "new fighters" },
    { "skin", "skin", "skins" },
    { "map", "map", "maps" },
    { "clothing", "clothing item", "clothing items" },
    { "fighter look", "fighter look", "fighter looks" },
    { "language", "language", "languages" },
    { "pack", "pack", "packs" },
    { "update", "update", "updates" },
};

/* ---- the answer: a small reader for the one shape the service sends ---- */

static const char *skip_space(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/* p at a string's opening quote: copies it (plain; an escape gives the character after the backslash, which is
 * enough for the words compared here) and returns the place after its closing quote, NULL when it does not end. */
static const char *read_string(const char *p, char *out, int size)
{
    int n = 0;
    for (p++; *p && *p != '"'; p++) {
        if (*p == '\\') { if (!*++p) return NULL; }
        if (out && n < size - 1) out[n++] = *p;
    }
    if (out && size > 0) out[n] = 0;
    return *p == '"' ? p + 1 : NULL;
}

int cr_parse(const char *json, long long local_time, CrInfo *out)
{
    const char *p = json;
    int have_count = 0, have_clock = 0;
    memset(out, 0, sizeof *out);
    out->local_time = local_time;
    if (!json) return 0;
    while (*p) {
        char key[32], word[32];
        const char *after;
        if (*p != '"') { p++; continue; }
        if (!(after = read_string(p, key, (int)sizeof key))) return 0;
        p = skip_space(after);
        if (*p != ':') continue;                /* a value (a title), not a name */
        p = skip_space(p + 1);
        if (*p == '"') {                        /* a text value: only "kind" matters; a title is passed over whole */
            if (!(after = read_string(p, word, (int)sizeof word))) return 0;
            p = after;
            if (!strcmp(key, "kind")) {
                int k;
                for (k = 0; k < CR_KINDS; k++)
                    if (!strcmp(word, k_kind[k].word)) { out->kinds[k]++; break; }
            }
            continue;
        }
        if ((*p >= '0' && *p <= '9') || *p == '-') {
            long long v = strtoll(p, NULL, 10);
            if (!strcmp(key, "release_at")) out->release_at = v;
            else if (!strcmp(key, "count")) { out->count = (int)v; have_count = 1; }
            else if (!strcmp(key, "server_time")) { out->server_time = v; have_clock = v > 0; }
        }
    }
    if (out->count < 0 || out->release_at < 0) return 0;
    if (out->count > 9999) out->count = 9999;
    return have_count && have_clock;
}

long long cr_local_release(const CrInfo *i) { return i->local_time + (i->release_at - i->server_time); }

long long cr_remaining(const CrInfo *i, long long now) { return cr_local_release(i) - now; }

int cr_shows(const CrInfo *i, long long now)
{
    return i->count > 0 && i->release_at > 0 && i->server_time > 0 && cr_remaining(i, now) > 0;
}

/* ---- the texts ---- */

static int put(char *out, int size, int n, const char *text)
{
    int len = (int)strlen(text);
    if (n < 0 || !out || size <= 0) return n;
    if (n + len > size - 1) len = size - 1 - n;
    if (len > 0) memcpy(out + n, text, (size_t)len);
    out[n + len] = 0;
    return n + len;
}

int cr_kinds_text(const CrInfo *i, char *out, int size)
{
    int k, n = 0, parts = 0, present = 0, sum = 0, named = 0, total, show, more;
    char part[48];
    if (!out || size <= 0) return 0;
    out[0] = 0;
    for (k = 0; k < CR_KINDS; k++)
        if (i->kinds[k] > 0) { present++; sum += i->kinds[k]; }
    total = i->count > sum ? i->count : sum;    /* the answer names at most 20: the count has them all */
    /* every kind when they fit, the rest ("N more") taking the last part when there is one */
    show = present;
    if (present > CR_PARTS || (present == CR_PARTS && total > sum)) show = CR_PARTS - 1;
    for (k = 0; k < CR_KINDS && parts < show; k++) {
        if (i->kinds[k] <= 0) continue;
        snprintf(part, sizeof part, "%s%d %s", parts ? ", " : "", i->kinds[k],
                 i->kinds[k] == 1 ? k_kind[k].one : k_kind[k].many);
        n = put(out, size, n, part);
        named += i->kinds[k];
        parts++;
    }
    more = total - named;
    if (more > 0) {
        if (!parts) snprintf(part, sizeof part, "%d new pack%s", more, more == 1 ? "" : "s");
        else snprintf(part, sizeof part, ", %d more", more);
        n = put(out, size, n, part);
    }
    return n;
}

int cr_in_text(long long remaining, char *out, int size)
{
    long long minutes;
    if (!out || size <= 0) return 0;
    if (remaining < 60) return snprintf(out, (size_t)size, "in under a minute");
    minutes = (remaining + 59) / 60;            /* never "in 0 min", and 2h 0m 30s is not yet "in 2h" */
    if (minutes < 60) return snprintf(out, (size_t)size, "in %d min", (int)minutes);
    if (minutes < 24 * 60) {
        if (minutes % 60 == 0) return snprintf(out, (size_t)size, "in %dh", (int)(minutes / 60));
        return snprintf(out, (size_t)size, "in %dh %dm", (int)(minutes / 60), (int)(minutes % 60));
    }
    if ((minutes / 60) % 24 == 0) return snprintf(out, (size_t)size, "in %dd", (int)(minutes / (24 * 60)));
    return snprintf(out, (size_t)size, "in %dd %dh", (int)(minutes / (24 * 60)), (int)((minutes / 60) % 24));
}

int cr_clock_text(int hour, int minute, int h12, char *out, int size)
{
    if (!out || size <= 0) return 0;
    if (hour < 0 || hour > 23) hour = 0;
    if (minute < 0 || minute > 59) minute = 0;
    if (!h12) return snprintf(out, (size_t)size, "%02d:%02d", hour, minute);
    return snprintf(out, (size_t)size, "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute, hour < 12 ? "AM" : "PM");
}

int cr_headline(int days, int hour, int minute, int h12, char *out, int size)
{
    char clock[16];
    if (!out || size <= 0) return 0;
    cr_clock_text(hour, minute, h12, clock, (int)sizeof clock);
    if (days <= 0) return snprintf(out, (size_t)size, "NEW CONTENT AT %s", clock);
    if (days == 1) return snprintf(out, (size_t)size, "NEW CONTENT TOMORROW AT %s", clock);
    return snprintf(out, (size_t)size, "NEW CONTENT IN %d DAYS", days);
}

int cr_toast_text(const CrInfo *i, long long now, int days, int hour, int minute, int h12, char *out, int size)
{
    char clock[16], in[32], kinds[160];
    if (!out || size <= 0) return 0;
    cr_clock_text(hour, minute, h12, clock, (int)sizeof clock);
    cr_in_text(cr_remaining(i, now), in, (int)sizeof in);
    cr_kinds_text(i, kinds, (int)sizeof kinds);
    if (days <= 0) return snprintf(out, (size_t)size, "New content at %s (%s): %s.", clock, in, kinds);
    if (days == 1) return snprintf(out, (size_t)size, "New content tomorrow at %s (%s): %s.", clock, in, kinds);
    return snprintf(out, (size_t)size, "New content %s: %s.", in, kinds);
}

/* ---- when it is said ---- */

int cr_toast(CrNotice *n, const CrInfo *i, long long now, unsigned long long now_ms, int in_menu)
{
    long long left;
    if (!in_menu || !cr_shows(i, now)) return 0;
    left = cr_remaining(i, now);
    if (n->told_release != i->release_at || n->told_count != i->count) {       /* a schedule not told yet */
        n->told_release = i->release_at;
        n->told_count = i->count;
        n->told_soon = left <= CR_SOON_S;       /* told this close to it: that was the reminder too */
        n->until = now_ms + CR_TOAST_MS;
    } else if (!n->told_soon && left <= CR_SOON_S) {
        n->told_soon = 1;
        n->until = now_ms + CR_TOAST_MS;
    }
    return now_ms < n->until;
}

int cr_after_due(CrNotice *n, const CrInfo *i, long long now, int spread, int in_fight)
{
    if (in_fight || i->count <= 0 || i->release_at <= 0 || i->server_time <= 0) return 0;
    if (n->checked_release == i->release_at) return 0;
    if (spread < 0) spread = 0;
    if (spread > CR_AFTER_SPREAD_S) spread = CR_AFTER_SPREAD_S;
    if (now < cr_local_release(i) + CR_AFTER_S + spread) return 0;
    n->checked_release = i->release_at;
    return 1;
}
