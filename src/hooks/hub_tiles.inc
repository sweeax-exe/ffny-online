// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Build 98: the online hub as tiles ("tile hub", the owner's choice). Included by online_screen.cpp after
// towers_ui.inc; draw_hub keeps the header line, the account checks and the one switch that opens every page,
// and calls hub_tiles_draw in place of the old list and its glass door.
//
//   QUICK MATCH         the big tile on the left: two fighters, the mode (LT/RT or the arrows turn it), the
//                       fighters choice (LB or a click), who fights right now
//   beside it           RANKED MATCH (who searches), RUN THE STREETS (only with the closed test's access, the same
//                       rts_access() check as before), FIGHT COMPLEX (the crowned king), TOWERS (your best floor),
//                       TOURNAMENT MATCH (while a relay offers sets), FIND A ROOM (open rooms), CREATE A ROOM
//   the bar             MY STATS, TURF WAR, MY CREW, PLAYERS ONLINE, VOICE CHAT, MY MUSIC (RB too), LEAVE (B too)
//
// The stick or the D-pad moves in 2D (hub_grid.c, tests/unit/test_hub_grid_native.py), A opens, B leaves, the
// mouse picks and clicks. The art is the game's own the hub already loads (the venue pictures, the fighters'
// portraits, the rank emblems, scripts/tower-art.py's buildings); without it the tiles are plain glass.
// RECOMP_HUB_LIST=1 brings the list of build 97 back (a fallback while the tiles are new).

void hub_faction_tile(ImDrawList *dl, const Frame &f, const HubRect &r, bool sel);     // faction_ui.inc (build 108)
std::string hub_faction_live();

bool hub_tiles_on()
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_HUB_LIST");
        on = !(e && *e == '1');
    }
    return on != 0;
}

const float HUB_TILE_X = 36, HUB_TILE_Y = 74, HUB_TILE_W = 568, HUB_TILE_H = 332, HUB_TILE_BIG_W = 230,
            HUB_TILE_BAR_H = 26, HUB_TILE_GAP = 8;

struct HubTilesState {
    unsigned long long best_at = 0;     // TOWERS: the best floors, read again every 3 s (a file)
    int best = 0;
    int logged_sel = -1;                // the [HUB] line of the chosen tile (tests read it)
} HT;

static bool hub_has(const int *shown, int count, int id)
{
    for (int k = 0; k < count; k++) if (shown[k] == id) return true;
    return false;
}

// A picture filling a box (cropped to the box's shape), from the part uv0..uv1 of the texture.
static void hub_cover(ImDrawList *dl, const Frame &f, const Tex &t, float x, float y, float w, float h, ImU32 tint,
                      ImVec2 a = ImVec2(0, 0), ImVec2 b = ImVec2(1, 1))
{
    if (!t.srv || t.w <= 0 || t.h <= 0 || w <= 0 || h <= 0) return;
    float sw = (b.x - a.x) * t.w, sh = (b.y - a.y) * t.h, box = (w * f.sx) / (h * f.sy);
    if (sw / sh > box) {
        float nw = sh * box / t.w, cx = (a.x + b.x) / 2;
        a.x = cx - nw / 2; b.x = cx + nw / 2;
    } else {
        float nh = sw / box / t.h, cy = (a.y + b.y) / 2;
        a.y = cy - nh / 2; b.y = cy + nh / 2;
    }
    image(dl, f, t, x, y, w, h, tint, a, b);
}

// The photo of a venue's poster (the game's venue pictures carry the name under it).
static void hub_venue_art(ImDrawList *dl, const Frame &f, int venue, float x, float y, float w, float h, bool sel)
{
    int k = sel ? 225 : 150;
    hub_cover(dl, f, venue_tex(venue), x, y, w, h, IM_COL32(k, k, k, 255), ImVec2(54 / 256.0f, 32 / 256.0f),
              ImVec2(198 / 256.0f, 138 / 256.0f));
}

