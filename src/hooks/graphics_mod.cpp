// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// "Enhanced graphics (community)", the work underneath: the manifest, downloads, install, switch and
// removal. See graphics_mod.h. The launcher's part (background work, settings, the log line) is
// graphics_mod_launcher.cpp.
#define _CRT_SECURE_NO_WARNINGS
#include "graphics_mod.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>

extern "C" {
#include "update_sig.h"
}

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
extern "C" const wchar_t *updater_host(int i);
extern "C" int updater_port(void);
#endif

namespace gfx
{
namespace
{
constexpr size_t kManifestMax = 1 << 20;
constexpr uint64_t kArchiveMax = 2ull << 30;

const char *const kProxy = "dxgi.dll", *const kParked = "dxgi.ffny-mod";

bool same_name(const std::string &a, const char *b)
{
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

// The name a pack file has in bin: the proxy is kept where nothing loads it (see graphics_mod.h).
std::string on_disk_name(const std::string &rel) { return same_name(rel, kProxy) ? kParked : rel; }

void say(std::string *error, const std::string &text)
{
    if (error) *error = text;
    fprintf(stderr, "[GRAPHICS] %s\n", text.c_str());
    fflush(stderr);
}

std::string utf8(const fs::path &p)
{
#if defined(_WIN32)
    const std::wstring &w = p.native();
    int n = w.empty() ? 0 : WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, nullptr, nullptr);
    return out;
#else
    return p.string();
#endif
}
fs::path from_utf8(const std::string &s) { return fs::u8path(s); }

// An update's downloads stop when the game starts (update() sets it for its thread).
thread_local const std::function<bool()> *t_go_on = nullptr;
bool stopped() { return t_go_on && *t_go_on && !(*t_go_on)(); }
const char *const kStopped = "The game started; the graphics pack update waits for the next launcher start.";

// ReShade and its add-ons write their own settings back when the program closes: ours to replace.
bool settings_file(const std::string &f)
{
    std::string ext = from_utf8(f).extension().string();
    return _stricmp(ext.c_str(), ".ini") == 0 || _stricmp(ext.c_str(), ".cfg") == 0 || _stricmp(ext.c_str(), ".cache") == 0;
}

// ---- a small JSON reader, enough for graphics.json ----
struct Json
{
    enum Kind { NUL, NUM, STR, ARR, OBJ } kind = NUL;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;
    const Json &at(const char *key) const
    {
        static const Json none;
        auto it = obj.find(key);
        return it == obj.end() ? none : it->second;
    }
};

struct Parser
{
    const char *p, *end;
    int depth = 0;
    void ws() { while (p < end && isspace((unsigned char)*p)) p++; }
    bool str(std::string &out)
    {
        if (p >= end || *p != '"') return false;
        for (p++; p < end && *p != '"'; p++) {
            if (*p == '\\') {
                if (++p >= end) return false;
                char c = *p;
                if (c == 'u') {
                    if (end - p < 5) return false;
                    unsigned v = (unsigned)strtoul(std::string(p + 1, p + 5).c_str(), nullptr, 16);
                    p += 4;
                    if (v < 0x80) out += (char)v;
                    else if (v < 0x800) { out += (char)(0xC0 | (v >> 6)); out += (char)(0x80 | (v & 0x3F)); }
                    else { out += (char)(0xE0 | (v >> 12)); out += (char)(0x80 | ((v >> 6) & 0x3F)); out += (char)(0x80 | (v & 0x3F)); }
                } else {
                    out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 'b' ? '\b' : c == 'f' ? '\f' : c;
                }
            } else {
                out += *p;
            }
        }
        if (p >= end) return false;
        p++;
        return true;
    }
    bool value(Json &v)
    {
        if (++depth > 32) return false;
        ws();
        if (p >= end) return false;
        bool ok = true;
        if (*p == '{') {
            v.kind = Json::OBJ;
            p++; ws();
            if (p < end && *p == '}') { p++; depth--; return true; }
            for (;;) {
                std::string key;
                ws();
                if (!str(key)) return false;
                ws();
                if (p >= end || *p++ != ':') return false;
                if (!value(v.obj[key])) return false;
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; break; }
                return false;
            }
        } else if (*p == '[') {
            v.kind = Json::ARR;
            p++; ws();
            if (p < end && *p == ']') { p++; depth--; return true; }
            for (;;) {
                v.arr.emplace_back();
                if (!value(v.arr.back())) return false;
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; break; }
                return false;
            }
        } else if (*p == '"') {
            v.kind = Json::STR;
            ok = str(v.str);
        } else if (*p == '-' || isdigit((unsigned char)*p)) {
            v.kind = Json::NUM;
            char *e = nullptr;
            v.num = strtod(std::string(p, std::min<size_t>(end - p, 40)).c_str(), &e);
            while (p < end && (isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E')) p++;
        } else if (end - p >= 4 && !strncmp(p, "null", 4)) {
            p += 4;
        } else if (end - p >= 4 && !strncmp(p, "true", 4)) {
            v.kind = Json::NUM; v.num = 1; p += 4;
        } else if (end - p >= 5 && !strncmp(p, "false", 5)) {
            v.kind = Json::NUM; p += 5;
        } else {
            return false;
        }
        depth--;
        return ok;
    }
};

// A path inside bin from the manifest: relative, '/' separated, no "..", no drive, no odd characters.
bool safe_rel(const std::string &rel)
{
    if (rel.empty() || rel.size() > 240 || rel[0] == '/' || rel[0] == '\\') return false;
    for (char c : rel)
        if ((unsigned char)c < 0x20 || strchr(":*?\"<>|\\", c)) return false;
    size_t at = 0;
    while (at <= rel.size()) {
        size_t slash = rel.find('/', at);
        if (slash == std::string::npos) slash = rel.size();
        std::string part = rel.substr(at, slash - at);
        if (part.empty() || part == "." || part == "..") return false;
        at = slash + 1;
    }
    return true;
}

bool hex64(const std::string &s)
{
    if (s.size() != 64) return false;
    for (char c : s) if (!isxdigit((unsigned char)c)) return false;
    return true;
}

bool read_archive(const Json &j, Archive &a)
{
    a.path = j.at("path").str;
    a.sha256 = j.at("sha256").str;
    double size = j.at("size").num;
    if (j.at("size").kind != Json::NUM || size <= 0 || size > (double)kArchiveMax) return false;
    a.size = (uint64_t)size;
    for (auto &c : a.sha256) c = (char)tolower((unsigned char)c);
    return safe_rel(a.path) && a.path.find('/') == std::string::npos && hex64(a.sha256);
}

bool read_list(const Json &j, std::vector<std::string> &out)
{
    if (j.kind != Json::ARR || j.arr.empty() || j.arr.size() > 20000) return false;
    for (auto &e : j.arr) {
        if (e.kind != Json::STR || !safe_rel(e.str)) return false;
        out.push_back(e.str);
    }
    return true;
}

// ---- HTTP ----
#if defined(_WIN32)
struct Server { std::wstring host; INTERNET_PORT port; bool https; std::wstring prefix; };

bool local_host(const std::string &host) { return host == "127.0.0.1" || host == "localhost" || host == "::1"; }

std::string test_server_host()
{
    const char *s = getenv("RECOMP_GRAPHICS_SERVER");
    if (!s || !*s) return "";
    std::string v = s;
    return v.substr(0, v.rfind(':'));
}

std::vector<Server> servers()
{
    std::vector<Server> out;
    if (const char *s = getenv("RECOMP_GRAPHICS_SERVER")) {
        std::string v = s;
        size_t colon = v.rfind(':');
        if (colon != std::string::npos) {
            std::string host = v.substr(0, colon);
            out.push_back({std::wstring(host.begin(), host.end()), (INTERNET_PORT)atoi(v.c_str() + colon + 1), false,
                           L"/graphics/"});
            return out;
        }
    }
    for (int i = 0; updater_host(i); i++) out.push_back({updater_host(i), (INTERNET_PORT)updater_port(), false, L"/graphics/"});
    out.push_back({updater_host(0) ? updater_host(0) : L"play.defjam.online", INTERNET_DEFAULT_HTTPS_PORT, true,
                   L"/updates/graphics/"});
    return out;
}

// GET server+prefix+name from byte `from`; the sink gets the body. Returns the HTTP status (0: no answer).
// *partial says whether the server honoured the range (206).
int http_get(const Server &sv, const std::string &name, uint64_t from, uint64_t *length,
             const std::function<bool(const char *, DWORD)> &sink, bool *partial)
{
    HINTERNET session = nullptr, connect = nullptr, request = nullptr;
    DWORD status = 0, size = sizeof(status);
    std::wstring path = sv.prefix + std::wstring(name.begin(), name.end());
    *partial = false;
    session = WinHttpOpen(L"DefJamRecomp-Graphics/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) WinHttpSetTimeouts(session, 5000, 5000, 15000, 30000);
    if (session) connect = WinHttpConnect(session, sv.host.c_str(), sv.port, 0);
    if (connect) request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, sv.https ? WINHTTP_FLAG_SECURE : 0);
    std::wstring range;
    if (from) range = L"Range: bytes=" + std::to_wstring(from) + L"-";
    bool ok = request && WinHttpSendRequest(request, from ? range.c_str() : WINHTTP_NO_ADDITIONAL_HEADERS,
                                            from ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr) &&
              WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status,
                                  &size, nullptr);
    if (ok && (status == 200 || status == 206)) {
        wchar_t text[32];
        DWORD tsize = sizeof(text);
        *partial = status == 206;
        if (length) *length = WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, text, &tsize, nullptr)
                                  ? _wcstoui64(text, nullptr, 10) : 0;
        for (;;) {
            static thread_local char buf[256 * 1024];
            DWORD avail = 0, got = 0;
            if (!WinHttpQueryDataAvailable(request, &avail)) { status = 0; break; }
            if (!avail) break;
            if (avail > sizeof(buf)) avail = sizeof(buf);
            if (!WinHttpReadData(request, buf, avail, &got)) { status = 0; break; }
            if (!got) break;
            if (!sink(buf, got)) { status = 0; break; }
        }
    } else if (!ok) {
        status = 0;
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return (int)status;
}

bool get_small(const std::string &name, std::string &out, size_t max, const Server **used)
{
    static std::vector<Server> list;
    list = servers();
    for (auto &sv : list) {
        out.clear();
        bool partial;
        int st = http_get(sv, name, 0, nullptr, [&](const char *d, DWORD n) {
            if (out.size() + n > max) return false;
            out.append(d, n);
            return true;
        }, &partial);
        if (st == 200) { if (used) *used = &sv; return true; }
    }
    return false;
}

bool file_sha256(const fs::path &path, std::string &hex)
{
    BCRYPT_HASH_HANDLE h = nullptr;
    unsigned char digest[32];
    std::ifstream in(path, std::ios::binary);
    if (!in || BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &h, nullptr, 0, nullptr, 0, 0)) return false;
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        if (in.gcount() > 0) BCryptHashData(h, (PUCHAR)buf.data(), (ULONG)in.gcount(), 0);
    }
    BCryptFinishHash(h, digest, 32, 0);
    BCryptDestroyHash(h);
    char t[65];
    for (int i = 0; i < 32; i++) snprintf(t + 2 * i, 3, "%02x", digest[i]);
    hex = t;
    return true;
}
#endif

