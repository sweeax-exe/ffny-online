// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// F0: the FIGHT COMPLEX and KING OF THE RING (community/fight-complex-design.md section 5 F0, the wire in
// community/fight-complex-f0-protocol.md). Included into online_screen.cpp's anonymous namespace after turf_ui.inc.
//
//   RINGS             one card per ring (an official King of the Hill room every relay with F0 keeps open): its art
//                     and name, the crowned king with the streak and the bounty (the board), what the room does now,
//                     who is in the line and who watches, this PC's ping to its relay; JOIN (the line) and WATCH.
//   KING OF THE RING  the current kings, best streaks, counted wins, bounties claimed and Ring Points of the season
//                     or of all time (GET /api/rings/board).
//
// No new netcode: a ring is a hill room joined by its code (netservers_join_on), its fights are hill fights. WATCH
// is the JOIN alone (a ring room's new member is away, the relay sends it the next fight to watch); JOIN is the
// JOIN, then READY 1 with the fighter once the room is there. The cards come from RINGS (netservers_rings), asked of
// every relay; a relay without F0 does not answer and adds nothing, so with no ring anywhere the page says so.
// The hill screen and the watching screens put a crown banner over a ring room (ring_banner).
//
// RECOMP_TEST_RINGS=<file>: the board answers that file's text (no account service). RECOMP_TEST_RINGS_UI=
// "<s>:<step>;...": steps from the hub's first frame on: complex, kotr, ringlog, ringjoin=N, ringwatch=N (the card
// N of the page, 0 first), leave. Each step writes a [RINGS-UI] line.

struct RingKing { long long account = 0, since = 0; int streak = 0, bounty = 0; std::string name, crew; };
struct RingStreak { long long account = 0; int streak = 0; std::string name, crew; };
struct RingRow { int rank = 0, bouts = 0; long long account = 0, value = 0; std::string name, crew; };
struct RingLive { int ring = 0, channel = 0, members = 0, line = 0, watching = 0; bool fighting = false; std::string relay, room; };
struct RingBout { long long at = 0, winner = 0, loser = 0; bool counted = false; std::string wname, lname, how, why; };
struct RingTypeBoard {
    std::string type, name, title;      // "cage", "King of the Cage", "Cage King"
    bool crowned = false;
    RingKing king;
    std::vector<RingStreak> streaks;    // counted streaks not crowned yet, best first
    std::vector<RingLive> live;
    std::map<std::string, std::vector<RingRow>> boards;
    std::vector<RingBout> bouts;
};
struct RingBoard {
    bool valid = false, enabled = false;
    std::string season;                 // "2026-11", "all"
    long long ends = 0, now = 0;
    int rules[8] = { 5, 100, 25, 225, 10, 50, 300, 3 };     // streak_to_crown, bounty_base, bounty_step, bounty_cap,
                                                            // points_win, points_crown, daily_points_cap, crown_distinct
    std::vector<RingTypeBoard> types;
    std::map<std::string, std::vector<RingRow>> overall;
    unsigned long long got = 0;
    const RingTypeBoard *of(const std::string &type) const
    {
        for (auto &t : types) if (t.type == type) return &t;
        return nullptr;
    }
};

struct RingsState {
    std::mutex mu;                      // board[], status[]
    RingBoard board[2];                 // 0 this season, 1 all time
    int status[2] = { 0, 0 };           // the last answer: 0 not asked yet, 200, 404 (off), other a failure
    std::atomic<int> busy[2] {};
    unsigned long long at[2] = { 0, 0 };
    // the page
    int tab = 0;                        // 0 RINGS, 1 KING OF THE RING
    int sel = 0, first = 0;             // the chosen card, the first one shown
    int kind = 0;                       // the board shown: best streak, counted wins, bounties, Ring Points
    int filter = 0;                     // 0 every ring, 1.. the board's types
    bool alltime = false;
    Page back = HUB;
    // a JOIN on its way: READY 1 once its room is there (a ring's new member is away)
    char pending[7] = "";
    int pending_fighter = -1;
    bool pending_pick = false;          // the saved fighter is not a roster fighter: CHOOSE FIGHTER in the room instead
    unsigned long long pending_at = 0;
    unsigned long long hub_seen = 0;    // the hub's first frame (the test steps count from it)
    bool gallery_entry = false;
    int gallery_state = 0;              // the gallery's rings: 0 the sample, 1 none (no relay has F0), 2 still asking
} RU;

const char *const RING_KIND_IDS[] = { "best_streak", "wins", "bounties", "points" };
const char *const RING_KIND_NAMES[] = { "BEST STREAK", "COUNTED WINS", "BOUNTIES CLAIMED", "RING POINTS" };
const int RING_KINDS = 4;

const char *rings_test_file()
{
    const char *e = getenv("RECOMP_TEST_RINGS");
    return e && *e ? e : nullptr;
}

