// The launcher's Saves page underneath: listing the profiles, backups of the save area, and
// bringing in Xbox profiles (scripts/save-import.py, ported). No UI, no game state and no
// platform calls beyond std::filesystem, so native unit tests build it on its own
// (tests/unit/test_save_manager.py). The page itself is pc_launcher_saves.inc; the glue that
// knows the data folder and whether the game runs is pc_launcher_saves.cpp.
//
// Layout: <data>/save/UserData/45410049/<profile folder>/ holds SaveMeta.xbx (UTF-16 "Name=..."),
// the profile file named like the profile (0x5990 bytes, "20CM" header with three HMAC-SHA1
// fields, docs/importing-xbox-saves.md) and saveimage.xbx; the Options file is a folder of the
// same shape named "Options". Backups are <data>/save-backups/<yyyymmdd-hhmmss>/UserData plus a
// backup.txt; only folders with that name shape are ever removed, and only inside save-backups.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace saves
{
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

constexpr const char *kTitleId = "45410049";
constexpr size_t kProfileFileSize = 0x5990;
constexpr int kKeepBackups = 10;
extern const uint8_t kPortKey[16];      // XboxSignatureKey as the port's kernel exports it: zeros

// ---- signing (the port of save-import.py's header_fault / signatures_ok / resign) ----
void hmac_sha1(const uint8_t *key, size_t key_size, const uint8_t *data, size_t size, uint8_t out[20]);
std::string header_fault(const Bytes &blob);                       // "" when it is a profile file
bool signatures_ok(const Bytes &blob, const uint8_t key[16]);       // all three fields verify
Bytes resign(const Bytes &blob, const uint8_t key[16]);
std::string folder_name(const std::string &name);                   // the game's folder for a name
bool parse_save_meta(const Bytes &raw, std::string &name);          // Name= of a SaveMeta.xbx
bool usable_name(const std::string &name);                          // 1..32 printable ASCII

// ---- what is in the save area ----
struct Entry
{
    enum Kind { PROFILE, OPTIONS, OTHER };
    Kind kind = OTHER;
    std::string folder;         // under UserData/45410049
    std::string name;           // from SaveMeta.xbx ("" when it has none)
    int64_t changed = 0;        // the main file's last change, Unix seconds (0 unknown)
    bool signed_ok = false;     // a profile: its signatures verify with the port's key
    bool created = false;       // a profile with a created fighter (profile +0x4D8A == 1)
};
std::vector<Entry> list_saves(const fs::path &save_dir);

// ---- backups ----
struct Backup
{
    std::string name;           // the folder's name, yyyymmdd-hhmmss[-n]
    fs::path path;
    int64_t when = 0;           // Unix seconds
    std::string reason;         // start, update, manual, restore, import
    int profiles = 0;
};
bool is_backup_name(const std::string &name);
std::vector<Backup> list_backups(const fs::path &backups);          // newest first
// Copies save_dir/UserData into a new backup. False with *error when there is nothing to copy or
// a copy fails (a half-made backup is removed). Then keeps the newest `keep`, never `protect`.
bool make_backup(const fs::path &save_dir, const fs::path &backups, const std::string &reason, int64_t now,
                 std::string *made, std::string *error, const std::string &protect = "", int keep = kKeepBackups);
// Removes the oldest backups past `keep` (never `protect`); returns how many went.
int rotate(const fs::path &backups, int keep, const std::string &protect = "");
// No backup was made on now's local calendar day.
bool daily_due(const fs::path &backups, int64_t now);
// Backs up the current saves (reason "restore"), then puts the backup's UserData in place.
bool restore(const fs::path &save_dir, const fs::path &backups, const std::string &name, int64_t now,
             std::string *message);
std::string describe_time(int64_t when);                            // "3 Nov 2026, 14:05"
std::string describe_reason(const std::string &reason);

// ---- bringing in Xbox profiles ----
using Groups = std::map<std::string, std::map<std::string, Bytes>>; // folder -> file -> bytes
bool read_zip(const fs::path &zip, Groups &groups, std::string *error);
bool read_folder(const fs::path &folder, Groups &groups, std::string *error);
bool inflate_raw(const uint8_t *src, size_t size, Bytes &out, size_t limit);
uint32_t crc32(const uint8_t *data, size_t size);

struct Candidate
{
    std::string source;         // the folder it came from
    std::string name;           // the profile name
    std::string folder;         // where it goes (folder_name(name))
    bool exists = false;        // that folder is in the save area already
    bool needs_signing = false;
    Bytes meta, data, image;
    bool has_image = false;
};
struct ImportScan
{
    std::vector<Candidate> found;
    std::vector<std::string> skipped;   // one plain line each
    Groups title;                       // the 45410049 folder's own files, if the source had them
    std::string error;                  // nothing could be read at all
};
ImportScan scan_import(const fs::path &source, const fs::path &save_dir);
struct ImportResult
{
    bool ok = false;
    int imported = 0, replaced = 0, kept = 0;
    std::string backup;                 // the backup made first ("" none: the save area was empty)
    std::string message;                // for the player
};
// Writes the candidates; an existing profile is replaced only when its name is in `replace`.
ImportResult apply_import(const ImportScan &scan, const std::set<std::string> &replace, const fs::path &save_dir,
                          const fs::path &backups, int64_t now);
} // namespace saves