// ---- the archives: ZIP, STORED entries only (make-graphics-pack.py writes them so) ----
uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

struct Entry { std::string name; uint64_t offset, size; };

bool zip_entries(std::ifstream &in, uint64_t file_size, std::vector<Entry> &out, std::string *error)
{
    size_t tail = (size_t)std::min<uint64_t>(file_size, 65536 + 22);
    std::vector<unsigned char> t(tail);
    in.seekg((std::streamoff)(file_size - tail));
    in.read((char *)t.data(), (std::streamsize)tail);
    if (!in) { say(error, "the archive cannot be read"); return false; }
    size_t e = tail;
    for (size_t i = tail - 22 + 1; i-- > 0;)
        if (le32(&t[i]) == 0x06054b50) { e = i; break; }
    if (e == tail) { say(error, "the archive is damaged (no directory)"); return false; }
    uint32_t count = le16(&t[e + 10]), cd_size = le32(&t[e + 12]), cd_off = le32(&t[e + 16]);
    if ((uint64_t)cd_off + cd_size > file_size || count > 20000) { say(error, "the archive is damaged (directory)"); return false; }
    std::vector<unsigned char> cd(cd_size);
    in.seekg(cd_off);
    in.read((char *)cd.data(), cd_size);
    size_t p = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 46 > cd.size() || le32(&cd[p]) != 0x02014b50) { say(error, "the archive is damaged (entry)"); return false; }
        uint16_t method = le16(&cd[p + 10]), nlen = le16(&cd[p + 28]), xlen = le16(&cd[p + 30]), clen = le16(&cd[p + 32]);
        uint32_t csize = le32(&cd[p + 20]), usize = le32(&cd[p + 24]), local = le32(&cd[p + 42]);
        if (p + 46 + nlen > cd.size()) { say(error, "the archive is damaged (name)"); return false; }
        std::string name((const char *)&cd[p + 46], nlen);
        p += 46 + nlen + xlen + clen;
        if (!name.empty() && name.back() == '/') continue;
        if (method != 0 || csize != usize) { say(error, "the archive has a compressed entry; only stored ones are used"); return false; }
        unsigned char lh[30];
        in.seekg(local);
        in.read((char *)lh, 30);
        if (!in || le32(lh) != 0x04034b50) { say(error, "the archive is damaged (local header)"); return false; }
        uint64_t data = (uint64_t)local + 30 + le16(lh + 26) + le16(lh + 28);
        if (data + usize > file_size) { say(error, "the archive is cut short"); return false; }
        out.push_back({name, data, usize});
    }
    return true;
}

