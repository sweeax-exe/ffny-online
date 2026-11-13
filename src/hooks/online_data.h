/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* What the online hub offers: the fighters, venues and match types, read at run time from the
 * player's own dump (game/fighter.xml, game/venue.xml), so no names ship with the port. */
#ifndef DEFJAM_ONLINE_DATA_H
#define DEFJAM_ONLINE_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OnlineFighter {
    int id;             /* Battle-mode ID (the Character record's front-end ID in assets/djv2.sod):
                         * what Controller.ControllerSetup takes. The fighter.xml ID, but for four
                         * fighters (online_data.c, k_battle_ids) */
    char name[32];
    const char *style;  /* "Street fighting", ...; "" if none */
} OnlineFighter;

typedef struct OnlineVenue {
    int fe_id;          /* venue.xml feID: what Game.ChooseVenue takes */
    char name[48];
} OnlineVenue;

typedef struct OnlineMode {
    int type;           /* front-end match-type index (Game.SetMatchType) */
    const char *name;
    int fighters;       /* fighters in the match; free places go to the CPU */
    int venue;          /* a special match's own venue (venue.xml feID); 0: the room votes */
} OnlineMode;

/* The venues with a one-on-one start table only (Gun Hill Garage, Red Hook Tire Co., Crow's Office):
 * never in a vote of a mode with more than two fighters (docs/research/team-and-special-match-types.md). */
#define ONLINE_ONE_ON_ONE_VENUE(fe) ((fe) == 17 || (fe) == 24 || (fe) == 26)
#define ONLINE_MODE_TEAM 1      /* Team, 2 vs 2: room slots 0 and 1 against 2 and 3 */
/* A new venue (FFNY Studio's new-venue packs, build 82 in the hub): the feIDs the account service lets a pack
 * claim. Offered only in rooms with community content on (not STANDARD ONLY); never in ranked or the quick pools. */
#define ONLINE_NEW_VENUE(fe) ((fe) >= 28 && (fe) <= 63)
#define ONLINE_NEW_FIGHTER(id) ((id) >= 100 && (id) <= 127)

/* Each returns the count and sets *out to a list that lives as long as the process. The two
 * from the dump are empty when it cannot be read. */
int online_fighters(const OnlineFighter **out);
int online_venues(const OnlineVenue **out);
int online_modes(const OnlineMode **out);

/* The new fighters (front-end IDs 100..127, content packs; new_fighters.h): offered in the hub's fighter
 * list, never as CPU places, random picks or in drafts. */
int online_new_fighters(const OnlineFighter **out);
const char *online_fighter_name(int id);    /* "Fighter 12" if unknown */
/* A fighter ID as saved by builds that offered Teck, Santos, O.E and Eric Sermon under their
 * fighter.xml IDs (13 and 14), as the Battle-mode ID; any other ID unchanged. */
int online_fighter_id(int id);
const char *online_venue_name(int fe_id);
const OnlineMode *online_mode(int type);    /* NULL if not offered */

/* RANDOM picks in the online hub (build 67): 0..n-1 (0 for n <= 1) from a host generator of its own,
 * seeded from the clock once, never the guest's rand() (the lockstep's state is not touched; the
 * pick is made on this PC and sent like any other). */
int online_random_below(int n);
/* A fighter of online_fighters() at random (55 when the list is empty): the tests' RANDOM pick. */
int online_random_fighter(void);
/* The same among the fighters not in blocked (a room's FIGHTER RULES, build 68); every fighter blocked: any. */
int online_random_fighter_except(const unsigned char *blocked, int nblocked);

#ifdef __cplusplus
}
#endif

#endif
