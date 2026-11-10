// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// FFNY Studio through the launcher: the signed manifest, the download, the install with its current pointer, the
// start with the handover arguments and the background work behind the home screen's Studio card. See
// studio_app.h; the card itself is pc_launcher_studio.inc.
#define _CRT_SECURE_NO_WARNINGS
#include "studio_app.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "pc_ui_actions.h"
extern "C" {
#include "account.h"
#include "pc_settings.h"
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

namespace fs = std::filesystem;

namespace studio
{
namespace
{
constexpr size_t kManifestMax = 64 * 1024;
constexpr uint64_t kZipMax = 1ull << 30;
const char *const kPrefix = "FFNYStudio/";

void log_line(const std::string &text)
{
    fprintf(stderr, "[STUDIO] %s\n", text.c_str());
    fflush(stderr);
}

bool fail(std::string *error, const std::string &text)
{
    if (error) *error = text;
    log_line(text);
    return false;
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

fs::path env_path(const char *name)
{
#if defined(_WIN32)
    std::wstring key(name, name + strlen(name));
    const wchar_t *v = _wgetenv(key.c_str());
    return v && *v ? fs::path(v) : fs::path();
#else
    const char *v = getenv(name);
    return v && *v ? fs::path(v) : fs::path();
#endif
}

// ---- the manifest: a flat JSON object (nested values are skipped) ----
struct Manifest
{
    int build = 0;
    std::string version, notes, setup, url, sha256;
    uint64_t size = 0;
};

struct Reader
{
    const char *p, *end;
    void ws() { while (p < end && isspace((unsigned char)*p)) p++; }
    bool str(std::string &out)
    {
        if (p >= end || *p != '"') return false;
        for (p++; p < end && *p != '"'; p++) {
            if (*p != '\\') { out += *p; continue; }
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
        }
        if (p >= end) return false;
        p++;
        return true;
    }
    // Any value; strings and numbers are given back as text (numbers marked by `number`).
    bool value(std::string &out, bool &number, int depth = 0)
    {
        ws();
        if (p >= end || depth > 16) return false;
        number = false;
        if (*p == '"') return str(out);
        if (*p == '{' || *p == '[') {
            char close = *p == '{' ? '}' : ']';
            p++;
            ws();
            if (p < end && *p == close) { p++; return true; }
            for (;;) {
                std::string skip;
                bool n;
                if (close == '}') {
                    ws();
                    if (!str(skip)) return false;
                    ws();
                    if (p >= end || *p++ != ':') return false;
                }
                if (!value(skip, n, depth + 1)) return false;
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == close) { p++; return true; }
                return false;
            }
        }
        const char *start = p;
        while (p < end && (isalnum((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.')) p++;
        if (p == start) return false;
        out.assign(start, p);
        number = isdigit((unsigned char)out[0]) || out[0] == '-';
        return true;
    }
};

bool parse_object(const std::string &text, std::map<std::string, std::pair<std::string, bool>> &fields)
{
    Reader r{text.data(), text.data() + text.size()};
    r.ws();
    if (r.p >= r.end || *r.p++ != '{') return false;
    r.ws();
    if (r.p < r.end && *r.p == '}') return true;
    for (;;) {
        std::string key, v;
        bool number;
        r.ws();
        if (!r.str(key)) return false;
        r.ws();
        if (r.p >= r.end || *r.p++ != ':') return false;
        if (!r.value(v, number)) return false;
        fields[key] = {v, number};
        r.ws();
        if (r.p < r.end && *r.p == ',') { r.p++; continue; }
        if (r.p < r.end && *r.p == '}') return true;
        return false;
    }
}

bool safe_version(const std::string &v)
{
    if (v.empty() || v.size() > 48 || !isalnum((unsigned char)v[0])) return false;
    for (char c : v)
        if (!isalnum((unsigned char)c) && !strchr(".-+_", c)) return false;
    return true;
}

bool safe_file_name(const std::string &n)
{
    if (n.empty() || n.size() > 120 || n[0] == '.') return false;
    for (char c : n)
        if (!isalnum((unsigned char)c) && !strchr(".-+_", c)) return false;
    return n.find("..") == std::string::npos;
}

bool hex64(std::string &s)
{
    if (s.size() != 64) return false;
    for (auto &c : s) {
        if (!isxdigit((unsigned char)c)) return false;
        c = (char)tolower((unsigned char)c);
    }
    return true;
}

bool read_manifest(const std::string &text, Manifest &m, std::string *error)
{
    std::map<std::string, std::pair<std::string, bool>> f;
    if (!parse_object(text, f)) return fail(error, "Studio's update information cannot be read.");
    auto num = [&](const char *k, double &out) {
        auto it = f.find(k);
        if (it == f.end() || !it->second.second) return false;
        char *e = nullptr;
        out = strtod(it->second.first.c_str(), &e);
        return e && !*e;
    };
    auto str = [&](const char *k) {
        auto it = f.find(k);
        return it == f.end() || it->second.second ? std::string() : it->second.first;
    };
    double build = 0, size = 0;
    m = Manifest();
    m.version = str("version");
    m.notes = str("notes");
    m.setup = str("setup");
    m.url = str("url");
    m.sha256 = str("sha256");
    if (!num("build", build) || build < 1 || build > 1e9 || !num("size", size) || size < 1 || size > (double)kZipMax ||
        !safe_version(m.version) || !safe_file_name(m.setup) || !hex64(m.sha256))
        return fail(error, "Studio's update information is incomplete.");
    m.build = (int)build;
    m.size = (uint64_t)size;
    if (m.notes.size() > 300) m.notes.resize(300);
    return true;
}

#if defined(_WIN32)
// ---- HTTP ----
// account: the manifest and the zip come through the account service with the player's session
// (/api/studio/latest, /api/studio/download/<setup>); host, port and prefix are then unused.
struct Server { std::wstring host; INTERNET_PORT port = 0; bool https = false; std::wstring prefix; bool account = false; };

std::wstring wide(const std::string &s) { return std::wstring(s.begin(), s.end()); }

bool local_host(const std::string &host) { return host == "127.0.0.1" || host == "localhost" || host == "::1"; }

std::string test_server()
{
    const char *s = getenv("RECOMP_STUDIO_SERVER");
    return s && *s ? s : "";
}

// FFNY Studio is in a closed test: the launcher asks the account service, which hands the manifest and the zip
// only to accounts with access. The public update folder is read only behind a test switch: RECOMP_STUDIO_SERVER
// (a local test server) or RECOMP_STUDIO_PUBLIC=1 (the update servers, should a manifest be published there).
bool public_path()
{
    if (!test_server().empty()) return true;
    const char *p = getenv("RECOMP_STUDIO_PUBLIC");
    return p && !strcmp(p, "1");
}

std::vector<Server> servers()
{
    std::vector<Server> out;
    std::string t = test_server();
    if (!t.empty()) {
        size_t colon = t.rfind(':');
        if (colon != std::string::npos) {
            out.push_back({wide(t.substr(0, colon)), (INTERNET_PORT)atoi(t.c_str() + colon + 1), false, L"/studio/"});
            return out;
        }
    }
    for (int i = 0; updater_host(i); i++) out.push_back({updater_host(i), (INTERNET_PORT)updater_port(), false, L"/studio/"});
    out.push_back({L"play.defjam.online", INTERNET_DEFAULT_HTTPS_PORT, true, L"/updates/studio/"});
    return out;
}

const char *const kClosed = "FFNY Studio is in a closed test; this account has no access.";

// Access for the signed-in account (GET /api/studio/access).
enum Access { ACCESS_UNKNOWN, ACCESS_ALLOWED, ACCESS_DENIED, ACCESS_SIGNED_OUT, ACCESS_NO_ANSWER };

bool signed_in()
{
    AccountState st = account_state();
    return st == ACCOUNT_IN || st == ACCOUNT_CHECKING;
}

Access ask_access()
{
    if (!signed_in()) return ACCESS_SIGNED_OUT;
    std::vector<char> buf(4096);
    int st = account_call("GET", "/api/studio/access", nullptr, buf.data(), (int)buf.size(), 0, nullptr);
    if (st == -1 || st == 401) return ACCESS_SIGNED_OUT;
    std::map<std::string, std::pair<std::string, bool>> f;
    if (st != 200 || !parse_object(buf.data(), f)) {
        log_line("access: no answer from the account service (status " + std::to_string(st) + ")");
        return ACCESS_NO_ANSWER;
    }
    bool allowed = f["allowed"].first == "true";
    log_line(std::string("access: ") + (allowed ? "allowed" : "not allowed") + " (" + f["reason"].first + ")");
    return allowed ? ACCESS_ALLOWED : ACCESS_DENIED;
}

int sink_call(void *ctx, const char *data, unsigned n)
{
    return (*(const std::function<bool(const char *, DWORD)> *)ctx)(data, (DWORD)n) ? 1 : 0;
}

// GET sv.host + path from byte `from`; the sink gets the body. Returns the HTTP status (0: no answer).
int http_get(const Server &sv, const std::wstring &path, uint64_t from, const std::function<bool(const char *, DWORD)> &sink,
             bool *partial)
{
    HINTERNET session = nullptr, connect = nullptr, request = nullptr;
    DWORD status = 0, size = sizeof(status);
    *partial = false;
    session = WinHttpOpen(L"DefJamRecomp-Studio/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
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
              WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size,
                                  nullptr);
    if (ok && (status == 200 || status == 206)) {
        *partial = status == 206;
        std::vector<char> buf(256 * 1024);
        for (;;) {
            DWORD avail = 0, got = 0;
            if (!WinHttpQueryDataAvailable(request, &avail)) { status = 0; break; }
            if (!avail) break;
            if (avail > buf.size()) avail = (DWORD)buf.size();
            if (!WinHttpReadData(request, buf.data(), avail, &got)) { status = 0; break; }
            if (!got) break;
            if (!sink(buf.data(), got)) { status = 0; break; }
        }
    } else if (!ok) {
        status = 0;
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return (int)status;
}

bool get_small(const Server &sv, const std::wstring &path, std::string &out, size_t max)
{
    out.clear();
    bool partial;
    return http_get(sv, path, 0, [&](const char *d, DWORD n) {
        if (out.size() + n > max) return false;
        out.append(d, n);
        return true;
    }, &partial) == 200;
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

// The signed manifest and the server that gave it: the account service (the closed test), or the public
// update folder behind a test switch. Either way it is believed only with the update key's signature.
bool fetch_manifest(Manifest &m, Server &used, std::string *error)
{
    std::string text, sig;
    bool got = false, by_account = !public_path();
    if (by_account) {
        std::vector<char> buf(8 * kManifestMax);
        int st = account_call("GET", "/api/studio/latest", nullptr, buf.data(), (int)buf.size(), 0, nullptr);
        if (st == 403) return fail(error, kClosed);
        if (st == -1 || st == 401) return fail(error, "Sign in to FFNY Online to get FFNY Studio.");
        if (st == 404) return fail(error, "FFNY Studio is not published yet.");
        std::map<std::string, std::pair<std::string, bool>> f;
        if (st != 200 || !parse_object(buf.data(), f) || f["manifest"].first.empty() || f["manifest"].first.size() > kManifestMax)
            return fail(error, "Could not reach the account service for FFNY Studio.");
        text = f["manifest"].first;
        sig = f["signature"].first;
        used = Server();
        used.account = true;
        got = true;
    }
    for (auto &sv : by_account ? std::vector<Server>() : servers()) {
        if (!get_small(sv, sv.prefix + L"studio-latest.json", text, kManifestMax)) continue;
        if (!get_small(sv, sv.prefix + L"studio-latest.json.sig", sig, 1024)) {
            log_line("studio-latest.json without its .sig on one server; trying the next");
            continue;
        }
        used = sv;
        got = true;
        break;
    }
    if (!got) return fail(error, "Could not reach the update server for FFNY Studio.");
    while (!sig.empty() && isspace((unsigned char)sig.back())) sig.pop_back();
    const char *key = nullptr;
    std::string test_key;
    if (const char *k = getenv("RECOMP_STUDIO_TEST_KEY")) {
        std::string t = test_server();
        bool local = by_account ? account_test_server_local() != 0 : !t.empty() && local_host(t.substr(0, t.rfind(':')));
        if (!local) {
            log_line("a test key is only taken with a Studio server or an account service on this PC; ignored");
        } else {
            test_key = k;
            key = test_key.c_str();
        }
    }
    if (!update_signature_ok(text.data(), (unsigned long)text.size(), sig.c_str(), key))
        return fail(error, "Studio's update information is not signed by the project; nothing was downloaded.");
    if (!read_manifest(text, m, error)) return false;
    log_line("published: " + m.version + " (build " + std::to_string(m.build) + "), " + m.setup + ", " +
             std::to_string(m.size) + " bytes");
    return true;
}

// Where the zip is: beside the manifest; an absolute url only when it is on the manifest's host.
std::vector<std::pair<Server, std::wstring>> zip_sources(const Manifest &m, const Server &used)
{
    std::vector<std::pair<Server, std::wstring>> out;
    if (used.account) {         // only through the account service; the manifest's url is not used
        out.push_back({used, L"/api/studio/download/" + wide(m.setup)});
        return out;
    }
    std::string u = m.url;
    bool https = u.compare(0, 8, "https://") == 0, http = u.compare(0, 7, "http://") == 0;
    if (https || http) {
        std::string rest = u.substr(https ? 8 : 7);
        size_t slash = rest.find('/');
        std::string hostport = rest.substr(0, slash), path = slash == std::string::npos ? "/" : rest.substr(slash);
        size_t colon = hostport.rfind(':');
        std::string host = colon == std::string::npos ? hostport : hostport.substr(0, colon);
        INTERNET_PORT port = colon == std::string::npos ? (https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT)
                                                        : (INTERNET_PORT)atoi(hostport.c_str() + colon + 1);
        bool path_ok = path.find("..") == std::string::npos && path.find_first_of("?#\\ ") == std::string::npos;
        if (_wcsicmp(wide(host).c_str(), used.host.c_str()) == 0 && path_ok)
            out.push_back({Server{used.host, port, https, L""}, wide(path)});
        else
            log_line("the manifest's url is not on the manifest's host; using the file beside the manifest");
    }
    out.push_back({used, used.prefix + wide(m.setup)});
    return out;
}

// The zip in `cache`, resumed and retried; checked against the manifest's size and SHA-256.
bool download(const Manifest &m, const Server &used, const fs::path &cache, fs::path &zip,
              const std::function<void(uint64_t, uint64_t)> &progress, std::string *error)
{
    std::error_code ec;
    fs::create_directories(cache, ec);
    zip = cache / fs::u8path(m.setup);
    fs::path part = zip;
    part += ".part";
    std::string hex;
    if (fs::exists(zip, ec) && fs::file_size(zip, ec) == m.size && file_sha256(zip, hex) && hex == m.sha256) return true;
    fs::remove(zip, ec);
    auto sources = zip_sources(m, used);
    for (int attempt = 0; attempt < 6; attempt++) {
        uint64_t have = fs::exists(part, ec) ? fs::file_size(part, ec) : 0;
        if (ec) have = 0;
        if (have > m.size) { fs::remove(part, ec); have = 0; }
        if (have < m.size) {
            auto &src = sources[attempt % sources.size()];
            bool partial = false;
            std::unique_ptr<FILE, int (*)(FILE *)> out(nullptr, fclose);
            uint64_t got = have;
            const std::function<bool(const char *, DWORD)> sink = [&](const char *d, DWORD n) {
                if (!out) {
                    out.reset(_wfopen(part.c_str(), partial ? L"ab" : L"wb"));     // 206: append; 200: all again
                    if (!out) return false;
                    if (!partial) got = 0;
                }
                if (got + n > m.size) return false;
                if (fwrite(d, 1, n, out.get()) != n) return false;
                got += n;
                if (progress) progress(got, m.size);
                return true;
            };
            int st;
            if (src.first.account) {
                int part206 = 0;
                std::string path(src.second.begin(), src.second.end());
                // account_download sets part206 before the first piece arrives; the sink reads `partial`
                const std::function<bool(const char *, DWORD)> first = [&](const char *d, DWORD n) {
                    partial = part206 != 0;
                    return sink(d, n);
                };
                st = account_download(path.c_str(), have, sink_call, (void *)&first, &part206);
            } else {
                st = http_get(src.first, src.second, have, sink, &partial);
            }
            out.reset();
            if (src.first.account && (st == 403 || st == 401 || st == -1))
                return fail(error, st == 403 ? kClosed : "Sign in to FFNY Online to get FFNY Studio.");
            if (st != 200 && st != 206) {
                log_line("download stopped at " + std::to_string(got) + " of " + std::to_string(m.size) + " bytes (status " +
                         std::to_string(st) + "); retrying");
                Sleep(attempt < 2 ? 300 : 1500);
                continue;
            }
            have = fs::exists(part, ec) ? fs::file_size(part, ec) : 0;
            if (have < m.size) continue;
        }
        if (fs::file_size(part, ec) != m.size || !file_sha256(part, hex) || hex != m.sha256) {
            fs::remove(part, ec);
            return fail(error, "The Studio download did not match its signed checksum; it was deleted.");
        }
        fs::rename(part, zip, ec);
        if (ec) return fail(error, "Cannot keep the Studio download: " + ec.message());
        return true;
    }
    return fail(error, "The Studio download did not finish; press again to continue where it stopped.");
}
#endif

// ---- the install ----
struct Current { std::string folder, version; int build = 0; };

fs::path root_path()
{
    fs::path r = env_path("RECOMP_STUDIO_DIR");
    if (!r.empty()) return r;
    fs::path data = fs::u8path(pcui::folder_path(pcui::Folder::Data));
    std::error_code ec;
    fs::path abs = fs::absolute(data, ec);
    if (!ec) data = abs;
    fs::path parent = data.lexically_normal().parent_path();
    return (parent.empty() ? data : parent) / "studio";
}

Current read_current(const fs::path &root)
{
    Current c;
    std::ifstream in(root / "current.txt", std::ios::binary);
    std::string line;
    while (in && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string k = line.substr(0, sp), v = line.substr(sp + 1);
        if (k == "folder" && safe_version(v)) c.folder = v;
        else if (k == "version" && safe_version(v)) c.version = v;
        else if (k == "build") c.build = atoi(v.c_str());
    }
    std::error_code ec;
    if (c.folder.empty() || !fs::is_regular_file(root / "versions" / c.folder / "FFNYStudio" / "FFNYStudio.exe", ec))
        return Current();
    return c;
}

std::string read_previous(const fs::path &root)
{
    std::ifstream in(root / "previous.txt", std::ios::binary);
    std::string v;
    std::getline(in, v);
    while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
    return safe_version(v) ? v : "";
}

bool write_small(const fs::path &file, const std::string &text)
{
    std::error_code ec;
    fs::path tmp = file;
    tmp += ".new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        out.flush();
        if (!out) return false;
    }
#if defined(_WIN32)
    return MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    fs::rename(tmp, file, ec);
    return !ec;
#endif
}

// Unpacks into versions\<folder>.part, moves it to versions\<folder>, then switches current.txt to it. The version
// in use until then becomes the previous one; any older version (and a stopped install) is deleted.
bool install(const Manifest &m, const fs::path &zip, const fs::path &root, std::string *error)
{
    std::error_code ec;
    Current cur = read_current(root);
    std::string folder = m.version;
    if (folder == cur.folder || folder == read_previous(root)) folder = m.version + "-b" + std::to_string(m.build);
    if (folder == cur.folder) return fail(error, "This Studio build is already installed.");
    fs::path versions = root / "versions", target = versions / fs::u8path(folder), part = versions / fs::u8path(folder + ".part");
    fs::create_directories(versions, ec);
    fs::remove_all(part, ec);
    fs::create_directories(part, ec);
    if (!studio_zip::extract(zip.wstring(), part.wstring(), kPrefix, error)) {
        fs::remove_all(part, ec);
        return false;
    }
    if (!fs::is_regular_file(part / "FFNYStudio" / "FFNYStudio.exe", ec)) {
        fs::remove_all(part, ec);
        return fail(error, "The Studio download has no FFNYStudio.exe.");
    }
    fs::remove_all(target, ec);
    fs::rename(part, target, ec);
    if (ec) {
        fs::remove_all(part, ec);
        return fail(error, "Cannot put Studio in place (is an old copy still open?).");
    }
    std::string text = "# FFNY Online launcher: the FFNY Studio in use (versions\\<folder>)\nfolder " + folder + "\nversion " +
                       m.version + "\nbuild " + std::to_string(m.build) + "\n";
    if (!write_small(root / "current.txt", text)) return fail(error, "Cannot record the new Studio version.");
    if (!cur.folder.empty()) write_small(root / "previous.txt", cur.folder + "\n");
    log_line("installed " + m.version + " (build " + std::to_string(m.build) + ") in " + utf8(target) +
             (cur.folder.empty() ? "" : "; previous " + cur.folder + " kept"));
    // Tidy: keep the current and the previous version; a copy still running stays until the next time.
    std::string keep_prev = cur.folder;
    for (auto &d : fs::directory_iterator(versions, ec)) {
        std::string name = utf8(d.path().filename());
        if (name == folder || name == keep_prev) continue;
        fs::remove_all(d.path(), ec);
        log_line(std::string(ec ? "could not remove " : "removed ") + "old version " + name);
    }
    fs::remove(zip, ec);
    return true;
}

// The folder holding setup's receipt, for --install-dir; empty in a development checkout.
fs::path install_dir()
{
    std::error_code ec;
    auto receipt = [&](const fs::path &d) {
        return !d.empty() && (fs::is_regular_file(d / "installed.ini", ec) || fs::is_regular_file(d / "installed.json", ec));
    };
    fs::path named = env_path("DEFJAM_INSTALL_DIR");
    if (receipt(named)) return fs::absolute(named, ec).lexically_normal();
    fs::path cwd = fs::current_path(ec);
    fs::path up = (cwd / ".." / ".." / "..").lexically_normal();
    if (!ec && cwd.parent_path().parent_path().filename() == "versions" && receipt(up)) return up;
    return fs::path();
}

fs::path account_file(const fs::path &data)
{
    const char *settings = pc_settings_path();
    if (settings && *settings) {
        fs::path s = fs::u8path(settings);
        if (s.has_parent_path()) return s.parent_path() / "account.dat";
    }
    return data / "account.dat";
}

#if defined(_WIN32)
// One argument for CreateProcessW: quoted, with trailing backslashes doubled (MSVC's argv rules).
std::wstring quote(const std::wstring &arg)
{
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { slashes++; continue; }
        if (c == L'"') { out.append(slashes * 2 + 1, L'\\'); out += c; slashes = 0; continue; }
        out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}
#endif

// ---- the launcher's view ----
std::mutex g_mutex;
View g_view;
bool g_have_manifest;
Manifest g_manifest;
#if defined(_WIN32)
Server g_server;
#endif
std::atomic<bool> g_checking{false};
std::chrono::steady_clock::time_point g_last_check;
bool g_started;
#if defined(_WIN32)
Access g_access = ACCESS_UNKNOWN;       // the account's access (the closed test); unused on the public path
bool g_was_signed_in;                   // launcher_starts watches the sign-in (the launcher's thread only)
bool g_want_check;
#endif

void refresh_locked()
{
#if defined(_WIN32)
    g_view.supported = true;
#endif
    Current c = read_current(root_path());
    g_view.installed = !c.folder.empty();
    g_view.installed_version = c.version;
    g_view.installed_build = c.build;
    g_view.available = g_have_manifest;
    if (g_have_manifest) {
        g_view.version = g_manifest.version;
        g_view.notes = g_manifest.notes;
        g_view.build = g_manifest.build;
        g_view.size = g_manifest.size;
    }
    g_view.update = g_view.installed && g_have_manifest && g_manifest.build > c.build;
#if defined(_WIN32)
    // the closed test: the card is for accounts with access. An installed Studio stays reachable while the
    // account service does not answer (Studio checks the access itself, and works offline for seven days).
    g_view.allowed = public_path() || g_access == ACCESS_ALLOWED || (g_access == ACCESS_NO_ANSWER && g_view.installed);
#endif
}

void set_progress(const std::string &text, double fraction)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_view.progress = text;
    g_view.fraction = fraction;
}

void finish(const std::string &error, const std::string &message)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_view.busy = false;
    g_view.progress.clear();
    g_view.fraction = -1;
    g_view.error = error;
    g_view.message = message;
    refresh_locked();
}

bool start_work()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_view.busy) return false;
    g_view.busy = true;
    g_view.error.clear();
    g_view.message.clear();
    return true;
}

#if defined(_WIN32)
bool fetch(std::string *error)
{
    Manifest m;
    Server used;
    Access access = ACCESS_ALLOWED;
    bool ok = false;
    if (!public_path()) access = ask_access();
    if (access == ACCESS_ALLOWED) ok = fetch_manifest(m, used, error);
    else if (access == ACCESS_DENIED) fail(error, kClosed);
    else if (access == ACCESS_SIGNED_OUT) fail(error, "Sign in to FFNY Online to get FFNY Studio.");
    else fail(error, "Could not reach the account service for FFNY Studio.");
    std::lock_guard<std::mutex> lock(g_mutex);
    g_view.checked = true;
    g_last_check = std::chrono::steady_clock::now();
    g_access = access;
    if (ok) {
        g_manifest = m;
        g_server = used;
        g_have_manifest = true;
    } else if (access == ACCESS_DENIED || access == ACCESS_SIGNED_OUT) {
        g_have_manifest = false;        // nothing is offered to an account without access
    }
    refresh_locked();
    return ok;
}

void run_check()
{
    std::string error;
    bool ok = fetch(&error);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // quiet: a failed background check only says so when nothing is installed (the card offers Retry)
        if (!ok && !g_view.busy && !g_view.installed) g_view.error = error;
        else if (ok && !g_view.busy && !g_view.installed) g_view.error.clear();
    }
    g_checking = false;
}

void run_install()
{
    std::string error;
    set_progress("Asking the update server for FFNY Studio...", -1);
    if (!fetch(&error)) { finish(error, ""); return; }
    Manifest m;
    Server sv;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        m = g_manifest;
        sv = g_server;
    }
    Current cur = read_current(root_path());
    if (!cur.folder.empty() && m.build <= cur.build) { finish("", "FFNY Studio is up to date."); return; }
    fs::path zip;
    bool updating = !cur.folder.empty();
    const char *what = updating ? "Updating FFNY Studio" : "Downloading FFNY Studio";
    auto progress = [what](uint64_t got, uint64_t total) {
        char t[160];
        snprintf(t, sizeof(t), "%s: %.1f of %.1f MB", what, got / 1048576.0, total / 1048576.0);
        set_progress(t, total ? (double)got / (double)total : -1);
    };
    set_progress(std::string(what) + "...", 0);
    if (!download(m, sv, root_path() / "cache", zip, progress, &error)) { finish(error, ""); return; }
    set_progress("Installing FFNY Studio...", -1);
    if (!install(m, zip, root_path(), &error)) { finish(error, ""); return; }
    finish("", (updating ? "Studio updated to " : "Studio installed: ") + m.version + ".");
}
#endif
} // namespace

std::string root_folder() { return utf8(root_path()); }

View view()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_view.busy) refresh_locked();
    return g_view;
}

