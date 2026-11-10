/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The first Toughness milestone of a profile's fighter ([gameplay] toughness_fix).
 *
 * A retail bug. The post-match reward pass (guest 0x00064ED0, once per slot) adds the match's
 * statistics to the milestone counters of the profile linked to the slot's pad, then recomputes
 * the fighter's bonus stats from them (0x00014C20). Every counter is added to, except the one at
 * profile +0x74: at 0x00064F78 the game stores (mov, where the others add) 1 when the slot won a
 * match with more than two fighters, else 0. The milestone test at 0x00014F8D wants that word to
 * reach 10 for +1 bonus Toughness (profile +0x120), so no fighter can ever earn it: the count is
 * back at 0 or 1 after every match.
 *
 * The fix makes the store an add (saturating), around the generated body: the count before the
 * pass plus the 0 or 1 the pass wrote. When the sum reaches 10 the bonus stats are recomputed once
 * more, so the point shows at once. Nothing else changes: the save format, the other counters and
 * the fight itself are as before, and an online record's bonus stays within the limits the clients
 * and the relay already allow (styles + 2 a stat). RECOMP_TOUGHNESS_FIX=0, or the setting off,
 * restores the retail store. The wrapper is in src/recomp_manual.c. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp/gen/recomp_types.h"
#include "recomp_settings.h"
#include "hooks/guest_call.h"
#include "toughness_fix.h"

#define MANAGERS        0x002FB148u
#define GAMEDATA_SLOT   0x18u
#define GS_MODE         0x344u          /* 1: Battle (a profile per pad), 0: Story (one slot) */
#define GS_SLOT_PORT    0x1AABu         /* s8 per slot, stride 5: the slot's pad port */
#define GS_PAD_PROFILE  0x1ABDu         /* s8 per port: profile index, -1 none */
#define SLOT_BASE       0x177Cu
#define SLOT_STRIDE     0x5950u
#define PROFILE_SLOTS   16
#define S_MULTI_WINS    0x74u           /* u16: wins of matches with more than two fighters */
#define S_BONUS_TOUGH   0x120u
#define MILESTONE       10u

extern void sub_00014C20(void);         /* thiscall on a profile slot: recompute its bonus stats */

static uint32_t s_profile;              /* the profile the pass about to run will handle, 0 none */
static uint16_t s_before;
static int s_slot;

/* The pass's own lookup (guest 0x0001E8F0, a pure read): the profile of a slot's pad. */
static uint32_t slot_profile(uint32_t gs, int slot)
{
    uint32_t mode = MEM32(gs + GS_MODE);
    int port, index;
    if (mode == 1) {
        port = (int8_t)MEM8(gs + GS_SLOT_PORT + (uint32_t)slot * 5u);
        if (port < 0) return 0;
    } else if (mode == 0) {
        if (MEM32(MEM32(gs) + 0x14) != (uint32_t)slot) return 0;
        port = (int8_t)MEM8(gs + GS_SLOT_PORT);
        if (port < 0) return 0;         /* the game would read the byte below the table */
    } else
        return 0;
    index = (int8_t)MEM8(gs + GS_PAD_PROFILE + (uint32_t)port);
    if (index < 0 || index >= PROFILE_SLOTS || !MEM32(gs + 4)) return 0;
    return MEM32(gs + 4) + SLOT_BASE + (uint32_t)index * SLOT_STRIDE;
}

void toughness_fix_before_rewards(int slot)
{
    uint32_t gs = MEM32(MANAGERS + GAMEDATA_SLOT * 4u);
    s_profile = 0;
    if (!gs || slot < 0 || slot > 3) return;
    s_profile = slot_profile(gs, slot);
    s_slot = slot;
    if (s_profile) {
        /* RECOMP_TEST_TOUGHNESS_WINS=9 (tests): the count a profile starts its first pass with */
        static int poked;
        const char *test = getenv("RECOMP_TEST_TOUGHNESS_WINS");
        if (test && !poked) {
            poked = 1;
            MEM16(s_profile + S_MULTI_WINS) = (uint16_t)atoi(test);
            fprintf(stderr, "[TOUGHNESS] test: slot %d's profile starts with %d wins\n", slot, atoi(test));
        }
        s_before = MEM16(s_profile + S_MULTI_WINS);
    }
}

void toughness_fix_after_rewards(void)
{
    static unsigned told;
    uint32_t p = s_profile, sum;
    uint16_t won;
    int on;
    s_profile = 0;
    if (!p) return;
    won = MEM16(p + S_MULTI_WINS);      /* what the pass stored: 1 a win with more than two fighters, else 0 */
    on = recomp_settings_get("gameplay", "toughness_fix", 1);
    sum = on ? (uint32_t)s_before + won : won;
    if (sum > 0xFFFFu) sum = 0xFFFFu;
    if (on && sum != won) {
        MEM16(p + S_MULTI_WINS) = (uint16_t)sum;
        if (sum >= MILESTONE)           /* the pass's own recompute saw 0 or 1 */
            guest_call(sub_00014C20, p, 0, NULL);
    }
    if (told++ < 64)
        fprintf(stderr, "[TOUGHNESS] slot %d: multi-fighter wins %u -> %u (%s), bonus toughness %d\n", s_slot,
                (unsigned)s_before, (unsigned)sum, on ? "fix on" : "retail", (int)MEM32(p + S_BONUS_TOUGH));
}
