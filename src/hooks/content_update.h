/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 86, new community content: the check in every menu, the notice and UPDATE NOW (content_update.c; the rules
 * in content_update_logic.h, which also describes the hand-off to the launcher). */
#ifndef DEFJAM_CONTENT_UPDATE_H
#define DEFJAM_CONTENT_UPDATE_H

#include "content_update_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The overlay, once a frame (render thread): the catalogue check every 10 minutes in any menu when signed in (and
 * on the online screens, online_page), the fight bookkeeping for the popup, and a taken UPDATE NOW. */
void content_update_frame(int online_page);
/* The packs the next sync brings (pc_content_news, read every 3 s); names " | " between (may be NULL). */
int content_update_news(char *names, int size);
/* CU_OK when UPDATE NOW may go now, else the refusal (why: its plain words, may be NULL). */
int content_update_gate(char *why, int size);
/* UPDATE NOW pressed (where: for the log): CU_OK when taken (the game closes within a frame or two, after a save
 * being written), else the refusal (why as above). */
int content_update_request(const char *where, char *why, int size);
/* 1 once UPDATE NOW was taken (the screens say "updating"). */
int content_update_closing(void);

/* This run was started by UPDATE NOW (--content-update on the command line). */
int content_update_restarted(void);
/* Once, in a run started by UPDATE NOW: the main menu is up, open the online hub. */
int content_update_take_open_hub(void);
/* The launcher in UPDATE NOW mode: 1 once, its first frame (PLAY is pressed by itself). */
int content_update_auto_play(void);

/* The popup on the main menu ("New community content is out (N packs). Update now?"). on_main_menu: the main menu
 * is up with no fight, session or room. RECOMP_TEST_CONTENT_POPUP=0 (tests) never shows it. */
int content_update_offer_due(int on_main_menu);
/* Its answer: later=1 is LATER (30 minutes of quiet), 0 UPDATE NOW (or closed by the menu going away). */
void content_update_offer_answer(int later);
/* The corner notice in a menu (not in a fight): 1 and its text while it shows. */
int content_update_toast(char *out, int size, int in_menu);

/* The relay refused a room for other community packs (ERROR 15) while news is out: the online screens offer
 * UPDATE NOW (any thread); take_refusal_offer: once. */
void content_update_note_refusal(void);
int content_update_take_refusal_offer(void);

#ifdef __cplusplus
}
#endif

#endif
