/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Build 97's lobby features (owner-approved 2026-11-19), the parts without sockets or screens: host kick, the
 * Quick Match ping limit and AFK in rooms. The relay side is server relay/protocol_kick.go, quick_ping.go and
 * afk.go; docs/netplay-protocol.md "Build 97: host kick, quick ping limits, AFK".
 *
 *   KICK   (0x3B, game -> relay): token 8, slot u8. The host removes a member (not while that member's match runs;
 *          not in ranked, tournament, crew/turf rooms or rings). The removed game gets ERROR 16 and may not come back
 *          into that room for 5 minutes (ERROR 16 again).
 *   ACTIVE (0x3C, game -> relay): token 8, idle u16 (s since this PC's last input), max_ping u16 (the player's own
 *          Quick Match max ping, 0 off), search u16 (s its quick match has searched, 0 not searching). Every
 *          NETLOBBY_ACTIVE_EVERY_MS from the hub on, before every QUICK, and at once on input while warned.
 *   ACTIVE (0x3C, relay -> game): left u16: the relay removes this member from its room in `left` s unless it shows
 *          activity (0: the warning is over). Then ERROR 17: removed for no activity.
 *
 * Old relays drop both unknown types silently: no kick, no AFK, no own max ping on them (the pool's limits still
 * hold between build-97 games, which read each other's seats only from relays that list them). */
#ifndef NETPLAY_LOBBY97_RULES_H
#define NETPLAY_LOBBY97_RULES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NETLOBBY_T_KICK          0x3B
#define NETLOBBY_T_ACTIVE        0x3C
#define NETLOBBY_ERR_KICKED      16
#define NETLOBBY_ERR_IDLE        17
#define NETLOBBY_KICK_BLOCK_S    300     /* the relay's KickBlockTime */
#define NETLOBBY_ACTIVE_EVERY_MS 5000
#define NETLOBBY_ACTIVE_LEN      6       /* ACTIVE's body after the token */

/* [online] quick_max_ping's choices (an enum setting): off, 100, 150, 200 ms */
#define NETLOBBY_MAX_PING_CHOICES 4
int netlobby_max_ping_ms(int choice);

/* ACTIVE's body after the token into out (NETLOBBY_ACTIVE_LEN bytes); values over 0xFFFF are 0xFFFF. */
int netlobby_active_body(uint8_t *out, unsigned idle_s, int max_ping_ms, unsigned search_s);

/* The quick search's line in the room page while alone: the player's own limit when set ("Waiting for a player
 * under 150 ms..."; the stage may be tighter for a while, the limit is what never widens), else the stage of the
 * widening after `search_s` s (under 120 ms, from 20 s under 180 ms, from 45 s anyone), on `where` ("every server"
 * or a list of names). Returns the length written. */
int netlobby_search_line(char *out, int size, int own_max_ms, int search_s, const char *where);

/* The AFK warning's lines: "STILL THERE?" and "Press any button: you leave the room in 42 s." */
int netlobby_afk_line(char *out, int size, int left_s);

/* Minutes, rounded up, until a block of NETLOBBY_KICK_BLOCK_S that began `since_ms` ago ends (1 at least). */
int netlobby_kick_minutes_left(unsigned long long since_ms);

#ifdef __cplusplus
}
#endif

#endif
