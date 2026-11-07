// "Enhanced graphics (community)": the launcher installs, switches and removes the community ReShade /
// DLSS pack (presets by Marleymarletto) in the game's bin folder, next to defjam_recomp.exe. The game is
// never changed; the pack loads through its dxgi.dll proxy, in the game's process only.
//
// The launcher and the game are the same exe and dxgi.dll beside it loads at process start, so the proxy is
// kept as dxgi.ffny-mod. Play (start_game_process, pc_settings.c) renames it to dxgi.dll, starts the game's
// process and renames it back as soon as that process has it loaded, or has ended. An older install's
// dxgi.dll is renamed at the next start. Start guard: a marker is written before the start with the mod and
// cleared when the game has run for a minute (or closed normally); a marker found later means the mod did
// not get through, and the next Play is without it, that one time.
//
// The pack comes from the update server: graphics.json (made by scripts/make-graphics-pack.py, signed
// like latest.json, update_sig.h) names a shared base archive and one small archive per preset with their
// sizes and SHA-256. Downloads go to <data>/graphics/cache (resumed, retried, checked), the files that
// were installed are recorded in <data>/graphics/installed.txt, and removal deletes exactly those (a file
// the running program holds, such as the loaded dxgi.dll, is renamed and deleted at the next start). A
// file already in bin that is not ours is never overwritten: the install stops and says so. An installed
// pack follows the published one: at the launcher's start and when Graphics opens, a graphics.json with
// another version brings the changed archives only and switches over whole (update(), below).
//
// Settings: [graphics] enabled, preset (performance by default, quality). RECOMP_GRAPHICS_SERVER=
// host:port (plain HTTP, /graphics/...) points at another server; with it on this PC (127.0.0.1 or
// localhost), RECOMP_GRAPHICS_TEST_KEY=<128 hex digits> is the key that must have signed graphics.json:
// for tests only.
#pragma once

#ifdef __cplusplus
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace gfx
{
namespace fs = std::filesystem;

struct Archive { std::string path, sha256; uint64_t size = 0; };
struct Manifest
{
    std::string version, notices, credits;
    Archive base, quality, performance;
    std::vector<std::string> base_files, quality_files, performance_files;
};

using Progress = std::function<void(uint64_t got, uint64_t total)>;

// The signed manifest from the server.
bool fetch_manifest(Manifest &m, std::string *error);
// The archive in `cache`, downloaded when missing or wrong; resumed and retried; checked against the
// manifest's size and SHA-256. Returns its path, empty on failure.
fs::path ensure_archive(const Archive &a, const fs::path &cache, const Progress &progress, std::string *error);
// Installs base + preset into `bin` from the cached archives, recording owned files under `data`.
bool install(const Manifest &m, const fs::path &bin, const fs::path &data, const std::string &preset,
             const Progress &progress, std::string *error);
// Swaps the preset's files only.
bool switch_preset(const Manifest &m, const fs::path &data, const std::string &preset, const Progress &progress,
                   std::string *error);
// Deletes exactly the recorded files. Works without the server and without a manifest.
bool remove(const fs::path &data, std::string *error);
// Deletes what an earlier removal had to leave (a file the program held).
void finish_pending(const fs::path &data);

// Brings an installed pack to the manifest's version when it differs. Only the archives whose SHA-256 differs
// from the installed one (recorded at install) are downloaded, checked like an install's and unpacked beside
// bin first; then, under bin_lock() and only while go_on() says yes, the old files go aside and the new ones in,
// with a journal, and the new record is the commit. Any failure puts the old pack back: never half a pack.
// True with *updated false: nothing to do, or the game started (go_on said no) and it waits for the next time.
bool update(const Manifest &m, const fs::path &data, const Progress &progress, const std::function<bool()> &go_on,
            std::string *error, bool *updated);
// An update that stopped half-way (the power went): finished when its record was written, else undone.
void recover_update(const fs::path &data);
// Held while the pack's files in bin change (an update's switch, Turn off) and while Play puts the proxy in place.
std::mutex &bin_lock();

struct Record
{
    bool present = false;
    std::string version, preset;
    fs::path bin;
    std::vector<std::string> files, dirs;
    std::string base_sha, preset_sha;   // the installed archives (empty: from before updates; all count as changed)
};
Record read_record(const fs::path &data);

// ---- the proxy's two names and the start guard ----
// Parks a dxgi.dll the record owns (an older install, or a start that stopped half-way). A dxgi.dll beside the
// parked proxy is not ours and stays. False when it could not be renamed now.
bool tidy_proxy(const fs::path &data);
// Before the game's process starts: true when the proxy is in place as dxgi.dll and the marker written. False:
// start without the mod (off, not installed, a foreign dxgi.dll, or the guard says so this one time).
bool begin_play(const fs::path &data, const fs::path &bin, bool enabled);
void start_failed(const fs::path &data);    // the process could not be started
// After it started: parks the proxy as soon as `child` (a process handle) has loaded it or ended, then clears
// the marker at guard_ms of running, or at a normal close before that. Returns when that is settled.
void watch_play(void *child, const fs::path &data, unsigned long guard_ms);
bool guard_note_launcher_start(const fs::path &data);   // a marker left behind becomes "next Play without the mod"
bool guard_tripped(const fs::path &data);
void clear_guard(const fs::path &data);

// ---- the launcher's view (background work) ----
struct View
{
    bool busy = false, installed = false;
    std::string preset;             // the installed one
    std::string version;
    std::string progress;           // what the background work does now
    double fraction = -1;           // its progress, -1 unknown
    std::string message;            // what happened last
    int tone = 0;                   // 0 plain, 1 done, 2 a problem
    std::string credits, notices;   // from the last manifest (credits has a default)
    bool skip_once = false;         // the next Play is without the mod (the start guard)
    std::string notice;             // the last update's outcome, for the card ("Graphics pack updated to ...")
    int notice_tone = 0;
};
View view();
void start_enable(const std::string &preset);
void start_switch(const std::string &preset);
void turn_off();                     // synchronous: removes the files now
void try_again();                    // the next Play is with the mod again
void launcher_starts();             // pending deletes, the proxy's name, the guard; re-install into a new version's bin when enabled
                                    // or else check_update()
void check_update();                 // in the background: a newer published pack replaces the installed one (not while the game runs)
bool gpu_supported(std::string *adapter, std::string *why);
fs::path bin_folder();               // where defjam_recomp.exe is
fs::path data_folder();              // <data>/graphics lives below it
} // namespace gfx
extern "C" {
#endif

/* One line for the log's header (crash and F9 reports): whether the pack is installed and loaded. */
void graphics_mod_log_header(void);

#if defined(_WIN32)
/* The game's process, pc_settings.c. Whether a start without the launcher has to go through its own process too. */
int graphics_mod_wanted(void);
int graphics_mod_before_start(void);        /* 1: started with the mod; then one of the next two */
void graphics_mod_start_failed(void);
void graphics_mod_started(void *process, unsigned long pid);    /* the notice and the watch, in the background */
void graphics_mod_settled(void);            /* waits for the watch; the proxy is parked when this returns */
#endif

#ifdef __cplusplus
}
#endif
