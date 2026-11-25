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
#define NETSERVER_NA    1       /* "na"   US East, nj.defjam.online (build 91 New Jersey; Ashburn before) */
#define NETSERVER_SG    2       /* "sg"   Asia (Singapore), sg2.defjam.online (build 91; sg. in 89..90, US West before) */
#define NETSERVER_SA    3       /* "sa"   South America (Sao Paulo), sa.defjam.online (build 91) */
#define NETSERVER_MIA   4       /* "mia"  US South (Miami), na2.defjam.online (build 91) */
#define NETSERVER_DFW   5       /* "dfw"  US Central (Dallas), dfw.defjam.online (build 91) */
#define NETSERVER_LAX   6       /* "lax"  US West (Los Angeles), lax.defjam.online (build 91) */
#define NETSERVER_MEX   7       /* "mex"  Mexico (Mexico City), mex.defjam.online (build 111) */
#define NETSERVER_BOG   8       /* "bog"  Colombia (Bogota), bog.defjam.online (build 111) */
#define NETSERVER_WAW   9       /* "waw"  Eastern Europe (Warsaw), waw.defjam.online (build 111) */
#define NETSERVER_WEST  NETSERVER_SG    /* the slot's name of builds 38..88 */
#define NETSERVER_COUNT 10      /* the rows of k_relays (checked there) */
#define NETSERVER_MAX   12      /* the most the wire's trailers and the picker take (8 until build 110) */
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
    NETCHOICE_RELAY3 = 6, NETCHOICE_SA = NETCHOICE_RELAY3, NETCHOICE_MIA = NETCHOICE_RELAY3 + 1,
    NETCHOICE_DFW = NETCHOICE_RELAY3 + 2, NETCHOICE_LAX = NETCHOICE_RELAY3 + 3,
    NETCHOICE_MEX = NETCHOICE_RELAY3 + 4, NETCHOICE_BOG = NETCHOICE_RELAY3 + 5, NETCHOICE_WAW = NETCHOICE_RELAY3 + 6,
    NETCHOICE_END = NETCHOICE_RELAY3 + NETSERVER_MAX - NETSERVER_LEGACY,
    NETCHOICE_WEST = NETCHOICE_SG
};

/* The setting's text: "auto", "all" or a relay's id ("eu", "na", "sg", "sa", "mia", "dfw", "lax", "mex", "bog", "waw"); also each relay's other
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
 * answer. Build 91 (the global pool): its own relay and every answering relay from the first second, whatever
 * the choice (QUICK_NEAR_S and QUICK_ALL_S are the steps of builds 38..90). 0 for a custom address or no relay. */
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

/* ---- the global quick-match pool (build 91, owner 2026-11-17) ----------------------------------
 * The relay is only WHERE a match runs, never WHO a player can meet. From the first second a quick match sees the
 * one-player quick rooms of every relay (ROOMLIST, with each room's player's pings to every relay) and pairs the
 * waiting players by the smallest ping sum: for each pair the relay with the smallest sum of the two pings (a tie:
 * the lower worse ping, then the lower relay index). Build 97 (owner 2026-11-19): that sum is the pair's ping, and a
 * pair is allowed while it is within the limit of the longer wait of the two (netservers_pool_limit, the stages
 * below: 120 ms, from 20 s 180, from 45 s any) and within each player's own Quick Match max ping, which never
 * widens (seat max_ping; ROOMLIST lists it with the room's wait). The relay (server relay/quick_ping.go
 * QuickPingStages) uses the same stages for two players on one relay; tests/unit/test_lobby97.py keeps them alike.
 * The pairs are taken greedily, best first (sum, worse ping, then the two room codes), so every game that reads
 * the same lists works out the same pairs. Both players then go to the pair's relay (the one already there
 * stays), where the relay's QUICK puts them in one room. Players of another fighter choice (original fighters
 * only) are a separate pool. */
#define QUICK_POOL_MAX 64

/* Build 97: the staged widening, the one place on the game side (the relay's QuickPingStages has the same) */
#define QUICK_STAGE1_S      20      /* from this wait, pairs within QUICK_STAGE1_MS */
#define QUICK_STAGE2_S      45      /* from this wait, any pair */
#define QUICK_STAGE0_MS     120
#define QUICK_STAGE1_MS     180
/* Build 109 (server relay/quick_cap.go): the ceiling at every stage, for a player who does not allow far opponents
 * ([online] quick_far, "Allow far opponents"). The relay's -quick-ping-cap / quick-ping-cap file sets it and lists it
 * in ROOMLIST (with each seat's QUICK_SEAT_* byte); this is the game's default when no list said one. */
#define QUICK_CAP_MS        300
#define QUICK_SEAT_BOUND    1       /* the seat's player is bound by the ceiling (a build-109 game, far not allowed) */
#define QUICK_SEAT_FAR      2       /* ... it allows far opponents */

