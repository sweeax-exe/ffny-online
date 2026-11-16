// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Run the Streets' social layer on the screen (build 86 closed test; rts_social.h has the game's side, the rules are
// rts_social_rules.c). Included by online_screen.cpp inside its anonymous namespace, after social_ui.inc (the chat
// window's state C, the fight chat FC, room_muted, the text helpers and the fonts).
//
//   Chat bubbles  a room line said in the fight shows as a speech bubble over the sayer's fighter for 5 s (two at
//                 most per fighter, the newest by the head); a fighter off the screen gets it at the screen's edge with
//                 an arrow toward them. Lines of muted and blocked players never show (room_muted, as the chat).
//   Speaker icon  over a fighter whose player talks (voice.c heard them in the last 300 ms; this PC: while it sends).
//   Docked chat   the chat window, compact, bottom left: ROOM / GLOBAL / PRIVATE (the newest conversation). ENTER or
//                 the pad's BACK (R3 when BACK is push-to-talk) opens it; while it is open the game gets no input on
//                 this PC (pc_ui.cpp's fight chat hold: the fighter stands, the fight goes on with a neutral pad).
//                 TAB / LB RB switch the tab, ENTER sends (empty: closes), ESC / B closes; the pad types on the
//                 on-screen keyboard (A). The same protocol, filters and mutes as the hub's chat; offline nothing.
//
// Drawing and reading only: nothing here is in a fight's input or checksums (the lines are T_CHAT and the service's).
// Tests: RECOMP_RTS_TEST_BUBBLES="S:FIGHTER:TEXT;..." shows a line over a fighter S seconds into the fight (offline
// too), RECOMP_RTS_TEST_TALKING=1,2 shows the speaker over those fighters, RECOMP_RTS_TEST_SAY="S:TEXT;..." sends
// room lines S seconds in (online), RECOMP_RTS_TEST_DOCK="S:TAB:TEXT" opens the dock S seconds in on tab 0..2,
// types TEXT and sends it (then closes 4 s later).

const ImU32 SPEAK_GREEN = IM_COL32(120, 224, 120, 255);
const float RTS_HEAD = 26.0f;       // world units above a fighter's feet: over the head

struct RtsBubble {
    int fighter = -1;
    std::string name, text;
    unsigned long long at = 0;
    bool mine = false;
    unsigned seq = 0;
};

struct RtsTestLine { float s = 0; int fighter = 0, tab = 0; std::string text; bool done = false; };

struct RtsSocial {
    unsigned long long since = 0;           // the first frame of this fight the overlay saw
    bool was_on = false;
    std::vector<RtsTestLine> test_bubbles, test_say, test_dock;
    uint32_t test_talking = 0;
    bool parsed = false;
    std::vector<std::pair<std::string, unsigned long long>> logged_test;
    // the dock
    bool live_before = false;
    int tab = 0;                            // 0 ROOM, 1 GLOBAL, 2 PRIVATE
    unsigned seen_room = 0;
    long long seen_global = 0;
    long long dm_with = 0, dm_opened = 0;
    int kb_frame = -1;                      // the on-screen keyboard was up in this frame (its B is not the dock's)
    unsigned long long dock_since = 0, test_close_at = 0;
    int test_step = 0;
} RS;

std::vector<RtsTestLine> rts_parse_test(const char *name, bool with_fighter)
{
    std::vector<RtsTestLine> out;
    const char *e = getenv(name);
    if (!e || !*e) return out;
    std::string all(e);
    size_t p = 0;
    while (p < all.size()) {
        size_t q = all.find(';', p);
        std::string part = all.substr(p, q == std::string::npos ? std::string::npos : q - p);
        p = q == std::string::npos ? all.size() : q + 1;
        size_t c1 = part.find(':');
        if (c1 == std::string::npos) continue;
        RtsTestLine t;
        t.s = (float)atof(part.c_str());
        std::string rest = part.substr(c1 + 1);
        if (with_fighter) {
            size_t c2 = rest.find(':');
            if (c2 == std::string::npos) continue;
            t.fighter = t.tab = atoi(rest.c_str());
            rest = rest.substr(c2 + 1);
        }
        t.text = rest;
        out.push_back(t);
    }
    return out;
}

void rts_parse_tests()
{
    if (RS.parsed) return;
    RS.parsed = true;
    RS.test_bubbles = rts_parse_test("RECOMP_RTS_TEST_BUBBLES", true);
    RS.test_say = rts_parse_test("RECOMP_RTS_TEST_SAY", false);
    RS.test_dock = rts_parse_test("RECOMP_RTS_TEST_DOCK", true);
    if (const char *t = getenv("RECOMP_RTS_TEST_TALKING"))
        for (const char *p = t; *p; p++) if (*p >= '0' && *p <= '3') RS.test_talking |= 1u << (*p - '0');
}

float rts_fight_seconds() { return RS.since ? (GetTickCount64() - RS.since) / 1000.0f : 0.0f; }

