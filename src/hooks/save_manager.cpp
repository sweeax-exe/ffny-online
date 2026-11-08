// The Saves page underneath: see save_manager.h. Standard C++17 only.
#define _CRT_SECURE_NO_WARNINGS
#include "save_manager.h"
#include "pc_ui_model.h"         // pcui::path_utf8: UTF-8 text of a path under C++17 and C++20

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

namespace saves
{
const uint8_t kPortKey[16] = {};

namespace
{
constexpr size_t kHeader = 0x4C;            // "20CM", three sizes, three 20-byte HMAC fields
constexpr size_t kIdentityEnd = 0x8C;       // the 64-byte identity block starts at kHeader
constexpr size_t kProfileStart = 0x80;      // the profile image starts here in the file
constexpr size_t kCreatedFlag = 0x4D8A;     // profile +0x4D8A: 1 with a created fighter (created_fighter.c)
constexpr size_t kMaxMember = 4u << 20;     // a profile is ~22 KiB: refuse anything absurd
constexpr uintmax_t kMaxCopy = 64u << 20;   // a backup never copies a file larger than this
constexpr size_t kMaxFiles = 20000;         // a picked folder or zip with more is not a save folder
constexpr uint64_t kHashMod = 0xFFFFFFFFFFC5ull;

// ---- SHA-1 and HMAC-SHA1 ----
struct Sha1
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    uint64_t length = 0;
    uint8_t buf[64];
    size_t used = 0;
    static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
    void block(const uint8_t *p)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
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
    void finish(uint8_t out[20])
    {
        uint64_t bits = length * 8;
        uint8_t one = 0x80, zero = 0, tail[8];
        add(&one, 1);
        while (used != 56) add(&zero, 1);
        for (int i = 0; i < 8; i++) tail[i] = (uint8_t)(bits >> (56 - 8 * i));
        add(tail, 8);
        for (int i = 0; i < 5; i++)
            for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(h[i] >> (24 - 8 * j));
    }
};

uint32_t rd16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

std::string lower(std::string s)
{
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

int64_t unix_time(fs::file_time_type t)
{
    using namespace std::chrono;
    auto sys = time_point_cast<system_clock::duration>(t - fs::file_time_type::clock::now() + system_clock::now());
    return duration_cast<seconds>(sys.time_since_epoch()).count();
}

std::tm local(int64_t when)
{
    std::time_t t = (std::time_t)when;
    std::tm tm = {};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

bool read_file(const fs::path &p, Bytes &out, uintmax_t limit)
{
    std::error_code ec;
    uintmax_t size = fs::file_size(p, ec);
    if (ec || size > limit) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.resize((size_t)size);
    if (size && !f.read((char *)out.data(), (std::streamsize)size)) return false;
    return true;
}

bool write_file(const fs::path &p, const Bytes &data)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write((const char *)data.data(), (std::streamsize)data.size());
    f.close();
    return !f.fail();
}

bool real_dir(const fs::path &p)
{
    std::error_code ec;
    return fs::symlink_status(p, ec).type() == fs::file_type::directory;
}

// Removes a folder we made: real folders and plain files only. A link or a junction anywhere
// inside is left alone (and so is the folder holding it), never followed.
bool remove_tree(const fs::path &p)
{
    std::error_code ec;
    auto type = fs::symlink_status(p, ec).type();
    if (ec || type == fs::file_type::not_found) return true;
    if (type == fs::file_type::regular) return fs::remove(p, ec);
    if (type != fs::file_type::directory) return false;
    bool ok = true;
    std::vector<fs::path> children;
    for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) children.push_back(it->path());
    if (ec) return false;
    for (auto &c : children) ok = remove_tree(c) && ok;
    return ok && fs::remove(p, ec);
}

// Copies real folders and plain files, never through a link or a junction.
bool copy_tree(const fs::path &from, const fs::path &to, int *files, std::string *error, int depth = 0)
{
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) { if (error) *error = "could not make " + pcui::path_utf8(to); return false; }
    for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        auto type = it->symlink_status(ec).type();
        if (ec) break;
        fs::path target = to / it->path().filename();
        if (type == fs::file_type::directory) {
            if (depth < 8 && !copy_tree(it->path(), target, files, error, depth + 1)) return false;
        } else if (type == fs::file_type::regular) {
            std::error_code e2;
            if (fs::file_size(it->path(), e2) > kMaxCopy) continue;
            if (!fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, e2)) {
                if (error) *error = "could not copy " + pcui::path_utf8(it->path().filename()) + ": " + e2.message();
                return false;
            }
            if (files) ++*files;
        }
    }
    if (ec) { if (error) *error = "could not read " + pcui::path_utf8(from) + ": " + ec.message(); return false; }
    return true;
}

int count_files(const fs::path &dir, int depth = 0)
{
    std::error_code ec;
    int n = 0;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        auto type = it->symlink_status(ec).type();
        if (type == fs::file_type::regular) n++;
        else if (type == fs::file_type::directory && depth < 8) n += count_files(it->path(), depth + 1);
    }
    return n;
}

// A file in `dir` by name, exactly or else ignoring case.
fs::path find_file(const fs::path &dir, const std::string &name)
{
    std::error_code ec;
    fs::path exact = dir / fs::u8path(name);
    if (fs::symlink_status(exact, ec).type() == fs::file_type::regular) return exact;
    std::string want = lower(name);
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->symlink_status(ec).type() == fs::file_type::regular && lower(pcui::path_utf8(it->path().filename())) == want)
            return it->path();
    return {};
}

std::string stamp(int64_t now)
{
    std::tm tm = local(now);
    char text[32];
    strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &tm);
    return text;
}

int64_t time_of_name(const std::string &name)
{
    std::tm tm = {};
    if (sscanf(name.c_str(), "%4d%2d%2d-%2d%2d%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
               &tm.tm_sec) != 6)
        return 0;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    return (int64_t)mktime(&tm);
}

// Newest first: by the stamp, then by the -n suffix of backups made within one second.
bool newer(const std::string &a, const std::string &b)
{
    std::string sa = a.substr(0, 15), sb = b.substr(0, 15);
    if (sa != sb) return sa > sb;
    int na = a.size() > 16 ? atoi(a.c_str() + 16) : 1, nb = b.size() > 16 ? atoi(b.c_str() + 16) : 1;
    return na > nb;
}

bool bad_file_name(const std::string &name)
{
    if (name.empty() || name.back() == '.' || name.back() == ' ') return true;
    return name.find_first_of("\\/:*?\"<>|") != std::string::npos;
}

