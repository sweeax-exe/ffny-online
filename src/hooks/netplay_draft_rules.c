/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Draft mode's pick and ban: the wire and the rules (see netplay_draft_rules.h). */
#include "netplay_draft_rules.h"

#include <stdio.h>
#include <string.h>

#define CREATED_FIGHTER 99

static const int k_ban_order[NETDRAFT_BANS] = { 0, 1, 1, 0, 0, 1 };     /* relay draftBanOrder */
static const int k_strike_order[4] = { 0, 1, 1, 0 };                   /* relay draftStrikeOrder */

typedef struct Reader { const uint8_t *b; int len, at, bad; } Reader;

static int u8(Reader *r)
{
    if (r->at >= r->len) { r->bad = 1; return 0; }
    return r->b[r->at++];
}

static int list(Reader *r, int *out, int max)
{
    int n = u8(r), i;
    for (i = 0; i < n; i++) {
        int v = u8(r);
        if (i < max) out[i] = v;
    }
    if (n > max) r->bad = 1;
    return n > max ? max : n;
}

int netdraft_decode(const uint8_t *b, int len, NetDraft *out)
{
    Reader r = { b, len, 0, 0 };
    int locked, s;
    memset(out, 0, sizeof(*out));
    if (!b) return 0;
    out->step = u8(&r); out->turn = u8(&r); out->secs = u8(&r); out->game = u8(&r);
    out->venue = u8(&r); out->first = u8(&r) & 1;
    out->ban_count = list(&r, out->bans, NETDRAFT_BANS);
    out->struck_count = list(&r, out->struck, NETDRAFT_MAX);
    out->venue_ban_count = list(&r, out->venue_bans, NETDRAFT_MAX);
    out->picks[0] = u8(&r); out->picks[1] = u8(&r);
    locked = u8(&r);
    out->locked[0] = locked & 1; out->locked[1] = (locked >> 1) & 1;
    out->cursors[0] = u8(&r); out->cursors[1] = u8(&r);
    out->option_count = list(&r, out->options, NETDRAFT_MAX);
    for (s = 0; s < 2; s++) out->lock_count[s] = list(&r, out->locks[s], NETDRAFT_MAX);
    out->afk = u8(&r);
    if (r.bad || r.at != len || out->step < NETDRAFT_BAN || out->step > NETDRAFT_DONE || out->turn > 1) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    out->valid = 1;
    return 1;
}

int netdraft_encode_act(uint8_t *out, int step, int action, int id)
{
    out[0] = (uint8_t)step;
    out[1] = (uint8_t)action;
    out[2] = (uint8_t)id;
    return 3;
}

int netdraft_active(const NetDraft *d) { return d && d->valid && d->step >= NETDRAFT_BAN && d->step <= NETDRAFT_LOSER_PICK; }

int netdraft_fighter_step(int step)
{
    return step == NETDRAFT_BAN || step == NETDRAFT_BLIND || step == NETDRAFT_WINNER_PICK || step == NETDRAFT_LOSER_PICK;
}

int netdraft_action(const NetDraft *d)
{
    if (!netdraft_active(d)) return 0;
    switch (d->step) {
    case NETDRAFT_BAN: return NETDRAFT_ACT_BAN;
    case NETDRAFT_STRIKE: return NETDRAFT_ACT_STRIKE;
    case NETDRAFT_VENUE_BAN: case NETDRAFT_VENUE_PICK: return NETDRAFT_ACT_VENUE;
    default: return NETDRAFT_ACT_PICK;
    }
}

int netdraft_my_turn(const NetDraft *d, int you)
{
    if (!netdraft_active(d) || you < 0 || you > 1) return 0;
    if (d->step == NETDRAFT_BLIND) return !d->locked[you];
    return d->turn == you;
}

int netdraft_ban_owner(const NetDraft *d, int i)
{
    return i < 0 || i >= NETDRAFT_BANS ? -1 : k_ban_order[i] ^ d->first;
}

