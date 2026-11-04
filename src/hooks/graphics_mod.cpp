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
        fs::path target = bin / from_utf8(e.name), part = target;
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
        else if (k == "dir" && safe_rel(v)) r.dirs.push_back(v);
        else if (k == "file" && safe_rel(v)) r.files.push_back(v);
    }
    return r;
}

#if defined(_WIN32)
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
                if (got + n > a.size) return false;
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
        if (fs::exists(bin / from_utf8(f), ec)) {
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
    r.files = all;
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
        if (std::find(r.files.begin(), r.files.end(), f) == r.files.end()) {
            say(error, "The installed pack does not match this preset; turn it off and on again.");
            return false;
        }
    if (!extract(pre, r.bin, preset_files(m, preset), error)) return false;
    r.preset = preset;
    write_record(data, r);
    fprintf(stderr, "[GRAPHICS] preset now %s\n", preset.c_str());
    return true;
}

bool remove(const fs::path &data, std::string *error)
{
    std::error_code ec;
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
