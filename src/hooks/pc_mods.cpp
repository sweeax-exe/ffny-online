#define _CRT_SECURE_NO_WARNINGS
#include "pc_mods.h"
#include "pc_ui_model.h"
#include "pc_ui_actions.h"
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <sstream>
#if defined(_WIN32)
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
extern char **environ;
#endif
#ifndef DEFJAM_VERSION
#define DEFJAM_VERSION "0.0.0"
#endif
#ifndef DEFJAM_MOD_SOURCE_DIR
#define DEFJAM_MOD_SOURCE_DIR "."
#endif

namespace pcui {
static ModView view;
static std::string status;
static bool running = false;
static bool testing_versions = false;
static fs::path job_root;
#if defined(_WIN32)
static HANDLE process = nullptr;
static std::wstring wide(const std::string &s) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    std::wstring result(count, 0);
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), result.data(), count);
    return result;
}
static std::wstring quote(const std::wstring &s) {
    std::wstring out = L"\""; size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') { slashes++; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\'); return out + L'"';
}
#else
static pid_t process = -1;
#endif
static fs::path root() { return fs::u8path(folder_path(Folder::Data)) / "mod-manager"; }
static std::map<std::string, std::map<std::string, std::string>> projection(const fs::path &path) {
    std::map<std::string, std::map<std::string, std::string>> result;
    std::error_code error;
    if (fs::is_symlink(path, error) || !fs::is_regular_file(path, error) || fs::file_size(path, error) > 8 * 1024 * 1024) return result;
    std::ifstream input(path);
    std::string section, line;
    for (unsigned count = 0; count < 100000 && std::getline(input, line); count++) {
        if (line.size() > 8192) return {};
        if (!line.empty() && line.front() == '[' && line.back() == ']') section = line.substr(1, line.size()-2);
        else { auto split = line.find('='); if (split != std::string::npos) result[section][line.substr(0,split)] = line.substr(split+1); }
    }
    return result;
}
static void read_view() {
    ModView next;
    auto saved = projection(root() / "view.ini");
    next.message = saved["manager"]["message"];
    next.catalogue = saved["manager"]["catalogue"];
    for (auto &entry : saved) {
        auto &row = entry.second;
        if (entry.first.rfind("catalogue.",0) == 0) {
            ModRow mod;
            mod.id = row["id"]; mod.name = row["name"]; mod.authors = row["authors"]; mod.description = row["description"];
            mod.installed = row["installed"]; mod.latest = row["latest"]; mod.load_after = row["load_after"]; mod.channel = row["channel"];
            mod.bytes = strtoull(row["bytes"].c_str(), nullptr, 10);
            mod.compatible = row["compatible"] == "1"; mod.revoked = row["revoked"] == "1"; mod.update = row["update"] == "1";
            if (pack_name(mod.id)) next.catalogue_mods.push_back(mod);
        } else if (entry.first.rfind("installed.",0) == 0 && pack_name(row["folder"])) {
            next.installed[row["folder"]] = {row["folder"], row["name"], row["version"], row["rollback"] == "1"};
        }
    }
    view = std::move(next);
}
bool mod_busy() { return running; }
bool mod_prerelease() { return testing_versions; }
void mod_set_prerelease(bool enabled) { if (!running) testing_versions = enabled; }
const ModView &mod_view() { static bool loaded = false; if (!loaded) { read_view(); loaded = true; } return view; }
const std::string &mod_status() { return status; }
bool mod_recovery_needed() { std::error_code error; return fs::exists(root() / "journal.json", error); }
void mod_cancel() {
    if (!running) return;
    std::ofstream cancel(job_root / "cancel"); cancel << "cancel\n";
    status = "Cancelling; the previous installation will be kept.";
}
bool mod_start(const std::string &command, const std::string &id) {
    if (running) return false;
    if (command != "status" && command != "refresh" && command != "install" && command != "remove" && command != "rollback" && command != "recover") return false;
    if (!id.empty() && !pack_name(id)) return false;
    const char *override_root = getenv("RECOMP_TEXTURE_ROOT");
    if (override_root && *override_root && command != "status" && command != "refresh") {
        status = "Managed installs use the data folder's mods directory; unset RECOMP_TEXTURE_ROOT first."; return false;
    }
    std::error_code error;
    auto source = fs::current_path();
    if (!fs::is_regular_file(source / "setup/mods.py", error)) source = fs::u8path(DEFJAM_MOD_SOURCE_DIR);
    auto script = source / "setup/mods.py";
    auto python = source.parent_path() /
#if defined(_WIN32)
        "python/python.exe";
#else
        "python/bin/python3";
#endif
    if (!fs::is_regular_file(script, error)) { status = "This installation has no mod helper; local mods still work."; return false; }
    std::string interpreter;
    if (fs::is_regular_file(python,error)) interpreter = python.u8string();
    else {
        const char *dev = getenv("DEFJAM_MOD_PYTHON");
        if (dev && *dev) interpreter = dev;
        else if (getenv("DEFJAM_INSTALL_DIR")) { status = "Bundled Python is missing; repair this installation to use catalogue downloads."; return false; }
        else interpreter =
#if defined(_WIN32)
            "python.exe";
#else
            "python3";
#endif
    }
    job_root = root();
    if (fs::is_symlink(job_root,error)) { status = "Refusing a symlinked manager directory."; return false; }
    fs::create_directories(job_root,error);
    if (error) { status = "Could not create mod-manager state directory."; return false; }
    fs::remove(job_root / "cancel",error);
    fs::remove(job_root / "progress.ini",error);
    std::vector<std::string> args = {interpreter, script.u8string(), command, "--data-dir", folder_path(Folder::Data), "--runtime", DEFJAM_VERSION,
                                   "--cancel-file", (job_root / "cancel").u8string()};
#if defined(DEFJAM_MOD_WIDE)
    args.insert(args.end(), {"--capability", "wide-v1"});
#endif
    if (testing_versions) args.push_back("--prerelease");
    if (!id.empty()) args.insert(args.end(), {"--id",id});
#if defined(_WIN32)
    std::wstring cmd;
    for (auto &arg : args) { if (!cmd.empty()) cmd += L' '; cmd += quote(wide(arg)); }
    STARTUPINFOW startup = {}; startup.cb = sizeof startup;
    PROCESS_INFORMATION child = {};
    if (!CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)) {
        status = "Could not start mod helper."; return false;
    }
    CloseHandle(child.hThread); process = child.hProcess;
