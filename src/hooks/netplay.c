/* Online play through a relay server. See netplay.h, docs/netplay.md and the wire
 * format in docs/netplay-protocol.md.
 *
 * Threads. A network thread owns the socket: it talks to the relay, keeps the
 * session alive, streams this PC's controller and stores what arrives. The game
 * thread calls the hooks below from the title's own functions; a fight step waits
 * there until every other player's input for that step has arrived. One critical
 * section covers the shared state.
 *
 * Two ways in. netplay_host/netplay_join make a two-player room by code; both
 * players then pick the match in the menus. netplay_lobby enters the hub: rooms,
 * quick match and the venue vote run on the server, and its START names the
 * match, which automatch.c sets up on every PC from the main menu.
 *
 * Lockstep. Fight step s uses, for every player, the pad-table entry (all 32
 * bytes: buttons, a second word, both sticks) that the player's PC captured
 * `delay` steps earlier. All PCs therefore feed the same entries to the same
 * steps, whatever their frame timing, and the first `delay` steps of a match get
 * an empty entry on all. The title polls the pad once per pass and may run
 * several steps a pass; the last word of an entry (always 0 from the poller) is
 * set to a marker when an entry is injected, so a reader can tell a fresh poll
 * from what it injected itself. A player is a position 0..players-1, the game's
 * player number; in a hub room positions follow the room slots in order. */
#define _CRT_SECURE_NO_WARNINGS
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif
#include "host.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "netplay.h"
#include "updater.h"
#include "automatch.h"
#include "online_data.h"
#include "account.h"
#include "rollback.h"
#include "test_observations.h"
#include "netplay_servers.h"
#include "netplay_check.h"
#include "anticheat.h"
#include "created_fighter.h"
#include "pc_input.h"

#define G_MATCH_PHASE   0x003B8F30u     /* test_telemetry.c G_PHASE */
#define G_MATCH_WINNER  0x003B8F40u     /* test_telemetry.c G_WINNER: the winner's fighter slot */
#define G_MATCH_TWIN   0x003B8F3Cu     /* the winner slot the decision writes first (phase 2 copies it to G_MATCH_WINNER) */
#define G_MATCH_FLAGS  0x003B9054u     /* bit 0x08: the match is over (sticky until the next match) */
#define G_RESULTS      0x003BE130u     /* the result history: 16-byte records, u16 code at +0xE */
#define G_RESULT_COUNT 0x003BE624u
#define RESULTS_MAX    ((G_RESULT_COUNT - G_RESULTS) / 0x10u)
#define G_CROWD_RECORDS 0x003494E0u     /* count of the crowd's "audience public" records */

#if !defined(_WIN32)

int netplay_host(const char *s, const char *n, int d) { (void)s; (void)n; (void)d; return -1; }
int netplay_join(const char *s, const char *n, const char *r) { (void)s; (void)n; (void)r; return -1; }
void netplay_stop(void) {}
enum NetplayState netplay_state(void) { return NETPLAY_OFF; }
void netplay_status(char *t, int size) { if (size > 0) snprintf(t, (size_t)size, "Online play is Windows only for now."); }
void netplay_room(char *c, int size) { if (size > 0) c[0] = 0; }
void netplay_autostart(void) {}
int netplay_active(void) { return 0; }
int netplay_in_fight(void) { return 0; }
int netplay_match_locked(void) { return 0; }
int netplay_voice_room(void) { return 0; }
void netplay_send_voice(const uint8_t *p, int n) { (void)p; (void)n; }
int netplay_room_name(int s, char *o, int n) { (void)s; if (n > 0) o[0] = 0; return 0; }
int netplay_created_record(int p, uint8_t *o) { (void)p; (void)o; return 0; }
int netplay_slot_created(int s, char *n, int ns, const char **st) { (void)s; if (ns > 0) n[0] = 0; if (st) *st = ""; return 0; }
int netplay_created_available(void) { return 0; }
int netplay_created_pick(int i) { (void)i; return 0; }
int netplay_lobby(const char *s, const char *n) { (void)s; (void)n; return -1; }
void netplay_lobby_list(int m) { (void)m; }
void netplay_lobby_who(void) {}
int netplay_lobby_players(NetPlayer *o, int m, int *a, int *b) { (void)o; (void)m; if (a) *a = -1; if (b) *b = 0; return -1; }
int netplay_lobby_rooms(NetRoomInfo *o, int m, unsigned *v) { (void)o; (void)m; if (v) *v = 0; return 0; }
void netplay_lobby_create(const NetRoomSettings *s) { (void)s; }
void netplay_lobby_join(const char *c) { (void)c; }
void netplay_lobby_quick(int m, int x) { (void)m; (void)x; }
void netplay_lobby_spectate(const char *c) { (void)c; }
int netplay_spectating(void) { return 0; }
void netplay_lobby_settings(const NetRoomSettings *s) { (void)s; }
void netplay_lobby_ready(int r, int f) { (void)r; (void)f; }
void netplay_lobby_vote(int c) { (void)c; }
void netplay_lobby_leave(void) {}
void netplay_lobby_yield(void) {}
void netplay_lobby_room(NetRoom *o) { memset(o, 0, sizeof(*o)); }
void netplay_lobby_ranked(int f) { (void)f; }
void netplay_lobby_ranked_ex(int f, unsigned m, int p) { (void)f; (void)m; (void)p; }
void netplay_lobby_ranked_again(const char *c) { (void)c; }
void netplay_lobby_ranked_moved(int ok) { (void)ok; }
void netplay_lobby_accept(int y) { (void)y; }
int netplay_lobby_accepted(void) { return -1; }
int netplay_lobby_queue_ext(NetQueueExt *o) { (void)o; return 0; }
int netplay_lobby_rankinfo(NetRankInfo *o) { (void)o; return 0; }
int netplay_lobby_draft(NetDraft *o) { memset(o, 0, sizeof(*o)); return 0; }
void netplay_lobby_draft_act(int s, int a, int i) { (void)s; (void)a; (void)i; }
int netplay_lobby_queue(int *w, int *s, char *t, int n) { (void)w; (void)s; if (t && n > 0) t[0] = 0; return NETQUEUE_NONE; }
int netplay_in_lobby(void) { return 0; }
int netplay_lobby_address(char *o, int n) { (void)o; (void)n; return 0; }
int netplay_rtt(void) { return -1; }
void netplay_build_fingerprint(uint8_t o[8]) { memset(o, 0, 8); }
int netplay_room_ping(int s) { (void)s; return -1; }
int netplay_lobby_caps(void) { return 0; }
void netplay_sync_point(void) {}
void netplay_on_main_menu(void) {}
int netplay_match_abandoned(void) { return 0; }
void netplay_on_match_start(void) {}
void netplay_on_pad_pass(void) {}
void netplay_on_step(void) {}
void netplay_on_update(void) {}
extern void sub_00137010(void);
void netplay_sound_command(void) { sub_00137010(); }
void *netplay_sound_handler(uint32_t va) { (void)va; return NULL; }
void netplay_on_pad_read(uint32_t a) { (void)a; }
int netplay_local_slot(void) { return -1; }
void netplay_on_seed(uint32_t e, int w, uint32_t x) { (void)e; (void)w; (void)x; }
void netplay_on_result(uint32_t w, uint32_t l, uint32_t c) { (void)w; (void)l; (void)c; }
void netplay_on_crash(void) {}
int netplay_toast(char *t, int n) { (void)t; (void)n; return 0; }
int netplay_versus(NetVersus *o) { (void)o; return 0; }
int netplay_match_info(NetVersus *o) { (void)o; return 0; }
int netplay_rematch_request(void) { return 0; }
void netplay_spectator_funccall(const char *t) { (void)t; }
int netplay_debug_json(char *o, int n) { return snprintf(o, (size_t)n, "{}"); }
int netplay_rollback_brief(char *o, int n) { return snprintf(o, (size_t)n, "offline"); }
extern void sub_00081170(void);
void netplay_crowd_update(void) { sub_00081170(); }

#else

#include "input_host.h"
#include "voice.h"

#ifndef DEFJAM_VERSION
#define DEFJAM_VERSION "dev"
#endif

#define DEFAULT_PORT  27777
#define PROJECT_SERVER_NAME    "play.defjam.online"
#define PROJECT_SERVER_ADDRESS "49.12.78.32"
#define PROJECT_NA_NAME        "na.defjam.online"     /* US East (build 30; "North America" until 37) */
#define PROJECT_NA_ADDRESS     "5.161.206.134"
#define PROJECT_WEST_NAME      "west.defjam.online"   /* US West (build 38) */
#define PROJECT_WEST_ADDRESS   "5.78.214.178"
#define PROTO         1
#define T_HELLO       0x01
#define T_WELCOME     0x02
#define T_PEER        0x03
#define T_DATA        0x04
#define T_PING        0x05
#define T_PONG        0x06
#define T_BYE         0x07
#define T_ERROR       0x08
/* the lobby extension */
#define T_LIST        0x10
#define T_ROOMS       0x11
#define T_CREATE      0x12
#define T_JOIN        0x13
#define T_QUICK       0x14
#define T_SETTINGS    0x15
#define T_READY       0x16
#define T_VOTE        0x17
#define T_LEAVE       0x18
#define T_ROOM        0x19
#define T_START       0x1A
#define T_OVER        0x1B
#define T_WHO         0x1C      /* who is online (relays since build 13) */
#define T_PLAYERS     0x1D
#define T_RANKED      0x1E      /* the ranked queue (relays since build 15) */
#define T_QUEUE       0x1F
#define TICKET_LEN    16        /* RANKED: the account service's one-time ticket */
#define T_ACCEPT      0x2C      /* ranked phase 2a: answer a found set */
#define T_DRAFT       0x2D      /* ranked phase 2b: a Draft set's pick and ban */
#define T_DRAFTACT    0x2E
#define T_RANKINFO    0x2F      /* ranked phase 2a: the set room's ranks and score */
/* King of the Hill (docs/research/king-of-the-hill.md section 8) */
#define T_CAPS        0x20      /* both ways: what this client / the relay supports */
#define T_HILL        0x21      /* the room's line, king and countdowns */
#define T_RESULT      0x22      /* a fighter's outcome: START seed, the winner's room slot (0xFF a draw) */
#define T_WATCH       0x23      /* START's body, for a fight this PC only watches */
#define T_YIELD       0x26      /* give up my spot in the line (relays since build 36, CAPS bit 2) */
#define T_NOTICE      0x27      /* relay -> game: a countdown before a relay restart (build 37 on) */
#define T_FIGHTER     0x28      /* created fighters (build 38): this client's record, u16 length + record */
#define T_RECORD      0x29      /* a room member's record (slot, u16 length, record); slot 0xFE: the relay's copy of ours */
#define T_RECGET      0x2A      /* the record of the member in a slot, again */
#define T_SPECTATE    0x2B      /* watch a room (spectator mode, relays with CAPS bit 4) */
#define SLOT_SPECTATOR 0xFD     /* ROOM `you` of a spectator */
#define RECORD_SELF   0xFE
#define CAPS_KOTH     1u
#define CAPS_ROLLBACK 2u        /* the relay lets matches roll back (its switch; off: lockstep only) */
#define CAPS_CREATED  8u        /* created fighters (client: can play them; relay: its switch is on) */
#define CAPS_SPECTATE 16u       /* spectator mode (docs/research/spectator-mode.md) */
#define CP_WAIT_MS    10000     /* START: how long to wait for a created fighter's missing record */
#define CAPS_TRIES    5         /* CAPS every RETRY_MS; no answer after these: an older relay */
#define WINNER_DRAW   0xFFu
#define RESULT_TRIES  75        /* RESULT every RETRY_MS until a HILL shows the fight over (30 s) */

/* DATA payloads between the game copies (opaque to the relay) */
#define P_GAMEHELLO   1     /* host -> guest: u32 session seed, u8 input delay, u8 lockstep version */
#define P_GAMEACK     2     /* guest -> host */
#define P_LIVEPAD     3     /* u32 sequence, 18 bytes XBOX_GAMEPAD */
#define P_STEPS       4     /* u32 match, u32 last step, u8 count, count x 32 bytes */
#define P_HASH        5     /* u32 match, u32 step, u32 hash; since build 23 also the HASH_PARTS words; since
                               * build 35 the raw fields, pre-fight generators, crowd, the shared generator, time
                               * scale and match generator apart (netplay_check.h); the first 37 bytes unchanged */
#define P_RTT         6     /* hub: u32 START seed, u16 this PC's ping to the relay in ms, u8 rollback,
                               * since build 23 u32 the game files' fingerprint */
#define LOCKSTEP_VERSION 4     /* 3: builds 19-20, rollback on; 4: build 21, rollback off again, reclaim on */

#define MAXP          NETPLAY_MAX_PLAYERS   /* fighters in a match: positions 0..3 */
#define ROOMSLOTS     NETPLAY_ROOM_SLOTS    /* members of a room: relay slots 0..9 */
#define PAD_TABLE     0x003B8F48u
#define ACTORS        0x003B92E0u
#define STRIDE        0x12E8u
#define ENTRY         32
#define MARK          0x594C504Eu   /* "NPLY" in the entry's last word */
#define RING          512           /* steps of input kept per player */
#define REDUNDANT     8             /* steps repeated in every STEPS packet */
#define HASH_EVERY    60
#define P_REMATCH     8             /* u32 START seed, u32 match number: Rematch chosen; 0xFFFFFFFF: back to the room */
#define REMATCH_NO    0xFFFFFFFFu
#define P_PINGINFO    9             /* in a room, every PINGINFO_MS: u16 this PC's ping to the relay (builds from 30) */
#define PINGINFO_MS   2000
#define HASH_PARTS    NETCHECK_PARTS    /* the checksum in parts: fighters 1-4, the AI generators, the rest */
#define STALL_LIMIT_MS 10000        /* mid-fight: a player has gone */
#define START_LIMIT_MS 90000        /* the first steps: another PC may still be loading the venue */
#define DECIDED_LIMIT_MS 1000       /* after the KO: a PC that went on ahead is not a lost player */
#define LOBBY_DELAY   3             /* hub matches: until the delay is agreed (P_RTT) */
#define DELAY_MIN     2
#define DELAY_MAX     10
#define RTT_WAIT_MS   5000          /* at the sync point: how long to wait for every player's ping */
#define RETRY_MS      400           /* lobby requests are repeated until the server answers */
#define RETRIES       8
#define RB_AHEAD      8             /* rollback: steps a PC runs on predicted entries before it waits */
#define RB_LESS       4             /* hub matches where every PC rolls back: the delay is this much shorter */

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static struct {
    volatile LONG state;            /* enum NetplayState */
    volatile LONG stop;
    HANDLE thread;
    SOCKET sock;
    struct sockaddr_storage server;
    int server_len;
    char server_text[128];
    int creating;                   /* code rooms: 1 host, 0 guest */
    int lobby;                      /* 1: the hub */
    char room[7];
    char name[25], peer_name[25];
    uint8_t build[8];
    uint8_t token[8];
    int have_token;
    int slot;                       /* this client's slot in its room */
    char status[192];
    /* session */
    uint32_t session;
    int delay;
    int acked;
    int players, pos;               /* players in the match, this PC's position */
    int fighters;                   /* in the fight, with the CPU ones */
    int pos_of_slot[ROOMSLOTS];     /* room slot -> position, -1 none */
    /* King of the Hill */
    int relay_caps;                 /* the relay's CAPS answer, -1 not yet */
    int caps_tries;
    ULONGLONG caps_sent;
    /* created fighters (created_fighter.c): every room member's record as the relay sent it, this
     * client's own upload, and the records the last START named */
    uint8_t cp_rec[ROOMSLOTS][CP_REC_LEN];
    int cp_have[ROOMSLOTS];
    uint8_t cp_mine[CP_REC_LEN];
    int cp_mine_state;              /* 0 not read, 1 have one, -1 none */
    uint32_t cp_acked;              /* CRC the relay keeps for us, 0 none */
    uint8_t cp_prev[CP_REC_LEN];    /* the record the relay kept before the player picked another */
    uint32_t cp_prev_crc;           /* its CRC, 0 none: a START may still name it */
    ULONGLONG cp_sent;
    int cp_tries;
    uint32_t cp_start_crc[ROOMSLOTS];   /* by room slot: the record START named, 0 none */
    volatile LONG cp_waiting;       /* START waits for records (cp_wait_m) */
    ULONGLONG cp_wait_since, cp_get_sent;
    AutoMatch cp_wait_m;
    int koth_match;                 /* this START is a King of the Hill fight */
    int koth_fights;                /* HILL `fights` when it started: a change means it is over */
    int koth_winner;                /* the RESULT to repeat: the winner's slot, WINNER_DRAW; -1 none */
    int koth_tries;
    ULONGLONG koth_sent;
    int koth_fought, koth_done;     /* the fight was seen fighting; its RESULT was made */
    int koth_ready_for;             /* HILL `fights` + 1 of the call this PC readied for by itself */
    volatile LONG start_deferred;   /* a START taken while the last fight's results still show */
    AutoMatch deferred_m;
    int koth_results;               /* this game still shows the results of a hill fight it fought (until the menu) */
    int koth_quit;                  /* Rematch on those results already took this PC back to the hill */
    ULONGLONG deferred_toast;       /* the last "... is waiting for you" notice */
    int peer_results[MAXP];         /* each position said (P_RTT) it was still on the last fight's results */
    int peer_on_results[MAXP];      /* ... and says so in its latest P_RTT */
    /* controllers */
    XBOX_GAMEPAD remote_pad[MAXP];
    uint32_t remote_seq[MAXP], local_seq;
    DWORD remote_packet[MAXP];
    XBOX_GAMEPAD last_sent;
    ULONGLONG last_pad_sent;
    /* link */
    ULONGLONG last_rx, last_ping, hello_sent, started;
    uint32_t ping_nonce;
    int rtt_ms;
    int rtt_recent[5], rtt_next;    /* the last pings, for a jitter margin */
    /* lockstep (step numbers count from 1 at each match start) */
    volatile LONG lockstep;         /* connected and not abandoned */
    uint32_t match, match_count, step;
    uint8_t latest_local[ENTRY];
    uint8_t in[MAXP][RING][ENTRY];
    uint32_t in_tag[MAXP][RING];    /* step + 1 (0 = empty) */
    uint32_t in_match[MAXP][RING];
    HANDLE arrived;
    /* this PC's checksums and the others', the last 16 each (netplay_check.h; builds before 23 send
     * no parts, builds before 34 no fields) */
    NetCheck local_hash[16], remote_hash[16];
    int desync;
    uint32_t desync_step;
    struct { uint32_t which, count; } seeds[32];
    /* the hub */
    NetRoom roomv;
    NetRoomInfo rooms[32];
    NetPlayer who[40];              /* the last PLAYERS */
    int who_count, who_online, who_rooms;
    int queue_state, queue_waited, queue_searching;     /* the ranked queue (NETQUEUE_*) */
    char queue_text[96];
    NetQueueExt queue_ext;          /* the long QUEUE's part (counts, the found set, a move) */
    int queue_accepted;             /* the answer to the found set shown: 1, 0, -1 none */
    char queue_match[7];            /* the found set it answers */
    int rk_fighter, rk_primary;     /* the search, for the MOVE's second RANKED */
    unsigned rk_modes;
    char rk_code[7];                /* a MOVE's reservation code ("" none) */
    NetRankInfo rank;               /* the last RANKINFO, of the room rank_room */
    char rank_room[7];
    NetDraft draft;                 /* the last DRAFT, of the room draft_room (netplay_draft_rules.h) */
    char draft_room[7];
    int set_match, set_game;        /* this START is a ranked set's game (its RESULT goes out); RANKINFO's game then */
    int room_count;
    unsigned rooms_version;
    int slot_rtt[ROOMSLOTS];        /* each room member's reported ping (P_PINGINFO), -1 none */
    ULONGLONG last_pinginfo;
    uint8_t pend[300];              /* the request waiting for an answer */
    int pend_len, pend_tries;
    ULONGLONG pend_sent;
    int match_live;                 /* START received, OVER not yet sent */
    int match_seen;                 /* the game has started that match */
    int over_pending;
    int start_this_play;            /* a START arrived since the room entered phase 2 */
    int peer_rtt[MAXP];             /* each position's ping as it reported it for this START, -1 not yet */
    int delay_fixed;                /* the match's delay is agreed */
    ULONGLONG last_rtt_sent;
    ULONGLONG last_recover;
    ULONGLONG start_tick;           /* when this PC took the room's START: load times are measured from it */
    int load_done, load_hinted;     /* this START's match is loaded; the long-load notice was shown */
    uint32_t start_seed;
    int start_slots[MAXP];
    /* the room match for its result report (account.c): set at START, sent once */
    AutoMatch report_m;
    char report_names[MAXP][25];
    char report_key[32];
    int reported, fighting_seen, last_late;
    int start_reported;             /* this match's "start" went to the account service */
    int seed_bypass;                /* crowd_reset seeds a generator with its own words */
    uint32_t rematch_want[MAXP];    /* the match number each player chose Rematch after, REMATCH_NO: went back */
    /* rollback (rollback.h): a fighting step runs on a prediction of each entry that has not
     * arrived (that player's last one); when the real one differs, the game goes back to the
     * save state taken before that step and runs the steps again. */
    uint8_t used[MAXP][RING][ENTRY];    /* each other player's entry as its step ran */
    uint32_t used_tag[MAXP][RING];      /* step + 1 */
    uint8_t used_pred[MAXP][RING];      /* 1: a prediction */
    uint8_t used_mixed[MAXP][RING];     /* 1: the step read two different predictions (netplay_on_pad_read) */
    uint8_t step_runs[RING];            /* tests (RECOMP_NETPLAY_FINALLOG): times the step started, its pad reads */
    uint8_t step_reads[RING];
    uint32_t final_step;                /* every step up to this one ran on the real entries */
    uint32_t updates;                   /* game updates since the match's reference copy */
    uint32_t hash_at[RING], hash_tag[RING], hash_sent;
    uint32_t hash_parts[RING][HASH_PARTS];
    uint32_t hash_fields[RING][NETCHECK_SLOTS][NETCHECK_FIELDS];    /* the raw words the hash covers */
    uint32_t hash_gens[RING][NETCHECK_GENS], hash_crowd[RING];      /* steps a checksum goes out for */
    uint32_t hash_split[RING][3];       /* the shared generator, the time scale word, the match generator */
    int rb_on, rb_ref_due, resim, predicting;
    int rb_fought;                      /* the phase word said "fighting" in this match */
    uint32_t rb_decided_step;           /* the step at which this PC saw the match decided (0: not yet) */
    int peer_rb[MAXP];                  /* each position rolls back (its P_RTT says so) */
    int rb_match;                       /* hub: this match rolls back (every peer_rb set at the sync point) */
    struct {
        int valid;
        uint32_t step, update;          /* the step about to run, the update about to start */
        long long test_step;
        uint32_t crowd_gen[6];
        int crowd_gen_on;
        uint32_t seeds[32][2];
    } snap[ROLLBACK_SLOTS];
    unsigned rb_count, rb_steps, rb_deepest, rb_failed;
    double rb_ms_max, rb_ms_total;
    /* the same for this match alone (rb_* above run on for the whole session), the step of its first
     * prediction (0: none yet) and whether its summary line went out */
    struct { unsigned count, steps, deepest, slow; double ms_max, ms_total; uint32_t first_pred; int on, told; unsigned num; } rbm;
    unsigned rb_matches;                /* rollback matches this session (rbm.num) */
    char toast[160];                /* a notice for the fight (netplay_toast), until toast_until */
    ULONGLONG toast_until;
    char notice[96];                /* the relay's maintenance NOTICE text, counted down to notice_at */
    ULONGLONG notice_at, notice_shown;
    /* the versus screen (netplay_versus): shown since vs_start (0: not shown), the fight under way
     * since vs_end (0: not yet); the king's position and streak as of the START */
    ULONGLONG vs_start, vs_end;
    int vs_king, vs_streak;
    /* spectator mode (docs/research/spectator-mode.md): this START came as a WATCH, so this PC has
     * no position (pos -1) and only runs the players' entries; the delay each player announced in
     * its P_RTT (0 none yet); since when the room plays (a lost WATCH is asked for again only at
     * first); the results gone through by itself (A on player 1's port) since spec_results */
    int watching;
    int peer_delay[MAXP];
    ULONGLONG playing_since;
    ULONGLONG spec_results;
    int spec_start_down;
} N;

/* This PC is in its room as a spectator (the relay's ROOM says `you` 0xFD). */
#define SPECTATOR_HERE() (N.roomv.in_room && N.roomv.you == SLOT_SPECTATOR)

/* Rollback (rollback.h) is on with RECOMP_ROLLBACK=on (off by default since build 21); off, every step waits for every entry. */
static int rollback_wanted(void) { return rollback_enabled(); }
/* Whether this match rolls back. A hub match does only when every player has it on, decided with the
 * delay at the sync point from the same P_RTT flags, so every PC reaches the same answer; in build 26
 * a PC with it on rolled back alone, with the full lockstep delay the other PC needed. Code rooms
 * (RECOMP_NETPLAY=host/join, tests only) exchange no flags: the local setting. */
static int match_rolls_back(void) { return N.lobby ? N.rb_match : rollback_wanted(); }
int netplay_rerunning(void) { return N.resim; }
int netplay_local_slot(void) { return N.lockstep && !N.watching ? N.pos : -1; }

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    WSADATA wsa;
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    N.sock = INVALID_SOCKET;
    N.arrived = CreateEventW(NULL, FALSE, FALSE, NULL);
    N.who_online = -1;              /* no PLAYERS yet */
    N.queue_state = NETQUEUE_NONE;
    N.queue_accepted = -1;
    N.relay_caps = -1;
    N.koth_winner = -1;
    return TRUE;
}

static void set_status(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    EnterCriticalSection(&g_cs);
    vsnprintf(N.status, sizeof(N.status), format, args);
    LeaveCriticalSection(&g_cs);
    va_end(args);
    fprintf(stderr, "[NETPLAY] %s\n", N.status);
    fflush(stderr);
}

/* A short notice the overlay shows over the fight: a wait, a desync, a player gone. */
static void toast(unsigned ms, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    EnterCriticalSection(&g_cs);
    vsnprintf(N.toast, sizeof(N.toast), format, args);
    N.toast_until = GetTickCount64() + ms;
    LeaveCriticalSection(&g_cs);
    va_end(args);
    fprintf(stderr, "[NETPLAY] notice: %s\n", N.toast);
}

static const char *player_name(int pos);
static void put32(uint8_t *p, uint32_t v);
static void send_data(const uint8_t *payload, int len);

/* The results screen's Rematch (Game.Rematch()) starts the next match on this PC alone: with one
 * player choosing Rematch and the other No, one game loaded a fight the other never joined and stood
 * on "FIGHT!" for 90 seconds (five F9 reports, 2026-10-23/24). Online it goes through only once every
 * player has chosen it after the same match; until then the call is held, the others are told, and
 * whoever chose first chooses again once they accept. Returns 1 when the call is to be held. */
int netplay_rematch_request(void)
{
    uint8_t p[9];
    int i, all = 1, gone = -1;
    uint32_t after;
    if (!N.lobby) return 0;
    /* King of the Hill: one fight per START, the winner stays on and the hill calls the next pair.
     * Holding Rematch with a notice kept kings on the results pressing it again and again while the
     * next challenger waited, loaded (13, 14 and 61 s in build 33's reports): Rematch now does what
     * No does there, on this game thread inside the front end's call, as the results screen's own
     * No path runs it (MemCard.QuerySave(battle/rewardun.swf), then Game.Quit()). Also after a
     * deferred next fight was called off (match_live is 0 then, koth_results still 1). */
    if (N.koth_results || (N.match_live && N.koth_match) || N.watching) {
        unsigned saved;
        if (N.koth_quit) return 1;      /* already on the way back */
        N.koth_quit = 1;
        fprintf(stderr, "[NETPLAY] King of the Hill: Rematch on the results goes back to the hill (as No)\n");
        saved = defjam_fe_call("MemCard.QuerySave( battle/rewardun.swf )");
        fprintf(stderr, "[NETPLAY] King of the Hill: MemCard.QuerySave -> %08X; Game.Quit()\n", saved);
        defjam_fe_call("Game.Quit()");
        return 1;
    }
    if (!N.match_live || !N.start_seed) return 0;
    EnterCriticalSection(&g_cs);
    after = N.match_count;
    N.rematch_want[N.pos] = after;
    for (i = 0; i < N.players; i++) {
        if (i == N.pos) continue;
        if (N.rematch_want[i] == REMATCH_NO) gone = i;
        if (N.rematch_want[i] != after) all = 0;
    }
    LeaveCriticalSection(&g_cs);
    p[0] = P_REMATCH;
    put32(p + 1, N.start_seed);
    put32(p + 5, after);
    send_data(p, 9);
    if (gone >= 0) {
        toast(8000, "%s went back to the room: choose No to join them.", player_name(gone));
        return 1;
    }
    if (all) {
        fprintf(stderr, "[NETPLAY] rematch: every player chose it\n");
        return 0;
    }
    for (i = 0; i < N.players; i++)
        if (i != N.pos && N.rematch_want[i] != after) break;
    fprintf(stderr, "[NETPLAY] rematch: waiting for player %d to choose it\n", i + 1);
    toast(8000, "Waiting for %s to choose Rematch...", player_name(i));
    return 1;
}

/* Spectator mode: the summary of a watched match is gone through by itself (remote_slot_state taps
 * A on player 1's port until the game is back in the menus). */
void netplay_spectator_funccall(const char *text)
{
    if (!N.watching || N.spec_results || !N.match_seen || strncmp(text, "Game.GetMatchSummary(", 21)) return;
    N.spec_results = GetTickCount64();
    fprintf(stderr, "[NETPLAY] watching: the match's summary; going through it to the room\n");
}

/* This PC left the results for the room: whoever waits on the rematch screen hears it. */
static void rematch_declined(void)
{
    uint8_t p[9];
    if (!N.lobby || !N.start_seed || N.koth_match || N.watching) return;
    p[0] = P_REMATCH;
    put32(p + 1, N.start_seed);
    put32(p + 5, REMATCH_NO);
    send_data(p, 9);
}

int netplay_toast(char *out, int size)
{
    int on;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    on = N.toast[0] && GetTickCount64() < N.toast_until;
    if (on) snprintf(out, size, "%s", N.toast);
    else if (N.watching && N.match_seen && N.lockstep && !N.spec_results && size > 0) {
        /* spectator mode: who fights, over the whole watched fight */
        int i, n = snprintf(out, (size_t)size, "SPECTATING:");
        for (i = 0; i < N.players && i < MAXP && n > 0 && n < size; i++)
            n += snprintf(out + n, (size_t)(size - n), "%s%s", i ? " vs " : " ", N.report_names[i][0] ? N.report_names[i] : "?");
        on = 1;
    }
    LeaveCriticalSection(&g_cs);
    return on;
}

/* The versus screen (netplay.h). Called by the overlay every frame; it reads the session and the
 * step counter and writes only vs_start/vs_end, which nothing in the match reads. */
#define VS_MIN_MS   4000u           /* shown at least this long after the START, when a load is instant */
#define VS_FADE_MS  400u
#define VS_LIMIT_MS 120000u         /* a match that never got going: the screen does not stay up for ever */
int netplay_versus(NetVersus *out)
{
    ULONGLONG now, start, end, from;
    int i, gone, under_way;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    start = N.vs_start;
    if (!N.lobby || !start) return 0;
    now = GetTickCount64();
    EnterCriticalSection(&g_cs);
    /* the match went away (abandoned, a player lost before the fight, the room's match over), or this
     * PC waits on its last fight's results (King of the Hill): no screen */
    gone = !N.match_live || N.start_deferred || N.state != NETPLAY_CONNECTED || now - start > VS_LIMIT_MS;
    /* under way: this START's fight has run the step after the input delay, which waits for every
     * other PC's first entry, so every game is past its load; or the fight went on offline */
    under_way = N.match_seen && (N.step > (uint32_t)N.delay + 1u || !N.lockstep);
    if (gone) {
        N.vs_start = N.vs_end = 0;
        LeaveCriticalSection(&g_cs);
        return 0;
    }
    if (under_way && !N.vs_end) N.vs_end = now;
    end = N.vs_end;
    from = end ? (end > start + VS_MIN_MS ? end : start + VS_MIN_MS) : 0;
    if (end && now >= from + VS_FADE_MS) {
        N.vs_start = N.vs_end = 0;
        LeaveCriticalSection(&g_cs);
        return 0;
    }
    if (out) {
        memset(out, 0, sizeof(*out));
        out->count = N.report_m.count < 2 ? 2 : N.report_m.count > 4 ? 4 : N.report_m.count;
        for (i = 0; i < out->count; i++) {
            out->fighter[i] = N.report_m.fighter[i];
            out->cpu[i] = i >= N.players || N.report_m.pad[i] < 0;
            out->ping[i] = out->cpu[i] ? -1 : N.peer_rtt[i];
            if (!out->cpu[i]) snprintf(out->name[i], sizeof(out->name[i]), "%s", N.report_names[i]);
        }
        for (i = 0; i < out->count; i++) {     /* created fighters: the record START named */
            int slot = N.start_slots[i];
            uint8_t ok[CP_REC_LEN];
            if (out->fighter[i] != CP_FIGHTER_ID || out->cpu[i] || slot < 0 || slot >= ROOMSLOTS) continue;
            if (N.cp_start_crc[slot] && N.cp_have[slot] && cp_record_crc(N.cp_rec[slot]) == N.cp_start_crc[slot] &&
                cp_record_sanitize(N.cp_rec[slot], ok)) {
                cp_record_name(ok, out->cp_name[i], sizeof(out->cp_name[i]));
                snprintf(out->cp_style[i], sizeof(out->cp_style[i]), "%s", cp_record_style(ok));
            }
        }
        out->you = N.pos;
        out->type = N.report_m.type;
        out->venue = N.report_m.venue;
        out->room_mode = N.roomv.settings.mode;
        out->king = N.koth_match ? N.vs_king : -1;
        out->streak = N.vs_streak;
        snprintf(out->room, sizeof(out->room), "%s", N.roomv.settings.name);
        out->ms = (int)(now - start);
        out->loaded = N.match_seen;
        out->alpha = !end || now < from ? 1.0f : 1.0f - (float)(now - from) / (float)VS_FADE_MS;
    }
    LeaveCriticalSection(&g_cs);
    return 1;
}

static const char *player_name(int pos)
{
    return pos >= 0 && pos < MAXP && N.report_names[pos][0] ? N.report_names[pos] : "the other player";
}

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* ---- wire helpers ------------------------------------------------------------ */

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int header(uint8_t *b, uint8_t type)
{
    b[0] = 'D'; b[1] = 'J'; b[2] = 'N'; b[3] = 'P'; b[4] = PROTO; b[5] = type;
    return 6;
}

