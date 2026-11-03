// What the app needs from the operating system, in one place: the program's own path, the usual
// folders, the environment, the install's play.lock and starting the game. POSIX (macOS, Linux) and
// Windows halves in app_os.cpp; paths are UTF-8 (the Windows app runs with the UTF-8 code page,
// src/app/app.manifest, and SDL hands main its arguments in UTF-8).
#pragma once

#include <stdint.h>

#include <filesystem>
#include <string>
#include <vector>

namespace app
{
namespace os
{

// The running program, resolved.
std::filesystem::path program();

// Where per-user application files go: %LOCALAPPDATA% on Windows, the home folder elsewhere
// (empty when there is none). On Windows DEFJAM_APP_USER_BASE replaces it (a test hook).
std::filesystem::path user_base();

// The environment the app and the game it starts read (UTF-8).
void set_env(const char *name, const std::string &value);
void unset_env(const char *name);

// Whether a file is a program that can be started (Windows: any existing file).
bool is_program(const std::filesystem::path &path);

// The working directory for the game.
bool change_dir(const std::filesystem::path &path);

// An exclusive, non-blocking lock on `path` (created if missing), as setup/engine.py takes it:
// flock off Windows, a file opened with no sharing on Windows. -1 when another process holds it.
// The lock goes with the handle when it is closed or the process ends.
intptr_t lock_file(const std::filesystem::path &path);
void unlock_file(intptr_t handle);

// Send this process's stdout and stderr to `base`.log and `base`.log.err, unless they go to a
// terminal (a person running it by hand keeps the output).
void redirect_output(const std::filesystem::path &base);

// This process's id, for unique scratch folder names.
unsigned long process_id();

// A program the app started and watches (the setup engine): a pid off Windows, a process handle on
// Windows; 0 when there is none.
struct Process
{
    intptr_t id = 0;
};

// Start `argv` (argv[0] is the program) without a console window. `env` is the whole environment
// as NAME=value strings, or null for this process's own. Returns false with the reason.
bool start(const std::vector<std::string> &argv, const std::vector<std::string> *env, Process *process,
           std::string *error);

// Whether it has ended: -1 while it runs; otherwise its exit code (4 when it was killed), and the
// process is forgotten (id 0). `wait` blocks until it ends.
int finished(Process &process);
int wait(Process &process);

// A file's bytes from `offset`, at most `max` of them, read so that another process may replace or
// delete the file meanwhile (the engine replaces its status file with os.replace; on Windows that
// fails while any reader lacks delete sharing, issue #40). Empty when it cannot be read.
std::string read_shared(const std::filesystem::path &path, uint64_t offset = 0, size_t max = 65536);

// Start `argv` (argv[0] is the program) in `dir`. Off Windows the program replaces this one
// (exec): open descriptors, such as play.lock's, stay with it. On Windows the program is started
// and waited for, and this process then exits with its exit code: the app's handles, play.lock's
// among them, stay open until then. Returns only when the program could not be started, with
// the reason.
std::string run_in_place(const std::vector<std::string> &argv, const std::filesystem::path &dir);

// Become `argv` (the app's own updated copy): exec off Windows; on Windows it is started and this
// process exits at once, so the new copy can remove the old program file. Returns only on failure.
std::string replace_with(const std::vector<std::string> &argv);

// Whether files can be created in `dir` (a probe file is made and removed).
bool writable(const std::filesystem::path &dir);

} // namespace os
} // namespace app
