// The RANKED board and the searching counts (build 62). Included into online_screen.cpp's anonymous
// namespace after ranked_ui.inc, like it.
//
//   RANKED_BOARD   a ladder per ranked mode (OPEN / CLASSIC / DRAFT tabs): place, name, rank, rating and
//                  set W-L, then the placed players whose rank is still provisional ("?", STILL SETTLING),
//                  as the website's ranked page. GET /api/ranked/leaderboard?format=text&provisional=1
//                  (the account service's public ladder; server/docs/ranked-design.md), 25 a page.
//   searching      "N SEARCHING" on the hub's RANKED MATCH, each mode card and the board's header:
//                  GET /api/ranked/searching?format=text every few seconds while one of them shows. No
//                  names; the counting is the service's (the matchmaker's own pool).
//
// Both are fetched on a thread of their own (account_fetch blocks); the pages read the last answer.

struct BoardRow {
    int place = 0, tier = 0, rating = 0, wins = 0, losses = 0;
    bool top = false;
    std::string name, rank;
};
struct BoardData {
    bool valid = false;
    int mode = -1, offset = 0, page = 25, total = 0, ptotal = 0, searching = -1, season = 0;
    std::vector<BoardRow> rows, prov;
};
struct SearchData {
    bool valid = false;
    int counts[NETRANK_MODES] = {};
    int players = 0;
    bool off[NETRANK_MODES] = {};
};

struct RankedBoardState {
    std::mutex mu;                      // data, search, failed
    BoardData data;                     // the last answer
    SearchData search;
    int failed_mode = -1, failed_offset = -1, failed_status = 0;     // the last request that failed
    std::atomic<int> board_busy{0}, search_busy{0};
    unsigned long long board_at = 0, search_at = 0;     // when each was last asked
    // the page
    int mode = NETRANK_CLASSIC, offset = 0, sel = 0, top = 0;
    Page back = HUB;                    // where B goes
} RB;

int mode_by_name(const std::string &s)
{
    for (int m = 0; m < NETRANK_MODES; m++) {
        std::string n = netranked_mode_title(m);
        for (char &c : n) c = (char)tolower((unsigned char)c);
        if (n == s) return m;
    }
    return -1;
}

std::string mode_key(int m)
{
    std::string n = netranked_mode_title(m);
    for (char &c : n) c = (char)tolower((unsigned char)c);
    return n;
}

// The board's text form (accounts RankedBoardText): "key=value" lines, entry= and prov= rows of
// place|name|rank|tier|rating|set wins|set losses|top. False without its mode= line (an older service
// answers JSON).
bool parse_board(const char *text, BoardData &d)
{
    d = BoardData();
    bool have_mode = false;
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        std::string l = end ? std::string(line, end - line) : std::string(line);
        line = end ? end + 1 : nullptr;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        int n = atoi(v.c_str());
        if (k == "mode") { d.mode = mode_by_name(v); have_mode = d.mode >= 0; }
        else if (k == "season") d.season = n;
        else if (k == "offset") d.offset = n;
        else if (k == "page") d.page = n > 0 ? n : 25;
        else if (k == "total") d.total = n;
        else if (k == "provisional_total") d.ptotal = n;
        else if (k == "searching") d.searching = n;
        else if (k == "entry" || k == "prov") {
            auto f = split_bar(v);
            if (f.size() < 7) continue;
            BoardRow r;
            r.place = atoi(f[0].c_str());
            r.name = f[1];
            r.rank = f[2];
            r.tier = std::max(0, std::min(7, atoi(f[3].c_str())));
            r.rating = atoi(f[4].c_str());
            r.wins = atoi(f[5].c_str());
            r.losses = atoi(f[6].c_str());
            r.top = f.size() > 7 && f[7] == "1";
            (k == "entry" ? d.rows : d.prov).push_back(r);
        }
    }
    d.valid = have_mode;
    return have_mode;
}

// The searching counts' text form (accounts SearchingText): <mode>=<n>, players=<n>, off=<mode>.
bool parse_searching(const char *text, SearchData &s)
{
    s = SearchData();
    int seen = 0;
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        std::string l = end ? std::string(line, end - line) : std::string(line);
        line = end ? end + 1 : nullptr;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        int m = mode_by_name(k);
        if (m >= 0) { s.counts[m] = std::max(0, atoi(v.c_str())); seen++; }
        else if (k == "players") s.players = std::max(0, atoi(v.c_str()));
        else if (k == "off" && mode_by_name(v) >= 0) s.off[mode_by_name(v)] = true;
    }
    s.valid = seen == NETRANK_MODES;
    return s.valid;
}

