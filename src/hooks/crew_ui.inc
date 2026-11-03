// Crew battles (the Social Update, stage 3): the crew battle screen of the design (CrewBattle): the two
// crews' lineups, who fights, the fights won, who watches; in a crew battle room it takes the place of the
// King of the Hill pages (the relay runs the battle as a hill by crews, netplay.h NetCrew). Included by
// online_screen.cpp after social_ui.inc (the social view C.v gives the rating change of the result).
//
// Pad: A READY when this PC is called, X CHANGE ORDER (to the back of its crew, before its fight), Y
// CHOOSE FIGHTER, B LEAVE (twice: a member who leaves forfeits their slot); the chat opens as everywhere
// (BACK / ENTER).

const ImU32 CREW_RED = IM_COL32(255, 92, 118, 255), CREW_BLUE = IM_COL32(118, 170, 255, 255);

bool crew_room(const NetRoom &r, NetCrew *out)
{
    if (gallery_on()) {
        if (!G.crew.battle) return false;
        *out = G.crew;
        return true;
    }
    if (!r.in_room || !NETPLAY_IS_KOTH(r.settings.mode)) return false;
    return netplay_lobby_crew(out) != 0;
}

// Gallery samples: crew-battle (the design's 5 v 5 mid-battle), crew-battle-up (this PC called),
// crew-battle-forming (waiting for the lineups), crew-battle-over (the result).
void crew_sample(const std::string &p, NetRoom &r)
{
    static const char *const a[5] = { "Rico", "Jaz", "Ann", "Tavo", "KaySlam" }, *const b[5] = { "Lino", "Sky", "Vex", "Ben", "Moe" };
    NetCrew &c = G.crew;
    memset(&c, 0, sizeof(c));
    c.battle = 12;
    c.format = 5;
    c.state = NETCREW_RUNNING;
    c.watchers = 6;
    c.received = GetTickCount64();
    snprintf(c.sides[0].tag, sizeof(c.sides[0].tag), "BXK");
    snprintf(c.sides[0].name, sizeof(c.sides[0].name), "Bronx Kings");
    snprintf(c.sides[1].tag, sizeof(c.sides[1].tag), "RHR");
    snprintf(c.sides[1].name, sizeof(c.sides[1].name), "Red Hook Riders");
    for (int s = 0; s < 2; s++) {
        c.sides[s].count = 5;
        for (int i = 0; i < 5; i++) {
            NetCrewEntry &e = c.sides[s].lineup[i];
            snprintf(e.name, sizeof(e.name), "%s", s ? b[i] : a[i]);
            e.slot = s * 5 + i;
            e.state = NETCREW_WAITING;
        }
    }
    // the design: Rico, Jaz out; Ann fights; Lino, Sky, Vex out; Ben fights: 2 - 3
    c.sides[0].lineup[0].state = c.sides[0].lineup[1].state = NETCREW_OUT;
    c.sides[1].lineup[0].state = c.sides[1].lineup[1].state = c.sides[1].lineup[2].state = NETCREW_OUT;
    c.sides[0].lineup[2].state = c.sides[1].lineup[3].state = NETCREW_FIGHTING;
    c.sides[0].lineup[2].wins = 2;
    c.sides[1].lineup[1].wins = 1; c.sides[1].lineup[2].wins = 1; c.sides[1].lineup[3].wins = 1;
    c.sides[0].score = 2;
    c.sides[1].score = 3;
    for (int i = 0; i < 10; i++) {
        r.players[i].slot = i;
        snprintf(r.players[i].name, sizeof(r.players[i].name), "%s", i < 5 ? a[i] : b[i - 5]);
    }
    r.player_count = 10;
    r.you = 3;      // Tavo: next for Bronx Kings
    r.hill.valid = 1;
    r.hill.phase = NETHILL_FIGHTING;
    r.hill.fights = 5;
    if (p == "crew-battle-up") {
        c.sides[0].lineup[2].state = NETCREW_OUT;
        c.sides[0].lineup[3].state = NETCREW_FIGHTING;
        c.sides[1].score = 4;
        r.hill.phase = NETHILL_CALLING;
        r.hill.ms_left = 14000;
        r.hill.received = GetTickCount64();
        r.hill.count = 2;
        r.hill.line[0].slot = 3; r.hill.line[0].state = NETLINE_CALLED;
        r.hill.line[1].slot = 8; r.hill.line[1].state = NETLINE_READY;
    } else if (p == "crew-battle-forming") {
        c.state = NETCREW_FORMING;
        c.secs_left = 151;
        c.sides[0].score = c.sides[1].score = 0;
        c.sides[0].count = 4;
        c.sides[1].count = 3;
        for (int s = 0; s < 2; s++)
            for (int i = 0; i < 5; i++) { c.sides[s].lineup[i].state = NETCREW_WAITING; c.sides[s].lineup[i].wins = 0; }
        c.watchers = 0;
        r.hill.phase = NETHILL_WAITING;
        r.hill.fights = 0;
    } else if (p == "crew-battle-over") {
        c.state = NETCREW_OVER;
        c.winner = 1;
        for (int i = 2; i < 5; i++) c.sides[1].lineup[i].state = NETCREW_OUT;
        c.sides[1].lineup[4].state = NETCREW_GONE;
        c.sides[0].lineup[2].state = NETCREW_WAITING;
        c.sides[0].lineup[2].wins = 4;
        c.sides[0].score = 5;
        r.hill.phase = NETHILL_WAITING;
    }
}

