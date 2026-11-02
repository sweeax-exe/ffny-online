// Game-free Windows bootstrap, silent installer and stable installation launcher. The wizard window
// is in wizard.cpp; the payload ZIP is read in unzip.cpp.
#include "bootstrap.h"
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <fstream>
#include <stdexcept>
#include <algorithm>

fs::path payload, scratch, statusPath, cancelPath, logPath;

std::wstring utf8(const std::string& text) {
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), size);
    return result;
}

std::string narrow(const std::wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), size, nullptr, nullptr);
    return result;
}

// The engine publishes progress by replacing the status file (os.replace) while the wizard polls it.
// Windows refuses that replace while any open handle lacks FILE_SHARE_DELETE, which std::ifstream's
// does, so read it with every sharing flag and close it at once (upstream issue #40).
bool readShared(const fs::path& path, std::string& text) {
    text.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    DWORD read = 0;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= 1048576;
    if (ok) {
        text.resize((size_t)size.QuadPart);
        ok = text.empty() || ReadFile(file, text.data(), (DWORD)text.size(), &read, nullptr);
        text.resize(ok ? read : 0);
    }
    CloseHandle(file);
    return ok;
}

// CommandLineToArgvW/CreateProcess quoting, including trailing backslashes.
static std::wstring quote(const std::wstring& text) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto c : text) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

HANDLE spawn(const fs::path& executable, const std::vector<std::wstring>& args, const fs::path& cwd) {
    std::wstring command = quote(executable.wstring());
    for (const auto& arg : args) command += L" " + quote(arg);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi))
        throw std::runtime_error("Could not start a setup tool (Windows error " + std::to_string(GetLastError()) + ")");
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

DWORD wait(HANDLE process) {
    WaitForSingleObject(process, INFINITE);
    DWORD code = 4;
    GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return code;
}

fs::path localAppData() {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &value)))
        throw std::runtime_error("Cannot locate your application data directory");
    fs::path result(value);
    CoTaskMemFree(value);
    return result;
}

// Wine exports wine_get_version from its ntdll; Windows never does.
bool runningUnderWine() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

static void launchInstalled() {
    wchar_t self[32768], source[32768], data[32768], exe[32768];
    GetModuleFileNameW(nullptr, self, 32768);
    auto root = fs::path(self).parent_path();
    auto receipt = root / L"installed.ini";
    GetPrivateProfileStringW(L"install", L"source", L"", source, 32768, receipt.c_str());
    GetPrivateProfileStringW(L"install", L"data", L"", data, 32768, receipt.c_str());
    GetPrivateProfileStringW(L"install", L"executable", L"", exe, 32768, receipt.c_str());
    if (!*source || !*data || !fs::is_regular_file(exe))
        throw std::runtime_error("No completed installation. Run setup to install or repair it.");
    // Lock held for the full game lifetime: updates cannot replace a running game.
    HANDLE lock = CreateFileW((root / L"play.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                             0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error("The game or an update is already running.");
    SetEnvironmentVariableW(L"DEFJAM_DATA", data);
    // The settings' Maintenance page names the installation and the logs folder from these.
    SetEnvironmentVariableW(L"DEFJAM_INSTALL_DIR", root.c_str());
    SetEnvironmentVariableW(L"DEFJAM_LOG_DIR", (fs::path(source) / L"logs").c_str());
    HANDLE process;
    try { process = spawn(exe, {}, source); }
    catch (...) { CloseHandle(lock); throw; }
    wait(process);
    CloseHandle(lock);
}

#ifndef DEFJAM_LAUNCHER_ONLY
void nativeLog(const std::string& message) {
    if (!logPath.empty()) { std::ofstream out(logPath, std::ios::app); out << message << '\n'; }
}

void prepareSession(const std::vector<std::wstring>& args) {
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot create setup session");
    wchar_t guid[64]; StringFromGUID2(id, guid, 64);
    auto root = localAppData() / L"DefJamSetup";
    scratch = root / guid;
    payload = scratch / L"payload";
    statusPath = scratch / L"status.txt";
    cancelPath = scratch / L"cancel";
    logPath = root / L"logs" / (std::wstring(guid) + L".log");
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == L"--log") logPath = args[i + 1];
    if (!logPath.parent_path().empty()) fs::create_directories(logPath.parent_path());
    fs::create_directories(scratch);
    nativeLog("Preparing setup files. Diagnostics remain here even if validation fails.");
}

static std::wstring sha256(const void* bytes, DWORD size) {
    BCRYPT_ALG_HANDLE alg{};
    BCRYPT_HASH_HANDLE hash{};
    unsigned char value[32];
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 provider unavailable");
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, (PUCHAR)bytes, size, 0) < 0 ||
        BCryptFinishHash(hash, value, sizeof(value), 0) < 0) {
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("Payload hashing failed");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    const wchar_t digits[] = L"0123456789abcdef";
    std::wstring result;
    for (auto c : value) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}

