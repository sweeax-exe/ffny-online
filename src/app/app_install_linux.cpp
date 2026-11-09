// Linux: the AppImage installs itself (docs/research/unified-app-design-2026-10-26.md §3.3, §6).
//
// The released app is one file, DefJamRecompiled-<version>-linux-x86_64.AppImage (scripts/package-appimage.py):
// the AppImage runtime and a read-only image holding this program and, beside it, the setup payload.
// The runtime mounts the image at a new /tmp/.mount_* folder on every start and says which file it
// came from in APPIMAGE (and where it is mounted in APPDIR). Nothing may remember the mount: setup
// copies what it needs out of the payload into the install folder, and Steam and the play wrapper are
// given the AppImage file.
//
// Its home is one file, ~/Applications/DefJamRecompiled.AppImage unless the player chose another
// folder, without the version in its name so that an update keeps the path Steam and the play
// wrapper know. The home is recorded, with its version, in ~/.local/share/DefJamRecompiled/location.json
// (XDG_DATA_HOME), so any other copy (the download, opened again) finds it. No menu entry or desktop
// shortcut is made (owner's choice, 2026-11-01): Add to Steam is the one shortcut.
//
// When the AppImage starts without --payload (as on Windows, app_install.cpp):
//   - a program not started from an AppImage (a development build) does nothing here;
//   - the copy in its home runs, and records its version;
//   - another copy hands over to the copy in the home: it first replaces that copy when it is a newer
//     release (a new file, renamed over the old one, which is atomic), then starts it with the same
//     arguments;
//   - with no home yet, the window opens on "Install Def Jam Recompiled": the folder, checked
//     (writable, programs may run from it, not inside an AppImage's mount), then the file copied there
//     and the home recorded, and the installed copy started.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
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

// The AppImage file this program was started from, or empty. Read once, before main clears the
// runtime's variables from the environment (they would otherwise reach the game and Steam). Only
// believed when APPDIR is this program's own folder: a variable inherited from some other AppImage
// is ignored.
fs::path appimage()
{
    static bool read = false;
    static fs::path value;
    if (read) return value;
    read = true;
    const char *file = getenv("APPIMAGE"), *dir = getenv("APPDIR");
    std::error_code ec;
    if (file && *file && dir && *dir && fs::equivalent(fs::path(dir), program().parent_path(), ec) &&
        fs::is_regular_file(file, ec))
        value = fs::canonical(file, ec);
    return value;
}

fs::path state_dir()
{
    const char *data = getenv("DEFJAM_APP_USER_BASE") ? nullptr : getenv("XDG_DATA_HOME");
    if (data && *data && fs::path(data).is_absolute()) return fs::path(data) / "DefJamRecompiled";
    fs::path home = os::user_base();
    return home.empty() ? home : home / ".local/share/DefJamRecompiled";
}

