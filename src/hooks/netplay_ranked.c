/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Ranked play: the wire forms and the ladder (see netplay_ranked.h). */
#include "netplay_ranked.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const k_ids[NETRANK_MODES] = { "open", "classic", "draft" };
static const char *const k_titles[NETRANK_MODES] = { "OPEN", "CLASSIC", "DRAFT" };

/* accounts/ranks.go Tiers: the rating each tier starts at */
static const int k_tier_min[8] = { -100000, 1200, 1350, 1500, 1650, 1800, 1950, 2100 };
static const char *const k_tier_names[8] = { "Street Rookie", "Hustler", "Brawler", "Contender", "Enforcer",
                                             "Kingpin", "Don of the Bronx", "Def Jam Legend" };

const char *netranked_mode_id(int mode) { return mode >= 0 && mode < NETRANK_MODES ? k_ids[mode] : NULL; }
const char *netranked_mode_title(int mode) { return mode >= 0 && mode < NETRANK_MODES ? k_titles[mode] : ""; }

int netranked_mode_of_key(const char *key)
{
    int i;
    if (!key || strncmp(key, "ranked_", 7)) return -1;
    for (i = 0; i < NETRANK_MODES; i++) if (!strcmp(key + 7, k_ids[i])) return i;
    return -1;
}

int netranked_encode_ext(uint8_t *out, unsigned modes, int primary, int rollback, const int pings[3], const char *code)
{
    int i, n = 0;
    if (primary < 0 || primary >= NETRANK_MODES) primary = NETRANK_CLASSIC;
    modes = (modes | (1u << primary)) & 7u;
    out[n++] = (uint8_t)modes;
    out[n++] = (uint8_t)((rollback ? 1 : 0) | (primary << 4));
    for (i = 0; i < 3; i++) {
        int p = pings ? pings[i] : 0;
        if (p < 0) p = 0;
        if (p > 0xFFFF) p = 0xFFFF;
        out[n++] = (uint8_t)p;
        out[n++] = (uint8_t)(p >> 8);
    }
    for (i = 0; i < NETRANK_CODE_LEN; i++) out[n++] = code && code[0] ? (uint8_t)code[i] : 0;
    return n;
}

int netranked_decode_queue_ext(const uint8_t *b, int len, NetQueueExt *out)
{
    int i, at = 0, alen;
    memset(out, 0, sizeof(*out));
    out->mode = -1;
    if (!b || len < 3 * 2 + 1 + 1 + 2 + NETRANK_CODE_LEN + 1) return 0;
    for (i = 0; i < NETRANK_MODES; i++, at += 2) out->counts[i] = b[at] | b[at + 1] << 8;
    out->mode = b[at] < NETRANK_MODES ? b[at] : -1;
    at++;
    out->accept_secs = b[at++];
    out->opp_rating = b[at] | b[at + 1] << 8;
    at += 2;
    for (i = 0; i < NETRANK_CODE_LEN; i++) out->code[i] = b[at + i] >= 0x20 && b[at + i] < 0x7F ? (char)b[at + i] : 0;
    out->code[NETRANK_CODE_LEN] = 0;
    if (!out->code[0]) memset(out->code, 0, sizeof(out->code));
    at += NETRANK_CODE_LEN;
    alen = b[at++];
    if (alen > 64 || at + alen > len) {
        memset(out, 0, sizeof(*out));
        out->mode = -1;
        return 0;
    }
    memcpy(out->addr, b + at, (size_t)alen);
    out->addr[alen] = 0;
    at += alen;
    if (at < len) {     /* build 54: a refusal's reason, a length byte and the text (printable ASCII kept) */
        int wlen = b[at++], k, o = 0;
        if (wlen > 96 || at + wlen != len) {
            memset(out, 0, sizeof(*out));
            out->mode = -1;
            return 0;
        }
        for (k = 0; k < wlen; k++)
            if (b[at + k] >= 0x20 && b[at + k] < 0x7F) out->why[o++] = (char)b[at + k];
        out->why[o] = 0;
    }
    out->valid = 1;
    return 1;
}

int netranked_decode_rankinfo(const uint8_t *b, int len, NetRankInfo *out)
{
    int i, at = 2, count;
    memset(out, 0, sizeof(*out));
    out->picker = -1;
    out->winner = NETRANK_SET_RUNNING;
    if (!b || len < 2) return 0;
    count = b[1];
    if (count > 4 || len < 2 + count * 7) return 0;
    out->mode = b[0] < NETRANK_MODES ? b[0] : NETRANK_CLASSIC;
    out->count = count;
    for (i = 0; i < count; i++, at += 7) {
        NetRankPlayer *p = &out->p[i];
        p->slot = b[at];
        p->division = b[at + 1];
        p->placements = b[at + 2];
        p->rating = b[at + 3] | b[at + 4] << 8;
        p->set_wins = b[at + 5];
        p->provisional = b[at + 6] & 1;
    }
    if (at == len) {
        out->valid = 1;
        return 1;
    }
    if (at + 3 != len) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    out->has_set = 1;
    out->game = b[at];
    out->picker = b[at + 1] == 0xFF ? -1 : b[at + 1];
    out->winner = b[at + 2];
    out->valid = 1;
    return 1;
}

