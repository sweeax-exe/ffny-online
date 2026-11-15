/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The season countdown and the hub fighter search: see season_search.h. */
#define _CRT_SECURE_NO_WARNINGS
#include "season_search.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

int season_countdown_text(const char *label, long long end, long long now, char *out, int size)
{
    long long left;
    int n;
    if (!out || size <= 0) return 0;
    out[0] = 0;
    if (end <= 0) return 0;
    left = end - now;
    if (left < 3600) n = snprintf(out, (size_t)size, "%s ENDS TODAY", label);
    else if (left < 86400) {
        long long h = left / 3600;
        n = snprintf(out, (size_t)size, "%s ENDS IN %lld HOUR%s", label, h, h == 1 ? "" : "S");
    } else {
        long long d = left / 86400;
        n = snprintf(out, (size_t)size, "%s ENDS IN %lld DAY%s", label, d, d == 1 ? "" : "S");
    }
    return n < 0 ? 0 : n >= size ? size - 1 : n;
}

/* The next letter or digit of s from *i on, lower case; 0 at the end. */
static int next_alnum(const char *s, int *i)
{
    while (s[*i]) {
        unsigned char c = (unsigned char)s[(*i)++];
        if (c < 0x80 && isalnum(c)) return tolower(c);
    }
    return 0;
}

/* s with only its letters and digits, lower case (at most size - 1 of them). */
static void squeeze(const char *s, char *out, int size)
{
    int i = 0, n = 0, c;
    while (n + 1 < size && (c = next_alnum(s, &i)) != 0) out[n++] = (char)c;
    out[n] = 0;
}

int fighter_name_matches(const char *name, const char *query)
{
    char q[64], nm[128];
    if (!query) return 1;
    squeeze(query, q, sizeof(q));
    if (!q[0]) return 1;        /* nothing typed, or only blanks and punctuation */
    if (!name) return 0;
    squeeze(name, nm, sizeof(nm));
    return strstr(nm, q) != 0;
}

static int first_letter(const char *s)
{
    int i = 0;
    return s ? next_alnum(s, &i) : 0;
}

int fighter_letter_jump(const char *const *names, int n, int cur, int dir)
{
    int here, best = 0, best_i = -1, low = 0, low_i = -1, high = 0, high_i = -1, i;
    if (n <= 0 || cur < 0 || cur >= n) return cur;
    here = first_letter(names[cur]);
    for (i = 0; i < n; i++) {
        int c = first_letter(names[i]);
        if (!c || c == here) continue;
        if (dir > 0 && c > here && (!best || c < best)) { best = c; best_i = i; }
        if (dir <= 0 && c < here && (!best || c > best)) { best = c; best_i = i; }
        if (!low || c < low) { low = c; low_i = i; }
        if (!high || c > high) { high = c; high_i = i; }
    }
    if (best_i >= 0) return best_i;
    /* wrap around the alphabet */
    if (dir > 0 && low_i >= 0) return low_i;
    if (dir <= 0 && high_i >= 0) return high_i;
    return cur;
}