namespace
{

const char *k_file = "DefJamRecompiled.AppImage";

fs::path default_folder() { return os::user_base() / "Applications"; }
fs::path location_file() { return state_dir() / "location.json"; }

// {"app": "<home file>", "version": "<its version>", "commit": "<its payload's commit>"}: written here
// only. A record without "commit" (1.0.0-beta.1 and older) counts as another build.
std::map<std::string, std::string> recorded()
{
    std::ifstream in(location_file(), std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return json_values(text.str());
}

bool record_home(const fs::path &file, const std::string &version, const std::string &commit = payload_commit())
{
    std::error_code ec;
    fs::create_directories(location_file().parent_path(), ec);
    std::string escaped;
    for (char c : file.string()) {
        if (c == '\\' || c == '"') escaped += '\\';
        escaped += c;
    }
    fs::path pending = location_file().string() + ".pending";
    {
        std::ofstream out(pending, std::ios::binary);
        out << "{\"app\": \"" << escaped << "\", \"version\": \"" << version << "\", \"commit\": \"" << commit
            << "\"}\n";
        if (!out) return false;
    }
    fs::rename(pending, location_file(), ec);
    return !ec;
}

fs::path home_file()
{
    fs::path chosen = recorded()["app"];
    return chosen.empty() || !chosen.is_absolute() ? default_folder() / k_file : chosen;
}

// Copy this AppImage to `file`: a hidden new file beside it first, made executable, then renamed over
// it, so the path holds the old or the new app at every moment. "" when done, else why not.
std::string place_into(const fs::path &file)
{
    std::error_code ec;
    fs::path folder = file.parent_path();
    fs::create_directories(folder, ec);
    if (!os::writable(folder)) return "Files cannot be created in " + folder.string() + ".";
    fs::path pending = folder / ("." + file.filename().string() + ".new");
    fs::copy_file(appimage(), pending, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        fs::remove(pending, ec);
        return "The app could not be copied to " + folder.string() + " (" + ec.message() + ").";
    }
    fs::permissions(pending, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                    fs::perms::others_read | fs::perms::others_exec, ec);
    fs::rename(pending, file, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(pending, ignored);
        return "The app could not be put in place in " + folder.string() + " (" + ec.message() + ").";
    }
    return "";
}

// Why a folder will not do as the app's home, or "".
std::string folder_problem(const fs::path &folder)
{
    if (folder.empty() || !folder.is_absolute()) return "Choose a full folder path.";
    std::string text = folder.lexically_normal().string();
    if (text.rfind("/tmp/.mount_", 0) == 0 || text.rfind("/run/", 0) == 0)
        return "Choose a folder of your own: this one goes away when the app closes.";
    fs::path probe = folder;
    std::error_code ec;
    while (!probe.empty() && !fs::exists(probe, ec) && probe != probe.parent_path()) probe = probe.parent_path();
    struct statvfs info;
    if (statvfs(probe.c_str(), &info) == 0) {
        if (info.f_flag & ST_RDONLY) return "That drive is read-only. Choose another folder.";
        if (info.f_flag & ST_NOEXEC)
            return "Programs cannot run from that drive (it is mounted noexec). Choose a folder in your home folder.";
    }
    return "";
}

// ---- the install page ---------------------------------------------------------------------------------

bool g_page = false;
char g_folder[1024];
std::string g_error;
std::atomic<bool> g_working(false), g_finished(false);
std::mutex g_result_lock;
std::string g_result;

std::mutex g_pick_lock;
std::string g_picked;
bool g_has_pick = false;

void SDLCALL picked(void *, const char *const *files, int)
{
    std::lock_guard<std::mutex> hold(g_pick_lock);
    if (files && files[0]) {
        g_picked = files[0];
        g_has_pick = true;
    }
}

void start_install()
{
    fs::path folder = fs::path(std::string(g_folder));
    g_error = folder_problem(folder);
    if (!g_error.empty()) return;
    g_working = true;
    g_finished = false;
    std::thread([folder] {
        fs::path file = folder / k_file;
        std::string why = place_into(file);
        if (why.empty() && !record_home(file, payload_version()))
            why = "The app's location could not be recorded in " + location_file().string() + ".";
        std::lock_guard<std::mutex> hold(g_result_lock);
        g_result = why;
        g_finished = true;
    }).detach();
}

} // namespace

Placement place_app(std::string *error)
{
    (void)error;
    fs::path me = appimage();
    if (me.empty()) return Placement::Development;
    fs::path home = home_file();
    std::error_code ec;
    if (fs::equivalent(me, home, ec)) {
        if (recorded()["version"] != payload_version() || recorded()["commit"] != payload_commit() ||
            recorded()["app"] != home.string())
            record_home(home, payload_version());
        return Placement::Installed;
    }
    if (!fs::is_regular_file(home, ec)) {
        snprintf(g_folder, sizeof g_folder, "%s", home.parent_path().string().c_str());
        g_page = true;
        return Placement::NeedsInstall;
    }
    // A newer release opened from wherever it was downloaded updates the installed copy first, and so
    // does another build of the same version (the next beta, or the release after its betas).
    std::string theirs = recorded()["version"], their_commit = recorded()["commit"];
    bool other = !payload_commit().empty() && theirs == payload_version() && their_commit != payload_commit();
    if (newer(payload_version(), theirs) || other) {
        fprintf(stderr, "[APP] updating the installed app %s from %s to %s\n", home.string().c_str(),
                build_label(theirs, other ? their_commit : "").c_str(),
                build_label(payload_version(), other ? payload_commit() : "").c_str());
        std::string why = place_into(home);
        if (why.empty()) record_home(home, payload_version());
        else {
            g_error = "Could not update the installed app: " + why + " Retry installation below. The existing copy was kept.";
            snprintf(g_folder, sizeof g_folder, "%s", home.parent_path().string().c_str());
            g_page = true; return Placement::NeedsInstall;
        }
    }
    auto &next = relaunch();
    next = {home.string()};
    for (const std::string &a : arguments()) next.push_back(a);
    fprintf(stderr, "[APP] handing over to the installed app: %s\n", home.string().c_str());
    return Placement::HandOver;
}

// The payload is inside the image, beside the program: nothing to unpack.
bool ensure_payload(std::string *error)
{
    (void)error;
    return true;
}

// The installed app has been replaced by its own update (app_update.cpp): record the new version.
// The new copy records its own commit when it first runs.
void note_updated_home(const fs::path &file, const std::string &version) { record_home(file, version, ""); }

bool install_screen_draw(float width, float height, UiResult *result)
{
    if (!g_page) return false;
    {
        std::lock_guard<std::mutex> hold(g_pick_lock);
        if (g_has_pick) {
            snprintf(g_folder, sizeof g_folder, "%s", g_picked.c_str());
            g_has_pick = false;
        }
    }
    if (g_finished.exchange(false)) {
        g_working = false;
        std::lock_guard<std::mutex> hold(g_result_lock);
        if (g_result.empty()) {
            fs::path installed = fs::path(std::string(g_folder)) / k_file;
            fprintf(stderr, "[APP] installed the app as %s\n", installed.string().c_str());
            auto &next = relaunch();
            next = {installed.string()};
            for (const std::string &a : arguments()) next.push_back(a);
            result->leave = true;
        } else {
            g_error = g_result;
            fprintf(stderr, "[APP] installing the app: %s\n", g_error.c_str());
        }
    }
    page_begin(width, height, "INSTALL DEF JAM RECOMPILED");
    ImGui::TextWrapped("The app keeps itself in a folder of your choice, as %s, so that its updates and a Steam "
                       "shortcut always find it. On the next screen it builds the game from your own copy of Def "
                       "Jam: Fight for NY, in a folder you choose there.", k_file);
    ImGui::Spacing();
    ImGui::TextWrapped("No menu entry or desktop shortcut is made. To open the app again, open that file, or add "
                       "it to Steam once the game is built.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Keep the app in");
    float button = ImGui::CalcTextSize("Change...").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::BeginDisabled(g_working);
    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##home", g_folder, sizeof g_folder);
    ui_target("install.folder");
    ImGui::SameLine();
    if (ImGui::Button("Change...")) SDL_ShowOpenFolderDialog(picked, nullptr, nullptr, g_folder, false);
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (!g_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_error.c_str());
    if (g_working) ImGui::TextDisabled("Installing...");
    page_actions();
    ImGui::BeginDisabled(g_working || !g_folder[0]);
    if (pcui::play_button("INSTALL", ImVec2(ImGui::GetFontSize() * 10, ImGui::GetFrameHeight() * 1.8f), 26))
        start_install();
    ui_target("install.start");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(g_working);
    if (ImGui::Button("Quit")) result->leave = true;
    ui_target("Quit");
    ImGui::EndDisabled();
    page_end();
    return true;
}

bool install_busy() { return g_working; }

} // namespace app
