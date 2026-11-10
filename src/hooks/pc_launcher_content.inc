// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The launcher's Content page (included by pc_ui.cpp after the glass helpers): FFNY Online's community
// packs, synced from our signed catalogue by setup/mods.py sync (pc_mods.cpp runs it) and read back from
// <data>/mod-manager/content.ini (pc_content.c). New fighters, clothing and venues ("content") are always
// on and decide who meets online; retextures and language packs ("replacement") can be switched off here.
// docs/content-packs.md.

// Sync once at the launcher's start and again with each update check; PLAY cancels a sync that is still
// downloading (the packs come at the next start) and waits for it to stop, since an install holds the
// lock the game takes.
static bool g_content_synced_once;
static bool g_content_play_waiting;
static UiTick g_content_next_sync;

static bool content_sync_allowed()
{
    const char *root = getenv("RECOMP_TEXTURE_ROOT");
    if (root && *root) return false;                    // Studio's test root, tests with their own packs
    if (pc_content_offline_only()) return false;
    const char *off = getenv("RECOMP_CONTENT");
    return !(off && !strcmp(off, "0"));
}

static void content_sync_tick()
{
    if (!content_sync_allowed() || pcui::mod_busy() || pcui::mod_recovery_needed()) return;
    if (g_content_synced_once && ui_ticks() < g_content_next_sync) return;
    g_content_synced_once = true;
    g_content_next_sync = ui_ticks() + 5 * 60000;
    pcui::mod_start("sync");
}

// PLAY pressed: true when the game may start now.
static bool content_play_gate(bool pressed)
{
    if (pressed && pcui::mod_busy()) {
        pcui::mod_cancel();
        g_content_play_waiting = true;
        return false;
    }
    if (g_content_play_waiting && !pcui::mod_busy()) {
        g_content_play_waiting = false;
        return true;
    }
    return pressed;
}

static const char *content_type_label(const char *type)
{
    if (!strcmp(type, "language")) return "Language";
    if (!strcmp(type, "fighter-texture")) return "Fighter look";
    if (!strcmp(type, "fighter-body") || !strcmp(type, "fighter-model")) return "Fighter";
    if (!strcmp(type, "clothing")) return "Clothing";
    if (!strcmp(type, "venue")) return "Venue";
    return type[0] ? type : "Pack";
}

#include "pc_launcher_community.inc"   // the Community tab: every submitted pack's page, votes

