/* Language settings shared by the game, app and native fixtures. */
#include <string.h>
#include "recomp_settings.h"
#include "pc_language.h"

static const char *const k_subtitles[] = { "auto", "all cutscenes", "as the game does", NULL };
static const char *const k_game[] = { "english", "french", NULL };

size_t pc_language_settings(RecompSetting *out, size_t max)
{
    size_t n = 0;
    if (max < 7) return 0;
    memset(out, 0, sizeof(*out) * 7);
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
    /* FFNY Online's synced catalogue packs (pc_content.c): the replacements this player switched off. */
    ADD("content", "off", RECOMP_SETTING_STRING, 0, 0, 0, NULL, "RECOMP_CONTENT_OFF", RECOMP_SETTING_RESTART,
        "Community packs switched off on this PC (pack ids, comma-separated; the launcher's Content page sets "
        "it). Only retextures and language packs can be off; new fighters, clothing and venues are always on.");
    /* Build 82's NEW badges (pc_content.c): a pack whose fighter or venue the player picked loses it. */
    ADD("content", "picked", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, 0,
        "Community packs whose new fighter or venue you have picked (pack ids, newest first): they no longer show "
        "NEW. The game writes it.");
    /* Build 90: FFNY Online's community content in offline play (pc_content.c, content_update_logic.h). */
    ADD("content", "community_offline", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_CONTENT_OFFLINE",
        RECOMP_SETTING_RESTART,
        "Use FFNY Online's community content in offline play: its new fighters, clothing, venues, skins, maps and "
        "retextures. Off starts the game with the original data only (your own packs still apply) and offline "
        "Battle and Story show the standard fighters and venues. Nothing is deleted; on again needs no download. "
        "Online always needs it on: the game then offers a restart with it, for that session only.");
    /* Build 90: "Turn all mods off" (mods_master_logic.h): the state to bring back; empty when the switch was not used. */
    ADD("content", "mods_backup", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, 0,
        "What was on before the launcher's Turn all mods off (builds 90 to 93; build 94 keeps it in "
        "mod-manager/mods-backup.txt, and this stays empty).");
#undef ADD
    return n;
}
