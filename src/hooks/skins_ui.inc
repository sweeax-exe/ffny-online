// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// ---- skins (build 85, fighter_skins.h): the skin window ---------------------------------------------------------
// Picking a stock fighter that has community skins opens this window, on the hub's CHOOSE FIGHTER and over the
// game's own select screens (pc_ui.cpp): "Default" (the fighter's own look) and each skin, with its picture and
// name. Left / right browse, A takes the look, B goes back. It opens on the skin played last with that fighter.
// Included by online_screen.cpp inside its anonymous namespace, after the drawing helpers and read_pad.

// One option's picture: the fighter's own portrait for Default, the skin's (online-art/skins/<pack id>.png),
// or, for a pack without art, the fighter's portrait tinted, under the skin's name.
void skin_picture(ImDrawList *dl, const Frame &f, int fe, const FighterSkin *skin, float x, float y, float w, float h,
                  bool lit)
{
    ImU32 tint = lit ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255);
    if (skin && skin->picture[0]) {
        const Tex &t = tex(std::string("skins/") + skin->id + ".png");
        if (t.srv && t.w > 0 && t.h > 0) {
            float v = std::min(1.0f, (t.w / t.h) / (w * f.sx / (h * f.sy)));
            image(dl, f, t, x, y, w, h, tint, ImVec2(0, 0), ImVec2(1, v));
            return;
        }
    }
    if (!portrait(dl, f, fe, x, y, w, h, skin ? (lit ? IM_COL32(255, 196, 120, 255) : IM_COL32(140, 110, 80, 255)) : tint))
        text(dl, f, g_title, 18, x + w / 2, y + h / 2 - 10, with_alpha(CREAM, 0.6f), online_fighter_name(fe), CENTER, 1.2f, w - 6);
}

// RECOMP_TEST_SKIN_KEYS=<left|right|a|b>,... (tests): the open window takes these presses, one every 700 ms, as if
// from a pad (the test copies' virtual pads do not reach the overlay's ImGui). Each window takes the list anew.
// Build 94: the window's own state for many skins (the grid): the name search.
struct SkinWinState { bool search = false; std::string q; };
SkinWinState g_skw;

void skin_test_keys(Pad &pad, bool opened_now)
{
    static const char *keys = getenv("RECOMP_TEST_SKIN_KEYS");
    static const char *at;
    static ULONGLONG next;
    if (opened_now) { g_skw.search = false; g_skw.q.clear(); g_skin_search_on = false; }
    if (!keys || !*keys) return;
    if (opened_now) { at = keys; next = GetTickCount64() + 1200; return; }
    if (!at || !*at || GetTickCount64() < next) return;
    next = GetTickCount64() + 700;
    size_t n = strcspn(at, ",");
    if (!strncmp(at, "left", n)) pad.left = true;
    else if (!strncmp(at, "right", n)) pad.right = true;
    else if (!strncmp(at, "a", n)) pad.a = true;
    else if (!strncmp(at, "b", n)) pad.b = true;
    else if (!strncmp(at, "up", n)) pad.up = true;
    else if (!strncmp(at, "down", n)) pad.down = true;
    else if (!strncmp(at, "lb", n)) pad.lb = true;       // the page back
    else if (!strncmp(at, "rb", n)) pad.rb = true;       // the page on
    else if (!strncmp(at, "q:", 2)) g_skw.q = std::string(at + 2, n - 2), g_skw.search = true;   // a typed search
    fprintf(stderr, "[SKINS] test press: %.*s\n", (int)n, at);
    at += n + (at[n] == ',');
}

