// The settings screen, shared by the launcher and the in-game overlay, and the overlay itself.
//
// One screen edits settings.ini through the settings library, so a change made here is the value
// everywhere (the file, the hotkeys, the other front end), and is saved as it is made. The overlay
// draws over the picture in the presentation step (toolkit d3d8_present_set_overlay) and takes the
// keyboard and mouse from the window; while it is open the game is polled at rest. The launcher
// (pc_launcher.cpp) is the same screen in a window of its own before the game starts.
//
// Threads: the window's thread delivers input events, the thread that presents draws. One lock keeps
// them out of ImGui at the same time. Nothing here touches ImGui while the overlay is closed except
// to look for its hotkeys.

#define _CRT_SECURE_NO_WARNINGS
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#else
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>
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
#include "pc_ui_model.h"
#include "pc_mods.h"
#include "pc_ui_theme.h"
#include "pc_ui_actions.h"
#include "pc_launcher_home.h"

#include "imgui.h"
#if defined(_WIN32)
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#else
#include "imgui_impl_vulkan.h"
#endif

extern "C" {
#include "recomp_settings.h"
#include "recorder.h"
#include "scene_launcher.h"
#include "dev_module.h"
#include "input_host.h"
#include "input_map.h"
}
#include "pc_input.h"
#include "pc_settings.h"
#include "pc_ui.h"
#include "pc_ui_internal.h"
#include "netplay.h"
#include "netplay_servers.h"
#include "account.h"
#include "bug_report.h"
#include "voice.h"
#include "online_screen.h"
#include "hd_build.h"
#include "updater.h"

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

static void commit()
{
    pcui::clear_action_status();
    pc_settings_save();
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
        {"display", "fullscreen", "Full screen"},
        {"display", "window_width", "Window width"},
        {"display", "window_height", "Window height"},
        {"display", "aspect", "Picture shape"},
        {"display", "render_scale", "Render scale"},
        {"display", "filter", "Scaling filter"},
        {"display", "vsync", "Vsync"},
        {"display", "gamma", "Game brightness curve"},
        {"display", "brightness", "Brightness"},
        {"display", "vignette", "Vignette"},
        {"display", "vignette_strength", "Vignette strength (%)"},
        {"display", "crowd_near", "Crowd near the camera"},
        {"display", "widescreen_hud", "Fight HUD position"},
        {"display", "widescreen_cutscenes", "Story cutscenes"},
        {"display", "widescreen_side_fill", "Beside 4:3 pictures"},
        {"display", "widescreen_fov_cap", "Widest field of view (degrees)"},
        {"display", "anti_aliasing", "Anti-aliasing"},
        {"display", "sharpen", "Sharpening"},
        {"display", "anisotropy", "Anisotropic filtering"},
        {"textures", "enabled", "Use HD texture packs"},
        {"textures", "packs", "Pack folders"},
        {"textures", "cache_mb", "Texture memory budget (MiB)"},
        {"textures", "dump", "Capture source textures"},
        {"textures", "dump_limit", "Maximum new captures"},
        {"input", "keyboard_player", "Keyboard and mouse play as"},
        {"input", "ports", "Controller ports"},
        {"input", "backend", "Pad backend"},
        {"input", "rumble", "Rumble strength"},
        {"input", "rumble_floor", "Weakest rumble pulse"},
        {"input", "rumble_min_ms", "Shortest rumble pulse (ms)"},
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
        {"ui", "scale", "Text and control size"},
        {"debug", "unlock_all", "Unlock all fighters and venues"},
        {"debug", "skip_intro", "Skip startup movies and legal screen"},
        {"ui", "overlay_pad", "Overlay pad buttons"},
        {"ui", "overlay_key", "Overlay key"},
        {"online", "server", "Server"},
        {"online", "name", "Your name"},
        {"online", "input_delay", "Input delay (steps)"},
        {"online", "rollback", "Rollback netcode (beta)"},
        {"online", "voice", "Voice chat"},
        {"online", "voice_key", "Push-to-talk key"},
        {"online", "voice_pad", "Push-to-talk on the controller"},
        {"online", "voice_volume", "Voices volume (%)"},
        {"online", "voice_mic_volume", "Microphone volume (%)"},
        {"online", "voice_auto_level", "Level my microphone"},
        {"debug", "record", "Record play-tests"},
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

#if defined(_WIN32)
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
#if defined(_WIN32)
    if (window) {
        UINT d = GetDpiForWindow(window);
        if (d) dpi *= d / 96.0f;
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

// The palette: near-black panels, Def Jam red for actions and selection, gold for highlights.
static const ImVec4 k_red(0.78f, 0.06f, 0.16f, 1.0f), k_red_hi(0.90f, 0.16f, 0.25f, 1.0f), k_red_lo(0.58f, 0.04f, 0.12f, 1.0f);
static const ImVec4 k_gold(0.96f, 0.72f, 0.20f, 1.0f);
static const ImVec4 k_bg(0.055f, 0.055f, 0.065f, 1.0f), k_panel(0.085f, 0.085f, 0.10f, 1.0f), k_sidebar(0.045f, 0.045f, 0.055f, 1.0f);
static ImFont *g_font_bold, *g_font_black;     // Segoe UI Bold / Black when present
static ImFont *g_font_display;                 // Anton (ffny-branding/fonts) for the launcher's headings and PLAY

void ui_apply_scale(UiWindow window)
{
    float s = display_scale(window);
    float &applied = g_scale_applied;
    ImGuiStyle &style = ImGui::GetStyle();
    if (s == applied) return;
    applied = s;
    style = ImGuiStyle();
    ImGui::StyleColorsDark();
    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.TabRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.SeparatorTextBorderSize = 2.0f;
    style.SeparatorTextPadding = ImVec2(0, 4);
    ImVec4 *c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.94f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.60f, 1.0f);
    c[ImGuiCol_WindowBg] = k_bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.17f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.23f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.23f, 0.28f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(k_red.x, k_red.y, k_red.z, 0.45f);
    c[ImGuiCol_HeaderHovered] = ImVec4(k_red.x, k_red.y, k_red.z, 0.70f);
    c[ImGuiCol_HeaderActive] = k_red;
    c[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.17f, 1.0f);
    c[ImGuiCol_TabHovered] = k_red_hi;
    c[ImGuiCol_TabSelected] = k_red;
    c[ImGuiCol_TabSelectedOverline] = k_gold;
    c[ImGuiCol_Button] = ImVec4(0.17f, 0.17f, 0.21f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);
    c[ImGuiCol_ButtonActive] = k_red;
    c[ImGuiCol_CheckMark] = k_gold;
    c[ImGuiCol_SliderGrab] = k_red_hi;
    c[ImGuiCol_SliderGrabActive] = k_gold;
    c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_NavCursor] = k_gold;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    style.ScaleAllSizes(s);
    style.FontScaleMain = s;
}

// Anton (SIL Open Font License, ffny-branding/fonts/ in the source tree, beside branding/) for the
// launcher's headings and PLAY; without it they use the heaviest system cut.
static ImFont *load_display_font(ImGuiIO &io)
{
    static const char relative[] = "ffny-branding/fonts/Anton-Regular.ttf";
    char path[1100];
    snprintf(path, sizeof(path), "%s", relative);
#if defined(_WIN32)
    char dir[MAX_PATH];
    bool found = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    DWORD n = found ? 0 : GetModuleFileNameA(nullptr, dir, MAX_PATH);
    for (int up = 0; !found && n && n < MAX_PATH && up <= 4; up++) {
        char *slash = strrchr(dir, '\\');
        if (!slash) break;
        *slash = 0;
        snprintf(path, sizeof(path), "%s\\%s", dir, relative);
        found = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    }
#else
    struct stat st;
    bool found = stat(path, &st) == 0;
    const char *base = found ? nullptr : SDL_GetBasePath();
    for (int up = 0; !found && base && up <= 4; up++) {
        std::string dir = base;
        for (int i = 0; i < up; i++) dir += "../";
        snprintf(path, sizeof(path), "%s%s", dir.c_str(), relative);
        found = stat(path, &st) == 0;
    }
#endif
    return found ? io.Fonts->AddFontFromFileTTF(path, 17.0f) : nullptr;
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
    char windows[MAX_PATH];
    g_font_bold = g_font_black = nullptr;
    if (GetWindowsDirectoryA(windows, sizeof(windows))) {
        // The first font added is the default; the heavier cuts are for headings and the Play button.
        static const char *const files[] = { "segoeui.ttf", "segoeuib.ttf", "seguibl.ttf" };
        ImFont **slots[] = { nullptr, &g_font_bold, &g_font_black };
        for (int i = 0; i < 3; i++) {
            char font[MAX_PATH + 32];
            snprintf(font, sizeof(font), "%s\\Fonts\\%s", windows, files[i]);
            if (GetFileAttributesA(font) == INVALID_FILE_ATTRIBUTES) continue;
            ImFont *f = io.Fonts->AddFontFromFileTTF(font, 17.0f);
            if (slots[i]) *slots[i] = f;
        }
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
    if (io.Fonts->Fonts.Size == 0) io.Fonts->AddFontDefault();     // the first font stays the default
    g_font_display = load_display_font(io);
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
        if (stacked && !strcmp(s->key, "render_scale"))
        {
            char current[24];
            snprintf(current, sizeof current, "%dx%s", v, v == 2 ? " (default)" : "");
            if (ImGui::BeginCombo(control, current))
            {
                for (int i = 1; i <= 4; i++)
                {
                    char item[24];
                    snprintf(item, sizeof item, "%dx%s", i, i == 2 ? " (default)" : "");
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
    {
        // The stored names (push_to_talk) read with spaces; a few read as their own words.
        auto shown = [s](const char *choice) {
            if (!strcmp(s->key, "crowd_near"))
                return std::string(!strcmp(choice, "fade") ? "Fade (original)" : "Hide");
            std::string text = choice;
            std::replace(text.begin(), text.end(), '_', ' ');
            return text;
        };
        if (ImGui::BeginCombo(control, shown(s->choices[value]).c_str()))
        {
            for (int i = 0; s->choices[i]; i++)
                if (ImGui::Selectable(shown(s->choices[i]).c_str(), i == value))
                    changed = recomp_settings_set(s->section, s->key, i) > 0;
            ImGui::EndCombo();
        }
        break;
    }
    case RECOMP_SETTING_STRING: {
        char buf[RECOMP_SETTING_TEXT_MAX];
        snprintf(buf, sizeof buf, "%s", locked ? s->value_text : s->stored_text);
        if (!stacked)
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
        if (ImGui::InputText(control, buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue))
            changed = recomp_settings_set_text(s->section, s->key, buf) > 0;
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

// ---- window size ----------------------------------------------------------------------------------------

struct WindowSize { int w, h; bool full = false; };    // full: the display's own size, as full screen

// The window sizes worth offering: the display's own modes and the console picture's 4:3 multiples
// (640x480, 1280x960, ...), largest first, each once, and only those whose window -- frame and title
// bar included -- fits the desktop, since a larger one is cut down to the work area anyway (make_window
// in d3d11_translator.c). Off Windows they come from SDL in points, the unit the window size is in there.
static std::vector<WindowSize> window_sizes()
{
    std::vector<WindowSize> out;
    int fit_w = 0, fit_h = 0;      // the largest client area that fits; 0: unknown, nothing filtered
    int native_w = 0, native_h = 0; // the display's current mode
    auto add = [&out](int w, int h) {
        if (w < 640 || h < 480) return;
        for (const WindowSize &s : out)
            if (s.w == w && s.h == h) return;
        out.push_back({ w, h });
    };
#if defined(_WIN32)
    for (DWORD i = 0;; i++) {
        DEVMODEW mode = {};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(nullptr, i, &mode)) break;
        add((int)mode.dmPelsWidth, (int)mode.dmPelsHeight);
    }
    {
        DEVMODEW mode = {};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
            native_w = (int)mode.dmPelsWidth;
            native_h = (int)mode.dmPelsHeight;
        }
    }
    RECT work, frame = { 0, 0, 0, 0 };
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) && AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE)) {
        fit_w = (work.right - work.left) - (frame.right - frame.left);
        fit_h = (work.bottom - work.top) - (frame.bottom - frame.top);
    }
#else
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    int count = 0;
    if (SDL_DisplayMode **modes = display ? SDL_GetFullscreenDisplayModes(display, &count) : nullptr) {
        for (int i = 0; i < count; i++) add(modes[i]->w, modes[i]->h);
        SDL_free(modes);
    }
    if (const SDL_DisplayMode *mode = display ? SDL_GetCurrentDisplayMode(display) : nullptr) {
        native_w = mode->w;
        native_h = mode->h;
    }
    SDL_Rect usable;
    if (display && SDL_GetDisplayUsableBounds(display, &usable)) {
        fit_w = usable.w;
        fit_h = usable.h - 32;      // SDL does not say how tall the title bar is; leave room for a typical one
    }
#endif
    for (int k = 1; k <= 8; k++) add(640 * k, 480 * k);
    if (fit_w > 0 && fit_h > 0)
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [=](const WindowSize &s) { return s.w > fit_w || s.h > fit_h; }), out.end());
    std::sort(out.begin(), out.end(),
              [](const WindowSize &a, const WindowSize &b) { return a.w != b.w ? a.w > b.w : a.h > b.h; });
    // The display's own size does not fit as a window (title bar, taskbar); it is offered first,
    // as full screen, which fills exactly that.
    if (native_w >= 640 && native_h >= 480) {
        WindowSize full = { native_w, native_h, true };
        out.insert(out.begin(), full);
    }
    return out;
}

static std::string size_label(int w, int h)
{
    char buf[64];
    int a = w, b = h;
    while (b) { int t = a % b; a = b; b = t; }                       // greatest common divisor
    int rw = a ? w / a : 0, rh = a ? h / a : 0;
    if (rw == 8 && rh == 5) { rw = 16; rh = 10; }                  // say 16:10, as monitors are sold
    if (rw > 0 && rh > 0 && rw <= 64 && rh <= 64)
        snprintf(buf, sizeof(buf), "%d x %d  (%d:%d)", w, h, rw, rh);
    else
        snprintf(buf, sizeof(buf), "%d x %d", w, h);
    return buf;
}

