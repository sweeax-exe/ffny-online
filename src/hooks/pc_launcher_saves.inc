// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The launcher's Saves page (included by pc_ui.cpp after the settings dialog, so it draws with the
// same glass, fonts and buttons). A glass dialog like Settings: the profiles and the Options file
// on the left with Import saves (Xbox folders or zips, PS2 .psu exports), the backups on the right with Back up now and Restore, and
// the profiles the game deleted (save-backups/quarantine, Bring back = import that copy), and
// one plain line at the foot that says what happened. The work is save_manager.cpp; where the files
// are and whether the game runs, pc_launcher_saves.cpp.
//
// Tests and pictures: RECOMP_LAUNCHER_PAGE=saves opens it at start; RECOMP_TEST_SAVES runs steps
// "<frame>:<action>[:<argument>]" separated by ';' once the dialog is open: backup, import:<path>
// (as if that folder or zip was picked), replace:<profile name> (ticks Replace), import-ok,
// restore:<row>, restore-ok, folder (Open folder), bring:<row> (Bring back a profile the game deleted),
// aside:<profile name> (Put aside), back:<profile name> (Bring back a profile put aside),
// unlock:<profile name> (Unlock all: opens its confirmation), unlock-cash (ticks "and $999,999 and development points"),
// unlock-ok, undo (Undo after an unlock).
//
// Put aside (save_manager.h): the game loads at most 16 user IDs, so a profile can be moved out of the
// folder it reads, whole, and brought back later; the count of profiles in the game is in the heading.
//
// Unlock all (save_unlock.h): the profile and the Options file are edited to everything unlocked, after
// a backup; Undo (at the foot, until the page closes) restores that backup, as Restore in the list does.

#include "pc_launcher_saves.h"
#include "created_fighter.h"     // cp_names_setting_get: the created fighters' online names
#include "save_unlock.h"

