// Windows: the app installs itself (D215; docs/research/unified-app-design-2026-10-26.md §3.4).
//
// The released program, DefJamRecompiled-<version>-windows-x64.exe, carries the setup payload ZIP as
// resource 101 with its SHA-256 built in (DEFJAM_APP_PAYLOAD_SHA256), as DefJamSetup.exe does. The
// app lives in a folder of its own, its home: the program and, beside it, payload\ (unpacked from
// that resource), %LOCALAPPDATA%\Programs\DefJamRecompiled unless the player chose another. The
// chosen home is recorded in %LOCALAPPDATA%\DefJamRecompiled\location.json, so any other copy (the
// download, opened again) finds it. The built game and the data folder are elsewhere, chosen on the
// first-run screen as before. No Start-menu or desktop shortcut is made (owner's choice, 2026-10-27).
//
// When the app starts without --payload:
//   - a program without the resource (a development build) does nothing here;
//   - the copy in its home makes sure payload\ is the one built into it (payload.sha256 records it),
//     unpacking it again when it is missing or another build's;
//   - another copy (the download) hands over to the copy in the home: it first replaces that copy
//     when it is a newer release, then starts it with the same arguments and exits;
//   - with no home yet, the window opens on "Install Def Jam Recompiled": the folder, checked (not
//     Program Files, not a synced, network or FAT drive, short enough), then the program copied
//     there, its payload unpacked and the home recorded, and the installed copy started.
// Replacing files in a home follows the update's rule (app_update.cpp): renamed aside to .old one
// by one, each step undone if a later one fails; the started copy removes the .old files.

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <bcrypt.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
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

const char *k_program = "DefJamRecompiled.exe";

fs::path default_home() { return os::user_base() / "Programs" / "DefJamRecompiled"; }
fs::path location_file() { return os::user_base() / "DefJamRecompiled" / "location.json"; }

// {"app": "<home>"}: written here, so only that one string needs reading back.
fs::path recorded_home()
{
    std::ifstream in(location_file(), std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    std::string s = text.str();
    size_t at = s.find("\"app\"");
    if (at == std::string::npos || (at = s.find('"', s.find(':', at) + 1)) == std::string::npos) return {};
    std::string value;
    for (size_t i = at + 1; i < s.size() && s[i] != '"'; i++) {
        if (s[i] == '\\' && i + 1 < s.size()) i++;
        value += s[i];
    }
    return value.empty() ? fs::path() : fs::path(value);
}

bool record_home(const fs::path &home)
{
    std::error_code ec;
    fs::create_directories(location_file().parent_path(), ec);
    std::string escaped;
    for (char c : home.string()) {
        if (c == '\\' || c == '"') escaped += '\\';
        escaped += c;
    }
    fs::path pending = location_file().string() + ".pending";
    {
        std::ofstream out(pending, std::ios::binary);
        out << "{\"app\": \"" << escaped << "\"}\n";
        if (!out) return false;
    }
    fs::rename(pending, location_file(), ec);
    return !ec;
}

fs::path home() { fs::path h = recorded_home(); return h.empty() ? default_home() : h; }

bool same_folder(const fs::path &a, const fs::path &b)
{
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

// ---- the payload built into the program -----------------------------------------------------------

#if defined(DEFJAM_APP_PAYLOAD_SHA256)
const char *k_payload_sha256 = DEFJAM_APP_PAYLOAD_SHA256;
#else
const char *k_payload_sha256 = nullptr;
#endif

bool embedded(const unsigned char **bytes, DWORD *size)
{
    if (!k_payload_sha256) return false;
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));  // RT_RCDATA
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(nullptr, resource);
    *bytes = (const unsigned char *)LockResource(loaded);
    *size = SizeofResource(nullptr, resource);
    return *bytes && *size;
}

std::string sha256(const unsigned char *bytes, DWORD size)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char value[32];
    std::string out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0 &&
            BCryptHashData(hash, (PUCHAR)bytes, size, 0) >= 0 && BCryptFinishHash(hash, value, sizeof value, 0) >= 0) {
            static const char digits[] = "0123456789abcdef";
            for (unsigned char c : value) { out += digits[c >> 4]; out += digits[c & 15]; }
        }
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return out;
}

std::string read_first_line(const fs::path &path)
{
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    return line;
}

