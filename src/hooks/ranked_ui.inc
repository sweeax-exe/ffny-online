// Ranked play's pages (ranked phase 2a: Classic and Open sets; phase 2b: Draft). Included into
// online_screen.cpp's anonymous namespace after its helpers (text, glass, the gallery's wrappers), so
// it is not a translation unit of its own (the hooks' CMake glob takes *.c and *.cpp only). The wire
// and the ladder are netplay_ranked.c; the lobby side is netplay.c (QUEUE, ACCEPT, RANKINFO).
//
//   RANKED_MODES    the mode chooser: Classic / Open / Draft, the "also search" toggle
//   RANKED          the search, the ACCEPT popup, the move to the set's relay
//   the room        the set's banner (mode, game, score, who picks the venue) and the rank on each card
//   the draft       a Draft set's bans, strikes and picks on the fighter grid and the venue cards
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
    // the draft: the grid's cursor and first row, the venue card, the cursor the relay has, the step seen
    int draft_sel = 0, draft_top = 0, venue_sel = 0, draft_cursor_sent = -1, draft_room_game = -1;
    char opp_name[40] = "";             // the set's opponent, for the results (build 52)
    char before_room[7] = "";           // the room whose "before" ranks were read
    NetRankPlayer last_opp = {};        // the opponent's entry in the set's last RANKINFO that had both
    bool have_last_opp = false;
} R;

// The gallery's samples (gallery_step sets them per page).
struct RankedSample {
    NetRankInfo rank = {};
    NetQueueExt queue = {};
    int accepted = -1;
    NetModeRank after[NETRANK_MODES] = {};
    NetDraft draft = {};
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

// The season (build 52): the stats text's season=<n>|<name>|<preseason 0/1>|<end unix s> line.
struct SeasonInfo { bool valid = false, pre = false; int n = 0; long long end = 0; std::string name; };
SeasonInfo read_season()
{
    static char buf[16384];
    SeasonInfo si;
    if (!stats_text(buf, sizeof(buf))) return si;
    const char *at = strstr(buf, "\nseason=");
    if (!at && !strncmp(buf, "season=", 7)) at = buf - 1;
    if (!at) return si;
    std::string l(at + 8);
    size_t nl = l.find('\n');
    if (nl != std::string::npos) l.resize(nl);
    std::vector<std::string> fs;
    size_t start = 0;
    for (;;) {
        size_t bar = l.find('|', start);
        fs.push_back(l.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    if (fs.size() < 3) return si;
    si.valid = true;
    si.n = atoi(fs[0].c_str());
    si.name = fs[1];
    si.pre = fs[2] == "1";
    si.end = fs.size() > 3 ? atoll(fs[3].c_str()) : 0;
    return si;
}

// "ENDS IN 5D 12H" (or hours and minutes on the last day); "" without an end.
std::string season_ends_in(const SeasonInfo &si)
{
    if (si.end <= 0) return "";
    long long left = si.end - (long long)time(nullptr);
    char b[48];
    if (left <= 0) snprintf(b, sizeof(b), "ENDING NOW");
    else if (left >= 86400) snprintf(b, sizeof(b), "ENDS IN %lldD %lldH", left / 86400, left % 86400 / 3600);
    else snprintf(b, sizeof(b), "ENDS IN %lldH %lldM", left / 3600, left % 3600 / 60);
    return b;
}

// The preseason's line: "PRESEASON - TEST WEEK - ENDS IN 5D 12H - RANKS RESET AT THE END".
std::string preseason_line(const SeasonInfo &si)
{
    std::string ends = season_ends_in(si);
    return std::string("PRESEASON  -  TEST WEEK  -  ") + (ends.empty() ? "" : ends + "  -  ") + "RANKS RESET AT THE END";
}

// A strip with the preseason's line, centred at cx (nothing outside a preseason).
void preseason_strip(ImDrawList *dl, const Frame &f, float cx, float y, float max_w, const char *extra = nullptr)
{
    SeasonInfo si = read_season();
    if (!si.valid || !si.pre) return;
    std::string l = preseason_line(si);
    float size = fit_size(f, g_title, 11, l.c_str(), max_w - 20);
    float w = text_width(f, g_title, size, l.c_str()) + 20, h = extra ? 30.0f : 18.0f;
    rect(dl, f, cx - w / 2, y, w, h, IM_COL32(120, 70, 8, 220), 5);
    frame_rect(dl, f, cx - w / 2, y, w, h, with_alpha(GOLD, 0.8f), 1.0f);
    text(dl, f, g_title, size, cx, y + 3, IM_COL32(255, 236, 190, 255), l.c_str(), CENTER, 0.6f);
    if (extra) text(dl, f, g_body, 8.5f, cx, y + 17, CREAM, extra, CENTER, 0);
}

std::string division_label(int division)
{
    char name[48];
    netranked_division_name(division, name, sizeof(name));
    return upper(name);
}

// A rank's colour by tier: the emblems' (rank_art.inc).
ImU32 tier_colour(int tier) { return tier_col(tier); }

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

// The fighter list for a ranked search (from the mode chooser, and back from a stopped search: players
// who stopped a search to take another fighter landed on the online menu, 2026-11-03). Draft has no pick.
void ranked_pick_fighter()
{
    if (R.primary == NETRANK_DRAFT) { S.page = RANKED_MODES; return; }
    S.ranked_pick = true;
    S.page = FIGHTERS;
    int fighter = saved_fighter();
    if (fighter == NETPLAY_CREATED_FIGHTER && R.primary != NETRANK_OPEN) fighter = 55;
    S.fighter_sel = grid_pos(grid_fighters(), fighter);
}

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
        "Three bans each, venue strikes and a blind pick, then winner picks first. Best of three, stock fighters." };
    int at = 0;
    for (int i = 0; i < 3; i++) if (order[i] == R.primary) at = i;
    if (pad.left) R.primary = order[(at + 2) % 3];
    if (pad.right) R.primary = order[(at + 1) % 3];
    for (int i = 0; i < 3; i++) if (order[i] == R.primary) at = i;
    if (pad.y) R.also = !R.also;

    NetModeRank ranks[NETRANK_MODES];
    read_ranks(ranks);
    page_title(dl, f, "RANKED MATCH");
    online_count(dl, f);
    const float w = 176, gap = 12, x0 = 320 - (3 * w + 2 * gap) / 2, y = 92, h = 252;
    for (int i = 0; i < 3; i++) {
        int m = order[i];
        float x = x0 + i * (w + gap);
        bool sel = i == at, draft = false;     // Draft is live since phase 2b
        if (mouse_pick(f, x, y, w, h)) R.primary = m;
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
                EmblemOpts eo;
                eo.rating = r.rating;
                eo.top = r.division >= NETRANK_DIVISIONS - 1 && r.place > 0 && r.place <= 10 ? r.place : 0;
                eo.alpha = sel ? 1.0f : 0.85f;
                emblem_big(dl, f, x + w / 2 - 36, y + 76, 72, tier, r.division, eo);
                text(dl, f, g_title, fit_size(f, g_title, 13, upper(r.label).c_str(), w - 16), x + w / 2, y + 148,
                     tier_colour(tier), upper(r.label).c_str(), CENTER, 0.9f);
                char num[16];
                snprintf(num, sizeof(num), "%d", r.rating);
                text(dl, f, g_title, 12, x + w / 2, y + 162, CREAM, num, CENTER, 0.8f);
            }
        }
        text(dl, f, g_body, 11, x + 10, y + 184, draft ? with_alpha(GREY, 0.7f) : with_alpha(WHITE, 0.85f), descs[i], LEFT, 0.6f, w - 20);
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
    preseason_strip(dl, f, 320, 58, 420, "A test week: bugs may happen, and every rank resets when Season 1 starts.");
    float hx = 54;
    hx += hint(dl, f, hx, 410, "a", R.primary == NETRANK_DRAFT ? "SEARCH" : "CHOOSE FIGHTER");
    hx += hint(dl, f, hx, 410, "y", R.also ? "ONLY THIS MODE" : "ALSO SEARCH");
    hint(dl, f, hx, 410, "b", "BACK");
    if (pad.b) { S.page = HUB; return; }
    if (pad.a && R.primary == NETRANK_DRAFT) {   // the fighters are drafted: straight to the search
        int fighter = saved_fighter();
        if (fighter == NETPLAY_CREATED_FIGHTER) fighter = 55;
        ranked_search(fighter);
        S.page = RANKED;
        return;
    }
    if (pad.a) ranked_pick_fighter();
}

