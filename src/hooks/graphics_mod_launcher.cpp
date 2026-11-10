// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// "Enhanced graphics (community)", the launcher's part: the background work and what the card shows,
// the settings, the re-install after an update, the GPU check and the log header line. See graphics_mod.h.
#define _CRT_SECURE_NO_WARNINGS
#include "graphics_mod.h"

#include <cstdio>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

#include "pc_ui_actions.h"
#include "pc_launcher_saves.h"
extern "C" {
#include "recomp_settings.h"
#include "pc_settings.h"
}

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#pragma comment(lib, "dxgi.lib")
#endif

namespace gfx
{
namespace
{
std::mutex g_mutex;
View g_view;

const char *kCredits = "Presets by Marleymarletto \xC2\xB7 ReShade by crosire \xC2\xB7 DLSS by NVIDIA \xC2\xB7 "
                       "DLSS 5 Manager / shader authors (see notices)";

void refresh_locked()
{
    Record r = read_record(data_folder());
    g_view.installed = r.present && !r.files.empty();
    g_view.preset = r.preset;
    g_view.version = r.version;
    g_view.skip_once = g_view.installed && guard_tripped(data_folder());
    if (g_view.credits.empty()) g_view.credits = kCredits;
}

void set_progress(const std::string &text, double fraction)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_view.progress = text;
    g_view.fraction = fraction;
}

void finish(const std::string &message, int tone)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_view.busy = false;
    g_view.progress.clear();
    g_view.fraction = -1;
    if (!message.empty()) { g_view.message = message; g_view.tone = tone; }
    refresh_locked();
}

Progress downloading(const char *what = "Downloading the graphics pack")
{
    return [what](uint64_t got, uint64_t total) {
        char t[160];
        snprintf(t, sizeof(t), "%s: %.0f of %.0f MB", what, got / 1048576.0, total / 1048576.0);
        set_progress(t, total ? (double)got / total : -1);
    };
}

std::atomic<bool> g_checking{false};    // one update check at a time
std::atomic<bool> g_playing{false};     // Play was pressed in this process: bin is the game's from now on

bool fetch(Manifest &m, std::string &error)
{
    set_progress("Asking the update server for the graphics pack...", -1);
    if (!fetch_manifest(m, &error)) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!m.credits.empty()) g_view.credits = m.credits;
    g_view.notices = m.notices;
    return true;
}

void save_setting(bool on, const std::string &preset)
{
    bool changed = recomp_settings_set("graphics", "enabled", on ? 1 : 0) > 0;
    changed |= recomp_settings_set("graphics", "preset", preset == "quality" ? 1 : 0) > 0;
    if (changed) pc_settings_save();
}

void run_install(std::string preset, bool reinstall)
{
    Manifest m;
    std::string error;
    if (!fetch(m, error)) { finish(error, 2); return; }
    set_progress("Installing the graphics pack...", -1);
    if (!install(m, bin_folder(), data_folder(), preset, downloading(), &error)) { finish(error, 2); return; }
    if (!recomp_settings_get("graphics", "enabled", 0)) {      // Turn off was pressed meanwhile
        remove(data_folder(), &error);
        finish("Enhanced graphics is off; its files are removed.", 1);
        return;
    }
    save_setting(true, preset);
    finish(reinstall ? "Enhanced graphics installed again for this version. It applies when you press Play."
                     : "Enhanced graphics is installed. It applies when you press Play.", 1);
}

bool start_work()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_view.busy) return false;
    g_view.busy = true;
    g_view.message.clear();
    return true;
}

bool may_change_bin() { return !g_playing && !pcsaves::game_running(); }

void notice(const std::string &text, int tone)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_view.notice = text;
    g_view.notice_tone = tone;
}

// Quiet unless there is something to do: a server that cannot be reached, or the same version, says nothing.
void run_update_check()
{
    Manifest m;
    std::string error;
    Record r = read_record(data_folder());
    if (r.present && !r.files.empty() && fetch_manifest(m, &error) && m.version != r.version && may_change_bin() &&
        start_work()) {
        fprintf(stderr, "[GRAPHICS] pack %s published, %s installed: updating\n", m.version.c_str(), r.version.c_str());
        set_progress("Updating the graphics pack...", -1);
        bool updated = false;
        bool ok = update(m, data_folder(), downloading("Updating the graphics pack"), may_change_bin, &error, &updated);
        if (updated) {
            std::string text = "Graphics pack updated to " + m.version + ".";
            notice(text, 1);
            finish(text, 1);
        } else if (!ok) {
            notice("Graphics pack update failed: see Graphics", 2);      // the card is short; the dialog says why
            finish("Graphics pack update failed: " + error, 2);
        } else {
            finish("", 0);          // waits for the next start (the game started), or turned off meanwhile
        }
    }
    g_checking = false;
}
} // namespace

void check_update()
{
    if (!recomp_settings_get("graphics", "enabled", 0) || g_playing || g_checking.exchange(true)) return;
    std::thread(run_update_check).detach();
}