// The 3D picture in the window: the shape the game draws (4:3, or the widescreen canvas), centred, or the whole
// window when a 4:3 picture is stretched (as fight_tag_place and menu_frame have it).
void rts_picture(ImVec2 d, float &px, float &py, float &pw, float &ph)
{
    float wide = ws_aspect(), a = wide > 0.0f ? wide : 4.0f / 3.0f;
    px = py = 0;
    pw = d.x;
    ph = d.y;
    if (wide > 0.0f || pc_display("aspect", 0) != 1) {
        if (pw > ph * a) { float w = ph * a; px = (pw - w) / 2; pw = w; }
        else { float h = pw / a; py = (ph - h) / 2; ph = h; }
    }
}

// The lines to show over the fighters now: the room's (online), or the test's.
std::vector<RtsBubble> rts_bubbles(const RtsSocialView &v)
{
    std::vector<RtsBubble> out;
    unsigned long long now = GetTickCount64();
    float t = rts_fight_seconds();
    for (auto &b : RS.test_bubbles) {
        if (t < b.s || t >= b.s + RTS_BUBBLE_MS / 1000.0f || b.fighter < 0 || b.fighter > 3) continue;
        RtsBubble x;
        x.fighter = b.fighter;
        x.name = b.fighter == v.local ? "You" : "Player " + std::to_string(b.fighter + 1);
        x.text = b.text;
        x.at = RS.since + (unsigned long long)(b.s * 1000.0f);
        x.mine = b.fighter == v.local;
        x.seq = 1000000u + (unsigned)(b.s * 10);
        out.push_back(x);
    }
    if (net_state() == NETPLAY_CONNECTED && net_in_lobby()) {
        static NetChatLine buf[NETPLAY_CHAT_LINES];
        int n = netplay_room_chat(buf, NETPLAY_CHAT_LINES, nullptr);
        for (int i = 0; i < n; i++) {
            const NetChatLine &l = buf[i];
            if (l.system || l.history || now - l.at >= RTS_BUBBLE_MS || l.at < RS.since || room_muted(l)) continue;
            int f = l.spectator ? -1 : netplay_fighter_of_room_slot(l.slot);
            if (f < 0 && l.mine) f = v.local;
            if (f < 0 || f > 3 || !(v.present & (1u << f))) continue;     // a spectator's line: the dock has it
            RtsBubble x;
            x.fighter = f;
            x.name = l.mine ? "You" : l.name;
            x.text = l.text;
            x.at = l.at;
            x.mine = l.mine != 0;
            x.seq = l.seq;
            out.push_back(x);
        }
    }
    return out;
}

// The fighters whose players talk now (bit per fighter slot).
uint32_t rts_talking(const RtsSocialView &v)
{
    uint32_t m = RS.test_talking & v.present;
    for (int slot = 0; slot < NETPLAY_ROOM_SLOTS; slot++) {
        int f = netplay_fighter_of_room_slot(slot);
        if (f >= 0 && f < 4 && voice_talking(slot)) m |= 1u << f;
    }
    if (v.local >= 0 && voice_sending()) m |= 1u << v.local;
    return m & v.present;
}

// A small speaker with sound waves, its middle at (x, y) in the menu's units; the waves pulse while it talks.
void rts_speaker(ImDrawList *dl, const Frame &f, float x, float y, float size, float alpha)
{
    float s = size * f.s();
    ImVec2 c = f.p(x, y);
    ImU32 col = with_alpha(SPEAK_GREEN, alpha), dark = with_alpha(IM_COL32(0, 0, 0, 200), alpha);
    float pulse = 0.5f + 0.5f * sinf((float)(GetTickCount64() % 100000) / 1000.0f * 9.0f);
    dl->AddCircleFilled(ImVec2(c.x + s * 0.15f, c.y), s * 1.05f, with_alpha(IM_COL32(0, 0, 0, 150), alpha), 20);
    ImVec2 body[4] = { ImVec2(c.x - s * 0.55f, c.y - s * 0.22f), ImVec2(c.x - s * 0.25f, c.y - s * 0.22f),
                       ImVec2(c.x - s * 0.25f, c.y + s * 0.22f), ImVec2(c.x - s * 0.55f, c.y + s * 0.22f) };
    dl->AddConvexPolyFilled(body, 4, col);
    ImVec2 cone[4] = { ImVec2(c.x - s * 0.25f, c.y - s * 0.22f), ImVec2(c.x + s * 0.12f, c.y - s * 0.55f),
                       ImVec2(c.x + s * 0.12f, c.y + s * 0.55f), ImVec2(c.x - s * 0.25f, c.y + s * 0.22f) };
    dl->AddConvexPolyFilled(cone, 4, col);
    for (int w = 0; w < 2; w++) {
        float r = s * (0.42f + 0.3f * w), a = alpha * (w == 0 ? 1.0f : 0.45f + 0.55f * pulse);
        dl->PathArcTo(ImVec2(c.x + s * 0.12f, c.y), r, -0.9f, 0.9f, 10);
        dl->PathStroke(with_alpha(SPEAK_GREEN, a), 0, std::max(1.2f, s * 0.13f));
    }
    (void)dark;
}