// One lineup: the crew's name, tag and fighters left, then a row per place.
void crew_panel(ImDrawList *dl, const Frame &f, const NetRoom &r, const NetCrew &c, int s, float x, float y, float w, float h,
                bool over)
{
    const NetCrewSide &side = c.sides[s];
    ImU32 accent = s == 0 ? CREW_RED : CREW_BLUE;
    glass(dl, f, x, y, w, h, 12, IM_COL32(16, 10, 12, 255), 0.72f);
    // the crew's level 8 reward, the versus card: a gold frame and a glow on its panel
    bool card = false;
    if (C.v.battle.id && (int)C.v.battle.id == (int)c.battle) card = C.v.battle.crews[s].level >= 8;
    if (card) dl->AddRect(f.p(x - 1, y - 1), f.p(x + w + 1, y + h + 1), IM_COL32(255, 205, 60, 200), 12 * f.s(), 0, 1.6f * f.s());
    int left = 0;
    for (int i = 0; i < side.count; i++) left += side.lineup[i].state == NETCREW_WAITING || side.lineup[i].state == NETCREW_FIGHTING;
    std::string name = upper(side.name);
    float nw = text_width(f, g_title, 15, name.c_str());
    nw = std::min(nw, w - 110);
    text(dl, f, g_title, 15, x + 12, y + 10, accent, name.c_str(), LEFT, 1.0f, w - 110);
    float tw = text_width(f, g_title, 9, side.tag) + 9;
    bool gold = social::gold_tag(C.v, side.tag);
    dl->AddRect(f.p(x + 18 + nw, y + 12), f.p(x + 18 + nw + tw, y + 26), gold ? GOLD : accent, 3 * f.s(), 0, 1.2f * f.s());
    text(dl, f, g_title, 9, x + 18 + nw + tw / 2, y + 14, gold ? GOLD : accent, side.tag, CENTER, 0);
    char lt[24];
    snprintf(lt, sizeof(lt), "%d LEFT", left);
    text(dl, f, g_title, 15, x + w - 12, y + 10, CREAM, lt, RIGHT, 1.0f);
    // the fighters still in, in order: the first fights (or is called next), the second is NEXT, the last LAST
    int in[NETPLAY_CREW_MAX], n_in = 0;
    for (int i = 0; i < side.count; i++)
        if (side.lineup[i].state == NETCREW_WAITING || side.lineup[i].state == NETCREW_FIGHTING) in[n_in++] = i;
    int next = n_in > 1 ? in[1] : -1, last = n_in > 2 ? in[n_in - 1] : -1;
    float ry = y + 38, rh = std::min(34.0f, (h - 46) / std::max(5, c.format) - 4);
    for (int i = 0; i < std::max(side.count, c.format); i++) {
        float yy = ry + i * (rh + 4);
        if (i >= side.count) {      // an empty place: nobody came for it yet
            dl->AddRect(f.p(x + 8, yy), f.p(x + w - 8, yy + rh), IM_COL32(255, 255, 255, 30), 6 * f.s(), 0, 1.0f * f.s());
            text(dl, f, g_title, 10, x + w / 2, yy + rh / 2 - 6, GREY, c.state == NETCREW_FORMING ? "WAITING FOR A FIGHTER" : "EMPTY",
                 CENTER, 0);
            continue;
        }
        const NetCrewEntry &e = side.lineup[i];
        bool out = e.state == NETCREW_OUT || e.state == NETCREW_GONE, fight = e.state == NETCREW_FIGHTING, me = e.slot >= 0 && e.slot == r.you;
        ImU32 fill = fight ? (s == 0 ? IM_COL32(90, 12, 26, 230) : IM_COL32(16, 40, 92, 230)) : IM_COL32(255, 255, 255, out ? 8 : 16);
        rect(dl, f, x + 8, yy, w - 16, rh, fill, 6);
        if (fight) dl->AddRect(f.p(x + 8, yy), f.p(x + w - 8, yy + rh), accent, 6 * f.s(), 0, 1.6f * f.s());
        else if (me) dl->AddRect(f.p(x + 8, yy), f.p(x + w - 8, yy + rh), with_alpha(ORANGE, 0.8f), 6 * f.s(), 0, 1.2f * f.s());
        // the fighter's portrait (CF in the design)
        float ps = rh - 6;
        int fighter = e.slot >= 0 ? slot_fighter(r, e.slot) : -1;
        rect(dl, f, x + 13, yy + 3, ps, ps, IM_COL32(0, 0, 0, 120), 4);
        if (fighter < 0 || gallery_on() || !portrait(dl, f, fighter, x + 13, yy + 3, ps, ps, out ? IM_COL32(255, 255, 255, 90) : IM_COL32_WHITE))
            text(dl, f, g_title, 8, x + 13 + ps / 2, yy + 3 + ps / 2 - 5, with_alpha(GREY, out ? 0.5f : 1.0f), "CF", CENTER, 0);
        text(dl, f, g_hand, 15, x + 20 + ps, yy + rh / 2 - 9, with_alpha(me ? ORANGE : CREAM, out ? 0.45f : 1.0f), e.name, LEFT, 0.6f,
             w - 120 - ps);
        std::string label;
        ImU32 lc = GREY;
        if (e.state == NETCREW_GONE) label = "LEFT";
        else if (out) label = "OUT";
        else if (fight) { label = c.state == NETCREW_RUNNING && r.hill.phase == NETHILL_CALLING ? "UP" : "FIGHTING"; lc = accent; }
        else if (over) { label = "STOOD"; lc = GOLD; }
        else if (i == next) { label = "NEXT"; lc = CREAM; }
        else if (i == last && c.state == NETCREW_RUNNING) { label = "LAST"; lc = GOLD; }
        if (e.wins > 0 && !label.empty() && !out) label += "  x" + std::to_string(e.wins);
        else if (e.wins > 0 && label.empty()) label = "x" + std::to_string(e.wins);
        text(dl, f, g_title, 11, x + w - 16, yy + rh / 2 - 7, with_alpha(lc, out ? 0.6f : 1.0f), label.c_str(), RIGHT, 0);
    }
}

