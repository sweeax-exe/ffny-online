/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The online relays' indices, the server choice and the rules of the staged quick-match search, as
 * plain functions without sockets or Windows (netplay_servers_rules.c), so a unit test can build
 * them alone (tests/unit/test_netplay_servers_native.py). netplay_servers.h includes this. */
#ifndef DEFJAM_NETPLAY_SERVERS_RULES_H
#define DEFJAM_NETPLAY_SERVERS_RULES_H

#ifdef __cplusplus
extern "C" {
#endif

/* The relays (build 38: three). The index is part of the tie rule below; the ids are the saved
 * setting's values. */
#define NETSERVER_EU    0       /* "eu"   Europe,  play.defjam.online */
#define NETSERVER_NA    1       /* "na"   US East, na.defjam.online (Ashburn) */
#define NETSERVER_WEST  2       /* "west" US West, west.defjam.online (Hillsboro) */
#define NETSERVER_COUNT 3

/* What [online] server says. All (saved "all"; "both" before build 38): the hub connects like
 * Automatic (the lowest ping) and the room list shows every relay's rooms; joining one moves the hub
 * there. Quick Match widens its search the same way for every choice but Custom (below). */
enum NetServerChoice {
    NETCHOICE_AUTO = 0, NETCHOICE_EU, NETCHOICE_NA, NETCHOICE_CUSTOM, NETCHOICE_ALL, NETCHOICE_WEST
};

/* The setting's text: "auto", "eu", "na", "west", "all"; also the relays' names and host names, the
 * old "both" (= all) and the old default play.defjam.online (= auto). Anything else is a custom
 * relay address. */
enum NetServerChoice netservers_parse_choice(const char *text);
/* The text netservers_set_choice saves; NULL for Custom. */
const char *netservers_choice_text(enum NetServerChoice c);
/* The relay a fixed choice (eu, na, west) names, -1 for the others. */
int netservers_choice_relay(enum NetServerChoice c);
/* The choice that names relay `index` alone. */
enum NetServerChoice netservers_relay_choice(int index);

/* ---- the staged quick-match search (build 38) ------------------------------------------------ */

#define QUICK_NEAR_S    10  /* Automatic and All: from then on the other North American relay too */
#define QUICK_ALL_S     30  /* every choice: from then on every relay */
#define QUICK_LAST_S    40  /* a waiting room this old is met even by the older room's side */

/* The other North American relay (US East <-> US West, ~65 ms apart); -1 for Europe. */
int netservers_partner(int relay);
/* The relays a quick match searches after `search_s` seconds of waiting alone, as a bit mask
 * (1 << relay), for a player on relay `cur` with choice `c`; `up` is the mask of the relays that
 * answer. Its own relay always; Automatic and All add the other North American relay from
 * QUICK_NEAR_S; every choice adds every answering relay from QUICK_ALL_S. 0 for a custom address or
 * no relay. */
unsigned netservers_scope(enum NetServerChoice c, int cur, int search_s, unsigned up);
/* Two quick rooms of the same mode, each with one player waiting, on different relays: whether the
 * player in room `mine` (on relay `my_relay`, `my_age` s old) moves to the room `theirs` is on. The
 * younger room's player moves to the waiting room's relay (within 2 s of each other the one on the
 * higher relay index moves: US West to US East or Europe, US East to Europe), so two players never
 * move at once and swap; the older room's player goes only once the other room has waited
 * QUICK_LAST_S (its player cannot come). */
int netservers_should_move(int my_relay, int my_age, int their_relay, int their_age);

#ifdef __cplusplus
}
#endif

#endif
