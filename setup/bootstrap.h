// What the setup's bootstrap (main.cpp) shares with its wizard (wizard.cpp) and payload reader (unzip.cpp).
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// The session: a private folder under %LOCALAPPDATA%\DefJamSetup\<guid>, the unpacked payload in it,
// the engine's status/progress files and cancel marker, and the session log.
extern fs::path payload, scratch, statusPath, cancelPath, logPath;

std::wstring utf8(const std::string& text);
std::string narrow(const std::wstring& text);
bool readShared(const fs::path& path, std::string& text);
HANDLE spawn(const fs::path& executable, const std::vector<std::wstring>& args, const fs::path& cwd = {});
DWORD wait(HANDLE process);
fs::path localAppData();
void nativeLog(const std::string& message);
void prepareSession(const std::vector<std::wstring>& args = {});
void unpack();                                   // verifies and unpacks the embedded payload into `payload`
HANDLE startEngine(std::vector<std::wstring> args);
bool runningUnderWine();

// unzip.cpp: a ZIP (stored or deflated entries, no ZIP64, no encryption) unpacked into `destination`
// in this process, every entry's CRC-32 checked. Throws std::runtime_error with a plain message.
void extractZip(const uint8_t* data, size_t size, const fs::path& destination);

// wizard.cpp: the window, and its debug/test switches (--ui-smoke, --ui-snapshot, ...).
int wizardMain(HINSTANCE instance, const std::vector<std::wstring>& args);
