// Game-free Windows bootstrap, native wizard, and stable installation launcher.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
// One folder for everything: the engine's install location is <folder>\app and its data
// location (dump copy, saves, settings, texture packs) <folder>\data. Asking a player for two
// folders, which must not overlap, was the setup's most common way to fail.
static HWND window, dumpEdit, installEdit, statusLabel, startButton, cancelButton, prerequisites, desktopShortcut, launchInfo;
static HWND progressBar;    // overall progress, 0-1000, from the engine's <status>.progress file
static HWND detailsButton, logView;     // Show details: the session log, followed live below the buttons
static unsigned long long logOffset = 0;
static std::string logPending;          // bytes after the last complete line, shown once it ends
static bool detailsShown = false;
static constexpr int WIZARD_HEIGHT = 560, LOG_VIEW_HEIGHT = 200, LOG_VIEW_CHARS = 200000;
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

// The engine publishes progress by replacing the status file (os.replace) while the wizard polls it.
// Windows refuses that replace while any open handle lacks FILE_SHARE_DELETE, which std::ifstream's
// does, so read it with every sharing flag and close it at once (upstream issue #40).
static bool readShared(const fs::path& path, std::string& text) {
    text.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    DWORD read = 0;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= 65536;
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

// An update started from the game's launcher (--update): the installation's own locations,
// no dump (the engine reuses the installed game files), and it starts by itself.
static bool g_update;
static fs::path g_update_install, g_update_data;
static DWORD g_wait_pid;

static fs::path folder() { return fs::path(editText(installEdit)); }
static fs::path installDir() { return g_update ? g_update_install : folder() / L"app"; }
static fs::path dataDir() { return g_update ? g_update_data : folder() / L"data"; }

// Lower-case, absolute and normalised, for comparing paths the way Windows does.
static std::wstring comparable(const fs::path& p) {
    std::error_code ec;
    auto full = fs::weakly_canonical(fs::absolute(p, ec), ec).wstring();
    for (auto& c : full) c = (wchar_t)towlower(c);
    while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
    return full;
}

static bool inside(const fs::path& child, const fs::path& parent) {
    auto c = comparable(child), p = comparable(parent);
    return c == p || (c.size() > p.size() && c.compare(0, p.size(), p) == 0 && (c[p.size()] == L'\\' || p.back() == L'\\'));
}

static void setBusy(bool value) {
    running = value;
    for (int id = 101; id <= 109; ++id) EnableWindow(GetDlgItem(window, id), !value);
    EnableWindow(prerequisites, !value);
    EnableWindow(desktopShortcut, !value);
    EnableWindow(GetDlgItem(window, 112), FALSE);
    SetWindowTextW(cancelButton, value ? L"Cancel" : L"Close");
}

// Appends what the engine has written to the session log since the last tick, whole lines only
// (a UTF-8 character never splits), keeping the newest LOG_VIEW_CHARS. A reader scrolled up stays
// where they are; at the bottom, the view follows the build.
static void tailLog() {
    if (logPath.empty()) return;
    std::ifstream in(logPath, std::ios::binary);
    if (!in) return;
    in.seekg(0, std::ios::end);
    auto size = (unsigned long long)in.tellg();
    if (size < logOffset) { logOffset = 0; logPending.clear(); }
    if (size == logOffset) return;
    in.seekg((std::streamoff)logOffset);
    std::string chunk((size_t)(size - logOffset), '\0');
    in.read(chunk.data(), (std::streamsize)chunk.size());
    chunk.resize((size_t)in.gcount());
    logOffset += chunk.size();
    logPending += chunk;
    auto end = logPending.rfind('\n');
    if (end == std::string::npos) return;
    std::wstring text;
    for (auto c : utf8(logPending.substr(0, end + 1))) {
        if (c == L'\n') text += L"\r\n";
        else if (c != L'\r') text += c;
    }
    logPending.erase(0, end + 1);
    if (text.size() > (size_t)LOG_VIEW_CHARS) text.erase(0, text.size() - LOG_VIEW_CHARS);

    SCROLLINFO si{sizeof(si), SIF_ALL};
    bool follow = !GetScrollInfo(logView, SB_VERT, &si) || si.nPos + (int)si.nPage > si.nMax;
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(logView, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    int first = (int)SendMessageW(logView, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(logView, WM_SETREDRAW, FALSE, 0);
    int length = GetWindowTextLengthW(logView);
    if (length + (int)text.size() > LOG_VIEW_CHARS) {
        // Drop whole lines from the top.
        int line = (int)SendMessageW(logView, EM_LINEFROMCHAR, length + (int)text.size() - LOG_VIEW_CHARS, 0) + 1;
        int cut = (int)SendMessageW(logView, EM_LINEINDEX, line, 0);
        if (cut < 0) { cut = length; line = (int)SendMessageW(logView, EM_GETLINECOUNT, 0, 0); }
        SendMessageW(logView, EM_SETSEL, 0, cut);
        SendMessageW(logView, EM_REPLACESEL, FALSE, (LPARAM)L"");
        length -= cut; first = first > line ? first - line : 0;
        selStart = selStart > (DWORD)cut ? selStart - cut : 0; selEnd = selEnd > (DWORD)cut ? selEnd - cut : 0;
    }
    SendMessageW(logView, EM_SETSEL, length, length);
    SendMessageW(logView, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    if (follow) {
        length = GetWindowTextLengthW(logView);
        SendMessageW(logView, EM_SETSEL, length, length);
        SendMessageW(logView, WM_VSCROLL, SB_BOTTOM, 0);
    } else {
        SendMessageW(logView, EM_SETSEL, selStart, selEnd);
        SendMessageW(logView, EM_LINESCROLL, 0, first - (int)SendMessageW(logView, EM_GETFIRSTVISIBLELINE, 0, 0));
    }
    SendMessageW(logView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(logView, nullptr, TRUE);
}

static void showDetails(bool show) {
    detailsShown = show;
    SetWindowTextW(detailsButton, show ? L"Hide details" : L"Show details");
    ShowWindow(logView, show ? SW_SHOW : SW_HIDE);
    RECT r; GetWindowRect(window, &r);
    int height = WIZARD_HEIGHT + (show ? LOG_VIEW_HEIGHT + 12 : 0);
    // Taller, the window moves up rather than hang below the taskbar.
    RECT work; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int top = r.top + height > work.bottom ? (std::max)((int)work.top, (int)work.bottom - height) : r.top;
    SetWindowPos(window, nullptr, r.left, top, r.right - r.left, height, SWP_NOZORDER);
    if (show) {
        int length = GetWindowTextLengthW(logView);
        SendMessageW(logView, EM_SETSEL, length, length);
        SendMessageW(logView, WM_VSCROLL, SB_BOTTOM, 0);
    }
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CTLCOLORSTATIC && (HWND)lp == logView) {
        // Opaque: a transparent background smears when the log scrolls.
        SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    if (message == WM_CTLCOLORSTATIC) {
        // Labels (and the read-only status box) on the window's own white, not on grey boxes.
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    if (message == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == 102) browse(dumpEdit, false);
        if (id == 103) browse(dumpEdit, true);
        if (id == 105) browse(installEdit, true);
        if (id == 109 && !running) {
            try {
                if (!g_update && editText(dumpEdit).empty()) throw std::runtime_error("Choose your Xbox dump first.");
                if (editText(installEdit).empty()) throw std::runtime_error("Choose a folder to install the game into.");
                if (!g_update && !preparationSmoke && inside(editText(dumpEdit), folder()))
                    throw std::runtime_error("Your dump is inside the install folder. Move the ISO to another folder "
                                             "(for example Documents), or choose a different install folder, then try again.");
                if (!g_update && !preparationSmoke && inside(folder(), editText(dumpEdit)))
                    throw std::runtime_error("The install folder is inside your dump folder. Choose an install folder "
                                             "somewhere else, then try again.");
                prepareSession();
                std::vector<std::wstring> args{L"--install-dir", installDir().wstring(),
                    L"--data-dir", dataDir().wstring(), L"--status-file", statusPath.wstring(), L"--cancel-file", cancelPath.wstring()};
                if (g_update) args.push_back(L"--reuse-extracted");
                else { args.push_back(L"--dump"); args.push_back(editText(dumpEdit)); }
                if (SendMessageW(prerequisites, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    args.push_back(L"--install-prerequisites");
                if (SendMessageW(desktopShortcut, BM_GETCHECK, 0, 0) != BST_CHECKED)
                    args.push_back(L"--no-desktop-shortcut");
                setBusy(true); timerTicks = 0; SetTimer(window, 1, 400, nullptr);
                SendMessageW(progressBar, PBM_SETPOS, 0, 0);
                SetWindowTextW(logView, L""); logOffset = 0; logPending.clear();
                SetWindowTextW(statusLabel, L"Preparing setup files... You can move the window or cancel.");
                std::thread([args]() {
                    try {
                        if (g_wait_pid) {
                            // The game that asked for this update closes itself; its files must be free.
                            HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, g_wait_pid);
                            if (old) { WaitForSingleObject(old, 30000); CloseHandle(old); }
                        }
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
        if (id == 117) showDetails(!detailsShown);
        if (id == 112) {
            auto launcher = installDir() / L"DefJamLauncher.exe";
            ShellExecuteW(window, L"open", launcher.c_str(), L"--launch", nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    if (message == WM_SETUP_READY) { preparationTicks = timerTicks; worker = (HANDLE)wp; return 0; }
    if (message == WM_SETUP_FAILED) {
        auto error = reinterpret_cast<std::wstring*>(lp);
        auto text = *error + L"\n\nLog: " + logPath.wstring(); delete error;
        KillTimer(window, 1); setBusy(false); SetWindowTextW(statusLabel, text.c_str());
        tailLog();
        if (preparationSmoke) DestroyWindow(window);
        else { showDetails(true); MessageBoxW(window, text.c_str(), L"Setup", MB_OK | MB_ICONERROR); }
        return 0;
    }
    if (message == WM_TIMER) {
        ++timerTicks;
        tailLog();
        if (!worker) return 0;
        {
            std::string text;
            if (readShared(statusPath, text) && !text.empty()) SetWindowTextW(statusLabel, utf8(text).c_str());
        }
        {
            std::string text;
            int permille = readShared(statusPath.wstring() + L".progress", text) && !text.empty() ? atoi(text.c_str()) : -1;
            if (permille >= 0 && permille <= 1000)
                SendMessageW(progressBar, PBM_SETPOS, (WPARAM)permille, 0);
        }
        if (WaitForSingleObject(worker, 0) == WAIT_OBJECT_0) {
            DWORD code; GetExitCodeProcess(worker, &code); CloseHandle(worker); worker = nullptr;
            KillTimer(window, 1); setBusy(false);
            tailLog();     // the engine's last lines, written as it exited
            EnableWindow(GetDlgItem(window, 112), code == 0);
            if (code == 0) {
                SendMessageW(progressBar, PBM_SETPOS, 1000, 0);
                auto path = installDir() / L"DefJamLauncher.exe";
                auto text = L"To play, select Play or double-click this file:\r\n" + path.wstring();
                SetWindowTextW(launchInfo, text.c_str());
                SetWindowTextW(statusLabel, L"Setup complete. Your game is ready to launch.");
            }
            if (preparationSmoke) {
                std::ifstream in(logPath); std::string log((std::istreambuf_iterator<char>(in)), {});
                smokeResult = code == 2 && preparationTicks >= 3 && log.find("separate, non-nested") != std::string::npos ? 0 : 4;
                DestroyWindow(window); return 0;
            }
            if (code) {
                showDetails(true);
                auto text = L"Setup stopped (exit " + std::to_wstring(code) + L").\n\nLog: " + logPath.wstring() +
                            L"\n\nIts last lines are shown under the buttons; Open logs opens the whole file.";
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

// --ui-shot FILE.bmp: draw the wizard off the desktop, write its picture and quit (README images;
// nothing is installed and no window appears on screen).
static std::wstring g_ui_shot;

static bool write_window_bmp(HWND hwnd, const std::wstring& path) {
    RECT r; GetWindowRect(hwnd, &r);
    int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    bool ok = PrintWindow(hwnd, dc, 2 /* PW_RENDERFULLCONTENT */) != 0;
    if (ok) {
        FILE* f = _wfopen(path.c_str(), L"wb");
        ok = f != nullptr;
        if (f) {
            BITMAPFILEHEADER fh{}; fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
            fh.bfSize = fh.bfOffBits + (DWORD)w * h * 4;
            fwrite(&fh, sizeof(fh), 1, f); fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
            fwrite(bits, 4, (size_t)w * h, f); fclose(f);
        }
    }
    SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc); ReleaseDC(nullptr, screen);
    return ok;
}

static int wizard(HINSTANCE instance, bool smoke = false, bool preparation = false) {
    preparationSmoke = preparation;
    WNDCLASSW wc{}; wc.lpfnWndProc = windowProc; wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(1));  // setup/app.rc.in
    wc.lpszClassName = L"DefJamSetupWizard"; RegisterClassW(&wc);
    window = CreateWindowW(wc.lpszClassName, L"FFNY Online \u2014 Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 740, WIZARD_HEIGHT, nullptr, nullptr, instance, nullptr);
    control(L"STATIC", L"Build and play from your own USA Xbox dump", 0, 24, 20, 660, 24, 0);
    control(L"STATIC", L"1. Choose your game image.  2. Keep or change the install folder.  3. Select Install. "
                       L"The first install can take up to an hour.", 0, 24, 46, 680, 36, 0);
    control(L"STATIC", L"Your game: the ISO/XISO file (or an extracted folder)", 0, 24, 90, 580, 20, 0);
    dumpEdit = control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 24, 113, 465, 26, 101);
    control(L"BUTTON", L"Image...", WS_TABSTOP, 499, 113, 86, 26, 102);
    control(L"BUTTON", L"Folder...", WS_TABSTOP, 595, 113, 90, 26, 103);
    control(L"STATIC", L"Install folder (the game, your saves and settings all go here; keep your ISO outside it)", 0, 24, 154, 670, 20, 0);
    auto root = localAppData() / L"DefJamRecompiled";
    installEdit = control(L"EDIT", root.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 24, 177, 560, 26, 104);
    control(L"BUTTON", L"Browse...", WS_TABSTOP, 595, 177, 90, 26, 105);
    prerequisites = control(L"BUTTON", L"Install missing Microsoft Build Tools if needed (Windows asks for permission)",
        WS_TABSTOP | BS_AUTOCHECKBOX, 24, 220, 675, 25, 108);
    SendMessageW(prerequisites, BM_SETCHECK, BST_CHECKED, 0);
    // HD textures are made in the launcher (Textures page), where they can be made again or removed;
    // the setup no longer offers them, so the first install stays short. --hd-textures still works.
    control(L"STATIC", L"HD textures: make them later in the launcher, on its Textures page.", 0, 24, 256, 670, 20, 0);
    desktopShortcut = control(L"BUTTON", L"Create a desktop shortcut", WS_TABSTOP | BS_AUTOCHECKBOX, 24, 300, 675, 25, 113);
    SendMessageW(desktopShortcut, BM_SETCHECK, BST_CHECKED, 0);
    progressBar = control(PROGRESS_CLASSW, L"", PBS_SMOOTH, 24, 341, 660, 18, 116);
    SendMessageW(progressBar, PBM_SETRANGE32, 0, 1000);
    statusLabel = control(L"STATIC", L"Choose your game image, then select Install.", 0, 24, 365, 670, 34, 0);
    launchInfo = control(L"EDIT", L"After setup, select Play, or use the desktop shortcut.",
                        WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 24, 405, 660, 46, 114);
    startButton = control(L"BUTTON", L"Install", WS_TABSTOP | BS_DEFPUSHBUTTON, 24, 462, 140, 30, 109);
    control(L"BUTTON", L"Open logs", WS_TABSTOP, 180, 462, 110, 30, 111);
    control(L"BUTTON", L"Play", WS_TABSTOP, 306, 462, 90, 30, 112);
    EnableWindow(GetDlgItem(window, 112), FALSE);
    detailsButton = control(L"BUTTON", L"Show details", WS_TABSTOP, 412, 462, 110, 30, 117);
    cancelButton = control(L"BUTTON", L"Close", WS_TABSTOP, 595, 462, 90, 30, 110);
    logView = control(L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                      24, 504, 660, LOG_VIEW_HEIGHT, 118);
    SendMessageW(logView, EM_SETLIMITTEXT, 2 * LOG_VIEW_CHARS, 0);
    SendMessageW(logView, WM_SETFONT, (WPARAM)CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"), TRUE);
    ShowWindow(logView, SW_HIDE);
    if (!g_ui_shot.empty()) {
        SetWindowTextW(dumpEdit, L"D:\\Games\\Def Jam - Fight for NY (USA).iso");
        SetWindowTextW(installEdit, L"D:\\Games\\FFNY Online");
        SetWindowPos(window, nullptr, -4000, -4000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        for (ULONGLONG until = GetTickCount64() + 600; GetTickCount64() < until;) {
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
            Sleep(10);
        }
        bool ok = write_window_bmp(window, g_ui_shot);
        DestroyWindow(window);
        return ok ? 0 : 4;
    }
    if (smoke) {
        // Hardware-free CI probe: controls exist and the wizard needs no Python,
        // D3D device, compiler or payload extraction merely to open.
        wchar_t title[128]{}; GetWindowTextW(window, title, 128);
        bool valid = wcscmp(title, L"FFNY Online \u2014 Setup") == 0 &&
                     window && dumpEdit && installEdit && prerequisites && desktopShortcut && launchInfo &&
                     detailsButton && logView && !IsWindowVisible(logView) &&
                     SendMessageW(desktopShortcut, BM_GETCHECK, 0, 0) == BST_CHECKED &&
                     startButton && cancelButton && !editText(installEdit).empty() && IsWindowEnabled(startButton);
        DestroyWindow(window);
        return valid ? 0 : 4;
    }
    if (g_update) {
        // Everything is known: show what happens and start.
        SetWindowTextW(window, L"FFNY Online — Update");
        SetWindowTextW(dumpEdit, L"(the game files you installed before are reused)");
        SetWindowTextW(installEdit, g_update_install.parent_path().c_str());
        for (int id : { 101, 102, 103, 104, 105 }) EnableWindow(GetDlgItem(window, id), FALSE);
        SetWindowTextW(startButton, L"Update");
        ShowWindow(window, SW_SHOW);
        PostMessageW(window, WM_COMMAND, 109, 0);
    } else if (preparation) {
        // A dump inside the install location: the engine must refuse it ("separate, non-nested").
        auto probe = localAppData() / L"DefJamSetup/probe";
        SetWindowTextW(dumpEdit, (probe / L"app/missing.iso").c_str());
        SetWindowTextW(installEdit, probe.c_str());
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
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS}; InitCommonControlsEx(&cc);
    int count; LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
    int result = 4;
    try {
        if (count > 1 && std::wstring(values[1]) == L"--launch") {
            launchInstalled(); result = 0;
        } else {
#ifdef DEFJAM_LAUNCHER_ONLY
            if (count == 1) { launchInstalled(); result = 0; }
            else throw std::runtime_error("Double-click this launcher or use --launch to start your installed game.");
#else
            if (count > 2 && std::wstring(values[1]) == L"--ui-shot") {
                g_ui_shot = values[2];
                result = wizard(instance);
            } else if (count > 1 && std::wstring(values[1]) == L"--ui-smoke") {
                result = wizard(instance, true);
            } else if (count > 1 && std::wstring(values[1]) == L"--ui-preparation-smoke") {
                result = wizard(instance, false, true);
            } else if (count > 1 && std::wstring(values[1]) == L"--update") {
                // From the game's launcher: --update --install-dir X --data-dir Y [--wait-pid N].
                for (int i = 2; i + 1 < count; i += 2) {
                    std::wstring k = values[i];
                    if (k == L"--install-dir") g_update_install = values[i + 1];
                    else if (k == L"--data-dir") g_update_data = values[i + 1];
                    else if (k == L"--wait-pid") g_wait_pid = (DWORD)wcstoul(values[i + 1], nullptr, 10);
                }
                if (g_update_install.empty() || g_update_data.empty())
                    throw std::runtime_error("An update needs --install-dir and --data-dir.");
                g_update = true;
                result = wizard(instance);
            } else if (count > 1 && std::wstring(values[1]) == L"--help") {
                MessageBoxW(nullptr, L"Silent setup:\nDefJamSetup.exe --silent --dump PATH --install-dir PATH --data-dir PATH [--install-prerequisites] [--hd-textures] [--log PATH]\n\nExit codes: 0 success, 2 invalid input, 3 missing prerequisites, 4 failure, 5 busy, 6 cancelled, 3010 restart required.", L"Setup options", MB_OK);
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