// One bubble: its tail's tip at (ax, ay) (menu units), or at the edge with an arrow toward `dir` when off the
// screen. Returns its top. Short lines (an emote, "GG") are drawn big.
float rts_bubble(ImDrawList *dl, const Frame &f, float ax, float ay, const RtsBubble &b, float alpha, bool tailed, bool edge,
                 float dir_x, float dir_y, float lo_x, float hi_x, float lo_y, bool newest, bool talking)
{
    bool short_line = b.text.size() <= 14 && b.text.find(' ') == std::string::npos ? true : b.text.size() <= 9;
    float size = short_line ? 15.0f : 10.5f, wrap = 150.0f;
    if (!newest) size *= 0.85f;
    ImFont *font = short_line ? g_title : g_body;
    float tw = std::min(wrap, text_width(f, font, size, b.text.c_str()));
    float th = text_height(f, font, size, b.text.c_str(), wrap);
    float nw = text_width(f, g_hand, 9.5f, b.name.c_str());
    float w = std::max(tw, nw + (talking ? 12.0f : 0.0f)) + 14, h = th + 19;
    float tail = tailed ? 6.0f : 0.0f;
    float x = ax - w / 2, y = ay - tail - h;
    x = std::max(lo_x, std::min(x, hi_x - w));
    y = std::max(lo_y, y);
    // a pop as it comes in
    float pop = alpha < 1.0f && GetTickCount64() - b.at < 200 ? 0.9f + 0.1f * alpha : 1.0f;
    float cx = x + w / 2, cy = y + h / 2;
    float bx = cx - w * pop / 2, by = cy - h * pop / 2, bw = w * pop, bh = h * pop;
    ImU32 fill = b.mine ? IM_COL32(70, 12, 22, 255) : IM_COL32(10, 10, 13, 255);
    rect(dl, f, bx + 1.5f, by + 2.0f, bw, bh, with_alpha(BLACK, 0.35f * alpha), 7);       // its shadow
    rect(dl, f, bx, by, bw, bh, with_alpha(fill, 0.86f * alpha), 7);
    dl->AddRect(f.p(bx, by), f.p(bx + bw, by + bh), with_alpha(b.mine ? RED : IM_COL32(255, 255, 255, 60), alpha), 7 * f.s(), 0,
                std::max(1.0f, 0.8f * f.s()));
    if (tailed) {       // the tail, down to the head
        float tx = std::max(bx + 10, std::min(ax, bx + bw - 10));
        ImVec2 tri[3] = { f.p(tx - 5, by + bh - 0.5f), f.p(tx + 5, by + bh - 0.5f), f.p(ax, by + bh + tail) };
        dl->AddConvexPolyFilled(tri, 3, with_alpha(fill, 0.86f * alpha));
    } else if (edge) {  // an arrow on the edge side, toward the fighter
        float l = sqrtf(dir_x * dir_x + dir_y * dir_y);
        if (l > 1e-3f) {
            float ux = dir_x / l, uy = dir_y / l, mx = cx + ux * (bw / 2 + 4), my = cy + uy * (bh / 2 + 4);
            ImVec2 tri[3] = { f.p(mx + ux * 7, my + uy * 7), f.p(mx - uy * 5, my + ux * 5), f.p(mx + uy * 5, my - ux * 5) };
            dl->AddConvexPolyFilled(tri, 3, with_alpha(b.mine ? RED : GOLD_TEXT, alpha));
        }
    }
    float ix = bx + 7;
    if (talking) { rts_speaker(dl, f, ix + 3.5f, by + 7.5f, 5.0f, alpha); ix += 12; }
    text(dl, f, g_hand, 9.5f, ix, by + 2, with_alpha(b.mine ? GOLD_TEXT : BLUE_NAME, alpha), b.name.c_str(), LEFT, 0.6f);
    text(dl, f, font, size, bx + 7, by + 14, with_alpha(CREAM, alpha), b.text.c_str(), LEFT, short_line ? 1.2f : 0.0f, wrap);
    return by;
}