// display.window_width and window_height as one choice of the sizes above. A size set by hand or by
// dragging the window that is not in the list is shown as it is, marked as custom, and kept until
// another is chosen. With no list (no display information) the two numbers are edited as before.
static void window_size_widget()
{
    RecompSetting *ws = find("display", "window_width"), *hs = find("display", "window_height");
    RecompSetting *fs = find("display", "fullscreen");
    if (!ws || !hs) return;
    static std::vector<WindowSize> sizes;
    static UiTick listed;
    if (sizes.empty() || ui_ticks() - listed > 5000) {           // a display change is picked up soon enough
        sizes = window_sizes();
        listed = ui_ticks();
    }
    if (sizes.empty()) {
        setting_widget(ws);
        setting_widget(hs);
        return;
    }
    bool locked = ws->source == RECOMP_SETTING_FROM_ENV || hs->source == RECOMP_SETTING_FROM_ENV;
    bool fullscreen = fs && fs->stored != 0;
    int w = ws->stored, h = hs->stored;
    bool listed_now = false;
    const WindowSize *full_entry = nullptr;
    for (const WindowSize &s : sizes) {
        if (s.full) full_entry = &s;
        else if (s.w == w && s.h == h) listed_now = true;
    }
    std::string current = fullscreen && full_entry ? size_label(full_entry->w, full_entry->h) + "  full screen"
                                                   : size_label(w, h) + (listed_now ? "" : "  custom");
    ImGui::PushID("window_size");
    if (locked) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    if (ImGui::BeginCombo("Window size", current.c_str())) {
        for (const WindowSize &s : sizes) {
            bool selected = s.full ? fullscreen : !fullscreen && s.w == w && s.h == h;
            std::string label = size_label(s.w, s.h) + (s.full ? "  full screen" : "");
            if (ImGui::Selectable(label.c_str(), selected) && !selected) {
                if (s.full) {
                    recomp_settings_set("display", "fullscreen", 1);
                } else {
                    recomp_settings_set("display", "fullscreen", 0);
                    recomp_settings_set("display", "window_width", s.w);
                    recomp_settings_set("display", "window_height", s.h);
                }
                commit();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (locked) ImGui::EndDisabled();
    if (locked)
        ImGui::SetItemTooltip("Set by an environment variable for this run.");
    else
        ImGui::SetItemTooltip("The display's own size as full screen, then the sizes that fit on the desktop as a "
                              "window. Dragging the window's edge sets any other size.");
    ImGui::PopID();
}

// ---- the pages ----------------------------------------------------------------------------------------------

static void page_display()
{
    ImGui::SeparatorText("Window and picture");
    settings_group("display", { "fullscreen", "window_width", "window_height", "aspect" });
    if (recomp_settings_get("display", "aspect", 0) >= 2) {
        /* A widescreen shape: its own options, under it. */
        ImGui::Indent();
        settings_group("display", { "widescreen_hud", "widescreen_cutscenes", "widescreen_side_fill",
                                    "widescreen_fov_cap" });
        ImGui::TextWrapped("Widescreen (beta): the 3D view gets wider; menus, the HUD and movies keep their shape. "
                           "widescreen (auto) matches your screen (16:9 or 16:10). It starts with the game: choose it "
                           "here before Play, or restart after changing it in the overlay.");
        ImGui::Unindent();
    }
    settings_group("display", { "filter", "vsync" });
    ImGui::SeparatorText("Quality");
    settings_group("display", { "render_scale", "anisotropy", "anti_aliasing", "sharpen" });
    ImGui::SeparatorText("Picture");
    settings_group("display", { "brightness", "gamma", "vignette" });
    if (recomp_settings_get("display", "vignette", 1)) {
        ImGui::Indent();
        settings_group("display", { "vignette_strength" });
        ImGui::Unindent();
    }
    settings_group("display", { "crowd_near" });
    ImGui::TextWrapped("Full screen, window size, filter, vsync, anti-aliasing, sharpening, anisotropic filtering, "
                       "the 4:3 and stretch shapes, brightness and the vignette apply as you change them. The widescreen shapes, render scale and the brightness curve apply the "
                       "next time the game starts. F11 or Alt+Enter switches full screen while playing.");
}

// Make the HD packs here when setup did not: the same local upscale, with a bar, and turn them on.
static void hd_build_section()
{
    int done = 0, total = 0;
    char text[256], packs[256];
    static bool applied;
    HdBuildState state = hd_build_status(&done, &total, text, sizeof(text), packs, sizeof(packs));
    ImGui::SeparatorText("Make HD textures");
    // Players reported white screens and wrong pictures with the packs on: say so, and offer the way back.
    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.3f, 1),
                       "Experimental: HD textures can cause a white screen, white boxes or wrong pictures.");
    if (recomp_settings_get("textures", "enabled", 0) && state != HD_BUILD_RUNNING) {
        ImGui::TextWrapped("If you see that, turn them off here and restart the game. Your packs stay on disk.");
        if (ImGui::Button("Turn HD textures off", ImVec2(ImGui::GetFontSize() * 12, 0))) {
            recomp_settings_set("textures", "enabled", 0);
            commit();
        }
        ImGui::Spacing();
    }
    if (state == HD_BUILD_DONE && !applied) {
        // Switch the new packs on, once, from this thread (the settings belong to the screen).
        recomp_settings_set_text("textures", "packs", packs);
        recomp_settings_set("textures", "enabled", 1);
        commit();
        applied = true;
    }
    if (state == HD_BUILD_RUNNING) {
        float f = total > 0 ? (float)done / (float)total : 0.0f;
        char overlay[64];
        if (total > 0) snprintf(overlay, sizeof(overlay), "%d / %d", done, total);
        else snprintf(overlay, sizeof(overlay), "preparing...");
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, k_red);
        ImGui::ProgressBar(total > 0 ? f : -1.0f * (float)ImGui::GetTime(), ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", text);
        if (ImGui::Button("Stop")) hd_build_cancel();
        ImGui::SameLine();
        ImGui::TextDisabled("Play is available again when it has finished.");
        return;
    }
    ImGui::TextWrapped("Upscales every texture of your own game four times, on this PC, with no download and no GPU. "
                       "It takes about 5 to 20 minutes and about 6 GB (15 GB free space while it works).");
    double gib = hd_build_free_gib();
    if (gib >= 0) ImGui::TextDisabled("Free space on the data drive: %.0f GB", gib);
    if (state == HD_BUILD_DONE || state == HD_BUILD_FAILED)
        ImGui::TextColored(state == HD_BUILD_DONE ? ImVec4(0.45f, 0.85f, 0.45f, 1) : ImVec4(1.0f, 0.45f, 0.4f, 1), "%s", text);
    ImGui::PushStyleColor(ImGuiCol_Button, k_red);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
    if (ImGui::Button(state == HD_BUILD_DONE ? "Make them again" : "Make HD textures", ImVec2(ImGui::GetFontSize() * 12, 0))) {
        applied = false;
        hd_build_start();
    }
    ImGui::PopStyleColor(2);
}

static std::vector<pcui::Pack> g_packs;
static std::vector<pcui::ModGroup> g_mod_groups;
static std::string g_pack_key, g_pack_path, g_pack_status;
static std::map<std::string,std::string> g_pack_conflicts;
static void refresh_packs(bool force = false)
{
    std::string key = get_text("textures", "packs"), path = pcui::folder_path(pcui::Folder::Mods);
    if (force || key != g_pack_key || path != g_pack_path)
    {
        g_pack_key = key;
        g_pack_path = path;
        g_packs = pcui::inventory(std::filesystem::u8path(path), key);
        g_mod_groups = pcui::mod_groups(std::filesystem::u8path(path),g_packs);
        g_pack_conflicts = pcui::pack_conflicts(std::filesystem::u8path(path),g_packs);
    }
}
static bool packs_available()
{
    refresh_packs();
    for (auto &p : g_packs)
        if (p.selected && p.problem.empty())
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
    recomp_settings_set_text("textures", "packs", text.c_str());
    commit();
    g_pack_key = text;
    g_pack_conflicts = pcui::pack_conflicts(std::filesystem::u8path(g_pack_path),g_packs);
    g_pack_status = "Saved. Higher priority overrides lower priority; applies at next launch.";
}
// Upstream's Mods page (installed packs, load order, the signed project catalogue through the mod
// helper) is beta and not wired into FFNY Online's launcher and updater yet: the Textures page below
// stays as it was, and RECOMP_MODS_PAGE=1 shows the Mods page in its place (tests).
static bool mods_page_enabled()
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_MODS_PAGE");
        on = e && *e == '1';
    }
    return on != 0;
}

static void page_mods();

static void page_textures()
{
    if (mods_page_enabled()) {
        page_mods();
        return;
    }
    hd_build_section();
    ImGui::SeparatorText("HD texture packs");
    settings_group("textures", { "enabled", "packs", "cache_mb" });
    ImGui::TextWrapped("Install packs under mods in the data folder. Enter folder names separated by semicolons; "
                       "the last pack wins when more than one replaces the same image. Changes apply after a restart.");
    ImGui::SeparatorText("Making a pack");
    settings_group("textures", { "dump", "dump_limit" });
    ImGui::TextWrapped("Captures are saved under hd-work/runtime in the data folder. Turn capture off when finished.");
}

static void page_mods()
{
    const bool launcher = g_mode == UI_LAUNCHER;
    static bool requested = false;
    if (launcher && !requested) { pcui::mod_start("status"); requested = true; }
    refresh_packs();
    bool locked = find("textures", "packs")->source == RECOMP_SETTING_FROM_ENV;
    ImGui::SeparatorText("Installed mods");
    if (pcui::mod_recovery_needed()) {
        ImGui::TextWrapped("An interrupted installation needs recovery before Play. If recovery fails, files are preserved in the data folder under mod-manager; see the status below.");
        ImGui::BeginDisabled(!launcher || pcui::mod_busy());
        if (ImGui::Button("Retry recovery")) pcui::mod_start("recover");
        ui_target("mods.recover");
        ImGui::EndDisabled();
    }
    settings_group("textures", {"enabled"});
    if (locked) ImGui::TextWrapped("Pack selection is set for this run by RECOMP_TEXTURE_PACKS.");
    if (ImGui::Button("Refresh local mods")) {
        refresh_packs(true);
        if (launcher) pcui::mod_start("status");
    }
    ui_target("mods.local-refresh");
    ImGui::SameLine();
    if (ImGui::Button("Open Mods Folder")) pcui::open_path(g_pack_path);
    if (ImGui::Button("Load Order...")) ImGui::OpenPopup("Load Order");
    ui_target("mods.order");
    ImGui::TextWrapped("Enable installed mods below. Load Order sets which mod wins when textures overlap.");
    const auto &managed = pcui::mod_view().installed;
    auto display_name = [&](const pcui::ModGroup &group) {
        auto found=managed.find(g_packs[group.members.front()].name);
        return found==managed.end() ? group.name : found->second.name;
    };
    const float scale=ImGui::GetStyle().FontScaleMain;
    const float available=ImGui::GetContentRegionAvail().x;
    ImGui::BeginDisabled(locked || pcui::mod_busy());
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
            ImGui::BeginDisabled(!valid && !selected);
            if(ImGui::Checkbox("##enabled",&checked)) { pcui::select_mod(g_packs,group,checked); save_packs(); }
            ui_target(("pack."+group.id).c_str());
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s",display_name(group).c_str());
            ImGui::TextDisabled(group.builtin ? "Built-in / generated locally" : found==managed.end() ? "Local mod" : "Project catalogue");
            if(group.builtin && ImGui::TreeNode("Components and details")) {
                ImGui::TextWrapped("Generated from your own dump. Transparency textures preserve menu edges; material-channel textures preserve surface data. Both belong to Recomp HD.");
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
                if(ImGui::SmallButton("Remove download")) pcui::mod_start("remove",pack_id);
                ui_target(("mod.remove."+pack_id).c_str());
                ImGui::EndDisabled();
                if(p.selected) ImGui::TextWrapped("Disable before removing its download.");
            }
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s",!valid?"Needs attention":selected && !checked?"Partly enabled":"Installed");
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
            ImGui::BeginDisabled(locked || pcui::mod_busy());
            if(ImGui::Checkbox(p.name.c_str(),&selected)) { p.selected=selected; save_packs(); }
            ImGui::EndDisabled();
            if(p.name=="faithful-hd") ImGui::TextWrapped("Legacy default name. The generated Recomp HD components appear together in the table above when installed.");
            ImGui::PopID();
        }
    }
    if(g_packs.empty()) ImGui::TextWrapped("No local packs found. Setup can generate Recomp HD textures from your own dump.");
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
        for(const auto &p:g_packs) if(p.selected) ++priority;
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
    ImGui::SeparatorText("Project catalogue");
    if (!launcher) ImGui::TextWrapped("Return to the launcher to download, update or remove mods. Current packs stay in use during this game.");
    ImGui::BeginDisabled(!launcher || pcui::mod_busy());
    bool testing_versions = pcui::mod_prerelease();
    if (ImGui::Checkbox("Show testing versions (release candidates)", &testing_versions)) {
        pcui::mod_set_prerelease(testing_versions); pcui::mod_start("status");
    }
    ui_target("mods.testing-versions");
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
        if (ImGui::Button(mod.installed.empty() ? "Download and install" : "Update available")) pcui::mod_start("install",mod.id);
        ui_target(("mod.install."+mod.id).c_str());
        ImGui::EndDisabled(); ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Memory and manual pack list")) settings_group("textures", {"cache_mb", "packs"});
}

