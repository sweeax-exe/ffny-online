// The settings screen, shared by the launcher and the in-game overlay, and the overlay itself.
//
// One screen edits settings.ini through the settings library, so a change made here is the value
// everywhere (the file, the hotkeys, the other front end), and is saved as it is made. The overlay
// draws over the picture in the presentation step (toolkit d3d8_present_set_overlay) and takes the
// keyboard and mouse from the window; while it is open the game is polled at rest. The launcher
// (pc_launcher.cpp) is the same screen in a window of its own before the game starts.
//
// Built with DEFJAM_APP for the app (src/app), which has no running game: the overlay and the
// Debug page's controls for this run (the recorder, Story scenes, EA's developer menu) are left out.
//
// Threads: the window's thread delivers input events, the thread that presents draws. One lock keeps
// them out of ImGui at the same time. Nothing here touches ImGui while the overlay is closed except
// to look for its hotkeys.

#define _CRT_SECURE_NO_WARNINGS
// PCUI_WIN32: the game's window on Windows, Win32 with Direct3D 11. Everything else (the game off
// Windows, and the app on every platform) is an SDL window; pc_ui_internal.h says the same.
#if defined(_WIN32) && !defined(DEFJAM_APP)
#define PCUI_WIN32 1
#endif
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <unistd.h>
#endif
#if defined(PCUI_WIN32)
#include <d3d11.h>
#else
#include <sys/stat.h>
#include <chrono>
#include <mutex>
#include <SDL3/SDL.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <filesystem>
#include <iterator>
#include "pc_ui_model.h"
#include "pc_mods.h"
#include "pc_ui_theme.h"
#include "pc_ui_actions.h"
#include "pc_launcher_home.h"

#include "imgui.h"
#if defined(PCUI_WIN32)
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#elif !defined(DEFJAM_APP)
#include "imgui_impl_vulkan.h"
#endif

extern "C" {
#include "recomp_settings.h"
#if !defined(DEFJAM_APP)
#include "recorder.h"
#include "scene_launcher.h"
#include "dev_module.h"
#include "patches/song_cursor.h"
#endif
#include "patches/cheats.h"
#include "input_host.h"
#include "input_map.h"
}
#include "pc_input.h"
#include "pc_settings.h"
#include "pc_ui.h"
#include "pc_ui_internal.h"
#include "song_convert.h"
#include "song_library.h"

#ifndef DEFJAM_VERSION
#define DEFJAM_VERSION "dev"
#endif

// ---- the lock -------------------------------------------------------------------------------

#if defined(_WIN32)
typedef ULONGLONG UiTick;
static UiTick ui_ticks() { return GetTickCount64(); }

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_cs_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_cs(PINIT_ONCE, PVOID, PVOID *)
{
    InitializeCriticalSection(&g_cs);
    return TRUE;
}

void ui_lock()
{
    InitOnceExecuteOnce(&g_cs_once, init_cs, nullptr, nullptr);
    EnterCriticalSection(&g_cs);
}

void ui_unlock() { LeaveCriticalSection(&g_cs); }
#else
typedef unsigned long long UiTick;
static UiTick ui_ticks()
{
    using namespace std::chrono;
    return (UiTick)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Recursive, like a critical section: a setting changed inside the screen calls back into it.
static std::recursive_mutex g_mutex;
void ui_lock() { g_mutex.lock(); }
void ui_unlock() { g_mutex.unlock(); }
#endif

// ---- small helpers --------------------------------------------------------------------------

static RecompSetting *find(const char *section, const char *key)
{
    return recomp_settings_find(section, key);
}

static std::string get_text(const char *section, const char *key)
{
    char buf[RECOMP_SETTING_TEXT_MAX];
    recomp_settings_get_text(section, key, buf, sizeof(buf), "");
    return buf;
}

static bool g_save_failed = false;
static void commit()
{
    pcui::clear_action_status();
    g_save_failed = pc_settings_save_checked() != 0;
}
static UiMode g_mode = UI_LAUNCHER;
struct UiTestTarget
{
    ImVec2 center;
    bool visible;
};
static std::map<std::string, UiTestTarget> g_targets;
static std::string g_reveal_target;
static std::string g_help;
void ui_target(const char *name)
{
    if (!getenv("RECOMP_LAUNCHER_SCRIPT"))
        return;
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImVec2 center((a.x + b.x) * .5f, (a.y + b.y) * .5f);
    auto *draw = ImGui::GetWindowDrawList();
    ImVec2 lo = draw->GetClipRectMin(), hi = draw->GetClipRectMax();
    bool visible =
        ImGui::IsItemVisible() && center.x >= lo.x && center.y >= lo.y && center.x < hi.x && center.y < hi.y;
    g_targets[name] = {center, visible};
    if (g_reveal_target == name && !visible)
        ImGui::SetScrollHereY(.5f);
}
bool ui_test_target(const char *name, float *x, float *y)
{
    auto it = g_targets.find(name);
    if (it == g_targets.end() || !it->second.visible)
    {
        g_reveal_target = name;
        return false;
    }
    g_reveal_target.clear();
    *x = it->second.center.x;
    *y = it->second.center.y;
    return true;
}

static const char *k_control_labels[INPUT_CONTROL_COUNT] = {
    "D-pad up",
    "D-pad down",
    "D-pad left",
    "D-pad right",
    "Start",
    "Back",
    "Left stick click",
    "Right stick click",
    "A",
    "B",
    "X",
    "Y",
    "Black",
    "White",
    "Left trigger",
    "Right trigger",
    "Left stick up",
    "Left stick down",
    "Left stick left",
    "Left stick right",
    "Right stick up",
    "Right stick down",
    "Right stick left",
    "Right stick right",
};

static const char *friendly(const char *section, const char *key)
{
    struct Label { const char *section, *key, *text; };
    static const Label labels[] = {
        {"language", "pack", "Language mod"},
        {"language", "story_subtitles", "Story cutscene subtitles"},
        {"language", "game", "Game language"},
        {"display", "fullscreen", "Full screen"},
        {"display", "window_width", "Window width"},
        {"display", "window_height", "Window height"},
        {"display", "aspect", "Aspect ratio"},
        {"display", "render_scale", "Render resolution"},
        {"display", "filter", "Image scaling"},
        {"display", "vsync", "Vsync"},
        {"display", "gamma", "Original brightness curve"},
        {"display", "brightness", "Brightness"},
        {"display", "vignette", "Vignette"},
        {"display", "vignette_strength", "Vignette strength (%)"},
        {"display", "color_tint", "Colour tint (%)"},
        {"display", "scratches", "Film scratches"},
        {"display", "texture_mips", "Reduce texture shimmer (mipmaps)"},
        {"display", "anisotropy", "Anisotropic filtering"},
        {"display", "msaa", "Anti-aliasing (MSAA)"},
        {"display", "widescreen_hud", "Fight HUD position"},
        {"display", "widescreen_cutscenes", "Story cutscenes"},
        {"display", "widescreen_side_fill", "Side bars"},
        {"display", "widescreen_fov_cap", "Widest field of view (degrees)"},
        {"textures", "enabled", "Enable texture mods"},
        {"textures", "packs", "Pack folders"},
        {"textures", "cache_mb", "Texture memory budget (MiB)"},
        {"textures", "dump", "Capture source textures"},
        {"textures", "dump_limit", "Maximum new captures"},
        {"input", "keyboard_player", "Keyboard and mouse play as"},
        {"input", "players", "Reserved player slots"},
        {"input", "backend", "Controller compatibility mode"},
        {"input", "rumble", "Vibration strength"},
        {"input", "rumble_floor", "Minimum vibration strength"},
        {"input", "rumble_min_ms", "Minimum vibration duration (ms)"},
        {"input", "rumble_on_connect", "Buzz a pad when it connects"},
        {"input", "hide_cursor", "Hide the pointer when idle"},
        {"input", "confine_cursor", "Keep the pointer in the window (full screen)"},
        {"gamepad", "left_deadzone", "Left stick dead zone"},
        {"gamepad", "right_deadzone", "Right stick dead zone"},
        {"gamepad", "trigger_threshold", "Trigger threshold"},
        {"gamepad", "button_threshold", "Button threshold"},
        {"gamepad", "left_stick", "Left stick comes from"},
        {"gamepad", "right_stick", "Right stick comes from"},
        {"launcher", "skip", "Skip the launcher at start"},
        {"launcher", "check_updates", "Check for updates when the app starts"},
        {"ui", "scale", "Text and control size"},
        {"debug", "unlock_all", "Unlock all fighters and venues"},
        {"debug", "skip_intro", "Skip startup movies and legal screen"},
        {"ui", "overlay_pad", "Open PC settings (controller shortcut)"},
        {"ui", "overlay_key", "Open PC settings (keyboard shortcut)"},
        {"debug", "record", "Record a diagnostic session"},
        {"debug", "mark_key", "Mark key"},
        {"debug", "mark_pad", "Mark pad buttons"},
        {"debug", "record_fps", "Pictures kept a second"},
        {"debug", "record_seconds", "Seconds kept before a mark"},
        {"debug", "record_audio", "Keep the sound"},
        {"debug", "record_game_state", "Log fighters and match events"},
        {"debug", "auto_bundle_hitch_ms", "Bundle a hitch longer than (ms)"},
        {"debug", "hang_seconds", "Bundle a hang after (s)"},
        {"debug", "keep_sessions", "Sessions kept whole"},
        {"debug", "record_guest_ram", "Crash bundles hold game memory"},
        {"songs", "restored", "Restored song: Holler If Ya Hear Me - 2Pac"},
        {"songs", "custom", "Your own songs"},
        {"songs", "folders", "More song folders"},
        {"songs", "skip_pad", "Next song (pad)"},
        {"songs", "restart_pad", "Song from the start (pad)"},
        {"songs", "skip_key", "Next song (key)"},
        {"songs", "restart_key", "Song from the start (key)"},
        {"cheats", "infinite_health", "Infinite health"},
        {"cheats", "max_blazin", "Max Blazin'"},
        {"cheats", "no_regen", "No health regeneration"},
        {"cheats", "fighter_size", "Fighter size"},
        {"cheats", "story_hero", "Play Story fights as"},
    };
    for (const Label &l : labels)
        if (!strcmp(l.section, section) && !strcmp(l.key, key)) return l.text;
    return key;
}

// ---- restart-needed tracking ----------------------------------------------------------------

struct StartValue { std::string section, key, text; };
static std::vector<StartValue> g_start_values;

static std::string value_text(const RecompSetting *s)
{
    char buf[RECOMP_SETTING_TEXT_MAX];
    return recomp_settings_format(s, s->stored, buf, sizeof(buf));
}

static void remember_start_values()
{
    if (!g_start_values.empty()) return;
    for (size_t i = 0; i < recomp_settings_count(); i++) {
        RecompSetting *s = recomp_settings_at(i);
        if ((s->flags & RECOMP_SETTING_RESTART) ||
            (!strcmp(s->section, "display") && !strcmp(s->key, "aspect")))
            g_start_values.push_back({ s->section, s->key, value_text(s) });
    }
}

static std::string restart_needed()
{
    std::string out;
    for (const StartValue &v : g_start_values) {
        RecompSetting *s = find(v.section.c_str(), v.key.c_str());
        if (s && value_text(s) != v.text) {
            if (v.key == "aspect")
            {
                int before = 0;
                for (int i = 0; s->choices[i]; i++)
                    if (v.text == s->choices[i])
                        before = i;
                if (!pcui::aspect_next_launch(before, s->stored))
                    continue;
            }
            if (!out.empty()) out += ", ";
            out += friendly(v.section.c_str(), v.key.c_str());
        }
    }
    return out;
}

// ---- learning a key or a pad button ------------------------------------------------------------

namespace {

struct Capture {
    enum Kind { NONE, KEY, PAD } kind = NONE;
    std::string section, key;       // the setting that gets the result
    bool append = false;            // keyboard bindings take up to four keys; others take one
    UiTick armed_at = 0;
};

Capture g_capture;
bool g_pad_prev[INPUT_SRC_COUNT];

void capture_start(Capture::Kind kind, const char *section, const char *key, bool append)
{
    g_capture.kind = kind;
    g_capture.section = section;
    g_capture.key = key;
    g_capture.append = append;
    g_capture.armed_at = ui_ticks() + 250;     // the click that started it is not the answer
    memset(g_pad_prev, 1, sizeof(g_pad_prev));       // a button already down does not count
}

void capture_cancel() { g_capture.kind = Capture::NONE; }

void capture_commit_key(int vk)
{
    const char *raw_name = xbox_InputMap_KeyName(vk);
    if (!raw_name) return;
    std::string name = raw_name;
    if (name.size() > 1) name[0] = (char)toupper((unsigned char)name[0]);     // "space" -> "Space"
    std::string text = name;
    if (g_capture.append) {
        std::string old = get_text(g_capture.section.c_str(), g_capture.key.c_str());
        InputBindings b;
        memset(&b, 0, sizeof(b));
        xbox_InputMap_BindingsParse(&b, 0, old.c_str());
        for (int i = 0; i < b.count[0]; i++)
            if (b.key[0][i] == vk) { capture_cancel(); return; }       // already there
        if (b.count[0] >= INPUT_BINDINGS_PER_CONTROL) { capture_cancel(); return; }
        if (!old.empty() && old != "none") text = old + ", " + name;
    }
    recomp_settings_set_text(g_capture.section.c_str(), g_capture.key.c_str(), text.c_str());
    commit();
    capture_cancel();
}

} // namespace

bool ui_capturing() { return g_capture.kind != Capture::NONE; }

#if defined(PCUI_WIN32)
bool ui_capture_message(HWND, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_capture.kind != Capture::KEY) return false;
    bool armed = ui_ticks() >= g_capture.armed_at;
    switch (msg) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        int vk = pc_input_vk_from_message(wp, lp);
        if (vk == VK_ESCAPE) { capture_cancel(); return true; }
        if (armed && vk != VK_F11 && vk != VK_MENU) capture_commit_key(vk);
        return true;
    }
    case WM_KEYUP: case WM_SYSKEYUP: case WM_CHAR: case WM_SYSCHAR:
        return true;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:     // clicks are the menu's: the left and right buttons have buttons of their own
        return false;
    case WM_MBUTTONDOWN: if (armed) capture_commit_key(VK_MBUTTON); return true;
    case WM_XBUTTONDOWN:
        if (armed) capture_commit_key(HIWORD(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2);
        return true;
    case WM_MOUSEWHEEL:
        if (armed) capture_commit_key(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? XBOX_VK_WHEEL_UP : XBOX_VK_WHEEL_DOWN);
        return true;
    }
    return false;
}
#else
bool ui_capture_sdl_event(const SDL_Event *e)
{
    if (g_capture.kind != Capture::KEY) return false;
    bool armed = ui_ticks() >= g_capture.armed_at;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: {
        int vk = host_posix_vk_from_scancode(e->key.scancode);
        if (vk == VK_ESCAPE) { capture_cancel(); return true; }
        if (armed && vk && !e->key.repeat && vk != VK_F11 && vk != VK_MENU && vk != 0xA4 && vk != 0xA5)
            capture_commit_key(vk);
        return true;
    }
    case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT:
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        // Clicks are the menu's: the left and right buttons have buttons of their own.
        if (e->button.button == SDL_BUTTON_MIDDLE) { if (armed) capture_commit_key(VK_MBUTTON); return true; }
        if (e->button.button == SDL_BUTTON_X1) { if (armed) capture_commit_key(VK_XBUTTON1); return true; }
        if (e->button.button == SDL_BUTTON_X2) { if (armed) capture_commit_key(VK_XBUTTON2); return true; }
        return false;
    case SDL_EVENT_MOUSE_WHEEL:
        if (armed && e->wheel.y != 0)
            capture_commit_key(e->wheel.y > 0 ? XBOX_VK_WHEEL_UP : XBOX_VK_WHEEL_DOWN);
        return true;
    }
    return false;
}
#endif