fs::path bin_folder()
{
    if (const char *b = getenv("RECOMP_GRAPHICS_BIN")) return fs::u8path(b);        // tests and pictures
#if defined(_WIN32)
    wchar_t path[MAX_PATH * 2];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH * 2)) return fs::path(path).parent_path();
#endif
    return fs::current_path();
}

fs::path data_folder()
{
    if (const char *d = getenv("RECOMP_GRAPHICS_DATA")) return fs::u8path(d);       // tests
    return fs::u8path(pcui::folder_path(pcui::Folder::Data));
}

View view()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_view.busy) refresh_locked();
    return g_view;
}

void start_enable(const std::string &preset)
{
    if (!start_work()) return;
    clear_guard(data_folder());
    save_setting(true, preset);
    std::thread(run_install, preset, false).detach();
}

void start_switch(const std::string &preset)
{
    View v = view();
    if (!v.installed) { save_setting(recomp_settings_get("graphics", "enabled", 0) != 0, preset); return; }
    if (!start_work()) return;
    save_setting(true, preset);
    std::thread([preset] {
        Manifest m;
        std::string error;
        if (!fetch(m, error)) { finish(error, 2); return; }
        if (!switch_preset(m, data_folder(), preset, downloading(), &error)) { finish(error, 2); return; }
        finish(std::string(preset == "quality" ? "Quality" : "Performance") + " preset set. It applies when you press Play.", 1);
    }).detach();
}

void turn_off()
{
    std::string error;
    save_setting(false, recomp_settings_get("graphics", "preset", 0) ? "quality" : "performance");
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // even while a download runs: its install will see the setting and the record is ours to remove
        g_view.busy = false;
    }
    bool ok;
    {
        std::lock_guard<std::mutex> lock(bin_lock());      // not in the middle of an update's switch
        ok = remove(data_folder(), &error);
    }
    clear_guard(data_folder());
    finish(ok ? "Enhanced graphics is off; its files are removed." : error, ok ? 1 : 2);
}

void try_again()
{
    clear_guard(data_folder());
    std::lock_guard<std::mutex> lock(g_mutex);
    g_view.message = "The next Play starts with Enhanced graphics again.";
    g_view.tone = 1;
    refresh_locked();
}

void launcher_starts()
{
    fs::path data = data_folder();
    recover_update(data);       // an update the power cut: finished or undone, before anything else looks
    finish_pending(data);
    tidy_proxy(data);          // before anything looks at the files: an older install's dxgi.dll gets its new name
    bool skip = guard_note_launcher_start(data);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (skip && recomp_settings_get("graphics", "enabled", 0)) {
            g_view.message = "Enhanced graphics was turned off for this start because the game did not start with it "
                             "last time. Try again from Graphics.";
            g_view.tone = 2;
        }
        refresh_locked();
    }
    bool on = recomp_settings_get("graphics", "enabled", 0) != 0;
    std::string preset = recomp_settings_get("graphics", "preset", 0) ? "quality" : "performance";
    Record r = read_record(data);
    std::error_code ec;
    if (!on) {
        if (r.present) {            // turned off while a removal could not finish
            std::string error;
            remove(data, &error);
        }
        return;
    }
    bool here = r.present && fs::equivalent(r.bin, bin_folder(), ec);
    bool complete = here;
    for (size_t i = 0; complete && i < r.files.size(); i++) complete = fs::exists(r.bin / fs::u8path(r.files[i]), ec);
    if (complete) {
        check_update();
        return;
    }
    fprintf(stderr, "[GRAPHICS] enabled but %s; installing it here\n",
            r.present && !here ? "installed for another version" : "not complete in this game folder");
    if (start_work()) std::thread(run_install, r.preset.empty() ? preset : r.preset, true).detach();
}

bool gpu_supported(std::string *adapter, std::string *why)
{
#if defined(_WIN32)
    if (const char *t = getenv("RECOMP_GRAPHICS_TEST_GPU")) {     // tests and pictures
        if (adapter) *adapter = t;
        bool ok = strstr(t, "RTX") != nullptr;
        if (why && !ok) *why = "Needs an NVIDIA RTX graphics card (found: " + std::string(t) + ").";
        return ok;
    }
    IDXGIFactory1 *factory = nullptr;
    bool ok = false;
    std::string found;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory))) {
        IDXGIAdapter1 *a = nullptr;
        for (UINT i = 0; !ok && factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d;
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                char name[256];
                WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name), nullptr, nullptr);
                if (found.empty()) found = name;
                if (d.VendorId == 0x10DE && strstr(name, "RTX")) { ok = true; found = name; }
            }
            a->Release();
        }
        factory->Release();
    }
    if (adapter) *adapter = found;
    if (why && !ok)
        *why = found.empty() ? "Needs an NVIDIA RTX graphics card." : "Needs an NVIDIA RTX graphics card (found: " + found + ").";
    return ok;
#else
    if (why) *why = "Windows with an NVIDIA RTX graphics card only.";
    return false;
#endif
}
} // namespace gfx

