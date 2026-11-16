/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 87, the notice of content to come (content_release.c; the rules and texts in content_release_logic.h):
 * accepted community packs go live together at a release window, and the game says so beforehand, in the place
 * and the look of build 86's "new community content is out" notice. Once the time has come, that notice (UPDATE
 * NOW, content_update.c) takes over. */
#ifndef DEFJAM_CONTENT_RELEASE_H
#define DEFJAM_CONTENT_RELEASE_H

#include "content_release_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* content_update_frame, once a frame (render thread). check_due: the catalogue check is due this frame (every ten
 * minutes in a menu), so the schedule is asked for with it; nothing here has a timer of its own. Returns 1 when
 * the release time has just passed and the caller should look at the catalogue now. */
int content_release_frame(int check_due, int in_fight);
/* The notice's two lines while it shows (1): "NEW CONTENT AT 17:00" and "3 new fighters, 1 skin - in 2h 15m". */
int content_release_lines(char *head, int head_size, char *detail, int detail_size);
/* The same for the online screens' gallery page: a fixed sample (17:00, in 2h 15m, 3 new fighters and 1 skin). */
void content_release_sample_lines(char *head, int head_size, char *detail, int detail_size);
/* The corner notice in a menu (not in a fight): 1 and its text while it shows. */
int content_release_toast(char *out, int size, int in_menu);

#ifdef __cplusplus
}
#endif

#endif