std::string join_names(const std::vector<std::string> &names)
{
    std::string out;
    size_t shown = std::min<size_t>(names.size(), 4);
    for (size_t i = 0; i < shown; i++) {
        if (i) out += i + 1 == shown && names.size() == shown ? " and " : ", ";
        out += names[i];
    }
    if (names.size() > shown) out += " and " + std::to_string(names.size() - shown) + " more";
    return out;
}

std::string plural(int n, const char *one, const char *many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}
} // namespace

// ---- signing ----

void hmac_sha1(const uint8_t *key, size_t key_size, const uint8_t *data, size_t size, uint8_t out[20])
{
    uint8_t k[64] = {}, pad[64], inner[20];
    if (key_size > 64) {
        Sha1 s;
        s.add(key, key_size);
        s.finish(k);
    } else if (key_size) {
        memcpy(k, key, key_size);
    }
    Sha1 a;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    a.add(pad, 64);
    a.add(data, size);
    a.finish(inner);
    Sha1 b;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    b.add(pad, 64);
    b.add(inner, 20);
    b.finish(out);
}

std::string header_fault(const Bytes &blob)
{
    if (blob.size() < kIdentityEnd + 1) return "too short";
    if (memcmp(blob.data(), "20CM", 4)) return "not a 20CM profile file";
    uint32_t size = rd32(&blob[4]), header = rd32(&blob[8]), payload = rd32(&blob[12]);
    if (size != blob.size()) return "its size field does not match the file";
    if (header != 0x40) return "unexpected header size";
    if (payload != blob.size() - kIdentityEnd) return "its payload size field does not match the file";
    return "";
}

bool signatures_ok(const Bytes &blob, const uint8_t key[16])
{
    if (blob.size() < kIdentityEnd + 1) return false;
    uint8_t h[20];
    hmac_sha1(key, 16, &blob[kHeader], kIdentityEnd - kHeader, h);
    if (memcmp(h, &blob[0x10], 20)) return false;
    hmac_sha1(key, 16, &blob[kIdentityEnd], blob.size() - kIdentityEnd, h);
    if (memcmp(h, &blob[0x24], 20)) return false;
    hmac_sha1(key, 16, &blob[0], 0x38, h);
    return !memcmp(h, &blob[0x38], 20);
}

Bytes resign(const Bytes &blob, const uint8_t key[16])
{
    Bytes out = blob;
    if (out.size() < kIdentityEnd + 1) return out;
    // The order matters: the 0x38 field covers the other two.
    hmac_sha1(key, 16, &out[kHeader], kIdentityEnd - kHeader, &out[0x10]);
    hmac_sha1(key, 16, &out[kIdentityEnd], out.size() - kIdentityEnd, &out[0x24]);
    hmac_sha1(key, 16, &out[0], 0x38, &out[0x38]);
    return out;
}

std::string folder_name(const std::string &name)
{
    uint64_t value = 0;
    for (unsigned char c : name) value = (value * 65536 + c) % kHashMod;
    char text[16];
    snprintf(text, sizeof(text), "%012llX", (unsigned long long)value);
    return text;
}

bool usable_name(const std::string &name)
{
    if (name.empty() || name.size() > 32) return false;
    for (unsigned char c : name)
        if (c < 0x20 || c > 0x7E) return false;
    return true;
}

bool parse_save_meta(const Bytes &raw, std::string &name)
{
    // UTF-16LE with a BOM ("Name=VY2\r\n..."); python decodes 'utf-16' (a BOM, or little-endian).
    size_t i = 0;
    bool big = false;
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) i = 2;
    else if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) { i = 2; big = true; }
    std::u16string text;
    for (; i + 1 < raw.size(); i += 2) text.push_back(big ? (char16_t)(raw[i] << 8 | raw[i + 1]) : (char16_t)(raw[i] | raw[i + 1] << 8));
    static const char16_t key[] = u"Name=";
    if (text.compare(0, 5, key) != 0) return false;
    name.clear();
    for (size_t k = 5; k < text.size() && text[k] != u'\r' && text[k] != u'\n'; k++) {
        char16_t c = text[k];
        if (c >= 0x80) name.push_back('?');     // not a name we can use; usable_name says so
        else name.push_back((char)c);
    }
    return true;
}

// ---- the save area ----

std::vector<Entry> list_saves(const fs::path &save_dir)
{
    std::vector<Entry> out;
    fs::path title = save_dir / "UserData" / kTitleId;
    std::error_code ec;
    for (fs::directory_iterator it(title, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->symlink_status(ec).type() != fs::file_type::directory) continue;
        Entry e;
        e.folder = pcui::path_utf8(it->path().filename());
        Bytes meta, blob;
        fs::path meta_path = find_file(it->path(), "SaveMeta.xbx"), main;
        if (!meta_path.empty() && read_file(meta_path, meta, 64 * 1024)) parse_save_meta(meta, e.name);
        if (!e.name.empty()) main = find_file(it->path(), e.name);
        std::error_code e2;
        if (!main.empty()) e.changed = unix_time(fs::last_write_time(main, e2));
        if (e.name == "Options" && !main.empty()) {
            e.kind = Entry::OPTIONS;
        } else if (!main.empty() && read_file(main, blob, kMaxMember) && header_fault(blob).empty()) {
            e.kind = Entry::PROFILE;
            e.signed_ok = signatures_ok(blob, kPortKey);
            e.created = blob.size() > kProfileStart + kCreatedFlag && blob[kProfileStart + kCreatedFlag] == 1;
        } else {
            e.kind = Entry::OTHER;
            if (!e.changed)
                for (fs::directory_iterator f(it->path(), e2), fend; !e2 && f != fend; f.increment(e2))
                    if (f->symlink_status(e2).type() == fs::file_type::regular)
                        e.changed = std::max(e.changed, unix_time(fs::last_write_time(f->path(), e2)));
        }
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        return lower(a.name.empty() ? a.folder : a.name) < lower(b.name.empty() ? b.folder : b.name);
    });
    return out;
}

// ---- backups ----

bool is_backup_name(const std::string &name)
{
    auto digits = [&](size_t from, size_t n) {
        for (size_t i = from; i < from + n; i++)
            if (i >= name.size() || !isdigit((unsigned char)name[i])) return false;
        return true;
    };
    if (name.size() < 15 || !digits(0, 8) || name[8] != '-' || !digits(9, 6)) return false;
    if (name.size() == 15) return true;
    return name[15] == '-' && name.size() >= 17 && name.size() <= 19 && digits(16, name.size() - 16);
}

