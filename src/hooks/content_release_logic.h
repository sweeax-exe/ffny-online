/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 87, the notice of content to come: accepted community packs go live together at a release window (the
 * account service's GET /api/packs/next-release, public), and the game says so beforehand: "NEW CONTENT AT 17:00",
 * what it is ("3 new fighters, 1 skin") and how long until then. The rules and the texts, without the game
 * (content_release_logic.c; the unit tests build it alone, tests/unit/test_content_release.py). The glue that asks
 * the service and reads the PC's clock is content_release.c.
 *
 * The time shown is the player's PC's: the answer carries the server's clock (server_time), so the release is
 * release_at - server_time seconds after the answer came, whatever the PC's clock and time zone say. */
#ifndef DEFJAM_CONTENT_RELEASE_LOGIC_H
#define DEFJAM_CONTENT_RELEASE_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* What a scheduled pack is (the service's "kind" words), in the order the notice names them. */
enum CrKind {
    CR_NEW_FIGHTER = 0, CR_SKIN, CR_MAP, CR_CLOTHING, CR_LOOK, CR_LANGUAGE, CR_PACK, CR_UPDATE,
    CR_KINDS
};

#define CR_TOAST_MS 12000ull            /* the corner notice */
#define CR_SOON_S 600                   /* the corner notice once more, ten minutes before */
#define CR_AFTER_S 60                   /* the catalogue check after the release time: this long after it ... */
#define CR_AFTER_SPREAD_S 120           /* ... plus up to this much, different on every PC */
#define CR_PARTS 4                      /* "3 new fighters, 1 skin, 2 maps, 5 more": at most this many parts */

typedef struct CrInfo {
    long long release_at;               /* the server's unix time the scheduled packs go live, 0 none */
    long long server_time;              /* the server's clock in the answer */
    long long local_time;               /* this PC's clock (unix) when the answer came */
    int count;                          /* scheduled packs */
    int kinds[CR_KINDS];                /* the named ones by kind (the answer names at most 20) */
} CrInfo;

/* Reads the service's answer; local_time is this PC's clock now. 1 when it is one (a count and the server's clock);
 * an answer with nothing scheduled is one too (count 0). */
int cr_parse(const char *json, long long local_time, CrInfo *out);
/* The release on this PC's clock (unix), and the seconds until then at now (this PC's clock). */
long long cr_local_release(const CrInfo *i);
long long cr_remaining(const CrInfo *i, long long now);
/* The notice shows: packs are scheduled and their time has not come. */
int cr_shows(const CrInfo *i, long long now);

/* "3 new fighters, 1 skin"; at most CR_PARTS parts, the rest as "N more". The length written. */
int cr_kinds_text(const CrInfo *i, char *out, int size);
/* "in 2h 15m", "in 2h", "in 15 min", "in under a minute", "in 1d 3h". */
int cr_in_text(long long remaining, char *out, int size);
/* "17:00", or "5:00 PM" where the PC shows 12-hour times. */
int cr_clock_text(int hour, int minute, int h12, char *out, int size);
/* "NEW CONTENT AT 17:00"; a day later "NEW CONTENT TOMORROW AT 17:00", further off "NEW CONTENT IN 3 DAYS".
 * days: the release's calendar day minus today's, on this PC's clock. */
int cr_headline(int days, int hour, int minute, int h12, char *out, int size);
/* The corner notice: "New content at 17:00 (in 2h 15m): 3 new fighters, 1 skin." */
int cr_toast_text(const CrInfo *i, long long now, int days, int hour, int minute, int h12, char *out, int size);

/* What this PC has said about a schedule, and done after it. */
typedef struct CrNotice {
    long long told_release;             /* the corner notice last told this release ... */
    int told_count;                     /* ... with this many packs */
    int told_soon;                      /* and its ten-minute reminder */
    unsigned long long until;           /* the corner notice shows until this (ms) */
    long long checked_release;          /* the catalogue check after this release was asked for */
} CrNotice;

/* The corner notice shows now: once per schedule (a release time and a count), once more CR_SOON_S before it; in
 * a menu only, and a fight uses nothing up. now: this PC's clock (unix); now_ms: a millisecond clock. */
int cr_toast(CrNotice *n, const CrInfo *i, long long now, unsigned long long now_ms, int in_menu);
/* The release time has passed: once per release, CR_AFTER_S + spread seconds after it (spread: 0..CR_AFTER_SPREAD_S,
 * this PC's own), 1 so the caller looks at the catalogue now (the UPDATE NOW notice takes over) and asks for the
 * next schedule. Never in a fight. */
int cr_after_due(CrNotice *n, const CrInfo *i, long long now, int spread, int in_fight);

#ifdef __cplusplus
}
#endif

#endif
