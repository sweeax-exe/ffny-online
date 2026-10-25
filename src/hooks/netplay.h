/* Online play through a relay server (docs/netplay-protocol.md, docs/netplay.md).
 *
 * Two copies of the port, each with its own dump, play one match: the host is
 * player 1, the guest player 2, on both PCs. In the menus each side sees the
 * other's controller live, as a second pad. In a fight the two simulations run
 * in lockstep: every simulation step takes both players' pad-table entries from
 * a buffer both sides fill identically, a few steps ahead (the input delay), and
 * every random generator is seeded from the session, not the clock.
 *
 * Windows only for now; elsewhere every call does nothing. */
#ifndef DEFJAM_NETPLAY_H
#define DEFJAM_NETPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum NetplayState {
    NETPLAY_OFF = 0,        /* not started, or cancelled */
    NETPLAY_CONNECTING,     /* waiting for the server's WELCOME */
    NETPLAY_WAITING,        /* in a room, no second player yet (host) */
    NETPLAY_SYNCING,        /* both present, agreeing on the session */
    NETPLAY_CONNECTED,      /* ready: play */
    NETPLAY_FAILED,         /* see netplay_status */
    NETPLAY_LOBBY           /* online hub: in the lobby or a room, between matches */
};

#define NETPLAY_MAX_PLAYERS 4      /* fighters in one match */
#define NETPLAY_ROOM_SLOTS 10      /* members of one room: King of the Hill rooms hold up to ten */

/* King of the Hill (docs/research/king-of-the-hill.md): NetRoomSettings.mode of such a room; every
 * fight in it is One on One. The venue is voted on by the two fighters, or drawn at random. */
#define NETPLAY_MODE_KOTH        0x40
#define NETPLAY_MODE_KOTH_RANDOM 0x41
#define NETPLAY_IS_KOTH(mode)    ((mode) == NETPLAY_MODE_KOTH || (mode) == NETPLAY_MODE_KOTH_RANDOM)

/* ---- the online hub: rooms, quick match, venue voting (docs/netplay-protocol.md) ---- */

typedef struct NetRoomSettings {
    char name[25];
    int is_public;          /* listed and found by quick match */
    int mode;               /* front-end match type, 0 One on One; or NETPLAY_MODE_KOTH* */
    int max;                /* players 2..4; King of the Hill 2..10 */
    int rounds;             /* 0 the game's default */
    int pool_count;         /* venues to vote on (venue.xml feID) */
    unsigned char pool[32];
} NetRoomSettings;

typedef struct NetRoomInfo {
    char code[7], name[25];
    int mode, players, max, phase;
} NetRoomInfo;

/* One player in the relay's list of who is online (WHO / PLAYERS). */
typedef struct NetPlayer {
    char name[25];
    int state;                      /* 0 in the hub, 1 in a room, 2 playing */
} NetPlayer;

typedef struct NetRoomPlayer {
    int slot;
    char name[25];
    int ready, fighter, voted;      /* voted: candidate index, -1 not yet */
} NetRoomPlayer;

/* King of the Hill: the line (HILL), in line order; line[0] is the king when there is one. */
enum NetHillPhase { NETHILL_WAITING = 0, NETHILL_CALLING, NETHILL_VOTING, NETHILL_FIGHTING };
enum NetLineState { NETLINE_WAITING = 0, NETLINE_CALLED, NETLINE_READY, NETLINE_FIGHTING, NETLINE_AWAY };
enum NetHillHow { NETHOW_NONE = 0, NETHOW_WON, NETHOW_DRAW, NETHOW_LEFT, NETHOW_VOID };

typedef struct NetHillEntry {
    int slot, state, wins;          /* state: NETLINE_*; wins: fights won in this room */
} NetHillEntry;

typedef struct NetHill {
    int valid;                      /* a HILL arrived for this room */
    int phase;                      /* NETHILL_* */
    int king;                       /* slot, -1 none */
    int streak;                     /* the king's wins in a row */
    int ms_left;                    /* calling: ready countdown; voting: vote countdown; as of `received` */
    unsigned long long received;
    uint32_t seed;                  /* fighting: the fight's START seed */
    int fights;                     /* fights finished in this room (wraps at 65536) */
    int last_winner, last_loser;    /* slots, -1 none */
    int last_how;                   /* NETHOW_* */
    int count;
    NetHillEntry line[NETPLAY_ROOM_SLOTS];
    /* the WATCH of the fight this PC watches (phase 1: the names only, no simulation) */
    uint32_t watch_seed;            /* 0: none */
    int watch_venue, watch_slots[2], watch_fighters[2];
} NetHill;

typedef struct NetRoom {
    int in_room;
    char code[7];
    int you, host, phase;           /* phase 0 lobby, 1 voting, 2 playing */
    NetRoomSettings settings;
    int candidate_count;
    unsigned char candidates[3];
    int votes[3];
    int ms_left;                    /* voting countdown, as of `received` */
    unsigned long long received;    /* GetTickCount64 when this state arrived */
    int player_count;
    NetRoomPlayer players[NETPLAY_ROOM_SLOTS];
    NetHill hill;                   /* King of the Hill rooms only */
    char error[96];                 /* the last refusal, "" if none */
    unsigned version;               /* changes whenever anything here does */
} NetRoom;

/* What the relay supports (its answer to CAPS): -1 not known yet (or no answer: an older relay,
 * then 0), else the bits; bit 0 King of the Hill. */
#define NETPLAY_CAP_KOTH 1
int netplay_lobby_caps(void);