std::vector<Backup> list_backups(const fs::path &backups)
{
    std::vector<Backup> out;
    std::error_code ec;
    for (fs::directory_iterator it(backups, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = pcui::path_utf8(it->path().filename());
        if (it->symlink_status(ec).type() != fs::file_type::directory || !is_backup_name(name)) continue;
        Backup b;
        b.name = name;
        b.path = it->path();
        std::ifstream info(it->path() / "backup.txt");
        std::string line;
        bool counted = false;
        while (std::getline(info, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.compare(0, 5, "when=")) b.when = atoll(line.c_str() + 5);
            else if (!line.compare(0, 7, "reason=")) b.reason = line.substr(7);
            else if (!line.compare(0, 9, "profiles=")) b.profiles = atoi(line.c_str() + 9), counted = true;
        }
        if (!b.when) b.when = time_of_name(name);
        if (!counted) b.profiles = count_profiles(it->path());
        out.push_back(b);
    }
    std::sort(out.begin(), out.end(), [](const Backup &a, const Backup &b) { return newer(a.name, b.name); });
    return out;
}

int rotate(const fs::path &backups, int keep, const std::string &protect)
{
    auto list = list_backups(backups);
    int kept = 0, removed = 0;
    std::string good;                       // the newest backup with a profile in it stays, whatever its age
    for (auto &b : list)
        if (b.profiles > 0) { good = b.name; break; }
    for (auto &b : list) {
        if (b.name == protect) continue;
        if (kept < keep) { kept++; continue; }
        if (b.name == good) {
            fprintf(stderr, "[SAVES] backup %s kept past the newest %d: it is the newest one with a profile (%d)\n",
                    b.name.c_str(), keep, b.profiles);
            continue;
        }
        // only ever a backup folder directly inside save-backups (list_backups found it there)
        if (!is_backup_name(pcui::path_utf8(b.path.filename()))) continue;
        if (remove_tree(b.path)) {
            removed++;
            fprintf(stderr, "[SAVES] old backup %s removed (the newest %d are kept)\n", b.name.c_str(), keep);
        }
    }
    return removed;
}

bool make_backup(const fs::path &save_dir, const fs::path &backups, const std::string &reason, int64_t now,
                 std::string *made, std::string *error, const std::string &protect, int keep)
{
    fs::path src = save_dir / "UserData";
    if (!real_dir(src) || !count_files(src)) {
        if (error) *error = "There are no saves to back up yet.";
        return false;
    }
    int profiles = count_profiles(save_dir);
    if (!profiles && (reason == "start" || reason == "update")) {
        // TitleMeta.xbx and TitleImage.xbx are always there: an empty save area would still copy, and
        // a run of such backups used to push the last good one out
        auto list = list_backups(backups);
        if (!list.empty() && list[0].profiles > 0) {
            fprintf(stderr, "[SAVES] %s backup skipped: the saves hold no profile and the newest backup %s has %d\n",
                    reason.c_str(), list[0].name.c_str(), list[0].profiles);
            if (error)
                *error = "skipped: the saves hold no profile and the newest backup (" + list[0].name + ") has " +
                         plural(list[0].profiles, "profile", "profiles");
            return false;
        }
    }
    std::error_code ec;
    fs::create_directories(backups, ec);
    std::string name = stamp(now);
    for (int n = 2; fs::exists(backups / name, ec) && n < 100; n++) name = stamp(now) + "-" + std::to_string(n);
    fs::path dst = backups / name;
    int files = 0;
    std::string why;
    if (!copy_tree(src, dst / "UserData", &files, &why)) {
        remove_tree(dst);
        if (error) *error = "The backup failed: " + why + ".";
        return false;
    }
    {
        std::ofstream info(dst / "backup.txt", std::ios::trunc);
        info << "when=" << now << "\nreason=" << reason << "\nprofiles=" << profiles << "\n";
    }
    fprintf(stderr, "[SAVES] backup %s (%s): %d files, %d profiles\n", name.c_str(), reason.c_str(), files, profiles);
    if (made) *made = name;
    rotate(backups, keep, protect);
    return true;
}

int count_profiles(const fs::path &save_dir)
{
    int profiles = 0;
    for (auto &e : list_saves(save_dir))
        if (e.kind == Entry::PROFILE) profiles++;
    return profiles;
}

bool daily_due(const fs::path &backups, int64_t now)
{
    std::tm today = local(now);
    for (auto &b : list_backups(backups)) {
        std::tm t = local(b.when);
        if (t.tm_year == today.tm_year && t.tm_yday == today.tm_yday) return false;
    }
    return true;
}

// Moves a UserData.replaced an earlier restore left behind into save-backups as a backup of its own.
static bool keep_leftover(const fs::path &old, const fs::path &backups, int64_t now, std::string *made,
                          std::string *error)
{
    std::error_code ec;
    fs::create_directories(backups, ec);
    std::string name = stamp(now);
    for (int n = 2; fs::exists(backups / name, ec) && n < 100; n++) name = stamp(now) + "-" + std::to_string(n);
    fs::path dst = backups / name;
    fs::create_directories(dst, ec);
    if (ec) { if (error) *error = "could not make " + pcui::path_utf8(dst); return false; }
    int files = count_files(old);
    fs::rename(old, dst / "UserData", ec);
    if (ec) {
        // another volume or a held file: copy, and remove the original only once every file is there
        std::string why;
        int copied = 0;
        if (!copy_tree(old, dst / "UserData", &copied, &why) || count_files(dst / "UserData") != files) {
            remove_tree(dst);
            if (error) *error = why.empty() ? "not every file could be copied" : why;
            return false;
        }
        remove_tree(old);
    }
    int profiles = count_profiles(dst);
    {
        std::ofstream info(dst / "backup.txt", std::ios::trunc);
        info << "when=" << now << "\nreason=leftover\nprofiles=" << profiles << "\nfrom=" << pcui::path_utf8(old)
             << " (left by a restore that did not finish)\n";
    }
    fprintf(stderr, "[SAVES] %s from an unfinished restore kept as backup %s: %d files, %d profiles\n",
            pcui::path_utf8(old).c_str(), name.c_str(), files, profiles);
    if (made) *made = name;
    return true;
}

bool restore(const fs::path &save_dir, const fs::path &backups, const std::string &name, int64_t now,
             std::string *message)
{
    auto say = [&](const std::string &text) { if (message) *message = text; };
    Backup chosen;
    for (auto &b : list_backups(backups))
        if (b.name == name) chosen = b;
    if (chosen.name.empty()) { say("That backup is not there any more."); return false; }
    fs::path src = chosen.path / "UserData";
    if (!real_dir(src)) { say("That backup has no saves in it."); return false; }
    fs::path live = save_dir / "UserData";
    fs::path incoming = save_dir / "UserData.restoring", old = save_dir / "UserData.replaced";
    std::string made, why, leftover;
    std::error_code ec;
    if (fs::exists(fs::symlink_status(old, ec)) && !keep_leftover(old, backups, now, &leftover, &why)) {
        say("Nothing was changed: an earlier restore left " + pcui::path_utf8(old.filename()) +
            " behind and it could not be moved into the backups (" + why + ").");
        return false;
    }
    bool had = real_dir(live) && count_files(live);
    if (had && !make_backup(save_dir, backups, "restore", now, &made, &why, name)) {
        say("Nothing was changed: your saves could not be backed up first. " + why);
        return false;
    }
    remove_tree(incoming);              // only ever a copy of a backup that is still there
    if (!copy_tree(src, incoming, nullptr, &why)) {
        remove_tree(incoming);
        say("Nothing was changed: the backup could not be copied (" + why + ").");
        return false;
    }
    if (fs::exists(live, ec)) {
        fs::rename(live, old, ec);
        if (ec) {
            remove_tree(incoming);
            say("Nothing was changed: the save folder is in use. Close anything that has it open and try again.");
            return false;
        }
    }
    fs::rename(incoming, live, ec);
    if (ec) {
        std::error_code back;
        fs::rename(old, live, back);
        remove_tree(incoming);
        say("Nothing was changed: the backup could not be put in place (" + ec.message() + ").");
        return false;
    }
    remove_tree(old);
    fprintf(stderr, "[SAVES] backup %s restored%s%s\n", name.c_str(), made.empty() ? "" : "; the saves before it are in ",
            made.c_str());
    say("Backup restored from " + describe_time(chosen.when) + "." +
        (made.empty() ? "" : " Your saves from just before are in a new backup."));
    return true;
}

// ---- profiles the game deletes ----

bool is_quarantine_name(const std::string &name)
{
    if (name.size() < 17 || name.size() > 15 + 1 + 48 || !is_backup_name(name.substr(0, 15)) || name[15] != '-')
        return false;
    for (size_t i = 16; i < name.size(); i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_' && name[i] != '-') return false;
    return true;
}

std::vector<Quarantined> list_quarantine(const fs::path &backups)
{
    std::vector<Quarantined> out;
    std::error_code ec;
    for (fs::directory_iterator it(backups / "quarantine", ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = pcui::path_utf8(it->path().filename());
        if (it->symlink_status(ec).type() != fs::file_type::directory || !is_quarantine_name(name)) continue;
        Quarantined q;
        q.name = name;
        q.path = it->path();
        q.folder = name.substr(16);
        q.when = time_of_name(name);
        Bytes meta;
        fs::path meta_path = find_file(it->path(), "SaveMeta.xbx");
        if (!meta_path.empty() && read_file(meta_path, meta, 64 * 1024)) parse_save_meta(meta, q.profile);
        out.push_back(q);
    }
    std::sort(out.begin(), out.end(), [](const Quarantined &a, const Quarantined &b) {
        std::string sa = a.name.substr(0, 15), sb = b.name.substr(0, 15);
        return sa != sb ? sa > sb : a.name > b.name;
    });
    return out;
}

bool quarantine(const fs::path &folder, const fs::path &backups, int64_t now, std::string *made, std::string *error,
                int keep)
{
    if (!real_dir(folder) || !count_files(folder)) {
        if (error) *error = "there is nothing in " + pcui::path_utf8(folder.filename());
        return false;
    }
    std::string base = pcui::path_utf8(folder.filename());
    for (char &ch : base)
        if (!isalnum((unsigned char)ch) && ch != '_') ch = '_';
    if (base.size() > 40) base.resize(40);
    fs::path q = backups / "quarantine";
    std::error_code ec;
    fs::create_directories(q, ec);
    std::string name = stamp(now) + "-" + base;
    for (int n = 2; fs::exists(q / name, ec) && n < 100; n++) name = stamp(now) + "-" + base + "-" + std::to_string(n);
    std::string why;
    if (!copy_tree(folder, q / name, nullptr, &why)) {
        remove_tree(q / name);
        if (error) *error = why;
        return false;
    }
    if (made) *made = name;
    auto list = list_quarantine(backups);
    for (size_t i = (size_t)std::max(keep, 1); i < list.size(); i++) {
        // only ever a folder of the quarantine name shape directly inside save-backups/quarantine
        if (!is_quarantine_name(pcui::path_utf8(list[i].path.filename())) || list[i].path.parent_path() != q) continue;
        if (remove_tree(list[i].path))
            fprintf(stderr, "[SAVES] old quarantine copy %s removed (the newest %d are kept)\n", list[i].name.c_str(), keep);
    }
    return true;
}

std::string describe_time(int64_t when)
{
    if (!when) return "an unknown time";
    std::tm t = local(when), now = local((int64_t)std::time(nullptr));
    char clock[16], date[48];
    strftime(clock, sizeof(clock), "%H:%M", &t);
    if (t.tm_year == now.tm_year && t.tm_yday == now.tm_yday) return std::string("today, ") + clock;
    std::tm y = local((int64_t)std::time(nullptr) - 86400);
    if (t.tm_year == y.tm_year && t.tm_yday == y.tm_yday) return std::string("yesterday, ") + clock;
    strftime(date, sizeof(date), "%b %Y", &t);
    return std::to_string(t.tm_mday) + " " + date + ", " + clock;
}

std::string describe_reason(const std::string &reason)
{
    if (reason == "start") return "Daily, when the game started";
    if (reason == "update") return "Before an update";
    if (reason == "manual") return "Made by you";
    if (reason == "restore") return "Before a restore";
    if (reason == "import") return "Before an import";
    if (reason == "leftover") return "Left by an unfinished restore";
    return "Backup";
}

// ---- zip and inflate ----

uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) {
        c ^= data[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

namespace
{
struct BitReader
{
    const uint8_t *p;
    size_t n, pos = 0;
    uint32_t buf = 0;
    int count = 0;
    bool bad = false;
    int bits(int need)
    {
        while (count < need) {
            if (pos >= n) { bad = true; return 0; }
            buf |= (uint32_t)p[pos++] << count;
            count += 8;
        }
        int v = (int)(buf & ((1u << need) - 1));
        buf >>= need;
        count -= need;
        return v;
    }
};

struct Huffman
{
    uint16_t counts[16];
    uint16_t symbols[320];
    bool build(const uint8_t *lengths, int n)
    {
        uint16_t offs[16];
        memset(counts, 0, sizeof(counts));
        for (int i = 0; i < n; i++) counts[lengths[i]]++;
        counts[0] = 0;
        int left = 1;
        for (int len = 1; len < 16; len++) {
            left <<= 1;
            left -= counts[len];
            if (left < 0) return false;             // over-subscribed
        }
        offs[1] = 0;
        for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + counts[len];
        for (int i = 0; i < n; i++)
            if (lengths[i]) symbols[offs[lengths[i]]++] = (uint16_t)i;
        return true;
    }
    int decode(BitReader &r) const
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            code |= r.bits(1);
            if (r.bad) return -1;
            int count = counts[len];
            if (code - first < count) return symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
                               131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025,
                                1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
                                13, 13};

bool inflate_codes(BitReader &r, const Huffman &lit, const Huffman &dist, Bytes &out, size_t limit)
{
    for (;;) {
        int sym = lit.decode(r);
        if (sym < 0) return false;
        if (sym < 256) {
            if (out.size() >= limit) return false;
            out.push_back((uint8_t)sym);
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = kLenBase[sym] + r.bits(kLenExtra[sym]);
            int d = dist.decode(r);
            if (d < 0 || d >= 30) return false;
            size_t back = kDistBase[d] + r.bits(kDistExtra[d]);
            if (r.bad || back > out.size() || out.size() + len > limit) return false;
            size_t from = out.size() - back;
            for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
        }
    }
}
} // namespace

bool inflate_raw(const uint8_t *src, size_t size, Bytes &out, size_t limit)
{
    BitReader r{src, size};
    out.clear();
    for (;;) {
        int last = r.bits(1), type = r.bits(2);
        if (r.bad) return false;
        if (type == 0) {
            r.buf = 0;
            r.count = 0;                                // to the byte boundary
            if (r.pos + 4 > r.n) return false;
            uint32_t len = rd16(r.p + r.pos), nlen = rd16(r.p + r.pos + 2);
            r.pos += 4;
            if ((len ^ 0xFFFFu) != nlen || r.pos + len > r.n || out.size() + len > limit) return false;
            out.insert(out.end(), r.p + r.pos, r.p + r.pos + len);
            r.pos += len;
        } else if (type == 1) {
            static Huffman lit, dist;
            static bool made;
            if (!made) {
                uint8_t l[288], d[30];
                for (int i = 0; i < 288; i++) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                for (int i = 0; i < 30; i++) d[i] = 5;
                lit.build(l, 288);
                dist.build(d, 30);
                made = true;
            }
            if (!inflate_codes(r, lit, dist, out, limit)) return false;
        } else if (type == 2) {
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            int nlit = r.bits(5) + 257, ndist = r.bits(5) + 1, ncode = r.bits(4) + 4;
            if (r.bad || nlit > 286 || ndist > 30) return false;
            uint8_t lengths[320] = {};
            for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)r.bits(3);
            Huffman code, lit, dist;
            if (r.bad || !code.build(lengths, 19)) return false;
            memset(lengths, 0, sizeof(lengths));
            for (int i = 0; i < nlit + ndist;) {
                int sym = code.decode(r);
                if (sym < 0) return false;
                if (sym < 16) { lengths[i++] = (uint8_t)sym; continue; }
                int repeat, value = 0;
                if (sym == 16) {
                    if (!i) return false;
                    value = lengths[i - 1];
                    repeat = 3 + r.bits(2);
                } else if (sym == 17) {
                    repeat = 3 + r.bits(3);
                } else {
                    repeat = 11 + r.bits(7);
                }
                if (r.bad || i + repeat > nlit + ndist) return false;
                while (repeat--) lengths[i++] = (uint8_t)value;
            }
            if (!lengths[256]) return false;
            if (!lit.build(lengths, nlit) || !dist.build(lengths + nlit, ndist)) return false;
            if (!inflate_codes(r, lit, dist, out, limit)) return false;
        } else {
            return false;
        }
        if (last) return true;
    }
}

namespace
{
// The parts of a member's path, or none when it must not be used (a folder, "..", a drive).
std::vector<std::string> member_parts(const std::string &name)
{
    std::vector<std::string> parts;
    std::string part;
    for (char c : name + "/") {
        if (c == '/' || c == '\\') {
            if (!part.empty()) parts.push_back(part);
            part.clear();
        } else {
            part.push_back(c);
        }
    }
    for (auto &p : parts)
        if (p == "." || p == ".." || p.find(':') != std::string::npos) return {};
    return parts;
}
} // namespace

bool read_zip(const fs::path &zip, Groups &groups, std::string *error)
{
    Bytes file;
    if (!read_file(zip, file, 256u << 20)) {
        if (error) *error = "the zip could not be read (or it is larger than 256 MB)";
        return false;
    }
    size_t n = file.size();
    long long eocd = -1;
    for (long long i = (long long)n - 22; i >= 0 && i >= (long long)n - 22 - 65535; i--)
        if (rd32(&file[(size_t)i]) == 0x06054B50u) { eocd = i; break; }
    if (eocd < 0) {
        if (error) *error = "it is not a zip file";
        return false;
    }
    uint32_t entries = rd16(&file[(size_t)eocd + 10]), cd = rd32(&file[(size_t)eocd + 16]);
    if (cd == 0xFFFFFFFFu || entries == 0xFFFFu) {
        if (error) *error = "this kind of zip (ZIP64) is not supported; make an ordinary zip";
        return false;
    }
    if (entries > kMaxFiles) {
        if (error) *error = "the zip has too many files to be a save folder";
        return false;
    }
    size_t at = cd;
    for (uint32_t e = 0; e < entries; e++) {
        if (at + 46 > n || rd32(&file[at]) != 0x02014B50u) {
            if (error) *error = "the zip is damaged";
            return false;
        }
        uint32_t flags = rd16(&file[at + 8]), method = rd16(&file[at + 10]), crc = rd32(&file[at + 16]);
        uint32_t csize = rd32(&file[at + 20]), usize = rd32(&file[at + 24]);
        uint32_t nlen = rd16(&file[at + 28]), xlen = rd16(&file[at + 30]), clen = rd16(&file[at + 32]);
        uint32_t local = rd32(&file[at + 42]);
        if (at + 46 + nlen > n) { if (error) *error = "the zip is damaged"; return false; }
        std::string name((const char *)&file[at + 46], nlen);
        at += 46 + nlen + xlen + clen;
        if (name.empty() || name.back() == '/' || name.back() == '\\') continue;
        auto parts = member_parts(name);
        if (parts.size() < 2 || usize > kMaxMember || csize > kMaxMember + 1024 || (flags & 1)) continue;
        if (method != 0 && method != 8) continue;
        if ((size_t)local + 30 > n || rd32(&file[local]) != 0x04034B50u) continue;
        size_t data = (size_t)local + 30 + rd16(&file[local + 26]) + rd16(&file[local + 28]);
        if (data + csize > n) continue;
        Bytes out;
        if (method == 0) {
            if (csize != usize) continue;
            out.assign(file.begin() + data, file.begin() + data + csize);
        } else if (!inflate_raw(&file[data], csize, out, kMaxMember) || out.size() != usize) {
            continue;
        }
        if (crc32(out.data(), out.size()) != crc) continue;
        groups[parts[parts.size() - 2]][parts.back()] = std::move(out);
    }
    return true;
}

bool read_folder(const fs::path &folder, Groups &groups, std::string *error)
{
    // First only names and sizes; then the bytes of the folders that can matter.
    std::map<std::string, std::map<std::string, fs::path>> found;
    size_t files = 0;
    std::vector<std::pair<fs::path, int>> todo = {{folder, 0}};
    while (!todo.empty()) {
        auto [dir, depth] = todo.back();
        todo.pop_back();
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            auto type = it->symlink_status(ec).type();
            if (type == fs::file_type::directory) {
                if (depth < 6) todo.push_back({it->path(), depth + 1});
            } else if (type == fs::file_type::regular) {
                if (++files > kMaxFiles) {
                    if (error) *error = "that folder has too many files; pick the folder that holds the profile folders";
                    return false;
                }
                found[pcui::path_utf8(dir.filename())][pcui::path_utf8(it->path().filename())] = it->path();
            }
        }
    }
    for (auto &[name, list] : found) {
        bool meta = false;
        for (auto &f : list) meta = meta || lower(f.first) == "savemeta.xbx";
        if (!meta && name != kTitleId) continue;
        for (auto &[file, path] : list) {
            Bytes bytes;
            if (read_file(path, bytes, kMaxMember)) groups[name][file] = std::move(bytes);
        }
    }
    return true;
}

// ---- PS2 profiles (hK's converter, ported) ----

namespace
{
constexpr size_t kPs2Header = 28;           // "20CM", three sizes, the PS2's own 12-byte check
constexpr size_t kPs2Payload = kPs2Header + 64;
constexpr size_t kPortraitStart = 384;      // payload offsets: the portrait, PS2 and Xbox
constexpr size_t kPs2PortraitEnd = 19852;
constexpr size_t kXboxPortraitEnd = 19844;
constexpr size_t kMaxPs2File = 16u << 20;
const char kIdentity[] = "DJFFNY::User 17 -";
const char kCardMagic[] = "Sony PS2 Memory Card Format";
const char kMaxMagic[] = "Ps2PowerSave";

bool starts_with(const Bytes &b, const char *text)
{
    size_t n = strlen(text);
    return b.size() >= n && !memcmp(b.data(), text, n);
}

void put32(Bytes &b, uint32_t v)
{
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
}

// hK's header_is: a 20CM file of `size` bytes whose identity block starts at `header`.
bool header_is(const Bytes &b, size_t size, size_t header)
{
    return b.size() == size && !memcmp(b.data(), "20CM", 4) && rd32(&b[4]) == size && rd32(&b[8]) == 64 &&
           rd32(&b[12]) == size - header - 64 && !memcmp(&b[header], kIdentity, sizeof(kIdentity) - 1);
}

std::string cstr(const uint8_t *p, size_t n)
{
    size_t len = 0;
    while (len < n && p[len]) len++;
    std::string s((const char *)p, len);
    for (auto &c : s)
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) c = '?';
    return s;
}
} // namespace

bool is_ps2_profile(const Bytes &blob)
{
    return header_is(blob, kPs2ProfileSize, kPs2Header);
}

bool read_psu(const Bytes &d, std::vector<std::pair<std::string, Bytes>> &files, std::string *error)
{
    auto fail = [&](const char *why) { if (error) *error = why; return false; };
    // Each entry is a 512-byte header: mode (0x20 a folder, 0x10 a file), its size (a folder: how
    // many entries), its name at +64. A file's bytes follow its header, padded to 1 KiB.
    struct E { uint32_t mode, size; std::string name; };
    auto entry = [&](size_t at, E &e) {
        if (at + 512 > d.size()) return false;
        e.mode = rd16(&d[at]);
        e.size = rd32(&d[at + 4]);
        e.name = cstr(&d[at + 64], 32);
        return true;
    };
    E dir, e;
    if (!entry(0, dir) || (dir.mode & 0x30) != 0x20 || dir.size < 3 || dir.size > 34)
        return fail("it is not a .psu save export");
    if (!entry(512, e) || (e.mode & 0x30) != 0x20 || e.name != "." || !entry(1024, e) || (e.mode & 0x30) != 0x20 || e.name != "..")
        return fail("the .psu file is damaged (its folder entries)");
    size_t at = 1536;
    for (uint32_t i = 0; i + 2 < dir.size; i++) {
        if (!entry(at, e)) return fail("the .psu file is cut short");
        at += 512;
        if ((e.mode & 0x30) != 0x10 || e.size > d.size() - at) return fail("the .psu file is damaged (a file entry)");
        files.push_back({e.name, Bytes(d.begin() + at, d.begin() + at + e.size)});
        at += ((size_t)e.size + 1023) / 1024 * 1024;
    }
    if (at != d.size()) return fail("the .psu file has the wrong size for what it lists");
    return true;
}

bool read_ps2(const Bytes &file, std::vector<Ps2Profile> &out, std::string *error)
{
    auto fail = [&](const std::string &why) { if (error) *error = why; return false; };
    if (starts_with(file, kCardMagic))
        return fail("a whole PS2 memory card (.ps2) cannot be imported yet. Export the Def Jam save from the card "
                    "as a .psu file (for example with mymcplus, or uLaunchELF on a PS2) and import that file");
    if (starts_with(file, kMaxMagic))
        return fail("MAX Drive saves (.max) cannot be imported yet. Turn it into a .psu file (for example with "
                    "mymcplus) and import that file");
    std::vector<std::pair<std::string, Bytes>> files;
    if (file.size() == kPs2ProfileSize && is_ps2_profile(file)) {
        files.push_back({"", file});
    } else {
        std::string why;
        if (!read_psu(file, files, &why)) return fail(why);
    }
    for (auto &[name, blob] : files) {
        if (!is_ps2_profile(blob)) continue;
        Ps2Profile p;
        p.file = name;
        if (!name.compare(0, 12, "BASLUS-21004")) p.region = "NTSC-U";
        else if (!name.compare(0, 12, "BESLES-52507")) p.region = "PAL";
        p.name = cstr(&blob[kPs2Payload], 20);
        p.data = blob;
        out.push_back(std::move(p));
    }
    return true;
}

std::string ps2_name_fault(const std::string &name)
{
    if (name.empty() || name.size() > 20) return "the fighter's name must be 1 to 20 characters";
    auto alnum = [](char c) { return isalnum((unsigned char)c) != 0 && (unsigned char)c < 0x80; };
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        bool ok = alnum(c) || (i > 0 && (c == '_' || c == '-' || (c == ' ' && i + 1 < name.size())));
        if (!ok) return "the fighter's name has characters a profile here cannot use";
    }
    std::string up;
    for (char c : name) up.push_back((char)toupper((unsigned char)c));
    if (up == "CON" || up == "PRN" || up == "AUX" || up == "NUL" ||
        (up.size() == 4 && (!up.compare(0, 3, "COM") || !up.compare(0, 3, "LPT")) && isdigit((unsigned char)up[3])))
        return "Windows keeps the fighter's name for itself";
    return "";
}

