// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Tournament sets: the room of a tournament match (relays answering CAPS bit 12, netplay.h NetTSet). The
// relay runs the set by the tournament's rules and sends TSET after every ROOM; this page only shows it
// and sends the player's choices (READY with the fighter, the loser's stage pick, VOTE 0 to start a game
// sooner). It takes the place of the room page while the room is in its lobby or shows the next game's
// venue; the match itself and the results go as in every room. Included by online_screen.cpp after
// ranked_ui.inc (slot_name, the colours, venue_tile).
//
// Pad: A READY / NOT READY (or the stage, when this player picks), X CHOOSE FIGHTER (not while the fighter
// is locked), the stick or the mouse over the stages, B LEAVE (twice: the set's DQ timer runs meanwhile).
//
// Gallery pages (RECOMP_TEST_ONLINE_GALLERY with RECOMP_TEST_ONLINE_GALLERY_ONLY=tset or hub-tset):
// hub-tset (TOURNAMENT MATCH on the hub), tset-coin (game 1, the other player selects first), tset-select
// (game 2, this player's fighter locked), tset-why (a draw replayed), tset-pick (this player picks, Dave's
// rule), tset-pick-wait (the other player picks), tset-intro (the venue before START), tset-waiting (the
// other side is missing: the DQ clock), tset-over (the set's result), tset-grid (CHOOSE FIGHTER in a set of
// stock fighters only: the created fighters left out).

struct TSetUi {
    bool gallery_entry = false;     // the gallery: TOURNAMENT MATCH on the sample hub
    bool sample_on = false;         // the gallery: the sample set below is this room's
    NetTSet sample = {};
    int pick_sel = 0;
    bool asked = false;             // TOURNAMENT MATCH pressed: say the next refusal
    unsigned refusal_seen = 0;
    unsigned long long leave_armed = 0;
    unsigned secs_key = 0;          // the countdown (TSET's secs as of its arrival)
    unsigned long long secs_at = 0;
    int secs0 = 0;
} TS;

const ImU32 TSET_GOLD = IM_COL32(255, 205, 60, 255), TSET_BLUE = IM_COL32(118, 170, 255, 255);

// This account's open tournament match, from the account service (GET /api/me/tournament, build 66):
// "match=<id>" and "state=ready|live|none" lines. Asked in the background while the hub is up.
struct TSetMine {
    std::mutex mu;
    std::atomic<int> busy{0};
    unsigned long long at = 0;      // last asked
    int status = 0;                 // its HTTP status (0 not asked yet or unreachable)
    long long match = 0;            // the open match (0 none)
} TM;

void tset_fetch_mine()
{
    if (gallery_on() || TM.busy.exchange(1)) return;
    TM.at = GetTickCount64();
    std::thread([]() {
        char buf[256] = {};
        int status = account_call("GET", "/api/me/tournament", nullptr, buf, (int)sizeof(buf), 0, nullptr);
        long long match = 0;
        if (status == 200) {
            const char *m = strstr(buf, "match=");
            if (m) match = atoll(m + 6);
        }
        int before;
        {
            std::lock_guard<std::mutex> lock(TM.mu);
            before = TM.status;
            if (status > 0) { TM.status = status; TM.match = status == 200 ? match : 0; }   // unreachable: keep the last answer
        }
        if (status != before || match)
            fprintf(stderr, "[ONLINE] tournament match: %lld (HTTP %d)\n", match, status);
        TM.busy = 0;
    }).detach();
}

// TOURNAMENT MATCH shows on the hub while signed in on a relay that offers tournament sets, and only
// when this player has a match ready or running (build 66: before, everyone saw it and 68 presses on
// 2026-11-08 got "You have no tournament match ready."). The hub asks every 30 seconds (every five
// minutes while the service does not answer). An account service older than build 66 (404) cannot say,
// so the entry shows as before.
bool tset_shown()
{
    if (gallery_on()) return TS.gallery_entry;
    int caps = lobby_caps();
    if (acc_state() != ACCOUNT_IN) {      // signed out: the next account asks afresh
        std::lock_guard<std::mutex> lock(TM.mu);
        TM.status = 0;
        TM.match = 0;
        TM.at = 0;
        return false;
    }
    if (!(caps > 0 && (caps & NETPLAY_CAP_TSET))) return false;
    int status;
    long long match;
    {
        std::lock_guard<std::mutex> lock(TM.mu);
        status = TM.status;
        match = TM.match;
    }
    unsigned long long now = GetTickCount64(), every = status == 0 || status == 200 || status == 404 ? 30000ull : 300000ull;
    if (!TM.at || now - TM.at > every) tset_fetch_mine();
    return status == 404 || (status == 200 && match > 0);
}

