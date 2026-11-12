// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// MILESTONES (build 79, owner request 2026-11-12): a profile's milestone progress, in the game's look.
// Included by online_screen.cpp inside its file-local namespace; online_screen::milestones_* call in.
//
// Players thought the created fighters' milestones were broken: the game counts punches, kicks, wins
// and more after every finished offline fight and turns them into bonus stat points, but never shows
// how far along a fighter is. Where it shows:
//   - the Battle user-ID screen (options/battleID), for the ID it shows (the screen's own script asks
//     Game.GetMilestone for that ID's badges; milestones.c keeps the ID), and
//   - the Story crib (story/crib), for the Story's profile.
// There a glass chip in the bottom right says VIEW MILESTONES; the pad's VIEW (BACK) or a click opens the
// page (pc_ui.cpp holds the game's input while it is open, as for EXIT). The page lists every milestone:
// what counts, what it gives, a bar, current / target, reached or not; up/down picks a row and the box
// under the list explains it. B, VIEW or ESC closes it. Nothing is written: the rows come from a copy of
// the profile (milestones_rules.c). After a finished match that reached a milestone, a toast says so
// (milestones.c, drawn by pc_ui.cpp with the other notices).
//
// Gallery pages (RECOMP_TEST_ONLINE_GALLERY_ONLY=milestones): "milestones" (the top of the list),
// "milestones-tough" (a Toughness row picked), "milestones-card" (the ID card's badges), "milestones-chip"
// (the chip over a stand-in for the user-ID screen).

struct MilestonesUi {
    int sel = 0, top = 0;
    unsigned long long opened = 0;
    bool gallery = false;               // the gallery shows a milestones page
    bool gallery_chip = false;          // ... the chip alone
    ImVec4 chip;                        // the chip's rectangle in window pixels, as last drawn
    unsigned long long chip_ms = 0;
} MS;

// The sample profile of the gallery: RICO, a Story fighter part of the way.
void ms_sample(uint8_t *p)
{
    memset(p, 0, MS_PROFILE_BYTES);
    memcpy(p + 0xC, "RICO", 4);
    auto w32 = [&](uint32_t o, uint32_t v) { p[o] = v & 255; p[o + 1] = (v >> 8) & 255; p[o + 2] = (v >> 16) & 255; p[o + 3] = v >> 24; };
    auto w16 = [&](uint32_t o, uint32_t v) { p[o] = v & 255; p[o + 1] = (v >> 8) & 255; };
    w32(0x68, 3420); w32(0x6C, 5126); w16(0x70, 250); w16(0x72, 88); w16(0x74, 7); w16(0x76, 31);
    w16(0x80, 2); w32(0x78, 312); w32(0x7C, 41); w32(0x84, 12); w32(0x88, 140); w16(0x8C, 5); w16(0x82, 1);
    const uint32_t base[6] = { 50, 45, 40, 38, 30, 20 };
    for (int i = 0; i < 6; i++) w32(0xFC + 4 * i, base[i]);
    p[0x140] = 0; p[0x141] = 3; p[0x142] = 0xFF;
    for (int i = 0; i < 30; i++) p[0x4D8B + (i >> 3)] |= (uint8_t)(1u << (i & 7));
}

// The profile the page is for: 0 none. name gets its user ID.
int ms_profile(uint8_t *buf, char *name, int size)
{
    if (gallery_on()) {
        ms_sample(buf);
        snprintf(name, size, "RICO");
        return 1;
    }
    int screen = g_ms_screen.load();
    int index = screen == 1 ? milestones_battle_profile() : screen == 2 ? milestones_current_profile() : -1;
    return index >= 0 && milestones_read_profile(index, buf, name, size);
}

bool ms_fight_running()
{
    static uint32_t last_beat;
    static unsigned long long moved;
    FightHudView v;
    fight_hud_view(&v);
    unsigned long long now = GetTickCount64();
    if (v.beat != last_beat) { last_beat = v.beat; moved = now; }
    return netplay_in_fight() || v.live || (moved && now - moved < 1500);
}

