/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Team Tower Online: the relay's TOWER as the screens read it. See towers_online.h. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "towers_online.h"

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static TowerNetView g_view;
static char g_room[8];

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    return TRUE;
}
static void lock(void) { InitOnceExecuteOnce(&g_once, init_once, NULL, NULL); EnterCriticalSection(&g_cs); }
static void unlock(void) { LeaveCriticalSection(&g_cs); }

static int rd16(const uint8_t *b) { return b[0] | b[1] << 8; }

/* a string field: u8 length, the bytes */
static int rd_str(const uint8_t *b, int len, int *i, char *out, int size)
{
    int n;
    if (*i >= len) return 0;
    n = b[(*i)++];
    if (*i + n > len) return 0;
    snprintf(out, (size_t)size, "%.*s", n < size ? n : size - 1, (const char *)b + *i);
    *i += n;
    return 1;
}

void towers_online_on_tower(const char *room, const uint8_t *b, int len)
{
    TowerNetView v;
    int i = 0;
    if (!room || len < 24) return;
    memset(&v, 0, sizeof v);
    v.state = b[0];
    v.flags = b[1];
    v.tower = b[2];
    v.floor = rd16(b + 3);
    v.floors = rd16(b + 5);
    v.beaten = rd16(b + 7);
    v.opp[0] = rd16(b + 9);
    v.opp[1] = rd16(b + 11);
    v.venue = b[13];
    v.secs = rd16(b + 14);
    v.saved_floor = rd16(b + 16);
    v.last_floor = rd16(b + 18);
    v.last_how = b[20];
    v.voids = b[21];
    i = 22;
    if (!rd_str(b, len, &i, v.end, (int)sizeof v.end) || !rd_str(b, len, &i, v.why, (int)sizeof v.why)) return;
    if (v.opp[0] == 0xFFFF) v.opp[0] = -1;
    if (v.opp[1] == 0xFFFF) v.opp[1] = -1;
    v.valid = 1;
    v.at = GetTickCount64();
    lock();
    if (memcmp(&v, &g_view, offsetof(TowerNetView, at)) || strncmp(g_room, room, 7)) {
        fprintf(stderr, "[TOWERS] online: room %.7s state %d, floor %d of %d (beaten %d), CPUs %d + %d, venue %d, secs %d, "
                "saved %d, last %d how %d, voids %d%s%s%s%s\n", room, v.state, v.floor, v.floors, v.beaten, v.opp[0], v.opp[1],
                v.venue, v.secs, v.saved_floor, v.last_floor, v.last_how, v.voids, v.end[0] ? ", end " : "", v.end,
                v.why[0] ? ": " : "", v.why);
    }
    g_view = v;
    snprintf(g_room, sizeof g_room, "%.7s", room);
    unlock();
}

void towers_online_room_left(void)
{
    lock();
    memset(&g_view, 0, sizeof g_view);
    g_room[0] = 0;
    unlock();
}

int towers_online_view_net(TowerNetView *out)
{
    int ok;
    lock();
    *out = g_view;
    ok = g_view.valid;
    if (ok && out->secs > 0) {      /* the clocks count down between two TOWERs */
        unsigned long long gone = (GetTickCount64() - g_view.at) / 1000ull;
        out->secs = gone >= (unsigned long long)out->secs ? 0 : out->secs - (int)gone;
    }
    unlock();
    return ok;
}
