// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Team Tower Online (towers_online.h): the screens. Included into online_screen.cpp after towers_ui.inc.
//
//   the card        TOWERS page, the Team Tower (standard) chosen: RT opens TEAM TOWER ONLINE (owner, 2026-11-22: as
//                   the public rooms): HOST A PUBLIC ROOM (in FIND A ROOM for everyone), HOST A PRIVATE ROOM (join by
//                   code), FIND A ROOM, JOIN BY CODE; with a saved online climb LEFT / RIGHT picks RESUME FLOOR n WITH
//                   <partner> (the room then takes only that partner) or NEW CLIMB. Both players signed in.
//   the room        a tower room's page (draw_room): WAITING with the code big until the partner is in; then the two
//                   players' cards and the climb (a saved climb: RESUME FLOOR n, the host's X: NEW CLIMB), A READY,
//                   Y CHOOSE FIGHTER (standard ones). Between floors the climb scene (towers_scene, as two pads
//                   see it) with the partner's name, the floor just fought and the next CPU pair, and the continue
//                   vote: both press A within 90 s (each player's tick shows), B SAVE & QUIT. The partner away: the
//                   clock until the climb is saved. The end: the result; the host's A starts a new climb.
// The relay runs all of it (server relay/tower.go): the page only shows TOWER and sends READY / TCHOICE.

struct TowerOnlineUi {
    int played_floor[2] = { 0, 0 };     // the last two floors this PC saw START (their CPUs for the BEATEN card)
    int played_opp[2][2] = { { -1, -1 }, { -1, -1 } };
    ULONGLONG summary_since = 0;
    int summary_key = -1;
} TWO;

const NetRoomPlayer *tower_player(const NetRoom &r, int slot)
{
    for (int i = 0; i < r.player_count; i++) if (r.players[i].slot == slot) return &r.players[i];
    return nullptr;
}

std::string tower_partner_name(const NetRoom &r)
{
    const NetRoomPlayer *p = tower_player(r, r.you == 0 ? 1 : 0);
    return p ? std::string(p->name) : std::string();
}

// The floors this PC played (their CPUs): noted while a floor's match is set up (towers.c's online floor).
void tower_online_note_floor()
{
    TowersOnlineFloor tf;
    towers_online_view(&tf);
    if (!tf.on || tf.floor == TWO.played_floor[0]) return;
    TWO.played_floor[1] = TWO.played_floor[0];
    TWO.played_opp[1][0] = TWO.played_opp[0][0];
    TWO.played_opp[1][1] = TWO.played_opp[0][1];
    TWO.played_floor[0] = tf.floor;
    TWO.played_opp[0][0] = tf.opp[0];
    TWO.played_opp[0][1] = tf.opp[1];
}

// TOWER as the two-pads climb scene sees it (towers_scene): the floor just fought and the next one.
TowersView tower_online_scene_view(const TowerNetView &t)
{
    TowersView v;
    memset(&v, 0, sizeof v);
    v.tower = t.tower >= 1 && t.tower <= TOWER_COUNT ? t.tower : TOWER_TEAM;
    v.floors = t.floors;
    v.floor = t.floor;
    v.team = 1;
    v.last_floor = t.last_how == TOWERNET_HOW_WIN ? t.last_floor : 0;
    v.state = t.last_how == TOWERNET_HOW_WIN ? TOWERS_WON : t.last_how == TOWERNET_HOW_NONE ? TOWERS_ARMED : TOWERS_DRAW;
    v.last_opponent = v.last_opponent2 = -1;
    for (int k = 0; k < 2; k++)
        if (TWO.played_floor[k] == t.last_floor && t.last_floor > 0) {
            v.last_opponent = TWO.played_opp[k][0];
            v.last_opponent2 = TWO.played_opp[k][1];
        }
    if (v.last_opponent < 0) v.last_floor = 0;      // no card for a floor this PC did not see
    v.next_opponent = t.opp[0];
    v.next_opponent2 = t.opp[1];
    v.next_venue = t.venue;
    if (t.floor >= 1 && t.floor <= t.floors) towers_floor_rules(v.tower, t.floor, t.floors, &v.next_rules);
    v.online = 1;
    v.board = (t.flags & TOWERNET_FLAG_BOARD) != 0;
    v.best = t.beaten;
    return v;
}