// The modes searched: the primary, and the other one when "also search" is on (a created fighter
// plays Open only, so Classic is left out then).
unsigned ranked_modes_for(int fighter)
{
    unsigned m = 1u << R.primary;
    if (R.also) m |= 1u << (R.primary == NETRANK_CLASSIC ? NETRANK_OPEN : NETRANK_CLASSIC);
    if (fighter == NETPLAY_CREATED_FIGHTER) m &= ~((1u << NETRANK_CLASSIC) | (1u << NETRANK_DRAFT));
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
        if (pad.b) ranked_pick_fighter();
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
                bool live = true;
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
    if (pad.b) { netplay_lobby_leave(); ranked_pick_fighter(); say("You left the ranked queue."); }
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
    if (ri.mode == NETRANK_DRAFT) return ri.game <= 1 ? "Both ready: the draft starts." : "Both ready: the next game's draft starts.";
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
    if (placing) {
        rank_plate(dl, f, x + w / 2, y, w - 12, upper(label).c_str(), 0, true);
        return;
    }
    int tier = netranked_tier_of(p->division);
    emblem_small(dl, f, x + 16, y + 8, 22, tier);
    rank_plate(dl, f, x + w / 2 + 10, y, w - 34, upper(label).c_str(), tier, false);
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
    snprintf(line, sizeof(line), "RANKED %s  -  GAME %d  -  SET %d-%d", netranked_mode_title(ri.mode), ri.game,
             p0 ? p0->set_wins : 0, p1 ? p1->set_wins : 0);
    text(dl, f, g_title, 14, 320, 58, ORANGE, line, CENTER, 1.0f);
    for (int s = 0; s < 2; s++) {
        const NetRankPlayer *p = s == 0 ? p0 : p1;
        if (!p) continue;
        char label[48], rating[16];
        netranked_player_label(p, 5, label, sizeof(label));
        bool placing = p->division == NETRANK_PLACEMENT || p->placements > 0;
        float x = s == 0 ? 36 : 640 - 36 - 196;
        // on the portrait's top left ("YOU" sits on its right corner); the 48 emblem beside the plate
        if (placing) {
            rank_plate(dl, f, x + 72, 78, 120, upper(label).c_str(), 0, true);
        } else {
            int tier = netranked_tier_of(p->division);
            emblem_small(dl, f, x + 20, 90, 40, tier);
            rank_plate(dl, f, x + 92, 78, 104, upper(label).c_str(), tier, false);
            snprintf(rating, sizeof(rating), "%d", p->rating);
            text(dl, f, g_title, 11, x + 92, 97, CREAM, rating, CENTER, 0.8f);
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
    for (int i = 0; i < r.player_count; i++)       // the room's other fighter (ranked_tick kept it during the set)
        if (r.players[i].slot != r.you && r.players[i].slot != NETPLAY_SLOT_SPECTATOR)
            snprintf(R.opp_name, sizeof(R.opp_name), "%s", r.players[i].name);
    R.have_before = false;      // the next set reads its own "before" (ranked_search, or ranked_tick)
    bool opp_in = false;
    for (int i = 0; i < R.set.count; i++) opp_in |= R.set.p[i].slot != r.you;
    if (!opp_in && R.have_last_opp && R.set.count < 4) {   // it left the room first: its last entry, the set's end
        NetRankPlayer o = R.last_opp;
        if (R.set.winner == o.slot) o.set_wins = std::max(o.set_wins, 2);
        R.set.p[R.set.count++] = o;
    }
    R.have_last_opp = false;
    R.results_up = true;
    R.results_since = R.results_fetch = GetTickCount64();
    S.page = RANKED_RESULTS;
    if (!gallery_on()) {
        netplay_lobby_leave();      // the set is over: nothing more starts in its room
        account_fetch_stats_page(0);
    }
}

// The highest season-floor tier at or below `tier` (Contender 3, Kingpin 5, Legend 7), -1 none.
int floor_tier_of(int tier)
{
    for (int t = tier; t >= 0; t--) if (t == 3 || t == 5 || t == 7) return t;
    return -1;
}

std::string title_case(const std::string &s)
{
    std::string out = s;
    bool start = true;
    for (char &c : out) {
        if (isalpha((unsigned char)c)) { c = start ? (char)toupper((unsigned char)c) : (char)tolower((unsigned char)c); start = false; }
        else start = c == ' ';
    }
    // the division numerals stay upper case
    for (const char *num : { " Iii", " Ii", " I" }) {
        size_t at = out.rfind(num);
        if (at != std::string::npos && at + strlen(num) == out.size())
            for (size_t i = at + 1; i < out.size(); i++) out[i] = (char)toupper((unsigned char)out[i]);
    }
    return out;
}

// The light behind a promotion: rays turning slowly round cx, cy.
void promo_rays(ImDrawList *dl, const Frame &f, float cx, float cy, float r, ImU32 col, float alpha)
{
    float spin = gallery_on() ? 0.0f : (float)(GetTickCount64() % 60000) / 60000.0f * 6.2831853f;
    ImVec2 c = f.p(cx, cy);
    const int n = 22;
    for (int i = 0; i < n; i++) {
        float a0 = spin + i * 6.2831853f / n, a1 = a0 + 0.07f;
        ImVec2 p1(c.x + cosf(a0) * r * f.s(), c.y + sinf(a0) * r * f.s()), p2(c.x + cosf(a1) * r * f.s(), c.y + sinf(a1) * r * f.s());
        dl->AddTriangleFilled(c, p1, p2, with_alpha(col, alpha * (i % 2 ? 0.10f : 0.18f)));
    }
    for (int g = 6; g >= 1; g--) dl->AddCircleFilled(c, r * 0.12f * g * f.s(), with_alpha(col, alpha * 0.035f), 40);
}

// One of the two cards under the bar: a promotion shield, a season floor.
void results_card(ImDrawList *dl, const Frame &f, float x, float y, float w, int icon, const char *head, const char *line)
{
    glass(dl, f, x, y, w, 36, 8, IM_COL32(12, 10, 12, 255), 0.75f);
    if (icon == 0) {        // three small shields
        for (int i = 0; i < 3; i++) {
            float sx = x + 12 + i * 9;
            ImVec2 p[5] = { f.p(sx, y + 12), f.p(sx + 7, y + 12), f.p(sx + 7, y + 18), f.p(sx + 3.5f, y + 23), f.p(sx, y + 18) };
            dl->AddConvexPolyFilled(p, 5, GOLD);
        }
    } else {                // a padlock
        rect(dl, f, x + 14, y + 16, 14, 11, GOLD, 2);
        dl->AddCircle(f.p(x + 21, y + 16), 4.5f * f.s(), GOLD, 16, 2.0f * f.s());
    }
    text(dl, f, g_title, 11, x + 44, y + 5, GOLD, head, LEFT, 0.8f);
    text(dl, f, g_body, 9.5f, x + 44, y + 19, CREAM, line, LEFT, 0, w - 50);
}

// After a set (the owner's approved design "Rank up"): VICTORY / DEFEAT on the left with the opponent, the
// emblem in the middle (PROMOTED! with rays and the new chevron's pulse, DEMOTED with the lost one fading,
// PLACED!, or the placement progress), the rating and the season's numbers on the right.
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
    char line[128];
    page_title(dl, f, "SET OVER");
    dl->AddLine(f.p(50, 60), f.p(150, 60), RED, 2.5f * f.s());

    // the left panel: the result, the set, the opponent
    glass(dl, f, 20, 96, 158, 150, 10, IM_COL32(10, 9, 11, 255), 0.72f);
    text(dl, f, g_stencil, 30, 32, 104, won ? GREEN : level ? CREAM : with_alpha(RED, 1.0f), won ? "VICTORY" : level ? "LEVEL" : "DEFEAT",
         LEFT, 1.6f);
    snprintf(line, sizeof(line), "RANKED %s, SET %d-%d", netranked_mode_title(mode), me ? me->set_wins : 0, opp ? opp->set_wins : 0);
    text(dl, f, g_title, fit_size(f, g_title, 12, line, 140), 32, 140, ORANGE, line, LEFT, 0.8f);
    text(dl, f, g_title, 8.5f, 32, 162, GREY, "OPPONENT", LEFT, 0.5f);
    if (R.opp_name[0]) {
        std::string oname = upper(R.opp_name);
        text(dl, f, g_title, fit_size(f, g_title, 14, oname.c_str(), 140), 32, 173, CREAM, oname.c_str(), LEFT, 0.8f);
    }
    if (opp) {
        char olabel[48];
        netranked_player_label(opp, 5, olabel, sizeof(olabel));
        bool oplacing = opp->division == NETRANK_PLACEMENT || opp->placements > 0;
        int otier = netranked_tier_of(oplacing ? 0 : opp->division);
        if (!oplacing) emblem_small(dl, f, 39, 199, 15, otier);
        text(dl, f, g_title, 11, oplacing ? 32.0f : 50.0f, 193, oplacing ? GREY : tier_colour(otier), upper(olabel).c_str(), LEFT, 0.7f);
        snprintf(line, sizeof(line), "RATING %d", opp->rating);
        text(dl, f, g_title, 10, 32, 214, GREY, line, LEFT, 0.6f);
    }

    if (!rated) {
        float dots = (float)(t % 1200) / 1200.0f;
        text(dl, f, g_hand, 18, 320, 200, CREAM, t - R.results_since < 60000 ? "UPDATING YOUR RATING..." : "YOUR RATING UPDATES SOON",
             CENTER, 1.0f);
        rect(dl, f, 260 + 80 * dots, 232, 40, 4, ORANGE, 2);
        text(dl, f, g_title, 11, 320, 248, GREY, "The set counts once both games' results are in.", CENTER, 0.8f, 240);
    } else {
        int delta = a.rating - (b.valid ? b.rating : 1500);
        bool placing = a.placements_left > 0;
        int tier = netranked_tier_of(a.division);
        bool promoted = b.valid && b.placements_left == 0 && a.division > b.division;
        bool demoted = b.valid && b.placements_left == 0 && a.division < b.division;
        bool placed = b.valid && b.placements_left > 0 && !placing;
        ImU32 tc = tier_colour(tier);
        if (placing) {
            emblem_fallback(dl, f, 320, 160, 46, 0, 0.25f);
            text(dl, f, g_stencil, 40, 320, 140, with_alpha(CREAM, 0.9f), "?", CENTER, 1.0f);
            snprintf(line, sizeof(line), "PLACEMENT %d / %d", a.placements_of - a.placements_left, a.placements_of);
            text(dl, f, g_stencil, 26, 320, 222, CREAM, line, CENTER, 1.6f);
            float w = 220, x = 320 - w / 2;
            for (int i = 0; i < a.placements_of && a.placements_of <= 10; i++) {
                float cw = (w - (a.placements_of - 1) * 6) / a.placements_of;
                rect(dl, f, x + i * (cw + 6), 260, cw, 10, i < a.placements_of - a.placements_left ? ORANGE : IM_COL32(255, 255, 255, 30), 4);
            }
            text(dl, f, g_title, 11, 320, 280, GREY, "Your emblem shows after the placement sets.", CENTER, 0.8f);
        } else {
            if (promoted || placed) promo_rays(dl, f, 320, 160, 150, tc, 1.0f);
            if (promoted || placed || demoted) {
                const char *what = promoted ? "PROMOTED!" : placed ? "PLACED!" : "DEMOTED";
                ImU32 hc = promoted || placed ? IM_COL32(255, 214, 90, 255) : with_alpha(RED, 1.0f);
                for (int g = 3; g >= 1; g--)
                    text(dl, f, g_hand, 44, 320, 46 - g * 0.5f, with_alpha(hc, 0.10f), what, CENTER, (float)g * 2.0f);
                text(dl, f, g_hand, 44, 320, 44, hc, what, CENTER, 1.6f);
            }
            EmblemOpts eo;
            eo.fresh = promoted || placed;
            eo.lost = demoted;
            eo.rating = a.rating;
            eo.top = tier >= 7 && a.place > 0 && a.place <= 10 ? a.place : 0;
            eo.glow = promoted || placed;
            emblem_big(dl, f, 320 - 70, 96, 140, tier, a.division, eo);
            std::string name = division_label(a.division);
            text(dl, f, g_stencil, 30, 320, 236, CREAM, name.c_str(), CENTER, 1.6f);
            if ((promoted || demoted) && b.valid) {
                std::string from = division_label(b.division), to = name;
                float fw = text_width(f, g_title, 12, from.c_str()), tw = text_width(f, g_title, 12, to.c_str());
                float total = 16 + fw + 30 + 16 + tw, x = 320 - total / 2;
                emblem_small(dl, f, x + 6, 280, 13, netranked_tier_of(b.division), 0.9f);
                text(dl, f, g_title, 12, x + 16, 273, GREY, from.c_str(), LEFT, 0.6f);
                x += 16 + fw + 4;
                text(dl, f, g_title, 13, x + 11, 272, demoted ? with_alpha(RED, 1.0f) : ORANGE, "->", CENTER, 0.6f);
                x += 26;
                emblem_small(dl, f, x + 6, 280, 13, tier);
                text(dl, f, g_title, 12, x + 16, 273, tc, to.c_str(), LEFT, 0.6f);
            }
            // the tier's bar: its three divisions (Legend: the rating over 2100)
            float bx = 200, bw = 240, by = 296;
            int first = tier * 3, cur = a.division;
            if (tier < 7) {
                for (int d = 0; d < 3; d++) {
                    float sx = bx + d * (bw / 3) + (d ? 2 : 0), sw = bw / 3 - (d ? 2 : 0);
                    rect(dl, f, sx, by, sw, 9, IM_COL32(255, 255, 255, 26), 4);
                    float fill = first + d < cur ? 1.0f : first + d == cur ? netranked_division_percent(a.rating, cur) / 100.0f : 0.0f;
                    if (fill > 0) rect(dl, f, sx, by, std::max(6.0f, sw * fill), 9, tc, 4);
                    char lab[24];
                    snprintf(lab, sizeof(lab), "%d %s", netranked_division_min(first + d) < 0 ? 0 : netranked_division_min(first + d),
                             d == 0 ? "III" : d == 1 ? "II" : "I");
                    if (first + d == 0) snprintf(lab, sizeof(lab), "III");
                    text(dl, f, g_title, 8, sx, by + 12, first + d == cur ? CREAM : GREY, lab, LEFT, 0.5f);
                }
                snprintf(line, sizeof(line), "%d %s", netranked_division_min(first + 3), k_tier_short[tier + 1]);
                text(dl, f, g_title, 8, bx + bw, by + 12, tier_colour(tier + 1), line, RIGHT, 0.5f);
                snprintf(line, sizeof(line), "%d TO %s", a.to_next, division_label(a.division + 1).c_str());
                text(dl, f, g_title, 13, bx, by + 25, CREAM, line, LEFT, 0.8f);
                text(dl, f, g_body, 9, bx + bw, by + 28, GREY, "Points per division: 50", RIGHT, 0);
            } else {
                rect(dl, f, bx, by, bw, 9, tc, 4);
                text(dl, f, g_title, 11, 320, by + 14, CREAM, eo.top ? "TOP 10 OF NEW YORK" : "2100 AND UP: EVERY LEGEND", CENTER, 0.6f);
            }
            // the cards: the promotion's shield, the season's floor
            int fl = floor_tier_of(tier);
            bool shield = promoted || placed, floor_new = fl >= 0 && (promoted || placed) && (!b.valid || netranked_tier_of(b.division) < fl ||
                                                                                          b.placements_left > 0);
            float cy = 348, cw = 168;
            int cards = (shield ? 1 : 0) + (floor_new ? 1 : 0);
            float cx = 320 - (cards * cw + (cards - 1) * 8) / 2;
            if (shield) {
                results_card(dl, f, cx, cy, cw, 0, "PROMOTION SHIELD", "No demotion for the next 3 sets");
                cx += cw + 8;
            }
            if (floor_new) {
                snprintf(line, sizeof(line), "You stay %s until the season ends", title_case(k_tier_short[fl]).c_str());
                results_card(dl, f, cx, cy, cw, 1, "SEASON FLOOR", line);
            }
            if (!shield && (a.hint == NETRANK_HINT_PROMOTION || a.hint == NETRANK_HINT_DEMOTION)) {
                const char *h = a.hint == NETRANK_HINT_PROMOTION ? "NEXT: PROMOTION SET" : "NEXT: DEMOTION SET";
                float w = text_width(f, g_title, 13, h) + 24;
                rect(dl, f, 320 - w / 2, 352, w, 22, a.hint == NETRANK_HINT_PROMOTION ? IM_COL32(120, 90, 10, 230) : IM_COL32(120, 12, 26, 230), 6);
                text(dl, f, g_title, 13, 320, 356, WHITE, h, CENTER, 1.0f);
            }
        }
        // the right panel: the rating and the season's numbers
        glass(dl, f, 462, 96, 158, 180, 10, IM_COL32(10, 9, 11, 255), 0.72f);
        text(dl, f, g_title, 9, 474, 104, GREY, "RATING", LEFT, 0.5f);
        snprintf(line, sizeof(line), "%d", a.rating);
        text(dl, f, g_title, 36, 474, 114, CREAM, line, LEFT, 1.6f);
        float rw = text_width(f, g_title, 36, line);
        snprintf(line, sizeof(line), "%+d", delta);
        text(dl, f, g_title, 18, 480 + rw, 130, delta >= 0 ? GREEN : with_alpha(RED, 1.0f), line, LEFT, 1.0f);
        if (b.valid) {
            snprintf(line, sizeof(line), "%s from %d", delta >= 0 ? "Up" : "Down", b.rating);
            text(dl, f, g_body, 9.5f, 474, 158, GREY, line, LEFT, 0);
        }
        float ry = 176;
        auto row = [&](const char *k, const char *v, ImU32 col) {
            dl->AddLine(f.p(474, ry - 3), f.p(608, ry - 3), IM_COL32(255, 255, 255, 22), 1.0f);
            text(dl, f, g_body, 9.5f, 474, ry + 1, CREAM, k, LEFT, 0);
            text(dl, f, g_title, 12, 608, ry - 1, col, v, RIGHT, 0.6f);
            ry += 24;
        };
        snprintf(line, sizeof(line), "%d L", a.set_losses);
        row("Sets", line, IM_COL32(255, 107, 126, 255));
        {
            char w2[16];
            snprintf(w2, sizeof(w2), "%d W", a.set_wins);
            text(dl, f, g_title, 12, 608 - text_width(f, g_title, 12, line) - 8, ry - 25, GREEN, w2, RIGHT, 0.6f);
        }
        bool new_peak = b.valid && a.peak > b.peak;
        snprintf(line, sizeof(line), "%d", a.peak);
        row("Peak", line, CREAM);
        if (new_peak) {
            float pw = text_width(f, g_title, 12, line);
            rect(dl, f, 608 - pw - 46, ry - 24, 40, 12, GOLD, 2);
            text(dl, f, g_title, 7.5f, 608 - pw - 26, ry - 23, BLACK, "NEW PEAK", CENTER, 0);
        }
        if (a.place > 0) snprintf(line, sizeof(line), "#%d", a.place);
        else snprintf(line, sizeof(line), "-");
        std::string ladder = title_case(netranked_mode_title(mode)) + " ladder";
        row(ladder.c_str(), line, CREAM);
        int fl = placing ? -1 : floor_tier_of(netranked_tier_of(a.division));
        std::string fl_name = fl >= 0 ? title_case(k_tier_short[fl]) : std::string("-");
        row("Season floor", fl_name.c_str(), fl >= 0 ? tier_colour(fl) : GREY);
    }
    preseason_strip(dl, f, 320, 394, 420);
    float hx = 54;
    // A (search again) only once the rating shows, and 3 s after: the A pressed through the game's
    // own results screens must not skip this one
    static unsigned long long rated_at;
    if (!rated) rated_at = 0;
    else if (!rated_at) rated_at = t;
    // (a scripted ranked test keeps pressing A through the fight's screens: its results stay up for the capture)
    static const bool test_set = getenv("RECOMP_TEST_RANKED_MODES") != nullptr;
    bool take_a = gallery_on() || (rated && t - rated_at > 3000 && !test_set);
    bool rewards = rewards_shown();
    if (take_a) hx += hint(dl, f, hx, 420, "a", "SEARCH AGAIN");
    if (rewards) hx += hint(dl, f, hx, 420, "y", "SEASON REWARDS");
    hint(dl, f, hx, 420, "b", "BACK");
    if (pad.a && take_a) open_ranked_modes();
    else if (pad.y && rewards) open_season_rewards(mode);
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
    // a set entered without this PC's search (a scripted test's queue): its ranks before it, from the stats
    // text read at the hub
    for (int i = 0; ri.count >= 2 && i < ri.count; i++)  // the opponent's rank, in case it leaves first
        if (ri.p[i].slot != r.you) { R.last_opp = ri.p[i]; R.have_last_opp = true; }
    if (ri.winner == NETRANK_SET_RUNNING) {          // the opponent's name, for the results
        for (int i = 0; i < r.player_count; i++)
            if (r.players[i].slot != r.you && r.players[i].slot != NETPLAY_SLOT_SPECTATOR)
                snprintf(R.opp_name, sizeof(R.opp_name), "%s", r.players[i].name);
    }
    if (!R.have_before && ri.winner == NETRANK_SET_RUNNING && strcmp(R.before_room, r.code)) {
        read_ranks(R.before);
        if (!R.before[ri.mode].valid) {     // not asked yet: ask, read again next frame
            static unsigned long long asked;
            if (!stats_busy() && GetTickCount64() - asked > 3000) { asked = GetTickCount64(); account_fetch_stats_page(0); }
        } else {
            R.have_before = true;
            snprintf(R.before_room, sizeof(R.before_room), "%s", r.code);
        }
    }
    if (ri.winner != NETRANK_SET_RUNNING && r.phase == 0 && !(R.results_up && !strcmp(R.set_room, r.code)))
        open_ranked_results(r, ri);
}

// ---- MY STATS: a tab per mode ----

void draw_ranked_stats(ImDrawList *dl, const Frame &f, int mode)
{
    NetModeRank ranks[NETRANK_MODES];
    read_ranks(ranks);
    const NetModeRank &r = ranks[mode];
    // the left panel (as the "Season rewards" design's): the mode, the emblem, the rank
    glass(dl, f, 24, 80, 196, 326, 10, IM_COL32(10, 9, 11, 255), 0.72f);
    text(dl, f, g_title, 12, 122, 88, ORANGE, (std::string("RANKED ") + netranked_mode_title(mode)).c_str(), CENTER, 0.8f);
    if (!r.valid) {
        emblem_fallback(dl, f, 122, 170, 46, 0, 0.25f);
        text(dl, f, g_stencil, 40, 122, 150, with_alpha(CREAM, 0.9f), "?", CENTER, 1.0f);
        text(dl, f, g_title, 15, 122, 236, WHITE, "NOT PLAYED THIS SEASON", CENTER, 1.0f, 180);
        text(dl, f, g_title, 11, 122, 276, GREY, "Five placement sets show your rank.", CENTER, 0.8f, 170);
        glass(dl, f, 232, 80, 384, 326, 10);
        text(dl, f, g_title, 13, 424, 230, GREY, "Your sets, peak and ladder place show here once you play.", CENTER, 0.8f, 300);
        preseason_strip(dl, f, 424, 346, 370, "A test week: bugs may happen, and every rank resets when Season 1 starts.");
        return;
    }
    char num[64];
    bool placing = r.placements_left > 0;
    int tier = netranked_tier_of(r.division);
    bool legend = r.division >= NETRANK_DIVISIONS - 1;
    if (placing) {
        emblem_fallback(dl, f, 122, 170, 46, 0, 0.25f);
        text(dl, f, g_stencil, 40, 122, 150, with_alpha(CREAM, 0.9f), "?", CENTER, 1.0f);
        text(dl, f, g_stencil, 22, 122, 236, CREAM, upper(r.label).c_str(), CENTER, 1.2f, 190);
        snprintf(num, sizeof(num), "%d OF %d PLACEMENT SETS PLAYED", r.placements_of - r.placements_left, r.placements_of);
        text(dl, f, g_title, 10, 122, 266, GREY, num, CENTER, 0.6f);
    } else {
        EmblemOpts eo;
        eo.rating = r.rating;
        eo.top = legend && r.place > 0 && r.place <= 10 ? r.place : 0;
        emblem_big(dl, f, 122 - 62, 104, 124, tier, r.division, eo);
        std::string lab = upper(r.label);
        text(dl, f, g_stencil, fit_size(f, g_stencil, 22, lab.c_str(), 186), 122, 230, CREAM, lab.c_str(), CENTER, 1.2f);
        text(dl, f, g_body, 9.5f, 122, 256, GREY, r.provisional ? "Your rank this season (provisional)" : "Your rank this season",
             CENTER, 0);
    }
    float ry = 282;
    auto row = [&](const char *k, const char *v, ImU32 col) {
        dl->AddLine(f.p(36, ry - 4), f.p(208, ry - 4), IM_COL32(255, 255, 255, 22), 1.0f);
        text(dl, f, g_body, 9.5f, 36, ry, CREAM, k, LEFT, 0);
        text(dl, f, g_title, 12, 208, ry - 2, col, v, RIGHT, 0.6f);
        ry += 22;
    };
    snprintf(num, sizeof(num), "%d", r.rating);
    row(r.provisional ? "Rating (provisional)" : "Rating", num, CREAM);
    if (r.peak_rank[0]) snprintf(num, sizeof(num), "%d, %s", r.peak, title_case(r.peak_rank).c_str());
    else snprintf(num, sizeof(num), "%d", r.peak);
    row("Peak", num, CREAM);
    if (r.place > 0 && r.of > 0) snprintf(num, sizeof(num), "#%d of %d", r.place, r.of);
    else snprintf(num, sizeof(num), "-");
    std::string ladder = title_case(netranked_mode_title(mode)) + " ladder";
    row(ladder.c_str(), num, CREAM);
    snprintf(num, sizeof(num), "%d W  %d L", r.set_wins, r.set_losses);
    row("Sets", num, GREEN);
    snprintf(num, sizeof(num), "Rating deviation %d", r.deviation);
    text(dl, f, g_body, 8.5f, 36, 388, GREY, num, LEFT, 0);

    // the right side: the division bar, the next set, the record
    glass(dl, f, 232, 80, 384, 326, 10);
    if (!placing) {
        text(dl, f, g_title, 9, 248, 92, GREY, legend ? "DEF JAM LEGEND" : "TO THE NEXT DIVISION", LEFT, 0.5f);
        division_bar(dl, f, 248, 108, 352, r.rating, r.division, tier_colour(tier));
        if (r.division < NETRANK_DIVISIONS - 1) {
            snprintf(num, sizeof(num), "%d TO %s", r.to_next, division_label(r.division + 1).c_str());
            text(dl, f, g_title, 13, 248, 122, CREAM, num, LEFT, 0.8f);
        } else {
            text(dl, f, g_title, 13, 248, 122, CREAM, r.place > 0 && r.place <= 10 ? "TOP 10 OF NEW YORK" : "2100 AND UP", LEFT, 0.8f);
        }
        int fl = floor_tier_of(tier);
        if (fl >= 0) {
            snprintf(num, sizeof(num), "Season floor: you stay %s until the season ends", title_case(k_tier_short[fl]).c_str());
            text(dl, f, g_body, 9.5f, 600, 125, GOLD, num, RIGHT, 0);
        }
    }
    if (r.hint) {
        const char *h = r.hint == NETRANK_HINT_PROMOTION ? "NEXT SET: PROMOTION SET" : "NEXT SET: DEMOTION SET";
        text(dl, f, g_hand, 15, 424, 146, r.hint == NETRANK_HINT_PROMOTION ? GOLD : RED, h, CENTER, 0.9f);
    }
    snprintf(num, sizeof(num), "%d", r.set_wins);
    big_number(dl, f, 300, 186, num, "SETS WON", GREEN);
    snprintf(num, sizeof(num), "%d", r.set_losses);
    big_number(dl, f, 424, 186, num, "SETS LOST", IM_COL32(235, 90, 80, 255));
    snprintf(num, sizeof(num), "%d", r.peak);
    big_number(dl, f, 548, 186, num, "PEAK", CREAM);
    // every tier, the reached ones lit: where this rank sits on the ladder
    for (int t = 0; t < 8; t++) {
        float cx = 262 + t * 46;
        bool reached = !placing && t <= tier;
        emblem_small(dl, f, cx, 300, t == tier && !placing ? 38 : 30, t, reached ? 1.0f : 0.28f);
        text(dl, f, g_title, 7.5f, cx, 322, reached ? tier_colour(t) : with_alpha(GREY, 0.6f), k_tier_short[t], CENTER, 0.4f);
    }
    SeasonInfo si = read_season();
    if (si.valid && si.pre) preseason_strip(dl, f, 424, 346, 370, "No tier rewards this week: one set earns the Preseason Tester title.");
    else if (rewards_shown()) text(dl, f, g_body, 9.5f, 424, 352, GREY,
                                   "Finish the season at a tier to unlock its rewards: SEASON REWARDS tab.", CENTER, 0, 340);
}

// ---- Draft: the pick and ban (ranked phase 2b; the rules are netplay_draft_rules.c) ----

void ranked_sample_rank(NetRankInfo &ri, int game, int w0, int w1, int picker, int winner);

const ImU32 DRAFT_BLUE = IM_COL32(90, 170, 255, 255);

int lobby_draft(NetDraft *out)
{
    if (gallery_on()) { *out = RG.draft; return RG.draft.valid; }
    return netplay_lobby_draft(out);
}

// Whether the room shows the draft: a Draft set's room with its pick and ban running, or the
// drafted fighters being revealed (the room's "vote" of the one venue).
bool draft_shown(const NetRoom &r, NetDraft *d)
{
    NetRankInfo ri;
    if (!ranked_room(r, &ri) || ri.mode != NETRANK_DRAFT || !lobby_draft(d) || r.you < 0 || r.you > 1) return false;
    if (r.phase == 0) return netdraft_active(d);
    return r.phase == 1 && d->step == NETDRAFT_PLAYING;
}

// The seconds left in the turn: the relay's, counted down here between its DRAFTs.
int draft_secs(const NetDraft &d)
{
    static int step = -1, turn = -1, game = -1, secs0 = -1, bans = -1, struck = -1;
    static unsigned long long since;
    if (gallery_on()) return d.secs;
    if (d.step != step || d.turn != turn || d.game != game || d.secs != secs0 || d.ban_count != bans || d.struck_count != struck) {
        step = d.step; turn = d.turn; game = d.game; secs0 = d.secs; bans = d.ban_count; struck = d.struck_count;
        since = GetTickCount64();
    }
    return std::max(0, d.secs - (int)((GetTickCount64() - since) / 1000));
}

// A red X over a tile.
void draft_cross(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, float thick = 3.0f)
{
    float m = std::min(w, h) * 0.12f;
    dl->AddLine(f.p(x + m, y + m), f.p(x + w - m, y + h - m), RED, thick * f.s());
    dl->AddLine(f.p(x + w - m, y + m), f.p(x + m, y + h - m), RED, thick * f.s());
}

// A padlock centred at (cx, cy), `w` wide.
void draft_padlock(ImDrawList *dl, const Frame &f, float cx, float cy, float w, ImU32 col = GOLD)
{
    float bw = w, bh = w * 0.75f, bx = cx - bw / 2, by = cy - bh / 2 + w * 0.2f;
    dl->PathArcTo(f.p(cx, by), w * 0.3f * f.s(), 3.14159f, 2 * 3.14159f, 12);
    dl->PathStroke(col, 0, w * 0.13f * f.s());
    rect(dl, f, bx, by, bw, bh, col, 2);
    dl->AddCircleFilled(f.p(cx, by + bh * 0.45f), w * 0.1f * f.s(), IM_COL32(40, 30, 10, 255));
}

// One small fighter tile in a ban strip: a ban (red X), a skipped one (dash) or one still to come.
void draft_ban_tile(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, int id, bool made)
{
    rect(dl, f, x, y, w, h, IM_COL32(0, 0, 0, 170), 3);
    if (!made) { frame_rect(dl, f, x, y, w, h, with_alpha(GREY, 0.5f), 1.0f); return; }
    if (id == NETDRAFT_NONE) { text(dl, f, g_title, 14, x + w / 2, y + h / 2 - 8, GREY, "-", CENTER, 0.6f); return; }
    if (!portrait(dl, f, id, x + 1, y + 1, w - 2, h - 2, IM_COL32(120, 120, 120, 255)))
        text(dl, f, g_body, 8, x + 2, y + h / 2 - 5, GREY, online_fighter_name(id), LEFT, 0, w - 4);
    draft_cross(dl, f, x, y, w, h, 2.0f);
}

// A player's side: who, the fighter (their pick, or the cursor they rest on), and their bans.
void draft_side(ImDrawList *dl, const Frame &f, const NetRoom &r, const NetDraft &d, int slot, float x, float y, bool me,
                int hover)
{
    const float w = 140, h = 256;
    bool turn = netdraft_my_turn(&d, slot);
    glass(dl, f, x, y, w, h, 10, turn ? IM_COL32(56, 14, 20, 255) : IM_COL32(10, 10, 13, 255), turn ? 0.72f : 0.6f);
    if (turn) frame_rect(dl, f, x, y, w, h, with_alpha(me ? GOLD : DRAFT_BLUE, 0.85f), 2.0f);
    text(dl, f, g_title, 12, x + w / 2, y + 6, me ? GOLD : DRAFT_BLUE, me ? "YOU" : upper(slot_name(r, slot)).c_str(),
         CENTER, 0.8f, w - 8);
    int pick = d.picks[slot], shown = pick != NETDRAFT_NONE ? pick : me ? hover : d.cursors[slot];
    bool hidden = !me && d.step == NETDRAFT_BLIND;
    float px = x + 10, py = y + 24, pw = w - 20, ph = 140;
    rect(dl, f, px, py, pw, ph, IM_COL32(0, 0, 0, 160), 4);
    if (hidden) {
        text(dl, f, g_stencil, 54, px + pw / 2, py + 40, with_alpha(CREAM, 0.5f), "?", CENTER, 1.4f);
        if (d.locked[slot]) {
            draft_padlock(dl, f, px + pw / 2, py + 110, 22);
            text(dl, f, g_title, 11, px + pw / 2, py + ph + 4, GOLD, "LOCKED IN", CENTER, 0.8f);
        }
    } else if (shown != NETDRAFT_NONE) {
        bool locked = pick != NETDRAFT_NONE;
        if (!portrait(dl, f, shown, px, py, pw, ph, locked ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255)))
            text(dl, f, g_title, 14, px + pw / 2, py + 60, CREAM, online_fighter_name(shown), CENTER, 1.0f, pw - 6);
        if (locked) frame_rect(dl, f, px, py, pw, ph, me ? GOLD : DRAFT_BLUE, 2.5f);
        float size = fit_size(f, g_hand, 15, online_fighter_name(shown), w - 10);
        text(dl, f, g_hand, size, x + w / 2, py + ph + 3, locked ? (me ? GOLD : DRAFT_BLUE) : GREY,
             online_fighter_name(shown), CENTER, 0.8f);
    }
    // the bans: three tiles
    int bans[3];
    int made = netdraft_bans_of(&d, slot, bans);
    text(dl, f, g_title, 10, x + w / 2, y + 186, GREY, "BANS", CENTER, 0.6f);
    for (int i = 0; i < 3; i++) draft_ban_tile(dl, f, x + 10 + i * 42, y + 200, 38, 50, bans[i], i < made);
}

