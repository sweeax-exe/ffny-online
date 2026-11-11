/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The online relays and the choice between them (build 30; build 38: three): Europe
 * (play.defjam.online), US East (na.defjam.online) and US West (west.defjam.online), all port 27777.
 *
 * Each relay answers STATUS (docs/netplay-protocol.md, "Relay status") without a session: a
 * background thread asks every relay every 10 s while the online screens or the launcher's settings
 * show, and keeps each one's round trip and player counts. The setting [online] server is auto, eu,
 * na (US East), west or all (netplay_servers_rules.h; auto: the relay with the lowest ping, decided
 * once per session; all: the same, and the rooms of every relay; "both" of builds 31..37 reads as
 * all); anything else is a custom address (tests, a private relay) used as it is. Accounts,
 * statistics and updates stay on the Europe server whatever is chosen.
 *
 * Tests: RECOMP_NETPLAY_SERVER_EU / _NA / _WEST replace the relays' addresses (e.g. local relays,
 * one started with -test-lag). */
#ifndef DEFJAM_NETPLAY_SERVERS_H
#define DEFJAM_NETPLAY_SERVERS_H

#include "netplay.h"    /* NetRoomInfo */
#include "netplay_servers_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

enum NetServerState {
    NETSERVER_UNKNOWN = 0,  /* not asked yet, or the first answers are still on their way */
    NETSERVER_UP,           /* answered STATUS: ping and counts are valid */
    NETSERVER_OFFLINE       /* no answer to the last round (down, blocked, or a relay older than STATUS) */
};

/* STATUS flags (the relay's answer) */
#define NETSERVER_RANKED   1
#define NETSERVER_KOTH     2
#define NETSERVER_ROLLBACK 4

typedef struct NetServerInfo {
    const char *id;         /* "eu", "na", "west" */
    const char *name;       /* "Europe", "US East", "US West" */
    const char *short_name; /* "EU", "US EAST", "US WEST" */
    char address[128];      /* host:port */
    int state;              /* NETSERVER_* */
    int ping_ms;            /* the best round trip of the last round, -1 unknown */
    int online, rooms;      /* everyone connected, open rooms */
    int waiting, idle;      /* this build's players waiting in public rooms / in the lobby */
    int flags;              /* NETSERVER_RANKED | ... */
    int version;            /* protocol version byte, 0 unknown */
} NetServerInfo;

/* Ask every relay now if the last round is older than 10 s (cheap to call every frame). */
void netservers_probe(void);
/* Copies the relays' state; returns NETSERVER_COUNT. */
int netservers_get(NetServerInfo *out, int max);
/* 1 while a round runs and nothing has come back yet from any relay. */
int netservers_probing(void);

enum NetServerChoice netservers_choice(void);
/* Stores the choice ("auto", "eu", "na", "west", "all") and drops any switch made this session. */
void netservers_set_choice(enum NetServerChoice c);
/* This session only, beating the setting: the quick-match hint's "search there", the ranked switch. */
void netservers_use(int index);

/* The relay to connect to now (NETSERVER_EU / _NA / _WEST), -1 while Automatic still waits for the first
 * answers (at most about 2.5 s after the first probe), -2 for a custom address. The automatic
 * choice is made once and kept for the session. */
int netservers_current(void);
/* Before a new connection (not in a room or match): Automatic moves to the relay with the lowest ping
 * when it is clearly better (40 ms and 30 % lower) or the current one is offline. Returns the relay
 * to use. */
int netservers_reconsider(void);
/* The address for netplay_lobby(): the chosen relay's, or the custom one. NULL while undecided. */
const char *netservers_address(char *buf, int size);
/* Tests (RECOMP_NETPLAY=quick with RECOMP_NETPLAY_SERVER=auto|eu|na|west|all): like netservers_address,
 * but waits up to wait_ms for Automatic's first answers. */
const char *netservers_address_wait(char *buf, int size, int wait_ms);
/* Moves the hub session to the relay chosen now (after netservers_set_choice or netservers_use):
 * nothing when it is already there; otherwise the old session ends (BYE, which also leaves its
 * room) and the hub connects to the new relay as `name`. Returns 1 when it reconnected, 0 when
 * nothing had to change, -1 while Automatic is undecided. */
int netservers_reconnect(const char *name);
/* The relay `address` is (by its host and port), -1 if none. */
int netservers_index_of(const char *address);
/* A relay's name ("US East") and short name ("US EAST"); "custom" for anything else. */
const char *netservers_name(int index);
const char *netservers_short(int index);

/* ---- Every relay's rooms (build 31, "both"; build 38: all three) ---------------------------- */