// Extracts `want` (all of them, nothing else) from the archive into bin, each through a temporary name.
bool extract(const fs::path &zip, const fs::path &bin, const std::vector<std::string> &want, std::string *error)
{
    std::error_code ec;
    uint64_t size = fs::file_size(zip, ec);
    std::ifstream in(zip, std::ios::binary);
    std::vector<Entry> entries;
    if (ec || !in || !zip_entries(in, size, entries, error)) return false;
    std::set<std::string> wanted(want.begin(), want.end()), seen;
    for (auto &e : entries) {
        if (!wanted.count(e.name)) { say(error, "the archive holds a file the manifest does not list: " + e.name); return false; }
        seen.insert(e.name);
    }
    if (seen.size() != wanted.size()) { say(error, "the archive lacks files the manifest lists"); return false; }
    std::vector<char> buf(1 << 20);
    for (auto &e : entries) {
        fs::path target = bin / from_utf8(on_disk_name(e.name)), part = target;
        part += ".ffny-part";
        fs::create_directories(target.parent_path(), ec);
        {
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            if (!out) { say(error, "cannot write " + e.name + " in the game folder"); return false; }
            in.seekg((std::streamoff)e.offset);
            uint64_t left = e.size;
            while (left) {
                size_t n = (size_t)std::min<uint64_t>(left, buf.size());
                in.read(buf.data(), (std::streamsize)n);
                if (!in) { say(error, "the archive cannot be read"); return false; }
                out.write(buf.data(), (std::streamsize)n);
                left -= n;
            }
            if (!out) { say(error, "cannot write " + e.name + " (disk full?)"); return false; }
        }
        fs::rename(part, target, ec);
        if (ec) {
            fs::remove(part, ec);
            say(error, "cannot replace " + e.name + " (in use?)");
            return false;
        }
    }
    return true;
}

fs::path record_path(const fs::path &data) { return data / "graphics" / "installed.txt"; }
fs::path pending_path(const fs::path &data) { return data / "graphics" / "pending-delete.txt"; }
fs::path marker_path(const fs::path &data) { return data / "graphics" / "play-started.txt"; }
fs::path skip_path(const fs::path &data) { return data / "graphics" / "skip-next-play.txt"; }

void touch(const fs::path &file)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "FFNY Online: Enhanced graphics start guard\n";
}

void write_record(const fs::path &data, const Record &r)
{
    std::error_code ec;
    fs::create_directories(data / "graphics", ec);
    fs::path tmp = record_path(data);
    tmp += ".new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "# FFNY Online: the Enhanced graphics files in the game folder (removed exactly by Turn off)\n";
        out << "version " << r.version << "\npreset " << r.preset << "\nbin " << utf8(r.bin) << "\n";
        if (!r.base_sha.empty()) out << "base-sha256 " << r.base_sha << "\n";
        if (!r.preset_sha.empty()) out << "preset-sha256 " << r.preset_sha << "\n";
        for (auto &d : r.dirs) out << "dir " << d << "\n";
        for (auto &f : r.files) out << "file " << f << "\n";
    }
    fs::rename(tmp, record_path(data), ec);
}

