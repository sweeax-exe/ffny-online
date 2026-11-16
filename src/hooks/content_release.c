/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 87, the notice of content to come (owner, 2026-11-16): accepted community packs no longer go live one by
 * one (rooms split each time), they go live together at a daily release window, and the game announces it:
 *   - the account service's public GET /api/packs/next-release says when and what ({"release_at", "count",
 *     "items":[{"title","kind"}], "next_window", "server_time"}; release_at 0 / count 0: nothing scheduled). It is
 *     asked with the catalogue check (content_update_frame: every ten minutes in a menu while signed in, and on the
 *     online pages), on a thread of its own, one request at a time; no timer of its own;
 *   - while packs are scheduled: "NEW CONTENT AT 17:00" on the online pages (the corner badge of build 86's notice)
 *     with what it is and how long until then, and a corner notice on the main menu, once per schedule and once
 *     more ten minutes before. The time is the PC's own (content_release_logic.h);
 *   - once the time has passed the notice is gone, and one to three minutes later (spread, so the PCs do not all
 *     ask at once) the catalogue is checked, so the UPDATE NOW notice takes over without waiting for the next
 *     ten-minute check.
 * Test switch: RECOMP_TEST_NEXT_RELEASE=<file> reads the service's answer from a file instead (its server_time is
 * "now": release_at - server_time seconds from the moment it is read). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "content_release.h"
#include "account.h"

static SRWLOCK g_lock = SRWLOCK_INIT;
static CrInfo g_info;                   /* the last answer (g_lock) */
static int g_have;
static volatile LONG g_busy;            /* a request runs */
static CrNotice g_notice;               /* render thread */
static int g_said_failure;

static void take(const CrInfo *i, const char *from)
{
    CrInfo old;
    int had;
    AcquireSRWLockExclusive(&g_lock);
    old = g_info;
    had = g_have;
    g_info = *i;
    g_have = 1;
    ReleaseSRWLockExclusive(&g_lock);
    if (had && old.release_at == i->release_at && old.count == i->count) return;
    if (i->count > 0 && i->release_at > 0) {
        char kinds[160];
        cr_kinds_text(i, kinds, (int)sizeof kinds);
        fprintf(stderr, "[CONTENT] next release (%s): %d pack(s) in %lld s (%s)\n", from, i->count,
                i->release_at - i->server_time, kinds);
    } else if (had && old.count > 0) {
        fprintf(stderr, "[CONTENT] next release (%s): nothing scheduled any more\n", from);
    } else {
        fprintf(stderr, "[CONTENT] next release (%s): nothing scheduled\n", from);
    }
    fflush(stderr);
}

static int read_test_file(const char *path)
{
    char buf[8192];
    CrInfo i;
    size_t n;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[CONTENT] next release (test): cannot read %s\n", path); return 0; }
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    if (!cr_parse(buf, (long long)time(NULL), &i)) {
        fprintf(stderr, "[CONTENT] next release (test): %s is not the service's answer\n", path);
        return 0;
    }
    take(&i, "test file");
    return 1;
}

static DWORD WINAPI fetch_thread(void *arg)
{
    static char buf[32768];             /* one request at a time (g_busy) */
    int len = 0, status;
    CrInfo i;
    (void)arg;
    status = account_fetch("/api/packs/next-release", buf, (int)sizeof buf, &len);
    if (status == 200 && cr_parse(buf, (long long)time(NULL), &i)) {
        take(&i, "service");
        g_said_failure = 0;
    } else if (!g_said_failure) {
        /* the last answer stays: its time still comes, and the next check asks again */
        g_said_failure = 1;
        fprintf(stderr, "[CONTENT] next release: no answer from the service (status %d); asked again with the next "
                        "content check\n", status);
        fflush(stderr);
    }
    InterlockedExchange(&g_busy, 0);
    return 0;
}

static void ask(void)
{
    static int test_read;
    const char *test = getenv("RECOMP_TEST_NEXT_RELEASE");
    HANDLE t;
    if (test && *test) {                /* tests: the answer from a file, once */
        if (!test_read) { test_read = 1; read_test_file(test); }
        return;
    }
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) return;
    t = CreateThread(NULL, 0, fetch_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else InterlockedExchange(&g_busy, 0);
}

