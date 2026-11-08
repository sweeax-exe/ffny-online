/*
 * Every player-facing setting, and the settings file, with nothing of the running game.
 *
 * The game (pc_settings.c) and the app that sets it up and starts it (src/app) both register this
 * catalogue: the display and texture table here, the input tables (pc_input.c, whose defaults come
 * from the toolkit's input map), the launcher's and overlay's own (pc_ui_settings.c) and [debug]
 * (here). The settings library keeps lines it does not know, so an app and a game of different
 * versions can share one file without losing each other's values.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>

#include "recomp_settings.h"
#include "pc_settings_catalog.h"
#include "pc_settings.h"
#include "pc_input.h"
#include "pc_ui.h"
#include "pc_debug.h"
#include "pc_language.h"
#include "pc_songs.h"
#include "patches/cheats.h"

/* Picture shape: the console's 4:3, stretched, or widescreen (docs/widescreen); the widescreen
 * choices are indices 2.. and are read once at start-up (src/patches/widescreen.c). */
static const char *const k_aspect[] = { "4:3", "stretch", "widescreen (auto)", "16:9", "16:10",
                                        "21:9 (experimental)", "32:9 (experimental)", NULL };
static const char *const k_filter[] = { "smooth", "sharp", "smooth (high quality)", NULL };
static const char *const k_aniso[] = { "off", "2x", "4x", "8x", "16x", NULL };
static const char *const k_msaa[] = { "off", "2x", "4x", "8x", NULL };
static const char *const k_ws_hud[] = { "auto", "edges", "centered", NULL };
static const char *const k_ws_cutscenes[] = { "authored", "fill", NULL };
static const char *const k_ws_fill[] = { "blur", "black", NULL };