// The bubbles and speaker icons; returns false when nothing is to be drawn.
bool rts_world_draw_impl(ImDrawList *dl, ImVec2 display, bool draw)
{
    RtsSocialView v;
    bool on = rts_social_view(&v);
    if (!on) {
        if (RS.was_on && !rts_social_on()) { RS.since = 0; RS.was_on = false; for (auto &t : RS.test_bubbles) t.done = false; }
        return false;
    }
    rts_parse_tests();
    if (!RS.was_on) {
        RS.was_on = true;
        RS.since = GetTickCount64();
        fprintf(stderr, "[RTSBUBBLE] Run the Streets fight on screen: bubbles and speaker icons on (local fighter %d, lens fov %.1f aspect %.3f)\n",
                v.local, (double)v.fov_deg, (double)v.aspect);
    }
    std::vector<RtsBubble> lines = rts_bubbles(v);
    uint32_t talking = rts_talking(v);
    if (lines.empty() && !talking) return false;
    if (!draw) return true;
    Frame f = menu_frame(display);
    float px, py, pw, ph;
    rts_picture(display, px, py, pw, ph);
    // the picture's box in the menu's units, with a margin
    float lo_x = (px - f.o.x) / f.sx + 8, hi_x = (px + pw - f.o.x) / f.sx - 8, lo_y = (py - f.o.y) / f.sy + 8,
          hi_y = (py + ph - f.o.y) / f.sy - 60;     // clear of the dock and the HUD's bottom
    RtsView rv;
    memcpy(rv.eye, v.eye, sizeof rv.eye);
    memcpy(rv.at, v.at, sizeof rv.at);
    rv.fov_deg = v.fov_deg;
    rv.aspect = v.aspect;
    unsigned long long now = GetTickCount64();
    // farthest first, so the nearer fighters' bubbles lie on top
    int order[4] = { 0, 1, 2, 3 };
    float dist[4] = {};
    for (int s = 0; s < 4; s++) {
        float dx = v.pos[s][0] - v.eye[0], dz = v.pos[s][2] - v.eye[2];
        dist[s] = dx * dx + dz * dz;
    }
    std::sort(order, order + 4, [&](int a, int b) { return dist[a] > dist[b]; });
    for (int oi = 0; oi < 4; oi++) {
        int s = order[oi];
        if (!(v.present & (1u << s))) continue;
        std::vector<const RtsBubble *> mine;
        for (auto &b : lines) if (b.fighter == s) mine.push_back(&b);
        std::sort(mine.begin(), mine.end(), [](const RtsBubble *a, const RtsBubble *b) { return a->at > b->at; });
        if (mine.size() > 2) mine.resize(2);        // the newest by the head, one older above it
        bool talks = (talking >> s) & 1;
        if (mine.empty() && !talks) continue;
        float p[3] = { v.pos[s][0], v.pos[s][1] + RTS_HEAD, v.pos[s][2] }, nx, ny;
        int front = rts_project(&rv, p, &nx, &ny);
        if (front < 0) continue;
        float sx = px + (nx * 0.5f + 0.5f) * pw, sy = py + (0.5f - ny * 0.5f) * ph;
        float ax = (sx - f.o.x) / f.sx, ay = (sy - f.o.y) / f.sy;
        float dir_x = ax - (lo_x + hi_x) / 2, dir_y = ay - (lo_y + hi_y) / 2;
        bool edge = rts_clamp_to_box(&ax, &ay, lo_x + 40, lo_y + 30, hi_x - 40, hi_y, front == 0) != 0;
        float top = ay;
        for (size_t i = 0; i < mine.size(); i++) {
            const RtsBubble &b = *mine[i];
            float a = rts_bubble_alpha((unsigned)(now - b.at)) * (i == 0 ? 1.0f : 0.72f);
            if (a <= 0.01f) continue;
            bool first = i == 0;
            top = rts_bubble(dl, f, ax, first ? ay : top - 3, b, a, first && !edge, first && edge, dir_x, dir_y, lo_x, hi_x,
                             lo_y, first, first && talks);
            bool again = false;
            for (auto &lt : RS.logged_test) if (lt.first == b.text && lt.second == b.at) again = true;
            if (!again) {
                RS.logged_test.push_back({ b.text, b.at });
                fprintf(stderr, "[RTSBUBBLE] fighter %d (%s): \"%s\" at %.0f,%.0f (menu units)%s, %.1f m from the camera\n", s,
                        b.name.c_str(), b.text.c_str(), (double)ax, (double)ay,
                        edge ? (front == 0 ? " on the edge (behind)" : " on the edge") : "", (double)(sqrtf(dist[s]) / RTS_UNITS_PER_M));
            }
        }
        if (talks && mine.empty()) {
            if (edge) rts_speaker(dl, f, ax, ay, 7.0f, 1.0f);
            else rts_speaker(dl, f, ax, ay - 9, 7.5f, 1.0f);
            static uint32_t logged_talk;
            if (!(logged_talk & (1u << s))) {
                logged_talk |= 1u << s;
                fprintf(stderr, "[RTSBUBBLE] speaker icon over fighter %d at %.0f,%.0f%s\n", s, (double)ax, (double)ay, edge ? " (edge)" : "");
            }
        }
    }
    return true;
}

// ---- the docked chat --------------------------------------------------------------------------------------------

const char *const k_dock_tabs[3] = { "ROOM", "GLOBAL", "PRIVATE" };

bool rts_dock_live() { return !gallery_on() && fight_chat_live() && rts_social_on(); }

int rts_dock_tabs() { return social_enabled() ? 3 : 1; }

// Out of a Run the Streets fight (the fight chat's own rules again): the next one starts with everything read.
void rts_dock_gone()
{
    if (!RS.live_before) return;
    RS.live_before = false;
    RS.test_step = 0;
    fprintf(stderr, "[RTSDOCK] docked chat gone (the fight is over)\n");
}

long long rts_dock_convo()
{
    if (RS.dm_with) {
        for (auto &c : C.v.convos) if (c.with.id == RS.dm_with) return RS.dm_with;
    }
    return C.v.convos.empty() ? 0 : C.v.convos[0].with.id;
}