void board_fetch(int mode, int offset)
{
    if (gallery_on() || RB.board_busy.exchange(1)) return;
    RB.board_at = GetTickCount64();
    std::thread([mode, offset]() {
        std::vector<char> buf(32768);
        char path[128];
        snprintf(path, sizeof(path), "/api/ranked/leaderboard?mode=%s&provisional=1&format=text&offset=%d",
                 mode_key(mode).c_str(), offset);
        int len = 0, status = account_fetch(path, buf.data(), (int)buf.size(), &len);
        BoardData d;
        bool ok = status == 200 && len > 0 && (buf[std::min(len, (int)buf.size() - 1)] = 0, parse_board(buf.data(), d));
        {
            std::lock_guard<std::mutex> lock(RB.mu);
            if (ok) {
                RB.data = d;
                RB.failed_mode = RB.failed_offset = -1;
                RB.failed_status = 0;
            } else {
                RB.failed_mode = mode;
                RB.failed_offset = offset;
                RB.failed_status = status;
            }
        }
        if (ok) fprintf(stderr, "[ONLINE] ranked board %s: %d on the ladder (%d shown from %d), %d settling, %d searching\n",
                        mode_key(mode).c_str(), d.total, (int)d.rows.size(), d.offset, d.ptotal, d.searching);
        else fprintf(stderr, "[ONLINE] ranked board %s: not loaded (HTTP %d)\n", mode_key(mode).c_str(), status);
        RB.board_busy = 0;
    }).detach();
}

// The searching counts: asked again every 4 s while a page that shows them is up (15 s after a failure:
// an older service, or ranked off, answers 404).
void searching_poll()
{
    if (gallery_on()) return;
    unsigned long long now = GetTickCount64();
    bool valid;
    {
        std::lock_guard<std::mutex> lock(RB.mu);
        valid = RB.search.valid;
    }
    if ((RB.search_at && now - RB.search_at < (valid ? 4000ull : 15000ull)) || RB.search_busy.exchange(1)) return;
    RB.search_at = now;
    std::thread([]() {
        char buf[512];
        int len = 0, status = account_fetch("/api/ranked/searching?format=text", buf, sizeof(buf), &len);
        SearchData s;
        bool ok = status == 200 && len > 0 && (buf[std::min(len, (int)sizeof(buf) - 1)] = 0, parse_searching(buf, s));
        SearchData before;
        {
            std::lock_guard<std::mutex> lock(RB.mu);
            before = RB.search;
            RB.search = ok ? s : SearchData();
        }
        bool changed = ok != before.valid || (ok && memcmp(s.counts, before.counts, sizeof(s.counts)));
        if (changed) {
            if (ok) fprintf(stderr, "[ONLINE] ranked searching: open %d, classic %d, draft %d (%d players)\n",
                            s.counts[NETRANK_OPEN], s.counts[NETRANK_CLASSIC], s.counts[NETRANK_DRAFT], s.players);
            else fprintf(stderr, "[ONLINE] ranked searching: no count (HTTP %d)\n", status);
        }
        RB.search_busy = 0;
    }).detach();
}

SearchData searching_now()
{
    std::lock_guard<std::mutex> lock(RB.mu);
    return RB.search;
}

int searching_count(int mode)
{
    SearchData s = searching_now();
    if (!s.valid) return -1;
    if (mode < 0 || mode >= NETRANK_MODES) return s.players;
    return s.off[mode] ? -1 : s.counts[mode];
}

// "7 SEARCHING" ("" without a count).
std::string searching_label(int n)
{
    if (n < 0) return "";
    return std::to_string(n) + " SEARCHING";
}