// The hub's entry: TJOIN 0 ("my match that is ready"); the relay's refusal is said when it comes.
void tset_join()
{
    TS.asked = true;
    TS.refusal_seen = netplay_lobby_tset_refusal(nullptr, 0);
    netplay_lobby_tset_join(0);
    say("Looking for your tournament match...", 8000);
}

void tset_poll()
{
    if (gallery_on() || !TS.asked) return;
    char why[80];
    unsigned seq = netplay_lobby_tset_refusal(why, sizeof(why));
    if (seq != TS.refusal_seen) {
        TS.refusal_seen = seq;
        TS.asked = false;
        say(why[0] ? why : "No tournament match is ready for you.", 8000);
    }
}

bool tset_room(const NetRoom &r, NetTSet *out)
{
    if (gallery_on()) {
        if (!TS.sample_on) return false;
        *out = TS.sample;
        return true;
    }
    if (!r.in_room) return false;
    if (netplay_lobby_tset(out)) { TS.asked = false; return true; }
    return false;
}

// Created fighters in this room: not in a set of stock fighters only (TSET's flags). The grid leaves them
// out there (grid_fighters): READY with one was refused and the relay swapped it (build 63).
bool tset_created_ok(const NetRoom &r)
{
    NetTSet t;
    return !(tset_room(r, &t) && (t.flags & NETTSET_STOCK_ONLY));
}

// TSET's secs, counted down here from its arrival (the relay sends it only with a ROOM).
int tset_secs(const NetTSet &t)
{
    if (gallery_on()) return t.secs;
    unsigned key = (unsigned)t.state * 977u + (unsigned)t.game * 31u + (unsigned)t.secs;
    if (key != TS.secs_key || t.received != TS.secs_at) { TS.secs_key = key; TS.secs_at = t.received; TS.secs0 = t.secs; }
    int gone = (int)((GetTickCount64() - TS.secs_at) / 1000);
    return std::max(0, TS.secs0 - gone);
}

std::string tset_clock(int secs)
{
    char b[16];
    if (secs >= 60) snprintf(b, sizeof(b), "%d:%02d", secs / 60, secs % 60);
    else snprintf(b, sizeof(b), "%d", secs);
    return b;
}

std::string tset_name(const NetRoom &r, int slot)
{
    if (slot == r.you) return "You";
    const char *n = slot_name(r, slot);
    return n && n[0] ? n : (slot == 0 ? "Player 1" : "Player 2");
}

const NetRoomPlayer *tset_player(const NetRoom &r, int slot)
{
    for (int i = 0; i < r.player_count; i++) if (r.players[i].slot == slot) return &r.players[i];
    return nullptr;
}

// The red-gold pill under the title: TOURNAMENT - BEST OF 3 - GAME 2 - 1-0 (this player's side first).
void tset_banner(ImDrawList *dl, const Frame &f, const NetRoom &r, const NetTSet &t, float y)
{
    int me = r.you == 1 ? 1 : 0;
    char line[96];
    if (t.state == NETTSET_OVER)
        snprintf(line, sizeof(line), "TOURNAMENT  -  BEST OF %d  -  FINAL %d-%d", t.best_of, t.wins[me], t.wins[1 - me]);
    else
        snprintf(line, sizeof(line), "TOURNAMENT  -  BEST OF %d  -  GAME %d  -  SET %d-%d", t.best_of, t.game, t.wins[me],
                 t.wins[1 - me]);
    float w = text_width(f, g_title, 13, line) + 24;
    rect(dl, f, 320 - w / 2, y, w, 22, IM_COL32(96, 64, 6, 235), 6);
    frame_rect(dl, f, 320 - w / 2, y, w, 22, with_alpha(TSET_GOLD, 0.8f), 1.2f);
    text(dl, f, g_title, 13, 320, y + 4, WHITE, line, CENTER, 1.0f);
}

std::string tset_rules_line(const NetTSet &t)
{
    std::string s;
    auto add = [&](const char *w) { s += (s.empty() ? "" : "  -  "); s += w; };
    if (t.flags & NETTSET_LOSER_PICKS) add("LOSER PICKS THE STAGE");
    if (t.flags & NETTSET_DAVES) add("DAVE'S RULE");
    if (t.flags & NETTSET_WINNER_LOCK) add("WINNER KEEPS THE FIGHTER");
    if (t.flags & NETTSET_STOCK_ONLY) add("STOCK FIGHTERS ONLY");
    return s;
}

