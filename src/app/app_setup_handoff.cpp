#include "app_setup_handoff.h"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdlib.h>

namespace {
namespace fs = std::filesystem;

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

DWORD parent_id(DWORD pid)
{
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    if (snapshot.value != INVALID_HANDLE_VALUE && Process32FirstW(snapshot.value, &entry)) {
        do { if (entry.th32ProcessID == pid) return entry.th32ParentProcessID; }
        while (Process32NextW(snapshot.value, &entry));
    }
    return 0;
}

fs::path image(HANDLE process)
{
    wchar_t path[32768];
    DWORD length = 32768;
    return QueryFullProcessImageNameW(process, 0, path, &length) ? fs::path(path) : fs::path();
}

bool same_path(const fs::path &a, const fs::path &b)
{
    if (a.empty() || b.empty()) return false;
    std::error_code ea, eb;
    auto ca = fs::weakly_canonical(a, ea), cb = fs::weakly_canonical(b, eb);
    return !ea && !eb && _wcsicmp(ca.c_str(), cb.c_str()) == 0;
}

bool older(HANDLE parent, HANDLE child)
{
    FILETIME pc{}, pe{}, pk{}, pu{}, cc{}, ce{}, ck{}, cu{};
    return GetProcessTimes(parent, &pc, &pe, &pk, &pu) &&
           GetProcessTimes(child, &cc, &ce, &ck, &cu) && CompareFileTime(&pc, &cc) <= 0;
}

std::wstring wide(const std::string &text)
{
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
    std::wstring result(size, L'\0');
    if (size) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), result.data(), size);
    return result;
}

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

HANDLE parse_handle(const char *text)
{
    char *end = nullptr;
    auto value = _strtoui64(text, &end, 10);
    return *text && end && !*end && value && value != UINT64_MAX ? (HANDLE)(uintptr_t)value : nullptr;
}
} // namespace
#endif

namespace app { namespace os {

SetupHandoff setup_handoff(const std::filesystem::path &root,
                          const std::vector<std::string> &argv, std::string *error)
{
#if defined(_WIN32)
    DWORD parent = parent_id(GetCurrentProcessId());
    Handle wrapper{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, parent)};
    if (!wrapper.value || !same_path(image(wrapper.value), root / "DefJamLauncher.exe") ||
        !older(wrapper.value, GetCurrentProcess())) return SetupHandoff::Unneeded;
    Handle self{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, GetCurrentProcessId())};
    if (!self.value || argv.empty()) {
        *error = "Could not prepare the launcher handoff.";
        return SetupHandoff::Failed;
    }
    // Explicit inheritance avoids leaking play.lock, the engine, or unrelated app handles.
    HANDLE handles[] = {self.value, wrapper.value};
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) {
        *error = "Could not prepare the launcher handoff handles.";
        return SetupHandoff::Failed;
    }
    bool ready = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                          handles, sizeof handles, nullptr, nullptr) != FALSE;
    std::wstring command = quote(wide(argv[0])) + L" --setup-handoff " +
        std::to_wstring((uintptr_t)self.value) + L" " + std::to_wstring((uintptr_t)wrapper.value) +
        L" " + quote(root.wstring());
    for (size_t i = 1; i < argv.size(); ++i) command += L" " + quote(wide(argv[i]));
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.lpAttributeList = attributes;
    PROCESS_INFORMATION pi{};
    bool started = ready && CreateProcessW(wide(argv[0]).c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi);
    DWORD code = GetLastError();
    DeleteProcThreadAttributeList(attributes);
    if (!started) {
        *error = "Could not restart for setup (Windows error " + std::to_string(code) + ").";
        return SetupHandoff::Failed;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return SetupHandoff::Started;
#else
    (void)root; (void)argv; (void)error;
    return SetupHandoff::Unneeded;
#endif
}

bool wait_setup_handoff(const char *app_handle, const char *wrapper_handle,
                        const std::filesystem::path &root, std::string *error, unsigned long timeout_ms)
{
#if defined(_WIN32)
    Handle previous{parse_handle(app_handle)}, wrapper{parse_handle(wrapper_handle)};
    if (!previous.value || !wrapper.value || previous.value == wrapper.value ||
        !GetProcessId(previous.value) || !GetProcessId(wrapper.value) ||
        (WaitForSingleObject(previous.value, 0) == WAIT_TIMEOUT &&
         !same_path(image(previous.value), image(GetCurrentProcess()))) ||
        (WaitForSingleObject(wrapper.value, 0) == WAIT_TIMEOUT &&
         !same_path(image(wrapper.value), root / "DefJamLauncher.exe")) ||
        GetProcessId(previous.value) != parent_id(GetCurrentProcessId()) ||
        !older(wrapper.value, previous.value)) {
        // Do not close a duplicate handle twice on malformed input.
        if (previous.value == wrapper.value) wrapper.value = nullptr;
        *error = "The setup launcher handoff is invalid. Close the app and run setup directly.";
        return false;
    }
    ULONGLONG started = GetTickCount64();
    for (HANDLE h : {previous.value, wrapper.value}) {
        ULONGLONG elapsed = GetTickCount64() - started;
        DWORD remaining = elapsed >= timeout_ms ? 0 : timeout_ms - (DWORD)elapsed;
        if (WaitForSingleObject(h, remaining) != WAIT_OBJECT_0) {
            *error = "The old launcher did not close. Close Def Jam and run setup directly; your installation is unchanged.";
            return false;
        }
    }
    return true;
#else
    (void)app_handle; (void)wrapper_handle; (void)root; (void)timeout_ms;
    *error = "A Windows setup handoff cannot be used on this platform.";
    return false;
#endif
}

} } // namespace app::os
