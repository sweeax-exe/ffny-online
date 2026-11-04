/* Cheats: the [cheats] settings and the Story one-shots (issue #71). See cheats.c. */
#ifndef DEFJAM_CHEATS_H
#define DEFJAM_CHEATS_H

#include <stddef.h>
#include <stdint.h>
#include "recomp_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Who a fight cheat applies to: [cheats] infinite_health and max_blazin. */
enum { CHEAT_NOBODY, CHEAT_PLAYER1, CHEAT_PLAYERS, CHEAT_EVERYONE };
/* [cheats] fighter_size. */
enum { CHEAT_SIZE_NORMAL, CHEAT_SIZE_GIANT, CHEAT_SIZE_TINY };

#define CHEAT_STYLE_COUNT 5     /* kickboxing, street fighting, martial arts, wrestling, submissions */
#define CHEAT_STYLE_NONE  0xFF  /* an empty hybrid style byte */
#define CHEAT_MAX_CASH    999999u
#define CHEAT_MAX_DEVPTS  99999u
#define CHEAT_CREATED_HERO 99   /* the fighter id Game.StartGame is given for the Story's created hero */
#define CHEAT_FIGHTER_MAX  141  /* the highest fighter id in fighter.xml */

/* Fill `out` with the [cheats] settings. Returns the count. */
size_t cheats_settings(RecompSetting *out, size_t max);

/* Read the [cheats] settings again; at start-up and after any change. */
void cheats_apply_settings(void);

/* The Story profile the game has loaded, as the overlay shows it. `valid` is 0 outside Story. */
typedef struct CheatStory {
    int valid;
    uint32_t cash, devpts;
    int style[3];               /* main, hybrid 1, hybrid 2; CHEAT_STYLE_NONE for an empty hybrid */
} CheatStory;
int cheats_story_info(CheatStory *out);

/* One-shot Story changes. They are queued and applied on the game's thread, at the next update
 * of the module manager (every frame) or the next front-end call; cheats_story_status() then says what happened. */
enum { CHEAT_STORY_CASH = 1, CHEAT_STORY_DEVPTS, CHEAT_STORY_STYLES };
void cheats_story_request(int action, int main_style, int hybrid1, int hybrid2);
const char *cheats_story_status(void);

/* The style id the game's GameCoreFightStyleToID (sub_001A4700) gives a combination, from its
 * table at 0x2EBF6F; hybrids are -1 when empty. Exposed for the unit test. */
uint32_t cheats_style_table_index(int main_style, int hybrid1, int hybrid2);

/* Guest hooks (src/recomp_manual.c). */
void cheats_fight_step(void);                    /* sub_00087700_enter: one simulation step */
void cheats_health_change(void);                 /* sub_001A50B0_enter: health += delta */
void cheats_match_start(void);                   /* sub_001A4D90_enter: the match is being built */
void cheats_game_tick(void);                     /* sub_00077040_enter: the module manager, every frame */
void cheats_on_funccall(const char *text);       /* each front-end call */
void cheats_start_game(uint32_t gamedata);       /* Game.StartGame, before the game reads the fighters */
void cheats_start_game_done(uint32_t gamedata);  /* Game.StartGame, after it returned */

#ifdef __cplusplus
}
#endif

#endif