const char *rts_dock_target()
{
    static std::string who;
    if (RS.tab == 1) return "everyone on the server";
    if (RS.tab == 2) { who = convo_name(rts_dock_convo()); return who.empty() ? "nobody yet" : who.c_str(); }
    return "the room";
}

bool rts_blocked(long long id)
{
    for (auto &b : C.v.blocked) if (b.id == id) return true;
    for (auto &m : C.v.muted) if (m.id == id) return true;
    return false;
}

int rts_unread(int tab)
{
    if (tab == 0) {
        static NetChatLine buf[NETPLAY_CHAT_LINES];
        int n = netplay_room_chat(buf, NETPLAY_CHAT_LINES, nullptr), u = 0;
        for (int i = 0; i < n; i++)
            if (buf[i].seq > RS.seen_room && !buf[i].mine && !buf[i].system && !buf[i].history && !room_muted(buf[i])) u++;
        return u;
    }
    if (tab == 1) {
        int u = 0;
        for (auto &l : C.v.global) if (l.seq > RS.seen_global && !l.mine && !l.system && !l.hidden && !rts_blocked(l.from)) u++;
        return u;
    }
    return C.v.unread;
}

// Sends the dock's line to its tab (ENTER, or SEND on the on-screen keyboard: fight_send comes here).
void rts_dock_send()
{
    std::string t = FC.draft;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    size_t lead = t.find_first_not_of(' ');
    FC.draft.clear();
    FC.cursor = 0;
    if (lead == std::string::npos) return;
    t = t.substr(lead);
    if (RS.tab == 0) {
        if (t.size() > 200) {
            size_t cut = 200;
            while (cut > 0 && ((unsigned char)t[cut] & 0xC0) == 0x80) cut--;
            t.resize(cut);
        }
        if (netplay_room_chat_send(t.c_str())) fprintf(stderr, "[RTSDOCK] room line sent: %s\n", t.c_str());
    } else if (RS.tab == 1 && social_enabled()) {
        std::string region = social::region_of_server(connected_server());
        social::send_global(region.empty() ? "eu" : region.c_str(), t);
        fprintf(stderr, "[RTSDOCK] global line sent (%s): %s\n", region.empty() ? "eu" : region.c_str(), t.c_str());
    } else if (RS.tab == 2 && social_enabled() && rts_dock_convo()) {
        social::send_dm(rts_dock_convo(), t);
        fprintf(stderr, "[RTSDOCK] private message sent: %s\n", t.c_str());
    }
}

struct DockLine { std::string name, text; bool mine = false, system = false, dim = false; };