#if defined(_WIN32)
namespace
{
const wchar_t *const kNotice = L"Loading Enhanced graphics, the window may not respond for a moment.";
std::thread g_watch;

struct FoundWindow { DWORD pid; HWND window; };

BOOL CALLBACK find_game_window(HWND w, LPARAM out)
{
    FoundWindow *f = (FoundWindow *)out;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != f->pid || !IsWindowVisible(w)) return TRUE;
    f->window = w;
    return FALSE;
}

LRESULT CALLBACK notice_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH back = CreateSolidBrush(RGB(20, 20, 24));
        FillRect(dc, &rc, back);
        DeleteObject(back);
        HFONT font = CreateFontW(-MulDiv(15, GetDeviceCaps(dc, LOGPIXELSY), 96), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ was = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(240, 240, 240));
        DrawTextW(dc, kNotice, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, was);
        DeleteObject(font);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// A small bar in the middle of the screen, without the focus, until the game's window answers (two seconds
// more) or it has ended; at most half a minute. A plain window: the launcher's Direct3D is closed by now.
void show_notice(HANDLE process, DWORD pid)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = notice_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"DefJamRecompGraphicsNotice";
    RegisterClassExW(&wc);
    HDC screen = GetDC(nullptr);
    int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) ReleaseDC(nullptr, screen);
    int w = MulDiv(640, dpi, 96), h = MulDiv(64, dpi, 96);
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"FFNY Online", WS_POPUP,
                                  (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return;
    fprintf(stderr, "[GRAPHICS] notice shown while the game starts\n");
    fflush(stderr);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    ULONGLONG start = GetTickCount64(), answered = 0;
    while (GetTickCount64() - start < 30000) {
        if (MsgWaitForMultipleObjects(1, &process, FALSE, 100, QS_ALLINPUT) == WAIT_OBJECT_0) break;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        FoundWindow f = {pid, nullptr};
        EnumWindows(find_game_window, (LPARAM)&f);
        if (f.window && !IsHungAppWindow(f.window)) {
            if (!answered) answered = GetTickCount64();
            if (GetTickCount64() - answered > 2000) break;
        } else {
            answered = 0;
        }
    }
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
} // namespace

extern "C" int graphics_mod_wanted(void)
{
    using namespace gfx;
    if (!recomp_settings_get("graphics", "enabled", 0)) return 0;
    Record r = read_record(data_folder());
    return r.present && !r.files.empty();
}

extern "C" int graphics_mod_before_start(void)
{
    using namespace gfx;
    std::lock_guard<std::mutex> lock(bin_lock());      // an update's switch finishes first; none starts after this
    g_playing = true;
    finish_pending(data_folder());
    return begin_play(data_folder(), bin_folder(), recomp_settings_get("graphics", "enabled", 0) != 0) ? 1 : 0;
}

extern "C" void graphics_mod_start_failed(void) { gfx::start_failed(gfx::data_folder()); }

extern "C" void graphics_mod_started(void *process, unsigned long pid)
{
    gfx::fs::path data = gfx::data_folder();
    const char *ms = getenv("RECOMP_GRAPHICS_GUARD_MS");     // tests
    unsigned long guard = ms && atoi(ms) > 0 ? (unsigned long)atoi(ms) : 60000;
    g_watch = std::thread(gfx::watch_play, process, data, guard);
    std::thread(show_notice, (HANDLE)process, (DWORD)pid).detach();
}

extern "C" void graphics_mod_settled(void)
{
    if (g_watch.joinable()) g_watch.join();
}
#endif

extern "C" void graphics_mod_log_header(void)
{
    using namespace gfx;
    Record r = read_record(data_folder());
    std::error_code ec;
    bool foreign = fs::exists(bin_folder() / "dxgi.dll", ec);
    bool proxy = foreign || fs::exists(bin_folder() / "dxgi.ffny-mod", ec);
    bool loaded = false, reshade = false;
#if defined(_WIN32)
    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    wchar_t path[MAX_PATH * 2];
    if (dxgi && GetModuleFileNameW(dxgi, path, MAX_PATH * 2)) loaded = fs::equivalent(fs::path(path).parent_path(), bin_folder(), ec);
    reshade = dxgi && (GetProcAddress(dxgi, "ReShadeRegisterAddon") || GetModuleHandleW(L"ReShade64.dll"));
#endif
    if (r.present && !r.files.empty())
        fprintf(stderr, "[GRAPHICS] community mod on (%s), pack %s; dxgi proxy %s, %s in this process (it loads in the "
                "game's only), ReShade %s\n", r.preset.c_str(), r.version.c_str(), proxy ? "present" : "missing",
                loaded ? "loaded" : "not loaded", reshade ? "yes" : "no");
    else
        fprintf(stderr, "[GRAPHICS] community mod off%s\n",
                foreign ? (loaded ? "; a dxgi.dll not installed by the launcher is loaded from the game folder"
                                : "; a dxgi.dll not installed by the launcher is in the game folder") : "");
    fflush(stderr);
}
