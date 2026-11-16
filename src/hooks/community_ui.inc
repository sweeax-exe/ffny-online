// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// ---- community fighters beyond the places (fighter_banks.h): the paged list behind MORE ------------------------
// The select grid's shelf shows up to 30 of them; its MORE cell opens this window over the game's own select screen
// (pc_ui.cpp) for the slot that picked it: every one of them, 12 to a page (4 x 3), each with its template's portrait
// (its own look is in its pack's pictures, which the game's screens show), its name and number. The d-pad moves,
// LB / RB turn the page, A takes the fighter for the slot, B keeps the one MORE gave it. Offline only.
// Included by online_screen.cpp inside its anonymous namespace, after the drawing helpers and read_pad.

static const int kCommunityCols = 4, kCommunityRows = 3, kCommunityPage = kCommunityCols * kCommunityRows;

// RECOMP_TEST_COMMUNITY_KEYS=<left|right|up|down|lb|rb|a|b>,... (tests): the open window takes these presses, one
// every 700 ms, as if from a pad (the test copies' virtual pads do not reach the overlay's ImGui).
void community_test_keys(Pad &pad, bool opened_now)
{
    static const char *keys = getenv("RECOMP_TEST_COMMUNITY_KEYS");
    static const char *at;
    static ULONGLONG next;
    if (!keys || !*keys) return;
    if (opened_now) { at = keys; next = GetTickCount64() + 1200; return; }
    if (!at || !*at || GetTickCount64() < next) return;
    next = GetTickCount64() + 700;
    size_t n = strcspn(at, ",");
    if (!strncmp(at, "left", n)) pad.left = true;
    else if (!strncmp(at, "right", n)) pad.right = true;
    else if (!strncmp(at, "up", n)) pad.up = true;
    else if (!strncmp(at, "down", n)) pad.down = true;
    else if (!strncmp(at, "lb", n)) pad.lb = true;
    else if (!strncmp(at, "rb", n)) pad.rb = true;
    else if (!strncmp(at, "a", n)) pad.a = true;
    else if (!strncmp(at, "b", n)) pad.b = true;
    fprintf(stderr, "[BANKS] test press: %.*s\n", (int)n, at);
    at += n + (at[n] == ',');
}

// The window: 0 open, 1 a fighter taken (*sel: its index in fighter_banks_at order), 2 back.
int community_window(ImDrawList *dl, const Frame &f, const Pad &pad_in, int *sel)
{
    Pad pad = pad_in;
    community_test_keys(pad, false);
    int n = fighter_banks_count();
    if (n <= 0) return 2;
    if (*sel < 0 || *sel >= n) *sel = 0;
    int pages = (n + kCommunityPage - 1) / kCommunityPage;
    if (pad.left && *sel > 0) *sel -= 1;
    if (pad.right && *sel + 1 < n) *sel += 1;
    if (pad.up && *sel >= kCommunityCols) *sel -= kCommunityCols;
    if (pad.down && *sel + kCommunityCols < n) *sel += kCommunityCols;
    if (pad.lb) *sel = std::max(0, *sel - kCommunityPage);
    if (pad.rb) *sel = std::min(n - 1, *sel + kCommunityPage);
    int page = *sel / kCommunityPage;
    rect(dl, f, -400, -400, 1440, 1280, IM_COL32(0, 0, 0, 160));
    const float cw = 112, ch = 92, gap = 10;
    const float pw = kCommunityCols * cw + (kCommunityCols - 1) * gap + 60, ph = kCommunityRows * (ch + gap) + 110;
    const float px = 320 - pw / 2, py = 40;
    glass(dl, f, px, py, pw, ph, 14, IM_COL32(10, 10, 13, 255), 0.92f);
    text(dl, f, g_title, 22, 320, py + 10, CREAM, "COMMUNITY FIGHTERS", CENTER, 1.6f);
    char head[64];
    snprintf(head, sizeof head, "PAGE %d / %d   -   %d FIGHTERS", page + 1, pages, n);
    text(dl, f, g_hand, 13, 320, py + 38, ORANGE, head, CENTER, 1.0f);
    int choose = -1;
    for (int k = 0; k < kCommunityPage; k++) {
        int i = page * kCommunityPage + k;
        if (i >= n) break;
        const FighterBanksEntry *e = fighter_banks_at(i);
        float x = px + 30 + (k % kCommunityCols) * (cw + gap), y = py + 62 + (k / kCommunityCols) * (ch + gap);
        bool lit = i == *sel;
        if (mouse_pick(f, x, y, cw, ch)) *sel = i, lit = true;
        if (mouse_click(f, x, y, cw, ch)) choose = i;
        rect(dl, f, x, y, cw, ch, IM_COL32(0, 0, 0, 170), 4);
        portrait(dl, f, e->template_fe_id, x + 2, y + 2, 48, ch - 4, lit ? IM_COL32_WHITE : IM_COL32(140, 140, 140, 255));
        float size = fit_size(f, g_title, 13, e->name, cw - 58);
        text(dl, f, g_title, size, x + 54, y + 18, lit ? WHITE : GREY, e->name, LEFT, 0.8f);
        char num[24];
        snprintf(num, sizeof num, "#%d", e->id);
        text(dl, f, g_body, 9, x + 54, y + 44, with_alpha(lit ? ORANGE : GREY, 0.9f), num, LEFT, 0.6f);
        if (lit) selector(dl, f, x, y, cw, ch);
    }
    float hx = px + 30;
    hx += hint(dl, f, hx, py + ph - 28, "a", "SELECT");
    hx += hint(dl, f, hx, py + ph - 28, "b", "BACK");
    hint(dl, f, hx, py + ph - 28, "rb", "PAGE");
    if (choose >= 0) { *sel = choose; return 1; }
    if (pad.a) return 1;
    if (pad.b) return 2;
    return 0;
}