static void send_raw(const uint8_t *b, int len)
{
    if (N.sock != INVALID_SOCKET)
        sendto(N.sock, (const char *)b, len, 0, (const struct sockaddr *)&N.server, N.server_len);
}

static void send_hello(void)
{
    uint8_t b[64];
    int n = header(b, T_HELLO), len = (int)strlen(N.name);
    b[n++] = N.lobby ? 2 : N.creating ? 0 : 1;
    memset(b + n, 0, 6);
    if (!N.lobby && !N.creating) memcpy(b + n, N.room, 6);
    n += 6;
    memcpy(b + n, N.build, 8); n += 8;
    b[n++] = (uint8_t)len;
    memcpy(b + n, N.name, (size_t)len); n += len;
    {   /* since build 26: the build number, so the relay can turn away builds older than the published one */
        int build = updater_local_build();
        if (build > 0) { put32(b + n, (uint32_t)build); n += 4; }
    }
    send_raw(b, n);
}

static void send_token_only(uint8_t type, uint32_t extra, int with_extra)
{
    uint8_t b[32];
    int n = header(b, type);
    memcpy(b + n, N.token, 8); n += 8;
    if (with_extra) { put32(b + n, extra); n += 4; }
    send_raw(b, n);
}

static void send_data(const uint8_t *payload, int len)
{
    uint8_t b[1200];
    int n = header(b, T_DATA);
    if (!N.have_token || len <= 0 || len > 1150) return;
    memcpy(b + n, N.token, 8); n += 8;
    memcpy(b + n, payload, (size_t)len); n += len;
    send_raw(b, n);
}

/* A hub request: header, token, then `body`. Repeated every RETRY_MS until the server answers
 * (the relay treats a repeat as the same request). A new request replaces a waiting one. */
static void request(uint8_t type, const uint8_t *body, int len)
{
    uint8_t b[sizeof(N.pend)];
    int n;
    if (!N.lobby || !N.have_token || len < 0 || len > (int)sizeof(b) - 14) return;
    n = header(b, type);
    memcpy(b + n, N.token, 8); n += 8;
    if (len) memcpy(b + n, body, (size_t)len);
    n += len;
    EnterCriticalSection(&g_cs);
    memcpy(N.pend, b, (size_t)n);
    N.pend_len = n;
    N.pend_tries = 1;
    N.pend_sent = GetTickCount64();
    N.roomv.error[0] = 0;
    LeaveCriticalSection(&g_cs);
    send_raw(b, n);
}

static void answered(void)
{
    N.pend_len = 0;
}

/* ---- the other players' controllers, as the title's other pads ---------------- */

/* The intros before a lockstep fight: the venue's (CLogic state 1) and the fighters' (state 3).
 * Every PC plays them on its own before the first step, and the title cuts them short when its
 * skip check (sub_00088B90) sees a face button on any port: this PC's pad, or another player's
 * live pad, which arrives whenever the network brings it. So one PC skipped an intro and another
 * did not, or later, and the fighters came out of the intro with different animation state (the
 * pose buffers and the blend from the intro clip into the fight stance; nothing the checksums
 * cover, every checksummed field was equal at the first step). A few steps in the poses parted,
 * then the joint positions the step reads for reach and pushes (fighter +0xDC, +0x834), then the
 * positions: by ulps when the intros ended a little apart, by whole units at step 60 when a
 * player skipped (owner's test 2026-10-28). The open "positions only" desync.
 *
 * Online every PC now skips both intros itself, from their first update: the first other player's
 * port presses A on every second update while one runs, so the skip check sees a press or a
 * release on every update, and the other players' ports read at rest otherwise until the first
 * step. Every PC then ends each intro at the first update the title takes the skip, and nothing
 * pressed on any PC makes a difference. The updates an intro takes can still differ by a few
 * between PCs, but a skipped intro leaves the same state: in tests the joints were equal from the
 * second step on.
 *   RECOMP_NETPLAY_INTROS=play (tests only): intros as before, left to the players.
 *   RECOMP_TEST_INTRO_PRESS=<player index> (tests only): on that PC the first other player's port
 *   holds A during the fighters' intro, as a press of that player arriving there and not on the
 *   other PCs (with =play: the desync on demand). */
#define G_LOGIC_STATE   0x00376F80u     /* CLogic (0x349760) + 0x2D820: 1 venue intro, 3 fighters' intros */
static uint32_t g_intro_state = 0xFFFFFFFFu, g_intro_updates;     /* the pre-fight state, its updates */

static int intro_window(void)       /* from the match start to the first step of a lockstep match */
{
    return N.lockstep && N.load_done && N.step == 0;
}

/* At the start of every game update (netplay_on_update): which pre-fight state runs, how long. */
static void intro_update(void)
{
    uint32_t st;
    if (!intro_window()) { g_intro_state = 0xFFFFFFFFu; return; }
    st = MEM32(G_LOGIC_STATE);
    if (st != g_intro_state) {
        fprintf(stderr, "[NETPLAY] pre-fight state %d after %u updates of state %d\n", (int)st, g_intro_updates,
                (int)g_intro_state);
        g_intro_state = st;
        g_intro_updates = 0;
    }
    else g_intro_updates++;
}

/* Created fighters' intro and victory clips, the same on every PC. A stock fighter has one intro
 * clip; a created fighter (fighter.xml id 87 and up) plays one of five, and the match load picks
 * them per slot (sub_00035ED0: 0x33E9E8 + 4*slot, and the victory clip at 0x340370) from the global
 * generator 0x33DB5C, which is seeded from the wall clock at boot and drawn at host timing. So
 * every PC picked its own: the intro speech of a two-copy test was 537i2 on one and 537i4 on the
 * other, the skipped intros left the fighters in different poses, and the positions parted by
 * ulps within the first seconds (the created-fighter desync of build 38: every real match, 2 in 7
 * on one PC; stock fighters never). sub_00087AF0 reads the slot's value when the intro starts
 * (sub_00035F40) and the victory clip at the end (sub_00035F50). In a lockstep match every PC
 * writes values made from the match's number at the start of every update instead. */
#define INTRO_VARIANTS  0x0033E9E8u     /* 4 x u32, 0..4 */
#define WIN_VARIANT     0x00340370u
static void intro_variants(void)
{
    static uint32_t told;
    uint32_t want[5], i, differ = 0;
    if (!N.lockstep || N.resim) return;
    for (i = 0; i < 5; i++) want[i] = mix32(N.match ^ mix32(0x1E7A0000u + i)) % 5u;
    for (i = 0; i < 4; i++) differ |= MEM32(INTRO_VARIANTS + 4u * i) != want[i];
    differ |= MEM32(WIN_VARIANT) != want[4];
    if (!differ) return;
    if (told != N.match) {
        told = N.match;
        fprintf(stderr, "[NETPLAY] created fighters' clips: this PC picked %d %d %d %d / %d, every PC plays %u %u %u %u / %u "
                "(match %08X, step %u)\n", (int)MEM32(INTRO_VARIANTS), (int)MEM32(INTRO_VARIANTS + 4u),
                (int)MEM32(INTRO_VARIANTS + 8u), (int)MEM32(INTRO_VARIANTS + 12u), (int)MEM32(WIN_VARIANT),
                want[0], want[1], want[2], want[3], want[4], N.match, N.step);
    }
    for (i = 0; i < 4; i++) MEM32(INTRO_VARIANTS + 4u * i) = want[i];
    MEM32(WIN_VARIANT) = want[4];
}

/* What another player's port reads before the fight. */
static void intro_pad(int slot, XBOX_INPUT_STATE *state)
{
    static int play = -1, test = -2;
    static uint32_t told;
    int press;
    if (play < 0) play = getenv("RECOMP_NETPLAY_INTROS") && !strcmp(getenv("RECOMP_NETPLAY_INTROS"), "play");
    if (test == -2) test = getenv("RECOMP_TEST_INTRO_PRESS") ? atoi(getenv("RECOMP_TEST_INTRO_PRESS")) : -1;
    if (!intro_window()) return;
    if (play) {
        if (test == N.pos && slot == (N.pos == 0 ? 1 : 0) && g_intro_state == 3u) {
            state->Gamepad.bAnalogButtons[0] = 255;
            state->dwPacketNumber++;
        }
        return;
    }
    press = (g_intro_state == 1u || g_intro_state == 3u) && slot == (N.pos == 0 ? 1 : 0) && !(g_intro_updates & 1u);
    memset(&state->Gamepad, 0, sizeof(state->Gamepad));
    state->dwPacketNumber = 0x40000000u + (g_intro_state << 8) + (uint32_t)press;
    if (!press) return;
    state->Gamepad.bAnalogButtons[0] = 255;
    if (told != N.match) {
        told = N.match;
        fprintf(stderr, "[NETPLAY] skipping the intros on every PC (match %08X)\n", N.match);
    }
}

static DWORD remote_slot_state(int slot, XBOX_INPUT_STATE *state)
{
    if (N.watching) {
        /* Spectator mode: the players' live pads are not here (the relay sends a spectator only the
         * lockstep's entries), so their ports read at rest; through the summary and the results,
         * which this game goes through by itself, player 1's port taps A (100 ms every 500 ms). */
        ULONGLONG since = N.spec_results;
        if (slot < 0 || slot >= N.players) return ERROR_DEVICE_NOT_CONNECTED;
        memset(state, 0, sizeof(*state));
        state->dwPacketNumber = 1;
        if (since && slot == 0) {
            ULONGLONG t = GetTickCount64() - since;
            int down = t % 500u < 100u;
            if (down) state->Gamepad.bAnalogButtons[0] = 255;
            state->dwPacketNumber = 2u + (DWORD)(t / 500u) * 2u + (DWORD)down;
        }
        if (N.lockstep) intro_pad(slot, state);
        return ERROR_SUCCESS;
    }
    if (N.state != NETPLAY_CONNECTED || !N.lockstep || slot < 0 || slot >= N.players || slot == N.pos)
        return ERROR_DEVICE_NOT_CONNECTED;
    EnterCriticalSection(&g_cs);
    state->dwPacketNumber = N.remote_packet[slot];
    state->Gamepad = N.remote_pad[slot];
    LeaveCriticalSection(&g_cs);
    intro_pad(slot, state);
    return ERROR_SUCCESS;
}

/* A hub match in lockstep: the title reads its pause (START) straight from the controllers, not
 * from the agreed inputs, so a press paused one game of the room only (and the other PCs saw it
 * on that player's port, through the live pads, and opened the pause there as that player): the
 * games parted, or a player kept everyone waiting. A port that read as unplugged mid-fight brought
 * up "Controller Disconnected" on one PC the same way. While a hub match runs, START reaches no
 * controller and every port reads as plugged in (players' Discord reports, 2026-10-22). */
#define REST_ONLY 0x0400      /* no Xbox button: the other ports read as pads at rest, START works */
static void fight_input(void)
{
    static LONG on = -1;
    /* the match itself, not the menus before it; once the other players are gone (it goes on
     * offline) their ports stay plugged-in pads at rest: unplugged, they brought up "Controller
     * Disconnected - port 2" mid-fight, which only that port's START closes (players' reports,
     * 2026-10-23); this PC's START works again then, to pause and quit */
    LONG want = !N.lobby || !N.match_seen ? 0 : N.lockstep ? 0x0010 : N.match_live ? REST_ONLY : 0;
    if (InterlockedExchange(&on, want) != want)
        xbox_HostInputSetNetplayFight((WORD)want);
}

static void assign_input(int on)
{
    unsigned mask = 0;
    int i, local = N.pos;
    if (!on) { xbox_HostInputSetNetplay(0, 0, 0, NULL); fight_input(); return; }
    /* a spectator drives no fighter: its own pad goes on the first port no player uses (with four
     * players the fourth, which the fight's entries override; spectator-mode.md 9) */
    if (local < 0) local = N.players < 4 ? N.players : 3;
    for (i = 0; i < N.players; i++)
        if (i != local) mask |= 1u << i;
    xbox_HostInputSetNetplayMulti(1, local, mask, remote_slot_state);
    fight_input();
}

static void reset_remote_pads(void)
{
    EnterCriticalSection(&g_cs);
    memset(N.remote_pad, 0, sizeof(N.remote_pad));
    memset(N.remote_seq, 0, sizeof(N.remote_seq));
    LeaveCriticalSection(&g_cs);
}

static void become_connected(void)
{
    if (!N.lobby) {
        N.match_count = N.step = 0;
        memset(N.in_tag, 0, sizeof(N.in_tag));
    }
    memset(N.seeds, 0, sizeof(N.seeds));
    N.desync = 0;
    InterlockedExchange(&N.state, NETPLAY_CONNECTED);
    InterlockedExchange(&N.lockstep, 1);
    assign_input(1);
    if (N.lobby) {
        N.vs_end = 0;
        N.vs_start = GetTickCount64();      /* the versus screen, until the fight is under way */
    }
    if (N.lobby && N.watching)
        set_status("Watching a match of %d players. It starts by itself.", N.players);
    else if (N.lobby)
        set_status("Match found: %d players, you are player %d. It starts by itself.", N.players, N.pos + 1);
    else
        set_status("Connected to %s. You are player %d. Both choose Versus; player 1 picks first.",
                   N.peer_name[0] ? N.peer_name : "the other player", N.pos + 1);
}

static void lose_peer(const char *why)
{
    InterlockedExchange(&N.lockstep, 0);
    assign_input(0);
    SetEvent(N.arrived);
    if (N.creating && !N.stop) {
        InterlockedExchange(&N.state, NETPLAY_WAITING);
        N.acked = 0;
        set_status("%s Room %s is open again; waiting for a player.", why, N.room);
    } else {
        InterlockedExchange(&N.state, NETPLAY_FAILED);
        set_status("%s", why);
    }
}

/* The hub match is over for this PC: tell the server and go back to the room. */
/* The contiguous window's use (64 MiB: textures, vertex data, rollback's audio arena) at a match's
 * start and end: when it runs dry the game draws with buffers it did not get (white blocks, the
 * announcer word sheet). Reports from build 19 showed it full at a second match's start. */
extern uint32_t xbox_ContiguousLiveBytes(void);
extern uint32_t xbox_ContiguousAllocatedBytes(void);
static void log_contiguous(const char *when)
{
    fprintf(stderr, "[MEM] contiguous memory %s: %.1f MB in use, %.1f MB reached\n", when,
            xbox_ContiguousLiveBytes() / 1048576.0, xbox_ContiguousAllocatedBytes() / 1048576.0);
}

static void rematch_declined(void);

static void disarm_crash_report(void);

static void end_hub_match(const char *why)
{
    log_contiguous("at the match end");
    rematch_declined();         /* whoever waits on the rematch screen goes back to the room too */
    InterlockedExchange(&N.lockstep, 0);
    assign_input(0);
    SetEvent(N.arrived);
    disarm_crash_report();
    N.match_live = N.match_seen = 0;
    N.over_pending = 1;
    if (N.watching) {
        N.watching = 0;         /* a spectator's match counts for nobody: no OVER */
        N.pos = 0;
    } else {
        request(T_OVER, NULL, 0);
    }
    InterlockedExchange(&N.state, NETPLAY_LOBBY);
    set_status("%s", why);
}

/* ---- what arrives -------------------------------------------------------------- */

/* ---- spectator mode: every entry of the watched match, and the checksums ----------
 *
 * A spectator keeps every player's entries of the match it watches, not only the players' ring of
 * RING steps: its venue may load seconds after theirs, and it then runs the fight from step 1 that
 * much later. In chunks of SPEC_CHUNK steps, allocated as the fight goes on, up to SPEC_CHUNKS (30
 * minutes). The players' checksums are kept SPEC_HASHES checkpoints back (a minute), its own too:
 * a spectator can run a few steps ahead of a player's checksum (it gets the entries `delay` steps
 * early) as well as far behind. docs/research/spectator-mode.md 5. Under g_cs. */
#define SPEC_CHUNK  4096u
#define SPEC_CHUNKS 27u
#define SPEC_HASHES 64u
#define SPEC_DELAY_WAIT_MS 20000    /* the sync point: how long a spectator waits for the players' delay */
typedef struct { uint32_t match[SPEC_CHUNK]; uint8_t e[SPEC_CHUNK][ENTRY]; } SpecChunk;
static SpecChunk *g_spec[MAXP][SPEC_CHUNKS];
static NetCheck g_spec_rh[MAXP][SPEC_HASHES], g_spec_lh[SPEC_HASHES];
static unsigned g_hash_matches;     /* checksums compared and equal, this session */
static void desync_detail(const NetCheck *mine, const NetCheck *theirs);

static void spec_reset(void)
{
    unsigned i, c;
    for (i = 0; i < MAXP; i++)
        for (c = 0; c < SPEC_CHUNKS; c++)
            if (g_spec[i][c]) memset(g_spec[i][c]->match, 0, sizeof(g_spec[i][c]->match));
    memset(g_spec_rh, 0, sizeof(g_spec_rh));
    memset(g_spec_lh, 0, sizeof(g_spec_lh));
}

static void spec_store(int from, uint32_t match, uint32_t s, const uint8_t *e)
{
    uint32_t c = s / SPEC_CHUNK;
    SpecChunk *k;
    if (from < 0 || from >= MAXP || !s || c >= SPEC_CHUNKS) return;
    if (!g_spec[from][c] && !(g_spec[from][c] = (SpecChunk *)calloc(1, sizeof(SpecChunk)))) return;
    k = g_spec[from][c];
    memcpy(k->e[s % SPEC_CHUNK], e, ENTRY);
    k->match[s % SPEC_CHUNK] = match;
}

/* Player `pos`'s entry for step s of the running match into out (NULL: only whether it is here). */
static int spec_entry(int pos, uint32_t s, uint8_t *out)
{
    uint32_t c = s / SPEC_CHUNK;
    SpecChunk *k;
    if (pos < 0 || pos >= MAXP || c >= SPEC_CHUNKS || !(k = g_spec[pos][c]) || k->match[s % SPEC_CHUNK] != N.match)
        return 0;
    if (out) memcpy(out, k->e[s % SPEC_CHUNK], ENTRY);
    return 1;
}

/* Compare this PC's checksum of checkpoint slot k with every player's, once each. */
static void spec_compare(unsigned k)
{
    const NetCheck *l = &g_spec_lh[k];
    int i;
    for (i = 0; i < MAXP; i++) {
        NetCheck *r = &g_spec_rh[i][k];
        if (!l->step || r->step != l->step || r->match != l->match) continue;
        if (r->hash != l->hash) {
            if (!N.desync) {
                N.desync = 1;
                N.desync_step = l->step;
                fprintf(stderr, "[NETPLAY] desync in match %08X at step %u (local %08X, remote %08X; this PC watches, "
                        "player %d)\n", l->match, l->step, l->hash, r->hash, i + 1);
                if (r->has_parts) desync_detail(l, r);
                toast(8000, "This picture went out of sync with the players' (their match is not affected).");
            }
        } else if (++g_hash_matches % 10 == 0) {
            fprintf(stderr, "[NETPLAY] in step: %u checksums compared and equal (match %08X, step %u)\n",
                    g_hash_matches, l->match, l->step);
        }
        r->step = 0;    /* compared */
    }
}

static void spec_remote_hash(int from, const NetCheck *in)
{
    unsigned k;
    if (from < 0 || from >= MAXP || !in->step || in->step % HASH_EVERY) return;
    k = (in->step / HASH_EVERY) % SPEC_HASHES;
    g_spec_rh[from][k] = *in;
    spec_compare(k);
}

static unsigned g_steps_rx[MAXP], g_steps_last[MAXP];
static uint32_t g_steps_match[MAXP];       /* the match of each player's latest STEPS: 0 or older while it still loads */
static void store_steps(int from, const uint8_t *p, int len)
{
    uint32_t match, last;
    int count, i;
    if (len < 9 || from < 0 || from >= MAXP) return;
    match = get32(p); last = get32(p + 4); count = p[8];
    g_steps_rx[from]++;
    g_steps_match[from] = match;
    g_steps_last[from] = last;
    if (len < 9 + count * ENTRY) return;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < count; i++) {
        uint32_t s = last - (uint32_t)(count - 1 - i);
        uint32_t k = s % RING;
        if (s == 0) continue;
        memcpy(N.in[from][k], p + 9 + i * ENTRY, ENTRY);
        N.in_tag[from][k] = s + 1;
        N.in_match[from][k] = match;
        if (N.watching) spec_store(from, match, s, p + 9 + i * ENTRY);
    }
    LeaveCriticalSection(&g_cs);
    SetEvent(N.arrived);
}

/* Which part of the checksum differs: a fighter (a player's or a CPU's), the CPU fighters'
 * generators, or the rest; since build 35 (both sides) also which raw word, here and there, and
 * whether the pre-fight generators and the crowd still agree. Early desyncs (steps 60-180, both
 * fighters, every generator equal) carried no more than "fighter 1, fighter 2" before. */
static void desync_detail(const NetCheck *mine, const NetCheck *theirs)
{
    char text[700];
    int fighters = N.fighters > N.players ? N.fighters : N.players;
    netcheck_parts_text(text, (int)sizeof(text), mine, theirs, fighters, N.players);
    fprintf(stderr, "[NETPLAY] desync detail: %s\n", text);
    if (netcheck_fields_text(text, (int)sizeof(text), mine, theirs, fighters))
        fprintf(stderr, "[NETPLAY] desync fields: %s\n", text);
}

/* Compare every checksum this PC and another have for the same match and step, once each. Only
 * the hash decides; the parts and fields say where the games parted. */
static void check_hashes(void)
{
    int i, j;
    for (i = 0; i < 16; i++)
        for (j = 0; j < 16; j++) {
            if (!N.local_hash[i].step || N.local_hash[i].match != N.remote_hash[j].match ||
                N.local_hash[i].step != N.remote_hash[j].step)
                continue;
            if (N.local_hash[i].hash != N.remote_hash[j].hash) {
                if (!N.desync) {
                    N.desync = 1;
                    N.desync_step = N.local_hash[i].step;
                    fprintf(stderr, "[NETPLAY] desync in match %08X at step %u (local %08X, remote %08X)\n",
                            N.local_hash[i].match, N.local_hash[i].step, N.local_hash[i].hash, N.remote_hash[j].hash);
                    if (N.remote_hash[j].has_parts) desync_detail(&N.local_hash[i], &N.remote_hash[j]);
                    toast(8000, "The games went out of sync: this match won't count.");
                }
            } else if (++g_hash_matches % 10 == 0) {
                fprintf(stderr, "[NETPLAY] in step: %u checksums compared and equal (match %08X, step %u)\n",
                        g_hash_matches, N.local_hash[i].match, N.local_hash[i].step);
            }
            N.remote_hash[j].step = 0;  /* compared */
        }
}

/* The game files a fight reads (fighters, moves, the CPU fighters' AI, and the database Battle
 * mode takes each fighter's record from), as this PC has them: the override folder's copy where
 * there is one (a mod). Two PCs whose files differ play the same inputs differently, and the setup
 * checks only default.xbe. Made once, on the network thread. */
static const char *const k_fight_files[] = {
    "fighter.xml", "gamedata.xml", "gconfig.xml", "venue.xml", "assets\\gamecore.xml", "assets\\combs.xml",
    "assets\\ai\\ai.viv", "motion\\main_all.dat", "assets\\djv2.sod",
};
static uint32_t g_data_print;
static char g_data_over[160];       /* the fight files read from the override folder, for the reports */

/* The toolkit's game-file redirect (kernel.h): a language mod's rebuilt copy of a file
 * (src/hooks/pc_language.c), which the game opens before the override folder's. */
#if defined(_WIN32)
extern BOOL (*g_xbox_game_file_redirect)(const char *relative, WCHAR *host_path, DWORD size);
#else
extern BOOL (*g_xbox_game_file_redirect)(const char *relative, char *host_path, DWORD size);
#endif

static uint32_t file_print(const char *name, int *overridden)
{
    static uint8_t buf[65536];
    const char *data = getenv("DEFJAM_DATA");
    char path[MAX_PATH];
    FILE *f = NULL;
    uint32_t h = 2166136261u;
    size_t got, i;
    *overridden = 0;
    /* A language mod that rebuilds one of these files changes what the fight reads: printed as
     * the game opens it, so two PCs with different such mods are told apart (a pack that only
     * translates text archives leaves the print alone). */
    if (g_xbox_game_file_redirect) {
#if defined(_WIN32)
        WCHAR wide[MAX_PATH];
        if (g_xbox_game_file_redirect(name, wide, MAX_PATH) && (f = _wfopen(wide, L"rb")) != NULL) *overridden = 1;
#else
        if (g_xbox_game_file_redirect(name, path, MAX_PATH) && (f = fopen(path, "rb")) != NULL) *overridden = 1;
#endif
    }
    if (!f && data && *data) {
        snprintf(path, sizeof(path), "%s\\override\\%s", data, name);
        if ((f = fopen(path, "rb")) != NULL) *overridden = 1;
    }
    if (!f) {
        snprintf(path, sizeof(path), "game\\%s", name);
        f = fopen(path, "rb");
    }
    if (!f) return 0;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
        for (i = 0; i < got; i++) h = (h ^ buf[i]) * 16777619u;
    fclose(f);
    return h ? h : 1;
}

static void make_data_print(void)
{
    char text[400], over[160];
    int n = 0, o = 0;
    unsigned i;
    uint32_t h = 2166136261u;
    over[0] = 0;
    for (i = 0; i < sizeof(k_fight_files) / sizeof(k_fight_files[0]); i++) {
        int overridden;
        uint32_t f = file_print(k_fight_files[i], &overridden);
        h = mix32(h ^ f);
        if (n < (int)sizeof(text)) n += snprintf(text + n, sizeof(text) - (size_t)n, "%s%s %08X", i ? ", " : "", k_fight_files[i], f);
        if (overridden && o < (int)sizeof(over)) o += snprintf(over + o, sizeof(over) - (size_t)o, " %s", k_fight_files[i]);
    }
    g_data_print = h ? h : 1;
    snprintf(g_data_over, sizeof(g_data_over), "%s", o ? over + 1 : "");
    fprintf(stderr, "[NETPLAY] game files %08X: %s%s%s\n", g_data_print, text, o ? "; from the override folder:" : "", over);
}

/* Hub: this PC's ping for the current START, so every PC derives the same input delay. */
static void send_rtt(void)
{
    uint8_t p[14];
    int rtt;
    if (N.pos < 0 || N.watching) return;    /* a spectator: nothing of the lockstep goes out */
    rtt = N.peer_rtt[N.pos] < 0 ? 0 : N.peer_rtt[N.pos];
    p[0] = P_RTT;
    put32(p + 1, N.start_seed);
    p[5] = (uint8_t)rtt;
    p[6] = (uint8_t)(rtt >> 8);
    p[7] = (uint8_t)N.peer_rb[N.pos];   /* builds before 17 send 7 bytes: no rollback */
    put32(p + 8, g_data_print);         /* builds before 23 send 8 bytes */
    p[12] = N.start_deferred ? 1 : 0;   /* builds before 34 send 12: 1, still on the last fight's results */
    p[13] = (uint8_t)(N.delay_fixed ? N.delay : 0);    /* builds before 40 send 13: the agreed delay, for spectators */
    send_data(p, 14);
    N.last_rtt_sent = GetTickCount64();
}

/* The delay every PC derives from the same pings: the slowest path between two players goes
 * through the relay, about half of each one's round trip, plus a step of margin. */
/* A match with a created fighter (its START names a record) plays in lockstep: a rollback at
 * step 4 of a created-fighter match crashed in sub_0008BE60 (the created fighters' runtime-built
 * looks are not in what a rollback restores; 2026-10-28). Every PC knows it from the START. */
static int match_has_created(void)
{
    int k;
    for (k = 0; k < ROOMSLOTS; k++) if (N.cp_start_crc[k]) return 1;
    return 0;
}

static int every_player_rolls_back(void)
{
    int i;
    /* King of the Hill fights stay in lockstep for now: with rollback, players' games froze or
     * crashed after the first fight of a hill in build 27, and the loser then never left the seat
     * (2026-10-25). Both PCs of a fight know it from START, so the delay agrees too. */
    {   /* tests (rollback-plan-2026-11.md step 6): RECOMP_ROLLBACK_KOTH=1 on every copy lifts the hill's exclusion */
        static int koth = -1;
        if (koth < 0) koth = getenv("RECOMP_ROLLBACK_KOTH") && *getenv("RECOMP_ROLLBACK_KOTH") == '1';
        if (N.koth_match && !koth) return 0;
    }
    if (match_has_created()) return 0;
    for (i = 0; i < N.players; i++) if (!N.peer_rb[i]) return 0;
    return 1;
}

static int agreed_delay(void)
{
    int a = 0, b = 0, i, d;
    for (i = 0; i < N.players; i++) {
        int r = N.peer_rtt[i] < 0 ? 0 : N.peer_rtt[i];
        if (r > a) { b = a; a = r; } else if (r > b) b = r;
    }
    d = ((a + b) / 2 * 3 + 49) / 50 + 1;      /* ms to 1/60 s steps, rounded up, + 1 */
    if (every_player_rolls_back()) d -= RB_LESS;   /* what the delay does not cover, a rollback does */
    return d < DELAY_MIN ? DELAY_MIN : d > DELAY_MAX ? DELAY_MAX : d;
}

static void on_data(int from, const uint8_t *p, int len)
{
    if (len < 1 || from < 0 || from >= MAXP || from == N.pos) return;
    switch (p[0]) {
    case P_GAMEHELLO:
        if (!N.lobby && !N.creating && len >= 7 && p[6] == LOCKSTEP_VERSION) {
            uint8_t ack = P_GAMEACK;
            N.session = get32(p + 1);
            N.delay = p[5] >= 1 && p[5] <= 12 ? p[5] : 3;
            send_data(&ack, 1);
            if (N.state != NETPLAY_CONNECTED) become_connected();
        }
        break;
    case P_GAMEACK:
        if (!N.lobby && N.creating && N.state == NETPLAY_SYNCING) {
            N.acked = 1;
            become_connected();
        }
        break;
    case P_LIVEPAD:
        if (len >= 5 + (int)sizeof(XBOX_GAMEPAD) && !N.watching) {
            uint32_t seq = get32(p + 1);
            EnterCriticalSection(&g_cs);
            if ((int32_t)(seq - N.remote_seq[from]) > 0) {
                static int seen, pressed;
                N.remote_seq[from] = seq;
                memcpy(&N.remote_pad[from], p + 5, sizeof(XBOX_GAMEPAD));
                N.remote_packet[from]++;
                if (!seen++)
                    fprintf(stderr, "[NETPLAY] the other player's controller is arriving (player %d)\n", from + 1);
                if (!pressed && (N.remote_pad[from].wButtons || N.remote_pad[from].bAnalogButtons[0] > 32)) {
                    pressed = 1;
                    fprintf(stderr, "[NETPLAY] first press from the other player's controller (buttons %04X)\n",
                            N.remote_pad[from].wButtons);
                }
            }
            LeaveCriticalSection(&g_cs);
        }
        break;
    case P_STEPS:
        store_steps(from, p + 1, len - 1);
        break;
    case P_RTT:
        if (N.lobby && N.match_live && len >= 7 && get32(p + 1) == N.start_seed) {
            int had = N.peer_rtt[from];
            N.peer_rtt[from] = p[5] | (p[6] << 8);
            N.peer_rb[from] = len >= 8 && p[7] == 1;
            if (len >= 12 && g_data_print && get32(p + 8) && get32(p + 8) != g_data_print) {
                static uint32_t warned_seed[MAXP];
                if (warned_seed[from] != N.start_seed) {
                    char who[32];
                    warned_seed[from] = N.start_seed;
                    fprintf(stderr, "[NETPLAY] player %d's game files differ from this PC's (%08X there, %08X here)\n",
                            from + 1, get32(p + 8), g_data_print);
                    if (!netplay_room_name(N.start_slots[from], who, (int)sizeof(who)))
                        snprintf(who, sizeof(who), "player %d", from + 1);
                    toast(8000, "%s's game files differ from yours (a mod?): this match may go out of sync.", who);
                }
            }
            N.peer_on_results[from] = len >= 13 && (p[12] & 1);
            if (len >= 14 && p[13] >= 1 && p[13] <= DELAY_MAX + 2 && N.peer_delay[from] != p[13]) {
                N.peer_delay[from] = p[13];         /* the delay that player's PC agreed */
                if (N.watching)
                    fprintf(stderr, "[NETPLAY] watching: player %d plays with input delay %d\n", from + 1, p[13]);
            }
            if (N.peer_on_results[from] && !N.peer_results[from]) {
                /* King of the Hill: that player's game still shows its last fight's results (the
                 * king, on the results screen); this fight starts once it leaves them. Said only to
                 * a player who is not on results too (with two in the room both are, every time). */
                N.peer_results[from] = 1;
                fprintf(stderr, "[NETPLAY] player %d is still on the last fight's results\n", from + 1);
                if (!N.start_deferred)
                    toast(8000, "%s is still on the last fight's results: the fight starts when they leave them.",
                          player_name(from));
            }
            SetEvent(N.arrived);
            if (N.delay_fixed && had < 0 && !N.watching) send_rtt();   /* it may still be waiting for ours */
        }
        break;
    case P_REMATCH:
        if (N.lobby && len >= 9 && get32(p + 1) == N.start_seed && N.start_seed && !N.watching) {
            uint32_t after = get32(p + 5);
            int mine;
            EnterCriticalSection(&g_cs);
            N.rematch_want[from] = after;
            mine = N.rematch_want[N.pos] == after;
            LeaveCriticalSection(&g_cs);
            fprintf(stderr, "[NETPLAY] rematch: player %d %s\n", from + 1,
                    after == REMATCH_NO ? "went back to the room" : "chose Rematch");
            if (after == REMATCH_NO)
                toast(8000, "%s went back to the room: choose No to join them.", player_name(from));
            else if (mine)
                toast(6000, "%s accepted the rematch: choose Rematch again to start.", player_name(from));
            else
                toast(8000, "%s wants a rematch! Choose Rematch to accept.", player_name(from));
        }
        break;
    case P_HASH: {
        NetCheck in;     /* as much as the sender's build sent: 13, 37 or 157 bytes */
        if (netcheck_unpack(p, len, &in) && N.watching) {
            EnterCriticalSection(&g_cs);
            spec_remote_hash(from, &in);
            LeaveCriticalSection(&g_cs);
        } else if (netcheck_unpack(p, len, &in)) {
            EnterCriticalSection(&g_cs);
            memmove(&N.remote_hash[1], &N.remote_hash[0], sizeof(N.remote_hash[0]) * 15);
            N.remote_hash[0] = in;
            check_hashes();
            LeaveCriticalSection(&g_cs);
        }
        break;
    }
    }
}

static void report_left(uint32_t left);
static void koth_check_result(int final);

/* ---- the hub's messages -------------------------------------------------------- */

