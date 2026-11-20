/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Recording mode and the slow-motion switch: the pure rules (see recording_mode.h). */
#include <stdio.h>
#include "recording_mode.h"

#define NOTE_MS 2000ull

float recording_slow_motion_scale(float v, int allowed)
{
    if (!allowed && v > 0.0f && v < 1.0f) return 1.0f;
    return v;
}

int recording_slow_motion_allowed(int online, int setting_on)
{
    return online || setting_on;
}

int recording_mode_note_text(int on, unsigned long long since, unsigned long long now, const char *key,
                             char *out, int size)
{
    if (!since || now < since || now - since >= NOTE_MS || !out || size <= 0) return 0;
    snprintf(out, (size_t)size, "Recording mode %s: %s", on ? "on" : "off", key && *key ? key : "F10");
    return 1;
}