int netdraft_bans_of(const NetDraft *d, int slot, int out[3])
{
    int i, n = 0;
    for (i = 0; i < 3; i++) out[i] = NETDRAFT_NONE;
    for (i = 0; i < d->ban_count && n < 3; i++)
        if (netdraft_ban_owner(d, i) == slot) out[n++] = d->bans[i];
    return n;
}

static int in(const int *list, int n, int v)
{
    int i;
    for (i = 0; i < n; i++) if (list[i] == v) return 1;
    return 0;
}

int netdraft_banned(const NetDraft *d, int fighter)
{
    return fighter != NETDRAFT_NONE && in(d->bans, d->ban_count, fighter);
}

int netdraft_fighter_legal(const NetDraft *d, int you, int fighter)
{
    if (!d->valid || fighter < 0 || fighter == NETDRAFT_NONE || fighter == CREATED_FIGHTER || you < 0 || you > 1) return 0;
    if (netdraft_banned(d, fighter)) return 0;
    if (d->step == NETDRAFT_BAN) return 1;
    return !in(d->locks[you], d->lock_count[you], fighter);
}

int netdraft_fighter_state(const NetDraft *d, int you, int fighter)
{
    int other = 1 - (you & 1);
    if (netdraft_banned(d, fighter)) return NETDRAFT_TILE_BANNED;
    if (you >= 0 && you <= 1 && d->picks[you] == fighter && (d->locked[you] || d->step == NETDRAFT_PLAYING))
        return NETDRAFT_TILE_MINE;
    if (d->picks[other] == fighter && d->picks[other] != NETDRAFT_NONE) return NETDRAFT_TILE_THEIRS;
    if (you >= 0 && you <= 1 && in(d->locks[you], d->lock_count[you], fighter)) return NETDRAFT_TILE_LOCKED;
    return NETDRAFT_TILE_AVAILABLE;
}

int netdraft_venue_state(const NetDraft *d, int venue)
{
    if (d->venue == venue && venue) return NETDRAFT_VENUE_CHOSEN;
    if (d->game <= 1) return in(d->struck, d->struck_count, venue) ? NETDRAFT_VENUE_STRUCK : NETDRAFT_VENUE_OPEN;
    return in(d->venue_bans, d->venue_ban_count, venue) ? NETDRAFT_VENUE_BANNED : NETDRAFT_VENUE_OPEN;
}

int netdraft_venue_legal(const NetDraft *d, int venue)
{
    if (!netdraft_active(d) || netdraft_fighter_step(d->step)) return 0;
    return in(d->options, d->option_count, venue) && netdraft_venue_state(d, venue) == NETDRAFT_VENUE_OPEN;
}

/* which of this player's turns in the step the current one is (from 1), and of how many */
static void turn_of(const NetDraft *d, int *nth, int *of)
{
    int i, n = 0;
    *nth = *of = 0;
    if (d->step == NETDRAFT_BAN) {
        int owner = netdraft_ban_owner(d, d->ban_count);
        for (i = 0; i < d->ban_count; i++) if (netdraft_ban_owner(d, i) == owner) n++;
        *nth = n + 1;
        *of = 3;
    } else if (d->step == NETDRAFT_STRIKE) {
        int owner = d->turn;
        for (i = 0; i < d->struck_count && i < 4; i++) if ((k_strike_order[i] ^ d->first) == owner) n++;
        *nth = n + 1;
        *of = 2;    /* A strikes the first and the last, B the two between */
    }
}