// The Draft pages' button row: a little lower than the other pages', clear of the grid and the cards.
const float DRAFT_HINT_Y = 416;

// The banner over the draft: the set, whose turn, the clock.
void draft_header(ImDrawList *dl, const Frame &f, const NetRoom &r, const NetDraft &d, const NetRankInfo &ri)
{
    page_title(dl, f, "DRAFT");
    ranked_banner(dl, f, r, ri, 50);
    char line[64];
    netdraft_banner(&d, r.you, line, sizeof(line));
    bool mine = netdraft_my_turn(&d, r.you);
    text(dl, f, g_stencil, 22, 320, 74, mine ? GOLD : CREAM, line, CENTER, 1.4f);
    if (netdraft_active(&d)) {
        int left = draft_secs(d);
        int total = d.step == NETDRAFT_BAN ? 20 : netdraft_fighter_step(d.step) ? 25 : 15;
        char secs[8];
        snprintf(secs, sizeof(secs), "%d", left);
        text(dl, f, g_title, 34, 600, 22, left <= 5 ? RED : CREAM, secs, RIGHT, 1.6f);
        rect(dl, f, 50, 113, 540, 4, IM_COL32(255, 255, 255, 30), 2.0f);
        rect(dl, f, 50, 113, 540 * std::min(1.0f, left / (float)total), 4, mine ? GOLD : ORANGE, 2.0f);
    }
}