typedef struct { const uint8_t *p; int n, i, bad; } Reader;

static int rd8(Reader *r)
{
    if (r->i >= r->n) { r->bad = 1; return 0; }
    return r->p[r->i++];
}

static void rdbytes(Reader *r, void *out, int count)
{
    if (r->i + count > r->n) { r->bad = 1; memset(out, 0, (size_t)count); return; }
    memcpy(out, r->p + r->i, (size_t)count);
    r->i += count;
}

static void rdstr(Reader *r, char *out, int size)
{
    int len = rd8(r), keep = len < size - 1 ? len : size - 1;
    if (r->i + len > r->n) { r->bad = 1; out[0] = 0; return; }
    memcpy(out, r->p + r->i, (size_t)keep);
    out[keep] = 0;
    r->i += len;
}

static void rdsettings(Reader *r, NetRoomSettings *s)
{
    memset(s, 0, sizeof(*s));
    rdstr(r, s->name, sizeof(s->name));
    s->is_public = rd8(r);
    s->mode = rd8(r);
    s->max = rd8(r);
    s->rounds = rd8(r);
    s->pool_count = rd8(r);
    if (s->pool_count > 32) { r->bad = 1; s->pool_count = 0; return; }
    rdbytes(r, s->pool, s->pool_count);
}

/* The most players a room of `mode` takes: King of the Hill rooms ten, the others four. */
static int room_max(int mode)
{
    return NETPLAY_IS_KOTH(mode) ? ROOMSLOTS : MAXP;
}

static int put_settings(uint8_t *b, const NetRoomSettings *s)
{
    int n = 0, len = (int)strlen(s->name), pool = s->pool_count, most = room_max(s->mode);
    if (len > 24) len = 24;
    if (len == 0) { b[n++] = 4; memcpy(b + n, "Room", 4); n += 4; }
    else { b[n++] = (uint8_t)len; memcpy(b + n, s->name, (size_t)len); n += len; }
    b[n++] = s->is_public ? 1 : 0;
    b[n++] = (uint8_t)s->mode;
    b[n++] = (uint8_t)(s->max < 2 ? 2 : s->max > most ? most : s->max);
    b[n++] = (uint8_t)s->rounds;
    if (pool < 1) pool = 0;
    if (pool > 32) pool = 32;
    b[n++] = (uint8_t)pool;
    memcpy(b + n, s->pool, (size_t)pool); n += pool;
    return n;
}

static void on_rooms(const uint8_t *b, int len)
{
    Reader r = { b, len, 0, 0 };
    NetRoomInfo list[32];
    int count = rd8(&r), i;
    if (count > 32) count = 32;
    for (i = 0; i < count && !r.bad; i++) {
        rdbytes(&r, list[i].code, 6);
        list[i].code[6] = 0;
        rdstr(&r, list[i].name, sizeof(list[i].name));
        list[i].mode = rd8(&r);
        list[i].players = rd8(&r);
        list[i].max = rd8(&r);
        list[i].phase = rd8(&r);
    }
    if (r.bad) return;
    EnterCriticalSection(&g_cs);
    memcpy(N.rooms, list, sizeof(list[0]) * (size_t)count);
    N.room_count = count;
    N.rooms_version++;
    if (N.pend_len && (N.pend[5] == T_LIST || N.pend[5] == T_LEAVE)) answered();
    LeaveCriticalSection(&g_cs);
}

static void on_players(const uint8_t *b, int len)
{
    Reader r = { b, len, 0, 0 };
    NetPlayer list[40];
    int online, rooms, count, i;
    online = rd8(&r);
    online |= rd8(&r) << 8;
    rooms = rd8(&r);
    rooms |= rd8(&r) << 8;
    count = rd8(&r);
    if (count > 40) count = 40;
    for (i = 0; i < count && !r.bad; i++) {
        rdstr(&r, list[i].name, sizeof(list[i].name));
        list[i].state = rd8(&r);
    }
    if (r.bad) return;
    EnterCriticalSection(&g_cs);
    memcpy(N.who, list, sizeof(list[0]) * (size_t)count);
    N.who_count = count;
    N.who_online = online;
    N.who_rooms = rooms;
    LeaveCriticalSection(&g_cs);
}

static void on_queue(const uint8_t *b, int len)
{
    /* by wire state: 0 checking, 1 searching, 2 refused, 3 off, 4 found, 5 accept, 6 move */
    static const char *const say[] = { "Checking your account...", "Searching for an opponent...",
        "Ranked play needs a signed-in account (sign in with Discord in the launcher).", "", "Opponent found!",
        "Opponent found: accept the set?", "The set is on another server: moving there..." };
    static const int as[] = { NETQUEUE_CHECKING, NETQUEUE_SEARCHING, NETQUEUE_REFUSED, NETQUEUE_OFF, NETQUEUE_FOUND,
                              NETQUEUE_ACCEPT, NETQUEUE_MOVE };
    NetQueueExt ext;
    int wire, state;
    if (len < 5 || b[0] > 6) return;
    wire = b[0];
    state = as[wire];
    netranked_decode_queue_ext(b + 5, len - 5, &ext);
    if (state == NETQUEUE_MOVE && (!ext.valid || !ext.code[0])) return;
    EnterCriticalSection(&g_cs);
    if (N.pend_len && N.pend[5] == T_RANKED) answered();
    /* a move under way: the old relay's last QUEUEs change nothing */
    if (N.queue_state == NETQUEUE_MOVE && state != NETQUEUE_FOUND) { LeaveCriticalSection(&g_cs); return; }
    if (N.queue_state != NETQUEUE_NONE || state == NETQUEUE_FOUND) {
        if (state != N.queue_state)
            fprintf(stderr, "[NETPLAY] ranked queue: state %d%s%s\n", wire, ext.code[0] ? ", set " : "", ext.code);
        N.queue_state = state == NETQUEUE_OFF ? NETQUEUE_NONE : state;
        N.queue_waited = b[1] | b[2] << 8;
        N.queue_searching = b[3] | b[4] << 8;
        if (ext.valid) N.queue_ext = ext;
        if (state == NETQUEUE_ACCEPT && strcmp(N.queue_match, ext.code)) {
            snprintf(N.queue_match, sizeof(N.queue_match), "%s", ext.code);
            N.queue_accepted = -1;      /* a new set: not answered yet */
        }
        if (state != NETQUEUE_ACCEPT && state != NETQUEUE_SEARCHING) N.queue_accepted = -1;
        if (state == NETQUEUE_SEARCHING) N.queue_match[0] = 0;
        snprintf(N.queue_text, sizeof(N.queue_text), "%s", say[wire]);
    }
    LeaveCriticalSection(&g_cs);
}

/* RANKINFO: the ranked room's ranks and its set's score, after every ROOM of the room. */
static void on_rankinfo(const uint8_t *b, int len)
{
    NetRankInfo ri;
    if (!netranked_decode_rankinfo(b, len, &ri)) return;
    EnterCriticalSection(&g_cs);
    if (!N.roomv.in_room) { LeaveCriticalSection(&g_cs); return; }
    if (!N.rank.valid || memcmp(&N.rank, &ri, sizeof(ri)))
        fprintf(stderr, "[NETPLAY] ranked set: %s game %d, score %d-%d, picker %d, winner %d\n",
                netranked_mode_title(ri.mode), ri.game, ri.count > 0 ? ri.p[0].set_wins : 0,
                ri.count > 1 ? ri.p[1].set_wins : 0, ri.picker, ri.winner);
    N.rank = ri;
    memcpy(N.rank_room, N.roomv.code, 7);
    /* this PC's RESULT got through: the game counted (or the set ended) */
    if (N.set_match && N.koth_winner >= 0 && (ri.game != N.set_game || ri.winner != NETRANK_SET_RUNNING)) {
        fprintf(stderr, "[NETPLAY] ranked set: the result is in\n");
        N.koth_winner = -1;
    }
    N.roomv.version++;
    LeaveCriticalSection(&g_cs);
}

/* DRAFT: a Draft set's pick and ban, on every change and every second. */
static void on_draft(const uint8_t *b, int len)
{
    NetDraft d;
    if (!netdraft_decode(b, len, &d)) return;
    EnterCriticalSection(&g_cs);
    if (!N.roomv.in_room) { LeaveCriticalSection(&g_cs); return; }
    if (!N.draft.valid || N.draft.step != d.step || N.draft.turn != d.turn || N.draft.game != d.game ||
        memcmp(N.draft_room, N.roomv.code, 6))
        fprintf(stderr, "[NETPLAY] draft: game %d step %d turn %d, %d bans, venue %d, picks %d %d\n", d.game, d.step,
                d.turn, d.ban_count, d.venue, d.picks[0], d.picks[1]);
    N.draft = d;
    memcpy(N.draft_room, N.roomv.code, 7);
    N.roomv.version++;
    LeaveCriticalSection(&g_cs);
}

static void on_room(const uint8_t *b, int len)
{
    Reader r = { b, len, 0, 0 };
    NetRoom v;
    int i, count, gone = 0;
    memset(&v, 0, sizeof(v));
    rdbytes(&r, v.code, 6);
    v.code[6] = 0;
    v.you = rd8(&r);
    v.host = rd8(&r);
    v.phase = rd8(&r);
    rdsettings(&r, &v.settings);
    v.candidate_count = rd8(&r);
    if (v.candidate_count > 3) return;
    rdbytes(&r, v.candidates, v.candidate_count);
    for (i = 0; i < v.candidate_count; i++) v.votes[i] = rd8(&r);
    v.ms_left = rd8(&r);
    v.ms_left |= rd8(&r) << 8;
    count = rd8(&r);
    if (count > ROOMSLOTS || (count > MAXP && !NETPLAY_IS_KOTH(v.settings.mode))) return;
    v.player_count = count;
    for (i = 0; i < count; i++) {
        v.players[i].slot = rd8(&r);
        rdstr(&r, v.players[i].name, sizeof(v.players[i].name));
        v.players[i].ready = rd8(&r);
        v.players[i].fighter = rd8(&r);
        v.players[i].voted = rd8(&r);
        if (v.players[i].voted == 0xFF) v.players[i].voted = -1;
    }
    if (r.bad) return;
    if (r.i < r.n) {    /* spectator mode: the spectators' names (relays answering CAPS bit 4) */
        int sc = rd8(&r);
        for (i = 0; i < sc && !r.bad; i++) {
            char name[25];
            rdstr(&r, name, sizeof(name));
            if (i < NETPLAY_MAX_SPECTATORS) snprintf(v.spectators[i], sizeof(v.spectators[i]), "%s", name);
        }
        v.spectator_count = sc < NETPLAY_MAX_SPECTATORS ? sc : NETPLAY_MAX_SPECTATORS;
        if (r.bad) return;
    }
    v.in_room = 1;
    v.received = GetTickCount64();
    EnterCriticalSection(&g_cs);
    if (!N.roomv.in_room || memcmp(N.roomv.code, v.code, 6)) {      /* another room: its pings come anew */
        for (i = 0; i < ROOMSLOTS; i++) N.slot_rtt[i] = -1;
        N.last_pinginfo = 0;
    }
    memcpy(v.error, N.roomv.error, sizeof(v.error));
    v.version = N.roomv.version + 1;
    /* the line comes in its own message (HILL, after every ROOM of such a room): keep the last */
    if (NETPLAY_IS_KOTH(v.settings.mode) && N.roomv.in_room && !memcmp(N.roomv.code, v.code, 6))
        v.hill = N.roomv.hill;
    N.roomv = v;
    N.slot = v.you;
    {   /* created fighters: a slot nobody holds any more has no record */
        int k, j;
        for (k = 0; k < ROOMSLOTS; k++) {
            int present = 0;
            for (j = 0; j < v.player_count; j++) if (v.players[j].slot == k) present = 1;
            if (!present) N.cp_have[k] = 0;
        }
    }
    memcpy(N.room, v.code, 7);
    if (N.pend_len && N.pend[5] != T_LIST && N.pend[5] != T_LEAVE) answered();
    if (v.phase != 2) N.over_pending = N.start_this_play = 0;
    if (v.phase == 2 && (N.roomv.phase != 2 || !N.playing_since)) N.playing_since = GetTickCount64();
    if (v.phase != 2) N.playing_since = 0;
    /* A player of the running match has left: the others cannot go on in lockstep. */
    if (N.match_live) {
        int k, j;
        for (k = 0; k < N.players; k++) {
            int found = 0;
            for (j = 0; j < count; j++) if (v.players[j].slot == N.start_slots[k]) found = 1;
            if (!found) gone = 1;
        }
    }
    LeaveCriticalSection(&g_cs);
    if (gone && N.lockstep) {
        uint32_t left = 0;
        int k, j;
        EnterCriticalSection(&g_cs);
        for (k = 0; k < N.players; k++) {
            int found = 0;
            for (j = 0; j < count; j++) if (v.players[j].slot == N.start_slots[k]) found = 1;
            if (!found) left |= 1u << k;
        }
        LeaveCriticalSection(&g_cs);
        InterlockedExchange(&N.lockstep, 0);
        fight_input();              /* their ports stay plugged in, at rest, until the match ends */
        SetEvent(N.arrived);
        set_status("A player left the match; it finishes offline.");
        if (N.fighting_seen && MEM32(G_MATCH_PHASE) < 2u) {
            report_left(left);
            toast(10000, "A player left the fight: it counts as their loss if they were signed in. START quits.");
        }
    }
}

/* King of the Hill: the room's line (docs/research/king-of-the-hill.md 8.4), after every ROOM of
 * such a room and once a second while a pair is called or votes. */
static void on_hill(const uint8_t *b, int len)
{
    Reader r = { b, len, 0, 0 };
    NetHill h;
    char code[7];
    int i, v, auto_ready = -1, called_off = 0;
    memset(&h, 0, sizeof(h));
    rdbytes(&r, code, 6);
    code[6] = 0;
    h.phase = rd8(&r);
    v = rd8(&r); h.king = v == 0xFF ? -1 : v;
    h.streak = rd8(&r); h.streak |= rd8(&r) << 8;
    h.ms_left = rd8(&r); h.ms_left |= rd8(&r) << 8;
    rdbytes(&r, &h.seed, 4);
    h.seed = get32((const uint8_t *)&h.seed);
    h.fights = rd8(&r); h.fights |= rd8(&r) << 8;
    v = rd8(&r); h.last_winner = v == 0xFF ? -1 : v;
    v = rd8(&r); h.last_loser = v == 0xFF ? -1 : v;
    h.last_how = rd8(&r);
    h.count = rd8(&r);
    if (r.bad || h.phase > NETHILL_FIGHTING || h.count > ROOMSLOTS) return;
    for (i = 0; i < h.count; i++) {
        h.line[i].slot = rd8(&r);
        h.line[i].state = rd8(&r);
        h.line[i].wins = rd8(&r); h.line[i].wins |= rd8(&r) << 8;
        if (h.line[i].slot >= ROOMSLOTS || h.line[i].state > NETLINE_AWAY) r.bad = 1;
    }
    if (r.bad) return;
    h.valid = 1;
    h.received = GetTickCount64();
    EnterCriticalSection(&g_cs);
    if (!N.roomv.in_room || memcmp(N.roomv.code, code, 6)) {     /* not (or no longer) this room */
        LeaveCriticalSection(&g_cs);
        return;
    }
    if (N.roomv.hill.valid && N.roomv.hill.fights != h.fights)
        fprintf(stderr, "[NETPLAY] hill: fight %d over (how %d, winner slot %d, loser slot %d), king slot %d x%d\n",
                h.fights, h.last_how, h.last_winner, h.last_loser, h.king, h.streak);
    if (N.roomv.hill.phase != h.phase || !N.roomv.hill.valid) {
        char line[96];
        int n = 0;
        for (i = 0; i < h.count && n < (int)sizeof(line) - 8; i++)
            n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%d:%d", i ? " " : "", h.line[i].slot, h.line[i].state);
        line[n] = 0;
        fprintf(stderr, "[NETPLAY] hill: phase %d, king slot %d x%d, line %s\n", h.phase, h.king, h.streak, line);
    }
    /* the watched fight stays known while it runs */
    h.watch_seed = N.roomv.hill.watch_seed;
    h.watch_venue = N.roomv.hill.watch_venue;
    memcpy(h.watch_slots, N.roomv.hill.watch_slots, sizeof(h.watch_slots));
    memcpy(h.watch_fighters, N.roomv.hill.watch_fighters, sizeof(h.watch_fighters));
    if (h.phase != NETHILL_FIGHTING) h.watch_seed = 0;
    N.roomv.hill = h;
    N.roomv.version++;
    /* this PC's RESULT got through: the fight is over on the relay */
    if (N.koth_winner >= 0 && (h.fights != N.koth_fights || h.phase != NETHILL_FIGHTING || h.seed != N.start_seed)) {
        fprintf(stderr, "[NETPLAY] hill: the result is in (fights %d)\n", h.fights);
        N.koth_winner = -1;
    }
    /* Called again while still on the results of the fight this PC just fought (the winner stays
     * on; with two in the room, both): the results and the summary take longer than the ready time,
     * so the king was skipped and lost the crown before its game was back on the hill (three-copy
     * test, 2026-10-25). Whoever has just fought is there: ready at once, with the same fighter.
     * koth_results: also after a deferred fight was called off (below), while the results still show. */
    if (h.phase == NETHILL_CALLING && N.koth_results && N.koth_ready_for != h.fights + 1) {
        for (i = 0; i < h.count; i++)
            if (h.line[i].slot == N.roomv.you && h.line[i].state == NETLINE_CALLED) {
                auto_ready = N.report_m.fighter[N.pos];
                N.koth_ready_for = h.fights + 1;
            }
    }
    /* The next fight was agreed while this game still showed the last one's results (a deferred
     * START), and the relay has ended it since: the opponent left (how 3) or did not come. Nothing
     * may be set up for it any more: this PC loaded a fight against a gone opponent once back from
     * the results, ran to step 8 and gave up (Farouk's F9 report 3d59ef, 2026-10-28). Over means the
     * hill's fight count moved on, or another fight runs: a HILL sent before the START (still
     * "calling", the same count) can arrive after it and must not end it. */
    if (N.start_deferred && N.koth_match &&
        (h.fights != N.koth_fights || (h.phase == NETHILL_FIGHTING && h.seed != N.start_seed)) &&
        InterlockedExchange(&N.start_deferred, 0))
        called_off = 1;
    LeaveCriticalSection(&g_cs);
    if (called_off) {
        fprintf(stderr, "[NETPLAY] hill: the deferred fight is over on the relay (fights %d, how %d, phase %d); not started\n",
                h.fights, h.last_how, h.phase);
        end_hub_match("The next fight was called off: back on the hill.");
        if (h.last_how == NETHOW_LEFT)
            toast(8000, "The next fight was called off (%s left): back on the hill.", player_name(1 - N.pos));
        else
            toast(8000, "The next fight was called off: back on the hill.");
    }
    if (auto_ready >= 0) {
        fprintf(stderr, "[NETPLAY] hill: called again straight after the fight; ready with fighter %d\n", auto_ready);
        netplay_lobby_ready(1, auto_ready);
    }
}

/* King of the Hill: a fight this PC watches (START's body). Phase 1: the names only; the fight is
 * not run here (docs/research/king-of-the-hill.md 9.2 is phase 2). */
static void on_start_as(const uint8_t *b, int len, int watching);
static void on_watch(const uint8_t *b, int len)
{
    Reader r = { b, len, 0, 0 };
    if (!NETPLAY_IS_KOTH(N.roomv.settings.mode)) {     /* spectator mode: the match this PC watches */
        if (SPECTATOR_HERE()) on_start_as(b, len, 1);
        return;
    }
    uint32_t seed;
    int count, venue, i, slots[2], fighters[2];
    if (len < 8) return;
    seed = get32(b);
    r.i = 4;
    rd8(&r);                    /* mode: 0, One on One */
    rd8(&r);                    /* rules */
    venue = rd8(&r);
    count = rd8(&r);
    if (count != 2) return;
    for (i = 0; i < 2; i++) { slots[i] = rd8(&r); fighters[i] = rd8(&r); }
    if (r.bad) return;
    EnterCriticalSection(&g_cs);
    if (N.roomv.in_room && N.roomv.hill.watch_seed != seed) {
        N.roomv.hill.watch_seed = seed;
        N.roomv.hill.watch_venue = venue;
        memcpy(N.roomv.hill.watch_slots, slots, sizeof(slots));
        memcpy(N.roomv.hill.watch_fighters, fighters, sizeof(fighters));
        N.roomv.version++;
        fprintf(stderr, "[NETPLAY] WATCH: seed %08X, venue %d, slots %d and %d (watching, not running the fight)\n",
                seed, venue, slots[0], slots[1]);
    }
    LeaveCriticalSection(&g_cs);
}

/* ---- created fighters (docs/research/created-fighter-online.md) ---- */

/* RECORD: a member's record (an empty one: none), or the relay's copy of ours. */
static void on_record(const uint8_t *b, int len)
{
    int slot, n;
    if (len < 3) return;
    slot = b[0];
    n = b[1] | b[2] << 8;
    if ((n != 0 && n != CP_REC_LEN) || len != 3 + n) return;
    if (n && !cp_record_sealed(b + 3)) return;
    EnterCriticalSection(&g_cs);
    if (slot == RECORD_SELF) {
        uint32_t crc = n ? cp_record_crc(b + 3) : 0;
        if (crc != N.cp_acked)
            fprintf(stderr, "[NETPLAY] the relay %s\n", crc ? "keeps this player's created fighter" : "keeps no created fighter for this player");
        N.cp_acked = crc;
        if (!crc && N.cp_mine_state > 0) N.cp_tries = 99;     /* refused: do not repeat */
    } else if (slot < ROOMSLOTS) {
        if (n) memcpy(N.cp_rec[slot], b + 3, CP_REC_LEN);
        if (N.cp_have[slot] != (n != 0) || (n && memcmp(N.cp_rec[slot], b + 3, CP_REC_LEN))) N.roomv.version++;
        N.cp_have[slot] = n != 0;
    }
    LeaveCriticalSection(&g_cs);
}

/* Whether every record the last START named is here (the own one included). */
static int cp_records_complete(int ask)
{
    int slot, complete = 1;
    for (slot = 0; slot < ROOMSLOTS; slot++) {
        if (!N.cp_start_crc[slot]) continue;
        if (slot == N.slot && N.cp_mine_state > 0 && cp_record_crc(N.cp_mine) == N.cp_start_crc[slot]) {
            memcpy(N.cp_rec[slot], N.cp_mine, CP_REC_LEN);
            N.cp_have[slot] = 1;
        } else if (slot == N.slot && N.cp_prev_crc && N.cp_prev_crc == N.cp_start_crc[slot]) {
            /* the relay started the match before the new pick reached it: every PC plays the old one */
            memcpy(N.cp_rec[slot], N.cp_prev, CP_REC_LEN);
            N.cp_have[slot] = 1;
        }
        if (N.cp_have[slot] && cp_record_crc(N.cp_rec[slot]) == N.cp_start_crc[slot]) continue;
        complete = 0;
        if (ask)
            fprintf(stderr, "[NETPLAY] slot %d plays record %08X; this PC has %s%08X\n", slot, N.cp_start_crc[slot],
                    N.cp_have[slot] ? "" : "none, last ", N.cp_have[slot] ? cp_record_crc(N.cp_rec[slot]) : 0u);
        if (ask && N.have_token) {
            uint8_t gb[6 + 8 + 1];
            int k = header(gb, T_RECGET);
            memcpy(gb + k, N.token, 8); k += 8;
            gb[k++] = (uint8_t)slot;
            send_raw(gb, k);
        }
    }
    return complete;
}

int netplay_created_record(int pos, uint8_t *out)
{
    int slot, ok = 0;
    if (pos < 0 || pos >= MAXP) return 0;
    EnterCriticalSection(&g_cs);
    slot = N.start_slots[pos];
    if (slot >= 0 && slot < ROOMSLOTS && N.cp_start_crc[slot] && N.cp_have[slot] &&
        cp_record_crc(N.cp_rec[slot]) == N.cp_start_crc[slot])
        ok = cp_record_sanitize(N.cp_rec[slot], out);
    LeaveCriticalSection(&g_cs);
    return ok;
}

int netplay_slot_created(int slot, char *name, int size, const char **style)
{
    uint8_t rec[CP_REC_LEN], ok[CP_REC_LEN];
    int have = 0;
    if (size > 0) name[0] = 0;
    if (style) *style = "";
    if (slot < 0 || slot >= ROOMSLOTS) return 0;
    EnterCriticalSection(&g_cs);
    if (slot == N.slot && N.cp_mine_state > 0) { memcpy(rec, N.cp_mine, CP_REC_LEN); have = 1; }
    else if (N.cp_have[slot]) { memcpy(rec, N.cp_rec[slot], CP_REC_LEN); have = 1; }
    LeaveCriticalSection(&g_cs);
    if (!have || !cp_record_sanitize(rec, ok)) return 0;
    cp_record_name(ok, name, size);
    if (style) *style = cp_record_style(ok);
    return 1;
}

int netplay_created_available(void)
{
    /* the relay keeps the picked record, or kept the one before and the new pick is on its way */
    return N.lobby && N.relay_caps > 0 && (N.relay_caps & CAPS_CREATED) && N.cp_mine_state > 0 &&
           ((N.cp_acked && N.cp_acked == cp_record_crc(N.cp_mine)) || (N.cp_prev_crc && N.cp_tries < RETRIES));
}

int netplay_created_pick(int index)
{
    uint8_t rec[CP_REC_LEN];
    if (!cp_local_set_pick(index) || !cp_local_record(rec)) return 0;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    if (N.cp_mine_state <= 0 || memcmp(rec, N.cp_mine, CP_REC_LEN)) {
        if (N.cp_mine_state > 0 && N.cp_acked && N.cp_acked == cp_record_crc(N.cp_mine)) {
            memcpy(N.cp_prev, N.cp_mine, CP_REC_LEN);
            N.cp_prev_crc = N.cp_acked;
        }
        memcpy(N.cp_mine, rec, CP_REC_LEN);
        if (N.cp_mine_state) N.cp_mine_state = 1;     /* not read yet (0): the session reads the pick */
        N.cp_tries = 0;
        N.cp_sent = 0;      /* the upload goes out with the next pass */
        N.roomv.version++;
        fprintf(stderr, "[NETPLAY] created fighter picked: record %08X\n", cp_record_crc(rec));
    }
    LeaveCriticalSection(&g_cs);
    return 1;
}

static void on_start_as(const uint8_t *b, int len, int watching)
{
    Reader r = { b, len, 0, 0 };
    AutoMatch m;
    uint32_t seed;
    int count, i, j, slots[MAXP], fighters[MAXP], pos = -1, deferred;
    if (len < 4) return;
    seed = get32(b);
    r.i = 4;
    memset(&m, 0, sizeof(m));
    m.type = rd8(&r);
    m.rules = rd8(&r);
    m.venue = rd8(&r);
    count = rd8(&r);
    if (count < 2 || count > MAXP) return;
    for (i = 0; i < count; i++) { slots[i] = rd8(&r); fighters[i] = rd8(&r); }
    if (r.bad) return;
    {   /* created fighters: the records START names (absent: none) */
        uint32_t named[ROOMSLOTS];
        memset(named, 0, sizeof(named));
        if (r.i < r.n) {
            int nc = rd8(&r), k;
            for (k = 0; k < nc && !r.bad; k++) {
                int s = rd8(&r), j;
                uint32_t crc = 0;
                for (j = 0; j < 4; j++) crc |= (uint32_t)rd8(&r) << (8 * j);     /* in order: no reads in one expression */
                if (s < ROOMSLOTS) named[s] = crc;
            }
            if (r.bad) return;
        }
        if (!(N.start_seed && seed == N.start_seed)) {
            EnterCriticalSection(&g_cs);
            memcpy(N.cp_start_crc, named, sizeof(named));
            LeaveCriticalSection(&g_cs);
        }
    }
    if (N.start_seed && seed == N.start_seed) { N.start_this_play = 1; return; }     /* a repeat */
    /* King of the Hill: the last fight of this PC is still on its results screen (also when a
     * START deferred there was called off since: its match is over, the results still show) */
    deferred = N.lobby && N.koth_results && NETPLAY_IS_KOTH(N.roomv.settings.mode);
    /* positions follow the room slots in order */
    for (i = 1; i < count; i++)
        for (j = i; j > 0 && slots[j - 1] > slots[j]; j--) {
            int t = slots[j]; slots[j] = slots[j - 1]; slots[j - 1] = t;
            t = fighters[j]; fighters[j] = fighters[j - 1]; fighters[j - 1] = t;
        }
    for (i = 0; i < ROOMSLOTS; i++) N.pos_of_slot[i] = -1;
    for (i = 0; i < count; i++) {
        if (slots[i] < ROOMSLOTS) N.pos_of_slot[slots[i]] = i;
        if (slots[i] == N.slot) pos = i;
        N.start_slots[i] = slots[i];
        m.fighter[i] = fighters[i];
        m.pad[i] = i;
    }
    if (pos < 0 && !watching) return;
    if (watching) pos = -1;         /* spectator mode: no position, every player is remote */
    m.count = count;
    {   /* the mode's free places go to CPU fighters, the same on every PC */
        const OnlineMode *mode = online_mode(m.type);
        const OnlineFighter *roster;
        int roster_count = online_fighters(&roster);
        for (i = count; mode && i < mode->fighters && i < MAXP; i++) {
            m.pad[i] = -1;
            m.fighter[i] = roster_count ? roster[mix32(seed ^ (0xC0FFEEu + (uint32_t)i)) % (uint32_t)roster_count].id : 55;
            m.count = i + 1;
        }
    }
    EnterCriticalSection(&g_cs);
    memset(N.rematch_want, 0, sizeof(N.rematch_want));
    N.start_seed = seed;
    N.session = seed;
    N.players = count;
    N.fighters = m.count;
    N.pos = pos;
    N.watching = watching;
    N.spec_start_down = 1;          /* a START held from before does not stop the watching */
    N.delay = LOBBY_DELAY;
    N.delay_fixed = 0;
    for (i = 0; i < MAXP; i++) N.peer_rtt[i] = -1, N.peer_rb[i] = 0, N.peer_results[i] = N.peer_on_results[i] = 0;
    for (i = 0; i < MAXP; i++) N.peer_delay[i] = 0;
    spec_reset();
    /* This PC rolls back only when its player has the beta on and the relay allows it: the relay's
     * switch turns the beta off for everyone without a new build (2026-10-26, after rollback
     * crashes). Sent in P_RTT, so every PC of the match decides from the same flags. */
    if (pos >= 0) N.peer_rb[pos] = rollback_wanted() && N.relay_caps > 0 && (N.relay_caps & CAPS_ROLLBACK);
    if (pos >= 0 && rollback_wanted() && !N.peer_rb[pos])
        fprintf(stderr, "[NETPLAY] rollback is switched off on the server: this match plays in lockstep\n");
    N.rb_match = 0;                         /* decided at the sync point */
    if (pos >= 0) {   /* this PC's ping as of now, with the worst of the last few as the jitter margin */
        int k, worst = N.rtt_ms > 0 ? N.rtt_ms : 0;
        for (k = 0; k < 5; k++) if (N.rtt_recent[k] > worst) worst = N.rtt_recent[k];
        N.peer_rtt[pos] = worst > 2000 ? 2000 : worst;
    }
    N.match_count = 0;
    N.match_live = 1;
    N.match_seen = 0;
    N.over_pending = 0;
    N.start_this_play = 1;
    N.start_tick = GetTickCount64();
    N.load_done = N.load_hinted = 0;
    N.report_m = m;
    for (i = 0; i < MAXP; i++) {
        N.report_names[i][0] = 0;
        if (i < count)
            for (j = 0; j < N.roomv.player_count; j++)
                if (N.roomv.players[j].slot == slots[i])
                    snprintf(N.report_names[i], sizeof(N.report_names[i]), "%s", N.roomv.players[j].name);
    }
    snprintf(N.report_key, sizeof(N.report_key), "%.6s-%08X", N.roomv.code[0] ? N.roomv.code : N.room, seed);
    N.reported = N.fighting_seen = 0;
    disarm_crash_report();
    /* King of the Hill: one fight, then RESULT and back to the hill */
    N.koth_match = NETPLAY_IS_KOTH(N.roomv.settings.mode);
    /* a ranked set's game: its RESULT goes to the relay too (the set's score, the loser's venue pick) */
    N.set_match = !watching && !N.koth_match && N.rank.valid && N.rank.has_set && !memcmp(N.rank_room, N.roomv.code, 6);
    N.set_game = N.rank.game;
    N.koth_fights = N.roomv.hill.fights;
    N.koth_winner = -1;
    N.koth_fought = N.koth_done = 0;
    N.start_deferred = 0;
    N.vs_king = -1;                 /* the versus screen's crown: the king as the START found it */
    N.vs_streak = N.roomv.hill.streak;
    if (N.koth_match && N.roomv.hill.valid)
        for (i = 0; i < count; i++) if (slots[i] == N.roomv.hill.king) N.vs_king = i;
    LeaveCriticalSection(&g_cs);
    reset_remote_pads();
    if (watching)
        fprintf(stderr, "[NETPLAY] START: seed %08X, type %d, rules %d, venue %d, %d players, this PC watches (spectator)\n",
                seed, m.type, m.rules, m.venue, count);
    else
        fprintf(stderr, "[NETPLAY] START: seed %08X, type %d, rules %d, venue %d, %d players, this PC is player %d%s\n",
                seed, m.type, m.rules, m.venue, count, pos + 1, N.koth_match ? " (a King of the Hill fight)" : "");
    if (deferred) {
        /* The king's next fight while its game still shows the last one's results: the new
         * opponent's controller would drive this PC's results menu as player 2 (it chose Rematch
         * over and over in the three-copy test), so the match is set up only once the game is back
         * in the menus (netplay_on_main_menu). The pings (P_RTT) go out meanwhile. */
        InterlockedExchange(&N.lockstep, 0);
        assign_input(0);
        N.start_deferred = 1;
        N.deferred_m = m;
        N.deferred_toast = GetTickCount64() - 4500;     /* the first notice in 1.5 s (the other's P_RTT first), then every 6 s */
        send_rtt();                 /* and the opponent, who otherwise sees only a long load (P_RTT's flag) */
        fprintf(stderr, "[NETPLAY] the match starts once this game is back from the last fight's results\n");
        return;
    }
    become_connected();
    EnterCriticalSection(&g_cs);
    if (!cp_records_complete(1)) {
        /* a created fighter's record has not arrived: the match is set up once it has (the network
         * thread asks again), so every PC writes the same one */
        N.cp_wait_m = m;
        N.cp_wait_since = N.cp_get_sent = GetTickCount64();
        InterlockedExchange(&N.cp_waiting, 1);
        LeaveCriticalSection(&g_cs);
        fprintf(stderr, "[NETPLAY] waiting for a created fighter's record\n");
        return;
    }
    LeaveCriticalSection(&g_cs);
    automatch_request(&m);
}

