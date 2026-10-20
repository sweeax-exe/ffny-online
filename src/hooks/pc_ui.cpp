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

#include "imgui.h"
#if defined(_WIN32)
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#else
#include "imgui_impl_vulkan.h"
#endif

extern "C" {
#include "recomp_settings.h"
#include "input_host.h"
#include "input_map.h"
}
#include "pc_input.h"
#include "pc_settings.h"
#include "pc_ui.h"
#include "pc_ui_internal.h"
#include "netplay.h"
#include "account.h"
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

static void commit() { pc_settings_save(); }

static const char *k_control_labels[INPUT_CONTROL_COUNT] = {
    "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start", "Back",
    "Left stick click", "Right stick click", "A", "B", "X", "Y", "Black", "White",
    "Left trigger", "Right trigger",
    "Left stick up", "Left stick down", "Left stick left", "Left stick right",
    "Right stick up", "Right stick down", "Right stick left", "Right stick right",
};

static const char *friendly(const char *section, const char *key)
{
    struct Label { const char *section, *key, *text; };
    static const Label labels[] = {
        { "display", "fullscreen", "Full screen" }, { "display", "window_width", "Window width" },
        { "display", "window_height", "Window height" }, { "display", "aspect", "Picture shape" },
        { "display", "render_scale", "Render scale" }, { "display", "filter", "Scaling filter" },
        { "display", "vsync", "Vsync" }, { "display", "gamma", "Game brightness curve" },
        { "display", "widescreen", "Widescreen 16:9 (experimental)" },
        { "display", "anti_aliasing", "Anti-aliasing" }, { "display", "sharpen", "Sharpening" },
        { "display", "anisotropy", "Anisotropic filtering" },
        { "textures", "enabled", "Use HD texture packs" }, { "textures", "packs", "Pack folders" },
        { "textures", "cache_mb", "Texture memory budget (MiB)" }, { "textures", "dump", "Capture source textures" },
        { "textures", "dump_limit", "Maximum new captures" },
        { "input", "keyboard_player", "Keyboard and mouse play as" }, { "input", "players", "Controllers at start" },
        { "input", "backend", "Pad backend" }, { "input", "rumble", "Rumble strength" },
        { "input", "rumble_floor", "Weakest rumble pulse" }, { "input", "rumble_min_ms", "Shortest rumble pulse (ms)" },
        { "input", "rumble_on_connect", "Buzz a pad when it connects" },
        { "input", "hide_cursor", "Hide the pointer when idle" }, { "input", "confine_cursor", "Keep the pointer in the window (full screen)" },
        { "gamepad", "left_deadzone", "Left stick dead zone" }, { "gamepad", "right_deadzone", "Right stick dead zone" },
        { "gamepad", "trigger_threshold", "Trigger threshold" }, { "gamepad", "button_threshold", "Button threshold" },
        { "gamepad", "left_stick", "Left stick comes from" }, { "gamepad", "right_stick", "Right stick comes from" },
        { "launcher", "skip", "Skip the launcher at start" }, { "ui", "scale", "Text and control size" },
        { "online", "server", "Server" }, { "online", "name", "Your name" },
        { "online", "input_delay", "Input delay (steps)" },
        { "ui", "overlay_pad", "Overlay pad buttons" }, { "ui", "overlay_key", "Overlay key" },
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
        if (s->flags & RECOMP_SETTING_RESTART)
            g_start_values.push_back({ s->section, s->key, value_text(s) });
    }
}

