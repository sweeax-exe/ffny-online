/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The start countdown's game side (see start_countdown.h).
 *
 * Where the fight starts: CLogic (0x349760, state at +0x2D820) plays the venue's intro (state 1) and
 * the fighters' (state 3) without simulation steps; the steps (sub_00087700) begin in state 5 (state
 * 5 from step 1 in the runs of 2026-11-11). The match phase word goes from 0 to 1 at the first step,
 * the game's own grey "FIGHT!" lettering is up from that step for about a second and a half, and the
 * fighters read their pads from that step on: a fighter could run and hit while the fight was still
 * being called (offline the CPU's first hit came at step 77). Step 1 is the first step after the
 * intro cameras, the same point on every PC. The overlay covers that lettering with the numbers
 * (fight_hud_ui.inc) and calls its own FIGHT! when the hold ends.
 *
 * The hold: sub_001BAA00 reads the fighter's pad entry, 0x3B8F48 + 0x20 * pad, pad = the word at
 * 0x3B9098 + 4 * (record +0x28); human and CPU input both come through that table
 * (docs/research/fighter-record-fields.md 4). While the step is held, words 0..6 of that entry are
 * set to 0, the entry online play uses for "no input" (its first delay steps). A button held
 * through the countdown counts as pressed at the first free step, on every PC alike. */
#include <stdio.h>
#include <stdlib.h>
#include "host.h"
#include "recomp/gen/recomp_types.h"
#include "start_countdown.h"
#include "recomp_settings.h"

#define ACTORS          0x003B92E0u
#define STRIDE          0x12E8u
#define F_SLOT          0x28u
#define PAD_TABLE       0x003B8F48u     /* 4 entries of 0x20 bytes */
#define PAD_MAP         0x003B9098u     /* the pad entry of each controller slot */
#define PADS            4u
#define G_LOGIC_STATE   0x00376F80u     /* CLogic + 0x2D820 */
#define LOG_STEPS       300u            /* CLogic's state changes logged over the first steps */

static int s_enabled = -1;
static uint32_t s_steps, s_told, s_logic, s_match;
static int s_offline_on, s_online, s_said;   /* [gameplay] offline_countdown at the setup; the fight went online */

int start_countdown_enabled(void)
{
    if (s_enabled < 0) {
#if defined(_WIN32)
        const char *v = getenv("RECOMP_START_COUNTDOWN");
        s_enabled = start_countdown_parse(v);
        if (!s_enabled) fprintf(stderr, "[COUNTDOWN] off (RECOMP_START_COUNTDOWN=%s)\n", v);
#else
        /* the fight overlays that draw it are Windows only for now (online_screen.cpp): no hold
         * without the numbers */
        s_enabled = 0;
#endif
    }
    return s_enabled;
}

void start_countdown_match_start(void)
{
    s_steps = 0;
    s_told = 0;
    s_logic = 0xFFFFFFFFu;
    s_match++;
    s_offline_on = recomp_settings_get("gameplay", "offline_countdown", 0) != 0;
    s_online = 0;
    s_said = 0;
}

int start_countdown_active(int online)
{
    if (online) s_online = 1;
    return start_countdown_applies(start_countdown_enabled(), s_online, s_offline_on);
}

void start_countdown_step(int rerun)
{
    uint32_t st;
    if (rerun) return;
    s_steps++;
    if (s_steps > LOG_STEPS || !start_countdown_enabled()) return;
    st = MEM32(G_LOGIC_STATE);
    if (st != s_logic) {
        fprintf(stderr, "[COUNTDOWN] fight %u: CLogic state %d from step %u\n", (unsigned)s_match, (int)st,
                (unsigned)s_steps);
        s_logic = st;
    }
}

uint32_t start_countdown_offline_step(void) { return s_steps; }

void start_countdown_hold(uint32_t actor, uint32_t step, int online)
{
    uint32_t slot, pad, base, i;
    if (!start_countdown_active(online)) {
        if (!s_said && start_countdown_enabled() && step >= 1u) {
            s_said = 1;
            fprintf(stderr, "[COUNTDOWN] fight %u: offline, no countdown ([gameplay] offline_countdown off)\n",
                    (unsigned)s_match);
        }
        return;
    }
    if (actor < ACTORS || (actor - ACTORS) % STRIDE) return;
    if (!start_countdown_held_at(1, step)) {
        if (s_told == 1u && step > START_COUNTDOWN_STEPS) {
            s_told = 2u;
            fprintf(stderr, "[COUNTDOWN] fight %u: FIGHT at step %u, the fighters' input is free\n",
                    (unsigned)s_match, (unsigned)step);
        }
        return;
    }
    slot = MEM32(actor + F_SLOT);
    if (slot >= 8u) return;
    pad = MEM32(PAD_MAP + slot * 4u);
    if (pad >= PADS) return;
    base = PAD_TABLE + pad * 0x20u;
    for (i = 0; i < 7u; i++) MEM32(base + i * 4u) = 0;
    if (!s_told) {
        s_told = 1u;
        fprintf(stderr, "[COUNTDOWN] fight %u: input held at rest for steps 1..%u (from step %u)\n",
                (unsigned)s_match, (unsigned)START_COUNTDOWN_STEPS, (unsigned)step);
    }
}
