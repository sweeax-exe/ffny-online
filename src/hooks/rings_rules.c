/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* RINGS (0x46) on the wire and the ring types' names; see rings_rules.h. */
#include "rings_rules.h"

#include <stdio.h>
#include <string.h>

int rings_build_request(uint8_t *out, int size, uint32_t nonce, const uint8_t print[8], uint32_t caps)
{
    int len = size < RINGS_REQ_LEN ? size : RINGS_REQ_LEN;
    if (!out || len < 22) return 0;
    memset(out, 0, (size_t)len);
    out[0] = 'D'; out[1] = 'J'; out[2] = 'N'; out[3] = 'P'; out[4] = 1; out[5] = RINGS_TYPE;
    out[6] = (uint8_t)nonce; out[7] = (uint8_t)(nonce >> 8); out[8] = (uint8_t)(nonce >> 16); out[9] = (uint8_t)(nonce >> 24);
    if (print) memcpy(out + 10, print, 8);
    out[18] = (uint8_t)caps; out[19] = (uint8_t)(caps >> 8); out[20] = (uint8_t)(caps >> 16); out[21] = (uint8_t)(caps >> 24);
    return len;
}

/* A u8-length string at b[*at] into out (cut to its size - 1); 0 when it runs past n. */
static int take_str(const uint8_t *b, int n, int *at, char *out, int size)
{
    int len;
    if (*at + 1 > n) return 0;
    len = b[(*at)++];
    if (*at + len > n) return 0;
    snprintf(out, (size_t)size, "%.*s", len > size - 1 ? size - 1 : len, (const char *)(b + *at));
    *at += len;
    return 1;
}

int rings_parse_answer(const uint8_t *b, int n, uint32_t *nonce, NetRingCard *out, int max)
{
    int count, k, at;
    if (!b || n < 11 || memcmp(b, "DJNP", 4) || b[4] != 1 || b[5] != RINGS_TYPE) return -1;
    if (nonce) *nonce = (uint32_t)b[6] | (uint32_t)b[7] << 8 | (uint32_t)b[8] << 16 | (uint32_t)b[9] << 24;
    count = b[10];
    at = 11;
    for (k = 0; k < count; k++) {
        NetRingCard c;
        int v;
        memset(&c, 0, sizeof(c));
        if (at + 9 > n) return -1;
        c.ring = b[at];
        c.type = b[at + 1];
        c.channel = b[at + 2];
        memcpy(c.code, b + at + 3, 6);
        c.code[6] = 0;
        at += 9;
        if (!take_str(b, n, &at, c.name, sizeof(c.name))) return -1;
        if (at + 7 > n) return -1;
        c.flags = b[at];
        c.members = b[at + 1];
        c.line = b[at + 2];
        c.watching = b[at + 3];
        c.phase = b[at + 4];
        c.king_streak = b[at + 5] | b[at + 6] << 8;
        at += 7;
        if (!take_str(b, n, &at, c.king, sizeof(c.king))) return -1;
        if (at + 3 > n) return -1;
        c.fights = b[at] | b[at + 1] << 8;
        c.venue_count = b[at + 2];
        at += 3;
        if (at + c.venue_count + 1 > n) return -1;
        for (v = 0; v < c.venue_count; v++)
            if (v < RINGS_VENUES) c.venues[v] = b[at + v];
        at += c.venue_count;
        if (c.venue_count > RINGS_VENUES) c.venue_count = RINGS_VENUES;
        c.rounds = b[at++];
        if (out && k < max) out[k] = c;
    }
    return count < max ? count : max;
}

static const char *const k_ids[RING_TYPES + 1] = { NULL, "cage", "boxing", "pit", "ringout" };
static const char *const k_titles[RING_TYPES + 1] = { "", "King of the Cage", "King of the Ring", "King of the Pit",
                                                      "King of the Ring-out" };
static const char *const k_kings[RING_TYPES + 1] = { "", "Cage King", "Ring Champion", "Pit Boss", "Ring-out King" };

const char *ring_type_id(int type) { return type >= 1 && type <= RING_TYPES ? k_ids[type] : NULL; }
const char *ring_type_title(int type) { return type >= 1 && type <= RING_TYPES ? k_titles[type] : ""; }
const char *ring_type_king(int type) { return type >= 1 && type <= RING_TYPES ? k_kings[type] : ""; }

int ring_type_of_id(const char *id)
{
    int i;
    if (!id) return RING_NONE;
    for (i = 1; i <= RING_TYPES; i++)
        if (!strcmp(id, k_ids[i])) return i;
    return RING_NONE;
}

int ring_room_mode(const NetRingCard *c) { return c && (c->flags & RING_FLAG_VOTE) ? 0x40 : 0x41; }