// The set panel: the score, the coin toss, each game played (the stage, who won, both fighters).
void tset_panel(ImDrawList *dl, const Frame &f, const NetRoom &r, const NetTSet &t, float x, float y, float w, float h)
{
    glass(dl, f, x, y, w, h, 10, IM_COL32(16, 12, 6, 255), 0.72f);
    std::string round = upper(t.round[0] ? t.round : "Tournament match");
    float rs = fit_size(f, g_title, 15, round.c_str(), w - 24);
    text(dl, f, g_title, rs, x + w / 2, y + 8, TSET_GOLD, round.c_str(), CENTER, 1.0f);
    // the score, side 1 left
    std::string n0 = upper(tset_name(r, 0).c_str()), n1 = upper(tset_name(r, 1).c_str());
    float ns0 = fit_size(f, g_title, 14, n0.c_str(), w / 2 - 40), ns1 = fit_size(f, g_title, 14, n1.c_str(), w / 2 - 40);
    text(dl, f, g_title, ns0, x + w / 2 - 34, y + 36, r.you == 0 ? ORANGE : CREAM, n0.c_str(), RIGHT, 1.0f);
    text(dl, f, g_title, ns1, x + w / 2 + 34, y + 36, r.you == 1 ? ORANGE : CREAM, n1.c_str(), LEFT, 1.0f);
    char score[16];
    snprintf(score, sizeof(score), "%d-%d", t.wins[0], t.wins[1]);
    text(dl, f, g_stencil, 26, x + w / 2, y + 28, WHITE, score, CENTER, 1.4f);
    char need[48];
    snprintf(need, sizeof(need), "FIRST TO %d", t.best_of / 2 + 1);
    text(dl, f, g_title, 10, x + w / 2, y + 60, GREY, need, CENTER, 0.6f);
    float ly = y + 78;
    if ((t.flags & NETTSET_COIN_TOSS) && t.first != NETTSET_NONE && t.game <= 1 && t.state != NETTSET_OVER) {
        std::string c = "COIN TOSS: " + upper(tset_name(r, t.first).c_str()) + (t.first == r.you ? " PICK FIRST" : " PICKS FIRST");
        float cw = text_width(f, g_title, 10, c.c_str()) + 14;
        rect(dl, f, x + w / 2 - cw / 2, ly, cw, 16, IM_COL32(0, 0, 0, 170), 4);
        text(dl, f, g_title, 10, x + w / 2, ly + 2, TSET_GOLD, c.c_str(), CENTER, 1.0f);
        ly += 22;
    }
    // the games, the latest at the bottom (as many as fit)
    int rows = std::max(1, (int)((y + h - 30 - ly) / 26));
    int from = std::max(0, t.game_count - rows);
    if (!t.game_count)
        text(dl, f, g_hand, 13, x + w / 2, ly + 6, with_alpha(ORANGE, 0.7f), "No game played yet.", CENTER, 0.8f);
    for (int i = from; i < t.game_count; i++) {
        const NetTSetGame &g = t.games[i];
        float gy = ly + (i - from) * 26;
        rect(dl, f, x + 10, gy, w - 20, 23, IM_COL32(0, 0, 0, 120), 5);
        char num[8];
        snprintf(num, sizeof(num), "G%d", i + 1);
        text(dl, f, g_title, 11, x + 16, gy + 5, GREY, num, LEFT, 0.6f);
        std::string venue = upper(online_venue_name(g.venue));
        float vs = fit_size(f, g_title, 10, venue.c_str(), 92);
        text(dl, f, g_title, vs, x + 40, gy + 2, CREAM, venue.c_str(), LEFT, 0.6f);
        std::string fighters = upper(fighter_label(g.fighters[0], 0).c_str()) + " V " + upper(fighter_label(g.fighters[1], 1).c_str());
        float fs = fit_size(f, g_title, 8, fighters.c_str(), 120);
        text(dl, f, g_title, fs, x + 40, gy + 13, GREY, fighters.c_str(), LEFT, 0.5f);
        std::string res = g.how == NETTSET_DRAW || g.winner == NETTSET_NONE ? "DRAW (REPLAYED)"
                        : upper(tset_name(r, g.winner).c_str()) + (g.winner == r.you ? " WON" : " WON");
        if (g.how == NETTSET_LEFT) res += " (LEFT)";
        if (g.how == NETTSET_FORFEIT) res += " (FORFEIT)";
        float rsz = fit_size(f, g_title, 10, res.c_str(), 110);
        text(dl, f, g_title, rsz, x + w - 16, gy + 6, g.winner == r.you ? GREEN : g.how == NETTSET_DRAW ? GREY : CREAM,
             res.c_str(), RIGHT, 0.6f);
    }
    std::string rules = tset_rules_line(t);
    if (!rules.empty()) {
        float s = fit_size(f, g_title, 9, rules.c_str(), w - 20);
        text(dl, f, g_title, s, x + w / 2, y + h - 18, with_alpha(TSET_GOLD, 0.75f), rules.c_str(), CENTER, 0.5f);
    }
}