static int info(CrInfo *out)
{
    int have;
    AcquireSRWLockShared(&g_lock);
    *out = g_info;
    have = g_have;
    ReleaseSRWLockShared(&g_lock);
    return have;
}

/* 0..CR_AFTER_SPREAD_S, this PC's own for this run. */
static int spread(void)
{
    static int value = -1;
    if (value < 0)
        value = (int)(((unsigned)GetCurrentProcessId() * 2654435761u ^ (unsigned)GetTickCount()) %
                      (unsigned)(CR_AFTER_SPREAD_S + 1));
    return value;
}

int content_release_frame(int check_due, int in_fight)
{
    CrInfo i;
    const char *test = getenv("RECOMP_TEST_NEXT_RELEASE");
    if (check_due || (test && *test && !g_have)) ask();
    if (!info(&i)) return 0;
    if (!cr_after_due(&g_notice, &i, (long long)time(NULL), spread(), in_fight)) return 0;
    fprintf(stderr, "[CONTENT] the release time has passed: checking the catalogue now\n");
    fflush(stderr);
    ask();                              /* and the schedule after it */
    return 1;
}

/* The PC shows 12-hour times (AM/PM). */
static int clock_12h(void)
{
    static int known = -1;
    if (known < 0) {
        wchar_t v[4] = L"";
        known = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_ITIME, v, 4) > 0 && v[0] == L'0';
    }
    return known;
}

/* The release's hour and minute on this PC's clock, and how many calendar days from today it is. */
static void local_clock(const CrInfo *i, long long now, int *days, int *hour, int *minute)
{
    time_t at = (time_t)cr_local_release(i), t = (time_t)now;
    struct tm a, n;
    *days = *hour = *minute = 0;
    if (localtime_s(&a, &at) || localtime_s(&n, &t)) return;
    *hour = a.tm_hour;
    *minute = a.tm_min;
    /* both at noon of their day: the difference in days whatever daylight saving does in between */
    a.tm_hour = n.tm_hour = 12;
    a.tm_min = n.tm_min = a.tm_sec = n.tm_sec = 0;
    a.tm_isdst = n.tm_isdst = -1;
    *days = (int)((mktime(&a) - mktime(&n) + 43200) / 86400);
    if (*days < 0) *days = 0;
}

static void lines(const CrInfo *i, long long now, int days, int hour, int minute, int h12, char *head, int head_size,
                  char *detail, int detail_size)
{
    char kinds[160], in[32];
    if (head && head_size > 0) cr_headline(days, hour, minute, h12, head, head_size);
    if (detail && detail_size > 0) {
        cr_kinds_text(i, kinds, (int)sizeof kinds);
        cr_in_text(cr_remaining(i, now), in, (int)sizeof in);
        snprintf(detail, (size_t)detail_size, "%s - %s", kinds, in);
    }
}

int content_release_lines(char *head, int head_size, char *detail, int detail_size)
{
    CrInfo i;
    long long now = (long long)time(NULL);
    int days, hour, minute;
    if (head && head_size > 0) head[0] = 0;
    if (detail && detail_size > 0) detail[0] = 0;
    if (!info(&i) || !cr_shows(&i, now)) return 0;
    local_clock(&i, now, &days, &hour, &minute);
    lines(&i, now, days, hour, minute, clock_12h(), head, head_size, detail, detail_size);
    return 1;
}

void content_release_sample_lines(char *head, int head_size, char *detail, int detail_size)
{
    CrInfo i;
    memset(&i, 0, sizeof i);
    i.server_time = i.local_time = 1000;
    i.release_at = 1000 + 2 * 3600 + 15 * 60;
    i.count = 4;
    i.kinds[CR_NEW_FIGHTER] = 3;
    i.kinds[CR_SKIN] = 1;
    lines(&i, 1000, 0, 17, 0, 0, head, head_size, detail, detail_size);
}

int content_release_toast(char *out, int size, int on_main_menu)
{
    CrInfo i;
    long long now = (long long)time(NULL);
    int days, hour, minute;
    if (!info(&i) || !cr_toast(&g_notice, &i, now, GetTickCount64(), on_main_menu)) return 0;
    local_clock(&i, now, &days, &hour, &minute);
    if (out && size > 0) cr_toast_text(&i, now, days, hour, minute, clock_12h(), out, size);
    return 1;
}