static std::string restart_needed()
{
    std::string out;
    for (const StartValue &v : g_start_values) {
        RecompSetting *s = find(v.section.c_str(), v.key.c_str());
        if (s && value_text(s) != v.text) {
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
    const char *raw_name = input_key_name(vk);
    if (!raw_name) return;
    std::string name = raw_name;
    if (name.size() > 1) name[0] = (char)toupper((unsigned char)name[0]);     // "space" -> "Space"
    std::string text = name;
    if (g_capture.append) {
        std::string old = get_text(g_capture.section.c_str(), g_capture.key.c_str());
        InputBindings b;
        memset(&b, 0, sizeof(b));
        input_bindings_parse(&b, 0, old.c_str());
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
        if (armed) capture_commit_key(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? INPUT_KEY_WHEEL_UP : INPUT_KEY_WHEEL_DOWN);
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
            capture_commit_key(e->wheel.y > 0 ? INPUT_KEY_WHEEL_UP : INPUT_KEY_WHEEL_DOWN);
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
                recomp_settings_set_text(g_capture.section.c_str(), g_capture.key.c_str(), input_source_name(s));
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
    style.ScaleAllSizes(s);
    style.FontScaleMain = s;
}

void ui_context_begin(UiWindow window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
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

// A bool, integer, choice or text setting as its natural control. Returns true when it changed.
static bool setting_widget(RecompSetting *s, const char *label = nullptr)
{
    if (!label) label = friendly(s->section, s->key);
    bool locked = s->source == RECOMP_SETTING_FROM_ENV;
    bool changed = false;
    ImGui::PushID(s);
    if (locked) ImGui::BeginDisabled();
    switch (s->type) {
    case RECOMP_SETTING_BOOL: {
        bool v = s->stored != 0;
        if (ImGui::Checkbox(label, &v)) { recomp_settings_set(s->section, s->key, v ? 1 : 0); changed = true; }
        break;
    }
    case RECOMP_SETTING_INT: {
        int v = s->stored;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        if (s->max - s->min > 2000) {
            if (ImGui::InputInt(label, &v, 10, 100)) { recomp_settings_set(s->section, s->key, v); changed = true; }
        } else {
            if (ImGui::SliderInt(label, &v, s->min, s->max)) { recomp_settings_set(s->section, s->key, v); changed = true; }
        }
        break;
    }
    case RECOMP_SETTING_ENUM: {
        int v = s->stored;
        const char *current = s->choices[v];
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        if (ImGui::BeginCombo(label, current)) {
            for (int i = 0; s->choices[i]; i++)
                if (ImGui::Selectable(s->choices[i], i == v)) { recomp_settings_set(s->section, s->key, i); changed = true; }
            ImGui::EndCombo();
        }
        break;
    }
    case RECOMP_SETTING_STRING: {
        char buf[RECOMP_SETTING_TEXT_MAX];
        snprintf(buf, sizeof(buf), "%s", s->stored_text);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
        if (ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            recomp_settings_set_text(s->section, s->key, buf);
            changed = true;
        }
        break;
    }
    }
    if (locked) ImGui::EndDisabled();
    if (s->help) ImGui::SetItemTooltip("%s", s->help);
    if (locked) ImGui::SetItemTooltip("Set by an environment variable for this run.");
    if (s->flags & RECOMP_SETTING_RESTART) {
        ImGui::SameLine();
        ImGui::TextDisabled("(restart)");
        ImGui::SetItemTooltip("Takes effect the next time the game starts.");
    }
    ImGui::PopID();
    if (changed) commit();
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
    settings_group("display", { "fullscreen" });
    window_size_widget();
    settings_group("display", { "widescreen", "aspect", "filter", "vsync" });
    ImGui::SeparatorText("Quality");
    settings_group("display", { "render_scale", "anisotropy", "anti_aliasing", "sharpen", "gamma" });
    ImGui::TextWrapped("Full screen, window size, picture shape, filter, vsync, anti-aliasing, sharpening and "
                       "anisotropic filtering apply as you change them. "
                       "Render scale and the brightness curve apply the next time the game starts. "
                       "F11 or Alt+Enter switches full screen while playing.");
}

// Make the HD packs here when setup did not: the same local upscale, with a bar, and turn them on.
static void hd_build_section()
{
    int done = 0, total = 0;
    char text[256], packs[256];
    static bool applied;
    HdBuildState state = hd_build_status(&done, &total, text, sizeof(text), packs, sizeof(packs));
    ImGui::SeparatorText("Make HD textures");
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

static void page_textures()
{
    hd_build_section();
    ImGui::SeparatorText("HD texture packs");
    settings_group("textures", { "enabled", "packs", "cache_mb" });
    ImGui::TextWrapped("Install packs under mods in the data folder. Enter folder names separated by semicolons; "
                       "the last pack wins when more than one replaces the same image. Changes apply after a restart.");
    ImGui::SeparatorText("Making a pack");
    settings_group("textures", { "dump", "dump_limit" });
    ImGui::TextWrapped("Captures are saved under hd-work/runtime in the data folder. Turn capture off when finished.");
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
                InputPad pad;
                input_pad_map(&map, &raw, &pad);
                ImGui::Indent();
                draw_stick("l", pad.lx / 32767.0f, pad.ly / 32767.0f, ImGui::GetFontSize() * 1.6f);
                ImGui::SameLine();
                draw_stick("r", pad.rx / 32767.0f, pad.ry / 32767.0f, ImGui::GetFontSize() * 1.6f);
                ImGui::SameLine();
                static const char *btn[8] = { "A", "B", "X", "Y", "Black", "White", "LT", "RT" };
                ImGui::BeginGroup();
                std::string pressed;
                for (int i = 0; i < 8; i++)
                    if (pad.analog[i] > 30) { pressed += btn[i]; pressed += ' '; }
                static const char *dig[8] = { "Up", "Down", "Left", "Right", "Start", "Back", "LS", "RS" };
                for (int i = 0; i < 8; i++)
                    if (pad.buttons & (1 << i)) { pressed += dig[i]; pressed += ' '; }
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
    settings_group("input", { "keyboard_player", "players", "backend" });
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
        for (int i = 0; i < INPUT_PAD_CONTROL_COUNT; i++) recomp_settings_reset_to_default("gamepad", input_control_name(i));
        commit();
    }
    ImGui::Spacing();
    if (ImGui::BeginTable("padmap", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Xbox control", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Pad button", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for (int i = 0; i < INPUT_PAD_CONTROL_COUNT; i++) {
            const char *key = input_control_name(i);
            RecompSetting *s = find("gamepad", key);
            if (!s) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(k_control_labels[i]);
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            int cur = input_source_from_name(s->stored_text);
            if (cur < 0) cur = 0;
            bool detecting = g_capture.kind == Capture::PAD && g_capture.key == key;
            ImGui::SetNextItemWidth(-1);
            if (detecting) {
                ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1), "Press a button on your pad...");
            } else if (ImGui::BeginCombo("##src", input_source_name(cur))) {
                for (int n = 0; n < INPUT_SRC_COUNT; n++)
                    if (ImGui::Selectable(input_source_name(n), n == cur)) {
                        recomp_settings_set_text("gamepad", key, input_source_name(n));
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
        for (int i = 0; i < INPUT_CONTROL_COUNT; i++) recomp_settings_reset_to_default("keyboard", input_control_name(i));
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
            const char *key = input_control_name(i);
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

#if defined(_WIN32)
static const char *k_show_file_label = "Show settings.ini in Explorer";

static void open_folder()
{
    char path[MAX_PATH * 2];
    snprintf(path, sizeof(path), "/select,\"%s\"", pc_settings_path());
    ShellExecuteA(nullptr, "open", "explorer.exe", path, nullptr, SW_SHOWNORMAL);
}
#else
extern char **environ;
#if defined(__APPLE__)
static const char *k_show_file_label = "Show settings.ini in Finder";
#else
static const char *k_show_file_label = "Show the settings folder";
#endif

// Finder selects the file (`open -R`); elsewhere the file manager opens its folder (`xdg-open`).
// No shell is involved, so a path with spaces needs no quoting.
static void open_folder()
{
    const char *path = pc_settings_path();
    if (!path || !*path) return;
    pid_t pid;
    int err;
#if defined(__APPLE__)
    char *argv[] = { (char *)"open", (char *)"-R", (char *)path, nullptr };
    err = posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, argv, environ);
#else
    std::string folder = path;
    size_t slash = folder.find_last_of('/');
    folder = slash == std::string::npos ? "." : folder.substr(0, slash ? slash : 1);
    char *argv[] = { (char *)"xdg-open", (char *)folder.c_str(), nullptr };
    err = posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ);
#endif
    if (err != 0)
        fprintf(stderr, "[UI] could not open the settings file's folder\n");
}
#endif

// The optional FFNY Online account: sign-in with Discord (src/hooks/account.c).
static void page_account()
{
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
    ImGui::SeparatorText("How it works");
    ImGui::TextWrapped("Everyone needs their own copy of the game and this same version. In a fight the games run in "
                       "lockstep through the relay server, so it is the same fight on every screen.");
}

static void page_general(UiMode mode)
{
    ImGui::SeparatorText("Start-up");
    settings_group("launcher", { "skip" });
    if (mode == UI_OVERLAY)
        ImGui::TextWrapped("With the launcher skipped, hold Shift while the game starts, or start it with --launcher, to see it again.");
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
        char message[256];
        UpdaterState state = updater_state();
        updater_message(message, sizeof(message));
        ImGui::Text("This is build %d.", updater_local_build());
        if (message[0]) { ImGui::SameLine(); ImGui::TextDisabled("%s", message); }
        if (state != UPDATER_CHECKING && state != UPDATER_DOWNLOADING && state != UPDATER_STARTED) {
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

// ---- the launcher's own layout ------------------------------------------------------------------------
//
// A banner (the title screen's skyline and the logo, when the front end could load them), the pages
// in a sidebar, and a footer with a large Play. The overlay keeps the compact tabbed screen below.

struct LauncherArt { ImTextureID tex; float w, h; };
static LauncherArt g_banner_art, g_logo_art;

void ui_set_launcher_art(ImTextureID banner, float banner_w, float banner_h,
                         ImTextureID logo, float logo_w, float logo_h)
{
    g_banner_art = { banner, banner_w, banner_h };
    g_logo_art = { logo, logo_w, logo_h };
}

static ImU32 col(const ImVec4 &v, float alpha = 1.0f) { return ImGui::GetColorU32(ImVec4(v.x, v.y, v.z, v.w * alpha)); }

// An image drawn to fill the rectangle, cropped to keep its shape.
static void draw_cover(ImDrawList *dl, const LauncherArt &art, ImVec2 a, ImVec2 b)
{
    float rw = b.x - a.x, rh = b.y - a.y;
    if (!art.tex || art.w <= 0 || art.h <= 0 || rw <= 0 || rh <= 0) return;
    float img = art.w / art.h, box = rw / rh;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (img > box) { float f = box / img; uv0.x = (1 - f) / 2; uv1.x = 1 - uv0.x; }       // crop the sides
    else           { float f = img / box; uv0.y = (1 - f) * 0.35f; uv1.y = uv0.y + f; }   // crop mostly below
    dl->AddImage(art.tex, a, b, uv0, uv1);
}

enum { PAGE_DISPLAY, PAGE_TEXTURES, PAGE_CONTROLLERS, PAGE_PAD, PAGE_KEYBOARD, PAGE_ONLINE, PAGE_GENERAL, PAGE_COUNT };
static const char *const k_page_names[PAGE_COUNT] = {
    "Display", "Textures", "Controllers", "Pad buttons", "Keyboard & mouse", "Online", "General" };
static const char *const k_page_hints[PAGE_COUNT] = {
    "Window, picture shape, resolution and image quality",
    "HD texture packs and making your own",
    "Who plays with which pad, rumble and dead zones",
    "Remap every button of a gamepad",
    "Keys and mouse buttons for the keyboard player",
    "Quick match, rooms and venue votes over the internet",
    "Start-up, the in-game overlay and files" };

static void draw_page(int page, UiMode mode)
{
    switch (page) {
    case PAGE_ONLINE: page_online(mode); break;
    case PAGE_DISPLAY: page_display(); break;
    case PAGE_TEXTURES: page_textures(); break;
    case PAGE_CONTROLLERS: page_controllers(); break;
    case PAGE_PAD: page_pad_buttons(); break;
    case PAGE_KEYBOARD: page_keyboard(); break;
    default: page_general(mode); break;
    }
}

// The update offer: a modal over the launcher when a newer build is signed and published.
static void update_popup(UiResult &result)
{
    static bool asked_once, dismissed;
    UpdaterState state = updater_state();
    char message[256];
    updater_message(message, sizeof(message));
    if (state == UPDATER_AVAILABLE && !asked_once && !dismissed) {
        ImGui::OpenPopup("Update available");
        asked_once = true;
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30, 0));
    if (!ImGui::BeginPopupModal("Update available", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;
    int build = 0;
    char version[64], notes[1024];
    updater_info(&build, version, sizeof(version), notes, sizeof(notes));
    ImGui::PushFont(g_font_black, ImGui::GetStyle().FontSizeBase * 1.4f);
    ImGui::TextColored(k_gold, "Version %s", version);
    ImGui::PopFont();
    ImGui::TextDisabled("Build %d - you have build %d", build, updater_local_build());
    if (notes[0]) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 29);
        ImGui::TextUnformatted(notes);
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    if (state == UPDATER_DOWNLOADING || state == UPDATER_INSTALLING) {
        char overlay[48];
        snprintf(overlay, sizeof(overlay), "%s %d%%", state == UPDATER_DOWNLOADING ? "Download" : "Installing",
                 (int)(updater_progress() * 100.0 + 0.5));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, k_red);
        ImGui::ProgressBar((float)updater_progress(), ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 29);
        ImGui::TextUnformatted(message);
        ImGui::PopTextWrapPos();
        ImGui::TextDisabled("You can keep this window open; the game stays as it is until you restart.");
    } else if (state == UPDATER_INSTALLED) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1), "%s", message);
        ImGui::PushStyleColor(ImGuiCol_Button, k_red);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
        if (ImGui::Button("Restart now", ImVec2(ImGui::GetFontSize() * 10, 0))) {
            updater_restart();
            result.leave = true;        // this copy closes; the helper starts the new launcher
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(ImGui::GetFontSize() * 8, 0))) { dismissed = true; ImGui::CloseCurrentPopup(); }
    } else if (state == UPDATER_STARTED) {
        ImGui::TextWrapped("%s", message);
        result.leave = true;            // the setup takes over; it waits for this window to close
    } else if (!updater_can_install()) {
        ImGui::TextWrapped("This copy was built from a source checkout, not installed by setup: get the new version "
                           "from the source and build it again.");
        if (ImGui::Button("OK", ImVec2(ImGui::GetFontSize() * 8, 0))) { dismissed = true; ImGui::CloseCurrentPopup(); }
    } else {
        if (state == UPDATER_FAILED) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1), "%s", message);
        ImGui::TextWrapped("The update downloads (about 55 MB), is checked, and is built from your installed game "
                           "files right here, usually in a few minutes. Saves, settings and HD textures stay.");
        ImGui::PushStyleColor(ImGuiCol_Button, k_red);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
        if (ImGui::Button("Install now", ImVec2(ImGui::GetFontSize() * 10, 0))) updater_install();
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(ImGui::GetFontSize() * 8, 0))) { dismissed = true; ImGui::CloseCurrentPopup(); }
    }
    ImGui::EndPopup();
}

