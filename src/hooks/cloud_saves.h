// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Cloud saves underneath (build 91): the save area as one archive for the account service, the check of a
// cloud save before it is restored, the restore itself (through the Saves page's backups) and the choice of
// what to do at a launcher start. No UI, no network and no platform calls beyond std::filesystem, like
// save_manager.h, so native unit tests build it on its own (tests/unit/test_cloud_saves.py). The network
// and the launcher's state are pc_launcher_cloud.cpp; the Saves page's Cloud view is pc_launcher_saves.inc.
// The service is server relay/accounts/cloudsaves.go, which checks an archive by the same rules.
//
// What goes up: <save>/UserData/45410049 (its TitleMeta.xbx / TitleImage.xbx, each profile folder and the
// Options folder) and <save>/aside (the profiles put aside), nothing else: never the partition images beside
// them. A file is taken when its path has the save area's shape and it is a save file: a .xbx starting FF FE
// (SaveMeta, TitleMeta) or "XPR0" (an image), or a "20CM" file (a profile, Options); at most kMaxFile bytes.
// Anything else in those folders is left out (and named in the log), so a stray desktop.ini never blocks it.
//
// The content hash, on both sides: SHA-256 over every file sorted by its path (bytes), each as its path, a
// 0 byte, its size as 8 bytes little-endian and its bytes. The zip itself (deflate, fixed times) may differ
// between builds; the hash says whether the saves did.
#pragma once
#include "save_manager.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cloud
{
namespace fs = std::filesystem;
using Bytes = saves::Bytes;
using Files = std::map<std::string, Bytes>;     // "UserData/45410049/<folder>/<file>" or "aside/<stash>/<file>"

constexpr size_t kMaxArchive = 4u << 20;        // the zip
constexpr size_t kMaxTotal = 8u << 20;          // its files unpacked
constexpr size_t kMaxFile = 64u << 10;          // one file
constexpr size_t kMaxFiles = 400;
constexpr int kKeep = 5;                        // versions the service keeps

std::string sha256_hex(const uint8_t *data, size_t size);
std::string content_hash(const Files &files);
// "" when `name` may be in a cloud save with these bytes; else why not (for the log).
std::string file_fault(const std::string &name, const Bytes &data);
// Profile files in it (in the game and put aside; the Options file is not one).
int count_profiles(const Files &files);
int count_game_profiles(const Files &files);    // only those in UserData/45410049

// The save area as the cloud takes it. False with *error when it cannot be read or is too large for the
// cloud; `skipped` gets what was left out.
bool collect(const fs::path &save_dir, Files &out, std::vector<std::string> *skipped, std::string *error);
// The archive (deflate where it helps, else stored; fixed times, sorted names).
Bytes pack(const Files &files);
// Reads and checks a cloud save: the zip's structure and CRCs, the limits, every file (file_fault) and at
// least one profile. False with *error (for the player).
bool unpack(const Bytes &zip, Files &out, std::string *error);

// ---- restore ----
// What a restore changes, per profile (the confirm lists it).
struct Change
{
    // ADDED: comes from the cloud; REPLACED: the cloud's copy replaces this PC's; SAME: no change;
    // ONLY_HERE: not in the cloud save, so it leaves the game (it is in the backup made first);
    // KEPT: a profile put aside that both have: this PC's stays (the cloud's is in its backup).
    enum What { ADDED, REPLACED, SAME, ONLY_HERE, KEPT };
    What what = SAME;
    std::string name;           // the profile's name (SaveMeta.xbx), else its folder; "Options" for the settings
    bool aside = false;         // a profile put aside
};
std::vector<Change> compare(const Files &local, const Files &cloud);
// Puts the cloud save in save-backups as a backup of its own (reason "cloud", `note` says which version),
// so it stays on this PC whatever happens next. False with *error.
bool keep_as_backup(const Files &cloud, const fs::path &backups, int64_t now, const std::string &note,
                    std::string *made, std::string *error);
// Restores a checked cloud save: kept as a backup (keep_as_backup), then the Saves page's restore puts its
// UserData in place, which backs up the current saves first ("Before a restore"). The cloud's profiles put
// aside come into <save>/aside where that folder is free; the PC's own profiles put aside stay. Never
// overwrites without a backup. *message for the player.
bool restore(const Files &cloud, const fs::path &save_dir, const fs::path &backups, int64_t now, const std::string &note,
             std::string *message);

// ---- the service's list (GET /api/saves?format=text) ----
struct Version
{
    std::string id, hash, pc;
    int64_t size = 0, bytes = 0, uploaded = 0;
    int files = 0, profiles = 0;
};
struct Listing
{
    bool ok = false;
    std::string account;        // the account's number: the sync state belongs to one account
    std::vector<Version> versions;   // newest first
};
Listing parse_listing(const std::string &text);
// A "key=value" line of a text answer ("" none).
std::string text_field(const std::string &text, const std::string &key);

// ---- this PC's sync state: <data>/cloud-saves.txt ----
struct SyncState
{
    std::string account;        // whose cloud it synced with
    std::string hash;           // the content hash this PC and the cloud last agreed on ("" never)
    std::string version;        // that version's id
    int64_t when = 0;
};
SyncState read_state(const fs::path &file);
bool write_state(const fs::path &file, const SyncState &state);

// ---- this PC's label: <data>/cloud-pc.txt ----
// What the cloud's list says a version came from. Never the Windows computer name (it often holds a real
// name): a short random label made once ("PC 7K2F"), which the player can rename on the CLOUD view.
// clean_label keeps letters, digits, space, dot, dash and underscore, 24 at most, trimmed ("" when nothing is
// left); the service keeps the same characters (cloudPC).
std::string clean_label(const std::string &text);
std::string new_label(uint32_t random);         // "PC " and 4 of 23456789ABCDEFGHJKLMNPQRSTUVWXYZ
std::string read_label(const fs::path &file);   // "" none or not usable
bool write_label(const fs::path &file, const std::string &label);

// ---- what to do ----
enum class Decision {
    NOTHING,        // nothing to send and nothing to offer
    IN_SYNC,        // this PC and the cloud's newest agree
    UPLOAD,         // only this PC changed (or the cloud is empty): send it
    OFFER_RESTORE,  // no profile here, the cloud has some: ask to restore (a new PC)
    CLOUD_NEWER,    // only the cloud changed (another PC sent it): ask, never restore silently
    CONFLICT,       // both changed since the last sync (or never synced and they differ): ask, keep both
};
// local: this PC's content hash ("" none); base: the last agreed hash ("" never); cloud: the newest
// version's hash ("" none). A PC without a profile never uploads.
Decision decide(const std::string &local, bool local_profiles, const std::string &base, const std::string &cloud);
const char *decision_name(Decision d);
} // namespace cloud
