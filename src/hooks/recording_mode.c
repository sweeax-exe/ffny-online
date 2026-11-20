/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Recording mode and the slow-motion switch: the game side (see recording_mode.h). The key and the pad
 * buttons are read by the overlay (pc_ui.cpp), which also leaves its own fight drawings out; the game's HUD
 * draw and the time scale are wrapped in src/recomp_manual.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recording_mode.h"
#include "recomp_settings.h"
#include "patches/online_guard.h"

static volatile int s_on;
static volatile unsigned long long s_since;
static volatile int s_slow_motion = 1;
static volatile int s_read;

/* RECOMP_RECORDING_MODE=1 starts with it on (tests, or a capture set up ahead). */
int recording_mode_on(void)
{
    static int env_read;
    if (!env_read) {
        const char *e = getenv("RECOMP_RECORDING_MODE");
        env_read = 1;
        if (e && e[0] == '1') s_on = 1;
    }
    return s_on;
}

void recording_mode_toggle(unsigned long long now)
{
    s_on = !recording_mode_on();
    s_since = now ? now : 1;
    fprintf(stderr, "[REC] recording mode %s\n", s_on ? "on (HUD and overlays hidden)" : "off");
    fflush(stderr);
}

int recording_mode_toast(char *out, int size, unsigned long long now)
{
    char key[32];
    recomp_settings_get_text("recording", "key", key, sizeof(key), "F10");
    return recording_mode_note_text(s_on, s_since, now, key, out, size);
}

void recording_slow_motion_apply_settings(void)
{
    s_slow_motion = recomp_settings_get("gameplay", "slow_motion", 1) != 0;
    s_read = 1;
}

int recording_slow_motion_allowed_now(void)
{
    if (!s_read) recording_slow_motion_apply_settings();
    return recording_slow_motion_allowed(ffny_online_locked(), s_slow_motion);
}