// Pads: the first button pressed on any of them becomes the answer.
static void capture_poll_pad()
{
    if (g_capture.kind != Capture::PAD) return;
    for (int slot = 0; slot < 4; slot++) {
        InputRaw raw;
        if (!xbox_HostInputRawPad(slot, &raw)) continue;
        for (int s = INPUT_SRC_SOUTH; s < INPUT_SRC_COUNT; s++) {
            bool down = raw.src[s] >= 16384;
            if (down && !g_pad_prev[s] && ui_ticks() >= g_capture.armed_at) {
                recomp_settings_set_text(g_capture.section.c_str(), g_capture.key.c_str(), xbox_InputMap_SourceName(s));
                commit();
                capture_cancel();
                return;
            }
            g_pad_prev[s] = down;
        }
    }
}

// ---- pads drive the menu --------------------------------------------------------------------------

void ui_feed_gamepad()
{
    ImGuiIO &io = ImGui::GetIO();
    InputRaw all;
    memset(&all, 0, sizeof(all));
    for (int slot = 0; slot < 4; slot++) {
        InputRaw raw;
        if (!xbox_HostInputRawPad(slot, &raw)) continue;
        for (int s = 0; s < INPUT_SRC_COUNT; s++)
            if (raw.src[s] > all.src[s]) all.src[s] = raw.src[s];
    }
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    auto key = [&](ImGuiKey k, int src) { io.AddKeyEvent(k, all.src[src] >= 16384); };
    auto analog = [&](ImGuiKey k, int src) {
        float v = all.src[src] / 32767.0f;
        io.AddKeyAnalogEvent(k, v > 0.25f, v > 0.25f ? v : 0.0f);
    };
    // Detecting a button for a binding must not also press the menu.
    bool detecting = g_capture.kind == Capture::PAD;
    if (detecting) {
        for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadRStickRight; k++) io.AddKeyEvent((ImGuiKey)k, false);
        return;
    }
    key(ImGuiKey_GamepadDpadUp, INPUT_SRC_DPAD_UP);      key(ImGuiKey_GamepadDpadDown, INPUT_SRC_DPAD_DOWN);
    key(ImGuiKey_GamepadDpadLeft, INPUT_SRC_DPAD_LEFT);  key(ImGuiKey_GamepadDpadRight, INPUT_SRC_DPAD_RIGHT);
    key(ImGuiKey_GamepadFaceDown, INPUT_SRC_SOUTH);      key(ImGuiKey_GamepadFaceRight, INPUT_SRC_EAST);
    key(ImGuiKey_GamepadFaceLeft, INPUT_SRC_WEST);       key(ImGuiKey_GamepadFaceUp, INPUT_SRC_NORTH);
    key(ImGuiKey_GamepadL1, INPUT_SRC_LEFT_SHOULDER);    key(ImGuiKey_GamepadR1, INPUT_SRC_RIGHT_SHOULDER);
    key(ImGuiKey_GamepadStart, INPUT_SRC_START);         key(ImGuiKey_GamepadBack, INPUT_SRC_BACK);
    analog(ImGuiKey_GamepadL2, INPUT_SRC_LEFT_TRIGGER);  analog(ImGuiKey_GamepadR2, INPUT_SRC_RIGHT_TRIGGER);
    analog(ImGuiKey_GamepadLStickUp, INPUT_SRC_LSTICK_UP);     analog(ImGuiKey_GamepadLStickDown, INPUT_SRC_LSTICK_DOWN);
    analog(ImGuiKey_GamepadLStickLeft, INPUT_SRC_LSTICK_LEFT); analog(ImGuiKey_GamepadLStickRight, INPUT_SRC_LSTICK_RIGHT);
}

// ---- context, fonts, style -----------------------------------------------------------------------

// Beyond the display's own scaling, a window much bigger than the game's 1280x960 default (full screen
// on a large monitor) gets bigger text, so the screen is as easy to read as it is in a window.
static float g_extra_scale = 1.0f;

static float display_scale(UiWindow window)
{
    float dpi = g_extra_scale;
#if defined(PCUI_WIN32)
    if (window) {
        UINT d = GetDpiForWindow(window);
        if (d) dpi *= d / 96.0f;
    }
#elif defined(_WIN32)
    // The app's SDL window on Windows: SDL measures it in pixels and reports the display's scaling
    // apart (docs/README-highdpi.md in SDL), so the screen is enlarged by it here.
    if (window) {
        float s = SDL_GetWindowDisplayScale((SDL_Window *)window) / SDL_GetWindowPixelDensity((SDL_Window *)window);
        if (s > 0.0f) dpi *= s;
    }
#else
    // The window's points are not its pixels on a Retina display, and the screen is drawn in points
    // (the launcher) or already sized by the picture's height (the overlay): nothing to add.
    (void)window;
#endif
    int percent = recomp_settings_get("ui", "scale", 100);
    return dpi * (percent / 100.0f);
}

static float g_scale_applied;

void ui_apply_scale(UiWindow window)
{
    float s = display_scale(window);
    float &applied = g_scale_applied;
    ImGuiStyle &style = ImGui::GetStyle();
    if (s == applied) return;
    applied = s;
    pcui::theme(s);
}

void ui_context_begin(UiWindow window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    pcui::clear_action_status();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigNavCaptureKeyboard = true;
    g_scale_applied = 0.0f;                 // a new context has not been styled yet
    // The system's UI font reads better than the built-in one at small sizes.
#if defined(_WIN32)
    char font[MAX_PATH];
    if (GetWindowsDirectoryA(font, sizeof(font))) {
        strcat(font, "\\Fonts\\segoeui.ttf");
        if (GetFileAttributesA(font) != INVALID_FILE_ATTRIBUTES)
            io.Fonts->AddFontFromFileTTF(font, 17.0f);
    }
#else
    static const char *const fonts[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    };
    struct stat st;
    for (const char *font : fonts)
        if (stat(font, &st) == 0 && io.Fonts->AddFontFromFileTTF(font, 17.0f)) break;
#endif
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    pcui::load_display_font();
    ui_apply_scale(window);
    remember_start_values();
}

void ui_context_end()
{
    ImGui::DestroyContext();
    g_capture.kind = Capture::NONE;
    g_start_values.clear();         // what was changed in the launcher has been applied: the overlay starts afresh
}

// ---- generic setting widgets --------------------------------------------------------------------------

// A hover tooltip for the last item, wrapped: ImGui's own one-line tooltip does not wrap, and the longer
// help texts ran past the right edge of the window and were cut off.
static void item_tooltip(const char *text)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;
    const auto &style = ImGui::GetStyle();
    ImVec2 area = ImGui::GetIO().DisplaySize;
    float margin = 8 * style.FontScaleMain;
    float font_size = ImGui::GetFontSize(), minimum_font = std::min(font_size, 17.0f);
    float wrap;
    ImVec2 text_size;
    for (;;)
    {
        ImGui::PushFont(nullptr, font_size / style.FontScaleMain);
        wrap = std::max(1.0f, std::min(ImGui::GetFontSize() * 35.0f,
                                     area.x - 2 * (margin + style.WindowPadding.x)));
        text_size = ImGui::CalcTextSize(text, nullptr, false, wrap);
        if (text_size.y + 2 * style.WindowPadding.y <= area.y - 2 * margin || font_size <= minimum_font)
            break;
        ImGui::PopFont();
        font_size = std::max(minimum_font, font_size * .9f);
    }
    ImVec2 size(text_size.x + 2 * style.WindowPadding.x, text_size.y + 2 * style.WindowPadding.y);
    ImVec2 mouse = ImGui::GetIO().MousePos;
    // ImGui may place a tall tooltip outside the viewport when no side of the pointer fits it.
    ImGui::SetNextWindowPos(ImVec2(
        std::clamp(mouse.x + 12 * style.FontScaleMain, margin, std::max(margin, area.x - margin - size.x)),
        std::clamp(mouse.y + 20 * style.FontScaleMain, margin, std::max(margin, area.y - margin - size.y))));
    if (!ImGui::BeginTooltip())
    {
        ImGui::PopFont();
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopFont();
}

// A bool, integer, choice or text setting as its natural control. Returns true when it changed.
static bool setting_widget(RecompSetting *s, const char *label = nullptr, bool stacked = false)
{
    if (!s)
        return false;
    if (!label)
        label = friendly(s->section, s->key);
    // An explicit pack list enables/disables texture replacements independently of the file switch.
    RecompSetting *packs = find("textures", "packs");
    bool pack_override = !strcmp(s->section, "textures") && !strcmp(s->key, "enabled") && packs &&
                         packs->source == RECOMP_SETTING_FROM_ENV;
    bool quick_field = stacked;
    stacked = stacked || ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 36;
    bool locked = s->source == RECOMP_SETTING_FROM_ENV || pack_override, changed = false;
    int value = pack_override ? packs->value_text[0] != 0 : locked ? s->value : s->stored;
    ImGui::PushID(s);
    const char *control = stacked ? "##value" : label;
    if (stacked)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", label);
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
    }
    if (locked)
        ImGui::BeginDisabled();
    switch (s->type)
    {
    case RECOMP_SETTING_BOOL:
        if (stacked)
        {
            const char *off = !strcmp(s->key, "fullscreen") ? "Windowed" : "Off";
            const char *on = !strcmp(s->key, "fullscreen") ? "Borderless fullscreen" : "On";
            if (ImGui::BeginCombo(control, value ? on : off))
            {
                for (int i = 0; i < 2; i++)
                {
                    if (ImGui::Selectable(i ? on : off, value == i))
                        changed = recomp_settings_set(s->section, s->key, i) > 0;
                    ui_target((std::string(s->section) + "." + s->key + (i ? ".on" : ".off")).c_str());
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            bool v = value != 0;
            if (ImGui::Checkbox(control, &v))
                changed = recomp_settings_set(s->section, s->key, v) > 0;
        }
        break;
    case RECOMP_SETTING_INT: {
        if (!stacked)
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        int v = value;
        if (!strcmp(s->key, "render_scale"))
        {
            char current[64];
            snprintf(current, sizeof current, "%dx (%dp)%s", v, v * 480, v == 2 ? " - Balanced" : v == 3 ? " - Sharper" : "");
            if (ImGui::BeginCombo(control, current))
            {
                for (int i = 1; i <= 4; i++)
                {
                    char item[64];
                    snprintf(item, sizeof item, "%dx (%dp)%s", i, i * 480, i == 2 ? " - Balanced" : i == 3 ? " - Sharper" : "");
                    if (ImGui::Selectable(item, v == i))
                        changed = recomp_settings_set(s->section, s->key, i) > 0;
                }
                ImGui::EndCombo();
            }
        }
        else if (s->max - s->min > 2000)
        {
            if (ImGui::InputInt(control, &v, 10, 100))
                changed = recomp_settings_set(s->section, s->key, v) > 0;
        }
        else if (pcui::slider_int(control, &v, s->min, s->max))
            changed = recomp_settings_set(s->section, s->key, v) > 0;
        break;
    }
    case RECOMP_SETTING_ENUM:
        if (!stacked)
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        if (ImGui::BeginCombo(control, s->choices[value]))
        {
            for (int i = 0; s->choices[i]; i++)
                if (ImGui::Selectable(s->choices[i], i == value))
                    changed = recomp_settings_set(s->section, s->key, i) > 0;
            ImGui::EndCombo();
        }
        break;
    case RECOMP_SETTING_STRING: {
        char buf[RECOMP_SETTING_TEXT_MAX];
        snprintf(buf, sizeof buf, "%s", locked ? s->value_text : s->stored_text);
        if (!stacked)
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
        if (ImGui::InputText(control, buf, sizeof buf))
            recomp_settings_set_text(s->section, s->key, buf);
        changed = ImGui::IsItemDeactivatedAfterEdit();
        break;
    }
    }
    std::string id = std::string(s->section) + "." + s->key;
    ui_target(id.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemFocused())
        g_help = s->help ? s->help : label;
    if (locked)
        ImGui::EndDisabled();
    if (s->help)
        item_tooltip(s->help);
    if (locked)
    {
        ImGui::TextDisabled("Set for this run by %s", pack_override ? "RECOMP_TEXTURE_PACKS"
                                                      : s->env      ? s->env
                                                                    : "the environment");
    }
    else if (!quick_field && (s->flags & RECOMP_SETTING_RESTART))
    {
        if (!stacked)
            ImGui::SameLine();
        ImGui::TextDisabled(g_mode == UI_LAUNCHER ? "(on Play)" : "(next launch)");
    }
    ImGui::PopID();
    if (changed)
        commit();
    return changed;
}

static void settings_group(const char *section, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        RecompSetting *s = find(section, key);
        if (s) setting_widget(s);
    }
}

// ---- the pages ----------------------------------------------------------------------------------------------

