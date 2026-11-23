/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Build 110: CUSTOM venues in the room settings (a player's wish, "an option to toggle select maps in private lobbies").
 * The host ticks the venues the room votes on in a checklist (VENUES row, A); the pool goes out as before, in SETTINGS /
 * CREATE (NetRoomSettings.pool, 1..32 venue feIDs), and every game in the room reads it from ROOM. No new message, no
 * new field: a game of build 109 or older in such a room sees the pool as "Custom (n)" and votes on the relay's draw
 * from it, as it always has (docs/netplay-protocol.md "Build 110: custom venue lists").
 *
 * The parts without screens: the setting online.room_venues ("1,6,11", the host's checklist, remembered for the next
 * room), and the checklist's toggles over the venues the room may have (its order is the checklist's). */
#ifndef NETPLAY_ROOM_VENUES_H
#define NETPLAY_ROOM_VENUES_H

#ifdef __cplusplus
extern "C" {
#endif

#define NETROOM_VENUES_MAX  32      /* the wire's pool (NetRoomSettings.pool; the relay's MaxPool) */
#define NETROOM_VENUES_SAVED 64     /* the remembered list: every venue there is (feIDs 1..63) */
#define NETROOM_VENUES_SAVED_TEXT 260   /* room for those as text, three digits and a comma each */

/* "1, 6,11" into ids (1..255), in its order, without repeats, at most max; anything else is skipped. Returns the count. */
int netroom_venues_parse(const char *text, unsigned char *out, int max);

/* ids as "1,6,11" (no spaces; empty for none) in out (size bytes, always terminated). Returns the length written; only
 * whole ids are written. */
int netroom_venues_format(const unsigned char *ids, int n, char *out, int size);

/* The ids of `in` that `order` has (the venues a room of this mode may have), in `order`'s order, at most max, into
 * out (may be `in`). Returns the count. */
int netroom_venues_fit(const unsigned char *in, int n, const int *order, int order_n, unsigned char *out, int max);

/* Ticks venue `id` on or off in pool (*count venues, kept in `order`'s order). Returns 1 now on, 0 now off,
 * -1 not added (the pool holds max already), -2 not one of `order`'s. */
int netroom_venues_toggle(unsigned char *pool, int *count, int id, const int *order, int order_n, int max);

/* The remembered list after a checklist: saved's ids that `order` does not have (venues this room cannot vote on), then
 * the pool's, no repeats, at most max. Into saved. Returns the count. */
int netroom_venues_merge(unsigned char *saved, int n, const unsigned char *pool, int count, const int *order, int order_n, int max);

/* Every venue of `order` on (the first max). Returns the count. */
int netroom_venues_all(unsigned char *pool, const int *order, int order_n, int max);

/* Whether pool holds venue id. */
int netroom_venues_has(const unsigned char *pool, int count, int id);

#ifdef __cplusplus
}
#endif

#endif