bool delete_or_park(const fs::path &data, const fs::path &file)
{
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    if (fs::remove(file, ec)) return true;
    // held by the running program (the loaded dxgi.dll): move it aside, delete it at the next start
    fs::path parked = file;
    parked += ".ffny-delete";
    fs::remove(parked, ec);
    fs::rename(file, parked, ec);
    if (ec) return false;
    std::ofstream(pending_path(data), std::ios::binary | std::ios::app) << utf8(parked) << "\n";
    return true;
}
} // namespace

Record read_record(const fs::path &data)
{
    Record r;
    std::ifstream in(record_path(data), std::ios::binary);
    std::string line;
    while (in && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t sp = line.find(' ');
        if (line.empty() || line[0] == '#' || sp == std::string::npos) continue;
        std::string k = line.substr(0, sp), v = line.substr(sp + 1);
        r.present = true;
        if (k == "version") r.version = v;
        else if (k == "preset") r.preset = v;
        else if (k == "bin") r.bin = from_utf8(v);
        else if (k == "base-sha256") r.base_sha = v;
        else if (k == "preset-sha256") r.preset_sha = v;
        else if (k == "dir" && safe_rel(v)) r.dirs.push_back(v);
        else if (k == "file" && safe_rel(v)) r.files.push_back(v);
    }
    return r;
}

bool tidy_proxy(const fs::path &data)
{
    std::error_code ec;
    Record r = read_record(data);
    if (!r.present || r.bin.empty()) return true;
    auto old = std::find_if(r.files.begin(), r.files.end(), [](const std::string &f) { return same_name(f, kProxy); });
    bool old_form = old != r.files.end();
    if (!old_form && std::find(r.files.begin(), r.files.end(), kParked) == r.files.end()) return true;
    fs::path live = r.bin / kProxy, parked = r.bin / kParked;
    bool have_live = fs::exists(live, ec), have_parked = fs::exists(parked, ec);
    if (have_live && (old_form || !have_parked)) {
        // Ours by the record: an install from before the proxy was parked, or a start that stopped before it
        // was renamed back. Windows renames a loaded DLL (this very process may hold it): the mapping stays
        // and the next start is clean.
        if (have_parked) fs::remove(parked, ec);
        fs::rename(live, parked, ec);
        if (ec) {
            fprintf(stderr, "[GRAPHICS] cannot rename the proxy yet (%s); trying again later\n", ec.message().c_str());
            fflush(stderr);
            return false;
        }
        fprintf(stderr, "[GRAPHICS] proxy parked as %s\n", kParked);
    }
    if (old_form) {
        *old = kParked;
        write_record(data, r);
    }
    return true;
}

bool begin_play(const fs::path &data, const fs::path &bin, bool enabled)
{
    std::error_code ec;
    bool tidy = tidy_proxy(data);
    if (!enabled) return false;
    Record r = read_record(data);
    if (!r.present || r.files.empty() || !fs::equivalent(r.bin, bin, ec)) return false;
    if (guard_tripped(data)) {
        clear_guard(data);
        fprintf(stderr, "[GRAPHICS] this start is without the community mod: the last one did not get through\n");
        fflush(stderr);
        return false;
    }
    fs::path live = r.bin / kProxy, parked = r.bin / kParked;
    if (fs::exists(live, ec)) {
        if (tidy) {         // tidy_proxy left it: both names exist, so this one is not ours
            fprintf(stderr, "[GRAPHICS] a dxgi.dll the launcher did not install is in the game folder; not starting the mod\n");
            fflush(stderr);
            return false;
        }
        touch(marker_path(data));       // ours, and it could not be renamed: it loads as it is
        return true;
    }
    if (!fs::exists(parked, ec)) {
        fprintf(stderr, "[GRAPHICS] the proxy is missing; starting without the community mod\n");
        fflush(stderr);
        return false;
    }
    touch(marker_path(data));
    fs::rename(parked, live, ec);
    if (ec) {
        fs::remove(marker_path(data), ec);
        fprintf(stderr, "[GRAPHICS] cannot put the proxy in place; starting without the community mod\n");
        fflush(stderr);
        return false;
    }
    fprintf(stderr, "[GRAPHICS] proxy in place for the game's start\n");
    fflush(stderr);
    return true;
}

void start_failed(const fs::path &data)
{
    std::error_code ec;
    tidy_proxy(data);
    fs::remove(marker_path(data), ec);
}

bool guard_note_launcher_start(const fs::path &data)
{
    std::error_code ec;
    if (fs::exists(marker_path(data), ec)) {
        touch(skip_path(data));
        fs::remove(marker_path(data), ec);
        fprintf(stderr, "[GRAPHICS] the last start with the community mod did not get through; the next one is without it\n");
        fflush(stderr);
    }
    return guard_tripped(data);
}

bool guard_tripped(const fs::path &data)
{
    std::error_code ec;
    return fs::exists(skip_path(data), ec) || fs::exists(marker_path(data), ec);
}

void clear_guard(const fs::path &data)
{
    std::error_code ec;
    fs::remove(skip_path(data), ec);
    fs::remove(marker_path(data), ec);
}

#if defined(_WIN32)
static bool loaded_from(HANDLE process, const fs::path &bin)
{
    HMODULE mods[1024];
    DWORD got = 0;
    if (!K32EnumProcessModulesEx(process, mods, sizeof(mods), &got, LIST_MODULES_ALL)) return false;    // still starting
    std::error_code ec;
    for (DWORD i = 0; i < got / sizeof(HMODULE) && i < 1024; i++) {
        wchar_t path[MAX_PATH * 2];
        if (!K32GetModuleFileNameExW(process, mods[i], path, MAX_PATH * 2)) continue;
        fs::path p(path);
        if (_wcsicmp(p.filename().c_str(), L"dxgi.dll") == 0 && fs::equivalent(p.parent_path(), bin, ec)) return true;
    }
    return false;
}