std::vector<DockLine> rts_dock_lines(int tab, int max)
{
    std::vector<DockLine> out;
    if (tab == 0) {
        static NetChatLine buf[NETPLAY_CHAT_LINES];
        int n = netplay_room_chat(buf, NETPLAY_CHAT_LINES, nullptr);
        for (int i = n - 1; i >= 0 && (int)out.size() < max; i--) {
            if (room_muted(buf[i])) continue;
            DockLine d;
            d.name = buf[i].system ? "" : buf[i].mine ? "You" : buf[i].name;
            d.text = buf[i].text;
            d.mine = buf[i].mine != 0;
            d.system = buf[i].system != 0;
            d.dim = buf[i].history != 0;
            out.push_back(d);
        }
    } else if (tab == 1) {
        for (size_t i = C.v.global.size(); i-- > 0 && (int)out.size() < max;) {
            const social::Line &l = C.v.global[i];
            if (l.hidden || (!l.mine && rts_blocked(l.from))) continue;
            DockLine d;
            d.name = l.system ? "" : l.mine ? "You" : l.name;
            d.text = l.text;
            d.mine = l.mine;
            d.system = l.system;
            out.push_back(d);
        }
    } else {
        long long with = rts_dock_convo();
        std::string who = convo_name(with);
        for (auto &th : C.v.threads) {
            if (th.with != with) continue;
            for (size_t i = th.msgs.size(); i-- > 0 && (int)out.size() < max;) {
                DockLine d;
                d.mine = th.msgs[i].from == C.v.me;
                d.name = d.mine ? "You" : who;
                d.text = th.msgs[i].text;
                out.push_back(d);
            }
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

void rts_dock_test()
{
    float t = rts_fight_seconds();
    for (auto &s : RS.test_say) {
        if (s.done || t < s.s || room_chat_state() != 1) continue;
        s.done = true;
        if (netplay_room_chat_send(s.text.c_str())) fprintf(stderr, "[RTSDOCK] test: room line said %.1f s in: %s\n", (double)t, s.text.c_str());
    }
    for (auto &d : RS.test_dock) {
        if (d.done) continue;
        if (RS.test_step == 0 && t >= d.s) {
            FC.typing = true;
            FC.pad = false;
            FC.opened = GetTickCount64();
            RS.tab = std::max(0, std::min(d.tab, rts_dock_tabs() - 1));
            FC.draft = d.text;
            FC.cursor = (int)FC.draft.size();
            RS.test_step = 1;
            fprintf(stderr, "[RTSDOCK] test: dock opened on %s with a line typed\n", k_dock_tabs[RS.tab]);
        } else if (RS.test_step == 1 && t >= d.s + 1.5f) {
            rts_dock_send();
            RS.test_step = 2;
            RS.test_close_at = GetTickCount64() + 4000;
        } else if (RS.test_step == 2 && GetTickCount64() >= RS.test_close_at) {
            FC.typing = false;
            RS.test_step = 0;
            d.done = true;
            fprintf(stderr, "[RTSDOCK] test: dock closed\n");
        }
        break;
    }
}

bool rts_dock_opens()
{
    return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) || fight_pad_opens();
}

// Draws the dock; returns true while it has the input (the game's input is held then).
bool rts_dock_draw(ImDrawList *dl, ImVec2 display)
{
    Frame f = menu_frame(display);
    if (!RS.live_before) {      // a new fight: what came before counts as read
        RS.live_before = true;
        RS.since = RS.since ? RS.since : GetTickCount64();
        unsigned last = 0;
        static NetChatLine buf[NETPLAY_CHAT_LINES];
        netplay_room_chat(buf, NETPLAY_CHAT_LINES, &last);
        RS.seen_room = last;
        for (auto &l : C.v.global) RS.seen_global = std::max(RS.seen_global, l.seq);
        RS.dock_since = GetTickCount64();
        fprintf(stderr, "[RTSDOCK] docked chat up (tabs %d, room chat %d)\n", rts_dock_tabs(), room_chat_state());
    }
    rts_parse_tests();
    if (social_enabled() && social::snapshot(C.v, C.have ? C.v.version : 0xFFFFFFFFu)) C.have = true;
    rts_dock_test();
    int tabs = rts_dock_tabs();
    if (RS.tab >= tabs) RS.tab = 0;
    bool kb_up = C.kb && C.kb_kind == KB_FIGHT;
    if (kb_up) RS.kb_frame = ImGui::GetFrameCount();
    bool kb_just = RS.kb_frame >= ImGui::GetFrameCount() - 1;
    if (!FC.typing && !QW.open && rts_dock_opens()) {
        FC.typing = true;
        FC.pad = !(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        FC.opened = GetTickCount64();
        FC.draft.clear();
        FC.cursor = 0;
        fprintf(stderr, "[RTSDOCK] opened (%s) on %s: game input held\n", FC.pad ? "pad" : "keyboard", k_dock_tabs[RS.tab]);
    }
    if (FC.typing && !kb_up && GetTickCount64() - FC.opened > 150) {
        bool sw_next = ImGui::IsKeyPressed(ImGuiKey_Tab, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false);
        bool sw_prev = ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false);
        if ((sw_next || sw_prev) && tabs > 1) {
            RS.tab = (RS.tab + (sw_prev ? tabs - 1 : 1)) % tabs;
            fprintf(stderr, "[RTSDOCK] tab %s\n", k_dock_tabs[RS.tab]);
        }
        if (RS.tab == 2 && (ImGui::IsKeyPressed(ImGuiKey_PageDown, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, false) ||
                            ImGui::IsKeyPressed(ImGuiKey_PageUp, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, false))) {
            int n = (int)C.v.convos.size(), at = 0;
            bool up = ImGui::IsKeyPressed(ImGuiKey_PageUp, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, false);
            for (int i = 0; i < n; i++) if (C.v.convos[(size_t)i].with.id == rts_dock_convo()) at = i;
            if (n) RS.dm_with = C.v.convos[(size_t)((at + (up ? n - 1 : 1)) % n)].with.id;
        }
        if (!FC.pad) {
            std::string typed = typed_chars();
            if (!typed.empty()) insert_text(FC.draft, FC.cursor, typed, 200);
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) erase_back(FC.draft, FC.cursor);
        }
        bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) && !kb_just);
        if (close) {
            FC.typing = FC.pad = false;
            fprintf(stderr, "[RTSDOCK] closed\n");
        } else if (!FC.pad && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
            if (FC.draft.find_first_not_of(' ') == std::string::npos) { FC.typing = false; fprintf(stderr, "[RTSDOCK] closed (empty line)\n"); }
            else rts_dock_send();
        } else if (FC.pad && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) && !kb_just) {
            open_keyboard(KB_FIGHT);
            C.caps = false;
        }
    }
    if (RS.tab == 2 && FC.typing) {     // the conversation shown is read
        long long w = rts_dock_convo();
        if (w && w != RS.dm_opened) { RS.dm_opened = w; social::open_thread(w); }
    }
    social_set_typing(FC.typing);

    const float X = 14, W = 286;
    if (!FC.typing) {
        // closed: the room's last lines a few seconds each (as the fight chat), then the chip with the unread counts
        static NetChatLine buf[NETPLAY_CHAT_LINES];
        int n = netplay_room_chat(buf, NETPLAY_CHAT_LINES, nullptr);
        unsigned long long now = GetTickCount64();
        float y = 444;
        int shown = 0;
        for (int i = n - 1; i >= 0 && shown < 3; i--) {
            const NetChatLine &l = buf[i];
            if (room_muted(l)) continue;
            float age = (float)(now - l.at);
            if (age >= FIGHT_LINE_MS) break;
            float a = (shown == 0 ? 1.0f : shown == 1 ? 0.75f : 0.45f);
            if (age > FIGHT_LINE_MS - 1500) a *= std::max(0.0f, (FIGHT_LINE_MS - age) / 1500.0f);
            shown++;
            const char *name = l.system ? "" : l.mine ? "You" : l.name;
            float nw = l.system ? 0 : text_width(f, g_hand, 10.5f, name) + 5;
            float tw = std::min(240.0f, text_width(f, g_body, 11, l.text));
            float th = std::max(13.0f, text_height(f, g_body, 11, l.text, 240));
            float h = th + 6;
            y -= h;
            rect(dl, f, X, y, nw + tw + 12, h, IM_COL32(0, 0, 0, (int)(110 * a)), 5);
            if (!l.system) text(dl, f, g_hand, 10.5f, X + 6, y + 2, with_alpha(l.mine ? GOLD_TEXT : BLUE_NAME, a), name, LEFT, 0);
            text(dl, f, g_body, 11, X + 6 + nw, y + 2, with_alpha(l.system ? ORANGE : CREAM, a), l.text, LEFT, 0, 240);
            y -= 3;
        }
        if (room_chat_state() >= 0) {
            std::string keys = std::string("ENTER / ") + fight_pad_name();
            float yy = 450, pw = text_width(f, g_body, 8, keys.c_str()) + 6, x = X;
            float w = pw + text_width(f, g_title, 8, "CHAT") + 12;
            for (int t = 0; t < tabs; t++) {
                int u = rts_unread(t);
                if (u > 0) w += text_width(f, g_title, 7.5f, k_dock_tabs[t]) + 20;
            }
            rect(dl, f, x, yy, w, 13, IM_COL32(0, 0, 0, 120), 4);
            rect(dl, f, x + 3, yy + 2, pw, 9, IM_COL32(70, 70, 70, 220), 2);
            text(dl, f, g_body, 8, x + 3 + pw / 2, yy + 2, WHITE, keys.c_str(), CENTER, 0);
            x += 7 + pw;
            text(dl, f, g_title, 8, x, yy + 2.5f, IM_COL32(242, 230, 200, 210), "CHAT", LEFT, 0);
            x += text_width(f, g_title, 8, "CHAT") + 8;
            for (int t = 0; t < tabs; t++) {
                int u = rts_unread(t);
                if (u <= 0) continue;
                char c[8];
                snprintf(c, sizeof c, "%d", std::min(u, 99));
                text(dl, f, g_title, 7.5f, x, yy + 3, DIM, k_dock_tabs[t], LEFT, 0);
                x += text_width(f, g_title, 7.5f, k_dock_tabs[t]) + 2;
                dl->AddCircleFilled(f.p(x + 6, yy + 6.5f), 5.2f * f.s(), RED, 14);
                text(dl, f, g_body, 7, x + 6, yy + 2.5f, WHITE, c, CENTER, 0);
                x += 18;
            }
        }
        return false;
    }

    // open: the compact window
    const float Y = 252, H = 214, TABH = 19, BAR = 26;
    glass(dl, f, X, Y, W, H, 8, IM_COL32(10, 10, 13, 255), 0.84f);
    float tx = X + 6;
    for (int t = 0; t < tabs; t++) {
        bool on = t == RS.tab;
        int u = on ? 0 : rts_unread(t);
        float tw = text_width(f, g_title, 9, k_dock_tabs[t]) + 16 + (u > 0 ? 12 : 0);
        if (on) rect(dl, f, tx, Y + 5, tw, TABH - 3, with_alpha(RED, 0.9f), 4);
        text(dl, f, g_title, 9, tx + 8, Y + 8, on ? IM_COL32(255, 245, 230, 255) : DIM, k_dock_tabs[t], LEFT, 0);
        if (u > 0) {
            float cx = tx + tw - 10;
            dl->AddCircleFilled(f.p(cx, Y + 13), 4.5f * f.s(), on ? WHITE : RED, 12);
            char c[8];
            snprintf(c, sizeof c, "%d", std::min(u, 9));
            text(dl, f, g_body, 6.5f, cx, Y + 9.5f, WHITE, c, CENTER, 0);
        }
        tx += tw + 4;
    }
    if (tabs > 1) text(dl, f, g_body, 7, X + W - 8, Y + 9, DIM, FC.pad ? "LB / RB" : "TAB", RIGHT, 0);
    rect(dl, f, X + 6, Y + TABH + 5, W - 12, 0.6f, LINE_SEP);
    // the tab's lines, the newest at the bottom
    float ly0 = Y + TABH + 9, ly1 = Y + H - BAR - 6, y = ly1;
    std::string empty;
    if (RS.tab == 0 && room_chat_state() < 0) empty = "No room chat on this server.";
    else if (RS.tab == 2 && !rts_dock_convo()) empty = "No private messages yet.";
    std::vector<DockLine> lines = rts_dock_lines(RS.tab, 12);
    if (RS.tab == 0) { unsigned last = 0; static NetChatLine b2[NETPLAY_CHAT_LINES]; netplay_room_chat(b2, NETPLAY_CHAT_LINES, &last); RS.seen_room = last; }
    if (RS.tab == 1) for (auto &l : C.v.global) RS.seen_global = std::max(RS.seen_global, l.seq);
    if (RS.tab == 2 && rts_dock_convo()) {
        std::string head = "WITH " + convo_name(rts_dock_convo());
        for (auto &c : head) c = (char)toupper((unsigned char)c);
        text(dl, f, g_title, 8, X + 10, ly0, GOLD_TEXT, head.c_str(), LEFT, 0);
        if (C.v.convos.size() > 1) text(dl, f, g_body, 7, X + W - 8, ly0, DIM, FC.pad ? "UP / DOWN: OTHER CHATS" : "PGUP / PGDN: OTHER CHATS", RIGHT, 0);
        ly0 += 12;
    }
    if (!empty.empty() && lines.empty()) text(dl, f, g_body, 10, X + W / 2, (ly0 + ly1) / 2 - 6, DIM, empty.c_str(), CENTER, 0, W - 30);
    dl->PushClipRect(f.p(X, ly0), f.p(X + W, ly1), true);
    for (size_t i = lines.size(); i-- > 0 && y > ly0;) {
        const DockLine &l = lines[i];
        float nw = l.name.empty() ? 0 : text_width(f, g_hand, 10.5f, l.name.c_str()) + 5;
        float th = std::max(13.0f, text_height(f, g_body, 10.5f, l.text.c_str(), W - 22 - nw));
        y -= th + 3;
        ImU32 tc = l.system ? ORANGE : l.dim ? DIM : CREAM;
        if (!l.name.empty()) text(dl, f, g_hand, 10.5f, X + 10, y, l.mine ? GOLD_TEXT : BLUE_NAME, l.name.c_str(), LEFT, 0);
        text(dl, f, g_body, 10.5f, X + 10 + nw, y + 0.5f, tc, l.text.c_str(), LEFT, 0, W - 22 - nw);
    }
    dl->PopClipRect();
    // the bar
    float bx = X + 6, by = Y + H - BAR - 2, bw = W - 12, bh = BAR - 4;
    rect(dl, f, bx, by, bw, bh, IM_COL32(0, 0, 0, 170), 6);
    dl->AddRect(f.p(bx, by), f.p(bx + bw, by + bh), with_alpha(RED, 0.85f), 6 * f.s(), 0, 1.0f * f.s());
    bool can = RS.tab == 0 ? room_chat_state() == 1 : RS.tab == 1 ? social_enabled() : rts_dock_convo() != 0;
    if (FC.pad) {
        const char *a = ps_icons() ? "X" : "A", *b = ps_icons() ? "O" : "B";
        char hint[96];
        snprintf(hint, sizeof hint, "%s: WRITE   %s: CLOSE", a, b);
        text(dl, f, g_body, 9, bx + 8, by + 5.5f, can ? CREAM : DIM, can ? hint : (std::string(b) + ": CLOSE").c_str(), LEFT, 0);
    } else {
        const char *keys = "ENTER SENDS - ESC CLOSES";
        float kw = text_width(f, g_body, 7, keys);
        dl->PushClipRect(f.p(bx + 4, by), f.p(bx + bw - kw - 12, by + bh), true);
        std::string before = FC.draft.substr(0, (size_t)std::max(0, std::min(FC.cursor, (int)FC.draft.size())));
        float cw = text_width(f, g_body, 10.5f, before.c_str()), room = bw - kw - 30;
        float shift = std::max(0.0f, cw - room);
        if (FC.draft.empty())
            text(dl, f, g_body, 10, bx + 8, by + 5, DIM, can ? (std::string("Message ") + rts_dock_target()).c_str() : "You can only read here.", LEFT, 0);
        else
            text(dl, f, g_body, 10.5f, bx + 8 - shift, by + 4.5f, CREAM, FC.draft.c_str(), LEFT, 0);
        if ((GetTickCount64() / 500) % 2 == 0) rect(dl, f, bx + 8 - shift + cw + 1, by + 4, 1.5f, 14, GOLD_TEXT);
        dl->PopClipRect();
        text(dl, f, g_body, 7, bx + bw - 6, by + 8, IM_COL32(242, 230, 200, 153), keys, RIGHT, 0);
    }
    text(dl, f, g_body, 7.5f, X + 2, Y + H + 2, IM_COL32(242, 230, 200, 140), "While the chat is open your fighter stands still.", LEFT, 0);
    if (kb_up) {
        draw_keyboard(dl, f, display);
        if (!C.kb) RS.kb_frame = ImGui::GetFrameCount();
    }
    return true;
}

const char *rts_shot_label()
{
    if (gallery_on()) return nullptr;
    if (rts_dock_live() && FC.typing) {
        static const char *const l[3] = { "rtsdock-room", "rtsdock-global", "rtsdock-private" };
        return l[RS.tab % 3];
    }
    RtsSocialView v;
    if (rts_social_view(&v)) return "rtsworld";
    return nullptr;
}