// The pad's VIEW button (as on the EXIT chip): two small overlapping frames.
void ms_view_glyph(ImDrawList *dl, const Frame &f, float x, float y)
{
    dl->AddCircleFilled(f.p(x + 8, y + 8), 8.5f * f.s(), IM_COL32(70, 70, 70, 255));
    float t = std::max(1.0f, 1.1f * f.s());
    dl->AddRect(f.p(x + 3.5f, y + 4.5f), f.p(x + 10, y + 9.5f), WHITE, 1.0f * f.s(), 0, t);
    dl->AddRectFilled(f.p(x + 6, y + 7), f.p(x + 12.5f, y + 12), WHITE, 1.0f * f.s());
}

void ms_draw_chip(ImDrawList *dl, const Frame &f)
{
    const float h = 22, y = 446, right = 606;
    float word = text_width(f, g_title, 13, "MILESTONES");
    float w = 10 + 16 + 6 + word + 12, x = right - w;
    glass(dl, f, x, y, w, h, 10, IM_COL32(10, 10, 13, 255), 0.62f);
    ms_view_glyph(dl, f, x + 10, y + 3);
    text(dl, f, g_title, 13, x + 32, y + 4, WHITE, "MILESTONES", LEFT, 1.0f);
    ImVec2 a = f.p(x, y), b = f.p(x + w, y + h);
    MS.chip = ImVec4(a.x, a.y, b.x, b.y);
    MS.chip_ms = GetTickCount64();
    static bool logged;
    if (!logged) {
        logged = true;
        fprintf(stderr, "[UI] milestones chip at %d,%d-%d,%d\n", (int)a.x, (int)a.y, (int)b.x, (int)b.y);
    }
}

// What the box under the list says about a row.
void ms_detail(const MilestoneRow &r, char *line1, int n1, char *line2, int n2)
{
    char count[24], target[24];
    milestones_count_text(r.count, count, sizeof(count));
    milestones_count_text(r.target, target, sizeof(target));
    if (r.group == MS_BADGE)
        snprintf(line1, n1, "%s: %s of %s. The Battle user-ID screen shows a badge on the ID card for it.", r.what,
                 count, target);
    else if (r.target2) {
        char c2[24], t2[24];
        milestones_count_text(r.count2, c2, sizeof(c2));
        milestones_count_text(r.target2, t2, sizeof(t2));
        snprintf(line1, n1, "Both needed: counters %s of %s and blocks %s of %s. Gives %s.", count, target, c2, t2, r.reward);
    } else if (r.note == MS_NOTE_COMEBACK)
        snprintf(line1, n1, "Big comeback: %s%% of %s%% health won back after being in danger. %s %s.", count, target,
                 r.reached ? "Reached:" : "Gives", r.reward);
    else
        snprintf(line1, n1, "%s: %s, needs %s. %s %s%s", r.what, count, target, r.reached ? "Reached:" : "Gives",
                 r.reward, r.note == MS_NOTE_REPLACES ? " (in place of the +1 at 100)." : ".");
    if (r.note == MS_NOTE_TOUGHNESS_FIX)
        snprintf(line2, n2, "Fixed in build 76: before, the game kept only the last fight here, so it never got past 1.");
    else if (r.note == MS_NOTE_TOUGHNESS_RETAIL)
        snprintf(line2, n2, "The Toughness fix is off: the game keeps only the last fight here (Settings, Gameplay).");
    else if (r.group == MS_BADGE)
        snprintf(line2, n2, "A badge only: it gives no stat points.");
    else if (!r.sure)
        snprintf(line2, n2, "* What the game counts here is read from its code, not confirmed: the name may be off.");
    else if (r.note == MS_NOTE_REPLACES)
        snprintf(line2, n2, "One bonus point over the +1: about half the time the stat shown goes up by one.");
    else if (r.note == MS_NOTE_COMEBACK)
        snprintf(line2, n2, "One big comeback is usually enough: get into danger, win back health and win the fight.");
    else
        snprintf(line2, n2, "Counts across all fights since the fighter was made.");
}

