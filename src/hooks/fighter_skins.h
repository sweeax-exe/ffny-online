/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Community skins (build 85): a new-fighter pack the team accepted as a SKIN is an extra outfit of its template, a
 * stock fighter: the same gameplay, a new look. It has no front-end ID of its own (it takes none of the 28 places
 * for new fighters) and no Character row; the launcher composes its model files, camera banks and IntroExtro entry
 * as for a new fighter (setup/content_compose.py apply_skin) and lists it in content-files/files.ini
 * (pc_content_skins). A player who picks the template and one of its skins plays the template with the skin's
 * model: the slot's model is swapped at match start (fighter_skins_start_game_done), so every gameplay value stays
 * the template's.
 *
 * The skins of one template are numbered from 1 in catalogue order (0 is the template's own look, "Default"). Two
 * PCs with the same content print have the same skins in the same order, so online the number travels in READY and
 * START (CAPS_SKINS, netplay.c) and every PC of the match, spectators included, loads the same model. A number this
 * PC does not know plays the template's own look. CPU fighters and random picks are Default.
 *
 * The last skin a player chose for a fighter is kept per data folder (<data>/skin-choices.ini, by pack id), so the
 * skin window opens on it next time; a skin that is gone is Default again, silently. */
#ifndef DEFJAM_FIGHTER_SKINS_H
#define DEFJAM_FIGHTER_SKINS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIGHTER_SKINS_PER 255       /* skins of one template the window and the wire take (build 94: 1..255; before, 15) */
#define FIGHTER_SKINS_WIRE_OLD 15   /* what a relay or game without CAPS_SKINSWIDE carries (READY's byte: 0..15): a higher
                                     * skin is Default for the whole match unless every player has the wide wire */

typedef struct FighterSkin {
    char id[96];                    /* the pack id */
    int template_fe_id;
    int index;                      /* 1..FIGHTER_SKINS_PER (its place among the template's skins, catalogue order) */
    char model[16];                 /* V2IP_7xxA */
    char name[48];                  /* the name in the game */
    char picture[1024];             /* <data>/online-art/skins/<id>.png ("" when the pack has no body art) */
} FighterSkin;

/* Read the composed skins again (cheap; content-files/files.ini and content.ini). */
void fighter_skins_refresh(void);
/* The skins of stock fighter fe_id (0 none), and skin `index` (1..count) of it, or NULL. */
int fighter_skins_count(int fe_id);
int fighter_skin_get(int fe_id, int index, FighterSkin *out);

/* The skin the player chose last for fe_id: its number now (0 Default, also for a skin that is gone). */
int fighter_skins_remembered(int fe_id);
void fighter_skins_remember(int fe_id, int index);

/* The match's looks, per set-up slot (Controller.ControllerSetup's first argument): fe_id and skin (0 Default).
 * fighter_skins_clear_slots: every slot Default. */
void fighter_skins_set_slot(int slot, int fe_id, int skin);
void fighter_skins_clear_slots(void);
int fighter_skins_slot(int slot);

/* After Game.StartGame (src/patches/dev_unlock.c): the model of every slot with a skin. gd: the game data. */
void fighter_skins_start_game_done(uint32_t gd);

/* The FuncCall hook (src/recomp_manual.c): Controller.ControllerSetup from the select screens. Offline, a pick of a
 * fighter with skins asks for the skin window (fighter_skins_take_window). */
void fighter_skins_after_funccall(const char *text);
/* The skin window the select screen asked for: 1 and the slot and fighter, once. */
int fighter_skins_take_window(int *slot, int *fe_id);

#ifdef __cplusplus
}
#endif

#endif
