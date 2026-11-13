// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The player's own songs on disk: see music_library.h.
#define _CRT_SECURE_NO_WARNINGS
#include "music_library.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

// ---- the player's order ----

fs::path order_path(const fs::path &folder) { return folder / "order.txt"; }

std::vector<OrderEntry> load_order(const fs::path &folder, bool *found)
{
    std::vector<OrderEntry> out;
    std::ifstream in(order_path(folder), std::ios::binary);
    if (found) *found = (bool)in;
    if (!in) return out;
    std::string line;
    while (std::getline(in, line) && out.size() < 100000) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos || tab + 1 >= line.size()) continue;
        OrderEntry e;
        e.added = strtoll(line.c_str(), nullptr, 10);
        e.rel = line.substr(tab + 1);
        out.push_back(std::move(e));
    }
    return out;
}

bool save_order(const fs::path &folder, const std::vector<OrderEntry> &order)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    fs::path tmp = folder / "order.txt.new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "# FFNY Online: your songs in your order (the launcher's and the game's Music), with when each was "
               "added (seconds since 1970)\n";
        for (auto &e : order) out << (long long)e.added << '\t' << e.rel << '\n';
        if (!out) return false;
    }
    fs::rename(tmp, order_path(folder), ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

// Song::mtime is in the file clock's seconds (MSVC counts from 1601); "added" is in seconds since 1970.
static int64_t file_clock_offset()
{
    static const int64_t offset = [] {
        auto f = std::chrono::duration_cast<std::chrono::milliseconds>(fs::file_time_type::clock::now().time_since_epoch()).count();
        auto u = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return (int64_t)std::llround((double)(f - u) / 1000.0);
    }();
    return offset;
}

std::vector<OrderEntry> merge_order(const std::vector<OrderEntry> &saved, bool had_list, std::vector<Song> &songs,
                                    int64_t now)
{
    std::map<std::string, size_t> rank;
    for (size_t i = 0; i < saved.size(); i++) rank.emplace(saved[i].rel, i);   // a repeated line: the first counts
    std::vector<size_t> pos(songs.size());
    for (size_t i = 0; i < songs.size(); i++) {
        auto k = rank.find(songs[i].rel);
        if (k != rank.end()) {
            pos[i] = k->second;
            songs[i].added = saved[k->second].added;
        } else {
            pos[i] = saved.size() + i;      // new: after the known ones, in scan order
            songs[i].added = had_list ? now : songs[i].mtime - file_clock_offset();
        }
    }
    std::vector<size_t> idx(songs.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return pos[a] < pos[b]; });
    std::vector<Song> sorted;
    sorted.reserve(songs.size());
    for (size_t i : idx) sorted.push_back(std::move(songs[i]));
    songs.swap(sorted);
    return order_of(songs);
}

std::vector<OrderEntry> order_of(const std::vector<Song> &songs)
{
    std::vector<OrderEntry> out;
    out.reserve(songs.size());
    for (auto &s : songs) out.push_back({s.rel, s.added});
    return out;
}

static bool same_order(const std::vector<OrderEntry> &a, const std::vector<OrderEntry> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].rel != b[i].rel || a[i].added != b[i].added) return false;
    return true;
}

void apply_order(const fs::path &folder, std::vector<Song> &songs)
{
    bool found = false;
    auto saved = load_order(folder, &found);
    int64_t now = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();
    auto merged = merge_order(saved, found, songs, now);
    if (!songs.empty() && (!found || !same_order(saved, merged))) save_order(folder, merged);
}

// ---- the artist ----

static std::string trim(std::string s)
{
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && ((unsigned char)s[b - 1] <= ' ')) b--;
    return s.substr(a, b - a);
}

std::string artist_from_name(const std::string &name, const std::string &rel)
{
    size_t dash = name.find(" - ");
    if (dash != std::string::npos && dash > 0) {
        std::string a = trim(name.substr(0, dash));
        // "01 - Title": a track number is no artist
        bool digits = !a.empty() && std::all_of(a.begin(), a.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (!a.empty() && !digits) return a;
    }
    size_t slash = rel.rfind('/');
    if (slash != std::string::npos) {
        std::string dir = rel.substr(0, slash);
        size_t up = dir.rfind('/');
        return trim(up == std::string::npos ? dir : dir.substr(up + 1));
    }
    return "";
}

static void put_utf8(std::string &out, unsigned cp)
{
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

// An ID3v2 text frame's value (its encoding byte first), up to the first NUL, as UTF-8.
static std::string id3_text(const unsigned char *p, size_t n)
{
    std::string out;
    if (!n) return out;
    int enc = p[0];
    p++, n--;
    if (enc == 0) {                                 // ISO-8859-1
        for (size_t i = 0; i < n && p[i]; i++) put_utf8(out, p[i]);
    } else if (enc == 3) {                          // UTF-8
        for (size_t i = 0; i < n && p[i]; i++) out += (char)p[i];
    } else {                                        // UTF-16 with a BOM (1) or big-endian (2)
        bool be = enc == 2;
        size_t i = 0;
        if (enc == 1 && n >= 2) {
            if (p[0] == 0xFE && p[1] == 0xFF) { be = true; i = 2; }
            else if (p[0] == 0xFF && p[1] == 0xFE) i = 2;
        }
        for (; i + 1 < n; i += 2) {
            unsigned c = be ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
            if (!c) break;
            if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
                unsigned d = be ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
                if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i += 2; }
            }
            put_utf8(out, c);
        }
    }
    return trim(out);
}

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }
static uint32_t syncsafe(const unsigned char *p) { return (uint32_t)(p[0] & 0x7F) << 21 | (p[1] & 0x7F) << 14 | (p[2] & 0x7F) << 7 | (p[3] & 0x7F); }

