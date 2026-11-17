/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Direct connection (P2P, opt-in; docs/research/p2p-optin.md): the wire forms, pure functions (no sockets,
 * no game memory), so tests/unit/test_netplay_p2p_native.py compiles them on their own.
 *
 * The relay stays the path every fight packet takes. With both players' consent ([online] p2p_direct on both
 * PCs) and the relay's switch on (CAPS bit 27), the relay tells each of the two players of a 1v1 match the
 * other one's public address and a match key (T_P2P); the games punch through their NATs with probes and,
 * once a probe is answered, send their inputs (P_STEPS) and live pads (P_LIVEPAD) a second time, directly.
 * The receiver takes whichever copy comes first (both are the same bytes). When the direct path stops
 * answering, nothing changes for the fight: the relay's copies were there all along. */
#ifndef DEFJAM_NETPLAY_P2P_H
#define DEFJAM_NETPLAY_P2P_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NETP2P_CAP        (1u << 27)  /* CAPS: client: it can; relay: its switch (p2p-allowed) is on */
#define NETP2P_T_ASK      0x3A        /* client <-> relay: P2P */
#define NETP2P_T_DIRECT   0x3B        /* game <-> game only (the relay never sees it) */
#define NETP2P_ASK_LEN    34          /* header 6, token 8, u32 seed, 16 zero bytes (never smaller than the answer) */
#define NETP2P_KEY_LEN    8
#define NETP2P_HEAD_LEN   (6 + NETP2P_KEY_LEN + 1)  /* header, key, kind */

/* the relay's answer status */
#define NETP2P_NO   0   /* not for this match (switch off, not 1v1, the other game cannot, not IPv4) */
#define NETP2P_OK   1   /* the other player's endpoint and the key follow */
#define NETP2P_WAIT 2   /* the other player has not asked (yet): its setting may be off */

/* direct packet kinds */
#define NETP2P_PROBE 1  /* u32 sequence, u32 the sender's clock in ms */
#define NETP2P_ACK   2  /* the probe's u32 sequence and u32 clock, echoed */
#define NETP2P_DATA  3  /* a DATA payload (P_STEPS, P_LIVEPAD) */

typedef struct NetP2PAnswer {
    uint32_t seed;
    int status;         /* NETP2P_NO, _OK, _WAIT */
    int slot;           /* NETP2P_OK: the other player's room slot */
    uint8_t ip[4];      /* NETP2P_OK: its IPv4 address, network order */
    uint16_t port;      /* NETP2P_OK: its port (host order) */
    uint8_t key[NETP2P_KEY_LEN];
} NetP2PAnswer;

/* The request: NETP2P_ASK_LEN bytes into b. */
int netp2p_ask(uint8_t *b, const uint8_t token[8], uint32_t seed);

/* The relay's answer (a whole datagram, header included). 1: parsed into *out; 0: not a P2P answer. */
int netp2p_answer(const uint8_t *b, int len, NetP2PAnswer *out);

/* A direct packet of `kind` with `n` body bytes into b (cap bytes). Returns its length, 0 if it does not fit. */
int netp2p_packet(uint8_t *b, int cap, const uint8_t key[NETP2P_KEY_LEN], int kind, const uint8_t *body, int n);

/* A direct packet with this key: 1 and its kind and body; 0 for anything else (another key, another type). */
int netp2p_parse(const uint8_t *b, int len, const uint8_t key[NETP2P_KEY_LEN], int *kind, const uint8_t **body, int *n);

/* Whether a DATA payload starting with this byte also goes on the direct path: the fight's inputs (P_STEPS 4)
 * and the live pads before the first step (P_LIVEPAD 3). Both are idempotent at the receiver (by step, by
 * sequence), so a second copy changes nothing. Everything else (voice, P_RTT, P_HASH, rematches, P_GONE) stays
 * on the relay only, which also serves spectators and the recordings. */
int netp2p_direct_payload(uint8_t first);

#ifdef __cplusplus
}
#endif

#endif