// A small chip: a pulsing dot and the count, right-aligned at x1 (its width returned).
float searching_chip(ImDrawList *dl, const Frame &f, float x1, float y, int n, float alpha)
{
    std::string l = searching_label(n);
    if (l.empty()) return 0;
    float w = text_width(f, g_title, 10, l.c_str()) + 22, x = x1 - w;
    rect(dl, f, x, y, w, 15, with_alpha(IM_COL32(0, 0, 0, 190), alpha), 7);
    float t = (float)(GetTickCount64() % 1400) / 1400.0f, pulse = 0.55f + 0.45f * (t < 0.5f ? t * 2 : 2 - t * 2);
    ImU32 dot = n > 0 ? GREEN : GREY;
    dl->AddCircleFilled(f.p(x + 9, y + 7.5f), 3.2f * f.s(), with_alpha(dot, alpha * (n > 0 ? pulse : 0.7f)));
    text(dl, f, g_title, 10, x + 16, y + 2, with_alpha(n > 0 ? CREAM : GREY, alpha), l.c_str(), LEFT, 0.6f);
    return w;
}

void open_ranked_board(int mode, Page back)
{
    S.page = RANKED_BOARD;
    RB.back = back;
    if (mode >= 0 && mode < NETRANK_MODES) RB.mode = mode;
    RB.offset = 0;
    RB.sel = RB.top = 0;
    board_fetch(RB.mode, RB.offset);
}