typedef struct NetQuickSeat {
    int relay;                      /* the relay its quick room is on */
    int age;                        /* seconds the room's player has waited (build 97: the relay's listed wait) */
    int max_ping;                   /* build 97: its player's own Quick Match max ping, ms (0 off) */
    int pings[NETSERVER_COUNT];     /* its player's pings by relay index, 0 unknown */
    char code[8];                   /* the room's code (the tie rule) */
    int flags;                      /* build 109: QUICK_SEAT_* (0: an older game or relay, never bound) */
    int cap_ms;                     /* build 109: the ceiling its relay listed (0: none, or an older relay) */
} NetQuickSeat;

/* The pair-ping limit after waiting `age_s` seconds; 0 = any ping. */
int netservers_pool_limit(int age_s);
/* Build 97: the relay seats a and b would play on (the smallest sum of their pings; a tie: the lower worse ping, then
 * the lower relay index), or -1 when their sum there is over `limit` ms (0: any) or over either seat's max_ping, or
 * no relay has both pings; *sum and *worse (may be NULL) are that relay's. Build 109: also -1 over the ceiling (the
 * lower of the two seats' cap_ms that are set) when either seat is QUICK_SEAT_BOUND, as the relay's quickPair. */
int netservers_pair_relay(const NetQuickSeat *a, const NetQuickSeat *b, int limit, int *sum, int *worse);
/* The pool's pairs: partner[i] (-1 unpaired) and relay[i] (where seat i plays, -1 unpaired) for every seat. Two
 * seats on the same relay are never paired (the relay joins them itself). n at most QUICK_POOL_MAX. */
void netservers_pool(const NetQuickSeat *seats, int n, int *partner, int *relay);
/* Build 94: seat `me`'s relay (*partner_out its partner, -1 none) only when the pool gives the same answer with every
 * wait QUICK_POOL_SLACK_S shorter and longer; -2 while it does not (wait for the next list), -1 unpaired. The two
 * games read each other's room age from different relays' lists at different moments, a second or two apart: near
 * a limit step (10, 20, 40 s) one could pair on its own relay while the other, a limit further, picks the first
 * one's relay, and both moved at once and swapped (new rooms, waits back to 0). Inside the slack both wait. */
#define QUICK_POOL_SLACK_S 3
int netservers_pool_steady(const NetQuickSeat *seats, int n, int me, int *partner_out);

/* ---- what players are shown (build 84) ---------------------------------------------------------
 * Players never see our relays' host or port: status lines say "the US East server". */

/* The relay's name ("Europe", "South America") when `address` is one of our relays (host name or IP, with or
 * without port 27777), "US West" for the relay closed in build 89; NULL for anything else (a custom relay,
 * shown as typed). */
const char *netservers_known_name(const char *address);
/* The player-facing text for a server address: "the US East server" for ours, the address as it was
 * typed for a custom one. Returns 1 for one of ours, 0 for a custom address. */
int netservers_display(const char *address, char *out, int size);

/* ---- CREATE A ROOM's SERVER row (build 111, owner 2026-11-24) ------------------------------------
 * Where a player's own room opens: Automatic (the default: a fresh ping round when the room opens, then the lowest
 * ping) or one relay of the table. Quick Match, Ranked, Faction War and Towers stay automatic; joining a room goes to
 * that room's relay as before. The row's value: ROOMSERVER_AUTO, or 1 + the relay's index. Saved as [online]
 * room_server: "auto" or the relay's id. */
#define ROOMSERVER_AUTO         0
#define ROOMSERVER_MARGIN_MS    10  /* Automatic leaves the relay the hub is on only for one at least this much faster */

/* The saved text's value: a relay's id or any of its words (netservers_parse_choice) is that relay; anything else
 * (NULL, "", "auto", "all", a custom address, a relay this build does not know) is Automatic. */
int netservers_room_parse(const char *text);
/* The text to save: "auto" or the relay's id ("auto" for a value out of range). */
const char *netservers_room_text(int value);
/* The relay a value names, -1 for Automatic (and anything out of range). */
int netservers_room_relay(int value);
/* One step (step < 0 left, > 0 right) through Automatic and the table's relays, wrapping; a relay whose bit is set in
 * `skip` (1 << index: no answer to the last round) is stepped over. Returns the new value. */
int netservers_room_step(int value, int step, unsigned skip);
/* Where the room opens: the chosen relay; for Automatic, by the fresh pings (ms by relay index, < 0 no answer) the
 * relay with the lowest ping (a tie: the lower index), but the relay the hub is on (`cur`, -1 none) while its ping is
 * within ROOMSERVER_MARGIN_MS of that. -1: nothing answered and the hub is on no relay of the table (open it where
 * the hub is). */
int netservers_room_target(int value, int cur, const int *pings, int count);

#ifdef __cplusplus
}
#endif

#endif
