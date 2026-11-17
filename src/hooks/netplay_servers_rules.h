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

/* The relays (build 38: three; build 91: a table of up to NETSERVER_MAX). The index is part of the tie rule below
 * and of the wire: RANKED's and QUICK's pings and ROOMLIST's listed pings go in this order (server
 * relay/protocol_ranked.go RelayNames), so the table only ever grows at its end; a closed relay keeps its
 * slot until every build that knows it is gone. The ids are the saved setting's values and the relays'
 * names for the ranked matchmaker. A new relay is one row in k_relays (netplay_servers_rules.c) and one
 * name at the end of the server's RelayNames. */
#define NETSERVER_EU    0       /* "eu"   Europe,  play.defjam.online */
#define NETSERVER_NA    1       /* "na"   North America (Miami), na2.defjam.online (build 91; US East, Ashburn, before) */
#define NETSERVER_SG    2       /* "sg"   Asia (Singapore), sg2.defjam.online (build 91; sg. in 89..90, US West before) */
#define NETSERVER_SA    3       /* "sa"   South America (Sao Paulo), sa.defjam.online (build 91) */
#define NETSERVER_WEST  NETSERVER_SG    /* the slot's name of builds 38..88 */
#define NETSERVER_COUNT 4       /* the rows of k_relays (checked there) */
#define NETSERVER_MAX   8       /* the most the wire's trailers and the picker take */
#define NETSERVER_LEGACY 3      /* the relays every build since 38 has in this order: the fixed three pings */

/* One relay of the table. */
typedef struct NetRelayDef {
    const char *id;             /* "eu": the setting's value and the matchmaker's relay name */
    const char *name;           /* "Europe": what players read (the picker shows it in capitals) */
    const char *short_name;     /* "EU": the room list's tag */
    const char *address;        /* "play.defjam.online:27777" */
    const char *fallback_ip;    /* used when the name does not resolve; "" none */
    const char *env;            /* tests: an address that replaces this relay's (RECOMP_NETPLAY_SERVER_EU) */
    const char *social;         /* the global chat region (server accounts/social.go SocialRegions) */
    const char *words;          /* more setting words for this relay, '|' between them, lower case */
} NetRelayDef;

/* The relay at index (0 .. NETSERVER_COUNT - 1), NULL beyond. */
const NetRelayDef *netservers_def(int index);
/* The relay whose id is `id` (case-insensitive), -1 none. */
int netservers_index_of_id(const char *id);
/* The relay whose global chat region is `region`, -1 none. */
int netservers_index_of_social(const char *region);
/* The fallback IPv4 address of one of our relays' host names (no port), NULL for any other name. */
const char *netservers_known_ip(const char *host);

/* What [online] server says. All (saved "all"; "both" before build 38): the hub connects like
 * Automatic (the lowest ping) and the room list shows every relay's rooms; joining one moves the hub
 * there. Quick Match widens its search the same way for every choice but Custom (below). The values
 * of builds 38..90 stay (relay 2's choice is 5); relays from index 3 on are NETCHOICE_RELAY3 + (index - 3). */
enum NetServerChoice {
    NETCHOICE_AUTO = 0, NETCHOICE_EU, NETCHOICE_NA, NETCHOICE_CUSTOM, NETCHOICE_ALL, NETCHOICE_SG,
    NETCHOICE_RELAY3 = 6, NETCHOICE_SA = NETCHOICE_RELAY3,
    NETCHOICE_END = NETCHOICE_RELAY3 + NETSERVER_MAX - NETSERVER_LEGACY,
    NETCHOICE_WEST = NETCHOICE_SG
};

/* The setting's text: "auto", "all" or a relay's id ("eu", "na", "sg", "sa"); also each relay's other
 * words (names, "brazil"), its host name with or without the port, the old "both" (= all), the old
 * default play.defjam.online (= auto) and US West's words (closed in build 89: auto). Anything else is a
 * custom relay address. */
