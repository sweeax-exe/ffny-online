/* Ranked play, the game's side of the wire and the rank ladder (ranked phase 2a;
 * docs/netplay-protocol.md "Ranked queue", server/docs/ranked-design.md). Pure C with no Windows or game
 * dependencies, so tests/unit/test_netplay_ranked_native.py compiles it natively:
 *   - the long RANKED form (modes, primary, rollback flag, pings to the three relays, a MOVE's code),
 *   - the long QUEUE (searching counts per mode, ACCEPT's mode/seconds/opponent rating, MOVE's code
 *     and host relay address),
 *   - RANKINFO (both players' ranks in the set's mode and the set's score),
 *   - the stats text's ranked_<mode>= lines (MY STATS, the results page),
 *   - the ladder (the account service's accounts/ranks.go): divisions, names, the bar to the next. */
#ifndef DEFJAM_NETPLAY_RANKED_H
#define DEFJAM_NETPLAY_RANKED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NETRANK_OPEN = 0, NETRANK_CLASSIC = 1, NETRANK_DRAFT = 2, NETRANK_MODES = 3 };

#define NETRANK_EXT_LEN     14      /* what the long RANKED adds to the 25-byte one */
#define NETRANK_CODE_LEN    6
#define NETRANK_PLACEMENT   0xFF    /* RANKINFO division: still placing */
#define NETRANK_DIVISIONS   22      /* Street Rookie III .. Def Jam Legend */
#define NETRANK_SET_RUNNING 0xFF    /* RANKINFO winner */
#define NETRANK_SET_LEVEL   0xFE

/* "open", "classic", "draft"; NULL out of range. */
const char *netranked_mode_id(int mode);
/* "OPEN", "CLASSIC", "DRAFT". */
const char *netranked_mode_title(int mode);
/* The mode a stats key names ("ranked_open" -> NETRANK_OPEN), -1 none. */
int netranked_mode_of_key(const char *key);

/* The long RANKED's 14 bytes: modes (bit per mode), flags (bit 0 rollback, bits 4-5 the primary),
 * pings (ms to EU, NA, West; <= 0 unknown, capped at 65535) and code (6 bytes, NULL zeros).
 * The primary is always among the modes. Returns NETRANK_EXT_LEN. */
int netranked_encode_ext(uint8_t *out, unsigned modes, int primary, int rollback, const int pings[3], const char *code);

/* The long QUEUE after its five short bytes (state, waited, searching). */
typedef struct NetQueueExt {
    int valid;
    int counts[NETRANK_MODES];  /* searching per mode */
    int mode;                   /* the found set's mode, -1 none */
    int accept_secs;            /* QUEUE 5: seconds left to accept */
    int opp_rating;             /* QUEUE 5: the opponent's rating */
    char code[NETRANK_CODE_LEN + 1];    /* QUEUE 6: the reservation code */
    char addr[65];              /* QUEUE 6: the host relay "host:port" */
} NetQueueExt;
/* Returns 1 and fills out from b (len bytes), 0 when it is short or malformed (out->valid 0). */
int netranked_decode_queue_ext(const uint8_t *b, int len, NetQueueExt *out);

typedef struct NetRankPlayer {
    int slot;
    int division;           /* 0..21, NETRANK_PLACEMENT */
    int placements;         /* placement sets left */
    int rating;
    int set_wins;
    int provisional;
} NetRankPlayer;

typedef struct NetRankInfo {
    int valid;
    int mode;               /* NETRANK_* */
    int count;
    NetRankPlayer p[4];
    int has_set;
    int game;               /* the running or next game (from 1); the last once over */
    int picker;             /* the slot picking the next venue, -1 both */
    int winner;             /* NETRANK_SET_RUNNING, a slot, NETRANK_SET_LEVEL */
} NetRankInfo;
/* RANKINFO's body. Returns 1 when it parsed. */
int netranked_decode_rankinfo(const uint8_t *b, int len, NetRankInfo *out);
/* The player in slot, NULL none. */
const NetRankPlayer *netranked_player(const NetRankInfo *ri, int slot);

/* One ranked_<mode>= value: rating|RD|division|placements left|placements of|provisional|to next|
 * set wins|set losses|label[|peak|peak rank|hint]. */
typedef struct NetModeRank {
    int valid;
    int rating, deviation, division, placements_left, placements_of, provisional, to_next;
    int set_wins, set_losses, peak;
    char label[48], peak_rank[32];
    int hint;               /* NETRANK_HINT_* */
} NetModeRank;
enum { NETRANK_HINT_NONE = 0, NETRANK_HINT_PROMOTION = 1, NETRANK_HINT_DEMOTION = 2 };
/* Returns 1 when the value had at least the ten original fields. */
int netranked_parse_line(const char *value, NetModeRank *out);

/* The ladder. */
int netranked_division_of(int rating);
/* The rating where division d starts (Street Rookie III: -100000). */
int netranked_division_min(int d);
/* 0..7 (Street Rookie .. Def Jam Legend). */
int netranked_tier_of(int d);
const char *netranked_tier_name(int tier);
/* "Kingpin II", "Def Jam Legend"; "PLACEMENT" for NETRANK_PLACEMENT. */
void netranked_division_name(int d, char *out, int size);
/* How far rating is through division d toward the next, 0..100 (100 at Legend). */
int netranked_division_percent(int rating, int d);
/* The label a RANKINFO player shows: "PLACEMENT 2/5", "Kingpin II", "Hustler I ?". */
void netranked_player_label(const NetRankPlayer *p, int placements_of, char *out, int size);

#ifdef __cplusplus
}
#endif

#endif
