/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 99: recording mode and the offline slow-motion switch.
 *
 * Recording mode ([recording] key, F10 by default; [recording] pad, none by default) hides everything drawn
 * over the fight on this PC: the overlay's own fight drawings (name tags, streak, banners, chat, ping, notices,
 * the crown) and the game's own fight HUD (health and momentum bars, clock, combo and points texts, the
 * KO/FIGHT lettering). The HUD is left out by skipping its per-frame draw sub_000B5AD0 (src/recomp_manual.c):
 * drawing only, the HUD's update and the simulation run as before, so online matches stay in step.
 *
 * Slow motion ([gameplay] slow_motion, on by default): the fight's per-step time scale 0x3B8F2C (set by
 * sub_001A4900 from the camera director's scale CLogic+4 and the knockout ramp) slows the simulation itself,
 * so off is for offline fights only; while this PC is online it always stays on. */
#ifndef DEFJAM_RECORDING_MODE_H
#define DEFJAM_RECORDING_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The pure rules (recording_mode_rules.c, unit-tested natively). */
/* The time scale the fight uses: `v` itself unless slow motion is not allowed, then a slow value (between 0
 * and 1, exclusive) is full speed. 0 (the pause) and 1 stay as they are. */
float recording_slow_motion_scale(float v, int allowed);
/* Slow motion is allowed: always online, else the setting. */
int recording_slow_motion_allowed(int online, int setting_on);
/* The note shown for 2 s after a toggle: 1 and `out` filled while it is up. `key` is the key's name. */
int recording_mode_note_text(int on, unsigned long long since, unsigned long long now, const char *key,
                             char *out, int size);

/* The game side (recording_mode.c). */
int recording_mode_on(void);                        /* the HUD and the overlay's fight drawings are hidden */
void recording_mode_toggle(unsigned long long now); /* the key or the pad buttons */
int recording_mode_toast(char *out, int size, unsigned long long now);
void recording_slow_motion_apply_settings(void);    /* reads [gameplay] slow_motion */
int recording_slow_motion_allowed_now(void);

#ifdef __cplusplus
}
#endif

#endif
