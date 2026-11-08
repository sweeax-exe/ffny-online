// Custom music, the host side (custom_music.h): the playlist and the player thread. The songs are
// decoded here (music_decode.c) and played on an XAudio2 voice of our own; nothing here reads or
// writes the guest. The game thread only tells us what its music does (custom_music_guest.c).
#define _CRT_SECURE_NO_WARNINGS
#include "custom_music.h"
#include "music_library.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "music_decode.h"
#include "recomp_settings.h"
}

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>
#pragma comment(lib, "xaudio2_8.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace
{
using musiclib::Song;

std::atomic<bool> g_active{false};          // songs to play, a mode that plays them, a working voice
std::atomic<bool> g_custom_turn{true};      // mix: our song is the music now (mine: always)
std::atomic<bool> g_game_playing{false};    // the game's music stream is open
std::atomic<bool> g_have_song{false};       // a song of ours is loaded and playing or paused
std::atomic<bool> g_in_fight{false};
std::atomic<int> g_game_volume{85};
std::atomic<unsigned> g_track_changes{0};
std::atomic<unsigned> g_fights_ended{0};    // fights over so far (musiclib::FightWatch)
std::atomic<unsigned> g_fights_begun{0};    // fights begun after one was over
musiclib::FightWatch g_fight_watch;         // the game thread's only
musiclib::Mode g_mode = musiclib::MODE_ORIGINAL;
bool g_shuffle = true;
bool g_per_match = true;                    // [music] new_song_per_match: each fight after the first starts the next song
float g_gain = 0.5f;
std::vector<Song> g_songs;
std::vector<size_t> g_order;
size_t g_next = 0;
std::mt19937 g_rng{std::random_device{}()};

void log(const char *fmt, ...)
{
    char line[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[MUSIC] %s\n", line);
    fflush(stderr);
}

void reorder(size_t avoid_first)
{
    g_order.resize(g_songs.size());
    for (size_t i = 0; i < g_order.size(); i++) g_order[i] = i;
    if (g_shuffle) {
        std::shuffle(g_order.begin(), g_order.end(), g_rng);
        if (g_order.size() > 1 && g_order[0] == avoid_first) std::swap(g_order[0], g_order[1]);
    }
    g_next = 0;
}

// The next song that opens, or nullptr when none of them does.
MusicDecoder *open_next(size_t *index)
{
    static size_t last = (size_t)-1;
    for (size_t tries = 0; tries < g_songs.size(); tries++) {
        if (g_next >= g_order.size()) reorder(last);
        size_t i = g_order[g_next++];
        Song &s = g_songs[i];
        if (s.state == musiclib::BAD) continue;
        char why[160] = "";
        MusicDecoder *d = music_decoder_open(musiclib::utf8(s.path).c_str(), why, sizeof(why));
        if (!d) {
            s.state = musiclib::BAD;
            log("skipped %s: %s", s.rel.c_str(), why);
            continue;
        }
        last = i;
        *index = i;
        return d;
    }
    return nullptr;
}

#if defined(_WIN32)
// ---- the player thread ----

constexpr int kFrames = 4096, kBuffers = 4;

struct Player
{
    IXAudio2 *xa = nullptr;
    IXAudio2MasteringVoice *master = nullptr;
    IXAudio2SourceVoice *voice = nullptr;
    int voice_rate = 0;
    bool started = false;
    int16_t pcm[kBuffers][kFrames * 2];
    int slot = 0;
    MusicDecoder *song = nullptr;
    size_t song_index = 0;
    unsigned song_ends_seen = 0;        // g_fights_ended when this song started
    bool draining = false;              // the song ended; waiting for the queue to play out
    unsigned long long frames = 0;
    unsigned songs_played = 0;

    bool init()
    {
        if (FAILED(XAudio2Create(&xa, 0, XAUDIO2_DEFAULT_PROCESSOR)) || !xa) return false;
        if (FAILED(xa->CreateMasteringVoice(&master))) return false;
        return true;
    }
    size_t queued()
    {
        if (!voice) return 0;
        XAUDIO2_VOICE_STATE st;
        voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return st.BuffersQueued;
    }
    bool make_voice(int rate)
    {
        if (voice && voice_rate == rate) return true;
        if (voice) { voice->DestroyVoice(); voice = nullptr; started = false; }
        WAVEFORMATEX wf = {};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = 2;
        wf.nSamplesPerSec = (DWORD)rate;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = 4;
        wf.nAvgBytesPerSec = (DWORD)rate * 4;
        if (FAILED(xa->CreateSourceVoice(&voice, &wf, 0, XAUDIO2_DEFAULT_FREQ_RATIO))) { voice = nullptr; return false; }
        voice_rate = rate;
        return true;
    }
    void close_song()
    {
        if (song) music_decoder_close(song);
        song = nullptr;
    }
    void start_song()
    {
        size_t index = 0;
        MusicDecoder *d = open_next(&index);
        if (!d) {
            log("none of the %zu songs can be played; the game's own music plays", g_songs.size());
            g_active = false;
            return;
        }
        // A new rate needs a new voice: let the old one finish first.
        if (voice && voice_rate != music_decoder_rate(d) && queued()) {
            while (queued() && g_active) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!make_voice(music_decoder_rate(d))) {
            log("no audio voice for %d Hz; skipped %s", music_decoder_rate(d), g_songs[index].rel.c_str());
            music_decoder_close(d);
            g_songs[index].state = musiclib::BAD;
            return;
        }
        song = d;
        song_index = index;
        song_ends_seen = g_fights_ended.load();
        songs_played++;
        g_have_song = true;
        draining = false;
        double sec = music_decoder_seconds(d);
        log("playing %s (%d Hz, %d ch, %.0f s; song %u, %zu in the list)", g_songs[index].rel.c_str(),
            music_decoder_rate(d), music_decoder_channels(d), sec, songs_played, g_songs.size());
    }
    void feed()
    {
        while (song && !draining && queued() < kBuffers - 1) {
            long n = music_decoder_read(song, pcm[slot], kFrames);
            if (n <= 0) {
                if (n < 0) log("%s is damaged part-way; it ends here", g_songs[song_index].rel.c_str());
                close_song();
                draining = true;
                return;
            }
            XAUDIO2_BUFFER b = {};
            b.AudioBytes = (UINT32)(n * 4);
            b.pAudioData = (const BYTE *)pcm[slot];
            if (FAILED(voice->SubmitSourceBuffer(&b))) { close_song(); return; }
            slot = (slot + 1) % kBuffers;
            frames += (unsigned long long)n;
        }
    }
    void run()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (!init()) {
            log("no audio device for the songs (XAudio2); the game's own music plays");
            g_active = false;
            return;
        }
        unsigned seen_changes = g_track_changes.load();
        unsigned seen_begun = g_fights_begun.load();
        float applied = -1;
        auto last_report = std::chrono::steady_clock::now();
        while (g_active) {
            unsigned changes = g_track_changes.load();
            if (changes != seen_changes) {
                seen_changes = changes;
                // mix: the game's own track was playing; its next track is ours instead
                if (g_mode == musiclib::MODE_MIX && !g_custom_turn) {
                    g_custom_turn = true;
                    log("the game changes track: one of your songs next");
                }
            }
            unsigned begun = g_fights_begun.load();
            if (begun != seen_begun) {
                seen_begun = begun;
                // A new fight (rematch, next match, the next fight of a rotation): the song that played
                // through the last one gives way to the next in the order. A song that began after that
                // fight was over (on its results) carries on. Paused or not, the new one waits for the game.
                if (g_per_match && (song || draining) && song_ends_seen < g_fights_ended.load()) {
                    log("a new fight: the next song (fight %u)", begun);
                    if (voice) { voice->DestroyVoice(); voice = nullptr; voice_rate = 0; started = false; }
                    applied = -1;
                    close_song();
                    draining = false;
                    start_song();
                    if (!g_active) break;
                }
            }
            bool want = g_game_playing && g_custom_turn;
            if (want) {
                if (!song && !draining) start_song();
                if (!g_active) break;
                feed();
                if (draining && !queued()) {
                    draining = false;
                    // mine, or a fight online (no hand-over mid-fight): the next song; mix: the game's turn
                    if (g_mode == musiclib::MODE_MIX && !g_in_fight) {
                        g_custom_turn = false;
                        g_have_song = false;
                        log("song over: the game's own music until its next track");
                    }
                }
                if (voice && !started) { voice->Start(0); started = true; }
            } else if (voice && started && queued()) {
                voice->Stop(0);         // paused with what is queued; Start resumes it
                started = false;
            }
            float v = g_gain * (float)g_game_volume.load() / 100.0f;
            if (voice && v != applied) { voice->SetVolume(v); applied = v; }
            auto now = std::chrono::steady_clock::now();
            if (now - last_report > std::chrono::seconds(30) && started) {
                last_report = now;
                log("%llu frames played so far (%u songs), volume %.2f", frames, songs_played, v);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        close_song();
        g_have_song = false;
        if (voice) voice->Stop(0);
    }
};

Player *g_player;

void start_player()
{
    g_player = new Player();
    std::thread([] { g_player->run(); }).detach();
}
#else
void start_player()
{
    log("custom music plays on Windows only for now; the game's own music plays");
    g_active = false;
}
#endif

} // namespace

extern "C" void custom_music_game_starts(const char *data_root)
{
    int mode = recomp_settings_get("music", "mode", 0);
    g_shuffle = recomp_settings_get("music", "shuffle", 1) != 0;
    g_per_match = recomp_settings_get("music", "new_song_per_match", 1) != 0;
    if (const char *gain = getenv("RECOMP_MUSIC_GAIN")) g_gain = std::max(0, std::min(200, atoi(gain))) / 100.0f;
    musiclib::fs::path folder;
    if (const char *dir = getenv("RECOMP_MUSIC_DIR")) folder = musiclib::from_utf8(dir);
    else if (data_root && *data_root) folder = musiclib::from_utf8(data_root) / "music";
    if (folder.empty() || mode == musiclib::MODE_ORIGINAL) {
        if (mode == musiclib::MODE_ORIGINAL) log("mode original: the game's own soundtrack");
        return;
    }
    size_t more = 0;
    std::vector<Song> all;
    try {
        all = musiclib::scan(folder, musiclib::kMaxFiles, musiclib::kMaxDepth, &more);
        musiclib::apply_index(folder, all);
    } catch (...) {
        all.clear();
    }
    size_t bad = 0;
    for (auto &s : all) {
        if (s.state == musiclib::BAD) { bad++; continue; }
        g_songs.push_back(s);
    }
    g_mode = musiclib::effective_mode(mode, g_songs.size());
    log("%zu song(s) in %s (%zu known bad, %zu over the limit), mode %s, shuffle %s, new song per match %s",
        g_songs.size(), musiclib::utf8(folder).c_str(), bad, more,
        g_mode == musiclib::MODE_MINE ? "mine" : g_mode == musiclib::MODE_MIX ? "mix" : "original", g_shuffle ? "on" : "off",
        g_per_match ? "on" : "off");
    if (g_mode == musiclib::MODE_ORIGINAL) return;
    reorder((size_t)-1);
    g_custom_turn = true;
    g_active = true;
    start_player();
}

extern "C" int custom_music_active(void) { return g_active.load(); }

// Silent while our song is the music; in mix the game's own track plays between ours. A song is loaded
// before the mute is asked for, so a song that cannot be opened never leaves the game silent.
extern "C" int custom_music_want_mute(void) { return g_active && g_custom_turn && g_have_song; }

extern "C" void custom_music_set_game_volume(int volume) { g_game_volume = volume; }
extern "C" void custom_music_set_in_fight(int in_fight) { g_in_fight = in_fight != 0; }

extern "C" void custom_music_note(int playing)
{
    if (g_game_playing.exchange(playing != 0) != (playing != 0) && g_active)
        log("game music stream %s", playing ? "opened: your song plays" : "stopped: your song pauses");
}

extern "C" void custom_music_note_track_change(void) { g_track_changes++; }

extern "C" void custom_music_note_phase(int phase)
{
    switch (g_fight_watch.feed(phase)) {
    case musiclib::FightWatch::ENDED:
        g_fights_ended++;
        if (g_active) log("fight over (%u so far, phase %d)", g_fights_ended.load(), phase);
        break;
    case musiclib::FightWatch::BEGAN:
        g_fights_begun++;
        if (g_active) log("the next fight begins (phase %d)", phase);
        break;
    default:
        break;
    }
}
