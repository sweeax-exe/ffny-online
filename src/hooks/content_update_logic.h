/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 86, UPDATE NOW for new community content: the rules, without the game (content_update_logic.c; the
 * unit tests build it alone). The glue that reads the game's state and does the restart is content_update.c.
 *
 * The hand-off: the game closes as on a normal exit (the window's close) and the launcher syncs, composes and
 * starts it again, back to the main menu and on to the online hub.
 *   - the game the launcher started (--launcher-child): it exits with CU_EXIT_CODE; the launcher, which waits
 *     for it in the background (pc_launcher_watch), opens again with --content-update instead of plain;
 *   - a game started with the launcher skipped: it starts "<exe> <its arguments> --launcher --content-update"
 *     itself (cu_launcher_command), then exits.
 * A launcher with --content-update presses PLAY by itself: PLAY waits for the sync (and its compose), the game
 * starts with --content-update too, skips the intro and opens the online hub once the main menu is up. */
#ifndef DEFJAM_CONTENT_UPDATE_LOGIC_H
#define DEFJAM_CONTENT_UPDATE_LOGIC_H

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CU_EXIT_CODE 0x46465544ul               /* "FFUD": the game closed for UPDATE NOW */
#define CU_FLAG L"--content-update"
#define CU_CHECK_MS (10ull * 60 * 1000)         /* the catalogue check, in any menu */
#define CU_SNOOZE_MS (30ull * 60 * 1000)        /* LATER */
#define CU_TOAST_MS 12000ull                    /* the corner notice */
#define CU_SAVE_WAIT_MS 15000ull                /* a save being written: UPDATE NOW waits this long at most */

/* Why UPDATE NOW cannot go now. */
enum CuRefusal {
    CU_OK = 0,
    CU_NO_NEWS,         /* nothing new (any more) */
    CU_IN_FIGHT,        /* a fight runs, online or offline */
    CU_IN_MATCH,        /* an online match is set up or runs (a room's START until back in the room) */
    CU_COUNTDOWN,       /* the room counts down to its match (the vote, a hill call) */
    CU_BUSY             /* already closing */
};

typedef struct CuGate {
    int news;           /* packs the next sync brings (pc_content_news) */
    int in_fight;       /* netplay_in_fight */
    int match_locked;   /* netplay_match_locked */
    int lockstep;       /* netplay_active */
    int room_phase;     /* NetRoom.phase when in a room (0 lobby, 1 voting, 2 playing), else 0 */
    int hill_busy;      /* a hill/rotation room calls, votes or fights */
    int already;        /* UPDATE NOW was taken: the game is closing */
} CuGate;

int cu_gate(const CuGate *g);
/* Plain words for a refusal ("" for CU_OK). */
const char *cu_refusal_text(int why);

/* The offer: the popup on the main menu, the corner notice in the other menus. */
typedef struct CuOffer {
    unsigned long long snooze_until;    /* LATER: neither shows before this */
    unsigned long long toast_until;     /* the corner notice shows until this */
    unsigned long long toast_at;        /* when it last started */
    int fight_since_menu;               /* a fight ran since the main menu was last up */
    int offered_news;                   /* the news count the popup last asked about (0 none) */
    int toasted_news;                   /* and the corner notice */
} CuOffer;

/* Once a frame: a fight runs. */
void cu_offer_frame(CuOffer *o, int in_fight);
/* The popup is due now: new content, the main menu up, not snoozed, and either a fight since the main menu
 * was last up or news it has not asked about yet. */
int cu_offer_popup_due(CuOffer *o, int news, int on_main_menu, unsigned long long now);
/* The popup was answered: later=1 snoozes both for CU_SNOOZE_MS. */
void cu_offer_answered(CuOffer *o, int news, int later, unsigned long long now);
/* The corner notice shows now (news not announced yet, or a snooze just ran out), in a menu only. */
int cu_offer_toast(CuOffer *o, int news, int in_menu, unsigned long long now);

/* The catalogue check is due: never in a fight; signed in (any menu) or on the online screens. */
int cu_check_due(unsigned long long last, unsigned long long now, int in_fight, int signed_in, int online_page);

/* The launcher's game closed with this code: open the launcher in UPDATE NOW mode. */
int cu_after_child_exit(unsigned long code);

/* Removes every whole-word occurrence of flag (unquoted) from a command line, in place; the count. */
size_t cu_strip_flag(wchar_t *line, const wchar_t *flag);
/* The command line for the launcher in UPDATE NOW mode: line without --no-launcher, --launcher-child,
 * --launcher and --content-update, plus " --launcher --content-update". 0 when out is too small. */
int cu_launcher_command(const wchar_t *line, wchar_t *out, size_t size);
/* The launcher reopened after a game: line without --content-update (it would press PLAY again), plus
 * " --launcher". 0 when out is too small. */
int cu_reopen_command(const wchar_t *line, wchar_t *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