Bytes ps2_to_xbox(const Bytes &ps2)
{
    if (!is_ps2_profile(ps2)) return {};
    const uint8_t *payload = &ps2[kPs2Payload];
    Bytes out;
    out.reserve(kProfileFileSize);
    out.insert(out.end(), {'2', '0', 'C', 'M'});
    put32(out, (uint32_t)kProfileFileSize);
    put32(out, 0x40);
    put32(out, (uint32_t)(kProfileFileSize - kIdentityEnd));
    out.resize(kHeader);                                // the three signatures, made below
    // The identity block as hK's Xbox reference has it (the Xbox import keeps another console's
    // identity as it is, so the game does not tie a profile to it).
    const char identity[] = "DJFFNY::User 17 -2030886228";
    out.insert(out.end(), identity, identity + sizeof(identity) - 1);
    out.resize(kIdentityEnd);
    out.insert(out.end(), payload, payload + kPortraitStart);
    // hK's neutral preview: two bytes, 4863 grey BGRA texels (the reference's last at 19834), then six
    // bytes from his reference, the first the created-fighter flag (profile +0x4D8A = 1).
    out.push_back(0);
    out.push_back(0);
    for (int i = 0; i < 4863; i++) out.insert(out.end(), {48, 48, 48, 255});
    out.insert(out.end(), {0x01, 0x89, 0x00, 0x81, 0xA0, 0x08});
    out.insert(out.end(), payload + kPs2PortraitEnd, ps2.data() + ps2.size());
    if (out.size() != kProfileFileSize || out.size() != kIdentityEnd + kXboxPortraitEnd + (kPs2ProfileSize - kPs2Payload - kPs2PortraitEnd))
        return {};
    out = resign(out, kPortKey);
    return header_fault(out).empty() && header_is(out, kProfileFileSize, kHeader) ? out : Bytes();
}

