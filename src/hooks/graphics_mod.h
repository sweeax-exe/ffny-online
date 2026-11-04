// "Enhanced graphics (community)": the launcher installs, switches and removes the community ReShade /
// DLSS pack (presets by Marleymarletto) in the game's bin folder, next to defjam_recomp.exe. The game is
// never changed; the pack loads through its dxgi.dll proxy at the next start of the program.
//
// The pack comes from the update server: graphics.json (made by scripts/make-graphics-pack.py, signed
// like latest.json, update_sig.h) names a shared base archive and one small archive per preset with their
// sizes and SHA-256. Downloads go to <data>/graphics/cache (resumed, retried, checked), the files that
// were installed are recorded in <data>/graphics/installed.txt, and removal deletes exactly those (a file
// the running program holds, such as the loaded dxgi.dll, is renamed and deleted at the next start). A
// file already in bin that is not ours is never overwritten: the install stops and says so.
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

struct Record { bool present = false; std::string version, preset; fs::path bin; std::vector<std::string> files, dirs; };
Record read_record(const fs::path &data);

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
    bool restart_needed = false;    // the files changed since this program started
};
View view();
void start_enable(const std::string &preset);
void start_switch(const std::string &preset);
void turn_off();                     // synchronous: removes the files now
void launcher_starts();             // pending deletes; re-install into a new version's bin when enabled
bool gpu_supported(std::string *adapter, std::string *why);
fs::path bin_folder();               // where defjam_recomp.exe is
fs::path data_folder();              // <data>/graphics lives below it
} // namespace gfx
extern "C" {
#endif

/* One line for the log's header (crash and F9 reports): whether the pack is installed and loaded. */
void graphics_mod_log_header(void);

#ifdef __cplusplus
}
#endif
