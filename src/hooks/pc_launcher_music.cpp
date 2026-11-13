// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The launcher's Music dialog underneath: see pc_launcher_music.h.
#define _CRT_SECURE_NO_WARNINGS
#include "pc_launcher_music.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "pc_ui_actions.h"

extern "C" {
#include "custom_music.h"
#include "pc_settings.h"
#include "recomp_settings.h"
}

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#else
#include <SDL3/SDL.h>
#endif

namespace pcmusic
{
namespace
{
std::mutex g_mutex;
View g_view;
std::vector<std::string> g_pending_add;     // asked for while the worker was busy
bool g_pending_refresh = false;
std::vector<std::string> g_dropped;
bool g_pick_done = false;
std::vector<std::string> g_picked;

void log_line(const std::string &text)
{
    fprintf(stderr, "[MUSIC] %s\n", text.c_str());
    fflush(stderr);
}

void set_progress(const std::string &text)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_view.progress = text;
}

void work(std::vector<std::string> adding)
{
    for (;;) {
        musiclib::fs::path dir = folder();
        std::string summary;
        int tone = 0;
        if (!adding.empty()) {
            std::vector<musiclib::fs::path> sources;
            for (auto &p : adding) sources.push_back(musiclib::from_utf8(p));
            musiclib::AddResult r;
            try {
                r = musiclib::add(dir, sources, [](int done, int total, const std::string &name) {
                    if (done < total) set_progress("Adding songs: " + std::to_string(done + 1) + " of " +
                                                   std::to_string(total) + "  \xC2\xB7  " + name);
                });
            } catch (...) {
                r.notes.push_back("the songs could not be copied");
            }
            summary = r.added == 1 ? "1 song added." : std::to_string(r.added) + " songs added.";
            if (!r.notes.empty())
                summary += " Not added: " + r.notes[0] + (r.notes.size() > 1 ? " (and " + std::to_string(r.notes.size() - 1) + " more)" : "");
            tone = r.added ? 1 : 2;
            log_line("launcher: " + summary);
            for (auto &n : r.notes) log_line("launcher: not added: " + n);
        }
        set_progress("Reading your music folder...");
        std::vector<musiclib::Song> songs;
        size_t more = 0;
        try {
            songs = musiclib::scan(dir, musiclib::kMaxFiles, musiclib::kMaxDepth, &more);
            musiclib::apply_index(dir, songs);
            size_t todo = 0, done = 0, bad = 0;
            for (auto &s : songs) todo += s.state == musiclib::UNCHECKED;
            for (auto &s : songs) {
                if (s.state != musiclib::UNCHECKED) continue;
                set_progress("Checking new songs: " + std::to_string(++done) + " of " + std::to_string(todo) +
                             "  \xC2\xB7  " + s.name);
                musiclib::check(s);
                if (s.state == musiclib::BAD) {
                    bad++;
                    log_line("launcher: " + s.rel + " cannot be played: " + s.why);
                }
            }
            if (todo) musiclib::save_index(dir, songs);
            if (!songs.empty()) set_progress("Reading your songs' artists...");
            for (auto &s : songs) musiclib::read_artist(s);
            if (bad && summary.empty()) {
                summary = bad == 1 ? "1 file cannot be played; the game skips it." :
                                     std::to_string(bad) + " files cannot be played; the game skips them.";
                tone = 2;
            }
        } catch (...) {
            summary = "The music folder could not be read.";
            tone = 2;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        // the player's own order, and when each song was added; under the lock, like move(), so a song
        // moved while this ran is not undone
        try {
            musiclib::apply_order(dir, songs);
        } catch (...) {
        }
        g_view.songs = std::move(songs);
        g_view.more = more;
        g_view.loaded = true;
        g_view.generation++;
        g_view.progress.clear();
        if (!summary.empty()) { g_view.message = summary; g_view.tone = tone; }
        if (g_pending_add.empty() && !g_pending_refresh) {
            g_view.busy = false;
            return;
        }
        adding.swap(g_pending_add);
        g_pending_add.clear();
        g_pending_refresh = false;
    }
}

void start(std::vector<std::string> adding)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_view.busy) {
        g_pending_add.insert(g_pending_add.end(), adding.begin(), adding.end());
        g_pending_refresh = true;
        return;
    }
    g_view.busy = true;
    std::thread(work, std::move(adding)).detach();
}
} // namespace

musiclib::fs::path folder()
{
    if (const char *dir = getenv("RECOMP_MUSIC_DIR")) return musiclib::from_utf8(dir);
    return musiclib::from_utf8(pcui::folder_path(pcui::Folder::Data)) / "music";
}

View view()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_view;
}

