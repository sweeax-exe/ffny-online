/* Draft mode's pick and ban on the game's side (ranked phase 2b; docs/netplay-protocol.md "Ranked
 * queue", relay netplay/relay/draft.go). The relay is the authority; this is pure C with no Windows
 * or game dependencies (tests/unit/test_netplay_draft_native.py compiles it natively):
 *   - DRAFT (0x2D) decoded, DRAFTACT (0x2E) encoded after its token,
 *   - the rules the pages draw from: whose turn, which action, each fighter tile's and venue
 *     card's state, the turn banner, each player's three-ban strip,
 *   - the tests' automatic choice (RECOMP_TEST_DRAFT=auto). */
#ifndef DEFJAM_NETPLAY_DRAFT_RULES_H
#define DEFJAM_NETPLAY_DRAFT_RULES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {      /* DRAFT steps */
    NETDRAFT_BAN = 1, NETDRAFT_STRIKE = 2, NETDRAFT_BLIND = 3, NETDRAFT_VENUE_BAN = 4, NETDRAFT_VENUE_PICK = 5,
    NETDRAFT_WINNER_PICK = 6, NETDRAFT_LOSER_PICK = 7, NETDRAFT_PLAYING = 8, NETDRAFT_DONE = 9
};
enum {      /* DRAFTACT actions */
    NETDRAFT_ACT_BAN = 1, NETDRAFT_ACT_STRIKE = 2, NETDRAFT_ACT_PICK = 3, NETDRAFT_ACT_VENUE = 4, NETDRAFT_ACT_CURSOR = 5
};
enum {      /* a fighter tile, as seen by one player */
    NETDRAFT_TILE_AVAILABLE = 0,
    NETDRAFT_TILE_BANNED = 1,       /* a red X: nobody plays it this set */
    NETDRAFT_TILE_LOCKED = 2,       /* a padlock: you won a game with it (Fearless: picked before) */
    NETDRAFT_TILE_MINE = 3,         /* your pick (gold) */
    NETDRAFT_TILE_THEIRS = 4        /* the opponent's visible pick */
};
enum {      /* a venue card */
    NETDRAFT_VENUE_OPEN = 0, NETDRAFT_VENUE_STRUCK = 1, NETDRAFT_VENUE_BANNED = 2, NETDRAFT_VENUE_CHOSEN = 3
};

#define NETDRAFT_NONE 0xFF      /* no fighter, hidden, a skipped ban, no slot */
#define NETDRAFT_BANS 6
#define NETDRAFT_MAX 32

typedef struct NetDraft {
    int valid;
    int step, turn, secs, game, venue, first;   /* first: A, the slot that bans first */
    int ban_count, bans[NETDRAFT_BANS];          /* by turn; NETDRAFT_NONE a skipped one */
    int struck_count, struck[NETDRAFT_MAX];
    int venue_ban_count, venue_bans[NETDRAFT_MAX];
    int picks[2], locked[2], cursors[2];         /* by slot; NETDRAFT_NONE none or hidden */
    int option_count, options[NETDRAFT_MAX];     /* this game's venues */
    int lock_count[2], locks[2][NETDRAFT_MAX];   /* by slot: the fighters it may not pick (won with) */
    int afk;                                     /* the AFK slot, NETDRAFT_NONE none */
} NetDraft;

/* DRAFT's body. Returns 1 when it parsed (out->valid). */
int netdraft_decode(const uint8_t *b, int len, NetDraft *out);
/* DRAFTACT's three bytes after the token. Returns 3. */
int netdraft_encode_act(uint8_t *out, int step, int action, int id);

/* 1 in a step whose clock runs (bans .. the loser's pick). */
int netdraft_active(const NetDraft *d);
/* 1 in a fighter step (bans and picks), 0 in a venue step. */
int netdraft_fighter_step(int step);
/* The DRAFTACT action of the current step (ban, strike, pick, venue), 0 none. */
int netdraft_action(const NetDraft *d);
/* 1 when slot `you` acts now (the blind pick: until locked). */
int netdraft_my_turn(const NetDraft *d, int you);
/* The slot whose ban the i-th is (A B B A A B from first). */
int netdraft_ban_owner(const NetDraft *d, int i);
/* Slot's bans in order into out[3] (NETDRAFT_NONE skipped); returns how many were made so far. */
int netdraft_bans_of(const NetDraft *d, int slot, int out[3]);
int netdraft_banned(const NetDraft *d, int fighter);
/* Whether slot `you` may take fighter now (not banned, not locked for you; Draft never plays 99). */
int netdraft_fighter_legal(const NetDraft *d, int you, int fighter);
/* A fighter tile's state for slot `you` (NETDRAFT_TILE_*). */
int netdraft_fighter_state(const NetDraft *d, int you, int fighter);
/* A venue card's state (NETDRAFT_VENUE_*); legal: whether it may be struck, banned or picked now. */
int netdraft_venue_state(const NetDraft *d, int venue);
int netdraft_venue_legal(const NetDraft *d, int venue);
/* The turn banner for slot `you`: "YOUR BAN 2/3", "OPPONENT BANNING 1/3", "STRIKE A VENUE 1/2",
 * "PICK YOUR FIGHTER", "OPPONENT PICKING", "BOTH PICKED", ... */
void netdraft_banner(const NetDraft *d, int you, char *out, int size);
/* A short line under the banner: what the step means. */
const char *netdraft_hint(const NetDraft *d, int you);

/* The tests' choice for slot `you` when it is their turn (RECOMP_TEST_DRAFT=auto): a legal fighter
 * of fighters[] (the grid's) or venue, picked from `seed`. Returns 1 and fills action/id, 0 when
 * there is nothing to do. */
int netdraft_auto(const NetDraft *d, int you, const int *fighters, int n, unsigned seed, int *action, int *id);

#ifdef __cplusplus
}
#endif

#endif
