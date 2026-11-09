// The app's own updates (docs/research/unified-app-design-2026-10-26.md §4, D215).
//
// 1. Check. When the app starts, unless [launcher] check_updates is off, the setup engine looks on
//    GitHub for the newest stable release whose app is newer than this app's setup payload
//    (engine.py --check-app-update, setup/releases.py newest_app). An answer is kept for twelve
//    hours, and a check that fails says nothing. Home then shows the newer version.
// 2. Download. Update runs engine.py --stage-app: the release's app is downloaded and used only
//    after its GitHub digest, its checksum file, its provenance and the setup payload inside it
//    agree (releases.select_app). It is unpacked beside this app, hidden, on the same volume.
// 3. Swap. This bundle is renamed aside and the verified one renamed into its place; a failure
//    renames it back. The window closes and the new app is started in this process with
//    --updated. The new app removes the old bundle. On Windows the app is a folder holding the
//    program and its payload (DefJamRecompiled.exe, payload\), and a folder with a running program
//    in it cannot be renamed: the program (which Windows lets be renamed while it runs) and the
//    payload are renamed aside one by one, each undone if a later step fails, the new app is
//    started and this one exits, and the new one removes the .old program and payload.
// 4. Build. The new app carries the new release's setup payload. Home offers to build it (after an
//    update it asks at once): a Clean Update of the installed game, while Play keeps the previous
//    build until the new one is activated.
//
// Only an installed app (a macOS bundle, or on Windows the program with its payload beside it)
// replaces itself; any other copy (a development build) points to the release page instead.

#define _CRT_SECURE_NO_WARNINGS
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "imgui.h"

extern "C" {
#include "recomp_settings.h"
}
#include "app.h"
#include "pc_ui_actions.h"
#include "pc_ui_internal.h"
#include "pc_ui_theme.h"

namespace fs = std::filesystem;

