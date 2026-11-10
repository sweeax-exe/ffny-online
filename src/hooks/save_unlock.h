// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// "Unlock everything" for a profile on the launcher's Saves page: a real edit of the save files the game
// reads, signed again, after a backup (save_manager.h), so the game loads the profile normally and
// nothing depends on a switch afterwards. docs/research/unlock-everything.md ("The save edit") has the layout and how
// it was read from the game's own code; tests/unit/test_save_unlock.py covers it. Standard C++17 only.
//
// What the game keeps where (guest addresses in the USA dump):
//   - The "Options" file holds the global record (game data +4, +0x50..+0x14E0): the reward points,
//     the unlocked fighters and venues (sorted id sets, the Unlock Rewards screen and Battle mode's
//     fighter and venue lists; the match types follow the venues) and the songs. These are shared by
//     every profile on the PC: the game has one set of them.
//   - A user ID's file holds its profile (0x5990 bytes: header, then the profile from +0xC): story
//     cash, development points, the created fighter, and from profile +0x4DC4 one 2-bit state per item
//     (0 locked, 1 unlocked, 2/3 owned) for hair styles, tattoos, jewellery, clothing, Blazin moves and
//     fighting styles, in that order, each (items / 4 + 1) bytes. The item counts come from the
//     database (assets/djv2.sod), as the game builds them (sub_00013CB0 and the five after it).
#pragma once
#include "save_manager.h"

namespace saves
{
// The parts of the database the edit needs: item counts in profile order and the two global sets.
struct UnlockLayout
{
    // hair styles, tattoos, jewellery, clothing, Blazin moves, fighting styles (the order in the profile)
    uint32_t items[6] = {};
    uint32_t character_table = 0, characters = 0;   // table index (an id is table << 24 | row) and rows
    uint32_t venue_table = 0, venues = 0;
};
extern const char *const kUnlockItemNames[6];       // "hair styles", "tattoos", ...
bool unlock_layout(const Bytes &sod, UnlockLayout &out, std::string *error);

struct UnlockCounts
{
    int items[6] = {};          // items that were locked and are now unlocked, per kind
    int fighters = 0, venues = 0, songs = 0;
    bool cash = false;          // the cash was raised to $999,999
};
constexpr uint32_t kUnlockCash = 999999;
// The edits on the file bytes, unsigned (resign() after). False with *error when the file is not one
// they know (nothing is changed then).
bool unlock_profile_bytes(Bytes &profile, const UnlockLayout &layout, bool cash, UnlockCounts &counts,
                          std::string *error);
bool unlock_options_bytes(Bytes &options, const UnlockLayout &layout, UnlockCounts &counts, std::string *error);

struct UnlockResult
{
    bool ok = false;
    std::string backup;         // the backup made first: Restore it to undo
    std::string message;        // for the player
    UnlockCounts counts;
};
// The profile in UserData/45410049/<folder> and the Options file: backup (reason "unlock"), edit, sign,
// write. Refused, with nothing changed, when either file is missing or not as expected.
UnlockResult unlock_everything(const fs::path &save_dir, const fs::path &backups, const std::string &folder,
                               const Bytes &sod, bool cash, int64_t now);
} // namespace saves