static RecompSetting g_display_settings[] = {
    { "textures", "enabled", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
      "Use texture packs from the data folder's mods directory." },
    { "textures", "packs", RECOMP_SETTING_STRING, 0, 0, 0, NULL, "RECOMP_TEXTURE_PACKS", RECOMP_SETTING_RESTART,
      "Pack folder names separated by semicolons. Later packs take precedence.", "faithful-hd" },
    { "textures", "cache_mb", RECOMP_SETTING_INT, PC_TEXTURE_CACHE_MB, 32, 4096, NULL, "RECOMP_TEXTURE_CACHE_MB", RECOMP_SETTING_RESTART,
      "Limits memory used by Upscaled Original Textures and downloadable texture packs. "
      "Try increasing it if textures pop in, load slowly, or cause repeated stutters: a low budget can force textures to unload and reload. "
      "Higher values use more graphics and system memory, including retained upload data. "
      "Leave memory for the rest of the game and other apps. Restart the game after changing this." },
    { "textures", "dump", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
      "Capture source textures for making a pack, in hd-work/runtime under the data folder." },
    { "textures", "dump_limit", RECOMP_SETTING_INT, 2000, 0, 65536, NULL, "RECOMP_TEXTURE_DUMP_LIMIT", RECOMP_SETTING_RESTART,
      "Maximum new source images captured in one run." },
    { "display", "fullscreen", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, 0,
      "Borderless full screen. Alt+Enter or F11 switches while playing." },
    { "display", "window_width", RECOMP_SETTING_INT, 1920, 320, 16384, NULL, NULL, 0,
      "Window size when not full screen. Resizing the window updates these." },
    { "display", "window_height", RECOMP_SETTING_INT, 1080, 240, 16384, NULL, NULL, 0, NULL },
    { "display", "aspect", RECOMP_SETTING_ENUM, 2, 0, 0, k_aspect, "RECOMP_ASPECT", 0,
      "4:3 keeps the console's picture shape with black bars; stretch fills the window; the widescreen shapes "
      "(beta) widen the 3D view and keep menus, HUD and movies in shape, and start with the game." },
    { "display", "widescreen_hud", RECOMP_SETTING_ENUM, 0, 0, 0, k_ws_hud, NULL, RECOMP_SETTING_RESTART,
      "Where the fight HUD goes in widescreen: the screen edges, or a centred 16:9 box (auto: edges up to 16:9)." },
    { "display", "widescreen_cutscenes", RECOMP_SETTING_ENUM, 0, 0, 0, k_ws_cutscenes, NULL, RECOMP_SETTING_RESTART,
      "Story cutscenes in widescreen: authored keeps the game's own 16:9 framing, fill always fills the screen." },
    { "display", "widescreen_side_fill", RECOMP_SETTING_ENUM, 0, 0, 0, k_ws_fill, NULL, RECOMP_SETTING_RESTART,
      "Beside 4:3 pictures (movies, menu backgrounds) in widescreen: a soft blurred extension, or black." },
    { "display", "widescreen_fov_cap", RECOMP_SETTING_INT, 150, 90, 170, NULL, NULL, RECOMP_SETTING_RESTART,
      "Widest horizontal field of view widescreen may use, in degrees (only very wide screens reach it)." },
    { "display", "render_scale", RECOMP_SETTING_INT, 2, 1, 4, NULL, "RECOMP_RENDER_SCALE",
      RECOMP_SETTING_RESTART,
      "Internal render height: 1x = 480p, 2x = 960p, 3x = 1440p, 4x = 1920p. "
      "1080p output: 2x balances speed and clarity; 3x is sharper. 1440p output: 3x. "
      "Width follows Picture shape; window size is separate. Higher scales cost GPU time. "
      "Applies on Play/next launch." },
    { "display", "filter", RECOMP_SETTING_ENUM, 0, 0, 0, k_filter, "RECOMP_PRESENT_FILTER", 0,
      "How the picture is scaled to the window. smooth (high quality) also averages a picture rendered "
      "larger than the window instead of skipping pixels (render scale above the window's size)." },
    { "display", "msaa", RECOMP_SETTING_ENUM, 0, 0, 0, k_msaa, "RECOMP_MSAA", RECOMP_SETTING_RESTART,
      "Multisample anti-aliasing: smooths the jagged edges of fighters and the arena, as the console's "
      "own anti-aliasing did. Costs GPU time and memory; lowered to what the graphics card supports." },
    { "display", "texture_mips", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, "RECOMP_TEXTURE_MIPS", 0,
      "Use the smaller texture levels the game ships, as the console does, so distant floors and "
      "crowds do not shimmer." },
    { "display", "anisotropy", RECOMP_SETTING_ENUM, 0, 0, 0, k_aniso, "RECOMP_ANISOTROPY", 0,
      "Anisotropic filtering: keeps floors and walls seen at a low angle sharp. Works best with the "
      "game's texture levels on." },
    { "display", "vsync", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_PRESENT_VSYNC", 0,
      "Pace frames on the display for even motion. Used when the refresh rate is a multiple of 60 (60, 120, 180, 240 Hz); other displays use the game's own 60 fps timer." },
    { "display", "gamma", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_GAMMA",
      RECOMP_SETTING_RESTART,
      "Apply the game's own brightness curve, as the console does." },
    { "display", "brightness", RECOMP_SETTING_INT, 100, 50, 200, NULL, "RECOMP_BRIGHTNESS", 0,
      "Brightness of the picture's mid-tones; black and white stay as they are. 100 is the console's picture, "
      "higher is brighter." },
    { "display", "vignette", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_VIGNETTE", 0,
      "The darkened screen edges that venues and scenes draw. Off removes them." },
    { "display", "vignette_strength", RECOMP_SETTING_INT, 100, 0, 100, NULL, "RECOMP_VIGNETTE_STRENGTH", 0,
      "How dark the vignette is, in percent of the game's own (100)." },
    { "display", "color_tint", RECOMP_SETTING_INT, 100, 0, 100, NULL, "RECOMP_COLOR_TINT", 0,
      "Strength of the colour tint venues and scenes put over the picture, in percent of the game's own "
      "(100); 0 shows the picture untinted." },
    { "display", "scratches", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_SCRATCHES", 0,
      "The film scratches some scenes draw over the picture. Off removes them." },
};
#define DISPLAY_SETTING_COUNT (sizeof(g_display_settings) / sizeof(g_display_settings[0]))

