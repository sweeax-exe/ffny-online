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
#include "netplay_ranked.h"
#include "netplay_draft_rules.h"

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
#define NETPLAY_MAX_SPECTATORS 16  /* spectators a room names (docs/research/spectator-mode.md) */
#define NETPLAY_SLOT_SPECTATOR 0xFD     /* NetRoom.you of a spectator */

/* King of the Hill (docs/research/king-of-the-hill.md): NetRoomSettings.mode of such a room; every
 * fight in it is One on One. The venue is voted on by the two fighters, or drawn at random. */
#define NETPLAY_MODE_KOTH        0x40
#define NETPLAY_MODE_KOTH_RANDOM 0x41
#define NETPLAY_IS_KOTH(mode)    ((mode) == NETPLAY_MODE_KOTH || (mode) == NETPLAY_MODE_KOTH_RANDOM)
/* FFA rotation (build 66): a room of up to eight with a line, as on the hill, where up to four fight a
 * Free for All (3-Way with three, One on One with two), the winner stays on and the next in the line
 * are called; the venue is drawn at random. Only on relays answering CAPS with NETPLAY_CAP_FFAROT. */
#define NETPLAY_MODE_FFAROT      0x42
#define NETPLAY_FFAROT_SLOTS     8
#define NETPLAY_IS_FFAROT(mode)  ((mode) == NETPLAY_MODE_FFAROT)
/* A room with a line (HILL): King of the Hill or FFA rotation. */
#define NETPLAY_IS_HILL(mode)    (NETPLAY_IS_KOTH(mode) || NETPLAY_IS_FFAROT(mode))

/* ---- the online hub: rooms, quick match, venue voting (docs/netplay-protocol.md) ---- */

typedef struct NetRoomSettings {
    char name[25];
    int is_public;          /* listed; join by code only when 0 */
    int mode;               /* front-end match type, 0 One on One; or NETPLAY_MODE_KOTH*, NETPLAY_MODE_FFAROT */
    int max;                /* players 2..4; King of the Hill 2..10; FFA rotation 2..8 */
    int rounds;             /* 0 the game's default */
    int pool_count;         /* venues to vote on (venue.xml feID) */
    unsigned char pool[32];
    int cpu_level;          /* build 67: the CPU fighters' level, NETPLAY_CPU_* (relays with NETPLAY_CAP_CPULEVEL) */
    int fighter_rules;      /* build 68: FIGHTER RULES, NETPLAY_RULES_* bits (relays with NETPLAY_CAP_ROOMRULES) */
    int max_ping;           /* build 68: MAX PING in ms, 0 off: the relay refuses joins over it */
} NetRoomSettings;

/* Room rules (build 68): which fighters a room's players may pick, and the highest ping a joining player
 * may have. The relay enforces both (a pick the rule leaves out plays another fighter) and names the
 * blocked fighters in ROOM (NetRoom.blocked), so CHOOSE FIGHTER greys them out. The banned tier is the
 * server's own list. */
#define NETPLAY_RULES_ALL        0
#define NETPLAY_RULES_NO_BANNED  1      /* no fighter of the server's banned tier */
#define NETPLAY_RULES_NO_CREATED 2      /* no created fighter */
#define NETPLAY_RULES_STOCK      3      /* both: stock fighters, no banned tier */
#define NETPLAY_MAX_BLOCKED      128

/* The CPU level of a 3-Way's or Free for All's CPU fighters (build 67): the game's own CPU difficulty
 * (its Battle mode's Easy / Normal / Hard), counted from the default down. Online CPU fighters always
 * played the game's Hard, which stays the default. Every PC of a match takes it from START. */
#define NETPLAY_CPU_HARD   0
#define NETPLAY_CPU_NORMAL 1
#define NETPLAY_CPU_EASY   2
#define NETPLAY_CPU_FILL(mode) ((mode) == 2 || (mode) == 3)    /* 3-Way, Free for All: free places go to CPUs */

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
    int ready, fighter, voted;      /* voted: candidate index, NETPLAY_VOTE_RANDOM, -1 not yet */
} NetRoomPlayer;

