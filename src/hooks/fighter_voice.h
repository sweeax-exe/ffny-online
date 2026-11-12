/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* A new fighter's own voice (docs/research/new-fighter-voice.md).
 *
 * A fighter's speech is a voice number N: audio/speech2.viv holds per N three banks (in-fight lines, taunts,
 * crowd/environment lines: speech\<Character|Taunts|Enviro>\Def_Jam_Characters\NNN.hdr + .dat) and two streams
 * (speech\streams\intro\NNN.asf, the entrance line, and ...\extro\NNN.asf, the victory line). A stock fighter's
 * N comes from the XBE table 0x2DC5D0[coreID]; a new fighter keeps its template's coreID, so it would speak with
 * the template's voice. With its own voice, the pack's installer adds the five files at N = 40 + (feID - 100)
 * (stock voices are 000..039 and 500..538, so 040..067 are free) and writes N into the fighter's Character row
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

#ifdef __cplusplus
extern "C" {
#endif

#define FV_FIRST_VOICE 40               /* N for feID 100 */
#define FV_FIRST_FE_ID 100
#define FV_LAST_FE_ID 127
#define FV_SPEECH_ARCHIVE "audio\\speech2.viv"
#define FV_FILES 8                      /* three .hdr/.dat pairs and two streams */

/* The own voice number of new fighter fe_id (40..67), or -1. */
int fighter_voice_number(int fe_id);

/* The i-th (0..FV_FILES-1) archive entry name a voice needs, e.g. "speech\Taunts\Def_Jam_Characters\040.hdr". */
void fighter_voice_entry_name(int voice, int i, char *out, size_t n);

/* Whether a BIG archive directory (the archive's first bytes, at least its header size) lists every entry of
 * voice (names compared without case, / and \ alike, each entry with a size above zero). 1 yes, 0 no. */
int fighter_voice_entries_present(const uint8_t *dir, size_t n, int voice);

#ifdef __cplusplus
}
#endif

#endif