// One '|' field list (the board's text form; text fields have '|' replaced, so a plain split).
std::vector<std::string> ring_fields(const std::string &v)
{
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = v.find('|', a);
        out.push_back(v.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

long long ring_num(const std::vector<std::string> &f, size_t i) { return i < f.size() ? atoll(f[i].c_str()) : 0; }
std::string ring_str(const std::vector<std::string> &f, size_t i) { return i < f.size() ? f[i] : std::string(); }

RingTypeBoard &ring_type_entry(RingBoard &b, const std::string &type)
{
    for (auto &t : b.types) if (t.type == type) return t;
    b.types.push_back(RingTypeBoard());
    b.types.back().type = type;
    return b.types.back();
}

// GET /api/rings/board?format=text (protocol doc section 4) into b; false without enabled=.
bool parse_ring_board(const char *text, RingBoard &b)
{
    b = RingBoard();
    bool any = false;
    for (auto &kv : turf_lines(text)) {
        const std::string &k = kv.first, &v = kv.second;
        auto f = ring_fields(v);
        if (k == "enabled") { b.enabled = atoi(v.c_str()) != 0; any = true; }
        else if (k == "season") b.season = v;
        else if (k == "ends") b.ends = atoll(v.c_str());
        else if (k == "now") b.now = atoll(v.c_str());
        else if (k == "rules") { for (size_t i = 0; i < 8 && i < f.size(); i++) if (!f[i].empty()) b.rules[i] = atoi(f[i].c_str()); }
        else if (k == "type" && !f[0].empty()) {
            RingTypeBoard &t = ring_type_entry(b, f[0]);
            t.name = ring_str(f, 1);
            t.title = ring_str(f, 2);
        } else if (k == "king" && !f[0].empty()) {
            RingTypeBoard &t = ring_type_entry(b, f[0]);
            t.crowned = true;
            t.king.account = ring_num(f, 1);
            t.king.name = ring_str(f, 2);
            t.king.crew = ring_str(f, 3);
            t.king.streak = (int)ring_num(f, 4);
            t.king.bounty = (int)ring_num(f, 5);
            t.king.since = ring_num(f, 6);
        } else if (k == "streak" && !f[0].empty()) {
            RingStreak s;
            s.account = ring_num(f, 1);
            s.name = ring_str(f, 2);
            s.crew = ring_str(f, 3);
            s.streak = (int)ring_num(f, 4);
            ring_type_entry(b, f[0]).streaks.push_back(s);
        } else if (k == "live" && !f[0].empty()) {
            RingLive l;
            l.relay = ring_str(f, 1);
            l.ring = (int)ring_num(f, 2);
            l.channel = (int)ring_num(f, 3);
            l.room = ring_str(f, 4);
            l.members = (int)ring_num(f, 5);
            l.line = (int)ring_num(f, 6);
            l.watching = (int)ring_num(f, 7);
            l.fighting = ring_num(f, 8) != 0;
            ring_type_entry(b, f[0]).live.push_back(l);
        } else if (k == "board" && !f[0].empty() && f.size() >= 7) {
            RingRow r;
            r.rank = (int)ring_num(f, 2);
            r.account = ring_num(f, 3);
            r.name = ring_str(f, 4);
            r.crew = ring_str(f, 5);
            r.value = ring_num(f, 6);
            r.bouts = (int)ring_num(f, 7);
            if (f[0] == "all") b.overall[f[1]].push_back(r);
            else ring_type_entry(b, f[0]).boards[f[1]].push_back(r);
        } else if (k == "bout" && !f[0].empty()) {
            RingBout o;
            o.at = ring_num(f, 1);
            o.winner = ring_num(f, 2);
            o.wname = ring_str(f, 3);
            o.loser = ring_num(f, 4);
            o.lname = ring_str(f, 5);
            o.how = ring_str(f, 6);
            o.counted = ring_num(f, 7) != 0;
            o.why = ring_str(f, 8);
            ring_type_entry(b, f[0]).bouts.push_back(o);
        }
    }
    b.valid = any;
    return any;
}

int ring_get(const char *path, std::vector<char> &buf, int *len)
{
    *len = 0;
    if (const char *file = rings_test_file()) {
        FILE *fp = fopen(file, "rb");
        if (!fp) return 0;
        size_t n = fread(buf.data(), 1, buf.size() - 1, fp);
        fclose(fp);
        buf[n] = 0;
        *len = (int)n;
        return 200;
    }
    int status = account_fetch(path, buf.data(), (int)buf.size(), len);
    buf[std::max(0, std::min(*len, (int)buf.size() - 1))] = 0;
    return status;
}

void rings_fetch_board(int which)
{
    if (gallery_on() || which < 0 || which > 1 || RU.busy[which].exchange(1)) return;
    RU.at[which] = GetTickCount64();
    std::thread([which]() {
        std::vector<char> buf(32768);
        int len = 0, status = ring_get(which ? "/api/rings/board?format=text&n=10&season=all" : "/api/rings/board?format=text&n=10",
                                       buf, &len);
        RingBoard b;
        bool ok = status == 200 && len > 0 && parse_ring_board(buf.data(), b);
        int before;
        {
            std::lock_guard<std::mutex> lock(RU.mu);
            before = RU.status[which];
            if (ok) { b.got = GetTickCount64(); RU.board[which] = b; }
            else if (status == 404) RU.board[which] = RingBoard();      // off: nothing to show
            RU.status[which] = ok ? 200 : status == 200 ? -2 : status;
        }
        if (ok) {
            int kings = 0;
            for (auto &t : b.types) kings += t.crowned;
            fprintf(stderr, "[ONLINE] rings board (%s): %s, season %s, %d types, %d crowned\n", which ? "all time" : "season",
                    b.enabled ? "on" : "off", b.season.c_str(), (int)b.types.size(), kings);
        } else if (before != status)
            fprintf(stderr, "[ONLINE] rings board (%s): %s (HTTP %d)\n", which ? "all time" : "season", status == 404 ? "off" : "not loaded", status);
        RU.busy[which] = 0;
    }).detach();
}

// ---- the gallery's sample: the protocol doc's example board, a little fuller ----

const char *const RINGS_SAMPLE =
    "enabled=1\n"
    "season=2026-11\n"
    "ends=1764547200\n"
    "now=1763499000\n"
    "rules=5|100|25|225|10|50|300|3\n"
    "type=cage|King of the Cage|Cage King\n"
    "type=boxing|King of the Ring|Ring Champion\n"
    "king=cage|12|Blaze|BXK|7|150|1763490000\n"
    "streak=cage|34|Crow||3\n"
    "streak=boxing|51|Kaze|HHT|4\n"
    "streak=boxing|77|Rex||2\n"
    "live=cage|ffny-online|1|1|K7Q2ZD|4|3|1|1|1763498990\n"
    "live=boxing|ffny-na|1|1|PX4M2A|0|0|0|0|1763498990\n"
    "board=cage|best_streak|1|12|Blaze|BXK|9|0\nboard=cage|best_streak|2|34|Crow||4|0\nboard=cage|best_streak|3|51|Kaze|HHT|3|0\n"
    "board=cage|wins|1|12|Blaze|BXK|31|0\nboard=cage|wins|2|34|Crow||18|0\n"
    "board=cage|bounties|1|51|Kaze|HHT|2|0\n"
    "board=cage|points|1|12|Blaze|BXK|640|0\nboard=cage|points|2|34|Crow||250|0\n"
    "board=boxing|best_streak|1|51|Kaze|HHT|6|0\nboard=boxing|best_streak|2|77|Rex||3|0\n"
    "board=boxing|wins|1|51|Kaze|HHT|22|0\nboard=boxing|points|1|51|Kaze|HHT|410|0\n"
    "board=all|best_streak|1|12|Blaze|BXK|9|0\nboard=all|best_streak|2|51|Kaze|HHT|6|0\nboard=all|best_streak|3|34|Crow||4|0\n"
    "board=all|best_streak|4|77|Rex||3|0\nboard=all|best_streak|5|90|Nia|QNF|3|0\nboard=all|best_streak|6|91|Sol||2|0\n"
    "board=all|best_streak|7|92|Oz||2|0\nboard=all|best_streak|8|93|Tia|RHR|2|0\nboard=all|best_streak|9|94|Mo||1|0\n"
    "board=all|best_streak|10|95|Vic||1|0\n"
    "board=all|wins|1|12|Blaze|BXK|31|0\nboard=all|wins|2|51|Kaze|HHT|28|0\nboard=all|wins|3|34|Crow||18|0\nboard=all|wins|4|77|Rex||9|0\n"
    "board=all|bounties|1|51|Kaze|HHT|2|0\nboard=all|bounties|2|34|Crow||1|0\n"
    "board=all|points|1|12|Blaze|BXK|640|0\nboard=all|points|2|51|Kaze|HHT|560|0\nboard=all|points|3|34|Crow||250|0\n"
    "bout=cage|1763498800|12|Blaze|34|Crow|win|1|\n"
    "bout=cage|1763498500|12|Blaze|77|Rex|left|1|\n";

// The sample cards: the Cage on Europe (busy: Blaze holds the room, x3), the boxing ring on US East (empty).
std::vector<NetRing> rings_sample_cards()
{
    std::vector<NetRing> out;
    NetRing a = {}, b = {};
    a.server = NETSERVER_EU; a.ping_ms = 38;
    NetRingCard &c = a.card;
    c.ring = 1; c.type = RING_CAGE; c.channel = 1;
    snprintf(c.code, sizeof(c.code), "K7Q2ZD");
    snprintf(c.name, sizeof(c.name), "The Cage");
    c.members = 4; c.line = 3; c.watching = 1; c.phase = NETHILL_FIGHTING; c.king_streak = 3; c.fights = 12;
    snprintf(c.king, sizeof(c.king), "Blaze");
    c.venue_count = 1; c.venues[0] = 10;
    b.server = NETSERVER_NA; b.ping_ms = 112;
    NetRingCard &d = b.card;
    d.ring = 2; d.type = RING_BOXING; d.channel = 1; d.flags = RING_FLAG_FREE;
    snprintf(d.code, sizeof(d.code), "PX4M2A");
    snprintf(d.name, sizeof(d.name), "The Ring");
    d.venue_count = 2; d.venues[0] = 4; d.venues[1] = 5;
    out.push_back(a);
    out.push_back(b);
    return out;
}

// The rings to show (the sample in the gallery); *asking: no RINGS round finished yet.
std::vector<NetRing> rings_cards(bool *asking)
{
    std::vector<NetRing> out;
    if (gallery_on()) {
        if (asking) *asking = RU.gallery_state == 2;
        if (RU.gallery_state == 0 || RU.gallery_state == 3) out = rings_sample_cards();
        if (RU.gallery_state == 3) out.resize(1);
        return out;
    }
    NetRing list[NETSERVER_COUNT * 4];
    int rounds = 0, n = netservers_rings(list, (int)(sizeof(list) / sizeof(list[0])), &rounds);
    if (asking) *asking = rounds == 0;
    out.assign(list, list + n);
    return out;
}

// ---- the hub ----

// The hub asks now and then: the board every minute while it answers (every five while off), the rings every 10 s.
void rings_poll()
{
    if (gallery_on()) return;
    if (!RU.hub_seen) RU.hub_seen = GetTickCount64();
    int status;
    {
        std::lock_guard<std::mutex> lock(RU.mu);
        status = RU.status[0];
    }
    unsigned long long now = GetTickCount64();
    if (!RU.at[0] || now - RU.at[0] > (status == 200 ? 60000ull : 300000ull)) rings_fetch_board(0);
    netservers_fetch_rings(10000);
}

// FIGHT COMPLEX shows on the hub while the board is on, or a relay lists a ring this session.
bool complex_shown()
{
    if (gallery_on()) return RU.gallery_entry;
    {
        std::lock_guard<std::mutex> lock(RU.mu);
        if (RU.status[0] == 200 && RU.board[0].valid && RU.board[0].enabled) return true;
    }
    return netservers_ring_of(-1, nullptr, nullptr) != 0;     // a code of NULL: any ring seen this session
}

void open_complex(Page back, int tab)
{
    S.page = COMPLEX;
    RU.back = back;
    RU.tab = tab;
    RU.first = 0;
    if (!gallery_on()) {
        netservers_fetch_rings(3000);
        rings_fetch_board(0);
        if (RU.alltime) rings_fetch_board(1);
    }
}

// ---- drawing ----

// The board's names for a type: "King of the Cage" / "Cage King", from the board when it says, else the game's.
std::string ring_title(const RingBoard &b, int type)
{
    const RingTypeBoard *t = ring_type_id(type) ? b.of(ring_type_id(type)) : nullptr;
    return t && !t->name.empty() ? t->name : ring_type_title(type);
}
std::string ring_king_title(const RingBoard &b, int type)
{
    const RingTypeBoard *t = ring_type_id(type) ? b.of(ring_type_id(type)) : nullptr;
    return t && !t->title.empty() ? t->title : ring_type_king(type);
}

RingBoard ring_board_now(int which = 0)
{
    std::lock_guard<std::mutex> lock(RU.mu);
    return RU.board[which];
}

// "KING OF THE CAGE": the hill screens' label of a ring room
std::string ring_title_of(int type) { return upper(ring_title(ring_board_now(0), type).c_str()); }

// A name with its crew tag: the tag's box, then the name; the width used.
float ring_name(ImDrawList *dl, const Frame &f, float x, float y, float size, const std::string &name, const std::string &crew,
                ImU32 col, float maxw)
{
    float used = 0;
    if (!crew.empty()) used = turf_tag(dl, f, x, y + size * 0.1f, size * 0.72f, crew, with_alpha(col, 0.85f)) + 5;
    std::string n = upper(name.empty() ? "(hidden)" : name.c_str());
    float s = fit_size(f, g_title, size, n.c_str(), std::max(20.0f, maxw - used));
    text(dl, f, g_title, s, x + used, y, col, n.c_str(), LEFT, 1.0f);
    return used + text_width(f, g_title, s, n.c_str());
}

// The king block of a ring type (a card's and the banner's words): "CAGE KING  BLAZE [BXK]  7 IN A ROW  BOUNTY 150".
void ring_king_block(ImDrawList *dl, const Frame &f, const RingBoard &b, int type, float x, float y, float w, bool big)
{
    const RingTypeBoard *t = ring_type_id(type) ? b.of(ring_type_id(type)) : nullptr;
    std::string kt = upper(ring_king_title(b, type).c_str());
    if (t && t->crowned) {
        tm_crown(dl, f.p(x, y + 1), f.s() * (big ? 1.1f : 0.85f), GOLD);
        text(dl, f, g_title, big ? 11 : 9, x + (big ? 24 : 19), y + 2, GOLD, kt.c_str(), LEFT, 0.8f);
        ring_name(dl, f, x, y + (big ? 18 : 14), big ? 20 : 15, t->king.name, t->king.crew, CREAM, w);
        char line[96];
        snprintf(line, sizeof(line), "%d IN A ROW   -   BOUNTY %d", t->king.streak, t->king.bounty);
        text(dl, f, g_title, big ? 12 : 10, x, y + (big ? 42 : 32), GOLD, line, LEFT, 0.8f, w);
        return;
    }
    tm_crown(dl, f.p(x, y + 1), f.s() * (big ? 1.1f : 0.85f), with_alpha(GREY, 0.6f));
    std::string none = "NO " + kt + " YET";
    text(dl, f, g_title, big ? 14 : 11, x + (big ? 24 : 19), y + 1, CREAM, none.c_str(), LEFT, 0.8f, w - 24);
    char line[128];
    snprintf(line, sizeof(line), "%d counted wins in a row take the crown", b.rules[0] > 0 ? b.rules[0] : 5);
    text(dl, f, g_hand, big ? 13 : 11, x, y + (big ? 20 : 16), ORANGE, line, LEFT, 0.8f, w);
    if (t && !t->streaks.empty()) {
        const RingStreak &s = t->streaks[0];
        snprintf(line, sizeof(line), "ON A RUN: %s  %d OF %d", upper(s.name.empty() ? "(hidden)" : s.name.c_str()).c_str(), s.streak,
                 b.rules[0] > 0 ? b.rules[0] : 5);
        text(dl, f, g_title, big ? 11 : 9, x, y + (big ? 40 : 31), with_alpha(GOLD, 0.9f), line, LEFT, 0.8f, w);
    }
}

// One ring's card at x, y (w x h): the art, the name, the crowned king, the room now, the line, the ping.
void ring_card(ImDrawList *dl, const Frame &f, const RingBoard &b, const NetRing &r, float x, float y, float w, float h, bool sel)
{
    const NetRingCard &c = r.card;
    glass(dl, f, x, y, w, h, 10, IM_COL32(10, 10, 13, 255), sel ? 0.78f : 0.6f);
    // the art: the ring's first venue, dimmed under its names
    const float ah = 112;
    int venue = c.venue_count ? c.venues[0] : 0;
    const Tex &t = venue ? venue_tex(venue) : no_tex();
    if (!image(dl, f, t, x + 4, y + 4, w - 8, ah, sel ? IM_COL32(255, 255, 255, 235) : IM_COL32(170, 170, 170, 220),
               ImVec2(0.04f, 0.10f), ImVec2(0.96f, 0.80f)))
        rect(dl, f, x + 4, y + 4, w - 8, ah, IM_COL32(60, 12, 18, 200), 8);
    dl->AddRectFilledMultiColor(f.p(x + 4, y + 44), f.p(x + w - 4, y + 4 + ah), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 220), IM_COL32(0, 0, 0, 220));
    std::string title = upper(ring_title(b, c.type).c_str());
    text(dl, f, g_title, 11, x + 12, y + 76, GOLD, title.c_str(), LEFT, 0.9f, w - 24);
    std::string name = upper(c.name);
    text(dl, f, g_stencil, fit_size(f, g_stencil, 26, name.c_str(), w - 24), x + 12, y + 88, CREAM, name.c_str(), LEFT, 1.6f);
    if (c.flags & RING_FLAG_FULL) {
        float tw = text_width(f, g_title, 10, "FULL") + 12;
        rect(dl, f, x + w - 10 - tw, y + 10, tw, 16, with_alpha(RED, 0.9f), 3);
        text(dl, f, g_title, 10, x + w - 10 - tw / 2, y + 12, WHITE, "FULL", CENTER, 0);
    }
    // the crowned king of the type (the board): one per type, across every relay and channel
    ring_king_block(dl, f, b, c.type, x + 12, y + ah + 14, w - 24, w > 230);
    // the room now (RINGS): its phase and the room's king
    float ry = y + ah + 70;
    dl->AddLine(f.p(x + 12, ry - 6), f.p(x + w - 12, ry - 6), IM_COL32(255, 255, 255, 30), 1.0f);
    bool fight = c.phase == NETHILL_FIGHTING;
    const char *now = fight ? "FIGHT ON" : c.phase == NETHILL_VOTING ? "PICKING THE VENUE" : c.phase == NETHILL_CALLING ? "NEXT FIGHT CALLED"
                    : c.line >= 2 ? "STARTING" : c.line == 1 ? "ONE WAITING: STEP IN" : "OPEN: BE THE FIRST";
    if (fight) {
        rect(dl, f, x + 12, ry, 50, 15, with_alpha(RED, 0.95f), 3);
        dl->AddCircleFilled(f.p(x + 20, ry + 7.5f), 2.8f * f.s(), (GetTickCount64() / 600) % 2 ? WHITE : with_alpha(WHITE, 0.3f));
        text(dl, f, g_title, 10, x + 26, ry + 2, WHITE, "LIVE", LEFT, 0);
        text(dl, f, g_title, 12, x + 68, ry + 1, CREAM, now, LEFT, 0.8f, w - 80);
    } else text(dl, f, g_title, 12, x + 12, ry + 1, c.line ? CREAM : with_alpha(GREEN, 0.95f), now, LEFT, 0.8f, w - 24);
    if (c.king[0]) {
        char line[96];
        snprintf(line, sizeof(line), "%s HOLDS THE RING  x%d", upper(c.king).c_str(), c.king_streak);
        text(dl, f, g_hand, 13, x + 12, ry + 18, ORANGE, line, LEFT, 0.8f, w - 24);
    }
    // who is in: the line and the watchers
    char counts[64];
    snprintf(counts, sizeof(counts), "IN LINE %d     WATCHING %d", c.line, c.watching);
    text(dl, f, g_title, 11, x + 12, y + h - 42, WHITE, counts, LEFT, 0.8f, w - 24);
    // this PC's ping to the ring's relay
    NetServerInfo sv[NETSERVER_COUNT];
    servers(sv);
    std::string where = r.server >= 0 && r.server < NETSERVER_COUNT ? upper(sv[r.server].short_name ? sv[r.server].short_name : "") : "";
    char ping[48];
    if (r.ping_ms >= 0) snprintf(ping, sizeof(ping), "%s  %d MS", where.c_str(), r.ping_ms);
    else snprintf(ping, sizeof(ping), "%s", where.c_str());
    ping_bars(dl, f, x + 12, y + h - 24, 11, r.ping_ms);
    text(dl, f, g_title, 11, x + 36, y + h - 25, ping_colour(r.ping_ms), ping, LEFT, 0.8f);
    if (sel) selector(dl, f, x, y, w, h);
}

void rings_tabs(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    static const char *const tabs[] = { "RINGS", "KING OF THE RING" };
    int want = RU.tab;
    if (pad.rb || pad.lb) want = 1 - RU.tab;
    float tx = 262;
    for (int i = 0; i < 2; i++) {
        float tw = text_width(f, g_title, 13, tabs[i]) + 20;
        if (mouse_click(f, tx, 52, tw, 20)) want = i;
        rect(dl, f, tx, 52, tw, 20, i == RU.tab ? IM_COL32(120, 12, 26, 230) : IM_COL32(0, 0, 0, 150), 5);
        text(dl, f, g_title, 13, tx + tw / 2, 54, i == RU.tab ? WHITE : GREY, tabs[i], CENTER, 0.8f);
        tx += tw + 4;
    }
    if (want != RU.tab) {
        RU.tab = want;
        if (want == 1 && !gallery_on()) rings_fetch_board(RU.alltime ? 1 : 0);
    }
}

// JOIN (the line) or WATCH (in the room, away) of a card.
void ring_go(const NetRing &r, bool join)
{
    const NetRingCard &c = r.card;
    enum AccountState acc = acc_state();
    if (c.flags & RING_FLAG_FULL) { say("This ring is full right now: try again after the next fight.", 5000); return; }
    if (join && acc != ACCOUNT_IN) {
        say(acc == ACCOUNT_WAITING ? "Finish signing in with Discord in your browser first."
                                   : "Sign in with Discord first: only signed-in fighters' wins count toward the crown.", 6000);
        return;
    }
    if (gallery_on()) return;
    int fighter = saved_fighter();
    bool roster_fighter = fighter != NETPLAY_CREATED_FIGHTER && fighter < FB_FIRST_LOGICAL && !ONLINE_NEW_FIGHTER(fighter);
    RU.pending[0] = 0;
    RU.pending_pick = false;
    if (join) {
        snprintf(RU.pending, sizeof(RU.pending), "%s", c.code);
        RU.pending_fighter = fighter;
        RU.pending_pick = !roster_fighter;      // the rings take roster fighters only (STANDARD rules)
        RU.pending_at = GetTickCount64();
    }
    fprintf(stderr, "[ONLINE] rings: %s %s \"%s\" (type %d, ch %d) on relay %d, fighter %d%s\n", join ? "join" : "watch", c.code,
            c.name, c.type, c.channel, r.server, fighter, join && !roster_fighter ? " (not a roster fighter: the pick first)" : "");
    netservers_join_on(r.server, c.code, ring_room_mode(&c), hub_name().c_str());
    say(std::string(join ? "Stepping into " : "Going to watch at ") + c.name + "...");
}

void draw_rings_tab(ImDrawList *dl, const Frame &f, const Pad &pad, const RingBoard &b)
{
    bool asking = false;
    std::vector<NetRing> cards = rings_cards(&asking);
    int n = (int)cards.size();
    if (!n) {
        glass(dl, f, 34, 84, 572, 318);
        tm_crown(dl, f.p(302, 150), f.s() * 2.0f, with_alpha(GREY, 0.45f));
        if (asking) text(dl, f, g_title, 18, 320, 210, CREAM, "LOOKING FOR RINGS...", CENTER, 1.0f);
        else {
            text(dl, f, g_title, 20, 320, 200, CREAM, "NO RINGS RIGHT NOW", CENTER, 1.2f);
            text(dl, f, g_hand, 15, 320, 232, ORANGE, "The rings open on the servers soon. Check back later.", CENTER, 0.8f, 480);
            if (b.valid && b.enabled)
                text(dl, f, g_title, 11, 320, 262, GREY, pad_text("The kings and the boards are under {RB} KING OF THE RING.").c_str(), CENTER, 0.8f, 480);
        }
        float hx = 54;
        hx += hint(dl, f, hx, 420, "x", "REFRESH");
        hx += hint(dl, f, hx, 420, "rb", "KING OF THE RING");
        hint(dl, f, hx, 420, "b", "BACK");
        if (pad.x && !gallery_on()) { netservers_fetch_rings(3000); say("Looking again..."); }
        return;
    }
    RU.sel = std::max(0, std::min(RU.sel, n - 1));
    if (pad.left && RU.sel > 0) RU.sel--;
    if (pad.right && RU.sel < n - 1) RU.sel++;
    // one card: large and centred; two: side by side; three and more: three at a time, the row follows the choice
    int per = n == 1 ? 1 : n == 2 ? 2 : 3;
    float w = per == 1 ? 300 : per == 2 ? 278 : 184, gap = per == 3 ? 10 : 16, h = 318;
    float x0 = 320 - (per * w + (per - 1) * gap) / 2;
    if (RU.sel < RU.first) RU.first = RU.sel;
    if (RU.sel >= RU.first + per) RU.first = RU.sel - per + 1;
    RU.first = std::max(0, std::min(RU.first, std::max(0, n - per)));
    for (int k = 0; k < per && RU.first + k < n; k++) {
        int i = RU.first + k;
        float x = x0 + k * (w + gap);
        if (mouse_pick(f, x, 84, w, h)) RU.sel = i;
        ring_card(dl, f, b, cards[i], x, 84, w, h, i == RU.sel);
    }
    if (RU.first > 0) text(dl, f, g_title, 26, 22, 220, ORANGE, "<", CENTER, 1.2f);
    if (RU.first + per < n) text(dl, f, g_title, 26, 618, 220, ORANGE, ">", CENTER, 1.2f);
    float hx = 54;
    hx += hint(dl, f, hx, 420, "a", "JOIN");
    hx += hint(dl, f, hx, 420, "y", "WATCH");
    hx += hint(dl, f, hx, 420, "x", "REFRESH");
    hx += hint(dl, f, hx, 420, "rb", "KING OF THE RING");
    hint(dl, f, hx, 420, "b", "BACK");
    if (pad.a) ring_go(cards[RU.sel], true);
    else if (pad.y) ring_go(cards[RU.sel], false);
    else if (pad.x && !gallery_on()) { netservers_fetch_rings(3000); rings_fetch_board(0); say("Refreshing..."); }
}

// KING OF THE RING: the current kings on the left, a board on the right (season or all time).
void draw_kotr_tab(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    int which = RU.alltime ? 1 : 0, status;
    RingBoard b, season;
    {
        std::lock_guard<std::mutex> lock(RU.mu);
        b = RU.board[which];
        season = RU.board[0];
        status = RU.status[which];
    }
    if (!gallery_on() && RU.busy[which] == 0 && GetTickCount64() - RU.at[which] > 60000) rings_fetch_board(which);
    if (pad.y) {
        RU.alltime = !RU.alltime;
        if (!gallery_on()) rings_fetch_board(RU.alltime ? 1 : 0);
    }
    if (pad.x && !gallery_on()) { rings_fetch_board(which); say("Refreshing..."); }
    float hx = 54;
    hx += hint(dl, f, hx, 420, "y", RU.alltime ? "THIS SEASON" : "ALL TIME");
    hx += hint(dl, f, hx, 420, "x", "REFRESH");
    hx += hint(dl, f, hx, 420, "lb", "RINGS");
    hint(dl, f, hx, 420, "b", "BACK");
    if (!b.valid || !b.enabled) {
        glass(dl, f, 34, 84, 572, 318);
        bool off = status == 404 || (b.valid && !b.enabled), failed = status != 0 && status != 200 && status != 404 && RU.busy[which] == 0;
        const char *msg = off ? "King of the Ring is not on right now." : failed ? "The board could not be loaded right now." : "LOADING THE BOARD...";
        text(dl, f, g_title, failed || off ? 15 : 18, 320, 220, failed || off ? GREY : CREAM, msg, CENTER, 1.0f);
        if (failed) text(dl, f, g_hand, 14, 320, 246, ORANGE, pad_text("PRESS {X} TO TRY AGAIN").c_str(), CENTER, 1.0f);
        return;
    }
    // the header: the season and its days left, or all time
    {
        std::string h = RU.alltime ? "ALL TIME" : turf_month(b.season) + " SEASON";
        text(dl, f, g_title, 14, 50, 82, CREAM, h.c_str(), LEFT, 1.0f);
        if (!RU.alltime && b.ends > 0 && b.now > 0) {
            long long now = b.now + (b.got && !gallery_on() ? (long long)((GetTickCount64() - b.got) / 1000) : 0);
            std::string left = "ENDS IN " + turf_span(b.ends - now);
            text(dl, f, g_title, 11, 590, 84, with_alpha(ORANGE, 0.9f), left.c_str(), RIGHT, 0.8f);
        }
    }
    // left: the current kings (always the season's: a crown is now)
    glass(dl, f, 34, 102, 248, 300);
    text(dl, f, g_hand, 15, 48, 108, ORANGE, "CURRENT KINGS", LEFT, 1.0f);
    const RingBoard &kb = season.valid ? season : b;
    float ky = 134;
    for (size_t i = 0; i < kb.types.size() && ky < 380; i++) {
        int type = ring_type_of_id(kb.types[i].type.c_str());
        std::string head = upper(kb.types[i].name.empty() ? ring_type_title(type) : kb.types[i].name.c_str());
        text(dl, f, g_title, 9, 48, ky, GREY, head.c_str(), LEFT, 0.5f);
        ring_king_block(dl, f, kb, type, 48, ky + 13, 224, false);
        ky += 64;
    }
    if (kb.types.empty()) text(dl, f, g_title, 12, 158, 220, GREY, "No ring has a king yet.", CENTER, 0.8f, 220);
    // right: one board, of every ring or one type
    if (pad.left) RU.kind = (RU.kind + RING_KINDS - 1) % RING_KINDS;
    if (pad.right) RU.kind = (RU.kind + 1) % RING_KINDS;
    int filters = 1 + (int)b.types.size();
    if (pad.down) RU.filter = (RU.filter + 1) % filters;
    if (pad.up) RU.filter = (RU.filter + filters - 1) % filters;
    RU.filter = std::max(0, std::min(RU.filter, filters - 1));
    const float x = 292, w = 314;
    glass(dl, f, x, 102, w, 300);
    {   // the board's name with its arrows, the ring under it
        std::string kind = RING_KIND_NAMES[RU.kind];
        text(dl, f, g_title, 16, x + w / 2, 108, WHITE, kind.c_str(), CENTER, 1.0f);
        text(dl, f, g_title, 18, x + 18, 106, ORANGE, "<", CENTER, 1.0f);
        text(dl, f, g_title, 18, x + w - 18, 106, ORANGE, ">", CENTER, 1.0f);
        if (mouse_click(f, x + 4, 104, 28, 22)) RU.kind = (RU.kind + RING_KINDS - 1) % RING_KINDS;
        if (mouse_click(f, x + w - 32, 104, 28, 22)) RU.kind = (RU.kind + 1) % RING_KINDS;
        std::string ring = RU.filter == 0 ? std::string("EVERY RING") : upper(b.types[RU.filter - 1].name.empty()
                                                                               ? ring_type_title(ring_type_of_id(b.types[RU.filter - 1].type.c_str()))
                                                                               : b.types[RU.filter - 1].name.c_str());
        text(dl, f, g_hand, 13, x + w / 2, 130, ORANGE, ring.c_str(), CENTER, 0.8f);
        if (filters > 1) {      // up / down picks the ring: small arrows at the line's ends
            text(dl, f, g_title, 9, x + 16, 133, GREY, "UP", LEFT, 0.5f);
            text(dl, f, g_title, 9, x + w - 16, 133, GREY, "DOWN", RIGHT, 0.5f);
            if (mouse_click(f, x + w / 2 - 80, 128, 160, 18)) RU.filter = (RU.filter + 1) % filters;
        }
    }
    const std::vector<RingRow> *rows = nullptr;
    static const std::vector<RingRow> none;
    if (RU.filter == 0) { auto it = b.overall.find(RING_KIND_IDS[RU.kind]); rows = it != b.overall.end() ? &it->second : &none; }
    else {
        auto &bd = b.types[RU.filter - 1].boards;
        auto it = bd.find(RING_KIND_IDS[RU.kind]);
        rows = it != bd.end() ? &it->second : &none;
    }
    text(dl, f, g_title, 9, x + 30, 150, GREY, "#", RIGHT, 0.5f);
    text(dl, f, g_title, 9, x + 42, 150, GREY, "FIGHTER", LEFT, 0.5f);
    text(dl, f, g_title, 9, x + w - 16, 150, GREY, RU.kind == 0 ? "WINS IN A ROW" : RU.kind == 3 ? "POINTS" : RU.kind == 2 ? "CLAIMED" : "WINS",
         RIGHT, 0.5f);
    dl->AddLine(f.p(x + 10, 163), f.p(x + w - 10, 163), IM_COL32(255, 255, 255, 30), 1.0f);
    if (rows->empty()) {
        text(dl, f, g_title, 13, x + w / 2, 240, GREY, RU.alltime ? "No counted bouts yet." : "No counted bouts yet this season.", CENTER, 0.8f, w - 30);
        text(dl, f, g_title, 10, x + w / 2, 262, GREY, "Only fights between signed-in players count.", CENTER, 0.8f, w - 30);
    }
    long long most = 1;
    for (auto &r : *rows) most = std::max(most, r.value);
    for (size_t i = 0; i < rows->size() && i < 10; i++) {
        const RingRow &r = (*rows)[i];
        float y = 168 + i * 23.0f;
        if (r.rank == 1) {
            float bx1 = x + 40 + (w - 52) * std::max(0.02f, (float)r.value / most);
            dl->AddRectFilledMultiColor(f.p(x + 40, y), f.p(bx1, y + 19), with_alpha(GOLD, 0.04f), with_alpha(GOLD, 0.30f),
                                        with_alpha(GOLD, 0.30f), with_alpha(GOLD, 0.04f));
            tm_crown(dl, f.p(x + 14, y + 3), f.s() * 0.95f, GOLD);
        } else text(dl, f, g_title, 15, x + 30, y + 1, WHITE, std::to_string(r.rank).c_str(), RIGHT, 1.0f);
        ring_name(dl, f, x + 42, y + 2, 14, r.name, r.crew, r.rank == 1 ? GOLD : WHITE, w - 120);
        text(dl, f, g_title, 14, x + w - 16, y + 2, with_alpha(GREEN, 0.9f), std::to_string(r.value).c_str(), RIGHT, 1.0f);
    }
}

void draw_complex(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    page_title(dl, f, "FIGHT COMPLEX");
    rings_tabs(dl, f, pad);
    RingBoard b = ring_board_now(0);
    if (!gallery_on() && RU.tab == 0) netservers_fetch_rings(3000);
    if (RU.tab == 0) draw_rings_tab(dl, f, pad, b);
    else draw_kotr_tab(dl, f, pad);
    if (pad.b) S.page = RU.back;
}

// ---- the ring room: its banner on the hill screen and the watching screens ----

// Room r is a ring (the RINGS answers of this session named its code): its card into *out.
bool ring_room(const NetRoom &r, NetRingCard *out)
{
    if (!r.in_room || !NETPLAY_IS_KOTH(r.settings.mode)) return false;
    if (gallery_on()) {
        for (auto &c : rings_sample_cards())
            if (!strcmp(c.card.code, r.code)) { if (out) *out = c.card; return true; }
        return false;
    }
    return netservers_ring_of(connected_server(), r.code, out) != 0;
}

// The crown banner: the ring's title and the type's crowned king with streak and bounty (or the way to the crown),
// on one strip at y from x0 to x1.
void ring_banner(ImDrawList *dl, const Frame &f, const NetRingCard &c, float x0, float y, float x1)
{
    RingBoard b = ring_board_now(0);
    const RingTypeBoard *t = ring_type_id(c.type) ? b.of(ring_type_id(c.type)) : nullptr;
    glass(dl, f, x0, y, x1 - x0, 20, 6, IM_COL32(40, 26, 4, 255), 0.75f);
    float x = x0 + 8;
    std::string title = upper(ring_title(b, c.type).c_str());
    text(dl, f, g_title, 11, x, y + 4, GOLD, title.c_str(), LEFT, 0.8f);
    x += text_width(f, g_title, 11, title.c_str()) + 12;
    if (t && t->crowned) {
        tm_crown(dl, f.p(x, y + 3), f.s() * 0.8f, GOLD);
        x += 20;
        x += ring_name(dl, f, x, y + 3, 12, t->king.name, t->king.crew, CREAM, std::max(40.0f, x1 - x - 150)) + 10;
        char line[64];
        snprintf(line, sizeof(line), "x%d   BOUNTY %d", t->king.streak, t->king.bounty);
        text(dl, f, g_title, 11, x1 - 8, y + 4, GOLD, line, RIGHT, 0.8f);
    } else {
        char line[96];
        snprintf(line, sizeof(line), "NO %s YET: %d COUNTED WINS IN A ROW TAKE THE CROWN", upper(ring_king_title(b, c.type).c_str()).c_str(),
                 b.rules[0] > 0 ? b.rules[0] : 5);
        text(dl, f, g_title, fit_size(f, g_title, 10, line, x1 - x - 8), x, y + 5, CREAM, line, LEFT, 0.8f);
    }
}

// What the hill screen's right panel says under the room king of a ring: the crown it holds, or none.
std::string ring_king_note(const NetRingCard &c, const char *room_king)
{
    RingBoard b = ring_board_now(0);
    const RingTypeBoard *t = ring_type_id(c.type) ? b.of(ring_type_id(c.type)) : nullptr;
    if (!t || !room_king || !room_king[0]) return "";
    if (t->crowned && !_stricmp(t->king.name.c_str(), room_king)) return "CROWNED - BOUNTY " + std::to_string(t->king.bounty);
    for (auto &s : t->streaks)
        if (!_stricmp(s.name.c_str(), room_king))
            return std::to_string(s.streak) + " OF " + std::to_string(b.rules[0] > 0 ? b.rules[0] : 5) + " TO THE CROWN";
    return "";
}

// ---- every frame of the online screens: the JOIN's READY, the board's refresh, the test steps ----

void rings_test_steps(const NetRoom &room);

void rings_tick(const NetRoom &room)
{
    if (gallery_on()) return;
    if (RU.pending[0]) {
        unsigned long long now = GetTickCount64();
        if (now - RU.pending_at > 20000) {
            fprintf(stderr, "[ONLINE] rings: the join of %s did not reach the room in time\n", RU.pending);
            RU.pending[0] = 0;
        } else if (room.in_room && !strncmp(room.code, RU.pending, 6) && room.hill.valid) {
            int state = -1;
            for (int i = 0; i < room.hill.count; i++)
                if (room.hill.line[i].slot == room.you) state = room.hill.line[i].state;
            if (RU.pending_pick) {
                say("The rings take roster fighters only: pick one, then press A to step into the line.", 7000);
                choose_fighter(55);
            } else if (state == NETLINE_AWAY) {
                fprintf(stderr, "[ONLINE] rings: in %s, into the line with fighter %d\n", room.code, RU.pending_fighter);
                netplay_lobby_ready(1, RU.pending_fighter);
            }
            RU.pending[0] = 0;
        }
    }
    if (room.in_room) {     // in a ring: the board's crown stays fresh (a crown changes hands in this room)
        static unsigned long long last;
        NetRingCard c;
        if (GetTickCount64() - last > 15000 && ring_room(room, &c)) {
            last = GetTickCount64();
            rings_fetch_board(0);
        }
    }
    rings_test_steps(room);
}

void rings_test_steps(const NetRoom &room)
{
    static std::vector<std::pair<float, std::string>> steps;
    static size_t next = 0;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        if (const char *e = getenv("RECOMP_TEST_RINGS_UI")) {
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
    if (next >= steps.size() || !RU.hub_seen || (GetTickCount64() - RU.hub_seen) / 1000.0f < steps[next].first) return;
    std::string step = steps[next++].second;
    std::string what = step.substr(0, step.find('=')), arg = step.find('=') == std::string::npos ? "" : step.substr(step.find('=') + 1);
    bool asking = false;
    std::vector<NetRing> cards = rings_cards(&asking);
    if (what == "complex" || what == "kotr") {
        open_complex(HUB, what == "kotr" ? 1 : 0);
        fprintf(stderr, "[RINGS-UI] %s: page open (hub entry %s, %d card%s%s)\n", what.c_str(), complex_shown() ? "shown" : "hidden",
                (int)cards.size(), cards.size() == 1 ? "" : "s", asking ? ", still asking" : "");
    } else if (what == "ringlog") {
        RingBoard b = ring_board_now(0);
        fprintf(stderr, "[RINGS-UI] ringlog: %d card%s%s, board %s (%d types)\n", (int)cards.size(), cards.size() == 1 ? "" : "s",
                asking ? " (still asking)" : "", b.valid ? (b.enabled ? "on" : "off") : "not loaded", (int)b.types.size());
        for (auto &c : cards)
            fprintf(stderr, "[RINGS-UI]   %s \"%s\" type %d ch %d relay %d ping %d: %d in, %d line, %d watching, phase %d, king \"%s\" x%d\n",
                    c.card.code, c.card.name, c.card.type, c.card.channel, c.server, c.ping_ms, c.card.members, c.card.line,
                    c.card.watching, c.card.phase, c.card.king, c.card.king_streak);
        NetRingCard rc;
        if (room.in_room) fprintf(stderr, "[RINGS-UI]   in room %s: %s\n", room.code, ring_room(room, &rc) ? rc.name : "not a ring");
    } else if (what == "ringjoin" || what == "ringwatch") {
        int i = atoi(arg.c_str());
        if (i >= 0 && i < (int)cards.size()) ring_go(cards[i], what == "ringjoin");
        fprintf(stderr, "[RINGS-UI] %s=%d: %s\n", what.c_str(), i, i >= 0 && i < (int)cards.size() ? cards[i].card.code : "no such card");
    } else if (what == "leave") {
        if (room.in_room) netplay_lobby_leave();
        S.page = HUB;
        fprintf(stderr, "[RINGS-UI] leave\n");
    }
}

// ---- the gallery: hub-complex (the hub's entry), complex (two rings), complex-one (one ring), complex-empty (no
// relay has F0), complex-asking, kotr, kotr-alltime, kotr-cage (the Cage's board), kotr-off (King of the Ring off),
// hill-ring (a ring room's hill screen, Blaze the crowned king), watch-ring, hill-hud-ring (the HUD over a watched
// ring fight). RECOMP_TEST_RINGS=<file> takes that file's text instead of the sample board.
bool rings_gallery(const std::string &p)
{
    bool hill = p == "hill-ring" || p == "watch-ring" || p == "hill-hud-ring";
    RU.gallery_entry = p.compare(0, 11, "hub-complex") == 0 || p.compare(0, 7, "complex") == 0 || p.compare(0, 4, "kotr") == 0 || hill;
    if (!RU.gallery_entry) return false;
    std::string text = RINGS_SAMPLE;
    if (const char *file = rings_test_file()) {
        FILE *fp = fopen(file, "rb");
        if (fp) {
            text.clear();
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, n);
            fclose(fp);
        }
    }
    RingBoard b;
    parse_ring_board(text.c_str(), b);
    if (p == "kotr-off") b.enabled = false;
    {
        std::lock_guard<std::mutex> lock(RU.mu);
        RU.board[0] = RU.board[1] = b;
        RU.board[1].season = "all";
        RU.status[0] = RU.status[1] = 200;
    }
    RU.gallery_state = p == "complex-empty" ? 1 : p == "complex-asking" ? 2 : 0;
    RU.alltime = p == "kotr-alltime";
    RU.kind = p == "kotr-cage" ? 1 : 0;
    RU.filter = p == "kotr-cage" ? 1 : 0;
    RU.sel = p == "complex" ? 0 : RU.sel;
    if (p == "hub-complex") { S.page = HUB; S.hub_sel = HUB_COMPLEX; G.ranked = true; return true; }
    if (hill) return false;     // the hill pages' sample room is made by the caller (gallery_step), a ring's code given
    S.page = COMPLEX;
    RU.back = HUB;
    RU.tab = p.compare(0, 4, "kotr") == 0 ? 1 : 0;
    RU.first = 0;
    if (p == "complex-one") RU.gallery_state = 3;
    return true;
}
