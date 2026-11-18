/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* F0, the Fight Complex and King of the Ring v1 (community/fight-complex-f0-protocol.md): the wire of RINGS (0x46),
 * one relay's list of its rings, and the ring types' names, as plain functions without sockets or Windows
 * (rings_rules.c), so a unit test builds them alone (tests/unit/test_rings_native.py).
 *
 * A ring is an official King of the Hill room the relay makes from its config: always open, never listed in
 * ROOMS, ROOMLIST or QUICK, joined by its code. The game asks every relay it knows (no session, like ROOMLIST)
 * and merges the cards; a relay without F0 drops RINGS silently (no answer: no rings there). */
#ifndef DEFJAM_RINGS_RULES_H
#define DEFJAM_RINGS_RULES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RINGS_TYPE      0x46    /* both ways, the header's type byte */
#define RINGS_REQ_LEN   1200    /* the request is padded to this: the relay never answers with more bytes */
#define RINGS_MAX       24      /* cards one relay's answer may carry (rings x channels) */
#define RINGS_VENUES    8

/* the ring's type (RINGS `type`; the report's and the board's ids) */
#define RING_NONE       0
#define RING_CAGE       1
#define RING_BOXING     2
#define RING_PIT        3
#define RING_RINGOUT    4
#define RING_TYPES      4

/* RINGS `flags` */
#define RING_FLAG_FREE  1       /* the free instance: empty, bound to no build */
#define RING_FLAG_FULL  2
#define RING_FLAG_VOTE  4       /* the fighters vote on the venue (ModeKoth); else drawn (ModeKothRandom) */

typedef struct NetRingCard {
    int ring;                   /* the ring's stable number on its relay (config order, 1..) */
    int type;                   /* RING_* */
    int channel;                /* 1.. */
    char code[7];               /* the room's code for JOIN */
    char name[25];              /* "The Cage", "The Cage 2" */
    int flags;                  /* RING_FLAG_* */
    int members, line, watching;    /* everyone in the room; not away (the queue, fighters too); away (watch only) */
    int phase;                  /* the HILL phase: 0 waiting, 1 calling, 2 voting, 3 fighting */
    int king_streak;            /* the room king's wins in a row (the room's, not the counted streak) */
    char king[25];              /* the room king's lobby name, "" none */
    int fights;                 /* fights finished in this instance */
    int venue_count;
    unsigned char venues[RINGS_VENUES];
    int rounds;                 /* the room's rounds byte (0: the game's default) */
} NetRingCard;

/* The request: header ('DJNP', 1, RINGS), nonce, the asker's build fingerprint, its CAPS word, zeros to
 * RINGS_REQ_LEN (or `size` when smaller). Returns the bytes written, 0 when `size` is too small (< 22). */
int rings_build_request(uint8_t *out, int size, uint32_t nonce, const uint8_t print[8], uint32_t caps);
/* One answer of n bytes (the header included): *nonce and up to `max` cards into out. Returns the card count
 * (0: the relay has no ring for this build right now), -1 when it is not a RINGS answer or is cut short. The
 * zero padding after the last card is ignored. */
int rings_parse_answer(const uint8_t *b, int n, uint32_t *nonce, NetRingCard *out, int max);

/* "cage", "boxing", "pit", "ringout" (the report's `ring` and the board's `type`); NULL for any other type. */
const char *ring_type_id(int type);
/* RING_* of such an id, RING_NONE for any other text (NULL too). */
int ring_type_of_id(const char *id);
/* What players read when the board did not say: "King of the Cage", "King of the Ring", "King of the Pit",
 * "King of the Ring-out"; and the crowned king's title: "Cage King", "Ring Champion", "Pit Boss", "Ring-out King".
 * "" for another type. */
const char *ring_type_title(int type);
const char *ring_type_king(int type);
/* The room mode JOIN goes to: NETPLAY_MODE_KOTH (0x40) when the card says the fighters vote, else 0x41. */
int ring_room_mode(const NetRingCard *c);

#ifdef __cplusplus
}
#endif

#endif