/* King of the Hill and FFA rotation: the line (HILL), in line order; line[0] is the king (FFA rotation:
 * the last winner, who stays on) when there is one. */
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
    int watch_venue, watch_count, watch_slots[NETPLAY_MAX_PLAYERS], watch_fighters[NETPLAY_MAX_PLAYERS];
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
    NetHill hill;                   /* King of the Hill and FFA rotation rooms only */
    int spectator_count;            /* spectator mode: who watches (relays with NETPLAY_CAP_SPECTATE) */
    char spectators[NETPLAY_MAX_SPECTATORS][25];
    char error[96];                 /* the last refusal, "" if none */
    unsigned version;               /* changes whenever anything here does */
    int blocked_count;              /* build 68: the fighters this room's rules leave out (NETPLAY_CREATED_FIGHTER */
    unsigned char blocked[NETPLAY_MAX_BLOCKED];     /* for created fighters), as the relay names them */
} NetRoom;

/* What the relay supports (its answer to CAPS): -1 not known yet (or no answer: an older relay,
 * then 0), else the bits; bit 0 King of the Hill, bit 2 YIELD (relays since build 36). */
#define NETPLAY_CAP_KOTH 1
#define NETPLAY_CAP_ROLLBACK 2     /* the relay lets matches roll back */
#define NETPLAY_CAP_YIELD 4
#define NETPLAY_CAP_SPECTATE 16    /* spectator mode: rooms can be watched (netplay_lobby_spectate) */
#define NETPLAY_CAP_FFAROT 8192    /* FFA rotation rooms (NETPLAY_MODE_FFAROT, build 66) */
#define NETPLAY_CAP_RANDOM_VENUE 16384  /* RANDOM in the venue vote (build 67): netplay_lobby_vote(NETPLAY_VOTE_RANDOM) */
/* NetRoomPlayer.voted and netplay_lobby_vote(): RANDOM, the relay draws the venue from the room's pool when
 * it wins. Only with relays answering NETPLAY_CAP_RANDOM_VENUE. */
#define NETPLAY_VOTE_RANDOM 0xFE
#define NETPLAY_CAP_CPULEVEL 32768 /* the room's CPU level (NetRoomSettings.cpu_level, build 67) */
#define NETPLAY_CAP_ROOMRULES 65536 /* room rules (NetRoomSettings.fighter_rules, max_ping, build 68) */
int netplay_lobby_caps(void);
/* 1 when the relay refused the hub session because this PC already has a live session there (ERROR 11,
 * build 46 relays): another copy of the game is online on this PC. Not a connection problem. */
int netplay_online_twice(void);

/* Room chat (the Social Update, stage 2b): lines through the relay to everyone in this PC's room, the
 * spectators included, in the room and during its matches. Only with relays answering CAPS bit 7
 * (NETPLAY_CAP_CHAT); a relay without it never sends a line. It is relay-only: it works whether the
 * account service's social features are on or not. Never part of a match's input or checksums. */
#define NETPLAY_CAP_CHAT 128
#define NETPLAY_CHAT_LINES 40
typedef struct NetChatLine {
    unsigned seq;                   /* 1, 2, ... in the order they arrived (this session) */
    int slot;                       /* the sender's room slot; NETPLAY_SLOT_SPECTATOR; -1 a system line */
    unsigned account;               /* the sender's account as the relay knows it, 0 none */
    int spectator, system, mine;    /* a spectator's; the relay's own (a refusal, to this PC only); this PC's */
    int history;                    /* said before this PC came in (the relay's last 50 lines of the room); at 0 */
    char name[33];                  /* as the relay knows it (the account's name), never the game's say */
    char text[201];
    unsigned long long at;          /* GetTickCount64 when it arrived */
    long long unix_at;              /* time() then, for the clock */
} NetChatLine;
/* This room's lines, oldest first (at most max, the newest kept); *last_seq: the newest line's seq
 * (0 none). Returns the count; 0 out of a room. */
