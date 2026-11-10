// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The player's own songs on disk (custom music): the folder <data>/music, what is in it, which files
// play, adding and removing songs. Used by the launcher's Music dialog (pc_launcher_music.inc) and by
// the game's music player (custom_music.cpp). Host only; nothing here touches the game.
//
// The folder holds the songs as they are (MP3, Ogg Vorbis, FLAC, WAV), in sub-folders too. Nothing is
// converted: the game decodes them as it plays. The launcher checks each new or changed file once in
// the background (it opens it and decodes a few seconds) and keeps the answer in
// <data>/music/.cache/index.tsv, so the game skips a file known to be bad without opening it.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace musiclib
{
namespace fs = std::filesystem;

enum State { UNCHECKED, PLAYS, BAD };

struct Song
{
    fs::path path;
    std::string rel;            // UTF-8, relative to the folder, '/' separated: the key in the index
    std::string name;           // the file name without its extension, for the list
    uint64_t size = 0;
    int64_t mtime = 0;
    int state = UNCHECKED;
    double seconds = -1;
    std::string why;            // BAD: the reason
};

constexpr size_t kMaxFiles = 2000;      // a huge folder: the first 2000 songs by name, the rest is skipped
constexpr int kMaxDepth = 4;

std::string utf8(const fs::path &p);
fs::path from_utf8(const std::string &s);

// The songs under `folder` (sorted by path, songs only, ".cache" and hidden folders left out). Never
// throws; an unreadable folder gives what could be read. `more` gets the number of songs left out.
std::vector<Song> scan(const fs::path &folder, size_t max_files = kMaxFiles, int max_depth = kMaxDepth,
                       size_t *more = nullptr);
// Fills state, seconds and why from the index for files whose size and time still match.
void apply_index(const fs::path &folder, std::vector<Song> &songs);
void save_index(const fs::path &folder, const std::vector<Song> &songs);
// Opens the song and decodes a few seconds: PLAYS or BAD with the reason.
void check(Song &song);

struct AddResult
{
    int added = 0, skipped = 0;
    std::vector<std::string> notes;     // one line per skipped file
};
// Copies the files (or the songs in the folders) given into `folder`. A name already there with the
// same size is not copied again; another file with the same name is kept as "name (2).ext".
// `progress(done, total, name)` is called before each file.
AddResult add(const fs::path &folder, const std::vector<fs::path> &sources,
              const std::function<void(int, int, const std::string &)> &progress = nullptr);
// Takes a song out of the folder: to the Recycle Bin on Windows, elsewhere to .cache/removed/.
bool remove(const fs::path &folder, const Song &song, std::string *why);

// The [music] settings.
enum Mode { MODE_AUTO, MODE_MINE, MODE_MIX, MODE_ORIGINAL };
// What `mode` means with `songs` playable songs: AUTO is MIX when there are songs, ORIGINAL when not.
Mode effective_mode(int mode, size_t songs);

// When a fight ends and the next begins, from the game's match phase read once per game update
// ([0x3B8F30]: 0 start, 1 fighting, 2 decided, 3 finished; anything else counts as no fight). A fight
// counts once it ran kMinFight updates; it is over once the phase stayed off "fighting" for kSettle
// updates, so a phase that flickers for a few updates (a rollback correcting a predicted KO) is no end.
// The next fight begins when the phase goes back to 0 or 1 after that end. One fight is one match: the
// game has no rounds inside a fight (a KOTH or FFA rotation is a new fight each time).
struct FightWatch
{
    enum Event { NONE, ENDED, BEGAN };
    static constexpr int kMinFight = 60, kSettle = 30;
    int last = -1, fighting = 0, away = 0;
    bool fought = false, ended = false;
    Event feed(int phase);
};

} // namespace musiclib
