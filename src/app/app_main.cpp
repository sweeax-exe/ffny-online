// Def Jam Recompiled, the app: setup, the launcher's Home and Settings, and Play, in a program of its
// own with no game code in it (docs/research/unified-app-design-2026-10-26.md, steps 2 and 3).
//
//   defjam_app [--install DIR] [--payload DIR] [--dump PATH] [--data DIR] [--play | --launcher]
//              [--updated]
//
// With nothing installed it opens on the first run's setup (app_setup.cpp), which runs the setup
// engine from the app's setup payload; Maintenance repairs or rebuilds an install the same way.
// Otherwise it does what the play wrapper does (setup/macos/Launcher, setup/linux/launcher.sh):
// holds the install's play.lock, reads the receipt, supplies the data folder and working directory
// and keeps the session's log. In between it shows the launcher's window (pc_launcher_window, the
// same screen and settings file as the game's own launcher), and Play then becomes the game with
// --no-launcher: the lock descriptor survives the exec, so the lock lives as long as the game, and
// the app is gone when the game ends (D215). Windows has no exec: there the app closes its window,
// starts the game, keeps the lock while it waits for it and exits with it (app_os.cpp). The window is skipped as the game's own launcher is
// (pc_launcher_wanted): when the player turned "Skip launcher" on, unless Shift is held or --launcher
// is given, and never when mods need recovery; --play always skips it.
//
// The install is --install DIR, else DEFJAM_INSTALL_DIR, else the nearest folder above this program
// that holds installed.json, else the platform's usual place. --dump and --data fill in the first
// run's choices. The app updates itself (app_update.cpp) and starts the new copy with --updated,
// which then shows its window (even when the launcher is skipped) and offers to build the game it
// carries. System calls are in app_os.cpp (POSIX and Windows).

#define _CRT_SECURE_NO_WARNINGS
#include "app_setup_handoff.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#endif
#if defined(_WIN32)
#include <windows.h>
#define strcasecmp _stricmp
#endif

#include <SDL3/SDL.h>
#if defined(_WIN32)
#include <SDL3/SDL_main.h>          // WinMain, handing main its arguments in UTF-8
#endif

extern "C" {
#include "recomp_settings.h"
}
#include "pc_input.h"
#include "pc_settings_catalog.h"
#include "pc_ui.h"
#include "pc_ui_actions.h"
#include "app.h"
#include "app_os.h"

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string &message)
{
    fprintf(stderr, "[APP] %s\n", message.c_str());
    fflush(stderr);
    if (!getenv("RECOMP_APP_NO_DIALOG"))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Def Jam Recompiled", message.c_str(), nullptr);
    exit(1);
}

fs::path program_path() { return app::os::program(); }