// A player's card: the portrait (the locked fighter, the confirmed one, or this PC's pick), the name, READY.
void tset_card(ImDrawList *dl, const Frame &f, const NetRoom &r, const NetTSet &t, int slot, float x, float y, float w, float h,
               int my_fighter)
{
    const NetRoomPlayer *p = tset_player(r, slot);
    glass(dl, f, x, y, w, h, 10, slot == r.you ? IM_COL32(44, 30, 20, 255) : IM_COL32(10, 10, 13, 255), 0.62f);
    char side[16];
    snprintf(side, sizeof(side), "SIDE %d", slot + 1);
    auto side_tag = [&]() {     // over the portrait
        float sw = text_width(f, g_title, 10, side);
        rect(dl, f, x + 5, y + 5, sw + 10, 15, IM_COL32(0, 0, 0, 180), 4);
        text(dl, f, g_title, 10, x + 10, y + 7, slot == 0 ? CREW_RED : CREW_BLUE, side, LEFT, 1.0f);
    };
    if (!p) {
        side_tag();
        text(dl, f, g_hand, 15, x + w / 2, y + 96, with_alpha(ORANGE, 0.7f), "NOT HERE", CENTER, 0.8f);
        if (t.state == NETTSET_WAITING) {
            std::string dq = "DQ IN " + tset_clock(tset_secs(t));
            text(dl, f, g_title, 13, x + w / 2, y + 122, RED, dq.c_str(), CENTER, 0.8f);
        }
        return;
    }
    int lock = slot < 2 ? t.locks[slot] : NETTSET_NONE;
    int shown = lock != NETTSET_NONE ? lock : p->fighter != 0xFF ? p->fighter : slot == r.you ? my_fighter : -1;
    // the other player's fighter is hidden until their READY is in (the relay shows it after it)
    float ph = (w - 8) * 1.5f + 4;
    if (shown < 0 || !portrait(dl, f, shown, x + 4, y + 4, w - 8, std::min(ph, h - 80)))
        text(dl, f, g_title, 40, x + w / 2, y + 70, GREY, "?", CENTER);
    side_tag();
    ImU32 nc = slot == r.you ? ORANGE : CREAM;
    float ns = fit_size(f, g_hand, 15, p->name, w - 8);
    text(dl, f, g_hand, ns, x + w / 2, y + h - 74, nc, p->name, CENTER, 1.0f);
    std::string label = shown >= 0 ? fighter_label(shown, slot) : "choosing";
    float ls = fit_size(f, g_title, 11, label.c_str(), w - 10);
    text(dl, f, g_title, ls, x + w / 2, y + h - 52, GREY, label.c_str(), CENTER, 0.6f);
    if (lock != NETTSET_NONE) {
        const char *tag = "LOCKED";
        float tw = text_width(f, g_title, 9, tag);
        rect(dl, f, x + w - tw - 16, y + 5, tw + 10, 14, IM_COL32(0, 0, 0, 180), 4);
        text(dl, f, g_title, 9, x + w - 11, y + 7, TSET_GOLD, tag, RIGHT, 1.0f);
    }
    if (t.state == NETTSET_SELECT && t.order == slot && !p->ready) {
        rect(dl, f, x + 10, y + h - 32, w - 20, 22, IM_COL32(96, 64, 6, 235), 5);
        text(dl, f, g_title, 12, x + w / 2, y + h - 29, WHITE, "SELECTS FIRST", CENTER, 0.8f);
    } else if (p->ready && t.state != NETTSET_OVER) {
        rect(dl, f, x + 10, y + h - 32, w - 20, 22, with_alpha(RED, 0.9f), 5);
        text(dl, f, g_title, 15, x + w / 2, y + h - 30, WHITE, "READY", CENTER, 0.8f);
    }
}