// The fighter grid of the bans and picks: available, banned (red X), locked (padlock), the
// opponent's cursor (blue outline), your pick (gold), theirs (blue).
void draw_draft_fighters(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const NetDraft &d)
{
    const auto &list = roster();
    int n = (int)list.size();
    const int cols = 6, rows = 4;
    if (!n) return;
    int you = r.you, opp = 1 - you;
    bool mine = netdraft_my_turn(&d, you);
    if (R.draft_sel >= n) R.draft_sel = 0;
    if (pad.left) R.draft_sel = (R.draft_sel + n - 1) % n;
    if (pad.right) R.draft_sel = (R.draft_sel + 1) % n;
    if (pad.up) R.draft_sel = std::max(0, R.draft_sel - cols);
    if (pad.down) R.draft_sel = std::min(n - 1, R.draft_sel + cols);
    if (pad.lb) R.draft_sel = std::max(0, R.draft_sel - cols * rows);
    if (pad.rb) R.draft_sel = std::min(n - 1, R.draft_sel + cols * rows);
    int row = R.draft_sel / cols;
    if (row < R.draft_top) R.draft_top = row;
    if (row >= R.draft_top + rows) R.draft_top = row - rows + 1;
    float cw = 44, ch = 56, gx = 320 - (cols * (cw + 4) - 4) / 2, gy = 130;
    glass(dl, f, gx - 6, gy - 6, cols * (cw + 4) + 8, rows * (ch + 5) + 20);
    for (int rr = 0; rr < rows; rr++)
        for (int c = 0; c < cols; c++) {
            int i = (R.draft_top + rr) * cols + c;
            if (i >= n) break;
            int id = list[i].id;
            float x = gx + c * (cw + 4), y = gy + rr * (ch + 5);
            if (mouse_pick(f, x, y, cw, ch)) R.draft_sel = i;
            int st = netdraft_fighter_state(&d, you, id);
            bool sel = i == R.draft_sel, dim = st == NETDRAFT_TILE_BANNED || st == NETDRAFT_TILE_LOCKED;
            rect(dl, f, x, y, cw, ch, IM_COL32(0, 0, 0, 150), 3);
            ImU32 tint = dim ? IM_COL32(90, 90, 90, 255) : sel ? IM_COL32_WHITE : IM_COL32(170, 170, 170, 255);
            if (!portrait(dl, f, id, x + 1, y + 1, cw - 2, ch - 2, tint))
                text(dl, f, g_body, 9, x + 3, y + 26, GREY, list[i].name, LEFT, 0, cw - 4);
            if (st == NETDRAFT_TILE_BANNED) draft_cross(dl, f, x, y, cw, ch);
            if (st == NETDRAFT_TILE_LOCKED) draft_padlock(dl, f, x + cw / 2, y + ch / 2, 18);
            if (st == NETDRAFT_TILE_MINE) frame_rect(dl, f, x, y, cw, ch, GOLD, 3.0f);
            if (st == NETDRAFT_TILE_THEIRS) frame_rect(dl, f, x, y, cw, ch, DRAFT_BLUE, 3.0f);
            if (d.cursors[opp] == id && d.picks[opp] != id && d.step != NETDRAFT_BLIND)     // their cursor (never blind)
                frame_rect(dl, f, x + 2, y + 2, cw - 4, ch - 4, with_alpha(DRAFT_BLUE, 0.9f), 1.5f);
            if (sel) selector(dl, f, x, y, cw, ch);
            if (mouse_click(f, x, y, cw, ch)) const_cast<Pad &>(pad).a = true;
        }
    char page[32];
    snprintf(page, sizeof(page), "%d / %d", R.draft_sel + 1, n);
    text(dl, f, g_title, 11, gx + cols * (cw + 4) - 4, gy + rows * (ch + 5) - 2, GREY, page, RIGHT, 0.8f);
    int cur = list[R.draft_sel].id;
    // the cursor goes to the relay for the other to see (and a pick that runs out takes it)
    if (!gallery_on() && cur != R.draft_cursor_sent && (d.step == NETDRAFT_BAN || d.step == NETDRAFT_BLIND ||
                                                       d.step == NETDRAFT_WINNER_PICK || d.step == NETDRAFT_LOSER_PICK)) {
        netplay_lobby_draft_act(d.step, NETDRAFT_ACT_CURSOR, cur);
        R.draft_cursor_sent = cur;
    }
    bool legal = mine && netdraft_fighter_legal(&d, you, cur);
    float hx = 54;
    if (mine) hx += hint(dl, f, hx, DRAFT_HINT_Y, "a", d.step == NETDRAFT_BAN ? "BAN" : "LOCK IN");
    hx += hint(dl, f, hx, DRAFT_HINT_Y, "lb", "PAGE");
    hint(dl, f, hx, DRAFT_HINT_Y, "b", "LEAVE (FORFEIT)");
    if (mine && pad.a) {
        if (legal) netplay_lobby_draft_act(d.step, netdraft_action(&d), cur);
        else say(netdraft_banned(&d, cur) ? "That fighter is banned." : "You won a game with that fighter: pick another.");
    }
}

