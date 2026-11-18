// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The launcher's Content page (included by pc_ui.cpp after the glass helpers): FFNY Online's community
// packs, synced from our signed catalogue by setup/mods.py sync (pc_mods.cpp runs it) and read back from
// <data>/mod-manager/content.ini (pc_content.c). New fighters, clothing and venues ("content") are always
// on and decide who meets online; retextures and language packs ("replacement") can be switched off here.
// A pack of several mods ("Pack (N mods)", content.ini bundle=) is one card with its mods listed; a single
// pack's card says "Single". docs/content-packs.md.

// Sync once at the launcher's start and again with each update check. Build 82: PLAY lets a sync that is
// still running finish first (new community content decides who this PC meets online), and starts one more
// when the last began over two minutes ago, so a catalogue that changed since then comes in before the game
// starts; a no-change sync takes a second or two. While it waits the button says PLAY NOW: pressed, it cancels
// the download (the packs come at the next start) and the game starts once the sync has stopped, since an
// install holds the lock the game takes.
// Build 94: the creator's credits under a pack's card ("Credits: Texture by Free | Voice by Ana"), small, wrapped to
// at most two lines; they come from the signed catalogue (content.ini credits=). credits_height is 0 when the pack
// names none; credits_draw draws them at pos.
static const float k_credits_line = 15.0f;
static float credits_height(const char *credits, float wrap, float s)
{
    if (!credits || !credits[0]) return 0;
    char line[560];
    snprintf(line, sizeof line, "Credits: %s", credits);
    float h = text_size(ImGui::GetFont(), 12 * s, line, wrap).y;
    return (h > 2 * k_credits_line * s ? 2 * k_credits_line * s : h) + 6 * s;
}
static void credits_draw(ImDrawList *ld, const char *credits, ImVec2 pos, float wrap, float s)
{
    if (!credits || !credits[0]) return;
    char line[560];
    snprintf(line, sizeof line, "Credits: %s", credits);
    ld->PushClipRect(pos, ImVec2(pos.x + wrap, pos.y + 2 * k_credits_line * s), true);
    ld->AddText(ImGui::GetFont(), 12 * s, pos, col(k_white, 0.5f), line, nullptr, wrap);
    ld->PopClipRect();
}

static bool g_content_synced_once;
static bool g_content_play_waiting;
static bool g_content_play_skip;        // PLAY NOW pressed: the sync is being cancelled
static UiTick g_content_next_sync;
static UiTick g_content_sync_started;

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
    if (pcui::mod_start("sync")) g_content_sync_started = ui_ticks();
}

// PLAY pressed: true when the game may start now.
static bool content_play_gate(bool pressed)
{
    if (pressed && g_content_play_waiting) {        // PLAY NOW: without the rest of the new content
        pcui::mod_cancel();
        g_content_play_skip = true;
        return false;
    }
    // build 83: the last sync failed (a catalogue fetch or a download: the network, the PC's root store, its clock):
    // one more try before the game starts, once per launcher run, whatever its age
    static bool retried_failed;
    bool failed_retry = false;
    if (pressed && !retried_failed && !pcui::mod_busy()) {
        PcContentSync last;
        if (pc_content_sync_result(&last) && !strcmp(last.result, "failed")) failed_retry = retried_failed = true;
    }
    if (pressed && !pcui::mod_busy() && content_sync_allowed() && !pcui::mod_recovery_needed() &&
        (!g_content_synced_once || failed_retry || ui_ticks() - g_content_sync_started > 2 * 60000)) {
        g_content_next_sync = 0;                    // a fresh look at the catalogue before the game starts
        g_content_synced_once = false;
        content_sync_tick();
    }
    if (pressed && pcui::mod_busy()) {
        g_content_play_waiting = true;
        return false;
    }
    if (g_content_play_waiting && !pcui::mod_busy()) {
        g_content_play_waiting = g_content_play_skip = false;
        return true;
    }
    return pressed;
}

// The line over PLAY while it waits for the sync (nullptr: none).
static const char *content_play_note()
{
    if (!g_content_play_waiting) return nullptr;
    if (g_content_play_skip) return "Stopping the download: the game starts in a moment.";
    // build 86: the game's UPDATE NOW opened the launcher, which pressed PLAY itself
    return content_update_restarted() ? "Getting the new community content. The game starts again by itself."
                                      : "Getting the newest community content first. PLAY NOW starts without it.";
}

static const char *content_type_label(const char *type)
{
    if (!strcmp(type, "language")) return "Language";
    if (!strcmp(type, "fighter-texture")) return "Fighter look";
    if (!strcmp(type, "fighter-body") || !strcmp(type, "fighter-model")) return "Fighter";
    if (!strcmp(type, "clothing")) return "Clothing";
    if (!strcmp(type, "venue")) return "Venue";
    if (!strcmp(type, "new-fighter")) return "New fighter";
    if (!strcmp(type, "bundle")) return "Pack";
    return type[0] ? type : "Pack";
}

