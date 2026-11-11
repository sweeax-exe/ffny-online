// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// "Unlock everything" for a profile: see save_unlock.h. Standard C++17 only.
#define _CRT_SECURE_NO_WARNINGS
#include "save_unlock.h"
#include "pc_ui_model.h"         // pcui::path_utf8

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace saves
{
const char *const kUnlockItemNames[6] = {"hair styles", "tattoos", "jewellery", "clothing", "Blazin moves",
                                         "fighting styles"};

namespace
{
// ---- the database (assets/djv2.sod, "SODB" version 2; scripts/sod-characters.py, studio/ffnystudio/sod.py) ----
// Table and field names are hashes: h = 0x2B5A5; h = (h + byte) * 33.
constexpr uint32_t kHairStyle = 0x7450591Au, kTattoo = 0x30948EA0u, kJewelry = 0x426B8667u, kClothing = 0x912016DDu,
                   kBlazin = 0x47D305FDu, kFightingStyle = 0x906455C6u, kCharacter = 0x4CE5B1B2u, kVenue = 0xD7DCA628u;
constexpr uint32_t kClothingColours = 0xAC3E66A4u;  // a clothing row: one item per colour (sub_000151C0)
constexpr uint32_t kJewelryVariants = 0xC2E12E98u;  // a jewellery row: one per entry, or one when empty (sub_000153D0)
constexpr uint32_t kClothingType = 0x0E334DC7u;     // "Type": a clothing row's shop section, 0..15

// ---- the profile file (a user ID): profile offset p is at file 0x80 + p ----
constexpr size_t kProfileFile = 0x5990, kProfileAt = 0x80;
constexpr size_t kItemStates = 0x4DC4;      // sub_000148B0: the six state arrays, one after another
constexpr size_t kCash = 0x20;              // GetPlayerCash (sub_0004CA30)
constexpr size_t kDevPoints = 0x28;         // GetPlayerDevPts (sub_0004CA70); the gym spends them (sub_0004EB60)
constexpr size_t kBaseStats = 0xFC;         // six dwords; the gym trains the first five
// Unlocked-row counters (sub_00015B80 raises them per delivered item): word per kind, in profile
// order of kinds hair, tattoo, jewellery, clothing, Blazin (styles have none); dwords per clothing type.
constexpr size_t kRowCounter[6] = {0xA8, 0xAA, 0xAE, 0xAC, 0xB0, 0};
constexpr size_t kClothingTypeCounters = 0xB8;
constexpr size_t kProfileEnd = 0x5910;      // the file holds the profile up to here

// ---- the Options file (the global record): record offset g is at file g + 0x3C ----
constexpr size_t kOptionsFile = 0x151C, kOptionsAt = 0x3C;
constexpr size_t kVenueCount = 0x1264, kCharCount = 0x1268, kVenueIds = 0x126C, kCharIds = 0x12E4;
constexpr uint32_t kVenueCap = 30, kCharCap = 80;      // sub_000144B0
constexpr size_t kSongStates = 0x1424, kSongIds = 0x1448, kSongNext = 0x14D8;
constexpr uint32_t kSongCap = 36;
constexpr uint32_t kNewFlag = 0x00800000u;  // a set element's own flag bit, ignored by its order (sub_00017000)

uint32_t rd(const Bytes &b, size_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
void wr(Bytes &b, size_t o, uint32_t v)
{
    b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24);
}

struct Sod
{
    const Bytes &b;
    bool ok(size_t o, size_t n = 4) const { return o <= b.size() && n <= b.size() - o; }
    bool u(size_t o, uint32_t &v) const { if (!ok(o)) return false; v = rd(b, o); return true; }
    // The first class block of a table: its row count and row pointers.
    bool rows(uint32_t table_hash, uint32_t &index, std::vector<uint32_t> &out) const
    {
        uint32_t data, n, stride;
        if (!u(16, data) || !u(data + 4, n) || !u(data + 8, stride) || stride != 1 || !ok(data + 12, 8ull * n)) return false;
        for (index = 0; index < n && rd(b, data + 12 + 4 * index) != table_hash; index++) {}
        if (index == n) return false;
        uint32_t table = rd(b, data + 12 + 4 * n + 4 * index), classes, block, count;
        if (!u(table + 0x10, classes) || classes == 0 || classes > 64 || !u(table + 0x18 + 4 * classes, block)) return false;
        if (!u(block + 4, count) || !u(block + 8, stride) || !stride || count > 100000 ||
            !ok(block + 12 + 4ull * count, 4ull * count * stride))
            return false;
        out.clear();
        for (uint32_t i = 0; i < count; i++) out.push_back(rd(b, block + 12 + 4 * count + 4 * i * stride));
        return true;
    }
    // A row's word for a field (the schema maps table hash ^ field hash to the word's index).
    bool field(uint32_t table_hash, uint32_t field_hash, uint32_t &word) const
    {
        uint32_t schema, n;
        if (!u(12, schema) || !u(schema + 4, n) || !ok(schema + 12, 12ull * n)) return false;
        for (uint32_t i = 0; i < n; i++)
            if (rd(b, schema + 12 + 4 * i) == (table_hash ^ field_hash)) {
                word = rd(b, schema + 12 + 4 * n + 8 * i);
                return word < 64;
            }
        return false;
    }
    // The element count of a row's list field.
    bool list_size(uint32_t row, uint32_t word, uint32_t &n) const
    {
        uint32_t list;
        return u(row + 4 * word, list) && u(list, n) && n < 4096;
    }
};

bool read_all(const fs::path &p, Bytes &out, size_t expect)
{
    std::error_code ec;
    if (fs::file_size(p, ec) != expect || ec) return false;
    std::ifstream f(p, std::ios::binary);
    out.resize(expect);
    return f && f.read((char *)out.data(), (std::streamsize)expect);
}

// Written beside the file, then renamed over it, so the game's file is never half written.
bool write_over(const fs::path &p, const Bytes &data, std::string *why)
{
    fs::path tmp = p;
    tmp += ".unlocking";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write((const char *)data.data(), (std::streamsize)data.size())) {
            if (why) *why = "it could not be written";
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (why) *why = "it is in use";
        return false;
    }
    return true;
}

bool identity_is(const Bytes &b, const char *prefix)
{
    size_t n = strlen(prefix);
    return b.size() > 0x4C + n && !memcmp(&b[0x4C], prefix, n);
}

// A sorted id set (count at `count_at`, `cap` slots at `ids_at`, unused slots 0xFFFFFFFF) given every
// row of `table`, as the game's own unlock-everything routine (sub_000158F0) inserts them.
bool fill_set(Bytes &b, size_t count_at, size_t ids_at, uint32_t cap, uint32_t table, uint32_t rows, int &added,
              std::string *error, const char *what)
{
    uint32_t count = rd(b, count_at);
    if (count > cap) { if (error) *error = std::string("its ") + what + " list is damaged"; return false; }
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t id = rd(b, ids_at + 4 * i);
        if (id >> 24 != table) { if (error) *error = std::string("its ") + what + " list is not one this game writes"; return false; }
        ids.push_back(id);
    }
    auto has = [&](uint32_t id) {
        for (uint32_t x : ids) if ((x & ~kNewFlag) == id) return true;
        return false;
    };
    std::vector<uint32_t> add;
    for (uint32_t r = 0; r < rows; r++)
        if (!has(table << 24 | r)) add.push_back(table << 24 | r);
    if (ids.size() + add.size() > cap) {
        if (error) *error = std::string("the game holds at most ") + std::to_string(cap) + " " + what + ", the data has more";
        return false;
    }
    ids.insert(ids.end(), add.begin(), add.end());
    std::stable_sort(ids.begin(), ids.end(), [](uint32_t a, uint32_t c) { return (a & ~kNewFlag) < (c & ~kNewFlag); });
    wr(b, count_at, (uint32_t)ids.size());
    for (uint32_t i = 0; i < cap; i++) wr(b, ids_at + 4 * i, i < ids.size() ? ids[i] : 0xFFFFFFFFu);
    added = (int)add.size();
    return true;
}