void watch_play(void *child, const fs::path &data, unsigned long guard_ms)
{
    HANDLE h = (HANDLE)child;
    Record r = read_record(data);
    ULONGLONG start = GetTickCount64();
    bool exited = false, loaded = false;
    while (!exited && !loaded && GetTickCount64() - start < 30000) {
        exited = WaitForSingleObject(h, 25) == WAIT_OBJECT_0;
        if (!exited) loaded = loaded_from(h, r.bin);
    }
    // A moment more than "loaded": the proxy's own start-up may still look at its file.
    if (loaded) exited = WaitForSingleObject(h, 2000) == WAIT_OBJECT_0;
    bool back = false;
    for (int i = 0; i < 50 && !back; i++) {
        back = tidy_proxy(data);
        if (!back) Sleep(100);
    }
    fprintf(stderr, "[GRAPHICS] the game %s the proxy after %llu ms; %s\n", loaded ? "loaded" : "did not load",
            (unsigned long long)(GetTickCount64() - start), back ? "parked again" : "still in place, parked at the next start");
    if (!exited) {
        ULONGLONG spent = GetTickCount64() - start;
        exited = spent < guard_ms && WaitForSingleObject(h, (DWORD)(guard_ms - spent)) == WAIT_OBJECT_0;
    }
    DWORD code = 1;
    if (exited) GetExitCodeProcess(h, &code);
    std::error_code ec;
    if (!exited || code == 0) fs::remove(marker_path(data), ec);    // running, or closed normally: it made it
    else fprintf(stderr, "[GRAPHICS] the game ended early with the mod (code 0x%lx)\n", (unsigned long)code);
    fflush(stderr);
}

bool fetch_manifest(Manifest &m, std::string *error)
{
    std::string text, sig;
    const Server *used = nullptr;
    if (!get_small("graphics.json", text, kManifestMax, &used)) {
        say(error, "Could not reach the update server for the graphics pack.");
        return false;
    }
    std::string sig_name = "graphics.json.sig";
    bool partial;
    if (http_get(*used, sig_name, 0, nullptr, [&](const char *d, DWORD n) { sig.append(d, n); return sig.size() < 1024; },
                 &partial) != 200) {
        say(error, "The graphics pack's signature is missing on the server.");
        return false;
    }
    while (!sig.empty() && isspace((unsigned char)sig.back())) sig.pop_back();
    const char *key = nullptr;
    std::string test_key;
    if (const char *k = getenv("RECOMP_GRAPHICS_TEST_KEY")) {
        if (!local_host(test_server_host())) {
            say(error, "A test key is only taken from a server on this PC; ignored.");
        } else {
            test_key = k;
            key = test_key.c_str();
        }
    }
    if (!update_signature_ok(text.data(), (unsigned long)text.size(), sig.c_str(), key)) {
        say(error, "The graphics pack's information is not signed by the project; nothing was installed.");
        return false;
    }
    Json j;
    Parser ps{text.data(), text.data() + text.size()};
    if (!ps.value(j) || j.kind != Json::OBJ || j.at("format").num != 1) {
        say(error, "The graphics pack's information has a format this launcher does not know; update the game.");
        return false;
    }
    m = Manifest();
    m.version = j.at("version").str;
    m.notices = j.at("notices").str;
    m.credits = j.at("credits").str;
    const Json &a = j.at("archives"), &in = j.at("installs");
    if (!read_archive(a.at("base"), m.base) || !read_archive(a.at("quality"), m.quality) ||
        !read_archive(a.at("performance"), m.performance) || !read_list(in.at("base"), m.base_files) ||
        !read_list(in.at("quality"), m.quality_files) || !read_list(in.at("performance"), m.performance_files) ||
        m.version.empty() || (!m.notices.empty() && !safe_rel(m.notices))) {
        say(error, "The graphics pack's information is incomplete; nothing was installed.");
        return false;
    }
    fprintf(stderr, "[GRAPHICS] pack %s: base %llu bytes, %zu files\n", m.version.c_str(), (unsigned long long)m.base.size,
            m.base_files.size());
    return true;
}

fs::path ensure_archive(const Archive &a, const fs::path &cache, const Progress &progress, std::string *error)
{
    std::error_code ec;
    fs::create_directories(cache, ec);
    fs::path final_path = cache / a.path, part = final_path;
    part += ".part";
    std::string hex;
    if (fs::exists(final_path, ec) && fs::file_size(final_path, ec) == a.size && file_sha256(final_path, hex) && hex == a.sha256)
        return final_path;
    fs::remove(final_path, ec);
    auto list = servers();
    for (int attempt = 0; attempt < 6; attempt++) {
        if (stopped()) { say(error, kStopped); return fs::path(); }     // the .part stays: resumed next time
        uint64_t have = fs::exists(part, ec) ? fs::file_size(part, ec) : 0;
        if (ec) have = 0;
        if (have > a.size) { fs::remove(part, ec); have = 0; }
        if (have < a.size) {
            const Server &sv = list[attempt % list.size()];
            bool partial = false;
            std::unique_ptr<FILE, int (*)(FILE *)> out(nullptr, fclose);
            uint64_t got = have;
            int st = http_get(sv, a.path, have, nullptr, [&](const char *d, DWORD n) {
                if (!out) {
                    // 206: append; 200: the whole file again
                    out.reset(_wfopen(part.c_str(), partial ? L"ab" : L"wb"));
                    if (!out) return false;
                    if (!partial) got = 0;
                }
                if (got + n > a.size || stopped()) return false;
                if (fwrite(d, 1, n, out.get()) != n) return false;
                got += n;
                if (progress) progress(got, a.size);
                return true;
            }, &partial);
            out.reset();
            if (st == 0 || (st != 200 && st != 206)) {
                fprintf(stderr, "[GRAPHICS] download of %s stopped at %llu of %llu bytes (status %d); retrying\n",
                        a.path.c_str(), (unsigned long long)got, (unsigned long long)a.size, st);
                Sleep(attempt < 2 ? 300 : 1500);
                continue;
            }
            have = fs::exists(part, ec) ? fs::file_size(part, ec) : 0;
            if (have < a.size) continue;
        }
        if (fs::file_size(part, ec) != a.size || !file_sha256(part, hex) || hex != a.sha256) {
            fs::remove(part, ec);
            say(error, "The download of " + a.path + " did not match its signed checksum; it was deleted.");
            return fs::path();
        }
        fs::rename(part, final_path, ec);
        if (ec) { say(error, "Cannot keep the download: " + ec.message()); return fs::path(); }
        return final_path;
    }
    say(error, "The download did not finish; press again to continue where it stopped.");
    return fs::path();
}
#else
bool fetch_manifest(Manifest &, std::string *error) { say(error, "Enhanced graphics is for Windows."); return false; }
fs::path ensure_archive(const Archive &, const fs::path &, const Progress &, std::string *error)
{
    say(error, "Enhanced graphics is for Windows.");
    return fs::path();
}
#endif