// A live line on a tile: a pulsing dot and the words, on a dark pill at x, y (its width returned).
static float hub_live(ImDrawList *dl, const Frame &f, float x, float y, const std::string &words, bool on, bool right = false)
{
    if (words.empty()) return 0;
    float w = text_width(f, g_title, 10, words.c_str()) + 22;
    if (right) x -= w;
    rect(dl, f, x, y, w, 15, IM_COL32(0, 0, 0, 190), 7);
    float t = (float)(GetTickCount64() % 1400) / 1400.0f, pulse = 0.55f + 0.45f * (t < 0.5f ? t * 2 : 2 - t * 2);
    dl->AddCircleFilled(f.p(x + 9, y + 7.5f), 3.2f * f.s(), with_alpha(on ? GREEN : GREY, on ? pulse : 0.7f));
    text(dl, f, g_title, 10, x + 16, y + 2, on ? CREAM : GREY, words.c_str(), LEFT, 0.6f);
    return w;
}

// What QUICK MATCH's chosen mode is, over its logo (the words the old door had).
static std::string hub_quick_line(int type, int index)
{
    if (int v = fixed_venue(type)) return "ONE ON ONE AT " + upper(online_venue_name(v));
    if (is_team(type)) return "TWO ON TWO, FOUR PLAYERS";
    if (NETPLAY_IS_KOTH(type)) return "KING OF THE HILL: UP TO 8 PLAYERS";
    if (NETPLAY_IS_FFAROT(type)) return "UP TO 8 PLAYERS, 4 FIGHT, WINNER STAYS";
    if (index > 0 && mode_name(type)) return upper(mode_name(type));
    return "";
}

// The live numbers the hub already has: who fights on this relay, the open rooms (every relay with All servers),
// the ring board's crowned king, the best Towers floor.
static void hub_counts(int *fighting, int *in_rooms, int *rooms)
{
    static NetPlayer list[128];
    int online = -1, r = -1;
    int n = lobby_players(list, 128, &online, &r);
    *fighting = *in_rooms = 0;
    for (int i = 0; i < n; i++) {
        if (list[i].state == 2) ++*fighting;
        else if (list[i].state == 1) ++*in_rooms;
    }
    NetServerInfo s[NETSERVER_COUNT];
    servers(s);
    int cur = connected_server();
    if (r < 0 && cur >= 0 && s[cur].state == NETSERVER_UP) r = s[cur].rooms;
    if (r >= 0 && cur >= 0 && all_servers())
        for (int i = 0; i < NETSERVER_COUNT; i++)
            if (i != cur && s[i].state == NETSERVER_UP && s[i].rooms > 0) r += s[i].rooms;
    *rooms = r;
}

static std::string hub_king_line()
{
    RingBoard b = ring_board_now(0);
    if (!b.valid) return "";
    const RingTypeBoard *best = nullptr;
    for (const RingTypeBoard &t : b.types)
        if (t.crowned && !t.king.name.empty() && (!best || t.king.streak > best->king.streak)) best = &t;
    if (!best) return "NO KING YET";
    return "KING " + upper(best->king.name.c_str()) + "  x" + std::to_string(best->king.streak);
}

static int hub_towers_best()
{
    unsigned long long now = GetTickCount64();
    if (!HT.best_at || now - HT.best_at > 3000) {
        HT.best_at = now;
        HT.best = 0;
        for (int t = 1; t <= TOWER_COUNT; t++) HT.best = std::max(HT.best, towers_best(t));
    }
    return HT.best;
}

