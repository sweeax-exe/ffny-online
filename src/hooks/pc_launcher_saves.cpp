// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The Saves page's glue (pc_launcher_saves.h).
#define _CRT_SECURE_NO_WARNINGS
#include "pc_launcher_saves.h"
#include "pc_ui_actions.h"
#include "pc_ui_model.h"
extern "C" {
#include "save_compat.h"
}

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#else
#include <SDL3/SDL.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

// The runtime's guest path translation (xboxrecomp kernel.h), the same rules every guest file call uses.
#if defined(_WIN32)
extern "C" BOOL xbox_translate_path(const char *xbox_path, WCHAR *host_path_buf, DWORD buf_size);
#else
extern "C" int xbox_translate_path(const char *xbox_path, char *host_path_buf, uint32_t buf_size);
#endif

namespace pcsaves
{
namespace fs = saves::fs;

fs::path save_dir() { return fs::u8path(pcui::folder_path(pcui::Folder::Saves)); }
fs::path backup_dir() { return fs::u8path(pcui::folder_path(pcui::Folder::Data)) / "save-backups"; }
int64_t now() { return (int64_t)std::time(nullptr); }

#if defined(_WIN32)
// One name per save area: the game holds it while it runs (saves_game_starts), so a launcher of any
// copy refuses to restore or import into that area meanwhile. FNV-1a of the lower-case full path.
static std::wstring marker_name()
{
    std::error_code ec;
    fs::path p = fs::absolute(save_dir(), ec).lexically_normal();
    std::string text = pcui::path_utf8(p);
    while (!text.empty() && (text.back() == '\\' || text.back() == '/')) text.pop_back();
    uint32_t h = 2166136261u;
    for (unsigned char c : text) {
        h ^= (uint32_t)tolower(c);
        h *= 16777619u;
    }
    wchar_t name[64];
    swprintf(name, 64, L"Local\\FFNY-Online-saves-%08x", h);
    return name;
}

// Another process of this very executable (the game, which a launcher starts as a second process).
static bool twin_running()
{
    wchar_t self[MAX_PATH * 2];
    if (!GetModuleFileNameW(nullptr, self, MAX_PATH * 2)) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    const wchar_t *base = wcsrchr(self, L'\\');
    base = base ? base + 1 : self;
    bool found = false;
    for (BOOL more = Process32FirstW(snap, &pe); more && !found; more = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == GetCurrentProcessId() || _wcsicmp(pe.szExeFile, base)) continue;
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!proc) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD n = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(proc, 0, path, &n) && !_wcsicmp(path, self)) found = true;
        CloseHandle(proc);
    }
    CloseHandle(snap);
    return found;
}

bool game_running()
{
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, marker_name().c_str());
    if (m) {
        CloseHandle(m);
        return true;
    }
    return twin_running();
}

static void hold_marker()
{
    static HANDLE held;
    if (!held) held = CreateMutexW(nullptr, FALSE, marker_name().c_str());   // released when the process ends
}
#else
static std::string lock_path() { return pcui::path_utf8(save_dir() / ".game-running.lock"); }

bool game_running()
{
    int fd = open(lock_path().c_str(), O_RDONLY);
    if (fd < 0) return false;
    bool busy = flock(fd, LOCK_EX | LOCK_NB) != 0;
    close(fd);
    return busy;
}