static const Archive &preset_archive(const Manifest &m, const std::string &preset)
{
    return preset == "quality" ? m.quality : m.performance;
}
static const std::vector<std::string> &preset_files(const Manifest &m, const std::string &preset)
{
    return preset == "quality" ? m.quality_files : m.performance_files;
}

bool install(const Manifest &m, const fs::path &bin, const fs::path &data, const std::string &preset,
             const Progress &progress, std::string *error)
{
    std::error_code ec;
    fs::path cache = data / "graphics" / "cache";
    Record old = read_record(data);
    if (old.present && !old.files.empty()) {
        // another version's bin, or a half-finished install: take ours out first
        if (!remove(data, error)) return false;
    }
    std::vector<std::string> all = m.base_files;
    for (auto &f : preset_files(m, preset)) if (std::find(all.begin(), all.end(), f) == all.end()) all.push_back(f);
    for (auto &f : all) {
        // ReShade and its add-ons write their own settings back when the program closes, so after Turn off these
        // came back and every Turn on was refused (2026-11-05). Settings are ours to replace; programs are not.
        if (settings_file(f)) continue;
        if (fs::exists(bin / from_utf8(f), ec)) {       // the proxy's own name counts: a dxgi.dll here is foreign
            say(error, "The game folder already has " + f + ", which this launcher did not install. Remove it "
                       "(another ReShade or mod?) and try again; nothing was changed.");
            return false;
        }
    }
    fs::path base = ensure_archive(m.base, cache, progress, error);
    if (base.empty()) return false;
    fs::path pre = ensure_archive(preset_archive(m, preset), cache, nullptr, error);
    if (pre.empty()) return false;
    // the record first, with every file and each folder that does not exist yet, so a stop half-way
    // can still be removed exactly
    Record r;
    r.present = true;
    r.version = m.version;
    r.preset = preset;
    r.bin = bin;
    r.base_sha = m.base.sha256;
    r.preset_sha = preset_archive(m, preset).sha256;
    r.files = all;
    for (auto &f : r.files) f = on_disk_name(f);
    std::set<std::string> dirs;
    for (auto &f : all)
        for (size_t s = f.find('/'); s != std::string::npos; s = f.find('/', s + 1))
            if (!fs::exists(bin / from_utf8(f.substr(0, s)), ec)) dirs.insert(f.substr(0, s));
    r.dirs.assign(dirs.begin(), dirs.end());
    write_record(data, r);
    if (!extract(base, bin, m.base_files, error) || !extract(pre, bin, preset_files(m, preset), error)) {
        std::string ignored;
        remove(data, &ignored);
        return false;
    }
    fprintf(stderr, "[GRAPHICS] installed %s (%s), %zu files in %s\n", m.version.c_str(), preset.c_str(), all.size(),
            utf8(bin).c_str());
    return true;
}

bool switch_preset(const Manifest &m, const fs::path &data, const std::string &preset, const Progress &progress,
                   std::string *error)
{
    Record r = read_record(data);
    if (!r.present || r.files.empty()) { say(error, "Enhanced graphics is not installed."); return false; }
    if (r.preset == preset) return true;
    fs::path pre = ensure_archive(preset_archive(m, preset), data / "graphics" / "cache", progress, error);
    if (pre.empty()) return false;
    for (auto &f : preset_files(m, preset))
        if (std::find(r.files.begin(), r.files.end(), on_disk_name(f)) == r.files.end()) {
            say(error, "The installed pack does not match this preset; turn it off and on again.");
            return false;
        }
    if (!extract(pre, r.bin, preset_files(m, preset), error)) return false;
    r.preset = preset;
    // from this manifest: when it is newer than the installed base the update brings that next
    r.preset_sha = preset_archive(m, preset).sha256;
    write_record(data, r);
    fprintf(stderr, "[GRAPHICS] preset now %s\n", preset.c_str());
    return true;
}

// ---- the update: changed archives only, unpacked beside bin, then switched over whole ----
namespace
{
const char *const kStage = "ffny-graphics-new", *const kBackup = "ffny-graphics-old";

fs::path journal_path(const fs::path &data) { return data / "graphics" / "update-journal.txt"; }

// What the switch does, written before it starts: 'a' a new file, 's' a file replaced, 'd' a file the new
// pack no longer has (both of these go to the backup first), 'm' a folder it made.
struct Journal { std::string version; fs::path bin; std::vector<std::pair<char, std::string>> items; };

bool write_journal(const fs::path &data, const Journal &j)
{
    std::error_code ec;
    fs::path tmp = journal_path(data);
    tmp += ".new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "version " << j.version << "\nbin " << utf8(j.bin) << "\n";
        for (auto &i : j.items) out << i.first << " " << i.second << "\n";
        if (!out) return false;
    }
    fs::rename(tmp, journal_path(data), ec);
    return !ec;
}