enum NetServerChoice netservers_parse_choice(const char *text);
/* The text netservers_set_choice saves; NULL for Custom. */
const char *netservers_choice_text(enum NetServerChoice c);
/* The relay a fixed choice names, -1 for the others. */
int netservers_choice_relay(enum NetServerChoice c);
/* The choice that names relay `index` alone (Europe's for an index out of range). */
enum NetServerChoice netservers_relay_choice(int index);

/* ---- the pings to every relay on the wire (build 91, CAPS bit 28) ------------------------------
 * The fixed part of RANKED's long form and of QUICK carries the pings to the first NETSERVER_LEGACY
 * relays (u16 LE each, 0 unknown). A relay that answered CAPS bit 28 also takes a trailer after them:
 * a count byte E, then E u16 pings for relays NETSERVER_LEGACY .. NETSERVER_LEGACY + E - 1. ROOMLIST
 * asked with bit 28 answers the same way after its per-room pings: E, then E u16 per listed room. */

/* Writes the trailer for pings[0 .. count-1] (ms, <= 0 unknown, capped at 65535): returns its size, 1 + 2 E
 * (E = count - NETSERVER_LEGACY, at least 0). out must take 1 + 2 * (NETSERVER_MAX - NETSERVER_LEGACY). */
int netservers_put_more_pings(unsigned char *out, const int *pings, int count);
/* Reads ROOMLIST's trailer at b (len bytes left) for `rooms` rooms into pings[room * NETSERVER_COUNT + relay]
 * (the relays this build knows; further ones skipped). Returns the bytes used, 0 when there is none, -1 when
 * it is short. */
int netservers_get_more_pings(const unsigned char *b, int len, int rooms, int *pings);

/* ---- the staged quick-match search (build 38) ------------------------------------------------ */

#define QUICK_NEAR_S    10  /* Automatic and All: from then on the other North American relay too */
#define QUICK_ALL_S     30  /* every choice: from then on every relay */
#define QUICK_LAST_S    40  /* a waiting room this old is met even by the older room's side */

/* The relay close to `relay` (US East <-> US West until build 88); -1: none any more (build 89). */
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

/* Build 90, the relay by ping sum (server relay/ping_sum.go, owner 2026-11-17): the same two quick rooms, each
 * with one player, when both rooms' relays listed their player's pings to every relay (mine, theirs: ms by
 * relay index, 0 unknown). The two play on the relay with the smaller sum of their pings (a tie: the lower
 * worse ping): 1 this player moves to their relay, 0 it stays (the other player comes). Both games work it out
 * from the same listed numbers, so they agree. -1 when the sum does not decide: a ping unknown, the same
 * relay, a full tie, or the other player has not come although it could have (its room QUICK_ALL_S +
 * QUICK_SUM_WAIT_S old and this one listed QUICK_SUM_WAIT_S): then netservers_should_move decides.
 * Build 91: when one of the two relays is beyond the first NETSERVER_LEGACY (South America), a player with no ping
 * to the other's relay cannot go there (a build before 91 lists none for it and never sees its rooms): that
 * player stays (0) and the other one moves (1), whatever the sums. */
#define QUICK_SUM_WAIT_S 12
int netservers_sum_move(int my_relay, int my_age, const int *mine, int their_relay, int their_age, const int *theirs);

/* ---- what players are shown (build 84) ---------------------------------------------------------
 * Players never see our relays' host or port: status lines say "the US East server". */

/* The relay's name ("Europe", "South America") when `address` is one of our relays (host name or IP, with or
 * without port 27777), "US West" for the relay closed in build 89; NULL for anything else (a custom relay,
 * shown as typed). */
const char *netservers_known_name(const char *address);
/* The player-facing text for a server address: "the US East server" for ours, the address as it was
 * typed for a custom one. Returns 1 for one of ours, 0 for a custom address. */
int netservers_display(const char *address, char *out, int size);

#ifdef __cplusplus
}
#endif

#endif