static void content_dialog(ImVec2 o, float w, float h, float s)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    static int count = -1;
    static bool changed;            // a switch moved
    static bool composing;          // ... and the launcher composes the packs' files again
    ImVec2 size(std::min(w - 40 * s, 820 * s), std::min(h - 40 * s, 620 * s));
    ImGui::SetNextWindowPos(ImVec2(o.x + (w - size.x) / 2, o.y + (h - size.y) / 2));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginPopupModal("Content", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) { count = -1; return; }
    static bool was_busy;
    bool busy = pcui::mod_busy();
    if (count < 0 || (was_busy && !busy)) count = pc_content_list(packs, PC_CONTENT_MAX);
    was_busy = busy;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), b(a.x + size.x, a.y + size.y);
    glass(dl, a, b, 16 * s, k_glass, 0.80f);
    dl->AddText(display_font(), 30 * s, ImVec2(a.x + 30 * s, a.y + 18 * s), col(k_white), "CONTENT");
    dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(a.x + 30 * s, a.y + 58 * s), col(k_white, 0.6f),
                "Community packs the FFNY team accepted, installed for every player. New fighters, clothing and venues "
                "are always on; looks and languages can be switched off here.", nullptr, size.x - 220 * s);
    float lx = a.x + 30 * s, inner = size.x - 60 * s;
    if (glass_button("##content_sync", busy ? "Syncing..." : "Sync now", ImVec2(b.x - 30 * s - 150 * s, a.y + 22 * s),
                     ImVec2(150 * s, 36 * s), s, GLASS_PLAIN, bold_font(), 14.5f * s, !busy && content_sync_allowed())) {
        g_content_next_sync = 0;
        g_content_synced_once = false;
        content_sync_tick();
    }

    // Installed | Community
    static int tab = 0;
    {
        static bool asked;
        const char *want = getenv("RECOMP_CONTENT_TAB");
        if (!asked && want && !strcmp(want, "community")) tab = 1;
        asked = true;
    }
    if (glass_button("##content_tab0", "Installed", ImVec2(lx, a.y + 100 * s), ImVec2(130 * s, 34 * s), s,
                     tab == 0 ? GLASS_RED : GLASS_PLAIN, bold_font(), 14.5f * s))
        tab = 0;
    if (glass_button("##content_tab1", "Community", ImVec2(lx + 140 * s, a.y + 100 * s), ImVec2(130 * s, 34 * s), s,
                     tab == 1 ? GLASS_RED : GLASS_PLAIN, bold_font(), 14.5f * s))
        tab = 1;
    float top = a.y + 148 * s, foot = 64 * s;
    if (tab == 1) {
        community_view(lx, top, inner, b.y - foot - top - 8 * s, s);
    } else {
    // The installed packs, in catalogue order.
    ImGui::SetCursorScreenPos(ImVec2(lx, top));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##content_list", ImVec2(inner, b.y - foot - top - 8 * s), ImGuiChildFlags_None);
    ImDrawList *ld = ImGui::GetWindowDrawList();
    float y = ImGui::GetCursorScreenPos().y;
    if (count == 0) {
        ld->AddText(ImGui::GetFont(), 14 * s, ImVec2(lx + 4 * s, y + 8 * s), col(k_white, 0.7f),
                    busy ? "Checking the catalogue..." : "No community packs yet. They arrive here by themselves once the team "
                                                         "accepts them.", nullptr, inner - 20 * s);
        ImGui::Dummy(ImVec2(inner - 20 * s, 60 * s));
    }
    for (int i = 0; i < count; i++) {
        PcContentPack &p = packs[i];
        bool content = !strcmp(p.kind, "content");
        bool on = content || !pc_content_is_off(p.id);
        ImVec2 ra(lx, y), rb(lx + inner - 14 * s, y + 76 * s);
        ld->AddRectFilled(ra, rb, col(k_white, on ? 0.05f : 0.025f), 12 * s);
        ld->AddRect(ra, rb, col(k_white, 0.06f), 12 * s);
        ImVec4 ink = on ? k_white : ImVec4(1, 1, 1, 0.55f);
        ld->AddText(bold_font(), 17 * s, ImVec2(ra.x + 18 * s, ra.y + 10 * s), col(ink), p.name[0] ? p.name : p.id);
        char line[400];
        snprintf(line, sizeof line, "%s  \xC2\xB7  %s  \xC2\xB7  v%s  \xC2\xB7  %.1f MB%s%s", content_type_label(p.type),
                 content ? "always on, part of online matching" : "your choice", p.version, (double)p.bytes / 1048576.0,
                 p.authors[0] ? "  \xC2\xB7  by " : "", p.authors);
        ld->PushClipRect(ra, ImVec2(rb.x - 150 * s, rb.y), true);
        ld->AddText(ImGui::GetFont(), 13 * s, ImVec2(ra.x + 18 * s, ra.y + 34 * s), col(ImVec4(0.80f, 0.80f, 0.82f, 1)), line);
        if (!strcmp(p.type, "language"))
            ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(ra.x + 18 * s, ra.y + 53 * s), col(k_gold),
                        on ? "Choose it under Settings > General > Game language." : "Off: not offered as a game language.");
        else if (p.description[0])
            ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(ra.x + 18 * s, ra.y + 53 * s), col(k_white, 0.5f), p.description);
        ld->PopClipRect();
        char id[128];
        snprintf(id, sizeof id, "##content_switch_%s", p.id);
        ImVec2 ba(rb.x - 136 * s, ra.y + 20 * s);
        if (content) {
            ld->AddText(bold_font(), 14 * s, ImVec2(ba.x + 22 * s, ba.y + 9 * s), col(k_green), "ALWAYS ON");
        } else if (glass_button(id, on ? "On" : "Off", ba, ImVec2(118 * s, 36 * s), s, on ? GLASS_RED : GLASS_PLAIN,
                                bold_font(), 14.5f * s, !busy)) {
            pc_content_set_off(p.id, on ? 1 : 0);
            pc_settings_save();
            changed = true;
            // fighter bodies, clothing and venues are composed into the game's files: again, now
            composing = pcui::mod_start("compose");
        }
        y += 86 * s;
        ImGui::SetCursorScreenPos(ImVec2(lx, y));
        ImGui::Dummy(ImVec2(1, 1));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    }

    // The foot: the sync's message, and DONE.
    {
        std::string text = tab == 1 ? community_status() : pcui::mod_status();
        if (changed && tab == 0)
            text = composing ? (busy ? "Applying the switched packs..." : "Switched packs are applied; they show when the game starts.")
                             : "Switched packs apply at the next start.";
        if (!text.empty()) {
            float wrap = size.x - 60 * s - 170 * s;
            dl->PushClipRect(a, ImVec2(b.x - 170 * s, b.y), true);
            float th = text_size(ImGui::GetFont(), 13.5f * s, text.c_str(), wrap).y;
            dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(a.x + 30 * s, b.y - foot / 2 - th / 2),
                        col(changed ? k_gold : ImVec4(1, 1, 1, 0.75f)), text.c_str(), nullptr, wrap);
            dl->PopClipRect();
        }
        if (glass_button("##content_done", "DONE", ImVec2(b.x - 30 * s - 130 * s, b.y - foot / 2 - 19 * s), ImVec2(130 * s, 38 * s),
                         s, GLASS_RED, display_font(), 18 * s) ||
            (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)))
            ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// FFNY Studio (docs in the Studio repository): its launcher button, hidden until Studio ships through the
// update path. RECOMP_STUDIO_BUTTON=1 shows the stub for tests.
static bool studio_button_visible()
{
    const char *v = getenv("RECOMP_STUDIO_BUTTON");
    return v && !strcmp(v, "1");
}
