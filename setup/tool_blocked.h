// Why Windows refused to start one of the setup's own tools, in words a player can act on.
// Pure (no Windows headers), so tests/unit/test_setup_tool_blocked.py compiles and checks it.
#pragma once
#include <string>
#include <vector>

struct ToolStartProblem {
    bool blocked = false;                 // a security policy or antivirus stopped the file
    std::wstring message;                 // the card's body text
    std::vector<std::wstring> advice;     // "What you can do" bullets, in order
};

inline std::wstring toolFileName(const std::wstring& path) {
    auto cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

// `error` is the GetLastError() of CreateProcess. Unblocked errors return blocked == false and the old text.
inline ToolStartProblem describeToolStartFailure(unsigned long error, const std::wstring& path) {
    ToolStartProblem p;
    std::wstring code = std::to_wstring(error), file = toolFileName(path);
    if (error == 4551 || error == 1260) {
        p.blocked = true;
        p.message = L"Windows blocked a setup tool (error " + code + L"): " + file + L". "
                    + (error == 4551 ? L"Smart App Control (or an App Control policy) only allows signed apps, and this tool is not signed."
                                     : L"A Windows policy does not allow this tool to run.");
        p.advice = {
            L"• Turn Smart App Control off: Windows Security > App & browser control > Smart App Control settings > Off. Then select Try again.",
            L"• Windows cannot turn it back on without resetting Windows. Microsoft Defender keeps protecting your PC while it is off."};
    } else if (error == 225) {
        p.blocked = true;
        p.message = L"Your antivirus blocked a setup tool (error 225): " + file + L".";
        p.advice = {L"• Allow or restore " + file + L" in your antivirus (check its quarantine), then select Try again."};
    } else {
        p.message = L"Could not start a setup tool (Windows error " + code + L")";
    }
    return p;
}