std::string tower_clock(int secs)
{
    char b[16];
    snprintf(b, sizeof b, "%d:%02d", std::max(0, secs) / 60, std::max(0, secs) % 60);
    return b;
}

// The players' cards: the fighter, the name, READY (the continue vote between floors).
void tower_player_card(ImDrawList *dl, const Frame &f, const NetRoom &r, int slot, float x, float y, float w, float h,
                       int my_fighter, bool vote)
{
    const NetRoomPlayer *p = tower_player(r, slot);
    bool me = slot == r.you;
    ImU32 col = TOWER_COL[TOWER_TEAM - 1];
    glass(dl, f, x, y, w, h, 10, me ? IM_COL32(30, 24, 18, 255) : IM_COL32(12, 12, 15, 255), 0.85f);
    rect(dl, f, x, y, w, 4, with_alpha(col, p ? 0.9f : 0.3f), 2);
    text(dl, f, g_title, 10, x + w / 2, y + 10, GREY, slot == 0 ? "PLAYER 1" : "PLAYER 2", CENTER, 0.6f);
    if (!p) {
        float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 3.0);
        text(dl, f, g_title, 13, x + w / 2, y + h / 2 - 10, with_alpha(CREAM, 0.4f + 0.5f * pulse), "WAITING...", CENTER, 0.8f);
        return;
    }
    int fighter = me && !p->ready && my_fighter >= 0 ? my_fighter : p->fighter;
    if (fighter >= 0 && fighter != 255) {
        portrait(dl, f, fighter, x + 10, y + 26, w - 20, h - 96);
        frame_rect(dl, f, x + 10, y + 26, w - 20, h - 96, with_alpha(col, 0.7f), 1.5f);
    }
    std::string name = upper(p->name);
    text(dl, f, g_title, std::min(15.0f, fit_size(f, g_title, 15, name.c_str(), w - 12)), x + w / 2, y + h - 64,
         me ? CREAM : WHITE, name.c_str(), CENTER, 1.0f);
    if (fighter >= 0 && fighter != 255)
        text(dl, f, g_hand, 11, x + w / 2, y + h - 44, ORANGE, upper(fighter_label(fighter).c_str()).c_str(), CENTER, 0.6f, w - 12);
    const char *state = p->ready ? (vote ? "CONTINUE" : "READY") : vote ? "PRESS A" : "NOT READY";
    rect(dl, f, x + 12, y + h - 26, w - 24, 18, p->ready ? with_alpha(IM_COL32(40, 140, 60, 255), 0.9f) : IM_COL32(40, 40, 46, 230), 4);
    text(dl, f, g_title, 11, x + w / 2, y + h - 24, WHITE, state, CENTER, 0.6f);
}

ULONGLONG g_tower_leave_armed;
// B leaves (twice within 4 s while a climb is under way: a slip of the thumb never ends a run's session)
void tower_online_leave(const Pad &pad, const char *said, bool confirm = false)
{
    if (!pad.b) return;
    ULONGLONG now = GetTickCount64();
    if (confirm && !(g_tower_leave_armed && now - g_tower_leave_armed < 4000)) {
        g_tower_leave_armed = now;
        say(pad_text("Press {b} again to save the climb and leave the room.").c_str(), 4000);
        return;
    }
    g_tower_leave_armed = 0;
    netplay_lobby_leave();
    S.page = HUB;
    say(said, 6000);
}