// The window over whatever page or screen is below: 0 open, 1 a look taken (*sel: 0 Default, 1.. a skin), 2 back.
int skin_window_row(ImDrawList *dl, const Frame &f, const Pad &pad, int fe, int *sel, int n)
{
    int options = n + 1;
    if (*sel < 0 || *sel >= options) *sel = 0;
    if (pad.left) *sel = (*sel + options - 1) % options;
    if (pad.right) *sel = (*sel + 1) % options;
    rect(dl, f, -400, -400, 1440, 1280, IM_COL32(0, 0, 0, 150));
    const int shown = std::min(options, 5);
    const float cw = 100, ch = 190, gap = 12, pw = shown * cw + (shown - 1) * gap + 60, ph = 300;
    const float px = 320 - pw / 2, py = 84;
    glass(dl, f, px, py, pw, ph, 14, IM_COL32(10, 10, 13, 255), 0.9f);
    text(dl, f, g_title, 22, 320, py + 12, CREAM, "CHOOSE A LOOK", CENTER, 1.6f);
    text(dl, f, g_hand, 15, 320, py + 40, ORANGE, online_fighter_name(fe), CENTER, 1.0f);
    int first = std::max(0, std::min(*sel - shown / 2, options - shown));
    int choose = -1;
    for (int k = 0; k < shown; k++) {
        int i = first + k;
        FighterSkin skin;
        bool have = i > 0 && fighter_skin_get(fe, i, &skin);
        float x = px + 30 + k * (cw + gap), y = py + 66;
        bool lit = i == *sel;
        if (mouse_pick(f, x, y, cw, ch)) *sel = i, lit = true;
        if (mouse_click(f, x, y, cw, ch)) choose = i;
        rect(dl, f, x, y, cw, ch, IM_COL32(0, 0, 0, 160), 4);
        skin_picture(dl, f, fe, have ? &skin : nullptr, x + 2, y + 2, cw - 4, ch - 40, lit);
        const char *name = i == 0 ? "DEFAULT" : have ? skin.name : "?";
        float size = fit_size(f, g_title, 13, name, cw - 8);
        text(dl, f, g_title, size, x + cw / 2, y + ch - 32, lit ? WHITE : GREY, name, CENTER, 0.8f);
        text(dl, f, g_body, 9, x + cw / 2, y + ch - 14, with_alpha(lit ? ORANGE : GREY, 0.9f), i == 0 ? "ITS OWN LOOK" : "SKIN",
             CENTER, 0.6f);
        if (lit) selector(dl, f, x, y, cw, ch);
    }
    if (first > 0) text(dl, f, g_title, 26, px + 12, py + 150, ORANGE, "<", CENTER, 1.0f);
    if (first + shown < options) text(dl, f, g_title, 26, px + pw - 12, py + 150, ORANGE, ">", CENTER, 1.0f);
    char count[32];
    snprintf(count, sizeof(count), "%d / %d", *sel + 1, options);
    text(dl, f, g_title, 11, px + pw - 18, py + 16, GREY, count, RIGHT, 0.8f);
    float hx = px + 30;
    hx += hint(dl, f, hx, py + ph - 28, "a", "SELECT");
    hint(dl, f, hx, py + ph - 28, "b", "BACK");
    if (choose >= 0) { *sel = choose; return 1; }
    if (pad.a) return 1;
    if (pad.b) return 2;
    return 0;
}