/* The relay the hub session is on (NETSERVER_*), -1 none, -2 a custom address. */
int netservers_connected(void);
/* The answering relay with the lowest ping, -1 none answered. */
int netservers_best(void);
/* Where a QUICK MATCH starts: the fixed choice's relay; with Automatic and All the lowest ping (else
 * the relay chosen now); -1 for a custom address or while undecided. */
int netservers_home(void);
/* The mask of the relays that answered the last STATUS round (1 << NETSERVER_*). */
unsigned netservers_up(void);

/* One room of the merged room list: the room, the relay it is on and this PC's ping to that relay. */
typedef struct NetServerRoom {
    NetRoomInfo room;
    int server;         /* NETSERVER_*; -2 a custom address */
    int ping_ms;        /* -1 unknown */
    int age_s;          /* seconds since the room opened, -1 unknown */
} NetServerRoom;

/* With All: asks every relay for its public rooms (ROOMLIST, no session) when the last answer is
 * older than 3 s. Cheap to call every frame; does nothing with any other choice. */
void netservers_fetch_rooms(void);
/* The room list to show: the connected relay's (LIST) and, with All, the other relays' (ROOMLIST,
 * answers up to 10 s old), the relay with the lowest ping first, each in its own order (newest
 * first). Returns the count; *version changes whenever the list may have. */
int netservers_rooms(NetServerRoom *out, int max, unsigned *version);

/* ---- Every relay's players (build 47, "All servers") ------------------------------------------ */

/* One player of the merged list of who is online: the player and the relay it is on. */
typedef struct NetServerPlayer {
    NetPlayer player;
    int server;         /* NETSERVER_* */
} NetServerPlayer;

/* With All: asks every relay but the hub's own (that one answers WHO) for its players (PLAYERLIST, no
 * session) when the last answer is older than 5 s. Cheap to call every frame; does nothing with any
 * other choice. */
void netservers_fetch_players(void);
/* The same whatever the choice (build 76: the chat window's ONLINE tab when the account service has no
 * list of its own); the same 5 s floor per relay. */
void netservers_fetch_players_all(void);
/* Who is online on every relay that answered: the hub's relay's WHO list first, then the others'
 * PLAYERLIST lists (answers up to 15 s old) by ping, each in the relay's order (hub, room, fighting;
 * by name), each tagged with its relay. Returns the count, -1 while no relay has answered yet;
 * *online: everyone connected to those relays (-1 likewise). */
int netservers_players(NetServerPlayer *out, int max, int *online);

/* Join room `code` (of match type `mode`) on relay `server`: at once when the hub is in that relay's
 * lobby, else the hub moves there (the old session ends) and joins once it is in. The same for a quick
 * match. A move waits at most 15 s for the new lobby. */
void netservers_join_on(int server, const char *code, int mode, const char *name);
void netservers_quick_on(int server, int mode, int max, const char *name);
/* Carries out a pending join or quick match once the hub is in the right lobby (call it often: every
 * frame of the online screens, the test thread). Returns 0 nothing happened, 1 the JOIN went out,
 * 2 the QUICK went out (*mode: its match type), -1 the move failed (no lobby within 15 s). */
int netservers_tick(int *mode);
/* Ranked: the queue said MOVE (QUEUE 6): the hub moves to the host relay and queues there again with
 * the reservation code (netplay_lobby_ranked_again). Call it every frame of the online screens and the
 * test thread. Returns 1 when a move started, -1 when the host relay is unknown (the search ends), 0
 * nothing to do. */
int netservers_ranked_move(const char *name);
/* 1 while a join or quick match waits for the move to its relay. */
int netservers_moving(void);

/* The staged quick-match search (build 38), this PC alone in its quick room `code` of match type
 * `mode`, searching for `search_s` seconds (since it was first alone in a quick room of this QUICK
 * MATCH, kept across moves) and alone in this room for `alone_s`: the relay to move to, or -1.
 * The relays searched are netservers_scope's (its own; with Automatic and All the other North
 * American relay from 10 s; every answering relay from 30 s, a fixed choice too). On one of them, a
 * public quick room of that mode in its lobby phase with a player waiting is met as
 * netservers_should_move says (the younger room's player moves to the waiting room's relay). The
 * rooms come from ROOMLIST answers at most 6 s old, asked every 3 s while this is called; a relay
 * without ROOMLIST counts by its STATUS (players waiting) from QUICK_LAST_S on. *waiting: players in
 * the rooms there; *ping: this PC's ping to it; *scope (may be NULL): the relays searched now, for the
 * screen's notice. -1 (scope 0) for a custom address and King of the Hill. */
int netservers_quick_move(int mode, int search_s, int alone_s, const char *code, int *waiting, int *ping,
                          unsigned *scope);

#ifdef __cplusplus
}
#endif

#endif
