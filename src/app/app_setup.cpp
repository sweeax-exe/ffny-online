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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
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
    UpdateReady     // the app was just updated: build the game it now carries
};

// The steps the engine reports, in order, by the start of its status lines.
struct Stage
{
    const char *name;
    const char *prefixes[4];
};
const Stage k_stages[] = {
    {"Prepare the setup files", {"Preparing verified release source", nullptr}},
    {"Verify your dump", {"Verifying and preparing your dump", "Verified installed dump", "Verified extraction"}},
    {"Check the build tools", {"Checking ", "Installing Microsoft Build Tools", "Preparing the build container",
                               "Building with this computer"}},
    {"Analyse the game's code", {"Analysis ", nullptr}},
    {"Translate it to C", {"Lift ", nullptr}},
    {"Compile the game", {"Build ", "Building in the build container", nullptr}},
    {"HD textures", {"Upscaling HD textures", "HD textures ready", nullptr}},
    {"Activate", {"Activating the verified installation", "Setup complete", "Installation is ready"}},
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
bool g_confirm_cancel = false;
std::string g_form_error;

// What the last operation was started with, to start it again with the Build Tools (Windows).
struct Request
{
    std::string operation, title, root, data, dump;
    bool hd = false;
} g_request;

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
    if (code == 0 && g_run.operation == "install")
        app::remember_install(app::install().root);         // found again at the next start
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
}

namespace
{

void path_row(const char *label, const char *id, char *value, bool folder, bool image_too)
{
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    float button = ImGui::CalcTextSize("Folder...").x + ImGui::GetStyle().FramePadding.x * 2;
    float buttons = (image_too ? 2 : 1) * (button + ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - buttons));
    ImGui::InputText("##path", value, sizeof g_dump);
    ui_target(id);
    if (image_too) {
        ImGui::SameLine();
        if (ImGui::Button("Image...")) choose(value, false);
    }
    ImGui::SameLine();
    if (ImGui::Button(folder ? "Folder..." : "Choose...")) choose(value, true);
    ImGui::PopID();
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
    } else if (app::newer(app::payload_version(), app::install().version)) {
        start_update_build();
    } else {
        g_screen = Screen::None;
    }
}

void first_run_page(float width, float height, UiResult *result)
{
    page_begin(width, height, "SET UP THE GAME");
    ImGui::TextWrapped("The game is built here from your own copy of Def Jam: Fight for NY (Xbox): a disc "
                       "image or an extracted folder. Nothing from it leaves this computer. Setup takes about "
                       "ten minutes and needs about 12 GB.");
    ImGui::Spacing();
    path_row("Your dump (disc image or extracted folder)", "setup.dump", g_dump, true, true);
    path_row("Install folder (the built game)", "setup.install", g_root, true, false);
    path_row("Data folder (your extracted dump, saves and settings)", "setup.data", g_data, true, false);
    probe_root();
    bool found = !g_found.version.empty();
    bool older = found && app::newer(app::payload_version(), g_found.version);
    if (older)
        ImGui::TextColored(pcui::gold(), "This folder already has Def Jam Recompiled v%s. Update it to v%s: it keeps "
                           "its own data folder, saves and settings, so the dump and data folder above are not "
                           "needed.", g_found.version.c_str(), app::payload_version().c_str());
    else if (found)
        ImGui::TextColored(pcui::gold(), "This folder already has Def Jam Recompiled v%s. Use it as it is, with its "
                           "own data folder, saves and settings.", g_found.version.c_str());
    ImGui::Spacing();
    if (!found) {
        ImGui::Checkbox("Generate HD textures (adds several minutes and about 15 GB)", &g_hd);
        ui_target("setup.hd");
    }
    tools_row();
    ImGui::Spacing();
    if (!g_form_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_form_error.c_str());
    bool ready = found ? g_root[0] != 0 : g_dump[0] && g_root[0] && g_data[0];
    std::string label = !found ? "INSTALL" : older ? "UPDATE TO V" + app::payload_version() : "USE THIS INSTALL";
    ImGui::BeginDisabled(!ready);
    if (pcui::play_button(label.c_str(), ImVec2(ImGui::GetFontSize() * 13, ImGui::GetFrameHeight() * 1.8f), 26))
    {
        if (found) {
            adopt_found();
        } else {
            g_form_error.clear();
            app::install().root = g_root;
            start("install", "Installing", g_root, g_data, g_dump, g_hd);
        }
    }
    ui_target(!found ? "setup.start" : older ? "setup.update" : "setup.use");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Quit")) result->leave = true;
    ui_target("Quit");
    ImGui::End();
}