int netplay_room_chat(NetChatLine *out, int max, unsigned *last_seq);
/* 1 this PC can write in its room's chat; 0 it reads only (why says so: not signed in, muted,
 * checking the account); -1 no room chat here (no room, or a relay without it). why may be NULL. */
int netplay_room_chat_can_write(char *why, int size);
/* Queues a line (UTF-8, 1..200 bytes; the network thread sends it and repeats it until the relay's
 * echo comes). Returns 1 when queued. */
int netplay_room_chat_send(const char *text);

/* Crew battles (the Social Update, stage 3): a King of the Hill room by crews on relays answering CAPS
 * bit 8. The battle comes from the account service (social.h); CREWJOIN enters its room once the relay
 * knows this account (room chat's IDENT). CREW is the battle's state for the crew battle screen. */
#define NETPLAY_CAP_CREW 256
/* Rollback everywhere (build 52): a relay answering CAPS bit 9 sends the modes in which rollback is
 * mandatory, whatever the players' setting says (0: none, the setting decides). A room's mode falls in
 * one class: a ranked set, a crew battle, a King of the Hill fight, Team, a 3- or 4-way, else 1v1. */
#define NETPLAY_CAP_RB_MODES 512
#define NETPLAY_RB_1V1    1u
#define NETPLAY_RB_TEAM   2u
#define NETPLAY_RB_FFA    4u
#define NETPLAY_RB_KOTH   8u
#define NETPLAY_RB_CREW   16u
#define NETPLAY_RB_RANKED 32u
#define NETPLAY_RB_ALL    63u
/* 1 when the relay makes rollback mandatory for the mode of this PC's room (and allows rollback): the
 * room shows a ROLLBACK tag instead of each player's choice. */
int netplay_room_rollback_mandatory(void);
#define NETPLAY_CREW_MAX 5
enum NetCrewMemberState { NETCREW_WAITING = 0, NETCREW_FIGHTING, NETCREW_OUT, NETCREW_GONE };
enum NetCrewState { NETCREW_FORMING = 0, NETCREW_RUNNING, NETCREW_OVER };
typedef struct NetCrewEntry {
    int slot;                       /* room slot, -1 gone */
    int state;                      /* NETCREW_WAITING .. NETCREW_GONE */
    int wins;
    char name[25];
} NetCrewEntry;
typedef struct NetCrewSide {
    char tag[8], name[32];
    int score;                      /* fights won */
    int count;
    NetCrewEntry lineup[NETPLAY_CREW_MAX];
} NetCrewSide;
typedef struct NetCrew {
    unsigned battle;
    int format, state, winner;      /* winner: 1 or 2 (side), 0 none or a draw */
    int secs_left;                  /* forming: seconds until it starts with what is there, as of `received` */
    int watchers;                   /* room members on the bench */
    unsigned long long received;
    NetCrewSide sides[2];
} NetCrew;
/* Enter crew battle `battle`'s room (repeated by the network thread until the relay seats this PC);
 * 0 stops asking. */
void netplay_lobby_crew_join(unsigned battle);
/* The battle of this PC's room; 0 when it is no crew battle room (or no CREW came yet). */
int netplay_lobby_crew(NetCrew *out);

/* Tournament sets (relays answering CAPS bit 12, only with an account service): a tournament match's
 * room runs a set by the tournament's rules. The relay decides every step (the coin toss, the start
 * stage, the loser's stage pick, Dave's rule, the winner's fighter lock, the score); the game shows
 * TSET and sends the choices: TJOIN enters the player's match (0: "my match that is ready") once the
 * relay knows this account (room chat's IDENT), TPICK is the loser's stage. docs/netplay.md. */
