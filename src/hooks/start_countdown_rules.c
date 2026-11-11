/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The start countdown's rules (see start_countdown.h). Pure C: no Windows, no game memory. */
#include <string.h>
#include "start_countdown.h"

int start_countdown_parse(const char *value)
{
    if (!value || !*value) return 1;
    return strcmp(value, "0") && strcmp(value, "off") && strcmp(value, "false") && strcmp(value, "no");
}

int start_countdown_held_at(int enabled, uint32_t step)
{
    return enabled && step >= 1u && step <= START_COUNTDOWN_STEPS;
}

int start_countdown_shown(int enabled, uint32_t step, float *t)
{
    uint32_t i;
    if (t) *t = 0.0f;
    if (!enabled || step < 1u || step > START_COUNTDOWN_STEPS + START_COUNTDOWN_FIGHT) return -1;
    i = step - 1u;
    if (step > START_COUNTDOWN_STEPS) {
        if (t) *t = (float)(step - START_COUNTDOWN_STEPS - 1u) / (float)(START_COUNTDOWN_FIGHT - 1u);
        return 0;
    }
    if (t) *t = (float)(i % START_COUNTDOWN_BEAT) / (float)(START_COUNTDOWN_BEAT - 1u);
    return (int)(START_COUNTDOWN_STEPS / START_COUNTDOWN_BEAT - i / START_COUNTDOWN_BEAT);
}
