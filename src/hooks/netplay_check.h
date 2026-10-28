/* The lockstep checksum message (P_HASH, netplay.c) and what a desync report says about it.
 * Pure functions, no guest memory: tests/unit/test_netplay_check_native.py builds them alone.
 *
 * The payload grew twice; every build reads the parts it knows and ignores the rest, so a longer
 * message from a newer build is still compared by an older one:
 *   13 bytes  (builds before 23)  u8 P_HASH, u32 match, u32 step, u32 hash
 *   37 bytes  (builds 23..33)     + NETCHECK_PARTS words: fighters 1-4, the CPU fighters' generators,
 *                                   the shared generator and time scale
 *   145 bytes (build 34 on)       + NETCHECK_EXTRA words, diagnostic only: per fighter slot 1-4 the
 *                                   six raw fields the hash covers, the hashes of the two pre-fight
 *                                   CLogic generators (0x376FDC, 0x375F68), the crowd hash
 * Only `hash` decides whether two PCs are in step; the rest says where they part. */
#ifndef NETPLAY_CHECK_H
#define NETPLAY_CHECK_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NETCHECK_PARTS    6
#define NETCHECK_SLOTS    4
#define NETCHECK_FIELDS   6
#define NETCHECK_GENS     2
#define NETCHECK_EXTRA    (NETCHECK_SLOTS * NETCHECK_FIELDS + NETCHECK_GENS + 1)
#define NETCHECK_LEN_OLD  13
#define NETCHECK_LEN_PARTS (NETCHECK_LEN_OLD + 4 * NETCHECK_PARTS)
#define NETCHECK_LEN_FULL (NETCHECK_LEN_PARTS + 4 * NETCHECK_EXTRA)

/* The fighter record's words the checksum covers (ACTORS + slot * STRIDE + field) */
static const uint32_t netcheck_field[NETCHECK_FIELDS] = { 0x30, 0x44, 0x48, 0x4C, 0x54, 0x350 };
/* The pre-fight CLogic generators (docs/research/fight-determinism.md), 0x88 bytes each */
static const uint32_t netcheck_gen[NETCHECK_GENS] = { 0x00376FDCu, 0x00375F68u };

typedef struct NetCheck {
    uint32_t match, step, hash;     /* step 0: empty, or already compared */
    uint32_t parts[NETCHECK_PARTS];
    uint32_t fields[NETCHECK_SLOTS][NETCHECK_FIELDS];
    uint32_t gens[NETCHECK_GENS];
    uint32_t crowd;
    int has_parts, has_extra;       /* what the sender's build sent */
} NetCheck;

