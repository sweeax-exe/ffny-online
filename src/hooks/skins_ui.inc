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
void skin_test_keys(Pad &pad, bool opened_now)
{
    static const char *keys = getenv("RECOMP_TEST_SKIN_KEYS");
    static const char *at;
    static ULONGLONG next;
    if (!keys || !*keys) return;
    if (opened_now) { at = keys; next = GetTickCount64() + 1200; return; }
    if (!at || !*at || GetTickCount64() < next) return;
    next = GetTickCount64() + 700;
    size_t n = strcspn(at, ",");
    if (!strncmp(at, "left", n)) pad.left = true;
    else if (!strncmp(at, "right", n)) pad.right = true;
    else if (!strncmp(at, "a", n)) pad.a = true;
    else if (!strncmp(at, "b", n)) pad.b = true;
    fprintf(stderr, "[SKINS] test press: %.*s\n", (int)n, at);
    at += n + (at[n] == ',');
}

// The window over whatever page or screen is below: 0 open, 1 a look taken (*sel: 0 Default, 1.. a skin), 2 back.
int skin_window(ImDrawList *dl, const Frame &f, const Pad &pad_in, int fe, int *sel)
{
    Pad pad = pad_in;
    skin_test_keys(pad, false);
    int n = fighter_skins_count(fe), options = n + 1;
    if (n <= 0) return 2;               // the skins went away under it (a new sync): the fighter's own look
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