// RECOMP_LAUNCHER_PAGE=online (or another page's name): open on that page, for screenshots.
static int initial_page()
{
    static const char *const names[] = { "display", "textures", "controllers", "pad", "keyboard", "online", "general" };
    const char *want = getenv("RECOMP_LAUNCHER_PAGE");
    for (int i = 0; want && i < PAGE_COUNT; i++)
        if (!strcmp(want, names[i])) return i;
    return PAGE_DISPLAY;
}

static UiResult draw_launcher(float x, float y, float width, float height)
{
    UiResult result;
    static int page = initial_page();
    static bool checked;
    if (!checked) { checked = true; updater_check(); }
    const float u = ImGui::GetFontSize() / 17.0f;           // one design pixel at the current scale
    ImGuiStyle &style = ImGui::GetStyle();

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

    // Banner.
    float banner_h = height * 0.27f;
    if (banner_h < 130 * u) banner_h = 130 * u;
    if (banner_h > 260 * u) banner_h = 260 * u;
    ImVec2 ba = origin, bb(origin.x + width, origin.y + banner_h);
    if (g_banner_art.tex)
        draw_cover(dl, g_banner_art, ba, bb);
    else
        dl->AddRectFilledMultiColor(ba, bb, col(k_red_lo), col(k_bg), col(k_bg), col(k_red_lo, 0.6f));
    // Fade into the page below and darken the left, where the logo sits.
    dl->AddRectFilledMultiColor(ImVec2(ba.x, ba.y + banner_h * 0.45f), bb, col(k_bg, 0), col(k_bg, 0), col(k_bg), col(k_bg));
    dl->AddRectFilledMultiColor(ba, ImVec2(ba.x + width * 0.6f, bb.y), col(k_bg, 0.75f), col(k_bg, 0), col(k_bg, 0), col(k_bg, 0.75f));
    float pad = 28 * u;
    if (g_logo_art.tex) {
        float lh = banner_h * 0.52f, lw = lh * g_logo_art.w / g_logo_art.h;
        if (lw > width * 0.55f) { lw = width * 0.55f; lh = lw * g_logo_art.h / g_logo_art.w; }
        ImVec2 la(ba.x + pad, ba.y + (banner_h - lh) * 0.42f);
        dl->AddImage(g_logo_art.tex, la, ImVec2(la.x + lw, la.y + lh));
    } else {
        ImGui::PushFont(g_font_black, style.FontSizeBase * 2.4f);
        dl->AddText(ImVec2(ba.x + pad, ba.y + banner_h * 0.3f), col(k_gold), "DEF JAM: FIGHT FOR NY");
        ImGui::PopFont();
    }
    {
        char version[96];
        snprintf(version, sizeof(version), "PC RECOMP  v%s  build %d", DEFJAM_VERSION, updater_local_build());
        ImGui::PushFont(g_font_bold, style.FontSizeBase * 0.85f);
        ImVec2 ts = ImGui::CalcTextSize(version);
        dl->AddText(ImVec2(bb.x - ts.x - pad, ba.y + 14 * u), col(ImVec4(1, 1, 1, 0.55f)), version);
        ImGui::PopFont();
    }
    dl->AddRectFilled(ImVec2(ba.x, bb.y - 3 * u), bb, col(k_red));

    // Sidebar, page and footer.
    float footer_h = 74 * u, side_w = 230 * u;
    float body_top = banner_h, body_h = height - banner_h - footer_h;
    dl->AddRectFilled(ImVec2(origin.x, origin.y + body_top), ImVec2(origin.x + side_w, origin.y + body_top + body_h), col(k_sidebar));

    ImGui::SetCursorPos(ImVec2(0, body_top));
    ImGui::BeginChild("##sidebar", ImVec2(side_w, body_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy(ImVec2(0, 14 * u));
    for (int i = 0; i < PAGE_COUNT; i++) {
        ImVec2 a = ImGui::GetCursorScreenPos();
        float item_h = 40 * u;
        ImGui::PushID(i);
        bool chosen = ImGui::InvisibleButton("##page", ImVec2(side_w, item_h));
        bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        ImGui::PopID();
        if (chosen) page = i;
        ImDrawList *sd = ImGui::GetWindowDrawList();
        ImVec2 b(a.x + side_w, a.y + item_h);
        if (page == i) {
            sd->AddRectFilledMultiColor(a, b, col(k_red, 0.38f), col(k_red, 0.0f), col(k_red, 0.0f), col(k_red, 0.38f));
            sd->AddRectFilled(a, ImVec2(a.x + 4 * u, b.y), col(k_red_hi));
        } else if (hovered) {
            sd->AddRectFilled(a, b, col(ImVec4(1, 1, 1, 0.05f)));
        }
        ImGui::PushFont(page == i ? g_font_bold : nullptr, style.FontSizeBase * 1.05f);
        float th = ImGui::GetFontSize();
        sd->AddText(ImVec2(a.x + 24 * u, a.y + (item_h - th) / 2), page == i ? col(ImVec4(1, 1, 1, 1)) :
                    col(style.Colors[ImGuiCol_Text], 0.72f), k_page_names[i]);
        ImGui::PopFont();
    }
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(side_w, body_top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30 * u, 22 * u));
    ImGui::BeginChild("##page", ImVec2(width - side_w, body_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushFont(g_font_black, style.FontSizeBase * 1.7f);
    ImGui::TextUnformatted(k_page_names[page]);
    ImGui::PopFont();
    ImGui::TextDisabled("%s", k_page_hints[page]);
    ImGui::Dummy(ImVec2(0, 6 * u));
    draw_page(page, UI_LAUNCHER);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Footer.
    float fy = origin.y + body_top + body_h;
    dl->AddRectFilled(ImVec2(origin.x, fy), ImVec2(origin.x + width, origin.y + height), col(k_panel));
    dl->AddLine(ImVec2(origin.x, fy), ImVec2(origin.x + width, fy), col(ImVec4(1, 1, 1, 0.06f)));
    float play_w = 190 * u, play_h = 46 * u, quit_w = 110 * u;
    float row = body_top + body_h + (footer_h - play_h) / 2;
    ImGui::SetCursorPos(ImVec2(pad, body_top + body_h + 12 * u));
    {
        std::string restart = restart_needed();
        if (!restart.empty())
            ImGui::TextColored(k_gold, "Applies at the next start: %s", restart.c_str());
        else
            ImGui::TextDisabled("Changes are saved as you make them.");
        ImGui::SetCursorPosX(pad);
        RecompSetting *skip = find("launcher", "skip");
        if (skip) setting_widget(skip);
    }
    ImGui::SetCursorPos(ImVec2(width - pad - play_w - quit_w - 12 * u, row + (play_h - 36 * u) / 2));
    if (ImGui::Button("Quit", ImVec2(quit_w, 36 * u))) result.leave = true;
    ImGui::SetCursorPos(ImVec2(width - pad - play_w, row));
    ImGui::PushStyleColor(ImGuiCol_Button, k_red);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, k_red_lo);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8 * u);
    ImGui::PushFont(g_font_black, style.FontSizeBase * 1.35f);
    UpdaterState upd = updater_state();
    bool making_hd = hd_build_status(nullptr, nullptr, nullptr, 0, nullptr, 0) == HD_BUILD_RUNNING ||
                     upd == UPDATER_DOWNLOADING || upd == UPDATER_INSTALLING;
    if (making_hd) ImGui::BeginDisabled();
    if (ImGui::Button("PLAY", ImVec2(play_w, play_h))) result.play = true;
    if (making_hd) ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        dl->AddRect(a, b, col(k_gold, ImGui::IsItemHovered() ? 0.9f : 0.45f), 8 * u, 0, 2 * u);
    }
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
    update_popup(result);
    ImGui::End();
    return result;
}