const NetRankPlayer *netranked_player(const NetRankInfo *ri, int slot)
{
    int i;
    if (!ri || !ri->valid) return NULL;
    for (i = 0; i < ri->count; i++) if (ri->p[i].slot == slot) return &ri->p[i];
    return NULL;
}

/* the next |-field of *s into out; returns 0 at the end */
static int field(const char **s, char *out, int size)
{
    const char *bar;
    int n;
    if (!*s) return 0;
    bar = strchr(*s, '|');
    n = bar ? (int)(bar - *s) : (int)strlen(*s);
    if (out && size > 0) {
        int k = n < size - 1 ? n : size - 1;
        memcpy(out, *s, (size_t)k);
        out[k] = 0;
        while (k > 0 && (out[k - 1] == '\r' || out[k - 1] == '\n')) out[--k] = 0;
    }
    *s = bar ? bar + 1 : NULL;
    return 1;
}

int netranked_parse_line(const char *value, NetModeRank *out)
{
    int *nums[9], i;
    char buf[48];
    const char *s = value;
    memset(out, 0, sizeof(*out));
    nums[0] = &out->rating; nums[1] = &out->deviation; nums[2] = &out->division; nums[3] = &out->placements_left;
    nums[4] = &out->placements_of; nums[5] = &out->provisional; nums[6] = &out->to_next; nums[7] = &out->set_wins;
    nums[8] = &out->set_losses;
    for (i = 0; i < 9; i++) {
        if (!field(&s, buf, sizeof(buf))) { memset(out, 0, sizeof(*out)); return 0; }
        *nums[i] = atoi(buf);
    }
    if (!field(&s, out->label, sizeof(out->label))) { memset(out, 0, sizeof(*out)); return 0; }
    out->peak = out->rating;
    if (field(&s, buf, sizeof(buf)) && buf[0]) out->peak = atoi(buf);
    field(&s, out->peak_rank, sizeof(out->peak_rank));
    if (field(&s, buf, sizeof(buf)))
        out->hint = !strcmp(buf, "promotion") ? NETRANK_HINT_PROMOTION : !strcmp(buf, "demotion") ? NETRANK_HINT_DEMOTION : 0;
    if (field(&s, buf, sizeof(buf))) out->place = atoi(buf);     /* build 52: |place|of */
    if (field(&s, buf, sizeof(buf))) out->of = atoi(buf);
    if (out->division < 0) out->division = 0;
    if (out->division >= NETRANK_DIVISIONS) out->division = NETRANK_DIVISIONS - 1;
    out->valid = 1;
    return 1;
}

int netranked_division_min(int d)
{
    int t;
    if (d <= 0) return k_tier_min[0];
    if (d >= NETRANK_DIVISIONS - 1) return k_tier_min[7];
    t = d / 3;
    if (t == 0) return k_tier_min[1] - 50 * (3 - d);     /* Street Rookie II 1100, I 1150 */
    return k_tier_min[t] + 50 * (d % 3);
}

int netranked_division_of(int rating)
{
    int d;
    for (d = NETRANK_DIVISIONS - 1; d > 0; d--) if (rating >= netranked_division_min(d)) return d;
    return 0;
}

int netranked_tier_of(int d)
{
    if (d < 0) d = 0;
    return d / 3 > 7 ? 7 : d / 3;
}

const char *netranked_tier_name(int tier) { return k_tier_names[tier < 0 ? 0 : tier > 7 ? 7 : tier]; }

void netranked_division_name(int d, char *out, int size)
{
    static const char *const roman[3] = { "III", "II", "I" };
    int t;
    if (d == NETRANK_PLACEMENT) { snprintf(out, (size_t)size, "PLACEMENT"); return; }
    t = netranked_tier_of(d);
    if (t == 7) snprintf(out, (size_t)size, "%s", k_tier_names[7]);
    else snprintf(out, (size_t)size, "%s %s", k_tier_names[t], roman[d % 3]);
}

int netranked_division_percent(int rating, int d)
{
    int lo, hi, pct;
    if (d >= NETRANK_DIVISIONS - 1) return 100;
    if (d < 0) d = 0;
    hi = netranked_division_min(d + 1);
    lo = d == 0 ? hi - 50 : netranked_division_min(d);
    pct = (rating - lo) * 100 / (hi - lo);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}

void netranked_player_label(const NetRankPlayer *p, int placements_of, char *out, int size)
{
    char name[40];
    if (!p) { snprintf(out, (size_t)size, "UNRANKED"); return; }
    if (p->division == NETRANK_PLACEMENT || p->placements > 0) {
        int of = placements_of > 0 ? placements_of : 5;
        int done = of - p->placements;
        snprintf(out, (size_t)size, "PLACEMENT %d/%d", done < 0 ? 0 : done, of);
        return;
    }
    netranked_division_name(p->division, name, sizeof(name));
    snprintf(out, (size_t)size, "%s%s", name, p->provisional ? " ?" : "");
}
