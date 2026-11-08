// The player's own songs on disk: see music_library.h.
#define _CRT_SECURE_NO_WARNINGS
#include "music_library.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

extern "C" {
#include "music_decode.h"
}

namespace musiclib
{

std::string utf8(const fs::path &p)
{
#if defined(_WIN32)
    const std::wstring &w = p.native();
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)std::max(n, 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
#else
    return p.string();
#endif
}

fs::path from_utf8(const std::string &s)
{
#if defined(_WIN32)
    if (s.empty()) return fs::path();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)std::max(n, 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return fs::path(w);
#else
    return fs::path(s);
#endif
}

static int64_t stamp(const fs::directory_entry &e)
{
    std::error_code ec;
    auto t = e.last_write_time(ec);
    if (ec) return 0;
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

static bool hidden_name(const fs::path &name)
{
    std::string n = utf8(name);
    return n.empty() || n[0] == '.';
}

std::vector<Song> scan(const fs::path &folder, size_t max_files, int max_depth, size_t *more)
{
    std::vector<Song> out;
    size_t left_out = 0;
    std::error_code ec;
    if (more) *more = 0;
    if (!fs::is_directory(folder, ec)) return out;
    // Breadth by folder, each folder's entries sorted, so a huge folder gives the same first songs.
    std::vector<std::pair<fs::path, int>> todo{{folder, 0}};
    for (size_t at = 0; at < todo.size() && at < 4096; at++) {
        std::vector<fs::directory_entry> entries;
        fs::directory_iterator it(todo[at].first, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            entries.push_back(*it);
            if (entries.size() > 100000) break;
        }
        ec.clear();
        std::sort(entries.begin(), entries.end(),
                  [](const fs::directory_entry &a, const fs::directory_entry &b) { return a.path() < b.path(); });
        for (auto &e : entries) {
            fs::path name = e.path().filename();
            if (hidden_name(name)) continue;
            std::error_code e2;
            if (e.is_directory(e2)) {
                if (todo[at].second + 1 < max_depth && !e.is_symlink(e2)) todo.push_back({e.path(), todo[at].second + 1});
                continue;
            }
            if (!e.is_regular_file(e2)) continue;
            std::string n8 = utf8(name);
            if (!music_decode_known_extension(n8.c_str())) continue;
            if (out.size() >= max_files) { left_out++; continue; }
            Song s;
            s.path = e.path();
            fs::path rel = e.path().lexically_relative(folder);
            s.rel = utf8(rel.empty() ? name : rel);
            std::replace(s.rel.begin(), s.rel.end(), '\\', '/');
            s.name = utf8(name.stem());
            s.size = e.file_size(e2);
            s.mtime = stamp(e);
            out.push_back(std::move(s));
        }
    }
    std::sort(out.begin(), out.end(), [](const Song &a, const Song &b) { return a.rel < b.rel; });
    if (more) *more = left_out;
    return out;
}

static fs::path index_path(const fs::path &folder) { return folder / ".cache" / "index.tsv"; }

void apply_index(const fs::path &folder, std::vector<Song> &songs)
{
    std::ifstream in(index_path(folder), std::ios::binary);
    if (!in) return;
    struct Rec { uint64_t size; int64_t mtime; int state; double seconds; std::string why; };
    std::map<std::string, Rec> known;
    std::string line;
    while (std::getline(in, line) && known.size() < 100000) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        size_t at = 0;
        for (int i = 0; i < 5; i++) {
            size_t tab = line.find('\t', at);
            if (tab == std::string::npos) break;
            f.push_back(line.substr(at, tab - at));
            at = tab + 1;
        }
        if (f.size() != 5) continue;
        Rec r;
        r.size = strtoull(f[0].c_str(), nullptr, 10);
        r.mtime = strtoll(f[1].c_str(), nullptr, 10);
        r.state = atoi(f[2].c_str());
        r.seconds = atof(f[3].c_str());
        r.why = f[4];
        if (r.state != PLAYS && r.state != BAD) continue;
        known[line.substr(at)] = r;
    }
    for (auto &s : songs) {
        auto k = known.find(s.rel);
        if (k == known.end() || k->second.size != s.size || k->second.mtime != s.mtime) continue;
        s.state = k->second.state;
        s.seconds = k->second.seconds;
        s.why = k->second.why;
    }
}

void save_index(const fs::path &folder, const std::vector<Song> &songs)
{
    std::error_code ec;
    fs::create_directories(folder / ".cache", ec);
    fs::path tmp = folder / ".cache" / "index.tsv.new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << "# FFNY Online custom music: size, time, state (1 plays, 2 bad), seconds, reason, file\n";
        for (auto &s : songs) {
            if (s.state == UNCHECKED) continue;
            std::string why = s.why;
            std::replace(why.begin(), why.end(), '\t', ' ');
            std::replace(why.begin(), why.end(), '\n', ' ');
            char head[96];
            snprintf(head, sizeof(head), "%llu\t%lld\t%d\t%.1f\t", (unsigned long long)s.size, (long long)s.mtime, s.state,
                     s.seconds);
            out << head << why << '\t' << s.rel << '\n';
        }
    }
    fs::rename(tmp, index_path(folder), ec);
    if (ec) fs::remove(tmp, ec);
}