std::string plural(int n, const char *one, const char *many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string thousands(uint32_t n)       // 272625 -> "272,625"
{
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return s;
}
} // namespace

bool unlock_layout(const Bytes &sod, UnlockLayout &out, std::string *error)
{
    auto fail = [&](const char *why) { if (error) *error = why; return false; };
    if (sod.size() < 24 || memcmp(sod.data(), "SODB", 4) || rd(sod, 4) != 2) return fail("the game's database is not there");
    Sod s{sod};
    static const uint32_t order[6] = {kHairStyle, kTattoo, kJewelry, kClothing, kBlazin, kFightingStyle};
    std::vector<uint32_t> rows;
    uint32_t index;
    for (int k = 0; k < 6; k++) {
        if (!s.rows(order[k], index, rows)) return fail("the game's database has no item table it needs");
        uint32_t n = (uint32_t)rows.size();
        out.rows[k] = n;
        if (order[k] == kClothing || order[k] == kJewelry) {
            uint32_t word, len, type_word = 0, type;
            if (!s.field(order[k], order[k] == kClothing ? kClothingColours : kJewelryVariants, word) ||
                (order[k] == kClothing && !s.field(kClothing, kClothingType, type_word)))
                return fail("the game's database has no item list it needs");
            n = 0;
            out.rows[k] = 0;
            for (uint32_t row : rows) {
                if (!s.list_size(row, word, len)) return fail("the game's database is damaged");
                if (order[k] == kJewelry) len = std::max(len, 1u);
                n += len;
                if (!len) continue;         // a clothing row without colours has no item
                out.rows[k]++;
                if (order[k] == kClothing) {
                    if (!s.u(row + 4 * type_word, type) || type >= 16) return fail("the game's database is damaged");
                    out.clothing_type_rows[type]++;
                }
            }
        }
        out.items[k] = n;
    }
    if (!s.rows(kCharacter, out.character_table, rows)) return fail("the game's database has no fighters");
    out.characters = (uint32_t)rows.size();
    if (!s.rows(kVenue, out.venue_table, rows)) return fail("the game's database has no venues");
    out.venues = (uint32_t)rows.size();
    size_t end = kItemStates;
    for (uint32_t n : out.items) end += n / 4 + 1;
    if (end > kProfileEnd) return fail("the game's database has more items than a profile holds");
    return true;
}

