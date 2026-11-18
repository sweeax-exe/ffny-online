/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* A new fighter's own voice (docs/research/new-fighter-voice.md).
 *
 * A fighter's speech is a voice number N: audio/speech2.viv holds per N three banks (in-fight lines, taunts,
 * crowd/environment lines: speech\<Character|Taunts|Enviro>\Def_Jam_Characters\NNN.hdr + .dat) and two streams
 * (speech\streams\intro\NNN.asf, the entrance line, and ...\extro\NNN.asf, the victory line). A stock fighter's
 * N comes from the XBE table 0x2DC5D0[coreID]; a new fighter keeps its template's coreID, so it would speak with
 * the template's voice. With its own voice, the pack's installer adds the five files at N = 40 + the fighter's
 * place index (new_fighter_places.h: 100..127 are 040..067, the 20 low IDs 068..087; stock voices are 000..039
 * and 500..538, so 040..087 are free) and writes N into the fighter's Character row
 * word 21 (speechID: copied by Game.StartGame, never read by the speech code). At match load, after the game
 * filled its per-slot speech rows and before it queues the banks (sub_0003DB00), the slot's voice number is
 * set to N when word 21 says so and all five files are in the speech archive the game opens; otherwise the slot
 * keeps the template's voice. Audio only: nothing here is read by the fight's simulation.
 *
 * Pure functions here (no game or host state), tested natively (tests/unit/test_fighter_voice.py). */
#ifndef DEFJAM_FIGHTER_VOICE_H
#define DEFJAM_FIGHTER_VOICE_H

#include <stddef.h>
#include <stdint.h>
#include "new_fighter_places.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FV_FIRST_VOICE 40               /* N for place 0 (feID 100) */
#define FV_SPEECH_ARCHIVE "audio\\speech2.viv"
#define FV_FILES 8                      /* three .hdr/.dat pairs and two streams */

/* The own voice number of new fighter fe_id (40..87: 40 + its place index), or -1. */
int fighter_voice_number(int fe_id);

/* The i-th (0..FV_FILES-1) archive entry name a voice needs, e.g. "speech\Taunts\Def_Jam_Characters\040.hdr". */
void fighter_voice_entry_name(int voice, int i, char *out, size_t n);

/* Whether a BIG archive directory (the archive's first bytes, at least its header size) lists every entry of
 * voice (names compared without case, / and \ alike, each entry with a size above zero). 1 yes, 0 no. */
int fighter_voice_entries_present(const uint8_t *dir, size_t n, int voice);

/* ---- own voices of community fighters beyond the places (fighter numbers 1000 and up, fighter_banks.h) ----
 *
 * The speech code addresses at most 200 voice numbers (its bank flags 0x33EA78[voice < 500 ? voice : voice - 400]),
 * so a numbered fighter has no number of its own: bank b (front-end ID 124 + b, player slot b) speaks with voice
 * 064 + b (the own voice of place 124 + b, which no place fighter holds). The composed speech archive has those
 * eight entries per bank voice (silence) and, per numbered fighter with an own voice, its files under its number
 * (setup/ffnycontent/unlimited.py): "speech\<bank>\Def_Jam_Characters\L<id>_<voice>.hdr" (a header per bank voice,
 * it names its voice), "...\L<id>.dat", "speech\streams\<intro|extro>\L<id>.asf". At the speech set-up the game's
 * in-memory copy of the archive's directory gets the bank voice's eight entries pointed (offset, size) at the bound
 * fighter's files, and the slot the bank voice. */

#define FV_BANK_FIRST_VOICE 64           /* bank 0's voice: FV_FIRST_VOICE + the place index of 124 */

/* The voice bank b (0..3) speaks with when its fighter has an own voice (64..67), or -1. */
int fighter_voice_bank_voice(int bank);

/* The entry numbered fighter id's own file has in place of the i-th entry of bank voice `voice`. */
void fighter_voice_numbered_entry_name(int id, int voice, int i, char *out, size_t n);

/* The directory record (8 bytes: big-endian offset and size, before the name) of entry `name` in a BIG directory
 * (names compared without case, / and \ alike): its byte position in dir, with the offset and size; -1 if none. */
long fighter_voice_dir_find(const uint8_t *dir, size_t n, const char *name, uint32_t *off, uint32_t *size);

/* For bank voice `voice` and numbered fighter id: pos[i], the record of the voice's i-th entry, and off[i] /
 * size[i], where the fighter's own i-th file is. 1 when all sixteen entries are there (sizes above zero), else 0. */
int fighter_voice_numbered_plan(const uint8_t *dir, size_t n, int voice, int id, long pos[FV_FILES],
                                uint32_t off[FV_FILES], uint32_t size[FV_FILES]);

#ifdef __cplusplus
}
#endif

#endif