void draw_tower_room(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r)
{
    TowerNetView t;
    bool have = towers_online_view_net(&t);
    const NetRoomPlayer *me = tower_player(r, r.you);
    bool host = r.you == r.host, ready = me && me->ready;
    std::string partner = tower_partner_name(r);
    ImU32 col = TOWER_COL[TOWER_TEAM - 1];
    int fighter = saved_fighter();
    if (fighter == NETPLAY_CREATED_FIGHTER || fighter >= 99) fighter = 55;      // standard fighters only
    char code[32];
    snprintf(code, sizeof(code), "CODE  %s", r.code);
    tower_online_note_floor();

    // between floors: the climb scene itself, with the vote over it
    if (have && t.state == TOWERNET_SUMMARY && r.player_count >= 2) {
        TowersView v = tower_online_scene_view(t);
        int key = t.last_floor * 16 + t.last_how;
        if (key != TWO.summary_key) { TWO.summary_key = key; TWO.summary_since = GetTickCount64(); }
        float st = (GetTickCount64() - TWO.summary_since) / 1000.0f;
        TowerRun run;
        memset(&run, 0, sizeof run);
        run.tower = v.tower;
        run.floors = v.floors;
        run.floor = v.floor;
        towers_scene(dl, f, ImGui::GetIO().DisplaySize, v, run, std::min(st, 3.5f), false);
        text(dl, f, g_hand, 13, 590, 62, CREAM, ("TEAM TOWER ONLINE  -  WITH " + upper(partner.c_str())).c_str(), RIGHT, 0.8f, 300);
        const char *head = t.last_how == TOWERNET_HOW_WIN ? "FLOOR CLEARED" : t.last_how == TOWERNET_HOW_DRAW ? "A DRAW: THIS FLOOR AGAIN"
                         : t.last_how == TOWERNET_HOW_VOID || t.last_how == TOWERNET_HOW_NORESULT ? "NO CONTEST: THIS FLOOR AGAIN"
                         : "NEXT FLOOR";
        // the vote strip at the foot
        glass(dl, f, 150, 404, 340, 40, 8, IM_COL32(12, 12, 15, 255), 0.9f);
        text(dl, f, g_title, 12, 162, 408, CREAM, head, LEFT, 0.8f);
        for (int s = 0; s < 2; s++) {
            const NetRoomPlayer *p = tower_player(r, s);
            std::string who = p ? upper(p->name) : std::string("-");
            float x = 162 + s * 160;
            rect(dl, f, x, 425, 10, 10, p && p->ready ? IM_COL32(60, 200, 90, 255) : IM_COL32(70, 70, 76, 255), 2);
            text(dl, f, g_title, 10, x + 16, 423, p && p->ready ? WHITE : GREY, who.c_str(), LEFT, 0.6f, 130);
        }
        text(dl, f, g_title, 22, 480, 410, t.secs <= 15 ? RED : CREAM, tower_clock(t.secs).c_str(), RIGHT, 1.2f);
        if (t.why[0]) text(dl, f, g_hand, 11, 320, 386, ORANGE, t.why, CENTER, 0.6f, 560);
        float hx = 54;
        hx += hint(dl, f, hx, 452, "a", ready ? "WAITING FOR " : "CONTINUE");
        if (ready) text(dl, f, g_title, 13, hx - 12, 453, WHITE, upper(partner.c_str()).c_str(), LEFT, 1.0f, 140), hx += 120;
        hint(dl, f, hx, 452, "b", "SAVE & QUIT");
        // the A that went through the results does not vote: the summary takes it after 2.5 s
        if (pad.a && !ready && st >= 2.5f) netplay_lobby_ready(1, me ? me->fighter : fighter);
        tower_online_leave(pad, "The climb is saved: the same pair resumes it from the Team Tower card.", true);
        return;
    }

    page_title(dl, f, "TEAM TOWER ONLINE");
    text(dl, f, g_title, 14, 590, 58, ORANGE, code, RIGHT);
    if (!have || r.player_count < 2) {
        // the waiting screen: the code, big
        glass(dl, f, 120, 96, 400, 230, 12, IM_COL32(14, 10, 12, 255), 0.85f);
        rect(dl, f, 120, 96, 400, 5, col, 2);
        bool away = have && t.state == TOWERNET_WAITING;
        text(dl, f, g_title, 15, 320, 112, CREAM, away ? "YOUR PARTNER LEFT THE ROOM" : r.settings.is_public ?
             "IN THE ROOM LIST FOR EVERYONE  -  OR GIVE THE CODE" : "TELL YOUR PARTNER THE CODE", CENTER, 1.0f);
        text(dl, f, g_title, 12, 320, 270, GOLD, upper(r.settings.name).c_str(), CENTER, 0.8f, 380);
        text(dl, f, g_stencil, 54, 320, 140, WHITE, r.code, CENTER, 1.6f);
        text(dl, f, g_hand, 12, 320, 212, ORANGE, r.settings.is_public ? "ON THEIR PC: FIND A ROOM, OR TOWERS > TEAM TOWER > RT ONLINE" : "ON THEIR PC: TOWERS > TEAM TOWER > RT ONLINE > JOIN BY CODE", CENTER, 0.8f, 380);
        if (away) {
            std::string line = "Waiting for them to come back with the code: the climb is saved in " + tower_clock(t.secs) + ".";
            text(dl, f, g_hand, 13, 320, 240, CREAM, line.c_str(), CENTER, 0.8f, 380);
        } else {
            float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 3.0);
            text(dl, f, g_title, 13, 320, 246, with_alpha(CREAM, 0.4f + 0.6f * pulse), "WAITING FOR YOUR PARTNER...", CENTER, 0.8f);
        }
        text(dl, f, g_hand, 11, 320, 298, GREY, "Both players sign in with their own FFNY Online account. Standard fighters only.",
             CENTER, 0.6f, 380);
        float hx = 240;
        hint(dl, f, hx, 392, "b", "LEAVE ROOM");
        tower_online_leave(pad, away ? "You left the room: the climb is saved when your partner does not come back." : "Room closed.");
        return;
    }

    // the two cards, the climb on the right
    float w = 120, top = 86, ch = 262;
    bool vote = t.state == TOWERNET_SUMMARY;
    tower_player_card(dl, f, r, 0, 52, top, w, ch, r.you == 0 ? fighter : -1, vote);
    tower_player_card(dl, f, r, 1, 52 + w + 8, top, w, ch, r.you == 1 ? fighter : -1, vote);
    text(dl, f, g_stencil, 16, 52 + w + 4, top + 112, CREAM, "+", CENTER, 1.4f);
    float px = 316, pw = 274;
    glass(dl, f, px, top, pw, ch, 10, IM_COL32(14, 10, 12, 255), 0.85f);
    rect(dl, f, px, top, 4, ch, col, 2);
    text(dl, f, g_title, 18, px + 16, top + 12, CREAM, "TEAM TOWER", LEFT, 1.2f);
    text(dl, f, g_hand, 12, px + 16, top + 36, ORANGE, "2 VS 2 AGAINST THE CPU PAIRS, FLOOR BY FLOOR", LEFT, 0.6f, pw - 30);
    std::string line, big;
    bool can_ready = false;
    switch (have ? t.state : -1) {
    case TOWERNET_SELECT: {
        bool saved = (t.flags & TOWERNET_FLAG_SAVED) != 0, resume = saved && (t.flags & TOWERNET_FLAG_RESUME);
        char l[96];
        if (saved) {
            snprintf(l, sizeof l, "SAVED CLIMB: FLOOR %d", t.saved_floor);
            text(dl, f, g_title, 15, px + 16, top + 64, GOLD, l, LEFT, 1.0f);
            text(dl, f, g_title, 13, px + 16, top + 88, resume ? WHITE : GREY, resume ? "> RESUME ON FLOOR" : "  RESUME", LEFT, 0.8f);
            text(dl, f, g_title, 13, px + 16, top + 106, resume ? GREY : WHITE, resume ? "  NEW CLIMB" : "> NEW CLIMB FROM FLOOR 1", LEFT, 0.8f);
            text(dl, f, g_hand, 11, px + 16, top + 130, GREY, resume ? "Your fighters stay the climb's." :
                 "A new climb ends the saved one.", LEFT, 0.6f, pw - 30);
        } else {
            text(dl, f, g_title, 15, px + 16, top + 64, WHITE, "A NEW CLIMB FROM FLOOR 1", LEFT, 1.0f);
        }
        text(dl, f, g_hand, 11, px + 16, top + 160, CREAM, "Each floor: two CPUs, the venue and the CPU level the server deals. "
             "A loss ends the climb; leave between floors and it is saved for the two of you.", LEFT, 0.6f, pw - 30);
        line = ready ? "Ready: the climb starts when both are." : "Pick your fighter and ready up.";
        can_ready = true;
        if (host && saved && pad.x && !ready) netplay_lobby_tower_choice(resume ? TOWERNET_CHOICE_NEW : TOWERNET_CHOICE_RESUME);
        break;
    }
    case TOWERNET_STARTING:
    case TOWERNET_PLAYING: {
        float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 4.0);
        char l[64];
        snprintf(l, sizeof l, t.floor > 0 ? "FLOOR %d OF %d" : "THE CLIMB", t.floor, t.floors);
        text(dl, f, g_title, 15, px + 16, top + 64, WHITE, l, LEFT, 1.0f);
        line = t.state == TOWERNET_STARTING ? "The server deals the floor..." : "The floor is on...";
        text(dl, f, g_title, 12, px + 16, top + 92, with_alpha(CREAM, 0.4f + 0.6f * pulse), upper(line.c_str()).c_str(), LEFT, 0.8f);
        line.clear();
        break;
    }
    case TOWERNET_OVER: {
        bool cleared = !strcmp(t.end, "cleared"), saved = !strcmp(t.end, "saved");
        big = cleared ? "TEAM TOWER CLEARED" : saved ? "CLIMB SAVED" : "KNOCKED OUT";
        char l[64];
        snprintf(l, sizeof l, "%d FLOORS BEATEN", t.beaten);
        text(dl, f, g_stencil, 22, px + 16, top + 60, cleared ? GOLD : saved ? CREAM : RED, big.c_str(), LEFT, 1.2f);
        text(dl, f, g_title, 15, px + 16, top + 94, WHITE, l, LEFT, 1.0f);
        text(dl, f, g_hand, 12, px + 16, top + 120, CREAM, t.why, LEFT, 0.6f, pw - 30);
        if (t.flags & TOWERNET_FLAG_BOARD)
            text(dl, f, g_hand, 11, px + 16, top + 200, GOLD, ("On the Team Tower board: " + upper(me ? me->name : "") + " + " +
                 upper(partner.c_str())).c_str(), LEFT, 0.6f, pw - 30);
        line = host ? "X: a new climb with the same partner." : "The host can start a new climb.";
        big.clear();
        break;
    }
    default:
        line = "Connecting the climb...";
    }
    if (have && t.why[0] && t.state != TOWERNET_OVER) text(dl, f, g_hand, 12, 320, 370, CREAM, t.why, CENTER, 0.6f, 540);
    if (!line.empty()) {
        float ls = fit_size(f, g_hand, 15, line.c_str(), 540);
        text(dl, f, g_hand, ls, 320, 354, ORANGE, line.c_str(), CENTER, 0.8f);
    }
    float hx = 54, hy = 392;
    if (can_ready && !ready) hx += hint(dl, f, hx, hy, "a", "READY");
    if (can_ready && !ready) hx += hint(dl, f, hx, hy, "y", "CHOOSE FIGHTER");
    if (can_ready && host && have && (t.flags & TOWERNET_FLAG_SAVED) && !ready)
        hx += hint(dl, f, hx, hy, "x", (t.flags & TOWERNET_FLAG_RESUME) ? "NEW CLIMB" : "RESUME");
    if (have && t.state == TOWERNET_OVER && host) hx += hint(dl, f, hx, hy, "x", "NEW CLIMB");
    hx += hint(dl, f, hx, hy, "rb", "VOICE");
    hint(dl, f, hx, hy, "b", "LEAVE ROOM");
    if (pad.a && can_ready && !ready) netplay_lobby_ready(1, fighter);
    if (pad.x && have && t.state == TOWERNET_OVER && host) netplay_lobby_tower_choice(TOWERNET_CHOICE_NEW);
    if (pad.y && can_ready && !ready) {
        S.page = FIGHTERS;
        S.fighter_sel = grid_pos(grid_fighters(), fighter);
    }
    if (pad.rb) { S.page = VOICE; S.voice_sel = 0; }
    tower_online_leave(pad, have && t.floor > 1 ? "You left the room: the climb is saved for the two of you." :
                       "You left the Team Tower room.");
}

