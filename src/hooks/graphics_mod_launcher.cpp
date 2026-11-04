// "Enhanced graphics (community)", the launcher's part: the background work and what the card shows,
// the settings, the re-install after an update, the GPU check and the log header line. See graphics_mod.h.
#define _CRT_SECURE_NO_WARNINGS
#include "graphics_mod.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "pc_ui_actions.h"
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
int g_loaded = -1;          // whether this program started with the pack's dxgi.dll loaded

const char *kCredits = "Presets by Marleymarletto \xC2\xB7 ReShade by crosire \xC2\xB7 DLSS by NVIDIA \xC2\xB7 "
                       "DLSS 5 Manager / shader authors (see notices)";

bool loaded_from_bin()
{
#if defined(_WIN32)
    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    wchar_t path[MAX_PATH * 2];
    if (!dxgi || !GetModuleFileNameW(dxgi, path, MAX_PATH * 2)) return false;
    std::error_code ec;
    return fs::equivalent(fs::path(path).parent_path(), bin_folder(), ec);
#else
    return false;
#endif
}

void refresh_locked()
{
    Record r = read_record(data_folder());
    g_view.installed = r.present && !r.files.empty();
    g_view.preset = r.preset;
    g_view.version = r.version;
    if (g_loaded < 0) g_loaded = loaded_from_bin() ? 1 : 0;
    g_view.restart_needed = g_view.installed != (g_loaded == 1);
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

Progress downloading()
{
    return [](uint64_t got, uint64_t total) {
        char t[160];
        snprintf(t, sizeof(t), "Downloading the graphics pack: %.0f of %.0f MB", got / 1048576.0, total / 1048576.0);
        set_progress(t, total ? (double)got / total : -1);
    };
}

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
    finish(reinstall ? "Enhanced graphics installed again for this version. Close and reopen the launcher to apply it."
                     : "Enhanced graphics is installed. Close and reopen the launcher to apply it.", 1);
}

bool start_work()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_view.busy) return false;
    g_view.busy = true;
    g_view.message.clear();
    return true;
}
} // namespace

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
        finish(std::string(preset == "quality" ? "Quality" : "Performance") + " preset set. Close and reopen the launcher to apply it.", 1);
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
    bool ok = remove(data_folder(), &error);
    finish(ok ? "Enhanced graphics is off; its files are removed." : error, ok ? 1 : 2);
}

void launcher_starts()
{
    fs::path data = data_folder();
    finish_pending(data);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
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
    if (complete) return;
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

extern "C" void graphics_mod_log_header(void)
{
    using namespace gfx;
    Record r = read_record(data_folder());
    std::error_code ec;
    bool proxy = fs::exists(bin_folder() / "dxgi.dll", ec);
    bool loaded = false, reshade = false;
#if defined(_WIN32)
    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    wchar_t path[MAX_PATH * 2];
    if (dxgi && GetModuleFileNameW(dxgi, path, MAX_PATH * 2)) loaded = fs::equivalent(fs::path(path).parent_path(), bin_folder(), ec);
    reshade = dxgi && (GetProcAddress(dxgi, "ReShadeRegisterAddon") || GetModuleHandleW(L"ReShade64.dll"));
#endif
    if (r.present && !r.files.empty())
        fprintf(stderr, "[GRAPHICS] community mod on (%s), pack %s; dxgi proxy %s, %s, ReShade %s\n", r.preset.c_str(),
                r.version.c_str(), proxy ? "present" : "missing", loaded ? "loaded" : "not loaded", reshade ? "yes" : "no");
    else
        fprintf(stderr, "[GRAPHICS] community mod off%s\n",
                proxy ? (loaded ? "; a dxgi.dll not installed by the launcher is loaded from the game folder"
                                : "; a dxgi.dll not installed by the launcher is in the game folder") : "");
    fflush(stderr);
}