void maintenance_page(float width, float height, UiResult *result)
{
    const Install &in = app::install();
    page_begin(width, height, "MAINTENANCE");
    ImGui::TextWrapped("Installed: v%s in %s", in.version.c_str(), in.root.string().c_str());
    ImGui::TextWrapped("Data: %s", in.data.string().c_str());
    ImGui::Spacing();
    ImGui::Checkbox("Also generate HD textures", &g_hd);
    tools_row();
    ImGui::Spacing();
    if (!g_form_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_form_error.c_str());
    // An older game: Repair and Clean Rebuild work on the version this app builds, so the update
    // comes first (setup refuses them across versions).
    bool older = in.installed && app::newer(app::payload_version(), in.version);
    if (older) {
        ImGui::SeparatorText("Update");
        ImGui::TextWrapped("Your game is v%s; this app builds v%s. Update builds the game again from your dump "
                           "with the new version, in about three to ten minutes. Your saves, settings and mods "
                           "are kept, and v%s stays playable until the new build is ready.",
                           in.version.c_str(), app::payload_version().c_str(), in.version.c_str());
        if (ImGui::Button(("Update to v" + app::payload_version()).c_str())) start_update_build();
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
    ImGui::SeparatorText("Clean Rebuild");
    ImGui::TextWrapped("Builds this version again from nothing: analysis, translation and compile. About ten "
                       "minutes. The current build stays playable until the new one is ready.");
    if (ImGui::Button("Clean Rebuild")) {
        g_form_error.clear();
        start("rebuild", "Rebuilding", in.root.string(), in.data.string(), "", g_hd);
    }
    ui_target("setup.rebuild");
    ImGui::EndDisabled();
    if (older)
        ImGui::TextDisabled("Repair and Clean Rebuild work on v%s, the version this app builds: update first.",
                            app::payload_version().c_str());
#if defined(_WIN32)
    if (in.installed) steam_section();
#endif
    ImGui::Spacing();
    ImGui::Separator();
    if (in.installed) {
        if (ImGui::Button("< Back to launcher")) {
            g_form_error.clear();
            g_screen = Screen::None;
        }
        ui_target("Back");
    } else if (ImGui::Button("Quit")) {
        result->leave = true;
    }
    ImGui::End();
}

void update_ready_page(float width, float height)
{
    const Install &in = app::install();
    page_begin(width, height, "UPDATE YOUR GAME");
    if (app::just_updated())
        ImGui::TextWrapped("Def Jam Recompiled is now v%s.", app::payload_version().c_str());
    ImGui::TextWrapped("Your game is v%s. Update it to v%s now: it is built again from your dump with the new "
                       "version, in about three to ten minutes. Your saves, settings and mods are kept.",
                       in.version.c_str(), app::payload_version().c_str());
    ImGui::TextWrapped("Later opens the launcher, where Play starts v%s and Update / Repair offers the update "
                       "again.", in.version.c_str());
    tools_row();
    ImGui::Spacing();
    if (!g_form_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_form_error.c_str());
    if (pcui::play_button("UPDATE GAME", ImVec2(ImGui::GetFontSize() * 12, ImGui::GetFrameHeight() * 1.8f), 26))
        start_update_build();
    ui_target("update.build");
    ImGui::SameLine();
    if (ImGui::Button("Later")) g_screen = Screen::None;
    ui_target("update.later");
    ImGui::End();
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
    ImGui::TextWrapped("%s", g_run.cancelling ? "Cancelling the current step..." : g_run.line.c_str());
    ImGui::TextDisabled("%s elapsed", elapsed(SDL_GetTicks() - g_run.started_ms).c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(g_run.cancelling);
    if (ImGui::Button("Cancel")) g_confirm_cancel = true;
    ui_target("setup.cancel");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Open log")) pcui::open_path(g_run.log.string(), true);
    if (g_confirm_cancel) {
        ImGui::OpenPopup("Cancel setup?");
        g_confirm_cancel = false;
    }
    if (ImGui::BeginPopupModal("Cancel setup?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Stop after the current step? What is done so far can be resumed.");
        if (ImGui::Button("Cancel setup")) {
            screen_request_cancel();
            ImGui::CloseCurrentPopup();
        }
        ui_target("setup.cancel-confirm");
        ImGui::SameLine();
        if (ImGui::Button("Keep going")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

void finished_page(float width, float height, UiResult *result)
{
    bool ok = g_run.exit_code == 0;
    const char *title = ok ? "DONE" : g_run.exit_code == 6 ? "CANCELLED" : "SETUP STOPPED";
    page_begin(width, height, title);
    stage_list();
    ImGui::Spacing();
    if (ok)
        ImGui::TextWrapped("Ready in %s. Version %s is installed.", elapsed(g_run.ended_ms - g_run.started_ms).c_str(),
                           app::install().version.c_str());
    else {
        ImGui::TextWrapped("%s", g_run.line.empty() ? "Setup stopped." : g_run.line.c_str());
#if defined(_WIN32)
        if (g_run.exit_code == 3)
            ImGui::TextWrapped("Building the game needs Microsoft's C++ Build Tools. Install Build Tools downloads "
                               "them from Microsoft (a few GB; Windows asks for your permission) and then carries on.");
        if (g_run.exit_code == 3010)
            ImGui::TextWrapped("The Build Tools are installed. Restart Windows, then open Def Jam Recompiled "
                               "again: setup carries on from where it stopped.");
#else
        if (g_run.exit_code == 3)
            ImGui::TextWrapped("Install Apple's Command Line Tools (Install Tools, or run xcode-select --install "
                               "in Terminal), then try again.");
#endif
        if (app::install().installed)
            ImGui::TextWrapped("The installed game is unchanged and still playable.");
    }
    ImGui::Spacing();
    if (ok || app::install().installed) {
        if (ImGui::Button("Continue")) g_screen = Screen::None;
        ui_target("setup.continue");
        if (ok) {
            ImGui::SameLine();
            if (ImGui::Button("Play")) result->play = true;
        }
    } else {
        if (ImGui::Button("Back")) g_screen = app::install().problem.empty() ? Screen::FirstRun : Screen::Maintenance;
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
    ImGui::End();
}

} // namespace

bool screen_draw(float width, float height, UiResult *result)
{
    if (!g_started) {
        g_started = true;
        const FirstRun &f = app::first_run();
        snprintf(g_dump, sizeof g_dump, "%s", f.dump.c_str());
        snprintf(g_root, sizeof g_root, "%s", f.install.c_str());
        snprintf(g_data, sizeof g_data, "%s", f.data.c_str());
        const Install &in = app::install();
        if (!in.installed) {
            g_screen = in.problem.empty() ? Screen::FirstRun : Screen::Maintenance;
            g_form_error = in.problem;
        } else if (app::newer(app::payload_version(), in.version)) {
            // An older game than this app builds (after the app's own update, or the first time a
            // newer app opens an older setup's installation): offer the update before the launcher.
            g_screen = Screen::UpdateReady;
        }
        start_tools_check();
    }
    take_pick();
    poll_tools_check();
    poll();
#if defined(_WIN32)
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
    }
    return true;
}

void screen_open_maintenance()
{
#if defined(_WIN32)
    steam_section_reset();
#endif
    g_form_error.clear();
    g_screen = Screen::Maintenance;
}

void start_update_build()
{
    const Install &in = app::install();
    g_form_error.clear();
    if (!start("update", "Updating to v" + app::payload_version(), in.root.string(), in.data.string(), "", false))
        g_screen = Screen::Maintenance;                     // which shows why
}

#if defined(_WIN32)
bool screen_busy() { return g_run.process.id != 0 || update_busy() || install_busy() || steam_busy(); }
#else
bool screen_busy() { return g_run.process.id != 0 || update_busy(); }
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