// The loser's stage pick: the venues the relay offers, with the countdown.
void tset_pick_grid(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const NetTSet &t)
{
    int n = t.choice_count;
    if (n <= 0) return;
    int per = n <= 6 ? n : n <= 12 ? (n + 1) / 2 : 6, rowsn = (n + per - 1) / per;
    if (TS.pick_sel >= n) TS.pick_sel = 0;
    if (pad.left) TS.pick_sel = (TS.pick_sel + n - 1) % n;
    if (pad.right) TS.pick_sel = (TS.pick_sel + 1) % n;
    if (pad.up && TS.pick_sel >= per) TS.pick_sel -= per;
    if (pad.down && TS.pick_sel + per < n) TS.pick_sel += per;
    float gap = 6, w = std::min(rowsn > 2 ? 84.0f : 112.0f, (540 - (per - 1) * gap) / per), h = (w - 6) * 0.75f + 20;
    float top = 124;
    for (int i = 0; i < n; i++) {
        int v = t.choices[i], row = i / per, col = i % per, inrow = std::min(per, n - row * per);
        float x = 320 - (inrow * w + (inrow - 1) * gap) / 2 + col * (w + gap), y = top + row * (h + gap);
        if (mouse_pick(f, x, y, w, h)) TS.pick_sel = i;
        bool sel = i == TS.pick_sel;
        venue_tile(dl, f, v, x, y, w, h, sel ? 1.0f : 0.7f, sel);
        if (sel) selector(dl, f, x, y, w, h);
        if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    // Dave's rule: where this player already won is not offered
    if (t.flags & NETTSET_DAVES) {
        std::string out;
        for (int i = 0; i < t.game_count; i++) {
            const NetTSetGame &g = t.games[i];
            if (g.winner != r.you || g.how == NETTSET_DRAW) continue;
            bool listed = false;
            for (int k = 0; k < n; k++) if (t.choices[k] == g.venue) listed = true;
            std::string name = upper(online_venue_name(g.venue));
            if (!listed && out.find(name) == std::string::npos) out += (out.empty() ? "" : ", ") + name;
        }
        if (!out.empty()) {
            std::string line = "DAVE'S RULE: NOT " + out + " (YOU WON THERE)";
            float s = fit_size(f, g_title, 10, line.c_str(), 540);
            text(dl, f, g_title, s, 320, top + rowsn * (h + gap) + 2, with_alpha(TSET_GOLD, 0.85f), line.c_str(), CENTER, 0.6f);
        }
    }
}

void tset_leave(const Pad &pad, bool over)
{
    if (!pad.b) return;
    unsigned long long now = GetTickCount64();
    if (over || (TS.leave_armed && now - TS.leave_armed < 4000)) {
        TS.leave_armed = 0;
        netplay_lobby_leave();
        S.page = HUB;
        say(over ? "You left the room." : "You left the set's room: come back before the DQ timer runs out.", 6000);
        return;
    }
    TS.leave_armed = now;
    say(pad_text("Press {b} again to leave. Out of the room, the set's DQ timer runs.").c_str(), 4000);
}

// The tournament room while it is in its lobby (r.phase 0).
void draw_tset(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const NetTSet &t)
{
    bool watcher = r.you == NETPLAY_SLOT_SPECTATOR;
    const NetRoomPlayer *me = watcher ? nullptr : tset_player(r, r.you);
    int other = r.you == 0 ? 1 : 0;
    const NetRoomPlayer *opp = tset_player(r, other);
    int lock = !watcher && r.you < 2 ? t.locks[r.you] : NETTSET_NONE;
    int fighter = lock != NETTSET_NONE ? lock : saved_fighter();
    // stock fighters only: a created fighter chosen before plays as the default fighter here (the grid
    // offers no created fighter in this set, so READY is never refused for it)
    if (fighter == NETPLAY_CREATED_FIGHTER && (t.flags & NETTSET_STOCK_ONLY)) fighter = 55;
    bool ready = me && me->ready;
    bool picking = t.state == NETTSET_PICK && t.picker == r.you && !watcher;
    page_title(dl, f, picking ? "PICK THE STAGE" : upper(r.settings.name).c_str());
    tset_banner(dl, f, r, t, 50);
    char code[32];
    snprintf(code, sizeof(code), "CODE  %s", r.code);
    text(dl, f, g_title, 14, 590, 58, ORANGE, code, RIGHT);

    std::string line;       // what happens now, under the cards
    if (picking) {
        int left = tset_secs(t);
        text(dl, f, g_title, 34, 600, 22, left <= 5 ? RED : CREAM, tset_clock(left).c_str(), RIGHT, 1.6f);
        std::string head = "YOU LOST GAME " + std::to_string(std::max(1, t.game - 1)) + ": PICK THE STAGE FOR GAME " +
                           std::to_string(t.game);
        text(dl, f, g_stencil, 18, 320, 80, TSET_GOLD, head.c_str(), CENTER, 1.4f);
        rect(dl, f, 50, 112, 540, 4, IM_COL32(255, 255, 255, 30), 2.0f);
        rect(dl, f, 50, 112, 540 * std::min(1.0f, left / 30.0f), 4, TSET_GOLD, 2.0f);
        tset_pick_grid(dl, f, pad, r, t);
        if (t.why[0]) text(dl, f, g_hand, 13, 320, 372, CREAM, t.why, CENTER, 0.8f, 540);
        float hx = 54;
        hx += hint(dl, f, hx, 392, "a", "PICK");
        hx += hint(dl, f, hx, 392, "rb", "VOICE");
        hint(dl, f, hx, 392, "b", "LEAVE ROOM");
        if (pad.a && TS.pick_sel < t.choice_count) {
            int v = t.choices[TS.pick_sel];
            netplay_lobby_tset_pick(v);
            say("Game " + std::to_string(t.game) + " at " + online_venue_name(v) + ".");
        }
        if (pad.rb) { S.page = VOICE; S.voice_sel = 0; }
        tset_leave(pad, false);
        return;
    }

    // the cards on the left, the set on the right
    float w = 120, gap = 8, top = 86, ch = 262;
    tset_card(dl, f, r, t, 0, 52, top, w, ch, fighter);
    tset_card(dl, f, r, t, 1, 52 + w + gap, top, w, ch, fighter);
    text(dl, f, g_stencil, 18, 52 + w + gap / 2, top + 110, CREAM, "VS", CENTER, 1.4f);
    tset_panel(dl, f, r, t, 316, top, 274, ch);

    std::string oname = opp ? opp->name : "the other player";
    bool hold = false;      // this player's READY would wait for the other's (coin toss order)
    switch (t.state) {
    case NETTSET_WAITING:
        line = "Waiting for " + oname + ": the side that does not come is DQ'd in " + tset_clock(tset_secs(t)) + ".";
        if (!opp) line = "Waiting for the other player: no show, DQ in " + tset_clock(tset_secs(t)) + ".";
        break;
    case NETTSET_PICK: {
        std::string who = t.picker < 2 ? tset_name(r, t.picker) : std::string("The loser");
        line = "Waiting for " + who + " to pick a stage (" + tset_clock(tset_secs(t)) + ")...";
        break;
    }
    case NETTSET_PLAYING:
        line = "Game " + std::to_string(t.game) + (t.venue ? std::string(" at ") + online_venue_name(t.venue) : "") + " is on...";
        break;
    case NETTSET_OVER:
        if (t.winner == NETTSET_LEVEL || t.winner == NETTSET_NONE)
            line = "The set ended level: the tournament host decides.";
        else if (t.winner == r.you)
            line = "You win the set " + std::to_string(t.wins[r.you]) + "-" + std::to_string(t.wins[other]) + ". The host confirms the result.";
        else
            line = tset_name(r, t.winner) + " wins the set " + std::to_string(t.wins[t.winner]) + "-" +
                   std::to_string(t.wins[1 - t.winner]) + ". The host confirms the result.";
        break;
    default:        // NETTSET_SELECT
        if (t.order < 2 && t.order != r.you && opp && !opp->ready && !watcher) {
            hold = true;
            line = oname + " selects first: their fighter shows here, then you choose.";
        } else if (t.order == r.you && !ready) {
            line = "You select first: choose your fighter and ready up.";
        } else if (lock != NETTSET_NONE && !ready) {
            line = "You won the last game: your fighter is locked to " + fighter_label(lock, r.you) + ".";
        } else if (t.order < 2 && t.order != r.you && opp && opp->ready && !ready) {
            line = oname + " is ready with " + fighter_label(opp->fighter, other) + ": your pick.";
        } else if (!ready) {
            line = "Game " + std::to_string(t.game) + (t.venue ? std::string(" at ") + online_venue_name(t.venue) : "") +
                   ": ready up.";
        } else {
            line = "Ready: the game starts when both are.";
        }
    }
    if (t.state == NETTSET_OVER) {
        std::string big = t.winner == r.you ? "YOU WIN THE SET" : t.winner < 2 ? upper(tset_name(r, t.winner).c_str()) + " WINS THE SET"
                                                                               : "NO WINNER";
        float bs = fit_size(f, g_stencil, 22, big.c_str(), 540);
        text(dl, f, g_stencil, bs, 320, 352, t.winner == r.you ? TSET_GOLD : CREAM, big.c_str(), CENTER, 1.4f);
        text(dl, f, g_hand, 13, 320, 376, ORANGE, line.c_str(), CENTER, 0.8f, 540);
    } else {
        float ls = fit_size(f, g_hand, 15, line.c_str(), 540);
        text(dl, f, g_hand, ls, 320, 354, ORANGE, line.c_str(), CENTER, 0.8f);
        if (t.why[0]) {
            std::string why = t.why;
            float ws = fit_size(f, g_hand, 13, why.c_str(), 540);
            text(dl, f, g_hand, ws, 320, 373, CREAM, why.c_str(), CENTER, 0.8f);
        }
    }
    float hx = 54, hy = 392;
    if (watcher) {
        hint(dl, f, hx, hy, "b", "LEAVE ROOM");
        if (pad.b) { netplay_lobby_leave(); S.page = HUB; say("You stopped watching the set."); }
        return;
    }
    bool can_ready = t.state == NETTSET_SELECT && !hold;
    if (t.state == NETTSET_OVER) {
        hint(dl, f, hx, hy, "b", "LEAVE ROOM");
        tset_leave(pad, true);
        return;
    }
    if (can_ready) hx += hint(dl, f, hx, hy, "a", ready ? "NOT READY" : "READY");
    if (can_ready && !ready && lock == NETTSET_NONE) hx += hint(dl, f, hx, hy, "x", "CHOOSE FIGHTER");
    hx += hint(dl, f, hx, hy, "rb", "VOICE");
    hint(dl, f, hx, hy, "b", "LEAVE ROOM");
    if (pad.a && hold) say(oname + " selects first this game.");
    else if (pad.a && can_ready) {
        if (!ready && (t.flags & NETTSET_STOCK_ONLY) && fighter == NETPLAY_CREATED_FIGHTER)
            say("Stock fighters only in this set: choose another fighter.");
        else
            netplay_lobby_ready(ready ? 0 : 1, fighter);
    }
    if (pad.x && can_ready && !ready && lock == NETTSET_NONE) {
        S.page = FIGHTERS;
        S.fighter_sel = grid_pos(grid_fighters(), fighter);
    }
    if (pad.rb) { S.page = VOICE; S.voice_sel = 0; }
    tset_leave(pad, false);
}

// The next game's venue before START (the room's phase 1, a single candidate): A starts it sooner (VOTE 0).
void draw_tset_intro(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const NetTSet &t)
{
    bool watcher = r.you == NETPLAY_SLOT_SPECTATOR;
    const NetRoomPlayer *me = watcher ? nullptr : tset_player(r, r.you);
    int venue = t.venue ? t.venue : r.candidate_count > 0 ? r.candidates[0] : 0;
    page_title(dl, f, ("GAME " + std::to_string(t.game)).c_str());
    tset_banner(dl, f, r, t, 50);
    glass(dl, f, 150, 84, 340, 246, 10);
    if (venue) venue_tile(dl, f, venue, 160, 94, 320, 226, 1.0f, true);
    for (int s = 0; s < 2; s++) {
        const NetRoomPlayer *p = tset_player(r, s);
        float x = s ? 500 : 30, w = 110;
        if (!p) continue;
        int fid = t.locks[s] != NETTSET_NONE ? t.locks[s] : p->fighter;
        glass(dl, f, x, 120, w, 190, 10, s == r.you ? IM_COL32(44, 30, 20, 255) : IM_COL32(10, 10, 13, 255), 0.62f);
        if (fid == 0xFF || !portrait(dl, f, fid, x + 4, 124, w - 8, 140)) text(dl, f, g_title, 40, x + w / 2, 170, GREY, "?", CENTER);
        float ns = fit_size(f, g_hand, 14, p->name, w - 8);
        text(dl, f, g_hand, ns, x + w / 2, 268, s == r.you ? ORANGE : CREAM, p->name, CENTER, 1.0f);
        std::string label = fid != 0xFF ? fighter_label(fid, s) : "";
        float ls = fit_size(f, g_title, 10, label.c_str(), w - 8);
        text(dl, f, g_title, ls, x + w / 2, 288, GREY, label.c_str(), CENTER, 0.6f);
    }
    std::string at = venue ? "GAME " + std::to_string(t.game) + " AT " + upper(online_venue_name(venue)) : "GAME " + std::to_string(t.game);
    float as = fit_size(f, g_stencil, 20, at.c_str(), 560);
    text(dl, f, g_stencil, as, 320, 340, TSET_GOLD, at.c_str(), CENTER, 1.4f);
    bool voted = me && me->voted >= 0;
    text(dl, f, g_hand, 14, 320, 368, ORANGE, voted ? "Waiting for the other player..." : "The game starts in a moment.", CENTER, 0.8f);
    if (watcher) return;
    float hx = 54;
    if (!voted) hx += hint(dl, f, hx, 392, "a", "START NOW");
    hint(dl, f, hx, 392, "rb", "VOICE");
    if (pad.a && !voted) netplay_lobby_vote(0);
    if (pad.rb) { S.page = VOICE; S.voice_sel = 0; }
}

// ---- gallery samples ----

bool tset_gallery(const std::string &p)
{
    TS.gallery_entry = p == "hub-tset";
    TS.sample_on = false;
    if (p == "hub-tset") { S.page = HUB; S.hub_sel = HUB_TSET; G.ranked = true; TW.gallery_entry = true; return true; }
    if (p.compare(0, 5, "tset-") != 0) return false;
    const auto &list = roster();
    auto fid = [&](int i) { return list.empty() ? 0 : list[(i * 7 + 3) % list.size()].id; };
    S.page = HUB;
    G.room = sample_room(0, 2, 2);
    snprintf(G.room.settings.name, sizeof(G.room.settings.name), "Winners round 1");
    G.room.players[0].ready = G.room.players[1].ready = 0;
    NetTSet &t = TS.sample;
    t = NetTSet();
    t.match = 7;
    t.best_of = 3;
    t.flags = NETTSET_LOSER_PICKS | NETTSET_DAVES | NETTSET_WINNER_LOCK | NETTSET_COIN_TOSS | NETTSET_STOCK_ONLY;
    t.state = NETTSET_SELECT;
    t.game = 1;
    t.first = 1;
    t.order = NETTSET_NONE;
    t.picker = NETTSET_NONE;
    t.venue = 10;       // the start stage
    t.locks[0] = t.locks[1] = NETTSET_NONE;
    t.winner = NETTSET_NONE;
    snprintf(t.round, sizeof(t.round), "Winners round 1");
    t.received = GetTickCount64();
    auto game = [&](int venue, int winner, int how) {
        NetTSetGame &g = t.games[t.game_count++];
        g.venue = venue; g.winner = winner; g.how = how;
        g.fighters[0] = fid(0); g.fighters[1] = fid(1);
        if (how != NETTSET_DRAW && winner < 2) t.wins[winner]++;
        t.game = t.game_count + 1;
    };
    static const int pool[] = { 1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13, 14, 15, 16, 18, 19, 20, 21 };
    if (p == "tset-coin") {
        t.order = 1;                        // Ben won the toss and selects first; Ann waits
        G.room.players[1].fighter = 0xFF;
    } else if (p == "tset-select") {
        game(10, 0, NETTSET_WIN);           // Ann won game 1: her fighter is locked; Ben picked the stage
        t.locks[0] = fid(0);
        t.venue = 4;
    } else if (p == "tset-why") {
        game(10, 0, NETTSET_WIN);
        game(4, NETTSET_NONE, NETTSET_DRAW);
        t.locks[0] = fid(0);
        t.venue = 4;
        snprintf(t.why, sizeof(t.why), "Game 3 was a draw: it is replayed on the same stage.");
    } else if (p == "tset-pick" || p == "tset-pick-wait") {
        game(10, 0, NETTSET_WIN);           // Ann won on Club Murder, Ben on stage 4: Ann picks game 3
        game(4, 1, NETTSET_WIN);
        t.locks[1] = fid(1);
        t.state = NETTSET_PICK;
        t.picker = 0;
        t.venue = 0;
        t.secs = 23;
        for (int v : pool) if (v != 10) t.choices[t.choice_count++] = v;     // Dave's rule: not Club Murder
        if (p == "tset-pick-wait") G.room.you = 1;
    } else if (p == "tset-intro") {
        game(10, 0, NETTSET_WIN);
        t.locks[0] = fid(0);
        t.state = NETTSET_PLAYING;
        t.venue = 4;
        G.room.phase = 1;
        G.room.candidate_count = 1;
        G.room.candidates[0] = 4;
        G.room.players[0].ready = G.room.players[1].ready = 1;
        G.room.players[1].voted = 0;
    } else if (p == "tset-waiting") {
        game(10, 1, NETTSET_LEFT);
        t.state = NETTSET_WAITING;
        t.secs = 512;
        G.room.player_count = 1;
        snprintf(t.why, sizeof(t.why), "Ben left during game 1 and lost it.");
    } else if (p == "tset-grid") {          // CHOOSE FIGHTER in a stock-only set: no created fighter offered
        G.cp_slots = 1;
        S.page = FIGHTERS; S.fighter_sel = 0; S.fighter_top = 0;
    } else if (p == "tset-over") {
        game(10, 0, NETTSET_WIN);
        game(4, 1, NETTSET_WIN);
        game(13, 0, NETTSET_WIN);
        t.state = NETTSET_OVER;
        t.winner = 0;
        t.game = 3;
    }
    TS.sample_on = true;
    return true;
}
