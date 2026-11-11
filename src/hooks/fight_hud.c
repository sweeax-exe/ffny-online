/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* See fight_hud.h. One sample per simulation step feeds the PERFECT tracker (fight_hud_rules.c):
 * each fighter's health (+0x350 of its record, the value the online result report and the test
 * telemetry read), the match's phase word (1 fighting, 2 decided, 3 finished) and its winner slot.
 * The round starts at the first step the phase says "fighting" (after a START the word still
 * holds the last match's value for a moment).
 *
 * Rollback runs steps again with corrected inputs: those samples carry the step numbers of the
 * first run (netplay_fight_step), and the tracker lets them replace what a mispredicted step
 * showed. The step is sampled after netplay's own work, so a rollback done there is already in the
 * state read here. Nothing is written to the game.
 *
 * Tests only: RECOMP_TEST_PERFECT=1 calls every decided round with a winner a perfect (the
 * banner's pictures without a scripted flawless fight). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "recomp/gen/recomp_types.h"
#include "fight_hud.h"
#include "fight_hud_rules.h"
#include "netplay.h"

#define ACTORS          0x003B92E0u
#define STRIDE          0x12E8u
#define F_SLOT          0x28u           /* 0xFFFFFFFF: the slot is unused */
#define F_HEALTH        0x350u
#define F_CONTROL       0x937u          /* bit 0: CPU */
#define G_PHASE         0x003B8F30u     /* 0 start, 1 fighting, 2 decided, 3 finished */
#define G_TWIN          0x003B8F3Cu     /* the winner slot the decision writes first */
#define G_WINNER        0x003B8F40u     /* phase 3: the winner's slot */
#define G_FLAGS         0x003B9054u     /* bit 0x08: the match is over (endings that skip phases 2 and 3) */
#define G_RESULTS       0x003BE130u     /* the result history: 16-byte records, u16 code at +0xE */
#define G_RESULT_COUNT  0x003BE624u
#define RESULTS_MAX     ((G_RESULT_COUNT - G_RESULTS) / 0x10u)
#define G_MODE_MASK     0x003B907Eu     /* 2: a Team match (slots 0 and 1 against 2 and 3) */
#define CONFIRM_STEPS   20u             /* the log line: this long after the decision, past any rollback */

static FightPerfect s_perfect;
static uint32_t s_step;
static int s_fighting, s_logged, s_test = -1;
static volatile uint32_t s_round, s_mask, s_beat, s_active, s_cpu, s_live, s_shown_step;

static float f32(uint32_t address)
{
    uint32_t bits = MEM32(address);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t record(int slot) { return ACTORS + (uint32_t)slot * STRIDE; }

void fight_hud_match_start(void)
{
    fight_perfect_reset(&s_perfect);
    s_step = s_shown_step = 0;
    s_fighting = s_logged = 0;
    s_mask = s_live = 0;
    s_active = s_cpu = 0;
    s_round++;
}

void fight_hud_step(void)
{
    FightSample s;
    uint32_t phase = MEM32(G_PHASE), count, code = 0, w = 0xFFFFFFFFu, cpu = 0, mask, step;
    int slot, rerun = netplay_rerunning();
    if (!rerun) s_beat++;
    if (netplay_fight_step(&step)) s_step = step;
    else if (!rerun) s_step++;
    s_shown_step = s_step;
    memset(&s, 0, sizeof(s));
    s.step = s_step;
    for (slot = 0; slot < FIGHT_HUD_SLOTS; slot++) {
        uint32_t r = record(slot);
        if (MEM32(r + F_SLOT) == 0xFFFFFFFFu) continue;
        s.active |= 1u << slot;
        s.health[slot] = f32(r + F_HEALTH);
        if (MEM8(r + F_CONTROL) & 1u) cpu |= 1u << slot;
    }
    s_active = s.active;
    s_cpu = cpu;
    if (!s_fighting) {
        if (phase != 1u) return;        /* the last match's phase, or the round not begun */
        s_fighting = 1;
    }
    /* decided: as netplay.c reads it (the phase word; or the result record with the "over" flag
     * for the endings that skip the phases); a decisive draw has no winner */
    count = MEM32(G_RESULT_COUNT);
    if (count && count <= RESULTS_MAX) code = MEM16(G_RESULTS + (count - 1u) * 0x10u + 0xEu);
    if (phase == 3u) {
        s.decided = 1;
        w = MEM32(G_WINNER);
    } else if (phase == 2u || ((code & 1u) && (MEM32(G_FLAGS) & 8u))) {
        s.decided = 1;
        w = MEM32(G_TWIN);
    }
    if (s.decided && !(code & 4u) && w < FIGHT_HUD_SLOTS)
        s.winners = fight_winner_mask((int)w, MEM8(G_MODE_MASK) == 2u, s.active);
    s_live = !s.decided;
    fight_perfect_sample(&s_perfect, &s);
    mask = fight_perfect_mask(&s_perfect);
    if (s_test < 0) {
        const char *v = getenv("RECOMP_TEST_PERFECT");
        s_test = v && v[0] == '1' && !v[1];
        if (s_test) fprintf(stderr, "[FIGHTHUD] RECOMP_TEST_PERFECT=1 (tests): every round with a winner is a perfect\n");
    }
    if (s_test && s_perfect.decided_step >= 0) mask = s_perfect.winners;
    s_mask = mask;
    if (!s_logged && !rerun && s_perfect.decided_step >= 0 &&
        s_step >= (uint32_t)s_perfect.decided_step + CONFIRM_STEPS) {
        s_logged = 1;
        fprintf(stderr, "[FIGHTHUD] round %u decided at step %d: winners 0x%X, %s (health lost at step %d %d %d %d)\n",
                (unsigned)s_round, (int)s_perfect.decided_step, (unsigned)s_perfect.winners,
                mask ? "PERFECT" : "no perfect", (int)s_perfect.lost_step[0], (int)s_perfect.lost_step[1],
                (int)s_perfect.lost_step[2], (int)s_perfect.lost_step[3]);
    }
}

void fight_hud_view(FightHudView *out)
{
    out->round = s_round;
    out->perfect = s_mask;
    out->beat = s_beat;
    out->live = s_live;
    out->active = s_active;
    out->cpu = s_cpu;
    out->step = s_shown_step;
}

/* Called on the game thread from the step that reports the result: the tracker has the steps
 * before this one, and the winners' health is read as it stands now. */
uint32_t fight_hud_report_perfect(uint32_t winners)
{
    int slot;
    if (!winners || !s_perfect.armed || (winners & ~s_perfect.tracked)) return 0;
    for (slot = 0; slot < FIGHT_HUD_SLOTS; slot++) {
        if (!(winners & (1u << slot))) continue;
        if (s_perfect.lost_step[slot] >= 0) return 0;
        if (fight_perfect_under(&s_perfect, slot, f32(record(slot) + F_HEALTH))) return 0;
    }
    return winners;
}