Bytes neutral_save_image()
{
    // hK's icon: XPR0, a 4 KiB file with a 2 KiB header, a 64x64 DXT1 texture of one grey.
    Bytes out;
    for (uint32_t v : {0x30525058u, 4096u, 2048u, 0x40001u, 0u, 0u, 0x06610C29u, 0u, 0xFFFFFFFFu}) put32(out, v);
    out.resize(2048, 0xAD);
    for (int i = 0; i < 256; i++) out.insert(out.end(), {0x86, 0x31, 0, 0, 0, 0, 0, 0});
    return out;
}

Bytes save_meta(const std::string &name)
{
    Bytes out = {0xFF, 0xFE};
    for (char c : "Name=" + name + "\r\n") {
        out.push_back((uint8_t)c);
        out.push_back(0);
    }
    return out;
}

// ---- bringing in Xbox profiles ----

ImportScan scan_import(const fs::path &source, const fs::path &save_dir)
{
    ImportScan scan;
    Groups groups;
    std::error_code ec;
    std::string why;
    auto type = fs::status(source, ec).type();
    bool read = false, ps2 = false;
    std::map<std::string, std::string> origin;          // group -> "PS2, PAL"
    std::string ext = lower(pcui::path_utf8(source.extension()));
    if (type == fs::file_type::regular) {
        // A PS2 save (a .psu export, a bare profile file, or a card or MAX file to explain) by its
        // first bytes or its name; anything else is read as a zip.
        Bytes head;
        {
            std::ifstream f(source, std::ios::binary);
            head.resize(32);
            f.read((char *)head.data(), (std::streamsize)head.size());
            head.resize((size_t)std::max<std::streamsize>(f.gcount(), 0));
        }
        uintmax_t size = fs::file_size(source, ec);
        ps2 = ext == ".psu" || ext == ".ps2" || ext == ".max" || starts_with(head, kCardMagic) ||
              starts_with(head, kMaxMagic) || (!ec && size == kPs2ProfileSize && starts_with(head, "20CM")) ||
              (head.size() >= 8 && (rd16(&head[0]) & 0x8030) == 0x8020 && rd32(&head[4]) >= 3 && rd32(&head[4]) <= 34);
        if (ps2) {
            Bytes file;
            std::vector<Ps2Profile> found;
            if (!read_file(source, file, kMaxPs2File)) why = "it could not be read (or it is larger than 16 MB)";
            else read = read_ps2(file, found, &why);
            if (read && found.empty()) {
                scan.error = "No Def Jam Story profile in " + pcui::path_utf8(source.filename()) +
                             ". Export the Story profile save (not the Options save) as a .psu file and pick that.";
                return scan;
            }
            for (auto &p : found) {
                std::string label = p.file.empty() ? pcui::path_utf8(source.filename()) : p.file;
                std::string fault = ps2_name_fault(p.name);
                Bytes xbox = fault.empty() ? ps2_to_xbox(p.data) : Bytes();
                if (fault.empty() && xbox.empty()) fault = "it could not be converted";
                if (!fault.empty()) {
                    scan.skipped.push_back(label + (p.name.empty() ? "" : " (" + p.name + ")") + ": " + fault);
                    continue;
                }
                auto &g = groups[label];
                g["SaveMeta.xbx"] = save_meta(p.name);
                g[p.name] = std::move(xbox);
                g["saveimage.xbx"] = neutral_save_image();
                origin[label] = "PS2" + (p.region.empty() ? std::string() : ", " + p.region);
            }
        } else {
            read = read_zip(source, groups, &why);
        }
    } else if (type == fs::file_type::directory) {
        read = read_folder(source, groups, &why);
    } else {
        why = "it is not there";
    }
    if (!read) {
        scan.error = "Could not read " + pcui::path_utf8(source.filename()) + ": " + why + ".";
        return scan;
    }
    fs::path title = save_dir / "UserData" / kTitleId;
    std::set<std::string> folders;
    for (auto &[folder, files] : groups) {
        if (folder == kTitleId) {
            for (const char *t : {"TitleMeta.xbx", "TitleImage.xbx"})
                if (files.count(t)) scan.title[kTitleId][t] = files.at(t);
        }
        const Bytes *meta = nullptr, *image = nullptr;
        for (auto &[file, bytes] : files) {
            if (lower(file) == "savemeta.xbx") meta = &bytes;
            if (lower(file) == "saveimage.xbx") image = &bytes;
        }
        if (!meta) continue;
        std::string name;
        if (!parse_save_meta(*meta, name) || !usable_name(name)) {
            scan.skipped.push_back(folder + ": its SaveMeta.xbx has no profile name this game uses");
            continue;
        }
        if (name == "Options") {
            scan.skipped.push_back(folder + ": the Options file (game settings) is not imported");
            continue;
        }
        if (!files.count(name)) {
            scan.skipped.push_back(folder + " (" + name + "): no profile file named " + name + " beside SaveMeta.xbx");
            continue;
        }
        const Bytes &data = files.at(name);
        std::string fault = header_fault(data);
        if (!fault.empty()) {
            scan.skipped.push_back(folder + " (" + name + "): not a profile file (" + fault + ")");
            continue;
        }
        if (bad_file_name(name)) {
            scan.skipped.push_back(folder + " (" + name + "): the name has characters Windows cannot use in a file name");
            continue;
        }
        Candidate c;
        c.source = folder;
        c.name = name;
        c.folder = folder_name(name);
        if (!folders.insert(c.folder).second) {
            scan.skipped.push_back(folder + " (" + name + "): a second copy of the same profile, left out");
            continue;
        }
        if (origin.count(folder)) c.origin = origin.at(folder);
        c.exists = fs::exists(title / c.folder, ec);
        c.needs_signing = !signatures_ok(data, kPortKey);
        c.meta = *meta;
        c.data = data;
        if (image) { c.image = *image; c.has_image = true; }
        scan.found.push_back(std::move(c));
    }
    if (scan.found.empty() && scan.skipped.empty())
        scan.error = "No Xbox profiles in " + pcui::path_utf8(source.filename()) +
                     ". Pick the folder that holds the profile folders (E:\\UDATA\\45410049 on the Xbox) or a .zip of it.";
    return scan;
}