uint32_t training_cost(const uint32_t stats[5])
{
    uint32_t cost = 0;
    for (int i = 0; i < 5; i++)
        for (uint32_t v = stats[i]; v < kGymStatCap; v++) cost += kGymCost[v / 5];
    return cost;
}

bool unlock_profile_bytes(Bytes &b, const UnlockLayout &layout, bool cash, UnlockCounts &counts, std::string *error)
{
    if (b.size() != kProfileFile || !header_fault(b).empty() || !identity_is(b, "DJFFNY::User")) {
        if (error) *error = "it is not a profile file of this game";
        return false;
    }
    size_t at = kProfileAt + kItemStates;
    for (int k = 0; k < 6; k++) {
        uint32_t n = layout.items[k];
        counts.items[k] = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t &byte = b[at + i / 4];
            int shift = (int)(i % 4) * 2;
            if (!((byte >> shift) & 3)) counts.items[k]++;
            byte |= (uint8_t)(1u << shift);        // locked -> unlocked; an owned item stays owned
        }
        at += n / 4 + 1;
    }
    // The shops' counts of unlocked rows: every row now (never lowered).
    for (int k = 0; k < 6; k++) {
        if (!kRowCounter[k]) continue;
        size_t o = kProfileAt + kRowCounter[k];
        uint32_t have = b[o] | b[o + 1] << 8, want = std::min<uint32_t>(layout.rows[k], 0xFFFF);
        if (have < want) { b[o] = (uint8_t)want; b[o + 1] = (uint8_t)(want >> 8); }
    }
    for (int t = 0; t < 16; t++) {
        size_t o = kProfileAt + kClothingTypeCounters + 4 * t;
        if (rd(b, o) < layout.clothing_type_rows[t]) wr(b, o, layout.clothing_type_rows[t]);
    }
    counts.cash = false;
    if (cash && rd(b, kProfileAt + kCash) < kUnlockCash) {
        wr(b, kProfileAt + kCash, kUnlockCash);
        counts.cash = true;
    }
    // Development points: enough to train the five stats from where they are to the gym's cap, no more.
    counts.dev_points = 0;
    if (cash) {
        uint32_t stats[5];
        for (int i = 0; i < 5; i++) stats[i] = rd(b, kProfileAt + kBaseStats + 4 * i);
        uint32_t want = training_cost(stats);
        if (rd(b, kProfileAt + kDevPoints) < want) {
            wr(b, kProfileAt + kDevPoints, want);
            counts.dev_points = want;
        }
    }
    return true;
}