// The bar's small icons, drawn (no picture needed): centred at cx, cy, about 12 units wide.
static void hub_icon(ImDrawList *dl, const Frame &f, int id, float cx, float cy, ImU32 col)
{
    float s = f.s(), t = std::max(1.0f, 1.4f * s);
    auto P = [&](float dx, float dy) { return f.p(cx + dx, cy + dy); };
    if (id == 4) {              // MY STATS: three bars
        rect(dl, f, cx - 6, cy + 1, 3, 5, col);
        rect(dl, f, cx - 1.5f, cy - 3, 3, 9, col);
        rect(dl, f, cx + 3, cy - 6, 3, 12, col);
    } else if (id == HUB_TURF) {    // TURF WAR: a flag on a pole
        dl->AddLine(P(-5, 6), P(-5, -6), col, t);
        dl->AddTriangleFilled(P(-4.5f, -6), P(6, -3), P(-4.5f, 0), col);
    } else if (id == HUB_CREW || id == 5) {     // MY CREW: three heads; PLAYERS ONLINE: one and a dot
        if (id == HUB_CREW) {
            dl->AddCircleFilled(P(-4.5f, -2), 2.2f * s, col);
            dl->AddCircleFilled(P(4.5f, -2), 2.2f * s, col);
            dl->AddRectFilled(P(-7.5f, 2), P(-1.5f, 6), col, 2 * s);
            dl->AddRectFilled(P(1.5f, 2), P(7.5f, 6), col, 2 * s);
        }
        dl->AddCircleFilled(P(0, -3), 2.8f * s, col);
        dl->AddRectFilled(P(-3.8f, 1), P(3.8f, 6.5f), col, 2.5f * s);
        if (id == 5) dl->AddCircleFilled(P(6, -5), 2.0f * s, GREEN);
    } else if (id == HUB_VOICE) {   // VOICE CHAT: a microphone
        dl->AddRectFilled(P(-2.5f, -7), P(2.5f, 1.5f), col, 2.5f * s);
        dl->AddBezierQuadratic(P(-5, -1), P(0, 7), P(5, -1), col, t);
        dl->AddLine(P(0, 3), P(0, 6.5f), col, t);
        dl->AddLine(P(-3, 6.5f), P(3, 6.5f), col, t);
    } else if (id == HUB_MUSIC) {   // MY MUSIC: a note
        dl->AddCircleFilled(P(-3, 4), 2.6f * s, col);
        dl->AddCircleFilled(P(4, 2.5f), 2.6f * s, col);
        dl->AddLine(P(-0.6f, 4), P(-0.6f, -6), col, t);
        dl->AddLine(P(6.4f, 2.5f), P(6.4f, -7.5f), col, t);
        dl->AddLine(P(-0.6f, -6), P(6.4f, -7.5f), col, t * 1.6f);
    } else if (id == HUB_LEAVE) {   // LEAVE: a door and an arrow out
        dl->AddRect(P(-6, -6), P(1, 6), col, 0, 0, t);
        dl->AddLine(P(-1, 0), P(7, 0), col, t);
        dl->AddTriangleFilled(P(7.5f, 0), P(4, -3), P(4, 3), col);
    }
}

static const char *hub_chip_label(int id, const char *const *items)
{
    return id == HUB_MUSIC ? "MY MUSIC" : items[id];
}

static const char *hub_desc(int id, const char *const *descs)
{
    return id == HUB_MUSIC ? "Your songs in the menus and the fights: which play and in what order. RB opens it from anywhere on the hub."
                           : descs[id];
}