static void on_start(const uint8_t *b, int len) { on_start_as(b, len, 0); }

static void on_packet(const uint8_t *b, int len)
{
    if (len < 6 || memcmp(b, "DJNP", 4) || b[4] != PROTO) return;
    N.last_rx = GetTickCount64();
    switch (b[5]) {
    case T_WELCOME:
        if (len >= 6 + 8 + 1 + 6) {
            memcpy(N.token, b + 6, 8);
            N.have_token = 1;
            if (N.lobby) {
                if (N.state == NETPLAY_CONNECTING) {
                    InterlockedExchange(&N.state, NETPLAY_LOBBY);
                    set_status("In the lobby of %s.", N.server_text);
                    pc_input_online_ports();    /* a room of four needs a controller in every port */
                    netplay_lobby_list(0xFF);
                    /* what this client supports; the network thread repeats it until the relay
                     * answers (an older relay never does: then no King of the Hill) */
                    N.caps_tries = 0;
                    N.caps_sent = 0;
                    N.relay_caps = -1;
                    N.cp_acked = 0;         /* the relay forgot our record with the session */
                    N.cp_tries = 0;
                }
                break;
            }
            N.slot = b[14] ? 1 : 0;
            N.pos = N.slot;
            memcpy(N.room, b + 15, 6);
            N.room[6] = 0;
            if (N.state == NETPLAY_CONNECTING) {
                InterlockedExchange(&N.state, NETPLAY_WAITING);
                if (N.creating)
                    set_status("Room %s is open. Tell the other player this code; waiting for them to join.", N.room);
                else
                    set_status("In room %s; waiting for the host.", N.room);
            }
        }
        break;
    case T_PEER:
        if (len >= 8 && !N.lobby) {
            int n = b[7];
            if (n == 0) {
                if (N.state == NETPLAY_CONNECTED || N.state == NETPLAY_SYNCING)
                    lose_peer("The other player left.");
            } else if (len >= 8 + n && N.state == NETPLAY_WAITING) {
                memcpy(N.peer_name, b + 8, (size_t)(n < 24 ? n : 24));
                N.peer_name[n < 24 ? n : 24] = 0;
                InterlockedExchange(&N.state, NETPLAY_SYNCING);
                if (N.creating) {
                    LARGE_INTEGER t;
                    QueryPerformanceCounter(&t);
                    N.session = mix32((uint32_t)t.QuadPart ^ (uint32_t)GetCurrentProcessId() ^ 0x9E3779B9u);
                    N.acked = 0;
                    N.hello_sent = 0;
                }
                set_status("%s joined; agreeing on the session...", N.peer_name);
            }
        }
        break;
    case T_DATA:
        if (len >= 8 && b[7] == P_VOICE) {     /* voice chat: by relay slot, in the room and the match */
            voice_on_packet(b[6], b + 7, len - 7);
            break;
        }
        if (len >= 10 && b[7] == P_PINGINFO && N.lobby && b[6] < ROOMSLOTS) {   /* a member's ping, by relay slot */
            if (N.slot_rtt[b[6]] < 0)
                fprintf(stderr, "[NETPLAY] room: the player in slot %d reports a ping of %d ms\n", b[6], b[8] | b[9] << 8);
            N.slot_rtt[b[6]] = b[8] | b[9] << 8;
            break;
        }
        if (len >= 7) {
            int from = N.lobby ? (b[6] < ROOMSLOTS ? N.pos_of_slot[b[6]] : -1) : (b[6] ? 1 : 0);
            /* King of the Hill: a fight this PC does not play in (it watches, or waits) is not its
             * lockstep; phase 1 does not run the watched fight */
            if (N.lobby && !N.match_live && NETPLAY_IS_KOTH(N.roomv.settings.mode)) break;
            on_data(from, b + 7, len - 7);
        }
        break;
    case T_CAPS:
        if (N.lobby && len >= 10) {
            int had = N.relay_caps;
            N.relay_caps = (int)(get32(b + 6) & 0x7FFFFFFFu);
            if (had != N.relay_caps)
                fprintf(stderr, "[NETPLAY] the relay supports %08X%s%s%s%s\n", (unsigned)N.relay_caps,
                        N.relay_caps & CAPS_KOTH ? " (King of the Hill)" : "",
                        N.relay_caps & CAPS_ROLLBACK ? " (rollback allowed)" : "",
                        N.relay_caps & CAPS_CREATED ? " (created fighters allowed)" : "",
                        N.relay_caps & CAPS_SPECTATE ? " (spectators)" : "");
            rollback_server_allows((N.relay_caps & CAPS_ROLLBACK) != 0);    /* for the next start's audio arena */
        }
        break;
    case T_HILL:
        if (N.lobby) on_hill(b + 6, len - 6);
        break;
    case T_RECORD:
        if (N.lobby) on_record(b + 6, len - 6);
        break;
    case T_WATCH:
        if (N.lobby) on_watch(b + 6, len - 6);
        break;
    case T_NOTICE:      /* u16 seconds left, u8 length, text: the relay restarts soon (maintenance) */
        if (len >= 9 && b[8] == len - 9) {
            unsigned secs = (unsigned)b[6] | (unsigned)b[7] << 8, tl = b[8] < sizeof(N.notice) ? b[8] : sizeof(N.notice) - 1;
            EnterCriticalSection(&g_cs);
            memcpy(N.notice, b + 9, tl);
            N.notice[tl] = 0;
            N.notice_at = GetTickCount64() + (ULONGLONG)secs * 1000u;
            N.notice_shown = 0;
            LeaveCriticalSection(&g_cs);
            fprintf(stderr, "[NETPLAY] the relay restarts in %u s: %s\n", secs, N.notice);
        }
        break;
    case T_PONG:
        if (len >= 10 && get32(b + 6) == N.ping_nonce)
        {
            N.rtt_ms = (int)(GetTickCount64() - N.last_ping);
            N.rtt_recent[N.rtt_next] = N.rtt_ms;
            N.rtt_next = (N.rtt_next + 1) % 5;
        }
        break;
    case T_ROOMS:
        if (N.lobby) on_rooms(b + 6, len - 6);
        break;
    case T_PLAYERS:
        if (N.lobby) on_players(b + 6, len - 6);
        break;
    case T_QUEUE:
        if (N.lobby) on_queue(b + 6, len - 6);
        break;
    case T_RANKINFO:
        if (N.lobby) on_rankinfo(b + 6, len - 6);
        break;
    case T_DRAFT:
        if (N.lobby) on_draft(b + 6, len - 6);
        break;
    case T_ROOM:
        if (N.lobby) on_room(b + 6, len - 6);
        break;
    case T_START:
        if (N.lobby) on_start(b + 6, len - 6);
        break;
    case T_ERROR:
        if (len >= 7) {
            static const char *const why[] = { "", "No room with that code.", "That room is full.",
                "The other player has a different version of the game.", "That name is not allowed.",
                "The server is full.", "Too many attempts; wait a minute.", "The server lost this session.",
                "Only the room's host can do that, and not while voting or playing.",
                "Not possible here: not in a room, or already in one.",
                "A new build is out: close the game and update in the launcher to play online." };
            int code = b[6];
            const char *text = code >= 1 && code <= 10 ? why[code] : "The server refused the request.";
            if (N.lobby && (code == 1 || code == 2 || code == 3 || code == 8 || code == 9 || code == 10)) {
                /* refused, but the session goes on */
                EnterCriticalSection(&g_cs);
                /* "not in a room" to a room action (a vote, ready, settings) while this side still
                 * shows the room: the server let us go and the news was lost, e.g. the other
                 * player quit during the vote. Its word counts: back to the lobby, or the vote
                 * screen stays up with no way out (a player's report, 2026-10-22). */
                if (code == 1 && SPECTATOR_HERE() && !(N.pend_len && N.pend[5] == T_SPECTATE)) {
                    /* spectator mode: the room this PC watched closed (its last player left) */
                    unsigned version = N.roomv.version;
                    memset(&N.roomv, 0, sizeof(N.roomv));
                    N.roomv.version = version + 1;
                    N.room[0] = 0;
                    text = "The room closed: its players left.";
                    if (N.watching) InterlockedExchange(&N.lockstep, 0);
                } else if (code == 9 && N.roomv.in_room && N.pend_len && N.pend[5] != T_CREATE &&
                    N.pend[5] != T_JOIN && N.pend[5] != T_QUICK) {
                    unsigned version = N.roomv.version;
                    memset(&N.roomv, 0, sizeof(N.roomv));
                    N.roomv.version = version + 1;
                    N.room[0] = 0;
                    text = "The room is gone (the other player left).";
                }
                answered();
                snprintf(N.roomv.error, sizeof(N.roomv.error), "%s", text);
                N.roomv.version++;
                LeaveCriticalSection(&g_cs);
                fprintf(stderr, "[NETPLAY] refused: %s\n", text);
                break;
            }
            if (N.lobby && code == 7) {             /* the server restarted: enter again */
                N.have_token = 0;
                N.started = GetTickCount64();
                memset(&N.roomv, 0, sizeof(N.roomv));
                InterlockedExchange(&N.state, NETPLAY_CONNECTING);
                set_status("Reconnecting to %s...", N.server_text);
                break;
            }
            InterlockedExchange(&N.lockstep, 0);
            InterlockedExchange(&N.state, NETPLAY_FAILED);
            set_status("%s", text);
        }
        break;
    }
}

/* ---- the network thread -------------------------------------------------------- */

static void send_live_pad(ULONGLONG now)
{
    XBOX_INPUT_STATE s;
    uint8_t p[5 + sizeof(XBOX_GAMEPAD)];
    memset(&s, 0, sizeof(s));
    xbox_HostInputGetLocalState(&s);
    if (!memcmp(&s.Gamepad, &N.last_sent, sizeof(XBOX_GAMEPAD)) && now - N.last_pad_sent < 100) return;
    N.last_sent = s.Gamepad;
    N.last_pad_sent = now;
    p[0] = P_LIVEPAD;
    put32(p + 1, ++N.local_seq);
    memcpy(p + 5, &s.Gamepad, sizeof(XBOX_GAMEPAD));
    send_data(p, (int)sizeof(p));
}

/* RECOMP_NETPLAY_LAG_MS=<ms>[,<jitter ms>] (tests): what arrives is handled that much later, so
 * two copies on one PC play as if far apart. In order: jitter delays, it does not reorder. */
/* 1 unless this is a King of the Hill room where this PC does not fight right now. */
static int koth_fighting_here(void)
{
    int i, on = 0;
    if (!NETPLAY_IS_KOTH(N.roomv.settings.mode)) return 1;
    EnterCriticalSection(&g_cs);
    if (N.roomv.hill.valid && N.roomv.hill.phase == NETHILL_FIGHTING)
        for (i = 0; i < N.roomv.hill.count; i++)
            if (N.roomv.hill.line[i].slot == N.roomv.you && N.roomv.hill.line[i].state == NETLINE_FIGHTING) on = 1;
    LeaveCriticalSection(&g_cs);
    return on;
}

#define LAG_QUEUE 1024
static uint8_t lag_buf[LAG_QUEUE][1500];
static int lag_len[LAG_QUEUE], lag_head, lag_n, lag_ms = -1, lag_jitter;
static ULONGLONG lag_due[LAG_QUEUE];

static DWORD WINAPI net_thread(LPVOID unused)
{
    uint8_t b[1500];
    (void)unused;
    if (lag_ms < 0) {
        const char *v = getenv("RECOMP_NETPLAY_LAG_MS");
        lag_ms = v ? atoi(v) : 0;
        lag_jitter = v && strchr(v, ',') ? atoi(strchr(v, ',') + 1) : 0;
        if (lag_ms > 0) fprintf(stderr, "[NETPLAY] tests: arriving packets held %d ms (+0..%d)\n", lag_ms, lag_jitter);
    }
    {   /* named for crash reports (main.c prints the thread's name) */
        typedef HRESULT (WINAPI *SetThreadDescriptionFn)(HANDLE, PCWSTR);
        SetThreadDescriptionFn name = (SetThreadDescriptionFn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                                    "SetThreadDescription");
        if (name) name(GetCurrentThread(), L"online play");
    }
    N.started = N.last_rx = GetTickCount64();
    if (!g_data_print) make_data_print();
    while (!N.stop) {
        ULONGLONG now = GetTickCount64();
        int got;
        ac_net_tick(now);       /* a late pass: the whole game stood still (anticheat.c) */
        /* the relay's restart countdown (T_NOTICE): the notice, refreshed every second, until then */
        if (N.notice_at && now - N.notice_shown >= 1000) {
            unsigned left = now < N.notice_at ? (unsigned)((N.notice_at - now + 999) / 1000) : 0;
            EnterCriticalSection(&g_cs);
            snprintf(N.toast, sizeof(N.toast), "%s in %u:%02u. Fights already running are played to the end; "
                     "no new fight starts until then.", N.notice, left / 60, left % 60);
            N.toast_until = now + 1500;
            N.notice_shown = now;
            if (!left) N.notice_at = 0;
            LeaveCriticalSection(&g_cs);
        }
        /* The first match after the game starts reads everything from the disk: 20 to 40 s on the
         * menu's LOADING, which players took for a freeze (F9 reports, build 23). */
        /* (the versus screen says it itself while it is up) */
        if (N.match_live && N.start_tick && !N.load_done && !N.load_hinted && !N.start_deferred && !N.vs_start &&
            now - N.start_tick > 8000) {
            N.load_hinted = 1;
            toast(12000, "Loading the match... The first one after the game starts can take up to half a minute.");
        }
        /* King of the Hill: the next opponent waits, loaded, while this player stays on the last
         * fight's results (13, 14 and 61 s in build 33, pressing Rematch): say so until they leave */
        if (N.start_deferred && now - N.deferred_toast >= 6000) {
            int other = N.players == 2 ? 1 - N.pos : -1;
            N.deferred_toast = now;
            /* not while the other one is on its results too (its P_RTT says so): nobody waits yet */
            if (other < 0 || !N.peer_on_results[other])
                toast(6500, "%s is waiting for you: choose No to start the next fight.", player_name(other));
        }
        if (N.state == NETPLAY_CONNECTING) {
            if (now - N.hello_sent >= 500) { send_hello(); N.hello_sent = now; }
            if (now - N.started > 10000) {
                InterlockedExchange(&N.state, NETPLAY_FAILED);
                if (N.lobby)
                    set_status("No answer from the server %s. It may be down, or an older server without the "
                               "online hub (play by code still works there).", N.server_text);
                else
                    set_status("No answer from the server %s. Check the address and that it is running.", N.server_text);
            }
        }
        if (N.have_token && N.state != NETPLAY_FAILED) {
            if (now - N.last_ping >= 1000) {
                N.ping_nonce++;
                N.last_ping = now;
                send_token_only(T_PING, N.ping_nonce, 1);
            }
            if (now - N.last_rx > 15000) {
                InterlockedExchange(&N.lockstep, 0);
                assign_input(0);
                InterlockedExchange(&N.state, NETPLAY_FAILED);
                set_status("The connection to the server was lost.");
            }
        }
        if (N.pend_len && now - N.pend_sent >= RETRY_MS) {
            uint8_t again[sizeof(N.pend)];
            int n = 0;
            EnterCriticalSection(&g_cs);
            if (N.pend_len && N.pend_tries >= RETRIES) {
                answered();
                snprintf(N.roomv.error, sizeof(N.roomv.error), "The server did not answer; try again.");
                N.roomv.version++;
            } else if (N.pend_len) {
                memcpy(again, N.pend, (size_t)N.pend_len);
                n = N.pend_len;
                N.pend_tries++;
                N.pend_sent = now;
            }
            LeaveCriticalSection(&g_cs);
            if (n) send_raw(again, n);
        }
        /* King of the Hill: CAPS until the relay answers (not a pending request: it must not
         * displace a JOIN or READY); no answer after CAPS_TRIES: an older relay, no King of the Hill */
        if (N.lobby && N.have_token && N.state == NETPLAY_LOBBY && N.relay_caps < 0 && now - N.caps_sent >= RETRY_MS) {
            if (N.caps_tries++ < CAPS_TRIES) {
                uint8_t cb[6 + 8 + 4];
                int n = header(cb, T_CAPS);
                memcpy(cb + n, N.token, 8); n += 8;
                put32(cb + n, CAPS_KOTH | CAPS_CREATED | CAPS_SPECTATE); n += 4;
                send_raw(cb, n);
            } else {
                N.relay_caps = 0;
                fprintf(stderr, "[NETPLAY] the relay did not answer CAPS: an older relay, no King of the Hill\n");
            }
            N.caps_sent = now;
        }
        /* created fighters: upload this player's record once the relay allows them, until it is kept */
        if (N.lobby && N.have_token && N.state == NETPLAY_LOBBY && N.relay_caps > 0 && (N.relay_caps & CAPS_CREATED)) {
            uint8_t fb[6 + 8 + 2 + CP_REC_LEN];
            int n = 0;
            if (!N.cp_mine_state) {
                uint8_t rec[CP_REC_LEN];
                int have = cp_local_record(rec);    /* the player's pick (outside g_cs: it reads files) */
                EnterCriticalSection(&g_cs);
                if (!N.cp_mine_state) {
                    if (have) memcpy(N.cp_mine, rec, CP_REC_LEN);
                    N.cp_mine_state = have ? 1 : -1;
                    N.cp_tries = 0;
                }
                LeaveCriticalSection(&g_cs);
            }
            EnterCriticalSection(&g_cs);
            if (N.cp_mine_state > 0 && N.cp_acked != cp_record_crc(N.cp_mine) && N.cp_tries < RETRIES &&
                now - N.cp_sent >= RETRY_MS) {
                n = header(fb, T_FIGHTER);
                memcpy(fb + n, N.token, 8); n += 8;
                fb[n++] = (uint8_t)CP_REC_LEN; fb[n++] = (uint8_t)(CP_REC_LEN >> 8);
                memcpy(fb + n, N.cp_mine, CP_REC_LEN); n += CP_REC_LEN;
                N.cp_tries++;
                N.cp_sent = now;
            }
            LeaveCriticalSection(&g_cs);
            if (n) send_raw(fb, n);
        }
        if (N.cp_waiting) {
            int complete, late;
            AutoMatch wm;
            EnterCriticalSection(&g_cs);
            complete = cp_records_complete(now - N.cp_get_sent >= RETRY_MS);
            if (now - N.cp_get_sent >= RETRY_MS) N.cp_get_sent = now;
            late = now - N.cp_wait_since >= CP_WAIT_MS;
            wm = N.cp_wait_m;
            if (complete || late) InterlockedExchange(&N.cp_waiting, 0);
            LeaveCriticalSection(&g_cs);
            if (complete) {
                fprintf(stderr, "[NETPLAY] the created fighters' records are here\n");
                automatch_request(&wm);
            } else if (late) {
                fprintf(stderr, "[NETPLAY] a created fighter's record never came: the match is left\n");
                end_hub_match("a created fighter's record never came");
            }
        }
        /* King of the Hill: this PC's RESULT, repeated until a HILL shows the fight over */
        if (N.lobby && N.have_token && N.koth_winner >= 0 && now - N.koth_sent >= RETRY_MS) {
            if (N.koth_tries++ < RESULT_TRIES) {
                uint8_t rb[6 + 8 + 5];
                int n = header(rb, T_RESULT);
                memcpy(rb + n, N.token, 8); n += 8;
                put32(rb + n, N.start_seed); n += 4;
                rb[n++] = (uint8_t)N.koth_winner;
                send_raw(rb, n);
            } else {
                N.koth_winner = -1;
            }
            N.koth_sent = now;
        }
        /* Hub: in a room, this PC's ping for the others' player cards and the high-ping warning */
        if (N.lobby && N.have_token && N.roomv.in_room && !SPECTATOR_HERE() && !N.match_live && N.rtt_ms >= 0 &&
            now - N.last_pinginfo >= PINGINFO_MS) {
            uint8_t p[3];
            int rtt = N.rtt_ms > 0xFFFF ? 0xFFFF : N.rtt_ms;
            p[0] = P_PINGINFO;
            p[1] = (uint8_t)rtt;
            p[2] = (uint8_t)(rtt >> 8);
            send_data(p, 3);
            N.last_pinginfo = now;
        }
        /* Hub: our ping for this START, until the match is under way. */
        if (N.lobby && N.match_live && !N.match_seen && now - N.last_rtt_sent >= 100)
            send_rtt();
        /* The room is playing but this PC never got its START: a VOTE makes the server send it again.
         * In a King of the Hill room only a fighter has a START to get (the HILL says who fights). */
        if (N.lobby && N.roomv.in_room && N.roomv.phase == 2 && !N.start_this_play && !N.over_pending &&
            koth_fighting_here() && now - N.last_recover >= 500 &&
            (!SPECTATOR_HERE() || (N.playing_since && now - N.playing_since < 10000))) {
            uint8_t vb[6 + 8 + 1];
            int n = header(vb, T_VOTE);
            memcpy(vb + n, N.token, 8); n += 8;
            vb[n++] = 0;
            send_raw(vb, n);
            N.last_recover = now;
        }
        if (!N.lobby && N.state == NETPLAY_SYNCING && N.creating && now - N.hello_sent >= 300) {
            uint8_t p[7];
            p[0] = P_GAMEHELLO;
            put32(p + 1, N.session);
            p[5] = (uint8_t)N.delay;
            p[6] = LOCKSTEP_VERSION;
            send_data(p, 7);
            N.hello_sent = now;
        }
        if ((N.state == NETPLAY_CONNECTED || N.state == NETPLAY_SYNCING) && !N.start_deferred && !N.watching)
            send_live_pad(now);
        {   /* wait up to 20 ms for a datagram, then take it; select() returns only when it is done,
             * unlike a blocking recv with SO_RCVTIMEO, whose timed-out request could still complete
             * later and write its status block into a stack slot another frame then used (the
             * "RIP=0x1E" crashes on this thread: 0x1E is a 30-byte packet's length;
             * docs/research/rip-1e-crash.md) */
            fd_set rd;
            struct timeval tv = { 0, 20000 };
            FD_ZERO(&rd);
            FD_SET(N.sock, &rd);
            got = select(0, &rd, NULL, NULL, &tv) > 0 ? recv(N.sock, (char *)b, sizeof(b), 0) : 0;
        }
        if (got > 0 && lag_ms > 0) {        /* tests: hold every arriving packet for a while */
            if (lag_n < LAG_QUEUE) {
                int at = (lag_head + lag_n++) % LAG_QUEUE;
                memcpy(lag_buf[at], b, (size_t)got);
                lag_len[at] = got;
                lag_due[at] = now + (ULONGLONG)lag_ms + (lag_jitter > 0 ? (ULONGLONG)(rand() % (lag_jitter + 1)) : 0);
            }
        } else if (got > 0)
            on_packet(b, got);
        while (lag_n && GetTickCount64() >= lag_due[lag_head]) {
            on_packet(lag_buf[lag_head], lag_len[lag_head]);
            lag_head = (lag_head + 1) % LAG_QUEUE;
            lag_n--;
        }
    }
    if (N.have_token)
        send_token_only(T_BYE, 0, 0);
    return 0;
}

static int resolve(const char *server)
{
    char host[128], port[16];
    const char *colon = strrchr(server, ':');
    struct addrinfo hints, *res = NULL;
    if (colon && colon != server) {
        snprintf(host, sizeof(host), "%.*s", (int)(colon - server), server);
        snprintf(port, sizeof(port), "%s", colon + 1);
    } else {
        snprintf(host, sizeof(host), "%s", server);
        snprintf(port, sizeof(port), "%d", DEFAULT_PORT);
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &res) || !res) {
        /* The project's server by its address when its name does not resolve (a resolver that
         * still remembers the name as missing, or a DNS outage). */
        const char *known = !_stricmp(host, PROJECT_SERVER_NAME) ? PROJECT_SERVER_ADDRESS :
                            !_stricmp(host, PROJECT_NA_NAME) ? PROJECT_NA_ADDRESS :
                            !_stricmp(host, PROJECT_WEST_NAME) ? PROJECT_WEST_ADDRESS : NULL;
        res = NULL;
        if (!known || getaddrinfo(known, port, &hints, &res) || !res)
            return -1;
        fprintf(stderr, "[NETPLAY] %s did not resolve; using %s\n", host, known);
    }
    memcpy(&N.server, res->ai_addr, res->ai_addrlen);
    N.server_len = (int)res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
}

/* The build's fingerprint: the version, the build number and the lockstep rules. Without the number
 * (builds 21-25) every remaster build had the same one and old and new builds met in rooms. */
void netplay_build_fingerprint(uint8_t out[8])
{
    uint32_t h1 = 2166136261u, h2;
    const char *v;
    int k, build = updater_local_build();
    for (v = DEFJAM_VERSION; *v; v++) h1 = (h1 ^ (uint8_t)*v) * 16777619u;
    for (k = 0; k < 4; k++) h1 = (h1 ^ (uint8_t)(build >> (8 * k))) * 16777619u;
    h2 = mix32(h1 ^ LOCKSTEP_VERSION);
    put32(out, h1); put32(out + 4, h2);
}

static int start(const char *server, const char *name, int creating, const char *room, int delay, int lobby)
{
    int i;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    netplay_stop();
    if (!server || !*server) { InterlockedExchange(&N.state, NETPLAY_FAILED); set_status("Enter the server address first."); return -1; }
    snprintf(N.server_text, sizeof(N.server_text), "%s", server);
    snprintf(N.name, sizeof(N.name), "%s", name && *name ? name : "Player");
    N.creating = creating;
    N.lobby = lobby;
    memset(N.room, 0, sizeof(N.room));
    if (!lobby && !creating) {
        for (i = 0; i < 6 && room && room[i]; i++) N.room[i] = (char)toupper((unsigned char)room[i]);
        if (i != 6) { InterlockedExchange(&N.state, NETPLAY_FAILED); set_status("A room code has 6 characters."); return -1; }
    }
    N.delay = delay >= 1 && delay <= 12 ? delay : 3;
    netplay_build_fingerprint(N.build);
    N.have_token = 0;
    N.peer_name[0] = 0;
    N.rtt_ms = -1;
    memset(N.rtt_recent, 0, sizeof(N.rtt_recent));
    N.local_seq = 0;
    N.players = N.fighters = 2;
    N.pos = 0;
    for (i = 0; i < ROOMSLOTS; i++) { N.pos_of_slot[i] = i < 2 ? i : -1; N.slot_rtt[i] = -1; }
    N.last_pinginfo = 0;
    N.relay_caps = -1;
    /* created fighters: read the player's own again (a new one may have been made meanwhile) */
    N.cp_mine_state = 0;
    N.cp_acked = 0;
    N.cp_prev_crc = 0;
    N.cp_tries = 0;
    memset(N.cp_have, 0, sizeof(N.cp_have));
    memset(N.cp_start_crc, 0, sizeof(N.cp_start_crc));
    N.cp_waiting = 0;
    created_fighter_reload();
    N.koth_match = 0;
    N.set_match = 0;
    N.koth_winner = -1;
    N.start_deferred = 0;
    N.koth_results = 0;
    memset(&N.rank, 0, sizeof(N.rank));
    reset_remote_pads();
    EnterCriticalSection(&g_cs);
    memset(N.in_tag, 0, sizeof(N.in_tag));
    memset(&N.roomv, 0, sizeof(N.roomv));
    N.room_count = 0;
    N.pend_len = 0;
    N.match_live = N.match_seen = N.over_pending = N.start_this_play = 0;
    N.start_seed = 0;
    N.match_count = 0;
    disarm_crash_report();
    LeaveCriticalSection(&g_cs);
    if (resolve(server)) { InterlockedExchange(&N.state, NETPLAY_FAILED); set_status("Cannot find the server %s.", server); return -1; }
    N.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (N.sock == INVALID_SOCKET) { InterlockedExchange(&N.state, NETPLAY_FAILED); set_status("No network socket."); return -1; }
    {   /* non-blocking: net_thread waits in select(), never in recv */
        u_long nonblocking = 1;
        ioctlsocket(N.sock, FIONBIO, &nonblocking);
    }
    N.stop = 0;
    N.hello_sent = 0;
    InterlockedExchange(&N.state, NETPLAY_CONNECTING);
    set_status("Connecting to %s...", server);
    N.thread = CreateThread(NULL, 0, net_thread, NULL, 0, NULL);
    if (N.thread) SetThreadPriority(N.thread, THREAD_PRIORITY_ABOVE_NORMAL);
    return N.thread ? 0 : -1;
}

int netplay_host(const char *server, const char *name, int input_delay)
{
    return start(server, name, 1, NULL, input_delay, 0);
}

int netplay_join(const char *server, const char *name, const char *room)
{
    return start(server, name, 0, room, 3, 0);
}

int netplay_lobby(const char *server, const char *name)
{
    return start(server, name, 0, NULL, LOBBY_DELAY, 1);
}

void netplay_stop(void)
{
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    InterlockedExchange(&N.lockstep, 0);
    assign_input(0);
    if (N.thread) {
        N.stop = 1;
        WaitForSingleObject(N.thread, 2000);
        CloseHandle(N.thread);
        N.thread = NULL;
    }
    if (N.sock != INVALID_SOCKET) { closesocket(N.sock); N.sock = INVALID_SOCKET; }
    N.have_token = 0;
    N.lobby = 0;
    N.match_live = N.match_seen = 0;
    disarm_crash_report();
    InterlockedExchange(&N.state, NETPLAY_OFF);
    SetEvent(N.arrived);
}

enum NetplayState netplay_state(void) { return (enum NetplayState)N.state; }

void netplay_status(char *text, int size)
{
    if (size <= 0) return;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    if (N.state == NETPLAY_CONNECTED && N.rtt_ms >= 0)
        snprintf(text, (size_t)size, "%s  Ping %d ms, input delay %d steps.%s", N.status, N.rtt_ms, N.delay,
                 N.desync ? "  The games went out of step in the last fight." : "");
    else if (N.state == NETPLAY_LOBBY && N.rtt_ms >= 0)
        snprintf(text, (size_t)size, "%s  Ping %d ms.", N.status, N.rtt_ms);
    else if (N.state == NETPLAY_OFF)
        snprintf(text, (size_t)size, "Not connected.");
    else
        snprintf(text, (size_t)size, "%s", N.status);
    LeaveCriticalSection(&g_cs);
}

void netplay_room(char *code, int size)
{
    if (size > 0) snprintf(code, (size_t)size, "%s", N.room);
}

/* Tests (src/hooks/test_control.c): the lockstep's state as one JSON object. */
int netplay_debug_json(char *out, int size)
{
    return snprintf(out, (size_t)size,
                    "{\"lockstep\":%d,\"lobby\":%d,\"players\":%d,\"pos\":%d,\"delay\":%d,\"step\":%u,"
                    "\"match\":\"%08X\",\"matches\":%u,\"desync\":%d,\"rtt_ms\":%d,\"match_live\":%d}",
                    (int)N.lockstep, N.lobby, N.players, N.pos, N.delay, (unsigned)N.step, (unsigned)N.match,
                    (unsigned)N.match_count, N.desync, N.rtt_ms, N.match_live);
}

/* ---- the hub, for the launcher and overlay ------------------------------------ */

int netplay_in_lobby(void) { return N.lobby; }

int netplay_lobby_address(char *out, int size)
{
    enum NetplayState st = (enum NetplayState)N.state;
    if (!N.lobby || st == NETPLAY_OFF || st == NETPLAY_FAILED || size <= 0) return 0;
    snprintf(out, (size_t)size, "%s", N.server_text);
    return 1;
}

int netplay_rtt(void) { return N.state != NETPLAY_OFF && N.state != NETPLAY_FAILED ? N.rtt_ms : -1; }

int netplay_room_ping(int slot)
{
    if (!N.lobby || !N.roomv.in_room || slot < 0 || slot >= ROOMSLOTS) return -1;
    if (slot == N.roomv.you) return N.rtt_ms;
    return N.slot_rtt[slot];
}

int netplay_lobby_caps(void) { return N.lobby ? N.relay_caps : 0; }

void netplay_lobby_list(int mode)
{
    uint8_t m = (uint8_t)mode;
    request(T_LIST, &m, 1);
}

/* Not a pending request (the online screens ask every few seconds, and a lost answer is simply
 * asked again), so it never displaces a JOIN or READY that is waiting for its ROOM. */
void netplay_lobby_who(void)
{
    uint8_t b[32];
    int n;
    if (!N.lobby || !N.have_token) return;
    n = header(b, T_WHO);
    memcpy(b + n, N.token, 8);
    send_raw(b, n + 8);
}

int netplay_lobby_players(NetPlayer *out, int max, int *online, int *rooms)
{
    int n;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    n = N.who_online < 0 ? -1 : (N.who_count < max ? N.who_count : max);
    if (n > 0) memcpy(out, N.who, sizeof(out[0]) * (size_t)n);
    if (online) *online = N.who_online;
    if (rooms) *rooms = N.who_rooms;
    LeaveCriticalSection(&g_cs);
    return n;
}

int netplay_lobby_rooms(NetRoomInfo *out, int max, unsigned *version)
{
    int n;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    n = N.room_count < max ? N.room_count : max;
    if (n > 0) memcpy(out, N.rooms, sizeof(out[0]) * (size_t)n);
    if (version) *version = N.rooms_version;
    LeaveCriticalSection(&g_cs);
    return n;
}

void netplay_lobby_create(const NetRoomSettings *settings)
{
    uint8_t b[64];
    request(T_CREATE, b, put_settings(b, settings));
}

void netplay_lobby_join(const char *code)
{
    uint8_t b[6];
    int i;
    for (i = 0; i < 6; i++) b[i] = (uint8_t)toupper((unsigned char)(code && code[i] ? code[i] : 0));
    request(T_JOIN, b, 6);
}

void netplay_lobby_spectate(const char *code)
{
    uint8_t b[6];
    int i;
    for (i = 0; i < 6; i++) b[i] = (uint8_t)toupper((unsigned char)(code && code[i] ? code[i] : 0));
    request(T_SPECTATE, b, 6);
}

int netplay_spectating(void)
{
    int on;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    on = N.watching || (N.lobby && SPECTATOR_HERE());
    LeaveCriticalSection(&g_cs);
    return on;
}

void netplay_lobby_quick(int mode, int max)
{
    int most = room_max(mode);
    uint8_t b[2] = { (uint8_t)mode, (uint8_t)(max < 2 ? 2 : max > most ? most : max) };
    request(T_QUICK, b, 2);
}