#define NETPLAY_CAP_TSET 4096
#define NETPLAY_TSET_CHOICES 32
#define NETPLAY_TSET_GAMES 16
enum NetTSetState { NETTSET_WAITING = 0, NETTSET_SELECT, NETTSET_PICK, NETTSET_PLAYING, NETTSET_OVER, NETTSET_REFUSED };
#define NETTSET_LOSER_PICKS 1u
#define NETTSET_DAVES       2u
#define NETTSET_WINNER_LOCK 4u
#define NETTSET_COIN_TOSS   8u
#define NETTSET_STOCK_ONLY  16u
#define NETTSET_NONE  0xFF          /* first, order, picker, a lock: none; winner: still running */
#define NETTSET_LEVEL 0xFE          /* winner: the set ended level (the host decides) */
enum NetTSetHow { NETTSET_WIN = 1, NETTSET_DRAW = 2, NETTSET_LEFT = 3, NETTSET_FORFEIT = 4 };
typedef struct NetTSetGame {
    int venue, winner;              /* winner: a slot, NETTSET_NONE a draw */
    int fighters[2];
    int how;                        /* NETTSET_WIN .. NETTSET_FORFEIT */
} NetTSetGame;
typedef struct NetTSet {
    unsigned match;
    int best_of, flags, state, game;    /* game: the one being set up or played (1-based); over: the last */
    int wins[2];                    /* by room slot (slot 0 is side 1) */
    int first, order, picker;       /* coin toss; whose READY counts first this game; the stage picker */
    int secs;                       /* as of `received`: pick time left, or until a missing side is DQ'd */
    int venue;                      /* this game's venue (0 until known) */
    int locks[2];                   /* by slot: the fighter that slot must play, NETTSET_NONE free */
    int winner;
    int choice_count;
    int choices[NETPLAY_TSET_CHOICES];
    int game_count;
    NetTSetGame games[NETPLAY_TSET_GAMES];
    char round[65], why[65];
    unsigned long long received;
} NetTSet;
/* Enter tournament match `match`'s room (0: this account's ready match); repeated by the network
 * thread until the relay seats this PC or refuses (netplay_lobby_tset_refusal). */
void netplay_lobby_tset_join(unsigned match);
/* The set of this PC's room; 0 when it is no tournament room (or no TSET came yet). */
int netplay_lobby_tset(NetTSet *out);
/* The stage pick (only the picker, only one of `choices`; the relay refuses anything else). */
void netplay_lobby_tset_pick(int venue);
/* The last TJOIN refusal's sentence (the relay's or the account service's); returns a number that
 * changes with each refusal (0: none yet). */
unsigned netplay_lobby_tset_refusal(char *why, int size);
/* 1 while a TJOIN waits for the relay's answer. */
int netplay_lobby_tset_joining(void);

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
/* Spectator mode (docs/research/spectator-mode.md): watch the room with this code; the room's
 * NetRoom.you is then NETPLAY_SLOT_SPECTATOR and every match that starts in it is run here from the
 * players' inputs, with no local player. Only when netplay_lobby_caps() has NETPLAY_CAP_SPECTATE. */
void netplay_lobby_spectate(const char *code);
/* Tests: the fights of their START a spectator that came late did not watch (the test knockout's "alt"
 * counts the fights as the players count them). */
int netplay_fights_skipped(void);
int netplay_spectating(void);   /* 1 while this PC watches a room or one of its matches */
/* 1 while this PC only listens to the room's voice chat: a spectator, or a watcher in an ordinary room.
 * A hill room's waiting players watch as members and still talk among themselves (build 70). */
int netplay_listen_only(void);
/* Build 70: live watching in King of the Hill and FFA rotation rooms (docs/research/king-of-the-hill.md
 * 9.2). A waiting player's game runs the fight it gets a WATCH for from the fighters' inputs, as a
 * spectator's does, when the relay answers NETPLAY_CAP_HILL_WATCH and the setting [online] hill_watch
 * is on. netplay_hill_watch fills the spectator HUD over that fight and returns 1 while it is up. */