// QUICK MATCH's tile: the art, two fighters, the mode with its arrows and the fighters choice. Returns true when a
// click landed on one of its own controls (it is then not a QUICK MATCH press).
static bool hub_quick_tile(ImDrawList *dl, const Frame &f, const HubRect &r, bool sel, const std::vector<int> &modes,
                           const char *sub, int fighting, int in_rooms)
{
    float x = r.x, y = r.y, w = r.w, h = r.h;
    bool used = false;
    // the fighters: two of the roster's portraits, face to face, fading into the tile at their foot
    {
        const std::vector<OnlineFighter> &ros = roster();
        int a = ros.size() > 0 ? ros[0].id : 55, b = ros.size() > 1 ? ros[ros.size() / 2].id : 22;
        for (const OnlineFighter &o : ros) { if (o.id == 55) a = 55; if (o.id == 22) b = 22; }
        float pw = (w - 36) / 2, ph = 128, py = y + 64;
        int k = sel ? 255 : 170;
        portrait(dl, f, a, x + 14, py, pw, ph, IM_COL32(k, k, k, 255));
        portrait(dl, f, b, x + w - 14 - pw, py, pw, ph, IM_COL32(k, k, k, 255));
        dl->AddRectFilledMultiColor(f.p(x + 4, py + ph * 0.55f), f.p(x + w - 4, py + ph + 2), IM_COL32(8, 8, 10, 0),
                                    IM_COL32(8, 8, 10, 0), IM_COL32(8, 8, 10, 235), IM_COL32(8, 8, 10, 235));
        text(dl, f, g_stencil, 26, x + w / 2, py + ph * 0.42f, with_alpha(RED, sel ? 1.0f : 0.7f), "VS", CENTER, 1.4f);
    }
    float ts = fit_size(f, g_title, 28, "QUICK MATCH", w - 28);
    text(dl, f, g_title, ts, x + 14, y + 12, sel ? CREAM : with_alpha(CREAM, 0.85f), "QUICK MATCH", LEFT, 1.6f);
    float ss = std::max(8.0f, fit_size(f, g_hand, 12, sub, w - 130));
    text(dl, f, g_hand, ss, x + 16, y + 45, sel ? ORANGE : with_alpha(ORANGE, 0.6f), sub, LEFT, 1.0f);
    std::string live = fighting > 0 ? std::to_string(fighting) + " FIGHTING NOW"
                     : in_rooms > 0 ? std::to_string(in_rooms) + " IN ROOMS" : "";
    hub_live(dl, f, x + w - 10, y + 44, live, fighting > 0 || in_rooms > 0, true);
    int count = (int)modes.size();
    if (!count) return false;
    int type = modes[S.quick_mode];
    float cx = x + w / 2, my = y + 214;
    std::string line = hub_quick_line(type, S.quick_mode);
    if (!line.empty()) text(dl, f, g_hand, 11, cx, my - 18, ORANGE, line.c_str(), CENTER, 0.8f, w - 20);
    const Tex &logo = tex(mode_logo(type));
    if (!logo.srv) text(dl, f, g_title, 16, cx, my + 4, CREAM, mode_name(type), CENTER);
    else image_fit(dl, f, logo, cx - 70, my, 140, 26);
    // the arrows (and LT / RT, L2 / R2 with PlayStation's), fading at the ends: the cycle does not wrap
    bool ps = ps_icons();
    bool can_l = S.quick_mode > 0, can_r = S.quick_mode < count - 1;
    text(dl, f, g_title, 22, x + 16, my - 1, can_l ? ORANGE : with_alpha(GREY, 0.35f), "<", CENTER, 1.2f);
    text(dl, f, g_title, 22, x + w - 16, my - 1, can_r ? ORANGE : with_alpha(GREY, 0.35f), ">", CENTER, 1.2f);
    text(dl, f, g_title, 9, x + 16, my + 25, with_alpha(CREAM, sel ? 0.8f : 0.4f), ps ? "L2" : "LT", CENTER, 0.6f);
    text(dl, f, g_title, 9, x + w - 16, my + 25, with_alpha(CREAM, sel ? 0.8f : 0.4f), ps ? "R2" : "RT", CENTER, 0.6f);
    if (mouse_click(f, x + 4, my - 4, 26, 36)) { if (can_l) S.quick_mode--; used = true; }
    if (mouse_click(f, x + w - 30, my - 4, 26, 36)) { if (can_r) S.quick_mode++; used = true; }
    // the fighters this search meets (LB or a click switches)
    bool orig = search_original(type);
    const char *words = original_words(orig);
    float tw = text_width(f, g_title, 11, words), bx = cx - tw / 2 - 10, by = y + h - 30;
    rect(dl, f, bx, by, tw + 20, 18, orig ? IM_COL32(40, 70, 120, 220) : IM_COL32(120, 12, 26, 200), 5);
    text(dl, f, g_title, 11, cx, by + 2, WHITE, words, CENTER, 0.8f);
    if (mouse_click(f, bx, by, tw + 20, 18)) { search_original_toggle(type); used = true; }
    return used;
}