void draw_ranked_board(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    static const int order[NETRANK_MODES] = { NETRANK_OPEN, NETRANK_CLASSIC, NETRANK_DRAFT };
    searching_poll();
    BoardData d;
    SearchData s;
    int failed_mode, failed_offset, failed_status;
    {
        std::lock_guard<std::mutex> lock(RB.mu);
        d = RB.data;
        s = RB.search;
        failed_mode = RB.failed_mode;
        failed_offset = RB.failed_offset;
        failed_status = RB.failed_status;
    }
    bool busy = RB.board_busy != 0;
    bool shown = d.valid && d.mode == RB.mode && d.offset == RB.offset;
    bool failed = !shown && failed_mode == RB.mode && failed_offset == RB.offset;
    // the mode: Y or left/right, as MY STATS' tabs; a click on a tab
    int at = 0;
    for (int i = 0; i < NETRANK_MODES; i++) if (order[i] == RB.mode) at = i;
    int want = at;
    if (pad.y || pad.right) want = (at + 1) % NETRANK_MODES;
    if (pad.left) want = (at + NETRANK_MODES - 1) % NETRANK_MODES;
    page_title(dl, f, "RANKED BOARD");
    {
        float tx = 262;
        for (int i = 0; i < NETRANK_MODES; i++) {
            const char *t = netranked_mode_title(order[i]);
            float w = text_width(f, g_title, 12, t) + 16;
            if (mouse_click(f, tx, 54, w, 18)) want = i;
            rect(dl, f, tx, 54, w, 18, i == at ? IM_COL32(120, 12, 26, 230) : IM_COL32(0, 0, 0, 150), 5);
            text(dl, f, g_title, 12, tx + w / 2, 56, i == at ? WHITE : GREY, t, CENTER, 0.8f);
            tx += w + 4;
        }
    }
    if (want != at) {
        RB.mode = order[want];
        RB.offset = 0;
        RB.sel = RB.top = 0;
        board_fetch(RB.mode, RB.offset);
        return;
    }
    // a page's answer: asked when missing (not again after a failure until X), and again after 30 s
    unsigned long long now = GetTickCount64();
    if (!busy && ((!shown && !failed) || (shown && now - RB.board_at > 30000))) board_fetch(RB.mode, RB.offset);
    if (pad.x) board_fetch(RB.mode, RB.offset);

    // the header: the mode, the season, who searches now (the service's fresher count, else the board's)
    glass(dl, f, 34, 78, 572, 328);
    std::string head = std::string("RANKED ") + netranked_mode_title(RB.mode);
    text(dl, f, g_title, 18, 48, 86, ORANGE, head.c_str(), LEFT, 1.3f);
    int searching = s.valid ? s.counts[RB.mode] : shown ? d.searching : -1;
    float hx1 = 594;
    if (s.valid && s.off[RB.mode]) {
        const char *off = "NOT QUEUEING RIGHT NOW";
        text(dl, f, g_title, 10, hx1, 90, GREY, off, RIGHT, 0.6f);
    } else {
        hx1 -= searching_chip(dl, f, hx1, 88, searching) + 8;
    }
    if (shown) {
        char line[64];
        snprintf(line, sizeof(line), "%d ON THE LADDER", d.total);
        text(dl, f, g_title, 10, hx1, 90, GREY, line, RIGHT, 0.6f);
    }
    // the columns
    const float xp = 78, xn = 90, xr = 300, xg = 508, xs = 590;
    text(dl, f, g_title, 9, xp, 110, GREY, "#", RIGHT, 0.5f);
    text(dl, f, g_title, 9, xn, 110, GREY, "PLAYER", LEFT, 0.5f);
    text(dl, f, g_title, 9, xr, 110, GREY, "RANK", LEFT, 0.5f);
    text(dl, f, g_title, 9, xg, 110, GREY, "RATING", RIGHT, 0.5f);
    text(dl, f, g_title, 9, xs, 110, GREY, "SETS W-L", RIGHT, 0.5f);
    dl->AddLine(f.p(44, 123), f.p(596, 123), IM_COL32(255, 255, 255, 30), 1.0f);

    if (!shown) {
        const char *msg = failed ? (failed_status == 404 ? "The ranked ladder is not open right now."
                                                         : "The ladder could not be loaded right now.")
                                 : "LOADING THE LADDER...";
        text(dl, f, g_title, failed ? 14 : 18, 320, 210, failed ? GREY : CREAM, msg, CENTER, 1.0f);
        if (failed) text(dl, f, g_hand, 14, 320, 236, ORANGE, pad_text("PRESS {X} TO TRY AGAIN").c_str(), CENTER, 1.0f);
    } else {
        // one list: the ladder's rows, then (on the first page) STILL SETTLING and its "?" rows
        struct Item { const BoardRow *row; bool prov, header; };
        std::vector<Item> items;
        for (const auto &r : d.rows) items.push_back({ &r, false, false });
        if (!d.prov.empty()) {
            items.push_back({ nullptr, true, true });
            for (const auto &r : d.prov) items.push_back({ &r, true, false });
        }
        StatsView me;
        bool have_me = read_stats(me) && !me.name.empty();
        const int visible = 11;
        const float y0 = 128, pitch = 24;
        int n = (int)items.size();
        if (pad.down && n) { RB.sel = std::min(n - 1, RB.sel + 1); if (items[RB.sel].header && RB.sel + 1 < n) RB.sel++; }
        if (pad.up && n) { RB.sel = std::max(0, RB.sel - 1); if (items[RB.sel].header) RB.sel = RB.sel > 0 ? RB.sel - 1 : RB.sel + 1; }
        if (RB.sel >= n) RB.sel = std::max(0, n - 1);
        if (RB.sel < RB.top) RB.top = RB.sel > 0 && items[RB.sel - 1].header ? RB.sel - 1 : RB.sel;
        if (RB.sel >= RB.top + visible) RB.top = RB.sel - visible + 1;
        RB.top = std::max(0, std::min(RB.top, std::max(0, n - visible)));
        if (!n) {
            text(dl, f, g_title, 15, 320, 196, GREY, "No ranked players yet this season.", CENTER, 0.8f);
            text(dl, f, g_title, 12, 320, 218, GREY, "Five placement sets put a player on the ladder.", CENTER, 0.8f);
        }
        for (int i = RB.top; i < n && i < RB.top + visible; i++) {
            const Item &it = items[i];
            float y = y0 + (i - RB.top) * pitch;
            if (it.header) {
                char line[80];
                snprintf(line, sizeof(line), "STILL SETTLING  -  %d PLAYER%s", d.ptotal, d.ptotal == 1 ? "" : "S");
                text(dl, f, g_hand, 15, 48, y + 3, ORANGE, line, LEFT, 0.9f);
                text(dl, f, g_body, 9, 594, y + 6, GREY, "Placed, but the rank is still provisional", RIGHT, 0);
                continue;
            }
            const BoardRow &r = *it.row;
            bool sel = i == RB.sel, mine = have_me && r.name == me.name;
            if (mouse_pick(f, 44, y - 2, 552, pitch - 2)) RB.sel = i;
            if (mine) rect(dl, f, 44, y - 2, 552, pitch - 2, IM_COL32(255, 168, 26, 50), 4);
            else if (sel) rect(dl, f, 44, y - 2, 552, pitch - 2, IM_COL32(255, 255, 255, 18), 4);
            if (sel) frame_rect(dl, f, 44, y - 2, 552, pitch - 2, with_alpha(RED, 0.9f), 1.5f);
            ImU32 tier = tier_colour(r.tier);
            ImU32 col = it.prov ? with_alpha(WHITE, 0.8f) : r.place == 1 ? GOLD : mine ? CREAM : WHITE;
            char num[32];
            if (it.prov) snprintf(num, sizeof(num), "?");
            else snprintf(num, sizeof(num), "%d", r.place);
            text(dl, f, g_title, 16, xp, y, it.prov ? GREY : col, num, RIGHT, 1.1f);
            emblem_small(dl, f, xr - 12, y + 9, 18, r.tier, it.prov ? 0.45f : 1.0f);
            text(dl, f, g_title, fit_size(f, g_title, 15, r.name.c_str(), xr - xn - 30), xn, y + 1, col, r.name.c_str(), LEFT, 1.1f);
            std::string rank = upper(r.rank.c_str());
            if (r.top) rank += "  #" + std::to_string(r.place);
            text(dl, f, g_title, fit_size(f, g_title, 12, rank.c_str(), xg - xr - 60), xr, y + 3,
                 it.prov ? with_alpha(tier, 0.75f) : tier, rank.c_str(), LEFT, 0.8f);
            snprintf(num, sizeof(num), it.prov ? "%d?" : "%d", r.rating);
            text(dl, f, g_title, 14, xg, y + 2, it.prov ? GREY : CREAM, num, RIGHT, 1.0f);
            snprintf(num, sizeof(num), "%d-%d", r.wins, r.losses);
            text(dl, f, g_title, 14, xs, y + 2, it.prov ? GREY : with_alpha(GREEN, 0.9f), num, RIGHT, 1.0f);
        }
        if (n > visible) {     // where the list is: a thin bar at the right edge
            float bh = 264.0f * visible / n, by = y0 + 264.0f * RB.top / n;
            rect(dl, f, 600, y0, 3, 264, IM_COL32(255, 255, 255, 25), 1.5f);
            rect(dl, f, 600, by, 3, bh, with_alpha(ORANGE, 0.8f), 1.5f);
        }
        // the ladder's pages (25 places each): LB / RB
        int pages = std::max(1, (d.total + d.page - 1) / d.page), pg = d.offset / std::max(1, d.page);
        if (pages > 1) {
            char line[48];
            snprintf(line, sizeof(line), "%s  PLACES %d-%d OF %d  %s", pg > 0 ? "<" : " ", d.offset + 1,
                     std::min(d.total, d.offset + d.page), d.total, pg + 1 < pages ? ">" : " ");
            text(dl, f, g_title, 12, 320, 392, busy ? GREY : CREAM, line, CENTER, 0.8f);
            int to = pg;
            if (pad.lb && pg > 0) to = pg - 1;
            if (pad.rb && pg + 1 < pages) to = pg + 1;
            if (to != pg && !busy) {
                RB.offset = to * d.page;
                RB.sel = RB.top = 0;
                board_fetch(RB.mode, RB.offset);
            }
        }
    }
    float hx = 54;
    if (shown && d.total > d.page) hx += hint(dl, f, hx, 420, "rb", "MORE PLACES");
    hx += hint(dl, f, hx, 420, "y", "NEXT MODE");
    hx += hint(dl, f, hx, 420, "x", "REFRESH");
    hint(dl, f, hx, 420, "b", "BACK");
    if (pad.b) S.page = RB.back;
}