#else
    std::vector<char *> argv;
    for (auto &arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    posix_spawnattr_t attr; posix_spawnattr_init(&attr);
    posix_spawn_file_actions_t files; posix_spawn_file_actions_init(&files);
#if defined(__APPLE__)
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
#elif defined(__GLIBC__) && __GLIBC_PREREQ(2,34)
    posix_spawn_file_actions_addclosefrom_np(&files,3);
#else
    long limit = sysconf(_SC_OPEN_MAX); if (limit < 0) limit = 1024;
    for (int fd=3;fd<limit;fd++) if (fcntl(fd,F_GETFD)>=0) posix_spawn_file_actions_addclose(&files,fd);
#endif
    int result = posix_spawnp(&process,interpreter.c_str(),&files,&attr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&files); posix_spawnattr_destroy(&attr);
    if (result) { status = "Could not start mod helper: " + std::to_string(result); return false; }
#endif
    running = true; status = "Working on mods..."; return true;
}
bool mod_poll() {
    if (!running) return false;
    auto progress = projection(job_root / "progress.ini");
    if (!progress["progress"]["message"].empty()) {
        status = progress["progress"]["message"];
        auto done = progress["progress"]["done"], total = progress["progress"]["total"];
        if (total != "0" && !total.empty()) status += " (" + done + " / " + total + " bytes)";
    }
    bool complete = false, ok = false;
#if defined(_WIN32)
    DWORD code;
    if (GetExitCodeProcess(process,&code) && code != STILL_ACTIVE) { complete = true; ok = code == 0; CloseHandle(process); process = nullptr; }
#else
    int code = 0; pid_t result = waitpid(process,&code,WNOHANG);
    if (result == process || (result < 0 && errno != EINTR)) { complete = true; ok = result == process && WIFEXITED(code) && WEXITSTATUS(code) == 0; process = -1; }
#endif
    if (!complete) return false;
    running = false; read_view();
    if (ok) status = view.message.empty() ? "Mod inventory refreshed." : view.message;
    else if (progress["progress"]["message"].empty()) status = "Mod helper failed. Local packs are unchanged; see the launcher log.";
    return true;
}
} // namespace pcui

extern "C" int pc_mods_needs_recovery(void) { return pcui::mod_recovery_needed() ? 1 : 0; }

extern "C" int pc_mods_game_lock(void) {
    // No manager state means no managed packs/transactions yet. Create the shared
    // lock anyway so a later CLI installer cannot race a game launched manually.
    auto root = std::filesystem::u8path(pcui::folder_path(pcui::Folder::Data)) / "mod-manager";
    std::error_code error;
    if (std::filesystem::is_symlink(root,error)) return 0;
    std::filesystem::create_directories(root,error);
    if (error || std::filesystem::exists(root / "journal.json",error)) {
        fprintf(stderr,"[MODS] Incomplete mod transaction; open Mods to recover before playing.\n"); return 0;
    }
    auto path = root / "runtime.lock";
    if (std::filesystem::is_symlink(path,error)) return 0;
#if defined(_WIN32)
    static HANDLE lock = INVALID_HANDLE_VALUE;
    lock = CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (lock == INVALID_HANDLE_VALUE) return 0;
    OVERLAPPED overlapped = {};
    if (!LockFileEx(lock,LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlapped)) { CloseHandle(lock); lock = INVALID_HANDLE_VALUE; return 0; }
#else
    static int lock = -1;
    lock = open(path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
    if (lock < 0 || flock(lock,LOCK_SH|LOCK_NB)) { if (lock >= 0) close(lock); lock = -1; return 0; }
#endif
    // Recheck after acquiring the lock: an installer may have crashed between
    // the first journal check and this acquisition.
    if (std::filesystem::exists(root / "journal.json",error)) {
#if defined(_WIN32)
        CloseHandle(lock); lock = INVALID_HANDLE_VALUE;
#else
        close(lock); lock = -1;
#endif
        return 0;
    }
    return 1;
}