void unpack() {
    auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!resource) throw std::runtime_error("Installer payload missing");
    auto loaded = LoadResource(nullptr, resource);
    auto bytes = LockResource(loaded);
    auto size = SizeofResource(nullptr, resource);
    if (!bytes || sha256(bytes, size) != PAYLOAD_SHA256)
        throw std::runtime_error("Installer payload verification failed. Download it again.");
    // Unique private staging folder; never remove or reuse arbitrary caller paths. Unpacked here, in
    // this process (unzip.cpp), with every entry's checksum checked, then checked for the parts setup
    // starts: an unpack that wrote nothing can no longer pass for one that worked.
    auto started = GetTickCount64();
    extractZip((const uint8_t*)bytes, size, payload);
    for (auto part : {L"manifest.json", L"engine/engine.py", L"python/python.exe", L"DefJamLauncher.exe"})
        if (!fs::is_regular_file(payload / part))
            throw std::runtime_error("The setup files were not unpacked completely (" + narrow(part) +
                                     " is missing). Check disk space and security software, then run setup again.");
    nativeLog("Setup payload prepared (" + std::to_string(GetTickCount64() - started) + " ms).");
}

HANDLE startEngine(std::vector<std::wstring> args) {
    std::vector<std::wstring> command{L"-B", (payload / L"engine/engine.py").wstring(),
        L"--payload", payload.wstring()};
    command.insert(command.end(), args.begin(), args.end());
    bool hasLog = false;
    for (const auto& arg : args) if (arg == L"--log") hasLog = true;
    if (!hasLog) { command.push_back(L"--log"); command.push_back(logPath.wstring()); }
    return spawn(payload / L"python/python.exe", command);
}
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int count; LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> args;
    for (int i = 1; i < count; ++i) args.emplace_back(values[i]);
    bool silent = std::find(args.begin(), args.end(), L"--silent") != args.end();
    int result = 4;
    try {
        if (!args.empty() && args[0] == L"--launch") {
            launchInstalled(); result = 0;
        } else {
#ifdef DEFJAM_LAUNCHER_ONLY
            if (args.empty()) { launchInstalled(); result = 0; }
            else throw std::runtime_error("Double-click this launcher or use --launch to start your installed game.");
#else
            static const wchar_t* windowModes[] = {L"--ui-smoke", L"--ui-preparation-smoke", L"--ui-shot", L"--ui-snapshot",
                                                   L"--ui-demo", L"--update"};
            bool window = args.empty() || std::any_of(std::begin(windowModes), std::end(windowModes),
                                                      [&](const wchar_t* m) { return args[0] == m; });
            if (window) {
                result = wizardMain(instance, args);
            } else if (args[0] == L"--help") {
                MessageBoxW(nullptr, L"Silent setup:\nDefJamSetup.exe --silent --dump PATH --install-dir PATH --data-dir PATH [--install-prerequisites] [--hd-textures] [--no-shortcuts] [--log PATH]\n\nExit codes: 0 success, 2 invalid input, 3 missing prerequisites (or not Windows), 4 failure, 5 busy, 6 cancelled, 3010 restart required.", L"Setup options", MB_OK);
                result = 0;
            } else {
                if (!silent) throw std::runtime_error("Command-line installation requires --silent; use --help for options.");
                prepareSession(args);
                if (runningUnderWine()) {
                    result = 3;
                    throw std::runtime_error("FFNY Online supports Windows only for now; this setup is running under Wine.");
                }
                unpack(); result = (int)wait(startEngine(args));
            }
#endif
        }
    } catch (const std::exception& ex) {
#ifndef DEFJAM_LAUNCHER_ONLY
        nativeLog(std::string("Setup stopped: ") + ex.what());
#endif
        if (result == 0) result = 4;
        if (!silent) MessageBoxW(nullptr, utf8(ex.what()).c_str(), L"FFNY Online Setup", MB_OK | MB_ICONERROR);
    }
    // Retain failed setup's scratch for diagnosis; successful session cleans only its own GUID directory.
    if (result == 0 && !scratch.empty()) { std::error_code error; fs::remove_all(scratch, error); }
    LocalFree(values); CoUninitialize();
    return result;
}
