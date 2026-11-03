// Def Jam Recompiled, the app: the launcher's Home and Settings in a program of its own, with no game
// code in it, and Play starting the game the setup built on this computer (docs/research/
// unified-app-design-2026-10-26.md, step 2).
//
//   defjam_app [--install DIR] [--play | --launcher]
//
// It does what the play wrapper does (setup/macos/Launcher, setup/linux/launcher.sh): finds the
// install, holds its play.lock, reads the receipt, supplies the data folder and working directory
// and keeps the session's log. In between it shows the launcher's window (pc_launcher_window, the
// same screen and settings file as the game's own launcher), and Play then becomes the game with
// --no-launcher: the lock descriptor survives the exec, so the lock lives as long as the game, and
// the app is gone when the game ends (D215). Windows has no exec: there the app closes its window,
// starts the game, keeps the lock while it waits for it and exits with it (app_os.cpp). The window is skipped as the game's own launcher is
// (pc_launcher_wanted): when the player turned "Skip launcher" on, unless Shift is held or --launcher
// is given, and never when mods need recovery; --play always skips it.
//
// The install is --install DIR, else DEFJAM_INSTALL_DIR, else the nearest folder above this program
// that holds installed.json. System calls are in app_os.cpp (POSIX and Windows).

#define _CRT_SECURE_NO_WARNINGS
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

// The top-level string values of a JSON object (the install receipt). Other values are skipped.
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
            skip();
        }
        space();
        if (i < text.size() && text[i] == ',') { i++; continue; }
        break;
    }
    return out;
}

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
    return {};
}

// Where the wrappers keep the session logs; the game's log goes there too (DEFJAM_LOG_DIR).
fs::path log_dir()
{
    if (const char *named = getenv("DEFJAM_LOG_DIR"); named && *named) return fs::path(named);
#if defined(_WIN32)
    fs::path base = app::os::user_base();
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

// Keep the ten newest session logs, so a problem report always has the last run, and send this
// session's output to a new one. A terminal keeps the output instead.
void open_session_log()
{
    fs::path logs = log_dir();
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

} // namespace

extern "C" int pc_mods_needs_recovery(void);   // pc_mods.cpp

// The USB model's hook, which pc_input.c hands the host input layer: there is no console here.
extern "C" void xbox_UsbSetPadCount(int count) { (void)count; }

int main(int argc, char **argv)
{
    const char *named = nullptr;
    bool play_now = false, force_launcher = false;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--install") && a + 1 < argc) named = argv[++a];
        else if (!strcmp(argv[a], "--play")) play_now = true;
        else if (!strcmp(argv[a], "--launcher")) force_launcher = true;
        else if (!strncmp(argv[a], "-psn", 4)) continue;                   // macOS's own, from Finder
        else if (!strncmp(argv[a], "-NS", 3)) a++;                           // an AppKit default and its value
        else fail(std::string("Unknown option: ") + argv[a]);
    }

    fs::path root = find_install(named);
    if (root.empty()) fail("No installation was found. Run Def Jam Setup to install the game.");
    // Acquire the lifetime lock before reading activation state or the receipt.
    if (app::os::lock_file(root / "play.lock") < 0) fail("The game or an update is already running.");
    std::error_code ec;
    if (fs::exists(root / ".setup-transaction", ec))
        fail("An update was interrupted. Run setup again to recover the installation.");
    std::ifstream file(root / "installed.json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    auto receipt = json_strings(text.str());
    const std::string source = receipt["source"], data = receipt["data"], executable = receipt["executable"];
    if (source.empty() || data.empty() || executable.empty() || !app::os::is_program(executable))
        fail("No completed installation was found. Run Def Jam Setup to install or repair it.");

    app::os::set_env("DEFJAM_DATA", data);
    app::os::set_env("DEFJAM_INSTALL_DIR", root.string());
    if (!app::os::change_dir(source)) fail("The installed game folder is missing. Run Def Jam Setup to repair it.");
    open_session_log();
    fprintf(stderr, "[APP] Def Jam Recompiled %s: install %s, game %s\n", DEFJAM_VERSION, root.string().c_str(),
            executable.c_str());

    // The same settings file as the game, with the same catalogue.
    const char *env = getenv("RECOMP_SETTINGS");
    std::string settings = env && !strcasecmp(env, "none") ? "" : env && *env ? env : (fs::path(data) / "settings.ini").string();
    pc_settings_open(settings.c_str());

    if (!SDL_Init(SDL_INIT_VIDEO)) fail(std::string("No display to show the launcher on: ") + SDL_GetError());
    int skip = pc_mods_needs_recovery() ? 0 : recomp_settings_get("launcher", "skip", 0);
    if (pc_launcher_wanted(skip, force_launcher && !play_now, play_now, shift_held(), 0)) {
        pc_input_start();                           // pads drive the menus, as in the game's launcher
        int open_setup = 0;
        int play = pc_launcher_window(&open_setup);
        pc_input_stop();
        if (play < 0) fail("The launcher window could not be opened.");
        if (open_setup) {
            pcui::setup_launch();
            SDL_Quit();
            return 0;
        }
        if (!play) {
            SDL_Quit();
            return 0;
        }
    }
    SDL_Quit();

    fprintf(stderr, "[APP] starting %s --no-launcher\n", executable.c_str());
#if defined(_WIN32)
    // The game keeps its run logs in logs\ under its folder, where the old play wrapper pointed too.
    app::os::set_env("DEFJAM_LOG_DIR", (fs::path(source) / "logs").string());
#endif
    std::string why = app::os::run_in_place({executable, "--no-launcher"}, source);
    fail("The game could not be started (" + why + "). Run Def Jam Setup to repair it.");
}