// A small rounded label after a title: "Single", "Pack (N mods)". Returns its width.
static float content_tag(ImDrawList *dl, ImVec2 at, const char *text, float s, const ImVec4 &tint)
{
    ImVec2 ts = text_size(bold_font(), 11.5f * s, text);
    ImVec2 a(at.x, at.y), b(at.x + ts.x + 14 * s, at.y + ts.y + 6 * s);
    dl->AddRectFilled(a, b, col(tint, 0.16f), 9 * s);
    dl->AddRect(a, b, col(tint, 0.50f), 9 * s);
    dl->AddText(bold_font(), 11.5f * s, ImVec2(a.x + 7 * s, a.y + 3 * s), col(tint), text);
    return b.x - a.x;
}

#include "pc_launcher_community.inc"   // the Community tab: every submitted pack's page, votes

// Build 94: the own packs of packs[] switched off (off) or on, all in one write of the own-off list (a setting holds
// 1023 characters; a player's own roster is several hundred packs). only_offline: just those that keep the game offline.
static void own_packs_switch(const PcContentPack *packs, int count, bool off, bool only_offline)
{
    std::vector<char> csv(MODS_OWN_TEXT);
    std::string ids;
    for (int i = 0; i < count; i++)
        if (packs[i].local && (!only_offline || !packs[i].online_ok)) ids += (ids.empty() ? "" : ",") + std::string(packs[i].id);
    if (pc_content_own_off_csv(csv.data(), (int)csv.size()) < 0) return;
    if (off) {
        if (!mods_list_add(csv.data(), csv.size(), ids.c_str())) return;
    } else {
        mods_list_remove(csv.data(), ids.c_str());
        for (int i = 0; i < count; i++)
            if (packs[i].local && pc_content_is_off(packs[i].id)) pc_content_set_off(packs[i].id, 0);   // an old [content] off entry
    }
    pc_content_own_off_put(csv.data());
}

// MY PACKS: the player's own packs (FFNY Studio's "Use in my game": <data>/mods/<id> with local.json, content.ini
// local=1). Each has a switch. One that adds or changes more than textures (clothing, venues, fighters, props)
// works offline only: while one is on the game refuses online (PC_CONTENT_LOCAL_LINE), so a line here says so,
// with one button that turns those off. Returns the y below the section.
static bool content_test_step(const char *want, bool busy);      // tests (below): RECOMP_TEST_CONTENT

