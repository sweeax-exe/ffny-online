// Ranked play's pages (ranked phase 2a: Classic and Open sets; Draft is phase 2b). Included into
// online_screen.cpp's anonymous namespace after its helpers (text, glass, the gallery's wrappers), so
// it is not a translation unit of its own (the hooks' CMake glob takes *.c and *.cpp only). The wire
// and the ladder are netplay_ranked.c; the lobby side is netplay.c (QUEUE, ACCEPT, RANKINFO).
//
//   RANKED_MODES    the mode chooser: Classic / Open (Draft "coming soon"), "also search" toggles
//   RANKED          the search, the ACCEPT popup, the move to the set's relay
//   the room        the set's banner (mode, game, score, who picks the venue) and the rank on each card
//   the versus      the ranks and the set's score over the cards
//   RANKED_RESULTS  after a set: the rating change, the division bar, PROMOTION / DEMOTION SET, placements
//   MY STATS        Y: a tab per mode beside the casual record

struct RankedUi {
    int primary = NETRANK_CLASSIC;      // the mode chosen on RANKED_MODES
    bool also = false;                  // also search the other of Classic and Open
    NetModeRank before[NETRANK_MODES] = {};     // the ranks when the search started (the results' "before")
    bool have_before = false;
    // the set just played: its room, mode, this PC's slot and its last RANKINFO
    char set_room[7] = "";
    NetRankInfo set = {};
    int set_you = -1;
    bool results_up = false;            // the results page opened for set_room
    unsigned long long results_since = 0, results_fetch = 0;
    int stats_tab = 0;                  // MY STATS: 0 casual, 1 + NETRANK_* a mode
} R;

// The gallery's samples (gallery_step sets them per page).
struct RankedSample {
    NetRankInfo rank = {};
    NetQueueExt queue = {};
    int accepted = -1;
    NetModeRank after[NETRANK_MODES] = {};
} RG;

int lobby_rankinfo(NetRankInfo *out)
{
    if (gallery_on()) { *out = RG.rank; return RG.rank.valid; }
    return netplay_lobby_rankinfo(out);
}
int lobby_queue_ext(NetQueueExt *out)
{
    if (gallery_on()) { *out = RG.queue; return RG.queue.valid; }
    return netplay_lobby_queue_ext(out);
}
int lobby_accepted() { return gallery_on() ? RG.accepted : netplay_lobby_accepted(); }

// Ranked shows in the hub only when a relay's STATUS says the queue is on (its account service's
// switch), or RECOMP_RANKED=1 (tests). Returns the relay to search on: the hub's own when it offers
// ranked, else one that does (Europe first), -1 none.
int ranked_relay()
{
    NetServerInfo sv[NETSERVER_COUNT];
    servers(sv);
    int cur = connected_server();
    auto offers = [&](int i) { return i >= 0 && sv[i].state == NETSERVER_UP && sv[i].version && (sv[i].flags & NETSERVER_RANKED); };
    if (offers(cur)) return cur;
    for (int i = 0; i < NETSERVER_COUNT; i++) if (offers(i)) return i;
    return -1;
}
bool ranked_test_switch()
{
    static int on = -1;
    if (on < 0) on = getenv("RECOMP_RANKED") != nullptr;
    return on != 0;
}

// The ranked_<mode>= lines of the stats text.
void read_ranks(NetModeRank out[NETRANK_MODES])
{
    static char buf[16384];
    for (int m = 0; m < NETRANK_MODES; m++) out[m] = NetModeRank();
    if (!stats_text(buf, sizeof(buf))) return;
    for (char *line = buf; line && *line;) {
        char *end = strchr(line, '\n');
        std::string l = end ? std::string(line, end - line) : std::string(line);
        size_t eq = l.find('=');
        if (eq != std::string::npos) {
            int m = netranked_mode_of_key(l.substr(0, eq).c_str());
            if (m >= 0) netranked_parse_line(l.c_str() + eq + 1, &out[m]);
        }
        line = end ? end + 1 : nullptr;
    }
}

std::string division_label(int division)
{
    char name[48];
    netranked_division_name(division, name, sizeof(name));
    return upper(name);
}

// A rank's colour by tier (the project's own: no badge art yet, phase 3).
ImU32 tier_colour(int tier)
{
    static const ImU32 c[8] = { IM_COL32(170, 150, 130, 255), IM_COL32(190, 120, 70, 255), IM_COL32(200, 200, 210, 255),
                                IM_COL32(255, 205, 60, 255), IM_COL32(90, 200, 220, 255), IM_COL32(120, 170, 255, 255),
                                IM_COL32(220, 120, 255, 255), IM_COL32(255, 90, 60, 255) };
    return c[tier < 0 ? 0 : tier > 7 ? 7 : tier];
}