// The venue cards of the strikes, the venue ban and the venue pick (the vote's cards).
void draw_draft_venues(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const NetDraft &d)
{
    int n = d.option_count;
    if (n <= 0) return;
    bool mine = netdraft_my_turn(&d, r.you);
    int per = n <= 5 ? n : (n + 1) / 2, rowsn = (n + per - 1) / per;
    if (R.venue_sel >= n) R.venue_sel = 0;
    if (pad.left) R.venue_sel = (R.venue_sel + n - 1) % n;
    if (pad.right) R.venue_sel = (R.venue_sel + 1) % n;
    if (pad.up && R.venue_sel >= per) R.venue_sel -= per;
    if (pad.down && R.venue_sel + per < n) R.venue_sel += per;
    float w = n <= 5 ? 112 : 118, gap = 8, h = (w - 10) * 0.75f + 40;   // the photos are 4:3
    for (int i = 0; i < n; i++) {
        int v = d.options[i], row = i / per, col = i % per, inrow = std::min(per, n - row * per);
        float x0 = 320 - (inrow * w + (inrow - 1) * gap) / 2, x = x0 + col * (w + gap), y = 128 + row * (h + 8);
        if (mouse_pick(f, x, y, w, h)) R.venue_sel = i;
        int st = netdraft_venue_state(&d, v);
        bool sel = i == R.venue_sel, out = st == NETDRAFT_VENUE_STRUCK || st == NETDRAFT_VENUE_BANNED;
        glass(dl, f, x, y, w, h, 8, st == NETDRAFT_VENUE_CHOSEN ? IM_COL32(56, 40, 10, 255) : IM_COL32(10, 10, 13, 255), 0.62f);
        float iw = w - 10, ih = h - 40;
        ImU32 tint = out ? IM_COL32(80, 80, 80, 255) : sel ? IM_COL32_WHITE : IM_COL32(170, 170, 170, 255);
        if (!image(dl, f, venue_tex(v), x + 5, y + 5, iw, ih, tint))
            text(dl, f, g_title, 13, x + w / 2, y + ih / 2 - 6, CREAM, online_venue_name(v), CENTER, 1.0f, iw);
        const Tex &banner = venue_name_tex(v);
        if (banner.srv) image_fit(dl, f, banner, x + 2, y + ih + 8, w - 4, 22, out ? IM_COL32(120, 120, 120, 255) : IM_COL32_WHITE);
        else text(dl, f, g_hand, 12, x + w / 2, y + ih + 10, ORANGE, online_venue_name(v), CENTER, 0.8f, w - 6);
        if (out) {
            draft_cross(dl, f, x + 5, y + 5, iw, ih);
            text(dl, f, g_title, 12, x + w / 2, y + ih / 2 - 7, WHITE, st == NETDRAFT_VENUE_STRUCK ? "STRUCK" : "BANNED", CENTER, 1.2f);
        }
        if (st == NETDRAFT_VENUE_CHOSEN) frame_rect(dl, f, x, y, w, h, GOLD, 3.0f);
        if (sel && mine) selector(dl, f, x, y, w, h);
        if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    {   // both ban strips, small, under the cards
        float y = 128 + rowsn * (h + 8) + 6;
        if (y < 380) {
            for (int s = 0; s < 2; s++) {
                int bans[3], made = netdraft_bans_of(&d, s, bans);
                float x = s == r.you ? 60 : 640 - 60 - 3 * 30;
                text(dl, f, g_title, 10, x + 45, y, s == r.you ? GOLD : DRAFT_BLUE, s == r.you ? "YOUR BANS" : "THEIR BANS", CENTER, 0.6f);
                for (int i = 0; i < 3; i++) draft_ban_tile(dl, f, x + i * 30, y + 14, 27, 36, bans[i], i < made);
            }
        }
    }
    int cur = d.options[R.venue_sel];
    float hx = 54;
    if (mine) hx += hint(dl, f, hx, DRAFT_HINT_Y, "a", d.step == NETDRAFT_STRIKE ? "STRIKE" : d.step == NETDRAFT_VENUE_BAN ? "BAN" : "PICK");
    hint(dl, f, hx, DRAFT_HINT_Y, "b", "LEAVE (FORFEIT)");
    if (mine && pad.a && netdraft_venue_legal(&d, cur)) netplay_lobby_draft_act(d.step, netdraft_action(&d), cur);
}

// Both drafted fighters and the venue, before the game starts.
void draw_draft_reveal(ImDrawList *dl, const Frame &f, const NetRoom &r, const NetDraft &d)
{
    int left = r.ms_left - (int)(GetTickCount64() - r.received);
    if (gallery_on()) left = 4000;
    for (int s = 0; s < 2; s++) {
        bool me = s == r.you;
        float x = me ? 40 : 640 - 40 - 170, y = 126;
        glass(dl, f, x, y, 170, 250, 10, IM_COL32(10, 10, 13, 255), 0.6f);
        text(dl, f, g_title, 12, x + 85, y + 6, me ? GOLD : DRAFT_BLUE, me ? "YOU" : upper(slot_name(r, s)).c_str(), CENTER, 0.8f, 160);
        int id = d.picks[s];
        if (id != NETDRAFT_NONE && !portrait(dl, f, id, x + 10, y + 26, 150, 190))
            text(dl, f, g_title, 15, x + 85, y + 100, CREAM, online_fighter_name(id), CENTER, 1.0f, 150);
        frame_rect(dl, f, x + 10, y + 26, 150, 190, me ? GOLD : DRAFT_BLUE, 2.5f);
        if (id != NETDRAFT_NONE) {
            float size = fit_size(f, g_hand, 18, online_fighter_name(id), 160);
            text(dl, f, g_hand, size, x + 85, y + 222, me ? GOLD : DRAFT_BLUE, online_fighter_name(id), CENTER, 1.0f);
        }
    }
    text(dl, f, g_stencil, 40, 320, 150, CREAM, "VS", CENTER, 1.8f);
    if (d.venue) {
        if (!image(dl, f, venue_tex(d.venue), 250, 210, 140, 105))
            text(dl, f, g_title, 13, 320, 250, CREAM, online_venue_name(d.venue), CENTER, 1.0f, 140);
        text(dl, f, g_hand, 15, 320, 322, ORANGE, online_venue_name(d.venue), CENTER, 0.9f, 170);
    }
    char line[48];
    snprintf(line, sizeof(line), "FIGHT IN %d", std::max(0, (left + 999) / 1000));
    text(dl, f, g_title, 16, 320, 356, WHITE, line, CENTER, 1.0f);
}

// The Draft page in the room (draw_room): the header, then the step's page.
void draw_draft(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const NetDraft &d)
{
    NetRankInfo ri;
    lobby_rankinfo(&ri);
    if (R.draft_room_game != d.game * 16 + d.step) {     // a new step: the cursor starts where it was
        R.draft_room_game = d.game * 16 + d.step;
        R.draft_cursor_sent = -1;
    }
    draft_header(dl, f, r, d, ri);
    if (d.step == NETDRAFT_PLAYING) {
        draw_draft_reveal(dl, f, r, d);
        return;
    }
    text(dl, f, g_title, 10, 320, 99, GREY, netdraft_hint(&d, r.you), CENTER, 0.6f, 460);
    if (netdraft_fighter_step(d.step)) {
        const auto &list = roster();
        int hover = list.empty() ? NETDRAFT_NONE : list[(size_t)std::min(R.draft_sel, (int)list.size() - 1)].id;
        draft_side(dl, f, r, d, r.you, 14, 126, true, hover);
        draft_side(dl, f, r, d, 1 - r.you, 640 - 14 - 140, 126, false, NETDRAFT_NONE);
        draw_draft_fighters(dl, f, pad, r, d);
    } else {
        draw_draft_venues(dl, f, pad, r, d);
    }
    if (pad.b) { netplay_lobby_leave(); S.page = HUB; say(d.game <= 1 ? "You left the draft (a dodge)." : "You left the set (a forfeit)."); }
}

// ---- the gallery's Draft pages ----

// A sample draft: Ann (slot 0) against Ben, the roster's fighters by index.
void ranked_sample_draft(NetDraft &d, int step, int game, int first)
{
    const auto &list = roster();
    auto id = [&](int i) { return list.empty() ? 55 : list[(size_t)i % list.size()].id; };
    d = NetDraft();
    d.valid = 1;
    d.step = step;
    d.game = game;
    d.first = first;
    d.secs = 14;
    d.afk = NETDRAFT_NONE;
    d.picks[0] = d.picks[1] = d.cursors[0] = d.cursors[1] = NETDRAFT_NONE;
    static const int starters[5] = { 1, 5, 6, 11, 19 }, pool[8] = { 1, 2, 5, 6, 9, 11, 13, 19 };
    if (game <= 1) { d.option_count = 5; for (int i = 0; i < 5; i++) d.options[i] = starters[i]; }
    else { d.option_count = 8; for (int i = 0; i < 8; i++) d.options[i] = pool[i]; }
    static const int bans[6] = { 3, 8, 14, 20, 26, 31 };
    d.ban_count = step == NETDRAFT_BAN ? 2 : 6;
    for (int i = 0; i < d.ban_count; i++) d.bans[i] = id(bans[i]);
    if (step != NETDRAFT_BAN) d.bans[4] = NETDRAFT_NONE;    // one ban ran out
}

// The pages: ranked-draft-ban, ranked-draft-venue (game 1's strikes), ranked-draft-pick (game 3:
// Ben won game 2 and picked first, Ann counterpicks; her game 1 fighter is locked),
// ranked-draft-blind, ranked-draft-reveal. Returns false for any other page.
bool ranked_draft_gallery(const std::string &p)
{
    if (p.compare(0, 12, "ranked-draft") != 0) return false;
    const auto &list = roster();
    auto id = [&](int i) { return list.empty() ? 55 : list[(size_t)i % list.size()].id; };
    S.page = HUB;
    G.room = sample_room(0, 2, 2);
    snprintf(G.room.settings.name, sizeof(G.room.settings.name), "Ranked");
    for (int i = 0; i < 2; i++) G.room.players[i].ready = 1;
    ranked_sample_rank(RG.rank, 1, 0, 0, -1, NETRANK_SET_RUNNING);
    RG.rank.mode = NETRANK_DRAFT;
    NetDraft &d = RG.draft;
    R.draft_top = 0;
    if (p == "ranked-draft-ban") {
        // A = Ben (slot 1): Ben, Ann, Ann banned; Ann's second ban; Ben's cursor rests on a fighter
        ranked_sample_draft(d, NETDRAFT_BAN, 1, 1);
        d.turn = 0;
        d.cursors[1] = id(11);
        R.draft_sel = 9;
    } else if (p == "ranked-draft-venue") {
        ranked_sample_draft(d, NETDRAFT_STRIKE, 1, 1);
        d.turn = 0;                 // Ben struck one; Ann's first strike
        d.struck_count = 1;
        d.struck[0] = 5;
        R.venue_sel = 2;
    } else if (p == "ranked-draft-blind") {
        ranked_sample_draft(d, NETDRAFT_BLIND, 1, 1);
        d.venue = 11;
        d.locked[1] = 1;            // Ben locked in: only that shows
        d.cursors[0] = id(12);
        R.draft_sel = 12;
    } else if (p == "ranked-draft-pick") {
        ranked_sample_rank(RG.rank, 3, 1, 1, 0, NETRANK_SET_RUNNING);
        RG.rank.mode = NETRANK_DRAFT;
        ranked_sample_draft(d, NETDRAFT_LOSER_PICK, 3, 1);
        d.turn = 0;
        d.venue = 9;
        d.venue_ban_count = 1;
        d.venue_bans[0] = 13;
        d.picks[1] = d.cursors[1] = id(5);
        d.locked[1] = 1;
        d.lock_count[0] = 1; d.locks[0][0] = id(2);     // Ann won game 1 with this one
        d.lock_count[1] = 1; d.locks[1][0] = id(7);
        d.cursors[0] = id(10);
        d.secs = 19;
        R.draft_sel = 10;
    } else if (p == "ranked-draft-reveal") {
        ranked_sample_draft(d, NETDRAFT_PLAYING, 1, 1);
        d.venue = 11;
        d.picks[0] = id(12);
        d.picks[1] = id(5);
        G.room.phase = 1;
        G.room.candidate_count = 1;
        G.room.candidates[0] = 11;
        G.room.ms_left = 4000;
    } else {
        return false;
    }
    return true;
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
    if (getenv("RECOMP_TEST_PRESEASON")) {      // build 52: the preseason's line on the ranked pages
        char line[96];
        snprintf(line, sizeof(line), "season=1|Preseason|1|%lld\n", (long long)time(nullptr) + 5 * 86400 + 12 * 3600 + 600);
        G.stats += line;
    }
    RG.queue.valid = 1;
    RG.queue.counts[NETRANK_OPEN] = 3; RG.queue.counts[NETRANK_CLASSIC] = 7; RG.queue.counts[NETRANK_DRAFT] = 0;
    RG.queue.mode = NETRANK_CLASSIC;
    RG.queue.accept_secs = 11;
    RG.queue.opp_rating = 1838;
    snprintf(RG.queue.code, sizeof(RG.queue.code), "MATCH1");
    R.primary = NETRANK_CLASSIC;
    R.also = true;
    if (ranked_draft_gallery(p)) return true;
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
    } else if (p.compare(0, 14, "ranked-results") == 0) {
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
        snprintf(R.opp_name, sizeof(R.opp_name), "Duke Marrone");
        // build 52: the promotion (Kingpin III to II... the sample: Enforcer I to Kingpin III), a demotion,
        // the last placement set, a Legend in the Top 10
        netranked_parse_line("1772|92|14|0|5|0|28|14|6|Enforcer I|1790|Enforcer I||212|2310", &R.before[NETRANK_CLASSIC]);
        netranked_parse_line("1806|88|15|0|5|0|44|15|6|Kingpin III|1806|Kingpin III||212|2310", &RG.after[NETRANK_CLASSIC]);
        if (p == "ranked-results-demoted") {
            R.set.winner = 1;
            netranked_parse_line("1702|90|13|0|5|0|0|20|9|Enforcer II|1760|Enforcer I||380|2310", &R.before[NETRANK_CLASSIC]);
            netranked_parse_line("1668|90|12|0|5|0|32|20|10|Enforcer III|1760|Enforcer I|demotion|402|2310",
                                 &RG.after[NETRANK_CLASSIC]);
        } else if (p == "ranked-results-placed") {
            netranked_parse_line("1590|180|10|1|5|1|0|4|0|PLACEMENT 4/5|1590|Contender II|", &R.before[NETRANK_CLASSIC]);
            netranked_parse_line("1626|150|10|0|5|0|24|5|0|Contender II|1626|Contender II||640|2310", &RG.after[NETRANK_CLASSIC]);
        } else if (p == "ranked-results-legend") {
            netranked_parse_line("2096|60|20|0|5|0|4|60|20|Don of the Bronx I|2096|Don of the Bronx I||14|2310",
                                 &R.before[NETRANK_CLASSIC]);
            netranked_parse_line("2131|58|21|0|5|0|0|61|20|Def Jam Legend (Top 10 of NY)|2131|Def Jam Legend||3|2310",
                                 &RG.after[NETRANK_CLASSIC]);
        }
        R.have_before = true;
    } else if (p == "stats-ranked" || p == "stats-ranked-draft") {
        S.page = STATS;
        S.stats_page = 0;
        R.stats_tab = 1 + (p == "stats-ranked-draft" ? NETRANK_DRAFT : NETRANK_CLASSIC);
    }
    return true;
}
