/* Created (Story mode) fighters in Battle setups. See created_fighter.c and
 * docs/research/created-fighter-online.md. */
#ifndef DEFJAM_CREATED_FIGHTER_H
#define DEFJAM_CREATED_FIGHTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP_FIGHTER_ID   99      /* ControllerSetup fighter byte for "this pad's created fighter" */
#define CP_NAME_BYTES   0x20    /* name field at profile+0xC */

/* Guest address of profile `index` (0..15), 0 before the profiles exist. */
uint32_t created_fighter_profile(int index);

/* Copy the record subset Battle reads (name, stats, moves, voice, style, archetype, flag,
 * recipe, options) from one profile to another. */
void created_fighter_copy_record(uint32_t dst, uint32_t src);
/* The same from a host copy of a whole profile (offsets as in the guest). */
void created_fighter_write_record(uint32_t dst, const uint8_t *profile);

/* Tests (RECOMP_TEST_CP): called by the automatch sequence for each slot after the pads were
 * bound; returns the fighter id to set up for the slot (99) after injecting, or -1. */
int created_fighter_test_slot(int slot, int pad);

/* Before each front-end call (automatch_hold_funccall): unlinks the injected profile from its
 * pad before the reward step can save it. Always 0 (never holds the call). */
int created_fighter_before_funccall(const char *text);

#ifdef __cplusplus
}
#endif

#endif
