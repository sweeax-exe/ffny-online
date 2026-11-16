/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Run the Streets (build 86, closed test): the open-world mode's door. Who may see it (the account service's
 * feature access), which map it plays (the official Run the Streets map from the signed catalogue), and the offline
 * fight against the CPU. The fight's rules are openworld.c's Big Arena. See docs/run-the-streets.md. */
#ifndef DEFJAM_RTS_H
#define DEFJAM_RTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The signed-in account may see and play Run the Streets (GET /api/features "run_the_streets", asked in the
 * background and cached: a yes 5 minutes, a no 1 minute, asked again at once when the account changes). Signed out,
 * no answer yet or no access: 0, and the mode stays completely hidden. Tests: RECOMP_RTS_ACCESS=1 grants, =0
 * refuses, without asking anyone (as RECOMP_STUDIO_BUTTON for Studio). Call from any thread; cheap. */
int rts_access(void);
/* The access answer's reason ("role", "owner", "no-role", "test", ...), "" none yet. */
const char *rts_access_reason(void);

/* The Run the Streets map on this PC: the [pack.*] of content.ini with official=1 and rts_map=1 that the launcher
 * composed and applied (lowest catalogue order first). Tests: RECOMP_RTS_VENUE=<feID>[,<code>] names a venue
 * instead. 1 and its venue feID, code ("28A") and pack id; 0: no map, the mode cannot start. */
typedef struct RtsMap {
    int fe_id;
    char code[8];
    char id[96];
    char name[64];
    char version[40];
} RtsMap;
int rts_map(RtsMap *out);

/* The spawn points of the map (x, z), up to max: the official map's <data>/mods/<id>/bigarena.json (its "spawns",
 * when its "venue" is V2BG_<code>), else a bigarena json the launcher serves (assets/bigarena_<code>.json or
 * assets/bigarena.json naming V2BG_<code>), else Knox and Jam 0.1's sixteen street spawns for a knox-and-jam pack. 0: none (openworld.c then uses its ring). Same content, same points on every
 * PC (the content print). */
int rts_spawns(const RtsMap *m, float *x, float *z, int max);

/* An offline Run the Streets fight: the player on pad 0 with `fighter` (Battle-mode ID) and skin, against `cpus`
 * (2..3) CPU fighters `cpu_fighters[]` (-1: drawn by the game's own random pick), on the map. Returns 0 when the map
 * or access is missing. The fights after it (the rematch) stay Run the Streets until the main menu shows again. */
int rts_offline_start(int fighter, int skin, int cpus, const int *cpu_fighters);
/* 1 from rts_offline_start until the main menu is back after its fights (the menu hook calls rts_main_menu()). */
int rts_offline_armed(void);
/* The match set-up (openworld.c): this offline fight is a Run the Streets one (and marks the fights as begun). */
int rts_offline_fight_starts(void);
void rts_main_menu(void);

#ifdef __cplusplus
}
#endif

#endif
