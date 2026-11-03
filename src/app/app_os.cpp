// The app's operating-system calls (app_os.h): POSIX first, then Windows.

#define _CRT_SECURE_NO_WARNINGS
#include "app_os.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <shlobj.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char **environ;
#endif

#include <fstream>

namespace fs = std::filesystem;

namespace app
{
namespace os
{

#if !defined(_WIN32)

fs::path program()
{
    std::error_code ec;
#if defined(__APPLE__)
    char raw[4096];
    uint32_t n = sizeof raw;
    if (_NSGetExecutablePath(raw, &n)) return {};
    return fs::canonical(raw, ec);
#else
    return fs::canonical("/proc/self/exe", ec);
#endif
}

fs::path user_base()
{
    const char *home = getenv("HOME");
    return home ? fs::path(home) : fs::path();
}

void set_env(const char *name, const std::string &value) { setenv(name, value.c_str(), 1); }
void unset_env(const char *name) { unsetenv(name); }

bool is_program(const fs::path &path) { return access(path.c_str(), X_OK) == 0; }

bool change_dir(const fs::path &path) { return chdir(path.c_str()) == 0; }

intptr_t lock_file(const fs::path &path)
{
    int fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void unlock_file(intptr_t handle)
{
    if (handle >= 0) close((int)handle);                // closing the descriptor lets the lock go
}

void redirect_output(const fs::path &base)
{
    if (isatty(STDERR_FILENO)) return;
    int out = open((base.string() + ".log").c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    int err = open((base.string() + ".log.err").c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (out >= 0) { dup2(out, STDOUT_FILENO); close(out); }
    if (err >= 0) { dup2(err, STDERR_FILENO); close(err); }
}

unsigned long process_id() { return (unsigned long)getpid(); }

bool start(const std::vector<std::string> &argv, const std::vector<std::string> *env, Process *process,
           std::string *error)
{
    std::vector<char *> args, envp;
    for (const std::string &a : argv) args.push_back(const_cast<char *>(a.c_str()));
    args.push_back(nullptr);
    if (env) {
        for (const std::string &e : *env) envp.push_back(const_cast<char *>(e.c_str()));
        envp.push_back(nullptr);
    }
    pid_t pid = 0;
    int rc = posix_spawn(&pid, args[0], nullptr, nullptr, args.data(), env ? envp.data() : environ);
    if (rc != 0) {
        process->id = 0;
        *error = strerror(rc);
        return false;
    }
    process->id = pid;
    return true;
}

static int ended(Process &process, int options)
{
    if (!process.id) return -1;
    int status = 0;
    pid_t done = waitpid((pid_t)process.id, &status, options);
    if (done == 0 || (done < 0 && errno == EINTR)) return -1;
    process.id = 0;
    return done == (pid_t)-1 ? 4 : WIFEXITED(status) ? WEXITSTATUS(status) : 4;
}

int finished(Process &process) { return ended(process, WNOHANG); }

int wait(Process &process)
{
    int code;
    while ((code = ended(process, 0)) < 0 && process.id) {}
    return code;
}

std::string read_shared(const fs::path &path, uint64_t offset, size_t max)
{
    std::ifstream f(path, std::ios::binary);
    if (!f || !f.seekg((std::streamoff)offset)) return {};
    std::string out(max, '\0');
    f.read(out.data(), (std::streamsize)max);
    out.resize((size_t)f.gcount());
    return out;
}

std::string run_in_place(const std::vector<std::string> &argv, const fs::path &dir)
{
    if (!dir.empty() && chdir(dir.c_str()) != 0) return strerror(errno);
    std::vector<char *> args;
    for (const std::string &a : argv) args.push_back(strdup(a.c_str()));
    args.push_back(nullptr);
    fflush(stdout);
    fflush(stderr);
    execv(args[0], args.data());
    return strerror(errno);
}

std::string replace_with(const std::vector<std::string> &argv) { return run_in_place(argv, {}); }

bool writable(const fs::path &dir) { return access(dir.c_str(), W_OK) == 0; }

#else // _WIN32

namespace
{

std::wstring wide(const std::string &text)
{
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n);
    return out;
}

std::string narrow(const wchar_t *text)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string windows_error(DWORD code)
{
    wchar_t *text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&text, 0, nullptr);
    std::string out = text ? narrow(text) : std::string();
    if (text) LocalFree(text);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    return out.empty() ? "Windows error " + std::to_string(code) : out;
}

// CommandLineToArgvW / the C runtime's quoting, trailing backslashes included (setup/main.cpp).
std::wstring quote(const std::wstring &text)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : text) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

} // namespace

fs::path program()
{
    std::wstring raw(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, raw.data(), (DWORD)raw.size());
    if (!n || n >= raw.size()) return {};
    raw.resize(n);
    std::error_code ec;
    fs::path p = fs::canonical(fs::path(raw), ec);
    return ec ? fs::path(raw) : p;
}

fs::path user_base()
{
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &value))) return {};
    fs::path out(value);
    CoTaskMemFree(value);
    return out;
}