template <typename Toggle>
static float my_packs_view(ImDrawList *ld, PcContentPack *packs, int count, float lx, float y, float inner, float s,
                           bool busy, Toggle &toggle)
{
    y += 8 * s;
    ld->AddText(display_font(), 22 * s, ImVec2(lx + 4 * s, y), col(k_white), "MY PACKS");
    ld->AddText(ImGui::GetFont(), 13 * s, ImVec2(lx + 120 * s, y + 7 * s), col(k_white, 0.6f),
                "Made by you in FFNY Studio (Use in my game). Only on this PC.");
    y += 34 * s;
    int blocking = 0, mine_total = 0, mine_off = 0;
    for (int i = 0; i < count; i++) {
        if (!packs[i].local) continue;
        mine_total++;
        mine_off += packs[i].off != 0;
        blocking += !packs[i].online_ok && !packs[i].off;
    }
    if (mine_off) {     // build 94: all of them on again in one press (the master switch brings back what was on)
        char line[160];
        snprintf(line, sizeof line, "%d of your %d packs are off.", mine_off, mine_total);
        ImVec2 ra(lx, y), rb(lx + inner - 14 * s, y + 52 * s);
        ld->AddRectFilled(ra, rb, col(k_white, 0.05f), 12 * s);
        ld->AddRect(ra, rb, col(k_white, 0.12f), 12 * s);
        ld->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(ra.x + 16 * s, ra.y + 17 * s), col(k_white, 0.85f), line);
        if (glass_button("##content_mine_all_on", "Turn all on", ImVec2(rb.x - 176 * s, ra.y + 9 * s), ImVec2(160 * s, 34 * s), s,
                         GLASS_PLAIN, bold_font(), 14 * s, !busy) ||
            content_test_step("allon", busy)) {
            own_packs_switch(packs, count, false, false);
            PcContentPack none{};
            snprintf(none.id, sizeof none.id, "%s", "-");
            toggle(none, false);        // saves the settings and composes again ("-" is no pack)
        }
        y += 62 * s;
    }
    if (blocking) {
        ImVec2 ra(lx, y), rb(lx + inner - 14 * s, y + 52 * s);
        ld->AddRectFilled(ra, rb, col(k_gold, 0.12f), 12 * s);
        ld->AddRect(ra, rb, col(k_gold, 0.45f), 12 * s);
        ld->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(ra.x + 16 * s, ra.y + 9 * s), col(k_gold),
                    PC_CONTENT_LOCAL_LINE, nullptr, inner - 230 * s);
        if (glass_button("##content_mine_off", "Turn them off", ImVec2(rb.x - 176 * s, ra.y + 9 * s), ImVec2(160 * s, 34 * s), s,
                         GLASS_RED, bold_font(), 14 * s, !busy)) {
            own_packs_switch(packs, count, true, true);
            PcContentPack none{};
            snprintf(none.id, sizeof none.id, "%s", "-");
            toggle(none, false);        // saves the settings and composes again ("-" is no pack: switched on, a no-op)
        }
        y += 62 * s;
    }
    for (int i = 0; i < count; i++) {
        PcContentPack &p = packs[i];
        if (!p.local) continue;
        bool on = !p.off;
        ImVec2 ra(lx, y), rb(lx + inner - 14 * s, y + 76 * s);
        ld->AddRectFilled(ra, rb, col(k_white, on ? 0.05f : 0.025f), 12 * s);
        ld->AddRect(ra, rb, col(k_white, 0.06f), 12 * s);
        ImVec4 ink = on ? k_white : ImVec4(1, 1, 1, 0.55f);
        const char *title = p.name[0] ? p.name : p.id;
        ld->PushClipRect(ra, ImVec2(rb.x - 150 * s, rb.y), true);
        ld->AddText(bold_font(), 17 * s, ImVec2(ra.x + 18 * s, ra.y + 10 * s), col(ink), title);
        content_tag(ld, ImVec2(ra.x + 18 * s + text_size(bold_font(), 17 * s, title).x + 10 * s, ra.y + 11 * s),
                    p.online_ok ? "Online too" : "Offline only", s, p.online_ok ? k_green : k_gold);
        char line[400];
        snprintf(line, sizeof line, "%s  \xC2\xB7  v%s  \xC2\xB7  %s", content_type_label(p.type), p.version,
                 p.online_ok ? "only changes how things look, so it can stay on online"
                             : "adds or changes more than looks: online is off while it is on");
        ld->AddText(ImGui::GetFont(), 13 * s, ImVec2(ra.x + 18 * s, ra.y + 34 * s), col(ImVec4(0.80f, 0.80f, 0.82f, 1)), line);
        if (p.composed && !p.applied && on)
            ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(ra.x + 18 * s, ra.y + 53 * s), col(k_gold),
                        "Not applied to your game files (it does not fit them, or the launcher has not prepared it yet).");
        ld->PopClipRect();
        char id[128];
        snprintf(id, sizeof id, "##content_mine_%s", p.id);
        if (glass_button(id, on ? "On" : "Off", ImVec2(rb.x - 136 * s, ra.y + 20 * s), ImVec2(118 * s, 36 * s), s,
                         on ? GLASS_RED : GLASS_PLAIN, bold_font(), 14.5f * s, !busy))
            toggle(p, on);
        y += 86 * s;
        ImGui::SetCursorScreenPos(ImVec2(lx, y));
        ImGui::Dummy(ImVec2(1, 1));
    }
    return y;
}

// Build 90, Turn all mods off (mods_master_logic.h): one button for the game as the original, one that brings back
// what was on. The settings it moves: community content in offline play, the player's own packs, HD textures, the
// community graphics pack and a language pack. Build 94: the own packs' switches and the backup are files (a setting
// holds 1023 characters, about 60 pack ids; a player's own roster is several hundred).
#define MODS_BACKUP_FILE "mods-backup.txt"

static void mods_state_now(ModsState &st, const std::string &local_ids)
{
    memset(&st, 0, sizeof st);
    st.community = recomp_settings_get("content", "community_offline", 1) != 0;
    st.textures = recomp_settings_get("textures", "enabled", 0) != 0;
    st.graphics = recomp_settings_get("graphics", "enabled", 0) != 0;
    recomp_settings_get_text("language", "pack", st.language_pack, sizeof st.language_pack, "");
    recomp_settings_get_text("content", "off", st.content_off, sizeof st.content_off, "");
    pc_content_own_off_csv(st.own_off, (int)sizeof st.own_off);
    mods_normalise(&st, local_ids.c_str());         // builds 90 to 93 kept an own pack's switch in [content] off
}