void check(Song &song)
{
    char why[160] = "";
    double seconds = -1;
    std::string p = utf8(song.path);
    if (music_decode_check(p.c_str(), 3.0, &seconds, why, sizeof(why))) {
        song.state = PLAYS;
        song.seconds = seconds;
        song.why.clear();
    } else {
        song.state = BAD;
        song.seconds = -1;
        song.why = why[0] ? why : "cannot be played";
    }
}

static void gather(const fs::path &source, std::vector<fs::path> &files, AddResult &r)
{
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        for (auto &s : scan(source, kMaxFiles, kMaxDepth)) files.push_back(s.path);
        return;
    }
    if (music_decode_known_extension(utf8(source.filename()).c_str())) {
        files.push_back(source);
    } else {
        r.skipped++;
        r.notes.push_back(utf8(source.filename()) + ": not a song (MP3, OGG, FLAC or WAV)");
    }
}

AddResult add(const fs::path &folder, const std::vector<fs::path> &sources,
              const std::function<void(int, int, const std::string &)> &progress)
{
    AddResult r;
    std::error_code ec;
    std::vector<fs::path> files;
    for (auto &s : sources) gather(s, files, r);
    fs::create_directories(folder, ec);
    fs::path canon_folder = fs::weakly_canonical(folder, ec);
    for (size_t i = 0; i < files.size(); i++) {
        const fs::path &src = files[i];
        std::string shown = utf8(src.filename());
        if (progress) progress((int)i, (int)files.size(), shown);
        std::error_code e2;
        fs::path parent = fs::weakly_canonical(src.parent_path(), e2);
        if (!e2 && !canon_folder.empty() && parent == canon_folder) continue;   // already in the folder
        uint64_t size = fs::file_size(src, e2);
        if (e2) { r.skipped++; r.notes.push_back(shown + ": cannot be read"); continue; }
        fs::path target = folder / src.filename();
        bool same = false;
        for (int n = 2; fs::exists(target, e2) && n < 1000; n++) {
            if (fs::file_size(target, e2) == size) { same = true; break; }
            target = folder / from_utf8(utf8(src.stem()) + " (" + std::to_string(n) + ")" + utf8(src.extension()));
        }
        if (same) { r.skipped++; r.notes.push_back(shown + ": already in your music"); continue; }
        fs::path part = target;
        part += ".part";
        if (!fs::copy_file(src, part, fs::copy_options::overwrite_existing, e2)) {
            fs::remove(part, e2);
            r.skipped++;
            r.notes.push_back(shown + ": could not be copied");
            continue;
        }
        fs::rename(part, target, e2);
        if (e2) { fs::remove(part, e2); r.skipped++; r.notes.push_back(shown + ": could not be copied"); continue; }
        r.added++;
    }
    if (progress) progress((int)files.size(), (int)files.size(), "");
    return r;
}

bool remove(const fs::path &folder, const Song &song, std::string *why)
{
    std::error_code ec;
    if (!fs::exists(song.path, ec)) return true;
#if defined(_WIN32)
    if (!getenv("RECOMP_MUSIC_NO_RECYCLE")) {     // tests: no test files in the player's Recycle Bin
    std::wstring from = song.path.native();
    from.push_back(L'\0');                              // double-NUL terminated list
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted && !fs::exists(song.path, ec)) return true;
    }
#endif
    fs::path bin = folder / ".cache" / "removed";
    fs::create_directories(bin, ec);
    fs::rename(song.path, bin / song.path.filename(), ec);
    if (ec) {
        if (why) *why = "could not be removed: " + ec.message();
        return false;
    }
    return true;
}

Mode effective_mode(int mode, size_t songs)
{
    if (mode == MODE_ORIGINAL || songs == 0) return MODE_ORIGINAL;
    if (mode == MODE_MINE) return MODE_MINE;
    return MODE_MIX;
}

FightWatch::Event FightWatch::feed(int phase)
{
    if (phase < 0 || phase > 3) phase = 0;
    int was = last;
    last = phase;
    if (phase == 1) {
        away = 0;
        if (++fighting >= kMinFight) fought = true;
        if (ended) { ended = false; return BEGAN; }
        return NONE;
    }
    fighting = 0;
    if (fought) {
        if (++away >= kSettle) { fought = false; ended = true; away = 0; return ENDED; }
        return NONE;
    }
    if (ended && phase == 0 && was >= 2) { ended = false; return BEGAN; }
    return NONE;
}

} // namespace musiclib