// The top-level values of a JSON object (the install receipt): strings decoded, anything else as
// its JSON text.
std::map<std::string, std::string> json_strings(const std::string &text)
{
    std::map<std::string, std::string> out;
    size_t i = 0;
    auto space = [&] { while (i < text.size() && isspace((unsigned char)text[i])) i++; };
    auto string = [&](std::string &value) -> bool {
        if (i >= text.size() || text[i] != '"') return false;
        for (i++; i < text.size(); i++) {
            char c = text[i];
            if (c == '"') { i++; return true; }
            if (c != '\\') { value += c; continue; }
            if (++i >= text.size()) return false;
            switch (text[i]) {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case 'r': value += '\r'; break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case 'u': {
                if (i + 4 >= text.size()) return false;
                unsigned cp = (unsigned)strtoul(text.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < text.size() && text[i + 1] == '\\' && text[i + 2] == 'u') {
                    unsigned low = (unsigned)strtoul(text.substr(i + 3, 4).c_str(), nullptr, 16);
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    i += 6;
                }
                if (cp < 0x80) value += (char)cp;
                else if (cp < 0x800) { value += (char)(0xC0 | cp >> 6); value += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) {
                    value += (char)(0xE0 | cp >> 12); value += (char)(0x80 | ((cp >> 6) & 0x3F));
                    value += (char)(0x80 | (cp & 0x3F));
                } else {
                    value += (char)(0xF0 | cp >> 18); value += (char)(0x80 | ((cp >> 12) & 0x3F));
                    value += (char)(0x80 | ((cp >> 6) & 0x3F)); value += (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: value += text[i];
            }
        }
        return false;
    };
    // Skip one value of any kind, nested objects and arrays included.
    auto skip = [&] {
        int depth = 0;
        do {
            space();
            if (i >= text.size()) return;
            char c = text[i];
            if (c == '"') { std::string ignored; if (!string(ignored)) return; }
            else if (c == '{' || c == '[') { depth++; i++; }
            else if (c == '}' || c == ']') { depth--; i++; }
            else { while (i < text.size() && !strchr(",}] \t\r\n", text[i])) i++; }
            space();
            if (depth > 0 && i < text.size() && (text[i] == ',' || text[i] == ':')) i++;
        } while (depth > 0);
    };
    space();
    if (i >= text.size() || text[i] != '{') return out;
    i++;
    for (;;) {
        space();
        std::string key;
        if (!string(key)) break;
        space();
        if (i >= text.size() || text[i] != ':') break;
        i++;
        space();
        if (i < text.size() && text[i] == '"') {
            std::string value;
            if (!string(value)) break;
            out[key] = value;
        } else {
            size_t from = i;
            skip();
            size_t to = i;
            while (to > from && isspace((unsigned char)text[to - 1])) to--;
            out[key] = text.substr(from, to - from);
        }
        space();
        if (i < text.size() && text[i] == ',') { i++; continue; }
        break;
    }
    return out;
}

// The platform's usual install and data folders, as the setup wizards propose them.
void default_locations(fs::path *root, fs::path *data)
{
    fs::path base = app::os::user_base();
    if (base.empty()) base = fs::current_path();
#if defined(_WIN32)
    base /= "DefJamRecompiled";
    *root = base / "app";
    *data = base / "data";
#elif defined(__APPLE__)
    base /= "Library/Application Support/DefJamRecompiled";
    *root = base / "app";
    *data = base / "data";
#else
    base /= "Games/DefJamRecompiled";
    *root = base / "App";
    *data = base / "Data";
#endif
}

// The install folder the player chose when it is not the usual one (the first run's Install into
// another folder, or an installation found in the folder chosen there), kept in the app's own
// per-user folder beside the usual install so the next start finds it.
fs::path install_record()
{
    fs::path root, data;
    default_locations(&root, &data);
    return root.parent_path() / "install.json";
}

// --install DIR, else DEFJAM_INSTALL_DIR, else the nearest folder above this program that holds
// installed.json, else the folder the player chose (install.json), else the usual place (where the
// first run proposes to install).
fs::path find_install(const char *named)
{
    if (named && *named) return fs::path(named);
    if (const char *env = getenv("DEFJAM_INSTALL_DIR"); env && *env) return fs::path(env);
    fs::path dir = program_path().parent_path();
    for (int up = 0; up <= 5 && !dir.empty(); up++) {
        std::error_code ec;
        if (fs::is_regular_file(dir / "installed.json", ec)) return dir;
        if (dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    if (std::ifstream record(install_record(), std::ios::binary); record) {
        std::stringstream text;
        text << record.rdbuf();
        fs::path chosen = json_strings(text.str())["install"];
        std::error_code ec;
        if (!chosen.empty() && fs::is_regular_file(chosen / "installed.json", ec)) return chosen;
    }
    fs::path root, data;
    default_locations(&root, &data);
    return root;
}

// --payload DIR, else DEFJAM_SETUP_PAYLOAD, else the payload packaged with the app: in the bundle's
// Resources on macOS, or beside the program.
fs::path find_payload(const char *named)
{
    std::error_code ec;
    if (named && *named) return fs::path(named);
    if (const char *env = getenv("DEFJAM_SETUP_PAYLOAD"); env && *env) return fs::path(env);
    fs::path dir = program_path().parent_path();
    for (const fs::path &candidate : {dir / "../Resources/payload", dir / "payload"})
        if (fs::is_regular_file(candidate / "manifest.json", ec)) return fs::canonical(candidate, ec);
    return {};
}

// Keep the ten newest session logs, so a problem report always has the last run, and send this
// session's output to a new one. A terminal keeps the output instead.
void open_session_log()
{
    fs::path logs = app::log_dir();
    std::error_code ec;
    if (logs.empty() || !fs::create_directories(logs, ec) && ec) return;
    app::os::set_env("DEFJAM_LOG_DIR", logs.string());
    std::vector<std::string> old;
    for (const auto &entry : fs::directory_iterator(logs, ec)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("play-", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".log") == 0)
            old.push_back(name);
    }
    std::sort(old.begin(), old.end());
    for (size_t k = 0; k + 9 < old.size(); k++) {
        fs::remove(logs / old[k], ec);
        fs::remove(logs / (old[k] + ".err"), ec);
    }
    char stamp[32];
    time_t now = time(nullptr);
    strftime(stamp, sizeof stamp, "play-%Y%m%d-%H%M%S", localtime(&now));
    app::os::redirect_output(logs / stamp);
}

// Whether Shift is down right now, before any key event has said so (as host_posix_shift_held).
// A couch/Deck escape route from the saved direct-play preference and old --play shortcuts.
bool controller_launcher_held()
{
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) return false;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    bool held = false;
    for (int i = 0; ids && i < count; ++i) {
        SDL_Gamepad *pad = SDL_OpenGamepad(ids[i]);
        if (!pad) continue;
        SDL_PumpEvents(); SDL_UpdateGamepads();
        held |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) &&
                SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        SDL_CloseGamepad(pad);
    }
    SDL_free(ids);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    return held;
}

bool shift_held()
{
#if defined(__APPLE__)
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskShift) != 0;
#elif defined(_WIN32)
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#else
    return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
#endif
}

// Tell the install where this app is, so its play wrapper (setup/macos/Launcher, on Windows
// DefJamLauncher.exe, on Linux launcher.sh), which shortcuts and older habits still open, starts the
// app instead of the game. Only an installed app is worth naming: the macOS bundle, the Windows
// program, or the Linux AppImage file.
void note_location()
{
    fs::path here = app::bundle_path();
#if defined(_WIN32)
    if (!here.empty()) here = app::program();
#endif
    const app::Install &in = app::install();
    std::error_code ec;
    if (here.empty() || !fs::is_directory(in.root, ec)) return;
    fs::path note = in.root / "app-location.txt";
    std::ifstream current(note);
    std::string recorded;
    std::getline(current, recorded);
    current.close();
    if (recorded == here.string()) return;
    fs::path pending = in.root / "app-location.txt.pending";
    {
        std::ofstream out(pending);
        out << here.string() << "\n";
        if (!out) return;
    }
    fs::rename(pending, note, ec);
}

intptr_t g_lock = -1;
fs::path g_payload;
std::string g_payload_version, g_payload_commit;
std::vector<std::string> g_arguments, g_relaunch;
bool g_updated = false;

// A value in a setup payload's manifest ("version", "source_commit"), or "" without one.
std::string read_payload_field(const fs::path &payload, const char *key)
{
    if (payload.empty()) return "";
    std::ifstream manifest(payload / "manifest.json", std::ios::binary);
    std::stringstream text;
    text << manifest.rdbuf();
    return json_strings(text.str())[key];
}
std::string read_payload_version(const fs::path &payload) { return read_payload_field(payload, "version"); }

// "0.13.1" as numbers, or empty when it is not a release version.
std::vector<int> release_version(const std::string &text)
{
    std::vector<int> parts;
    std::string s = text.rfind('v', 0) == 0 ? text.substr(1) : text;
    std::stringstream in(s);
    std::string part;
    while (std::getline(in, part, '.')) {
        if (part.empty() || part.find_first_not_of("0123456789") != std::string::npos) return {};
        parts.push_back(atoi(part.c_str()));
    }
    return parts.size() == 3 ? parts : std::vector<int>();
}

} // namespace

