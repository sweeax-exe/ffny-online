/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Faction War's crew-only fighters (owner-approved 2026-11-25, from the December season 2026-12), as pure C
 * (tests/unit/test_faction_crew_native.py compiles faction_crew.c natively).
 *
 * In a faction match each player may play only a fighter of its own faction's story crew: no community fighters, no
 * created fighters, nothing of FFNY Studio. The account service holds the one list (server relay/accounts/
 * faction_crews.go) and sends it with the rule on /api/factions/live and /me: crew_rule=1 while the season is
 * crew-only, crew_dmob= and crew_crow= the Battle-mode IDs, comma-separated. The game ships no list of its own (nor
 * fighter names): without the service's crew_rule=1 nothing is restricted (the November season stays as it was),
 * and the service scores a faction match only when every fighter was on its side's crew.
 *
 * faction_ui.inc keeps the lists; online_screen.cpp's CHOOSE FIGHTER shows only the player's crew in a faction
 * match's room (RANDOM draws among them; the COMMUNITY and CREATED tabs have no cards there). */
#ifndef DEFJAM_FACTION_CREW_H
#define DEFJAM_FACTION_CREW_H

#ifdef __cplusplus
extern "C" {
#endif

#define FACTION_CREW_MAX 64     /* a crew's IDs at most (the service sends 25 and 23) */

/* The IDs of a crew line ("16,17,21"): stored in out (at most max), the count returned. Anything that is not a
 * Battle-mode ID (0..255; no created fighter 99, no community number) is skipped, as are repeats; NULL or "": 0. */
int faction_crew_parse(const char *list, int *out, int max);

/* The pairs of a line "59:3,83:3" (crew_locked: fighter:fights left; crew_versions: version:character): a[] and b[]
 * (at most max), the count. Entries that are not two numbers 0..255 / 0..999 are skipped; NULL or "": 0. */
int faction_crew_pairs(const char *list, int *a, int *b, int max);

/* 1 when `id` is one of the n IDs. */
int faction_crew_has(const int *ids, int n, int id);

/* The offered fighters that a crew allows: the IDs of offered[] (n_offered) that are in the crew, in their order,
 * written to out (at most max); the count. */
int faction_crew_filter(const int *crew, int n_crew, const int *offered, int n_offered, int *out, int max);

/* RANDOM within the crew: the (r mod count)-th fighter of offered[] that the crew allows; -1 when none is. */
int faction_crew_random(const int *crew, int n_crew, const int *offered, int n_offered, unsigned r);

/* The select's line for the side ("FACTION WAR: D-MOB'S CREW ONLY"; side 1 D-Mob, 2 Crow; "" else). */
const char *faction_crew_line(int side);

#ifdef __cplusplus
}
#endif

#endif
