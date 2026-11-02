// Add to Steam (Windows; design §7): Maintenance's Steam section adds the app to the player's Steam
// library as a non-Steam game that starts the game directly (--play), with the library artwork, and
// removes it again.
//
// The work is setup/engine.py --steam (setup/steam_shortcuts.py), run from the app's payload like the
// update check: it answers in a moment, in a JSON file this reads when it exits. Steam rewrites its
// list of games when it exits, so the list is changed only while Steam is closed. When Steam is
// running the section asks first, then the engine closes Steam (steam.exe -shutdown), makes the
// change and starts Steam again (--close-steam). Exit 4 means Steam was running and was not to be
// closed; 2 means it could not be done, and the message says why.

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

#include "app.h"
#include "app_os.h"
#include "pc_ui_actions.h"
#include "pc_ui_internal.h"

namespace fs = std::filesystem;

namespace app
{
namespace
{

struct Steam
{
    os::Process process;
    std::string action;                 // the request in flight: status, add or remove
    bool asked = false;                 // the status has been asked for since the page opened
    bool known = false;                 // an answer has arrived
    std::string root, user, message, details;
    bool running = false, added = false;
    bool confirm = false;               // asking whether Steam may be closed
    std::string confirm_action;
    bool added_here = false;            // added in this session: Home keeps showing it (IN STEAM)
    std::string failed;                 // why the last add or remove failed, for Home's dialog
} g_steam;

fs::path folder() { return os::user_base() / "DefJamRecompiled"; }
fs::path answer_file() { return folder() / "steam-answer.json"; }

void ask(const std::string &action, bool close)
{
    std::error_code ec;
    fs::create_directories(folder(), ec);
    fs::remove(answer_file(), ec);
    std::vector<std::string> args = {"--steam", action, "--app-program", program().string(), "--steam-record",
                                     (folder() / "steam.json").string(), "--steam-result", answer_file().string()};
    if (!install().root.empty()) args.insert(args.end(), {"--install-dir", install().root.string()});
    if (close) args.push_back("--close-steam");
    std::string error;
    if (!spawn_engine(args, &g_steam.process, &error)) {
        g_steam.known = true;
        g_steam.message = error;
        return;
    }
    g_steam.action = action;
    fprintf(stderr, "[APP] steam: %s%s\n", action.c_str(), close ? " (closing Steam)" : "");
}

void poll()
{
    if (!g_steam.process.id) return;
    int code = os::finished(g_steam.process);
    if (code < 0) return;
    auto values = json_values(os::read_shared(answer_file()));
    g_steam.known = true;
    g_steam.root = values["steam"];
    g_steam.user = values["user"];
    g_steam.running = values["running"] == "true";
    g_steam.added = values["added"] == "true";
    g_steam.message = values.count("message") ? values["message"] : (code ? "Steam could not be changed" : "");
    g_steam.details = g_steam.action == "status" ? "" : values["details"];
    fprintf(stderr, "[APP] steam: %s exit %d; %s; %s\n", g_steam.action.c_str(), code,
            g_steam.added ? "in the library" : "not in the library", g_steam.message.c_str());
    if (code == 4) {                    // Steam started meanwhile: ask again
        g_steam.confirm = true;
        g_steam.confirm_action = g_steam.action;
        g_steam.details.clear();
    } else if (code != 0 && g_steam.action != "status") {
        g_steam.failed = g_steam.message;
    } else if (g_steam.action == "add" && g_steam.added) {
        g_steam.added_here = true;
    }
    g_steam.action.clear();
}

void request(const std::string &action)
{
    g_steam.details.clear();
    if (g_steam.running) {
        g_steam.confirm = true;
        g_steam.confirm_action = action;
    } else {
        ask(action, false);
    }
}

} // namespace

void steam_section()
{
    poll();
    if (!g_steam.asked) {
        g_steam.asked = true;
        ask("status", false);
    }
    ImGui::SeparatorText("Steam");
    bool busy = g_steam.process.id != 0;
    if (!g_steam.known) {
        ImGui::TextDisabled("Looking for Steam...");
        return;
    }
    if (g_steam.root.empty()) {
        ImGui::TextWrapped("%s", g_steam.message.empty() ? "Steam is not installed on this computer."
                                                         : g_steam.message.c_str());
        return;
    }
    std::string owner = g_steam.user.empty() ? "your" : g_steam.user + "'s";
    if (g_steam.added)
        ImGui::TextWrapped("In %s Steam library, with its artwork. Steam starts the game directly, with Steam "
                           "Input and the Steam overlay.", owner.c_str());
    else
        ImGui::TextWrapped("Adds the game to %s Steam library as a non-Steam game, with its library artwork, so "
                           "Steam starts it directly, with Steam Input and the Steam overlay.", owner.c_str());
    if (g_steam.confirm) {
        ImGui::TextWrapped("Steam is running. It has to be closed while the game is %s, because it rewrites its "
                           "list of games when it exits. It is started again afterwards.",
                           g_steam.confirm_action == "add" ? "added" : "removed");
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Close Steam and continue")) {
            g_steam.confirm = false;
            ask(g_steam.confirm_action, true);
        }
        ui_target("steam.close");
        ImGui::SameLine();
        if (ImGui::Button("Cancel##steam")) g_steam.confirm = false;
        ui_target("steam.cancel");
        ImGui::EndDisabled();
    } else {
        ImGui::BeginDisabled(busy);
        if (g_steam.added) {
            if (ImGui::Button("Remove from Steam")) request("remove");
            ui_target("steam.remove");
        } else {
            if (ImGui::Button("Add to Steam")) request("add");
            ui_target("steam.add");
        }
        ImGui::EndDisabled();
    }
    if (busy && g_steam.action != "status")
        ImGui::TextDisabled(g_steam.running ? "Closing Steam and changing its library..." : "Changing Steam's library...");
    else if (!g_steam.details.empty())
        ImGui::TextWrapped("%s", g_steam.details.c_str());
    else if (!g_steam.message.empty() && !g_steam.added && g_steam.action.empty() && g_steam.user.empty())
        ImGui::TextWrapped("%s", g_steam.message.c_str());
}

