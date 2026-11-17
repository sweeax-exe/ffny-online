/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Run the Streets' social layer (build 86 closed test): chat bubbles and speaker icons over the fighters, proximity
 * voice and the chat docked bottom-left (rts_social_ui.inc draws; rules in rts_social_rules.c). This file is the
 * game's side: once a frame, after the render camera is set (openworld.c ow_camera), it copies the camera and the
 * fighters' positions to host memory, which the overlay (render thread) and the voice mixer (voice thread) read.
 * Read only: no game memory is written, nothing goes into a fight's input or checksums.
 *
 * Only in a Run the Streets fight (ow_rts_active()). Tests: RECOMP_RTS_SOCIAL_TEST=1 treats any Big Arena fight as
 * one (an FFA quick room with RECOMP_OW_ONLINE_TEST=1); RECOMP_RTS_VOICE_LOG=1 logs the voice gains every 2 s. */
#ifndef DEFJAM_RTS_SOCIAL_H
#define DEFJAM_RTS_SOCIAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RtsSocialView {
    int on;                 /* a Run the Streets fight runs and its steps run (the overlays may draw) */
    float eye[3], at[3];    /* the render camera as set this frame */
    float up[3];            /* its up vector (0x2DBF58) */
    float fov_deg, aspect;  /* the scene's lens: its viewport's field of view, widened as the widescreen patch does */
    float pos[4][3];        /* the fighters' positions (feet) */
    uint32_t present;       /* fighter slots in the fight (bit per slot) */
    int local;              /* this PC's fighter, -1 none (a spectator or a watcher: the camera's target hears) */
    uint32_t cpu;           /* build 88: the CPU fighters among them (record +0x937 bit 0) */
    float yaw[4];           /* build 88: their facings (+0x54, degrees; forward is (sin, cos) on x/z) */
    int roam_mode;          /* the local fighter's free roam mode (OW_ROAM_*; free when free roam is off) */
    int lock_target;        /* its target while engaged or locked (the lock marker), -1 none */
    unsigned long long at_ms;   /* GetTickCount64 of the copy */
} RtsSocialView;

/* The game thread, once a frame after the render camera is final (openworld.c). */
void rts_social_camera(void);
/* The last copy; 0 when none is fresh (no Run the Streets fight on screen). Any thread. */
int rts_social_view(RtsSocialView *out);
/* Build 88, the minimap: the map's walls as x/z segments (x0, z0, x1, z1), copied once per fight from the
 * collision planes (every upright plane's two farthest corners). Returns how many (up to max); 0 none yet. */
int rts_social_walls(float (*seg)[4], int max);

/* 1 while a Run the Streets fight (or a test's Big Arena one) runs: the rules for voice and the overlays. */
int rts_social_on(void);

/* Proximity voice for the talker in room slot `room_slot`: the gain (0..1) and the pan (-1..1) this PC hears them
 * at. 0 (gain 1, pan 0) outside a Run the Streets fight or for a talker without a fighter: voice as everywhere
 * else. The voice thread, every 20 ms block. */
int rts_social_voice(int room_slot, float *gain, float *pan);
/* 1: the voice output is to be stereo (a Run the Streets room, or the test switch). */
int rts_social_voice_stereo(void);

#ifdef __cplusplus
}
#endif

#endif