void launcher_starts()
{
#if defined(_WIN32)
    bool now_in = signed_in();
    if (!g_started) {
        g_started = true;
        g_was_signed_in = now_in;
        g_want_check = true;
        log_line("root " + root_folder());
        log_line(public_path() ? "source: the public update folder (test switch)" : "source: the account service (closed test)");
    } else if (now_in != g_was_signed_in && !public_path()) {
        // signed in or out since the last frame: the access is another account's (or nobody's)
        g_was_signed_in = now_in;
        g_want_check = true;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_access = now_in ? ACCESS_UNKNOWN : ACCESS_SIGNED_OUT;
        g_have_manifest = false;
        if (!g_view.busy) g_view.error.clear();
    }
    if (now_in && !g_want_check && !public_path()) {
        // a tester who just got the role sees the card without restarting the launcher: ask again every ten
        // minutes while the answer is no (or none came)
        std::lock_guard<std::mutex> lock(g_mutex);
        if ((g_access == ACCESS_DENIED || g_access == ACCESS_NO_ANSWER) && !g_view.busy &&
            std::chrono::steady_clock::now() - g_last_check >= std::chrono::minutes(10))
            g_want_check = true;
    }
    if (g_want_check && !g_checking.exchange(true)) {
        g_want_check = false;
        std::thread(run_check).detach();
    }
#endif
}