void draw_crew_battle(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const NetCrew &c)
{
    const NetHill &h = r.hill;
    HillView v = hill_view(r);
    bool over = c.state == NETCREW_OVER;
    int mine = -1, my_place = -1;
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < c.sides[s].count; i++) if (c.sides[s].lineup[i].slot >= 0 && c.sides[s].lineup[i].slot == r.you) { mine = s; my_place = i; }
    // the room theme (the level 10 reward): the winners' colour behind the page
    for (int s = 0; s < 2; s++)
        if (C.v.battle.id && (unsigned)C.v.battle.id == c.battle && C.v.battle.crews[s].level >= 10)
            dl->AddRectFilledMultiColor(f.p(s ? 320 : 0, 0), f.p(s ? 640 : 320, 480), with_alpha(s ? CREW_BLUE : CREW_RED, s ? 0 : 0.10f),
                                        with_alpha(s ? CREW_BLUE : CREW_RED, s ? 0.10f : 0), with_alpha(s ? CREW_BLUE : CREW_RED, s ? 0.10f : 0),
                                        with_alpha(s ? CREW_BLUE : CREW_RED, s ? 0 : 0.10f));
    text(dl, f, g_stencil, 34, 320, 12, CREAM, "CREW BATTLE", CENTER, 1.6f);
    char sub[96];
    snprintf(sub, sizeof(sub), "%d VS %d  -  WINNER STAYS ON", c.format, c.format);
    text(dl, f, g_title, 13, 320, 52, ORANGE, sub, CENTER, 1.0f);

    crew_panel(dl, f, r, c, 0, 26, 74, 214, 300, over);
    crew_panel(dl, f, r, c, 1, 400, 74, 214, 300, over);

    // the middle: VS, the fights won, what happens now
    text(dl, f, g_stencil, 50, 320, 104, CREAM, "VS", CENTER, 1.8f);
    char sc[8];
    snprintf(sc, sizeof(sc), "%d", c.sides[0].score);
    text(dl, f, g_stencil, 46, 300, 168, CREW_RED, sc, RIGHT, 1.6f);
    text(dl, f, g_title, 24, 320, 182, GREY, "-", CENTER, 0);
    snprintf(sc, sizeof(sc), "%d", c.sides[1].score);
    text(dl, f, g_stencil, 46, 340, 168, CREW_BLUE, sc, LEFT, 1.6f);
    char round[48];
    snprintf(round, sizeof(round), "FIGHTS WON  -  ROUND %d", h.fights + (over ? 0 : 1));
    text(dl, f, g_title, 10, 320, 226, GREY, round, CENTER, 0.6f);
    glass(dl, f, 248, 244, 144, 92, 10);
    std::string l1, l2;
    ImU32 c1 = CREAM;
    char buf[160];
    const NetCrewEntry *fa = nullptr, *fb = nullptr;
    for (int i = 0; i < c.sides[0].count; i++) if (c.sides[0].lineup[i].state == NETCREW_FIGHTING) fa = &c.sides[0].lineup[i];
    for (int i = 0; i < c.sides[1].count; i++) if (c.sides[1].lineup[i].state == NETCREW_FIGHTING) fb = &c.sides[1].lineup[i];
    bool called_me = h.valid && h.phase == NETHILL_CALLING && v.my_state == NETLINE_CALLED;
    if (over) {
        if (c.winner) {
            snprintf(buf, sizeof(buf), "%s WIN", upper(c.sides[c.winner - 1].name).c_str());
            l1 = buf;
            c1 = c.winner == 1 ? CREW_RED : CREW_BLUE;
        } else {
            l1 = "A DRAW";
        }
        const social::Battle &b = C.v.battle;
        if (b.id && (unsigned)b.id == c.battle && b.state == "over") {
            if (b.rated) snprintf(buf, sizeof(buf), "Rating %+d / %+d. XP +%d / +%d.", b.dx[0], b.dx[1], b.xp[0], b.xp[1]);
            else snprintf(buf, sizeof(buf), "Not rated: %s.", b.unrated.c_str());
            l2 = buf;
        } else {
            l2 = "The result goes to the crew ladder.";
        }
    } else if (c.state == NETCREW_FORMING) {
        l1 = "WAITING FOR THE CREWS";
        snprintf(buf, sizeof(buf), "%d of %d v %d of %d in. Starts when both are full, or in %d:%02d with who is here (not rated).",
                 c.sides[0].count, c.format, c.sides[1].count, c.format, c.secs_left / 60, c.secs_left % 60);
        l2 = buf;
    } else if (called_me) {
        int secs = (v.ms_left + 999) / 1000;
        snprintf(buf, sizeof(buf), "YOU'RE UP  %d", secs);
        l1 = buf;
        c1 = ORANGE;
        l2 = pad_text("Press {a} now. Not ready in time: to the back of your crew (twice: out).");
    } else if (fa && fb) {
        const NetCrewEntry *stay = fa->wins > fb->wins ? fa : fb->wins > fa->wins ? fb : nullptr;
        if (stay && h.phase == NETHILL_FIGHTING) snprintf(buf, sizeof(buf), "%s stays on (x%d)", stay->name, stay->wins);
        else snprintf(buf, sizeof(buf), "%s v %s", fa->name, fb->name);
        l1 = buf;
        snprintf(buf, sizeof(buf), "%s  -  %d watching", h.phase == NETHILL_FIGHTING ? "fight on" : "both called",
                 gallery_on() ? c.watchers : std::max(0, r.player_count - 2));
        l2 = buf;
    } else {
        l1 = "NEXT FIGHT";
        l2 = "The next two are called in a moment.";
    }
    text(dl, f, g_title, 13, 320, 252, c1, l1.c_str(), CENTER, 1.0f, 134);
    text(dl, f, g_body, 9.5f, 320, 290, CREAM, l2.c_str(), CENTER, 0, 134);
    if (mine < 0 && r.you != NETPLAY_SLOT_SPECTATOR)
        text(dl, f, g_hand, 12, 320, 344, GREY, "You watch from the bench.", CENTER, 0.6f);
    else if (mine >= 0 && !over && c.sides[mine].lineup[my_place].state == NETCREW_WAITING && v.next == r.you)
        text(dl, f, g_hand, 14, 320, 344, ORANGE, "YOU'RE UP NEXT", CENTER, 0.8f);

    // the hints: A READY (called), X CHANGE ORDER, Y CHOOSE FIGHTER, B LEAVE
    float hx = 30, hy = 392;
    int fighter = slot_fighter(r, r.you);
    bool can_order = mine >= 0 && !over && c.sides[mine].lineup[my_place].state == NETCREW_WAITING && yield_offered();
    if (called_me) hx += hint(dl, f, hx, hy, "a", "READY");
    if (can_order) hx += hint(dl, f, hx, hy, "x", "CHANGE ORDER");
    hx += hint(dl, f, hx, hy, "y", "CHOOSE FIGHTER");
    unsigned long long now = GetTickCount64();
    bool armed = S.give_up_at && now - S.give_up_at < 4000;
    hx += hint(dl, f, hx, hy, "b", armed ? "AGAIN: LEAVE" : over ? "LEAVE" : "LEAVE (FORFEIT)");
    text(dl, f, g_body, 8.5f, 610, 418, GREY, "Leaving counts as a lost fight. BACK opens the chat.", RIGHT, 0);
    (void)hx;
    if (gallery_on()) return;
    if (pad.a && called_me) netplay_lobby_ready(1, fighter);
    if (pad.x && can_order) {
        netplay_lobby_yield();
        say("To the back of your crew's order.");
    }
    if (pad.y) choose_fighter(fighter);
    if (pad.b) {
        if (over || armed) { S.give_up_at = 0; leave_hill(over ? "You left the crew battle." : "You left the crew battle: your slot is forfeited."); }
        else { S.give_up_at = now; say("B again leaves the battle: your slot is forfeited."); }
    }
}