// A small rank plate: the label in its tier's colour on a dark pill, `w` wide, centred at cx.
void rank_plate(ImDrawList *dl, const Frame &f, float cx, float y, float w, const char *label, int tier, bool placing)
{
    ImU32 col = placing ? GREY : tier_colour(tier);
    rect(dl, f, cx - w / 2, y, w, 17, IM_COL32(0, 0, 0, 190), 5);
    frame_rect(dl, f, cx - w / 2, y, w, 17, with_alpha(col, 0.7f), 1.0f);
    float size = fit_size(f, g_title, 11, label, w - 8);
    text(dl, f, g_title, size, cx, y + 2 + (11 - size) * 0.5f, col, label, CENTER, 0.8f);
}

// The bar to the next division: rating's place in its division, 0..1.
void division_bar(ImDrawList *dl, const Frame &f, float x, float y, float w, int rating, int division, ImU32 col)
{
    float pct = netranked_division_percent(rating, division) / 100.0f;
    rect(dl, f, x, y, w, 8, IM_COL32(255, 255, 255, 30), 4);
    rect(dl, f, x, y, std::max(8.0f, w * pct), 8, col, 4);
}

// ---- the mode chooser ----

void open_ranked_modes()
{
    S.page = RANKED_MODES;
    if (!gallery_on()) account_fetch_stats_page(0);    // each mode's rank on its card
}