void ui_home_fields()
{
    bool two = ImGui::GetContentRegionAvail().x > 390 * ImGui::GetStyle().FontScaleMain;
    auto begin = [](const char *id) {
        ImGui::TableNextColumn();
        ImGui::BeginChild(id, ImVec2(0, 80 * ImGui::GetStyle().FontScaleMain), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    };
    if (ImGui::BeginTable("quick fields", two ? 2 : 1, ImGuiTableFlags_SizingStretchSame))
    {
        begin("mode");
        setting_widget(find("display", "fullscreen"), "Display mode", true);
        ImGui::EndChild();
        begin("size");
        ImGui::TextDisabled("Window size");
        int w = pc_display("window_width", 1280), h = pc_display("window_height", 960);
        char label[48];
        snprintf(label, sizeof label, "%d x %d", w, h);
        bool locked = find("display", "window_width")->source == RECOMP_SETTING_FROM_ENV ||
                      find("display", "window_height")->source == RECOMP_SETTING_FROM_ENV;
        ImGui::BeginDisabled(pc_display("fullscreen", 0) != 0 || locked);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##window size", label))
        {
            const int sizes[][2] = {{1280, 960}, {1280, 720}, {1280, 800}, {1920, 1080}, {2560, 1440}};
            bool custom = true;
            for (auto &size : sizes)
            {
                char name[48];
                snprintf(name, sizeof name, "%d x %d", size[0], size[1]);
                bool selected = w == size[0] && h == size[1];
                custom &= !selected;
                if (ImGui::Selectable(name, selected))
                {
                    pc_display_set("window_width", size[0]);
                    pc_display_set("window_height", size[1]);
                }
            }
            if (custom)
                ImGui::TextDisabled("Custom: %s (kept)", label);
            ImGui::EndCombo();
        }
        ui_target("display.window_size");
        ImGui::EndDisabled();
        ImGui::EndChild();
        begin("aspect");
        setting_widget(find("display", "aspect"), "Picture shape", true);
        ImGui::EndChild();
        begin("render_scale");
        setting_widget(find("display", "render_scale"), "Render scale", true);
        ImGui::EndChild();
        begin("vsync");
        setting_widget(find("display", "vsync"), "VSync", true);
        ImGui::EndChild();
        begin("textures");
        bool available = packs_available();
        ImGui::BeginDisabled(!available && !recomp_settings_get("textures", "enabled", 0));
        setting_widget(find("textures", "enabled"), available ? "HD textures" : "HD textures (not installed)",
                       true);
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::EndTable();
    }
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
    settings_group("input", { "keyboard_player", "ports", "backend" });
    ImGui::SeparatorText("Sticks and triggers");
    settings_group("gamepad", { "left_deadzone", "right_deadzone", "trigger_threshold", "button_threshold",
                                "left_stick", "right_stick" });
    ImGui::SeparatorText("Rumble");
    settings_group("input", { "rumble", "rumble_floor", "rumble_min_ms", "rumble_on_connect" });
    ImGui::SeparatorText("Pointer");
    settings_group("input", { "hide_cursor", "confine_cursor" });
}

static void page_pad_buttons()
{
    ImGui::TextWrapped("Which physical button acts as each Xbox control. Choose from the list, or press Detect and then the "
                       "button on your pad. Names are positional: south is the bottom face button (Xbox A, PlayStation cross), "
                       "east the right one, west the left, north the top.");
    if (ImGui::Button("Reset all to default")) {
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

// The optional FFNY Online account: sign-in with Discord (src/hooks/account.c).
static void page_account()
{
    account_init();             // the launcher shows before main() loads the saved session
    account_refresh();
    enum AccountState state = account_state();
    char text[256];
    account_text(text, sizeof(text));
    float w = ImGui::GetFontSize() * 11;
    if (state == ACCOUNT_IN) {
        ImGui::TextColored(k_gold, "Signed in with Discord: %s", text);
        if (ImGui::Button("Sign out", ImVec2(w * 0.7f, 0))) account_sign_out();
        ImGui::SameLine();
        if (ImGui::Button("Delete account...", ImVec2(w, 0))) ImGui::OpenPopup("Delete account");
        if (ImGui::BeginPopupModal("Delete account", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Delete your FFNY Online account and everything stored with it?");
            if (ImGui::Button("Delete", ImVec2(w * 0.7f, 0))) { account_delete(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Keep it", ImVec2(w * 0.7f, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    } else if (state == ACCOUNT_WAITING) {
        ImGui::TextColored(k_gold, "Finish signing in with Discord in your browser...");
        if (ImGui::Button("Cancel", ImVec2(w * 0.7f, 0))) account_cancel();
    } else if (state == ACCOUNT_CHECKING) {
        ImGui::TextDisabled("Checking your account...");
    } else {
#if defined(_WIN32)
        if (ImGui::Button("Sign in with Discord", ImVec2(w, 0))) account_sign_in();
        ImGui::SameLine();
        ImGui::TextDisabled("Optional: online you appear with your Discord name.");
#else
        ImGui::TextDisabled("Signing in with Discord comes to this system with online play.");
#endif
        if (text[0]) ImGui::TextColored(state == ACCOUNT_FAILED ? ImVec4(1.0f, 0.45f, 0.4f, 1) : ImVec4(0.6f, 0.6f, 0.64f, 1),
                                        "%s", text);
    }
#if defined(_WIN32)
    if (ImGui::SmallButton("Privacy")) ShellExecuteA(nullptr, "open", account_privacy_url(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

// Voice chat's settings, the microphone's level and what to know about it: the overlay's Online page
// and the launcher's own Voice chat page (the launcher signs in on its start page, and the server and
// the name are not for players to change there).
static void voice_settings()
{
    settings_group("online", { "voice", "voice_key", "voice_pad", "voice_volume", "voice_mic_volume", "voice_auto_level" });
    {
        float meter = voice_mic_meter();
        if (meter >= 0.0f) {
            ImGui::ProgressBar(meter, ImVec2(ImGui::GetFontSize() * 16, 0), "your microphone");
            ImGui::SameLine();
            ImGui::TextDisabled(meter > 0.95f ? "too loud: lower your microphone volume" : "");
        } else {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("The level of your microphone shows here while you are in a room and it is open "
                                "(after your first push-to-talk press).");
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Everyone in your room hears you, in the room and in the fight. Push-to-talk: hold the key "
                        "above while the game window has the focus. Your voice goes live through the project's server "
                        "and is not recorded.");
    ImGui::PopTextWrapPos();
}

// Online: the connection settings. The hub itself (quick match, rooms, the venue vote) is in the
// game, behind the main menu's ONLINE entry (src/hooks/online_screen.cpp).
static void page_online(UiMode mode)
{
    enum NetplayState state = netplay_state();
    char status[256];
    netplay_status(status, sizeof(status));

    ImGui::SeparatorText("Account");
    page_account();
    ImGui::SeparatorText("Connection");
    settings_group("online", { "server", "name" });
    {
        char shown[32];
        if (account_game_name(shown, sizeof(shown)))
            ImGui::TextDisabled("Online you appear as %s, from your Discord account.", shown);
    }
    ImGui::Spacing();
    ImGui::PushFont(g_font_bold, ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextColored(k_gold, "Online play is in the game: choose ONLINE in the main menu.");
    ImGui::PopFont();
    ImGui::TextWrapped("Quick match puts you straight into a room; or pick a room from the list, or open your own with "
                       "the mode, the players and the venues to vote on. In the room you choose your fighter from the "
                       "roster and ready up; then the room votes on the venue and the match starts by itself.%s",
                       mode == UI_LAUNCHER ? " Press PLAY to start the game." : "");
    if (state != NETPLAY_OFF) {
        ImGui::Spacing();
        ImVec4 tone = state == NETPLAY_FAILED ? ImVec4(1.0f, 0.45f, 0.4f, 1) : ImVec4(0.6f, 0.6f, 0.64f, 1);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(tone, "%s", status);
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Go offline", ImVec2(ImGui::GetFontSize() * 7, 0))) netplay_stop();
    }
    ImGui::SeparatorText("Voice chat");
    voice_settings();
    ImGui::SeparatorText("How it works");
    ImGui::TextWrapped("Everyone needs their own copy of the game and this same version. In a fight the games run in "
                       "lockstep through the relay server, so it is the same fight on every screen.");
}

// An update in progress, as the launcher's GAME card and its "Update available" window show it:
// "Step 6 of 8 · Translating the game code", the setup's own line of what it is doing (or the MB
// downloaded), the percentage and the time so far. The steps come from updater_stage (1 is the
// download, then setup/engine.py STAGE_NAMES); with a setup that reports none, "Installing build N".
struct UpdateProgress {
    float fraction = 0;
    int percent = 0;
    char counter[32] = "";      // "Step 6 of 8", or "" when the setup reports no steps
    char name[96] = "";         // "Translating the game code" (or "Installing build 32")
    char name_short[96] = "";   // the name without a "(...)" remark, for narrow places
    char step[128] = "";        // "Step 6 of 8 · Translating the game code"
    char step_short[128] = "";  // the same with the short name
    char detail[256] = "";
    char elapsed[32] = "";      // "2:41 so far"
};

static UpdateProgress update_progress(UpdaterState state, int build)
{
    UpdateProgress p;
    int index = 0, total = 0;
    p.fraction = (float)updater_progress();
    p.percent = (int)(p.fraction * 100.0f + 0.5f);
    if (updater_stage(&index, &total, p.name, sizeof(p.name)) && total >= index)
        snprintf(p.counter, sizeof(p.counter), "Step %d of %d", index, total);
    else
        snprintf(p.name, sizeof(p.name), "%s build %d", state == UPDATER_DOWNLOADING ? "Downloading" : "Installing", build);
    snprintf(p.name_short, sizeof(p.name_short), "%s", p.name);
    if (char *remark = strstr(p.name_short, " (")) *remark = 0;
    snprintf(p.step, sizeof(p.step), "%s%s%s", p.counter, p.counter[0] ? "  \xC2\xB7  " : "", p.name);
    snprintf(p.step_short, sizeof(p.step_short), "%s%s%s", p.counter, p.counter[0] ? "  \xC2\xB7  " : "", p.name_short);
    unsigned long long got = 0, size = 0;
    if (state == UPDATER_DOWNLOADING && updater_download(&got, &size) && size)
        snprintf(p.detail, sizeof(p.detail), "%.1f MB of %.1f MB", got / 1048576.0, size / 1048576.0);
    else
        updater_message(p.detail, sizeof(p.detail));
    for (char *c = p.detail; *c; c++)
        if (*c == '\n' || *c == '\r' || *c == '\t') *c = ' ';
    int secs = updater_elapsed();
    if (secs >= 3600)
        snprintf(p.elapsed, sizeof(p.elapsed), "%d:%02d:%02d so far", secs / 3600, secs / 60 % 60, secs % 60);
    else if (secs > 0)
        snprintf(p.elapsed, sizeof(p.elapsed), "%d:%02d so far", secs / 60, secs % 60);
    return p;
}

// text cut to one line of at most max_w, ending in "..." when it had to be cut (never inside a
// UTF-8 sequence).
static void fit_line(ImFont *font, float size, const char *text, float max_w, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", text);
    if (font->CalcTextSizeA(size, FLT_MAX, 0, out).x <= max_w) return;
    size_t n = strlen(out);
    while (n > 0) {
        do n--; while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80);
        while (n > 0 && out[n - 1] == ' ') n--;
        if (n + 4 > out_size) continue;
        memcpy(out + n, "...", 4);
        if (font->CalcTextSizeA(size, FLT_MAX, 0, out).x <= max_w) return;
    }
    snprintf(out, out_size, "...");
}

// A step name for a place max_w wide: whole, then without its remark, then cut.
static void fit_step(ImFont *font, float size, const char *whole, const char *shorter, float max_w, char *out,
                     size_t out_size)
{
    if (font->CalcTextSizeA(size, FLT_MAX, 0, whole).x <= max_w) snprintf(out, out_size, "%s", whole);
    else fit_line(font, size, shorter, max_w, out, out_size);
}

static void page_general(UiMode mode)
{
    (void)mode;
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
    ImGui::SeparatorText("Appearance");
    settings_group("ui", { "scale" });
    ImGui::SeparatorText("Bug reports");
    ImGui::TextWrapped("Something wrong in the game? Press F9 while it is on screen: the game takes a picture and sends "
                       "it with its log to the developers, and shows a code you can quote on Discord. After a crash the "
                       "launcher offers to send the crash log. Your Windows user folder is blanked in the log.");
    ImGui::SeparatorText("Startup movies");
    settings_group("debug", {"skip_intro"});
    ImGui::TextWrapped("Start at Press Start. Story cutscenes still play. Applies on the next launch.");
}
static void page_maintenance(UiMode mode)
{
    const char *names[] = {"Data", "Saves", "Mods", "Installation", "Settings file", "Play logs"};
    pcui::Folder paths[] = {pcui::Folder::Data,    pcui::Folder::Saves,    pcui::Folder::Mods,
                            pcui::Folder::Install, pcui::Folder::Settings, pcui::Folder::Logs};
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
    ImGui::SeparatorText("Update / Repair");
    if (mode == UI_OVERLAY)
        ImGui::TextWrapped("Close the game and open Update / Repair in the launcher to continue in Setup.");
    else
    {
        if (ImGui::Button("Choose Setup application"))
            pcui::setup_choose();
    }
    ImGui::TextWrapped("Setup: %s", pcui::setup_path().empty() ? "not selected" : pcui::setup_path().c_str());
    ImGui::TextWrapped("Use Update / Repair on the launcher's Home screen to open the wizard and close the "
                       "game. Setup verifies releases and builds your dump locally.");
    ImGui::SeparatorText("Files");
    ImGui::TextWrapped("Settings are saved as you change them in: %s", pc_settings_path());
    if (ImGui::Button(k_show_file_label)) open_folder();
    ImGui::SameLine();
    static bool confirm_reset;
    if (ImGui::Button("Reset every setting to default")) confirm_reset = true;
    if (confirm_reset) ImGui::OpenPopup("Reset every setting?");
    if (ImGui::BeginPopupModal("Reset every setting?", &confirm_reset, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Display, controller, key and launcher settings all go back to their defaults.");
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
    ImGui::SeparatorText("Updates");
    {
        char message[512];
        UpdaterState state = updater_state();
        updater_message(message, sizeof(message));
        ImGui::Text("This is build %d.", updater_local_build());
        if (state == UPDATER_DOWNLOADING || state == UPDATER_INSTALLING) {
            int build = 0;
            updater_info(&build, nullptr, 0, nullptr, 0);
            UpdateProgress p = update_progress(state, build);
            ImGui::SameLine();
            ImGui::TextDisabled("%s  %d%%%s%s", p.step, p.percent, p.elapsed[0] ? "  \xC2\xB7  " : "", p.elapsed);
        } else if (message[0]) { ImGui::SameLine(); ImGui::TextDisabled("%s", message); }
        if (state != UPDATER_CHECKING && state != UPDATER_DOWNLOADING && state != UPDATER_INSTALLING
                && state != UPDATER_STARTED) {
            if (ImGui::Button("Check for updates")) updater_check();
            if (state == UPDATER_AVAILABLE && mode == UI_LAUNCHER) {
                ImGui::SameLine();
                if (ImGui::Button("Install the update")) updater_install();
            }
        }
    }
    ImGui::SeparatorText("About");
    ImGui::Text("Def Jam: Fight for NY - Recompiled, version %s", DEFJAM_VERSION);
    ImGui::TextDisabled("Dear ImGui %s (MIT)  -  SDL3 (zlib)", IMGUI_VERSION);
}

static void page_debug(UiMode mode)
{
    ImGui::SeparatorText("Developer tools");
    settings_group("debug", {"unlock_all"});
    ImGui::TextWrapped("For testing: unlocks fighters and venues and can save the change to your profile. "
                       "Keep off for a save you care about.");
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
        bool can_start = active || dev_module_can_start() != 0;
        ImGui::BeginDisabled(playing >= 0 || !can_start);
        ImGui::SameLine();
        if (ImGui::Button("Play scene")) {
            scene_launcher_request(pick);
            dev_module_request(active ? 0 : 1);
        }
        item_tooltip("Plays the scene in the game's developer cinema mode, then returns to the main menu. "
                              "Only from the main menu. Close the overlay to watch.");
        ImGui::EndDisabled();
        if (!active) {
            ImGui::BeginDisabled(!can_start);
            if (ImGui::Button("Developer menu")) dev_module_request(0);
            item_tooltip("EA's own debug menu, driven by pad 1 (try the d-pad and A). "
                                  "Only from the main menu: the Story and fights cannot be left for it.");
            ImGui::EndDisabled();
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
}

static int g_page = 0;
static bool g_advanced = false, g_select_page = false, g_return_focus = false, g_setup_request = false;
static std::string g_home_return = "Advanced";
static const char *page_names[] = {"Display", "Controls", "Textures", "Online", "Interface", "Diagnostics", "Maintenance"};
static void controls_page()
{
    if (ImGui::CollapsingHeader("Players, devices and feel", ImGuiTreeNodeFlags_DefaultOpen))
        page_controllers();
    if (ImGui::CollapsingHeader("Gamepad button mapping"))
        page_pad_buttons();
    if (ImGui::CollapsingHeader("Keyboard and mouse bindings"))
        page_keyboard();
}

// ---- the launcher's own layout ------------------------------------------------------------------------
//
// One screen over a living picture (the background breathes in a slow zoom and embers rise over it):
// the logo and a large PLAY on the left, frosted-glass cards for the account, the game's build and the
// news on the right. The settings open as a glass dialog: the few that matter first, every page under
// Advanced. The overlay keeps the compact tabbed screen below.

struct LauncherArt { ImTextureID tex; float w, h; };
static LauncherArt g_banner_art, g_logo_art, g_bg_art, g_blur_art;

void ui_set_launcher_art(ImTextureID banner, float banner_w, float banner_h,
                         ImTextureID logo, float logo_w, float logo_h)
{
    g_banner_art = { banner, banner_w, banner_h };
    g_logo_art = { logo, logo_w, logo_h };
}

void ui_set_launcher_backdrop(ImTextureID picture, float picture_w, float picture_h,
                              ImTextureID blurred, float blurred_w, float blurred_h)
{
    g_bg_art = { picture, picture_w, picture_h };
    g_blur_art = { blurred, blurred_w, blurred_h };
}

static ImU32 col(const ImVec4 &v, float alpha = 1.0f) { return ImGui::GetColorU32(ImVec4(v.x, v.y, v.z, v.w * alpha)); }

static const ImVec4 k_white(1, 1, 1, 1), k_black(0, 0, 0, 1), k_glass(0.07f, 0.07f, 0.085f, 1.0f);
static const ImVec4 k_green(0.35f, 0.85f, 0.45f, 1.0f), k_error(1.0f, 0.45f, 0.4f, 1.0f);
static const ImVec4 k_discord(0.345f, 0.396f, 0.949f, 1.0f);
static const char *const k_discord_invite = "https://discord.gg/zCZKVDnKaP";

// Where the background picture lies this frame, in screen pixels (larger than the window while it
// zooms). The glass samples the blurred copy at the same place, so it looks like frosted glass over it.
static ImVec2 g_pic_a, g_pic_b;
// The strip along the top that drags the borderless window, and where its buttons start (pixels).
static float g_caption_h, g_controls_x;

static ImFont *display_font() { return g_font_display ? g_font_display : g_font_black ? g_font_black : ImGui::GetFont(); }
static ImFont *bold_font() { return g_font_bold ? g_font_bold : ImGui::GetFont(); }

static void open_url(const char *url)
{
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
    SDL_OpenURL(url);
#endif
}

// Frosted glass: the blurred picture behind the panel, a dark translucent fill in one even tone, a soft
// highlight along the top edge that fades out to the sides, and a faint dark edge. No light frame and
// no step across the middle.
static void glass(ImDrawList *dl, ImVec2 a, ImVec2 b, float rounding, const ImVec4 &tint = k_glass, float alpha = 0.55f)
{
    float pw = g_pic_b.x - g_pic_a.x, ph = g_pic_b.y - g_pic_a.y;
    if (g_blur_art.tex && pw > 0 && ph > 0)
        dl->AddImageRounded(g_blur_art.tex, a, b, ImVec2((a.x - g_pic_a.x) / pw, (a.y - g_pic_a.y) / ph),
                            ImVec2((b.x - g_pic_a.x) / pw, (b.y - g_pic_a.y) / ph), IM_COL32_WHITE, rounding);
    dl->AddRectFilled(a, b, col(tint, alpha), rounding);
    dl->AddRectFilled(a, b, col(k_white, 0.035f), rounding);       // one tone, the lighter one, top to bottom
    float mid = (a.x + b.x) / 2, y = a.y + 1.0f, t = y + (rounding > 10 ? 1.5f : 1.0f);
    dl->AddRectFilledMultiColor(ImVec2(a.x + rounding, y), ImVec2(mid, t), col(k_white, 0), col(k_white, 0.24f),
                                col(k_white, 0.24f), col(k_white, 0));
    dl->AddRectFilledMultiColor(ImVec2(mid, y), ImVec2(b.x - rounding, t), col(k_white, 0.24f), col(k_white, 0),
                                col(k_white, 0), col(k_white, 0.24f));
    dl->AddRect(a, b, col(k_black, 0.35f), rounding, 0, 1.0f);
}

// A soft light around a rectangle, for the PLAY button.
static void glow(ImDrawList *dl, ImVec2 a, ImVec2 b, float rounding, const ImVec4 &c, float strength, float s)
{
    for (int i = 6; i >= 1; i--) {
        float g = i * 3.5f * s;
        dl->AddRectFilled(ImVec2(a.x - g, a.y - g), ImVec2(b.x + g, b.y + g), col(c, strength * 0.05f * (7 - i) / 6.0f), rounding + g);
    }
}

static ImVec2 text_size(ImFont *font, float size, const char *s, float wrap = 0.0f)
{
    return font->CalcTextSizeA(size, FLT_MAX, wrap, s);
}

static void text_center(ImDrawList *dl, ImFont *font, float size, ImVec2 a, ImVec2 b, ImU32 c, const char *s)
{
    ImVec2 ts = text_size(font, size, s);
    dl->AddText(font, size, ImVec2(floorf((a.x + b.x - ts.x) / 2), floorf((a.y + b.y - ts.y) / 2)), c, s);
}

enum GlassKind { GLASS_PLAIN, GLASS_RED, GLASS_DISCORD };

// A button made of glass at a screen position; the label is drawn centred in the given font.
static bool glass_button(const char *id, const char *label, ImVec2 a, ImVec2 size, float s, GlassKind kind = GLASS_PLAIN,
                         ImFont *font = nullptr, float font_size = 0.0f, bool enabled = true)
{
    ImGui::SetCursorScreenPos(a);
    if (!enabled) ImGui::BeginDisabled();
    bool pressed = ImGui::InvisibleButton(id, size);
    if (!enabled) ImGui::EndDisabled();
    bool hot = enabled && (ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible));
    bool held = enabled && ImGui::IsItemActive();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 b(a.x + size.x, a.y + size.y);
    float r = std::min(9.0f * s, size.y * 0.3f);
    if (kind == GLASS_RED) {
        float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 2.2f);
        if (enabled) glow(dl, a, b, r, k_red_hi, hot ? 1.0f : 0.5f + 0.3f * pulse, s);
        glass(dl, a, b, r, !enabled ? ImVec4(0.30f, 0.30f, 0.32f, 1) : held ? k_red_lo : hot ? k_red_hi : k_red, 0.84f);
    } else if (kind == GLASS_DISCORD) {
        glass(dl, a, b, r, k_discord, hot ? 0.88f : 0.68f);
    } else {
        glass(dl, a, b, r, hot ? ImVec4(0.20f, 0.20f, 0.24f, 1) : k_glass, hot ? 0.62f : 0.5f);
    }
    text_center(dl, font ? font : ImGui::GetFont(), font_size > 0 ? font_size : ImGui::GetFontSize(), a, b,
                col(k_white, enabled ? 1.0f : 0.45f), label);
    return pressed && enabled;
}

// Embers rising over the right of the picture, drifting in a draught and cooling as they climb.
struct Ember { float x, y, vx, vy, age, life, size, heat, phase; };

static float ember_random()
{
    static unsigned seed = 0x2468ACEu;
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed >> 8) * (1.0f / 16777216.0f);
}

static void ember_spawn(Ember &p, float w, float h, float s, bool anywhere)
{
    p.x = w * (0.28f + 0.74f * ember_random());
    p.y = anywhere ? h * ember_random() : h + 8 * s;
    p.vx = (3 + 10 * ember_random()) * s;
    p.vy = -(20 + 48 * ember_random()) * s;
    p.life = 4.0f + 6.0f * ember_random();
    p.age = anywhere ? p.life * ember_random() : 0.0f;
    p.size = (0.8f + 1.9f * ember_random() * ember_random()) * s;
    p.heat = ember_random();
    p.phase = 6.2832f * ember_random();
}

static void embers(ImDrawList *dl, ImVec2 o, float w, float h, float s)
{
    static Ember e[150];
    static bool started;
    if (!started) {
        for (Ember &p : e) ember_spawn(p, w, h, s, true);
        started = true;
    }
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f), t = (float)ImGui::GetTime();
    for (Ember &p : e) {
        p.age += dt;
        p.x += (p.vx + sinf(t * 1.3f + p.phase) * 9 * s) * dt;
        p.y += p.vy * dt;
        if (p.age >= p.life || p.y < -10 * s || p.x > w + 10 * s) { ember_spawn(p, w, h, s, false); continue; }
        float k = p.age / p.life;
        float a = std::min(1.0f, p.age / 0.6f) * (k > 0.6f ? (1.0f - k) / 0.4f : 1.0f);
        a *= 0.7f + 0.3f * sinf(t * 11.0f + p.phase * 3.0f);
        float cool = k * 0.6f + (1.0f - p.heat) * 0.4f;
        ImVec4 c(1.0f, 0.78f - 0.45f * cool, 0.38f - 0.32f * cool, 1.0f);
        ImVec2 q(o.x + p.x, o.y + p.y);
        dl->AddCircleFilled(q, p.size * 4.5f, col(c, 0.07f * a), 10);
        dl->AddCircleFilled(q, p.size * 2.0f, col(c, 0.22f * a), 8);
        dl->AddLine(q, ImVec2(q.x - p.vx * 0.06f, q.y - p.vy * 0.06f), col(c, 0.5f * a), p.size);
        dl->AddCircleFilled(q, p.size, col(ImVec4(1.0f, 0.92f - 0.4f * cool, 0.62f - 0.4f * cool, 1.0f), a), 6);
    }
}

// The picture: it covers the window and breathes (a slow zoom in and out over 48 s, a little drift),
// darker on the left under the logo and PLAY, with a fire's flicker along the bottom and the embers.
static void backdrop(ImDrawList *dl, ImVec2 o, float w, float h, float s)
{
    float t = (float)ImGui::GetTime();
    if (g_bg_art.tex && g_bg_art.w > 0 && g_bg_art.h > 0) {
        float cover = std::max(w / g_bg_art.w, h / g_bg_art.h);
        float zoom = 1.03f + 0.035f * (0.5f - 0.5f * cosf(t * 6.2832f / 48.0f));
        float pw = g_bg_art.w * cover * zoom, ph = g_bg_art.h * cover * zoom;
        float cx = o.x + w * 0.5f + sinf(t * 6.2832f / 70.0f) * 6 * s;
        float cy = o.y + h * 0.5f + cosf(t * 6.2832f / 90.0f) * 4 * s;
        g_pic_a = ImVec2(cx - pw * 0.5f, cy - ph * 0.5f);
        g_pic_b = ImVec2(cx + pw * 0.5f, cy + ph * 0.5f);
        dl->AddImage(g_bg_art.tex, g_pic_a, g_pic_b);
    } else {
        g_pic_a = g_pic_b = o;
        dl->AddRectFilledMultiColor(o, ImVec2(o.x + w, o.y + h), col(k_bg), col(k_red_lo, 0.5f), col(k_red_lo, 0.7f), col(k_bg));
    }
    dl->AddRectFilledMultiColor(o, ImVec2(o.x + w * 0.45f, o.y + h), col(k_black, 0.35f), col(k_black, 0),
                                col(k_black, 0), col(k_black, 0.35f));
    float flicker = 0.75f + 0.15f * sinf(t * 1.7f) + 0.10f * sinf(t * 4.3f + 1.0f);
    dl->AddRectFilledMultiColor(ImVec2(o.x, o.y + h * 0.6f), ImVec2(o.x + w, o.y + h), col(k_red, 0), col(k_red, 0),
                                col(k_red, 0.16f * flicker), col(k_red, 0.08f * flicker));
    embers(dl, o, w, h, s);
}

static void card_title(ImDrawList *dl, ImVec2 a, float s, const char *title)
{
    dl->AddText(display_font(), 17 * s, ImVec2(a.x + 20 * s, a.y + 11 * s), col(ImVec4(0.86f, 0.84f, 0.80f, 1)), title);
}

// One line of a card: a bold headline or a quieter detail, optionally wrapped to the card.
static float card_line(ImDrawList *dl, ImVec2 at, float s, const ImVec4 &c, const char *text, bool bold, float wrap = 0.0f)
{
    ImFont *f = bold ? bold_font() : ImGui::GetFont();
    float size = (bold ? 16.5f : 13.5f) * s;
    dl->AddText(f, size, at, col(c), text, nullptr, wrap);
    return text_size(f, size, text, wrap).y;
}

struct AccountNumbers { int rank = 0, wins = 0, losses = 0, matches = 0; bool have = false; };

static AccountNumbers account_numbers()
{
    static char buf[16384];
    AccountNumbers n;
    if (!account_stats(buf, sizeof(buf))) return n;
    n.have = true;
    for (const char *line = buf; line && *line;) {
        const char *eq = strchr(line, '='), *end = strchr(line, '\n');
        if (eq && (!end || eq < end)) {
            size_t k = (size_t)(eq - line);
            int v = atoi(eq + 1);
            if (k == 4 && !strncmp(line, "rank", 4)) n.rank = v;
            else if (k == 4 && !strncmp(line, "wins", 4)) n.wins = v;
            else if (k == 6 && !strncmp(line, "losses", 6)) n.losses = v;
            else if (k == 7 && !strncmp(line, "matches", 7)) n.matches = v;
        }
        line = end ? end + 1 : nullptr;
    }
    return n;
}

static void account_card(ImVec2 a, ImVec2 b, float s)
{
    static bool fetched;
    account_init();             // the launcher shows before main() loads the saved session
    account_refresh();
    enum AccountState state = account_state();
    char text[256];
    account_text(text, sizeof(text));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    glass(dl, a, b, 14 * s);
    card_title(dl, a, s, "ACCOUNT");
    ImVec2 at(a.x + 20 * s, a.y + 40 * s);
    float wrap = b.x - a.x - 40 * s;
    if (state == ACCOUNT_IN) {
        if (!fetched) { fetched = true; account_fetch_stats(); }
        card_line(dl, at, s, k_white, text, true);
        AccountNumbers n = account_numbers();
        char line[160];
        if (!n.have) snprintf(line, sizeof(line), "Signed in with Discord");
        else if (n.matches == 0) snprintf(line, sizeof(line), "No online matches yet  \xC2\xB7  signed in with Discord");
        else if (n.rank > 0) snprintf(line, sizeof(line), "Rank %d  \xC2\xB7  %d wins  \xC2\xB7  %d losses", n.rank, n.wins, n.losses);
        else snprintf(line, sizeof(line), "%d wins  \xC2\xB7  %d losses", n.wins, n.losses);
        card_line(dl, ImVec2(at.x, at.y + 26 * s), s, ImVec4(0.80f, 0.80f, 0.82f, 1), line, false);
        if (glass_button("##signout", "Sign out", ImVec2(b.x - 96 * s, a.y + 10 * s), ImVec2(78 * s, 24 * s), s,
                         GLASS_PLAIN, nullptr, 12.5f * s))
            account_sign_out();
    } else if (state == ACCOUNT_WAITING) {
        card_line(dl, at, s, k_gold, "Finish signing in with Discord in your browser...", false, wrap);
        if (glass_button("##cancel", "Cancel", ImVec2(at.x, b.y - 38 * s), ImVec2(100 * s, 28 * s), s))
            account_cancel();
    } else if (state == ACCOUNT_CHECKING) {
        card_line(dl, at, s, ImVec4(0.75f, 0.75f, 0.78f, 1), "Checking your account...", false);
    } else {
#if defined(_WIN32)
        card_line(dl, at, s, state == ACCOUNT_FAILED ? k_error : ImVec4(0.80f, 0.80f, 0.82f, 1),
                  state == ACCOUNT_FAILED && text[0] ? text : "Sign in to play ranked and to fight under your Discord name.",
                  false, wrap);
        if (glass_button("##signin", "Sign in with Discord", ImVec2(at.x, b.y - 40 * s), ImVec2(190 * s, 30 * s), s,
                         GLASS_DISCORD, bold_font(), 14 * s))
            account_sign_in();
#else
        card_line(dl, at, s, ImVec4(0.80f, 0.80f, 0.82f, 1), "Signing in with Discord comes to this system with online play.",
                  false, wrap);
#endif
    }
}

static void status_dot(ImDrawList *dl, ImVec2 at, float s, const ImVec4 &c)
{
    ImVec2 q(at.x + 5 * s, at.y + 10 * s);
    dl->AddCircleFilled(q, 7 * s, col(c, 0.18f), 16);
    dl->AddCircleFilled(q, 4 * s, col(c), 12);
}

static void game_card(ImVec2 a, ImVec2 b, float s, UiResult &result)
{
    UpdaterState state = updater_state();
    int build = 0, local = updater_local_build();
    char version[64], notes[8], message[512], line[200];
    updater_info(&build, version, sizeof(version), notes, sizeof(notes));
    updater_message(message, sizeof(message));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    glass(dl, a, b, 14 * s);
    card_title(dl, a, s, "GAME");
    ImVec2 at(a.x + 20 * s, a.y + 40 * s), head(at.x + 18 * s, at.y);
    ImVec4 detail(0.80f, 0.80f, 0.82f, 1);
    snprintf(line, sizeof(line), "Build %d  \xC2\xB7  v%s", local, DEFJAM_VERSION);
    switch (state) {
    case UPDATER_CURRENT:
        status_dot(dl, at, s, k_green);
        card_line(dl, head, s, k_white, "Ready to play", true);
        snprintf(line, sizeof(line), "Build %d  \xC2\xB7  up to date", local);
        break;
    case UPDATER_AVAILABLE: {
        char title[64];
        status_dot(dl, at, s, k_gold);
        snprintf(title, sizeof(title), "Build %d is out", build);
        card_line(dl, head, s, k_white, title, true);
        snprintf(line, sizeof(line), "You have build %d", local);
        if (glass_button("##update", "Update", ImVec2(b.x - 112 * s, a.y + 40 * s), ImVec2(92 * s, 30 * s), s, GLASS_RED,
                         bold_font(), 14 * s))
            ImGui::OpenPopup("Update available");
        break;
    }
    case UPDATER_DOWNLOADING:
    case UPDATER_INSTALLING: {
        // Step and percentage, what the setup is doing, the bar, then the build and the time so far
        // (the card is taller while an update runs: launcher_screen).
        UpdateProgress p = update_progress(state, build);
        char text[256], pct[16];
        float right = b.x - 20 * s, head_px = 16.5f * s, detail_px = 13.5f * s, note_px = 12.5f * s;
        status_dot(dl, at, s, k_gold);
        snprintf(pct, sizeof(pct), "%d%%", p.percent);
        float pw = text_size(bold_font(), head_px, pct).x;
        dl->AddText(bold_font(), head_px, ImVec2(right - pw, head.y), col(k_gold), pct);
        // the card is narrow: the step's name heads it, "Step 6 of 8" goes on the line under the bar
        fit_step(bold_font(), head_px, p.name, p.name_short, right - pw - 12 * s - head.x, text, sizeof(text));
        card_line(dl, head, s, k_white, text, true);
        fit_line(ImGui::GetFont(), detail_px, p.detail, right - at.x, text, sizeof(text));
        card_line(dl, ImVec2(at.x, at.y + 26 * s), s, detail, text, false);
        ImVec2 ba(at.x, at.y + 49 * s), bb(right, at.y + 55 * s);
        dl->AddRectFilled(ba, bb, col(k_white, 0.10f), 3 * s);
        if (p.fraction > 0)
            dl->AddRectFilled(ba, ImVec2(ba.x + (bb.x - ba.x) * std::min(p.fraction, 1.0f), bb.y), col(k_red_hi), 3 * s);
        if (p.counter[0])
            snprintf(text, sizeof(text), "%s  \xC2\xB7  build %d", p.counter, build);
        else
            snprintf(text, sizeof(text), "Updating to build %d", build);
        dl->AddText(ImGui::GetFont(), note_px, ImVec2(at.x, at.y + 63 * s), col(k_white, 0.55f), text);
        if (p.elapsed[0]) {
            float ew = text_size(ImGui::GetFont(), note_px, p.elapsed).x;
            dl->AddText(ImGui::GetFont(), note_px, ImVec2(right - ew, at.y + 63 * s), col(k_white, 0.55f), p.elapsed);
        }
        line[0] = 0;
        break;
    }
    case UPDATER_INSTALLED: {
        char title[64];
        status_dot(dl, at, s, k_green);
        snprintf(title, sizeof(title), "Build %d is installed", build);
        card_line(dl, head, s, k_white, title, true);
        snprintf(line, sizeof(line), "Restart the launcher to use it");
        if (glass_button("##restart", "Restart", ImVec2(b.x - 112 * s, a.y + 40 * s), ImVec2(92 * s, 30 * s), s, GLASS_RED,
                         bold_font(), 14 * s)) {
            updater_restart();
            result.leave = true;        // this copy closes; the helper starts the new launcher
        }
        break;
    }
    case UPDATER_FAILED:
        status_dot(dl, at, s, k_error);
        card_line(dl, head, s, k_white, "Ready to play", true);
        snprintf(line, sizeof(line), "Update check failed: %s", message);
        break;
    case UPDATER_STARTED:
        status_dot(dl, at, s, k_gold);
        card_line(dl, head, s, k_white, "The update is starting", true);
        break;
    default:
        status_dot(dl, at, s, ImVec4(0.6f, 0.6f, 0.64f, 1));
        card_line(dl, head, s, k_white, "Ready to play", true);
        snprintf(line, sizeof(line), "Build %d  \xC2\xB7  looking for updates...", local);
        break;
    }
    if (line[0]) {
        dl->PushClipRect(a, b, true);
        card_line(dl, ImVec2(at.x, at.y + 26 * s), s, detail, line, false);
        dl->PopClipRect();
    }
}

static void news_card(ImVec2 a, ImVec2 b, float s)
{
    int build = 0, local = updater_local_build();
    char version[64], notes[1024], title[64];
    updater_info(&build, version, sizeof(version), notes, sizeof(notes));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    glass(dl, a, b, 14 * s);
    card_title(dl, a, s, "NEWS");
    ImVec2 at(a.x + 20 * s, a.y + 40 * s);
    float wrap = b.x - a.x - 40 * s;
    dl->PushClipRect(a, ImVec2(b.x, b.y - 46 * s), true);
    if (notes[0] && build > 0) {
        snprintf(title, sizeof(title), build > local ? "New in build %d" : "What's new in build %d", build);
        float y = card_line(dl, at, s, k_white, title, true);
        card_line(dl, ImVec2(at.x, at.y + y + 8 * s), s, ImVec4(0.82f, 0.82f, 0.84f, 1), notes, false, wrap);
    } else {
        float y = card_line(dl, at, s, k_white, "News, events and the leaderboard", true);
        card_line(dl, ImVec2(at.x, at.y + y + 8 * s), s, ImVec4(0.82f, 0.82f, 0.84f, 1),
                  "Patch notes, tournaments and players to fight: the FFNY Online community is on Discord.", false, wrap);
    }
    dl->PopClipRect();
    if (glass_button("##news_discord", "All news on Discord", ImVec2(at.x, b.y - 42 * s), ImVec2(170 * s, 28 * s), s,
                     GLASS_PLAIN, nullptr, 13 * s))
        open_url(k_discord_invite);
}

enum { PAGE_DISPLAY, PAGE_TEXTURES, PAGE_CONTROLLERS, PAGE_PAD, PAGE_KEYBOARD, PAGE_ONLINE, PAGE_GENERAL,
       PAGE_DEBUG, PAGE_MAINTENANCE, PAGE_COUNT };
static const char *const k_page_names[PAGE_COUNT] = {
    "Display", "Textures", "Controllers", "Pad buttons", "Keyboard & mouse", "Voice chat", "General",
    "Diagnostics", "Maintenance" };
static const char *const k_page_hints[PAGE_COUNT] = {
    "Window, picture shape, resolution and image quality",
    "HD texture packs and making your own",
    "Who plays with which pad, rumble and dead zones",
    "Remap every button of a gamepad",
    "Keys and mouse buttons for the keyboard player",
    "Push-to-talk, the key and the volumes",
    "Start-up, the in-game overlay and files",
    "Developer tools, texture capture and the play-test recorder",
    "Folders, setup and update / repair" };

static void draw_page(int page, UiMode mode)
{
    switch (page) {
    case PAGE_ONLINE: voice_settings(); break;        // the rest of Online is the start page's Account card
    case PAGE_DISPLAY: page_display(); break;
    case PAGE_TEXTURES: page_textures(); break;
    case PAGE_CONTROLLERS: page_controllers(); break;
    case PAGE_PAD: page_pad_buttons(); break;
    case PAGE_KEYBOARD: page_keyboard(); break;
    case PAGE_DEBUG: page_debug(mode); break;
    case PAGE_MAINTENANCE: page_maintenance(mode); break;
    default: page_general(mode); break;
    }
}

// The picture quality as one choice: render scale, anti-aliasing and texture filtering together.
static void quality_widget()
{
    struct Preset { const char *name; int scale, aa, anisotropy; };
    static const Preset presets[] = {
        { "Low (fastest)", 1, 0, 0 }, { "Medium", 2, 1, 4 }, { "High", 3, 1, 4 }, { "Ultra (strong graphics cards)", 4, 1, 4 } };
    int scale = recomp_settings_get("display", "render_scale", 2), aa = recomp_settings_get("display", "anti_aliasing", 1);
    int anisotropy = recomp_settings_get("display", "anisotropy", 4), current = -1;
    for (int i = 0; i < 4; i++)
        if (presets[i].scale == scale && presets[i].aa == aa && presets[i].anisotropy == anisotropy) current = i;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    if (ImGui::BeginCombo("Picture quality", current >= 0 ? presets[current].name : "Custom (see Advanced)")) {
        for (int i = 0; i < 4; i++) {
            if (ImGui::Selectable(presets[i].name, i == current) && i != current) {
                recomp_settings_set("display", "render_scale", presets[i].scale);
                recomp_settings_set("display", "anti_aliasing", presets[i].aa);
                recomp_settings_set("display", "anisotropy", presets[i].anisotropy);
                commit();
            }
            if (i == current) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Render scale, anti-aliasing and texture filtering together. The render scale applies the "
                          "next time the game starts.");
}

// The online server (build 30): Automatic, Europe or North America, each with its ping and players
// as the relays answer (netplay_servers.c). A custom address (Advanced, Online) shows as such.
static std::string server_line(const NetServerInfo &s)
{
    char line[128];
    if (s.state == NETSERVER_UP)
        snprintf(line, sizeof(line), "%s  \xC2\xB7  %d ms  \xC2\xB7  %d online", s.name, s.ping_ms, s.online);
    else if (s.state == NETSERVER_OFFLINE)
        snprintf(line, sizeof(line), "%s  \xC2\xB7  offline", s.name);
    else
        snprintf(line, sizeof(line), "%s  \xC2\xB7  checking...", s.name);
    return line;
}

static void server_widget()
{
    netservers_probe();
    NetServerInfo s[NETSERVER_COUNT];
    int n = netservers_get(s, NETSERVER_COUNT);
    enum NetServerChoice choice = netservers_choice();
    int nearest = -1;
    for (int i = 0; i < n; i++)
        if (s[i].state == NETSERVER_UP && (nearest < 0 || s[i].ping_ms < s[nearest].ping_ms)) nearest = i;
    std::string automatic = nearest >= 0 ? std::string("Automatic (now ") + s[nearest].name + ")" : "Automatic (lowest ping)";
    // Both (build 31): the rooms and quick matches of Europe and North America
    std::string both = nearest >= 0 ? std::string("Both servers (home: ") + s[nearest].name + ")" : "Both servers";
    char buf[128];
    std::string preview = choice == NETCHOICE_AUTO ? automatic : choice == NETCHOICE_BOTH ? both :
        choice == NETCHOICE_CUSTOM ? std::string("Custom: ") + recomp_settings_get_text("online", "server", buf, sizeof(buf), "") :
        server_line(s[choice == NETCHOICE_EU ? NETSERVER_EU : NETSERVER_NA]);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
    if (ImGui::BeginCombo("Server", preview.c_str())) {
        if (ImGui::Selectable(automatic.c_str(), choice == NETCHOICE_AUTO)) netservers_set_choice(NETCHOICE_AUTO);
        for (int i = 0; i < n; i++) {
            enum NetServerChoice c = i == NETSERVER_EU ? NETCHOICE_EU : NETCHOICE_NA;
            if (ImGui::Selectable(server_line(s[i]).c_str(), choice == c)) netservers_set_choice(c);
        }
        if (ImGui::Selectable(both.c_str(), choice == NETCHOICE_BOTH)) netservers_set_choice(NETCHOICE_BOTH);
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Where you play online. Automatic takes the server with the lowest ping when you go online. "
                          "Players on one server meet only players on the same server; accounts and stats are shared. "
                          "Both servers: rooms and Quick Match on Europe and North America; matches across the ocean "
                          "have higher ping.");
}

static void page_basics()
{
    ImGui::SeparatorText("Picture");
    window_size_widget();
    quality_widget();
    settings_group("display", { "widescreen", "crowd_near" });
    ImGui::SeparatorText("Voice chat");
    settings_group("online", { "voice", "voice_key", "voice_pad" });
    ImGui::SeparatorText("Online");
    server_widget();
    settings_group("online", { "rollback" });
    ImGui::TextDisabled("Beta: used only when the other player has it on too. Applies the next time the game starts.");
    ImGui::SeparatorText("HD textures");
    settings_group("textures", { "enabled" });
    ImGui::TextDisabled("Experimental: they can cause white screens or wrong pictures. Make them under Advanced, Textures.");
}

// RECOMP_LAUNCHER_PAGE=basics (or a page's name: display, voice, ...): open the settings there, for
// screenshots.
static int initial_page()
{
    static const char *const names[] = { "display", "textures", "controllers", "pad", "keyboard", "voice", "general" };
    const char *want = getenv("RECOMP_LAUNCHER_PAGE");
    if (want && !strcmp(want, "basics")) return PAGE_COUNT;
    for (int i = 0; want && i < PAGE_COUNT; i++)
        if (!strcmp(want, names[i])) return i;
    return -1;
}

// The dialog's own style: translucent fields and buttons on the glass.
static void push_glass_style()
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.15f));
}

static void pop_glass_style() { ImGui::PopStyleColor(5); }

// A modal on glass, with its name (up to "##") as the heading in the display font.
static bool begin_glass_modal(const char *name, float width)
{
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 1.4f, ImGui::GetFontSize() * 1.2f));
    bool open = ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                                      ImGuiWindowFlags_NoTitleBar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) return false;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), b(a.x + ImGui::GetWindowWidth(), a.y + ImGui::GetWindowHeight());
    dl->PushClipRect(a, b, false);
    glass(dl, a, b, 14 * ImGui::GetFontSize() / 17.0f, k_glass, 0.80f);
    dl->PopClipRect();
    const char *end = strstr(name, "##");
    ImGui::PushFont(display_font(), ImGui::GetStyle().FontSizeBase * 1.45f);
    ImGui::TextUnformatted(name, end);
    ImGui::PopFont();
    push_glass_style();
    return true;
}

static void end_glass_modal()
{
    pop_glass_style();
    ImGui::EndPopup();
}

// After a crash, an offer to send that run's log (bug_report.c); F9 cannot, the game is gone.
static bool g_crash_checked;        // crash_popup has looked: the one-time notices may come after it
static void crash_popup()
{
    static bool checked, sent;
    static char log_path[300];
    // After the update check, so the two modals never replace each other: an offered update comes
    // first, this after "Later".
    UpdaterState upd = updater_state();
    if (ImGui::IsPopupOpen("Update available")) return;
    if (!checked && ((upd != UPDATER_IDLE && upd != UPDATER_CHECKING) || ImGui::GetTime() > 8.0)) {
        checked = true;
        g_crash_checked = true;
        if (bug_report_last_crash(log_path, sizeof(log_path))) ImGui::OpenPopup("The game crashed");
    }
    if (!begin_glass_modal("The game crashed", ImGui::GetFontSize() * 30)) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
    ImGui::TextUnformatted("The game crashed last time. Send its log to the developers? It shows where the crash "
                           "happened, so it can be fixed.");
    ImGui::TextDisabled("Only that session's log is sent, with your Windows user folder blanked. See Privacy.");
    char status[160];
    bool busy = bug_report_busy();
    if (sent && bug_report_toast(status, sizeof(status))) ImGui::TextColored(k_gold, "%s", status);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (!sent) {
        ImGui::PushStyleColor(ImGuiCol_Button, k_red);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
        if (ImGui::Button("Send crash report", ImVec2(ImGui::GetFontSize() * 11, 0))) {
            bug_report_send("crash", log_path, nullptr);
            bug_report_crash_seen(log_path);
            sent = true;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("No thanks", ImVec2(ImGui::GetFontSize() * 8, 0))) {
            bug_report_crash_seen(log_path);
            ImGui::CloseCurrentPopup();
        }
    } else {
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Close", ImVec2(ImGui::GetFontSize() * 8, 0))) ImGui::CloseCurrentPopup();
        if (busy) ImGui::EndDisabled();
    }
    end_glass_modal();
}

// RECOMP_TEST_NOTICE=gpu|launcher (screenshots): that one-time notice at once, whatever the PC.
static bool notice_test(const char *which)
{
    const char *t = getenv("RECOMP_TEST_NOTICE");
    return t && !strcmp(t, which);
}

// A one-time notice may open: the update offer and the crash report came first, nothing else is open.
static bool notice_may_open()
{
    return (g_crash_checked || getenv("RECOMP_TEST_NOTICE")) &&
           !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

// The graphics card the game draws with: the first adapter, as the game's D3D11 device takes it
// (its log says "D3D8: graphics card ..."). False when it cannot be read (or off Windows).
static bool first_gpu(char *name, size_t size, unsigned *vendor, unsigned *mb)
{
    const char *t = getenv("RECOMP_TEST_GPU");     // tests: "vendor,MB", e.g. 8086,128
    if (t && *t) {
        *vendor = (unsigned)strtoul(t, nullptr, 16);
        const char *c = strchr(t, ',');
        *mb = c ? (unsigned)atoi(c + 1) : 128;
        snprintf(name, size, "Intel(R) UHD Graphics (test)");
        return true;
    }
#if defined(_WIN32)
    IDXGIFactory1 *factory = nullptr;
    IDXGIAdapter1 *adapter = nullptr;
    DXGI_ADAPTER_DESC1 desc;
    bool ok = false;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)) &&
        SUCCEEDED(factory->EnumAdapters1(0, &adapter)) && SUCCEEDED(adapter->GetDesc1(&desc))) {
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, (int)size, nullptr, nullptr);
        *vendor = desc.VendorId;
        *mb = (unsigned)(desc.DedicatedVideoMemory / (1024 * 1024));
        ok = true;
    }
    if (adapter) adapter->Release();
    if (factory) factory->Release();
    return ok;
#else
    (void)name; (void)size; (void)vendor; (void)mb;
    return false;
#endif
}

// Players on laptops with an integrated graphics card (Intel, or less than 1 GB of its own memory)
// got a slow game or white boxes with HD textures on. Once, while HD textures are on: recommend
// them off and render scale 1; either answer is remembered in settings.ini ([launcher]
// gpu_notice_seen).
static void gpu_notice()
{
    static bool decided;
    static char name[128];
    static unsigned vendor, mb;
    if (!decided && notice_may_open()) {
        decided = true;
        bool test = notice_test("gpu");
        bool weak = first_gpu(name, sizeof(name), &vendor, &mb) && (vendor == 0x8086 || mb < 1024);
        if (test || (weak && recomp_settings_get("textures", "enabled", 0) &&
                     !recomp_settings_get("launcher", "gpu_notice_seen", 0))) {
            fprintf(stderr, "[UI] graphics card notice: %s (vendor %04X, %u MB)\n", name, vendor, mb);
            ImGui::OpenPopup("Graphics card");
        }
    }
    if (!begin_glass_modal("Graphics card", ImGui::GetFontSize() * 30)) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
    ImGui::PushFont(g_font_bold, ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextColored(k_gold, "%s", name);
    ImGui::PopFont();
    ImGui::TextDisabled(vendor == 0x8086 ? "Built into the processor, %u MB of its own memory" : "%u MB of its own memory", mb);
    ImGui::Spacing();
    ImGui::TextUnformatted("This graphics card is on the weak side for HD textures. With them on, the game can run "
                           "slowly or show white boxes. We recommend HD textures off and render scale 1.");
    ImGui::TextDisabled("You can change both any time under Settings.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    bool close = false;
    ImGui::PushStyleColor(ImGuiCol_Button, k_red);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
    if (ImGui::Button("Use the recommended settings", ImVec2(ImGui::GetFontSize() * 14, 0))) {
        recomp_settings_set("textures", "enabled", 0);
        recomp_settings_set("display", "render_scale", 1);
        fprintf(stderr, "[UI] graphics card notice: HD textures off, render scale 1\n");
        close = true;
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button("Keep my settings", ImVec2(ImGui::GetFontSize() * 9, 0))) close = true;
    if (close) {
        recomp_settings_set("launcher", "gpu_notice_seen", 1);
        commit();
        ImGui::CloseCurrentPopup();
    }
    end_glass_modal();
}

// Started from the setup's build folder instead of with the launcher (pc_settings.c): say how to
// start it properly, every time; the game still plays.
static void outside_launcher_notice()
{
    static bool decided;
    if (!decided && notice_may_open()) {
        decided = true;
        if (pc_settings_started_outside_launcher() || notice_test("launcher")) ImGui::OpenPopup("Start with the launcher");
    }
    if (!begin_glass_modal("Start with the launcher", ImGui::GetFontSize() * 30)) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
    ImGui::PushFont(g_font_bold, ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextColored(k_gold, "Start FFNY Online with the launcher");
    ImGui::PopFont();
    ImGui::TextUnformatted("Use the desktop shortcut, or DefJamLauncher.exe in your install folder.");
    ImGui::Spacing();
    ImGui::TextDisabled("This copy was started from the setup's build folder. From there it finds none of your "
                        "data: it runs as build 0, gets no updates and cannot play online.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(ImGui::GetFontSize() * 8, 0))) ImGui::CloseCurrentPopup();
    end_glass_modal();
}

// The update offer: a modal over the launcher when a newer build is signed and published.
static void update_popup(UiResult &result)
{
    static int offered;             // the build offered last: each new one is offered once
    UpdaterState state = updater_state();
    char message[512];
    updater_message(message, sizeof(message));
    if (state == UPDATER_AVAILABLE) {
        int newest = 0;
        updater_info(&newest, nullptr, 0, nullptr, 0);
        if (newest > 0 && newest != offered) {
            ImGui::OpenPopup("Update available");
            offered = newest;
        }
    }
    if (!begin_glass_modal("Update available", ImGui::GetFontSize() * 30)) return;
    int build = 0;
    char version[64], notes[1024];
    updater_info(&build, version, sizeof(version), notes, sizeof(notes));
    ImGui::PushFont(g_font_bold, ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextColored(k_gold, "Version %s", version);
    ImGui::PopFont();
    ImGui::TextDisabled("Build %d - you have build %d", build, updater_local_build());
    if (notes[0]) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
        ImGui::TextUnformatted(notes);
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    if (state == UPDATER_DOWNLOADING || state == UPDATER_INSTALLING) {
        // "Step 6 of 8 · Translating the game code", the bar with the percentage, what the setup is
        // doing on one line, and the time so far.
        UpdateProgress p = update_progress(state, build);
        char text[256], overlay[16];
        float width = ImGui::GetFontSize() * 28;
        ImGui::PushFont(g_font_bold, 0.0f);
        fit_step(ImGui::GetFont(), ImGui::GetFontSize(), p.step, p.step_short, width, text, sizeof(text));
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
        snprintf(overlay, sizeof(overlay), "%d%%", p.percent);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, k_red);
        ImGui::ProgressBar(std::min(p.fraction, 1.0f), ImVec2(width, 0), overlay);
        ImGui::PopStyleColor();
        fit_line(ImGui::GetFont(), ImGui::GetFontSize(), p.detail, width, text, sizeof(text));
        ImGui::TextUnformatted(text);
        if (p.elapsed[0]) ImGui::TextDisabled("%s", p.elapsed);
        ImGui::TextDisabled("You can keep this window open; the game stays as it is until you restart.");
        if (ImGui::Button("Hide", ImVec2(ImGui::GetFontSize() * 8, 0))) ImGui::CloseCurrentPopup();
    } else if (state == UPDATER_INSTALLED) {
        ImGui::TextColored(k_green, "%s", message);
        ImGui::PushStyleColor(ImGuiCol_Button, k_red);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
        if (ImGui::Button("Restart now", ImVec2(ImGui::GetFontSize() * 10, 0))) {
            updater_restart();
            result.leave = true;        // this copy closes; the helper starts the new launcher
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(ImGui::GetFontSize() * 8, 0))) ImGui::CloseCurrentPopup();
    } else if (state == UPDATER_STARTED) {
        ImGui::TextWrapped("%s", message);
        result.leave = true;            // the setup takes over; it waits for this window to close
    } else if (!updater_can_install()) {
        ImGui::TextWrapped("This copy was built from a source checkout, not installed by setup: get the new version "
                           "from the source and build it again.");
        if (ImGui::Button("OK", ImVec2(ImGui::GetFontSize() * 8, 0))) ImGui::CloseCurrentPopup();
    } else {
        if (state == UPDATER_FAILED) ImGui::TextColored(k_error, "%s", message);
        ImGui::TextWrapped("The update downloads (about 55 MB), is checked, and is built from your installed game "
                           "files right here, usually in a few minutes. Saves, settings and HD textures stay.");
        ImGui::PushStyleColor(ImGuiCol_Button, k_red);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
        if (ImGui::Button("Install now", ImVec2(ImGui::GetFontSize() * 10, 0))) updater_install();
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(ImGui::GetFontSize() * 8, 0))) ImGui::CloseCurrentPopup();
    }
    end_glass_modal();
}

// The settings as a glass dialog over the launcher: Basics (window, quality, voice, HD), and every
// page under Advanced.
static void settings_dialog(ImVec2 o, float w, float h, float s, int &tab, int &page)
{
    ImVec2 size(std::min(w - 40 * s, 840 * s), std::min(h - 40 * s, 610 * s));
    ImGui::SetNextWindowPos(ImVec2(o.x + (w - size.x) / 2, o.y + (h - size.y) / 2));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) return;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), b(a.x + size.x, a.y + size.y);
    glass(dl, a, b, 16 * s, k_glass, 0.80f);
    dl->AddText(display_font(), 30 * s, ImVec2(a.x + 30 * s, a.y + 18 * s), col(k_white), "SETTINGS");
    static const char *const tabs[] = { "Basics", "Advanced" };
    for (int i = 0; i < 2; i++) {
        ImVec2 ta(b.x - 30 * s - (2 - i) * 118 * s, a.y + 22 * s);
        if (glass_button(i ? "##tab_advanced" : "##tab_basics", tabs[i], ta, ImVec2(110 * s, 32 * s), s,
                         tab == i ? GLASS_RED : GLASS_PLAIN, bold_font(), 14 * s))
            tab = i;
    }
    push_glass_style();
    float top = 72 * s, foot = 64 * s;
    ImVec2 body(size.x - 60 * s, size.y - top - foot);
    ImGui::SetCursorPos(ImVec2(30 * s, top));
    if (tab == 0) {
        ImGui::BeginChild("##basics", body, ImGuiChildFlags_None);
        page_basics();
        ImGui::EndChild();
    } else {
        float side = 180 * s;
        ImGui::BeginChild("##pages", ImVec2(side, body.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        for (int i = 0; i < PAGE_COUNT; i++) {
            ImVec2 pa = ImGui::GetCursorScreenPos();
            float item_h = 34 * s;
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##page", ImVec2(side - 10 * s, item_h))) page = i;
            bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            ImDrawList *sd = ImGui::GetWindowDrawList();
            ImVec2 pb(pa.x + side - 10 * s, pa.y + item_h);
            if (page == i) {
                sd->AddRectFilled(pa, pb, col(k_red, 0.55f), 7 * s);
            } else if (hovered) {
                sd->AddRectFilled(pa, pb, col(k_white, 0.06f), 7 * s);
            }
            float fs = 14.5f * s;
            sd->AddText(page == i ? bold_font() : ImGui::GetFont(), fs, ImVec2(pa.x + 14 * s, pa.y + (item_h - fs) / 2 - 1 * s),
                        col(k_white, page == i ? 1.0f : 0.72f), k_page_names[i]);
        }
        ImGui::EndChild();
        ImGui::SameLine(0, 14 * s);
        ImGui::BeginChild("##page", ImVec2(body.x - side - 14 * s, body.y), ImGuiChildFlags_None);
        ImGui::TextDisabled("%s", k_page_hints[page]);
        draw_page(page, UI_LAUNCHER);
        ImGui::EndChild();
    }
    pop_glass_style();
    {
        std::string restart = restart_needed();
        const char *note = restart.empty() ? "Changes are saved as you make them." : nullptr;
        char buf[300];
        if (!note) { snprintf(buf, sizeof(buf), "Applies at the next start: %s", restart.c_str()); note = buf; }
        dl->PushClipRect(a, ImVec2(b.x - 170 * s, b.y), true);
        dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(a.x + 30 * s, b.y - foot / 2 - 8 * s),
                    restart.empty() ? col(k_white, 0.55f) : col(k_gold), note);
        dl->PopClipRect();
    }
    if (glass_button("##done", "DONE", ImVec2(b.x - 30 * s - 130 * s, b.y - foot / 2 - 19 * s), ImVec2(130 * s, 38 * s), s,
                     GLASS_RED, display_font(), 18 * s) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

#if defined(_WIN32)
// The borderless window's own minimise and close buttons, top right.
static bool window_controls(ImVec2 o, float w, float s)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float bw = 46 * s, bh = 34 * s;
    bool close = false;
    for (int i = 0; i < 2; i++) {
        ImVec2 a(o.x + w - (2 - i) * bw, o.y), b(a.x + bw, a.y + bh);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        bool pressed = ImGui::InvisibleButton("##window", ImVec2(bw, bh), ImGuiButtonFlags_None);
        ImGui::PopID();
        bool hot = ImGui::IsItemHovered();
        if (hot) dl->AddRectFilled(a, b, i ? col(ImVec4(0.85f, 0.10f, 0.15f, 1)) : col(k_white, 0.10f));
        ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
        float r = 5 * s;
        ImU32 ink = col(k_white, hot ? 1.0f : 0.75f);
        if (i == 0) {
            dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), ink, 1.2f * s);
            if (pressed) ShowWindow((HWND)ImGui::GetMainViewport()->PlatformHandleRaw, SW_MINIMIZE);
        } else {
            dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ink, 1.2f * s);
            dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), ink, 1.2f * s);
            close = close || pressed;
        }
    }
    return close;
}
#endif