// Writes the state; false when the own packs' file could not be saved (nothing else was changed then).
static bool mods_state_apply(const ModsState &st)
{
    if (!pc_content_own_off_put(st.own_off)) return false;
    recomp_settings_set("content", "community_offline", st.community ? 1 : 0);
    recomp_settings_set("textures", "enabled", st.textures ? 1 : 0);
    recomp_settings_set("graphics", "enabled", st.graphics ? 1 : 0);
    recomp_settings_set_text("language", "pack", st.language_pack);
    recomp_settings_set_text("content", "off", st.content_off);
    pc_settings_save();
    return true;
}

// The ids of the player's own packs, comma-separated.
static std::string mods_local_ids(const PcContentPack *packs, int count)
{
    std::string ids;
    for (int i = 0; i < count; i++)
        if (packs[i].local) ids += (ids.empty() ? "" : ",") + std::string(packs[i].id);
    return ids;
}

// The backup the master switch made: present (a file or an old setting holds something), usable (it reads as a state).
static void mods_backup_load(ModsState &out, bool &present, bool &usable, const std::string &local_ids)
{
    std::vector<char> text(MODS_BACKUP_TEXT + 16);
    if (pc_content_state_read(MODS_BACKUP_FILE, text.data(), (int)text.size()) <= 0)
        recomp_settings_get_text("content", "mods_backup", text.data(), MODS_TEXT, "");      // builds 90 to 93
    present = text[0] != 0;
    usable = present && mods_backup_decode(text.data(), &out);
    if (usable) usable = mods_normalise(&out, local_ids.c_str());
}

static void mods_backup_clear()
{
    pc_content_state_write(MODS_BACKUP_FILE, "");
    recomp_settings_set_text("content", "mods_backup", "");
}

// Pressed: the master button. Returns true when it moved a setting (the own packs' files are composed again).
static bool mods_master_press(const std::string &local_ids)
{
    auto now = std::make_unique<ModsState>(), before = std::make_unique<ModsState>(), next = std::make_unique<ModsState>();
    bool present = false, usable = false;
    mods_state_now(*now, local_ids);
    mods_backup_load(*before, present, usable, local_ids);
    // a backup that is itself all off would bring nothing back: it is no help to the player, so it counts as none
    if (usable && !mods_is_all_off(before.get(), local_ids.c_str())) {         // back on, exactly as it was
        if (!mods_state_apply(*before)) return false;
        mods_backup_clear();
        pc_settings_save();
        fprintf(stderr, "[CONTENT] mods back on as before (community %d, textures %d, graphics %d, language pack '%s')\n",
                before->community, before->textures, before->graphics, before->language_pack);
        return true;
    }
    if (mods_is_all_off(now.get(), local_ids.c_str()) || usable) {
        // everything is off and there is nothing to restore: the community content and every own pack come on; textures,
        // the graphics pack and the language mod stay as they are
        mods_back_on_default(now.get(), local_ids.c_str(), next.get());
        if (!mods_state_apply(*next)) return false;
        mods_backup_clear();
        pc_settings_save();
        fprintf(stderr, "[CONTENT] mods back on (no usable backup): community content and all own packs on\n");
        return true;
    }
    std::vector<char> text(MODS_BACKUP_TEXT);
    if (!mods_all_off(now.get(), local_ids.c_str(), next.get()) || !mods_backup_encode(now.get(), text.data(), text.size())) return false;
    if (!pc_content_state_write(MODS_BACKUP_FILE, text.data())) return false;
    recomp_settings_set_text("content", "mods_backup", "");
    if (!mods_state_apply(*next)) { mods_backup_clear(); return false; }
    fprintf(stderr, "[CONTENT] all mods off (saved %u bytes)\n", (unsigned)strlen(text.data()));
    return true;
}

// Tests: RECOMP_TEST_CONTENT="<frame>:<step>;..." runs steps in order once the Content page is open and no job is
// running: master (presses the master switch's button), allon (MY PACKS' "Turn all on") and report (one [CONTENT-TEST] line: the community setting,
// how many own packs are on, whether a backup is kept, the helper's last status line).
static bool content_test_step(const char *want, bool busy)
{
    struct Step { int frame; std::string action; bool done; };
    static std::vector<Step> steps;
    static bool parsed;
    if (!parsed) {
        parsed = true;
        const char *text = getenv("RECOMP_TEST_CONTENT");
        std::string all = text ? text : "";
        for (size_t at = 0; at < all.size();) {
            size_t end = all.find(';', at);
            if (end == std::string::npos) end = all.size();
            std::string item = all.substr(at, end - at);
            size_t colon = item.find(':');
            if (colon != std::string::npos) steps.push_back({atoi(item.c_str()), item.substr(colon + 1), false});
            at = end + 1;
        }
    }
    static int last_frame = -1;
    for (auto &st : steps) {
        if (st.done) continue;
        // not in the frame of the step before: a press starts its compose job later in that frame (content_dialog)
        if (busy || pcui::mod_busy() || ImGui::GetFrameCount() <= last_frame || ImGui::GetFrameCount() < st.frame ||
            st.action != want) return false;
        st.done = true;
        last_frame = ImGui::GetFrameCount();
        fprintf(stderr, "[CONTENT-TEST] step %s\n", st.action.c_str());
        return true;
    }
    return false;
}