/* Asks the account service for a ticket (it blocks), then joins the queue with it. */
static DWORD WINAPI ranked_thread(LPVOID param)
{
    char ticket[33];
    uint8_t b[TICKET_LEN + 1 + NETRANK_EXT_LEN];
    int i, fighter = (int)(INT_PTR)param, pings[3] = { 0, 0, 0 }, n;
    NetServerInfo sv[NETSERVER_COUNT];
    unsigned modes;
    int primary;
    char code[7];
    if (!account_ranked_ticket(ticket)) {
        EnterCriticalSection(&g_cs);
        if (N.queue_state == NETQUEUE_TICKET) {
            N.queue_state = NETQUEUE_REFUSED;
            snprintf(N.queue_text, sizeof(N.queue_text), "%s", account_state() == ACCOUNT_IN ?
                     "The account service did not answer; try again." :
                     "Ranked play needs a signed-in account (sign in with Discord in the launcher).");
        }
        LeaveCriticalSection(&g_cs);
        return 0;
    }
    for (i = 0; i < TICKET_LEN; i++) {
        unsigned v;
        sscanf(ticket + 2 * i, "%2x", &v);
        b[i] = (uint8_t)v;
    }
    SecureZeroMemory(ticket, sizeof(ticket));
    b[TICKET_LEN] = (uint8_t)fighter;
    /* the long form: the modes, the primary, rollback, the pings to the three relays, a MOVE's code */
    n = netservers_get(sv, NETSERVER_COUNT);
    for (i = 0; i < n && i < 3; i++) pings[i] = sv[i].state == NETSERVER_UP ? sv[i].ping_ms : 0;
    EnterCriticalSection(&g_cs);
    modes = N.rk_modes;
    primary = N.rk_primary;
    memcpy(code, N.rk_code, sizeof(code));
    LeaveCriticalSection(&g_cs);
    netranked_encode_ext(b + TICKET_LEN + 1, modes, primary, rollback_enabled(), pings, code);
    fprintf(stderr, "[NETPLAY] ranked: queueing for %s (modes %u)%s%s, pings %d/%d/%d ms\n", netranked_mode_title(primary),
            modes, code[0] ? ", set " : "", code, pings[0], pings[1], pings[2]);
    EnterCriticalSection(&g_cs);
    i = N.queue_state == NETQUEUE_TICKET;   /* not cancelled meanwhile */
    if (i) N.queue_state = NETQUEUE_CHECKING;
    LeaveCriticalSection(&g_cs);
    if (i) request(T_RANKED, b, (int)sizeof(b));
    return 0;
}

void netplay_lobby_ranked(int fighter)
{
    netplay_lobby_ranked_ex(fighter, 1u << NETRANK_CLASSIC, NETRANK_CLASSIC);
}

static void ranked_start(int fighter, int again)
{
    HANDLE h;
    if (!N.lobby || !N.have_token) return;
    EnterCriticalSection(&g_cs);
    if (!again && N.queue_state != NETQUEUE_NONE && N.queue_state != NETQUEUE_REFUSED) {
        LeaveCriticalSection(&g_cs);
        return;                             /* already searching */
    }
    N.queue_accepted = -1;
    N.queue_match[0] = 0;
    memset(&N.queue_ext, 0, sizeof(N.queue_ext));
    N.queue_state = NETQUEUE_TICKET;
    N.queue_waited = N.queue_searching = 0;
    snprintf(N.queue_text, sizeof(N.queue_text), "Checking your account...");
    LeaveCriticalSection(&g_cs);
    h = CreateThread(NULL, 0, ranked_thread, (LPVOID)(INT_PTR)fighter, 0, NULL);
    if (h) CloseHandle(h);
}

void netplay_lobby_ranked_ex(int fighter, unsigned modes, int primary)
{
    if (primary < 0 || primary >= NETRANK_MODES) primary = NETRANK_CLASSIC;
    modes |= 1u << primary;
    if (fighter == 99 && primary != NETRANK_OPEN) fighter = 55;    /* a created fighter plays in Open only */
    EnterCriticalSection(&g_cs);
    N.rk_fighter = fighter;
    N.rk_modes = modes & 7u;
    N.rk_primary = primary;
    N.rk_code[0] = 0;
    LeaveCriticalSection(&g_cs);
    ranked_start(fighter, 0);
}

void netplay_lobby_ranked_again(const char *code)
{
    EnterCriticalSection(&g_cs);
    snprintf(N.rk_code, sizeof(N.rk_code), "%s", code ? code : "");
    LeaveCriticalSection(&g_cs);
    ranked_start(N.rk_fighter, 1);
}

void netplay_lobby_ranked_moved(int ok)
{
    EnterCriticalSection(&g_cs);
    if (N.queue_state == NETQUEUE_MOVE) {
        N.queue_state = ok ? NETQUEUE_TICKET : NETQUEUE_REFUSED;    /* TICKET: the new relay's RANKED follows */
        snprintf(N.queue_text, sizeof(N.queue_text), "%s", ok ? "Moving to the set's server..." :
                 "The set's server could not be reached; search again.");
    }
    LeaveCriticalSection(&g_cs);
}

void netplay_lobby_accept(int yes)
{
    uint8_t b[6 + 8 + 1];
    int n;
    EnterCriticalSection(&g_cs);
    if (N.queue_state != NETQUEUE_ACCEPT || !N.lobby || !N.have_token) { LeaveCriticalSection(&g_cs); return; }
    N.queue_accepted = yes ? 1 : 0;
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[NETPLAY] ranked: %s set %s\n", yes ? "accepting" : "declining", N.queue_match);
    n = header(b, T_ACCEPT);
    memcpy(b + n, N.token, 8); n += 8;
    b[n++] = (uint8_t)(yes ? 1 : 0);
    send_raw(b, n);     /* no answer comes: the next QUEUE says how it went; sent twice against a loss */
    send_raw(b, n);
}

int netplay_lobby_accepted(void)
{
    int a;
    EnterCriticalSection(&g_cs);
    a = N.queue_state == NETQUEUE_ACCEPT || N.queue_state == NETQUEUE_SEARCHING ? N.queue_accepted : -1;
    LeaveCriticalSection(&g_cs);
    return a;
}

int netplay_lobby_queue_ext(NetQueueExt *out)
{
    int ok;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    *out = N.queue_ext;
    ok = out->valid;
    LeaveCriticalSection(&g_cs);
    return ok;
}

int netplay_lobby_rankinfo(NetRankInfo *out)
{
    int ok;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    ok = N.rank.valid && N.roomv.in_room && !memcmp(N.rank_room, N.roomv.code, 6);
    if (ok) *out = N.rank;
    else memset(out, 0, sizeof(*out));
    LeaveCriticalSection(&g_cs);
    return ok;
}

int netplay_lobby_draft(NetDraft *out)
{
    int ok;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    ok = N.draft.valid && N.roomv.in_room && !memcmp(N.draft_room, N.roomv.code, 6);
    if (ok) *out = N.draft;
    else memset(out, 0, sizeof(*out));
    LeaveCriticalSection(&g_cs);
    return ok;
}

void netplay_lobby_draft_act(int step, int action, int id)
{
    uint8_t b[6 + 8 + 3];
    int n;
    EnterCriticalSection(&g_cs);
    if (!N.lobby || !N.have_token || !N.roomv.in_room) { LeaveCriticalSection(&g_cs); return; }
    LeaveCriticalSection(&g_cs);
    if (action != NETDRAFT_ACT_CURSOR)
        fprintf(stderr, "[NETPLAY] draft: step %d action %d id %d\n", step, action, id);
    n = header(b, T_DRAFTACT);
    memcpy(b + n, N.token, 8); n += 8;
    n += netdraft_encode_act(b + n, step, action, id);
    send_raw(b, n);     /* the next DRAFT is the answer; a lock goes twice against a loss (a repeat gets DRAFT) */
    if (action != NETDRAFT_ACT_CURSOR) send_raw(b, n);
}

int netplay_lobby_queue(int *waited, int *searching, char *text, int size)
{
    int state;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    state = N.queue_state;
    if (waited) *waited = N.queue_waited;
    if (searching) *searching = N.queue_searching;
    if (text && size > 0) snprintf(text, (size_t)size, "%s", N.queue_text);
    LeaveCriticalSection(&g_cs);
    return state;
}

void netplay_lobby_settings(const NetRoomSettings *settings)
{
    uint8_t b[64];
    request(T_SETTINGS, b, put_settings(b, settings));
}

void netplay_lobby_ready(int ready, int fighter)
{
    uint8_t b[2] = { (uint8_t)(ready ? 1 : 0), (uint8_t)fighter };
    request(T_READY, b, 2);
}

void netplay_lobby_vote(int choice)
{
    uint8_t b = (uint8_t)choice;
    request(T_VOTE, &b, 1);
}

void netplay_lobby_yield(void)
{
    request(T_YIELD, NULL, 0);
}

void netplay_lobby_leave(void)
{
    EnterCriticalSection(&g_cs);
    memset(&N.roomv, 0, sizeof(N.roomv));
    N.roomv.version = (unsigned)GetTickCount64();
    N.room[0] = 0;
    N.pend_len = 0;                 /* whatever was waiting belonged to the room */
    N.queue_state = NETQUEUE_NONE;  /* LEAVE also ends a ranked search */
    memset(&N.rank, 0, sizeof(N.rank));
    LeaveCriticalSection(&g_cs);
    /* The relay never answers LEAVE (docs/netplay-protocol.md), so it is sent twice and not
     * waited for; if both are lost the server still has this client in the room and says so
     * with its next ROOM, which brings the room back here. */
    if (N.lobby && N.have_token) {
        uint8_t b[6 + 8];
        int n = header(b, T_LEAVE);
        memcpy(b + n, N.token, 8);
        n += 8;
        send_raw(b, n);
        send_raw(b, n);
    }
    netplay_lobby_list(0xFF);
}

void netplay_lobby_room(NetRoom *out)
{
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    *out = N.roomv;
    LeaveCriticalSection(&g_cs);
}

/* A JSON string member's value: the names here are the game's printable ASCII. */
static int json_put(char *out, int size, int n, const char *s)
{
    if (n < size) out[n++] = '"';
    for (; *s && n < size - 2; s++) {
        if (*s == '"' || *s == '\\') out[n++] = '\\';
        if ((unsigned char)*s >= 0x20) out[n++] = *s;
    }
    if (n < size) out[n++] = '"';
    return n;
}

/* The room match for the account service (account.c), from a signed-in player's game: "start"
 * once the fight is loaded, then "result" when it is decided (winners as a mask of fighter slots,
 * `draw`), or "left" naming the players (mask) who stopped answering before that; "crashed" from
 * the crash handler when this game dies during the fight (netplay_on_crash). The service counts a
 * result when every player's game reports it alike, gives a player who left the loss, and makes a
 * match whose leaver's game crashed a no contest (netplay/relay/accounts/stats.go). Positions are
 * the fighters' controller slots: the room's players in slot order, then the CPU fighters. The
 * JSON goes to json; returns its length, 0 when there is no match to report. Called with g_cs
 * not held. */
static int report_json(const char *event, uint32_t winners, uint32_t left, int draw, char *json, int size)
{
    AutoMatch m;
    char names[MAXP][25], key[32];
    const OnlineMode *mode;
    int i, n = 0, pos, players, first = 1;
    EnterCriticalSection(&g_cs);
    if (!N.report_key[0]) {
        LeaveCriticalSection(&g_cs);
        return 0;
    }
    m = N.report_m;
    memcpy(names, N.report_names, sizeof(names));
    memcpy(key, N.report_key, sizeof(key));
    pos = N.pos;
    players = N.players;
    LeaveCriticalSection(&g_cs);
    mode = online_mode(m.type);
    n += snprintf(json + n, (size_t)(size - n), "{\"key\":\"%s\",\"mode\":%d,\"mode_name\":", key, m.type);
    n = json_put(json, size, n, mode ? mode->name : "");
    n += snprintf(json + n, (size_t)(size - n), ",\"venue\":%d,\"venue_name\":", m.venue);
    n = json_put(json, size, n, online_venue_name(m.venue));
    n += snprintf(json + n, (size_t)(size - n), ",\"you\":%d,\"players\":[", pos);
    for (i = 0; i < m.count && i < MAXP && n < size - 200; i++) {
        int cpu = i >= players;
        n += snprintf(json + n, (size_t)(size - n), "%s{\"slot\":%d,\"name\":", i ? "," : "", i);
        n = json_put(json, size, n, cpu ? "CPU" : names[i]);
        n += snprintf(json + n, (size_t)(size - n), ",\"fighter\":%d,\"fighter_name\":", m.fighter[i]);
        n = json_put(json, size, n, online_fighter_name(m.fighter[i]));
        n += snprintf(json + n, (size_t)(size - n), ",\"cpu\":%s}", cpu ? "true" : "false");
    }
    n += snprintf(json + n, (size_t)(size - n), "],\"winners\":[");
    for (i = 0; i < m.count && i < MAXP; i++)
        if (winners & (1u << i)) { n += snprintf(json + n, (size_t)(size - n), "%s%d", first ? "" : ",", i); first = 0; }
    n += snprintf(json + n, (size_t)(size - n), "],\"draw\":%s,\"event\":\"%s\"", draw ? "true" : "false", event);
    if (left) {
        first = 1;
        n += snprintf(json + n, (size_t)(size - n), ",\"left\":[");
        for (i = 0; i < m.count && i < MAXP; i++)
            if (left & (1u << i)) { n += snprintf(json + n, (size_t)(size - n), "%s%d", first ? "" : ",", i); first = 0; }
        n += snprintf(json + n, (size_t)(size - n), "]");
    }
    if (strcmp(event, "start") && strcmp(event, "crashed")) {
        /* build 37: the fight as it stood (time, each fighter's health in percent) and this PC's
         * anti-cheat facts, for the account service and the team's #cheat-flags */
        n += snprintf(json + n, (size_t)(size - n), ",\"fight\":{\"steps\":%u,\"health\":[", (unsigned)N.step);
        for (i = 0; i < m.count && i < MAXP && n < size - 40; i++) {
            float h = MEMF(ACTORS + (uint32_t)i * STRIDE + 0x350u), top = MEMF(ACTORS + (uint32_t)i * STRIDE + 0x354u);
            int pct = top > 0.0f && h == h ? (int)(100.0f * h / top + 0.5f) : -1;
            n += snprintf(json + n, (size_t)(size - n), "%s%d", i ? "," : "", pct < -1 ? -1 : pct > 100 ? 100 : pct);
        }
        n += snprintf(json + n, (size_t)(size - n), "]}");
        /* build 37: the fingerprint of the fight files and the ones read from the override folder */
        n += snprintf(json + n, (size_t)(size - n), ",\"files\":{\"print\":\"%08X\",\"override\":", (unsigned)g_data_print);
        n = json_put(json, size, n, g_data_over);
        n += snprintf(json + n, (size_t)(size - n), "}");
        n = ac_json(json, size - 2, n);
    }
    n += snprintf(json + n, (size_t)(size - n), "}");
    return n < size ? n : 0;
}

static void report_event(const char *event, uint32_t winners, uint32_t left, int draw)
{
    char json[2048];
    static int show = -1;   /* RECOMP_NETPLAY_REPORT_LOG=1 (tests): every report as it is made */
    if (!report_json(event, winners, left, draw, json, (int)sizeof(json))) return;
    if (show < 0) show = getenv("RECOMP_NETPLAY_REPORT_LOG") != NULL;
    if (show) fprintf(stderr, "[NETPLAY] report: %s\n", json);
    account_report_match(json);
}

/* The "crashed" report of the running match, built when its "start" goes out so that the crash
 * handler only copies host memory it owns: no guest memory, no g_cs (the crashed thread may hold
 * it), no allocation. Armed from the start report until the match is reported or ends. */
#define CRASH_REPORT_MS 1500        /* how long the crash handler waits for the service */
static char g_crash_report[2048];
static volatile LONG g_crash_armed;

static void disarm_crash_report(void) { InterlockedExchange(&g_crash_armed, 0); }

static void arm_crash_report(void)
{
    char json[2048];
    int n;
    disarm_crash_report();
    n = report_json("crashed", 0, 0, 0, json, (int)sizeof(json));
    if (n <= 0 || n >= (int)sizeof(g_crash_report)) return;
    memcpy(g_crash_report, json, (size_t)n + 1);
    InterlockedExchange(&g_crash_armed, 1);
}

/* From the crash handler (main.c), after the crash report is written: when an online match this
 * game reported the start of is running and not yet decided, tell the account service that this
 * game crashed, so the match counts for nobody instead of as this player's loss. Bounded
 * (CRASH_REPORT_MS); once. */
void netplay_on_crash(void)
{
    int status;
    if (!InterlockedExchange(&g_crash_armed, 0)) return;
    fprintf(stderr, "[NETPLAY] the game crashed during an online match: telling the account service (no contest)\n");
    fflush(stderr);
    status = account_report_crash(g_crash_report, CRASH_REPORT_MS);
    if (status > 0) fprintf(stderr, "[ACCOUNT] crash report sent: HTTP %d\n", status);
    else fprintf(stderr, "[ACCOUNT] crash report: %s\n", status < 0 ? "not sent (not signed in)" : "no answer in time");
    fflush(stderr);
}

/* The winners of a room match whose decision named fighter slot w: that slot, or in a Team match
 * (2 vs 2) its whole side. The game's decision names one fighter (the last one put out's primary
 * opponent; docs/research/team-and-special-match-types.md 3.4), and the side is the slot's pair:
 * slots 0 and 1 against 2 and 3. */
static uint32_t winners_of(uint32_t w)
{
    if (w >= MAXP) return 0;
    if (N.report_m.type == ONLINE_MODE_TEAM && N.fighters == 4) return 3u << (w & 2u);
    return 1u << w;
}

/* The room match decided, from netplay_on_step (code 4 a draw); once per match. */
void netplay_on_result(uint32_t winners, uint32_t losers, uint32_t code)
{
    (void)losers;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    if (!N.lobby || !N.match_live || N.reported || !N.report_key[0] || N.desync || !N.lockstep || N.watching) {
        LeaveCriticalSection(&g_cs);
        return;
    }
    N.reported = 1;
    LeaveCriticalSection(&g_cs);
    disarm_crash_report();
    fprintf(stderr, "[NETPLAY] match result: winners 0x%X, code 0x%X; reporting it when signed in\n",
            (unsigned)winners, (unsigned)code);
    report_event("result", winners, 0, (code & 4u) != 0);
}

/* Back in the menus after a room match whose decision was recorded but never reported: a ring out
 * (result code 0x801) ends the fight without the decided phases and without the decided flag, so the
 * step's reading saw no winner and the match went unreported (two-copy Ringout test, 2026-10-26). The
 * game marks every fighter it put out as ended (+0x932 bit 0x200, team-and-special-match-types.md
 * 3.4); the ones still standing are the winners, a Team match's side among them. */
#define F_DEFEAT_WORD 0x932u
static void report_missed_result(void)
{
    uint32_t count = MEM32(G_RESULT_COUNT), code = 0, standing = 0;
    int slot;
    if (N.reported || !N.fighting_seen || N.koth_match || !N.lockstep || N.desync) return;
    if (count && count <= RESULTS_MAX) code = MEM16(G_RESULTS + (count - 1u) * 0x10u + 0xEu);
    if (!(code & 1u)) return;                           /* nothing decisive recorded */
    if (code & 4u) { netplay_on_result(0, 0, 4u); return; }
    for (slot = 0; slot < N.fighters && slot < MAXP; slot++)
        if (!(MEM16(ACTORS + (uint32_t)slot * STRIDE + F_DEFEAT_WORD) & 0x200u)) standing |= 1u << slot;
    if (!standing) return;
    if (N.report_m.type == ONLINE_MODE_TEAM && N.fighters == 4) {
        standing = (standing & 3u ? 3u : 0u) | (standing & 0xCu ? 0xCu : 0u);
        if (standing == 0xFu) return;                   /* both sides standing: no side decided */
    } else if (standing & (standing - 1u)) {
        return;                                         /* more than one standing: not one winner */
    }
    fprintf(stderr, "[NETPLAY] the decision (code %04X) came without the decided phase: the fighters standing win\n",
            (unsigned)code);
    netplay_on_result(standing, 0, 1u);
}

/* Players stopped answering, or left the room, during this match's fight, before it was decided:
 * a match left that way kept the leaver's record clean, and the one who stayed played on against
 * an idle fighter for nothing (players' reports, 2026-10-24). Reported once, as "left". */
static void report_left(uint32_t left)
{
    int send;
    EnterCriticalSection(&g_cs);
    send = N.lobby && N.match_live && N.match_seen && N.start_reported && !N.reported && !N.desync && left && !N.watching;
    if (send) N.reported = 1;
    LeaveCriticalSection(&g_cs);
    if (!send) return;
    disarm_crash_report();
    fprintf(stderr, "[NETPLAY] left the match before it was decided: players 0x%X; reporting it when signed in\n",
            (unsigned)left);
    report_event("left", 0, left, 0);
}

void netplay_sync_point(void)
{
    if (!N.lockstep) return;
    if (N.lobby && N.match_live && !N.delay_fixed && N.watching) {
        /* Spectator mode: the delay the players agreed, from their P_RTT (byte 13, once their sync
         * point fixed it); players of older builds do not send it: then from their pings, as they
         * derive it. It never rolls back. */
        ULONGLONG t0 = GetTickCount64();
        int i, from = -1;
        for (;;) {
            for (i = 0; i < N.players && from < 0; i++) if (N.peer_delay[i] > 0) from = i;
            if (from >= 0 || GetTickCount64() - t0 > SPEC_DELAY_WAIT_MS || !N.lockstep) break;
            WaitForSingleObject(N.arrived, 20);
        }
        N.delay = from >= 0 ? N.peer_delay[from] : agreed_delay();
        N.rb_match = 0;
        N.delay_fixed = 1;
        fprintf(stderr, "[NETPLAY] input delay %d steps (pings %d/%d/%d/%d ms) (watching: %s)\n", N.delay,
                N.peer_rtt[0], N.peer_rtt[1], N.peer_rtt[2], N.peer_rtt[3],
                from >= 0 ? "the players' agreed delay" : "derived from the players' pings");
    } else if (N.lobby && N.match_live && !N.delay_fixed) {
        /* Every player's ping for this START, then the delay they all derive from them. */
        ULONGLONG t0 = GetTickCount64();
        int known, i;
        for (;;) {
            known = 1;
            for (i = 0; i < N.players; i++) if (N.peer_rtt[i] < 0) known = 0;
            if (known || GetTickCount64() - t0 > RTT_WAIT_MS || !N.lockstep) break;
            WaitForSingleObject(N.arrived, 20);
        }
        N.delay = agreed_delay();
        N.rb_match = every_player_rolls_back();     /* from the same flags as the delay's discount */
        N.delay_fixed = 1;
        send_rtt();
        fprintf(stderr, "[NETPLAY] input delay %d steps (pings %d/%d/%d/%d ms%s)\n", N.delay,
                N.peer_rtt[0], N.peer_rtt[1], N.peer_rtt[2], N.peer_rtt[3], known ? "" : ", some missing");
        if (N.rb_match) {
            fprintf(stderr, "[NETPLAY] rollback on for this match: every player has it on\n");
        } else if (N.koth_match) {
            fprintf(stderr, "[NETPLAY] rollback off for this match: King of the Hill fights play in lockstep\n");
        } else if (match_has_created()) {
            fprintf(stderr, "[NETPLAY] rollback off for this match: a created fighter plays; lockstep\n");
        } else {
            for (i = 0; i < N.players && i < MAXP; i++) {
                char who[40];
                if (N.peer_rb[i]) continue;
                if (i == N.pos) snprintf(who, sizeof(who), "this PC (player %d)", i + 1);
                else if (!netplay_room_name(N.start_slots[i], who, (int)sizeof(who)))
                    snprintf(who, sizeof(who), "player %d", i + 1);
                fprintf(stderr, "[NETPLAY] rollback off for this match: %s has it off%s; lockstep\n", who,
                        N.peer_rtt[i] < 0 ? " (its ping did not arrive)" : "");
            }
        }
    }
    EnterCriticalSection(&g_cs);
    memset(N.seeds, 0, sizeof(N.seeds));
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[NETPLAY] sync point: setting up the room's match\n");
}

void netplay_on_main_menu(void)
{
    if (N.lobby && InterlockedExchange(&N.start_deferred, 0)) {
        /* King of the Hill: back from the last fight's results; the next one, agreed meanwhile */
        N.koth_results = N.koth_quit = 0;
        log_contiguous("at the match end");
        fprintf(stderr, "[NETPLAY] back from the last fight's results: setting up the next King of the Hill fight\n");
        N.start_tick = GetTickCount64();    /* the load starts now */
        become_connected();
        automatch_request(&N.deferred_m);
        return;
    }
    N.spec_results = 0;         /* spectator mode: the results are behind it */
    if (N.lobby && N.match_live && N.match_seen && N.watching) {
        end_hub_match("Back in the room; watching the next match when it starts.");
        N.koth_results = N.koth_quit = 0;
        return;
    }
    if (N.lobby && N.match_live && N.match_seen) {
        report_missed_result();     /* a decision the phase word never showed (a ring out) */
        koth_check_result(1);   /* King of the Hill: the fight's RESULT, a draw if none was recorded */
        end_hub_match("Match over; back in the room.");
    }
    if (N.koth_results)         /* also after the next fight was called off meanwhile */
        fprintf(stderr, "[NETPLAY] back from the last fight's results: on the hill\n");
    N.koth_results = 0;
    N.koth_quit = 0;
}

/* At the sync point, before the match is set up: the room's START came, but a player has left
 * since (the room says so, and the lockstep is off). Then no match is set up at all: the match
 * is over for this PC and it stays in the menu, instead of starting a fight the other side will
 * never join and dropping back to the main menu without a word. */
int netplay_match_abandoned(void)
{
    if (!N.lobby || !N.match_live || N.match_seen || N.lockstep) return 0;
    end_hub_match("The other player left before the fight started.");
    toast(6000, "The other player left before the fight started.");
    fprintf(stderr, "[NETPLAY] a player left before the match was set up; it is not started\n");
    return 1;
}

/* Tests (RECOMP_TEST_KOTH_SCRIPT): the actions of `script` due now. Each is "fighter:N@k",
 * "yield@k" or "leave@k" (while watching the k-th fight this copy is not in), or the same with
 * "@cK" (when called for the K-th time, before readying). Returns 1 when one acted. */
static int koth_script_run(const char *script, int called, int n, int *fighter)
{
    const char *p = script;
    int acted = 0;
    while (p && *p) {
        const char *at = strchr(p, '@'), *next = strchr(p, ',');
        if (at && (!next || at < next) && (at[1] == 'c') == (called != 0) && atoi(at + 1 + (called != 0)) == n) {
            const char *when = called ? "called" : "watching";
            if (!strncmp(p, "fighter:", 8)) {
                *fighter = atoi(p + 8);
                fprintf(stderr, "[NETPLAY] tests: %s (%d); fighter %d from the next fight\n", when, n, *fighter);
                netplay_lobby_ready(0, *fighter);
            } else if (!strncmp(p, "yield", 5)) {
                fprintf(stderr, "[NETPLAY] tests: %s (%d); giving up the spot\n", when, n);
                netplay_lobby_yield();
            } else if (!strncmp(p, "leave", 5)) {
                fprintf(stderr, "[NETPLAY] tests: %s (%d); leaving the hill\n", when, n);
                netplay_lobby_leave();
            }
            acted = 1;
        }
        p = next ? next + 1 : NULL;
    }
    return acted;
}

/* Unattended sessions for tests: RECOMP_NETPLAY=host or join, RECOMP_NETPLAY_SERVER, and
 * RECOMP_NETPLAY_CODE_FILE, where the host writes its room code once it has one and the
 * guest waits for it (up to a minute). RECOMP_NETPLAY=quick enters the hub, quick-matches
 * (RECOMP_NETPLAY_QUICK="mode,players", default "0,2") and readies with RECOMP_NETPLAY_FIGHTER
 * (default 55), voting for the first venue (King of the Hill, RECOMP_NETPLAY_QUICK="65,3": ready when
 * called, RECOMP_NETPLAY_READY_DELAY_MS later; RECOMP_NETPLAY_NAME names the player); =ranked does the same through the ranked queue
 * (the copy must be signed in; RECOMP_ACCOUNT_SERVER for test accounts); =lobby only
 * enters the hub. */
/* RECOMP_TEST_DRAFT=auto (tests): in a Draft set's pick and ban, make a legal choice on each of this
 * player's turns, a moment after the turn starts (the pages show it), as a player would. */
static void test_draft_tick(int you)
{
    static int seen_step = -1, seen_turn = -1, seen_game = -1, seen_bans = -1, seen_struck = -1;
    static ULONGLONG since;
    static unsigned acts;
    const char *on = getenv("RECOMP_TEST_DRAFT");
    const OnlineFighter *all;
    int ids[160], n, i, action, id;
    NetDraft d;
    if (!on || strcmp(on, "auto") || !netplay_lobby_draft(&d) || !netdraft_my_turn(&d, you)) return;
    if (d.step != seen_step || d.turn != seen_turn || d.game != seen_game || d.ban_count != seen_bans ||
        d.struck_count != seen_struck) {
        seen_step = d.step; seen_turn = d.turn; seen_game = d.game; seen_bans = d.ban_count; seen_struck = d.struck_count;
        since = GetTickCount64();
        return;
    }
    if (GetTickCount64() - since < 1500) return;
    n = online_fighters(&all);
    for (i = 0; i < n && i < 160; i++) ids[i] = all[i].id;
    if (!netdraft_auto(&d, you, ids, i, (unsigned)GetTickCount64() * 2654435761u + (unsigned)you + ++acts, &action, &id))
        return;
    fprintf(stderr, "[NETPLAY] tests: draft step %d: %s %d\n", d.step,
            action == NETDRAFT_ACT_BAN ? "ban" : action == NETDRAFT_ACT_STRIKE ? "strike" :
            action == NETDRAFT_ACT_VENUE ? "venue" : "pick", id);
    netplay_lobby_draft_act(d.step, action, id);
    since = GetTickCount64();       /* a lost one goes again after the same wait */
}