// Unpack the built-in payload into `dest` (which must not exist). "" when done, else why not.
std::string unpack_payload(const fs::path &dest)
{
    const unsigned char *bytes;
    DWORD size;
    if (!embedded(&bytes, &size)) return "This program carries no setup files.";
    if (sha256(bytes, size) != k_payload_sha256) return "This program's setup files are damaged. Download it again.";
    std::error_code ec;
    fs::path zip = fs::temp_directory_path(ec) / ("DefJamRecompiled-payload-" + std::to_string(os::process_id()) + ".zip");
    {
        std::ofstream out(zip, std::ios::binary);
        out.write((const char *)bytes, size);
        if (!out) return "The setup files could not be written to " + zip.parent_path().string() + " (is the disk full?).";
    }
    fs::create_directories(dest, ec);
    // Windows' own tar (Windows 10 1803 and later) reads ZIP files; PowerShell's Expand-Archive otherwise.
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    os::Process unpack;
    std::string error;
    int code = -1;
    fs::path tar = fs::path(system) / "tar.exe";
    if (fs::is_regular_file(tar, ec) && os::start({tar.string(), "-xf", zip.string(), "-C", dest.string()}, nullptr, &unpack, &error))
        code = os::wait(unpack);
    if (code != 0) {
        fs::remove_all(dest, ec);
        fs::create_directories(dest, ec);
        os::set_env("DEFJAM_APP_UNPACK_ZIP", zip.string());
        os::set_env("DEFJAM_APP_UNPACK_TO", dest.string());
        if (os::start({(fs::path(system) / "WindowsPowerShell/v1.0/powershell.exe").string(), "-NoProfile", "-NonInteractive",
                       "-Command", "$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath $env:DEFJAM_APP_UNPACK_ZIP "
                                   "-DestinationPath $env:DEFJAM_APP_UNPACK_TO"}, nullptr, &unpack, &error))
            code = os::wait(unpack);
        os::unset_env("DEFJAM_APP_UNPACK_ZIP");
        os::unset_env("DEFJAM_APP_UNPACK_TO");
    }
    fs::remove(zip, ec);
    if (code != 0 || !fs::is_regular_file(dest / "manifest.json", ec)) {
        fs::remove_all(dest, ec);
        return "The setup files could not be unpacked into " + dest.string() + ".";
    }
    return "";
}

// Put this program and its payload into `folder` (the home): new copies first, then each renamed in,
// the old ones aside to .old (removed by the copy that starts next), every step undone if one fails.
std::string place_into(const fs::path &folder, bool program_too)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (!os::writable(folder)) return "Files cannot be created in " + folder.string() + ".";
    fs::path program = folder / k_program, payload = folder / "payload";
    fs::remove_all(folder / "payload.new", ec);
    std::string why = unpack_payload(folder / "payload.new");
    if (!why.empty()) return why;
    if (program_too) {
        fs::remove(program.string() + ".new", ec);
        if (!CopyFileW(os::program().wstring().c_str(), (program.wstring() + L".new").c_str(), FALSE)) {
            fs::remove_all(folder / "payload.new", ec);
            return "The app could not be copied to " + folder.string() + ".";
        }
    }
    fs::remove(program.string() + ".old", ec);
    fs::remove_all(folder / "payload.old", ec);
    std::vector<std::pair<fs::path, fs::path>> steps, done;
    if (program_too && fs::exists(program, ec)) steps.push_back({program, program.string() + ".old"});
    if (fs::exists(payload, ec)) steps.push_back({payload, folder / "payload.old"});
    if (program_too) steps.push_back({program.string() + ".new", program});
    steps.push_back({folder / "payload.new", payload});
    for (const auto &step : steps) {
        fs::rename(step.first, step.second, ec);
        if (ec) {
            std::error_code back;
            for (auto it = done.rbegin(); it != done.rend(); ++it) fs::rename(it->second, it->first, back);
            fs::remove_all(folder / "payload.new", back);
            fs::remove(program.string() + ".new", back);
            return "The app could not be put in place in " + folder.string() + " (" + ec.message() + ").";
        }
        done.push_back(step);
    }
    std::ofstream(folder / "payload.sha256") << k_payload_sha256 << "\n";
    return "";
}

// Why a folder will not do as the app's home, or "".
std::string folder_problem(const fs::path &folder)
{
    std::string text = folder.string();
    if (folder.empty() || !folder.is_absolute()) return "Choose a full folder path.";
    if (text.size() > 150) return "Choose a shorter folder path.";
    std::string lower = text;
    for (char &c : lower) c = (char)tolower((unsigned char)c);
    for (const char *name : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"}) {
        const char *value = getenv(name);
        std::string v = value ? value : "";
        for (char &c : v) c = (char)tolower((unsigned char)c);
        if (!v.empty() && lower.rfind(v, 0) == 0)
            return "Program Files needs administrator rights to change. Choose a folder of your own.";
    }
    for (const char *name : {"OneDrive", "OneDriveConsumer", "OneDriveCommercial"}) {
        const char *value = getenv(name);
        std::string v = value ? value : "";
        for (char &c : v) c = (char)tolower((unsigned char)c);
        if (!v.empty() && lower.rfind(v, 0) == 0) return "Choose a folder outside OneDrive: syncing gets in the way of updates.";
    }
    std::wstring root = folder.root_path().wstring();
    UINT kind = GetDriveTypeW(root.c_str());
    if (kind == DRIVE_REMOTE) return "Choose a folder on this computer, not on a network drive.";
    wchar_t fs_name[MAX_PATH] = {};
    if (GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs_name, MAX_PATH) &&
        _wcsicmp(fs_name, L"NTFS") != 0 && _wcsicmp(fs_name, L"ReFS") != 0)
        return "Choose a folder on an NTFS drive (this one is FAT or exFAT).";
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
        std::string why = place_into(folder, true);
        if (why.empty() && !record_home(folder)) why = "The app's location could not be recorded in " + location_file().string() + ".";
        std::lock_guard<std::mutex> hold(g_result_lock);
        g_result = why;
        g_finished = true;
    }).detach();
}

} // namespace