ImportResult apply_import(const ImportScan &scan, const std::set<std::string> &replace, const fs::path &save_dir,
                          const fs::path &backups, int64_t now)
{
    ImportResult r;
    fs::path title = save_dir / "UserData" / kTitleId;
    std::vector<const Candidate *> writes;
    std::vector<std::string> kept, done, failed;
    for (auto &c : scan.found) {
        if (c.exists && !replace.count(c.name)) kept.push_back(c.name);
        else writes.push_back(&c);
    }
    r.kept = (int)kept.size();
    if (writes.empty()) {
        r.ok = true;
        r.message = kept.empty() ? "Nothing to import." : "Nothing imported: you already have " + join_names(kept) + ".";
        return r;
    }
    std::string why;
    if (!make_backup(save_dir, backups, "import", now, &r.backup, &why) && why != "There are no saves to back up yet.") {
        r.message = "Nothing was imported: your saves could not be backed up first. " + why;
        return r;
    }
    std::error_code ec;
    fs::create_directories(title, ec);
    for (auto *c : writes) {
        fs::path target = title / c->folder, incoming = title / (c->folder + ".importing");
        remove_tree(incoming);
        fs::create_directories(incoming, ec);
        bool ok = !ec && write_file(incoming / "SaveMeta.xbx", c->meta) &&
                  write_file(incoming / fs::u8path(c->name), resign(c->data, kPortKey)) &&
                  (!c->has_image || write_file(incoming / "saveimage.xbx", c->image));
        bool existed = fs::exists(target, ec);
        if (ok && existed) ok = remove_tree(target);      // it is in the backup just made
        if (ok) {
            fs::rename(incoming, target, ec);
            ok = !ec;
        }
        if (!ok) {
            remove_tree(incoming);
            failed.push_back(c->name);
            fprintf(stderr, "[SAVES] import of %s failed\n", c->name.c_str());
            continue;
        }
        std::string converted = c->origin.empty() ? "" : " (converted from " + c->origin + ")";
        fprintf(stderr, "[SAVES] imported %s from %s into %s%s%s%s\n", c->name.c_str(), c->source.c_str(),
                c->folder.c_str(), c->needs_signing ? " (signed again with the port's key)" : "", converted.c_str(),
                existed ? ", replacing the one there" : "");
        done.push_back(c->name);
        r.imported++;
        if (existed) r.replaced++;
    }
    if (r.imported)
        for (auto &[file, bytes] : scan.title.count(kTitleId) ? scan.title.at(kTitleId) : std::map<std::string, Bytes>())
            if (!fs::exists(title / file, ec)) write_file(title / file, bytes);
    std::string text;
    if (r.imported) {
        text = "Imported " + plural(r.imported, "profile", "profiles");
        if (r.replaced) text += " (" + std::to_string(r.replaced) + " replaced)";
        text += ": " + join_names(done) + ".";
    }
    if (!kept.empty()) text += std::string(text.empty() ? "" : " ") + "Kept " + plural((int)kept.size(), "profile", "profiles") +
                               " you already had: " + join_names(kept) + ".";
    if (!failed.empty()) text += std::string(text.empty() ? "" : " ") + "Could not write " + join_names(failed) + ".";
    if (!scan.skipped.empty())
        text += " Skipped " + plural((int)scan.skipped.size(), "folder that is not a profile", "folders that are not profiles") + ".";
    r.ok = failed.empty();
    r.message = text;
    return r;
}
} // namespace saves