bool ui_launcher_caption(float x, float y)
{
    if (!ImGui::GetCurrentContext() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return false;
    return y >= 0 && y < g_caption_h && x < g_controls_x;
}

static UiResult draw_launcher(float x, float y, float width, float height)
{
    UiResult result;
    static int start_page = initial_page();
    static int tab = start_page == PAGE_COUNT || start_page < 0 ? 0 : 1;
    static int page = start_page >= 0 && start_page < PAGE_COUNT ? start_page : PAGE_DISPLAY;
    // At the start and every five minutes while it stays open: a build published meanwhile shows at
    // once (the Game card, the update offer), without starting the launcher again.
    static UiTick next_check;
    if (ui_ticks() >= next_check) {
        UpdaterState st = updater_state();
        next_check = ui_ticks() + 5 * 60000;
        if (st == UPDATER_IDLE || st == UPDATER_CURRENT || st == UPDATER_FAILED) updater_check();
    }
    // One design pixel: the layout is drawn for 1100 x 688 and scaled to the window, centred.
    const float s = std::min(width / 1100.0f, height / 688.0f);

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    backdrop(dl, origin, width, height, s);
    ImVec2 o(origin.x + (width - 1100 * s) / 2, origin.y + (height - 688 * s) / 2);
    auto at = [&](float dx, float dy) { return ImVec2(o.x + dx * s, o.y + dy * s); };

    // The logo, or the name in the display font without it.
    if (g_logo_art.tex && g_logo_art.w > 0) {
        float lw = 300 * s, lh = lw * g_logo_art.h / g_logo_art.w;
        if (lh > 300 * s) { lh = 300 * s; lw = lh * g_logo_art.w / g_logo_art.h; }
        ImVec2 la = at(48, 64);
        dl->AddImage(g_logo_art.tex, la, ImVec2(la.x + lw, la.y + lh));
    } else {
        dl->AddText(display_font(), 64 * s, at(48, 150), col(k_white), "FFNY");
        dl->AddText(display_font(), 40 * s, at(48, 222), col(k_red_hi), "ONLINE");
    }

    // The top strip: it drags the window; the name and build, and the window's buttons.
    g_caption_h = 44 * s;
    g_controls_x = width;
    {
        char label[96];
        snprintf(label, sizeof(label), "FFNY Online  \xC2\xB7  build %d", updater_local_build());
        float right = width;
#if defined(_WIN32)
        right = width - 92 * s;
        g_controls_x = right;
        if (window_controls(origin, width, s)) result.leave = true;
#endif
        ImVec2 ts = text_size(ImGui::GetFont(), 13 * s, label);
        dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(origin.x + right - ts.x - 18 * s, origin.y + (34 * s - ts.y) / 2),
                    col(k_white, 0.62f), label);
    }

    // PLAY and the two small buttons under it.
    UpdaterState upd = updater_state();
    bool busy = hd_build_status(nullptr, nullptr, nullptr, 0, nullptr, 0) == HD_BUILD_RUNNING ||
                upd == UPDATER_DOWNLOADING || upd == UPDATER_INSTALLING;
    {
        std::string restart = restart_needed();
        const char *note = busy ? (upd == UPDATER_DOWNLOADING || upd == UPDATER_INSTALLING ? "PLAY is back when the update is in."
                                                                                           : "PLAY is back when the HD textures are made.")
                                : nullptr;
        char buf[300];
        if (!note && !restart.empty()) { snprintf(buf, sizeof(buf), "Applies at the next start: %s", restart.c_str()); note = buf; }
        if (note) dl->AddText(ImGui::GetFont(), 13 * s, at(48, 472), col(k_gold), note, nullptr, 270 * s);
    }
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (glass_button("##play", "PLAY", at(48, 498), ImVec2(270 * s, 64 * s), s, GLASS_RED, display_font(), 36 * s, !busy))
        result.play = true;
    if (glass_button("##settings", "Settings", at(48, 578), ImVec2(130 * s, 36 * s), s, GLASS_PLAIN, nullptr, 15 * s))
        ImGui::OpenPopup("Settings");
    if (glass_button("##discord", "Discord", at(188, 578), ImVec2(130 * s, 36 * s), s, GLASS_PLAIN, nullptr, 15 * s))
        open_url(k_discord_invite);
    dl->AddText(ImGui::GetFont(), 12.5f * s, at(48, 648), col(k_white, 0.5f), "Fan project, not affiliated with EA.");

    // The cards. While an update runs the GAME card is taller (step, what it is doing, bar, time)
    // and the news gives it the room.
    float grow = upd == UPDATER_DOWNLOADING || upd == UPDATER_INSTALLING ? 24 : 0;
    account_card(at(740, 64), at(1060, 168), s);
    game_card(at(740, 184), at(1060, 288 + grow), s, result);
    news_card(at(740, 304 + grow), at(1060, 520), s);

    {
        static bool opened;
        if (!opened && start_page >= 0) ImGui::OpenPopup("Settings");
        opened = true;
    }
    settings_dialog(origin, width, height, s, tab, page);
    update_popup(result);
    crash_popup();
    outside_launcher_notice();
    gpu_notice();
    ImGui::End();
    return result;
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
    if (mode == UI_LAUNCHER)
        return draw_launcher(x, y, width, height);

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
            g_home_return = home.route == 2 ? "Configure" : home.route == 3 ? "Mods" : "Advanced";
        }
        if (home.route == 4)
            g_setup_request = true;
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
            pcui::heading("ADVANCED SETTINGS", 24 * scale);
        bool popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!ui_capturing() && !popup)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false))
            {
                g_page = (g_page + 1) % 7;
                g_select_page = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false))
            {
                g_page = (g_page + 6) % 7;
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
                for (int i = 0; i < 7; i++)
                    if (ImGui::Selectable(page_names[i], g_page == i))
                        g_page = i;
                ImGui::EndCombo();
            }
            ui_target("Category");
        }
        else if (ImGui::BeginTabBar("categories", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
        {
            int requested = g_page;
            for (int i = 0; i < 7; i++)
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
            page_online(mode);
            break;
        case 4:
            page_general(mode);
            break;
        case 5:
            page_debug(mode);
            break;
        case 6:
            page_maintenance(mode);
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
    if (!compact_footer && !pcui::action_status().empty())
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
        if (advanced || width < 900 * scale)
        {
            ImGui::BeginDisabled(pcui::mod_busy() || pcui::mod_recovery_needed());
            if (pcui::play_button("PLAY", ImVec2(100 * scale, 0),
                                  tight ? ImGui::GetFontSize() / scale : 30.0f))
                result.play = true;
            ui_target("Play");
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (!compact_footer)
            setting_widget(find("launcher", "skip"), "Skip launcher next time");
        else
        {
            const char *status = mode == UI_LAUNCHER || restart.empty() ? "Saved" : "Next launch";
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
            setting_widget(find("launcher", "skip"), "Skip launcher next time");
        else
        {
            const char *status = mode == UI_LAUNCHER || restart.empty() ? "Saved" : "Next launch";
            if (ImGui::GetContentRegionAvail().x >= ImGui::CalcTextSize(status).x)
                ImGui::TextDisabled("%s", status);
        }
    }
    if (mode == UI_LAUNCHER && pcui::mod_busy()) {
        result.play = false;
        ImGui::TextWrapped("%s", pcui::mod_status().c_str());
    }
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
    ImGui::End();
    return result;
}

// ---- the overlay ----------------------------------------------------------------------------------------------------

#if !defined(_WIN32)
extern "C" void host_posix_overlay_changed(int open);      // src/host_posix.c
#endif

namespace {

std::atomic<bool> g_open(false);
std::atomic<bool> g_toggle(false);
bool g_online = false;              // the overlay shows the online screens (the main menu's ONLINE)
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

// After the overlay closes the game gets its controls back only once every button and key is
// released: the A or B that closed it would otherwise reach the menu underneath (and A on ONLINE
// would open the online screens again at once).
bool g_hold_input = false;

bool controls_released()
{
    for (int slot = 0; slot < 4; slot++) {
        InputRaw raw;
        if (!xbox_HostInputRawPad(slot, &raw)) continue;
        for (int s = 0; s < INPUT_SRC_COUNT; s++)
            if (raw.src[s] >= 12000) return false;
    }
#if defined(_WIN32)
    static const int keys[] = { VK_RETURN, VK_ESCAPE, VK_SPACE, VK_BACK, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
                                'K', 'L', 'J', 'I', 'W', 'A', 'S', 'D', VK_LBUTTON };
    for (int k : keys)
        if (GetAsyncKeyState(k) & 0x8000) return false;
#endif
    return true;
}

void release_input_when_idle()
{
    if (g_hold_input && !g_open.load() && controls_released()) {
        g_hold_input = false;
        xbox_HostInputSetUiActive(0);
    }
}

// Pad buttons that open and close the overlay, from any pad.
bool pad_toggle_pressed() { return chords_pressed(g_chords); }

// The recorder's mark from a pad; checked once a frame wherever the overlay chord is.
void pad_mark_poll()
{
    if (recorder_on() && !g_mark_chords.empty() && !ui_capturing() && chords_pressed(g_mark_chords))
        recorder_mark("pad");
}

void set_open(bool open)
{
    if (g_open.load() == open) return;
    g_open = open;
    g_just_opened = open;
    if (!open) g_online = false;
    if (open) { g_hold_input = false; xbox_HostInputSetUiActive(1); }
    else g_hold_input = true;           // released in release_input_when_idle
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

// RECOMP_OVERLAY_SHOT_DIR=<folder>: test runs write the finished picture, the overlay included,
// every RECOMP_OVERLAY_SHOT_MS (default 2000) while the overlay draws, as <ms since the first>.bmp
// (<ms>-<page>.bmp while RECOMP_TEST_ONLINE_GALLERY shows the online pages).
// It reads the game's own back buffer, never the screen, so nothing else on the desktop gets in.
static void overlay_shot(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv)
{
    static const char *dir = getenv("RECOMP_OVERLAY_SHOT_DIR");
    static UiTick first, last;
    static unsigned period = 0;
    if (!dir || !*dir) return;
    if (!period) {
        const char *ms = getenv("RECOMP_OVERLAY_SHOT_MS");
        period = ms && atoi(ms) > 0 ? (unsigned)atoi(ms) : 2000;
    }
    UiTick now = ui_ticks();
    if (!first) first = now;
    if (last && now - last < period) return;
    last = now;
    ID3D11Resource *res = nullptr;
    ID3D11Texture2D *back = nullptr, *staging = nullptr;
    rtv->GetResource(&res);
    if (!res) return;
    if (FAILED(res->QueryInterface(IID_PPV_ARGS(&back)))) { res->Release(); return; }
    res->Release();
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    if (SUCCEEDED(dev->CreateTexture2D(&desc, nullptr, &staging))) {
        D3D11_MAPPED_SUBRESOURCE map;
        ctx->CopyResource(staging, back);
        if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
            char path[1024];
            const char *label = online_screen::shot_label();     // the online gallery's page
            snprintf(path, sizeof(path), "%s/%06u%s%s.bmp", dir, (unsigned)(now - first), label ? "-" : "",
                     label ? label : "");
            FILE *f = fopen(path, "wb");
            if (f) {
                unsigned w = desc.Width, h = desc.Height, row = w * 4, size = 54 + row * h;
                unsigned char header[54] = { 'B', 'M' };
                memcpy(header + 2, &size, 4);
                header[10] = 54; header[14] = 40;
                memcpy(header + 18, &w, 4);
                int height = -(int)h;
                memcpy(header + 22, &height, 4);
                header[26] = 1; header[28] = 32;
                fwrite(header, 1, 54, f);
                std::vector<unsigned char> line(row);
                for (unsigned y = 0; y < h; y++) {
                    const unsigned char *src = (const unsigned char *)map.pData + (size_t)y * map.RowPitch;
                    for (unsigned x = 0; x < w; x++) {
                        line[x * 4 + 0] = src[x * 4 + (bgra ? 0 : 2)];
                        line[x * 4 + 1] = src[x * 4 + 1];
                        line[x * 4 + 2] = src[x * 4 + (bgra ? 2 : 0)];
                        line[x * 4 + 3] = 255;
                    }
                    fwrite(line.data(), 1, row, f);
                }
                fclose(f);
            }
            ctx->Unmap(staging, 0);
        }
        staging->Release();
    }
    back->Release();
}
#endif

extern "C" int pc_ui_overlay_open(void) { return g_open.load() ? 1 : 0; }

extern "C" void pc_ui_apply_settings(void)
{
    ui_lock();
    std::string key = get_text("ui", "overlay_key");
    int vk = xbox_InputMap_KeyFromName(key.c_str());
    g_overlay_vk = vk > 0 ? vk : VK_F1;
    parse_chords();
    if (g_ready) ui_apply_scale(g_window);
    ui_unlock();
}

#if defined(_WIN32)
extern "C" int pc_ui_window_message(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
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

// A newer build published while the game runs: say so in the menus. The launcher checks only at its
// start, and players stay in the game for hours, so hotfixes went unseen. Checked a minute after start
// and every five minutes; shown for fifteen seconds when found and again every ten minutes, never over a
// fight. The online pages keep a badge up meanwhile (online_screen.cpp).
static bool update_notice(char *out, size_t size)
{
    static UiTick next_check, until, again;
    static int shown_build;
    UiTick now = ui_ticks();
    if (!next_check) next_check = now + 60000;
    if (now >= next_check) {
        next_check = now + 5 * 60000;
        UpdaterState st = updater_state();
        if (st != UPDATER_AVAILABLE && st != UPDATER_DOWNLOADING && st != UPDATER_INSTALLING && st != UPDATER_INSTALLED)
            updater_check();
    }
    if (updater_state() == UPDATER_AVAILABLE && !netplay_in_fight()) {
        int build = 0;
        char version[64], notes[8];
        updater_info(&build, version, sizeof(version), notes, sizeof(notes));
        if (build > updater_local_build() && (build != shown_build || now >= again)) {
            shown_build = build;
            until = now + 15000;
            again = now + 10 * 60000;
        }
    }
    if (!until || now >= until) return false;
    snprintf(out, size, "Build %d is out: close the game and the launcher installs it.", shown_build);
    return true;
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
    if (!ui_capturing() && !g_chords.empty() && pad_toggle_pressed()) toggled = true;
    if (toggled) set_open(!g_open.load());
    release_input_when_idle();
    if (online_screen::take_open_request()) {      // ONLINE chosen in the game's main menu
        set_open(true);
        g_online = true;
    }
    // Closed, the overlay still draws the versus screen while a room's match loads (it takes no
    // input), and the ONLINE entry's art on the main menu.
    bool versus = !g_open.load() && online_screen::versus_wanted();
    bool decor = !g_open.load() && !versus && online_screen::decor_wanted();
    char notice[160];
    // F9's notice shows over everything, the online screens and the settings included (pressed in
    // the online menu, it was hidden there); the netplay notices are for the fight only.
    bool toast = bug_report_toast(notice, sizeof(notice)) ||
                 (!g_open.load() && netplay_toast(notice, sizeof(notice))) ||  // online: a wait, a desync
                 update_notice(notice, sizeof(notice));                         // a newer build is out
    char talk[160];
    bool talking = voice_overlay(talk, sizeof(talk)) != 0;               // voice chat: who talks
    if (!g_open.load() && !decor && !versus && !toast && !talking) { ui_unlock(); return; }

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
    online_screen::prepare(dev);
    if (!decor && !versus) ui_feed_gamepad();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    bool close_request = false;
    if (versus) {
        online_screen::draw_versus(ImGui::GetForegroundDrawList(), io.DisplaySize);
    } else if (decor) {
        online_screen::draw_decor(ImGui::GetForegroundDrawList(), io.DisplaySize);
    } else if (g_online) {
        if (!online_screen::draw(io.DisplaySize)) close_request = true;
    } else if (g_open.load()) {
    // Dim the game and put the settings in a panel in the middle.
    if (g_just_opened) {
        g_just_opened = false;
        ImGui::SetNextWindowFocus();
    }
    ImDrawList *bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
    float pw = io.DisplaySize.x * 0.82f, ph = io.DisplaySize.y * 0.86f;
    if (pw < 560) pw = io.DisplaySize.x;
    if (ph < 420) ph = io.DisplaySize.y;
    {
        ImGui::SetNextWindowBgAlpha(0.96f);
        UiResult r = ui_draw(UI_OVERLAY, (io.DisplaySize.x - pw) / 2, (io.DisplaySize.y - ph) / 2, pw, ph);
        close_request = r.leave;
    }
    // Escape or the pad's cancel button closes the overlay, unless a popup or a capture is open.
    if (!ui_capturing() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
        close_request = true;
    }
    if (talking) {  // small, bottom left, over everything
        ImDrawList *fg = ImGui::GetForegroundDrawList();
        ImFont *font = g_font_bold ? g_font_bold : ImGui::GetFont();
        float size = std::max(15.0f, io.DisplaySize.y * 0.022f);
        ImVec2 t = font->CalcTextSizeA(size, FLT_MAX, 0, talk);
        ImVec2 at(size, io.DisplaySize.y - t.y - size * 1.2f);
        fg->AddRectFilled(ImVec2(at.x - size * 0.5f, at.y - size * 0.3f), ImVec2(at.x + t.x + size * 0.5f, at.y + t.y + size * 0.3f),
                          IM_COL32(0, 0, 0, 170), size * 0.3f);
        fg->AddText(font, size, at, IM_COL32(120, 220, 120, 255), talk);
    }
    if (toast) {    // last, so no screen drawn this frame covers it
        ImDrawList *fg = ImGui::GetForegroundDrawList();
        ImFont *font = g_font_bold ? g_font_bold : ImGui::GetFont();
        float size = std::max(18.0f, io.DisplaySize.y * 0.028f);
        ImVec2 t = font->CalcTextSizeA(size, FLT_MAX, 0, notice);
        ImVec2 at((io.DisplaySize.x - t.x) * 0.5f, io.DisplaySize.y * 0.16f);
        fg->AddRectFilled(ImVec2(at.x - size, at.y - size * 0.5f), ImVec2(at.x + t.x + size, at.y + t.y + size * 0.5f),
                          IM_COL32(0, 0, 0, 200), size * 0.3f);
        fg->AddText(font, size, at, IM_COL32(255, 168, 26, 255), notice);
    }

    ImGui::Render();
    ID3D11RenderTargetView *old_rtv = nullptr;
    ID3D11DepthStencilView *old_dsv = nullptr;
    ctx->OMGetRenderTargets(1, &old_rtv, &old_dsv);
    ID3D11RenderTargetView *rtv = (ID3D11RenderTargetView *)window_rtv;
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    overlay_shot(dev, ctx, rtv);
    ctx->OMSetRenderTargets(1, &old_rtv, old_dsv);       // the title's own target, as it was
    if (old_rtv) old_rtv->Release();
    if (old_dsv) old_dsv->Release();
    (void)width;
    if (close_request) {
        bool for_match = g_online && online_screen::closed_for_match();
        set_open(false);
        // Closed by a match starting, not by a press: the fight's own input is not to be held
        // (MATCH FOUND can run out while the match is loading and the player already holds a move).
        if (for_match) {
            g_hold_input = false;
            xbox_HostInputSetUiActive(0);
            fprintf(stderr, "[UI] closed for the match: game input back at once\n");
        }
    }
    ui_unlock();
}
#endif

#if !defined(_WIN32)
// ---- off Windows: SDL events, and the overlay through the Vulkan present --------------------------------------------

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
    if (!ui_capturing() && !g_chords.empty() && pad_toggle_pressed()) toggled = true;
    if (toggled) set_open(!g_open.load());
    release_input_when_idle();
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
#endif
