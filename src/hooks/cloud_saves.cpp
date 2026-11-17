// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Cloud saves underneath: see cloud_saves.h. Standard C++17 only (and stb's deflate).
#define _CRT_SECURE_NO_WARNINGS
#include "cloud_saves.h"
#include "pc_ui_model.h"         // pcui::path_utf8

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

// stb's zlib-style deflate (the same header bug_report.c uses for its PNGs), private to this file.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4505)
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../tools/xboxrecomp/src/nv2a/stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace cloud
{
namespace
{
// ---- SHA-256 ----
struct Sha256
{
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint64_t length = 0;
    uint8_t buf[64];
    size_t used = 0;
    static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t *p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
            0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
            0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
            0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void add(const uint8_t *p, size_t n)
    {
        length += n;
        while (n) {
            size_t k = std::min(64 - used, n);
            memcpy(buf + used, p, k);
            used += k; p += k; n -= k;
            if (used == 64) { block(buf); used = 0; }
        }
    }
    std::string hex()
    {
        uint64_t bits = length * 8;
        uint8_t one = 0x80, zero = 0, tail[8];
        add(&one, 1);
        while (used != 56) add(&zero, 1);
        for (int i = 0; i < 8; i++) tail[i] = (uint8_t)(bits >> (56 - 8 * i));
        add(tail, 8);
        char out[65];
        for (int i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", h[i]);
        return std::string(out, 64);
    }
};

void put16(Bytes &b, uint32_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
void put32(Bytes &b, uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
uint32_t rd16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

std::string lower(std::string s)
{
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool ends_with(const std::string &s, const char *tail)
{
    size_t n = strlen(tail);
    return s.size() >= n && !s.compare(s.size() - n, n, tail);
}

bool starts_with(const Bytes &b, const char *text, size_t n)
{
    return b.size() >= n && !memcmp(b.data(), text, n);
}

// One path component: 1..64 printable ASCII characters a Windows file name can have, not starting with a
// space, not ending in a space or a dot, never "." or ".." (relay/accounts/cloudsaves.go cloudPart).
bool good_part(const std::string &p)
{
    if (p.empty() || p.size() > 64 || p == "." || p == ".." || p[0] == ' ' || p.back() == ' ' || p.back() == '.')
        return false;
    for (unsigned char c : p)
        if (c < 0x20 || c > 0x7E || strchr("\\/:*?\"<>|", c)) return false;
    return true;
}

std::vector<std::string> split(const std::string &name)
{
    std::vector<std::string> parts;
    size_t at = 0;
    for (;;) {
        size_t slash = name.find('/', at);
        parts.push_back(name.substr(at, slash == std::string::npos ? slash : slash - at));
        if (slash == std::string::npos) break;
        at = slash + 1;
    }
    return parts;
}

bool is_profile_file(const std::string &name, const Bytes &data)
{
    std::string base = name.substr(name.rfind('/') + 1);
    return starts_with(data, "20CM", 4) && lower(base) != "options";
}

// The folder a file belongs to ("UserData/45410049/<folder>" or "aside/<stash>"), "" for the title's own files.
std::string group_of(const std::string &name)
{
    auto parts = split(name);
    if (parts.size() == 4) return parts[0] + "/" + parts[1] + "/" + parts[2];
    if (parts.size() == 3 && parts[0] == "aside") return parts[0] + "/" + parts[1];
    return "";
}

bool read_bytes(const fs::path &p, Bytes &out, uintmax_t limit)
{
    std::error_code ec;
    uintmax_t size = fs::file_size(p, ec);
    if (ec || size > limit) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.resize((size_t)size);
    return !size || (bool)f.read((char *)out.data(), (std::streamsize)size);
}

bool write_bytes(const fs::path &p, const Bytes &data)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write((const char *)data.data(), (std::streamsize)data.size());
    f.close();
    return !f.fail();
}

std::string stamp(int64_t now)
{
    std::time_t t = (std::time_t)now;
    std::tm tm = {};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char text[32];
    strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &tm);
    return text;
}

// Writes files (cloud paths) under `root`; false with *why on the first failure.
bool write_tree(const Files &files, const fs::path &root, const std::string &prefix, std::string *why)
{
    for (auto &kv : files) {
        if (kv.first.compare(0, prefix.size(), prefix)) continue;
        fs::path p = root / fs::u8path(kv.first.substr(prefix.size()));
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        if (ec || !write_bytes(p, kv.second)) {
            if (why) *why = "could not write " + kv.first;
            return false;
        }
    }
    return true;
}

// The profile's name in a group (its SaveMeta.xbx), else the folder's.
std::string group_name(const Files &files, const std::string &group)
{
    auto it = files.find(group + "/SaveMeta.xbx");
    std::string name;
    if (it != files.end() && saves::parse_save_meta(it->second, name) && !name.empty()) return name;
    return group.substr(group.rfind('/') + 1);
}

std::map<std::string, Files> groups(const Files &files)
{
    std::map<std::string, Files> out;
    for (auto &kv : files) {
        std::string g = group_of(kv.first);
        if (!g.empty()) out[g][kv.first.substr(g.size() + 1)] = kv.second;
    }
    return out;
}

bool group_is_profile(const Files &group)
{
    for (auto &kv : group)
        if (is_profile_file(kv.first, kv.second)) return true;
    return false;
}

bool group_is_options(const Files &group)
{
    for (auto &kv : group)
        if (lower(kv.first) == "options" && starts_with(kv.second, "20CM", 4)) return true;
    return false;
}
} // namespace

std::string sha256_hex(const uint8_t *data, size_t size)
{
    Sha256 s;
    s.add(data, size);
    return s.hex();
}

std::string content_hash(const Files &files)
{
    Sha256 s;
    for (auto &kv : files) {        // std::map: sorted by the path's bytes, as Go's sort.Strings
        s.add((const uint8_t *)kv.first.data(), kv.first.size());
        uint8_t zero = 0, size[8];
        s.add(&zero, 1);
        uint64_t n = kv.second.size();
        for (int i = 0; i < 8; i++) size[i] = (uint8_t)(n >> (8 * i));
        s.add(size, 8);
        if (!kv.second.empty()) s.add(kv.second.data(), kv.second.size());
    }
    return s.hex();
}

std::string file_fault(const std::string &name, const Bytes &data)
{
    auto parts = split(name);
    for (auto &p : parts)
        if (!good_part(p)) return "a file name that is not allowed (" + name + ")";
    bool place = (parts.size() == 3 && parts[0] == "UserData" && parts[1] == saves::kTitleId && ends_with(lower(parts[2]), ".xbx")) ||
                 (parts.size() == 4 && parts[0] == "UserData" && parts[1] == saves::kTitleId) ||
                 (parts.size() == 3 && parts[0] == "aside");
    if (!place) return "a file outside the save area (" + name + ")";
    if (data.size() > kMaxFile) return "a file larger than 64 KiB (" + name + ")";
    bool xbx = ends_with(lower(name), ".xbx");
    if (xbx && (starts_with(data, "\xFF\xFE", 2) || starts_with(data, "XPR0", 4))) return "";
    if (!xbx && starts_with(data, "20CM", 4)) return "";
    return "not a Def Jam save file (" + name + ")";
}

int count_profiles(const Files &files)
{
    int n = 0;
    for (auto &kv : files) n += is_profile_file(kv.first, kv.second);
    return n;
}

int count_game_profiles(const Files &files)
{
    int n = 0;
    for (auto &kv : files) n += !kv.first.compare(0, 9, "UserData/") && is_profile_file(kv.first, kv.second);
    return n;
}

bool collect(const fs::path &save_dir, Files &out, std::vector<std::string> *skipped, std::string *error)
{
    out.clear();
    size_t total = 0;
    auto skip = [&](const std::string &why) { if (skipped) skipped->push_back(why); };
    // a folder's own files (depth 0) under `prefix`; profile: a folder in it is reported
    auto take_files = [&](const fs::path &dir, const std::string &prefix, bool profile) -> bool {
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            auto type = it->symlink_status(ec).type();
            std::string leaf = pcui::path_utf8(it->path().filename()), name = prefix + leaf;
            if (type != fs::file_type::regular) {
                if (type == fs::file_type::directory && profile) skip("a folder inside a profile (" + name + ")");
                continue;
            }
            Bytes data;
            std::error_code e2;
            if (fs::file_size(it->path(), e2) > kMaxFile) { skip("a file larger than 64 KiB (" + name + ")"); continue; }
            if (!read_bytes(it->path(), data, kMaxFile)) {
                if (error) *error = "could not read " + name;
                return false;
            }
            std::string why = file_fault(name, data);
            if (!why.empty()) { skip(why); continue; }
            total += data.size();
            out[name] = std::move(data);
        }
        if (ec) { if (error) *error = "could not read " + pcui::path_utf8(dir); return false; }
        return true;
    };
    std::error_code ec;
    fs::path title = save_dir / "UserData" / saves::kTitleId;
    std::string tprefix = std::string("UserData/") + saves::kTitleId + "/";
    if (fs::symlink_status(title, ec).type() == fs::file_type::directory) {
        for (fs::directory_iterator it(title, ec), end; !ec && it != end; it.increment(ec)) {
            auto type = it->symlink_status(ec).type();
            std::string leaf = pcui::path_utf8(it->path().filename());
            if (type == fs::file_type::directory) {
                if (!good_part(leaf)) { skip("a folder name that is not allowed (" + leaf + ")"); continue; }
                if (!take_files(it->path(), tprefix + leaf + "/", true)) return false;
            }
        }
        if (!take_files(title, tprefix, false)) return false;
    }
    fs::path aside = saves::aside_dir(save_dir);
    if (fs::symlink_status(aside, ec).type() == fs::file_type::directory) {
        for (fs::directory_iterator it(aside, ec), end; !ec && it != end; it.increment(ec)) {
            std::string leaf = pcui::path_utf8(it->path().filename());
            if (it->symlink_status(ec).type() != fs::file_type::directory) continue;
            if (!leaf.compare(0, 8, ".moving-")) continue;     // put_aside's own copy while it moves one
            if (!good_part(leaf)) { skip("a folder name that is not allowed (" + leaf + ")"); continue; }
            if (!take_files(it->path(), "aside/" + leaf + "/", true)) return false;
        }
    }
    if (out.size() > kMaxFiles || total > kMaxTotal) {
        if (error)
            *error = "Your saves are too large for the cloud (" + std::to_string(out.size()) + " files, " +
                     std::to_string(total >> 10) + " KiB; at most 400 files and 8 MiB).";
        return false;
    }
    return true;
}

Bytes pack(const Files &files)
{
    Bytes zip, central;
    uint16_t count = 0;
    for (auto &kv : files) {
        const Bytes &raw = kv.second;
        uint32_t crc = saves::crc32(raw.data(), raw.size());
        Bytes body;
        uint16_t method = 0;
        if (raw.size() > 64) {
            int n = 0;
            unsigned char *z = stbi_zlib_compress((unsigned char *)raw.data(), (int)raw.size(), &n, 8);
            // zlib's own two header bytes and its Adler-32 around the deflate stream a zip holds
            if (z && n > 6 && (size_t)(n - 6) < raw.size()) {
                body.assign(z + 2, z + n - 4);
                method = 8;
            }
            STBIW_FREE(z);
        }
        if (!method) body = raw;
        uint32_t offset = (uint32_t)zip.size();
        put32(zip, 0x04034b50);
        put16(zip, 20); put16(zip, 0); put16(zip, method);
        put16(zip, 0); put16(zip, 0x21);            // 1980-01-01 00:00: the same bytes every time
        put32(zip, crc); put32(zip, (uint32_t)body.size()); put32(zip, (uint32_t)raw.size());
        put16(zip, (uint16_t)kv.first.size()); put16(zip, 0);
        zip.insert(zip.end(), kv.first.begin(), kv.first.end());
        zip.insert(zip.end(), body.begin(), body.end());
        put32(central, 0x02014b50);
        put16(central, 20); put16(central, 20); put16(central, 0); put16(central, method);
        put16(central, 0); put16(central, 0x21);
        put32(central, crc); put32(central, (uint32_t)body.size()); put32(central, (uint32_t)raw.size());
        put16(central, (uint16_t)kv.first.size()); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0); put32(central, 0); put32(central, offset);
        central.insert(central.end(), kv.first.begin(), kv.first.end());
        count++;
    }
    uint32_t cd = (uint32_t)zip.size();
    zip.insert(zip.end(), central.begin(), central.end());
    put32(zip, 0x06054b50);
    put16(zip, 0); put16(zip, 0); put16(zip, count); put16(zip, count);
    put32(zip, (uint32_t)central.size()); put32(zip, cd); put16(zip, 0);
    return zip;
}

bool unpack(const Bytes &zip, Files &out, std::string *error)
{
    out.clear();
    auto fail = [&](const std::string &why) {
        if (error) *error = "The cloud save was not taken: " + why + ".";
        out.clear();
        return false;
    };
    if (zip.size() > kMaxArchive) return fail("it is larger than 4 MiB");
    if (zip.size() < 22) return fail("it is not a zip archive");
    size_t end = std::string::npos;
    for (size_t i = zip.size() - 22 + 1; i-- > 0 && zip.size() - i <= 22 + 65535;)
        if (rd32(&zip[i]) == 0x06054b50) { end = i; break; }
    if (end == std::string::npos) return fail("it is not a zip archive");
    uint32_t entries = rd16(&zip[end + 10]), cd_size = rd32(&zip[end + 12]), cd = rd32(&zip[end + 16]);
    if (!entries || entries > kMaxFiles) return fail("it holds " + std::to_string(entries) + " files (1..400)");
    if ((uint64_t)cd + cd_size > end) return fail("its directory is damaged");
    size_t at = cd, total = 0;
    std::set<std::string> seen;
    for (uint32_t i = 0; i < entries; i++) {
        if (at + 46 > end || rd32(&zip[at]) != 0x02014b50) return fail("its directory is damaged");
        uint16_t flags = (uint16_t)rd16(&zip[at + 8]), method = (uint16_t)rd16(&zip[at + 10]);
        uint32_t crc = rd32(&zip[at + 16]), csize = rd32(&zip[at + 20]), usize = rd32(&zip[at + 24]);
        uint32_t nlen = rd16(&zip[at + 28]), xlen = rd16(&zip[at + 30]), clen = rd16(&zip[at + 32]), local = rd32(&zip[at + 42]);
        if (at + 46 + nlen > end) return fail("its directory is damaged");
        std::string name((const char *)&zip[at + 46], nlen);
        at += 46 + nlen + xlen + clen;
        if (!name.empty() && name.back() == '/') continue;     // a folder entry
        if (flags & 1) return fail(name + " is encrypted");
        if (method != 0 && method != 8) return fail(name + " is packed in a way that is not allowed");
        if (usize > kMaxFile) return fail(name + " is larger than 64 KiB");
        if (!seen.insert(lower(name)).second) return fail(name + " is in it twice");
        if ((uint64_t)local + 30 > zip.size() || rd32(&zip[local]) != 0x04034b50) return fail(name + " is damaged");
        size_t data = local + 30 + rd16(&zip[local + 26]) + rd16(&zip[local + 28]);
        if ((uint64_t)data + csize > zip.size()) return fail(name + " is damaged");
        Bytes raw;
        if (method == 0) {
            if (csize != usize) return fail(name + " is damaged");
            raw.assign(zip.begin() + data, zip.begin() + data + csize);
        } else if (!saves::inflate_raw(&zip[data], csize, raw, kMaxFile) || raw.size() != usize) {
            return fail(name + " is damaged");
        }
        if (saves::crc32(raw.data(), raw.size()) != crc) return fail(name + " is damaged");
        if ((total += raw.size()) > kMaxTotal) return fail("it is larger than 8 MiB unpacked");
        std::string why = file_fault(name, raw);
        if (!why.empty()) return fail(why);
        out[name] = std::move(raw);
    }
    if (!count_profiles(out)) return fail("there is no profile in it");
    return true;
}

// ---- restore ----

std::vector<Change> compare(const Files &local, const Files &cloud)
{
    std::vector<Change> out;
    auto here = groups(local), there = groups(cloud);
    std::set<std::string> keys;
    for (auto &kv : here) keys.insert(kv.first);
    for (auto &kv : there) keys.insert(kv.first);
    for (auto &key : keys) {
        auto h = here.find(key), t = there.find(key);
        const Files &any = h != here.end() ? h->second : t->second;
        bool options = group_is_options(any);
        if (!options && !group_is_profile(any)) continue;
        Change c;
        c.aside = !key.compare(0, 6, "aside/");
        c.name = options ? "Options" : group_name(h != here.end() ? local : cloud, key);
        if (h == here.end()) c.what = Change::ADDED;
        else if (t == there.end()) c.what = c.aside ? Change::SAME : Change::ONLY_HERE;   // aside: it stays
        else if (h->second == t->second) c.what = Change::SAME;
        else c.what = c.aside ? Change::KEPT : Change::REPLACED;
        out.push_back(c);
    }
    std::stable_sort(out.begin(), out.end(), [](const Change &a, const Change &b) {
        if (a.aside != b.aside) return !a.aside;
        if ((a.name == "Options") != (b.name == "Options")) return b.name == "Options";
        return lower(a.name) < lower(b.name);
    });
    return out;
}

bool keep_as_backup(const Files &cloud, const fs::path &backups, int64_t now, const std::string &note,
                    std::string *made, std::string *error)
{
    std::error_code ec;
    fs::create_directories(backups, ec);
    std::string name = stamp(now);
    for (int n = 2; fs::exists(backups / name, ec) && n < 100; n++) name = stamp(now) + "-" + std::to_string(n);
    fs::path dst = backups / name, staging = backups / (".cloud-" + name);
    saves::fs::remove_all(staging, ec);
    std::string why;
    if (!write_tree(cloud, staging, "", &why)) {
        fs::remove_all(staging, ec);
        if (error) *error = "The cloud save could not be kept on this PC: " + why + ".";
        return false;
    }
    {
        std::ofstream info(staging / "backup.txt", std::ios::trunc);
        info << "when=" << now << "\nreason=cloud\nprofiles=" << count_game_profiles(cloud) << "\nfrom=" << note << "\n";
    }
    fs::rename(staging, dst, ec);
    if (ec) {
        fs::remove_all(staging, ec);
        if (error) *error = "The cloud save could not be kept on this PC (" + ec.message() + ").";
        return false;
    }
    if (made) *made = name;
    saves::rotate(backups, saves::kKeepBackups, name);
    fprintf(stderr, "[CLOUD] cloud save %s kept as backup %s (%zu files)\n", note.c_str(), name.c_str(), cloud.size());
    return true;
}

bool restore(const Files &cloud, const fs::path &save_dir, const fs::path &backups, int64_t now, const std::string &note,
             std::string *message)
{
    auto say = [&](const std::string &text) { if (message) *message = text; };
    if (!count_profiles(cloud)) { say("Nothing was changed: that cloud save holds no profile."); return false; }
    std::string made, why;
    if (!keep_as_backup(cloud, backups, now, note, &made, &why)) { say("Nothing was changed. " + why); return false; }
    std::string text;
    if (count_game_profiles(cloud) || cloud.count(std::string("UserData/") + saves::kTitleId + "/TitleMeta.xbx")) {
        bool ok = saves::restore(save_dir, backups, made, now, &text);
        if (!ok) { say(text); return false; }
    }
    // the cloud's profiles put aside: into <save>/aside where that folder is free; the PC's own stay
    int added = 0;
    auto there = groups(cloud);
    for (auto &kv : there) {
        if (kv.first.compare(0, 6, "aside/")) continue;
        fs::path dst = saves::aside_dir(save_dir) / fs::u8path(kv.first.substr(6));
        std::error_code ec;
        if (fs::exists(fs::symlink_status(dst, ec))) continue;
        Files one;
        for (auto &f : kv.second) one[f.first] = f.second;
        fs::path staging = saves::aside_dir(save_dir) / fs::u8path(".moving-cloud-" + kv.first.substr(6));
        fs::remove_all(staging, ec);
        if (!write_tree(one, staging, "", &why)) { fs::remove_all(staging, ec); continue; }
        fs::rename(staging, dst, ec);
        if (ec) { fs::remove_all(staging, ec); continue; }
        added++;
    }
    std::string out = "Your cloud save from " + note + " is in place.";
    if (added) out += " " + std::to_string(added) + (added == 1 ? " profile" : " profiles") + " put aside came with it.";
    out += " Your saves from before are in a backup.";
    say(out);
    fprintf(stderr, "[CLOUD] restored %s (%d profiles in the game, %d put aside added)\n", note.c_str(),
            count_game_profiles(cloud), added);
    return true;
}

// ---- the service's answers ----

std::string text_field(const std::string &text, const std::string &key)
{
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > key.size() && !line.compare(0, key.size(), key) && line[key.size()] == '=')
            return line.substr(key.size() + 1);
    }
    return "";
}