Placement place_app(std::string *error)
{
    const unsigned char *bytes;
    DWORD size;
    if (!embedded(&bytes, &size)) return Placement::Development;
    fs::path me = os::program(), here = me.parent_path(), h = home();
    std::error_code ec;
    if (same_folder(here, h)) return Placement::Installed;
    fs::path installed = h / k_program;
    if (!fs::is_regular_file(installed, ec)) {
        snprintf(g_folder, sizeof g_folder, "%s", h.string().c_str());
        g_page = true;
        return Placement::NeedsInstall;
    }
    // A newer release opened from wherever it was downloaded updates the installed copy first.
    std::ifstream manifest(h / "payload/manifest.json", std::ios::binary);
    std::stringstream text;
    text << manifest.rdbuf();
    std::string s = text.str(), theirs;
    size_t at = s.find("\"version\"");
    if (at != std::string::npos && (at = s.find('"', s.find(':', at) + 1)) != std::string::npos)
        theirs = s.substr(at + 1, s.find('"', at + 1) - at - 1);
    if (newer(DEFJAM_VERSION, theirs)) {
        fprintf(stderr, "[APP] updating the installed app in %s from v%s to v%s\n", h.string().c_str(), theirs.c_str(), DEFJAM_VERSION);
        std::string why = place_into(h, true);
        if (!why.empty()) fprintf(stderr, "[APP] %s Starting the installed copy as it is.\n", why.c_str());
    }
    auto &next = relaunch();
    next = {installed.string()};
    for (const std::string &a : arguments()) next.push_back(a);
    fprintf(stderr, "[APP] handing over to the installed app: %s\n", installed.string().c_str());
    (void)error;
    return Placement::HandOver;
}

bool ensure_payload(std::string *error)
{
    fs::path folder = os::program().parent_path();
    std::error_code ec;
    if (fs::is_regular_file(folder / "payload/manifest.json", ec) &&
        read_first_line(folder / "payload.sha256") == k_payload_sha256)
        return true;
    fprintf(stderr, "[APP] unpacking the setup files this app carries into %s\n", (folder / "payload").string().c_str());
    std::string why = place_into(folder, false);
    if (!why.empty()) *error = why;
    return why.empty();
}

bool install_screen_draw(float width, float height, UiResult *result)
{
    if (!g_page) return false;
    {
        std::lock_guard<std::mutex> hold(g_pick_lock);
        if (g_has_pick) {
            snprintf(g_folder, sizeof g_folder, "%s", (fs::path(g_picked) / "DefJamRecompiled").string().c_str());
            g_has_pick = false;
        }
    }
    if (g_finished.exchange(false)) {
        g_working = false;
        std::lock_guard<std::mutex> hold(g_result_lock);
        if (g_result.empty()) {
            fs::path installed = fs::path(std::string(g_folder)) / k_program;
            fprintf(stderr, "[APP] installed the app in %s\n", g_folder);
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
    ImGui::TextWrapped("The app installs itself in a folder of its own. On the next screen it builds the game from "
                       "your own copy of Def Jam: Fight for NY, in a folder you choose there.");
    ImGui::Spacing();
    ImGui::TextWrapped("Nothing is added to the Start menu or the desktop. To open the app again, open this file "
                       "again, or DefJamRecompiled.exe in the folder below.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Install the app in");
    float button = ImGui::CalcTextSize("Change...").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::BeginDisabled(g_working);
    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##home", g_folder, sizeof g_folder);
    ui_target("install.folder");
    ImGui::SameLine();
    if (ImGui::Button("Change...")) SDL_ShowOpenFolderDialog(picked, nullptr, nullptr, nullptr, false);
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (!g_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_error.c_str());
    if (g_working) ImGui::TextDisabled("Installing...");
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
    ImGui::End();
    return true;
}

bool install_busy() { return g_working; }

} // namespace app
