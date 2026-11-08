// Release an older Windows shortcut wrapper before setup replaces its executable.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace app { namespace os {

enum class SetupHandoff { Unneeded, Started, Failed };

// If our parent is this install's DefJamLauncher.exe, start argv with private inherited
// handles to us and that wrapper. The caller must leave its window and exit on Started.
// Other parents, and POSIX's exec-based wrappers, need no handoff.
SetupHandoff setup_handoff(const std::filesystem::path &root,
                          const std::vector<std::string> &argv, std::string *error);

// The resumed app calls this before taking locks or running setup. Only the two process
// handles supplied by setup_handoff are inherited; both must exit within the timeout.
bool wait_setup_handoff(const char *app_handle, const char *wrapper_handle,
                        const std::filesystem::path &root, std::string *error,
                        unsigned long timeout_ms = 10000);

} } // namespace app::os
