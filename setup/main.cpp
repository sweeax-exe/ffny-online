// Game-free Windows bootstrap, native wizard, and stable installation launcher.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
static HWND window, dumpEdit, installEdit, dataEdit, statusLabel, startButton, cancelButton, prerequisites;
static HANDLE worker = nullptr;
static fs::path payload, scratch, statusPath, cancelPath, logPath;
static bool running = false;
static bool preparationSmoke = false;
static unsigned timerTicks = 0;
static unsigned preparationTicks = 0;
static int smokeResult = 4;
static constexpr UINT WM_SETUP_READY = WM_APP + 1, WM_SETUP_FAILED = WM_APP + 2;

static std::wstring utf8(const std::string& text) {
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), size);
    return result;
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

static HANDLE spawn(const fs::path& executable, const std::vector<std::wstring>& args, const fs::path& cwd = {}) {
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

static DWORD wait(HANDLE process) {
    WaitForSingleObject(process, INFINITE);
    DWORD code = 4;
    GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return code;
}

static fs::path systemTool(const wchar_t* name) {
    wchar_t buffer[32768];
    GetSystemDirectoryW(buffer, 32768);
    return fs::path(buffer) / name;
}

static fs::path localAppData() {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &value)))
        throw std::runtime_error("Cannot locate your application data directory");
    fs::path result(value);
    CoTaskMemFree(value);
    return result;
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
    HANDLE process;
    try { process = spawn(exe, {}, source); }
    catch (...) { CloseHandle(lock); throw; }
    wait(process);
    CloseHandle(lock);
}

#ifndef DEFJAM_LAUNCHER_ONLY
static void nativeLog(const std::string& message) {
    if (!logPath.empty()) { std::ofstream out(logPath, std::ios::app); out << message << '\n'; }
}

static void prepareSession(const std::vector<std::wstring>& args = {}) {
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

static void unpack() {
    auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!resource) throw std::runtime_error("Installer payload missing");
    auto loaded = LoadResource(nullptr, resource);
    auto bytes = LockResource(loaded);
    auto size = SizeofResource(nullptr, resource);
    if (!bytes || sha256(bytes, size) != PAYLOAD_SHA256)
        throw std::runtime_error("Installer payload verification failed. Download it again.");
    // Unique private staging folder; never remove or reuse arbitrary caller paths.
    auto archive = scratch / L"payload.zip";
    { std::ofstream out(archive, std::ios::binary); out.write((const char*)bytes, size);
      if (!out) throw std::runtime_error("Could not write payload; check disk space"); }
    SetEnvironmentVariableW(L"DEFJAM_SETUP_ARCHIVE", archive.c_str());
    SetEnvironmentVariableW(L"DEFJAM_SETUP_PAYLOAD", payload.c_str());
    DWORD result = wait(spawn(systemTool(L"WindowsPowerShell/v1.0/powershell.exe"),
        {L"-NoProfile", L"-NonInteractive", L"-Command",
         L"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath $env:DEFJAM_SETUP_ARCHIVE -DestinationPath $env:DEFJAM_SETUP_PAYLOAD"}));
    if (result) throw std::runtime_error("Cannot unpack setup payload");
    nativeLog("Setup payload prepared.");
}

static HANDLE startEngine(std::vector<std::wstring> args) {
    std::vector<std::wstring> command{L"-B", (payload / L"engine/engine.py").wstring(),
        L"--payload", payload.wstring()};
    command.insert(command.end(), args.begin(), args.end());
    bool hasLog = false;
    for (const auto& arg : args) if (arg == L"--log") hasLog = true;
    if (!hasLog) { command.push_back(L"--log"); command.push_back(logPath.wstring()); }
    return spawn(payload / L"python/python.exe", command);
}

static std::wstring editText(HWND edit) {
    std::wstring text(GetWindowTextLengthW(edit) + 1, L'\0');
    GetWindowTextW(edit, text.data(), (int)text.size());
    text.resize(wcslen(text.c_str()));
    return text;
}