// One of the other tiles: its art, title, subtitle and live line.
static void hub_tile(ImDrawList *dl, const Frame &f, const HubRect &r, int id, bool sel, const char *title, const char *sub,
                     const std::string &live, bool live_on)
{
    float x = r.x, y = r.y, w = r.w, h = r.h;
    static const struct { int id, venue; } art[] = { { HUB_RANKED, 5 }, { HUB_RTS, 14 }, { HUB_COMPLEX, 22 }, { HUB_TSET, 6 },
                                                      { 2, 16 }, { 3, 17 } };
    for (auto &a : art) if (a.id == id) hub_venue_art(dl, f, a.venue, x + 2, y + 2, w - 4, h - 4, sel);
    if (id == HUB_TOWERS) {     // a night sky and the three buildings
        dl->AddRectFilledMultiColor(f.p(x + 2, y + 2), f.p(x + w - 2, y + h - 2), IM_COL32(22, 26, 52, 255),
                                    IM_COL32(22, 26, 52, 255), IM_COL32(60, 28, 22, 255), IM_COL32(60, 28, 22, 255));
        int k = sel ? 255 : 170;
        float th = h - 8, tw = th / 2;
        for (int t = 0; t < TOWER_ART_COUNT; t++) {
            const Tex &tt = tower_tex(t + 1);
            float tx = x + w - 10 - (TOWER_ART_COUNT - t) * (tw * 0.78f) - tw * 0.2f;
            if (tt.srv) image_fit(dl, f, tt, tx, y + h - th - 2, tw, th, IM_COL32(k, k, k, 255));
        }
    }
    if (id == HUB_RANKED) {     // the top rank's emblem
        const Tex &em = tex("ranks/em8-256.png");
        if (em.srv) image_fit(dl, f, em, x + w - h + 6, y + 6, h - 12, h - 12, IM_COL32(255, 255, 255, sel ? 255 : 170));
    }
    if (id == HUB_COMPLEX) crown(dl, f, x + w - 30, y + 22, 34, with_alpha(GOLD, sel ? 1.0f : 0.6f));
    if (id == HUB_FACTION) hub_faction_tile(dl, f, r, sel);     // build 108: both bosses, face to face
    // the words read on any picture: a dark fade under them
    dl->AddRectFilledMultiColor(f.p(x + 2, y + h * 0.30f), f.p(x + w - 2, y + h - 2), IM_COL32(8, 8, 10, 0),
                                IM_COL32(8, 8, 10, 0), IM_COL32(8, 8, 10, 230), IM_COL32(8, 8, 10, 230));
    float size = fit_size(f, g_title, 20, title, w - 24);
    text(dl, f, g_title, size, x + 12, y + h - 42, sel ? CREAM : with_alpha(CREAM, 0.82f), title, LEFT, 1.4f);
    float ss = std::max(8.0f, fit_size(f, g_hand, 10, sub, w - 24));
    text(dl, f, g_hand, ss, x + 13, y + h - 18, sel ? ORANGE : with_alpha(ORANGE, 0.6f), sub, LEFT, 0.9f);
    if (id == HUB_RTS) {
        float tw = text_width(f, g_title, 9, "CLOSED TEST") + 12;
        rect(dl, f, x + w - tw - 8, y + 8, tw, 14, IM_COL32(150, 14, 30, 230), 4);
        text(dl, f, g_title, 9, x + w - 14, y + 9.5f, WHITE, "CLOSED TEST", RIGHT, 0.6f);
    }
    hub_live(dl, f, x + 8, y + 8, live, live_on);
}