static void experience_widget()
{
    int current = pc_experience_current();
    ImGui::Text("Experience: %s", current == 0 ? "Original" : current == 1 ? "Modern" : "Custom");
    if (ImGui::GetContentRegionAvail().x > 400 * ImGui::GetStyle().FontScaleMain) ImGui::SameLine();
    static std::string preset_error;
    static int proposed = 1;
    if (ImGui::Button("Change experience")) {
        proposed = current == 0 ? 0 : 1;
        preset_error.clear();
        ImGui::OpenPopup("Choose experience");
    }
    ui_target("experience.change");
    ImVec2 area = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(560.0f * ImGui::GetStyle().FontScaleMain, area.x - 24),
                                  std::min(460.0f * ImGui::GetStyle().FontScaleMain, area.y - 24)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Choose experience", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::BeginChild("preset choices", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2));
        ImGui::RadioButton("Modern - recommended", &proposed, 1);
        ui_target("experience.modern");
        ImGui::TextWrapped("Widescreen Auto, 2x (960p), high-quality scaling, mipmaps, 2x anti-aliasing and 16x texture filtering. Keeps the original scene effects. Upscaled Original Textures and downloadable texture packs are optional.");
        ImGui::Spacing();
        ImGui::RadioButton("Original", &proposed, 0);
        ui_target("experience.original");
        ImGui::TextWrapped("4:3, 1x (480p), original textures, mipmaps and 2x anti-aliasing. Keeps the original scene effects.");
        ImGui::Separator();
        ImGui::TextWrapped("Replaces graphics customisations, including brightness and widescreen framing. Keeps your display mode, window size, bindings, language, saves and installed mods. Original turns texture mods off; Modern keeps their current state.");
        if (!preset_error.empty()) ImGui::TextWrapped("%s", preset_error.c_str());
        ImGui::EndChild();
        if (ImGui::Button("Apply experience")) {
            int status = pc_experience_apply(proposed);
            g_save_failed = status > 0;
            if (status < 0) preset_error = "A launch environment override locks part of this preset. Remove the override before changing experience.";
            else ImGui::CloseCurrentPopup();
        }
        ui_target("experience.apply");
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
static void reset_topic(const char *label, std::initializer_list<const char *> sections)
{
    ImGui::PushID(label);
    if (ImGui::Button(label)) ImGui::OpenPopup("Reset this category?");
    ImVec2 area = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(area.x - 24, area.y - 24));
    if (ImGui::BeginPopupModal("Reset this category?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(440.0f * ImGui::GetStyle().FontScaleMain, area.x - 70));
        ImGui::TextWrapped("Restore factory defaults for this category? Other settings, installed mods and saves stay as they are. Graphics factory defaults differ from Original and Modern presets.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Reset category")) {
            for (size_t i = 0; i < recomp_settings_count(); ++i) {
                auto setting = recomp_settings_at(i);
                for (auto section : sections) if (!strcmp(setting->section, section))
                    recomp_settings_reset_to_default(setting->section, setting->key);
            }
            commit(); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

static void page_display()
{
    experience_widget();
    ImGui::SeparatorText("Graphics and display");
    settings_group("display", {"fullscreen", "aspect", "render_scale", "brightness"});
    bool quality = ImGui::CollapsingHeader("Image quality");
    ui_target("graphics.quality");                  // scripted tests open the sections by name
    if (quality)
        settings_group("display", {"filter", "msaa", "texture_mips", "anisotropy", "vsync"});
    bool effects = ImGui::CollapsingHeader("Original visual effects");
    ui_target("graphics.effects");
    if (effects)
        settings_group("display", {"vignette", "vignette_strength", "color_tint", "scratches"});
    if (recomp_settings_get("display", "aspect", 0) >= 2 && ImGui::CollapsingHeader("Widescreen framing")) {
        settings_group("display", {"widescreen_hud", "widescreen_cutscenes", "widescreen_side_fill"});
        ImGui::TextWrapped("Auto chooses 16:9 or 16:10. Wider ratios are experimental. Original framing preserves the game's cutscene composition.");
    }
    bool advanced = ImGui::CollapsingHeader("Advanced graphics");
    ui_target("graphics.advanced");
    if (advanced) {
        settings_group("display", {"window_width", "window_height", "widescreen_fov_cap", "gamma"});
        reset_topic("Reset graphics to factory defaults", {"display"});
        ImGui::TextWrapped("Custom window dimensions do not change internal render resolution. The field-of-view limit only affects very wide screens.");
    }
}

static std::vector<pcui::Pack> g_packs;
static std::vector<pcui::ModGroup> g_mod_groups;
static std::string g_pack_key, g_pack_path, g_pack_status;
static std::map<std::string,std::string> g_pack_conflicts;
static void refresh_packs(bool force = false)
{
    std::string packs = get_text("textures", "packs"), language = get_text("language", "pack");
    std::string key = packs + "\n" + language, path = pcui::folder_path(pcui::Folder::Mods);
    if (force || key != g_pack_key || path != g_pack_path)
    {
        g_pack_key = key;
        g_pack_path = path;
        g_packs = pcui::inventory(std::filesystem::u8path(path), packs, language);
        g_mod_groups = pcui::mod_groups(std::filesystem::u8path(path),g_packs);
        g_pack_conflicts = pcui::pack_conflicts(std::filesystem::u8path(path),g_packs);
    }
}
static bool packs_available()
{
    refresh_packs();
    for (auto &p : g_packs)
        if (p.selected && p.problem.empty() && !p.language)
            return true;
    return false;
}
static void save_packs()
{
    auto text = pcui::selected_packs(g_packs);
    if (text.size() >= RECOMP_SETTING_TEXT_MAX)
    {
        g_pack_status = "Pack list is too long; change was not saved.";
        g_pack_path.clear(); // Reload inventory next frame, even when the saved list is empty.
        return;
    }
    auto language = pcui::selected_language(g_packs);
    recomp_settings_set_text("textures", "packs", text.c_str());
    // A language mod is on or off here too; it is saved as [language] pack, not in the order.
    if (find("language", "pack")->source != RECOMP_SETTING_FROM_ENV)
        recomp_settings_set_text("language", "pack", language.c_str());
    commit();
    g_pack_key = text + "\n" + get_text("language", "pack");
    g_pack_conflicts = pcui::pack_conflicts(std::filesystem::u8path(g_pack_path),g_packs);
    g_pack_status = g_save_failed ? "Could not save mod selections. Retry save below." : "Selections saved for next launch. Higher priority overrides lower priority.";
}
#if defined(DEFJAM_APP)
void pc_app_setup_hd();
#endif
static void page_textures()
{
    const bool launcher = g_mode == UI_LAUNCHER;
    static bool requested = false;
    if (launcher && !requested) { pcui::mod_start("status"); requested = true; }
    refresh_packs();
    bool locked = find("textures", "packs")->source == RECOMP_SETTING_FROM_ENV;
    bool language_locked = find("language", "pack")->source == RECOMP_SETTING_FROM_ENV;
    static bool browse = false, browse_requested = false;
    if (ImGui::BeginTabBar("mod views", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
                                      ImGuiTabBarFlags_DrawSelectedOverline)) {
        bool installed_active = ImGui::BeginTabItem("Installed mods");
        ui_target("mods.installed");
        if (installed_active) { browse = false; ImGui::EndTabItem(); }
        bool browse_active = ImGui::BeginTabItem("Browse mods");
        ui_target("mods.browse");
        if (browse_active) { browse = true; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    if (browse && launcher && !browse_requested && !pcui::mod_busy()) {
        browse_requested = pcui::mod_start("refresh");
    }
    if (!pcui::mod_status().empty()) ImGui::TextWrapped("%s", pcui::mod_status().c_str());
    if (!browse) {
    ImGui::SeparatorText("Installed mods");
#if defined(DEFJAM_APP)
    if (ImGui::Button("Generate upscaled textures")) pc_app_setup_hd();
    ui_target("mods.setup-hd");
    ImGui::TextWrapped("Upscaled Original Textures are higher-resolution versions of the original textures, generated from your game copy. Find downloadable texture packs and language mods in the Browse mods tab.");
#endif
    if (pcui::mod_recovery_needed()) {
        ImGui::TextWrapped("An interrupted installation needs recovery before Play. If recovery fails, files are preserved in the data folder under mod-manager; see the status below.");
        ImGui::BeginDisabled(!launcher || pcui::mod_busy());
        if (ImGui::Button("Retry recovery")) pcui::mod_start("recover");
        ui_target("mods.recover");
        ImGui::EndDisabled();
    }
    if (packs_available() && !recomp_settings_get("textures", "enabled", 0))
        ImGui::TextWrapped("Texture mods are globally off. Enable a mod below to turn them on.");
    if (locked) ImGui::TextWrapped("Pack selection is set for this run by RECOMP_TEXTURE_PACKS.");
    if (language_locked) ImGui::TextWrapped("The language is set for this run by RECOMP_LANGUAGE_PACK.");
    if (find("textures", "enabled")->source == RECOMP_SETTING_FROM_ENV) ImGui::TextWrapped("Texture mod activation is locked by a launch environment override.");
    if (ImGui::Button("Refresh local mods")) {
        refresh_packs(true);
        if (launcher) pcui::mod_start("status");
    }
    ui_target("mods.local-refresh");
    ImGui::SameLine();
    if (ImGui::Button("Open Mods Folder")) pcui::open_path(g_pack_path);
    if (ImGui::Button("Load Order...")) ImGui::OpenPopup("Load Order");
    ui_target("mods.order");
    ImGui::TextWrapped("Enable installed mods below. Load Order sets which mod wins when textures overlap. "
                       "A language mod translates the game itself, sits beneath every texture mod, and only one is on "
                       "at a time; with none the game is in English.");
    const auto &managed = pcui::mod_view().installed;
    auto display_name = [&](const pcui::ModGroup &group) {
        const auto &first=g_packs[group.members.front()];
        auto found=managed.find(first.name);
        if (group.builtin) return group.name;
        if (found!=managed.end()) return found->second.name;
        return first.language && !first.title.empty() ? first.title : group.name;
    };
    const float scale=ImGui::GetStyle().FontScaleMain;
    const float available=ImGui::GetContentRegionAvail().x;
    ImGui::BeginDisabled(pcui::mod_busy());
    if(ImGui::BeginTable("installed mod table",3,ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("On",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFrameHeight()+8);
        ImGui::TableSetupColumn("Mod",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status",ImGuiTableColumnFlags_WidthFixed,std::min(145*scale,available*.30f));
        ImGui::TableHeadersRow();
        for(const auto &group:g_mod_groups) {
            // Missing saved names are warnings, not installed mods to download.
            if(!group.builtin && g_packs[group.members.front()].problem=="Not installed") continue;
            auto &p=g_packs[group.members.front()];
            const std::string pack_id=p.name;
            auto found=managed.find(pack_id);
            size_t selected=0; bool valid=true;
            for(auto i:group.members) { selected+=g_packs[i].selected; valid &= g_packs[i].problem.empty(); }
            ImGui::PushID(group.id.c_str());
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            bool checked=selected==group.members.size();
            ImGui::BeginDisabled((p.language ? language_locked : locked || find("textures", "enabled")->source == RECOMP_SETTING_FROM_ENV) || (!valid && !selected));
            if(ImGui::Checkbox("##enabled",&checked)) {
                pcui::select_mod(g_packs,group,checked);
                if (checked && !p.language) recomp_settings_set("textures", "enabled", 1);
                save_packs();
            }
            ui_target(("pack."+group.id).c_str());
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s",display_name(group).c_str());
            if(p.language) ImGui::TextDisabled(found==managed.end() ? "Language mod (local)" : "Language mod (project catalogue)");
            else ImGui::TextDisabled(group.builtin ? "Built-in / generated locally" : found==managed.end() ? "Local mod" : "Project catalogue");
            if(group.builtin && ImGui::TreeNode("Components and details")) {
                ImGui::TextWrapped("Generated from your own dump. Transparency textures preserve menu edges; material-channel textures preserve surface data. Both belong to Upscaled Original Textures.");
                for(auto i:group.members) ImGui::TextWrapped("%s: %s",g_packs[i].name.c_str(),g_packs[i].selected?"Enabled":"Disabled");
                ImGui::TreePop();
            }
            for(auto i:group.members) {
                if(!g_packs[i].problem.empty()) ImGui::TextWrapped("%s",g_packs[i].problem.c_str());
                if(!g_pack_conflicts[g_packs[i].name].empty()) ImGui::TextWrapped("%s",g_pack_conflicts[g_packs[i].name].c_str());
            }
            if(found!=managed.end() && launcher) {
                if(found->second.rollback) {
                    if(ImGui::SmallButton("Restore previous version")) pcui::mod_start("rollback",pack_id);
                    ui_target(("mod.rollback."+pack_id).c_str());
                }
                ImGui::BeginDisabled(p.selected);
                if(ImGui::SmallButton("Uninstall mod")) pcui::mod_start("remove",pack_id);
                ui_target(("mod.remove."+pack_id).c_str());
                ImGui::EndDisabled();
                if(p.selected) ImGui::TextWrapped("Disable before uninstalling.");
            }
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s",!valid ? "Needs attention" : !selected ? "Installed - off" : !p.language && !recomp_settings_get("textures", "enabled", 0) ? "Globally off" : selected && !checked ? "Partly enabled" : "On next launch");
            if(group.builtin) {
                ImGui::TextWrapped("%zu component%s",group.members.size(),group.members.size()==1?"":"s");
                if(group.members.size()==1) ImGui::TextWrapped("Only one generated component found. Setup can generate the full set.");
            } else if(found!=managed.end()) ImGui::TextWrapped("%s",found->second.version.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    bool missing=false;
    for(const auto &p:g_packs) if(p.problem=="Not installed") missing=true;
    if(missing && ImGui::CollapsingHeader("Unavailable saved pack selections")) {
        ImGui::TextWrapped("These saved names have no installed folder. They are not extra components that need downloading. Existing selections are kept until you change them.");
        for(auto &p:g_packs) if(p.problem=="Not installed") {
            ImGui::PushID(p.name.c_str());
            bool selected=p.selected;
            ImGui::BeginDisabled((p.language ? language_locked : locked) || pcui::mod_busy());
            if(ImGui::Checkbox(p.name.c_str(),&selected)) { p.selected=selected; save_packs(); }
            ImGui::EndDisabled();
            if(p.name=="faithful-hd") ImGui::TextWrapped("Legacy default name. The generated Upscaled Original Textures components appear together in the table above when installed.");
            ImGui::PopID();
        }
    }
    if(g_packs.empty()) ImGui::TextWrapped("No local packs found. Setup can generate Upscaled Original Textures from your own dump.");
    const float modal_scale = ImGui::GetStyle().FontScaleMain;
    const auto viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(760 * modal_scale, viewport->WorkSize.x - 24), std::min(460 * modal_scale, viewport->WorkSize.y - 24)), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * .5f, viewport->WorkPos.y + viewport->WorkSize.y * .5f), ImGuiCond_Appearing, ImVec2(.5f,.5f));
    if (ImGui::BeginPopupModal("Load Order", nullptr, ImGuiWindowFlags_None)) {
        ImGui::TextWrapped("Highest priority at the top. Mods above override matching textures from mods below. Priority is shown in descending order.");
        ImGui::BeginChild("ordered mods", ImVec2(0, std::min(250 * modal_scale, viewport->WorkSize.y * .35f)));
        ImGui::BeginDisabled(locked || pcui::mod_busy());
        auto order=pcui::priority_groups(g_packs,g_mod_groups);
        size_t priority=0;
        for(const auto &p:g_packs) if(p.selected && !p.language) ++priority;
        if(ImGui::BeginTable("priority table",3,ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Priority",ImGuiTableColumnFlags_WidthFixed,75*modal_scale);
            ImGui::TableSetupColumn("Mod",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Change",ImGuiTableColumnFlags_WidthFixed,85*modal_scale);
            ImGui::TableHeadersRow();
            for(size_t n=0;n<order.size();++n) {
                const auto &group=order[n];
                ImGui::PushID(group.id.c_str());
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                if(group.members.size()>1) ImGui::Text("%zu - %zu",priority,priority-group.members.size()+1);
                else ImGui::Text("%zu",priority);
                priority-=group.members.size();
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s",display_name(group).c_str());
                if(group.builtin && group.members.size()>1) ImGui::TextWrapped("Both built-in components");
                if(!g_packs[group.members.front()].problem.empty()) ImGui::TextWrapped("%s (saved selection)",g_packs[group.members.front()].problem.c_str());
                ImGui::TableNextColumn();
                bool moved=false;
                ImGui::BeginDisabled(n==0);
                if(ImGui::SmallButton("Higher")) moved=pcui::move_priority(g_packs,g_mod_groups,n,true);
                ui_target(("pack.higher."+group.id).c_str());
                ui_target(("pack.later."+group.id).c_str()); // Existing automation alias, same loader direction.
                ImGui::EndDisabled();
                ImGui::BeginDisabled(n+1==order.size());
                if(ImGui::SmallButton("Lower")) moved=pcui::move_priority(g_packs,g_mod_groups,n,false);
                ui_target(("pack.lower."+group.id).c_str());
                ui_target(("pack.earlier."+group.id).c_str());
                ImGui::EndDisabled(); ImGui::PopID();
                if(moved) { save_packs(); refresh_packs(true); break; }
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled(); ImGui::EndChild();
        ImGui::TextWrapped("Changes apply on the next game start.");
        if (ImGui::Button("Done") || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) ImGui::CloseCurrentPopup();
        ui_target("mods.order-done");
        ImGui::EndPopup();
    }
    if (!g_pack_status.empty()) ImGui::TextWrapped("%s",g_pack_status.c_str());
    bool advanced_mods = ImGui::CollapsingHeader("Advanced mods");
    ui_target("mods.advanced");
    if (advanced_mods) {
        settings_group("textures", {"enabled", "cache_mb"});
        ImGui::TextWrapped("%s", find("textures", "cache_mb")->help);
        settings_group("textures", {"packs"});
    }
    } else {
    ImGui::SeparatorText("Browse mods");
    if (!launcher) ImGui::TextWrapped("Return to the launcher to download, update or remove mods. Current packs stay in use during this game.");
    ImGui::BeginDisabled(!launcher || pcui::mod_busy());
    bool testing_versions = pcui::mod_prerelease();
    if (ImGui::CollapsingHeader("Advanced catalogue options")) {
    if (ImGui::Checkbox("Show testing versions (release candidates)", &testing_versions)) {
        pcui::mod_set_prerelease(testing_versions); pcui::mod_start("status");
    }
    ui_target("mods.testing-versions");
    }
    if (ImGui::Button("Refresh catalogue")) pcui::mod_start("refresh");
    ui_target("mods.catalogue-refresh");
    ImGui::EndDisabled();
    if (pcui::mod_busy()) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) pcui::mod_cancel();
        ui_target("mods.cancel");
    }
    if (!pcui::mod_status().empty()) ImGui::TextWrapped("%s",pcui::mod_status().c_str());
    if (!pcui::mod_view().catalogue.empty()) ImGui::TextWrapped("%s",pcui::mod_view().catalogue.c_str());
    for (const auto &mod : pcui::mod_view().catalogue_mods) {
        ImGui::PushID(mod.id.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(mod.name.c_str());
        if(!mod.authors.empty()) ImGui::TextWrapped("By %s",mod.authors.c_str());
        ImGui::TextWrapped("%s",mod.description.c_str());
        if (mod.revoked) ImGui::TextWrapped("Installed version withdrawn from the catalogue. Disable it or choose a reviewed replacement.");
        if (!mod.installed.empty()) ImGui::Text("Installed: %s",mod.installed.c_str());
        if (!mod.latest.empty()) ImGui::Text("Available: %s / %.1f MB",mod.latest.c_str(),mod.bytes/1000000.0);
        else ImGui::TextWrapped("No compatible version for this runtime and selected channel.");
        if (mod.channel == "prerelease") ImGui::TextWrapped("Testing version: please report problems before the final release.");
        if (!mod.load_after.empty()) ImGui::TextWrapped("Suggested after: %s",mod.load_after.c_str());
        ImGui::BeginDisabled(!launcher || pcui::mod_busy() || !mod.compatible || (!mod.installed.empty() && !mod.update));
        if (ImGui::Button(mod.installed.empty() ? "Install and enable" : "Update")) pcui::mod_start(mod.installed.empty() ? "install-enable" : "install",mod.id);
        ui_target(("mod.install."+mod.id).c_str());
        ImGui::EndDisabled(); ImGui::PopID();
    }
    if (pcui::mod_view().catalogue_mods.empty() && !pcui::mod_busy())
        ImGui::TextWrapped("No catalogue entries available. Check your connection and select Refresh catalogue to try again.");
    }
}

void ui_home_fields()
{
    experience_widget();
    ImGui::Spacing();
    bool two = ImGui::GetContentRegionAvail().x >= 480 * ImGui::GetStyle().FontScaleMain;
    if (ImGui::BeginTable("quick graphics", two ? 2 : 1, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); setting_widget(find("display", "fullscreen"), "Display mode", true);
        ImGui::TableNextColumn(); setting_widget(find("display", "render_scale"), "Graphics quality", true);
        ImGui::EndTable();
    }
    refresh_packs();
    bool installed = false;
    for (const auto &p : g_packs) if (p.problem.empty() && !p.language) installed = true;
    ImGui::TextWrapped("Texture mods: %s", !installed ? "Not installed (optional)" :
                       !packs_available() || !recomp_settings_get("textures", "enabled", 0) ? "Installed - off" : "On next launch");
}

void ui_home_devices()
{
    bool any = false;
    for (int i = 0; i < 4; i++)
    {
        char name[64];
        if (xbox_HostInputSlotInfo(i, name, sizeof name))
        {
            ImGui::TextWrapped("Player %d: %s", i + 1, name);
            any = true;
        }
    }
    if (!any)
        ImGui::TextDisabled("No controller connected / keyboard available");
}

static void draw_stick(const char *id, float x, float y, float radius)
{
    ImVec2 c = ImGui::GetCursorScreenPos();
    c.x += radius; c.y += radius;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddCircle(c, radius, IM_COL32(150, 150, 150, 255), 24, 2.0f);
    dl->AddCircleFilled(ImVec2(c.x + x * radius * 0.9f, c.y - y * radius * 0.9f), radius * 0.16f, IM_COL32(255, 190, 50, 255));
    ImGui::Dummy(ImVec2(radius * 2, radius * 2));
    (void)id;
}

static void page_controllers()
{
    ImGui::SeparatorText("Players");
    InputPadMap map;
    pc_input_current_padmap(&map);
    for (int slot = 0; slot < 4; slot++) {
        char name[64];
        int what = xbox_HostInputSlotInfo(slot, name, sizeof(name));
        ImGui::PushID(slot);
        ImGui::Text("Player %d:", slot + 1);
        ImGui::SameLine();
        if (!what) { ImGui::TextDisabled("nothing connected"); ImGui::PopID(); continue; }
        ImGui::TextUnformatted(name);
        InputRaw raw;
        if (what & 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Test rumble")) xbox_HostInputRumbleTest(slot);
            if (xbox_HostInputRawPad(slot, &raw)) {
                XBOX_GAMEPAD pad;
                xbox_InputMap_PadMap(&map, &raw, &pad);
                ImGui::Indent();
                draw_stick("l", pad.sThumbLX / 32767.0f, pad.sThumbLY / 32767.0f, ImGui::GetFontSize() * 1.6f);
                ImGui::SameLine();
                draw_stick("r", pad.sThumbRX / 32767.0f, pad.sThumbRY / 32767.0f, ImGui::GetFontSize() * 1.6f);
                ImGui::SameLine();
                static const char *btn[8] = { "A", "B", "X", "Y", "Black", "White", "LT", "RT" };
                ImGui::BeginGroup();
                std::string pressed;
                for (int i = 0; i < 8; i++)
                    if (pad.bAnalogButtons[i] > 30) { pressed += btn[i]; pressed += ' '; }
                static const char *dig[8] = { "Up", "Down", "Left", "Right", "Start", "Back", "LS", "RS" };
                for (int i = 0; i < 8; i++)
                    if (pad.wButtons & (1 << i)) { pressed += dig[i]; pressed += ' '; }
                ImGui::TextDisabled("what the game sees:");
                ImGui::TextUnformatted(pressed.empty() ? "-" : pressed.c_str());
                ImGui::EndGroup();
                ImGui::Unindent();
            }
        }
        ImGui::PopID();
    }
    ImGui::TextWrapped("Press buttons and push the sticks to check them. The circles show the sticks after the dead zones below.");

    ImGui::SeparatorText("Who plays");
    settings_group("input", {"keyboard_player", "rumble"});
    if (ImGui::CollapsingHeader("Stick and trigger calibration"))
        settings_group("gamepad", {"left_deadzone", "right_deadzone", "trigger_threshold", "button_threshold"});
    if (ImGui::CollapsingHeader("Advanced controls")) {
        settings_group("input", {"players"});
        ImGui::TextWrapped("0 means Automatic. Reserved slots let controllers join after the game starts.");
#if defined(_WIN32)
        settings_group("input", {"backend"});
#endif
        settings_group("gamepad", {"left_stick", "right_stick"});
        settings_group("input", {"rumble_floor", "rumble_min_ms", "rumble_on_connect", "hide_cursor", "confine_cursor"});
        reset_topic("Reset controls to factory defaults", {"input", "gamepad", "keyboard"});
    }
}

static void page_pad_buttons()
{
    ImGui::TextWrapped("Which physical button acts as each Xbox control. Choose from the list, or press Detect and then the "
                       "button on your pad. Names are positional: south is the bottom face button (Xbox A, PlayStation cross), "
                       "east the right one, west the left, north the top.");
    if (ImGui::Button("Reset controller bindings")) {
        for (int i = 0; i < INPUT_PAD_CONTROL_COUNT; i++) recomp_settings_reset_to_default("gamepad", xbox_InputMap_ControlName(i));
        commit();
    }
    ImGui::Spacing();
    if (ImGui::BeginTable("padmap", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Xbox control", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Pad button", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for (int i = 0; i < INPUT_PAD_CONTROL_COUNT; i++) {
            const char *key = xbox_InputMap_ControlName(i);
            RecompSetting *s = find("gamepad", key);
            if (!s) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(k_control_labels[i]);
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            int cur = xbox_InputMap_SourceFromName(s->stored_text);
            if (cur < 0) cur = 0;
            bool detecting = g_capture.kind == Capture::PAD && g_capture.key == key;
            ImGui::SetNextItemWidth(-1);
            if (detecting) {
                ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1), "Press a button on your pad...");
            } else if (ImGui::BeginCombo("##src", xbox_InputMap_SourceName(cur))) {
                for (int n = 0; n < INPUT_SRC_COUNT; n++)
                    if (ImGui::Selectable(xbox_InputMap_SourceName(n), n == cur)) {
                        recomp_settings_set_text("gamepad", key, xbox_InputMap_SourceName(n));
                        commit();
                    }
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            if (detecting) {
                if (ImGui::Button("Cancel")) capture_cancel();
            } else {
                if (ImGui::Button("Detect")) capture_start(Capture::PAD, "gamepad", key, false);
                ImGui::SameLine();
                if (ImGui::Button("Default")) { recomp_settings_reset_to_default("gamepad", key); commit(); }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void page_keyboard()
{
    ImGui::TextWrapped("The keys and mouse buttons that press each Xbox control. Each control takes up to four. Press Add, then a key, "
                       "a side or middle mouse button or a wheel notch (Esc cancels); the left and right buttons have buttons of "
                       "their own while you are adding. F11 and Alt+Enter are full screen and cannot be bound.");
    if (ImGui::Button("Reset all to default")) {
        for (int i = 0; i < INPUT_CONTROL_COUNT; i++) recomp_settings_reset_to_default("keyboard", xbox_InputMap_ControlName(i));
        commit();
    }
    ImGui::SameLine();
    RecompSetting *kp = find("input", "keyboard_player");
    if (kp) {
        ImGui::SameLine();
        ImGui::TextDisabled("Keyboard and mouse play as: %s", kp->choices[kp->stored]);
    }
    ImGui::Spacing();
    if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Xbox control", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for (int i = 0; i < INPUT_CONTROL_COUNT; i++) {
            const char *key = xbox_InputMap_ControlName(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(k_control_labels[i]);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            bool capturing = g_capture.kind == Capture::KEY && g_capture.key == key;
            if (capturing)
                ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1), "Press a key, side button or wheel...");
            else {
                std::string t = get_text("keyboard", key);
                ImGui::TextUnformatted(t.empty() ? "none" : t.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (capturing) {
                if (ImGui::Button("Left click")) capture_commit_key(VK_LBUTTON);
                ImGui::SameLine();
                if (ImGui::Button("Right click")) capture_commit_key(VK_RBUTTON);
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) capture_cancel();
            } else {
                if (ImGui::Button("Add")) capture_start(Capture::KEY, "keyboard", key, true);
                ImGui::SameLine();
                if (ImGui::Button("Clear")) { recomp_settings_set_text("keyboard", key, "none"); commit(); }
                ImGui::SameLine();
                if (ImGui::Button("Default")) { recomp_settings_reset_to_default("keyboard", key); commit(); }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static const char *k_show_file_label = "Show settings file";
static void open_folder()
{
    pcui::open_path(pcui::folder_path(pcui::Folder::Settings), true);
}
static void page_general(UiMode mode)
{
    ImGui::SeparatorText("In-game overlay");
    {
        RecompSetting *k = find("ui", "overlay_key");
        if (k) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s:", friendly("ui", "overlay_key"));
            ImGui::SameLine();
            bool capturing = g_capture.kind == Capture::KEY && g_capture.key == "overlay_key";
            if (capturing) {
                ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1), "Press a key...");
            } else {
                ImGui::TextUnformatted(k->stored_text);
                ImGui::SameLine();
                if (ImGui::Button("Change##overlaykey")) capture_start(Capture::KEY, "ui", "overlay_key", false);
            }
        }
        RecompSetting *p = find("ui", "overlay_pad");
        if (p) setting_widget(p);
        ImGui::TextWrapped("The overlay opens and closes with the key above or the pad buttons. A combination joined with + (for "
                           "example left_stick_click+right_stick_click) is held for half a second. While it is open the game "
                           "sees no input and keeps running.");
    }
    ImGui::SeparatorText("Language and subtitles");
    settings_group("language", { "game", "story_subtitles" });
    if (!get_text("language", "pack").empty())
        ImGui::TextWrapped("A language mod is on (Mods page), so the game uses its language; the game language "
                           "above applies when no language mod is on.");
    ImGui::TextWrapped("French is built into the game: menus and Story subtitles in French, with the voices and "
                       "menu icons in English. The game has English subtitles for every Story cutscene but shows "
                       "them only in the opening one (in French, in all of them). Choose all cutscenes to see them "
                       "everywhere; a language mod asks for them itself. Both apply on the next launch.");
    ImGui::SeparatorText("Appearance");
    settings_group("ui", { "scale" });

}
#if defined(DEFJAM_APP)
static bool g_app_maintenance = false;   // the app's own Maintenance screen was asked for
float pc_app_beside_play_width();
void pc_app_beside_play(float width, float height);
void pc_app_home_notice();               // src/app/app_update.cpp: a newer app, or a build to update
void pc_app_open_uninstall();            // src/app/app_setup.cpp: the app's Uninstall screen
#endif
static bool g_setup_request = false;
static void page_maintenance(UiMode mode)
{
    const char *names[] = {"Data", "Saves", "Mods", "Installation", "Settings file", "Play logs"};
    pcui::Folder paths[] = {pcui::Folder::Data,    pcui::Folder::Saves,    pcui::Folder::Mods,
                            pcui::Folder::Install, pcui::Folder::Settings, pcui::Folder::Logs};
    // Update / Repair first: what the page is most often opened for (owner, 2026-11-02).
    ImGui::SeparatorText("Repair game");
#if defined(DEFJAM_APP)
    (void)mode;
    ImGui::TextWrapped("Repair checks this version's files and rebuilds what is missing; Clean Rebuild builds "
                       "it again from nothing. Your saves and settings are kept.");
    if (ImGui::Button("Repair or rebuild..."))
        g_app_maintenance = true;
    ui_target("Maintenance");
#else
    if (mode == UI_OVERLAY)
        ImGui::TextWrapped("Close the game and open Update / Repair in the launcher to continue in Setup.");
    else
    {
        if (ImGui::Button("Open update / repair")) g_setup_request = true;
        if (ImGui::Button("Choose Setup application")) pcui::setup_choose();
    }
    ImGui::TextWrapped("Setup: %s", pcui::setup_path().empty() ? "not selected" : pcui::setup_path().c_str());
    ImGui::TextWrapped("Use Update / Repair on the launcher's Home screen to open the wizard and close the "
                       "game. Setup verifies releases and builds your dump locally.");
#endif
    for (int i = 0; i < 6; i++)
    {
        ImGui::PushID(i);
        auto path = pcui::folder_path(paths[i]);
        ImGui::SeparatorText(names[i]);
        ImGui::TextWrapped("%s", path.empty() ? "Not available for this run" : path.c_str());
        if (ImGui::Button("Open"))
            pcui::open_path(path, i == 4);
        ImGui::SameLine();
        if (ImGui::Button("Copy path"))
            pcui::copy_text(path);
        ui_target((std::string("Copy ") + names[i]).c_str());
        ImGui::PopID();
    }
    ImGui::SeparatorText("Files");
    ImGui::TextWrapped("Settings are saved as you change them in: %s", pc_settings_path());
    if (ImGui::Button(k_show_file_label)) open_folder();
    ImGui::SameLine();
    static bool confirm_reset;
    if (ImGui::Button("Reset every setting to default")) confirm_reset = true;
    if (confirm_reset) ImGui::OpenPopup("Reset every setting?");
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(ImGui::GetIO().DisplaySize.x - 24, ImGui::GetIO().DisplaySize.y - 24));
    if (ImGui::BeginPopupModal("Reset every setting?", &confirm_reset, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Resets graphics, bindings, language, texture selections, cheats, diagnostics and app preferences to factory defaults. Does not delete saves or installed mods, and cannot undo permanent Story profile edits.");
        if (ImGui::Button("Reset")) {
            for (size_t i = 0; i < recomp_settings_count(); i++) {
                RecompSetting *s = recomp_settings_at(i);
                recomp_settings_reset_to_default(s->section, s->key);
            }
            commit();
            confirm_reset = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { confirm_reset = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::SeparatorText("About");
    ImGui::Text("Def Jam: Fight for NY - Recompiled, version %s", DEFJAM_VERSION);
    ImGui::TextDisabled("Dear ImGui %s (MIT)  -  SDL3 (zlib)", IMGUI_VERSION);
#if defined(DEFJAM_APP)
    // Last on the page (owner, 2026-11-02): the app's own Uninstall screen (src/app/app_setup.cpp).
    ImGui::SeparatorText("Uninstall");
    ImGui::TextWrapped("Removes the game from this computer, with its Steam shortcut and this app. You choose "
                       "whether its data folder goes too; your saves are copied first.");
    if (ImGui::Button("Uninstall..."))
        pc_app_open_uninstall();
    ui_target("Uninstall");
#endif
}

static void page_debug(UiMode mode)
{
    ImGui::SeparatorText("Developer tools");

    ImGui::SeparatorText("Texture authoring");
    settings_group("textures", {"dump", "dump_limit"});
    ImGui::TextWrapped("Source captures go to hd-work/runtime. Turn capture off when finished.");
    ImGui::SeparatorText("Play-test recorder");
    settings_group("debug", { "record" });
    ImGui::TextWrapped("Keeps logs/sessions/<time>/ for every run: each frame's timing, what the game was doing, the sound, "
                       "and the last seconds of pictures. The mark key, the mark pad buttons (View and the left stick "
                       "click held together, by default) or the button below save a bug bundle of the "
                       "seconds before it; a crash or a hang saves one by itself. Bundles hold the game's own pictures "
                       "and sound: keep them on this computer. Read one with scripts/bug-bundle.py.");
    ImGui::Indent();
    settings_group("debug", { "mark_key", "mark_pad", "record_fps", "record_seconds", "record_audio", "record_game_state",
                              "auto_bundle_hitch_ms", "hang_seconds", "keep_sessions", "record_guest_ram" });
    ImGui::Unindent();
#if !defined(DEFJAM_APP)
    if (mode == UI_OVERLAY) {
        char status[1024];
        recorder_status(status, sizeof status);
        ImGui::SeparatorText("This run");
        ImGui::TextWrapped("%s", status);
        if (recorder_on()) {
            if (ImGui::Button("Mark a bug here")) recorder_mark("overlay");
            item_tooltip("Saves the last seconds of play, from before the overlay was opened, as a bug bundle.");
        } else {
            ImGui::TextDisabled("Turn recording on above and restart the game to record.");
        }

        /* Story scenes through the game's developer module (src/hooks/dev_module.c, scene_launcher.c). */
        ImGui::SeparatorText("Story scenes");
        static int pick = 0;
        int count = scene_launcher_count();
        if (count <= 0) {
            ImGui::TextDisabled("The game's scene list is not readable yet.");
            return;
        }
        if (pick >= count) pick = 0;
        char current[16];
        scene_launcher_name(pick, current, sizeof current);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::BeginCombo("Scene", current)) {
            for (int i = 0; i < count; i++) {
                char name[16];
                scene_launcher_name(i, name, sizeof name);
                if (ImGui::Selectable(name, i == pick)) pick = i;
                if (i == pick) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        int playing = scene_launcher_playing();
        bool active = dev_module_active() != 0;
        ImGui::BeginDisabled(playing >= 0);
        ImGui::SameLine();
        if (ImGui::Button("Play scene")) {
            scene_launcher_request(pick);
            dev_module_request(active ? 0 : 1);
        }
        item_tooltip("Plays the scene in the game's developer cinema mode, then returns to the main menu. "
                              "Whatever is running now (a fight, the Story) is left. Close the overlay to watch.");
        ImGui::EndDisabled();
        if (!active) {
            if (ImGui::Button("Developer menu")) dev_module_request(0);
            item_tooltip("EA's own debug menu, driven by pad 1 (try the d-pad and A). "
                                  "Leaves what is running now.");
        } else if (ImGui::Button("Back to the main menu")) {
            dev_module_leave();
        }
        if (playing >= 0) {
            char name[16];
            scene_launcher_name(playing, name, sizeof name);
            ImGui::Text("Playing %s.", name);
        } else {
            ImGui::TextDisabled(active ? "The developer module is running." : "The developer module is not running.");
        }
    }
#endif
}


// ---- cheats (src/patches/cheats.c; issue #71) ----------------------------------------------------------

static std::vector<pcui::Fighter> g_roster;
static bool g_roster_read;
static const char *k_styles[CHEAT_STYLE_COUNT] = {"Kickboxing", "Street fighting", "Martial arts", "Wrestling",
                                                  "Submissions"};
static const char *style_name(int style)
{
    return style >= 0 && style < CHEAT_STYLE_COUNT ? k_styles[style] : "none";
}
static void read_roster()
{
    if (g_roster_read)
        return;
    g_roster_read = true;
    std::ifstream in(std::filesystem::u8path("game/fighter.xml"), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    g_roster = pcui::roster(text);
}
static bool style_combo(const char *label, int *style, bool hybrid)
{
    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11);
    if (ImGui::BeginCombo(label, *style == CHEAT_STYLE_NONE ? "None" : style_name(*style)))
    {
        if (hybrid && ImGui::Selectable("None", *style == CHEAT_STYLE_NONE))
            *style = CHEAT_STYLE_NONE, changed = true;
        for (int i = 0; i < CHEAT_STYLE_COUNT; i++)
            if (ImGui::Selectable(k_styles[i], *style == i))
                *style = i, changed = true;
        ImGui::EndCombo();
    }
    return changed;
}
static void hero_picker()
{
    RecompSetting *hero = find("cheats", "story_hero");
    if (!hero)
        return;
    read_roster();
    bool locked = hero->source == RECOMP_SETTING_FROM_ENV;
    int value = locked ? hero->value : hero->stored;
    const char *off = "Off (your own fighter)";
    std::string current = value < 0 ? off : "Fighter " + std::to_string(value);
    for (const auto &f : g_roster)
        if (f.id == value)
            current = f.name;
    if (locked)
        ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    if (ImGui::BeginCombo(friendly("cheats", "story_hero"), current.c_str()))
    {
        int chosen = value;
        if (ImGui::Selectable(off, value < 0))
            chosen = -1;
        for (const auto &f : g_roster)
        {
            ImGui::PushID(f.id);
            if (ImGui::Selectable(f.name.c_str(), f.id == value))
                chosen = f.id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
        if (chosen != value && recomp_settings_set("cheats", "story_hero", chosen) > 0)
            commit();
    }
    ui_target("cheats.story_hero");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemFocused())
        g_help = hero->help;
    item_tooltip(hero->help);
    if (locked)
    {
        ImGui::EndDisabled();
        ImGui::TextDisabled("Set for this run by %s", hero->env);
    }
    if (g_roster.empty())
        ImGui::TextDisabled("The fighter list comes from the game's fighter.xml (game/fighter.xml), which was not found.");
}
// ---- Songs: restored songs and the player's own (docs/custom-songs.md) -----------------------------

struct SongRow
{
    SongEntry entry;
    SongTags tags;
};
static std::vector<SongRow> g_song_rows;
static bool g_songs_scanned = false;
static std::string g_songs_status;

static void songs_scan()
{
    std::string data = pcui::folder_path(pcui::Folder::Data);
    std::vector<SongEntry> found(SONG_LIBRARY_MAX);
    int n = song_library_scan(data.c_str(), found.data(), (int)found.size());
    g_song_rows.clear();
    for (int i = 0; i < n; i++)
    {
        SongRow row;
        row.entry = found[i];
        if (!song_read_tags(found[i].path, &row.tags))
            snprintf(row.tags.title, sizeof row.tags.title, "(cannot read)");
        g_song_rows.push_back(row);
    }
    g_songs_scanned = true;
}

static void songs_save()
{
    std::string data = pcui::folder_path(pcui::Folder::Data);
    std::vector<SongEntry> entries;
    for (auto &row : g_song_rows)
        entries.push_back(row.entry);
    g_songs_status = song_library_save(data.c_str(), entries.data(), (int)entries.size())
                         ? "Saved. The game uses it the next time it starts."
                         : "Could not save the song choices.";
}

// The Beatbox of a profile, from the state file the game writes when the profile loads
// (<data>/song-state/<profile>.txt): every song with its state byte (bits 0-1: 0 never arranged,
// 1 or 3 on, 2 off; bits 2-7 its place) and name. Saving adds a "launcher" line; the next load of
// that profile follows the file over the save (src/patches/song_store.c).
struct BeatboxSong
{
    std::string id, label;
    int state;
};
static std::vector<std::string> g_bb_profiles;
static int g_bb_profile = 0;
static std::vector<BeatboxSong> g_bb_songs;
static std::string g_bb_retail, g_bb_status;
static bool g_bb_loaded = false;
static const int k_bb_on_limit = 35;

static std::string bb_dir() { return pcui::folder_path(pcui::Folder::Data) + "/song-state"; }

static void bb_list_profiles()
{
    g_bb_profiles.clear();
    std::error_code ec;
    for (auto &e : std::filesystem::directory_iterator(std::filesystem::u8path(bb_dir()), ec))
    {
        std::string name = pcui::path_utf8(e.path().filename());
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0 && name != "library.txt")
            g_bb_profiles.push_back(name.substr(0, name.size() - 4));
    }
    std::sort(g_bb_profiles.begin(), g_bb_profiles.end());
    if (g_bb_profile >= (int)g_bb_profiles.size())
        g_bb_profile = 0;
}

static void bb_load()
{
    g_bb_songs.clear();
    g_bb_retail.clear();
    g_bb_loaded = true;
    if (g_bb_profiles.empty())
        return;
    std::ifstream f(std::filesystem::u8path(bb_dir() + "/" + g_bb_profiles[g_bb_profile] + ".txt"));
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#' || line.compare(0, 8, "launcher") == 0)
            continue;
        if (line.compare(0, 7, "retail ") == 0)
        {
            g_bb_retail = line;
            continue;
        }
        char id[32] = "";
        unsigned state = 0;
        int used = 0;
        if (sscanf(line.c_str(), "%31s %x %n", id, &state, &used) < 2)
            continue;
        BeatboxSong s{id, used > 0 && used < (int)line.size() ? line.substr(used) : std::string(id), (int)state};
        if (s.label.empty() || s.label == " - ")
            s.label = id;
        g_bb_songs.push_back(s);
    }
    // In Beatbox order: arranged songs by place, then the rest.
    std::stable_sort(g_bb_songs.begin(), g_bb_songs.end(), [](const BeatboxSong &a, const BeatboxSong &b) {
        int pa = (a.state & 3) ? a.state >> 2 : 999, pb = (b.state & 3) ? b.state >> 2 : 999;
        return pa < pb;
    });
}

static bool bb_on(const BeatboxSong &s) { return (s.state & 3) != 0 && (s.state & 3) != 2; }

static void bb_save()
{
    std::string path = bb_dir() + "/" + g_bb_profiles[g_bb_profile] + ".txt";
    std::ofstream f(std::filesystem::u8path(path), std::ios::trunc);
    f << "# The Beatbox of profile " << g_bb_profiles[g_bb_profile]
      << ", changed on the launcher's Songs page: the next load of the profile follows it.\n";
    if (!g_bb_retail.empty())
        f << g_bb_retail << "\n";
    f << "launcher 1\n";
    for (size_t k = 0; k < g_bb_songs.size(); k++)
    {
        int state = (int)(std::min<size_t>(k, 62) << 2) | (bb_on(g_bb_songs[k]) ? 3 : 2);
        g_bb_songs[k].state = state;
        char hex[8];
        snprintf(hex, sizeof hex, "%02X", state);
        f << g_bb_songs[k].id << " " << hex << " " << g_bb_songs[k].label << "\n";
    }
    g_bb_status = f.good() ? "Saved. The game uses it the next time this profile loads." : "Could not save the Beatbox.";
}

static void page_beatbox()
{
    float scale = ImGui::GetStyle().FontScaleMain;
    ImGui::SeparatorText("Beatbox");
    if (!g_bb_loaded)
    {
        bb_list_profiles();
        bb_load();
    }
    if (g_bb_profiles.empty())
    {
        ImGui::TextWrapped("Load a profile in the game once (start Story or Battle with it) and its Beatbox appears here.");
        return;
    }
    ImGui::SetNextItemWidth(240 * scale);
    if (ImGui::BeginCombo("Profile", g_bb_profiles[g_bb_profile].c_str()))
    {
        for (int i = 0; i < (int)g_bb_profiles.size(); i++)
            if (ImGui::Selectable(g_bb_profiles[i].c_str(), i == g_bb_profile))
            {
                g_bb_profile = i;
                bb_load();
            }
        ImGui::EndCombo();
    }
    ui_target("songs.beatbox.profile");
    int on = 0;
    for (auto &s : g_bb_songs)
        on += bb_on(s);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, on >= k_bb_on_limit ? ImVec4(1, 0.75f, 0.2f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::Text("%d of %d songs on", on, k_bb_on_limit);
    ImGui::PopStyleColor();
    ImGui::TextDisabled("The songs this profile's Beatbox plays, in its order. Changes apply the next time the profile loads.");
    bool changed = false;
    int move_from = -1, move_to = -1;
    if (ImGui::BeginTable("beatbox", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 50 * scale);
        ImGui::TableSetupColumn("Song");
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 110 * scale);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < g_bb_songs.size(); i++)
        {
            BeatboxSong &s = g_bb_songs[i];
            ImGui::PushID((int)i + 10000);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool v = bb_on(s);
            bool can = v || on < k_bb_on_limit;
            if (!can)
                ImGui::BeginDisabled();
            if (ImGui::Checkbox("##on", &v))
            {
                s.state = (s.state & 0xFC) | (v ? 3 : 2);
                changed = true;
            }
            if (!can)
                ImGui::EndDisabled();
            ui_target(("songs.beatbox.on." + s.id).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.label.c_str());
            ImGui::TableNextColumn();
            if (ImGui::ArrowButton("##up", ImGuiDir_Up) && i > 0)
            {
                move_from = (int)i;
                move_to = (int)i - 1;
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##down", ImGuiDir_Down) && i + 1 < g_bb_songs.size())
            {
                move_from = (int)i;
                move_to = (int)i + 1;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (move_from >= 0)
    {
        std::swap(g_bb_songs[move_from], g_bb_songs[move_to]);
        changed = true;
    }
    if (changed)
        bb_save();
    if (!g_bb_status.empty())
        ImGui::TextDisabled("%s", g_bb_status.c_str());
}

static void page_songs(UiMode mode)
{
    float scale = ImGui::GetStyle().FontScaleMain;
    ImGui::SeparatorText("Songs");
    settings_group("songs", {"restored", "custom"});
    ImGui::TextWrapped("Your own music plays like the game's soundtrack: in the Beatbox, in the menus with the song "
                       "banner, and in fights. Put MP3, FLAC, Ogg or WAV files in the songs folder (subfolders are "
                       "fine), or add folders below. The first start after adding a song converts it, a few seconds a "
                       "song.");
    char folder[SONG_LIBRARY_PATH];
    song_library_folder(pcui::folder_path(pcui::Folder::Data).c_str(), folder, sizeof folder);
    if (ImGui::Button("Open songs folder"))
        pcui::open_path(folder);
    ui_target("songs.open_folder");
    ImGui::SameLine();
    if (ImGui::Button("Copy its path"))
        pcui::copy_text(folder);
    ui_target("songs.copy_folder");
    ImGui::SameLine();
    if (ImGui::Button("Look for songs again") || !g_songs_scanned)
    {
        songs_scan();
        g_bb_loaded = false;
    }
    ui_target("songs.rescan");
    settings_group("songs", {"folders"});
    ImGui::SeparatorText("Shortcuts");
    ImGui::TextWrapped("Skip to the next song, or play the current one from its start, in the menus and in fights. "
                       "On a pad, hold the right stick in and press the D-pad (the game sees the D-pad press too).");
    settings_group("songs", {"skip_pad", "restart_pad", "skip_key", "restart_key"});

    int on = 0;
    for (auto &row : g_song_rows)
        on += row.entry.on;
    ImGui::SeparatorText("Your songs");
    {
        // The two limits, where they cannot be missed.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.75f, 0.2f, 1));
        ImGui::BulletText("Up to 27 of your own songs in the game (next to its own 33).");
        ImGui::BulletText("Up to 35 songs switched on in the Beatbox at once.");
        ImGui::PopStyleColor();
    }
    ImGui::TextWrapped("Interactive (experimental) builds a song on a licensed song's arrangement: in fights it "
                       "switches between the song and filtered versions of it as the fight goes.");
    if (g_song_rows.empty())
        ImGui::TextDisabled("No music files found yet.");
    if (!g_song_rows.empty())
    ImGui::Text("%d file(s) found, %d chosen for the game%s", (int)g_song_rows.size(), on,
                on > SONG_LIBRARY_ROOM ? " (the first 27 are used)" : "");
    if (mode != UI_LAUNCHER)
        ImGui::TextDisabled("Changes apply the next time the game starts.");
    bool changed = false;
    if (!g_song_rows.empty() && ImGui::BeginTable("songs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("In game", ImGuiTableColumnFlags_WidthFixed, 70 * scale);
        ImGui::TableSetupColumn("Song");
        ImGui::TableSetupColumn("Artist");
        ImGui::TableSetupColumn("Plays", ImGuiTableColumnFlags_WidthFixed, 190 * scale);
        ImGui::TableHeadersRow();
        int used = 0;
        for (size_t i = 0; i < g_song_rows.size(); i++)
        {
            SongRow &row = g_song_rows[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool in_game = row.entry.on != 0;
            if (ImGui::Checkbox("##on", &in_game))
            {
                row.entry.on = in_game;
                changed = true;
            }
            ui_target(("songs.on." + std::string(row.entry.id)).c_str());
            if (row.entry.on)
                used++;
            ImGui::TableNextColumn();
            if (row.entry.on && used > SONG_LIBRARY_ROOM)
                ImGui::TextDisabled("%s (no room)", row.tags.title);
            else
                ImGui::TextUnformatted(row.tags.title);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nAlbum: %s", row.entry.path, row.tags.album);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.tags.artist);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            int interactive = row.entry.interactive;
            const char *modes[] = {"Straight", "Interactive (experimental)"};
            if (ImGui::Combo("##mode", &interactive, modes, 2))
            {
                row.entry.interactive = interactive;
                changed = true;
            }
            ui_target(("songs.mode." + std::string(row.entry.id)).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (changed)
        songs_save();
    if (!g_songs_status.empty())
        ImGui::TextDisabled("%s", g_songs_status.c_str());
    page_beatbox();
}

static void page_cheats(UiMode mode)
{
    ImGui::SeparatorText("Fights");
    settings_group("cheats", {"infinite_health", "max_blazin", "no_regen", "fighter_size"});
    ImGui::TextWrapped("Fight cheats apply as you change them, in every mode, and are never saved into a profile. "
                       "Max Blazin' fills the meter five seconds into a fight; start Blazin' as usual. Giant and tiny "
                       "change only the picture: reach and collision stay the same.");
    ImGui::SeparatorText("Story fights");
    hero_picker();
    ImGui::TextWrapped("Story fights are played as that fighter, with their look and moves. Cutscenes, the crib and "
                       "the shops still show your own fighter. Nothing is saved into your profile.");

    ImGui::SeparatorText("Story profile");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.75f, 0.2f, 1));
    ImGui::TextWrapped("These change the Story profile you are playing. Once the game saves, the change is permanent "
                       "and cannot be undone. Copy your save folder first (Maintenance, Saves).");
    ImGui::PopStyleColor();
    if (mode == UI_LAUNCHER)
    {
        ImGui::TextWrapped("Open the in-game overlay while you play Story to use these.");
        return;
    }
#if !defined(DEFJAM_APP)
    CheatStory story;
    static int pending, main_style = -1, hybrid1 = CHEAT_STYLE_NONE, hybrid2 = CHEAT_STYLE_NONE;
    if (!cheats_story_info(&story))
    {
        ImGui::TextDisabled("Start Story mode with your profile first.");
        main_style = -1;
    }
    else
    {
        ImGui::Text("Cash: $%u    Development points: %u", story.cash, story.devpts);
        ImGui::Text("Fighting styles: %s%s%s%s%s", style_name(story.style[0]),
                    story.style[1] == CHEAT_STYLE_NONE ? "" : ", ", story.style[1] == CHEAT_STYLE_NONE ? "" : style_name(story.style[1]),
                    story.style[2] == CHEAT_STYLE_NONE ? "" : ", ", story.style[2] == CHEAT_STYLE_NONE ? "" : style_name(story.style[2]));
        if (ImGui::Button("Max cash ($999,999)"))
            pending = CHEAT_STORY_CASH;
        ui_target("Max cash");
        ImGui::SameLine();
        if (ImGui::Button("Max development points (99,999)"))
            pending = CHEAT_STORY_DEVPTS;
        ui_target("Max development points");
        if (main_style < 0)
        {
            main_style = story.style[0] < CHEAT_STYLE_COUNT ? story.style[0] : 0;
            hybrid1 = story.style[1];
            hybrid2 = story.style[2];
        }
        style_combo("Main style", &main_style, false);
        style_combo("First hybrid", &hybrid1, true);
        style_combo("Second hybrid", &hybrid2, true);
        if (ImGui::Button("Change fighting styles"))
            pending = CHEAT_STORY_STYLES;
        ui_target("Change fighting styles");
        item_tooltip("Your fighter's main style and up to two hybrid styles, as if learned at the gym. Each style "
                     "can be chosen once; the second hybrid needs a first.");
        if (pending)
            ImGui::OpenPopup("Change your Story profile?");
    }
    if (ImGui::BeginPopupModal("Change your Story profile?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
        ImGui::TextWrapped("%s is written into the Story profile you are playing. When the game next saves, the "
                           "change is permanent and cannot be undone. Copy your save folder first if you may want "
                           "to go back.",
                           pending == CHEAT_STORY_CASH     ? "$999,999 cash"
                           : pending == CHEAT_STORY_DEVPTS ? "99,999 development points"
                                                           : "The new fighting styles");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Apply"))
        {
            cheats_story_request(pending, main_style, hybrid1, hybrid2);
            pending = 0;
            ImGui::CloseCurrentPopup();
        }
        ui_target("Apply");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            pending = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (*cheats_story_status())
        ImGui::TextWrapped("%s", cheats_story_status());
#endif
}

static int g_page = 0;
static bool g_advanced = false, g_select_page = false, g_return_focus = false;
static std::string g_home_return = "Settings";
static const char *page_names[] = {"Graphics", "Controls", "Mods", "Songs", "Language & access", "Cheats", "App preferences", "Help"};
static const int k_pages = (int)(sizeof page_names / sizeof page_names[0]);
static void controls_page()
{
    if (ImGui::CollapsingHeader("Controller mapping")) page_pad_buttons();
    if (ImGui::CollapsingHeader("Keyboard and mouse")) page_keyboard();
    page_controllers();
}
UiResult ui_draw(UiMode mode, float x, float y, float width, float height)
{
    UiResult result;
    g_mode = mode;
    capture_poll_pad();
    g_targets.clear();
    g_help.clear();
    pcui::setup_poll();
    if (pcui::mod_poll()) refresh_packs(true);
    static bool recovery_attempted = false;
    if (mode == UI_LAUNCHER && pcui::mod_recovery_needed() && !pcui::mod_busy() && !recovery_attempted) {
        recovery_attempted = true; pcui::mod_start("recover");
    }
    if (!pcui::mod_recovery_needed()) recovery_attempted = false;
    float scale = ImGui::GetStyle().FontScaleMain;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse;
    // At maximum text size, keep enough height for the independently scrolling controls.
    bool tight = height < 250 * scale;
    ImVec2 padding = ImGui::GetStyle().WindowPadding;
    if (tight)
        padding.y = std::min(padding.y, 8 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::Begin("##settings", nullptr, flags);
    ImGui::PopStyleVar();
    const char *brand = tight ? "DEF JAM" : "DEF JAM RECOMPILED";
    float header_width = ImGui::CalcTextSize(brand).x +
                         ImGui::CalcTextSize("/ v" DEFJAM_VERSION).x + ImGui::GetStyle().ItemSpacing.x;
    bool version_inline = header_width < ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(brand);
    if (version_inline)
        ImGui::SameLine();
    ImGui::TextDisabled("/ v%s", DEFJAM_VERSION);
    ImGui::Separator();
    bool advanced = mode == UI_OVERLAY || g_advanced;
    bool compact_footer = height < 600 * scale && pcui::action_status().empty();
    float footer = ImGui::GetFrameHeightWithSpacing() * (compact_footer ? 1.0f : 2.1f) +
                   (compact_footer && tight ? 8 : 25) * scale;
#if defined(DEFJAM_APP)
    if (mode == UI_LAUNCHER) pc_app_home_notice();
#else
    if (mode == UI_LAUNCHER) {
        if (ImGui::Button("Check for updates")) pcui::open_path("https://github.com/vyanhursky/DefJamFFNY-recomp/releases/latest");
        ui_target("updates.check");
        ImGui::TextWrapped("Opens the latest release in your browser. Help provides setup and repair.");
        ImGui::Separator();
    }
#endif
    ImGui::BeginChild("page", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (!advanced)
    {
        auto home = ui_launcher_home(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y,
                                     g_return_focus ? g_home_return.c_str() : nullptr);
        g_return_focus = false;
        result.play = home.play && !pcui::mod_busy() && !pcui::mod_recovery_needed();
        if (home.route >= 1 && home.route <= 3)
        {
            g_advanced = true;
            ImGui::SetScrollY(0);
            g_page = home.route == 2 ? 1 : home.route == 3 ? 2 : g_page;
            g_select_page = true;
            g_home_return = home.route == 2 ? "Configure" : home.route == 3 ? "Mods" : "Settings";
        }
        if (home.route == 4) {
            g_advanced = true; g_page = 7; g_select_page = true; g_home_return = "Help";
        }
        if (home.route == 5) {
            g_advanced = true; ImGui::SetScrollY(0); g_page = 3; g_select_page = true; g_home_return = "Songs";
        }
    }
    else
    {
        bool compact_header = height < 500 * scale;
        if (mode == UI_LAUNCHER)
        {
            if (ImGui::Button(compact_header ? "< Back" : "< Back to launcher"))
            {
                g_advanced = false;
                ImGui::SetScrollY(0);
                g_return_focus = true;
            }
            ui_target("Back");
            if (compact_header || width > 650 * scale)
                ImGui::SameLine();
        }
        if (!compact_header)
            pcui::heading("SETTINGS", 24 * scale);
        bool popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!ui_capturing() && !popup)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false))
            {
                g_page = (g_page + 1) % k_pages;
                g_select_page = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false))
            {
                g_page = (g_page + k_pages - 1) % k_pages;
                g_select_page = true;
            }
            if (mode == UI_LAUNCHER && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
            {
                g_advanced = false;
                ImGui::SetScrollY(0);
                g_return_focus = true;
            }
        }
        // At large text sizes a vertical selector keeps every category available.
        bool narrow = ImGui::GetContentRegionAvail().x < 680 * scale;
        if (narrow)
        {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##category", page_names[g_page]))
            {
                for (int i = 0; i < k_pages; i++)
                    if (ImGui::Selectable(page_names[i], g_page == i))
                        g_page = i;
                ImGui::EndCombo();
            }
            ui_target("Category");
        }
        else if (ImGui::BeginTabBar("categories", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
        {
            int requested = g_page;
            for (int i = 0; i < k_pages; i++)
            {
                bool active =
                    ImGui::BeginTabItem(page_names[i], nullptr,
                                        g_select_page && requested == i ? ImGuiTabItemFlags_SetSelected : 0);
                ui_target(page_names[i]);
                if (active)
                {
                    g_page = i;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        g_select_page = false;
        bool inspector = ImGui::GetContentRegionAvail().x >= 900 * scale;
        if (!inspector) {
            if (ImGui::Button("About the selected option")) ImGui::OpenPopup("Option help");
            ui_target("settings.help");
            ImGui::SetNextWindowSize(ImVec2(std::min(500 * scale, width - 24), std::min(300 * scale, height - 24)), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Option help", nullptr, ImGuiWindowFlags_NoResize)) {
                ImGui::BeginChild("help text", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
                ImGui::TextWrapped("%s", g_help.empty() ? "Focus an option to see its explanation here." : g_help.c_str());
                ImGui::EndChild();
                if (ImGui::Button("Done") || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::BeginChild("settings content",
                          ImVec2(inspector ? ImGui::GetContentRegionAvail().x - 280 * scale : 0, 0),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        switch (g_page)
        {
        case 0:
            page_display();
            break;
        case 1:
            controls_page();
            break;
        case 2:
            page_textures();
            break;
        case 3:
            page_songs(mode);
            break;
        case 4:
            page_general(mode);
            break;
        case 5:
            page_cheats(mode);
            ImGui::SeparatorText("Permanent profile changes");
            if (recomp_settings_get("debug", "unlock_all", 0)) {
                ImGui::TextWrapped("Unlock all is enabled. Turning it off does not undo saved unlocks.");
                if (ImGui::Button("Turn off unlock all")) { recomp_settings_set("debug", "unlock_all", 0); commit(); }
            } else {
                if (ImGui::Button("Unlock all fighters and venues...")) ImGui::OpenPopup("Permanent unlocks");
                ui_target("cheats.unlock_all");
            }
            ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(width - 24, height - 24));
            if (ImGui::BeginPopupModal("Permanent unlocks", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(420 * scale, width - 80));
                ImGui::TextWrapped("This can permanently save unlocks to your Story profile. Turning it off will not restore your previous progress. Back up your saves in Help first.");
                ImGui::PopTextWrapPos();
                if (ImGui::Button("Enable permanent unlocks")) { recomp_settings_set("debug", "unlock_all", 1); commit(); ImGui::CloseCurrentPopup(); }
                ui_target("cheats.unlock_all.confirm");
                if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            break;
        case 6:
            settings_group("debug", {"skip_intro"});
            ImGui::TextWrapped("Skip startup movies and the legal screen; Story cutscenes still play.");
            ImGui::Spacing();
            settings_group("launcher", {"skip", "check_updates"});
            ImGui::TextWrapped("To reopen a skipped launcher, hold Shift or both shoulder buttons at startup, or use --launcher. Steam shortcuts open the launcher unless you choose to skip it.");
            break;
        case 7:
            page_maintenance(mode);
            if (ImGui::CollapsingHeader("Developer diagnostics")) page_debug(mode);
            break;
        }
        ImGui::EndChild();
        if (inspector)
        {
            ImGui::SameLine();
            ImGui::BeginChild("context help", ImVec2(0, 0),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            pcui::heading("ABOUT THIS OPTION", 18 * scale);
            ImGui::Spacing();
            ImGui::TextWrapped(
                "%s", g_help.empty()
                          ? "Focus or hover a setting to see what it does. Changes save as you make them."
                          : g_help.c_str());
            ImGui::Spacing();
            ImGui::TextColored(pcui::gold(), mode == UI_LAUNCHER ? "Startup changes apply on Play."
                                                                 : "Startup changes apply next launch.");
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    std::string restart = restart_needed();
    if (g_save_failed) {
        ImGui::TextColored(pcui::gold(), "Could not save settings.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Retry save")) commit();
    }
    else if (!compact_footer && !pcui::action_status().empty())
    {
        ImGui::BeginChild("last action", ImVec2(0, ImGui::GetTextLineHeight() * 2), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        ImGui::TextWrapped("%s", pcui::action_status().c_str());
        ImGui::EndChild();
    }
    else if (!compact_footer && !restart.empty())
        ImGui::TextWrapped("%s: %s", mode == UI_LAUNCHER ? "Applies on Play" : "Next launch",
                           restart.c_str());
    else if (!compact_footer)
        ImGui::TextDisabled(mode == UI_LAUNCHER ? "Saved as you change / startup options apply on Play"
                                                : "Saved as you change / startup options apply next launch");
    // Keep actions reachable even at 250% text scale; the body scrolls independently.
    if (mode == UI_LAUNCHER)
    {
        if (ImGui::Button("Quit"))
            result.leave = true;
        ui_target("Quit");
        ImGui::SameLine();
        if (true)
        {
            ImGui::BeginDisabled(pcui::mod_busy() || pcui::mod_recovery_needed());
            if (pcui::play_button("PLAY", ImVec2(100 * scale, 0),
                                  tight ? ImGui::GetFontSize() / scale : 30.0f))
                result.play = true;
            ui_target("Play");
            if (!advanced && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            ImGui::EndDisabled();
            ImGui::SameLine();
#if defined(DEFJAM_APP)
            if (float beside = pc_app_beside_play_width(); beside > 0) {
                pc_app_beside_play(beside, 0);
                ImGui::SameLine();
            }
#endif
        }
        if (!compact_footer)
            ImGui::TextDisabled("Navigate with mouse, keyboard, or controller.");
        else
        {
            const char *status = g_save_failed ? "Not saved" : mode == UI_LAUNCHER || restart.empty() ? "Saved" : "Next launch";
            if (ImGui::GetContentRegionAvail().x >= ImGui::CalcTextSize(status).x)
                ImGui::TextDisabled("%s", status);
        }
    }
    else
    {
        if (ImGui::Button("Close"))
            result.leave = true;
        ImGui::SameLine();
        if (!compact_footer)
            ImGui::TextDisabled("Navigate with mouse, keyboard, or controller.");
        else
        {
            const char *status = g_save_failed ? "Not saved" : mode == UI_LAUNCHER || restart.empty() ? "Saved" : "Next launch";
            if (ImGui::GetContentRegionAvail().x >= ImGui::CalcTextSize(status).x)
                ImGui::TextDisabled("%s", status);
        }
    }
    if (mode == UI_LAUNCHER && pcui::mod_busy()) {
        result.play = false;
        ImGui::TextWrapped("%s", pcui::mod_status().c_str());
    }
#if defined(DEFJAM_APP)
    // The app runs setup itself (src/app/app_setup.cpp): no wizard to find and open.
    if (g_setup_request || g_app_maintenance)
        result.setup = true;
    g_setup_request = g_app_maintenance = false;
#else
    if (g_setup_request)
    {
        ImGui::OpenPopup("Update / Repair");
        g_setup_request = false;
    }
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0, 0), ImVec2(std::max(200.0f, width - 32 * scale), std::max(160.0f, height - 32 * scale)));
    if (ImGui::BeginPopupModal("Update / Repair", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushTextWrapPos(std::min(ImGui::GetFontSize() * 34, std::max(120.0f, width - 100 * scale)));
        ImGui::TextWrapped("Open Def Jam Setup to choose a release, update or repair. The launcher will "
                           "close. No installation starts until you choose it in the wizard.");
        ImGui::TextWrapped("Setup: %s",
                           pcui::setup_path().empty() ? "not selected" : pcui::setup_path().c_str());
        if (ImGui::Button("Choose Setup..."))
            pcui::setup_choose();
        if (width > 650 * scale)
            ImGui::SameLine();
        if (ImGui::Button("Download Setup"))
            pcui::open_path("https://github.com/vyanhursky/DefJamFFNY-recomp/releases/latest");
        ImGui::BeginDisabled(!pcui::setup_ready());
        if (ImGui::Button("Open Setup"))
        {
            result.setup = true;
            result.leave = true;
            ImGui::CloseCurrentPopup();
        }
        ui_target("SetupOpen");
        ImGui::EndDisabled();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
#endif
    ImGui::End();
    return result;
}

#if !defined(DEFJAM_APP)
// ---- the overlay ----------------------------------------------------------------------------------------------------

#if !defined(_WIN32)
extern "C" void host_posix_overlay_changed(int open);      // src/host_posix.c
#endif

namespace {

std::atomic<bool> g_open(false);
std::atomic<bool> g_toggle(false);
bool g_just_opened = false;
bool g_ready = false;               // the context and backends exist (under the lock)
#if defined(_WIN32)
HWND g_window = nullptr;
ID3D11Device *g_device = nullptr;
#else
UiWindow g_window = nullptr;        // unused: the overlay is drawn in the picture's own window
#endif
int g_overlay_vk = VK_F1;

struct Chord { int src[3]; int n; UiTick since; bool fired; };
std::vector<Chord> g_chords;        // read and written on the drawing thread, and by apply_settings under the lock
std::vector<Chord> g_mark_chords;   // [debug] mark_pad: the recorder's mark, from a pad
std::vector<Chord> g_song_chords[2]; // [songs] skip_pad, restart_pad (src/patches/song_cursor.c)
int g_song_vk[2] = {VK_F8, VK_F7};  // [songs] skip_key, restart_key (0: none)

void parse_chord_list(const std::string &text, std::vector<Chord> &chords)
{
    chords.clear();
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t comma = text.find(',', pos);
        std::string alt = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? text.size() + 1 : comma + 1;
        Chord c = {};
        size_t p = 0;
        while (p <= alt.size() && c.n < 3) {
            size_t plus = alt.find('+', p);
            std::string name = alt.substr(p, plus == std::string::npos ? std::string::npos : plus - p);
            p = plus == std::string::npos ? alt.size() + 1 : plus + 1;
            size_t a = name.find_first_not_of(" \t"), b = name.find_last_not_of(" \t");
            if (a == std::string::npos) continue;
            int src = xbox_InputMap_SourceFromName(name.substr(a, b - a + 1).c_str());
            if (src > INPUT_SRC_NONE) c.src[c.n++] = src;
        }
        if (c.n) chords.push_back(c);
    }
}

void parse_chords()
{
    parse_chord_list(get_text("ui", "overlay_pad"), g_chords);
    parse_chord_list(get_text("debug", "mark_pad"), g_mark_chords);
    parse_chord_list(get_text("songs", "skip_pad"), g_song_chords[0]);
    parse_chord_list(get_text("songs", "restart_pad"), g_song_chords[1]);
}

// A chord of `chords` was just completed, on any pad: one button at once, two or three held half a second.
bool chords_pressed(std::vector<Chord> &chords)
{
    InputRaw all;
    memset(&all, 0, sizeof(all));
    for (int slot = 0; slot < 4; slot++) {
        InputRaw raw;
        if (!xbox_HostInputRawPad(slot, &raw)) continue;
        for (int s = 0; s < INPUT_SRC_COUNT; s++)
            if (raw.src[s] > all.src[s]) all.src[s] = raw.src[s];
    }
    UiTick now = ui_ticks();
    bool fire = false;
    for (Chord &c : chords) {
        bool held = true;
        for (int i = 0; i < c.n; i++) held = held && all.src[c.src[i]] >= 16384;
        if (!held) { c.since = 0; c.fired = false; continue; }
        if (!c.since) c.since = now;
        if (!c.fired && now - c.since >= (c.n > 1 ? 500u : 0u)) { c.fired = true; fire = true; }
    }
    return fire;
}

// Pad buttons that open and close the overlay, from any pad.
bool pad_toggle_pressed() { return chords_pressed(g_chords); }

// The recorder's mark from a pad; checked once a frame wherever the overlay chord is.
void pad_mark_poll()
{
    if (recorder_on() && !g_mark_chords.empty() && !ui_capturing() && chords_pressed(g_mark_chords))
        recorder_mark("pad");
}

// [songs] skip_pad / restart_pad: a chord acts at once, when its last button goes down, on any pad;
// not while the left stick is in as well (L3+R3 is the overlay's chord, unless the chord names it).
void song_pad_poll()
{
    InputRaw all;
    memset(&all, 0, sizeof(all));
    for (int slot = 0; slot < 4; slot++) {
        InputRaw raw;
        if (!xbox_HostInputRawPad(slot, &raw)) continue;
        for (int s = 0; s < INPUT_SRC_COUNT; s++)
            if (raw.src[s] > all.src[s]) all.src[s] = raw.src[s];
    }
    for (int what = 0; what < 2; what++)
        for (Chord &c : g_song_chords[what]) {
            bool held = true, names_l3 = false;
            for (int i = 0; i < c.n; i++) {
                held = held && all.src[c.src[i]] >= 16384;
                names_l3 = names_l3 || c.src[i] == INPUT_SRC_LEFT_STICK_CLICK;
            }
            if (!names_l3 && all.src[INPUT_SRC_LEFT_STICK_CLICK] >= 16384) held = false;
            if (held && !c.fired) song_cursor_request(what == 0 ? SONG_REQUEST_SKIP : SONG_REQUEST_RESTART, "pad");
            c.fired = held;
        }
}

// [songs] skip_key / restart_key: which request a key-down is, if any.
int song_key_request(int vk)
{
    if (vk <= 0 || vk == g_overlay_vk) return 0;
    if (vk == g_song_vk[0]) return SONG_REQUEST_SKIP;
    if (vk == g_song_vk[1]) return SONG_REQUEST_RESTART;
    return 0;
}

void set_open(bool open)
{
    if (g_open.load() == open) return;
    g_open = open;
    g_just_opened = open;
    xbox_HostInputSetUiActive(open ? 1 : 0);
#if !defined(_WIN32)
    host_posix_overlay_changed(open ? 1 : 0);       // the pointer shows, and typing is delivered
#endif
    fprintf(stderr, "[UI] overlay %s\n", open ? "opened" : "closed");
    fflush(stderr);
    recorder_event("overlay", open ? "\"open\":true" : "\"open\":false");
}

} // namespace

#if defined(_WIN32)
extern "C" void pc_ui_set_window(HWND window) { g_window = window; }
#endif

extern "C" int pc_ui_overlay_open(void) { return g_open.load() ? 1 : 0; }

extern "C" void pc_ui_apply_settings(void)
{
    ui_lock();
    std::string key = get_text("ui", "overlay_key");
    int vk = xbox_InputMap_KeyFromName(key.c_str());
    g_overlay_vk = vk > 0 ? vk : VK_F1;
    std::string song_keys[2] = {get_text("songs", "skip_key"), get_text("songs", "restart_key")};
    for (int i = 0; i < 2; i++)
        g_song_vk[i] = song_keys[i].empty() ? 0 : xbox_InputMap_KeyFromName(song_keys[i].c_str());
    parse_chords();
    if (g_ready) ui_apply_scale(g_window);
    ui_unlock();
}

#if defined(_WIN32)
extern "C" int pc_ui_window_message(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lp & (1 << 30)) && !g_open.load()) {
        int request = song_key_request((int)wp);
        if (request) {
            ui_lock();
            bool capturing = ui_capturing();
            ui_unlock();
            if (!capturing) {
                song_cursor_request(request, "key");
                return 1;
            }
        }
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lp & (1 << 30)) && recorder_mark_vk() &&
        (int)wp == recorder_mark_vk() && (int)wp != g_overlay_vk) {
        ui_lock();
        bool capturing = ui_capturing();
        ui_unlock();
        if (!capturing) {
            recorder_mark("key");
            return 1;
        }
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lp & (1 << 30)) && (int)wp == g_overlay_vk) {
        ui_lock();
        bool capturing = ui_capturing();
        ui_unlock();
        if (!capturing) {
            g_toggle = true;
            return 1;
        }
    }
    if (!g_open.load()) return 0;
    ui_lock();
    int handled = 0;
    if (g_ready) {
        if (ui_capture_message(window, msg, wp, lp)) handled = 1;
        else ImGui_ImplWin32_WndProcHandler(window, msg, wp, lp);
    }
    ui_unlock();
    return handled;
}

extern "C" void pc_ui_overlay(void *device, void *context, void *window_rtv, unsigned width, unsigned height, void *)
{
    static bool configured;
    if (!configured) {
        configured = true;
        pc_ui_apply_settings();
    }
    ui_lock();
    bool toggled = g_toggle.exchange(false);
    pad_mark_poll();
    if (!ui_capturing() && !g_open.load()) song_pad_poll();
    if (!ui_capturing() && !g_chords.empty() && pad_toggle_pressed()) toggled = true;
    if (toggled) set_open(!g_open.load());
    if (!g_open.load()) { ui_unlock(); return; }

    ID3D11Device *dev = (ID3D11Device *)device;
    ID3D11DeviceContext *ctx = (ID3D11DeviceContext *)context;
    if (!g_ready) {
        if (!g_window) { ui_unlock(); return; }
        ui_context_begin(g_window);
        ImGui_ImplWin32_Init(g_window);
        ImGui_ImplDX11_Init(dev, ctx);
        g_device = dev;
        g_ready = true;
        fprintf(stderr, "[UI] overlay ready (Dear ImGui %s)\n", IMGUI_VERSION);
        fflush(stderr);
    }

    {
        float h = height / 960.0f;
        g_extra_scale = h < 1.0f ? 1.0f : h > 2.4f ? 2.4f : h;
    }
    ui_apply_scale(g_window);
    ui_feed_gamepad();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Dim the game and put the settings in a panel in the middle.
    ImGuiIO &io = ImGui::GetIO();
    if (g_just_opened) {
        g_just_opened = false;
        ImGui::SetNextWindowFocus();
    }
    ImDrawList *bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
    float pw = io.DisplaySize.x * 0.82f, ph = io.DisplaySize.y * 0.86f;
    if (pw < 560) pw = io.DisplaySize.x;
    if (ph < 420) ph = io.DisplaySize.y;
    bool close_request = false;
    {
        ImGui::SetNextWindowBgAlpha(0.96f);
        UiResult r = ui_draw(UI_OVERLAY, (io.DisplaySize.x - pw) / 2, (io.DisplaySize.y - ph) / 2, pw, ph);
        close_request = r.leave;
    }
    // Escape or the pad's cancel button closes the overlay, unless a popup or a capture is open.
    if (!ui_capturing() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
        close_request = true;

    ImGui::Render();
    ID3D11RenderTargetView *old_rtv = nullptr;
    ID3D11DepthStencilView *old_dsv = nullptr;
    ctx->OMGetRenderTargets(1, &old_rtv, &old_dsv);
    ID3D11RenderTargetView *rtv = (ID3D11RenderTargetView *)window_rtv;
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ctx->OMSetRenderTargets(1, &old_rtv, old_dsv);       // the title's own target, as it was
    if (old_rtv) old_rtv->Release();
    if (old_dsv) old_dsv->Release();
    (void)width;
    if (close_request) set_open(false);
    ui_unlock();
}
#endif
#endif  // !DEFJAM_APP

#if !defined(PCUI_WIN32)
// ---- an SDL window (off Windows, and the app): SDL events, and the overlay through the Vulkan present --------------------------------------------

static ImGuiKey imgui_key(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return (ImGuiKey)(ImGuiKey_A + (sc - SDL_SCANCODE_A));
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return (ImGuiKey)(ImGuiKey_1 + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return (ImGuiKey)(ImGuiKey_F1 + (sc - SDL_SCANCODE_F1));
    switch (sc) {
    case SDL_SCANCODE_0: return ImGuiKey_0;
    case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
    case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
    case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
    case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
    case SDL_SCANCODE_HOME: return ImGuiKey_Home;
    case SDL_SCANCODE_END: return ImGuiKey_End;
    case SDL_SCANCODE_INSERT: return ImGuiKey_Insert;
    case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
    case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
    case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
    case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
    case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
    case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_MINUS: return ImGuiKey_Minus;
    case SDL_SCANCODE_EQUALS: return ImGuiKey_Equal;
    case SDL_SCANCODE_PERIOD: return ImGuiKey_Period;
    case SDL_SCANCODE_COMMA: return ImGuiKey_Comma;
    case SDL_SCANCODE_SLASH: return ImGuiKey_Slash;
    case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
    case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
    case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
    case SDL_SCANCODE_LALT: return ImGuiKey_LeftAlt;
    case SDL_SCANCODE_RALT: return ImGuiKey_RightAlt;
    case SDL_SCANCODE_LGUI: return ImGuiKey_LeftSuper;
    case SDL_SCANCODE_RGUI: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

static float g_extra_scale_set = 1.0f;
static unsigned long long g_last_frame_ns;

void ui_set_extra_scale(float scale) { g_extra_scale = scale; (void)g_extra_scale_set; }

bool ui_sdl_event(const SDL_Event *e, float scale_x, float scale_y)
{
    ImGuiIO &io = ImGui::GetIO();
    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION:
        io.AddMousePosEvent(e->motion.x * scale_x, e->motion.y * scale_y);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int b = e->button.button == SDL_BUTTON_LEFT ? 0 : e->button.button == SDL_BUTTON_RIGHT ? 1 :
                e->button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
        io.AddMousePosEvent(e->button.x * scale_x, e->button.y * scale_y);
        if (b >= 0) io.AddMouseButtonEvent(b, e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        io.AddMouseWheelEvent(e->wheel.x, e->wheel.y);
        return true;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return true;
    case SDL_EVENT_TEXT_INPUT:
        io.AddInputCharactersUTF8(e->text.text);
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        SDL_Keymod mod = e->key.mod;
        io.AddKeyEvent(ImGuiMod_Ctrl, (mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (mod & SDL_KMOD_ALT) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (mod & SDL_KMOD_GUI) != 0);
        ImGuiKey k = imgui_key(e->key.scancode);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, e->type == SDL_EVENT_KEY_DOWN);
        return true;
    }
    default:
        return false;
    }
}

void ui_sdl_frame(float width, float height, float framebuffer_scale)
{
    ImGuiIO &io = ImGui::GetIO();
    unsigned long long now = SDL_GetTicksNS();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(framebuffer_scale, framebuffer_scale);
    io.DeltaTime = g_last_frame_ns ? (float)((now - g_last_frame_ns) / 1e9) : 1.0f / 60.0f;
    if (io.DeltaTime < 0.0001f) io.DeltaTime = 0.0001f;
    if (io.DeltaTime > 0.25f) io.DeltaTime = 0.25f;
    g_last_frame_ns = now;
}

#if !defined(DEFJAM_APP)
// ---- the overlay, drawn by the toolkit's Vulkan present --------------------------------------------------------------

#include "d3d8_vk.h"

namespace {

uint32_t g_vk_generation;
std::atomic<unsigned> g_frame_w(0), g_frame_h(0);       // what the overlay was last drawn into, in pixels

// Asked once a frame whether to draw: this is where the hotkey and the pad chord are looked for.
int overlay_active(void *)
{
    static bool configured;
    if (!configured) {
        configured = true;
        pc_ui_apply_settings();
    }
    ui_lock();
    bool toggled = g_toggle.exchange(false);
    pad_mark_poll();
    if (!ui_capturing() && !g_open.load()) song_pad_poll();
    if (!ui_capturing() && !g_chords.empty() && pad_toggle_pressed()) toggled = true;
    if (toggled) set_open(!g_open.load());
    bool open = g_open.load();
    ui_unlock();
    return open ? 1 : 0;
}

void overlay_init_backend(const D3D8VkOverlayFrame *f)
{
    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion = VK_API_VERSION_1_1;
    info.Instance = (VkInstance)f->instance;
    info.PhysicalDevice = (VkPhysicalDevice)f->physical_device;
    info.Device = (VkDevice)f->device;
    info.QueueFamily = f->queue_family;
    info.Queue = (VkQueue)f->queue;
    info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
    info.MinImageCount = 2;
    info.ImageCount = f->image_count < 2 ? 2 : f->image_count;
    info.PipelineInfoMain.RenderPass = (VkRenderPass)f->render_pass;
    ImGui_ImplVulkan_Init(&info);
    g_vk_generation = f->generation;
}

void overlay_draw(const D3D8VkOverlayFrame *f, void *)
{
    ui_lock();
    if (!g_open.load()) { ui_unlock(); return; }
    if (g_ready && f->generation != g_vk_generation) {
        // The window's image has another format: the pipelines were built for the old pass.
        ImGui_ImplVulkan_Shutdown();
        overlay_init_backend(f);
    }
    if (!g_ready) {
        ui_context_begin(nullptr);
        g_last_frame_ns = 0;
        overlay_init_backend(f);
        g_ready = true;
        fprintf(stderr, "[UI] overlay ready (Dear ImGui %s, Vulkan)\n", IMGUI_VERSION);
        fflush(stderr);
    }

    g_frame_w = f->width;
    g_frame_h = f->height;
    {
        float h = f->height / 960.0f;
        ui_set_extra_scale(h < 1.0f ? 1.0f : h > 2.4f ? 2.4f : h);
    }
    ui_apply_scale(nullptr);
    ui_feed_gamepad();
    ImGui_ImplVulkan_NewFrame();
    ui_sdl_frame((float)f->width, (float)f->height, 1.0f);
    ImGui::NewFrame();

    // Dim the game and put the settings in a panel in the middle (as the Direct3D overlay does).
    ImGuiIO &io = ImGui::GetIO();
    if (g_just_opened) {
        g_just_opened = false;
        ImGui::SetNextWindowFocus();
    }
    ImDrawList *bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
    float pw = io.DisplaySize.x * 0.82f, ph = io.DisplaySize.y * 0.86f;
    if (pw < 560) pw = io.DisplaySize.x;
    if (ph < 420) ph = io.DisplaySize.y;
    bool close_request = false;
    {
        ImGui::SetNextWindowBgAlpha(0.96f);
        UiResult r = ui_draw(UI_OVERLAY, (io.DisplaySize.x - pw) / 2, (io.DisplaySize.y - ph) / 2, pw, ph);
        close_request = r.leave;
    }
    // Escape or the pad's cancel button closes the overlay, unless a popup or a capture is open.
    if (!ui_capturing() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
        close_request = true;

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)f->command_buffer);
    if (close_request) set_open(false);
    ui_unlock();
}

} // namespace

// Offer an SDL event to the overlay, on the thread that reads the window's events. Returns non-zero
// when the overlay used it (its key, a key being captured for a binding) and it should go no further.
extern "C" int pc_ui_sdl_event(const void *event, float window_width, float window_height)
{
    const SDL_Event *e = (const SDL_Event *)event;
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && !g_open.load()) {
        int request = song_key_request(host_posix_vk_from_scancode(e->key.scancode));
        if (request) {
            ui_lock();
            bool capturing = ui_capturing();
            ui_unlock();
            if (!capturing) {
                song_cursor_request(request, "key");
                return 1;
            }
        }
    }
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && recorder_mark_vk() &&
        host_posix_vk_from_scancode(e->key.scancode) == recorder_mark_vk() &&
        host_posix_vk_from_scancode(e->key.scancode) != g_overlay_vk) {
        ui_lock();
        bool capturing = ui_capturing();
        ui_unlock();
        if (!capturing) {
            recorder_mark("key");
            return 1;
        }
    }
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat &&
        host_posix_vk_from_scancode(e->key.scancode) == g_overlay_vk) {
        ui_lock();
        bool capturing = ui_capturing();
        ui_unlock();
        if (!capturing) {
            g_toggle = true;
            return 1;
        }
    }
    if (!g_open.load()) return 0;
    ui_lock();
    int handled = 0;
    if (g_ready) {
        if (ui_capture_sdl_event(e)) {
            handled = 1;
        } else {
            // The pointer is in the window's points; the overlay is drawn in the pixels of the
            // image it was last drawn into, which need not be the window's own pixel size.
            float fw = (float)g_frame_w.load(), fh = (float)g_frame_h.load();
            float sx = window_width > 0 && fw > 0 ? fw / window_width : 1.0f;
            float sy = window_height > 0 && fh > 0 ? fh / window_height : 1.0f;
            ui_sdl_event(e, sx, sy);
        }
    }
    ui_unlock();
    return handled;
}

// Hand the overlay to the Vulkan present step. Call before the graphics device is made.
extern "C" void pc_ui_register_overlay(void)
{
    D3D8VkOverlay overlay = { overlay_active, overlay_draw, nullptr };
    d3d8_vk_set_overlay(&overlay);
}
#endif  // !DEFJAM_APP
#endif