// ARTIST= in a Vorbis comment block (FLAC's VORBIS_COMMENT, Ogg Vorbis' comment header): vendor, count, entries.
static std::string vorbis_artist(const unsigned char *p, size_t n)
{
    if (n < 8) return "";
    size_t at = 4 + (size_t)le32(p);
    if (at + 4 > n) return "";
    uint32_t count = le32(p + at);
    at += 4;
    for (uint32_t i = 0; i < count && at + 4 <= n; i++) {
        size_t len = le32(p + at);
        at += 4;
        if (len > n - at) break;
        std::string c((const char *)p + at, len);
        at += len;
        if (c.size() > 7) {
            std::string key = c.substr(0, 7);
            for (auto &ch : key) ch = (char)toupper((unsigned char)ch);
            if (key == "ARTIST=") return trim(c.substr(7));
        }
    }
    return "";
}

static std::string tag_artist(const fs::path &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    auto read = [&](std::vector<unsigned char> &buf, size_t n) {
        buf.resize(n);
        f.read((char *)buf.data(), (std::streamsize)n);
        buf.resize((size_t)f.gcount());
        return buf.size() == n;
    };
    std::vector<unsigned char> h, body;
    if (!read(h, 10)) return "";
    if (!memcmp(h.data(), "ID3", 3) && h[3] >= 2 && h[3] <= 4) {
        int ver = h[3];
        size_t size = std::min<size_t>(syncsafe(h.data() + 6), 4u << 20);
        size_t at = 0, head = ver == 2 ? 6 : 10;
        std::string band;                           // TPE2 (album artist) when there is no TPE1
        while (at + head <= size) {
            if (!read(h, head)) break;
            at += head;
            if (!h[0]) break;                       // padding
            size_t len = ver == 2 ? ((size_t)h[3] << 16 | h[4] << 8 | h[5]) : ver == 4 ? syncsafe(h.data() + 4) : be32(h.data() + 4);
            if (len > size - at) break;
            bool lead = ver == 2 ? !memcmp(h.data(), "TP1", 3) : !memcmp(h.data(), "TPE1", 4);
            bool second = ver == 2 ? !memcmp(h.data(), "TP2", 3) : !memcmp(h.data(), "TPE2", 4);
            if ((lead || (second && band.empty())) && len < 4096) {
                if (!read(body, len)) break;
                std::string v = id3_text(body.data(), len);
                if (lead && !v.empty()) return v;
                if (second) band = v;
            } else {
                f.seekg((std::streamoff)len, std::ios::cur);
            }
            at += len;
        }
        if (!band.empty()) return band;
        // ID3v1 at the end
        f.clear();
        f.seekg(-128, std::ios::end);
        if (read(body, 128) && !memcmp(body.data(), "TAG", 3)) {
            std::string v;
            for (int i = 33; i < 63 && body[i]; i++) put_utf8(v, body[i]);
            return trim(v);
        }
        return "";
    }
    if (!memcmp(h.data(), "fLaC", 4)) {
        f.seekg(4);
        for (int blocks = 0; blocks < 64; blocks++) {
            if (!read(h, 4)) break;
            size_t len = (size_t)h[1] << 16 | h[2] << 8 | h[3];
            if ((h[0] & 0x7F) == 4) {
                if (len > (1u << 20) || !read(body, len)) break;
                return vorbis_artist(body.data(), len);
            }
            if (h[0] & 0x80) break;                 // the last block
            f.seekg((std::streamoff)len, std::ios::cur);
        }
        return "";
    }
    if (!memcmp(h.data(), "OggS", 4)) {
        f.seekg(0);
        read(body, 64 * 1024);
        static const unsigned char mark[] = {3, 'v', 'o', 'r', 'b', 'i', 's'};
        auto it = std::search(body.begin(), body.end(), mark, mark + 7);
        if (it == body.end()) return "";
        size_t at = (size_t)(it - body.begin()) + 7;
        return vorbis_artist(body.data() + at, body.size() - at);
    }
    if (!memcmp(h.data(), "RIFF", 4) && !memcmp(h.data() + 8, "WA", 2)) {
        f.seekg(12);
        for (int chunks = 0; chunks < 64; chunks++) {
            if (!read(h, 8)) break;
            size_t len = le32(h.data() + 4);
            if (!memcmp(h.data(), "LIST", 4) && len >= 4 && len < (1u << 20)) {
                if (!read(body, len)) break;
                if (memcmp(body.data(), "INFO", 4)) continue;
                for (size_t at = 4; at + 8 <= len;) {
                    size_t sub = le32(body.data() + at + 4);
                    if (sub > len - at - 8) break;
                    if (!memcmp(body.data() + at, "IART", 4)) {
                        std::string v;
                        for (size_t i = 0; i < sub && body[at + 8 + i]; i++) v += (char)body[at + 8 + i];
                        return trim(v);
                    }
                    at += 8 + sub + (sub & 1);
                }
                continue;
            }
            f.seekg((std::streamoff)(len + (len & 1)), std::ios::cur);
        }
    }
    return "";
}