namespace app
{

std::map<std::string, std::string> json_values(const std::string &text) { return json_strings(text); }

} // namespace app

namespace app
{

Install &install()
{
    static Install value;
    return value;
}

FirstRun &first_run()
{
    static FirstRun value;
    return value;
}

fs::path payload() { return g_payload; }
fs::path program() { return program_path(); }

fs::path launch_target()
{
#if defined(__linux__)
    if (!appimage().empty()) return appimage();
#endif
    return program();
}

#if !defined(__linux__)        // Linux: app_install_linux.cpp
fs::path state_dir()
{
    fs::path base = os::user_base();
    if (base.empty()) return base;
#if defined(__APPLE__)
    return base / "Library/Application Support/DefJamRecompiled";
#else
    return base / "DefJamRecompiled";
#endif
}
#endif
std::vector<std::string> &arguments() { return g_arguments; }
bool just_updated() { return g_updated; }
std::string payload_version() { return g_payload_version; }
std::string payload_commit() { return g_payload_commit; }
bool other_build(const Install &in)
{
    return !g_payload_commit.empty() && in.version == g_payload_version && in.source_commit != g_payload_commit;
}
bool game_behind(const Install &in) { return newer(g_payload_version, in.version) || other_build(in); }
std::string build_label(const std::string &version, const std::string &commit)
{
    std::string label = "v" + version;
    if (!commit.empty()) label += " (build " + commit.substr(0, 7) + ")";
    return label;
}
std::vector<std::string> &relaunch() { return g_relaunch; }

bool newer(const std::string &a, const std::string &b)
{
    std::vector<int> x = release_version(a), y = release_version(b);
    return !x.empty() && !y.empty() && x > y;
}

// Where the wrappers keep the session logs; the game's log goes there too (DEFJAM_LOG_DIR).
fs::path log_dir()
{
    if (const char *named = getenv("DEFJAM_LOG_DIR"); named && *named) return fs::path(named);
#if defined(_WIN32)
    fs::path base = os::user_base();
    return base.empty() ? base : base / "DefJamRecompiled" / "logs";
#else
    const char *home = getenv("HOME");
#endif
#if defined(__APPLE__)
    return home ? fs::path(home) / "Library/Logs/DefJamRecompiled" : fs::path();
#elif !defined(_WIN32)
    const char *state = getenv("XDG_STATE_HOME");
    if (state && *state) return fs::path(state) / "DefJamRecompiled/logs";
    return home ? fs::path(home) / ".local/state/DefJamRecompiled/logs" : fs::path();
#endif
}

bool lock_install()
{
    std::error_code ec;
    if (g_lock >= 0) return true;
    if (!fs::is_directory(install().root, ec)) return true;     // nothing installed there to guard yet
    intptr_t handle = os::lock_file(install().root / "play.lock");
    if (handle < 0) return false;
    g_lock = handle;
    return true;
}

void unlock_install()
{
    os::unlock_file(g_lock);                                    // closing the handle lets the lock go
    g_lock = -1;
}

std::string receipt_version(const fs::path &root)
{
    std::ifstream file(root / "installed.json", std::ios::binary);
    if (!file) return "";
    std::stringstream text;
    text << file.rdbuf();
    return json_strings(text.str())["version"];
}

void forget_install()
{
    std::error_code ec;
    fs::remove(install_record(), ec);
}

void remember_install(const fs::path &root)
{
    fs::path usual, data, record = install_record();
    default_locations(&usual, &data);
    std::error_code ec;
    if (fs::equivalent(root, usual, ec)) {                      // the usual place needs no record
        fs::remove(record, ec);
        return;
    }
    std::string text = root.string(), escaped;
    for (char c : text) {
        if (c == '\\' || c == '"') escaped += '\\';
        escaped += c;
    }
    fs::create_directories(record.parent_path(), ec);
    fs::path temp = record;
    temp += ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << "{\"install\": \"" << escaped << "\"}\n";
        if (!out) return;
    }
    fs::rename(temp, record, ec);
    fprintf(stderr, "[APP] remembering the install folder %s\n", text.c_str());
}