size_t pc_debug_settings(RecompSetting *out, size_t max)
{
    size_t n = 0;
    if (max < 11) return 0;
    memset(out, 0, sizeof(*out) * 11);
#define ADD(...) do { RecompSetting s_ = { __VA_ARGS__ }; out[n++] = s_; } while (0)
    ADD("debug", "record", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, "RECOMP_RECORD", RECOMP_SETTING_RESTART,
        "Keep a play-test record of every run under logs/sessions: frame times, events, the sound and the last "
        "seconds of pictures. The mark key writes a bug bundle of those seconds. See docs/playtest-recorder.md.");
    ADD("debug", "mark_key", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
        "While recording, the key that marks a moment and writes a bug bundle of the seconds around it.", "F9");
    ADD("debug", "mark_pad", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
        "While recording, pad buttons that mark a moment, for a pad or a Steam Deck. Alternatives are separated "
        "by commas; a combination joined with + must be held for half a second. Button names as in [gamepad]. "
        "Empty for none.", "back+left_stick_click");
    ADD("debug", "record_fps", RECOMP_SETTING_INT, 10, 0, 30, NULL, "RECOMP_RECORD_FPS", RECOMP_SETTING_RESTART,
        "Pictures a second kept for bundles (0 for none). Each one is read back from the graphics card, which "
        "costs that frame a little; frames.csv records how much.");
    ADD("debug", "record_seconds", RECOMP_SETTING_INT, 30, 5, 120, NULL, "RECOMP_RECORD_SECONDS",
        RECOMP_SETTING_RESTART, "How many seconds before a mark a bundle holds.");
    ADD("debug", "record_audio", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
        "Also keep the mixed sound (about 11 MB a minute; older sessions lose it, see keep_sessions).");
    ADD("debug", "record_game_state", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
        "Also log the fighters, health and match events (the test harness's read-only probes).");
    ADD("debug", "auto_bundle_hitch_ms", RECOMP_SETTING_INT, 0, 0, 5000, NULL, "RECOMP_RECORD_AUTO_HITCH_MS",
        RECOMP_SETTING_RESTART,
        "A frame at least this long writes a bundle by itself (at most five a run, a minute apart). 0: only marks.");
    ADD("debug", "hang_seconds", RECOMP_SETTING_INT, 15, 0, 600, NULL, "RECOMP_RECORD_HANG_SECS",
        RECOMP_SETTING_RESTART, "No frame for this many seconds writes a hang bundle. 0: never.");
    ADD("debug", "keep_sessions", RECOMP_SETTING_INT, 20, 1, 500, NULL, NULL, RECOMP_SETTING_RESTART,
        "Sessions kept whole; older ones keep their bundles and events only.");
    ADD("debug", "record_guest_ram", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
        "Crash and hang bundles also hold the game's 64 MB of memory, for offline inspection.");
#undef ADD
    return n;
}

size_t pc_settings_catalog(RecompSetting *out, size_t max)
{
    size_t n = DISPLAY_SETTING_COUNT;
    if (max < PC_SETTINGS_MAX) return 0;
    memcpy(out, g_display_settings, sizeof(g_display_settings));
    n += pc_input_settings(out + n, 64);
    n += pc_ui_settings(out + n, 16);
    n += pc_debug_settings(out + n, 16);
    n += pc_language_settings(out + n, 4);
    n += pc_songs_settings(out + n, 8);
    n += cheats_settings(out + n, 8);
    return n;
}

static RecompSetting g_settings[PC_SETTINGS_MAX];
static char g_path[4096];   /* empty: do not load or save */

static void settings_log(const char *message)
{
    fprintf(stderr, "[SETTINGS] %s\n", message);
}

