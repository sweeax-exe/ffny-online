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

/* The player's created fighter, sanitized: the newest Story save of the save area with a created
 * fighter (or RECOMP_CP_RECORD, a profile dump, for tests). 1 if there is one. Read once, then
 * on created_fighter_reload(). */
int cp_local_record(uint8_t rec[CP_REC_LEN]);
void created_fighter_reload(void);

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