namespace app
{
namespace
{

const long long k_check_seconds = 12 * 60 * 60;

enum class Check
{
    Idle,
    Running,
    Done
};

enum class Stage
{
    None,
    Confirm,        // what the update does, and Download and update
    Downloading,
    Failed,
    NotBundle       // this copy cannot replace itself: the release page
};

Check g_check = Check::Idle;
os::Process g_check_process;
fs::path g_check_file;
std::string g_check_status = "Not checked yet";
long long g_last_checked = 0;
std::string g_tag, g_version, g_url;    // the newer app, when there is one

Stage g_stage = Stage::None;
os::Process g_stage_process;
fs::path g_staged, g_scratch, g_status, g_cancel, g_log;
std::string g_line, g_error;
unsigned long long g_started_ms = 0;
bool g_cancelling = false, g_leave = false;

fs::path cache_dir()
{
#if defined(_WIN32)
    fs::path base = os::user_base();
    return base.empty() ? base : base / "DefJamRecompiled" / "cache";
#else
    const char *home = getenv("HOME");
#endif
#if defined(__APPLE__)
    return home ? fs::path(home) / "Library/Caches/DefJamRecompiled" : fs::path();
#elif !defined(_WIN32)
    const char *cache = getenv("XDG_CACHE_HOME");
    if (cache && *cache) return fs::path(cache) / "DefJamRecompiled";
    return home ? fs::path(home) / ".cache/DefJamRecompiled" : fs::path();
#endif
}

} // namespace

// The bundle this program is in (…/Def Jam Recompiled.app/Contents/MacOS/defjam_app), or empty.
// On Windows the folder holding the program and, beside it, the payload it runs setup from. On Linux
// the AppImage file it was started from (app_install_linux.cpp).
fs::path bundle_path()
{
#if defined(_WIN32)
    std::error_code ec;
    fs::path home = app::program().parent_path();
    return !app::payload().empty() && fs::equivalent(app::payload(), home / "payload", ec) ? home : fs::path();
#elif defined(__linux__)
    return app::appimage();
#endif
    fs::path p = app::program();
    fs::path contents = p.parent_path().parent_path();
    if (p.parent_path().filename() != "MacOS" || contents.filename() != "Contents" ||
        contents.parent_path().extension() != ".app")
        return {};
    return contents.parent_path();
}

namespace
{

fs::path bundle() { return bundle_path(); }

#if defined(_WIN32)
// The program inside a staged Windows app (setup/releases.py: APP_PROGRAM_WINDOWS).
const char *releases_program() { return "DefJamRecompiled.exe"; }
#elif defined(__linux__)
// The AppImage inside a staged Linux app (setup/releases.py: APP_PROGRAM_LINUX).
const char *releases_program() { return "DefJamRecompiled.AppImage"; }
#endif

fs::path beside(const fs::path &bundle, const char *suffix)
{
    return bundle.parent_path() / ("." + bundle.filename().string() + suffix);
}

std::map<std::string, std::string> read_keys(const fs::path &path)
{
    std::map<std::string, std::string> keys;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) keys[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return keys;
}

// The first line of the engine's status file, read so that it may replace the file meanwhile.
std::string read_line(const fs::path &path)
{
    std::string text = os::read_shared(path);
    text = text.substr(0, text.find('\n'));
    if (!text.empty() && text.back() == '\r') text.pop_back();
    return text;
}

void take_answer(std::map<std::string, std::string> &keys)
{
    g_tag = g_version = g_url = "";
    if (keys.count("version") && app::newer(keys["version"], app::payload_version())) {
        g_tag = keys["tag"];
        g_version = keys["version"];
        g_url = keys["url"];
        fprintf(stderr, "[APP] update: v%s is available (%s)\n", g_version.c_str(), g_url.c_str());
    }
}

void start_check(bool manual = false)
{
    g_check = Check::Done;
    if (app::payload().empty()) { g_check_status = "This development copy has no update payload. Open the release page."; return; }
    if (!manual && !recomp_settings_get("launcher", "check_updates", 1)) {
        g_check_status = "Automatic checks are off"; return;
    }
    g_check_status = "Checking for updates...";
    std::error_code ec;
    fs::create_directories(cache_dir(), ec);
    g_check_file = cache_dir() / "app-update.txt";
    auto keys = read_keys(g_check_file);
    long long checked = keys.count("checked") ? atoll(keys["checked"].c_str()) : 0;
    if (!manual && keys["current"] == app::payload_version() && time(nullptr) - checked < k_check_seconds) {
        take_answer(keys);
        g_last_checked = checked;
        g_check_status = "Showing last successful check";
        return;
    }
    std::string error;
    fs::path result = g_check_file.string() + ".new";
    fs::remove(result, ec);
    if (!spawn_engine({"--check-app-update", result.string(), "--log", (app::log_dir() / "update-check.log").string()},
                      &g_check_process, &error)) {
        fprintf(stderr, "[APP] update check: %s\n", error.c_str());
        g_check_status = "Could not check for updates. You can still play.";
        return;
    }
    g_check = Check::Running;
}

void poll_check()
{
    if (g_check != Check::Running) return;
    int code = os::finished(g_check_process);
    if (code < 0) return;
    g_check = Check::Done;
    fs::path result = g_check_file.string() + ".new";
    std::error_code ec;
    if (code != 0) {
        fprintf(stderr, "[APP] update check failed (see update-check.log)\n");
        g_check_status = "Could not check for updates. You can still play.";
        fs::remove(result, ec);
        return;
    }
    auto keys = read_keys(result);
    if (keys["current"] != app::payload_version()) {
        g_check_status = "Could not read the update result. Try checking again.";
        fs::remove(result, ec); return;
    }
    {
        std::ofstream out(result, std::ios::app);
        out << "checked=" << (long long)time(nullptr) << "\n";
    }
    fs::rename(result, g_check_file, ec);
    take_answer(keys);
    g_last_checked = time(nullptr);
    g_check_status = g_version.empty() ? "Up to date" : "Update available";
}

void start_download()
{
    fs::path here = bundle();
    if (here.empty()) {
        g_stage = Stage::NotBundle;
        return;
    }
    std::error_code ec;
#if defined(_WIN32)
    fs::path folder = here;                             // the app's own folder holds the new copy
    g_staged = here / ".update";
#else
    fs::path folder = here.parent_path();
    g_staged = beside(here, ".update");
#endif
    fs::remove_all(g_staged, ec);                       // a download an earlier run left unfinished
    if (!os::writable(folder)) {
        g_error = "This app cannot be replaced where it is (" + folder.string() +
                  " is not writable). Download v" + g_version + " from the release page instead.";
        g_stage = Stage::Failed;
        return;
    }
    char stamp[32];
    time_t now = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    g_scratch = fs::temp_directory_path(ec) / ("DefJamRecompiled-update-" + std::to_string(os::process_id()) + "-" + stamp);
    fs::create_directories(g_scratch, ec);
    g_status = g_scratch / "status.txt";
    g_cancel = g_scratch / "cancel";
    fs::create_directories(app::log_dir(), ec);
    g_log = app::log_dir() / ("update-" + std::string(stamp) + ".log");
    g_line.clear();
    g_error.clear();
    g_cancelling = false;
    fprintf(stderr, "[APP] update: downloading %s into %s, log %s\n", g_tag.c_str(), g_staged.string().c_str(),
            g_log.string().c_str());
    if (!spawn_engine({"--stage-app", g_tag, "--stage-dir", g_staged.string(), "--status-file", g_status.string(),
                       "--cancel-file", g_cancel.string(), "--log", g_log.string()},
                      &g_stage_process, &g_error)) {
        g_stage = Stage::Failed;
        return;
    }
    g_started_ms = SDL_GetTicks();
    g_stage = Stage::Downloading;
}

// The verified bundle into this one's place. On success the new program is set to start when the
// window closes, and the window is asked to close.
void swap()
{
#if defined(_WIN32)
    // The program and the payload, each renamed aside and the new one renamed in; every step undone
    // in reverse when a later one fails.
    fs::path home = bundle(), program = app::program();
    std::vector<std::pair<fs::path, fs::path>> done;    // renames made, to undo
    auto undo = [&] {
        std::error_code back;
        for (auto it = done.rbegin(); it != done.rend(); ++it) fs::rename(it->second, it->first, back);
    };
    std::error_code ec;
    fs::remove(program.string() + ".old", ec);
    fs::remove_all(home / "payload.old", ec);
    const std::pair<fs::path, fs::path> steps[] = {
        {program, program.string() + ".old"},
        {home / "payload", home / "payload.old"},
        {g_staged / releases_program(), program},
        {g_staged / "payload", home / "payload"},
        {g_staged / "payload.sha256", home / "payload.sha256.new"},
    };
    for (const auto &step : steps) {
        // An antivirus scan of the freshly staged files can refuse a rename for a moment: retry.
        for (int attempt = 0; attempt < 40; ++attempt) {
            fs::rename(step.first, step.second, ec);
            if (!ec) break;
            Sleep(250);
        }
        if (ec) {
            fprintf(stderr, "[APP] could not rename %s to %s: %s\n", step.first.string().c_str(),
                    step.second.string().c_str(), ec.message().c_str());
            undo();
            g_error = "The new app could not be put in place (" + ec.message() + "). This version is still installed.";
            g_stage = Stage::Failed;
            return;
        }
        done.push_back(step);
    }
    // Which built-in payload payload\ now holds (app_install.cpp), so the new copy keeps it.
    fs::remove(home / "payload.sha256", ec);
    fs::rename(home / "payload.sha256.new", home / "payload.sha256", ec);
    fs::remove_all(g_staged, ec);
    fs::path here = home;
#elif defined(__linux__)
    // One file: the verified AppImage renamed over this one. Atomic on one filesystem, and this
    // program keeps running from its own mount of the old image.
    fs::path here = bundle();
    std::error_code ec;
    fs::rename(g_staged / releases_program(), here, ec);
    if (ec) {
        g_error = "The new app could not be put in place (" + ec.message() + "). This version is still installed.";
        g_stage = Stage::Failed;
        return;
    }
    fs::remove_all(g_staged, ec);
    app::note_updated_home(here, g_version);
#else
    fs::path here = bundle(), old = beside(here, ".old");
    std::error_code ec;
    fs::remove_all(old, ec);
    fs::rename(here, old, ec);
    if (ec) {
        g_error = "This app could not be moved aside (" + ec.message() + "). Nothing was changed.";
        g_stage = Stage::Failed;
        return;
    }
    fs::rename(g_staged, here, ec);
    if (ec) {
        std::error_code back;
        fs::rename(old, here, back);
        g_error = "The new app could not be put in place (" + ec.message() + "). This version is still installed.";
        g_stage = Stage::Failed;
        return;
    }
#endif
    fprintf(stderr, "[APP] update: v%s is in place at %s; starting it\n", g_version.c_str(), here.string().c_str());
    auto &next = app::relaunch();
#if defined(_WIN32)
    next = {app::program().string()};
#elif defined(__linux__)
    next = {here.string()};
#else
    next = {(here / "Contents/MacOS/defjam_app").string()};
#endif
    for (const std::string &a : app::arguments())
        if (a != "--updated") next.push_back(a);
    next.push_back("--updated");
    g_leave = true;
}

void poll_download()
{
    if (g_stage != Stage::Downloading) return;
    std::string line = read_line(g_status);
    if (!line.empty()) g_line = line;
    int code = os::finished(g_stage_process);
    if (code < 0) return;
    line = read_line(g_status);
    if (!line.empty()) g_line = line;
    std::error_code ec;
    fs::remove_all(g_scratch, ec);
    fprintf(stderr, "[APP] update: download ended, exit %d (%s)\n", code, g_line.c_str());
    if (code == 0) {
        swap();
        return;
    }
    fs::remove_all(g_staged, ec);
    g_error = code == 6 ? "The update was cancelled. Nothing was changed." : g_line.empty() ? "The update stopped." : g_line;
    g_stage = Stage::Failed;
}

std::string elapsed(unsigned long long ms)
{
    char text[32];
    snprintf(text, sizeof text, "%u:%02u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60));
    return text;
}

} // namespace

void update_cleanup()
{
    fs::path here = bundle();
    if (here.empty()) return;
    std::error_code ec;
#if defined(_WIN32)
    // The previous program may still be exiting for a moment after it started this one.
    fs::path old = app::program().string() + ".old";
    for (int tries = 0; tries < 30 && fs::exists(old, ec); tries++) {
        if (fs::remove(old, ec)) break;
        SDL_Delay(100);
    }
    if (fs::exists(here / "payload.old", ec)) {
        fs::remove_all(here / "payload.old", ec);
        fprintf(stderr, "[APP] update: removed the previous app\n");
    }
    fs::remove_all(here / ".update", ec);
    return;
#elif defined(__linux__)
    fs::remove_all(beside(here, ".update"), ec);    // a download an earlier run left unfinished
    return;
#endif
    if (fs::exists(beside(here, ".old"), ec)) {
        fs::remove_all(beside(here, ".old"), ec);
        fprintf(stderr, "[APP] update: removed the previous app\n");
    }
}

bool update_busy() { return g_stage_process.id != 0; }

void update_request_cancel()
{
    if (!g_stage_process.id || g_cancelling) return;
    std::ofstream(g_cancel) << "cancel";
    g_cancelling = true;
}

bool update_screen_draw(float width, float height, UiResult *result)
{
    if (g_check == Check::Idle) start_check();
    poll_check();
    poll_download();
    if (g_leave) {
        result->leave = true;                           // main starts the new app when the window closes
        page_begin(width, height, "UPDATED");
        ImGui::TextWrapped("Starting Def Jam Recompiled v%s...", g_version.c_str());
        page_end();
        return true;
    }
    switch (g_stage) {
    case Stage::None: return false;
    case Stage::Confirm:
        page_begin(width, height, "UPDATE THE APP");
        ImGui::TextWrapped("Def Jam Recompiled v%s is available; this app is v%s.", g_version.c_str(),
                           app::payload_version().c_str());
        ImGui::TextWrapped("The new app is downloaded from the project's GitHub release and checked against the "
                           "release's checksums before it replaces this one. Your game, saves and settings stay as "
                           "they are until the new game is ready. After restarting, the app builds game v%s locally. "
                           "This can take several minutes. You can cancel the build and keep playing the previous version.", g_version.c_str());
        ImGui::Spacing();
        page_actions();
        if (pcui::play_button("UPDATE APP AND GAME", ImVec2(ImGui::GetFontSize() * 15, ImGui::GetFrameHeight() * 1.8f), 24))
            start_download();
        ui_target("update.download");
        ImGui::SameLine();
        if (ImGui::Button("What's new")) pcui::open_path(g_url);
        ImGui::SameLine();
        if (ImGui::Button("Later") || back_pressed()) g_stage = Stage::None;
        ui_target("update.not-now");
        page_end();
        return true;
    case Stage::Downloading:
        page_begin(width, height, "UPDATING THE APP");
        ImGui::TextWrapped("%s", g_cancelling ? "Cancelling..." : g_line.empty() ? "Starting..." : g_line.c_str());
        ImGui::TextDisabled("%s elapsed", elapsed(SDL_GetTicks() - g_started_ms).c_str());
        ImGui::Spacing();
        page_actions();
        ImGui::BeginDisabled(g_cancelling);
        if (ImGui::Button("Cancel")) update_request_cancel();
        ui_target("update.cancel");
        ImGui::EndDisabled();
        page_end();
        return true;
    case Stage::Failed:
        page_begin(width, height, "UPDATE STOPPED");
        ImGui::TextWrapped("%s", g_error.c_str());
        ImGui::Spacing();
        page_actions();
        if (ImGui::Button("Back") || back_pressed()) g_stage = Stage::None;
        ui_target("update.back");
        if (!g_url.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Release page")) pcui::open_path(g_url);
        }
        if (!g_log.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Open log")) pcui::open_path(g_log.string(), true);
        }
        page_end();
        return true;
    case Stage::NotBundle:
        page_begin(width, height, "UPDATE THE APP");
        ImGui::TextWrapped("This copy of the app is not an installed application (a development build), so it "
                           "cannot replace itself. Get v%s from the release page.", g_version.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Release page")) pcui::open_path(g_url);
        ImGui::SameLine();
        page_actions();
        if (ImGui::Button("Back") || back_pressed()) g_stage = Stage::None;
        page_end();
        return true;
    }
    return false;
}

} // namespace app

// The installed game's version, which Home shows under its art (the app's own is in the header).
const char *pc_app_game_version()
{
    const app::Install &in = app::install();
    return in.installed && !in.version.empty() ? in.version.c_str() : DEFJAM_VERSION;
}

// One line on Home, above the launcher's own: a newer app to download, or this app's version of the
// game to build. Drawn inside the launcher's Home (pc_ui.cpp, DEFJAM_APP).
void pc_app_home_notice()
{
    using namespace app;
    ImGui::BeginDisabled(g_check == Check::Running);
    if (ImGui::Button("Check for updates")) start_check(true);
    ui_target("updates.check");
    ImGui::EndDisabled();
    if (app::payload().empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Releases")) pcui::open_path("https://github.com/vyanhursky/DefJamFFNY-recomp/releases");
    }
    if (!app::payload().empty() && ImGui::GetContentRegionAvail().x > 700 * ImGui::GetStyle().FontScaleMain) ImGui::SameLine();
    ImGui::TextWrapped("%s", g_check_status.c_str());
    if (g_last_checked && g_check != Check::Running) {
        time_t checked = (time_t)g_last_checked;
        char date[64]; strftime(date, sizeof date, "%b %d, %H:%M", localtime(&checked));
        ImGui::SameLine(); ImGui::TextDisabled("(%s)", date);
    }
    const Install &in = install();
    // Prefer a newer online target to rebuilding an intermediate local payload.
    if (!g_version.empty()) {
        if (ImGui::Button(("Update to v" + g_version).c_str())) g_stage = Stage::Confirm;
        ui_target("home.update");
        ImGui::SameLine();
        if (ImGui::SmallButton("What's new")) pcui::open_path(g_url);
    } else if (in.installed && game_behind(in)) {
        bool other = other_build(in);
        ImGui::TextWrapped("Play starts %s. Finish updating to %s when ready.",
                           build_label(in.version, other ? in.source_commit : "").c_str(),
                           build_label(payload_version(), other ? payload_commit() : "").c_str());
        if (ImGui::SmallButton("Finish game update")) start_update_build();
        ui_target("home.build-update");
    }
    ImGui::Separator();
}