namespace {

struct SavesPage
{
    bool loaded = false;
    std::vector<saves::Entry> entries;
    std::vector<saves::Entry> aside;                // profiles put aside (<save>/aside)
    std::vector<saves::Backup> backups;
    std::vector<saves::Quarantined> deleted;        // profiles the game deleted (save-backups/quarantine)
    std::string bring_back;                         // a quarantine copy to import, at the dialog's level
    std::map<std::string, std::string> online;      // "<folder>/<file>" -> online name of its created fighter
    std::string message;
    int tone = 0;                                   // 0 plain, 1 done, 2 refused or failed
    int restore_row = -1;
    saves::ImportScan scan;
    std::set<std::string> replace;
    std::string source;
    bool picking = false;
    // RECOMP_TEST_SAVES
    struct Step { int frame; std::string action, arg; bool done; };
    std::vector<Step> steps;
    bool want_import_ok = false, want_restore_ok = false;
    bool open_restore = false;      // opened at the dialog's own level (a row's button is in a child)
    // Unlock all: the profile asked about, the cash box, and the backup Undo restores
    std::string unlock_folder, unlock_name, undo_backup, undo_name;
    bool unlock_cash = false, open_unlock = false, want_unlock_ok = false;
};
SavesPage g_sp;

void saves_say(const std::string &text, int tone)
{
    g_sp.message = text;
    g_sp.tone = tone;
    fprintf(stderr, "[SAVES] %s\n", text.c_str());
    fflush(stderr);
}

// The setting online.created_names: "<save key>=<name>" entries separated by ';', with '%', ';' and
// '=' written as %25, %3B and %3D (created_fighter.h).
std::string saves_unescape(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out.push_back((char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

void saves_refresh()
{
    g_sp.entries = saves::list_saves(pcsaves::save_dir());
    g_sp.aside = saves::list_aside(pcsaves::save_dir());
    g_sp.backups = saves::list_backups(pcsaves::backup_dir());
    g_sp.deleted = saves::list_quarantine(pcsaves::backup_dir());
    g_sp.online.clear();
    char text[1024] = "";
    cp_names_setting_get(text, (int)sizeof(text));
    std::string all = text;
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find(';', at);
        if (end == std::string::npos) end = all.size();
        std::string item = all.substr(at, end - at);
        size_t eq = item.find('=');
        if (eq != std::string::npos) g_sp.online[saves_unescape(item.substr(0, eq))] = saves_unescape(item.substr(eq + 1));
        at = end + 1;
    }
    g_sp.loaded = true;
}

bool saves_refuse_while_running(const char *what)
{
    if (!pcsaves::game_running()) return false;
    saves_say(std::string("Close the game first: ") + what + " only while it is closed. Nothing was changed.", 2);
    return true;
}

void saves_backup_now()
{
    std::string made, why;
    if (saves::make_backup(pcsaves::save_dir(), pcsaves::backup_dir(), "manual", pcsaves::now(), &made, &why))
        saves_say("Backup made (" + saves::describe_time(pcsaves::now()) + "). The newest 10 are kept.", 1);
    else
        saves_say(why, 2);
    saves_refresh();
}

void saves_start_import(const std::string &path)
{
    g_sp.source = path;
    g_sp.replace.clear();
    g_sp.scan = saves::scan_import(saves::fs::u8path(path), pcsaves::save_dir());
    if (!g_sp.scan.error.empty()) {
        saves_say(g_sp.scan.error, 2);
        return;
    }
    if (g_sp.scan.found.empty()) {
        saves_say("No profile to import in " + pcui::path_utf8(saves::fs::u8path(path).filename()) + ": " +
                      (g_sp.scan.skipped.empty() ? std::string("nothing usable") : g_sp.scan.skipped[0]) + ".", 2);
        return;
    }
    fprintf(stderr, "[SAVES] import: %zu profile(s) found in %s\n", g_sp.scan.found.size(), path.c_str());
    ImGui::OpenPopup("Import saves##confirm");
}

void saves_do_import()
{
    if (saves_refuse_while_running("saves can be imported")) return;
    auto r = saves::apply_import(g_sp.scan, g_sp.replace, pcsaves::save_dir(), pcsaves::backup_dir(), pcsaves::now());
    std::string text = r.message;
    if (!r.backup.empty()) text += " Your saves from before are in a backup.";
    saves_say(text, r.ok && r.imported ? 1 : r.ok ? 0 : 2);
    g_sp.scan = saves::ImportScan();
    saves_refresh();
}

void saves_ask_restore(int row)
{
    if (row < 0 || row >= (int)g_sp.backups.size()) return;
    if (saves_refuse_while_running("a backup can be restored")) return;
    g_sp.restore_row = row;
    g_sp.open_restore = true;
}

void saves_do_restore()
{
    if (g_sp.restore_row < 0 || g_sp.restore_row >= (int)g_sp.backups.size()) return;
    if (saves_refuse_while_running("a backup can be restored")) return;
    std::string message;
    bool ok = saves::restore(pcsaves::save_dir(), pcsaves::backup_dir(), g_sp.backups[g_sp.restore_row].name, pcsaves::now(),
                             &message);
    saves_say(message, ok ? 1 : 2);
    g_sp.restore_row = -1;
    saves_refresh();
}

void saves_put_aside(const std::string &folder)
{
    if (saves_refuse_while_running("a profile can be put aside")) return;
    std::string message;
    bool ok = saves::put_aside(pcsaves::save_dir(), folder, &message);
    saves_say(message, ok ? 1 : 2);
    saves_refresh();
}

void saves_bring_back_aside(const std::string &stash)
{
    if (saves_refuse_while_running("a profile can be brought back")) return;
    std::string message;
    bool ok = saves::bring_back(pcsaves::save_dir(), stash, &message);
    saves_say(message, ok ? 1 : 2);
    saves_refresh();
}

void saves_ask_unlock(const std::string &folder, const std::string &name)
{
    if (saves_refuse_while_running("a profile can be unlocked")) return;
    g_sp.unlock_folder = folder;
    g_sp.unlock_name = name;
    g_sp.unlock_cash = false;
    g_sp.open_unlock = true;
}

void saves_do_unlock()
{
    if (g_sp.unlock_folder.empty() || saves_refuse_while_running("a profile can be unlocked")) return;
    saves::Bytes sod;
    std::string where;
    if (!pcsaves::game_database(sod, &where)) {
        saves_say("Nothing was changed: the game's files were not found (assets/djv2.sod).", 2);
        return;
    }
    fprintf(stderr, "[SAVES] unlock: database %s\n", where.c_str());
    auto r = saves::unlock_everything(pcsaves::save_dir(), pcsaves::backup_dir(), g_sp.unlock_folder, sod, g_sp.unlock_cash,
                                      pcsaves::now());
    if (r.ok) {
        g_sp.undo_backup = r.backup;
        g_sp.undo_name = g_sp.unlock_name;
        saves_say(r.message + " Undo puts it back.", 1);
    } else {
        saves_say(r.message, 2);
    }
    g_sp.unlock_folder.clear();
    saves_refresh();
}

void saves_undo_unlock()
{
    if (g_sp.undo_backup.empty() || saves_refuse_while_running("a backup can be restored")) return;
    std::string message;
    bool ok = saves::restore(pcsaves::save_dir(), pcsaves::backup_dir(), g_sp.undo_backup, pcsaves::now(), &message);
    saves_say(ok ? g_sp.undo_name + " is back as it was before Unlock everything." : message, ok ? 1 : 2);
    if (ok) g_sp.undo_backup.clear();
    saves_refresh();
}

int saves_in_game()
{
    int n = 0;
    for (auto &e : g_sp.entries) n += e.kind == saves::Entry::PROFILE;
    return n;
}

void saves_open_folder()
{
    std::error_code ec;
    saves::fs::create_directories(pcsaves::save_dir() / "UserData" / saves::kTitleId, ec);
    pcui::open_path(pcui::path_utf8(pcsaves::save_dir()));
}

void saves_test_steps()
{
    static bool parsed;
    if (!parsed) {
        parsed = true;
        const char *text = getenv("RECOMP_TEST_SAVES");
        std::string all = text ? text : "";
        size_t at = 0;
        while (at < all.size()) {
            size_t end = all.find(';', at);
            if (end == std::string::npos) end = all.size();
            std::string item = all.substr(at, end - at);
            size_t a = item.find(':'), b = a == std::string::npos ? a : item.find(':', a + 1);
            if (a != std::string::npos)
                g_sp.steps.push_back({atoi(item.c_str()), item.substr(a + 1, b == std::string::npos ? b : b - a - 1),
                                      b == std::string::npos ? "" : item.substr(b + 1), false});
            at = end + 1;
        }
    }
    for (auto &s : g_sp.steps) {
        if (s.done || ImGui::GetFrameCount() < s.frame) continue;
        s.done = true;
        fprintf(stderr, "[SAVES] test step %s %s\n", s.action.c_str(), s.arg.c_str());
        if (s.action == "backup") saves_backup_now();
        else if (s.action == "import") saves_start_import(s.arg);
        else if (s.action == "replace") g_sp.replace.insert(s.arg);
        else if (s.action == "import-ok") g_sp.want_import_ok = true;
        else if (s.action == "restore") saves_ask_restore(atoi(s.arg.c_str()));
        else if (s.action == "restore-ok") g_sp.want_restore_ok = true;
        else if (s.action == "folder") saves_open_folder();
        else if (s.action == "bring" && atoi(s.arg.c_str()) >= 0 && atoi(s.arg.c_str()) < (int)g_sp.deleted.size())
            g_sp.bring_back = pcui::path_utf8(g_sp.deleted[atoi(s.arg.c_str())].path);
        else if (s.action == "aside") {
            std::string folder;
            for (auto &e : g_sp.entries)
                if (e.kind == saves::Entry::PROFILE && e.name == s.arg) folder = e.folder;
            if (!folder.empty()) saves_put_aside(folder);
        } else if (s.action == "unlock") {
            for (auto &e : g_sp.entries)
                if (e.kind == saves::Entry::PROFILE && e.name == s.arg) saves_ask_unlock(e.folder, e.name);
        } else if (s.action == "unlock-cash") {
            g_sp.unlock_cash = true;
        } else if (s.action == "unlock-ok") {
            g_sp.want_unlock_ok = true;
        } else if (s.action == "undo") {
            saves_undo_unlock();
        } else if (s.action == "back") {
            std::string stash;
            for (auto &e : g_sp.aside)
                if (e.name == s.arg && stash.empty()) stash = e.stash;
            if (!stash.empty()) saves_bring_back_aside(stash);
        }
    }
}

// A row of a list: a quiet translucent band, its headline and a detail line (clear of a button
// `room` wide at its right end).
void saves_row(ImDrawList *dl, ImVec2 a, ImVec2 b, float s, const char *head, const char *detail, const ImVec4 &detail_c,
               float room = 0)
{
    dl->AddRectFilled(a, b, col(k_white, 0.05f), 10 * s);
    dl->AddRect(a, b, col(k_white, 0.06f), 10 * s);
    float right = b.x - a.x - 32 * s - room;
    dl->PushClipRect(a, ImVec2(b.x - room, b.y), true);
    dl->AddText(bold_font(), 16 * s, ImVec2(a.x + 16 * s, a.y + 10 * s), col(k_white), head);
    dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(a.x + 16 * s, a.y + 33 * s), col(detail_c), detail, nullptr, right);
    dl->PopClipRect();
}

std::string saves_profile_line(const saves::Entry &e, ImVec4 &c)
{
    std::string line = "Changed " + saves::describe_time(e.changed);
    if (e.created) {
        auto found = g_sp.online.find(e.folder + "/" + e.name);
        line += "  \xC2\xB7  created fighter";
        if (found != g_sp.online.end() && !found->second.empty()) line += " \"" + found->second + "\" online";
    } else {
        line += "  \xC2\xB7  no created fighter";
    }
    if (!e.signed_ok) {
        line = "Signed by another Xbox: it shows as damaged. Import it with Import saves.";
        c = k_gold;
    }
    return line;
}

void saves_profiles(float w, float h, float s)
{
    ImGui::BeginChild("##profiles", ImVec2(w, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec4 detail(0.80f, 0.80f, 0.82f, 1);
    float row_h = 60 * s, gap = 8 * s, rw = ImGui::GetContentRegionAvail().x;
    int shown = 0;
    std::string aside_folder, back_stash, unlock_folder, unlock_name;   // pressed this frame (acted on after the lists)
    for (auto &e : g_sp.entries) {
        if (e.kind == saves::Entry::OTHER) continue;
        ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + rw, a.y + row_h);
        std::string head, line;
        ImVec4 c = detail;
        if (e.kind == saves::Entry::OPTIONS) {
            head = "Options";
            line = "Game settings  \xC2\xB7  changed " + saves::describe_time(e.changed);
        } else {
            head = e.name;
            line = saves_profile_line(e, c);
        }
        bool profile = e.kind == saves::Entry::PROFILE;
        saves_row(dl, a, b, s, head.c_str(), line.c_str(), c, profile ? 204 * s : 0);
        if (profile) {
            ImGui::PushID(e.folder.c_str());
            ImGui::BeginDisabled(!e.signed_ok);
            if (glass_button("##unlock", "Unlock all", ImVec2(b.x - 204 * s, a.y + 16 * s), ImVec2(92 * s, 28 * s), s,
                             GLASS_PLAIN, bold_font(), 13.5f * s)) {
                unlock_folder = e.folder;
                unlock_name = e.name;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(e.signed_ok ? "Every fighter, venue, song, item, Blazin and style unlocked (asks first)"
                                              : "Bring this profile in with Import saves first");
            if (glass_button("##aside", "Put aside", ImVec2(b.x - 104 * s, a.y + 16 * s), ImVec2(90 * s, 28 * s), s,
                             GLASS_PLAIN, bold_font(), 13.5f * s))
                aside_folder = e.folder;
            ImGui::PopID();
            ImGui::SetCursorScreenPos(a);
        }
        ImGui::Dummy(ImVec2(rw, row_h + gap));
        shown++;
    }
    int other = 0;
    for (auto &e : g_sp.entries) other += e.kind == saves::Entry::OTHER;
    if (!shown) {
        ImGui::PushTextWrapPos(rw);
        ImGui::TextColored(ImVec4(1, 1, 1, 0.65f), "No profiles yet. Make one in the game under Select User ID, or "
                                                   "bring your Xbox or PS2 profiles in with Import saves.");
        ImGui::PopTextWrapPos();
    }
    if (other) ImGui::TextColored(ImVec4(1, 1, 1, 0.5f), "%d other folder%s in the save area", other, other == 1 ? "" : "s");
    // Profiles put aside: out of the folder the game reads (it loads at most 16 user IDs)
    if (!g_sp.aside.empty()) {
        ImGui::Dummy(ImVec2(rw, 6 * s));
        ImVec2 at = ImGui::GetCursorScreenPos();
        dl->AddText(display_font(), 15 * s, at, col(ImVec4(0.86f, 0.84f, 0.80f, 1)), "PUT ASIDE");
        dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(at.x + 92 * s, at.y + 2 * s), col(k_white, 0.5f),
                    "Not in the game. Bring one back to play it.");
        ImGui::Dummy(ImVec2(rw, 26 * s));
        bool full = saves_in_game() >= saves::kMaxUserIds;
        for (auto &e : g_sp.aside) {
            ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + rw, a.y + row_h);
            ImVec4 c = detail;
            std::string line = saves_profile_line(e, c);
            saves_row(dl, a, b, s, e.name.c_str(), line.c_str(), c, 118 * s);
            ImGui::PushID(e.stash.c_str());
            if (glass_button("##back", "Bring back", ImVec2(b.x - 118 * s, a.y + 16 * s), ImVec2(104 * s, 28 * s), s,
                             GLASS_PLAIN, bold_font(), 13.5f * s))
                back_stash = e.stash;
            if (full && ImGui::IsItemHovered())
                ImGui::SetTooltip("The game has 16 user IDs, its most. Put one aside first.");
            ImGui::PopID();
            ImGui::SetCursorScreenPos(a);
            ImGui::Dummy(ImVec2(rw, row_h + gap));
        }
    }
    ImGui::EndChild();
    if (!unlock_folder.empty()) saves_ask_unlock(unlock_folder, unlock_name);
    else if (!aside_folder.empty()) saves_put_aside(aside_folder);
    else if (!back_stash.empty()) saves_bring_back_aside(back_stash);
}

void saves_backups(float w, float h, float s)
{
    ImGui::BeginChild("##backups", ImVec2(w, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float row_h = 60 * s, gap = 8 * s, rw = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < (int)g_sp.backups.size(); i++) {
        auto &b = g_sp.backups[i];
        ImVec2 a = ImGui::GetCursorScreenPos(), z(a.x + rw, a.y + row_h);
        std::string head = saves::describe_time(b.when);
        if (!head.empty()) head[0] = (char)toupper((unsigned char)head[0]);
        std::string line = saves::describe_reason(b.reason) + "  \xC2\xB7  " + std::to_string(b.profiles) +
                           (b.profiles == 1 ? " profile" : " profiles");
        saves_row(dl, a, ImVec2(z.x, z.y), s, head.c_str(), line.c_str(), ImVec4(0.80f, 0.80f, 0.82f, 1));
        ImGui::PushID(i);
        if (glass_button("##restore", "Restore", ImVec2(z.x - 96 * s, a.y + 16 * s), ImVec2(82 * s, 28 * s), s, GLASS_PLAIN,
                         bold_font(), 13.5f * s))
            saves_ask_restore(i);
        ImGui::PopID();
        ImGui::SetCursorScreenPos(a);
        ImGui::Dummy(ImVec2(rw, row_h + gap));
    }
    if (g_sp.backups.empty())
        ImGui::TextColored(ImVec4(1, 1, 1, 0.65f), "No backups yet.");
    // Profiles the game itself deleted: Bring back imports the copy (asks before replacing one)
    for (int i = 0; i < (int)g_sp.deleted.size(); i++) {
        auto &q = g_sp.deleted[i];
        ImVec2 a = ImGui::GetCursorScreenPos(), z(a.x + rw, a.y + row_h);
        std::string head = saves::describe_time(q.when);
        if (!head.empty()) head[0] = (char)toupper((unsigned char)head[0]);
        std::string line = "Deleted by the game  \xC2\xB7  " + (q.profile.empty() ? q.folder : q.profile);
        saves_row(dl, a, ImVec2(z.x, z.y), s, head.c_str(), line.c_str(), ImVec4(0.80f, 0.80f, 0.82f, 1));
        ImGui::PushID(1000 + i);
        if (glass_button("##bring", "Bring back", ImVec2(z.x - 110 * s, a.y + 16 * s), ImVec2(96 * s, 28 * s), s,
                         GLASS_PLAIN, bold_font(), 13.5f * s))
            g_sp.bring_back = pcui::path_utf8(q.path);
        ImGui::PopID();
        ImGui::SetCursorScreenPos(a);
        ImGui::Dummy(ImVec2(rw, row_h + gap));
    }
    ImGui::EndChild();
}

void saves_import_confirm(float s)
{
    if (!begin_glass_modal("Import saves##confirm", ImGui::GetFontSize() * 32)) return;
    float wrap = ImGui::GetFontSize() * 30;
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextDisabled("From %s", pcui::path_utf8(saves::fs::u8path(g_sp.source).filename()).c_str());
    ImGui::Spacing();
    int writes = 0;
    bool converted = false;
    for (auto &c : g_sp.scan.found) {
        ImGui::PushFont(bold_font(), 0.0f);
        ImGui::TextUnformatted(c.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (!c.origin.empty()) {
            converted = true;
            ImGui::TextDisabled("(%s)", c.origin.c_str());
            ImGui::SameLine();
        }
        if (!c.exists) {
            ImGui::TextColored(k_green, "new");
            writes++;
        } else {
            bool on = g_sp.replace.count(c.name) > 0;
            ImGui::TextColored(k_gold, "you have a profile with this name");
            ImGui::PushID(c.name.c_str());
            if (ImGui::Checkbox("Replace mine with this one", &on)) {
                if (on) g_sp.replace.insert(c.name);
                else g_sp.replace.erase(c.name);
            }
            ImGui::PopID();
            writes += on;
        }
    }
    if (!g_sp.scan.skipped.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Not imported:");
        for (size_t i = 0; i < g_sp.scan.skipped.size() && i < 4; i++) ImGui::TextDisabled("  %s", g_sp.scan.skipped[i].c_str());
        if (g_sp.scan.skipped.size() > 4) ImGui::TextDisabled("  and %zu more", g_sp.scan.skipped.size() - 4);
    }
    ImGui::Spacing();
    if (converted)
        ImGui::TextWrapped("A PS2 profile is converted to the Xbox layout (PS2 converter by hK). Its fighter and "
                           "progress come over; its picture on Select User ID starts out as a plain dark square.");
    ImGui::TextWrapped("Your saves are backed up first. Each profile is signed again for this PC, so the game "
                       "loads it; it will not load on an Xbox afterwards.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::BeginDisabled(!writes);
    ImGui::PushStyleColor(ImGuiCol_Button, k_red);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
    char label[48];
    snprintf(label, sizeof(label), writes == 1 ? "Import 1 profile" : "Import %d profiles", writes);
    bool go = ImGui::Button(label, ImVec2(ImGui::GetFontSize() * 11, 0));
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    if (g_sp.want_import_ok) { g_sp.want_import_ok = false; go = true; }
    ImGui::SameLine();
    if (go) {
        saves_do_import();
        ImGui::CloseCurrentPopup();
    } else if (ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 8, 0))) {
        g_sp.scan = saves::ImportScan();
        ImGui::CloseCurrentPopup();
    }
    end_glass_modal();
    (void)s;
}

void saves_restore_confirm()
{
    if (!begin_glass_modal("Restore a backup##confirm", ImGui::GetFontSize() * 28)) return;
    bool valid = g_sp.restore_row >= 0 && g_sp.restore_row < (int)g_sp.backups.size();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
    if (valid) {
        auto &b = g_sp.backups[g_sp.restore_row];
        ImGui::Text("Put back your saves from %s?", saves::describe_time(b.when).c_str());
        ImGui::TextDisabled("%s, %d profile%s.", saves::describe_reason(b.reason).c_str(), b.profiles, b.profiles == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::TextWrapped("Your saves as they are now are backed up first, so you can go back to them.");
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, k_red);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
    bool go = ImGui::Button("Restore", ImVec2(ImGui::GetFontSize() * 9, 0)) && valid;
    ImGui::PopStyleColor(2);
    if (g_sp.want_restore_ok) { g_sp.want_restore_ok = false; go = valid; }
    ImGui::SameLine();
    if (go) {
        saves_do_restore();
        ImGui::CloseCurrentPopup();
    } else if (ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 8, 0))) {
        g_sp.restore_row = -1;
        ImGui::CloseCurrentPopup();
    }
    end_glass_modal();
}

void saves_unlock_confirm()
{
    if (!begin_glass_modal("Unlock everything##confirm", ImGui::GetFontSize() * 30)) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
    ImGui::Text("This changes %s. A backup is kept. Undo with Restore.", g_sp.unlock_name.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("Unlocked: every fighter and venue (and so every match type) and every song; in the shops all "
                       "clothing, jewellery, tattoos and hair styles, and every Blazin move and fighting style to buy "
                       "or learn. What you own stays owned.");
    ImGui::Spacing();
    ImGui::TextDisabled("Fighters, venues and songs are shared by every profile on this PC. Stats, level and story "
                        "progress stay as they are. Online play is not affected.");
    ImGui::Spacing();
    ImGui::Checkbox("and $999,999 and development points for Story", &g_sp.unlock_cash);
    ImGui::TextDisabled("Enough development points to train every stat to its maximum in the gym (up to 272,625). "
                        "Cash and points are only raised, never lowered.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, k_red);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_red_hi);
    bool go = ImGui::Button("Unlock everything", ImVec2(ImGui::GetFontSize() * 11, 0));
    ImGui::PopStyleColor(2);
    if (g_sp.want_unlock_ok) { g_sp.want_unlock_ok = false; go = true; }
    ImGui::SameLine();
    if (go) {
        saves_do_unlock();
        ImGui::CloseCurrentPopup();
    } else if (ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 8, 0))) {
        g_sp.unlock_folder.clear();
        ImGui::CloseCurrentPopup();
    }
    end_glass_modal();
}

} // namespace

static void saves_dialog(ImVec2 o, float w, float h, float s)
{
    ImVec2 size(std::min(w - 40 * s, 900 * s), std::min(h - 40 * s, 620 * s));
    ImGui::SetNextWindowPos(ImVec2(o.x + (w - size.x) / 2, o.y + (h - size.y) / 2));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginPopupModal("Saves", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) {
        g_sp.loaded = false;        // read again the next time it opens
        g_sp.undo_backup.clear();   // Undo is for this visit; Restore in the list stays
        return;
    }
    if (!g_sp.loaded) saves_refresh();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), b(a.x + size.x, a.y + size.y);
    glass(dl, a, b, 16 * s, k_glass, 0.80f);
    dl->AddText(display_font(), 30 * s, ImVec2(a.x + 30 * s, a.y + 18 * s), col(k_white), "SAVES");
    dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(a.x + 30 * s, a.y + 58 * s), col(k_white, 0.6f),
                "Your profiles and their backups, on this PC.");
    if (glass_button("##saves_folder", "Open folder", ImVec2(b.x - 30 * s - 130 * s, a.y + 22 * s), ImVec2(130 * s, 32 * s),
                     s, GLASS_PLAIN, bold_font(), 14 * s))
        saves_open_folder();

    push_glass_style();
    saves_test_steps();
    float top = 92 * s, foot = 64 * s, between = 24 * s, button_h = 36 * s;
    float inner = size.x - 60 * s, left_w = (inner - between) * 0.54f, right_w = inner - between - left_w;
    float list_top = top + 30 * s, list_h = size.y - list_top - foot - button_h - 16 * s;
    float lx = a.x + 30 * s, rx = lx + left_w + between;
    dl->AddText(display_font(), 17 * s, ImVec2(lx, a.y + top), col(ImVec4(0.86f, 0.84f, 0.80f, 1)), "PROFILES");
    {
        int in_game = saves_in_game();
        char count[64];
        snprintf(count, sizeof(count), "%d of %d user IDs in the game", in_game, saves::kMaxUserIds);
        float cw = text_size(ImGui::GetFont(), 12.5f * s, count).x;
        dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(lx + left_w - cw, a.y + top + 4 * s),
                    col(in_game >= saves::kMaxUserIds ? k_gold : ImVec4(1, 1, 1, 0.5f)), count);
    }
    dl->AddText(display_font(), 17 * s, ImVec2(rx, a.y + top), col(ImVec4(0.86f, 0.84f, 0.80f, 1)), "BACKUPS");
    {
        const char *hint = "Before every update and once a day you play. The newest 10 are kept.";
        float hw = text_size(ImGui::GetFont(), 12.5f * s, hint).x, room = right_w - 80 * s;
        dl->PushClipRect(ImVec2(rx + 80 * s, a.y + top), ImVec2(rx + right_w, a.y + top + 24 * s), true);
        dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(rx + right_w - std::min(hw, room), a.y + top + 4 * s),
                    col(k_white, 0.5f), hint);
        dl->PopClipRect();
    }
    ImGui::SetCursorScreenPos(ImVec2(lx, a.y + list_top));
    saves_profiles(left_w, list_h, s);
    ImGui::SetCursorScreenPos(ImVec2(rx, a.y + list_top));
    saves_backups(right_w, list_h, s);

    float by = a.y + list_top + list_h + 12 * s;
    if (glass_button("##saves_import", "Import saves", ImVec2(lx, by), ImVec2(190 * s, button_h), s, GLASS_PLAIN,
                     bold_font(), 14.5f * s))
        ImGui::OpenPopup("##import_from");
    if (glass_button("##saves_backup", "Back up now", ImVec2(rx, by), ImVec2(150 * s, button_h), s, GLASS_PLAIN, bold_font(),
                     14.5f * s))
        saves_backup_now();
    ImGui::SetNextWindowPos(ImVec2(lx, by + button_h + 4 * s));
    if (ImGui::BeginPopup("##import_from")) {
        void *window = nullptr;
#if defined(_WIN32)
        window = ImGui::GetMainViewport()->PlatformHandleRaw;
#endif
        bool pick = false;
        pcsaves::Pick what = pcsaves::Pick::FOLDER;
        if (ImGui::MenuItem("Xbox: from a folder (E:\\UDATA\\45410049)...")) pick = true;
        if (ImGui::MenuItem("Xbox: from a .zip file...")) { pick = true; what = pcsaves::Pick::ZIP; }
        if (ImGui::MenuItem("PS2: from a .psu save export...")) { pick = true; what = pcsaves::Pick::PS2; }
        ImGui::EndPopup();
        if (pick && !saves_refuse_while_running("saves can be imported")) {
            g_sp.picking = true;
            pcsaves::pick_start(what, window);
        }
    }
    {
        std::string path;
        if (g_sp.picking && pcsaves::pick_poll(path)) {
            g_sp.picking = false;
            if (!path.empty()) saves_start_import(path);
        }
    }
    if (!g_sp.bring_back.empty()) {
        std::string path = g_sp.bring_back;
        g_sp.bring_back.clear();
        if (!saves_refuse_while_running("saves can be imported")) saves_start_import(path);
    }
    if (g_sp.open_restore) {
        g_sp.open_restore = false;
        ImGui::OpenPopup("Restore a backup##confirm");
    }
    if (g_sp.open_unlock) {
        g_sp.open_unlock = false;
        ImGui::OpenPopup("Unlock everything##confirm");
    }
    saves_import_confirm(s);
    saves_restore_confirm();
    saves_unlock_confirm();
    pop_glass_style();

    float undo_w = g_sp.undo_backup.empty() ? 0 : 110 * s;
    if (undo_w && glass_button("##saves_undo", "Undo", ImVec2(b.x - 30 * s - 130 * s - undo_w, b.y - foot / 2 - 17 * s),
                               ImVec2(96 * s, 34 * s), s, GLASS_PLAIN, bold_font(), 14.5f * s))
        saves_undo_unlock();
    if (!g_sp.message.empty()) {
        ImVec4 c = g_sp.tone == 1 ? k_green : g_sp.tone == 2 ? k_gold : ImVec4(1, 1, 1, 0.75f);
        float wrap = size.x - 60 * s - 150 * s - undo_w;
        dl->PushClipRect(a, ImVec2(b.x - 170 * s - undo_w, b.y), true);
        float th = text_size(ImGui::GetFont(), 13.5f * s, g_sp.message.c_str(), wrap).y;
        dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(a.x + 30 * s, b.y - foot / 2 - th / 2), col(c), g_sp.message.c_str(),
                    nullptr, wrap);
        dl->PopClipRect();
    }
    if (glass_button("##saves_done", "DONE", ImVec2(b.x - 30 * s - 130 * s, b.y - foot / 2 - 19 * s), ImVec2(130 * s, 38 * s),
                     s, GLASS_RED, display_font(), 18 * s) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