static DWORD WINAPI autostart_thread(LPVOID unused)
{
    const char *mode = getenv("RECOMP_NETPLAY"), *server = getenv("RECOMP_NETPLAY_SERVER");
    const char *file = getenv("RECOMP_NETPLAY_CODE_FILE");
    static char chosen[128];
    int i;
    (void)unused;
    /* RECOMP_NETPLAY_SERVER=auto|eu|na|west|all: the server choice as a player makes it (build 30), with
     * RECOMP_NETPLAY_SERVER_EU/_NA/_WEST for the relays' addresses; anything else is the address */
    if (netservers_choice() != NETCHOICE_CUSTOM) {
        server = netservers_address_wait(chosen, sizeof(chosen), 4000);
        fprintf(stderr, "[NETPLAY] tests: server %s\n", server);
    }
    if (!strcmp(mode, "lobby")) {
        netplay_lobby(server, "Test player");
        return 0;
    }
    if (!strcmp(mode, "spectate")) {
        /* RECOMP_NETPLAY=spectate (tests, spectator mode): enter the hub and watch the first listed room
         * that is not a King of the Hill (RECOMP_NETPLAY_SPECTATE=<code>: that room), from the room list
         * as a player does with WATCH; again whenever this PC is in no room (its room closed). */
        const char *name = getenv("RECOMP_NETPLAY_NAME"), *want = getenv("RECOMP_NETPLAY_SPECTATE");
        ULONGLONG asked = 0, tried = 0;
        netplay_lobby(server, name && *name ? name : "Test spectator");
        for (i = 0; i < 600 && N.state == NETPLAY_CONNECTING; i++) Sleep(100);
        for (i = 0; i < 50 && N.relay_caps < 0; i++) Sleep(100);
        if (!(N.relay_caps > 0 && (N.relay_caps & CAPS_SPECTATE))) {
            fprintf(stderr, "[NETPLAY] tests: the relay does not take spectators (caps %d)\n", N.relay_caps);
            return 0;
        }
        while (!N.stop && N.state != NETPLAY_FAILED && N.state != NETPLAY_OFF) {
            NetRoom r;
            ULONGLONG now = GetTickCount64();
            netplay_lobby_room(&r);
            if (!r.in_room && now - tried >= 1000) {
                const char *code = NULL;
                NetRoomInfo list[32];
                int n, k;
                if (now - asked >= 1000) { netplay_lobby_list(0xFF); asked = now; }
                n = netplay_lobby_rooms(list, 32, NULL);
                if (want && *want) code = want;
                for (k = 0; k < n && !code; k++)
                    if (!NETPLAY_IS_KOTH(list[k].mode) && list[k].players > 0) code = list[k].code;
                if (code) {
                    fprintf(stderr, "[NETPLAY] tests: watching room %.6s\n", code);
                    netplay_lobby_spectate(code);
                    tried = now;
                }
            }
            Sleep(100);
        }
        return 0;
    }
    if (!strcmp(mode, "quick") || !strcmp(mode, "ranked")) {
        const char *f = getenv("RECOMP_NETPLAY_FIGHTER"), *name = getenv("RECOMP_NETPLAY_NAME");
        const char *delay = getenv("RECOMP_NETPLAY_READY_DELAY_MS");
        /* RECOMP_TEST_READY_PLAYERS=<n> (tests of three or four copies): ready (or, on the hill, answer
         * a call) only while the room holds n players. The relay starts a room's vote once two are
         * present and every one of them is ready, so the first two copies of a three-copy test started
         * their match before the third joined, and the third opened a room of its own. */
        const char *rp = getenv("RECOMP_TEST_READY_PLAYERS");
        int fighter = f && *f ? atoi(f) : 55, readied = 0, ready_delay = delay ? atoi(delay) : 0;
        int ready_players = rp && *rp ? atoi(rp) : 0, told_wait = 0;
        /* RECOMP_TEST_READY_SPECTATORS=<n> (spectator tests): ready only while n spectators watch the room */
        const char *rs = getenv("RECOMP_TEST_READY_SPECTATORS");
        int ready_spectators = rs && *rs ? atoi(rs) : 0, told_spec = 0;
        unsigned voted_in = 0;
        ULONGLONG called_at = 0;
        netplay_lobby(server, name && *name ? name : "Test player");
        for (i = 0; i < 600 && N.state == NETPLAY_CONNECTING; i++) Sleep(100);
        {   /* RECOMP_TEST_SERVER_SWITCH="<ms>,<eu|na|west|auto>": after that long in the lobby, switch the
             * server the way the hub's server picker does (netservers_reconnect), then quick-match there */
            const char *sw = getenv("RECOMP_TEST_SERVER_SWITCH");
            if (sw && *sw && strchr(sw, ',')) {
                const char *to = strchr(sw, ',') + 1;
                Sleep((DWORD)atoi(sw));
                fprintf(stderr, "[NETPLAY] tests: switching the server to %s\n", to);
                if (!_stricmp(to, "auto")) netservers_set_choice(NETCHOICE_AUTO);
                else netservers_use(netservers_choice_relay(netservers_parse_choice(to)) >= 0 ?
                                    netservers_choice_relay(netservers_parse_choice(to)) : NETSERVER_EU);
                netservers_reconnect(name && *name ? name : "Test player");
                for (i = 0; i < 600 && N.state != NETPLAY_LOBBY && N.state != NETPLAY_FAILED; i++) Sleep(100);
                fprintf(stderr, "[NETPLAY] tests: now on %s\n", N.server_text);
            }
        }
        /* RECOMP_NETPLAY_QUICK="mode,players", default "0,2" (One on One) */
        const char *q = getenv("RECOMP_NETPLAY_QUICK"), *listed = getenv("RECOMP_TEST_JOIN_LISTED");
        const char *who = name && *name ? name : "Test player";
        int qmode = 0, qmax = 2, moves = 0;
        char alone_in[7] = "";
        ULONGLONG alone_since = 0, search_since = 0;
        unsigned scope_seen = 0;
        if (q && *q) sscanf(q, "%d,%d", &qmode, &qmax);
        /* the relay lets only a client that announced King of the Hill into such a room */
        for (i = 0; i < 50 && NETPLAY_IS_KOTH(qmode) && N.relay_caps < 0; i++) Sleep(100);
        /* a created fighter (fighter 99): only once the relay keeps this player's record */
        for (i = 0; i < 100 && fighter == NETPLAY_CREATED_FIGHTER && !netplay_created_available(); i++) Sleep(100);
        if (fighter == NETPLAY_CREATED_FIGHTER)
            fprintf(stderr, "[NETPLAY] tests: the created fighter is %savailable\n", netplay_created_available() ? "" : "NOT ");
        /* RECOMP_TEST_CP_REPICK=<index>: once the relay keeps the first pick, pick another created
         * fighter as the fighter select does, and wait until the relay keeps that one */
        {
            const char *repick = getenv("RECOMP_TEST_CP_REPICK");
            if (repick && *repick && fighter == NETPLAY_CREATED_FIGHTER && netplay_created_available()) {
                uint32_t want = 0, kept = 0;
                int picked = netplay_created_pick(atoi(repick));
                EnterCriticalSection(&g_cs);
                want = cp_record_crc(N.cp_mine);
                LeaveCriticalSection(&g_cs);
                for (i = 0; i < 100 && picked; i++) {
                    EnterCriticalSection(&g_cs);
                    kept = N.cp_acked;
                    LeaveCriticalSection(&g_cs);
                    if (kept == want) break;
                    Sleep(100);
                }
                fprintf(stderr, "[NETPLAY] tests: picked created fighter %s: record %08X, the relay keeps %08X\n",
                        repick, want, kept);
            }
        }
        if (listed && *listed) {
            /* RECOMP_TEST_JOIN_LISTED=<s>: no quick match; join the first open room the room list shows
             * (with RECOMP_NETPLAY_SERVER=all the other relays' rooms too, moving there to join), as a
             * player does from FIND A ROOM. Every change of the list is logged; nothing is joined when
             * no room shows within <s> seconds. */
            NetServerRoom list[32];
            ULONGLONG until = GetTickCount64() + (ULONGLONG)atoi(listed) * 1000, asked = 0;
            int shown = -1, joined = 0;
            while (!N.stop && GetTickCount64() < until && !joined) {
                int n, k;
                if (GetTickCount64() - asked >= 3000) { netplay_lobby_list(0xFF); asked = GetTickCount64(); }
                netservers_fetch_rooms();
                n = netservers_rooms(list, 32, NULL);
                if (n != shown) {
                    shown = n;
                    fprintf(stderr, "[NETPLAY] tests: the room list shows %d room%s", n, n == 1 ? "" : "s");
                    for (k = 0; k < n && k < 6; k++)
                        fprintf(stderr, "%s %s on %s (mode %d, %d/%d, %d ms)", k ? "," : ":", list[k].room.code,
                                netservers_short(list[k].server),
                                list[k].room.mode, list[k].room.players, list[k].room.max, list[k].ping_ms);
                    fprintf(stderr, "\n");
                }
                for (k = 0; k < n; k++) {
                    const NetRoomInfo *x = &list[k].room;
                    if (x->phase != 0 || x->players >= x->max || NETPLAY_IS_KOTH(x->mode)) continue;
                    fprintf(stderr, "[NETPLAY] tests: joining the listed room %s on %s\n", x->code,
                            netservers_short(list[k].server));
                    if (list[k].server == netservers_connected()) netplay_lobby_join(x->code);
                    else netservers_join_on(list[k].server, x->code, x->mode, who);
                    joined = 1;
                    break;
                }
                Sleep(250);
            }
            if (!joined) fprintf(stderr, "[NETPLAY] tests: no open room listed within %s s; joining nothing\n", listed);
        } else if (!strcmp(mode, "ranked")) {
            /* RECOMP_TEST_RANKED_MODES="open" or "classic,open": the modes, the first the primary
             * (default Classic); the found set is accepted (RECOMP_TEST_RANKED_ACCEPT=0 declines) */
            const char *rm = getenv("RECOMP_TEST_RANKED_MODES");
            unsigned mask = 0;
            int primary = -1, k;
            for (k = 0; rm && k < NETRANK_MODES; k++) {
                const char *at = strstr(rm, netranked_mode_id(k));
                if (at) {
                    mask |= 1u << k;
                    if (primary < 0 || at < strstr(rm, netranked_mode_id(primary))) primary = k;
                }
            }
            if (primary < 0) netplay_lobby_ranked(fighter);
            else netplay_lobby_ranked_ex(fighter, mask, primary);
        } else {
            netplay_lobby_quick(qmode, qmax);
        }
        ULONGLONG koth_readied = 0;
        /* RECOMP_TEST_KOTH_SCRIPT="fighter:22@1,yield@c2,leave@3" (tests, koth_script_run): change the
         * fighter (from the next fight), give up the spot or leave the hill while watching a fight or
         * when called, as the hill screens' X, LB and a second LB do */
        const char *kscript = getenv("RECOMP_TEST_KOTH_SCRIPT");
        uint32_t watched_seed = 0;
        int watched = 0, calls = 0, in_call = 0;
        /* a move to another relay ends this session and starts the next: go on */
        while (netservers_moving() || (!N.stop && N.state != NETPLAY_FAILED && N.state != NETPLAY_OFF)) {
            NetRoom r;
            netservers_tick(NULL);
            netplay_lobby_room(&r);
            if (!strcmp(mode, "ranked")) {
                /* the found set: accept it (as the ACCEPT popup's A); a move to the host relay: go */
                if (netplay_lobby_queue(NULL, NULL, NULL, 0) == NETQUEUE_ACCEPT && netplay_lobby_accepted() < 0) {
                    const char *acc = getenv("RECOMP_TEST_RANKED_ACCEPT");
                    Sleep(1500);    /* the popup shows a moment (gallery and screenshots) */
                    netplay_lobby_accept(!(acc && !strcmp(acc, "0")));
                }
                netservers_ranked_move(who);
                if (r.in_room) test_draft_tick(r.you);
            }
            /* The staged search (build 38), as the online screens do it: alone in the quick room, this
             * relay first, then the other North American one from 10 s (Automatic, All), every relay from
             * 30 s; a player waiting there for this mode is met (at most three moves). The search time
             * runs from the first wait alone and is kept across moves. */
            if (!listed && !strcmp(mode, "quick") && r.in_room && r.phase == 0 && r.player_count < 2 &&
                !NETPLAY_IS_KOTH(r.settings.mode) && !netservers_moving() && moves < 3) {
                int waiting = 0, ping = -1, to, search_s, alone_s;
                unsigned scope = 0;
                ULONGLONG now = GetTickCount64();
                if (strcmp(alone_in, r.code)) { snprintf(alone_in, sizeof(alone_in), "%s", r.code); alone_since = now; }
                if (!search_since) search_since = now;
                search_s = (int)((now - search_since) / 1000);
                alone_s = (int)((now - alone_since) / 1000);
                to = netservers_quick_move(qmode, search_s, alone_s, r.code, &waiting, &ping, &scope);
                if (scope && scope != scope_seen) {
                    int k;
                    fprintf(stderr, "[NETPLAY] tests: quick match searching on");
                    for (k = 0; k < NETSERVER_COUNT; k++) if (scope >> k & 1u) fprintf(stderr, " %s", netservers_short(k));
                    fprintf(stderr, " after %d s\n", search_s);
                    scope_seen = scope;
                }
                if (to >= 0) {
                    moves++;
                    toast(8000, "Nobody here yet: moving to %s (%d ms), where %d player%s waiting.",
                          netservers_name(to), ping, waiting, waiting == 1 ? " is" : "s are");
                    fprintf(stderr, "[NETPLAY] tests: moving the quick match to %s after %d s\n", netservers_short(to), search_s);
                    alone_in[0] = 0;
                    scope_seen = 0;
                    netservers_quick_on(to, qmode, qmax, who);
                    continue;
                }
            } else if (r.in_room && r.player_count >= 2) {
                alone_in[0] = 0;
                search_since = 0;           /* met: a later wait alone starts over */
            } else if (!r.in_room) {
                alone_in[0] = 0;
            }
            if (r.in_room && ready_players > 0 && r.player_count < ready_players && !N.match_live) {
                if (!told_wait) {
                    fprintf(stderr, "[NETPLAY] tests: %d of %d players in the room; not ready until all are\n",
                            r.player_count, ready_players);
                    told_wait = 1;
                }
                Sleep(100);
                continue;
            }
            if (r.in_room && ready_spectators > 0 && r.spectator_count < ready_spectators && !N.match_live &&
                !NETPLAY_IS_KOTH(r.settings.mode)) {
                if (!told_spec) {
                    fprintf(stderr, "[NETPLAY] tests: %d of %d spectators in the room; not ready until all are\n",
                            r.spectator_count, ready_spectators);
                    told_spec = 1;
                }
                Sleep(100);
                continue;
            }
            if (told_spec) {
                fprintf(stderr, "[NETPLAY] tests: %d spectators watch the room; readying\n", r.spectator_count);
                told_spec = 0;
            }
            if (r.in_room && r.player_count >= ready_players && told_wait) {
                fprintf(stderr, "[NETPLAY] tests: the room holds %d players; readying\n", r.player_count);
                told_wait = 0;
            }
            if (r.in_room && NETPLAY_IS_KOTH(r.settings.mode)) {
                /* King of the Hill: ready when called (back from the last fight), after
                 * RECOMP_NETPLAY_READY_DELAY_MS (tests: the "YOU'RE UP" screen shows meanwhile);
                 * vote when fighting */
                int me, state = -1;
                for (me = 0; me < r.hill.count; me++)
                    if (r.hill.line[me].slot == r.you) state = r.hill.line[me].state;
                if (state != NETLINE_CALLED || N.match_live) called_at = 0;
                else if (!called_at) called_at = GetTickCount64();
                if (kscript && *kscript && r.hill.valid && r.hill.phase == NETHILL_FIGHTING && state >= 0 &&
                    state != NETLINE_FIGHTING && r.hill.seed && r.hill.seed != watched_seed) {
                    watched_seed = r.hill.seed;
                    koth_script_run(kscript, 0, ++watched, &fighter);
                }
                {
                    int now_called = r.hill.valid && r.hill.phase == NETHILL_CALLING &&
                                     (state == NETLINE_CALLED || state == NETLINE_READY) && !N.match_live;
                    if (now_called && !in_call && kscript && *kscript && koth_script_run(kscript, 1, ++calls, &fighter)) {
                        in_call = 1;
                        Sleep(300);
                        continue;
                    }
                    in_call = now_called;
                }
                if (r.hill.valid && r.hill.phase == NETHILL_CALLING && state == NETLINE_CALLED && !N.match_live &&
                    GetTickCount64() - called_at >= (ULONGLONG)ready_delay && GetTickCount64() - koth_readied > 1000) {
                    fprintf(stderr, "[NETPLAY] tests: called on the hill; ready with fighter %d\n", fighter);
                    netplay_lobby_ready(1, fighter);
                    koth_readied = GetTickCount64();
                }
                if (r.phase == 1 && state == NETLINE_FIGHTING && voted_in != r.version) {
                    for (me = 0; me < r.player_count; me++)
                        if (r.players[me].slot == r.you && r.players[me].voted < 0) { netplay_lobby_vote(0); break; }
                    voted_in = r.version;
                }
                Sleep(100);
                continue;
            }
            {   /* RECOMP_NETPLAY_TEST_POOL="4,6" (tests): the host makes its room vote on these venues */
                const char *tp = getenv("RECOMP_NETPLAY_TEST_POOL");
                if (tp && *tp && r.in_room && r.phase == 0 && r.you == r.host && !N.match_live) {
                    NetRoomSettings want = r.settings;
                    const char *c = tp;
                    want.pool_count = 0;
                    while (*c && want.pool_count < 32) {
                        int v = atoi(c);
                        if (v > 0 && v < 256) want.pool[want.pool_count++] = (unsigned char)v;
                        while (*c && *c != ',') c++;
                        if (*c == ',') c++;
                    }
                    if (want.pool_count && (want.pool_count != r.settings.pool_count ||
                                            memcmp(want.pool, r.settings.pool, (size_t)want.pool_count))) {
                        static ULONGLONG asked;
                        if (GetTickCount64() - asked > 1000) {
                            asked = GetTickCount64();
                            fprintf(stderr, "[NETPLAY] tests: the room votes on venues %s\n", tp);
                            netplay_lobby_settings(&want);
                        }
                        Sleep(100);
                        continue;
                    }
                }
            }
            if (r.in_room && r.phase == 0 && !N.match_live) {
                /* Ready again whenever this player shows as not ready: after a
                   match, or after a scripted press toggled it in the overlay. */
                int me, mine = !readied;
                for (me = 0; me < r.player_count; me++)
                    if (r.players[me].slot == r.you) { mine = !r.players[me].ready; break; }
                if (mine && !readied) {
                    netplay_lobby_ready(1, fighter);
                    readied = 1;
                } else if (mine && readied) {
                    readied = 0;    /* the server shows us not ready: send it again next pass */
                }
            }
            if (r.in_room && r.phase == 1 && voted_in != r.version) {
                int me;
                for (me = 0; me < r.player_count; me++)
                    if (r.players[me].slot == r.you && r.players[me].voted < 0) { netplay_lobby_vote(0); break; }
                voted_in = r.version;
            }
            if (r.phase == 2) readied = 0;
            Sleep(100);
        }
        return 0;
    }
    if (!strcmp(mode, "host")) {
        netplay_host(server, "Test host", 3);
        for (i = 0; i < 600 && !N.room[0] && N.state != NETPLAY_FAILED; i++) Sleep(100);
        if (N.room[0] && file && *file) {
            FILE *f = fopen(file, "w");
            if (f) { fputs(N.room, f); fclose(f); }
        }
    } else {
        char code[16] = "";
        for (i = 0; i < 600 && file && *file; i++) {
            FILE *f = fopen(file, "r");
            if (f) {
                if (fgets(code, sizeof(code), f) && strlen(code) >= 6) { fclose(f); break; }
                fclose(f);
            }
            Sleep(100);
        }
        code[6] = 0;
        netplay_join(server, "Test guest", code);
    }
    return 0;
}

void netplay_autostart(void)
{
    const char *mode = getenv("RECOMP_NETPLAY"), *server = getenv("RECOMP_NETPLAY_SERVER");
    static LONG once;
    HANDLE t;
    if (InterlockedExchange(&once, 1)) return;
    if (!mode || !server || (strcmp(mode, "host") && strcmp(mode, "join") && strcmp(mode, "quick") && strcmp(mode, "ranked") && strcmp(mode, "lobby") &&
                               strcmp(mode, "spectate"))) return;
    t = CreateThread(NULL, 0, autostart_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

int netplay_active(void) { return N.lockstep != 0; }
int netplay_in_fight(void) { return MEM32(G_MATCH_PHASE) == 1u; }
int netplay_match_locked(void) { return N.lobby && N.match_live; }

int netplay_voice_room(void)
{
    return N.lobby && N.have_token && (N.roomv.in_room || N.match_live) &&
           (N.state == NETPLAY_LOBBY || N.state == NETPLAY_CONNECTED);
}

void netplay_send_voice(const uint8_t *payload, int len)
{
    if (netplay_voice_room() && !netplay_spectating()) send_data(payload, len);    /* a spectator only listens */
}

int netplay_room_name(int slot, char *out, int size)
{
    int i, found = 0;
    if (size <= 0) return 0;
    out[0] = 0;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < N.roomv.player_count; i++)
        if (N.roomv.players[i].slot == slot) {
            snprintf(out, (size_t)size, "%s", N.roomv.players[i].name);
            found = out[0] != 0;
        }
    LeaveCriticalSection(&g_cs);
    return found;
}

/* ---- the game's hooks ------------------------------------------------------------ */

#define AI_GENS       0x003BFC70u   /* the CPU fighters' four generators, 0x88 bytes each (docs/research/fight-determinism.md) */
#define SHARED_GEN    0x0039C0E0u   /* the utility generator menus, camera and effects draw too */
#define TIME_SCALE    0x00349764u   /* CLogic+4, the step's scale (slow motion) */
#define MATCH_GEN     0x003C1400u   /* the match generator the fight handlers draw (0x88 bytes, state at +4) */

/* The checksum of the state a step starts from, its parts, and the raw fighter words it covers
 * (netcheck_field: action, position x/y/z, facing, health). */
static uint32_t fighter_hash(uint32_t parts[HASH_PARTS], uint32_t raw[NETCHECK_SLOTS][NETCHECK_FIELDS],
                             uint32_t split[3])
{
    uint32_t h = 2166136261u, a;
    int slot;
    unsigned i;
    memset(parts, 0, sizeof(uint32_t) * HASH_PARTS);
    memset(raw, 0, sizeof(uint32_t) * NETCHECK_SLOTS * NETCHECK_FIELDS);
    for (slot = 0; slot < (N.fighters > N.players ? N.fighters : N.players); slot++) {
        uint32_t one = 2166136261u;
        for (i = 0; i < NETCHECK_FIELDS; i++) {
            uint32_t v = MEM32(ACTORS + (uint32_t)slot * STRIDE + netcheck_field[i]);
            h = (h ^ v) * 16777619u;
            one = (one ^ v) * 16777619u;
            if (slot < NETCHECK_SLOTS) raw[slot][i] = v;
        }
        if (slot < 4) parts[slot] = one;
    }
    parts[4] = parts[5] = 2166136261u;
    for (a = 0; a < 4u * 0x88u; a += 4) parts[4] = (parts[4] ^ MEM32(AI_GENS + a)) * 16777619u;
    for (a = 0; a < 0x88u; a += 4) parts[5] = (parts[5] ^ MEM32(SHARED_GEN + a)) * 16777619u;
    split[0] = parts[5];                /* the shared generator alone (P_HASH extras, build 35 on) */
    parts[5] = (parts[5] ^ MEM32(TIME_SCALE)) * 16777619u;     /* part 5 as builds 23..34 send it */
    split[1] = MEM32(TIME_SCALE);       /* the word itself: the report prints both values */
    split[2] = 2166136261u;
    for (a = 0; a < 0x88u; a += 4) split[2] = (split[2] ^ MEM32(MATCH_GEN + a)) * 16777619u;
    return h;
}

/* The checksums of steps up to `upto` (the state each one started from), once they are final. */
static void send_hashes(uint32_t upto)
{
    uint32_t t;
    for (t = N.hash_sent + 1; t <= upto; t++) {
        uint8_t p[NETCHECK_LEN_FULL];
        NetCheck c;
        uint32_t k = t % RING;
        if (t % HASH_EVERY || N.hash_tag[k] != t + 1) continue;
        memset(&c, 0, sizeof(c));
        c.match = N.match; c.step = t; c.hash = N.hash_at[k];
        memcpy(c.parts, N.hash_parts[k], sizeof(c.parts));
        memcpy(c.fields, N.hash_fields[k], sizeof(c.fields));
        memcpy(c.gens, N.hash_gens[k], sizeof(c.gens));
        c.crowd = N.hash_crowd[k];
        c.shared = N.hash_split[k][0]; c.time_scale = N.hash_split[k][1]; c.match_gen = N.hash_split[k][2];
        c.has_parts = c.has_extra = 1;
        EnterCriticalSection(&g_cs);
        memmove(&N.local_hash[1], &N.local_hash[0], sizeof(N.local_hash[0]) * 15);
        N.local_hash[0] = c;
        check_hashes();
        LeaveCriticalSection(&g_cs);
        send_data(p, netcheck_pack(p, (int)sizeof(p), P_HASH, &c));     /* older builds read the first 37 bytes */
    }
    if (upto > N.hash_sent) N.hash_sent = upto;
}

/* RECOMP_ROLLBACK_SOAK=<depth> (tests, offline): every few updates of a fight the game goes back
 * <depth> steps (2..12) and runs them again, and the per-step fingerprints of the second run
 * (test_telemetry.c) are compared with the first. A difference names the first step that came out
 * otherwise: what the save states miss. */
static int soak_depth(void)
{
    static int d = -1;
    if (d < 0) {
        const char *v = getenv("RECOMP_ROLLBACK_SOAK");
        d = v ? atoi(v) : 0;
        if (d && (d < 2 || d > 12)) d = 6;
    }
    return d;
}

static void rollback_crash_state(void);

/* One line per match with rollback: its own counts, not the session's (B7 of rollback-plan-2026-11.md). */
static void rbm_summary(const char *why)
{
    if (!N.rbm.on || N.rbm.told) return;
    N.rbm.told = 1;
    fprintf(stderr, "[ROLLBACK] match %u %s: %u rollbacks, %u steps run again, deepest %u, %.2f ms average, %.2f ms longest, "
            "%u slow; first prediction %s%u\n", N.rbm.num, why, N.rbm.count, N.rbm.steps, N.rbm.deepest,
            N.rbm.count ? N.rbm.ms_total / N.rbm.count : 0.0, N.rbm.ms_max, N.rbm.slow,
            N.rbm.first_pred ? "at step " : "never", N.rbm.first_pred);
}

static void rbm_reset(void)
{
    rbm_summary("ended with rollback still on (the next match started)");
    memset(&N.rbm, 0, sizeof(N.rbm));
}

static void soak_match_start(void)
{
    rbm_reset();
    N.step = 0;
    N.final_step = N.hash_sent = N.updates = 0;
    N.rb_on = N.rb_ref_due = N.resim = N.predicting = 0;
    memset(N.snap, 0, sizeof(N.snap));
    rollback_reset();
    fprintf(stderr, "[ROLLBACK] soak: going back %d steps every few updates\n", soak_depth());
}

void netplay_on_match_start(void)
{
    if (!N.lockstep) { if (soak_depth()) soak_match_start(); return; }
    rbm_reset();        /* the last match's line, if it never said "off for the rest" (B3, G2) */
    EnterCriticalSection(&g_cs);
    N.match_count++;
    N.match = mix32(N.session ^ (N.match_count * 0x9E3779B9u));   /* the same on every PC, new per match */
    N.step = 0;
    /* every match of a START is reported on its own (the results screen's rematch runs a
     * second match without a new START): the key gets the match's number */
    if (N.report_key[0]) {
        char *dash = strchr(N.report_key, '-');
        if (dash && (dash = strchr(dash + 1, '-')) != NULL) *dash = 0;
        snprintf(N.report_key + strlen(N.report_key), sizeof(N.report_key) - strlen(N.report_key), "-%u",
                 (unsigned)N.match_count);
    }
    N.reported = N.fighting_seen = N.start_reported = 0;
    disarm_crash_report();
    memset(N.latest_local, 0, sizeof(N.latest_local));
    N.desync = 0;
    if (N.match_live) N.match_seen = 1;
    N.final_step = N.hash_sent = N.updates = 0;
    N.rb_on = N.rb_ref_due = N.resim = N.predicting = N.rb_fought = 0;
    N.rb_decided_step = 0;
    memset(N.used_tag, 0, sizeof(N.used_tag));
    memset(N.hash_tag, 0, sizeof(N.hash_tag));
    memset(N.snap, 0, sizeof(N.snap));
    LeaveCriticalSection(&g_cs);
    rollback_reset();
    /* From the load on, not from the first step: a START pressed while the venue loads or the
     * fighters' intro runs (players press it to skip the intro) paused that game before its
     * first step, and the others waited for it until they gave up (seen on The Pit, whose load is
     * the longest). */
    fight_input();
    N.load_done = 1;
    fprintf(stderr, "[NETPLAY] match %08X starts in lockstep (%d players, delay %d); loaded in %.1f s since the START%s\n",
            N.match, N.players, N.delay, N.start_tick ? (GetTickCount64() - N.start_tick) / 1000.0 : -1.0,
            N.watching ? " (watching)" : "");
    if (N.lobby && N.match_live && N.report_key[0] && !N.watching) {
        N.start_reported = 1;
        ac_match_begin();
        report_event("start", 0, 0, 0);
        arm_crash_report();
    }
    log_contiguous("at the match start");
}

void netplay_on_pad_pass(void) {}

static void send_steps(uint32_t last)
{
    uint8_t p[1 + 9 + REDUNDANT * ENTRY];
    uint32_t first = last >= REDUNDANT ? last - REDUNDANT + 1 : 1, s;
    int count = 0, local = N.pos;
    p[0] = P_STEPS;
    put32(p + 1, N.match);
    put32(p + 5, last);
    EnterCriticalSection(&g_cs);
    for (s = first; s <= last; s++) {
        uint32_t k = s % RING;
        if (N.in_tag[local][k] == s + 1)
            memcpy(p + 10 + count * ENTRY, N.in[local][k], ENTRY);
        else
            memset(p + 10 + count * ENTRY, 0, ENTRY);
        count++;
    }
    LeaveCriticalSection(&g_cs);
    p[9] = (uint8_t)count;
    send_data(p, 10 + count * ENTRY);
}

/* ---- the crowd, the same on every PC ------------------------------------------------------------
 *
 * The arena's spectators start animating while the match loads, at host timing, and one of the
 * generators they draw from (0x33DB5C) is seeded from the wall clock at boot, so each PC's crowd
 * leaves the loading window in a state of its own. Through the crowd's record table that reaches
 * the fight as catches, holds and weapon offers, which parted the lockstep. At the first fight step
 * every PC therefore restarts the crowd the way the game does at a match start on a loaded arena
 * (sub_00080880), from shared seeds, and evens out what that restart leaves alone. From then on the
 * crowd updates once a step on every PC. docs/research/crowd-lockstep-reset.md (recipe a, G1). */
extern void sub_000AA0D0(void);     /* generator seed: ecx = generator, two words */
extern void sub_0002A720(void);     /* the global generator 0x33DB5C from one word */
extern void sub_00080880(void);     /* the crowd's match-start restart: ecx = manager */
extern void sub_0007F030(void);     /* publishes the spectators' records: ecx = manager */
extern void sub_0007D240(void);     /* resets one member's state: ecx = member */
extern void sub_0007F3D0(void);     /* crowd intensity: ecx = manager, one float */
extern void sub_0007F6E0(void);     /* deals the next idle state by quota: ecx = manager, returns it */
extern void sub_0007D2F0(void);     /* starts a member's clip: ecx = member, state, -1 */
extern void sub_0007D560(void);     /* samples a member's pose: ecx = member, 0 */

#define CROWD_VTABLE    0x0029EBE8u
#define GLOBAL_GEN      0x0033DB5Cu     /* 6 words; the crowd and other, host-timed users draw it */

/* The crowd's own copy of the global generator for the match (crowd_reset makes it from the match
 * seed); netplay_crowd_update swaps it in around every crowd update, so draws elsewhere at host
 * timing cannot reach the crowd. */
static uint32_t g_crowd_gen[6];
static int g_crowd_gen_on;
#define CROWD_MANAGER   0x0002B9F0u     /* the manager inside the arena object */

static void guest_call(void (*fn)(void), uint32_t ecx, int nargs, const uint32_t *args)
{
    uint32_t sp = g_esp;
    int i;
    g_esp = (sp - 0x80u) & ~3u;         /* below the hook's frame */
    for (i = nargs - 1; i >= 0; i--) PUSH32(g_esp, args[i]);
    PUSH32(g_esp, 0u);                  /* a return address the lifted ret only pops */
    g_ecx = ecx;
    fn();
    g_esp = sp;                         /* whatever the callee popped */
}

/* ---- created fighters' skeletons, posed in the step -----------------------------------------------
 *
 * A created fighter's model (the pad's CP-Fighter object, vtable 0x298B18) has +0xABB4 bit 0 set and
 * poses the skeleton at *(+0x9D0) through its own animation controller at +0x8B0. That controller's
 * pose pass (sub_00097F90, a virtual method: the animation sample sub_000230A0, the world matrices
 * sub_00024070, the attachments sub_000252A0) runs only from the render traversal, once per drawn
 * frame, and the fight step reads the joints (+0xDC.. of the fighter record, sub_001A7610 ->
 * sub_000445A0 -> sub_0009EC10) from whatever pose it left. So the joints a step saw depended on
 * how many frames each PC drew and when (measured with one copy unpaced: created fighters desynced
 * 4 of 4 matches, stock fighters 0 of 4; docs/research/created-fighter-online.md, section 7). In a
 * lockstep fight every PC now runs that pass for each created fighter at the start of every step,
 * so the step reads a pose made from the simulation alone. The render's own passes still run (the
 * model is drawn from them; skipping them hid the fighters), and the measurement says they leave
 * nothing the next step's pass does not redo. RECOMP_NETPLAY_CP_EVAL=game (tests): as the game. */
extern void sub_00097F90(void);
#define CP_MODEL_VTABLE 0x00298B18u
#define CP_ALT_CONTROL  0x8B0u
#define CP_ALT_FLAGS    0xABB4u
static uint32_t g_cp_ctrl[4];
static int g_cp_ctrls;
static uint32_t g_cp_ctrl_match;

static int cp_eval_in_step(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_NETPLAY_CP_EVAL");
        on = !(e && !strcmp(e, "game"));
    }
    return on;
}

static void cp_collect(void)
{
    int pad;
    g_cp_ctrls = 0;
    for (pad = 0; pad < 4; pad++) {
        uint32_t obj = created_fighter_model(pad);
        if (obj && MEM32(obj) == CP_MODEL_VTABLE && (MEM32(obj + CP_ALT_FLAGS) & 1u))
            g_cp_ctrl[g_cp_ctrls++] = obj + CP_ALT_CONTROL;
    }
    g_cp_ctrl_match = N.match;
    if (g_cp_ctrls)
        fprintf(stderr, "[NETPLAY] created fighters: %d skeleton(s) posed in the step (match %08X)\n", g_cp_ctrls, N.match);
}

/* At the start of every step of a lockstep fight: each created fighter's pose pass, once. */
static void cp_pose_in_step(void)
{
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp, esp = g_esp;
    double fp[8];
    int fp_top = g_fp_top, i;
    uint16_t fp_cw = g_fp_control_word;
    if (!N.lockstep || N.resim || !cp_eval_in_step()) return;
    if (g_cp_ctrl_match != N.match) cp_collect();
    if (!g_cp_ctrls) return;
    memcpy(fp, g_fp_stack, sizeof(fp));
    for (i = 0; i < g_cp_ctrls; i++) guest_call(sub_00097F90, g_cp_ctrl[i], 0, NULL);
    g_eax = eax; g_ecx = ecx; g_edx = edx; g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp; g_esp = esp;
    memcpy(g_fp_stack, fp, sizeof(fp));
    g_fp_top = fp_top;
    g_fp_control_word = fp_cw;
}

/* The crowd's update (vtable+8 of the crowd manager, called only indirectly by the arena), as
 * recomp_lookup_manual hands it out: in a lockstep fight it draws from the crowd's copy of the
 * global generator. */
extern void sub_00081170(void);
void netplay_crowd_update(void)
{
    uint32_t real[6], i;
    if (!N.lockstep || !g_crowd_gen_on) { sub_00081170(); return; }
    for (i = 0; i < 6; i++) { real[i] = MEM32(GLOBAL_GEN + 4u * i); MEM32(GLOBAL_GEN + 4u * i) = g_crowd_gen[i]; }
    sub_00081170();
    for (i = 0; i < 6; i++) { g_crowd_gen[i] = MEM32(GLOBAL_GEN + 4u * i); MEM32(GLOBAL_GEN + 4u * i) = real[i]; }
}

/* The spectators' draw lists. Each member's animation object (member + 0xE0) is "shown" by
 * sub_00084110, which appends it, without looking, to two arrays: the draw list of the crowd
 * renderer at [A+4] (entries +0x130, count +0x220; drawn by sub_00083BD0) and the list at
 * [A+0x10] + 0x510 (entries +0x440, count +0x530; sub_000838E0). "Hide" (sub_00084140) takes it out
 * again. The game pairs them: the match-end cleanup hides every member (sub_00086F40 ->
 * sub_0007E710) before a light restart shows them again (sub_000891A0 -> sub_00080880 ->
 * sub_0007D240). The restart below runs on a crowd that is shown, so without this every spectator
 * was listed, and drawn, once more per match (2-3 identical draws per frame online: faded
 * spectators near the camera looked solid, alpha 0.2 drawn twice is 0.36, and the GPU did the
 * work twice; F9 reports 81db9e, a91ed1, 04723a, 83a66d, 2026-10-28). The lists are render
 * bookkeeping only (read by the renderers at draw time, not by the crowd's update), so taking the
 * members out before the restart puts them back exactly once and leaves the simulation alone. */
#define CROWD_DRAW_CAP  60u     /* entries of either array (0x130..0x220, 0x440..0x530) */
static int crowd_list_ok(uint32_t a) { return a >= 0x80000000u && a < 0x84000000u; }

/* Takes every listed spectator out of (drop != 0), or counts them and their repeats in, both lists. */
static void crowd_draw_lists(uint32_t mgr, uint32_t n, int drop, unsigned *entries, unsigned *repeats)
{
    uint32_t owners[2][16], i, j, k;
    unsigned nown[2] = { 0, 0 };
    static const uint32_t base[2] = { 0x130u, 0x440u }, cnt[2] = { 0x220u, 0x530u };
    if (entries) *entries = 0;
    if (repeats) *repeats = 0;
    for (i = 0; i < n; i++) {           /* the lists the members are on (one renderer per model kind) */
        uint32_t m = MEM32(mgr + 0xA90u + 4u * i), a, o[2];
        int t;
        if (!crowd_list_ok(m)) continue;
        a = m + 0xE0u;
        o[0] = MEM32(a + 4u);
        o[1] = MEM32(a + 0x10u) + 0x510u;
        for (t = 0; t < 2; t++) {
            if (!crowd_list_ok(o[t]) || MEM32(o[t] + cnt[t]) > CROWD_DRAW_CAP) continue;
            for (k = 0; k < nown[t] && owners[t][k] != o[t]; k++) {}
            if (k == nown[t] && nown[t] < 16) owners[t][nown[t]++] = o[t];
        }
    }
    for (j = 0; j < 2; j++)
        for (k = 0; k < nown[j]; k++) {
            uint32_t o = owners[j][k], c = MEM32(o + cnt[j]), out = 0, e, u;
            for (e = 0; e < c; e++) {
                uint32_t v = MEM32(o + base[j] + 4u * e);
                int member = 0, seen = 0;
                for (i = 0; i < n && !member; i++) member = MEM32(mgr + 0xA90u + 4u * i) + 0xE0u == v;
                for (u = 0; u < e && !seen; u++) seen = MEM32(o + base[j] + 4u * u) == v;
                if (entries && member) (*entries)++;
                if (repeats && member && seen) (*repeats)++;
                if (drop && member) continue;               /* the order of the rest is kept */
                MEM32(o + base[j] + 4u * out) = v;
                out++;
            }
            if (drop) MEM32(o + cnt[j]) = out;
        }
}

