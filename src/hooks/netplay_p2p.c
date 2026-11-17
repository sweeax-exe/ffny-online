/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Direct connection (P2P, opt-in): the wire forms. See netplay_p2p.h and docs/research/p2p-optin.md. */
#include "netplay_p2p.h"

#include <string.h>

static void header(uint8_t *b, uint8_t type)
{
    b[0] = 'D'; b[1] = 'J'; b[2] = 'N'; b[3] = 'P'; b[4] = 1; b[5] = type;
}

int netp2p_ask(uint8_t *b, const uint8_t token[8], uint32_t seed)
{
    header(b, NETP2P_T_ASK);
    memcpy(b + 6, token, 8);
    b[14] = (uint8_t)seed; b[15] = (uint8_t)(seed >> 8); b[16] = (uint8_t)(seed >> 16); b[17] = (uint8_t)(seed >> 24);
    memset(b + 18, 0, NETP2P_ASK_LEN - 18);
    return NETP2P_ASK_LEN;
}

int netp2p_answer(const uint8_t *b, int len, NetP2PAnswer *out)
{
    if (!b || !out || len < 11 || memcmp(b, "DJNP", 4) || b[4] != 1 || b[5] != NETP2P_T_ASK) return 0;
    memset(out, 0, sizeof(*out));
    out->seed = b[6] | (b[7] << 8) | (b[8] << 16) | ((uint32_t)b[9] << 24);
    out->status = b[10];
    if (out->status == NETP2P_OK) {
        if (len != 11 + 1 + 4 + 2 + NETP2P_KEY_LEN) return 0;
        out->slot = b[11];
        memcpy(out->ip, b + 12, 4);
        out->port = (uint16_t)(b[16] | (b[17] << 8));
        memcpy(out->key, b + 18, NETP2P_KEY_LEN);
        if (!out->port) return 0;
    } else if (out->status != NETP2P_NO && out->status != NETP2P_WAIT) {
        return 0;
    }
    return 1;
}

int netp2p_packet(uint8_t *b, int cap, const uint8_t key[NETP2P_KEY_LEN], int kind, const uint8_t *body, int n)
{
    if (!b || n < 0 || NETP2P_HEAD_LEN + n > cap) return 0;
    header(b, NETP2P_T_DIRECT);
    memcpy(b + 6, key, NETP2P_KEY_LEN);
    b[6 + NETP2P_KEY_LEN] = (uint8_t)kind;
    if (n) memcpy(b + NETP2P_HEAD_LEN, body, (size_t)n);
    return NETP2P_HEAD_LEN + n;
}

int netp2p_parse(const uint8_t *b, int len, const uint8_t key[NETP2P_KEY_LEN], int *kind, const uint8_t **body, int *n)
{
    int i;
    uint8_t diff = 0;
    if (!b || len < NETP2P_HEAD_LEN || memcmp(b, "DJNP", 4) || b[4] != 1 || b[5] != NETP2P_T_DIRECT) return 0;
    for (i = 0; i < NETP2P_KEY_LEN; i++) diff |= (uint8_t)(b[6 + i] ^ key[i]);
    if (diff) return 0;
    *kind = b[6 + NETP2P_KEY_LEN];
    *body = b + NETP2P_HEAD_LEN;
    *n = len - NETP2P_HEAD_LEN;
    return 1;
}

int netp2p_direct_payload(uint8_t first)
{
    return first == 3 || first == 4;     /* P_LIVEPAD, P_STEPS (netplay.c) */
}
