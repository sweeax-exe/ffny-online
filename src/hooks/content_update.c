/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 86, new community content (owner, 2026-11-16): packs the team accepts go live at once, but players did not
 * notice they had to restart, and rooms only match PCs with the same packs. So:
 *   - the catalogue check (setup/mods.py check, pc_mods_check_start) runs every 10 minutes in any menu while signed
 *     in, not only on the online screens;
 *   - the news shows in every menu (a corner notice) and, on the main menu, as a popup once per news and after each
 *     fight: "New community content is out (N packs). Update now?" UPDATE NOW / LATER (30 minutes of quiet);
 *   - UPDATE NOW closes the game as its window's close does (after a save being written) and the launcher syncs,
 *     composes and starts it again, back to the main menu and the online hub (content_update_logic.h). Never in a
 *     fight, a match or a room's countdown.
 * Test switches: RECOMP_TEST_CONTENT_NEWS=<n> fakes the news (pc_content_news), RECOMP_TEST_CONTENT_POPUP=0 keeps
 * the popup away, RECOMP_TEST_UPDATE_NOW_DRY=1 logs what UPDATE NOW would do instead of closing the game. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "content_update.h"
#include "account.h"
#include "memcard_fast.h"
#include "netplay.h"
#include "pc_content.h"
#include "pc_input.h"
#include "pc_mods.h"

int pc_settings_start_content_update(void);     /* pc_settings.c: the launcher in UPDATE NOW mode */

static CuOffer g_offer;
static unsigned long long g_last_check;
static volatile LONG g_requested;               /* UPDATE NOW taken */
static unsigned long long g_requested_at;
static volatile LONG g_refusal_offer;
static int g_hub_opened, g_auto_played;

static unsigned long long now_ms(void) { return GetTickCount64(); }

static int has_flag(const char *flag)
{
#if defined(_WIN32)
    const char *cmd = GetCommandLineA();
    const char *p = cmd ? strstr(cmd, flag) : NULL;
    size_t n = strlen(flag);
    while (p) {
        if ((p == cmd || p[-1] == ' ' || p[-1] == '\t') && (!p[n] || p[n] == ' ' || p[n] == '\t')) return 1;
        p = strstr(p + n, flag);
    }
#else
    (void)flag;
#endif
    return 0;
}

int content_update_restarted(void)
{
    static int known = -1;
    if (known < 0) known = has_flag("--content-update");
    return known;
}

int content_update_news(char *names, int size)
{
    static unsigned long long read_at;
    static int cached;
    static char cached_names[256];
    unsigned long long now = now_ms();
    if (!read_at || now - read_at > 3000) {
        read_at = now;
        cached = pc_content_news(cached_names, sizeof(cached_names));
    }
    if (names && size > 0) snprintf(names, (size_t)size, "%s", cached_names);
    return cached;
}

static void gate_state(CuGate *g)
{
    NetRoom r;
    memset(g, 0, sizeof *g);
    g->news = content_update_news(NULL, 0);
    g->in_fight = netplay_in_fight();
    g->match_locked = netplay_match_locked();
    g->lockstep = netplay_active();
    g->already = g_requested != 0;
    netplay_lobby_room(&r);
    if (r.in_room) {
        int i;
        g->room_phase = r.phase;
        if (r.hill.valid && r.hill.phase != NETHILL_WAITING)
            for (i = 0; i < r.hill.count && i < NETPLAY_ROOM_SLOTS; i++)
                if (r.hill.line[i].slot == r.you && r.hill.line[i].state != NETLINE_WAITING &&
                    r.hill.line[i].state != NETLINE_AWAY)
                    g->hill_busy = 1;
    }
}

int content_update_gate(char *why, int size)
{
    CuGate g;
    int r;
    gate_state(&g);
    r = cu_gate(&g);
    if (why && size > 0) snprintf(why, (size_t)size, "%s", cu_refusal_text(r));
    return r;
}

int content_update_request(const char *where, char *why, int size)
{
    int r = content_update_gate(why, size);
    if (r != CU_OK) {
        fprintf(stderr, "[CONTENT] UPDATE NOW (%s) refused: %s\n", where ? where : "?", cu_refusal_text(r));
        return r;
    }
    g_requested_at = now_ms();
    InterlockedExchange(&g_requested, 1);
    fprintf(stderr, "[CONTENT] UPDATE NOW (%s): %d pack(s) waiting; the game closes, the launcher gets them and starts "
                    "it again\n", where ? where : "?", content_update_news(NULL, 0));
    fflush(stderr);
    return CU_OK;
}

