// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// ---- character groups (build 96, char_groups.h): one card per group, and the version window -----------------
// CHOOSE FIGHTER's COMMUNITY tab shows community fighters that are the same character as one card (the picture of the
// version played last with it, else of the newest, and "N VERSIONS"); picking it opens this window, the skin window's
// look (skins_ui.inc): each version's picture, its version name, creator and credits. Left / right browse, A takes the
// version, B goes back. It opens on the version picked last of that group (character-choices.ini).
// Included by online_screen.cpp inside its anonymous namespace, after the drawing helpers, read_pad and skins_ui.inc.

// A group of the grid: its key, the name shown and its members here (feIDs, in catalogue order).
struct CharGroupHere {
    std::string key, name;
    std::vector<const CharGroupFighter *> members;
};

// The members of group `key` this pick can use: a place fighter in the grid's list, or a numbered fighter where the
// grid offers community fighters by number (numbers: community_numbers_here).
CharGroupHere char_group_here(const char *key, const std::vector<OnlineFighter> &list, bool numbers)
{
    CharGroupHere g;
    g.key = key;
    const CharGroupFighter *all = nullptr;
    int n = char_groups_fighters(&all);
    for (int i = 0; i < n; i++) {
        if (strcmp(all[i].key, key)) continue;
        bool ok = false;
        if (all[i].fe_id >= FB_FIRST_LOGICAL) ok = numbers && fighter_banks_find(all[i].fe_id) != nullptr;
        else for (const OnlineFighter &o : list) if (o.id == all[i].fe_id) { ok = true; break; }
        if (!ok) continue;
        if (g.members.empty()) g.name = all[i].character;
        g.members.push_back(&all[i]);
    }
    return g;
}

// The member the group's card shows: the one picked last (character-choices.ini), else the newest.
int char_group_face_of(const CharGroupHere &g)
{
    char pack[96];
    char_groups_remembered(g.key.c_str(), pack, sizeof pack);
    std::vector<int> orders;
    int remembered = -1;
    for (size_t i = 0; i < g.members.size(); i++) {
        orders.push_back(g.members[i]->order);
        if (pack[0] && !strcmp(g.members[i]->id, pack)) remembered = (int)i;
    }
    return char_group_face(orders.data(), (int)orders.size(), remembered);
}

// A version's label: its version name, else its pack's title, else the fighter's name.
const char *char_group_label(const CharGroupFighter *m)
{
    const char *fighter = m->fe_id >= FB_FIRST_LOGICAL && fighter_banks_find(m->fe_id) ? fighter_banks_find(m->fe_id)->name
                                                                                         : online_fighter_name(m->fe_id);
    return char_group_version_label(m->version_name, m->name, fighter);
}

// The "N VERSIONS" plate on a card's top right.
void versions_badge(ImDrawList *dl, const Frame &f, float x, float y, float w, int n, bool lit)
{
    char t[24];
    snprintf(t, sizeof t, "%d VERSIONS", n);
    float tw = text_width(f, g_title, 7, t) + 6;
    rect(dl, f, x + w - tw - 2, y + 2, tw, 11, with_alpha(ORANGE, lit ? 0.95f : 0.75f), 3);
    text(dl, f, g_title, 7, x + w - tw / 2 - 2, y + 3, WHITE, t, CENTER, 0.6f);
}

// RECOMP_TEST_GROUP_KEYS=<left|right|a|b>,... (tests): the open window takes these presses, one every 700 ms.
void group_test_keys(Pad &pad, bool opened_now)
{
    static const char *keys = getenv("RECOMP_TEST_GROUP_KEYS");
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
    fprintf(stderr, "[GROUPS] test press: %.*s\n", (int)n, at);
    at += n + (at[n] == ',');
}