void refresh() { start({}); }
void add(const std::vector<std::string> &paths) { if (!paths.empty()) start(paths); }

void say(const std::string &text, int tone)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_view.message = text;
    g_view.tone = tone;
}

void remove(const musiclib::Song &song)
{
    std::string why;
    if (musiclib::remove(folder(), song, &why)) {
        say(song.name + " removed.", 1);
        log_line("launcher: removed " + song.rel);
    } else {
        say(song.name + " " + why, 2);
    }
    refresh();
}

int sort_setting()
{
    int sort = recomp_settings_get("music", "sort", musiclib::SORT_CUSTOM);
    return sort >= 0 && sort < musiclib::SORT_COUNT ? sort : musiclib::SORT_CUSTOM;
}

void set_sort(int sort)
{
    if (sort < 0 || sort >= musiclib::SORT_COUNT) return;
    if (recomp_settings_set("music", "sort", sort) > 0) {
        pc_settings_save();
        static const char *const names[] = {"custom", "name", "artist", "added"};
        log_line(std::string("sort ") + names[sort]);
    }
    custom_music_order_changed();
}

void set_shuffle(bool on)
{
    if (recomp_settings_set("music", "shuffle", on ? 1 : 0) > 0) {
        pc_settings_save();
        log_line(std::string("shuffle ") + (on ? "on" : "off"));
    }
    custom_music_order_changed();
}

std::vector<musiclib::Song> sorted(const View &v)
{
    std::vector<musiclib::Song> out = v.songs;
    musiclib::sort_songs(out, sort_setting());
    return out;
}

bool move(const std::string &rel, int delta)
{
    std::vector<musiclib::Song> list;
    size_t at = 0, to = 0;
    bool saved = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        list = g_view.songs;
        musiclib::sort_songs(list, sort_setting());
        for (at = 0; at < list.size() && list[at].rel != rel; at++) {}
        if (at >= list.size()) return false;
        to = musiclib::move_song(list, at, delta);
        if (to == at) return false;
        g_view.songs = list;
        g_view.generation++;
        saved = musiclib::save_order(folder(), musiclib::order_of(list));
    }
    log_line("moved " + rel + " from " + std::to_string(at + 1) + " to " + std::to_string(to + 1) +
             (saved ? "" : " (the order could not be saved)"));
    if (sort_setting() != musiclib::SORT_CUSTOM) set_sort(musiclib::SORT_CUSTOM);
    else custom_music_order_changed();
    return true;
}

static void picked(std::vector<std::string> paths)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_picked = std::move(paths);
    g_pick_done = true;
}

#if defined(_WIN32)
void pick_start(void *window)
{
    std::vector<std::string> result;
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog *dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT | FOS_NOCHANGEDIR);
        static const COMDLG_FILTERSPEC filter[] = {{L"Songs (MP3, OGG, FLAC, WAV)", L"*.mp3;*.ogg;*.oga;*.flac;*.wav"},
                                                   {L"All files", L"*.*"}};
        dialog->SetFileTypes(2, filter);
        dialog->SetTitle(L"Add songs to FFNY Online");
        if (SUCCEEDED(dialog->Show((HWND)window))) {
            IShellItemArray *items = nullptr;
            if (SUCCEEDED(dialog->GetResults(&items)) && items) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; i++) {
                    IShellItem *item = nullptr;
                    if (FAILED(items->GetItemAt(i, &item)) || !item) continue;
                    PWSTR name = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name) {
                        result.push_back(musiclib::utf8(musiclib::fs::path(name)));
                        CoTaskMemFree(name);
                    }
                    item->Release();
                }
                items->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(com)) CoUninitialize();
    picked(std::move(result));
}
#else
static void dialog_done(void *, const char *const *files, int)
{
    std::vector<std::string> out;
    for (int i = 0; files && files[i]; i++) out.push_back(files[i]);
    picked(std::move(out));
}

void pick_start(void *window)
{
    static const SDL_DialogFileFilter filter[] = {{"Songs", "mp3;ogg;oga;flac;wav"}};
    SDL_ShowOpenFileDialog(dialog_done, nullptr, (SDL_Window *)window, filter, 1, nullptr, true);
}
#endif

bool pick_poll(std::vector<std::string> &paths)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_pick_done) return false;
    g_pick_done = false;
    paths.swap(g_picked);
    g_picked.clear();
    return true;
}

void dropped(const std::vector<std::string> &paths)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dropped.insert(g_dropped.end(), paths.begin(), paths.end());
}

bool take_dropped(std::vector<std::string> &paths)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_dropped.empty()) return false;
    paths.swap(g_dropped);
    g_dropped.clear();
    return true;
}
} // namespace pcmusic