// A chip of the bar: an icon and its word.
static void hub_chip(ImDrawList *dl, const Frame &f, const HubRect &r, int id, bool sel, const char *label)
{
    bool leave = id == HUB_LEAVE;
    ImU32 col = sel ? CREAM : with_alpha(GREY, 0.9f);
    float size = std::max(8.0f, fit_size(f, g_title, 11, label, r.w - 30));
    float tw = text_width(f, g_title, size, label), cw = 14 + 5 + tw, x0 = r.x + (r.w - cw) / 2;
    hub_icon(dl, f, id, x0 + 7, r.y + r.h / 2, leave && sel ? IM_COL32(255, 96, 104, 255) : col);
    text(dl, f, g_title, size, x0 + 19, r.y + (r.h - size * 1.25f) / 2, col, label, LEFT, 0.8f);
}

void hub_tiles_draw(ImDrawList *dl, const Frame &f, const Pad &pad, const int *shown, int count,
                    const std::vector<int> &quick_modes, bool ranked_elsewhere, int ranked_at, const char *const *items,
                    const char *const *subs, const char *const *descs)
{
    int ids[HUB_GRID_MAX], n = 0, nright = 0, nchips = 0;
    ids[n++] = 0;
    static const int right_order[] = { HUB_RANKED, HUB_FACTION, HUB_RTS, HUB_COMPLEX, HUB_TOWERS, HUB_TSET, 2, 3 };
    static const int chip_order[] = { 4, HUB_TURF, HUB_CREW, 5, HUB_VOICE, HUB_MUSIC, HUB_LEAVE };
    for (int id : right_order) if (hub_has(shown, count, id)) { ids[n++] = id; nright++; }
    for (int id : chip_order) if (id == HUB_MUSIC || hub_has(shown, count, id)) { ids[n++] = id; nchips++; }
    HubRect r[HUB_GRID_MAX];
    HubRect area = { HUB_TILE_X, HUB_TILE_Y, HUB_TILE_W, HUB_TILE_H };
    n = hub_grid_layout(nright, nchips, area, HUB_TILE_BIG_W, HUB_TILE_BAR_H, HUB_TILE_GAP, r);
    if (n <= 0) return;
    int cur = 0;
    for (int i = 0; i < n; i++) if (ids[i] == S.hub_sel) cur = i;
    int dx = (pad.right ? 1 : 0) - (pad.left ? 1 : 0), dy = (pad.down ? 1 : 0) - (pad.up ? 1 : 0);
    if (dx) dy = 0;
    if (dx || dy) cur = hub_grid_move(r, n, cur, dx, dy);
    for (int i = 0; i < n; i++) if (mouse_pick(f, r[i].x, r[i].y, r[i].w, r[i].h)) cur = i;
    S.hub_sel = ids[cur];
    if (HT.logged_sel != S.hub_sel) {
        HT.logged_sel = S.hub_sel;
        fprintf(stderr, "[HUB] tile %s\n", hub_chip_label(S.hub_sel, items));
    }
    // QUICK MATCH's mode: LT / RT (left and right move between the tiles); the cycle does not wrap (build 66)
    int mode_count = (int)quick_modes.size();
    if (S.hub_sel == 0 && mode_count && (pad.lt || pad.rt))
        S.quick_mode = std::max(0, std::min(mode_count - 1, S.quick_mode + (pad.rt ? 1 : -1)));

    int fighting = 0, in_rooms = 0, rooms = -1;
    hub_counts(&fighting, &in_rooms, &rooms);
    // the bar's glass: one strip under the chips
    {
        int first = 1 + nright;
        if (nchips) glass(dl, f, r[first].x - 4, r[first].y - 3, HUB_TILE_W + 8, HUB_TILE_BAR_H + 6, 9);
    }
    for (int i = 0; i < n; i++) {
        int id = ids[i];
        bool sel = i == cur, chip = i > nright;
        const HubRect &b = r[i];
        bool own_click = false;
        if (!chip) {
            glass(dl, f, b.x, b.y, b.w, b.h, 8, sel ? IM_COL32(44, 32, 22, 255) : IM_COL32(10, 10, 13, 255), sel ? 0.80f : 0.62f);
            dl->PushClipRect(f.p(b.x, b.y), f.p(b.x + b.w, b.y + b.h), true);
            if (id == 0) {
                hub_venue_art(dl, f, 21, b.x + 2, b.y + 2, b.w - 4, b.h - 4, sel);
                dl->AddRectFilled(f.p(b.x, b.y), f.p(b.x + b.w, b.y + b.h), IM_COL32(8, 8, 10, 120));
                own_click = hub_quick_tile(dl, f, b, sel, quick_modes, subs[0], fighting, in_rooms);
            } else {
                std::string live;
                bool on = false;
                const char *sub = subs[id];
                std::string elsewhere;
                if (id == HUB_RANKED) {
                    searching_poll();
                    int k = searching_count(-1);
                    if (k >= 0) { live = std::to_string(k) + " SEARCHING"; on = k > 0; }
                    if (ranked_elsewhere) {
                        NetServerInfo s[NETSERVER_COUNT];
                        servers(s);
                        elsewhere = "ON THE " + upper(s[ranked_at].name) + " SERVER";
                        sub = elsewhere.c_str();
                    }
                } else if (id == HUB_COMPLEX) {
                    live = hub_king_line();
                    on = !live.empty() && live != "NO KING YET";
                } else if (id == HUB_FACTION) {
                    live = hub_faction_live();
                    on = !live.empty();
                } else if (id == HUB_TOWERS) {
                    int best = hub_towers_best();
                    if (best > 0) { live = "YOUR BEST: FLOOR " + std::to_string(best); on = true; }
                } else if (id == 2 && rooms >= 0) {
                    live = std::to_string(rooms) + (rooms == 1 ? " OPEN ROOM" : " OPEN ROOMS");
                    on = rooms > 0;
                }
                hub_tile(dl, f, b, id, sel, items[id], sub, live, on);
            }
            dl->PopClipRect();
            if (!sel) frame_rect(dl, f, b.x + 0.5f, b.y + 0.5f, b.w - 1, b.h - 1, IM_COL32(255, 255, 255, 26), 1.0f);
        } else {
            if (sel) rect(dl, f, b.x, b.y, b.w, b.h, id == HUB_LEAVE ? IM_COL32(110, 14, 26, 200) : IM_COL32(60, 40, 24, 220), 6);
            hub_chip(dl, f, b, id, sel, hub_chip_label(id, items));
        }
        if (sel) selector(dl, f, b.x, b.y, b.w, b.h);
        if (!own_click && mouse_click(f, b.x, b.y, b.w, b.h)) const_cast<Pad &>(pad).a = true;
    }
    // the chosen entry in a line under the bar (the old door's words; its first sentence where all do not fit)
    {
        std::string d = upper(hub_desc(S.hub_sel, descs));
        if (S.hub_sel == HUB_RANKED && ranked_elsewhere)
            d = upper(pad_text("Ranked is not offered on this server right now. Press {a} to switch over, then choose Ranked Match again.").c_str());
        float w = 552;
        if (text_width(f, g_body, 10, d.c_str()) > w) {
            size_t dot = d.find(". ");
            if (dot != std::string::npos) d = d.substr(0, dot + 1);
        }
        float size = std::max(8.0f, fit_size(f, g_body, 10, d.c_str(), w));
        text(dl, f, g_body, size, 44, HUB_TILE_Y + HUB_TILE_H + 4, with_alpha(CREAM, 0.85f), d.c_str(), LEFT, 0.8f);
    }
}
