/*
 * The launcher's and overlay's own settings, and the rule for when the launcher shows.
 *
 * Kept in plain C, apart from the Dear ImGui code, so the rule can be unit tested without a
 * window and the settings table builds without C++.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <string.h>

#include "pc_ui.h"

static const char *const k_button_icons[] = { "auto", "xbox", "playstation", NULL };

size_t pc_ui_settings(RecompSetting *out, size_t max)
{
    size_t n = 0;
    if (max < 8) return 0;
    memset(out, 0, sizeof(*out) * 8);

#define ADD(...) do { RecompSetting s_ = { __VA_ARGS__ }; out[n++] = s_; } while (0)
    ADD("launcher", "skip", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, "RECOMP_NO_LAUNCHER", 0,
        "Skip the launcher and start the game at once. Hold Shift while the game starts, start it with "
        "--launcher, or turn this off in the in-game overlay to see the launcher again.");
    ADD("debug", "unlock_all", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, "RECOMP_UNLOCK_ALL", RECOMP_SETTING_RESTART,
        "Debug: every fighter and venue is available on any profile, through the developers' own unlock "
        "routine in the game. Meant for testing; it changes the profile in memory and asks the game to save "
        "it, so do not use it on a save you care about.");
    ADD("debug", "skip_intro", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, "RECOMP_SKIP_INTRO", RECOMP_SETTING_RESTART,
        "Start at Press Start, skipping the startup legal screen and movies. Story cutscenes still play. "
        "Applies when starting the game; changing it in the overlay requires a restart.");
    ADD("ui", "overlay_key", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, 0,
        "The key that opens and closes the in-game overlay, one key name (see the [keyboard] section).",
        "F1");
    ADD("ui", "overlay_pad", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, 0,
        "Pad buttons that open and close the overlay. Alternatives are separated by commas; a combination "
        "of buttons joined with + must be held for half a second. Button names as in [gamepad]. "
        "Empty for none.",
        "left_stick_click+right_stick_click");
    ADD("ui", "scale", RECOMP_SETTING_INT, 100, 60, 250, NULL, NULL, 0,
        "Size of the launcher and overlay text and controls in percent, on top of your display's scaling.");
    ADD("launcher", "gpu_notice_seen", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, 0,
        "The launcher told you once that an integrated or small graphics card runs better with HD textures "
        "off and render scale 1 (0 shows the notice again).");
    ADD("ui", "button_icons", RECOMP_SETTING_ENUM, 0, 0, 0, k_button_icons, "RECOMP_BUTTON_ICONS", 0,
        "The button symbols on the online screens: auto shows PlayStation symbols (cross, circle, square, "
        "triangle) when player 1 plays with a PlayStation controller and Xbox letters otherwise, also on "
        "the keyboard. The game's own menus keep their Xbox letters.");
#undef ADD
    return n;
}

/* A pad's name says PlayStation: SDL's names (PS3/PS4/PS5 Controller, DualSense Wireless Controller),
 * Sony's own ("Wireless Controller", the DualShock 4 over USB) and the common drivers' (DualShock,
 * Sixaxis). Case does not matter. */
int pc_ui_pad_is_playstation(const char *name)
{
    static const char *const words[] = { "dualsense", "dualshock", "playstation", "sixaxis", "sony",
                                         "ps3", "ps4", "ps5", NULL };
    char low[128];
    size_t i;
    if (!name || !*name) return 0;
    for (i = 0; name[i] && i + 1 < sizeof(low); i++)
        low[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    low[i] = 0;
    if (!strcmp(low, "wireless controller") || !strcmp(low, "wireless controller + keyboard")) return 1;
    for (i = 0; words[i]; i++)
        if (strstr(low, words[i])) return 1;
    return 0;
}

int pc_ui_button_icons_pick(int setting, int slot_what, const char *slot_name, int sdl_playstation)
{
    if (setting == PC_UI_ICONS_XBOX) return PC_UI_ICONS_XBOX;
    if (setting == PC_UI_ICONS_PLAYSTATION) return PC_UI_ICONS_PLAYSTATION;
    if (!(slot_what & 1)) return PC_UI_ICONS_XBOX;      /* no pad: the keyboard (or nothing) */
    return sdl_playstation > 0 || pc_ui_pad_is_playstation(slot_name) ? PC_UI_ICONS_PLAYSTATION : PC_UI_ICONS_XBOX;
}

int pc_launcher_wanted(int skip_setting, int force_flag, int no_flag, int shift_held, int test_run)
{
    if (test_run) return 0;
    if (no_flag) return 0;
    if (force_flag || shift_held) return 1;
    return skip_setting ? 0 : 1;
}
