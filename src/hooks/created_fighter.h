/* Created (Story mode) fighters online. See created_fighter.c and
 * docs/research/created-fighter-online.md. */
#ifndef DEFJAM_CREATED_FIGHTER_H
#define DEFJAM_CREATED_FIGHTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP_FIGHTER_ID   99      /* ControllerSetup fighter byte for "this pad's created fighter" */
#define CP_NAME_BYTES   0x20    /* name field at profile+0xC */
#define CP_NAME_MAX     8       /* what the game's user ID screen allows */
#define CP_PAYLOAD      0x127   /* name, profile +0xFC..+0x143, flag, recipe, options */
#define CP_REC_LEN      (1 + CP_PAYLOAD + 4)    /* version, payload, CRC-32 (netplay/relay/protocol_created.go) */
#define CP_REC_VERSION  1

/* ---- the record (pure functions, no guest memory) ---- */

/* A record from a profile image (profile offsets: name +0xC, stats +0xFC, flag +0x4D8A,
 * recipe +0x5564, options +0x55FC; the buffer holds at least 0x5622 bytes). Sealed. */
void cp_record_from_profile(const uint8_t *profile, uint8_t rec[CP_REC_LEN]);
/* Version and CRC are right. */
int cp_record_sealed(const uint8_t rec[CP_REC_LEN]);
uint32_t cp_record_crc(const uint8_t rec[CP_REC_LEN]);
/* Every PC turns a received record into the same playable one: 0 when it cannot be played (bad
 * shape, no name, not a created archetype, no style); otherwise 1 and `out` holds the record with
 * the name cut to printable ASCII, voice, styles, stats, move keys and recipe kept to what the
 * game has (unknown database keys become the defaults), resealed. Deterministic. */
int cp_record_sanitize(const uint8_t in[CP_REC_LEN], uint8_t out[CP_REC_LEN]);
/* The record's name (NUL-terminated, at most CP_NAME_MAX characters). */
void cp_record_name(const uint8_t rec[CP_REC_LEN], char *out, int size);
/* The record's fighting styles in words ("KICKBOXING / STREETFIGHTING"), from its archetype's
 * fighter.xml name; "" if unknown. */
const char *cp_record_style(const uint8_t rec[CP_REC_LEN]);
/* The effective stats Battle mode computes from the record (sub_0001EE80). */
void cp_record_stats(const uint8_t rec[CP_REC_LEN], int out[6]);

/* ---- the player's own ---- */

/* Every Story save of the save area with a created fighter, sanitized, newest first (or, for
 * tests, RECOMP_CP_RECORD: profile dumps or user ID saves separated by ';', in that order). Read
 * once, then again after created_fighter_reload(). */
#define CP_LOCAL_MAX    8
typedef struct CpLocal {
    uint8_t rec[CP_REC_LEN];        /* the checked record */
    char name[CP_NAME_MAX + 1];     /* the fighter's name (the user ID) */
    char key[260];                  /* which save: "<user ID folder>/<file>" (tests: the path) */
    char saved[20];                 /* the save's time, "2026-10-28 14:30" local; "" for tests */
} CpLocal;
/* Copies up to `max` entries into `out` (NULL: none); returns how many there are. */
int cp_local_list(CpLocal *out, int max);
/* The player's pick: the entry whose save the setting online.created names, else the newest
 * (0); RECOMP_CP_PICK=<index> overrides it for tests. -1 when there is none. */
int cp_local_pick(void);
/* Picks entry `index` and remembers its save in the setting (saved to the settings file).
 * 0 if there is no such entry. */
int cp_local_set_pick(int index);
/* The picked record (CP_REC_LEN bytes). 1 if there is one. */
int cp_local_record(uint8_t rec[CP_REC_LEN]);
void created_fighter_reload(void);
/* The remembered pick (setting online.created; "" none): read and written through
 * the settings file (pc_settings.c). */
void cp_pick_setting_get(char *out, int size);
void cp_pick_setting_set(const char *key);

/* ---- in a match (game thread) ---- */

/* Guest address of profile `index` (0..15), 0 before the profiles exist. */
uint32_t created_fighter_profile(int index);
/* A new automatch sequence: no pad plays a record yet. */
void created_fighter_begin(void);
/* Puts the record into a spare profile slot, links the pad to it and clears the pad's built
 * look; returns CP_FIGHTER_ID, or -1 (no pad, no slot). Call between the automatch sequence's
 * SetBMCurrentUserIndex and ControllerSetup. */
int created_fighter_inject(int slot, int pad, const uint8_t rec[CP_REC_LEN]);
/* Before each front-end call: unlinks the injected profiles from their pads before the reward
 * step (which would save them as user IDs). Always 0. */
int created_fighter_before_funccall(const char *text);
/* Every fight step (netplay_on_step): unlinks the pads once every created fighter's look is built. */
void created_fighter_on_step(void);

/* Tests (RECOMP_TEST_CP): the fighter id to set up for the slot after injecting, or -1. */
int created_fighter_test_slot(int slot, int pad);

#ifdef __cplusplus
}
#endif

#endif