void netdraft_banner(const NetDraft *d, int you, char *out, int size)
{
    int nth, of, mine = netdraft_my_turn(d, you);
    if (!d->valid) { snprintf(out, (size_t)size, "DRAFT"); return; }
    turn_of(d, &nth, &of);
    switch (d->step) {
    case NETDRAFT_BAN:
        if (mine) snprintf(out, (size_t)size, "YOUR BAN %d/%d", nth, of);
        else snprintf(out, (size_t)size, "OPPONENT BANNING %d/%d", nth, of);
        return;
    case NETDRAFT_STRIKE:
        if (mine) snprintf(out, (size_t)size, "STRIKE A VENUE %d/%d", nth, of);
        else snprintf(out, (size_t)size, "OPPONENT STRIKING");
        return;
    case NETDRAFT_BLIND:
        snprintf(out, (size_t)size, "%s", mine ? "PICK YOUR FIGHTER" : d->locked[1 - (you & 1)] ? "BOTH LOCKED" : "LOCKED: OPPONENT PICKING");
        return;
    case NETDRAFT_VENUE_BAN:
        snprintf(out, (size_t)size, "%s", mine ? "BAN A VENUE" : "OPPONENT BANNING A VENUE");
        return;
    case NETDRAFT_VENUE_PICK:
        snprintf(out, (size_t)size, "%s", mine ? "PICK THE VENUE" : "OPPONENT PICKING THE VENUE");
        return;
    case NETDRAFT_WINNER_PICK:
    case NETDRAFT_LOSER_PICK:
        snprintf(out, (size_t)size, "%s", mine ? (d->step == NETDRAFT_LOSER_PICK ? "COUNTERPICK" : "PICK YOUR FIGHTER")
                                               : "OPPONENT PICKING");
        return;
    case NETDRAFT_PLAYING:
        snprintf(out, (size_t)size, "GAME %d", d->game);
        return;
    default:
        snprintf(out, (size_t)size, "%s", d->afk == NETDRAFT_NONE ? "THE DRAFT IS OVER" :
                                          d->afk == you ? "YOU WERE AFK" : "OPPONENT AFK");
    }
}

const char *netdraft_hint(const NetDraft *d, int you)
{
    int mine = netdraft_my_turn(d, you);
    switch (d->valid ? d->step : 0) {
    case NETDRAFT_BAN: return mine ? "Nobody plays a banned fighter this set. Let the clock run out to skip."
                                   : "Bans last the whole set: three each.";
    case NETDRAFT_STRIKE: return "Strike starters until one is left: game 1 is played there.";
    case NETDRAFT_BLIND: return "Blind pick: neither sees the other's fighter until both are locked.";
    case NETDRAFT_VENUE_BAN: return mine ? "You won the last game: ban a venue, then the loser picks."
                                         : "The last game's winner bans a venue; you pick next.";
    case NETDRAFT_VENUE_PICK: return mine ? "You lost the last game: you pick where the next one is."
                                          : "The last game's loser picks the venue.";
    case NETDRAFT_WINNER_PICK: return mine ? "Winner picks first, in the open. Not the fighter you won with."
                                           : "The winner picks first; then you counterpick.";
    case NETDRAFT_LOSER_PICK: return mine ? "Counterpick: you see their fighter." : "The loser counterpicks.";
    case NETDRAFT_PLAYING: return "Both fighters are drafted.";
    default: return "";
    }
}

int netdraft_auto(const NetDraft *d, int you, const int *fighters, int n, unsigned seed, int *action, int *id)
{
    int i, legal = 0, pick;
    if (!netdraft_my_turn(d, you)) return 0;
    *action = netdraft_action(d);
    if (netdraft_fighter_step(d->step)) {
        for (i = 0; i < n; i++) if (netdraft_fighter_legal(d, you, fighters[i])) legal++;
        if (!legal) return 0;
        pick = (int)(seed % (unsigned)legal);
        for (i = 0; i < n; i++)
            if (netdraft_fighter_legal(d, you, fighters[i]) && pick-- == 0) { *id = fighters[i]; return 1; }
        return 0;
    }
    for (i = 0; i < d->option_count; i++) if (netdraft_venue_legal(d, d->options[i])) legal++;
    if (!legal) return 0;
    pick = (int)(seed % (unsigned)legal);
    for (i = 0; i < d->option_count; i++)
        if (netdraft_venue_legal(d, d->options[i]) && pick-- == 0) { *id = d->options[i]; return 1; }
    return 0;
}