// ---- tests and the gallery ----

// RECOMP_TEST_RANKED_BOARD="<s>:<step>;..." (tests): each step that many seconds after the hub is up,
// logged as "[ONLINE] tests: ranked board step <step>": hub (RANKED MATCH chosen), modes, board=<mode>,
// down=<n> (n rows down the board), queue=<mode> (search that mode alone with a stock fighter, as from the
// mode cards), search (the queue's page).
void ranked_board_test_step()
{
    static std::vector<std::pair<float, std::string>> steps;
    static size_t next = 0;
    static bool parsed = false;
    static unsigned long long up_at = 0;
    if (!parsed) {
        parsed = true;
        if (const char *e = getenv("RECOMP_TEST_RANKED_BOARD")) {
            std::string s = e;
            size_t a = 0;
            while (a < s.size()) {
                size_t b = s.find(';', a);
                std::string step = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
                size_t c = step.find(':');
                if (c != std::string::npos) steps.push_back({ (float)atof(step.substr(0, c).c_str()), step.substr(c + 1) });
                if (b == std::string::npos) break;
                a = b + 1;
            }
        }
    }
    if (next >= steps.size() || gallery_on()) return;
    if (!up_at) {
        if (net_state() != NETPLAY_LOBBY || !net_in_lobby()) return;
        up_at = GetTickCount64();
    }
    if ((GetTickCount64() - up_at) / 1000.0f < steps[next].first) return;
    std::string step = steps[next++].second;
    std::string what = step.substr(0, step.find('=')), arg = step.find('=') == std::string::npos ? "" : step.substr(step.find('=') + 1);
    fprintf(stderr, "[ONLINE] tests: ranked board step %s\n", step.c_str());
    if (what == "hub") { S.page = HUB; S.hub_sel = HUB_RANKED; }
    else if (what == "modes") open_ranked_modes();
    else if (what == "board") open_ranked_board(mode_by_name(arg), RANKED_MODES);
    else if (what == "down") RB.sel += atoi(arg.c_str());
    else if (what == "search") S.page = RANKED;
    else if (what == "queue" && mode_by_name(arg) >= 0) {     // as a player does: the mode, A, a stock fighter
        R.primary = mode_by_name(arg);
        R.also = false;
        ranked_search(55);
        S.page = RANKED;
    }
}

