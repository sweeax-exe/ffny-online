/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Milestone progress: the game side (milestones.h). Read-only: profiles are copied out of guest
 * memory, nothing is written back, and the save is never touched.
 *
 *   which profile   the Story's current one (save block +0x5AC7C, as IsStoryComplete and MemCard.Save
 *                   read it), or on the Battle user-ID screen the ID it shows (its script asks
 *                   Game.GetMilestone(id, n) for the ID card's badges whenever the shown ID changes)
 *   the toast       around the post-match reward pass of a slot (toughness_fix.c calls in): the rows
 *                   before and after; a stat milestone reached by this match is announced over the
 *                   summary for eight seconds. One log line per pass ([MILESTONE]) says what the pass
 *                   added, so an F9 report shows whether a match counted.
 *
 * RECOMP_TEST_MILESTONE_TOAST=<text>: the toast shows that text once a user ID is loaded (tests). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif
#include "recomp/gen/recomp_types.h"
#include "recomp_settings.h"
#include "milestones.h"

#define MANAGERS        0x002FB148u
#define GAMEDATA_SLOT   0x18u
#define SLOT_BASE       0x177Cu
#define SLOT_STRIDE     0x5950u
#define PROFILE_SLOTS   16
#define CURRENT_PROFILE 0x5AC7Cu        /* save block +: the current profile index, -1 none */
#define TOAST_MS        8000u

static volatile int g_battle_id = -1;

static uint64_t now_ms(void)
{
#if defined(_WIN32)
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

#if defined(_WIN32)
static SRWLOCK g_lock = SRWLOCK_INIT;
static void lock(void) { AcquireSRWLockExclusive(&g_lock); }
static void unlock(void) { ReleaseSRWLockExclusive(&g_lock); }
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void lock(void) { pthread_mutex_lock(&g_lock); }
static void unlock(void) { pthread_mutex_unlock(&g_lock); }
#endif

static char g_toast[160];
static uint64_t g_toast_since;          /* 0: nothing to show; else when the overlay first showed it */
static int g_toast_new;

static uint32_t save_block(void)
{
    uint32_t gs = MEM32(MANAGERS + GAMEDATA_SLOT * 4u);
    return gs ? MEM32(gs + 4u) : 0;
}

int milestones_current_profile(void)
{
    uint32_t save = save_block();
    int32_t index;
    if (!save) return -1;
    index = (int32_t)MEM32(save + CURRENT_PROFILE);
    return index >= 0 && index < PROFILE_SLOTS ? index : -1;
}

int milestones_read_profile(int index, uint8_t *buf, char *name, int name_size)
{
    uint32_t save = save_block(), p;
    int i;
    if (!save || index < 0 || index >= PROFILE_SLOTS || !buf) return 0;
    p = save + SLOT_BASE + (uint32_t)index * SLOT_STRIDE;
    if (!MEM8(p + 0xC)) return 0;       /* no user ID in the slot */
    memcpy(buf, (const void *)XBOX_PTR(p), MS_PROFILE_BYTES);
    if (name && name_size > 0) {
        for (i = 0; i < name_size - 1 && i < 16; i++) {
            char c = (char)buf[0xC + i];
            if (!c) break;
            name[i] = c >= 32 && c < 127 ? c : '?';
        }
        name[i] = 0;
    }
    return 1;
}

void milestones_on_funccall(const char *text)
{
    if (!text) return;
    if (!strncmp(text, "Game.GetMilestone(", 18)) {
        int id = atoi(text + 18);
        g_battle_id = id >= 0 && id < PROFILE_SLOTS ? id : -1;
    } else if (!strncmp(text, "Control.GetScreenInfo(", 22) && !strstr(text, "battleID")) {
        g_battle_id = -1;               /* left the user-ID screen: the next visit asks again */
    }
}

int milestones_battle_profile(void) { return g_battle_id; }

/* ---- around the reward pass (game thread) ---- */

static MilestoneRow g_before[MS_MAX_ROWS];
static int g_before_n;
static uint32_t g_before_profile;
static uint32_t g_before_counts[6];     /* punches, kicks, speed a, speed b, tough wins, tough comeback */

static int rows_of(uint32_t profile, MilestoneRow *rows, uint32_t *counts)
{
    static uint8_t copy[MS_PROFILE_BYTES];
    memcpy(copy, (const void *)XBOX_PTR(profile), MS_PROFILE_BYTES);
    if (counts) {
        counts[0] = MEM32(profile + 0x68);
        counts[1] = MEM32(profile + 0x6C);
        counts[2] = MEM16(profile + 0x70);
        counts[3] = MEM16(profile + 0x72);
        counts[4] = MEM16(profile + 0x74);
        counts[5] = MEM16(profile + 0x76);
    }
    return milestones_rows(copy, MS_PROFILE_BYTES, recomp_settings_get("gameplay", "toughness_fix", 1), rows,
                           MS_MAX_ROWS);
}

void milestones_before_rewards(uint32_t profile)
{
    g_before_profile = profile;
    g_before_n = profile ? rows_of(profile, g_before, g_before_counts) : 0;
}

void milestones_after_rewards(uint32_t profile)
{
    static unsigned told;
    MilestoneRow after[MS_MAX_ROWS];
    uint32_t c[6];
    char text[160];
    int n, k;
    if (!profile || profile != g_before_profile) { g_before_profile = 0; return; }
    g_before_profile = 0;
    n = rows_of(profile, after, c);
    k = milestones_newly_reached(g_before, g_before_n, after, n, text, sizeof(text));
    if (told++ < 64)
        fprintf(stderr, "[MILESTONE] profile %u: punches %u -> %u, kicks %u -> %u, speed %u/%u -> %u/%u, "
                "toughness %u/%u -> %u/%u, reached %d -> %d%s%s\n",
                (unsigned)((profile - SLOT_BASE - save_block()) / SLOT_STRIDE),
                (unsigned)g_before_counts[0], (unsigned)c[0], (unsigned)g_before_counts[1], (unsigned)c[1],
                (unsigned)g_before_counts[2], (unsigned)g_before_counts[3], (unsigned)c[2], (unsigned)c[3],
                (unsigned)g_before_counts[4], (unsigned)g_before_counts[5], (unsigned)c[4], (unsigned)c[5],
                milestones_reached(g_before, g_before_n), milestones_reached(after, n), k ? ": " : "", k ? text : "");
    if (!k) return;
    lock();
    snprintf(g_toast, sizeof(g_toast), "%s", text);
    g_toast_new = 1;
    unlock();
}

int milestones_toast(char *out, int size)
{
    int shown = 0;
    uint64_t now = now_ms();
    static int test_done;
    if (!test_done && milestones_current_profile() >= 0) {    /* once a user ID is loaded */
        const char *t = getenv("RECOMP_TEST_MILESTONE_TOAST");
        test_done = 1;
        if (t && *t) {
            lock();
            snprintf(g_toast, sizeof(g_toast), "%s", t);
            g_toast_new = 1;
            unlock();
        }
    }
    lock();
    if (g_toast_new) {
        g_toast_new = 0;
        g_toast_since = now ? now : 1;
    }
    if (g_toast_since && now - g_toast_since < TOAST_MS) {
        if (out && size > 0) snprintf(out, (size_t)size, "%s", g_toast);
        shown = 1;
    } else {
        g_toast_since = 0;
    }
    unlock();
    return shown;
}