bool read_journal(const fs::path &data, Journal &j)
{
    std::ifstream in(journal_path(data), std::ios::binary);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.compare(0, 8, "version ")) j.version = line.substr(8);
        else if (!line.compare(0, 4, "bin ")) j.bin = from_utf8(line.substr(4));
        else if (line.size() > 2 && line[1] == ' ' && strchr("asdm", line[0]) && safe_rel(line.substr(2)))
            j.items.push_back({line[0], line.substr(2)});
    }
    return !j.bin.empty();
}

// Removes a folder of ours; a file the program holds is parked and deleted at a later start.
void clear_folder(const fs::path &data, const fs::path &dir)
{
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    fs::remove_all(dir, ec);
    if (!ec) return;
    std::vector<fs::path> left;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() != ".ffny-delete") left.push_back(it->path());
    for (auto &p : left) delete_or_park(data, p);
    fs::remove_all(dir, ec);
}

// Puts the old pack back: the new files out, the backup in. False leaves the journal for the next start.
bool roll_back(const fs::path &data, const Journal &j)
{
    std::error_code ec;
    fs::path backup = j.bin / kBackup;
    bool ok = true;
    for (auto i = j.items.rbegin(); i != j.items.rend(); ++i) {
        fs::path t = j.bin / from_utf8(i->second), b = backup / from_utf8(i->second);
        if (i->first == 'a') {
            if (!delete_or_park(data, t)) ok = false;
        } else if (i->first == 's' || i->first == 'd') {
            if (!fs::exists(b, ec)) continue;           // not moved yet: the old one is still in place
            fs::rename(b, t, ec);
            if (ec && delete_or_park(data, t)) fs::rename(b, t, ec);
            if (ec) ok = false;
        }
    }
    std::vector<std::string> dirs;
    for (auto &i : j.items) if (i.first == 'm') dirs.push_back(i.second);
    std::sort(dirs.begin(), dirs.end(), [](const std::string &a, const std::string &b) { return a.size() > b.size(); });
    for (auto &d : dirs) {
        fs::path p = j.bin / from_utf8(d);
        if (fs::is_directory(p, ec) && fs::is_empty(p, ec)) fs::remove(p, ec);
    }
    if (!ok) {
        fprintf(stderr, "[GRAPHICS] could not put the old pack fully back yet; trying again at the next start\n");
        fflush(stderr);
        return false;
    }
    clear_folder(data, j.bin / kStage);
    clear_folder(data, backup);
    fs::remove(journal_path(data), ec);
    fprintf(stderr, "[GRAPHICS] update undone: the pack is as it was\n");
    fflush(stderr);
    return true;
}
} // namespace

std::mutex &bin_lock()
{
    static std::mutex m;
    return m;
}

void recover_update(const fs::path &data)
{
    Journal j;
    if (!read_journal(data, j)) return;
    std::lock_guard<std::mutex> lock(bin_lock());
    Record r = read_record(data);
    if (r.present && r.version == j.version) {       // the record was written: the new pack is complete
        std::error_code ec;
        clear_folder(data, j.bin / kStage);
        clear_folder(data, j.bin / kBackup);
        fs::remove(journal_path(data), ec);
        return;
    }
    roll_back(data, j);
}