// Build 94: a fighter with many skins (more than five looks): a grid of 6 x 3 per page with paging and a name search.
// Pad: left / right / up / down move, LB / RB (Q / E, PageUp / PageDown) turn a page, A takes, B goes back (first
// closes the search). Keyboard: TAB or Ctrl+F opens the search, typed letters filter the names, BACKSPACE takes one
// off. Mouse: hover picks, click takes, the wheel turns pages, the page buttons at the foot. "Default" is always the
// first cell. The look taken is the skin's number among the fighter's skins (not its place in the filtered list).
int skin_window_grid(ImDrawList *dl, const Frame &f, Pad pad, int fe, int *sel, int n)
{
    const int cols = 6, rows = 3, per = cols * rows;
    ImGuiIO &io = ImGui::GetIO();
    SkinWinState &w = g_skw;
    if (pad.keys && (ImGui::IsKeyPressed(ImGuiKey_Tab, false) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)))) {
        w.search = !w.search;
        if (!w.search) w.q.clear();
    }
    if (w.search && pad.keys) {
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c < 0x80 && (isalnum((int)c) || c == ' ' || c == '-' || c == '.') && w.q.size() < 24) w.q += (char)c;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !w.q.empty()) w.q.pop_back();
        if (pad.b) { w.search = false; w.q.clear(); pad.b = false; }       // B / ESC closes the search, not the window
    }
    g_skin_search_on = w.search;
    // the cells: Default, then the skins whose name has the search in it
    std::vector<int> vis;
    vis.push_back(0);
    for (int i = 1; i <= n; i++) {
        FighterSkin s;
        if (fighter_skin_get(fe, i, &s) && (w.q.empty() || fighter_name_matches(s.name, w.q.c_str()))) vis.push_back(i);
    }
    int cnt = (int)vis.size(), pos = 0;
    for (int i = 0; i < cnt; i++) if (vis[i] == *sel) pos = i;
    if (!w.q.empty() && *sel != vis[pos]) pos = 0;
    if (!w.q.empty() && pos == 0 && cnt > 1 && *sel != 0) pos = 1;      // typing: on the first match
    auto move = [&](int d) { pos = std::max(0, std::min(cnt - 1, pos + d)); };
    if (pad.left) pos = (pos + cnt - 1) % cnt;
    if (pad.right) pos = (pos + 1) % cnt;
    if (pad.up) move(-cols);
    if (pad.down) move(cols);
    if (pad.lb) move(-per);
    if (pad.rb) move(per);
    const float cw = 88, ch = 84, gap = 5, pw = cols * cw + (cols - 1) * gap + 40, ph = 56 + rows * (ch + gap) + 40;
    const float px = 320 - pw / 2, py = 30;
    if (mouse_in(f, px, py, pw, ph) && io.MouseWheel != 0) move(io.MouseWheel > 0 ? -cols : cols);
    rect(dl, f, -400, -400, 1440, 1280, IM_COL32(0, 0, 0, 150));
    glass(dl, f, px, py, pw, ph, 14, IM_COL32(10, 10, 13, 255), 0.9f);
    text(dl, f, g_title, 22, 320, py + 10, CREAM, "CHOOSE A LOOK", CENTER, 1.6f);
    text(dl, f, g_hand, 15, 320, py + 36, ORANGE, online_fighter_name(fe), CENTER, 1.0f);
    int pages = (cnt + per - 1) / per, page = pos / per, first = page * per, choose = -1;
    for (int k = 0; k < per && first + k < cnt; k++) {
        int i = vis[first + k];
        FighterSkin skin;
        bool have = i > 0 && fighter_skin_get(fe, i, &skin);
        float x = px + 20 + (k % cols) * (cw + gap), y = py + 56 + (k / cols) * (ch + gap);
        bool lit = first + k == pos;
        if (mouse_pick(f, x, y, cw, ch)) pos = first + k, lit = true;
        if (mouse_click(f, x, y, cw, ch)) choose = i;
        rect(dl, f, x, y, cw, ch, IM_COL32(0, 0, 0, 160), 4);
        skin_picture(dl, f, fe, have ? &skin : nullptr, x + 2, y + 2, cw - 4, ch - 30, lit);
        const char *name = i == 0 ? "DEFAULT" : have ? skin.name : "?";
        float size = fit_size(f, g_title, 12, name, cw - 6);
        text(dl, f, g_title, size, x + cw / 2, y + ch - 27, lit ? WHITE : GREY, name, CENTER, 0.8f);
        char no[16];
        snprintf(no, sizeof(no), i == 0 ? "OWN LOOK" : "SKIN %d", i);
        text(dl, f, g_body, 9, x + cw / 2, y + ch - 13, with_alpha(lit ? ORANGE : GREY, 0.9f), no, CENTER, 0.6f);
        if (lit) selector(dl, f, x, y, cw, ch);
    }
    if (cnt == 1) text(dl, f, g_body, 14, 320, py + 150, GREY, "NO SKIN HAS THAT NAME", CENTER, 0.8f);
    char info[48];
    snprintf(info, sizeof(info), "PAGE %d / %d", page + 1, pages);
    text(dl, f, g_title, 11, px + pw - 18, py + 14, GREY, info, RIGHT, 0.8f);
    snprintf(info, sizeof(info), "%d SKINS", n);
    text(dl, f, g_title, 11, px + 18, py + 14, GREY, info, LEFT, 0.8f);
    float fy = py + ph - 26;
    float hx = px + 20;
    hx += hint(dl, f, hx, fy, "a", "SELECT");
    hx += hint(dl, f, hx, fy, "b", w.search ? "CLOSE SEARCH" : "BACK");
    if (w.search) {
        std::string l = "SEARCH: " + upper(w.q.c_str()) + ((GetTickCount64() / 500) % 2 ? "_" : " ");
        text(dl, f, g_title, 14, hx + 16, fy + 2, ORANGE, l.c_str(), LEFT, 1.0f);
    } else {
        text(dl, f, g_body, 10, hx + 16, fy + 4, GREY, "TAB: SEARCH BY NAME     Q / E: PAGE", LEFT, 0.6f);
    }
    {   // the page buttons for the mouse
        const float bw = 56, bh = 20, bx = px + pw - 20 - 2 * bw - 6, by = fy - 2;
        for (int b = 0; b < 2; b++) {
            float x = bx + b * (bw + 6);
            bool on = b ? page + 1 < pages : page > 0;
            if (on && mouse_pick(f, x, by, bw, bh)) rect(dl, f, x, by, bw, bh, IM_COL32(255, 255, 255, 40), 4);
            else rect(dl, f, x, by, bw, bh, IM_COL32(255, 255, 255, 14), 4);
            text(dl, f, g_title, 11, x + bw / 2, by + 4, on ? WHITE : GREY, b ? "NEXT >" : "< PREV", CENTER, 0.8f);
            if (on && mouse_click(f, x, by, bw, bh)) move(b ? per : -per);
        }
    }
    *sel = vis[std::max(0, std::min(cnt - 1, pos))];
    if (choose >= 0) { *sel = choose; return 1; }
    if (pad.a) return 1;
    if (pad.b) return 2;
    return 0;
}

// The window over whatever page or screen is below: 0 open, 1 a look taken (*sel: 0 Default, 1.. a skin), 2 back.
int skin_window(ImDrawList *dl, const Frame &f, const Pad &pad_in, int fe, int *sel)
{
    Pad pad = pad_in;
    skin_test_keys(pad, false);
    int n = fighter_skins_count(fe);
    if (n <= 0) { g_skin_search_on = false; return 2; }       // the skins went away under it (a new sync): the fighter's own look
    if (*sel < 0 || *sel > n) *sel = 0;
    int r = n + 1 > 5 ? skin_window_grid(dl, f, pad, fe, sel, n) : skin_window_row(dl, f, pad, fe, sel, n);
    if (r) { g_skin_search_on = false; g_skw.search = false; g_skw.q.clear(); }
    return r;
}