void draw_ranked_modes(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    static const int order[3] = { NETRANK_CLASSIC, NETRANK_OPEN, NETRANK_DRAFT };
    static const char *const subs[3] = { "STOCK FIGHTERS ONLY", "EVERY FIGHTER, YOUR CREATED ONE TOO", "PICK AND BAN" };
    static const char *const descs[3] = {
        "The game's own roster, nothing else. Best of three: game 1 on a starter venue, then the loser picks.",
        "Any fighter, Story-mode and your created fighter included. Best of three, the loser picks the venue.",
        "Bans, venue strikes and blind picks before every game. Coming soon." };
    int at = 0;
    for (int i = 0; i < 3; i++) if (order[i] == R.primary) at = i;
    if (pad.left || pad.right)      // Classic and Open only (Draft: phase 2b) R.primary = R.primary == NETRANK_CLASSIC ? NETRANK_OPEN : NETRANK_CLASSIC;
    for (int i = 0; i < 3; i++) if (order[i] == R.primary) at = i;
    if (pad.y) R.also = !R.also;

    NetModeRank ranks[NETRANK_MODES];
    read_ranks(ranks);
    page_title(dl, f, "RANKED MATCH");
    online_count(dl, f);
    const float w = 176, gap = 12, x0 = 320 - (3 * w + 2 * gap) / 2, y = 82, h = 262;
    for (int i = 0; i < 3; i++) {
        int m = order[i];
        float x = x0 + i * (w + gap);
        bool sel = i == at, draft = m == NETRANK_DRAFT;
        if (!draft && mouse_pick(f, x, y, w, h)) R.primary = m;
        glass(dl, f, x, y, w, h, 10, sel ? IM_COL32(56, 14, 20, 255) : IM_COL32(10, 10, 13, 255), sel ? 0.70f : 0.58f);
        ImU32 head = draft ? with_alpha(GREY, 0.6f) : sel ? CREAM : with_alpha(CREAM, 0.75f);
        text(dl, f, g_stencil, 30, x + w / 2, y + 14, head, netranked_mode_title(m), CENTER, 1.6f);
        text(dl, f, g_hand, 12, x + w / 2, y + 54, draft ? with_alpha(GREY, 0.6f) : ORANGE, subs[i], CENTER, 0.8f, w - 12);
        if (draft) {
            rect(dl, f, x + 20, y + 112, w - 40, 26, IM_COL32(0, 0, 0, 170), 6);
            text(dl, f, g_title, 15, x + w / 2, y + 116, GREY, "COMING SOON", CENTER, 1.0f);
        } else {
            const NetModeRank &r = ranks[m];
            if (!r.valid) {
                text(dl, f, g_title, 15, x + w / 2, y + 100, WHITE, "UNRANKED", CENTER, 1.0f);
                text(dl, f, g_title, 11, x + w / 2, y + 122, GREY, "5 placement sets show your rank", CENTER, 0.8f, w - 16);
            } else if (r.placements_left > 0) {
                text(dl, f, g_title, 15, x + w / 2, y + 100, WHITE, upper(r.label).c_str(), CENTER, 1.0f, w - 10);
                text(dl, f, g_title, 11, x + w / 2, y + 122, GREY, "placement sets", CENTER, 0.8f);
            } else {
                int tier = netranked_tier_of(r.division);
                rank_plate(dl, f, x + w / 2, y + 96, w - 30, upper(r.label).c_str(), tier, false);
                char num[16];
                snprintf(num, sizeof(num), "%d", r.rating);
                text(dl, f, g_title, 26, x + w / 2, y + 118, CREAM, num, CENTER, 1.4f);
                division_bar(dl, f, x + 20, y + 152, w - 40, r.rating, r.division, tier_colour(tier));
            }
        }
        text(dl, f, g_body, 11, x + 10, y + 176, draft ? with_alpha(GREY, 0.7f) : with_alpha(WHITE, 0.85f), descs[i], LEFT, 0.6f, w - 20);
        if (sel) selector(dl, f, x, y, w, h);
        if (!draft && mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    // "also search": the other of Classic and Open
    int other = R.primary == NETRANK_CLASSIC ? NETRANK_OPEN : NETRANK_CLASSIC;
    {
        std::string line = std::string("ALSO SEARCH ") + netranked_mode_title(other);
        float bx = 320 - 110, by = 352;
        rect(dl, f, bx, by, 18, 18, IM_COL32(0, 0, 0, 170), 4);
        frame_rect(dl, f, bx, by, 18, 18, ORANGE, 1.2f);
        if (R.also) text(dl, f, g_title, 16, bx + 9, by, ORANGE, "X", CENTER, 1.0f);
        text(dl, f, g_title, 14, bx + 26, by + 1, R.also ? CREAM : GREY, line.c_str(), LEFT, 1.0f);
        if (mouse_click(f, bx, by, 220, 18)) R.also = !R.also;
        text(dl, f, g_hand, 11, 320, by + 22, with_alpha(ORANGE, 0.8f),
             "Found sooner: after 45 s either mode you both search may match.", CENTER, 0.7f);
    }
    float hx = 54;
    hx += hint(dl, f, hx, 410, "a", "CHOOSE FIGHTER");
    hx += hint(dl, f, hx, 410, "y", R.also ? "ONLY THIS MODE" : "ALSO SEARCH");
    hint(dl, f, hx, 410, "b", "BACK");
    if (pad.b) { S.page = HUB; return; }
    if (pad.a) {
        S.ranked_pick = true;
        S.page = FIGHTERS;
        int fighter = saved_fighter();
        if (fighter == NETPLAY_CREATED_FIGHTER && R.primary != NETRANK_OPEN) fighter = 55;
        S.fighter_sel = grid_pos(grid_fighters(), fighter);
    }
}

// The modes searched: the primary, and the other one when "also search" is on (a created fighter
// plays Open only, so Classic is left out then).
unsigned ranked_modes_for(int fighter)
{
    unsigned m = 1u << R.primary;
    if (R.also) m |= 1u << (R.primary == NETRANK_CLASSIC ? NETRANK_OPEN : NETRANK_CLASSIC);
    if (fighter == NETPLAY_CREATED_FIGHTER) m &= ~(1u << NETRANK_CLASSIC);
    return m;
}

void ranked_search(int fighter)
{
    unsigned modes = ranked_modes_for(fighter);
    if (!(modes & (1u << R.primary))) {     // a created fighter with Classic: Open, the only mode it plays
        R.primary = NETRANK_OPEN;
        modes = 1u << NETRANK_OPEN;
        say("A created fighter plays Open only: searching Open.");
    }
    read_ranks(R.before);
    R.have_before = true;
    R.results_up = false;
    R.set_room[0] = 0;
    netplay_lobby_ranked_ex(fighter, modes, R.primary);
}

// ---- the search, ACCEPT and the move ----

void draw_accept(ImDrawList *dl, const Frame &f, const Pad &pad, const NetQueueExt &q)
{
    int answered = lobby_accepted();
    rect(dl, f, -200, -100, 1040, 680, IM_COL32(0, 0, 0, 215));     // the search behind it, dimmed
    glass(dl, f, 150, 120, 340, 220, 14, IM_COL32(40, 12, 16, 255), 0.97f);
    text(dl, f, g_stencil, 28, 320, 134, CREAM, "OPPONENT FOUND", CENTER, 1.6f);
    std::string mode = std::string("RANKED ") + netranked_mode_title(q.mode >= 0 ? q.mode : R.primary) + "  -  BEST OF THREE";
    text(dl, f, g_hand, 14, 320, 172, ORANGE, mode.c_str(), CENTER, 0.9f);
    if (q.opp_rating > 0) {
        int d = netranked_division_of(q.opp_rating);
        char num[48];
        snprintf(num, sizeof(num), "AROUND %s  -  %d", division_label(d).c_str(), q.opp_rating);
        rank_plate(dl, f, 320, 198, 280, num, netranked_tier_of(d), false);
    }
    // the countdown: the relay's seconds, counted down here between its QUEUEs
    static char shown_for[7];
    static unsigned long long since;
    static int secs0;
    if (strcmp(shown_for, q.code) || q.accept_secs > secs0) {
        snprintf(shown_for, sizeof(shown_for), "%s", q.code);
        since = GetTickCount64();
    }
    secs0 = q.accept_secs;
    int left = std::max(0, q.accept_secs - (int)((GetTickCount64() - since) / 1000));
    if (gallery_on()) left = 11;
    char secs[8];
    snprintf(secs, sizeof(secs), "%d", left);
    text(dl, f, g_title, 40, 320, 222, left <= 5 ? RED : CREAM, secs, CENTER, 1.8f);
    rect(dl, f, 180, 272, 280, 5, IM_COL32(255, 255, 255, 30), 2.5f);
    rect(dl, f, 180, 272, 280 * std::min(1.0f, left / 15.0f), 5, ORANGE, 2.5f);
    if (answered == 1) {
        text(dl, f, g_hand, 16, 320, 290, GREEN, "ACCEPTED: WAITING FOR THE OPPONENT...", CENTER, 0.9f);
        return;
    }
    text(dl, f, g_title, 11, 320, 286, GREY, "Declining or letting it run out locks the queue for a few minutes.",
         CENTER, 0.7f, 300);
    float hx = 214;
    hx += hint(dl, f, hx, 312, "a", "ACCEPT");
    hint(dl, f, hx + 10, 312, "b", "DECLINE");
    if (pad.a) netplay_lobby_accept(1);
    else if (pad.b) netplay_lobby_accept(0);
}

void draw_ranked(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    int waited = 0, searching = 0;
    char why[96];
    int st = lobby_queue(&waited, &searching, why, sizeof(why));
    NetQueueExt q;
    bool ext = lobby_queue_ext(&q) != 0;
    int fighter = saved_fighter();
    page_title(dl, f, "RANKED MATCH");
    online_count(dl, f);
    glass(dl, f, 36, 74, 180, 308);
    if (!portrait(dl, f, fighter, 46, 84, 160, 240))
        text(dl, f, g_title, 26, 126, 180, with_alpha(CREAM, 0.6f), online_fighter_name(fighter), CENTER, 1.4f, 150);
    text(dl, f, g_hand, 22, 126, 330, ORANGE, fighter_label(fighter).c_str(), CENTER, 1.2f);
    glass(dl, f, 226, 86, 378, 236);

    float hx = 54;
    if (st == NETQUEUE_REFUSED || st == NETQUEUE_NONE) {
        text(dl, f, g_hand, 24, 240, 110, RED, "NOT SEARCHING", LEFT, 1.0f);
        text(dl, f, g_title, 14, 240, 150, WHITE, st == NETQUEUE_REFUSED ? why : "The search ended.", LEFT, 0.8f, 360);
        hx += hint(dl, f, hx, 392, "a", "TRY AGAIN");
        hint(dl, f, hx, 392, "b", "BACK");
        if (pad.a) ranked_search(fighter);
        if (pad.b) S.page = HUB;
        return;
    }
    float t = (float)(GetTickCount64() % 1600) / 1600.0f;
    const char *head = st == NETQUEUE_FOUND || st == NETQUEUE_ACCEPT ? "OPPONENT FOUND!" : st == NETQUEUE_MOVE ? "MOVING SERVER"
                     : st == NETQUEUE_SEARCHING ? "SEARCHING" : "CHECKING YOUR ACCOUNT";
    text(dl, f, g_hand, 26, 240, 98, st == NETQUEUE_FOUND || st == NETQUEUE_ACCEPT ? GREEN : ORANGE, head, LEFT, 1.0f);
    {   // the modes searched, the primary first
        std::string modes = std::string("RANKED ") + netranked_mode_title(R.primary);
        unsigned m = ranked_modes_for(fighter);
        for (int k = 0; k < NETRANK_MODES; k++)
            if (k != R.primary && (m >> k & 1u)) modes += std::string(" + ") + netranked_mode_title(k);
        text(dl, f, g_title, 13, 590, 104, CREAM, modes.c_str(), RIGHT, 1.0f);
    }
    if (st == NETQUEUE_SEARCHING || st == NETQUEUE_ACCEPT) {
        rect(dl, f, 240, 142, 340, 4, IM_COL32(255, 255, 255, 30), 2);
        rect(dl, f, 240 + 300 * (t < 0.5f ? t * 2 : 2 - t * 2), 142, 40, 4, ORANGE);
        char line[64];
        snprintf(line, sizeof(line), "%d:%02d", waited / 60, waited % 60);
        text(dl, f, g_title, 34, 240, 160, CREAM, line, LEFT, 1.6f);
        if (ext) {      // searching per mode (the matchmaker's counts, every relay)
            for (int k = 0; k < NETRANK_MODES; k++) {
                float x = 360 + k * 78;
                bool live = k != NETRANK_DRAFT;
                snprintf(line, sizeof(line), "%d", q.counts[k]);
                text(dl, f, g_title, 22, x, 160, live ? WHITE : with_alpha(GREY, 0.6f), line, CENTER, 1.2f);
                text(dl, f, g_title, 10, x, 188, live ? GREY : with_alpha(GREY, 0.6f), netranked_mode_title(k), CENTER, 0.8f);
            }
            text(dl, f, g_title, 10, 438, 202, GREY, "SEARCHING ON EVERY SERVER", CENTER, 0.6f);
        } else {
            snprintf(line, sizeof(line), "%d PLAYER%s SEARCHING", searching, searching == 1 ? "" : "S");
            text(dl, f, g_title, 14, 240, 212, GREY, line, LEFT, 1.0f);
        }
        text(dl, f, g_title, 12, 240, 232, WHITE,
             "The matchmaker pairs players of a similar rating on the server with the best ping for both; the longer the "
             "search, the wider it looks.", LEFT, 0.8f, 350);
    } else if (st == NETQUEUE_MOVE || (st == NETQUEUE_TICKET && netservers_moving())) {
        text(dl, f, g_title, 14, 240, 150, WHITE, "The set is hosted on the server with the best ping for both of you.",
             LEFT, 0.8f, 350);
    }
    hint(dl, f, hx, 392, "b", "STOP SEARCHING");
    if (st == NETQUEUE_ACCEPT && ext) { draw_accept(dl, f, pad, q); return; }
    if (pad.b) { netplay_lobby_leave(); S.page = HUB; say("You left the ranked queue."); }
}

// ---- the set in the room ----

bool ranked_room(const NetRoom &r, NetRankInfo *ri)
{
    return r.in_room && !NETPLAY_IS_KOTH(r.settings.mode) && lobby_rankinfo(ri) && ri->has_set;
}

// The banner over the room or the vote: the mode, the game, the score, who picks the venue.
void ranked_banner(ImDrawList *dl, const Frame &f, const NetRoom &r, const NetRankInfo &ri, float y)
{
    const NetRankPlayer *me = netranked_player(&ri, r.you), *opp = nullptr;
    for (int i = 0; i < ri.count; i++) if (ri.p[i].slot != r.you) opp = &ri.p[i];
    char line[96];
    snprintf(line, sizeof(line), "RANKED %s  -  GAME %d  -  SET %d-%d", netranked_mode_title(ri.mode), ri.game,
             me ? me->set_wins : 0, opp ? opp->set_wins : 0);
    float w = text_width(f, g_title, 13, line) + 24;
    rect(dl, f, 320 - w / 2, y, w, 22, IM_COL32(120, 12, 26, 230), 6);
    text(dl, f, g_title, 13, 320, y + 4, WHITE, line, CENTER, 1.0f);
}

const char *ranked_picker_line(const NetRoom &r, const NetRankInfo &ri, char *buf, int size)
{
    if (ri.game <= 1) return "Game 1: a starter venue, drawn for you.";
    if (ri.picker < 0) return "A draw: both vote for the next venue.";
    if (ri.picker == r.you) return "You lost the last game: you pick the next venue.";
    snprintf(buf, (size_t)size, "%s lost the last game and picks the next venue.", slot_name(r, ri.picker));
    return buf;
}

// The rank on a room card, over the portrait's foot.
void ranked_card(ImDrawList *dl, const Frame &f, const NetRankInfo &ri, int slot, float x, float y, float w)
{
    const NetRankPlayer *p = netranked_player(&ri, slot);
    if (!p) return;
    char label[48];
    netranked_player_label(p, 5, label, sizeof(label));
    bool placing = p->division == NETRANK_PLACEMENT || p->placements > 0;
    rank_plate(dl, f, x + w / 2, y, w - 12, upper(label).c_str(), netranked_tier_of(placing ? 0 : p->division), placing);
}

// A created fighter: Open only (the search's primary, or the set's mode in its room).
bool ranked_created_ok(const NetRoom &r)
{
    NetRankInfo ri;
    if (r.in_room) return !ranked_room(r, &ri) || ri.mode == NETRANK_OPEN;
    return !S.ranked_pick || R.primary == NETRANK_OPEN;
}

int &ranked_stats_tab() { return R.stats_tab; }

// ---- the versus screen ----

void ranked_versus(ImDrawList *dl, const Frame &f, const NetVersus &v, float alpha)
{
    NetRankInfo ri;
    if (v.count != 2 || NETPLAY_IS_KOTH(v.room_mode) || !lobby_rankinfo(&ri) || !ri.has_set) return;
    int k = dl->VtxBuffer.Size;
    // positions follow the room's slots (a ranked room holds slots 0 and 1)
    const NetRankPlayer *p0 = netranked_player(&ri, 0), *p1 = netranked_player(&ri, 1);
    char line[96];
    snprintf(line, sizeof(line), "RANKED %s  -  GAME %d", netranked_mode_title(ri.mode), ri.game);
    text(dl, f, g_title, 14, 320, 60, ORANGE, line, CENTER, 1.0f);
    snprintf(line, sizeof(line), "%d - %d", p0 ? p0->set_wins : 0, p1 ? p1->set_wins : 0);
    text(dl, f, g_stencil, 30, 320, 268, CREAM, line, CENTER, 1.6f);
    text(dl, f, g_title, 10, 320, 300, GREY, "SET, BEST OF THREE", CENTER, 0.8f);
    for (int s = 0; s < 2; s++) {
        const NetRankPlayer *p = s == 0 ? p0 : p1;
        if (!p) continue;
        char label[48], rating[16];
        netranked_player_label(p, 5, label, sizeof(label));
        bool placing = p->division == NETRANK_PLACEMENT || p->placements > 0;
        float x = s == 0 ? 36 : 640 - 36 - 196;
        rank_plate(dl, f, x + 98, 78, 172, upper(label).c_str(), netranked_tier_of(placing ? 0 : p->division), placing);
        if (!placing) {
            snprintf(rating, sizeof(rating), "%d", p->rating);
            text(dl, f, g_title, 11, x + 98, 98, CREAM, rating, CENTER, 0.8f);
        }
    }
    for (int i = k; i < dl->VtxBuffer.Size; i++) {      // the page's fade
        ImU32 &c = dl->VtxBuffer[i].col;
        c = (c & 0x00FFFFFFu) | ((ImU32)((c >> 24) * alpha) << 24);
    }
}

// ---- after the set: the results ----

void open_ranked_results(const NetRoom &r, const NetRankInfo &ri)
{
    snprintf(R.set_room, sizeof(R.set_room), "%s", r.code);
    R.set = ri;
    R.set_you = r.you;
    R.results_up = true;
    R.results_since = R.results_fetch = GetTickCount64();
    S.page = RANKED_RESULTS;
    if (!gallery_on()) {
        netplay_lobby_leave();      // the set is over: nothing more starts in its room
        account_fetch_stats_page(0);
    }
}

void draw_ranked_results(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    const NetRankInfo &ri = R.set;
    const NetRankPlayer *me = netranked_player(&ri, R.set_you), *opp = nullptr;
    for (int i = 0; i < ri.count; i++) if (ri.p[i].slot != R.set_you) opp = &ri.p[i];
    bool won = ri.winner == R.set_you, level = ri.winner == NETRANK_SET_LEVEL;
    int mode = ri.mode;
    NetModeRank now[NETRANK_MODES];
    if (gallery_on()) for (int m = 0; m < NETRANK_MODES; m++) now[m] = RG.after[m];
    else read_ranks(now);
    const NetModeRank &a = now[mode], &b = R.before[mode];
    int sets_before = b.valid ? b.set_wins + b.set_losses : 0, sets_now = a.valid ? a.set_wins + a.set_losses : 0;
    bool rated = a.valid && sets_now > sets_before;
    unsigned long long t = GetTickCount64();
    // the account service rates the set once both games' reports are in: ask again every 3 s, a minute
    if (!gallery_on() && !rated && t - R.results_since < 60000 && t - R.results_fetch > 3000 && !stats_busy()) {
        R.results_fetch = t;
        account_fetch_stats_page(0);
    }

    page_title(dl, f, "SET OVER");
    glass(dl, f, 36, 74, 568, 300);
    text(dl, f, g_stencil, 46, 320, 86, won ? GREEN : level ? CREAM : with_alpha(RED, 1.0f),
         won ? "VICTORY" : level ? "LEVEL" : "DEFEAT", CENTER, 2.0f);
    char line[128];
    snprintf(line, sizeof(line), "RANKED %s  -  SET %d-%d", netranked_mode_title(mode), me ? me->set_wins : 0, opp ? opp->set_wins : 0);
    text(dl, f, g_title, 15, 320, 142, ORANGE, line, CENTER, 1.0f);
    if (!rated) {
        float dots = (float)(t % 1200) / 1200.0f;
        text(dl, f, g_hand, 18, 320, 200, CREAM, t - R.results_since < 60000 ? "UPDATING YOUR RATING..." : "YOUR RATING UPDATES SOON",
             CENTER, 1.0f);
        rect(dl, f, 220 + 160 * dots, 232, 40, 4, ORANGE, 2);
        text(dl, f, g_title, 11, 320, 248, GREY, "The set counts once both games' results are in.", CENTER, 0.8f);
    } else {
        int delta = a.rating - (b.valid ? b.rating : 1500);
        bool placing = a.placements_left > 0;
        if (placing) {
            snprintf(line, sizeof(line), "PLACEMENT %d / %d", a.placements_of - a.placements_left, a.placements_of);
            text(dl, f, g_stencil, 28, 320, 176, CREAM, line, CENTER, 1.6f);
            float w = 300, x = 320 - w / 2;
            for (int i = 0; i < a.placements_of && a.placements_of <= 10; i++) {
                float cw = (w - (a.placements_of - 1) * 6) / a.placements_of;
                rect(dl, f, x + i * (cw + 6), 218, cw, 10, i < a.placements_of - a.placements_left ? ORANGE : IM_COL32(255, 255, 255, 30), 4);
            }
            text(dl, f, g_title, 12, 320, 238, GREY, "Your rank shows after the placement sets.", CENTER, 0.8f);
        } else {
            int tier = netranked_tier_of(a.division);
            bool promoted = b.valid && b.placements_left == 0 && a.division > b.division;
            bool demoted = b.valid && b.placements_left == 0 && a.division < b.division;
            bool placed = b.valid && b.placements_left > 0;
            rank_plate(dl, f, 320, 172, 260, upper(a.label).c_str(), tier, false);
            if (promoted || placed || demoted) {
                const char *what = promoted ? "PROMOTED!" : placed ? "PLACED!" : "DEMOTED";
                text(dl, f, g_hand, 20, 320, 194, promoted || placed ? GOLD : RED, what, CENTER, 1.0f);
            }
            snprintf(line, sizeof(line), "%d", a.rating);
            text(dl, f, g_title, 34, 300, 214, CREAM, line, RIGHT, 1.6f);
            snprintf(line, sizeof(line), "%+d", delta);
            text(dl, f, g_title, 26, 312, 220, delta >= 0 ? GREEN : with_alpha(RED, 1.0f), line, LEFT, 1.4f);
            division_bar(dl, f, 170, 262, 300, a.rating, a.division, tier_colour(tier));
            if (a.division < NETRANK_DIVISIONS - 1) {
                snprintf(line, sizeof(line), "%d TO %s", a.to_next, division_label(a.division + 1).c_str());
                text(dl, f, g_title, 11, 470, 276, GREY, line, RIGHT, 0.8f);
            }
        }
        if (a.hint == NETRANK_HINT_PROMOTION || a.hint == NETRANK_HINT_DEMOTION) {
            const char *h = a.hint == NETRANK_HINT_PROMOTION ? "NEXT: PROMOTION SET" : "NEXT: DEMOTION SET";
            float w = text_width(f, g_title, 13, h) + 24;
            rect(dl, f, 320 - w / 2, 300, w, 22, a.hint == NETRANK_HINT_PROMOTION ? IM_COL32(120, 90, 10, 230) : IM_COL32(120, 12, 26, 230), 6);
            text(dl, f, g_title, 13, 320, 304, WHITE, h, CENTER, 1.0f);
        }
        snprintf(line, sizeof(line), "SETS %d W  %d L   PEAK %d", a.set_wins, a.set_losses, a.peak);
        text(dl, f, g_title, 12, 320, 340, GREY, line, CENTER, 0.8f);
    }
    float hx = 54;
    hx += hint(dl, f, hx, 392, "a", "SEARCH AGAIN");
    hint(dl, f, hx, 392, "b", "BACK");
    if (pad.a) open_ranked_modes();
    else if (pad.b) S.page = HUB;
}

// Every frame (draw): a MOVE to the set's relay, and the results once the set is over and this PC
// is back in its room.
void ranked_tick(const NetRoom &r)
{
    if (gallery_on()) return;
    int mv = netservers_ranked_move(hub_name().c_str());
    if (mv > 0) say("The set is on another server: moving there...");
    else if (mv < 0) say("The set's server could not be reached. Search again.", 6000);
    NetRankInfo ri;
    if (!ranked_room(r, &ri)) return;
    if (ri.winner != NETRANK_SET_RUNNING && r.phase == 0 && !(R.results_up && !strcmp(R.set_room, r.code)))
        open_ranked_results(r, ri);
}

// ---- MY STATS: a tab per mode ----

void draw_ranked_stats(ImDrawList *dl, const Frame &f, int mode)
{
    NetModeRank ranks[NETRANK_MODES];
    read_ranks(ranks);
    const NetModeRank &r = ranks[mode];
    glass(dl, f, 34, 78, 572, 328);
    text(dl, f, g_stencil, 30, 52, 88, CREAM, (std::string("RANKED ") + netranked_mode_title(mode)).c_str(), LEFT, 1.6f);
    if (mode == NETRANK_DRAFT && !r.valid) {
        text(dl, f, g_title, 18, 320, 200, GREY, "DRAFT IS COMING SOON", CENTER, 1.2f);
        return;
    }
    if (!r.valid) {
        text(dl, f, g_title, 18, 320, 190, WHITE, "NOT PLAYED THIS SEASON", CENTER, 1.2f);
        text(dl, f, g_title, 12, 320, 220, GREY, "Five placement sets show your rank.", CENTER, 0.8f);
        return;
    }
    char num[64];
    bool placing = r.placements_left > 0;
    int tier = netranked_tier_of(r.division);
    rank_plate(dl, f, 180, 140, 260, upper(r.label).c_str(), tier, placing);
    snprintf(num, sizeof(num), "%d", r.rating);
    big_number(dl, f, 180, 170, num, r.provisional ? "RATING (PROVISIONAL)" : "RATING", CREAM);
    if (placing) {
        snprintf(num, sizeof(num), "%d OF %d PLACEMENT SETS PLAYED", r.placements_of - r.placements_left, r.placements_of);
        text(dl, f, g_title, 12, 180, 236, GREY, num, CENTER, 0.8f);
    } else {
        division_bar(dl, f, 60, 236, 240, r.rating, r.division, tier_colour(tier));
        if (r.division < NETRANK_DIVISIONS - 1) {
            snprintf(num, sizeof(num), "%d TO %s", r.to_next, division_label(r.division + 1).c_str());
            text(dl, f, g_title, 11, 300, 250, GREY, num, RIGHT, 0.8f);
        }
    }
    if (r.hint) {
        const char *h = r.hint == NETRANK_HINT_PROMOTION ? "NEXT SET: PROMOTION SET" : "NEXT SET: DEMOTION SET";
        text(dl, f, g_hand, 15, 180, 272, r.hint == NETRANK_HINT_PROMOTION ? GOLD : RED, h, CENTER, 0.9f);
    }
    snprintf(num, sizeof(num), "%d", r.set_wins);
    big_number(dl, f, 400, 130, num, "SETS WON", GREEN);
    snprintf(num, sizeof(num), "%d", r.set_losses);
    big_number(dl, f, 510, 130, num, "SETS LOST", IM_COL32(235, 90, 80, 255));
    snprintf(num, sizeof(num), "%d", r.peak);
    big_number(dl, f, 400, 220, num, "PEAK", CREAM);
    text(dl, f, g_title, 13, 510, 236, WHITE, r.peak_rank[0] ? upper(r.peak_rank).c_str() : "-", CENTER, 1.0f, 120);
    text(dl, f, g_hand, 13, 510, 260, ORANGE, "PEAK RANK", CENTER, 1.0f);
    snprintf(num, sizeof(num), "RATING DEVIATION %d", r.deviation);
    text(dl, f, g_title, 11, 52, 380, GREY, num, LEFT, 0.8f);
}

// ---- the gallery's ranked pages ----

void ranked_sample_rank(NetRankInfo &ri, int game, int w0, int w1, int picker, int winner)
{
    ri = NetRankInfo();
    ri.valid = 1;
    ri.mode = NETRANK_CLASSIC;
    ri.count = 2;
    ri.p[0] = { 0, 16, 0, 1872, w0, 0 };
    ri.p[1] = { 1, NETRANK_PLACEMENT, 3, 1500, w1, 1 };
    ri.has_set = 1;
    ri.game = game;
    ri.picker = picker;
    ri.winner = winner;
}

// The pages: ranked-modes, ranked-search, ranked-accept, ranked-room, ranked-vote, ranked-versus,
// ranked-results, ranked-results-placement, stats-ranked. Returns false for any other page.
bool ranked_gallery(const std::string &p)
{
    RG = RankedSample();
    if (p.compare(0, 6, "ranked") != 0 && p != "stats-ranked") return false;
    G.ranked = true;
    // Ann's ranks: Classic Kingpin II 1872 (a promotion set next), Open placing 2/5
    G.stats += "ranked_classic=1872|92|16|0|5|0|28|14|6|Kingpin II|1903|Kingpin I|promotion\n"
               "ranked_open=1524|260|9|3|5|1|0|2|0|PLACEMENT 2/5|1524|Contender III|\n";
    RG.queue.valid = 1;
    RG.queue.counts[NETRANK_OPEN] = 3; RG.queue.counts[NETRANK_CLASSIC] = 7; RG.queue.counts[NETRANK_DRAFT] = 0;
    RG.queue.mode = NETRANK_CLASSIC;
    RG.queue.accept_secs = 11;
    RG.queue.opp_rating = 1838;
    snprintf(RG.queue.code, sizeof(RG.queue.code), "MATCH1");
    R.primary = NETRANK_CLASSIC;
    R.also = true;
    if (p == "ranked-modes") { S.page = RANKED_MODES; }
    else if (p == "ranked" || p == "ranked-search") { S.page = RANKED; G.queue = NETQUEUE_SEARCHING; }
    else if (p == "ranked-accept") { S.page = RANKED; G.queue = NETQUEUE_ACCEPT; }
    else if (p == "ranked-room" || p == "ranked-vote") {
        S.page = HUB;
        G.room = sample_room(0, 2, 2);
        snprintf(G.room.settings.name, sizeof(G.room.settings.name), "Ranked");
        ranked_sample_rank(RG.rank, 2, 1, 0, 1, NETRANK_SET_RUNNING);
        if (p == "ranked-vote") {
            G.room.phase = 1;
            G.room.candidate_count = 3;
            G.room.candidates[0] = 2; G.room.candidates[1] = 9; G.room.candidates[2] = 13;
            G.room.ms_left = 12000;
        }
    } else if (p == "ranked-versus") {
        G.state = NETPLAY_CONNECTED;
        G.room = sample_room(0, 2, 2);
        G.room.phase = 2;
        G.versus = sample_versus("versus-1v1");
        snprintf(G.versus.room, sizeof(G.versus.room), "Ranked");
        G.versus.ms = 3000 + G.page_ms;     // settled: the cards in, the ranks on them
        ranked_sample_rank(RG.rank, 2, 1, 0, 1, NETRANK_SET_RUNNING);
    } else if (p == "ranked-results" || p == "ranked-results-placement") {
        S.page = RANKED_RESULTS;
        bool placing = p == "ranked-results-placement";
        ranked_sample_rank(R.set, 3, 2, 1, 0, 0);
        R.set.mode = placing ? NETRANK_OPEN : NETRANK_CLASSIC;
        R.set_you = 0;
        R.results_since = GetTickCount64();
        for (int m = 0; m < NETRANK_MODES; m++) R.before[m] = RG.after[m] = NetModeRank();
        netranked_parse_line("1872|92|16|0|5|0|28|14|6|Kingpin II|1903|Kingpin I|promotion", &R.before[NETRANK_CLASSIC]);
        netranked_parse_line("1906|88|17|0|5|0|44|15|6|Kingpin I|1906|Kingpin I|", &RG.after[NETRANK_CLASSIC]);
        netranked_parse_line("1524|260|9|3|5|1|0|2|0|PLACEMENT 2/5|1524|Contender III|", &R.before[NETRANK_OPEN]);
        netranked_parse_line("1561|231|10|2|5|1|0|3|0|PLACEMENT 3/5|1561|Contender II|", &RG.after[NETRANK_OPEN]);
        R.have_before = true;
    } else if (p == "stats-ranked") {
        S.page = STATS;
        S.stats_page = 0;
        R.stats_tab = 1 + NETRANK_CLASSIC;
    }
    return true;
}