UiResult ui_draw(UiMode mode, float x, float y, float width, float height)
{
    UiResult result;
    capture_poll_pad();
    if (mode == UI_LAUNCHER)
        return draw_launcher(x, y, width, height);

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##settings", nullptr, flags);

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.22f, 1.0f), "DEF JAM: FIGHT FOR NY");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled(mode == UI_LAUNCHER ? "Recompiled - launcher" : "Recompiled - settings");
    ImGui::Separator();

    float footer = ImGui::GetFrameHeightWithSpacing() * 2.2f + ImGui::GetStyle().ItemSpacing.y * 2;
    ImGui::BeginChild("page", ImVec2(0, -footer), ImGuiChildFlags_None);
    if (ImGui::BeginTabBar("pages")) {
        if (ImGui::BeginTabItem("Display")) { page_display(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Textures")) { page_textures(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Controllers")) { page_controllers(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Pad buttons")) { page_pad_buttons(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Keyboard & mouse")) { page_keyboard(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Online")) { page_online(mode); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("General")) { page_general(mode); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::Separator();
    std::string restart = restart_needed();
    if (!restart.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Changes that need a restart to apply: %s", restart.c_str());
    else
        ImGui::TextDisabled("Changes are saved as you make them.");

    float bw = ImGui::GetFontSize() * 9;
    if (mode == UI_LAUNCHER) {
        RecompSetting *skip = find("launcher", "skip");
        if (skip) setting_widget(skip);
        ImGui::SameLine(ImGui::GetWindowWidth() - bw * 2 - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("Quit", ImVec2(bw, 0))) result.leave = true;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.7f, 0.3f, 1.0f));
        if (ImGui::Button("Play", ImVec2(bw, 0))) result.play = true;
        ImGui::PopStyleColor(2);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
    } else {
        RecompSetting *skip = find("launcher", "skip");
        if (skip) setting_widget(skip);
        ImGui::SameLine(ImGui::GetWindowWidth() - bw - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("Close", ImVec2(bw, 0))) result.leave = true;
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

void parse_chords()
{
    g_chords.clear();
    std::string text = get_text("ui", "overlay_pad");
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
            int src = input_source_from_name(name.substr(a, b - a + 1).c_str());
            if (src > INPUT_SRC_NONE) c.src[c.n++] = src;
        }
        if (c.n) g_chords.push_back(c);
    }
}

// Pad buttons that open and close the overlay, from any pad.
bool pad_toggle_pressed()
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
    for (Chord &c : g_chords) {
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
}

} // namespace