// The window: 0 open, 1 a version taken (*sel: its index in g.members), 2 back. blocked(fe): left out here.
template <class Blocked>
int char_group_window(ImDrawList *dl, const Frame &f, const Pad &pad_in, const CharGroupHere &g, int *sel, Blocked blocked)
{
    Pad pad = pad_in;
    group_test_keys(pad, false);
    int options = (int)g.members.size();
    if (options <= 0) return 2;
    if (*sel < 0 || *sel >= options) *sel = 0;
    if (pad.left) *sel = (*sel + options - 1) % options;
    if (pad.right) *sel = (*sel + 1) % options;
    rect(dl, f, -400, -400, 1440, 1280, IM_COL32(0, 0, 0, 150));
    const int shown = std::min(options, 5);
    const float cw = 100, ch = 214, gap = 12, pw = std::max(330.0f, shown * cw + (shown - 1) * gap + 60), ph = 324;
    const float px = 320 - pw / 2, py = 72;
    const float x0 = 320 - (shown * cw + (shown - 1) * gap) / 2;
    glass(dl, f, px, py, pw, ph, 14, IM_COL32(10, 10, 13, 255), 0.9f);
    text(dl, f, g_title, 22, 320, py + 12, CREAM, "CHOOSE A VERSION", CENTER, 1.6f);
    text(dl, f, g_hand, 15, 320, py + 40, ORANGE, g.name.c_str(), CENTER, 1.0f, pw - 40);
    int first = std::max(0, std::min(*sel - shown / 2, options - shown));
    int choose = -1;
    for (int k = 0; k < shown; k++) {
        int i = first + k;
        const CharGroupFighter *m = g.members[i];
        float x = x0 + k * (cw + gap), y = py + 66;
        bool lit = i == *sel, off = blocked(m->fe_id);
        if (mouse_pick(f, x, y, cw, ch)) *sel = i, lit = true;
        if (mouse_click(f, x, y, cw, ch)) choose = i;
        rect(dl, f, x, y, cw, ch, IM_COL32(0, 0, 0, 160), 4);
        if (!portrait(dl, f, m->fe_id, x + 2, y + 2, cw - 4, ch - 64, lit ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255)))
            text(dl, f, g_title, 14, x + cw / 2, y + 60, with_alpha(CREAM, 0.6f), char_group_label(m), CENTER, 1.0f, cw - 8);
        if (off) {
            rect(dl, f, x + 2, y + (ch - 64) / 2 - 7, cw - 4, 14, with_alpha(RED, 0.85f));
            text(dl, f, g_title, 8, x + cw / 2, y + (ch - 64) / 2 - 5, WHITE, "NOT HERE", CENTER, 0.6f);
        }
        const char *name = char_group_label(m);
        float size = fit_size(f, g_title, 13, name, cw - 8);
        text(dl, f, g_title, size, x + cw / 2, y + ch - 58, lit ? WHITE : GREY, name, CENTER, 0.8f);
        std::string by = m->authors[0] ? std::string("BY ") + upper(m->authors) : std::string("COMMUNITY");
        float bs = fit_size(f, g_body, 9, by.c_str(), cw - 8);
        text(dl, f, g_body, bs, x + cw / 2, y + ch - 40, with_alpha(lit ? ORANGE : GREY, 0.9f), by.c_str(), CENTER, 0.6f);
        if (m->credits[0]) {    // the credits, small, at most two lines
            ImVec2 a = f.p(x + 2, y + ch - 27), b = f.p(x + cw - 2, y + ch - 3);
            dl->PushClipRect(a, b, true);
            text(dl, f, g_body, 7, x + 6, y + ch - 27, with_alpha(CREAM, lit ? 0.75f : 0.45f), m->credits, LEFT, 0.5f, cw - 12);
            dl->PopClipRect();
        }
        if (lit) selector(dl, f, x, y, cw, ch);
    }
    if (first > 0) text(dl, f, g_title, 26, px + 12, py + 160, ORANGE, "<", CENTER, 1.0f);
    if (first + shown < options) text(dl, f, g_title, 26, px + pw - 12, py + 160, ORANGE, ">", CENTER, 1.0f);
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