static void netcheck_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t netcheck_get32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* FNV-1a over `count` words, as the checksum hashes them. */
static uint32_t netcheck_fnv_words(const uint32_t *w, unsigned count)
{
    uint32_t h = 2166136261u;
    unsigned i;
    for (i = 0; i < count; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

/* The message, type byte first (p[0] = `type`), as long as `c` has parts for (13, 37 or 145
 * bytes). Returns its length, 0 if `size` is too small. */
static int netcheck_pack(uint8_t *p, int size, uint8_t type, const NetCheck *c)
{
    int n = c->has_parts ? (c->has_extra ? NETCHECK_LEN_FULL : NETCHECK_LEN_PARTS) : NETCHECK_LEN_OLD, k, f, at;
    if (size < n) return 0;
    p[0] = type;
    netcheck_put32(p + 1, c->match);
    netcheck_put32(p + 5, c->step);
    netcheck_put32(p + 9, c->hash);
    if (!c->has_parts) return n;
    for (k = 0; k < NETCHECK_PARTS; k++) netcheck_put32(p + 13 + 4 * k, c->parts[k]);
    if (!c->has_extra) return n;
    at = NETCHECK_LEN_PARTS;
    for (k = 0; k < NETCHECK_SLOTS; k++)
        for (f = 0; f < NETCHECK_FIELDS; f++, at += 4) netcheck_put32(p + at, c->fields[k][f]);
    for (k = 0; k < NETCHECK_GENS; k++, at += 4) netcheck_put32(p + at, c->gens[k]);
    netcheck_put32(p + at, c->crowd);
    return n;
}

/* A received message (type byte first) of `len` bytes: what its length holds, the rest zero.
 * Returns 0 for a message too short to be one. */
static int netcheck_unpack(const uint8_t *p, int len, NetCheck *c)
{
    int k, f, at;
    memset(c, 0, sizeof(*c));
    if (len < NETCHECK_LEN_OLD) return 0;
    c->match = netcheck_get32(p + 1);
    c->step = netcheck_get32(p + 5);
    c->hash = netcheck_get32(p + 9);
    if (len < NETCHECK_LEN_PARTS) return 1;
    c->has_parts = 1;
    for (k = 0; k < NETCHECK_PARTS; k++) c->parts[k] = netcheck_get32(p + 13 + 4 * k);
    if (len < NETCHECK_LEN_FULL) return 1;
    c->has_extra = 1;
    at = NETCHECK_LEN_PARTS;
    for (k = 0; k < NETCHECK_SLOTS; k++)
        for (f = 0; f < NETCHECK_FIELDS; f++, at += 4) c->fields[k][f] = netcheck_get32(p + at);
    for (k = 0; k < NETCHECK_GENS; k++, at += 4) c->gens[k] = netcheck_get32(p + at);
    c->crowd = netcheck_get32(p + at);
    return 1;
}

/* Appends to out (n chars so far), never past size - 1; returns the new length. */
static int netcheck_cat(char *out, int size, int n, const char *format, ...)
{
    va_list args;
    int w;
    if (n >= size - 1) return n;
    va_start(args, format);
    w = vsnprintf(out + n, (size_t)(size - n), format, args);
    va_end(args);
    if (w < 0) return n;
    return n + w >= size ? size - 1 : n + w;
}

/* The parts' line of a desync report (since build 23): which part of the checksum differs. */
static int netcheck_parts_text(char *out, int size, const NetCheck *mine, const NetCheck *theirs, int fighters, int players)
{
    int n = 0, slot, any = 0;
    if (size <= 0) return 0;
    out[0] = 0;
    for (slot = 0; slot < NETCHECK_SLOTS && slot < fighters; slot++)
        if (mine->parts[slot] != theirs->parts[slot]) {
            n = netcheck_cat(out, size, n, "%sfighter %d (%s %d)", any ? ", " : "", slot + 1,
                             slot < players ? "player" : "CPU", slot < players ? slot + 1 : slot + 1 - players);
            any = 1;
        }
    n = netcheck_cat(out, size, n, "%s; the CPU fighters' generators %s; the shared generator and time scale %s",
                     any ? "" : "no fighter's fields", mine->parts[4] == theirs->parts[4] ? "equal" : "differ",
                     mine->parts[5] == theirs->parts[5] ? "equal" : "differ");
    return n;
}

/* The fields' line (build 34 on, both sides): every raw word that differs, "fighter 1 0x44 here X
 * there Y" (positions also as floats), then the pre-fight generators and the crowd. Returns 0 and
 * writes nothing when either side sent no fields. */
static int netcheck_fields_text(char *out, int size, const NetCheck *mine, const NetCheck *theirs, int fighters)
{
    int n = 0, slot, f, k, any = 0, gens = 0;
    if (size <= 0) return 0;
    out[0] = 0;
    if (!mine->has_extra || !theirs->has_extra) return 0;
    for (slot = 0; slot < NETCHECK_SLOTS && slot < fighters; slot++)
        for (f = 0; f < NETCHECK_FIELDS; f++) {
            uint32_t a = mine->fields[slot][f], b = theirs->fields[slot][f];
            if (a == b) continue;
            n = netcheck_cat(out, size, n, "%sfighter %d 0x%X here %08X there %08X", any ? ", " : "", slot + 1,
                             (unsigned)netcheck_field[f], (unsigned)a, (unsigned)b);
            if (netcheck_field[f] >= 0x44 && netcheck_field[f] <= 0x4C) {
                float fa, fb;
                memcpy(&fa, &a, 4);
                memcpy(&fb, &b, 4);
                n = netcheck_cat(out, size, n, " (%g / %g)", (double)fa, (double)fb);
            }
            any = 1;
        }
    if (!any) n = netcheck_cat(out, size, n, "no fighter field differs");
    for (k = 0; k < NETCHECK_GENS; k++) if (mine->gens[k] != theirs->gens[k]) gens = 1;
    n = netcheck_cat(out, size, n, "; pre-fight generators %s", gens ? "differ" : "equal");
    for (k = 0; gens && k < NETCHECK_GENS; k++)
        if (mine->gens[k] != theirs->gens[k])
            n = netcheck_cat(out, size, n, " (%08X here %08X there %08X)", (unsigned)netcheck_gen[k],
                             (unsigned)mine->gens[k], (unsigned)theirs->gens[k]);
    n = netcheck_cat(out, size, n, "; crowd %s", mine->crowd == theirs->crowd ? "equal" : "differ");
    if (mine->crowd != theirs->crowd)
        n = netcheck_cat(out, size, n, " (here %08X there %08X)", (unsigned)mine->crowd, (unsigned)theirs->crowd);
    return n;
}

#endif