int content_update_closing(void) { return g_requested != 0; }

/* The game closes as its window's close does (d3d11_translator.c: the pads stopped, the logs flushed, the process
 * ends); saves are written as they are made, so only one being written right now is waited for. */
static void close_for_update(void)
{
    static int done;
    int child = has_flag("--launcher-child");
    if (done) return;
    if (memcard_writing() && now_ms() - g_requested_at < CU_SAVE_WAIT_MS) {
        static int said;
        if (!said++) fprintf(stderr, "[CONTENT] UPDATE NOW: a save is being written; closing once it is done\n");
        return;
    }
    done = 1;
    if (getenv("RECOMP_TEST_UPDATE_NOW_DRY")) {
        fprintf(stderr, "[CONTENT] UPDATE NOW (test): would close with code %s and %s\n",
                child ? "FFUD" : "0", child ? "let the waiting launcher open in update mode"
                                            : "start the launcher in update mode itself");
        fflush(stderr);
        return;
    }
#if defined(_WIN32)
    if (!child && !pc_settings_start_content_update()) {
        fprintf(stderr, "[CONTENT] UPDATE NOW: the launcher could not be started; the game stays open\n");
        fflush(stderr);
        InterlockedExchange(&g_requested, 0);
        done = 0;
        return;
    }
    pc_input_stop();            /* a pad must not be left vibrating */
    fprintf(stderr, "[CONTENT] UPDATE NOW: the game closes (%s)\n",
            child ? "the launcher waiting for it opens in update mode" : "the launcher was started in update mode");
    fflush(stderr);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), child ? CU_EXIT_CODE : 0);
#endif
}

void content_update_frame(int online_page)
{
    unsigned long long now = now_ms();
    int in_fight = netplay_in_fight();
    cu_offer_frame(&g_offer, in_fight);
    if (cu_check_due(g_last_check, now, in_fight, account_state() == ACCOUNT_IN, online_page)) {
        g_last_check = now;
        pc_mods_check_start();          /* one at a time; the news shows once it wrote news.ini */
    }
    if (g_requested) close_for_update();
}

int content_update_take_open_hub(void)
{
    if (g_hub_opened || !content_update_restarted()) return 0;
    g_hub_opened = 1;
    fprintf(stderr, "[CONTENT] started again by UPDATE NOW: opening the online hub\n");
    return 1;
}

int content_update_auto_play(void)
{
    if (g_auto_played || !content_update_restarted()) return 0;
    g_auto_played = 1;
    fprintf(stderr, "[CONTENT] launcher in UPDATE NOW mode: PLAY once the new community content is in\n");
    fflush(stderr);
    return 1;
}

int content_update_offer_due(int on_main_menu)
{
    const char *popup = getenv("RECOMP_TEST_CONTENT_POPUP");
    int news;
    if (g_requested || (popup && !strcmp(popup, "0"))) return 0;
    news = content_update_news(NULL, 0);
    if (!cu_offer_popup_due(&g_offer, news, on_main_menu, now_ms())) return 0;
    fprintf(stderr, "[CONTENT] popup: new community content is out (%d pack%s)\n", news, news == 1 ? "" : "s");
    g_offer.offered_news = news;        /* shown: not again for this news until a fight or newer news */
    g_offer.fight_since_menu = 0;
    return 1;
}

void content_update_offer_answer(int later)
{
    cu_offer_answered(&g_offer, content_update_news(NULL, 0), later, now_ms());
    fprintf(stderr, "[CONTENT] popup: %s\n", later ? "LATER (quiet for 30 minutes)" : "UPDATE NOW");
}

int content_update_toast(char *out, int size, int in_menu)
{
    int news = g_requested ? 0 : content_update_news(NULL, 0);
    if (!cu_offer_toast(&g_offer, news, in_menu, now_ms())) return 0;
    if (out && size > 0)
        snprintf(out, (size_t)size, "New community content is out (%d pack%s). UPDATE NOW: on the main menu or the "
                 "online hub.", news, news == 1 ? "" : "s");
    return 1;
}

void content_update_note_refusal(void) { InterlockedExchange(&g_refusal_offer, 1); }
int content_update_take_refusal_offer(void) { return InterlockedExchange(&g_refusal_offer, 0) != 0; }
