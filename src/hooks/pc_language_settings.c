/* Language settings shared by the game, app and native fixtures. */
#include <string.h>
#include "recomp_settings.h"
#include "pc_language.h"

static const char *const k_subtitles[] = { "auto", "all cutscenes", "as the game does", NULL };
static const char *const k_game[] = { "english", "french", NULL };

size_t pc_language_settings(RecompSetting *out, size_t max)
{
    size_t n = 0;
    if (max < 3) return 0;
    memset(out, 0, sizeof(*out) * 3);
#define ADD(...) do { RecompSetting s_ = { __VA_ARGS__ }; out[n++] = s_; } while (0)
    ADD("language", "pack", RECOMP_SETTING_STRING, 0, 0, 0, NULL, "RECOMP_LANGUAGE_PACK", RECOMP_SETTING_RESTART,
        "Language mod: a folder in the mods folder with language.ini (the Mods page sets it); empty plays the "
        "game in its own English.");
    ADD("language", "story_subtitles", RECOMP_SETTING_ENUM, 0, 0, 0, k_subtitles, "RECOMP_STORY_SUBTITLES",
        RECOMP_SETTING_RESTART,
        "Subtitles in Story cutscenes: auto follows the language pack (a translation shows them in every "
        "cutscene), or choose.");
    ADD("language", "game", RECOMP_SETTING_ENUM, 0, 0, 0, k_game, "RECOMP_GAME_LANGUAGE", RECOMP_SETTING_RESTART,
        "The game's own language: english, or the french built into the game (text and Story subtitles; the "
        "voices stay English). A language mod takes its place.");
#undef ADD
    return n;
}