int pc_settings_open(const char *path)
{
    size_t count = pc_settings_catalog(g_settings, PC_SETTINGS_MAX);
    int read;

    snprintf(g_path, sizeof g_path, "%s", path ? path : "");
    recomp_settings_register(g_settings, count);
    recomp_settings_set_log(settings_log);
    read = g_path[0] ? recomp_settings_load(g_path) : 0;
    if (!g_path[0])
        fprintf(stderr, "[SETTINGS] defaults only (RECOMP_SETTINGS=none)\n");
    else if (read < 0)
        fprintf(stderr, "[SETTINGS] could not read %s; using defaults\n", g_path);
    else {
        fprintf(stderr, "[SETTINGS] %s (%d value(s) read)\n", g_path, read);
        /* Write it out on first run, and after an upgrade adds a setting, so
         * there is always a complete, commented file to edit. */
        if (read < (int)count && recomp_settings_save(g_path))
            fprintf(stderr, "[SETTINGS] could not write %s\n", g_path);
    }
    return read;
}

int pc_display(const char *key, int fallback)
{
    return recomp_settings_get("display", key, fallback);
}

const char *pc_settings_path(void) { return g_path; }

int pc_settings_save_checked(void)
{
    int error = g_path[0] ? recomp_settings_save(g_path) : 0;
    if (error) fprintf(stderr, "[SETTINGS] could not write %s\n", g_path);
    return error ? 1 : 0;
}
void pc_settings_save(void) { (void)pc_settings_save_checked(); }

/* Keep this ownership list identical for both directions. HD is a separate optional install;
 * Original disables replacements without deleting packs. Modern preserves their selection. */
static const struct { const char *key; int original, modern; } k_experience[] = {
    {"aspect", 0, 2}, {"render_scale", 1, 2}, {"filter", 0, 2},
    {"msaa", 1, 1}, {"texture_mips", 1, 1}, {"anisotropy", 0, 4},
    {"vsync", 1, 1}, {"gamma", 1, 1}, {"brightness", 100, 100},
    {"vignette", 1, 1}, {"vignette_strength", 100, 100},
    {"color_tint", 100, 100}, {"scratches", 1, 1},
    {"widescreen_hud", 0, 0}, {"widescreen_cutscenes", 0, 0},
    {"widescreen_side_fill", 0, 0}, {"widescreen_fov_cap", 150, 150}
};
int pc_experience_current(void)
{
    int original = !recomp_settings_get("textures", "enabled", 0), modern = 1;
    for (size_t i = 0; i < sizeof(k_experience)/sizeof(k_experience[0]); ++i) {
        int v = recomp_settings_get("display", k_experience[i].key, -1);
        original &= v == k_experience[i].original;
        modern &= v == k_experience[i].modern;
    }
    return original ? 0 : modern ? 1 : -1;
}
int pc_experience_apply(int modern)
{
    if (modern != 0 && modern != 1) return -1;
    /* An environment override must not leave a silently partial preset. */
    for (size_t i = 0; i < sizeof(k_experience)/sizeof(k_experience[0]); ++i) {
        RecompSetting *s = recomp_settings_find("display", k_experience[i].key);
        if (!s || s->source == RECOMP_SETTING_FROM_ENV) return -1;
    }
    RecompSetting *textures = recomp_settings_find("textures", "enabled");
    RecompSetting *packs = recomp_settings_find("textures", "packs");
    if (!modern && ((textures && textures->source == RECOMP_SETTING_FROM_ENV) ||
                   (packs && packs->source == RECOMP_SETTING_FROM_ENV))) return -1;
    for (size_t i = 0; i < sizeof(k_experience)/sizeof(k_experience[0]); ++i)
        recomp_settings_set("display", k_experience[i].key,
                            modern ? k_experience[i].modern : k_experience[i].original);
    if (!modern) recomp_settings_set("textures", "enabled", 0);
    return pc_settings_save_checked();
}

void pc_display_set(const char *key, int value)
{
    if (recomp_settings_set("display", key, value) > 0 && g_path[0]) {
        if (recomp_settings_save(g_path))
            fprintf(stderr, "[SETTINGS] could not write %s\n", g_path);
    }
}