Listing parse_listing(const std::string &text)
{
    Listing l;
    std::istringstream in(text);
    std::string line;
    bool counted = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.compare(0, 8, "account=")) l.account = line.substr(8);
        else if (!line.compare(0, 9, "versions=")) counted = true;
        else if (!line.compare(0, 2, "v=")) {
            std::vector<std::string> f;
            size_t at = 2;
            for (int i = 0; i < 7; i++) {
                size_t bar = line.find('|', at);
                if (bar == std::string::npos) break;
                f.push_back(line.substr(at, bar - at));
                at = bar + 1;
            }
            if (f.size() != 7 || f[1].size() != 64) continue;
            Version v;
            v.id = f[0]; v.hash = f[1];
            v.size = atoll(f[2].c_str()); v.bytes = atoll(f[3].c_str());
            v.files = atoi(f[4].c_str()); v.profiles = atoi(f[5].c_str());
            v.uploaded = atoll(f[6].c_str());
            v.pc = line.substr(at);
            l.versions.push_back(v);
        }
    }
    l.ok = counted && !l.account.empty();
    return l;
}

SyncState read_state(const fs::path &file)
{
    SyncState s;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "account") s.account = v;
        else if (k == "hash" && (v.size() == 64 || v.empty())) s.hash = v;
        else if (k == "version") s.version = v;
        else if (k == "when") s.when = atoll(v.c_str());
    }
    return s;
}