static void content_test_report(const std::string &local_ids)
{
    auto now = std::make_unique<ModsState>(), before = std::make_unique<ModsState>();
    bool present = false, usable = false;
    int on = 0, total = 0;
    mods_state_now(*now, local_ids);
    mods_backup_load(*before, present, usable, local_ids);
    mods_own_count(now.get(), local_ids.c_str(), &on, &total);
    fprintf(stderr, "[CONTENT-TEST] report community=%d own_on=%d own_total=%d backup=%d/%d all_off=%d status='%s'\n",
            now->community, on, total, present, usable, mods_is_all_off(now.get(), local_ids.c_str()), pcui::mod_status().c_str());
}

// What the card says, worked out at most twice a second (it reads files and compares every own pack).
struct ModsCard {
    bool saved = false, all_off = false;
    std::string title, line, key;
    double stamp = -10;
};
static ModsCard g_mods_card;

static void mods_card_refresh(const std::string &local_ids)
{
    double t = ImGui::GetTime();
    if (t - g_mods_card.stamp < 0.5 && g_mods_card.key == local_ids) return;
    g_mods_card.stamp = t;
    g_mods_card.key = local_ids;
    auto now = std::make_unique<ModsState>(), before = std::make_unique<ModsState>();
    bool present = false, usable = false;
    mods_state_now(*now, local_ids);
    mods_backup_load(*before, present, usable, local_ids);
    bool saved = usable && !mods_is_all_off(before.get(), local_ids.c_str());
    bool all_off = mods_is_all_off(now.get(), local_ids.c_str());
    int own_on = 0, own_total = 0;
    mods_own_count(now.get(), local_ids.c_str(), &own_on, &own_total);
    std::string title, line;
    if (all_off) {
        title = "Mods: OFF";
        line = "The game starts as the original: no community content, no packs of yours, no HD textures, no graphics pack, no "
               "language mod. Online still asks to restart with the community content. ";
        line += saved ? "Turn mods back on brings back what was on before (own packs you had switched off stay off)."
                      : "Turn mods back on switches on the community content and all your own packs; HD textures, the graphics "
                        "pack and the language mod stay as they are.";
    } else if (saved) {
        title = "Mods: partly on";
        line = "You turned all mods off and switched some back on. The button brings back what was on before.";
    } else {
        title = "Mods: ON";
        std::string on;
        auto add = [&](const std::string &what) { on += (on.empty() ? "" : ", ") + what; };
        if (now->community) add("community content");
        if (own_on > 0) add(own_on == own_total ? "your own packs" : "your own packs (" + std::to_string(own_on) + " of " + std::to_string(own_total) + ")");
        if (now->textures) add("HD textures");
        if (now->graphics) add("graphics pack");
        if (now->language_pack[0]) add("language mod");
        line = "On: " + (on.empty() ? std::string("nothing") : on);
        if (own_total > 0 && own_on == 0) line += ". All your own packs are off (MY PACKS, below)";
        line += ". The button turns them all off at once and remembers them.";
    }
    g_mods_card.saved = saved;
    g_mods_card.all_off = all_off;
    g_mods_card.title = title;
    g_mods_card.line = line;
}

static float mods_master_card(ImDrawList *ld, float lx, float y, float inner, float s, bool busy, const std::string &local_ids,
                              bool &moved)
{
    mods_card_refresh(local_ids);
    const ModsCard &c = g_mods_card;
    ImVec2 ra(lx, y), rb(lx + inner - 14 * s, y + 92 * s);
    ld->AddRectFilled(ra, rb, col(k_white, 0.05f), 12 * s);
    ld->AddRect(ra, rb, col(c.all_off ? k_gold : k_green, 0.55f), 12 * s);
    ld->PushClipRect(ra, ImVec2(rb.x - 210 * s, rb.y), true);
    ld->AddText(bold_font(), 18 * s, ImVec2(ra.x + 18 * s, ra.y + 8 * s), col(k_white), c.title.c_str());
    ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(ra.x + 18 * s, ra.y + 33 * s), col(ImVec4(0.80f, 0.80f, 0.82f, 1)),
                c.line.c_str(), nullptr, inner - 250 * s);
    ld->PopClipRect();
    const bool back = c.saved || c.all_off;         // the button always does what its label says
    if (glass_button("##content_mods_master", back ? "Turn mods back on" : "Turn all mods off",
                     ImVec2(rb.x - 196 * s, ra.y + 28 * s), ImVec2(178 * s, 36 * s), s, back ? GLASS_PLAIN : GLASS_RED,
                     bold_font(), 14.5f * s, !busy) ||
        content_test_step("master", busy)) {
        moved = mods_master_press(local_ids);
        g_mods_card.stamp = -10;
    }
    if (content_test_step("report", busy)) content_test_report(local_ids);
    return y + 104 * s;
}