void set_env(const char *name, const std::string &value) { _putenv_s(name, value.c_str()); }
void unset_env(const char *name) { _putenv_s(name, ""); }

bool is_program(const fs::path &path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool change_dir(const fs::path &path)
{
    std::error_code ec;
    fs::current_path(path, ec);
    return !ec;
}

intptr_t lock_file(const fs::path &path)
{
    // No sharing at all, as setup/engine.py's game_lock and the old play wrapper open it.
    HANDLE h = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
            fprintf(stderr, "[APP] %s: %s\n", path.string().c_str(), windows_error(error).c_str());
        return -1;
    }
    return (intptr_t)h;
}

void unlock_file(intptr_t handle)
{
    if (handle >= 0) CloseHandle((HANDLE)handle);
}

void redirect_output(const fs::path &base)
{
    // A WIN32 program has no console; a terminal that started it with its output redirected keeps it.
    if (_isatty(_fileno(stderr))) return;
    _wfreopen((base.wstring() + L".log").c_str(), L"a", stdout);
    _wfreopen((base.wstring() + L".log.err").c_str(), L"a", stderr);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

unsigned long process_id() { return (unsigned long)GetCurrentProcessId(); }

namespace
{

std::wstring command_line(const std::vector<std::string> &argv)
{
    std::wstring command = quote(fs::path(argv[0]).wstring());
    for (size_t i = 1; i < argv.size(); i++) command += L" " + quote(wide(argv[i]));
    return command;
}

} // namespace

bool start(const std::vector<std::string> &argv, const std::vector<std::string> *env, Process *process,
           std::string *error)
{
    process->id = 0;
    if (argv.empty()) {
        *error = "nothing to start";
        return false;
    }
    std::wstring program = fs::path(argv[0]).wstring();
    std::wstring command = command_line(argv);
    std::wstring block;                                 // NAME=value\0...\0\0, as CreateProcessW takes it
    if (env) {
        for (const std::string &e : *env) block += wide(e) + L'\0';
        block += L'\0';
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NO_WINDOW | (env ? CREATE_UNICODE_ENVIRONMENT : 0);
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, flags,
                        env ? (void *)block.data() : nullptr, nullptr, &si, &pi)) {
        *error = windows_error(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    process->id = (intptr_t)pi.hProcess;
    return true;
}

static int ended(Process &process, DWORD timeout)
{
    if (!process.id) return -1;
    HANDLE h = (HANDLE)process.id;
    if (WaitForSingleObject(h, timeout) != WAIT_OBJECT_0) return -1;
    DWORD code = 4;
    if (!GetExitCodeProcess(h, &code)) code = 4;
    CloseHandle(h);
    process.id = 0;
    return (int)code;
}

int finished(Process &process) { return ended(process, 0); }
int wait(Process &process) { return ended(process, INFINITE); }

std::string read_shared(const fs::path &path, uint64_t offset, size_t max)
{
    HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string out;
    LARGE_INTEGER at;
    at.QuadPart = (LONGLONG)offset;
    if (SetFilePointerEx(file, at, nullptr, FILE_BEGIN)) {
        out.resize(max);
        DWORD read = 0;
        if (!ReadFile(file, out.data(), (DWORD)max, &read, nullptr)) read = 0;
        out.resize(read);
    }
    CloseHandle(file);                                  // at once: the engine may replace it next
    return out;
}

std::string run_in_place(const std::vector<std::string> &argv, const fs::path &dir)
{
    if (argv.empty()) return "nothing to start";
    std::wstring program = fs::path(argv[0]).wstring();
    std::wstring command = command_line(argv);
    std::wstring cwd = dir.wstring();
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    fflush(stdout);
    fflush(stderr);
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi))
        return windows_error(GetLastError());
    CloseHandle(pi.hThread);
    // The window is gone; this process only keeps play.lock until the game ends (D215: the app
    // closes after a session). Steam and the shell see the session last as long as the game.
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    fprintf(stderr, "[APP] the game ended (exit %lu)\n", (unsigned long)code);
    fflush(stderr);
    exit((int)code);
}

std::string replace_with(const std::vector<std::string> &argv)
{
    Process next;
    std::string error;
    fflush(stdout);
    fflush(stderr);
    if (!start(argv, nullptr, &next, &error)) return error;
    CloseHandle((HANDLE)next.id);
    exit(0);
}

bool writable(const fs::path &dir)
{
    fs::path probe = dir / (".write-probe-" + std::to_string(GetCurrentProcessId()));
    HANDLE h = CreateFileW(probe.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

#endif

} // namespace os
} // namespace app
