// The app's own screens over the launcher: the first run's setup, Maintenance (Repair, Clean
// Rebuild) and an operation's progress.
//
// The work is setup/engine.py's, run from the app's setup payload with that payload's Python, as
// the setup wizards run it: the same choices on its command line, its one-line progress in a status
// file the screen reads every frame, and a cancel file it looks for between steps. Its exit code
// says how it ended (0 done, 2 a choice it refuses, 3 the build tools are missing, 4 failed,
// 5 busy, 6 cancelled, and on Windows 3010 when the Build Tools it installed need a restart) and its
// last status line why. On Windows the app first asks the engine whether Microsoft's C++ Build
// Tools are there (--check-toolchain), and when they are not it offers to install them with the
// setup (--install-prerequisites; Windows asks the player for permission). The previous build stays playable until the
// new one is activated, so a failed or cancelled Repair leaves the game as it was.

#define _CRT_SECURE_NO_WARNINGS
#include "app_setup_handoff.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <sys/stat.h>
#include "pc_settings.h"
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include "imgui.h"

#include "app.h"
#include "app_os.h"
#include "pc_ui_actions.h"
#include "pc_ui_internal.h"
#include "pc_ui_theme.h"

namespace fs = std::filesystem;

namespace app
{
namespace
{

enum class Screen
{
    None,           // the launcher's own Home and Settings
    FirstRun,       // nothing installed yet: choose the dump and the folders
    Maintenance,    // Repair or Clean Rebuild of the install
    Running,        // an operation's progress
    Finished,       // how it ended
    UpdateReady,    // the game is older than this app: build the version it carries
    Uninstall       // remove the game, its Steam shortcut, the app and (if chosen) its data
};

// The steps the engine reports, in order, by the start of its status lines.
struct Stage
{
    const char *name;
    const char *prefixes[4];
};
const Stage k_stages[] = {
    {"Prepare the setup files", {"Preparing verified release source", nullptr}},
    {"Check your game files", {"Verifying and preparing your dump", "Verified installed dump", "Verified extraction"}},
    {"Check the build tools", {"Checking ", "Installing Microsoft Build Tools", "Preparing the build container",
                               "Building with"}},
    {"Prepare game code", {"Analysis ", nullptr}},
    {"Prepare the build", {"Lift ", nullptr}},
    {"Build the game", {"Build ", "Building in the build container", nullptr}},
    {"Upscaled Original Textures", {"Upscaling HD textures", "HD textures ready", nullptr}},
    {"Finish setup", {"Activating the verified installation", "Setup complete", "Installation is ready"}},
};
const int k_stage_count = (int)(sizeof k_stages / sizeof k_stages[0]);

struct Run
{
    os::Process process;
    std::string operation, title;
    fs::path scratch, status, cancel, log;
    unsigned long long started_ms = 0, ended_ms = 0;
    std::string line;                   // the last status line
    uint64_t log_read = 0;              // how much of setup's log the stage list has read
    int stage = -1;                     // the furthest stage reported
    bool reused[k_stage_count] = {};
    bool hd = false;
    bool cancelling = false;
    int exit_code = -1;
};

Screen g_screen = Screen::None;
bool g_started = false;
Run g_run;
char g_dump[2048], g_root[2048], g_data[2048];
bool g_hd = false;
int g_setup_step = 0, g_experience = 1;
bool g_apply_experience = false;
bool g_confirm_cancel = false;
std::string g_form_error;

// What the last operation was started with, to start it again with the Build Tools (Windows).
struct Request
{
    std::string operation, title, root, data, dump;
    bool hd = false;
} g_request;
bool g_resume_setup = false, g_handoff_started = false, g_resume_tools = false;

// Windows: whether Microsoft's C++ Build Tools are missing (asked of the engine when the app starts),
// and whether setup should install them first.
os::Process g_tools_check;
bool g_tools_missing = false, g_install_tools = true;

// A folder or file chosen in the system's dialog arrives on another thread.
std::mutex g_pick_lock;
std::string g_picked;
char *g_pick_into = nullptr;

void SDLCALL picked(void *into, const char *const *files, int)
{
    std::lock_guard<std::mutex> hold(g_pick_lock);
    if (files && files[0]) {
        g_picked = files[0];
        g_pick_into = (char *)into;
    }
}

void choose(char *into, bool folder)
{
    const char *start = into[0] ? into : nullptr;
    if (folder)
        SDL_ShowOpenFolderDialog(picked, into, nullptr, start, false);
    else {
        static const SDL_DialogFileFilter images[] = {{"Disc images", "iso;xiso"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(picked, into, nullptr, images, 2, start, false);
    }
}

void take_pick()
{
    std::lock_guard<std::mutex> hold(g_pick_lock);
    if (g_pick_into) {
        snprintf(g_pick_into, sizeof g_dump, "%s", g_picked.c_str());
        g_pick_into = nullptr;
    }
}

std::string read_text(const fs::path &path)
{
    std::string s = os::read_shared(path);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

fs::path python_in(const fs::path &payload)
{
    std::error_code ec;
    if (fs::is_regular_file(payload / "python/python.exe", ec)) return payload / "python/python.exe";
    if (fs::is_regular_file(payload / "python/bin/python3", ec)) return payload / "python/bin/python3";
    // The Linux payload keeps one real executable, python3.N.
    for (const auto &entry : fs::directory_iterator(payload / "python/bin", ec)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("python3.", 0) == 0 && name.find('-') == std::string::npos) return entry.path();
    }
    return {};
}

} // namespace

bool spawn_engine(const std::vector<std::string> &engine_args, os::Process *process, std::string *error)
{
    fs::path payload = app::payload();
    fs::path python = python_in(payload);
    if (payload.empty() || python.empty()) {
        *error = "This copy of the app has no setup files. Start it with --payload <setup payload folder>.";
        return false;
    }
    std::vector<std::string> args = {python.string(), "-B", (payload / "engine/engine.py").string(),
                                     "--payload", payload.string()};
    args.insert(args.end(), engine_args.begin(), engine_args.end());

    // As the macOS wizard runs it: a plain environment, so nothing from a package manager or a
    // development shell changes the build. Elsewhere the engine may need the session's own
    // (the build container's runtime folder, the display for Steam; on Windows the user's folders).
    std::vector<std::string> env_text;
    const std::vector<std::string> *env = nullptr;
#if defined(__APPLE__)
    const char *home = getenv("HOME");
    const char *tmp = getenv("TMPDIR");
    env_text = {std::string("HOME=") + (home ? home : ""), "PATH=/usr/bin:/bin:/usr/sbin:/sbin",
                std::string("TMPDIR=") + (tmp ? tmp : "/tmp"), "LANG=en_US.UTF-8"};
    env = &env_text;
#endif
    std::string why;
    if (!os::start(args, env, process, &why)) {
        *error = "Setup could not start: " + why;
        return false;
    }
    return true;
}

namespace
{

// Start the engine for `operation`. Returns false with g_form_error set when it could not start.
bool start(const std::string &operation, const std::string &title, const std::string &root,
           const std::string &data, const std::string &dump, bool hd, bool install_tools = false)
{
    if (app::payload().empty()) {
        g_form_error = "This copy of the app has no setup files. Start it with --payload <setup payload folder>.";
        return false;
    }
    // Old Windows wrappers wait for this app and keep their own .exe mapped. Resume the
    // exact request in a direct app instance only after both this instance and its wrapper exit.
    std::vector<std::string> next = {app::program().string(), "--install", root,
        "--payload", app::payload().string(), "--launcher", "--resume-setup",
        operation, title, root, data, dump, hd ? "1" : "0",
        (install_tools || (g_tools_missing && g_install_tools)) ? "1" : "0"};
    auto handoff = os::setup_handoff(root, next, &g_form_error);
    if (handoff == os::SetupHandoff::Failed) return false;
    if (handoff == os::SetupHandoff::Started) {
        g_handoff_started = true;
        return true;
    }
    g_request = {operation, title, root, data, dump, hd};
    g_run = Run();
    g_run.operation = operation;
    g_run.title = title;
    g_run.hd = hd;
    char stamp[32];
    time_t now = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    std::error_code ec;
    g_run.scratch = fs::temp_directory_path(ec) / ("DefJamRecompiled-setup-" + std::to_string(os::process_id()) + "-" + stamp);
    fs::create_directories(g_run.scratch, ec);
    g_run.status = g_run.scratch / "status.txt";
    g_run.cancel = g_run.scratch / "cancel";
    fs::create_directories(app::log_dir(), ec);
    g_run.log = app::log_dir() / ("setup-" + std::string(stamp) + ".log");

    std::vector<std::string> args = {"--bundled-only", "--operation", operation, "--install-dir", root,
                                     "--data-dir", data, "--status-file", g_run.status.string(),
                                     "--cancel-file", g_run.cancel.string(), "--log", g_run.log.string()};
    if (!dump.empty()) args.insert(args.end(), {"--dump", dump});
    if (hd) args.push_back("--hd-textures");
    // The app the player downloaded is the only copy: setup makes no Applications, Desktop or menu
    // shortcut (it would otherwise make them again after every operation). The player moves the
    // app wherever they like.
    args.push_back("--no-shortcuts");
#if defined(_WIN32)
    // Microsoft's Build Tools, when the check found them missing and the player kept the box ticked
    // (or chose Install Build Tools after setup stopped for them).
    if (install_tools || (g_tools_missing && g_install_tools)) args.push_back("--install-prerequisites");
#else
    (void)install_tools;
#endif
    fprintf(stderr, "[APP] setup: %s (%s), log %s\n", operation.c_str(), root.c_str(), g_run.log.string().c_str());
    fflush(stderr);
    // Setup takes play.lock itself, to keep the game from starting while it works; finish() takes it
    // back for the app.
    app::unlock_install();
    if (!spawn_engine(args, &g_run.process, &g_form_error)) {
        app::lock_install();
        return false;
    }
    g_run.started_ms = SDL_GetTicks();
    g_screen = Screen::Running;
    return true;
}

// A progress line against the stage list.
void note_stage(const std::string &line)
{
    for (int s = 0; s < k_stage_count; s++)
        for (const char *prefix : k_stages[s].prefixes)
            if (prefix && line.rfind(prefix, 0) == 0) {
                if (s > g_run.stage) g_run.stage = s;
                if (line.find("reused") != std::string::npos) g_run.reused[s] = true;
            }
}

// The status file holds only the newest line, and a quick step ("Lift already verified; reused")
// is replaced before a frame reads it; setup also appends every line to its log, which the stage
// list reads as it grows. The status file is what the screen shows.
void note_status(const std::string &line)
{
    std::string chunk = os::read_shared(g_run.log, g_run.log_read, 1 << 20);
    size_t used = 0, end;
    while ((end = chunk.find('\n', used)) != std::string::npos) {   // a line still being written waits
        std::string text = chunk.substr(used, end - used);
        if (!text.empty() && text.back() == '\r') text.pop_back();
        note_stage(text);
        used = end + 1;
    }
    g_run.log_read += used;
    if (line.empty() || line == g_run.line) return;
    g_run.line = line;
    note_stage(line);
}

void finish(int code)
{
    g_run.exit_code = code;
    g_run.ended_ms = SDL_GetTicks();
    note_status(read_text(g_run.status));
    std::error_code ec;
    fs::remove_all(g_run.scratch, ec);
    fprintf(stderr, "[APP] setup %s ended: exit %d (%s)\n", g_run.operation.c_str(), code, g_run.line.c_str());
    // Back to the install as it now is: the new build, or the old one after a failure.
    std::string why;
    if (app::lock_install() && app::load_install(&why))
        fprintf(stderr, "[APP] install %s, version %s\n", app::install().root.string().c_str(), app::install().version.c_str());
    else if (!why.empty())
        fprintf(stderr, "[APP] %s\n", why.c_str());
    fflush(stderr);
    if (code == 0) g_tools_missing = false;                 // setup found or installed them
    if (code == 0 && g_run.operation == "install" && app::install().installed) {
        if (g_apply_experience) {
            int saved = pc_experience_apply(g_experience);
            if (saved) g_run.line += " Experience could not be saved; choose it in Settings.";
            g_apply_experience = false;
        }
        app::remember_install(app::install().root);
    }         // found again at the next start
    g_screen = Screen::Finished;
}

void poll()
{
    if (!g_run.process.id) return;
    note_status(read_text(g_run.status));
    int code = os::finished(g_run.process);
    if (code >= 0) finish(code);
}

// Windows: ask the engine whether the Build Tools are there (it answers in about a second), and
// read the answer when it comes. Elsewhere setup checks its own tools as it goes.
void start_tools_check()
{
#if defined(_WIN32)
    fs::path python = python_in(app::payload());
    if (python.empty()) return;
    std::string error;
    if (!os::start({python.string(), "-B", (app::payload() / "engine/engine.py").string(), "--payload",
                    app::payload().string(), "--check-toolchain"}, nullptr, &g_tools_check, &error))
        fprintf(stderr, "[APP] Build Tools check: %s\n", error.c_str());
#endif
}

void poll_tools_check()
{
    int code = os::finished(g_tools_check);
    if (code < 0) return;
    g_tools_missing = code == 3;
    fprintf(stderr, "[APP] Microsoft C++ Build Tools: %s\n", g_tools_missing ? "missing" : "installed");
}

// The Build Tools choice, when they are missing (Windows).
void tools_row()
{
    if (!g_tools_missing) return;
    ImGui::Checkbox("Install Microsoft's C++ Build Tools first (needed to build the game; a few GB from "
                    "Microsoft, and Windows asks for your permission)", &g_install_tools);
    ui_target("setup.tools");
    if (!g_install_tools)
        ImGui::TextDisabled("Without them setup stops before it builds the game.");
}

std::string elapsed(unsigned long long ms)
{
    char text[32];
    snprintf(text, sizeof text, "%u:%02u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60));
    return text;
}

} // namespace

static bool g_page_body = false;
bool back_pressed()
{
    return !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
           !ImGui::GetIO().WantTextInput &&
           (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false));
}
void page_actions()
{
    if (!g_page_body) return;
    ImGui::EndChild();
    g_page_body = false;
    ImGui::Separator();
}
void page_end()
{
    page_actions();
    ImGui::End();
}
void page_begin(float width, float height, const char *heading)
{
    float scale = ImGui::GetStyle().FontScaleMain;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##app", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("DEF JAM RECOMPILED");
    ImGui::SameLine();
    ImGui::TextDisabled("/ v%s", DEFJAM_VERSION);
    ImGui::Separator();
    pcui::heading(heading, 24 * scale);
    ImGui::Spacing();
    float actions = std::min(height * .30f, ImGui::GetFrameHeightWithSpacing() * 2.6f);
    ImGui::BeginChild("app body", ImVec2(0, -actions), ImGuiChildFlags_None);
    g_page_body = true;
}

namespace
{

void path_row(const char *label, const char *id, char *value, bool folder, bool image_too)
{
    ImGui::PushID(id);
    ImGui::TextWrapped("%s", label);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##path", value, sizeof g_dump);
    ui_target(id);
    if (image_too) {
        if (ImGui::Button("Choose disc image")) choose(value, false);
        if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize("Choose extracted folder").x + 220) ImGui::SameLine();
    }
    if (ImGui::Button(image_too ? "Choose extracted folder" : "Change folder")) choose(value, folder);
    ImGui::PopID();
}

struct SpaceReview { bool valid = false; std::string text; };
std::future<SpaceReview> g_space_job;
SpaceReview g_space;
std::string g_space_key, g_space_running_key;
fs::path existing_parent(fs::path p) {
    std::error_code ec;
    while (!p.empty() && !fs::exists(p, ec)) p = p.parent_path();
    return p;
}
bool contains_path(const fs::path &a, const fs::path &b) {
    auto i = a.begin(), j = b.begin();
    for (; i != a.end() && j != b.end() && *i == *j; ++i, ++j) {}
    return i == a.end();
}
SpaceReview review_space(std::string dump, std::string root, std::string data, bool hd)
{
    try {
        auto source = fs::weakly_canonical(fs::u8path(dump));
        auto install = fs::weakly_canonical(fs::u8path(root));
        auto storage = fs::weakly_canonical(fs::u8path(data));
        if (contains_path(install, storage) || contains_path(storage, install) ||
            contains_path(source, install) || contains_path(install, source) ||
            contains_path(source, storage) || contains_path(storage, source))
            return {false, "Choose separate, non-nested folders for your game copy, installation and game data."};
        if (!fs::exists(source)) return {false, "The selected game copy was not found."};
        uintmax_t bytes = 0;
        if (!fs::is_regular_file(storage / "extraction-state.json")) {
            if (fs::is_directory(source)) {
                if (!fs::is_regular_file(source / "default.xbe")) return {false, "Choose the extracted Xbox folder containing default.xbe."};
                for (const auto &e : fs::recursive_directory_iterator(source)) if (e.is_regular_file()) bytes += e.file_size();
            } else bytes = fs::file_size(source);
        }
        auto a = existing_parent(install), b = existing_parent(storage);
        auto av = fs::space(a), bv = fs::space(b);
#if defined(_WIN32)
        bool same = _wcsicmp(a.root_name().c_str(), b.root_name().c_str()) == 0;
#else
        struct stat sa{}, sb{};
        bool same = stat(a.c_str(), &sa) == 0 && stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev;
#endif
        uintmax_t gib = 1024ull * 1024 * 1024;
        uintmax_t build = 12 * gib, content = bytes + gib + (hd ? 15 * gib : 0);
        char message[512];
        if (same) snprintf(message, sizeof message, "Setup working space: %.1f GiB required; %.1f GiB available on the destination drive. Includes the game-copy space and temporary build files%s.",
                           double(build + content)/gib, double(av.available)/gib, hd ? " and optional HD generation" : "");
        else snprintf(message, sizeof message, "Install drive: 12 GiB required, %.1f GiB available. Data drive: %.1f GiB required, %.1f GiB available. Includes temporary working space.",
                      double(av.available)/gib, double(content)/gib, double(bv.available)/gib);
        bool fits = same ? av.available >= build + content : av.available >= build && bv.available >= content;
        return {fits, std::string(fits ? "" : "Not enough free space. ") + message};
    } catch (const std::exception &e) { return {false, std::string("Could not check locations: ") + e.what()}; }
}
void space_review()
{
    std::string key = std::string(g_dump) + "\n" + g_root + "\n" + g_data + (g_hd ? "1" : "0");
    if (g_space_job.valid() && g_space_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto answer = g_space_job.get();
        if (g_space_running_key == key) { g_space = answer; g_space_key = key; }
    }
    if (g_space_key != key && !g_space_job.valid()) {
        g_space = {}; g_space_running_key = key;
        g_space_job = std::async(std::launch::async, review_space, std::string(g_dump), std::string(g_root), std::string(g_data), g_hd);
    }
    ImGui::TextWrapped("%s", g_space_key == key ? g_space.text.c_str() : "Checking selected locations and free space...");
    if (!g_space_job.valid() && ImGui::SmallButton("Recheck space")) { g_space_key.clear(); g_space.valid = false; }
}

// An installation already in the folder chosen on the first run (an older setup's, say): the app
// takes it over instead of installing there, which setup would refuse, and offers to update it
// when it is older than this app.
struct Found
{
    std::string root, version;      // the folder last looked at, and the version installed there
} g_found;

void probe_root()
{
    if (g_found.root == g_root) return;
    g_found.root = g_root;
    g_found.version = g_root[0] ? app::receipt_version(g_root) : "";
}

// Take over the installation in g_root (and remember it for the next start), then update it when
// it is older than this app, or go to the launcher.
void adopt_found()
{
    g_form_error.clear();
    app::unlock_install();
    app::install().root = g_root;
    if (!app::lock_install()) {
        g_form_error = "The game in this folder is running. Close it, then try again.";
        return;
    }
    std::string why;
    bool loaded = app::load_install(&why);
    app::remember_install(g_root);
    fprintf(stderr, "[APP] using the installation in %s (v%s) %s\n", g_root, app::install().version.c_str(),
            why.c_str());
    if (!loaded) {
        g_form_error = why;
        g_screen = Screen::Maintenance;
    } else if (app::game_behind(app::install())) {
        start_update_build();
    } else {
        g_screen = Screen::None;
    }
}

// The installed game and what this app builds: a build is named only when the versions match.
std::string installed_label()
{
    const Install &in = app::install();
    return app::build_label(in.version, app::other_build(in) ? in.source_commit : "");
}
std::string target_label()
{
    return app::build_label(app::payload_version(), app::other_build(app::install()) ? app::payload_commit() : "");
}

void first_run_page(float width, float height, UiResult *result)
{
    page_begin(width, height, "SET UP YOUR GAME");
    ImGui::Text("Step %d of 3", g_setup_step + 1);
    probe_root();
    bool found = !g_found.version.empty();
    bool older = found && app::newer(app::payload_version(), g_found.version);
    if (g_setup_step == 0) {
        ImGui::TextWrapped("Choose your USA Xbox copy of Def Jam: Fight for NY. Setup builds the PC game locally; your game files stay on this computer.");
        path_row("Your game copy", "setup.dump", g_dump, true, true);
        if (ImGui::CollapsingHeader("Use an existing installation"))
            path_row("Installation folder", "setup.install", g_root, true, false);
        if (found) ImGui::TextWrapped("Found v%s. Your current settings, saves and mods will be kept.", g_found.version.c_str());
        ImGui::TextWrapped("A system file picker opens when choosing files. On Steam Deck you may need Desktop Mode for this step.");
    } else if (g_setup_step == 1) {
        ImGui::RadioButton("Modern - recommended", &g_experience, 1); ui_target("setup.modern");
        ImGui::TextWrapped("Widescreen and sharper 2x graphics, with the game's original visual style. Upscaled Original Textures and downloadable texture packs are optional.");
        ImGui::Spacing();
        ImGui::RadioButton("Original", &g_experience, 0); ui_target("setup.original");
        ImGui::TextWrapped("Original 4:3 proportions, 480p rendering and original textures, with console-style graphics effects.");
        ImGui::Spacing();
        ImGui::TextWrapped("You can change experience or individual settings later. Both experiences keep the same game speed and content.");
    } else {
        ImGui::Text("Experience: %s", g_experience ? "Modern" : "Original");
        if (g_experience) {
            ImGui::Checkbox("Upscaled Original Textures (optional)", &g_hd); ui_target("setup.hd");
            ImGui::TextWrapped("Generates higher-resolution versions of the original textures from your game copy on this PC. Adds several minutes and 15 GiB of working space. You can do this later in Mods.");
        } else g_hd = false;
        ImGui::TextWrapped("Downloadable texture packs and language mods are available in Mods after setup. No optional downloads are started automatically.");
        if (ImGui::CollapsingHeader("Change locations")) {
            path_row("Installation location", "setup.install", g_root, true, false);
            path_row("Game data and saves location", "setup.data", g_data, true, false);
        }
        ImGui::TextWrapped("Game: %s\nData and saves: %s", g_root, g_data);
        space_review();
        tools_row();
        ImGui::TextWrapped("Setup may take several minutes. It checks your copy before building; details and progress stay visible.");
    }
    if (!g_form_error.empty()) ImGui::TextWrapped("%s", g_form_error.c_str());
    page_actions();
    if (g_setup_step == 2 && !g_space.valid)
        ImGui::TextWrapped("%s", g_space_job.valid() ? "Checking space before setup..." : "Check the location / free-space message above before starting.");
    bool next = g_setup_step < 2;
    bool ready = found || (g_dump[0] && g_root[0] && g_data[0] && (next || (!g_space_job.valid() && g_space.valid)));
    ImGui::BeginDisabled(!ready);
    const char *label = found ? (older ? "Update existing game" : "Use existing game") : next ? "Continue" : "Set up game";
    if (ImGui::Button(label)) {
        if (found) adopt_found();
        else if (next) ++g_setup_step;
        else {
            g_form_error.clear(); g_apply_experience = true;
            app::install().root = g_root;
            start("install", "SETTING UP YOUR GAME", g_root, g_data, g_dump, g_hd);
        }
    }
    ui_target(!next && !found ? "setup.start" : found ? "setup.use" : "setup.next");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (g_setup_step > 0) {
        if (ImGui::Button("Back") || back_pressed()) --g_setup_step;
        ui_target("setup.back"); ImGui::SameLine();
    }
    if (ImGui::Button("Quit")) result->leave = true;
    ui_target("Quit");
    page_end();
}

void maintenance_page(float width, float height, UiResult *result)
{
    const Install &in = app::install();
    page_begin(width, height, "REPAIR AND RECOVERY");
    ImGui::TextWrapped("Installed: v%s in %s", in.version.c_str(), in.root.string().c_str());
    ImGui::TextWrapped("Data: %s", in.data.string().c_str());
    ImGui::Spacing();
    ImGui::Checkbox("Generate Upscaled Original Textures", &g_hd);
    tools_row();
    ImGui::Spacing();
    if (!g_form_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_form_error.c_str());
    // An older game: Repair and Clean Rebuild work on the version this app builds, so the update
    // comes first (setup refuses them across versions).
    bool older = in.installed && app::game_behind(in);
    if (older) {
        ImGui::SeparatorText("Update");
        ImGui::TextWrapped("Your game is %s; this app builds %s. Update builds the game again from your dump "
                           "with the new version, in about three to ten minutes. Your saves, settings and mods "
                           "are kept, and %s stays playable until the new build is ready.",
                           installed_label().c_str(), target_label().c_str(), installed_label().c_str());
        if (ImGui::Button(("Update to " + target_label()).c_str())) start_update_build();
        ui_target("setup.update");
    }
    ImGui::BeginDisabled(older);
    ImGui::SeparatorText("Repair");
    ImGui::TextWrapped("Checks every file of this version and rebuilds only what is missing or changed. Your "
                       "saves and settings are kept.");
    if (ImGui::Button("Repair")) {
        g_form_error.clear();
        start("repair", "Repairing", in.root.string(), in.data.string(), "", g_hd);
    }
    ui_target("setup.repair");
    ImGui::SeparatorText("Rebuild game");
    ImGui::TextWrapped("Builds this version again from nothing: analysis, translation and compile. About ten "
                       "minutes. The current build stays playable until the new one is ready.");
    if (ImGui::Button("Rebuild game")) {
        g_form_error.clear();
        start("rebuild", "Rebuilding", in.root.string(), in.data.string(), "", g_hd);
    }
    ui_target("setup.rebuild");
    ImGui::EndDisabled();
    if (older)
        ImGui::TextDisabled("Repair and Clean Rebuild work on v%s, the version this app builds: update first.",
                            app::payload_version().c_str());
#if defined(_WIN32) || defined(__linux__)
    if (in.installed) steam_section();
#endif
    ImGui::Spacing();
    page_actions();
    if (in.installed) {
        if (ImGui::Button("< Back to launcher") || back_pressed()) {
            g_form_error.clear();
            g_screen = Screen::None;
        }
        ui_target("Back");
    } else if (ImGui::Button("Quit")) {
        result->leave = true;
    }
    page_end();
}

void update_ready_page(float width, float height)
{
    const Install &in = app::install();
    page_begin(width, height, "FINISH YOUR UPDATE");
    if (app::just_updated())
        ImGui::TextWrapped("The app is updated to %s. The game update is still to come.", target_label().c_str());
    ImGui::TextWrapped("Your game is %s. Update it to %s now: it is built again from your dump with the new "
                       "version, in about three to ten minutes. Your saves, settings and mods are kept.",
                       installed_label().c_str(), target_label().c_str());
    ImGui::TextWrapped("Later opens the launcher, where Play starts %s and Check for updates offers the update "
                       "again.", installed_label().c_str());
    tools_row();
    ImGui::Spacing();
    if (!g_form_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_form_error.c_str());
    page_actions();
    if (pcui::play_button("UPDATE GAME", ImVec2(ImGui::GetFontSize() * 12, ImGui::GetFrameHeight() * 1.8f), 26))
        start_update_build();
    ui_target("update.build");
    ImGui::SameLine();
    if (ImGui::Button("Later") || back_pressed()) g_screen = Screen::None;
    ui_target("update.later");
    page_end();
}

// ---- Uninstall (Advanced > Maintenance, last on the page) ----------------------------------------
// The work runs on its own thread, one step after another: the Steam shortcut this app added
// (Windows), a copy of the saves and settings when the data folder goes too, setup's own uninstall
// (the build, its receipt, the launcher and the shortcuts an older setup made), then the data folder
// and the app's records. Nothing is deleted when the saves could not be copied exactly. A running
// program's folder cannot be removed, so on Windows a small script removes the app and its logs
// once the window has closed.
struct Removal
{
    bool data = false;                      // also delete the data folder
    std::atomic<int> state{0};              // 0 not started, 1 working, 2 done, 3 stopped
    std::mutex lock;
    std::string step, error, notes;         // under lock
    bool saves_copied = false, settings_copied = false;
    fs::path backup;                        // where the saves and settings are copied
} g_removal;

void removal_step(const std::string &text)
{
    std::lock_guard<std::mutex> hold(g_removal.lock);
    g_removal.step = text;
    fprintf(stderr, "[APP] uninstall: %s\n", text.c_str());
    fflush(stderr);
}

void removal_stop(const std::string &text)
{
    {
        std::lock_guard<std::mutex> hold(g_removal.lock);
        g_removal.error = text;
    }
    fprintf(stderr, "[APP] uninstall stopped: %s\n", text.c_str());
    fflush(stderr);
    g_removal.state = 3;
}

// How many files are under `dir`, and how many bytes they hold.
std::pair<uintmax_t, uintmax_t> tally(const fs::path &dir)
{
    uintmax_t files = 0, bytes = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec))
        if (it->is_regular_file(ec)) {
            files++;
            bytes += it->file_size(ec);
        }
    return {files, bytes};
}

// "Def Jam Recompiled saves <date time>" in the player's Documents folder.
fs::path backup_folder()
{
    fs::path base;
    if (const char *documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) base = documents;
    if (base.empty()) base = app::os::user_base();
    char stamp[32];
    time_t now = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M", localtime(&now));
    return base / (std::string("Def Jam Recompiled saves ") + stamp);
}

// Only a data folder setup made (it keeps extraction-state.json there) is deleted, and never a
// drive, the home folder or a folder that holds the install.
bool removable_data(const fs::path &data, const fs::path &root, std::string *why)
{
    std::error_code ec;
    fs::path d = fs::weakly_canonical(data, ec), r = fs::weakly_canonical(root, ec);
    fs::path home = fs::weakly_canonical(app::os::user_base(), ec);
    bool holds_install = false;
    for (fs::path p = r; !p.empty() && p != p.root_path(); p = p.parent_path())
        if (p == d) holds_install = true;
    if (d.empty() || d == d.root_path() || d == home || d == home.parent_path() || holds_install)
        *why = "The data folder is not one setup made on its own, so it is left as it is.";
    else if (!fs::is_regular_file(d / "extraction-state.json", ec))
        *why = "The data folder has no extraction-state.json, so it was not made by setup and is left as it is.";
    return why->empty();
}

bool same_file_contents(const fs::path &a, const fs::path &b)
{
    std::ifstream first(a, std::ios::binary), second(b, std::ios::binary);
    if (!first || !second) return false;
    char x[65536], y[65536];
    do {
        first.read(x, sizeof x); second.read(y, sizeof y);
        if (first.bad() || second.bad() || first.gcount() != second.gcount() ||
            memcmp(x, y, (size_t)first.gcount())) return false;
    } while (first.gcount());
    return true;
}
bool same_save_tree(const fs::path &a, const fs::path &b)
{
    try {
        if (tally(a) != tally(b)) return false;
        for (auto &entry : fs::recursive_directory_iterator(a)) {
            if (entry.is_symlink()) return false; // Never promise a backup of an external target.
            if (entry.is_regular_file() && !same_file_contents(entry.path(), b / fs::relative(entry.path(), a))) return false;
        }
        return true;
    } catch (...) { return false; }
}

void removal_work(fs::path root, fs::path data, bool with_data, fs::path backup)
{
    std::error_code ec;
    std::string message;
#if defined(_WIN32) || defined(__linux__)
    removal_step("Removing it from Steam");
    if (!app::steam_remove_now(&message))
        return removal_stop("The Steam shortcut could not be removed" + (message.empty() ? std::string() : ": " + message) +
                            ". Nothing else was changed.");
#endif
    if (with_data) {
        fs::path save = data / "save";
        bool has_saves = fs::is_directory(save, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) return removal_stop("Could not inspect your saves. Your data was not deleted.");
        ec.clear();
        if (has_saves) {
            removal_step("Copying your saves to " + backup.string());
            fs::create_directories(backup, ec);
            fs::copy(save, backup / "save", fs::copy_options::recursive, ec);
            if (ec)
                return removal_stop("Your saves could not be copied (" + ec.message() + "). Your game and data were not deleted.");
            if (!same_save_tree(save, backup / "save"))
                return removal_stop("The copy of your saves does not match them. Your game and data were not deleted.");
            g_removal.saves_copied = true;
        }
        bool has_settings = fs::is_regular_file(data / "settings.ini", ec);
        if (ec && ec != std::errc::no_such_file_or_directory) return removal_stop("Could not inspect your settings. Your data was not deleted.");
        ec.clear();
        if (has_settings) {
            fs::create_directories(backup, ec);
            fs::copy_file(data / "settings.ini", backup / "settings.ini", fs::copy_options::overwrite_existing, ec);
            if (ec) return removal_stop("Your settings could not be copied (" + ec.message() + "). Your data was not deleted.");
            if (!same_file_contents(data / "settings.ini", backup / "settings.ini")) return removal_stop("The settings backup could not be verified. Your data was not deleted.");
            g_removal.settings_copied = true;
        }
    }
    removal_step("Uninstalling the game");
    char stamp[32];
    time_t now = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    std::string error;
    os::Process process;
    if (!spawn_engine({"--uninstall", "--install-dir", root.string(), "--log",
                       (app::log_dir() / ("uninstall-" + std::string(stamp) + ".log")).string()},
                      &process, &error))
        return removal_stop(error);
    if (int code = os::wait(process); code != 0)
        return removal_stop("Setup's uninstall stopped (exit " + std::to_string(code) + "); its log is in " +
                            app::log_dir().string() + ".");
    // What the install folder still holds: the locks and records setup and the app keep there.
    for (const char *name : {"play.lock", "setup.lock", "setup-owner.json", "app-location.txt"})
        fs::remove(root / name, ec);
    fs::remove(root, ec);                                       // only when it is empty now
    if (with_data) {
        removal_step("Deleting the data folder");
        fs::remove_all(data, ec);
        if (ec) {
            std::lock_guard<std::mutex> hold(g_removal.lock);
            g_removal.notes = "Some of the data folder could not be deleted (" + ec.message() + "): " + data.string();
        }
    }
    app::forget_install();
#if defined(_WIN32)
    fs::path base = app::os::user_base() / "DefJamRecompiled";
    for (const char *name : {"location.json", "steam.json", "steam-answer.json"})
        fs::remove(base / name, ec);
    fs::remove_all(base / "cache", ec);
#endif
    removal_step("Done");
    g_removal.state = 2;
}

void start_removal()
{
    const Install &in = app::install();
    g_form_error.clear();
    std::string why;
    if (g_removal.data && !removable_data(in.data, in.root, &why)) {
        g_form_error = why;
        return;
    }
    app::unlock_install();                                      // setup's uninstall takes it
    // The app works in the installed game's folder (as the game does), which Windows will not
    // delete while a process is in it; setup's uninstall inherits it too.
    std::error_code ec;
    os::change_dir(fs::temp_directory_path(ec));
    g_removal.state = 1;
    std::thread(removal_work, in.root, in.data, g_removal.data, g_removal.backup).detach();
}

// Windows: once the window has closed, remove the app's own folder (only an installed app: the
// program with payload\ and payload.sha256 beside it) and its logs, then its per-user folder if
// nothing is left in it.
void removal_cleanup()
{
#if defined(_WIN32)
    std::error_code ec;
    fs::path folder = app::program().parent_path();
    bool installed_app = fs::is_regular_file(folder / "payload.sha256", ec) && fs::is_directory(folder / "payload", ec) &&
                         folder != folder.root_path();
    fs::path base = app::os::user_base() / "DefJamRecompiled";
    fs::path script = fs::temp_directory_path(ec) /
                      ("DefJamRecompiled-uninstall-" + std::to_string(app::os::process_id()) + ".cmd");
    {
        std::ofstream out(script, std::ios::binary | std::ios::trunc);
        out << "@echo off\r\nping -n 4 127.0.0.1 >nul\r\n";
        if (installed_app) out << "rmdir /s /q \"" << folder.string() << "\"\r\n";
        out << "rmdir /s /q \"" << (base / "logs").string() << "\"\r\n";
        out << "rmdir \"" << base.string() << "\" 2>nul\r\n";
        out << "del \"%~f0\"\r\n";
    }
    const char *shell = getenv("ComSpec");
    os::Process process;
    std::string error;
    if (!os::start({shell && *shell ? shell : "cmd.exe", "/c", script.string()}, nullptr, &process, &error))
        fprintf(stderr, "[APP] uninstall: the app could not remove itself: %s\n", error.c_str());
#endif
}

void uninstall_page(float width, float height, UiResult *result)
{
    const Install &in = app::install();
    page_begin(width, height, "UNINSTALL");
    int state = g_removal.state;
    if (state == 0) {
        ImGui::TextWrapped("This removes Def Jam Recompiled from this computer:");
        ImGui::BulletText("the game built in %s", in.root.string().c_str());
#if defined(_WIN32)
        ImGui::BulletText("its Steam shortcut and artwork, if this app added them (Steam is closed and started "
                          "again if it is running)");
        ImGui::BulletText("this app, when you close it afterwards");
#endif
        ImGui::Spacing();
        ImGui::Checkbox("Also delete the data folder", &g_removal.data);
        ui_target("uninstall.data");
        ImGui::TextDisabled("%s", in.data.string().c_str());
        if (g_removal.data)
            ImGui::TextWrapped("It holds the copy of your dump, your saves, settings, mods and Upscaled Original Textures. Your saves "
                               "and settings are copied first, to:\n%s", g_removal.backup.string().c_str());
        else
            ImGui::TextWrapped("It stays, with your saves, settings and mods, for when you install again.");
        ImGui::Spacing();
        if (!g_form_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_form_error.c_str());
        page_actions();
        if (pcui::play_button("UNINSTALL", ImVec2(ImGui::GetFontSize() * 10, ImGui::GetFrameHeight() * 1.8f), 26))
            start_removal();
        ui_target("uninstall.start");
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || back_pressed()) {
            g_form_error.clear();
            g_screen = Screen::None;
        }
        ui_target("uninstall.cancel");
    } else if (state == 1) {
        std::lock_guard<std::mutex> hold(g_removal.lock);
        ImGui::TextWrapped("%s...", g_removal.step.c_str());
        ImGui::TextDisabled("Keep this window open until it finishes.");
    } else {
        std::lock_guard<std::mutex> hold(g_removal.lock);
        if (state == 2)
            ImGui::TextWrapped("Def Jam Recompiled is uninstalled.");
        else
            ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_removal.error.c_str());
        std::error_code ec;
        bool copied = g_removal.saves_copied || g_removal.settings_copied;
        if (copied) ImGui::TextWrapped("Backed up%s%s to %s", g_removal.saves_copied ? " saves" : "",
                                     g_removal.settings_copied ? " settings" : "", g_removal.backup.string().c_str());
        if (!g_removal.notes.empty()) ImGui::TextWrapped("%s", g_removal.notes.c_str());
#if defined(_WIN32)
        if (state == 2) ImGui::TextWrapped("The app removes itself when you close it.");
#else
        if (state == 2) ImGui::TextWrapped("To finish, delete the app itself (move it to the Trash).");
#endif
        ImGui::Spacing();
        page_actions();
        if (copied) {
            if (ImGui::Button("Open the saves folder")) pcui::open_path(g_removal.backup.string());
            ui_target("uninstall.saves");
            ImGui::SameLine();
        }
        if (ImGui::Button("Close")) {
            if (state == 2) removal_cleanup();
            result->leave = true;
        }
        ui_target("uninstall.close");
    }
    page_end();
}