static void crowd_reset(uint32_t arena)
{
    uint32_t mgr = arena + CROWD_MANAGER, xml, n, i, args[2];
    unsigned listed = 0, repeats = 0, listed_after = 0, repeats_after = 0;
    int with_text;
    static int crowd_dedupe = -1;   /* RECOMP_CROWD_LISTS=keep (tests only): the old repeated listing */
    if (crowd_dedupe < 0) crowd_dedupe = !(getenv("RECOMP_CROWD_LISTS") && !strcmp(getenv("RECOMP_CROWD_LISTS"), "keep"));
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp, esp = g_esp;
    double fp[8];
    int fp_top = g_fp_top;
    uint16_t fp_cw = g_fp_control_word;
    if (MEM32(mgr) != CROWD_VTABLE) {
        fprintf(stderr, "[NETPLAY] crowd: no crowd manager at %08X (vtable %08X); left as it is\n", mgr, MEM32(mgr));
        return;
    }
    n = MEM32(mgr + 0x18u);
    xml = MEM32(mgr + 0x24u);
    if (!n || n > 256 || !MEM32(mgr + 0xA90u) || (MEM32(mgr + 0x20u) & 1u)) {
        fprintf(stderr, "[NETPLAY] crowd: not in a state to restart (members %u, flags %08X); left as it is\n",
                n, MEM32(mgr + 0x20u));
        return;
    }
    /* The game's restart re-reads the arena's crowd text; by the first fight step that text can be
     * gone, and then the same restart is done field by field (recipe b). */
    with_text = xml && MEM32(xml + 0xA4u) && (int32_t)MEM32(xml + 0xA8u) > 0;
    memcpy(fp, g_fp_stack, sizeof(fp));
    /* 1. the crowd's own generator, and the global one, from the match's seed */
    args[0] = mix32(N.session ^ 0xC0A7D001u);
    args[1] = mix32(N.session ^ 0x5EED0002u) | 1u;
    N.seed_bypass = 1;
    guest_call(sub_000AA0D0, mgr + 0xE18u, 2, args);
    N.seed_bypass = 0;
    {   /* the crowd's own copy of the global generator, made by the game's own seeding routine */
        uint32_t real[6];
        for (i = 0; i < 6; i++) real[i] = MEM32(GLOBAL_GEN + 4u * i);
        args[0] = mix32(N.session ^ 0x0033DB5Cu);
        guest_call(sub_0002A720, 0, 1, args);
        for (i = 0; i < 6; i++) { g_crowd_gen[i] = MEM32(GLOBAL_GEN + 4u * i); MEM32(GLOBAL_GEN + 4u * i) = real[i]; }
        g_crowd_gen_on = 1;
    }
    /* 2. the game's own restart: states, clip times, variants, offers, intensity. It shows every
     * member again, so they leave the draw lists first (the game hides them at a match's end). */
    crowd_draw_lists(mgr, n, 0, &listed, &repeats);
    if (crowd_dedupe) crowd_draw_lists(mgr, n, 1, NULL, NULL);
    if (with_text) {
        guest_call(sub_00080880, mgr, 0, NULL);
    } else {
        uint32_t recs = MEM32(0x003494E4u);
        for (i = 0; i < n; i++) {
            uint32_t m = MEM32(mgr + 0xA90u + 4u * i), idx;
            if (!m) continue;
            guest_call(sub_0007D240, m, 0, NULL);
            MEM32(m + 0xC8u) = 0xFFFFFFFFu;
            MEM32(m + 0x10u) = MEM32(m);
            idx = MEM32(m + 0x0Cu);
            if (recs && idx != 0xFFFFFFFFu && idx < MEM32(0x003494E0u)) MEM32(recs + idx * 0x34u + 0x30u) = 0xFFFFFFFFu;
        }
        MEM32(mgr + 0xC98u) = 0;
        MEM32(mgr + 0xCD0u) = 0;
        MEM32(mgr + 0xCCCu) = 0xFFFFFFFFu;
        MEM32(mgr + 0xCD4u) = 0;
        MEM32(mgr + 0xC20u) = MEM32(mgr + 0xC24u) = 0x42700000u;     /* 60.0 */
        MEM32(mgr + 0xC2Cu) = 0xBF800000u;                           /* -1.0 */
        MEM32(mgr + 0xC34u) = 0;
        args[0] = 0;                                                  /* 0.0f */
        guest_call(sub_0007F3D0, mgr, 1, args);
        for (i = 0; i < n; i++) {                                     /* member order, as the game deals */
            uint32_t m = MEM32(mgr + 0xA90u + 4u * i), state;
            if (!m || MEM32(m) == 0xCu) continue;
            guest_call(sub_0007F6E0, mgr, 0, NULL);
            state = g_eax;
            args[0] = state;
            args[1] = 0xFFFFFFFFu;
            guest_call(sub_0007D2F0, m, 2, args);
            args[0] = 0;
            guest_call(sub_0007D560, m, 1, args);
        }
    }
    /* 3. what it leaves: the update rotation, each spectator's speed and two timers */
    MEM32(mgr + 0x1Cu) = 0;
    for (i = 0; i < n; i++) {
        uint32_t m = MEM32(mgr + 0xA90u + 4u * i), bits;
        float speed;
        if (!m) continue;
        speed = MEM32(m) == 0xDu ? 0.75f : (float)((int)(mix32(N.session ^ (0xC20D0000u + i)) % 60u) - 30) * 0.01f + 1.0f;
        memcpy(&bits, &speed, 4);
        MEM32(m + 0x68u) = bits;
        MEM8(m + 0xDCu) = 0;
        MEM32(m + 0xD8u) = 0;
    }
    /* 4. the records the fighters read, before the step's first update */
    guest_call(sub_0007F030, mgr, 0, NULL);
    crowd_draw_lists(mgr, n, 0, &listed_after, &repeats_after);
    fprintf(stderr, "[NETPLAY] crowd draw lists: %u spectator entries (%u repeated) before the restart, %u (%u repeated) "
            "after%s\n", listed, repeats, listed_after, repeats_after, crowd_dedupe ? "" : " (RECOMP_CROWD_LISTS=keep)");
    g_eax = eax; g_ecx = ecx; g_edx = edx; g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp; g_esp = esp;
    memcpy(g_fp_stack, fp, sizeof(fp));
    g_fp_top = fp_top;
    g_fp_control_word = fp_cw;
    fprintf(stderr, "[NETPLAY] crowd restarted for the match (%u spectators, %s)\n", n,
            with_text ? "the game's restart" : "field by field");
}

/* ---- the shared generator, pinned to the step ---------------------------------------------------
 *
 * The object-form generator at 0x39C0E0 is drawn inside the fight step (the camera director) and
 * outside it at host timing: the music/ambience director when an audio voice is idle, the screen
 * transition machine, a render-path effect, animation creation and CArena's update. On The Terror
 * Dome, where it is drawn about once a second, two PCs came out of the loading window three draws
 * apart and the fighters parted by step 31 (docs/research/generator-timescale-desync.md, the lab
 * case). So at the start of every online step, and of every step rollback runs again, every PC
 * seeds it the same way through the game's own seed routine (this = generator, state, odd
 * increment; it writes the 32-float table and the two words, nothing else): whatever was drawn
 * outside the step is gone before the step can read it. */
/* RECOMP_NETPLAY_GEN_PIN=off (tests only): the generator as the game leaves it, to compare */
static int shared_gen_pin(void)
{
    static int on = -1;
    if (on < 0) on = !(getenv("RECOMP_NETPLAY_GEN_PIN") && !strcmp(getenv("RECOMP_NETPLAY_GEN_PIN"), "off"));
    return on;
}
static void shared_gen_reseed(uint32_t step)
{
    uint32_t args[2];
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp, esp = g_esp;
    double fp[8];
    int fp_top = g_fp_top;
    uint16_t fp_cw = g_fp_control_word;
    memcpy(fp, g_fp_stack, sizeof(fp));
    args[0] = mix32(N.match ^ mix32(step * 0x9E3779B9u ^ SHARED_GEN));
    args[1] = mix32(N.match ^ mix32(step * 0x85EBCA6Bu ^ 0x5EED39C1u)) | 1u;   /* the increment stays odd */
    N.seed_bypass = 1;
    defjam_test_quiet_seed = 1;     /* not a [TEST-SEED] line a step */
    guest_call(sub_000AA0D0, SHARED_GEN, 2, args);
    defjam_test_quiet_seed = 0;
    N.seed_bypass = 0;
    g_eax = eax; g_ecx = ecx; g_edx = edx; g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp; g_esp = esp;
    memcpy(g_fp_stack, fp, sizeof(fp));
    g_fp_top = fp_top;
    g_fp_control_word = fp_cw;
}

/* King of the Hill: the fight decided, so the relay can rotate the line: RESULT with the winner's
 * room slot, or WINNER_DRAW. The phase word alone missed fights that end without the decided
 * phases: both fighters killed by the venue (result code 0x1105, "everybody out", a draw) went from
 * phase 1 straight to the summary, no RESULT went out and the hill stood at "fighting" (three-copy
 * test, 2026-10-25). The game's own result record (docs/research/match-end-state.md: the history
 * ring and its code) says it in every case; a code without the decisive bit is an elimination
 * along the way. Also when the fight went on offline (one report decides after 10 s). `final`: the
 * game has left the fight; with nothing decisive recorded it counts as a draw, so the hill moves on. */
static void koth_check_result(int final)
{
    uint32_t phase, count, code = 0, w = 0xFFFFFFFFu;
    int winner = -1;
    const char *how = "";
    /* match_seen: this START's match is set up (the last one's phase and records linger until then) */
    if (!N.lobby || !(N.koth_match || N.set_match) || !N.match_live || !N.match_seen || N.koth_done || N.resim) return;
    if (!final && N.lockstep && N.rb_on && N.step > N.final_step) return;  /* the state may come from a prediction */
    phase = MEM32(G_MATCH_PHASE);
    if (phase == 1u) N.koth_fought = 1;
    if (!N.koth_fought) return;
    count = MEM32(G_RESULT_COUNT);
    if (count && count <= RESULTS_MAX) code = MEM16(G_RESULTS + (count - 1u) * 0x10u + 0xEu);
    if ((code & 1u) && (code & 4u)) {
        winner = (int)WINNER_DRAW;
        how = "a draw";
    } else if ((code & 1u) || (phase >= 2u && phase <= 3u)) {
        /* the winner slot is written with the decision; wait for it if only the record says so */
        if (phase == 3u) w = MEM32(G_MATCH_WINNER);
        else if (phase == 2u || (MEM32(G_MATCH_FLAGS) & 8u)) w = MEM32(G_MATCH_TWIN);
        else if (!final) return;
        winner = w < (uint32_t)N.players && w < MAXP ? N.start_slots[w] : (int)WINNER_DRAW;
        how = winner == (int)WINNER_DRAW ? "no winner slot" : "won";
    } else if (final) {
        winner = (int)WINNER_DRAW;
        how = "left the fight with no result recorded, counted as a draw";
    } else {
        return;
    }
    EnterCriticalSection(&g_cs);
    N.koth_done = 1;
    if (N.koth_match)
        N.koth_results = 1;     /* the winner's scene, the summary and the results follow, until the menu */
    N.koth_tries = 0;
    N.koth_sent = 0;
    N.koth_winner = winner;
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[NETPLAY] %s: the fight is decided (%s; code %04X, phase %u), winner %s %d; sending RESULT\n",
            N.koth_match ? "King of the Hill" : "ranked set", how, (unsigned)code, (unsigned)phase, winner == (int)WINNER_DRAW ? "none, slot" : "room slot", winner);
}
/* A player's own controller dropped mid-fight and the game showed "Controller Disconnected"
 * (port 2) that START could not close (players' report, 2026-10-26). Our input layer never
 * unplugs a port (a lost pad reads as a pad at rest), so this logs when the title itself sees a
 * room port unplugged (the XAPI handle sub_001EB960 tests) and tells the player when the PC's
 * last controller goes and comes back. */
#define G_PAD_HANDLE(port) (0x3C9340u + (uint32_t)(port) * 0x90u)
int xbox_HostInputSlotInfo(int slot, char *name, int size);
static void watch_pads(void)
{
    static int had_pad = -1;
    static unsigned told;
    unsigned gone = 0;
    int i, pads = 0;
    if (!N.lobby || !N.match_seen) { had_pad = -1; told = 0; return; }
    for (i = 0; i < 4; i++)
        if (xbox_HostInputSlotInfo(i, NULL, 0) & 1) pads++;
    for (i = 0; i < N.players && i < 4; i++)
        if (!MEM32(G_PAD_HANDLE(i))) gone |= 1u << i;
    if (gone != told) {
        fprintf(stderr, "[NETPLAY] the game sees ports %X unplugged (this PC is player %d; lockstep %d, live %d, step %u)\n",
                gone, N.pos + 1, (int)N.lockstep, N.match_live, (unsigned)N.step);
        told = gone;
    }
    if (had_pad == 1 && !pads) {
        fprintf(stderr, "[NETPLAY] this PC's last controller disconnected during an online match\n");
        toast(10000, "Your controller disconnected: reconnect it to keep fighting.");
    }
    if (had_pad == 0 && pads) toast(4000, "Controller connected again.");
    had_pad = pads > 0;
}

/* The fight's first step waits until the versus screen has been up VS_MIN_MS: with an instant load
 * it covered the venue intro (the fighters walking in, the announcer) and was gone in a moment.
 * Nothing of the fight has run yet, so the state is the same on every PC; the others wait for this
 * PC's entries as for a slower load (START_LIMIT_MS). RECOMP_VERSUS_HOLD=0 turns it off (tests). */
static void versus_hold(void)
{
    static int off = -1;
    ULONGLONG start = N.vs_start;
    if (off < 0) off = getenv("RECOMP_VERSUS_HOLD") && !strcmp(getenv("RECOMP_VERSUS_HOLD"), "0");
    if (off || N.step != 0 || N.resim || !N.lobby || !start) return;
    while (N.lockstep && N.match_live && N.vs_start == start && GetTickCount64() - start < VS_MIN_MS)
        WaitForSingleObject(N.arrived, 10);
}

/* RECOMP_NETPLAY_STEPLOG (tests): the fighter records, the match globals (0x3B8F28..0x3B9074), the
 * fight's weapon table (0x3BE778, 16 x 0xA4) and the weapon manager (0x376144) of the last STEPLOG_RING
 * steps, kept in memory and written to logs/desync-<match>-p<position>.bin when this PC sees a desync,
 * so the copies' files show the first step and the very fields where they parted (the checksum covers
 * six fields a fighter). Each record: u32 step, then the ranges' bytes in this order. */
static uint32_t weapon_hash(void);      /* the weapon manager, below with its update */
static unsigned g_weapon_waits;
#define STEPLOG_RING 256
static const uint32_t k_steplog_ranges[][2] = {
    { ACTORS, ACTORS + 4u * STRIDE }, { 0x003B8F28u, 0x003B9074u }, { 0x003BE778u, 0x003BF1B8u },
    { 0x00376144u, 0x00376E70u },
};
#define STEPLOG_BYTES (4u * STRIDE + (0x003B9074u - 0x003B8F28u) + (0x003BF1B8u - 0x003BE778u) + \
                       (0x00376E70u - 0x00376144u))
static uint8_t *g_steplog;
static uint32_t g_steplog_step[STEPLOG_RING], g_steplog_dumped;

static void steplog_keep(uint32_t s)
{
    uint8_t *at;
    uint32_t i, r, n = 0;
    if (!g_steplog && !(g_steplog = (uint8_t *)malloc((size_t)STEPLOG_RING * STEPLOG_BYTES))) return;
    at = g_steplog + (size_t)(s % STEPLOG_RING) * STEPLOG_BYTES;
    for (r = 0; r < sizeof(k_steplog_ranges) / sizeof(k_steplog_ranges[0]); r++)
        for (i = k_steplog_ranges[r][0]; i < k_steplog_ranges[r][1]; i++) at[n++] = MEM8(i);
    g_steplog_step[s % STEPLOG_RING] = s;
    if (N.desync && g_steplog_dumped != N.match) {     /* the game thread writes it, once per match */
        char path[96];
        FILE *f;
        uint32_t first = s >= STEPLOG_RING ? s - STEPLOG_RING + 1 : 1, t;
        g_steplog_dumped = N.match;
        snprintf(path, sizeof(path), "logs\\desync-%08X-p%d.bin", N.match, N.pos + 1);
        if ((f = fopen(path, "wb")) != NULL) {
            for (t = first; t <= s; t++) {
                if (g_steplog_step[t % STEPLOG_RING] != t) continue;
                fwrite(&t, 4, 1, f);
                fwrite(g_steplog + (size_t)(t % STEPLOG_RING) * STEPLOG_BYTES, 1, STEPLOG_BYTES, f);
            }
            fclose(f);
            fprintf(stderr, "[NETPLAY] the last %u steps' fighter records are in %s\n", s - first + 1, path);
        }
    }
}

/* START pressed on this PC's own controller (an edge): a spectator stops watching with it. */
static int spec_stop_pressed(void)
{
    XBOX_INPUT_STATE st;
    int down;
    memset(&st, 0, sizeof(st));
    down = xbox_HostInputGetLocalState(&st) == ERROR_SUCCESS && (st.Gamepad.wButtons & 0x0010);
    if (down && !N.spec_start_down) { N.spec_start_down = 1; return 1; }
    if (!down) N.spec_start_down = 0;
    return 0;
}

/* Spectator mode, step s (its checksum taken): compare it with the players', then wait for every
 * player's entry of the step. Never a give-up that counts for anyone: a feed that stops only stops
 * the watching, and the game goes on on its own (START, then Quit, goes back to the room). */
static void spec_step(uint32_t s)
{
    ULONGLONG since;
    int waited_long = 0;
    if (s % HASH_EVERY == 0 && N.hash_tag[s % RING] == s + 1) {
        uint32_t k = s % RING;
        unsigned slot = (s / HASH_EVERY) % SPEC_HASHES;
        NetCheck c;
        memset(&c, 0, sizeof(c));
        c.match = N.match; c.step = s; c.hash = N.hash_at[k];
        memcpy(c.parts, N.hash_parts[k], sizeof(c.parts));
        memcpy(c.fields, N.hash_fields[k], sizeof(c.fields));
        memcpy(c.gens, N.hash_gens[k], sizeof(c.gens));
        c.crowd = N.hash_crowd[k];
        c.shared = N.hash_split[k][0]; c.time_scale = N.hash_split[k][1]; c.match_gen = N.hash_split[k][2];
        c.has_parts = c.has_extra = 1;
        EnterCriticalSection(&g_cs);
        g_spec_lh[slot] = c;
        spec_compare(slot);
        LeaveCriticalSection(&g_cs);
    }
    if (spec_stop_pressed()) {
        fprintf(stderr, "[NETPLAY] watching: stopped with START at step %u\n", s);
        InterlockedExchange(&N.lockstep, 0);
        toast(10000, "You stopped watching. START, then Quit, goes back to the room.");
        return;
    }
    if (s <= (uint32_t)N.delay) return;
    since = GetTickCount64();
    for (;;) {
        int ready = 1, i, late = -1;
        ULONGLONG now;
        EnterCriticalSection(&g_cs);
        for (i = 0; i < N.players; i++) if (!spec_entry(i, s, NULL)) { ready = 0; late = i; }
        LeaveCriticalSection(&g_cs);
        if (ready || !N.lockstep) {
            if (ready && waited_long)
                fprintf(stderr, "[NETPLAY] watching: waited %.1f s for player %d's entries at step %u\n",
                        (GetTickCount64() - since) / 1000.0, N.last_late + 1, s);
            break;
        }
        N.last_late = late;
        WaitForSingleObject(N.arrived, 4);
        now = GetTickCount64();
        if (now - since >= 1000) waited_long = 1;
        if (spec_stop_pressed()) {
            fprintf(stderr, "[NETPLAY] watching: stopped with START at step %u\n", s);
            InterlockedExchange(&N.lockstep, 0);
            toast(10000, "You stopped watching. START, then Quit, goes back to the room.");
            break;
        }
        {
            int decided = N.fighting_seen && MEM32(G_MATCH_PHASE) >= 2u;
            if (now - since > (decided ? DECIDED_LIMIT_MS : s <= (uint32_t)N.delay + 1 ? START_LIMIT_MS : STALL_LIMIT_MS)) {
                InterlockedExchange(&N.lockstep, 0);
                if (decided) {
                    fprintf(stderr, "[NETPLAY] watching: the match is decided and the players went on (step %u): "
                            "the rest runs on this PC\n", s);
                } else {
                    fprintf(stderr, "[NETPLAY] watching: the feed stopped at step %u (player %d's entries); not "
                            "watching any more\n", s, late + 1);
                    toast(10000, "The match's feed stopped. START, then Quit, goes back to the room.");
                }
                break;
            }
        }
    }
}

void netplay_on_step(void)
{
    uint32_t s, target, k, need;
    ULONGLONG waited_since, last_send, last_note;
    if (N.lockstep) versus_hold();
    created_fighter_on_step();  /* created fighters: unlinked once their looks are built */
    cp_pose_in_step();          /* created fighters: posed once a step, on every PC alike */
    fight_input();              /* also after a fight went on offline */
    if (!N.resim) watch_pads();
    koth_check_result(0);
    if (!N.lockstep) {
        if (soak_depth()) {     /* tests: the soak counts steps and prepares the save states itself */
            s = ++N.step;
            if (s == 1 && !N.resim && rollback_prepare() == 0) {
                N.rb_on = N.rb_ref_due = N.rbm.on = 1;
                N.rbm.num = ++N.rb_matches;
                rollback_crash_cb = rollback_crash_state;
            }
        }
        return;
    }
    /* RECOMP_ONLINE_CROWD=off (tests only): the spectators cannot touch the fight. Their animation
     * differs between the PCs (it starts while the match loads, at host timing) and reaches the
     * fighters through the crowd's record table as catches, holds and weapons, which parts the
     * lockstep. With the record count at 0 no scan sees a spectator. Catches and crowd weapons are
     * part of the game, so this is a comparison switch, not the fix. docs/research/spectator-shove.md */
    {
        static int crowd_off = -1;
        if (crowd_off < 0) crowd_off = getenv("RECOMP_ONLINE_CROWD") && !strcmp(getenv("RECOMP_ONLINE_CROWD"), "off");
        if (crowd_off) MEM32(G_CROWD_RECORDS) = 0;
    }
    /* The room match decided (the game's phase word: 1 fighting, 2 decided, 3 finished; see
     * test_telemetry.c): report its result. Only after this match was seen fighting, as the
     * phase still holds the last match's value for a moment after a START. */
    /* With rollback: the step at which the match was decided, by any ending. Some endings stop the fight steps
     * at once and skip the phase words 2 and 3 (venue 26, code 0x201): a PC that had predicted a step past
     * the others' last one never saw it final, so it never reported the result nor switched rollback off, and
     * the room's next match never started (two-copy run at venue 26, 2026-11-02). The decision counts once
     * the step it was seen at is final. */
    if (N.lobby && N.match_live && N.rb_on && !N.resim && !N.rb_decided_step && N.rb_fought) {
        uint32_t phase = MEM32(G_MATCH_PHASE), count = MEM32(G_RESULT_COUNT), code = 0;
        if (count && count <= RESULTS_MAX) code = MEM16(G_RESULTS + (count - 1u) * 0x10u + 0xEu);
        if ((phase >= 2u && phase <= 3u) || ((code & 1u) && ((code & 4u) || (MEM32(G_MATCH_FLAGS) & 8u))))
            N.rb_decided_step = N.step;
    }
    if (N.lobby && N.match_live && !N.reported && !N.resim &&
        (!N.rb_on || N.step <= N.final_step || (N.rb_decided_step && N.final_step >= N.rb_decided_step))) {
        /* The game's result record says draws too (a double knockout, a venue taking both out, a
         * time-up tie): read only through the phase word, those were reported as "slot 0 won", and the
         * venue endings that skip phases 2 and 3 not at all (docs/research/team-and-special-match-types.md
         * 10; the same reading as koth_check_result). */
        uint32_t phase = MEM32(G_MATCH_PHASE), count = MEM32(G_RESULT_COUNT), code = 0;
        if (phase == 1u) N.fighting_seen = 1;
        if (N.fighting_seen && N.match_seen && count && count <= RESULTS_MAX)
            code = MEM16(G_RESULTS + (count - 1u) * 0x10u + 0xEu);
        if ((code & 1u) && (code & 4u)) {
            netplay_on_result(0, 0, 4u);                            /* a decisive draw */
        } else if (N.fighting_seen && phase != 1u && phase >= 2u && phase <= 3u) {
            uint32_t w = MEM32(G_MATCH_WINNER);
            netplay_on_result(winners_of(w), 0, w < MAXP ? 1u : 4u);
        } else if ((code & 1u) && (MEM32(G_MATCH_FLAGS) & 8u)) {   /* decided, the phase word skipped */
            uint32_t w = MEM32(G_MATCH_TWIN);
            netplay_on_result(winners_of(w), 0, w < MAXP ? 1u : 4u);
        }
    }
    s = ++N.step;
    if (s == 1) {
        static int crowd_on = -1;   /* RECOMP_ONLINE_CROWD=keep: tests compare without the restart */
        if (crowd_on < 0) crowd_on = !(getenv("RECOMP_ONLINE_CROWD") && !strcmp(getenv("RECOMP_ONLINE_CROWD"), "keep"));
        if (crowd_on) crowd_reset(g_ecx);
        if (!N.resim && match_rolls_back() && rollback_prepare() == 0) {
            N.rb_on = N.rb_ref_due = N.rbm.on = 1;
            N.rbm.num = ++N.rb_matches;
            rollback_crash_cb = rollback_crash_state;   /* a crash before the first rollback says its state too */
        }
        {   /* what the fight made of the setup: the mode mask (1 one on one, 2 team, 4 3-way, 8 4-way), the
             * arena kind, and per fighter its flags (+0x937: 4 = team side marker) and the slots its
             * opponent (+0) and partner (+4) pointers name (docs/research/team-and-special-match-types.md 3.2) */
            char line[200];
            int n = 0, slot;
            for (slot = 0; slot < N.fighters && slot < MAXP; slot++) {
                uint32_t rec = ACTORS + (uint32_t)slot * STRIDE, opp = MEM32(rec), mate = MEM32(rec + 4u);
                int o = opp >= ACTORS && (opp - ACTORS) % STRIDE == 0 ? (int)((opp - ACTORS) / STRIDE) : -1;
                int m = mate >= ACTORS && (mate - ACTORS) % STRIDE == 0 ? (int)((mate - ACTORS) / STRIDE) : -1;
                n += snprintf(line + n, sizeof(line) - (size_t)n, " %d:flags %02X opp %d mate %d", slot, MEM8(rec + 0x937u), o, m);
            }
            fprintf(stderr, "[NETPLAY] fight setup: type %d, mode mask %02X, arena kind %u;%s\n", N.report_m.type,
                    MEM8(0x003B907Eu), MEM32(0x003B9084u), line);
            fprintf(stderr, "[NETPLAY] weapons at the first step: %08X (their update waited %u times)\n", weapon_hash(),
                    g_weapon_waits);
            g_weapon_waits = 0;
        }
    }
    /* before anything in the step draws, and before the checksum: the same state on every PC */
    if (shared_gen_pin()) shared_gen_reseed(s);
    {
        /* The arena hands its frame argument (edi, kept by sub_00087700 for the crowd update that
         * follows) to the crowd; it is the host's measured frame time, so the spectators' clips
         * advanced by a different amount on each PC. Online the crowd gets the fight's fixed step. */
        uint32_t dt = g_edi;
        float f;
        memcpy(&f, &dt, 4);
        if (s <= 3 || s == 300)
            fprintf(stderr, "[NETPLAY] crowd frame argument at step %u: %08X (%g as a float)\n", s, dt, (double)f);
        if (f > 0.001f && f < 0.25f) {
            f = 1.0f / 60.0f;
            memcpy(&g_edi, &f, 4);
        }
    }
    if (s <= 120 && getenv("RECOMP_CROWD_TRACE")) {
        /* tests: the crowd's three parts, every step, to see which parts first and when */
        uint32_t hr = 2166136261u, hg = 2166136261u, hu = 2166136261u, k, recs = MEM32(0x003494E4u);
        uint32_t count = MEM32(0x003494E0u);
        for (k = 0; recs && k < count * 0x34u && k < 0x4000u; k++) hr = (hr ^ MEM8(recs + k)) * 16777619u;
        for (k = 0; k < 0x88u; k++) hg = (hg ^ MEM8(g_ecx + CROWD_MANAGER + 0xE18u + k)) * 16777619u;
        for (k = 0; k < 6u; k++) hu = (hu ^ g_crowd_gen[k]) * 16777619u;
        fprintf(stderr, "[NETPLAY] crowd parts at step %u: records %08X crowd-gen %08X global-gen %08X\n", s, hr, hg, hu);
    }
    k = s % RING;
    if (s == 1 || s == 2 || s % HASH_EVERY == 0) {
        /* the crowd's state, to compare between the PCs: the records, its generator, the global one;
         * on every checksum's step it goes out with the checksum (diagnostic only), and the two
         * pre-fight CLogic generators with it (docs/research/fight-determinism.md) */
        uint32_t h = 2166136261u, j, recs = MEM32(0x003494E4u), count = MEM32(0x003494E0u);
        for (j = 0; recs && j < count * 0x34u && j < 0x4000u; j++) h = (h ^ MEM8(recs + j)) * 16777619u;
        for (j = 0; j < 0x88u; j++) h = (h ^ MEM8(g_ecx + CROWD_MANAGER + 0xE18u + j)) * 16777619u;
        for (j = 0; j < 6u; j++) h = (h ^ g_crowd_gen[j]) * 16777619u;     /* the crowd's copy, not the real one */
        if (s == 1 || s == 2 || s == 60 || s == 300 || s == 900 || s == 1800)
            fprintf(stderr, "[NETPLAY] crowd hash at step %u: %08X (%u records)\n", s, h, count);
        N.hash_crowd[k] = h;
        for (j = 0; j < NETCHECK_GENS; j++) {
            uint32_t g = 2166136261u, a;
            for (a = 0; a < 0x88u; a += 4) g = (g ^ MEM32(netcheck_gen[j] + a)) * 16777619u;
            N.hash_gens[k][j] = g;
        }
    }
    N.step_runs[k] = (uint8_t)(N.hash_tag[k] == s + 1 ? N.step_runs[k] + 1 : 1);
    N.step_reads[k] = 0;
    N.hash_at[k] = fighter_hash(N.hash_parts[k], N.hash_fields[k], N.hash_split[k]);   /* the state as this step starts */
    {   /* the x87 control word the step starts with (its precision bits round every result): a
         * change is logged, at most 12 a match, to compare the PCs of a desynced match */
        static uint16_t last_cw;
        static uint32_t cw_match, cw_told;
        if (cw_match != N.match) { cw_match = N.match; cw_told = 0; last_cw = 0; }
        if (g_fp_control_word != last_cw && !N.resim) {
            if (cw_told++ < 12)
                fprintf(stderr, "[NETPLAY] x87 control word at step %u: %04X (was %04X)\n", s, g_fp_control_word, last_cw);
            last_cw = g_fp_control_word;
        }
    }
    N.hash_tag[k] = s + 1;
    {   /* RECOMP_NETPLAY_STEPLOG=1 (tests): the checksum's fields of every fighter at every step, to find
         * the step and the field where the PCs of a desynced match part (the checksum goes out every 60) */
        static int steplog = -1;
        if (steplog < 0) steplog = getenv("RECOMP_NETPLAY_STEPLOG") != NULL;
        if (steplog && !N.resim) {
            static const uint32_t fields[] = { 0x30, 0x44, 0x48, 0x4C, 0x54, 0x350 };
            char line[400];
            int n, slot;
            unsigned fi;
            {   /* and the crowd's records the fighters read (spectator-shove.md): a part of its own */
                uint32_t h = 2166136261u, kk, recs = MEM32(0x003494E4u), count = MEM32(0x003494E0u);
                for (kk = 0; recs && kk < count * 0x34u && kk < 0x4000u; kk++) h = (h ^ MEM8(recs + kk)) * 16777619u;
                n = snprintf(line, sizeof(line), "[STEPHASH] %08X %u crowd %08X", N.match, s, h);
            }
            for (slot = 0; slot < N.fighters && slot < MAXP; slot++) {
                n += snprintf(line + n, sizeof(line) - (size_t)n, " |");
                for (fi = 0; fi < sizeof(fields) / sizeof(fields[0]); fi++)
                    n += snprintf(line + n, sizeof(line) - (size_t)n, " %08X", MEM32(ACTORS + (uint32_t)slot * STRIDE + fields[fi]));
            }
            fprintf(stderr, "%s\n", line);
            steplog_keep(s);
        }
    }
    if (N.resim) return;                /* a re-run: the entries are all known or predicted */
    if (N.watching) { spec_step(s); return; }
    target = s + (uint32_t)N.delay;
    k = target % RING;
    EnterCriticalSection(&g_cs);
    memcpy(N.in[N.pos][k], N.latest_local, ENTRY);
    N.in_tag[N.pos][k] = target + 1;
    N.in_match[N.pos][k] = N.match;
    LeaveCriticalSection(&g_cs);
    send_steps(target);

    if (!N.rb_on) send_hashes(s);      /* with rollback, once the steps before are final (netplay_on_update) */

    /* Wait for every other player's entry for this step; while they fight, with rollback, only
     * for the entries RB_AHEAD steps back (the newer ones are predicted). */
    if (s <= (uint32_t)N.delay) return;
    /* only once every other PC is under way (an entry of its own has arrived): one may still be
     * loading the venue, and a PC that ran ahead would give up on it after STALL_LIMIT_MS */
    N.predicting = N.rb_on && rollback_ready() && MEM32(G_MATCH_PHASE) == 1u && N.final_step > (uint32_t)N.delay;
    if (N.predicting && !N.rbm.first_pred) {   /* B3/G4: whether a match predicts at all, and from when */
        N.rbm.first_pred = s;
        fprintf(stderr, "[ROLLBACK] first prediction in match %u at step %u (final %u, delay %d)\n", N.rbm.num, s,
                N.final_step, N.delay);
    }
    need = N.predicting ? (s > RB_AHEAD ? s - RB_AHEAD : 0) : s;
    if (need <= (uint32_t)N.delay) return;
    waited_since = last_send = GetTickCount64();
    last_note = 0;
    for (;;) {
        int ready = 1, i, late = -1;
        uint32_t t = need;
        if (N.rb_on) { t = N.final_step + 1; if (t <= (uint32_t)N.delay) t = (uint32_t)N.delay + 1; }
        EnterCriticalSection(&g_cs);
        for (; t <= need; t++)
            for (i = 0; i < N.players; i++)
                if (i != N.pos && !(N.in_tag[i][t % RING] == t + 1 && N.in_match[i][t % RING] == N.match)) { ready = 0; late = i; }
        LeaveCriticalSection(&g_cs);
        if (ready || !N.lockstep) {
            ULONGLONG waited = GetTickCount64() - waited_since;
            /* a frozen picture of half a second or more: say why once it moves again */
            if (ready && waited >= 500 && s > (uint32_t)N.delay + 1 && !last_note)
                toast(4000, "Waited %.1f s for %s (connection).", waited / 1000.0, player_name(N.last_late));
            if (waited >= 1000 && last_note) {     /* the screen could not say it meanwhile: say it now */
                /* King of the Hill: the other one (the king) sat on the last fight's results, which
                 * its P_RTT said; the 13 to 61 s waits of build 33 were that, not a slow load */
                int results = N.last_late >= 0 && N.last_late < MAXP && N.peer_results[N.last_late];
                const char *why = results ? "leave the last fight's results" : "finish loading the fight";
                toast(5000, "Waited %.1f s for %s to %s.", waited / 1000.0, player_name(N.last_late), why);
                fprintf(stderr, "[NETPLAY] waited %.1f s at the fight's start for player %d to %s\n",
                        waited / 1000.0, N.last_late, results ? "leave the last fight's results" : "finish loading");
            }
            break;
        }
        N.last_late = late;
        WaitForSingleObject(N.arrived, 4);
        {
            ULONGLONG now = GetTickCount64();
            /* At the fight's start the other PC may still be loading the venue (a slower drive): the
             * picture stands on "FIGHT!" meanwhile, which players took for a freeze (four F9
             * reports, 2026-10-23). The game draws nothing while it waits here, so this is noted and
             * said once the fight moves (above). */
            if (late >= 0 && g_steps_match[late] != N.match && now - waited_since >= 1500) last_note = now;
            if (now - last_send >= 50) { send_steps(target); last_send = now; }   /* the others may be waiting for ours */
            /* The fight decided (KO, time): what follows is the winner's scene and the results, the same
             * on every PC however they part, and a PC that skips ahead stops sending steps. Waiting the
             * mid-fight ten seconds there froze the winner's scene and then said the fight would not
             * count, after it had (Farouk's F9 report 63684c, 2026-10-23). */
            int decided = N.fighting_seen && MEM32(G_MATCH_PHASE) >= 2u;
            if (now - waited_since > (decided ? DECIDED_LIMIT_MS :
                                      need <= (uint32_t)N.delay + 1 ? START_LIMIT_MS : STALL_LIMIT_MS)) {
                uint32_t u;
                if (decided) {
                    fprintf(stderr, "[NETPLAY] the match is decided and player %d went on ahead (step %u): the rest "
                            "runs on this PC alone\n", late, s);
                    InterlockedExchange(&N.lockstep, 0);
                    set_status("The match is over; the rest runs on this PC.");
                    break;
                }
                fprintf(stderr, "[NETPLAY] gave up at step %u waiting for the entries up to %u (final %u, phase %u, "
                        "predicting %d, rollback %d); player %d has:", s, need, N.final_step, MEM32(G_MATCH_PHASE),
                        N.predicting, N.rb_on, late + 1);
                /* entries of another match are an earlier match's leftovers in the ring, never used */
                for (u = N.final_step + 1; u <= need + 2 && late >= 0; u++)
                    fprintf(stderr, " %u:%u/%08X", u, N.in_tag[late][u % RING], N.in_match[late][u % RING]);
                fprintf(stderr, " (match %08X; STEPS packets from it %u this session, the latest for match %08X "
                        "step %u)\n", N.match, late >= 0 ? g_steps_rx[late] : 0, late >= 0 ? g_steps_match[late] : 0,
                        late >= 0 ? g_steps_last[late] : 0);
                InterlockedExchange(&N.lockstep, 0);
                set_status("A player stopped answering during the fight; continuing offline.");
                if (late >= 0 && N.fighting_seen && need > (uint32_t)N.delay + 60) {
                    report_left(1u << late);
                    toast(10000, "%s stopped answering: it counts as their loss if they were signed in. START quits.",
                          player_name(late));
                } else
                    toast(10000, "%s stopped answering: the fight goes on offline and won't count.", player_name(late));
                break;
            }
        }
    }
}