// The page: over the game's screen, in its 640x480 space. Returns false when the player closes it.
bool ms_page(ImDrawList *dl, const Frame &f, ImVec2 display, const Pad &pad)
{
    static uint8_t buf[MS_PROFILE_BYTES];
    char name[24] = "";
    MilestoneRow rows[MS_MAX_ROWS];
    int n = 0;
    if (ms_profile(buf, name, sizeof(name)))
        n = milestones_rows(buf, MS_PROFILE_BYTES, recomp_settings_get("gameplay", "toughness_fix", 1), rows, MS_MAX_ROWS);
    if (!gallery_on()) dl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 150));
    const float px = 28, py = 26, pw = 584, ph = 430;
    glass(dl, f, px, py, pw, ph, 14, IM_COL32(10, 10, 13, 255), 0.88f);
    text(dl, f, g_stencil, 28, px + 20, py + 12, CREAM, "MILESTONES", LEFT, 1.6f);
    if (n) {
        char line[64];
        text(dl, f, g_title, 17, px + pw - 20, py + 10, WHITE, name, RIGHT, 1.2f);
        snprintf(line, sizeof(line), "%d of %d reached", milestones_reached(rows, n), n);
        text(dl, f, g_hand, 13, px + pw - 20, py + 31, ORANGE, line, RIGHT, 1.0f);
    }
    if (!n) {
        text(dl, f, g_title, 15, 320, 200, WHITE, "No user ID here.", CENTER, 1.2f);
        text(dl, f, g_hand, 14, 320, 224, ORANGE, "Pick or load a user ID first.", CENTER, 1.0f);
    } else {
        if (pad.up) MS.sel = (MS.sel + n - 1) % n;
        if (pad.down) MS.sel = (MS.sel + 1) % n;
        if (pad.lb) MS.sel = std::max(0, MS.sel - 10);
        if (pad.rb) MS.sel = std::min(n - 1, MS.sel + 10);
        if (MS.sel >= n) MS.sel = n - 1;
        const int visible = 9;
        const float ly = py + 70, rh = 25;
        if (MS.sel < MS.top) MS.top = MS.sel;
        if (MS.sel >= MS.top + visible) MS.top = MS.sel - visible + 1;
        if (MS.top > n - visible) MS.top = std::max(0, n - visible);
        // the columns' heads
        text(dl, f, g_cond, 11, px + 22, ly - 13, GREY, "STAT", LEFT, 0);
        text(dl, f, g_cond, 11, px + 112, ly - 13, GREY, "WHAT COUNTS", LEFT, 0);
        text(dl, f, g_cond, 11, px + 318, ly - 13, GREY, "GIVES", LEFT, 0);
        text(dl, f, g_cond, 11, px + 410, ly - 13, GREY, "PROGRESS", LEFT, 0);
        for (int i = MS.top; i < n && i < MS.top + visible; i++) {
            const MilestoneRow &r = rows[i];
            float y = ly + (i - MS.top) * rh;
            bool sel = i == MS.sel;
            if (mouse_pick(f, px + 10, y, pw - 20, rh - 2)) MS.sel = i;
            if (sel) glass(dl, f, px + 10, y, pw - 20, rh - 2, 6, RED, 0.55f);
            else if ((i & 1) == 0) rect(dl, f, px + 10, y, pw - 20, rh - 2, IM_COL32(255, 255, 255, 10), 6);
            bool first = i == 0 || i == MS.top || rows[i - 1].group != r.group;
            if (first || sel)
                text(dl, f, g_cond, 12, px + 22, y + 6, sel ? WHITE : r.group == MS_BADGE ? YELLOW : CREAM,
                     milestones_group_name(r.group), LEFT, 0);
            char what[80];
            snprintf(what, sizeof(what), "%s%s", r.what, r.sure ? "" : " *");
            text(dl, f, g_title, 13, px + 112, y + 5, r.reached ? WHITE : IM_COL32(220, 220, 220, 255), what, LEFT, 1.0f);
            text(dl, f, g_title, 12, px + 318, y + 6, r.group == MS_BADGE ? YELLOW : ORANGE, r.reward, LEFT, 1.0f);
            // the bar and the numbers
            const float bx = px + 410, bw = 64, bh = 7, by = y + 9;
            rect(dl, f, bx, by, bw, bh, IM_COL32(255, 255, 255, 40), 3);
            float fr = milestones_fraction(&r);
            if (fr > 0) rect(dl, f, bx, by, std::max(3.0f, bw * fr), bh, r.reached ? GREEN : ORANGE, 3);
            char prog[48];
            if (r.reached && !r.target2) {
                char t[24];
                milestones_count_text(r.target, t, sizeof(t));
                snprintf(prog, sizeof(prog), "DONE  %s", t);
            } else if (r.target2) {
                snprintf(prog, sizeof(prog), "%d%%", (int)(fr * 100));
                if (r.reached) snprintf(prog, sizeof(prog), "DONE");
            } else
                milestones_progress_text(&r, prog, sizeof(prog));
            text(dl, f, g_body, 11, px + pw - 20, y + 6, r.reached ? GREEN : WHITE, prog, RIGHT, 0.8f);
        }
        // more above / below
        if (MS.top > 0)
            dl->AddTriangleFilled(f.p(314, ly - 4), f.p(326, ly - 4), f.p(320, ly - 11), GREY);
        if (MS.top + visible < n)
            dl->AddTriangleFilled(f.p(314, ly + visible * rh + 1), f.p(326, ly + visible * rh + 1),
                                  f.p(320, ly + visible * rh + 8), GREY);
        // the picked row, explained
        char l1[200], l2[200];
        ms_detail(rows[MS.sel], l1, sizeof(l1), l2, sizeof(l2));
        const float dy = ly + visible * rh + 12;
        rect(dl, f, px + 10, dy, pw - 20, 44, IM_COL32(255, 255, 255, 14), 6);
        text(dl, f, g_title, 12.5f, px + 22, dy + 6, WHITE, l1, LEFT, 1.0f, pw - 44);
        text(dl, f, g_hand, 12.5f, px + 22, dy + 25, ORANGE, l2, LEFT, 0.8f, pw - 44);
    }
    // the rule every player has to know
    const float hy = py + ph - 64;
    text(dl, f, g_title, 11.5f, px + 22, hy, CREAM,
         "Counts only offline fights with this user ID (Story, or Battle with the ID picked) that end by KO or time.",
         LEFT, 0.8f, pw - 44);
    text(dl, f, g_title, 11.5f, px + 22, hy + 14, CREAM,
         "Quitting a fight from pause does not count, online fights never count, and the game must save afterwards.",
         LEFT, 0.8f, pw - 44);
    float hx = px + 22;
    hx += hint(dl, f, hx, py + ph - 26, "lb", "PAGE");
    hx += hint(dl, f, hx, py + ph - 26, "rb", "PAGE");
    hint(dl, f, hx, py + ph - 26, "b", "BACK");
    if (gallery_on()) return true;
    auto pressed = [](std::initializer_list<ImGuiKey> keys) {
        for (ImGuiKey k : keys) if (ImGui::IsKeyPressed(k, false)) return true;
        return false;
    };
    // VIEW again closes it, but not the press that opened it (it reads as new in the first frames)
    bool view = GetTickCount64() - MS.opened > 400 && pressed({ ImGuiKey_GamepadBack });
    if (pad.b || view) {
        fprintf(stderr, "[UI] milestones page closed\n");
        fflush(stderr);
        return false;
    }
    return true;
}

// The gallery's pages: the sample profile, the page as it opens, a row picked, the badges, the chip.
bool milestones_gallery(const std::string &p)
{
    MS.gallery = MS.gallery_chip = false;
    if (p.compare(0, 10, "milestones") != 0) return false;
    MS.gallery = true;
    MS.gallery_chip = p == "milestones-chip";
    MS.sel = p == "milestones-tough" ? 6 : p == "milestones-card" ? 18 : 0;
    MS.top = 0;
    return true;
}