// The gallery's pages: ranked-board (Classic, the top of the ladder), ranked-board-settling (scrolled
// to STILL SETTLING), ranked-board-draft (nobody yet), ranked-hub (RANKED MATCH with its count).
bool ranked_board_gallery(const std::string &p)
{
    SearchData s;
    s.valid = true;
    s.counts[NETRANK_OPEN] = 3; s.counts[NETRANK_CLASSIC] = 7; s.counts[NETRANK_DRAFT] = 0;
    s.players = 9;
    {
        std::lock_guard<std::mutex> lock(RB.mu);
        RB.search = s;
    }
    if (p == "ranked-hub") { S.page = HUB; S.hub_sel = HUB_RANKED; return true; }
    if (p.compare(0, 12, "ranked-board") != 0) return false;
    BoardData d;
    d.valid = true;
    d.mode = p == "ranked-board-draft" ? NETRANK_DRAFT : NETRANK_CLASSIC;
    d.season = 1;
    d.searching = s.counts[d.mode];
    if (d.mode == NETRANK_CLASSIC) {
        static const char *const names[] = { "Kaze", "Rex", "Nia", "Jay", "Sol", "Ann", "Oz", "Vic", "Mo", "Tia", "Uma", "Lou",
                                             "Brooklyn Bruiser", "Hal" };
        static const char *const ranks[] = { "Def Jam Legend", "Def Jam Legend", "Don of the Bronx I", "Don of the Bronx III",
                                             "Kingpin I", "Kingpin II", "Kingpin III", "Enforcer I", "Enforcer II",
                                             "Enforcer III", "Hustler I", "Hustler II", "Contender I", "Contender III" };
        static const int tiers[] = { 7, 7, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 2, 2 };
        static const int ratings[] = { 2188, 2131, 2054, 1990, 1931, 1872, 1820, 1771, 1730, 1688, 1622, 1590, 1544, 1470 };
        for (int i = 0; i < 14; i++) {
            BoardRow r;
            r.place = i + 1; r.name = names[i]; r.rank = ranks[i]; r.tier = tiers[i]; r.rating = ratings[i];
            r.wins = 30 - i * 2; r.losses = 6 + i; r.top = i < 2;
            d.rows.push_back(r);
        }
        d.total = 14;
        static const char *const pn[] = { "Redman", "Dee", "Fox" };
        static const char *const pr[] = { "Kingpin III", "Hustler I", "Contender II" };
        static const int pt[] = { 5, 3, 2 }, prt[] = { 1801, 1640, 1515 };
        for (int i = 0; i < 3; i++) {
            BoardRow r;
            r.name = pn[i]; r.rank = pr[i]; r.tier = pt[i]; r.rating = prt[i]; r.wins = 4 - i; r.losses = 1 + i;
            d.prov.push_back(r);
        }
        d.ptotal = 3;
    }
    {
        std::lock_guard<std::mutex> lock(RB.mu);
        RB.data = d;
    }
    S.page = RANKED_BOARD;
    RB.back = RANKED_MODES;
    RB.mode = d.mode;
    RB.offset = 0;
    RB.sel = p == "ranked-board-settling" ? 15 : 0;
    RB.top = p == "ranked-board-settling" ? 6 : 0;
    return true;
}