// ---- the Team Tower card's ONLINE (TOWERS page, RT) ----

struct TowerOnlineCard {
    bool open = false;
    int sel = 0;                    // HOST PUBLIC, HOST PRIVATE, FIND A ROOM, JOIN BY CODE
    bool resume = true;             // with a saved climb: resume it (else a new one)
    std::atomic<int> fetching{0};   // 1 asking the service for the saved climb, 2 answered
    std::mutex mu;
    bool saved = false;
    int floor = 0, floors = 0;
    std::string partner;
} TOC;

void tower_online_fetch_saved()
{
    if (TOC.fetching.exchange(1) == 1) return;
    std::thread([]() {
        static char reply[1024];
        int st = account_call("GET", "/api/towers/online?format=text", NULL, reply, (int)sizeof reply, 6000, NULL);
        reply[sizeof reply - 1] = 0;
        std::lock_guard<std::mutex> g(TOC.mu);
        TOC.saved = st == 200 && strstr(reply, "saved=1") != nullptr;
        const char *p = strstr(reply, "partner=");
        TOC.partner.clear();
        if (p) { p += 8; while (*p && *p != '\n' && *p != '\r') TOC.partner += *p++; }
        p = strstr(reply, "floor=");
        TOC.floor = p ? atoi(p + 6) : 0;
        p = strstr(reply, "floors=");
        TOC.floors = p ? atoi(p + 7) : 0;
        fprintf(stderr, "[TOWERS] online card: %s\n", TOC.saved ? ("a saved climb on floor " + std::to_string(TOC.floor) +
                " with " + TOC.partner).c_str() : st == 200 ? "no saved climb" : "the service did not answer");
        TOC.fetching = 2;
    }).detach();
}