#define NETPLAY_CAP_HILL_WATCH 131072
typedef struct {
    int count;                                  /* the fighters, 2..4, in their positions */
    int slot[NETPLAY_MAX_PLAYERS];              /* their room slots */
    char name[NETPLAY_MAX_PLAYERS][25];
    int king, streak;                           /* the king's position as the fight started (-1 none), its streak */
    int called;                                 /* this PC is called for the next fight: on its way out */
    int leaving;                                /* the watched fight is being left for the hill screen */
} NetHillWatch;
int netplay_hill_watch(NetHillWatch *out);
/* Build 73: the room menu between hill fights (King of the Hill and FFA rotation, not crew battles). On this
 * PC's own fight's results START goes to the room screen and START held leaves the room; START from a watched
 * fight goes to the room screen too (held: leaves). netplay_hill_menu fills what the screens show of it and
 * returns 1 while the results strip is up (this PC's game on its own hill fight's results). */
typedef struct {
    int results;                /* on its own hill fight's results, from the summary on: the strip shows */
    int next_set;               /* its next fight is set already (its START came): the room menu comes after it */
    int room_req;               /* START asked for the room screen (on the way, or there until its next fight) */
    int room_next;              /* ... asked for while the next fight was set: after that one */
    int auto_ms;                /* called on its results: it readies itself in this many ms (-1: not) */
    int hold_ms, hold_total_ms; /* START held this long towards leaving the room, of the hold that leaves */
    unsigned stopped_seed;      /* the watched fight START left for the room screen (0: none) */
    int pick;                   /* this PC's pick the room does not have yet (-1: none) */
} NetHillMenu;
int netplay_hill_menu(NetHillMenu *out);
/* Tests and pad scripts: 1 while the game runs a fight this PC only watches in a hill room; the front
 * end's calls then are not anchors for a pad script (a copy's presses count its own fights). */
int netplay_hill_watch_quiet(void);
/* The account report key of this PC's last room match ("ROOM12-1A2B3C4D-1"; "" before the first), for
 * REPORT PLAYER. Returns its length. */
int netplay_last_match_key(char *out, int size);
void netplay_lobby_settings(const NetRoomSettings *settings);   /* host only */
void netplay_lobby_ready(int ready, int fighter);
void netplay_lobby_vote(int choice);
void netplay_lobby_leave(void);
/* King of the Hill: give up this PC's place in the line (to the back; a king gives up the crown).
 * Only when netplay_lobby_caps() has NETPLAY_CAP_YIELD and this PC neither votes nor fights. */
void netplay_lobby_yield(void);
void netplay_lobby_room(NetRoom *out);

/* Ranked (One on One against a player of a similar rating; signed-in players only). The search
 * runs until a match is found (the ranked room's ROOM arrives, both players ready, the vote
 * starts) or netplay_lobby_leave. Ranked phase 2a: three modes (netplay_ranked.h), the long
 * RANKED form, ACCEPT, MOVE to the host relay, and RANKINFO in the set's room. */
enum NetQueueState {
    NETQUEUE_NONE = -1,     /* not searching */
    NETQUEUE_CHECKING = 0,  /* the server checks the account */
    NETQUEUE_SEARCHING = 1,
    NETQUEUE_REFUSED = 2,   /* see the text */
    NETQUEUE_OFF = 3,
    NETQUEUE_FOUND = 4,
    NETQUEUE_TICKET = 5,    /* asking the account service for a ticket */
    NETQUEUE_ACCEPT = 6,    /* a set is found: accept it (netplay_lobby_accept) within the seconds left */
    NETQUEUE_MOVE = 7       /* the set is hosted on another relay: netservers_ranked_move */
};
/* Classic only (the original entry and the tests' RECOMP_NETPLAY=ranked without RECOMP_TEST_RANKED_MODES). */
void netplay_lobby_ranked(int fighter);
/* The search in modes (bit per NETRANK_* mode) with primary first; fighter 99 (a created fighter)
 * plays in Open only. */