static void content_dialog(ImVec2 o, float w, float h, float s)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    static int count = -1;
    static bool changed;            // a switch moved
    static bool composing;          // ... and the launcher composes the packs' files again
    static bool reload_list;        // build 94: read the packs' switches again (own packs' state comes from the list)
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
    if (count < 0 || reload_list || (was_busy && !busy)) { count = pc_content_list(packs, PC_CONTENT_MAX); reload_list = false; }
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
    {   // build 90: Turn all mods off / back on
        bool moved = false;
        y = mods_master_card(ld, lx, y, inner, s, busy, mods_local_ids(packs, count), moved);
        ImGui::SetCursorScreenPos(ImVec2(lx, y));
        ImGui::Dummy(ImVec2(1, 1));
        if (moved) {
            changed = true;
            composing = pcui::mod_start("compose");     // the own packs' files follow the switches
            fprintf(stderr, "[CONTENT] compose %s after the master switch\n", composing ? "started" : "NOT started");
            reload_list = true;
        }
    }
    {   // build 90: community content in offline play (settings.ini [content] community_offline, default on)
        RecompSetting *sw = find("content", "community_offline");
        bool on = recomp_settings_get("content", "community_offline", 1) != 0;
        bool locked = sw && sw->source == RECOMP_SETTING_FROM_ENV;
        ImVec2 ra(lx, y), rb(lx + inner - 14 * s, y + 78 * s);
        ld->AddRectFilled(ra, rb, col(k_white, 0.05f), 12 * s);
        ld->AddRect(ra, rb, col(on ? k_green : k_gold, 0.45f), 12 * s);
        ld->PushClipRect(ra, ImVec2(rb.x - 150 * s, rb.y), true);
        ld->AddText(bold_font(), 17 * s, ImVec2(ra.x + 18 * s, ra.y + 9 * s), col(k_white), "Community content in offline play");
        ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(ra.x + 18 * s, ra.y + 33 * s), col(ImVec4(0.80f, 0.80f, 0.82f, 1)),
                    on ? "Offline Battle and Story use the community packs below. Online always needs them."
                       : "Off: offline play has only the game's own fighters and venues; your own packs still apply. "
                         "The packs stay installed. Online asks to restart with them on.",
                    nullptr, inner - 190 * s);
        ld->PopClipRect();
        if (glass_button("##content_offline_switch", on ? "On" : "Off", ImVec2(rb.x - 136 * s, ra.y + 21 * s),
                         ImVec2(118 * s, 36 * s), s, on ? GLASS_RED : GLASS_PLAIN, bold_font(), 14.5f * s, !locked)) {
            recomp_settings_set("content", "community_offline", on ? 0 : 1);
            pc_settings_save();
        }
        y += 90 * s;
        ImGui::SetCursorScreenPos(ImVec2(lx, y));
        ImGui::Dummy(ImVec2(1, 1));
    }
    // community content is "always on" only while it is in offline play too; with the switch off it is online only
    const bool comm_off = recomp_settings_get("content", "community_offline", 1) == 0;
    const char *k_always = comm_off ? "ONLINE ONLY" : "ALWAYS ON";
    const ImVec4 k_always_ink = comm_off ? k_gold : k_green;
    int community = 0, mine = 0;
    for (int i = 0; i < count; i++) (packs[i].local ? mine : community)++;
    if (community == 0) {
        ld->AddText(ImGui::GetFont(), 14 * s, ImVec2(lx + 4 * s, y + 8 * s), col(k_white, 0.7f),
                    busy ? "Checking the catalogue..." : "No community packs yet. They arrive here by themselves once the team "
                                                         "accepts them.", nullptr, inner - 20 * s);
        ImGui::Dummy(ImVec2(inner - 20 * s, 60 * s));
    }
    // a switch moved: the composed files follow (fighter bodies, clothing and venues are composed into the
    // game's files: again, now)
    auto toggle = [&](const PcContentPack &p, bool on) {
        if (p.local) pc_content_own_set_off(p.id, on ? 1 : 0);
        else pc_content_set_off(p.id, on ? 1 : 0);
        reload_list = true;             // the list is read again next frame: each pack's switch as it is now
        pc_settings_save();
        changed = true;
        composing = pcui::mod_start("compose");
    };
    for (int i = 0; i < count; i++) {
        PcContentPack &p = packs[i];
        if (p.local) continue;          // MY PACKS, below
        if (p.bundle[0]) {
            // a pack of several mods: one card where its first mod comes, its mods listed inside
            bool first = true;
            for (int j = 0; j < i && first; j++) first = strcmp(packs[j].bundle, p.bundle) != 0;
            if (!first) continue;
            int members[PC_CONTENT_MAX], nm = 0, ncontent = 0;
            long long bytes = 0;
            for (int j = i; j < count; j++)
                if (!strcmp(packs[j].bundle, p.bundle)) {
                    members[nm++] = j;
                    ncontent += !strcmp(packs[j].kind, "content");
                    bytes += packs[j].bytes;
                }
            float row_h = 30 * s;
            float credits_h = credits_height(p.credits, inner - 14 * s - 150 * s, s);
            ImVec2 ra(lx, y), rb(lx + inner - 14 * s, y + 70 * s + nm * row_h + 8 * s + credits_h);
            ld->AddRectFilled(ra, rb, col(k_white, 0.05f), 12 * s);
            ld->AddRect(ra, rb, col(k_white, 0.06f), 12 * s);
            const char *title = p.bundle_name[0] ? p.bundle_name : p.bundle;
            ld->PushClipRect(ra, ImVec2(rb.x - 150 * s, rb.y), true);
            ld->AddText(bold_font(), 17 * s, ImVec2(ra.x + 18 * s, ra.y + 10 * s), col(k_white), title);
            char tag[48];
            snprintf(tag, sizeof tag, "Pack (%d mods)", nm);
            content_tag(ld, ImVec2(ra.x + 18 * s + text_size(bold_font(), 17 * s, title).x + 10 * s, ra.y + 11 * s), tag, s,
                        k_gold);
            char line[400];
            snprintf(line, sizeof line, "%s  \xC2\xB7  v%s  \xC2\xB7  %.1f MB%s%s",
                     ncontent == nm ? (comm_off ? "off for offline play, on for online matching" : "always on, part of online matching") :
                     ncontent ? (comm_off ? "new content off for offline play, the rest your choice"
                                          : "new content always on, the rest your choice") : "your choice per mod",
                     p.version, (double)bytes / 1048576.0, p.authors[0] ? "  \xC2\xB7  by " : "", p.authors);
            ld->AddText(ImGui::GetFont(), 13 * s, ImVec2(ra.x + 18 * s, ra.y + 36 * s), col(ImVec4(0.80f, 0.80f, 0.82f, 1)), line);
            ld->PopClipRect();
            if (ncontent == nm)
                ld->AddText(bold_font(), 14 * s, ImVec2(rb.x - (comm_off ? 128 : 114) * s, ra.y + 29 * s), col(k_always_ink), k_always);
            // its mods: name and type; a replacement (a language, a look) keeps its own switch
            float iy = ra.y + 64 * s;
            for (int m = 0; m < nm; m++) {
                PcContentPack &q = packs[members[m]];
                bool content = !strcmp(q.kind, "content");
                bool on = content || !pc_content_is_off(q.id);
                ld->AddLine(ImVec2(ra.x + 18 * s, iy), ImVec2(rb.x - 18 * s, iy), col(k_white, 0.06f));
                ld->PushClipRect(ImVec2(ra.x, iy), ImVec2(rb.x - 130 * s, iy + row_h), true);
                const char *name = q.name[0] ? q.name : q.id;
                ImVec4 ink = on ? k_white : ImVec4(1, 1, 1, 0.55f);
                ld->AddText(ImGui::GetFont(), 14 * s, ImVec2(ra.x + 30 * s, iy + 7 * s), col(ink), name);
                float nx = ra.x + 30 * s + text_size(ImGui::GetFont(), 14 * s, name).x;
                char sub[160];
                snprintf(sub, sizeof sub, "  \xC2\xB7  %s%s", (q.skin ? "Skin" : content_type_label(q.type)),
                         !strcmp(q.type, "language") ? (on ? "  \xC2\xB7  choose it under Settings > General > Game language"
                                                           : "  \xC2\xB7  off: not offered as a game language") : "");
                ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(nx, iy + 8 * s), col(k_white, 0.55f), sub);
                ld->PopClipRect();
                if (!content) {
                    char id[128];
                    snprintf(id, sizeof id, "##content_switch_%s", q.id);
                    if (glass_button(id, on ? "On" : "Off", ImVec2(rb.x - 114 * s, iy + 3 * s), ImVec2(96 * s, row_h - 6 * s), s,
                                     on ? GLASS_RED : GLASS_PLAIN, bold_font(), 13 * s, !busy))
                        toggle(q, on);
                } else if (ncontent != nm) {
                    ld->AddText(bold_font(), 12 * s, ImVec2(rb.x - (comm_off ? 114 : 104) * s, iy + 8 * s), col(k_always_ink), k_always);
                }
                iy += row_h;
            }
            credits_draw(ld, p.credits, ImVec2(ra.x + 18 * s, iy + 6 * s), inner - 14 * s - 150 * s, s);
            y = rb.y + 10 * s;
            ImGui::SetCursorScreenPos(ImVec2(lx, y));
            ImGui::Dummy(ImVec2(1, 1));
            continue;
        }
        bool content = !strcmp(p.kind, "content");
        bool on = content || !pc_content_is_off(p.id);
        float credits_h = credits_height(p.credits, inner - 14 * s - 150 * s - 18 * s, s);
        ImVec2 ra(lx, y), rb(lx + inner - 14 * s, y + 76 * s + credits_h);
        ld->AddRectFilled(ra, rb, col(k_white, on ? 0.05f : 0.025f), 12 * s);
        ld->AddRect(ra, rb, col(k_white, 0.06f), 12 * s);
        ImVec4 ink = on ? k_white : ImVec4(1, 1, 1, 0.55f);
        const char *title = p.name[0] ? p.name : p.id;
        ld->PushClipRect(ra, ImVec2(rb.x - 150 * s, rb.y), true);
        ld->AddText(bold_font(), 17 * s, ImVec2(ra.x + 18 * s, ra.y + 10 * s), col(ink), title);
        content_tag(ld, ImVec2(ra.x + 18 * s + text_size(bold_font(), 17 * s, title).x + 10 * s, ra.y + 11 * s), "Single", s,
                    ImVec4(1, 1, 1, on ? 0.75f : 0.45f));
        ld->PopClipRect();
        char line[400];
        snprintf(line, sizeof line, "%s  \xC2\xB7  %s  \xC2\xB7  v%s  \xC2\xB7  %.1f MB%s%s", (p.skin ? "Skin" : content_type_label(p.type)),
                 content ? (comm_off ? "off for offline play, on for online matching" : "always on, part of online matching")
                         : "your choice", p.version, (double)p.bytes / 1048576.0,
                 p.authors[0] ? "  \xC2\xB7  by " : "", p.authors);
        ld->PushClipRect(ra, ImVec2(rb.x - 150 * s, rb.y), true);
        ld->AddText(ImGui::GetFont(), 13 * s, ImVec2(ra.x + 18 * s, ra.y + 34 * s), col(ImVec4(0.80f, 0.80f, 0.82f, 1)), line);
        if (!strcmp(p.type, "language"))
            ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(ra.x + 18 * s, ra.y + 53 * s), col(k_gold),
                        on ? "Choose it under Settings > General > Game language." : "Off: not offered as a game language.");
        else if (!strcmp(p.type, "new-fighter") && p.template_name[0]) {
            // where to find it: new fighters are an outfit in their template's spot until they get their own cell
            char where[160];
            snprintf(where, sizeof where, "Find it in %s's spot on the select screen (one of the outfits there).",
                     p.template_name);
            ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(ra.x + 18 * s, ra.y + 53 * s), col(k_gold), where);
        } else if (p.description[0])
            ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(ra.x + 18 * s, ra.y + 53 * s), col(k_white, 0.5f), p.description);
        ld->PopClipRect();
        credits_draw(ld, p.credits, ImVec2(ra.x + 18 * s, ra.y + 74 * s), inner - 14 * s - 150 * s - 18 * s, s);
        char id[128];
        snprintf(id, sizeof id, "##content_switch_%s", p.id);
        ImVec2 ba(rb.x - 136 * s, ra.y + 20 * s);
        if (content) {
            ld->AddText(bold_font(), 14 * s, ImVec2(ba.x + (comm_off ? 8 : 22) * s, ba.y + 9 * s), col(k_always_ink), k_always);
        } else if (glass_button(id, on ? "On" : "Off", ba, ImVec2(118 * s, 36 * s), s, on ? GLASS_RED : GLASS_PLAIN,
                                bold_font(), 14.5f * s, !busy)) {
            toggle(p, on);
        }
        y += 86 * s + credits_h;
        ImGui::SetCursorScreenPos(ImVec2(lx, y));
        ImGui::Dummy(ImVec2(1, 1));
    }
    if (mine) y = my_packs_view(ld, packs, count, lx, y, inner, s, busy, toggle);
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