float steam_play_width()
{
    poll();
    if (!g_steam.asked) {
        g_steam.asked = true;
        ask("status", false);
    }
    // Nothing until Steam's state is known, without Steam or an account in it, or once the game is
    // in the library (unless it was added here, which IN STEAM confirms).
    if (!g_steam.known || g_steam.root.empty() || g_steam.user.empty()) return 0;
    if (g_steam.added && !g_steam.added_here) return 0;
    return ImGui::CalcTextSize("ADD TO STEAM").x + ImGui::GetStyle().FramePadding.x * 4;
}

void steam_play_button(float width, float height)
{
    ImVec2 size(width, height);
    if (g_steam.added) {
        ImGui::BeginDisabled();
        ImGui::Button("IN STEAM", size);
        ImGui::EndDisabled();
        ui_target("home.steam");
        return;
    }
    bool working = g_steam.process.id != 0 && g_steam.action != "status";
    ImGui::BeginDisabled(g_steam.process.id != 0);
    if (ImGui::Button(working ? "ADDING..." : "ADD TO STEAM", size)) {
        g_steam.failed.clear();
        request("add");
    }
    ImGui::EndDisabled();
    ui_target("home.steam");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Adds the game to %s's Steam library with its artwork, so Steam starts it directly.",
                          g_steam.user.c_str());
    // Steam is running (it has to be closed while its list changes), or the add failed.
    const char *popup = "Add to Steam##ask";
    if (((g_steam.confirm && g_steam.confirm_action == "add") || !g_steam.failed.empty()) &&
        !ImGui::IsPopupOpen(popup))
        ImGui::OpenPopup(popup);
    if (ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
        if (!g_steam.failed.empty()) {
            ImGui::TextWrapped("The game was not added to Steam: %s", g_steam.failed.c_str());
            if (ImGui::Button("OK")) {
                g_steam.failed.clear();
                ImGui::CloseCurrentPopup();
            }
            ui_target("steam.ok");
        } else if (!g_steam.confirm) {
            ImGui::CloseCurrentPopup();     // answered on Maintenance meanwhile
        } else {
            ImGui::TextWrapped("Steam is running. It has to be closed while the game is added, because it "
                               "rewrites its list of games when it exits. It is started again afterwards.");
            if (ImGui::Button("Close Steam and continue")) {
                g_steam.confirm = false;
                ask("add", true);
                ImGui::CloseCurrentPopup();
            }
            ui_target("steam.close");
            ImGui::SameLine();
            if (ImGui::Button("Cancel##steam")) {
                g_steam.confirm = false;
                ImGui::CloseCurrentPopup();
            }
            ui_target("steam.cancel");
        }
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}

void steam_section_reset()
{
    if (!g_steam.process.id) g_steam.asked = false;     // ask again (Steam may have started or stopped)
    g_steam.confirm = false;
}

bool steam_busy() { return g_steam.process.id != 0; }

bool steam_remove_now(std::string *message)
{
    std::error_code ec;
    if (!fs::exists(folder() / "steam.json", ec)) return true;      // this app never added it
    fs::path answer = folder() / "steam-uninstall.json";
    std::vector<std::string> args = {"--steam", "remove", "--app-program", program().string(), "--steam-record",
                                     (folder() / "steam.json").string(), "--steam-result", answer.string(),
                                     "--close-steam"};
    if (!install().root.empty()) args.insert(args.end(), {"--install-dir", install().root.string()});
    os::Process process;
    if (!spawn_engine(args, &process, message)) return false;
    int code = os::wait(process);
    auto values = json_values(os::read_shared(answer));
    fs::remove(answer, ec);
    *message = values.count("message") ? values["message"] : "";
    fprintf(stderr, "[APP] uninstall: steam remove exit %d; %s\n", code, message->c_str());
    return code == 0;
}

} // namespace app