void read_artist(Song &song)
{
    std::string a;
    try {
        a = tag_artist(song.path);
    } catch (...) {
        a.clear();
    }
    song.artist = a.empty() ? artist_from_name(song.name, song.rel) : a;
}

// ---- sorting ----

static int compare_text(const std::string &a, const std::string &b)
{
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
}

void sort_songs(std::vector<Song> &songs, int sort)
{
    switch (sort) {
    case SORT_NAME:
        std::stable_sort(songs.begin(), songs.end(), [](const Song &a, const Song &b) { return compare_text(a.name, b.name) < 0; });
        break;
    case SORT_ARTIST:
        std::stable_sort(songs.begin(), songs.end(), [](const Song &a, const Song &b) {
            if (a.artist.empty() != b.artist.empty()) return b.artist.empty();      // no artist: last
            int c = compare_text(a.artist, b.artist);
            return c ? c < 0 : compare_text(a.name, b.name) < 0;
        });
        break;
    case SORT_ADDED:
        std::stable_sort(songs.begin(), songs.end(), [](const Song &a, const Song &b) { return a.added > b.added; });
        break;
    default:
        break;
    }
}

size_t move_song(std::vector<Song> &songs, size_t at, int delta)
{
    if (at >= songs.size()) return at;
    long to = (long)at + delta;
    to = std::max(0L, std::min((long)songs.size() - 1, to));
    Song s = std::move(songs[at]);
    songs.erase(songs.begin() + (long)at);
    songs.insert(songs.begin() + to, std::move(s));
    return (size_t)to;
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

void LevelMeter::feed(const int16_t *stereo, long frames)
{
    const long per_block = rate > 0 ? (long)rate * 2 / 5 : 17640;
    for (long i = 0; i < frames; i++) {
        double l = stereo[2 * i] / 32768.0, r = stereo[2 * i + 1] / 32768.0;
        block_sum += 0.5 * (l * l + r * r);
        if (++block_frames >= per_block) {
            double ms = block_sum / (double)block_frames;
            if (ms > kGate * kGate) {
                gated_sum += ms;
                gated_blocks++;
            }
            block_sum = 0;
            block_frames = 0;
        }
    }
}

double LevelMeter::rms() const
{
    return gated_blocks ? std::sqrt(gated_sum / (double)gated_blocks) : 0.0;
}

double LevelMeter::gain() const
{
    double level = rms();
    if (level <= 0) return 1.0;
    return std::max(kMinGain, std::min(kMaxGain, kTarget / level));
}

void Playlist::reset(size_t own_songs, size_t game_turns, bool shuffled, uint64_t seed)
{
    own = own_songs;
    game = game_turns;
    shuffle = shuffled;
    rng.seed(seed);
    last = (size_t)-1;
    new_round();
}

void Playlist::new_round()
{
    order.clear();
    if (shuffle) {
        for (size_t i = 0; i < own + game; i++) order.push_back(i);
        std::shuffle(order.begin(), order.end(), rng);
        // no immediate repeat across rounds (one game turn after another is the game's next track)
        if (order.size() > 1 && order[0] == last) std::swap(order[0], order[1 + rng() % (order.size() - 1)]);
    } else {
        size_t g = 0;
        for (size_t i = 0; i < own; i++) {
            order.push_back(i);
            if (g < game) order.push_back(own + g++);
        }
        while (g < game) order.push_back(own + g++);
    }
    at = 0;
}

void Playlist::continue_after(size_t own_item)
{
    last = own_item;            // shuffled: the new round does not start with it
    new_round();
    if (shuffle) return;
    for (size_t i = 0; i < order.size(); i++)
        if (order[i] == own_item) { at = i + 1; return; }
}

size_t Playlist::next(bool avoid_game)
{
    if (own + game == 0) return 0;
    if (at >= order.size()) new_round();
    if (avoid_game && own && is_game(order[at])) {
        size_t j = at;
        while (j < order.size() && is_game(order[j])) j++;
        if (j == order.size()) {            // only game turns left in this round: the next round
            new_round();
            j = at;
            while (j < order.size() && is_game(order[j])) j++;
        }
        if (j < order.size()) std::swap(order[at], order[j]);
    }
    last = order[at++];
    return last;
}

} // namespace musiclib