static void hold_marker()
{
    static int held = -1;
    if (held >= 0) return;
    std::error_code ec;
    fs::create_directories(save_dir(), ec);
    held = open(lock_path().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (held >= 0) flock(held, LOCK_EX | LOCK_NB);
}
#endif

// ---- the picker ----

static std::mutex g_pick_mutex;
static bool g_pick_done;
static std::string g_picked;

static void picked(const std::string &path)
{
    std::lock_guard<std::mutex> lock(g_pick_mutex);
    g_picked = path;
    g_pick_done = true;
}

#if defined(_WIN32)
void pick_start(Pick what, void *window)
{
    std::string result;
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog *dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
        if (what == Pick::ZIP) {
            static const COMDLG_FILTERSPEC filter[] = {{L"Zip files (*.zip)", L"*.zip"}, {L"All files", L"*.*"}};
            dialog->SetFileTypes(2, filter);
            dialog->SetTitle(L"Choose a .zip of Xbox saves");
            options |= FOS_FILEMUSTEXIST;
        } else if (what == Pick::PS2) {
            static const COMDLG_FILTERSPEC filter[] = {{L"PS2 saves (*.psu)", L"*.psu"}, {L"All files", L"*.*"}};
            dialog->SetFileTypes(2, filter);
            dialog->SetTitle(L"Choose a PS2 Def Jam save exported as .psu");
            options |= FOS_FILEMUSTEXIST;
        } else {
            dialog->SetTitle(L"Choose the folder with your Xbox saves (UDATA\\45410049)");
            options |= FOS_PICKFOLDERS;
        }
        dialog->SetOptions(options);
        if (SUCCEEDED(dialog->Show((HWND)window))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR name = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name) {
                    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
                    if (n > 1) {
                        result.resize((size_t)n);
                        WideCharToMultiByte(CP_UTF8, 0, name, -1, result.data(), n, nullptr, nullptr);
                        result.resize((size_t)n - 1);
                    }
                    CoTaskMemFree(name);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(com)) CoUninitialize();
    picked(result);
}
#else
static void dialog_done(void *, const char *const *files, int)
{
    picked(files && files[0] ? files[0] : "");
}

void pick_start(Pick what, void *window)
{
    static const SDL_DialogFileFilter zip[] = {{"Zip files", "zip"}};
    static const SDL_DialogFileFilter ps2[] = {{"PS2 saves (.psu)", "psu"}, {"All files", "*"}};
    if (what == Pick::ZIP)
        SDL_ShowOpenFileDialog(dialog_done, nullptr, (SDL_Window *)window, zip, 1, nullptr, false);
    else if (what == Pick::PS2)
        SDL_ShowOpenFileDialog(dialog_done, nullptr, (SDL_Window *)window, ps2, 2, nullptr, false);
    else
        SDL_ShowOpenFolderDialog(dialog_done, nullptr, (SDL_Window *)window, nullptr, false);
}
#endif

bool pick_poll(std::string &path)
{
    std::lock_guard<std::mutex> lock(g_pick_mutex);
    if (!g_pick_done) return false;
    g_pick_done = false;
    path = g_picked;
    g_picked.clear();
    return true;
}
bool game_database(saves::Bytes &out, std::string *where)
{
    fs::path data = fs::u8path(pcui::folder_path(pcui::Folder::Data));
    fs::path install = fs::u8path(pcui::folder_path(pcui::Folder::Install));
    const fs::path tries[] = {data / "override" / "assets" / "djv2.sod", install / "game" / "assets" / "djv2.sod",
                              fs::path("game") / "assets" / "djv2.sod", data / "extracted" / "assets" / "djv2.sod"};
    for (auto &p : tries) {
        std::error_code ec;
        uintmax_t size = fs::file_size(p, ec);
        if (ec || size < 24 || size > (64u << 20)) continue;
        std::ifstream f(p, std::ios::binary);
        out.resize((size_t)size);
        if (!f || !f.read((char *)out.data(), (std::streamsize)out.size())) continue;
        if (where) *where = pcui::path_utf8(p);
        return true;
    }
    return false;
}
} // namespace pcsaves

extern "C" void saves_game_starts(void)
{
    pcsaves::hold_marker();
    const char *off = getenv("RECOMP_SAVES_BACKUP");
    if (off && !strcmp(off, "0")) return;
    auto backups = pcsaves::backup_dir();
    int64_t t = pcsaves::now();
    if (!saves::daily_due(backups, t)) {
        fprintf(stderr, "[SAVES] daily backup: there is one from today already\n");
        return;
    }
    std::string made, why;
    if (saves::make_backup(pcsaves::save_dir(), backups, "start", t, &made, &why))
        fprintf(stderr, "[SAVES] daily backup made: %s\n", pcui::path_utf8(backups / made).c_str());
    else
        fprintf(stderr, "[SAVES] daily backup: %s\n", why.c_str());
    fflush(stderr);
}

extern "C" void saves_backup_before_update(void)
{
    std::string made, why;
    auto backups = pcsaves::backup_dir();
    if (saves::make_backup(pcsaves::save_dir(), backups, "update", pcsaves::now(), &made, &why))
        fprintf(stderr, "[SAVES] backup before the update: %s\n", pcui::path_utf8(backups / made).c_str());
    else
        fprintf(stderr, "[SAVES] backup before the update: %s\n", why.c_str());
    fflush(stderr);
}

// XAPI's tree delete is about to run (sub_001FBCCA_enter, src/recomp_manual.c): XDeleteSaveGame removing
// a profile, or its other caller. When the folder is one directly under UDATA/45410049, a copy goes to
// save-backups/quarantine first; the delete itself goes ahead (the game recreates the profile right
// after for its requests 6 and 19). Runs on the game's storage thread; a profile is about 30 KB.
extern "C" void saves_game_deletes(const char *guest_path)
{
    if (!guest_path) return;
    char path[300];
    size_t n = 0;
    while (n + 1 < sizeof(path) && guest_path[n]) { path[n] = guest_path[n]; n++; }
    path[n] = 0;
    while (n && (path[n - 1] == '\\' || path[n - 1] == '/')) path[--n] = 0;
    const char *udata = defjam_save_user_data_dir();
    if (!n || !udata || !*udata) return;
    namespace fs = saves::fs;
    fs::path host;
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 2];
    if (!xbox_translate_path(path, buf, MAX_PATH)) return;
    host = fs::path(buf);
#else
    char buf[4096];
    if (!xbox_translate_path(path, buf, sizeof(buf))) return;
    host = fs::path(buf);
#endif
    std::error_code ec;
    fs::path title = fs::u8path(udata) / saves::kTitleId;
    if (!fs::is_directory(host, ec) || !fs::equivalent(host.parent_path(), title, ec) || ec) return;
    std::string folder = pcui::path_utf8(host.filename()), made, why;
    if (saves::quarantine(host, pcsaves::backup_dir(), pcsaves::now(), &made, &why))
        fprintf(stderr, "[SAVES] the game deleted profile %s (copy in save-backups/quarantine/%s)\n", folder.c_str(),
                made.c_str());
    else
        fprintf(stderr, "[SAVES] the game deleted profile %s (no copy: %s)\n", folder.c_str(), why.c_str());
    fflush(stderr);
}