void netplay_lobby_ranked_ex(int fighter, unsigned modes, int primary);
/* After a MOVE, on the host relay: the same search again with the reservation code. */
void netplay_lobby_ranked_again(const char *code);
/* netservers_ranked_move took the MOVE (1: the hub moves; 0: it cannot, the search ends). */
void netplay_lobby_ranked_moved(int ok);
/* Answers a found set (NETQUEUE_ACCEPT): 1 accept, 0 decline (a dodge). */
void netplay_lobby_accept(int yes);
/* The answer given to the found set shown now: 1 accepted, 0 declined, -1 none yet. */
int netplay_lobby_accepted(void);
/* The long QUEUE's part (counts per mode, the found set, a move); 0 when there is none. */
int netplay_lobby_queue_ext(NetQueueExt *out);
/* RANKINFO of the ranked room this PC is in (ranks, the set's score); 0 none. */
int netplay_lobby_rankinfo(NetRankInfo *out);
/* The ranked set's opponent's name in room `room` (kept from the RANKINFOs while it was there, so
 * also after it left); 0 and "" none. */
int netplay_lobby_rank_opponent(const char *room, char *out, int size);
/* DRAFT of the Draft set's room this PC is in (ranked phase 2b, netplay_draft_rules.h); 0 none. */
int netplay_lobby_draft(NetDraft *out);
/* DRAFTACT: a ban, strike, pick, venue or cursor move (NETDRAFT_ACT_*) in step. */
void netplay_lobby_draft_act(int step, int action, int id);
/* The queue state; waited (seconds), searching (players in the queue) and text (why refused)
 * when not NULL. */
int netplay_lobby_queue(int *waited, int *searching, char *text, int size);
int netplay_in_lobby(void);     /* 1 while the session came from netplay_lobby */
/* The relay address of the hub session ("host:port" as given to netplay_lobby); 0 when there is
 * none (offline, or a failed connection). */
int netplay_lobby_address(char *out, int size);
int netplay_rtt(void);          /* this PC's last ping to its relay in ms, -1 unknown */
void netplay_build_fingerprint(uint8_t out[8]);     /* HELLO's build field: rooms meet only the same one */
/* The fight files' print (fighters, moves, AI, database, animation archives), made once on a background
 * thread; main.c starts it at boot, the network thread waits for it only if it still runs. */
void netplay_data_print_start(void);
/* A room member's ping to the relay in ms, as it reports it (relay slot), -1 not known; this PC's
 * own slot gives netplay_rtt(). Players report it every two seconds in a room (builds from 30). */
int netplay_room_ping(int slot);
/* 1 while the relay still has this PC's room playing although this PC is back from the match (it sent
 * OVER): another player is still on the results, or its game hangs there. `slots` (may be NULL) gets the
 * room slots of the players still on the results as far as this PC knows (bit per slot; 0 when it does
 * not know who). READY is refused then; the room page says what it waits for. */
int netplay_room_results_wait(unsigned *slots);
/* The relay's maintenance countdown (a restart is planned; the relay holds new fights until then): the
 * seconds left (0: due now), its text in `text` ("Server update"); -1 when no countdown runs. */
int netplay_maintenance(char *text, int size);
/* A room member's rollback (relay slot), as its PC reports it every two seconds in a room (build 46 on):
 * -1 not known; else bit 0 it rolls back in the next match, bit 1 its setting is on but its session runs
 * without rollback (a restart would use it). A match rolls back only when every player's bit 0 is set. */
int netplay_room_rollback(int slot);

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

/* 1 from a hub room's START until the game is back in the room: the match is automatch's, and the
 * menu's own match-setup calls are dropped (automatch_hold_funccall). */
int netplay_match_locked(void);

/* 1 while a fight runs, online or not (the game's match phase): no notices over it. */
int netplay_in_fight(void);
/* 1 while this PC (a fighter of a hub room's match) is on the match's summary and results: its own menu
 * there, the fight over (the quick-chat wheel may open; never in the fight). */