bool write_state(const fs::path &file, const SyncState &s)
{
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "# FFNY Online cloud saves: what this PC and the cloud last agreed on (the launcher writes it)\n"
            << "account=" << s.account << "\nhash=" << s.hash << "\nversion=" << s.version << "\nwhen=" << s.when << "\n";
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    return !ec;
}

// ---- this PC's label ----

std::string clean_label(const std::string &text)
{
    std::string out;
    for (unsigned char c : text) {
        if (out.size() >= 24) break;
        if (isalnum(c) || c == ' ' || c == '.' || c == '-' || c == '_') {
            if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
            out.push_back((char)c);
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string new_label(uint32_t random)
{
    static const char k[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";     // no 0/O, 1/I
    std::string out = "PC ";
    for (int i = 0; i < 4; i++) {
        out.push_back(k[random % 32]);
        random /= 32;
    }
    return out;
}

std::string read_label(const fs::path &file)
{
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.compare(0, 6, "label=")) {
            std::string l = clean_label(line.substr(6));
            if (l == line.substr(6)) return l;
        }
    }
    return "";
}

bool write_label(const fs::path &file, const std::string &label)
{
    std::string l = clean_label(label);
    if (l.empty()) return false;
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "# FFNY Online cloud saves: this PC's name in your cloud's list (rename it on Saves, Cloud)\nlabel=" << l
            << "\n";
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    return !ec;
}

// ---- what to do ----

Decision decide(const std::string &local, bool local_profiles, const std::string &base, const std::string &cloud)
{
    if (!cloud.empty() && local == cloud) return Decision::IN_SYNC;
    if (cloud.empty()) return local_profiles ? Decision::UPLOAD : Decision::NOTHING;
    if (!local_profiles) return base == cloud ? Decision::NOTHING : Decision::OFFER_RESTORE;
    if (base == cloud) return Decision::UPLOAD;              // only this PC changed
    if (!base.empty() && base == local) return Decision::CLOUD_NEWER;   // only the cloud changed
    return Decision::CONFLICT;
}

const char *decision_name(Decision d)
{
    switch (d) {
    case Decision::NOTHING: return "nothing";
    case Decision::IN_SYNC: return "in-sync";
    case Decision::UPLOAD: return "upload";
    case Decision::OFFER_RESTORE: return "offer-restore";
    case Decision::CLOUD_NEWER: return "cloud-newer";
    case Decision::CONFLICT: return "conflict";
    }
    return "?";
}
} // namespace cloud
