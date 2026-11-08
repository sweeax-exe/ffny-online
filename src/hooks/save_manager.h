// The launcher's Saves page underneath: listing the profiles, backups of the save area, and
// bringing in Xbox profiles (scripts/save-import.py, ported) and PS2 ones (converted, hK's
// converter ported). No UI, no game state and no
// platform calls beyond std::filesystem, so native unit tests build it on its own
// (tests/unit/test_save_manager.py). The page itself is pc_launcher_saves.inc; the glue that
// knows the data folder and whether the game runs is pc_launcher_saves.cpp.
//
// Layout: <data>/save/UserData/45410049/<profile folder>/ holds SaveMeta.xbx (UTF-16 "Name=..."),
// the profile file named like the profile (0x5990 bytes, "20CM" header with three HMAC-SHA1
// fields, docs/importing-xbox-saves.md) and saveimage.xbx; the Options file is a folder of the
// same shape named "Options". Backups are <data>/save-backups/<yyyymmdd-hhmmss>/UserData plus a
// backup.txt; only folders with that name shape are ever removed, and only inside save-backups.
// A profile folder the game itself deletes (XDeleteSaveGame) is copied first to
// <data>/save-backups/quarantine/<yyyymmdd-hhmmss>-<folder>/ (the folder's own files); the newest 30
// are kept, and only folders of that name shape inside quarantine are ever removed.
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
// a copy fails (a half-made backup is removed), and for a daily ("start") or "update" backup with no
// profile in it while the newest backup has some. Then keeps the newest `keep`, never `protect`.
bool make_backup(const fs::path &save_dir, const fs::path &backups, const std::string &reason, int64_t now,
                 std::string *made, std::string *error, const std::string &protect = "", int keep = kKeepBackups);
// Removes the oldest backups past `keep` (never `protect`, and never the newest backup that holds a
// profile); returns how many went.
int rotate(const fs::path &backups, int keep, const std::string &protect = "");
// Profiles in save_dir/UserData/45410049: folders holding a profile file (Entry::PROFILE).
int count_profiles(const fs::path &save_dir);
// No backup was made on now's local calendar day.
bool daily_due(const fs::path &backups, int64_t now);
// Backs up the current saves (reason "restore"), then puts the backup's UserData in place.
bool restore(const fs::path &save_dir, const fs::path &backups, const std::string &name, int64_t now,
             std::string *message);
// ---- profiles the game deletes ----
constexpr int kKeepQuarantine = 30;
struct Quarantined
{
    std::string name;           // the folder's name in quarantine, yyyymmdd-hhmmss-<folder>[-n]
    fs::path path;
    std::string folder;         // the profile folder it was (from the name)
    std::string profile;        // the profile's name from its SaveMeta.xbx ("" none)
    int64_t when = 0;           // when the game deleted it, Unix seconds
};
bool is_quarantine_name(const std::string &name);
std::vector<Quarantined> list_quarantine(const fs::path &backups);  // newest first
// Copies one profile folder (its files) to <backups>/quarantine/<stamp>-<folder>, then keeps the newest
// `keep` there. False with *error when the folder is missing or empty, or the copy fails.
bool quarantine(const fs::path &folder, const fs::path &backups, int64_t now, std::string *made, std::string *error,
                int keep = kKeepQuarantine);
std::string describe_time(int64_t when);                            // "3 Nov 2026, 14:05"
std::string describe_reason(const std::string &reason);

// ---- bringing in Xbox profiles ----
using Groups = std::map<std::string, std::map<std::string, Bytes>>; // folder -> file -> bytes
bool read_zip(const fs::path &zip, Groups &groups, std::string *error);
bool read_folder(const fs::path &folder, Groups &groups, std::string *error);
bool inflate_raw(const uint8_t *src, size_t size, Bytes &out, size_t limit);
uint32_t crc32(const uint8_t *data, size_t size);

// ---- bringing in PS2 profiles (hK's PS2 -> Xbox converter, ported; docs/importing-xbox-saves.md) ----
// A PS2 Story profile is 22888 bytes: "20CM", its three sizes, 12 bytes of the PS2's own check, the
// 64-byte identity block at 28 and the profile from 92 (the fighter's name first). The Xbox file
// keeps the profile around a portrait of another size: the PS2 portrait (payload 384..19852) becomes
// a neutral Xbox preview (384..19844) that the game draws again when it next saves the profile.
constexpr size_t kPs2ProfileSize = 22888;
struct Ps2Profile
{
    std::string file;           // its name in the export ("BESLES-52507...")
    std::string region;         // "PAL", "NTSC-U" or "" unknown
    std::string name;           // the fighter's name at +92 ("" none)
    Bytes data;                 // the 22888 PS2 bytes
};
bool is_ps2_profile(const Bytes &blob);
// The files of a .psu export (uLaunchELF, PCSX2, mymc): false with *error when it is not one.
bool read_psu(const Bytes &file, std::vector<std::pair<std::string, Bytes>> &files, std::string *error);
// The PS2 profiles in a picked file: a .psu export or a bare profile file. False with *error (for the
// player) when the file is not a PS2 save we read, a whole memory card (.ps2) or a MAX Drive save.
// True with nothing in `out` when it is a PS2 save without a Story profile.
bool read_ps2(const Bytes &file, std::vector<Ps2Profile> &out, std::string *error);
// "" when the fighter's name can be a profile here (1..20 letters, digits, space, _ and -, not a
// name Windows keeps for itself); else why not.
std::string ps2_name_fault(const std::string &name);
Bytes ps2_to_xbox(const Bytes &ps2);    // the 0x5990-byte Xbox profile, signed with kPortKey ({} if not a PS2 profile)
Bytes neutral_save_image();             // a plain grey 64x64 saveimage.xbx (XPR0, DXT1)
Bytes save_meta(const std::string &name);   // SaveMeta.xbx: UTF-16LE "Name=<name>\r\n" with a BOM

struct Candidate
{
    std::string source;         // the folder it came from
    std::string name;           // the profile name
    std::string folder;         // where it goes (folder_name(name))
    std::string origin;         // "" an Xbox profile; "PS2, PAL" and the like for a converted one
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