#if defined(_WIN32)
extern "C" void pc_ui_set_window(HWND window) { g_window = window; }

// RECOMP_OVERLAY_SHOT_DIR=<folder>: test runs write the finished picture, the overlay included,
// every RECOMP_OVERLAY_SHOT_MS (default 2000) while the overlay draws, as <ms since the first>.bmp.
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
            snprintf(path, sizeof(path), "%s/%06u.bmp", dir, (unsigned)(now - first));
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
    int vk = input_key_from_name(key.c_str());
    g_overlay_vk = vk > 0 ? vk : VK_F1;
    parse_chords();
    if (g_ready) ui_apply_scale(g_window);
    ui_unlock();
}

#if defined(_WIN32)
extern "C" int pc_ui_window_message(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
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
    if (!ui_capturing() && !g_chords.empty() && pad_toggle_pressed()) toggled = true;
    if (toggled) set_open(!g_open.load());
    release_input_when_idle();
    if (online_screen::take_open_request()) {      // ONLINE chosen in the game's main menu
        set_open(true);
        g_online = true;
    }
    // Closed, the overlay still draws the ONLINE entry's art on the main menu.
    bool decor = !g_open.load() && online_screen::decor_wanted();
    if (!g_open.load() && !decor) { ui_unlock(); return; }

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
    if (!decor) ui_feed_gamepad();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    bool close_request = false;
    if (decor) {
        online_screen::draw_decor(ImGui::GetForegroundDrawList(), io.DisplaySize);
    } else if (g_online) {
        if (!online_screen::draw(io.DisplaySize)) close_request = true;
    } else {
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
