/* Shared cheat settings for the game and the game-free app (PR #97). */
#include <string.h>
#include "cheats.h"

static const char *const k_targets[] = { "off", "player 1", "every player", "everyone", NULL };
static const char *const k_sizes[] = { "normal", "giant", "tiny", NULL };

size_t cheats_settings(RecompSetting *out, size_t max)
{
    size_t n = 0;
    if (max < 5) return 0;
    memset(out, 0, sizeof(*out) * 5);
#define ADD(...) do { RecompSetting s_ = { __VA_ARGS__ }; out[n++] = s_; } while (0)
    ADD("cheats", "infinite_health", RECOMP_SETTING_ENUM, 0, 0, 0, k_targets, "RECOMP_CHEAT_INFINITE_HEALTH", 0,
        "Cheat: fighters who lose no health. Player 1 is the first fighter on a pad or the keyboard; every player "
        "is every fighter not played by the computer. Hits still land and react. Lasts while it is on; it is "
        "never saved into a profile.");
    ADD("cheats", "max_blazin", RECOMP_SETTING_ENUM, 0, 0, 0, k_targets, "RECOMP_CHEAT_MAX_BLAZIN", 0,
        "Cheat: the momentum meter stays full and Blazin' is always ready; start it as usual. Applies to the same "
        "choice of fighters as infinite health. Never saved into a profile.");
    ADD("cheats", "no_regen", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, "RECOMP_CHEAT_NO_REGEN", 0,
        "Cheat: no fighter gets health back by itself during a fight. Never saved into a profile.");
    ADD("cheats", "fighter_size", RECOMP_SETTING_ENUM, 0, 0, 0, k_sizes, "RECOMP_CHEAT_FIGHTER_SIZE", 0,
        "Cheat: giant or tiny fighters. Only the picture changes: reach and collision stay as they are.");
    ADD("cheats", "story_hero", RECOMP_SETTING_INT, -1, -1, CHEAT_FIGHTER_MAX, NULL, "RECOMP_CHEAT_STORY_HERO", 0,
        "Cheat: in Story fights, play as another fighter, with that fighter's look and moves, instead of your "
        "created fighter. The fighter's ID from fighter.xml; -1 is off. Only fights change: cutscenes, the crib "
        "and the shops still show your own fighter. Nothing is saved into the profile.");
#undef ADD
    return n;
}
