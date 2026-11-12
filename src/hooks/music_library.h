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
#include <random>
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
    double level_gain = -1;     // the game's player: LevelMeter::gain() once measured, -1 before
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

// How loud a song is, to bring the player's songs to the level of the game's own music (most
// releases are mastered far louder). Gated RMS in 400 ms blocks of interleaved stereo: blocks under
// -50 dBFS (silence, fades) do not count, like the gate of a loudness meter, so a quiet intro does
// not pull the gain up. No K-weighting; the blocks' mean power is enough to even out masters.
struct LevelMeter
{
    static constexpr double kGate = 0.00316;    // -50 dBFS
    static constexpr double kTarget = 0.1;      // -20 dBFS RMS: about the game's own music
    static constexpr double kMinGain = 0.1, kMaxGain = 2.0;
    int rate = 44100;
    double block_sum = 0, gated_sum = 0;
    long block_frames = 0, gated_blocks = 0;
    void feed(const int16_t *stereo, long frames);
    double rms() const;     // of the blocks over the gate; 0 when none
    // The gain that brings the song to kTarget, within kMinGain..kMaxGain (1 when nothing was heard).
    double gain() const;
};

// The order the music plays in: the player's songs (items 0..own-1) and, in mix, `game` turns of the game's
// own soundtrack (items own..own+game-1, all the same: "the game's music until its next track"). Each item
// once per round; shuffled, so the first item is random across both; in order, the songs in folder order
// with a game turn after each while any are left (the mix of earlier builds). A new round never starts
// with the item the last one ended on. Host only: its own random numbers, never the game's.
struct Playlist
{
    static constexpr size_t kGameTracks = 30;   // the game's soundtrack, about 30 tracks (audio/music*.viv)
    // The game turns mix gives `own` songs: as many as songs, up to the soundtrack's size, so a few songs
    // still play about half of the time and many songs share it fairly with the game's tracks.
    static size_t game_turns(size_t own) { return own < kGameTracks ? own : kGameTracks; }

    size_t own = 0, game = 0;
    bool shuffle = true;
    std::mt19937_64 rng;
    std::vector<size_t> order;
    size_t at = 0;
    size_t last = (size_t)-1;

    void reset(size_t own_songs, size_t game_turns, bool shuffled, uint64_t seed);
    bool is_game(size_t item) const { return item >= own; }
    // The next item. `avoid_game`: a song rather than a game turn when one is left (a new fight after the
    // game's music played through the last one, so the new fight does not keep the game's track).
    size_t next(bool avoid_game = false);

private:
    void new_round();
};

} // namespace musiclib