static void browse(HWND edit, bool folder) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    DWORD options; dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    if (!folder) {
        COMDLG_FILTERSPEC filter[]{{L"Xbox disc images", L"*.iso;*.xiso"}, {L"All files", L"*.*"}};
        dialog->SetFileTypes(2, filter);
    }
    if (SUCCEEDED(dialog->Show(window))) {
        IShellItem* item = nullptr; PWSTR path = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                SetWindowTextW(edit, path); CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
}

static HWND control(const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND result = CreateWindowExW(type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
        type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(result, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return result;
}

static void setBusy(bool value) {
    running = value;
    for (int id = 101; id <= 109; ++id) EnableWindow(GetDlgItem(window, id), !value);
    EnableWindow(prerequisites, !value);
    EnableWindow(GetDlgItem(window, 112), FALSE);
    SetWindowTextW(cancelButton, value ? L"Cancel" : L"Close");
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == 102) browse(dumpEdit, false);
        if (id == 103) browse(dumpEdit, true);
        if (id == 105) browse(installEdit, true);
        if (id == 107) browse(dataEdit, true);
        if (id == 109 && !running) {
            try {
                if (editText(dumpEdit).empty()) throw std::runtime_error("Choose your Xbox dump first.");
                prepareSession();
                std::vector<std::wstring> args{L"--dump", editText(dumpEdit), L"--install-dir", editText(installEdit),
                    L"--data-dir", editText(dataEdit), L"--status-file", statusPath.wstring(), L"--cancel-file", cancelPath.wstring()};
                if (SendMessageW(prerequisites, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    args.push_back(L"--install-prerequisites");
                setBusy(true); timerTicks = 0; SetTimer(window, 1, 400, nullptr);
                SetWindowTextW(statusLabel, L"Preparing setup files... You can move the window or cancel.");
                std::thread([args]() {
                    try {
                        unpack();
                        if (fs::exists(cancelPath)) {
                            nativeLog("Setup cancelled during preparation.");
                            PostMessageW(window, WM_SETUP_FAILED, 6, (LPARAM)new std::wstring(L"Setup cancelled."));
                        } else {
                            auto process = startEngine(args);
                            PostMessageW(window, WM_SETUP_READY, (WPARAM)process, 0);
                        }
                    } catch (const std::exception& ex) {
                        nativeLog(std::string("Setup stopped: ") + ex.what());
                        PostMessageW(window, WM_SETUP_FAILED, 4, (LPARAM)new std::wstring(utf8(ex.what())));
                    }
                }).detach();
            } catch (const std::exception& ex) {
                nativeLog(std::string("Setup stopped: ") + ex.what());
                KillTimer(window, 1); setBusy(false);
                if (!preparationSmoke) MessageBoxW(window, utf8(ex.what()).c_str(), L"Setup", MB_OK | MB_ICONERROR);
            }
        }
        if (id == 110) SendMessageW(window, WM_CLOSE, 0, 0);
        if (id == 111) {
            auto path = logPath.empty() ? localAppData() / L"DefJamSetup/logs" : logPath;
            fs::create_directories(logPath.empty() ? path : path.parent_path());
            ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (id == 112) {
            auto launcher = fs::path(editText(installEdit)) / L"DefJamLauncher.exe";
            ShellExecuteW(window, L"open", launcher.c_str(), L"--launch", nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    if (message == WM_SETUP_READY) { preparationTicks = timerTicks; worker = (HANDLE)wp; return 0; }
    if (message == WM_SETUP_FAILED) {
        auto error = reinterpret_cast<std::wstring*>(lp);
        auto text = *error + L"\n\nLog: " + logPath.wstring(); delete error;
        KillTimer(window, 1); setBusy(false); SetWindowTextW(statusLabel, text.c_str());
        if (preparationSmoke) DestroyWindow(window);
        else MessageBoxW(window, text.c_str(), L"Setup", MB_OK | MB_ICONERROR);
        return 0;
    }
    if (message == WM_TIMER) {
        ++timerTicks;
        if (!worker) return 0;
        if (fs::exists(statusPath)) {
            std::ifstream in(statusPath); std::string text((std::istreambuf_iterator<char>(in)), {});
            SetWindowTextW(statusLabel, utf8(text).c_str());
        }
        if (WaitForSingleObject(worker, 0) == WAIT_OBJECT_0) {
            DWORD code; GetExitCodeProcess(worker, &code); CloseHandle(worker); worker = nullptr;
            KillTimer(window, 1); setBusy(false);
            EnableWindow(GetDlgItem(window, 112), code == 0);
            if (preparationSmoke) {
                std::ifstream in(logPath); std::string log((std::istreambuf_iterator<char>(in)), {});
                smokeResult = code == 2 && preparationTicks >= 3 && log.find("separate, non-nested") != std::string::npos ? 0 : 4;
                DestroyWindow(window); return 0;
            }
            if (code) {
                auto text = L"Setup stopped (exit " + std::to_wstring(code) + L").\n\nLog: " + logPath.wstring() + L"\n\nSelect Open logs for details.";
                MessageBoxW(window, text.c_str(), L"Setup", MB_OK | MB_ICONINFORMATION);
            }
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        if (running) {
            if (MessageBoxW(window, L"Cancel setup? Completed steps can be resumed.", L"Setup", MB_YESNO) == IDYES) {
                std::ofstream marker(cancelPath); marker << "cancel";
                SetWindowTextW(statusLabel, L"Cancelling the current step...");
            }
        } else DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wp, lp);
}

static int wizard(HINSTANCE instance, bool smoke = false, bool preparation = false) {
    preparationSmoke = preparation;
    WNDCLASSW wc{}; wc.lpfnWndProc = windowProc; wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"DefJamSetupWizard"; RegisterClassW(&wc);
    window = CreateWindowW(wc.lpszClassName, L"Def Jam Recompiled \u2014 Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 740, 470, nullptr, nullptr, instance, nullptr);
    control(L"STATIC", L"Build and play from your own USA Xbox dump", 0, 24, 20, 660, 24, 0);
    control(L"STATIC", L"First setup can take an hour and needs internet for missing Microsoft Build Tools.", 0, 24, 49, 670, 25, 0);
    control(L"STATIC", L"Your ISO/XISO or extracted dump", 0, 24, 87, 580, 20, 0);
    dumpEdit = control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 24, 110, 465, 26, 101);
    control(L"BUTTON", L"Image...", WS_TABSTOP, 499, 110, 86, 26, 102);
    control(L"BUTTON", L"Folder...", WS_TABSTOP, 595, 110, 90, 26, 103);
    control(L"STATIC", L"Install location", 0, 24, 151, 580, 20, 0);
    auto root = localAppData() / L"DefJamRecompiled";
    installEdit = control(L"EDIT", (root / L"app").c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 24, 174, 560, 26, 104);
    control(L"BUTTON", L"Browse...", WS_TABSTOP, 595, 174, 90, 26, 105);
    control(L"STATIC", L"Data location (dump copy, saves and settings; preserved across updates)", 0, 24, 215, 660, 20, 0);
    dataEdit = control(L"EDIT", (root / L"data").c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 24, 238, 560, 26, 106);
    control(L"BUTTON", L"Browse...", WS_TABSTOP, 595, 238, 90, 26, 107);
    prerequisites = control(L"BUTTON", L"Install missing Microsoft Build Tools (requires consent and administrator access)",
        WS_TABSTOP | BS_AUTOCHECKBOX, 24, 278, 675, 25, 108);
    statusLabel = control(L"STATIC", L"Choose your dump and destinations, then select Install / Repair.", 0, 24, 314, 670, 40, 0);
    startButton = control(L"BUTTON", L"Install / Repair", WS_TABSTOP | BS_DEFPUSHBUTTON, 24, 372, 140, 30, 109);
    control(L"BUTTON", L"Open logs", WS_TABSTOP, 180, 372, 110, 30, 111);
    control(L"BUTTON", L"Play", WS_TABSTOP, 306, 372, 90, 30, 112);
    EnableWindow(GetDlgItem(window, 112), FALSE);
    cancelButton = control(L"BUTTON", L"Close", WS_TABSTOP, 595, 372, 90, 30, 110);
    if (smoke) {
        // Hardware-free CI probe: controls exist and the wizard needs no Python,
        // D3D device, compiler or payload extraction merely to open.
        wchar_t title[128]{}; GetWindowTextW(window, title, 128);
        bool valid = wcscmp(title, L"Def Jam Recompiled \u2014 Setup") == 0 &&
                     window && dumpEdit && installEdit && dataEdit && prerequisites &&
                     startButton && cancelButton && !editText(installEdit).empty() &&
                     !editText(dataEdit).empty() && IsWindowEnabled(startButton);
        DestroyWindow(window);
        return valid ? 0 : 4;
    }
    if (preparation) {
        SetWindowTextW(dumpEdit, L"missing.iso");
        auto probe = localAppData() / L"DefJamSetup/probe";
        SetWindowTextW(installEdit, probe.c_str());
        SetWindowTextW(dataEdit, (probe / L"data").c_str());
        PostMessageW(window, WM_COMMAND, 109, 0);
    } else ShowWindow(window, SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return preparation ? smokeResult : 0;
}
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&cc);
    int count; LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
    int result = 4;
    try {
        if (count > 1 && std::wstring(values[1]) == L"--launch") {
            launchInstalled(); result = 0;
        } else {
#ifdef DEFJAM_LAUNCHER_ONLY
            throw std::runtime_error("Use --launch to start your installed game.");
#else
            if (count > 1 && std::wstring(values[1]) == L"--ui-smoke") {
                result = wizard(instance, true);
            } else if (count > 1 && std::wstring(values[1]) == L"--ui-preparation-smoke") {
                result = wizard(instance, false, true);
            } else if (count > 1 && std::wstring(values[1]) == L"--help") {
                MessageBoxW(nullptr, L"Silent setup:\nDefJamSetup.exe --silent --dump PATH --install-dir PATH --data-dir PATH [--install-prerequisites] [--log PATH]\n\nExit codes: 0 success, 2 invalid input, 3 missing prerequisites, 4 failure, 5 busy, 6 cancelled, 3010 restart required.", L"Setup options", MB_OK);
                result = 0;
            } else if (count > 1) {
                bool silent = false;
                std::vector<std::wstring> args;
                for (int i = 1; i < count; ++i) { args.emplace_back(values[i]); if (args.back() == L"--silent") silent = true; }
                if (!silent) throw std::runtime_error("Command-line installation requires --silent; use --help for options.");
                prepareSession(args); unpack(); result = (int)wait(startEngine(args));
            } else result = wizard(instance);
#endif
        }
    } catch (const std::exception& ex) {
#ifndef DEFJAM_LAUNCHER_ONLY
        nativeLog(std::string("Setup stopped: ") + ex.what());
#endif
        bool silent = false;
        for (int i = 1; i < count; ++i) if (std::wstring(values[i]) == L"--silent") silent = true;
        if (!silent) MessageBoxW(nullptr, utf8(ex.what()).c_str(), L"Def Jam Setup", MB_OK | MB_ICONERROR);
    }
    // Retain failed setup's scratch for diagnosis; successful session cleans only its own GUID directory.
    if (result == 0 && !scratch.empty()) { std::error_code error; fs::remove_all(scratch, error); }
    LocalFree(values); CoUninitialize();
    return result;
}