bool load_install(std::string *why)
{
    Install &in = install();
    std::error_code ec;
    in.installed = false;
    in.problem.clear();
    std::ifstream file(in.root / "installed.json", std::ios::binary);
    if (!file) return false;                                    // the first run: nothing to say
    std::stringstream text;
    text << file.rdbuf();
    auto receipt = json_strings(text.str());
    in.data = receipt["data"];
    in.source = receipt["source"];
    in.executable = receipt["executable"];
    in.version = receipt["version"];
    in.source_commit = receipt["source_commit"];
    if (fs::exists(in.root / ".setup-transaction", ec))
        *why = "An update was interrupted. Select Repair to recover the installation.";
    else if (in.source.empty() || in.data.empty() || in.executable.empty() || !os::is_program(in.executable))
        *why = "The installation is incomplete. Select Repair to fix it.";
    else if (!os::change_dir(in.source))
        *why = "The installed game folder is missing. Select Repair to fix it.";
    in.problem = *why;
    if (!why->empty()) return false;
    os::set_env("DEFJAM_DATA", in.data.string());
    os::set_env("DEFJAM_INSTALL_DIR", in.root.string());
    // The same settings file as the game, with the same catalogue.
    const char *env = getenv("RECOMP_SETTINGS");
    std::string settings = env && !strcasecmp(env, "none") ? "" : env && *env ? env : (in.data / "settings.ini").string();
    pc_settings_open(settings.c_str());
    in.installed = true;
    note_location();
    return true;
}

} // namespace app