void tower_online_open_card()
{
    TOC.open = true;
    TOC.sel = 0;
    TOC.resume = true;
    TOC.fetching = 0;
    if (!gallery_on() && towers_signed_in()) tower_online_fetch_saved();
}

void tower_online_host(bool is_public, bool resume)
{
    NetRoomSettings s;
    memset(&s, 0, sizeof s);
    snprintf(s.name, sizeof s.name, "Team Tower");      // the relay names it (the host, NEW CLIMB or the floor)
    s.is_public = is_public ? 1 : 0;
    s.mode = NETPLAY_MODE_TOWER;
    s.max = 2;
    s.rounds = resume ? 1 : 0;      // relay/tower.go TowerRoundsResume
    s.pool_count = 1;
    s.pool[0] = 1;
    netplay_lobby_create(&s);
    fprintf(stderr, "[TOWERS] online: hosting a %s Team Tower room (%s)\n", is_public ? "public" : "private",
            resume ? "resume the saved climb" : "a new climb");
}

// Drawn over the TOWERS page while open; 1 when it took the input.
bool tower_online_card(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    if (!TOC.open) return false;
    ImU32 col = TOWER_COL[TOWER_TEAM - 1];
    float x = 120, y = 70, w = 400, h = 300;
    glass(dl, f, x, y, w, h, 12, IM_COL32(14, 10, 12, 255), 0.95f);
    rect(dl, f, x, y, w, 5, col, 2);
    text(dl, f, g_stencil, 24, x + w / 2, y + 14, CREAM, "TEAM TOWER ONLINE", CENTER, 1.4f);
    text(dl, f, g_hand, 12, x + w / 2, y + 46, ORANGE, "CLIMB TOGETHER ON TWO PCS: 2 VS 2 AGAINST THE CPU PAIRS", CENTER, 0.6f, w - 30);
    bool signed_now = gallery_on() || towers_signed_in();
    bool online = gallery_on() || netplay_in_lobby();
    bool saved;
    int floor;
    std::string partner;
    {
        std::lock_guard<std::mutex> g(TOC.mu);
        saved = TOC.saved;
        floor = TOC.floor;
        partner = TOC.partner;
    }
    float ry = y + 72;
    if (saved) {    // the climb this room is for
        std::string a = "RESUME FLOOR " + std::to_string(floor) + " WITH " + upper(partner.c_str());
        glass(dl, f, x + 20, ry, w - 40, 30, 6, IM_COL32(40, 30, 14, 255), 0.85f);
        text(dl, f, g_title, 12, x + 32, ry + 8, GOLD, "<", LEFT, 0.6f);
        text(dl, f, g_title, 12, x + w - 32, ry + 8, GOLD, ">", RIGHT, 0.6f);
        text(dl, f, g_title, std::min(13.0f, fit_size(f, g_title, 13, a.c_str(), w - 90)), x + w / 2, ry + 7,
             TOC.resume ? GOLD : CREAM, TOC.resume ? a.c_str() : "NEW CLIMB FROM FLOOR 1", CENTER, 0.8f);
        if (pad.left || pad.right) TOC.resume = !TOC.resume;
        ry += 40;
    } else if (TOC.fetching.load() == 1) {
        text(dl, f, g_hand, 11, x + w / 2, ry + 6, GREY, "Looking for your saved climb...", CENTER, 0.6f);
        ry += 26;
    }
    static const char *const rows[] = { "HOST A PUBLIC ROOM", "HOST A PRIVATE ROOM", "FIND A ROOM", "JOIN BY CODE" };
    static const char *const sub[] = { "In the room list for everyone: your partner picks it there.",
                                       "Join by code only: tell your partner the code.",
                                       "The Team Tower rooms are in the room list.",
                                       "Your partner's code, six characters." };
    if (pad.up) TOC.sel = (TOC.sel + 3) % 4;
    if (pad.down) TOC.sel = (TOC.sel + 1) % 4;
    for (int i = 0; i < 4; i++) {
        float yy = ry + i * 40;
        bool sel = i == TOC.sel;
        if (mouse_pick(f, x + 20, yy, w - 40, 34)) TOC.sel = i;
        glass(dl, f, x + 20, yy, w - 40, 34, 6, sel ? IM_COL32(44, 32, 22, 255) : IM_COL32(10, 10, 13, 255), sel ? 0.85f : 0.6f);
        if (sel) selector(dl, f, x + 20, yy, w - 40, 34);
        text(dl, f, g_title, 14, x + 34, yy + 3, sel ? CREAM : GREY, rows[i], LEFT, 0.8f);
        text(dl, f, g_hand, 10, x + 34, yy + 20, with_alpha(ORANGE, sel ? 1.0f : 0.6f), sub[i], LEFT, 0.6f, w - 70);
        if (mouse_click(f, x + 20, yy, w - 40, 34)) const_cast<Pad &>(pad).a = true;
    }
    const char *why = !signed_now ? "Sign in first (Y in the hub): online climbs need your FFNY Online account." :
                      !online ? "Not connected to FFNY Online: open the hub's online tiles first." : nullptr;
    if (why) text(dl, f, g_hand, 11, x + w / 2, y + h - 40, RED, why, CENTER, 0.6f, w - 30);
    else text(dl, f, g_hand, 10, x + w / 2, y + h - 40, GREY, "Both sign in with their own account. Standard fighters only. "
              "A saved climb resumes with the same partner only.", CENTER, 0.6f, w - 30);
    float hx = x + 90;
    hx += hint(dl, f, hx, y + h - 20, "a", "OK");
    hint(dl, f, hx, y + h - 20, "b", "BACK");
    if (pad.b) { TOC.open = false; return true; }
    if (pad.a && !gallery_on()) {
        if (why) { say(why, 6000); return true; }
        TOC.open = false;
        switch (TOC.sel) {
        case 0: case 1:
            tower_online_host(TOC.sel == 0, saved && TOC.resume);
            say(TOC.sel == 0 ? "Opening a public Team Tower room..." : "Opening a private Team Tower room...");
            S.page = HUB;
            break;
        case 2:
            S.page = ROOMS;
            netplay_lobby_list(0xFF);
            S.last_list = GetTickCount64();
            break;
        default:
            S.page = CODE;
            S.code_pos = 0;
        }
    }
    return true;
}
