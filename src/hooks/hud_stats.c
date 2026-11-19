/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* See hud_stats.h. The streaks live here under a lock (netplay's thread records, the overlay's reads); the
 * recent form is one GET /api/players/recent?format=text per START on a thread of its own, through
 * account_fetch (no session token: the route is public, so no answer can sign the game out). An old account
 * service answers 404 and nothing shows. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud_stats.h"
#include "account.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static SRWLOCK g_lock = SRWLOCK_INIT;
static HudStreaks g_streaks;

#define RECENT_MAX 4
static HudRecent g_recent[RECENT_MAX];
static int g_recent_n, g_recent_last = 10;
static unsigned g_recent_seq;       /* counts the requests: an older answer is dropped */
static char g_recent_names[RECENT_MAX][HUD_STREAK_NAME];
static int g_recent_count;

void hud_stats_result(const char *room, const char *const *names, int count, unsigned winners)
{
    int i;
    AcquireSRWLockExclusive(&g_lock);
    hud_streaks_result(&g_streaks, room, names, count, winners);
    fprintf(stderr, "[HUDSTATS] room streaks after a decision (winners 0x%X):", winners);
    for (i = 0; i < count && i < 4; i++)
        if (names[i] && names[i][0]) fprintf(stderr, " %s=%d", names[i], hud_streaks_get(&g_streaks, room, names[i]));
    fprintf(stderr, "\n");
    ReleaseSRWLockExclusive(&g_lock);
}

int hud_stats_streak(const char *room, const char *name)
{
    int n;
    AcquireSRWLockShared(&g_lock);
    n = hud_streaks_get(&g_streaks, room, name);
    ReleaseSRWLockShared(&g_lock);
    return n;
}

static void url_put(char *out, size_t size, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(out);
    for (; *s && n + 4 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
}

static DWORD WINAPI recent_thread(void *arg)
{
    unsigned seq = (unsigned)(uintptr_t)arg;
    char path[512] = "/api/players/recent?format=text&n=10&names=", *buf;
    HudRecent got[RECENT_MAX];
    int i, n = 10, k, len = 0, status;
    AcquireSRWLockShared(&g_lock);
    for (i = 0; i < g_recent_count; i++) {
        if (i) url_put(path, sizeof(path), ",");
        url_put(path, sizeof(path), g_recent_names[i]);
    }
    ReleaseSRWLockShared(&g_lock);
    buf = (char *)calloc(1, 4096);
    if (!buf) return 0;
    status = account_fetch(path, buf, 4095, &len);
    buf[4095] = 0;
    k = status == 200 ? hud_recent_parse(buf, got, RECENT_MAX, &n) : 0;
    free(buf);
    AcquireSRWLockExclusive(&g_lock);
    if (seq == g_recent_seq) {
        memcpy(g_recent, got, sizeof(HudRecent) * (size_t)k);
        g_recent_n = k;
        g_recent_last = n;
    }
    ReleaseSRWLockExclusive(&g_lock);
    fprintf(stderr, "[HUDSTATS] recent form: HTTP %d, %d of the players\n", status, k);
    return 0;
}

void hud_stats_recent_request(const char *const *names, int count)
{
    int i, n = 0;
    unsigned seq;
    HANDLE th;
    AcquireSRWLockExclusive(&g_lock);
    for (i = 0; i < count && n < RECENT_MAX; i++)
        if (names[i] && names[i][0]) snprintf(g_recent_names[n++], HUD_STREAK_NAME, "%s", names[i]);
    g_recent_count = n;
    g_recent_n = 0;         /* the last match's lines are not this one's */
    seq = ++g_recent_seq;
    ReleaseSRWLockExclusive(&g_lock);
    if (!n || (getenv("RECOMP_RECENT_FORM") && !strcmp(getenv("RECOMP_RECENT_FORM"), "off"))) return;
    th = CreateThread(NULL, 0, recent_thread, (void *)(uintptr_t)seq, 0, NULL);
    if (th) CloseHandle(th);
}

int hud_stats_recent_label(const char *name, char *out, int size)
{
    int i, k = 0;
    if (!out || size < 1) return 0;
    out[0] = 0;
    if (!name || !*name) return 0;
    AcquireSRWLockShared(&g_lock);
    for (i = 0; i < g_recent_n; i++)
        if (!_stricmp(g_recent[i].name, name)) { k = hud_recent_label(&g_recent[i], g_recent_last, out, size); break; }
    ReleaseSRWLockShared(&g_lock);
    return k;
}

#else

static HudStreaks g_streaks;
void hud_stats_result(const char *room, const char *const *names, int count, unsigned winners)
{ hud_streaks_result(&g_streaks, room, names, count, winners); }
int hud_stats_streak(const char *room, const char *name) { return hud_streaks_get(&g_streaks, room, name); }
void hud_stats_recent_request(const char *const *names, int count) { (void)names; (void)count; }
int hud_stats_recent_label(const char *name, char *out, int size) { (void)name; if (out && size > 0) out[0] = 0; return 0; }

#endif