bool update(const Manifest &m, const fs::path &data, const Progress &progress, const std::function<bool()> &go_on,
            std::string *error, bool *updated)
{
    std::error_code ec;
    if (updated) *updated = false;
    recover_update(data);
    Record r = read_record(data);
    if (!r.present || r.files.empty() || r.version == m.version || r.bin.empty() || !fs::is_directory(r.bin, ec)) return true;
    std::string preset = r.preset == "quality" ? "quality" : "performance";
    const Archive &pa = preset_archive(m, preset);
    struct Part { const Archive *a; const std::vector<std::string> *files; };
    std::vector<Part> changed;
    if (r.base_sha != m.base.sha256) changed.push_back({&m.base, &m.base_files});
    if (r.preset_sha != pa.sha256) changed.push_back({&pa, &preset_files(m, preset)});

    std::vector<std::string> next, staged;          // on-disk names: the whole new pack, and what comes new
    for (auto *list : {&m.base_files, &preset_files(m, preset)})
        for (auto &f : *list)
            if (std::find(next.begin(), next.end(), on_disk_name(f)) == next.end()) next.push_back(on_disk_name(f));
    for (auto &p : changed)
        for (auto &f : *p.files) staged.push_back(on_disk_name(f));
    std::set<std::string> old(r.files.begin(), r.files.end()), next_set(next.begin(), next.end());
    for (auto &f : next)
        if (!old.count(f) && !settings_file(f) && (fs::exists(r.bin / from_utf8(f), ec) ||
                                                    (f == kParked && fs::exists(r.bin / kProxy, ec)))) {
            say(error, "The game folder already has " + f + ", which this launcher did not install; the installed "
                       "pack is unchanged.");
            return false;
        }

    // the downloads, checked against the signed manifest; the old pack is untouched meanwhile
    struct Scope { Scope(const std::function<bool()> *g) { t_go_on = g; } ~Scope() { t_go_on = nullptr; } } scope(&go_on);
    std::vector<fs::path> zips;
    for (auto &p : changed) {
        fs::path z = ensure_archive(*p.a, data / "graphics" / "cache", progress, error);
        if (z.empty()) {
            if (!stopped()) return false;
            say(error, kStopped);           // waits: the old pack is in place, the .part is resumed next time
            return true;
        }
        zips.push_back(z);
    }
    if (stopped()) { say(error, kStopped); return true; }
    fs::path stage = r.bin / kStage, backup = r.bin / kBackup;
    clear_folder(data, stage);
    clear_folder(data, backup);
    for (size_t i = 0; i < zips.size(); i++)
        if (!extract(zips[i], stage, *changed[i].files, error)) {
            clear_folder(data, stage);
            return false;
        }

    // the switch: nothing else changes bin meanwhile, and Play waits for it (a few renames)
    std::lock_guard<std::mutex> lock(bin_lock());
    Record now = read_record(data);
    if (!now.present || now.files.empty()) { clear_folder(data, stage); return true; }      // turned off meanwhile
    if (stopped()) { clear_folder(data, stage); say(error, kStopped); return true; }
    if (now.version != r.version || now.preset != r.preset || !fs::equivalent(now.bin, r.bin, ec)) {
        clear_folder(data, stage);
        say(error, "The graphics pack changed meanwhile; the update runs again at the next start.");
        return false;
    }
    Journal j;
    j.version = m.version;
    j.bin = r.bin;
    std::set<std::string> made;
    for (auto &f : staged) {
        j.items.push_back({fs::exists(r.bin / from_utf8(f), ec) ? 's' : 'a', f});
        for (size_t s = f.find('/'); s != std::string::npos; s = f.find('/', s + 1))
            if (!fs::exists(r.bin / from_utf8(f.substr(0, s)), ec)) made.insert(f.substr(0, s));
    }
    for (auto &f : r.files)
        if (!next_set.count(f)) j.items.push_back({'d', f});
    for (auto &d : made) j.items.push_back({'m', d});
    if (!write_journal(data, j)) {
        clear_folder(data, stage);
        say(error, "Cannot write in the launcher's data folder; the installed pack is unchanged.");
        return false;
    }
    std::string failed;
    for (auto &i : j.items) {
        if (i.first == 'm') continue;
        fs::path t = r.bin / from_utf8(i.second), b = backup / from_utf8(i.second);
        if ((i.first == 's' || i.first == 'd') && fs::exists(t, ec)) {
            fs::create_directories(b.parent_path(), ec);
            fs::rename(t, b, ec);
            if (ec) { failed = i.second; break; }
        }
        if (i.first == 's' || i.first == 'a') {
            fs::create_directories(t.parent_path(), ec);
            fs::rename(stage / from_utf8(i.second), t, ec);
            if (ec) { failed = i.second; break; }
        }
    }
    Record nr = r;
    if (failed.empty()) {
        nr.version = m.version;
        nr.files = next;
        nr.base_sha = m.base.sha256;
        nr.preset_sha = pa.sha256;
        std::set<std::string> dirs(r.dirs.begin(), r.dirs.end());
        dirs.insert(made.begin(), made.end());
        nr.dirs.assign(dirs.begin(), dirs.end());
        write_record(data, nr);                     // the commit
        if (read_record(data).version != m.version) failed = "installed.txt";
    }
    if (!failed.empty()) {
        roll_back(data, j);
        say(error, "Cannot replace " + failed + " (in use?); the installed pack is unchanged.");
        return false;
    }
    fs::remove(journal_path(data), ec);
    clear_folder(data, stage);
    clear_folder(data, backup);
    fprintf(stderr, "[GRAPHICS] updated %s -> %s (%s): %zu archive(s) downloaded, %zu files replaced or added\n",
            r.version.c_str(), m.version.c_str(), preset.c_str(), changed.size(), staged.size());
    fflush(stderr);
    if (updated) *updated = true;
    return true;
}

bool remove(const fs::path &data, std::string *error)
{
    std::error_code ec;
    tidy_proxy(data);           // a proxy left under its live name is taken out as well
    Record r = read_record(data);
    if (!r.present) return true;
    bool ok = true;
    size_t gone = 0;
    if (!r.bin.empty() && fs::is_directory(r.bin, ec)) {
        for (auto &f : r.files) {
            fs::path p = r.bin / from_utf8(f);
            fs::path part = p;
            part += ".ffny-part";
            fs::remove(part, ec);
            if (delete_or_park(data, p)) gone++;
            else ok = false;
        }
        std::vector<std::string> dirs = r.dirs;
        std::sort(dirs.begin(), dirs.end(), [](const std::string &a, const std::string &b) { return a.size() > b.size(); });
        for (auto &d : dirs) {
            fs::path p = r.bin / from_utf8(d);
            if (fs::is_directory(p, ec) && fs::is_empty(p, ec)) fs::remove(p, ec);
        }
    }
    if (!ok) { say(error, "Some files could not be removed; close the game and press Turn off again."); return false; }
    fs::remove(record_path(data), ec);
    fprintf(stderr, "[GRAPHICS] removed: %zu files taken out of %s\n", gone, utf8(r.bin).c_str());
    return true;
}

void finish_pending(const fs::path &data)
{
    std::error_code ec;
    std::ifstream in(pending_path(data), std::ios::binary);
    if (!in) return;
    std::vector<std::string> left;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 13 || line.compare(line.size() - 12, 12, ".ffny-delete")) continue;   // only our parked files
        fs::path p = from_utf8(line);
        if (fs::exists(p, ec) && !fs::remove(p, ec)) left.push_back(line);
    }
    in.close();
    if (left.empty()) fs::remove(pending_path(data), ec);
    else {
        std::ofstream out(pending_path(data), std::ios::binary | std::ios::trunc);
        for (auto &l : left) out << l << "\n";
    }
}
} // namespace gfx