bool unlock_options_bytes(Bytes &b, const UnlockLayout &layout, UnlockCounts &counts, std::string *error)
{
    if (b.size() != kOptionsFile || !header_fault(b).empty() || !identity_is(b, "DJFFNY::Global")) {
        if (error) *error = "the Options file is not one of this game";
        return false;
    }
    Bytes work = b;
    if (!fill_set(work, kOptionsAt + kCharCount, kOptionsAt + kCharIds, kCharCap, layout.character_table,
                  layout.characters, counts.fighters, error, "fighters") ||
        !fill_set(work, kOptionsAt + kVenueCount, kOptionsAt + kVenueIds, kVenueCap, layout.venue_table, layout.venues,
                  counts.venues, error, "venues"))
        return false;
    // Songs (sub_00016C30 with state 1): a locked song (state byte 0) gets 1 | its unlock order << 2.
    uint32_t songs = 0, next = rd(work, kOptionsAt + kSongNext);
    while (songs < kSongCap && rd(work, kOptionsAt + kSongIds + 4 * songs) != 0xFFFFFFFFu) songs++;
    counts.songs = 0;
    for (uint32_t i = 0; i < songs; i++) {
        uint8_t &state = work[kOptionsAt + kSongStates + i];
        if (state) continue;
        state = (uint8_t)(1 | (uint8_t)(next << 2));
        next++;
        counts.songs++;
    }
    wr(work, kOptionsAt + kSongNext, next);
    b.swap(work);
    return true;
}

UnlockResult unlock_everything(const fs::path &save_dir, const fs::path &backups, const std::string &folder,
                               const Bytes &sod, bool cash, int64_t now)
{
    UnlockResult r;
    auto refuse = [&](const std::string &why) { r.message = "Nothing was changed: " + why + "."; return r; };
    UnlockLayout layout;
    std::string why;
    if (!unlock_layout(sod, layout, &why)) return refuse(why);
    fs::path title = save_dir / "UserData" / kTitleId;
    Entry profile, options;
    for (auto &e : list_saves(save_dir)) {
        if (e.kind == Entry::PROFILE && e.folder == folder) profile = e;
        if (e.kind == Entry::OPTIONS) options = e;
    }
    if (profile.folder.empty()) return refuse("that profile is not in the game's save folder");
    if (options.folder.empty()) return refuse("the game's Options file is not there yet (start the game once)");
    fs::path pfile = title / profile.folder / fs::u8path(profile.name), ofile = title / options.folder / "Options";
    Bytes pb, ob;
    if (!read_all(pfile, pb, kProfileFile)) return refuse("the profile file could not be read");
    if (!read_all(ofile, ob, kOptionsFile)) return refuse("the Options file could not be read");
    if (!signatures_ok(pb, kPortKey)) return refuse("the profile is signed by another Xbox (bring it in with Import saves first)");
    if (!unlock_profile_bytes(pb, layout, cash, r.counts, &why) || !unlock_options_bytes(ob, layout, r.counts, &why))
        return refuse(why);
    if (!make_backup(save_dir, backups, "unlock", now, &r.backup, &why)) return refuse("no backup could be made (" + why + ")");
    pb = resign(pb, kPortKey);
    ob = resign(ob, kPortKey);
    if (!write_over(ofile, ob, &why) || !write_over(pfile, pb, &why)) {
        std::string undo;
        bool back = restore(save_dir, backups, r.backup, now, &undo);
        r.message = "The save could not be written (" + why + "). " +
                    (back ? std::string("Your saves were put back as they were.") : "Restore the newest backup to undo.");
        return r;
    }
    int items = 0;
    for (int n : r.counts.items) items += n;
    r.ok = true;
    r.message = profile.name + ": everything is unlocked (" + plural(items, "item", "items") + ", " +
                plural(r.counts.fighters, "fighter", "fighters") + ", " + plural(r.counts.venues, "venue", "venues") +
                ", " + plural(r.counts.songs, "song", "songs") + (r.counts.cash ? ", $999,999" : "") +
                (r.counts.dev_points ? ", " + thousands(r.counts.dev_points) + " development points" : "") + " new).";
    fprintf(stderr, "[SAVES] unlock %s: items %d/%d/%d/%d/%d/%d, fighters %d, venues %d, songs %d, cash %d, dev points %u, backup %s\n",
            profile.folder.c_str(), r.counts.items[0], r.counts.items[1], r.counts.items[2], r.counts.items[3],
            r.counts.items[4], r.counts.items[5], r.counts.fighters, r.counts.venues, r.counts.songs, (int)r.counts.cash,
            r.counts.dev_points, r.backup.c_str());
    fflush(stderr);
    return r;
}
} // namespace saves