void stage_list()
{
    for (int s = 0; s < k_stage_count; s++) {
        if (s == 6 && !g_run.hd) continue;
        bool done = g_run.exit_code == 0 || s < g_run.stage;
        bool current = s == g_run.stage && g_run.exit_code < 0;
        bool stopped = s == g_run.stage && g_run.exit_code > 0;      // where it failed or was cancelled
        const char *mark = done ? "[x]" : current ? "[>]" : stopped ? "[!]" : "[ ]";
        ImVec4 colour = current || stopped ? pcui::gold() : done ? ImVec4(0.8f, 0.8f, 0.8f, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1);
        ImGui::TextColored(colour, "%s %s%s", mark, k_stages[s].name, g_run.reused[s] ? "  (reused)" : "");
    }
}

void running_page(float width, float height)
{
    page_begin(width, height, g_run.title.c_str());
    stage_list();
    ImGui::Spacing();
    ImGui::TextWrapped("%s", g_run.cancelling ? "Stopping setup..." : "Preparing your local game. This can take several minutes.");
    if (ImGui::CollapsingHeader("Show technical details")) ImGui::TextWrapped("%s", g_run.line.c_str());
    ImGui::TextDisabled("%s elapsed", elapsed(SDL_GetTicks() - g_run.started_ms).c_str());
    ImGui::Spacing();
    page_actions();
    ImGui::BeginDisabled(g_run.cancelling);
    if (ImGui::Button("Cancel") || back_pressed()) g_confirm_cancel = true;
    ui_target("setup.cancel");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Open log")) pcui::open_path(g_run.log.string(), true);
    if (g_confirm_cancel) {
        ImGui::OpenPopup("Cancel setup?");
        g_confirm_cancel = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(width - 24, height - 24));
    if (ImGui::BeginPopupModal("Cancel setup?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Cancel setup? Your existing game and saves will be kept. Some prepared files may be reused, but the game build may need to start again.");
        if (ImGui::Button("Cancel setup")) {
            screen_request_cancel();
            ImGui::CloseCurrentPopup();
        }
        ui_target("setup.cancel-confirm");
        ImGui::SameLine();
        if (ImGui::Button("Keep installing") || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    page_end();
}

void finished_page(float width, float height, UiResult *result)
{
    bool ok = g_run.exit_code == 0;
    const char *title = ok ? "READY TO PLAY" : g_run.exit_code == 6 ? "CANCELLED" : "SETUP STOPPED";
    page_begin(width, height, title);
    stage_list();
    ImGui::Spacing();
    if (ok)
        ImGui::TextWrapped("Ready in %s. Version %s is installed. %s", elapsed(g_run.ended_ms - g_run.started_ms).c_str(),
                           app::install().version.c_str(), g_run.line.c_str());
    else {
        ImGui::TextWrapped("%s", g_run.line.empty() ? "Setup stopped." : g_run.line.c_str());
#if defined(_WIN32)
        if (g_run.exit_code == 3)
            ImGui::TextWrapped("Building the game needs Microsoft's C++ Build Tools. Install Build Tools downloads "
                               "them from Microsoft (a few GB; Windows asks for your permission) and then carries on.");
        if (g_run.exit_code == 3010)
            ImGui::TextWrapped("The Build Tools are installed. Restart Windows, then open Def Jam Recompiled "
                               "again and retry setup. Some prepared files may be reused.");
#elif defined(__APPLE__)
        if (g_run.exit_code == 3)
            ImGui::TextWrapped("Install Apple's Command Line Tools (Install Tools, or run xcode-select --install "
                               "in Terminal), then try again.");
#else
        if (g_run.exit_code == 3)
            ImGui::TextWrapped("The build tools that came with the app did not work on this computer. The setup log "
                               "says why; please include it in a bug report.");
#endif
        if (app::install().installed)
            ImGui::TextWrapped("The installed game is unchanged and still playable.");
    }
    ImGui::Spacing();
    page_actions();
    if (!ok && g_run.exit_code != 3010) {
        if (ImGui::Button("Try again")) {
            Request again = g_request;
            start(again.operation, again.title, again.root, again.data, again.dump, again.hd);
        }
        ui_target("setup.retry");
        ImGui::SameLine();
    }
    if (ok || app::install().installed) {
        if (ImGui::Button("Back to launcher")) g_screen = Screen::None;
        ui_target("setup.continue");
        if (ok) {
            ImGui::SameLine();
            if (ImGui::Button("Play")) result->play = true;
            ui_target("setup.play");
#if defined(_WIN32) || defined(__linux__)
            if (float beside = steam_play_width(); beside > 0) {       // the owner's choice: right by Play
                ImGui::SameLine();
                steam_play_button(beside, 0);
            }
#endif
        }
    } else {
        if (ImGui::Button("Change setup options") || back_pressed()) g_screen = app::install().problem.empty() ? Screen::FirstRun : Screen::Maintenance;
        ui_target("setup.back");
    }
#if defined(__APPLE__)
    if (g_run.exit_code == 3) {
        ImGui::SameLine();
        if (ImGui::Button("Install Tools")) {
            os::Process tools;
            std::string error;
            if (os::start({"/usr/bin/xcode-select", "--install"}, nullptr, &tools, &error)) os::wait(tools);
        }
    }
#elif defined(_WIN32)
    if (g_run.exit_code == 3) {
        ImGui::SameLine();
        if (ImGui::Button("Install Build Tools")) {
            Request again = g_request;
            if (!start(again.operation, again.title, again.root, again.data, again.dump, again.hd, true))
                g_screen = app::install().installed || !app::install().problem.empty() ? Screen::Maintenance
                                                                                        : Screen::FirstRun;
        }
        ui_target("setup.install-tools");
    }
#endif
    ImGui::SameLine();
    if (ImGui::Button("Open log")) pcui::open_path(g_run.log.string(), true);
    ImGui::SameLine();
    if (ImGui::Button("Quit")) result->leave = true;
    ui_target("Quit");
    page_end();
}

} // namespace

bool screen_draw(float width, float height, UiResult *result)
{
    if (g_handoff_started) { result->leave = true; return true; }
    if (!g_started) {
        g_started = true;
        const FirstRun &f = app::first_run();
        snprintf(g_dump, sizeof g_dump, "%s", f.dump.c_str());
        snprintf(g_root, sizeof g_root, "%s", f.install.c_str());
        snprintf(g_data, sizeof g_data, "%s", f.data.c_str());
        const Install &in = app::install();
        if (g_resume_setup) {
            g_resume_setup = false;
            auto request = g_request;
            if (!start(request.operation, request.title, request.root, request.data, request.dump,
                       request.hd, g_resume_tools)) g_screen = Screen::Maintenance;
        } else if (!in.installed) {
            g_screen = in.problem.empty() ? Screen::FirstRun : Screen::Maintenance;
            g_form_error = in.problem;
        } else if (app::game_behind(in)) {
            // An older game than this app builds, or another build of the same version (after the app's own update, or the first time a
            // newer app opens an older setup's installation): offer the update before the launcher.
            if (app::just_updated()) start_update_build(); // consent covered both phases before app restart
            else g_screen = Screen::UpdateReady;
        }
        start_tools_check();
    }
    take_pick();
    poll_tools_check();
    poll();
#if defined(_WIN32) || defined(__linux__)
    if (install_screen_draw(width, height, result)) return true;
#endif
    if (update_screen_draw(width, height, result)) return true;
    switch (g_screen) {
    case Screen::None: return false;
    case Screen::FirstRun: first_run_page(width, height, result); break;
    case Screen::Maintenance: maintenance_page(width, height, result); break;
    case Screen::Running: running_page(width, height); break;
    case Screen::Finished: finished_page(width, height, result); break;
    case Screen::UpdateReady: update_ready_page(width, height); break;
    case Screen::Uninstall: uninstall_page(width, height, result); break;
    }
    return true;
}

void resume_setup(const std::string &operation, const std::string &title, const std::string &root,
                  const std::string &data, const std::string &dump, bool hd, bool install_tools)
{
    // The same operations the screens can start; this is a one-shot request, never an update argument.
    if (operation != "install" && operation != "repair" && operation != "rebuild" && operation != "update")
        return;
    g_request = {operation, title, root, data, dump, hd};
    g_resume_tools = install_tools;
    g_resume_setup = true;
}

void screen_open_maintenance()
{
#if defined(_WIN32) || defined(__linux__)
    steam_section_reset();
#endif
    g_form_error.clear();
    g_screen = Screen::Maintenance;
}

void screen_open_hd()
{
    g_hd = true;
    screen_open_maintenance();
    g_form_error = "Upscaled Original Textures are selected. Repair game will generate any missing upscaled textures and keep your saves/settings. Allow 15 GiB extra working space.";
}

void start_update_build()
{
    const Install &in = app::install();
    g_form_error.clear();
    // Setup's Clean Update needs a newer version; another build of the same one is a Clean Rebuild
    // from this app's payload (setup/releases.py policy).
    std::string title = "Updating to " + target_label();
    bool started = app::other_build(in) ? start("rebuild", title, in.root.string(), in.data.string(), "", g_hd)
                                        : start("update", title, in.root.string(), in.data.string(), "", g_hd);
    if (!started)
        g_screen = Screen::Maintenance;                     // which shows why
}

#if defined(_WIN32) || defined(__linux__)
bool screen_busy()
{
    return g_run.process.id != 0 || update_busy() || install_busy() || steam_busy() || g_removal.state == 1;
}
#else
bool screen_busy() { return g_run.process.id != 0 || update_busy() || g_removal.state == 1; }
#endif

void screen_request_cancel()
{
    update_request_cancel();
    if (!g_run.process.id || g_run.cancelling) return;
    std::ofstream(g_run.cancel) << "cancel";
    g_run.cancelling = true;
    fprintf(stderr, "[APP] setup: cancel requested\n");
}

} // namespace app

// Home's Play row (pc_launcher_home.cpp, DEFJAM_APP): Add to Steam beside Play, on Windows and Linux
// once the game is installed. The width it needs, or 0.
float pc_app_beside_play_width()
{
#if defined(_WIN32) || defined(__linux__)
    return app::install().installed ? app::steam_play_width() : 0;
#else
    return 0;
#endif
}

void pc_app_beside_play(float width, float height)
{
#if defined(_WIN32) || defined(__linux__)
    app::steam_play_button(width, height);
#else
    (void)width;
    (void)height;
#endif
}

// Advanced > Maintenance's Uninstall (pc_ui.cpp, DEFJAM_APP): the app's own Uninstall screen.
void pc_app_open_uninstall()
{
    if (app::g_removal.state != 0) return;
    app::g_form_error.clear();
    app::g_removal.backup = app::backup_folder();
    app::g_screen = app::Screen::Uninstall;
}

void pc_app_setup_hd() { app::screen_open_hd(); }