/* Tests (the offline soak): each step's pad words as the first pass read them, put back when the step
 * runs again, as an online match puts back the entries. Without it a re-run read whatever the test bot
 * decided anew (its memory is not rewound), so the soak counted the bot's second thoughts as steps that
 * "came out otherwise" (2026-11-02: 6% of steps; two re-runs of the same steps agreed on all). */
#define SOAK_PAD_RING 64
static uint32_t s_soak_pad[SOAK_PAD_RING][MAXP][8], s_soak_pad_tag[SOAK_PAD_RING][MAXP];
/* RECOMP_ROLLBACK_SOAK_WRONG: this re-run gets other buttons (a wrong prediction) for the steps from
 * s_soak_wrong on; reads for earlier steps (an update without a fight step at the start of the re-run
 * reads the last final step) stay right, as online, where only steps not yet final are predicted. Applied
 * to those too, the soak itself spoiled the save of the step it went back to (2026-11-02). */
static uint32_t s_soak_wrong;

static void soak_pad(uint32_t actor)
{
    uint32_t slot, base, k, i;
    if (actor < ACTORS || (actor - ACTORS) % STRIDE) return;
    slot = (actor - ACTORS) / STRIDE;
    if (slot >= MAXP) return;
    base = PAD_TABLE + slot * 0x20u;
    k = N.step % SOAK_PAD_RING;
    if (!N.resim) {
        for (i = 0; i < 8; i++) s_soak_pad[k][slot][i] = MEM32(base + i * 4u);
        s_soak_pad_tag[k][slot] = N.step + 1;
    } else if (s_soak_pad_tag[k][slot] == N.step + 1) {
        for (i = 0; i < 8; i++) MEM32(base + i * 4u) = s_soak_pad[k][slot][i];
        /* a wrong prediction: the buttons of 23 steps earlier (another press, another stick) */
        if (s_soak_wrong && N.step >= s_soak_wrong && s_soak_pad_tag[(N.step + SOAK_PAD_RING - 23) % SOAK_PAD_RING][slot] == N.step + 1 - 23)
            for (i = 0; i < 7; i++) MEM32(base + i * 4u) = s_soak_pad[(N.step + SOAK_PAD_RING - 23) % SOAK_PAD_RING][slot][i];
    }
}

void netplay_on_pad_read(uint32_t actor)
{
    uint32_t slot, base, s;
    int i;
    uint8_t entry[ENTRY];
    if (!N.lockstep) {
        if (soak_depth() && N.rb_on) soak_pad(actor);
        return;
    }
    if (actor < ACTORS || (actor - ACTORS) % STRIDE) return;
    slot = (actor - ACTORS) / STRIDE;
    if ((int)slot >= N.players) return;
    base = PAD_TABLE + slot * 0x20u;
    /* This PC's player: a fresh poll (no marker) is the input to send next. */
    if ((int)slot == N.pos && MEM32(base + 28) != MARK && !N.resim) {
        EnterCriticalSection(&g_cs);
        for (i = 0; i < ENTRY; i++) N.latest_local[i] = MEM8(base + (uint32_t)i);
        N.latest_local[28] = N.latest_local[29] = N.latest_local[30] = N.latest_local[31] = 0;
        LeaveCriticalSection(&g_cs);
    }
    s = N.step;
    memset(entry, 0, sizeof(entry));
    if (s > (uint32_t)N.delay) {
        uint32_t k = s % RING;
        int predicted = 0;
        EnterCriticalSection(&g_cs);
        if (N.watching)
            spec_entry((int)slot, s, entry);    /* spectator mode: the whole match is kept */
        else if (N.in_tag[slot][k] == s + 1 && N.in_match[slot][k] == N.match)
            memcpy(entry, N.in[slot][k], ENTRY);
        else if (N.predicting || N.resim) {     /* not here yet: that player's last entry */
            uint32_t t;
            predicted = 1;
            for (t = s - 1; t > (uint32_t)N.delay && t + 64 > s; t--)
                if (N.in_tag[slot][t % RING] == t + 1 && N.in_match[slot][t % RING] == N.match) {
                    memcpy(entry, N.in[slot][t % RING], ENTRY);
                    break;
                }
        }
        if ((int)slot != N.pos) N.step_reads[k]++;
        if ((int)slot != N.pos && !N.watching) {
            /* A step can read its pads more than once: updates that run no fight step (slow motion, moves
             * that stop the steps) read the entries of the step still current. If one of those reads ran
             * on a prediction and a later one on the entry that had arrived meanwhile, the record said
             * "real" and the step that had used the prediction was never run again: the PCs parted by a
             * few thousandths in the fighters' positions (two-copy runs, 2026-11-02, steps 10 200 and
             * 37 440). So a prediction, once used in a step, stays recorded until a re-run reads it anew
             * (rollback_to clears the records), and two different predictions in one step count as wrong. */
            if (N.used_tag[slot][k] == s + 1 && N.used_pred[slot][k]) {
                if (predicted && memcmp(N.used[slot][k], entry, ENTRY)) N.used_mixed[slot][k] = 1;
                if (!predicted) {
                    static unsigned told;
                    if (told++ < 20)
                        fprintf(stderr, "[ROLLBACK] step %u read slot %u's prediction, then its entry, in one step: "
                                "kept as predicted\n", s, slot);
                }
            } else {
                memcpy(N.used[slot][k], entry, ENTRY);
                N.used_tag[slot][k] = s + 1;
                N.used_pred[slot][k] = (uint8_t)predicted;
                N.used_mixed[slot][k] = 0;
            }
        }
        LeaveCriticalSection(&g_cs);
    }
    {   /* RECOMP_NETPLAY_PADLOG=n: the first n reads, polled and injected, with the slot's bindings */
        static int budget = -1;
        if (budget < 0) budget = getenv("RECOMP_NETPLAY_PADLOG") ? atoi(getenv("RECOMP_NETPLAY_PADLOG")) : 0;
        if (budget > 0 && (s % 30 == 5 || s < 8)) {
            uint32_t bind = 0x003B90C8u + slot * 0x18u;
            budget--;
            fprintf(stderr, "[PADLOG] step %u slot %u polled %08X %08X %08X w7 %08X | injected %08X %08X %08X | "
                    "mask %08X bind %08X %08X %08X %08X %08X %08X\n", s, slot,
                    MEM32(base), MEM32(base + 4), MEM32(base + 8), MEM32(base + 28),
                    get32(entry), get32(entry + 4), get32(entry + 8), MEM32(0x003B8FC8u + slot * 4u),
                    MEM32(bind), MEM32(bind + 4), MEM32(bind + 8), MEM32(bind + 12), MEM32(bind + 16), MEM32(bind + 20));
        }
    }
    for (i = 0; i < 7; i++)
        MEM32(base + (uint32_t)i * 4) = get32(entry + i * 4);
    MEM32(base + 28) = MARK;
}

/* ---- rollback ---------------------------------------------------------------------------
 *
 * At the start of every game update (the main loop's dt read, src/recomp_manual.c) the state is
 * saved for the step about to run. When an entry arrives that differs from the prediction a step
 * ran with, the save from before that step is loaded and the updates since are run again here,
 * the way the main loop runs them (sub_00011CD0: input, the screen manager, the event manager and
 * its snapshot, the render subsystem's tick), with the sound engine's commands silenced. */
extern void sub_000DD930(void);     /* the input subsystem's update: ecx = it, dt */
extern void sub_00077040(void);     /* the screen manager's update: ecx = it, dt; al = go on */
extern void sub_000BD860(void);     /* the event manager: ecx = it */
extern void sub_000BDF40(void);     /* its copy of the public buffers: ecx = it */
extern void sub_0001A240(void);     /* the render subsystem's per-update tick: ecx = it, dt */
extern void sub_00137010(void);     /* the sound engine's command call */
#define DT_BITS 0x3C888889u         /* 1/60, the constant every update gets */

void netplay_sound_command(void)
{
    if (N.resim) { g_eax = 0; g_esp += 4; return; }     /* cdecl: the caller pops the arguments */
    if (N.rb_on && rollback_sound_command_dead(g_esp)) { g_eax = 0; g_esp += 4; return; }
    rollback_sound_context(1);
    sub_00137010();
    rollback_sound_context(0);
}

/* The audio subsystem's message-bus handlers (rollback-step-structure.md 2.1): while steps run
 * again they do nothing, so the game's sound state stays what the sound engine was told. cdecl. */
#define SOUND_HANDLERS(X) X(00034A60) X(00038440) X(00035250) X(00035670) X(00035890)                           X(00035970) X(00035A20) X(00035AE0) X(00038620) X(00038760)
#define SOUND_WRAP(a) extern void sub_##a(void);     static void snd_##a(void) { if (N.resim) { g_eax = 0; g_esp += 4; return; } sub_##a(); }
SOUND_HANDLERS(SOUND_WRAP)

/* The weapon manager (CLogic+0x2C9E4 = 0x376144, 16 slots of 0xD0; docs/research/weapon-position-timing.md):
 * its update (vtable+8, called once per game update through the task list, so recomp_lookup_manual
 * hands it out through netplay_sound_handler) moves the weapons lying in the arena. While a room's match
 * loads, the game updates at each PC's own pace, so the weapons had settled differently by the first
 * fight step (four-copy Team test, 2026-10-26: their matrices differed at step 1, the fighters at step
 * 2). In a lockstep room match the update waits for the first fight step; from then on it runs once a
 * step on every PC. */
extern void sub_000BCC50(void);
#define WEAPON_MANAGER 0x00376144u
static unsigned g_weapon_waits;

static uint32_t weapon_hash(void)
{
    uint32_t h = 2166136261u, slot, k;
    for (slot = 0; slot < 16; slot++)
        for (k = 0; k < 16; k++) h = (h ^ MEM32(WEAPON_MANAGER + slot * 0xD0u + 0xCu + 4u * k)) * 16777619u;
    return h;
}

static void weapon_update(void)
{
    if (N.lockstep && N.lobby && N.match_live && (!N.match_seen || N.step == 0)) {
        g_weapon_waits++;
        g_esp += 8;                 /* thiscall, ret 4: the dt argument and the return address */
        return;
    }
    sub_000BCC50();
}

void *netplay_sound_handler(uint32_t va)
{
    if (va == 0x000BCC50u) return (void *)weapon_update;
#define SOUND_CASE(a) if (va == 0x##a##u) return (void *)snd_##a;
    SOUND_HANDLERS(SOUND_CASE)
    return NULL;
}

static uint32_t g_last_rb_target, g_last_rb_from;

/* For other diagnostics (the crowd guard in recomp_manual.c): where the fight and its rollback are. */
int netplay_rollback_brief(char *out, int size)
{
    if (!N.lockstep) return snprintf(out, (size_t)size, "not in an online fight (update %u)", N.updates);
    return snprintf(out, (size_t)size, "step %u, final %u, update %u, rollback %s, %u rollbacks (deepest %u, the last back to %u from %u)%s",
                    N.step, N.final_step, N.updates, N.rb_on ? "on" : "off", N.rb_count, N.rb_deepest,
                    g_last_rb_target, g_last_rb_from, N.resim ? ", running steps again" : "");
}

static void rollback_crash_state(void)
{
    fprintf(stderr, "[ROLLBACK] at the crash: %s, step %u, final %u, update %u, %u rollbacks, the last back to %u from %u, thread %lu%s\n",
            N.resim ? "running steps again" : "a normal update", N.step, N.final_step, N.updates, N.rb_count,
            g_last_rb_target, g_last_rb_from, (unsigned long)GetCurrentThreadId(), N.predicting ? ", predicting" : "");
    fprintf(stderr, "[ROLLBACK] this match: %u rollbacks, deepest %u, first prediction at step %u\n", N.rbm.count,
            N.rbm.deepest, N.rbm.first_pred);
    rollback_crash_objects();   /* the registers' objects: vtable, +0xC, +0x10, their pages, made in a re-run? */
    {   /* EAGL's loaded objects (list head 0x3A52A8, next at +0, previous at +4: the insert at 0x10A3DD
         * stores the old head at +0 of the new one): which one is bad, and its page */
        uint32_t o = MEM32(0x003A52A8u), heap_lo, heap_hi;
        int k;
        rollback_heap_range(&heap_lo, &heap_hi);
        for (k = 0; k < 48 && o; k++) {
            int sane = o >= heap_lo && o < heap_hi;
            fprintf(stderr, "[ROLLBACK]   loaded object %08X (%s)%s\n", o, rollback_page_state(o), sane ? "" : " NOT A HEAP ADDRESS");
            if (!sane) break;
            {   /* dlsym's pointers: +8 string table, +0x10 symbol count, +0x14 symbol table, +0x420 chain, +0x424 flags */
                static const uint32_t off[] = { 8u, 0x10u, 0x14u, 0x18u, 0x1Cu, 0x420u, 0x424u, 0x42Cu };
                unsigned j;
                for (j = 0; j < sizeof(off) / sizeof(off[0]); j++) {
                    uint32_t v = MEM32(o + off[j]);
                    fprintf(stderr, "[ROLLBACK]     +%03X = %08X (%s)\n", off[j], v, rollback_page_state(v));
                }
            }
            o = MEM32(o);
        }
    }
    rollback_arena_history();
    fflush(stderr);
}

static void snap_save(void)
{
    uint32_t step = N.step + 1;
    int slot = (int)(step % ROLLBACK_SLOTS);
    if (rollback_save(slot)) { N.snap[slot].valid = 0; return; }
    N.snap[slot].valid = 1;
    N.snap[slot].step = step;
    N.snap[slot].update = N.updates;
    N.snap[slot].test_step = defjam_test_current_step();
    memcpy(N.snap[slot].crowd_gen, g_crowd_gen, sizeof(g_crowd_gen));
    N.snap[slot].crowd_gen_on = g_crowd_gen_on;
    EnterCriticalSection(&g_cs);
    memcpy(N.snap[slot].seeds, N.seeds, sizeof(N.snap[slot].seeds));
    LeaveCriticalSection(&g_cs);
}

static void rollback_to(uint32_t f)
{
    int slot = (int)(f % ROLLBACK_SLOTS);
    uint32_t n, j;
    /* the objects the main loop updates, from the game's registry (sub_00012CF0 reads 0x2FB148[slot]) */
    uint32_t input = MEM32(0x002FB148u + 0x19u * 4u), machine = MEM32(0x002FB148u + 0x17u * 4u);
    uint32_t em = MEM32(0x002FB148u + 0x1Eu * 4u), render = MEM32(0x002FB148u + 4u * 4u);
    uint32_t dt = DT_BITS, depth = N.step + 1 - f;
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp, esp = g_esp;
    double fp[8];
    int fp_top = g_fp_top;
    uint16_t fp_cw = g_fp_control_word;
    LARGE_INTEGER freq, t0, t1;
    double ms;
    unsigned loads_before;
    if (!N.snap[slot].valid || N.snap[slot].step != f || N.updates < N.snap[slot].update ||
        N.updates - N.snap[slot].update > ROLLBACK_HOLD_SAVES) {
        if (N.rb_failed++ < 5)
            fprintf(stderr, "[ROLLBACK] no save state for step %u (now %u): this match may go out of sync\n", f, N.step);
        N.final_step = f;
        return;
    }
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    memcpy(fp, g_fp_stack, sizeof(fp));
    n = N.updates - N.snap[slot].update;
    g_last_rb_target = f;
    g_last_rb_from = N.step;
    rollback_crash_cb = rollback_crash_state;
    loads_before = rollback_dynamic_loads();
    if (rollback_load(slot)) {      /* not loaded (too old for the held frees): as with no save */
        if (N.rb_failed++ < 5)
            fprintf(stderr, "[ROLLBACK] the save for step %u could not be loaded (now %u): this match may go out of sync\n",
                    f, N.step);
        N.final_step = f;
        return;
    }
    rollback_check_tables("right after a load");
    {   /* the steps run again record anew which entries they read (netplay_on_pad_read) */
        uint32_t t;
        int i;
        EnterCriticalSection(&g_cs);
        for (t = f - 1; t <= N.step + 1 && t + RING > N.step; t++)
            for (i = 0; i < MAXP; i++) N.used_tag[i][t % RING] = 0;
        LeaveCriticalSection(&g_cs);
    }
    if (N.rb_decided_step && f <= N.rb_decided_step) N.rb_decided_step = 0;    /* seen again after the re-run */
    N.step = f - 1;
    N.updates = N.snap[slot].update;
    defjam_test_set_step(N.snap[slot].test_step);
    memcpy(g_crowd_gen, N.snap[slot].crowd_gen, sizeof(g_crowd_gen));
    g_crowd_gen_on = N.snap[slot].crowd_gen_on;
    EnterCriticalSection(&g_cs);
    memcpy(N.seeds, N.snap[slot].seeds, sizeof(N.snap[slot].seeds));
    LeaveCriticalSection(&g_cs);
    N.resim = 1;
    rollback_set_resim(1);
    for (j = 0; j < n; j++) {
        /* not the save gone back to: an update without a fight step at the start of the re-run would
         * save it again, one update later (equivalent, but every later load of it then starts there) */
        if (j && N.step + 1 != f && !s_soak_wrong) snap_save();     /* tests: the wrong pass keeps none */
        guest_call(sub_000DD930, input, 1, &dt);
        guest_call(sub_00077040, machine, 1, &dt);
        if (g_eax & 0xFFu) {
            guest_call(sub_000BD860, em, 0, NULL);
            guest_call(sub_000BDF40, em, 0, NULL);
            guest_call(sub_0001A240, render, 1, &dt);
        }
        N.updates++;
    }
    N.resim = 0;
    rollback_set_resim(0);
    g_eax = eax; g_ecx = ecx; g_edx = edx; g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp; g_esp = esp;
    memcpy(g_fp_stack, fp, sizeof(fp));
    g_fp_top = fp_top;
    g_fp_control_word = fp_cw;
    QueryPerformanceCounter(&t1);
    ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
    N.rb_count++;
    N.rb_steps += depth;
    if (depth > N.rb_deepest) N.rb_deepest = depth;
    if (ms > N.rb_ms_max) N.rb_ms_max = ms;
    N.rbm.count++;
    N.rbm.steps += depth;
    if (depth > N.rbm.deepest) N.rbm.deepest = depth;
    if (ms > N.rbm.ms_max) N.rbm.ms_max = ms;
    N.rbm.ms_total += ms;
    if (ms > 30.0) {        /* a stutter: say how deep, and whether models were loaded in it */
        static unsigned told;
        N.rbm.slow++;
        if (told++ < 60)
            fprintf(stderr, "[ROLLBACK] slow: back %u step(s) in %.1f ms, %u model load(s) in it, step %u\n",
                    depth, ms, rollback_dynamic_loads() - loads_before, f);
    }
    N.rb_ms_total += ms;
    if (N.rb_count <= 10 || N.rbm.count <= 3 || N.rb_count % 200 == 0)
        fprintf(stderr, "[ROLLBACK] #%u: back to step %u, %u updates again in %.2f ms (now at step %u)\n",
                N.rb_count, f, n, ms, N.step);
}

void netplay_on_update(void)
{
    uint32_t t, f = 0;
    int i;
    rollback_check_tables(N.resim ? "a re-run update starts" : "an update starts");
    intro_update();
    intro_variants();
    koth_check_result(0);       /* also between the fight's steps: an ending can stop them */
    if (N.resim || !N.rb_on) return;
    /* before any save, load or the reference copy: the device holds no texture or palette. Only
     * while the fight runs (its saves and loads): around it the game loads and frees the venue's
     * textures itself, and releasing them under it is not needed. */
    if (MEM32(G_MATCH_PHASE) == 1u) rollback_unbind_textures();
    /* The fight is decided and every step ran on the real entries, or the match went on offline:
     * no more rollback in this match (write tracking off, no saves on the results screen). */
    if (MEM32(G_MATCH_PHASE) == 1u) N.rb_fought = 1;   /* a rematch starts with the last match's phase */
    if ((!N.lockstep && !soak_depth()) ||
        (N.lockstep && N.rb_fought && MEM32(G_MATCH_PHASE) >= 2u && N.final_step >= N.step && !N.predicting) ||
        (N.lockstep && N.rb_decided_step && N.final_step >= N.rb_decided_step)) {
        N.rb_on = 0;
        rollback_reset();
        fprintf(stderr, "[ROLLBACK] off for the rest of this match (step %u, %u rollbacks)\n", N.step, N.rb_count);
        rbm_summary(N.lockstep ? "decided" : "went on offline");
        return;
    }
    if (N.rb_ref_due) {
        N.rb_ref_due = 0;
        if (rollback_reference()) { N.rb_on = 0; return; }
        N.updates = 0;
        memset(N.snap, 0, sizeof(N.snap));
    }
    rollback_discover(MEM32(G_MATCH_PHASE) == 1u ? 16 : 4);
    /* which steps ran on the real entries; the first that ran on a wrong prediction */
    EnterCriticalSection(&g_cs);
    for (t = N.final_step + 1; t <= N.step; t++) {
        uint32_t k = t % RING;
        int all = 1, wrong = 0;
        if (t > (uint32_t)N.delay)
            for (i = 0; i < N.players; i++) {
                if (i == N.pos) continue;
                if (!(N.in_tag[i][k] == t + 1 && N.in_match[i][k] == N.match)) { all = 0; continue; }
                if (N.used_tag[i][k] == t + 1 && N.used_pred[i][k] &&
                    (N.used_mixed[i][k] || memcmp(N.used[i][k], N.in[i][k], ENTRY))) wrong = 1;
            }
        if (wrong) { f = t; break; }
        if (!all) break;
        N.final_step = t;
        {   /* RECOMP_NETPLAY_FINALLOG=1 (tests): each step as it becomes final, with the fields its checksum covers
             * (as its last run started it), to find the first step two PCs part at by diffing their logs */
            static int finallog = -1;
            if (finallog < 0) finallog = getenv("RECOMP_NETPLAY_FINALLOG") != NULL;
            if (finallog && N.hash_tag[k] == t + 1) {
                char line[300];
                int n = snprintf(line, sizeof(line), "[FINAL] %08X %u runs %u reads %u pred %u%u |", N.match, t, N.step_runs[k],
                                 N.step_reads[k], N.used_pred[0][k], N.used_pred[1][k]), sl, fi;
                for (sl = 0; sl < 2; sl++)
                    for (fi = 0; fi < NETCHECK_FIELDS; fi++)
                        n += snprintf(line + n, sizeof(line) - (size_t)n, " %08X", N.hash_fields[k][sl][fi]);
                fprintf(stderr, "%s\n", line);
            }
        }
    }
    LeaveCriticalSection(&g_cs);
    if (f) rollback_to(f);
    if (!N.lockstep && soak_depth()) {      /* tests: go back and compare */
        static unsigned soaks, same, differ;
        long long t = defjam_test_current_step();
        int d = 2 + (int)((N.updates / 5) % (uint32_t)(soak_depth() - 1));
        if (t > 40 && N.step > (uint32_t)d + 2 && N.updates % 5 == 0) {
            unsigned long long before[16];
            int k;
            static int twice = -1;      /* RECOMP_ROLLBACK_SOAK_TWICE=1: a second re-run, compared with the first */
            static int wrong = -1;      /* RECOMP_ROLLBACK_SOAK_WRONG=1: between them, one on wrong buttons */
            if (twice < 0) {
                wrong = getenv("RECOMP_ROLLBACK_SOAK_WRONG") != NULL;
                twice = wrong || getenv("RECOMP_ROLLBACK_SOAK_TWICE") != NULL;
            }
            /* the step gone back to, the same for every pass: a pass on wrong buttons can end at another
             * step (a move that stops the fight steps for a while), and the next pass must still go back
             * to this one to be compared (2026-11-02: the carrier report showed two different saves) */
            uint32_t f0 = N.step + 1 - (uint32_t)d;
            for (k = 0; k < d; k++) before[k] = defjam_test_step_hash(t - d + k);
            if (rollback_soak_diff_on() && !twice) rollback_soak_before();
            if (wrong && rollback_soak_diff_on()) rollback_soak_phase(1);
            rollback_to(f0);
            rollback_soak_phase(0);
            if (rollback_soak_diff_on() && !twice) rollback_soak_after(d, t, 0);
            if (twice) {
                /* Both re-runs draw nothing, so what the first pass's renders wrote is no difference here:
                 * a step or a word that differs between them is state the load did not put back, or a
                 * read of something host-timed (rollback-plan-2026-11.md step 3). */
                static unsigned twice_same, twice_differ;
                unsigned long long first[16];
                for (k = 0; k < d; k++) first[k] = defjam_test_step_hash(t - d + k);
                if (rollback_soak_diff_on()) rollback_soak_before();
                if (wrong) {    /* what a wrong prediction leaves behind that the load does not take back */
                    s_soak_wrong = N.step + 1 - (uint32_t)d;
                    rollback_to(f0);
                    s_soak_wrong = 0;
                }
                if (wrong && rollback_soak_diff_on()) rollback_soak_phase(3);
                rollback_to(f0);
                rollback_soak_phase(0);
                {
                    int differ_here = 0;
                    for (k = 0; k < d; k++) {
                        unsigned long long again = defjam_test_step_hash(t - d + k);
                        if (!first[k] || !again) continue;
                        if (first[k] == again) twice_same++;
                        else {
                            differ_here = 1;
                            if (twice_differ++ < 20)
                                fprintf(stderr, "[ROLLBACK] soak #%u: step %lld came out otherwise in the second re-run (back %d)\n",
                                        soaks + 1, t - d + k, d);
                        }
                    }
                    /* every saved word that differs, when a step did (the first such soaks) */
                    if (rollback_soak_diff_on()) rollback_soak_after(d, t, differ_here);
                    if (differ_here && wrong) rollback_soak_carriers(t);
                }
                if ((soaks + 1) % 100 == 0)
                    fprintf(stderr, "[ROLLBACK] soak, two re-runs: %u steps the same, %u otherwise\n", twice_same, twice_differ);
            }
            soaks++;
            for (k = 0; k < d; k++) {       /* k = 0: the state the load put back */
                unsigned long long after = defjam_test_step_hash(t - d + k);
                if (!before[k] || !after) continue;
                if (before[k] == after) same++;
                else if (differ++ < 20)
                    fprintf(stderr, "[ROLLBACK] soak #%u: step %lld came out otherwise after going back %d "
                            "(%016llX, first %016llX)\n", soaks, t - d + k, d, after, before[k]);
            }
            if (soaks % 100 == 0)
                fprintf(stderr, "[ROLLBACK] soak: %u times back, %u steps the same, %u otherwise\n", soaks, same, differ);
        }
    }
    send_hashes(N.final_step + 1 < N.step ? N.final_step + 1 : N.step);
    snap_save();
    N.updates++;
    if (N.updates % 1800 == 0 && N.rb_count) {
        unsigned kb, changing, skipped;
        rollback_stats(&kb, &changing, &skipped);
        fprintf(stderr, "[ROLLBACK] %u rollbacks, %u steps run again, deepest %u, %.2f ms average, %.2f ms longest; "
                "save %u KB (%u heap pages change)\n", N.rb_count, N.rb_steps, N.rb_deepest,
                N.rb_ms_total / N.rb_count, N.rb_ms_max, kb, changing);
    }
}

void netplay_on_seed(uint32_t esp, int words, uint32_t which)
{
    int i, slot = -1;
    uint32_t count;
    if (!N.lockstep || N.seed_bypass) return;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < 32; i++)
        if (N.seeds[i].which == which || !N.seeds[i].which) { slot = i; break; }
    if (slot < 0) slot = (int)(which % 32);
    N.seeds[slot].which = which;
    count = N.seeds[slot].count++;
    LeaveCriticalSection(&g_cs);
    for (i = 0; i < words; i++) {
        uint32_t v = mix32(N.session ^ mix32(which * 0x9E3779B9u + count * 0x85EBCA6Bu + (uint32_t)i));
        if (i == 1) v |= 1;     /* an additive term stays odd */
        MEM32(esp + 4 + 4 * (uint32_t)i) = v;
    }
}

/* Discord's presence (netplay.h): the room match as START set it up. Reads only. */
int netplay_match_info(NetVersus *out)
{
    int i, live;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    if (!N.lobby || !out) return 0;
    EnterCriticalSection(&g_cs);
    live = N.match_live && !N.start_deferred && N.state == NETPLAY_CONNECTED;
    if (live) {
        memset(out, 0, sizeof(*out));
        out->count = N.report_m.count < 2 ? 2 : N.report_m.count > 4 ? 4 : N.report_m.count;
        for (i = 0; i < out->count; i++) {
            int slot = N.start_slots[i];
            uint8_t ok[CP_REC_LEN];
            out->fighter[i] = N.report_m.fighter[i];
            out->cpu[i] = i >= N.players || N.report_m.pad[i] < 0;
            out->ping[i] = -1;
            if (!out->cpu[i]) snprintf(out->name[i], sizeof(out->name[i]), "%s", N.report_names[i]);
            if (out->fighter[i] == CP_FIGHTER_ID && !out->cpu[i] && slot >= 0 && slot < ROOMSLOTS &&
                N.cp_start_crc[slot] && N.cp_have[slot] && cp_record_crc(N.cp_rec[slot]) == N.cp_start_crc[slot] &&
                cp_record_sanitize(N.cp_rec[slot], ok))
                cp_record_name(ok, out->cp_name[i], sizeof(out->cp_name[i]));
        }
        out->you = N.pos;
        out->type = N.report_m.type;
        out->venue = N.report_m.venue;
        out->room_mode = N.roomv.settings.mode;
        out->king = N.koth_match ? N.vs_king : -1;
        out->streak = N.vs_streak;
        snprintf(out->room, sizeof(out->room), "%s", N.roomv.settings.name);
        out->ms = (int)(GetTickCount64() - N.start_tick);
        out->loaded = N.match_seen;
        out->alpha = 1.0f;
    }
    LeaveCriticalSection(&g_cs);
    return live;
}

#endif