/* Enter the hub on `server`. The state becomes NETPLAY_LOBBY once the server answers; a room's
 * START makes it NETPLAY_CONNECTED for the match, which starts by itself (automatch.h), and it
 * returns to NETPLAY_LOBBY when the game is back at the main menu. */
int netplay_lobby(const char *server, const char *name);
void netplay_lobby_list(int mode);                      /* mode 0xFF: any */
int netplay_lobby_rooms(NetRoomInfo *out, int max, unsigned *version);
void netplay_lobby_who(void);   /* asks who is online; the answer comes as netplay_lobby_players */
/* The last answer: the players listed (up to max), how many are connected and how many rooms
 * there are; -1 before any answer. */
int netplay_lobby_players(NetPlayer *out, int max, int *online, int *rooms);
void netplay_lobby_create(const NetRoomSettings *settings);
void netplay_lobby_join(const char *code);
void netplay_lobby_quick(int mode, int max);
void netplay_lobby_settings(const NetRoomSettings *settings);   /* host only */
void netplay_lobby_ready(int ready, int fighter);
void netplay_lobby_vote(int choice);
void netplay_lobby_leave(void);
void netplay_lobby_room(NetRoom *out);

/* Ranked (One on One against a player of a similar rating; signed-in players only). The search
 * runs until a match is found (the ranked room's ROOM arrives, both players ready, the vote
 * starts) or netplay_lobby_leave. */
enum NetQueueState {
    NETQUEUE_NONE = -1,     /* not searching */
    NETQUEUE_CHECKING = 0,  /* the server checks the account */
    NETQUEUE_SEARCHING = 1,
    NETQUEUE_REFUSED = 2,   /* see the text */
    NETQUEUE_OFF = 3,
    NETQUEUE_FOUND = 4,
    NETQUEUE_TICKET = 5     /* asking the account service for a ticket */
};
void netplay_lobby_ranked(int fighter);
/* The queue state; waited (seconds), searching (players in the queue) and text (why refused)
 * when not NULL. */
int netplay_lobby_queue(int *waited, int *searching, char *text, int size);
int netplay_in_lobby(void);     /* 1 while the session came from netplay_lobby */

/* From automatch: the agreed match is about to be set up (every PC reaches this point at the
 * same place in the front end), and the front end is back in the menus (the main menu, or the
 * battle match-type screen that a match's summary leads to). */
void netplay_sync_point(void);
void netplay_on_main_menu(void);
int netplay_match_abandoned(void);  /* the sync point: a player left before the match was set up */

/* Start as the host (create a room) or join `room` (6 characters). `server` is
 * "host" or "host:port" (default port 27777); `name` is shown to the other
 * player. Any earlier session is ended first. Returns 0 if it could start. */
int netplay_host(const char *server, const char *name, int input_delay);
int netplay_join(const char *server, const char *name, const char *room);

/* End the session (BYE to the server) and give the controllers back. */
void netplay_stop(void);

enum NetplayState netplay_state(void);

/* A line for the launcher or overlay: the room code, the other player, the ping,
 * or what went wrong. */
void netplay_status(char *text, int size);

/* The room code once in a room ("" before). */
void netplay_room(char *code, int size);

/* 1 while a session is connected and the game should run in lockstep. */
int netplay_active(void);

/* 1 while a fight runs, online or not (the game's match phase): no notices over it. */
int netplay_in_fight(void);
int netplay_rerunning(void);    /* 1 while rollback runs steps again */

/* Voice chat (voice.c): 1 while this PC is in a room of the online hub (its lobby or its match); a
 * voice packet to every other member; the name of the member in relay slot `slot` (0 if none). */
int netplay_voice_room(void);
void netplay_send_voice(const uint8_t *payload, int len);
int netplay_room_name(int slot, char *out, int size);

/* Tests: start a session from RECOMP_NETPLAY=host|join, RECOMP_NETPLAY_SERVER and
 * RECOMP_NETPLAY_CODE_FILE, if they are set. Call once at start-up. */
void netplay_autostart(void);

/* ---- hooks from the game (src/recomp_manual.c) ------------------------------ */
void netplay_on_match_start(void);                  /* sub_001A4D90: a match is set up      */
void netplay_on_pad_pass(void);                     /* sub_001AD380: pad entries just read */
void netplay_on_step(void);                         /* sub_00087700: one simulation step    */
void netplay_on_update(void);                       /* sub_0002C530: a game update begins   */
void netplay_sound_command(void);   /* the sound engine's command call, silent while steps re-run */
void *netplay_sound_handler(uint32_t va);   /* the audio subsystem's bus handlers, or NULL */
int netplay_rematch_request(void);   /* Game.Rematch() online: 1 to hold it for the others */
int netplay_toast(char *text, int size);
int netplay_debug_json(char *out, int size);    /* tests: the lockstep's state as JSON */   /* a notice for the fight right now (wait, desync, gone) */
void netplay_crowd_update(void);  /* the crowd's update, as recomp_lookup_manual hands it out */
void netplay_on_result(uint32_t winners, uint32_t losers, uint32_t code);  /* the room match decided */
/* The crash handler (main.c), after the crash report is written: a running online match this
 * game reported the start of becomes a no contest at the account service ("crashed"). Copies
 * host memory only, waits at most 1.5 s, once. */
void netplay_on_crash(void);
void netplay_on_pad_read(uint32_t actor);           /* sub_001BAA00: a fighter reads its pad */
/* Seed arguments on the stack at `esp` (after the return address); `which`
 * names the call site so the two sides derive the same value for it. */
void netplay_on_seed(uint32_t esp, int words, uint32_t which);

#ifdef __cplusplus
}
#endif

#endif