extern "C" int pc_mods_needs_recovery(void);   // pc_mods.cpp

// The USB model's hook, which pc_input.c hands the host input layer: there is no console here.
extern "C" void xbox_UsbSetPadCount(int count) { (void)count; }

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "--setup-handoff")) {
        std::string error;
        if (!app::os::wait_setup_handoff(argv[2], argv[3], argv[4], &error)) fail(error);
        // Internal handles are one-shot: never forward them to an app update or placement.
        for (int i = 1; i + 4 < argc; ++i) argv[i] = argv[i + 4];
        argc -= 4;
        argv[argc] = nullptr;
    }
    const char *named = nullptr, *payload = nullptr;
    bool play_now = false, force_launcher = false;
    app::FirstRun &first = app::first_run();
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--resume-setup") && a + 7 < argc) a += 7;
        else g_arguments.push_back(argv[a]);
    }
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--updated")) g_updated = true;             // started by its own update
        else if (!strcmp(argv[a], "--install") && a + 1 < argc) named = argv[++a];
        else if (!strcmp(argv[a], "--payload") && a + 1 < argc) payload = argv[++a];
        else if (!strcmp(argv[a], "--dump") && a + 1 < argc) first.dump = argv[++a];
        else if (!strcmp(argv[a], "--data") && a + 1 < argc) first.data = argv[++a];
        else if (!strcmp(argv[a], "--play")) play_now = true;
        else if (!strcmp(argv[a], "--launcher")) force_launcher = true;
        else if (!strcmp(argv[a], "--resume-setup") && a + 7 < argc) {
            app::resume_setup(argv[a+1], argv[a+2], argv[a+3], argv[a+4], argv[a+5],
                              !strcmp(argv[a+6], "1"), !strcmp(argv[a+7], "1"));
            a += 7;
            force_launcher = true;
        }
        else if (!strncmp(argv[a], "-psn", 4)) continue;                   // macOS's own, from Finder
        else if (!strncmp(argv[a], "-NS", 3)) a++;                           // an AppKit default and its value
        else fail(std::string("Unknown option: ") + argv[a]);
    }

    app::Install &in = app::install();
    in.root = find_install(named);
    g_payload = find_payload(payload);
    fs::path default_root, default_data;
    default_locations(&default_root, &default_data);
    first.install = in.root.string();
    if (first.data.empty()) {
        const char *env = getenv("DEFJAM_DATA");
        first.data = env && *env ? env : default_data.string();
    }

    open_session_log();
    // Known before the app places itself: the Linux AppImage records it with its home, and compares
    // it with the installed copy's.
    g_payload_version = read_payload_version(g_payload);
    g_payload_commit = read_payload_field(g_payload, "source_commit");
#if defined(_WIN32) || defined(__linux__)
    // The released Windows program and Linux AppImage install themselves, or hand over to the
    // installed copy (app_install.cpp, app_install_linux.cpp). A development build, or one given its
    // setup files, runs as it is.
    if (!payload && !getenv("DEFJAM_SETUP_PAYLOAD")) {
        std::string error;
        switch (app::place_app(&error)) {
        case app::Placement::HandOver: {
            std::string why = app::os::replace_with(g_relaunch);
            fail("The installed app could not be started (" + why + ").");
        }
        case app::Placement::Installed:
            if (!app::ensure_payload(&error)) fail(error);
            g_payload = find_payload(nullptr);
            break;
        case app::Placement::NeedsInstall:
        case app::Placement::Development:
            break;
        }
    }