void check()
{
#if defined(_WIN32)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_view.busy || (g_view.checked && std::chrono::steady_clock::now() - g_last_check < std::chrono::minutes(2))) return;
    }
    if (g_checking.exchange(true)) return;
    std::thread(run_check).detach();
#endif
}

bool launch(std::string *error)
{
#if defined(_WIN32)
    fs::path root = root_path();
    Current c = read_current(root);
    if (c.folder.empty()) return fail(error, "FFNY Studio is not installed.");
    fs::path exe = root / "versions" / fs::u8path(c.folder) / "FFNYStudio" / "FFNYStudio.exe";
    std::error_code ec;
    fs::path data = fs::absolute(fs::u8path(pcui::folder_path(pcui::Folder::Data)), ec).lexically_normal();
    fs::path account = fs::absolute(account_file(data), ec).lexically_normal();
    fs::path install = install_dir();
    auto bare = [](fs::path p) {        // no trailing separator
        std::wstring w = p.native();
        while (w.size() > 3 && (w.back() == L'\\' || w.back() == L'/')) w.pop_back();
        return w;
    };
    std::wstring cmd = quote(exe.native());
    if (!install.empty()) cmd += L" --install-dir " + quote(bare(install));
    cmd += L" --data-root " + quote(bare(data)) + L" --account-file " + quote(account.native());
    std::wstring dir = exe.parent_path().native();
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(0);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
                        nullptr, dir.c_str(), &si, &pi))
        return fail(error, "FFNY Studio could not be started (error " + std::to_string(GetLastError()) + ").");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    log_line("started " + c.version + " (pid " + std::to_string(pi.dwProcessId) + "): " + utf8(fs::path(cmd)));
    return true;
#else
    return fail(error, "FFNY Studio is for Windows.");
#endif
}

void press()
{
#if defined(_WIN32)
    View v = view();
    if (v.busy) return;
    if (!v.allowed) {
        log_line("button: not available (no FFNY Studio access for this account)");
        return;
    }
    log_line(std::string("button: ") + (v.update ? "update" : v.installed ? "open" : "get"));
    if (v.installed && !v.update) {
        std::string error;
        bool ok = launch(&error);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_view.error = ok ? "" : error;
        g_view.message = ok ? "FFNY Studio is starting." : "";
        return;
    }
    if (!start_work()) return;
    std::thread(run_install).detach();
#endif
}
} // namespace studio
