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
// (empty when there is none).
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

// Start `argv` (argv[0] is the program) in `dir`. Off Windows the program replaces this one
// (exec): open descriptors, such as play.lock's, stay with it. On Windows the program is started
// and waited for, and this process then exits with its exit code: the app's handles, play.lock's
// among them, stay open until then. Returns only when the program could not be started, with
// the reason.
std::string run_in_place(const std::vector<std::string> &argv, const std::filesystem::path &dir);

} // namespace os
} // namespace app