#endif
#if defined(__linux__)
    // The AppImage runtime's own variables name this run's mount: read once (app::appimage), then
    // kept from the engine, the game and Steam, which must never remember the mount.
    app::appimage();
    for (const char *name : {"APPDIR", "APPIMAGE", "ARGV0", "OWD"}) app::os::unset_env(name);
#endif
    g_payload_version = read_payload_version(g_payload);     // the Windows app may have just unpacked it
    g_payload_commit = read_payload_field(g_payload, "source_commit");
    app::update_cleanup();
    // Acquire the lifetime lock before reading activation state or the receipt.
    if (!app::lock_install()) fail("The game or an update is already running.");
    std::string why;
    if (!app::load_install(&why)) {
        if (!why.empty()) fprintf(stderr, "[APP] %s\n", why.c_str());
        pc_settings_open("");                       // the screens' own settings, at their defaults
    }
    fprintf(stderr, "[APP] Def Jam Recompiled %s: install %s (%s), setup files %s (v%s)%s\n", DEFJAM_VERSION,
            in.root.string().c_str(), in.installed ? ("v" + in.version).c_str() : "not installed",
            g_payload.empty() ? "none" : g_payload.string().c_str(), g_payload_version.c_str(),
            g_updated ? ", just updated" : "");

    if (!SDL_Init(SDL_INIT_VIDEO)) fail(std::string("No display to show the launcher on: ") + SDL_GetError());
    int skip = pc_mods_needs_recovery() ? 0 : recomp_settings_get("launcher", "skip", 0);
    // Without a playable install there is only the setup to show. Just after its own update the app
    // always shows its window: the update was started from it, and the new app offers to build the
    // game it carries (a player who skips the launcher would otherwise go straight into the old one).
    bool force = force_launcher || g_updated || shift_held() || ((skip || play_now) && controller_launcher_held());
    if (!in.installed || pc_launcher_wanted(skip, force, play_now && !force, shift_held(), 0)) {
        pc_input_start();                           // pads drive the menus, as in the game's launcher
        int open_setup = 0;
        int play = pc_launcher_window(&open_setup);
        pc_input_stop();
        if (play < 0) fail("The launcher window could not be opened.");
        if (!g_relaunch.empty()) {
            // The app replaced itself: start the new one in this process, without the lock, which
            // it takes again.
            SDL_Quit();
            app::unlock_install();
            // A scripted run (RECOMP_LAUNCHER_SCRIPT, src/hooks/pc_launcher_sdl.cpp) gives the
            // updated app a script of its own, or none: this one's times start again there.
            const char *after = getenv("RECOMP_LAUNCHER_SCRIPT_AFTER_UPDATE");
            if (after) app::os::set_env("RECOMP_LAUNCHER_SCRIPT", after);
            else app::os::unset_env("RECOMP_LAUNCHER_SCRIPT");
            app::os::unset_env("RECOMP_LAUNCHER_SCRIPT_AFTER_UPDATE");
            fprintf(stderr, "[APP] starting the updated app: %s\n", g_relaunch[0].c_str());
            std::string error = app::os::replace_with(g_relaunch);
            fail("The updated app could not be started (" + error + ").");
        }
        if (!play || !in.installed) {
            SDL_Quit();
            return 0;
        }
    }
    SDL_Quit();

    fprintf(stderr, "[APP] starting %s --no-launcher\n", in.executable.string().c_str());
#if defined(_WIN32)
    // The game keeps its run logs in logs\ under its folder, where the old play wrapper pointed too.
    app::os::set_env("DEFJAM_LOG_DIR", (in.source / "logs").string());
#endif
    std::string error = app::os::run_in_place({in.executable.string(), "--no-launcher"}, in.source);
    fail("The game could not be started (" + error + "). Select Repair to fix it.");
}