int netplay_on_results(void);
int netplay_rerunning(void);    /* 1 while rollback runs steps again */
/* For diagnostics: one line on the online fight's step, rollback state and counters. */
int netplay_rollback_brief(char *out, int size);
int netplay_local_slot(void);   /* tests: this PC's fighter slot in a lockstep fight, -1 otherwise */

/* Voice chat (voice.c): 1 while this PC is in a room of the online hub (its lobby or its match); a
 * voice packet to every other member; the name of the member in relay slot `slot` (0 if none). */
int netplay_voice_room(void);
void netplay_send_voice(const uint8_t *payload, int len);
int netplay_room_name(int slot, char *out, int size);

/* Created fighters (created_fighter.c, docs/research/created-fighter-online.md). The fighter id a
 * player sends to play the fighter they made in Story mode. */
#define NETPLAY_CREATED_FIGHTER 99
/* 1 when this player may pick their created fighter now: the relay's switch is on and it keeps
 * this player's record. */
int netplay_created_available(void);
/* The player picks created fighter `index` of cp_local_list (created_fighter.h): remembered, and
 * uploaded to the relay in place of the one it keeps (one record per player, as before). 0 if
 * there is no such fighter. */
int netplay_created_pick(int index);
/* The checked record of match position `pos` as START named it (CP_REC_LEN bytes into out); 0 if
 * that position plays no created fighter or the record is not here. */
int netplay_created_record(int pos, uint8_t *out);
/* The created fighter of the room member in relay slot `slot` (this player's own included): its
 * name and fighting styles; 0 if the member has none. */
int netplay_slot_created(int slot, char *name, int size, const char **style);

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
/* Every front-end call (automatch_hold_funccall): a spectator's game goes through the summary of a
 * watched match by itself from its first Game.GetMatchSummary on. */
void netplay_spectator_funccall(const char *text);
void netplay_results_funccall(const char *text);   /* the results' No (the tip screen): said to the others at once */
int netplay_toast(char *text, int size);

/* The pre-fight versus screen (online_screen.cpp) of a room's match: what its START set up, copied
 * out of the session. Returns 1 while the screen is to be drawn: from the START (match found) until
 * the fight's first steps have run on every PC (the step after the input delay, which waits for the
 * slowest loader), at least VS_MIN_MS, then a short fade. It only reads: nothing here touches the
 * match, its inputs or its timing. Never offline, in Story or for a King of the Hill watcher. */
typedef struct NetVersus {
    int count;                  /* fighters, 2..4 */
    int fighter[4];             /* fighter.xml IDs */
    char name[4][25];           /* each player's room name; "" for a CPU */
    int cpu[4];                 /* 1: a CPU takes the place */
    int ping[4];                /* each player's ping to the server, as it reported it; -1 unknown */
    int you;                    /* this PC's position */
    int type, venue;            /* front-end match type, venue feID */
    int room_mode;              /* the room's mode: NETPLAY_MODE_KOTH* for King of the Hill */
    int king, streak;           /* King of the Hill: the king's position (-1 none), wins in a row */
    char room[25];              /* the room's name */
    int ms;                     /* since the START */
    int loaded;                 /* this PC's fight is set up; the others may still be loading */
    float alpha;                /* 1, falling to 0 over the fade */
    char cp_name[4][17];        /* a created fighter (fighter 99): its name (up to 16, CP_NAME_MAX); "" otherwise */
    char cp_style[4][64];       /* and its fighting styles ("KICKBOXING / STREETFIGHTING") */
} NetVersus;
int netplay_versus(NetVersus *out);
/* Discord's presence (discord_presence.c): the hub room's match as its START set it up, from the
 * START until the game is back in the room, with the same fields as netplay_versus (ms: since the
 * START; alpha 1). Only reads, from any thread. 0 when there is no such match. */
int netplay_match_info(NetVersus *out);
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
