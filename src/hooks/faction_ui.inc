// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// FACTION WAR (build 108, owner-approved 2026-11-21): D-Mob's Team against Crow's Team, across every server. Included
// by online_screen.cpp after hub_tiles.inc. The account service runs it (server relay/accounts/factions.go): the
// picks, the queue, the points and the boards; this file is the screens and the queue's legs in the game.
//
//   THE PICKER      the first time a signed-in player opens the hub (and once at each new season), full screen:
//                   D-Mob on the left in green and gold, Crow on the right in red and black, the bosses' own large
//                   CHOOSE FIGHTER renders (online-art factions/*.png, scripts/online-art.py). Left / right moves:
//                   the side steps forward and lights up (eased, ~250 ms, a light sweep), the other dims and steps
//                   back. A asks "JOIN ...? You can switch next season", A again picks: a flash, a punch-in, a
//                   shake, the front end's impact sound and the banner YOU RIDE WITH ... (A / B skip after 0.8 s).
//                   A side the balance rule closed shows chains, a lock and the service's one-line reason.
//   THE PAGE        FACTION WAR: the season's tug of war (both totals live, counting up, the side that scored pulses),
//                   1 ON 1 and 2 ON 2 (A searches, always against the other faction), X the BOARD (both factions'
//                   ranked players and your place), your line (points, place, record).
//   THE QUEUE       POST /api/factions/queue every 1.5 s while searching (the relay the hub is on and its pings). The
//                   service makes the match and names the host: the host opens a private room of the mode on its
//                   relay and posts its code; the others join it by code on that relay (netservers_join_on) in the
//                   order the service gives (2 on 2: the host's partner first, so the room's slots are the teams) and
//                   post their slot. The fights are ordinary room fights; the games' match reports score them.
//   THE HUB TILE    FACTION WAR beside RANKED MATCH while the service answers /api/factions/live: both bosses, the bar.
//
// The live totals: GET /api/factions/live (cached by the service) every 2.5 s while the hub, the picker or the page
// shows. Tests: RECOMP_TEST_OPEN_ONLINE=factions|faction-pick opens a page; [FACTION] lines say what happened.

enum { FW_NONE = 0, FW_DMOB = 1, FW_CROW = 2 };

const char *fw_id(int f) { return f == FW_DMOB ? "dmob" : f == FW_CROW ? "crow" : ""; }
int fw_of(const std::string &s) { return s == "dmob" ? FW_DMOB : s == "crow" ? FW_CROW : FW_NONE; }
const char *fw_team(int f) { return f == FW_DMOB ? "D-MOB'S TEAM" : f == FW_CROW ? "CROW'S TEAM" : ""; }
const char *fw_boss(int f) { return f == FW_DMOB ? "D-MOB" : f == FW_CROW ? "CROW" : ""; }
int fw_fighter(int f) { return f == FW_DMOB ? 59 : 58; }       // the Battle-mode IDs (towers_rules.c)
int fw_other(int f) { return f == FW_DMOB ? FW_CROW : FW_DMOB; }
const char *fw_tagline(int f)
{
    return f == FW_DMOB ? "OLD MONEY. OLD SCHOOL. THE CITY WAS HIS FIRST." : "EVERY BLOCK PAYS. EVERY CORNER ANSWERS TO CROW.";
}
// D-Mob: his green suit and gold; Crow: his red and black (the retail renders' own colours)
ImU32 fw_main(int f, float a = 1.0f) { return with_alpha(f == FW_DMOB ? IM_COL32(28, 168, 92, 255) : IM_COL32(206, 24, 40, 255), a); }
ImU32 fw_deep(int f, float a = 1.0f) { return with_alpha(f == FW_DMOB ? IM_COL32(6, 52, 30, 255) : IM_COL32(58, 4, 10, 255), a); }
ImU32 fw_trim(int f, float a = 1.0f) { return with_alpha(f == FW_DMOB ? IM_COL32(255, 206, 72, 255) : IM_COL32(232, 228, 224, 255), a); }

std::string fw_num(long long v)        // 12,345
{
    std::string s = std::to_string(v < 0 ? -v : v), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return (v < 0 ? "-" : "") + out;
}

std::string fw_season_words(const std::string &season)     // "2026-11" -> "NOV 2026"
{
    static const char *const m[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int y = 0, mo = 0;
    if (sscanf(season.c_str(), "%d-%d", &y, &mo) != 2 || mo < 1 || mo > 12) return upper(season.c_str());
    return std::string(m[mo - 1]) + " " + std::to_string(y);
}

std::string fw_ends_words(long long ends, long long now)
{
    if (ends <= 0 || now <= 0 || ends <= now) return "";
    long long s = ends - now, d = s / 86400, h = (s % 86400) / 3600;
    if (d >= 2) return "ENDS IN " + std::to_string(d) + " DAYS";
    if (d == 1) return "ENDS IN 1 DAY " + std::to_string(h) + " H";
    return "ENDS IN " + std::to_string(h) + " H " + std::to_string((s % 3600) / 60) + " MIN";
}

// ---- the service's answers ----

struct FwLive {
    bool valid = false;
    std::string season;
    long long pts[3] = { 0, 0, 0 };
    int members[3] = { 0, 0, 0 }, searching[3] = { 0, 0, 0 };
    long long seq = 0, ends = 0, now = 0, last_at = 0;
    int last = FW_NONE;
    // build 112: crew-only fighters (faction_crew.h): the season is crew-only, from which season, each side's crew
    bool crew_rule = false;
    std::string crew_from;
    std::vector<int> crew[3];
    std::map<int, int> crew_versions;   // a story version -> its character (the first version's ID)
    int crew_lock = 3, crew_total[3] = { 0, 0, 0 };
};
struct FwMe {
    bool valid = false;
    int faction = FW_NONE, locked = FW_NONE;
    std::string picked, lock_note;
    bool can_pick = false;
    int points = 0, place = 0, of = 0, fights = 0, wins = 0, losses = 0, draws = 0;
    // build 112, the variety rule: locked fighter IDs (all versions) -> scored fights left; characters scored with
    std::map<int, int> crew_locked;
    int crew_progress = 0, crew_total = 0, crew_bonus = 0;
};
struct FwRow { int place = 0; long long account = 0; std::string name; int points = 0, wins = 0, losses = 0; };
struct FwBoard {
    bool valid = false, me = false;
    std::string season;
    long long pts[3] = { 0, 0, 0 };
    int members[3] = { 0, 0, 0 }, winner = FW_NONE;
    std::vector<FwRow> rows[3];
    int me_faction = FW_NONE, me_place = 0, me_points = 0, me_wins = 0, me_losses = 0;
};
struct FwQueue {
    std::string state, match, code, relay, why, error;
    int size = 0, mode = 0, seat = -1, waited = 0, searching = 0, dmob = 0, crow = 0, status = 0;
};

void fw_parse_live(const std::vector<std::pair<std::string, std::string>> &kv, FwLive &l)
{
    for (auto &p : kv) {
        const std::string &k = p.first, &v = p.second;
        long long n = atoll(v.c_str());
        if (k == "enabled") l.valid = n != 0;
        else if (k == "season") l.season = v;
        else if (k == "dmob") l.pts[FW_DMOB] = n;
        else if (k == "crow") l.pts[FW_CROW] = n;
        else if (k == "dmob_members") l.members[FW_DMOB] = (int)n;
        else if (k == "crow_members") l.members[FW_CROW] = (int)n;
        else if (k == "dmob_searching") l.searching[FW_DMOB] = (int)n;
        else if (k == "crow_searching") l.searching[FW_CROW] = (int)n;
        else if (k == "seq") l.seq = n;
        else if (k == "last") l.last = fw_of(v);
        else if (k == "last_at") l.last_at = n;
        else if (k == "ends") l.ends = n;
        else if (k == "now") l.now = n;
        else if (k == "crew_rule") l.crew_rule = n != 0;
        else if (k == "crew_from") l.crew_from = v;
        else if (k == "crew_dmob" || k == "crew_crow") {
            int ids[FACTION_CREW_MAX], c = faction_crew_parse(v.c_str(), ids, FACTION_CREW_MAX);
            l.crew[k == "crew_dmob" ? FW_DMOB : FW_CROW].assign(ids, ids + c);
        } else if (k == "crew_versions") {
            int a[FACTION_CREW_MAX], b[FACTION_CREW_MAX], c = faction_crew_pairs(v.c_str(), a, b, FACTION_CREW_MAX);
            l.crew_versions.clear();
            for (int i = 0; i < c; i++) l.crew_versions[a[i]] = b[i];
        } else if (k == "crew_lock") l.crew_lock = n > 0 && n < 50 ? (int)n : 3;
        else if (k == "crew_dmob_total") l.crew_total[FW_DMOB] = (int)n;
        else if (k == "crew_crow_total") l.crew_total[FW_CROW] = (int)n;
    }
}

void fw_parse_me(const std::vector<std::pair<std::string, std::string>> &kv, FwMe &m)
{
    for (auto &p : kv) {
        const std::string &k = p.first, &v = p.second;
        int n = atoi(v.c_str());
        if (k == "faction") m.faction = fw_of(v);
        else if (k == "picked_season") m.picked = v;
        else if (k == "can_pick") m.can_pick = n != 0;
        else if (k == "locked") m.locked = fw_of(v);
        else if (k == "lock_note") m.lock_note = v;
        else if (k == "points") m.points = n;
        else if (k == "place") m.place = n;
        else if (k == "of") m.of = n;
        else if (k == "fights") m.fights = n;
        else if (k == "wins") m.wins = n;
        else if (k == "losses") m.losses = n;
        else if (k == "draws") m.draws = n;
        else if (k == "crew_progress") m.crew_progress = n;
        else if (k == "crew_total") m.crew_total = n;
        else if (k == "crew_bonus") m.crew_bonus = n;
        else if (k == "crew_locked") {
            int a[FACTION_CREW_MAX], b[FACTION_CREW_MAX], c = faction_crew_pairs(v.c_str(), a, b, FACTION_CREW_MAX);
            m.crew_locked.clear();
            for (int i = 0; i < c; i++) m.crew_locked[a[i]] = b[i];
        }
    }
    m.valid = true;
}

void fw_parse_board(const char *text, FwBoard &b)
{
    b = FwBoard();
    FwLive l;
    auto kv = turf_lines(text);
    fw_parse_live(kv, l);
    b.valid = l.valid;
    b.season = l.season;
    for (int i = 0; i < 3; i++) { b.pts[i] = l.pts[i]; b.members[i] = l.members[i]; }
    for (auto &p : kv) {
        auto fl = ring_fields(p.second);
        if (p.first == "winner") b.winner = fw_of(p.second);
        else if (p.first == "row" && fl.size() >= 7) {
            int fac = fw_of(fl[0]);
            if (!fac) continue;
            FwRow r;
            r.place = (int)ring_num(fl, 1);
            r.account = ring_num(fl, 2);
            r.name = ring_str(fl, 3);
            r.points = (int)ring_num(fl, 4);
            r.wins = (int)ring_num(fl, 5);
            r.losses = (int)ring_num(fl, 6);
            b.rows[fac].push_back(r);
        } else if (p.first == "me" && fl.size() >= 5) {
            b.me = true;
            b.me_faction = fw_of(fl[0]);
            b.me_place = (int)ring_num(fl, 1);
            b.me_points = (int)ring_num(fl, 2);
            b.me_wins = (int)ring_num(fl, 3);
            b.me_losses = (int)ring_num(fl, 4);
        }
    }
}

// The gallery's samples (and the captures' fallback without a service).
const char *const FW_SAMPLE_LIVE = "enabled=1\nseason=2026-11\ndmob=1284\ncrow=1197\ndmob_members=41\ncrow_members=38\n"
                                   "seq=17\nlast=dmob\nends=1764547200\nnow=1763700000\n";
const char *const FW_SAMPLE_BOARD =
    "enabled=1\nseason=2026-11\ndmob=1284\ncrow=1197\ndmob_members=41\ncrow_members=38\nends=1764547200\nnow=1763700000\n"
    "row=dmob|1|12|Blaze|96|30|6\nrow=dmob|2|51|Kaze|81|25|6\nrow=dmob|3|34|Nia|64|19|7\nrow=dmob|4|77|Rex|52|15|7\n"
    "row=dmob|5|90|Oz|40|11|7\nrow=crow|1|91|Sol|101|32|5\nrow=crow|2|92|Tia|77|23|8\nrow=crow|3|93|Mo|60|18|6\n"
    "row=crow|4|94|Vic|44|13|5\nme=dmob|3|64|19|7\n";

struct FactionNet {
    std::mutex mu;
    FwLive live;
    int live_status = -1;           // 200 on, 404 off, 0 unreachable, -1 not asked yet
    ULONGLONG live_at = 0;
    std::atomic<int> live_busy{0};
    FwMe me;
    int me_status = -1;
    ULONGLONG me_at = 0;
    std::atomic<int> me_busy{0};
    FwBoard board;
    int board_status = -1;
    ULONGLONG board_at = 0;
    std::atomic<int> board_busy{0};
    // the pick: busy while asked; then its answer
    std::atomic<int> pick_busy{0};
    int pick_status = 0, pick_faction = FW_NONE;
    std::string pick_why, pick_error;
    ULONGLONG pick_done = 0;
    // the queue
    std::atomic<int> q_gen{0};
    bool q_on = false;
    int q_size = 1;
    std::string q_net;              // the body's "relay" and "pings" (made on the screens' thread)
    FwQueue q;
    ULONGLONG q_since = 0;
    std::string room_post;          // a /room body waiting for the queue's thread ("" none)
    std::string room_posted_for;    // the match whose room was posted (host code or a joiner's slot)
    std::string room_code;          // build 112: the faction match's room code, once posted (the crew rule's room)
    // build 112, the variety rule: this PC's crew fights started here that /me does not show yet (a character it may
    // not play again at once: the service scores ReportWindow later); each goes once /me counts more fights, or 150 s on
    struct Pending { int character, fights; ULONGLONG at; };
    std::vector<Pending> pending;
} FN;

// ---- the requests (each on its own short thread, as the Towers board) ----

void fw_fetch_live()
{
    if (gallery_on() || FN.live_busy.exchange(1)) return;
    FN.live_at = GetTickCount64();
    std::thread([]() {
        char buf[2048];
        int status = acc_state() == ACCOUNT_IN ? account_call("GET", "/api/factions/live?format=text", nullptr, buf, sizeof buf, 6000, nullptr) : -1;
        if (status < 0) { int len = 0; status = account_fetch("/api/factions/live?format=text", buf, (int)sizeof buf - 1, &len); if (len >= 0 && len < (int)sizeof buf) buf[len] = 0; }
        buf[sizeof buf - 1] = 0;
        FwLive l;
        if (status == 200) fw_parse_live(turf_lines(buf), l);
        {
            std::lock_guard<std::mutex> lock(FN.mu);
            if (status == 200 && l.valid) {
                if (FN.live.seq != l.seq || !FN.live.valid)
                    fprintf(stderr, "[FACTION] live: season %s, D-Mob %lld, Crow %lld (seq %lld, last %s)\n", l.season.c_str(),
                            l.pts[FW_DMOB], l.pts[FW_CROW], l.seq, fw_id(l.last));
                if (FN.live.crew_rule != l.crew_rule || !FN.live.valid)
                    fprintf(stderr, "[FACTION] crew rule %s (crew-only from %s; D-Mob's crew %d fighters, Crow's %d)\n",
                            l.crew_rule ? "ON" : "off", l.crew_from.empty() ? "-" : l.crew_from.c_str(),
                            (int)l.crew[FW_DMOB].size(), (int)l.crew[FW_CROW].size());
                FN.live = l;
            }
            if (status == 404 && FN.live_status != 404) fprintf(stderr, "[FACTION] live: off (HTTP 404)\n");
            FN.live_status = status == 200 && l.valid ? 200 : status;
        }
        FN.live_busy = 0;
    }).detach();
}

void fw_fetch_me()
{
    if (gallery_on() || acc_state() != ACCOUNT_IN || FN.me_busy.exchange(1)) return;
    FN.me_at = GetTickCount64();
    std::thread([]() {
        char buf[2048];
        int status = account_call("GET", "/api/factions/me?format=text", nullptr, buf, sizeof buf, 6000, nullptr);
        buf[sizeof buf - 1] = 0;
        FwMe m;
        FwLive l;
        if (status == 200) { auto kv = turf_lines(buf); fw_parse_me(kv, m); fw_parse_live(kv, l); }
        {
            std::lock_guard<std::mutex> lock(FN.mu);
            if (status == 200) {
                bool changed = !FN.me.valid || FN.me.faction != m.faction || FN.me.points != m.points || FN.me.can_pick != m.can_pick ||
                               FN.me.locked != m.locked;
                if (changed)
                    fprintf(stderr, "[FACTION] me: faction %s, can pick %d, locked %s, %d points, place %d of %d, %dW %dL\n",
                            m.faction ? fw_id(m.faction) : "none", m.can_pick ? 1 : 0, m.locked ? fw_id(m.locked) : "none", m.points,
                            m.place, m.of, m.wins, m.losses);
                FN.me = m;
                if (l.valid) { FN.live = l; FN.live_status = 200; }
                FN.pending.erase(std::remove_if(FN.pending.begin(), FN.pending.end(), [&](const FactionNet::Pending &p) {
                                     return m.fights > p.fights || GetTickCount64() - p.at > 150000; }), FN.pending.end());
            }
            FN.me_status = status;
        }
        FN.me_busy = 0;
    }).detach();
}

void fw_fetch_board()
{
    if (gallery_on() || FN.board_busy.exchange(1)) return;
    FN.board_at = GetTickCount64();
    std::thread([]() {
        std::vector<char> buf(16384);
        const char *path = "/api/factions/board?format=text&n=10";
        int status = acc_state() == ACCOUNT_IN ? account_call("GET", path, nullptr, buf.data(), (int)buf.size(), 8000, nullptr) : -1;
        if (status < 0) { int len = 0; status = account_fetch(path, buf.data(), (int)buf.size() - 1, &len); }
        buf[buf.size() - 1] = 0;
        FwBoard b;
        if (status == 200) fw_parse_board(buf.data(), b);
        {
            std::lock_guard<std::mutex> lock(FN.mu);
            if (status == 200 && b.valid) FN.board = b;
            FN.board_status = status;
        }
        fprintf(stderr, "[FACTION] board: HTTP %d, D-Mob %d rows, Crow %d rows, you %s #%d (%d pts)\n", status,
                (int)b.rows[FW_DMOB].size(), (int)b.rows[FW_CROW].size(), b.me ? fw_id(b.me_faction) : "-", b.me_place, b.me_points);
        FN.board_busy = 0;
    }).detach();
}

void fw_pick(int faction)
{
    if (FN.pick_busy.exchange(1)) return;
    {
        std::lock_guard<std::mutex> lock(FN.mu);
        FN.pick_status = 0;
        FN.pick_faction = faction;
        FN.pick_why.clear();
        FN.pick_error.clear();
    }
    fprintf(stderr, "[FACTION] pick %s...\n", fw_id(faction));
    std::thread([faction]() {
        char buf[1024], body[64];
        snprintf(body, sizeof body, "{\"faction\":\"%s\"}", fw_id(faction));
        int status = gallery_on() ? 200 : account_call("POST", "/api/factions/pick?format=text", body, buf, sizeof buf, 8000, nullptr);
        if (gallery_on()) buf[0] = 0;
        buf[sizeof buf - 1] = 0;
        std::string why, error;
        for (auto &kv : turf_lines(buf)) {
            if (kv.first == "why") why = kv.second;
            else if (kv.first == "error") error = kv.second;
        }
        fprintf(stderr, "[FACTION] pick %s: HTTP %d%s%s\n", fw_id(faction), status, why.empty() ? "" : ", why ", why.c_str());
        {
            std::lock_guard<std::mutex> lock(FN.mu);
            FN.pick_status = status;
            FN.pick_why = why;
            FN.pick_error = error;
            FN.pick_done = GetTickCount64();
            if (status == 200) { FN.me.faction = faction; FN.me.can_pick = false; FN.me.picked = FN.live.season; }
        }
        FN.me_at = 0;       // asked again soon (the place, the members)
        FN.pick_busy = 0;
    }).detach();
}

// The queue's thread: one per search (q_gen), polls while the search is on; posts the room's code or slot when the
// screens hand one over; leaves the queue when the search is called off before the room was full.
void fw_queue_thread(int gen)
{
    for (;;) {
        std::string body, room;
        {
            std::lock_guard<std::mutex> lock(FN.mu);
            if (FN.q_gen.load() != gen || !FN.q_on) break;
            body = "{\"size\":" + std::to_string(FN.q_size) + (FN.q_net.empty() ? "" : "," + FN.q_net) + "}";
            room.swap(FN.room_post);
        }
        char buf[1024];
        if (!room.empty()) {
            int st = account_call("POST", "/api/factions/room?format=text", room.c_str(), buf, sizeof buf, 8000, nullptr);
            buf[sizeof buf - 1] = 0;
            fprintf(stderr, "[FACTION] room %s: HTTP %d %s\n", room.c_str(), st, st == 200 ? "" : buf);
        }
        int status = account_call("POST", "/api/factions/queue?format=text", body.c_str(), buf, sizeof buf, 8000, nullptr);
        buf[sizeof buf - 1] = 0;
        FwQueue q;
        q.status = status;
        for (auto &kv : turf_lines(buf)) {
            const std::string &k = kv.first, &v = kv.second;
            int n = atoi(v.c_str());
            if (k == "state") q.state = v;
            else if (k == "match") q.match = v;
            else if (k == "code") q.code = v;
            else if (k == "relay") q.relay = v;
            else if (k == "why") q.why = v;
            else if (k == "error") q.error = v;
            else if (k == "size") q.size = n;
            else if (k == "mode") q.mode = n;
            else if (k == "seat") q.seat = n;
            else if (k == "waited") q.waited = n;
            else if (k == "searching") q.searching = n;
            else if (k == "dmob_searching") q.dmob = n;
            else if (k == "crow_searching") q.crow = n;
        }
        if (q.state.empty()) q.state = status == 200 ? "searching" : "error";
        bool stop = false;
        {
            std::lock_guard<std::mutex> lock(FN.mu);
            if (FN.q_gen.load() != gen) break;
            if (q.state != FN.q.state || q.code != FN.q.code)
                fprintf(stderr, "[FACTION] queue: %s (HTTP %d) match %s seat %d mode %d code %s relay %s%s%s\n", q.state.c_str(),
                        status, q.match.c_str(), q.seat, q.mode, q.code.c_str(), q.relay.c_str(), q.why.empty() ? "" : " why ",
                        q.why.c_str());
            FN.q = q;
            if (q.state == "ready" || q.state == "refused" || status == 401 || status == 404) { FN.q_on = false; stop = true; }
        }
        if (stop) return;
        for (int i = 0; i < 15; i++) {      // 1.5 s, sooner when a room post is waiting
            Sleep(100);
            std::lock_guard<std::mutex> lock(FN.mu);
            if (FN.q_gen.load() != gen || !FN.q_on || !FN.room_post.empty()) break;
        }
    }
    // called off: out of the queue (and out of a match that is still being seated)
    char buf[256];
    int st = account_call("POST", "/api/factions/leave?format=text", "{}", buf, sizeof buf, 6000, nullptr);
    fprintf(stderr, "[FACTION] queue left: HTTP %d\n", st);
}

void fw_queue_start(int size)
{
    int gen;
    {
        std::lock_guard<std::mutex> lock(FN.mu);
        gen = ++FN.q_gen;
        FN.q_on = true;
        FN.q_size = size;
        FN.q = FwQueue();
        FN.q.state = "searching";
        FN.q_since = GetTickCount64();
        FN.room_post.clear();
        FN.room_posted_for.clear();
        FN.room_code.clear();
    }
    fprintf(stderr, "[FACTION] search %s\n", size == 2 ? "2 on 2" : "1 on 1");
    // build 112: the side and the season's crew rule, fresh for the match (the fighters' tests have no hub that asks)
    FN.live_at = 0;
    FN.me_at = 0;
    fw_fetch_live();
    fw_fetch_me();
    if (!gallery_on()) std::thread(fw_queue_thread, gen).detach();
}

void fw_queue_stop()
{
    std::lock_guard<std::mutex> lock(FN.mu);
    if (!FN.q_on) return;
    FN.q_on = false;        // the thread sees it, leaves the queue and ends
    fprintf(stderr, "[FACTION] search called off\n");
}

bool fw_searching() { std::lock_guard<std::mutex> lock(FN.mu); return FN.q_on; }
FwQueue fw_queue() { std::lock_guard<std::mutex> lock(FN.mu); return FN.q; }

// What the screens read (the gallery: the samples).
FwLive fw_live()
{
    if (gallery_on()) { FwLive l; fw_parse_live(turf_lines(FW_SAMPLE_LIVE), l); return l; }
    if (!FN.live_at || GetTickCount64() - FN.live_at > 2500) fw_fetch_live();
    std::lock_guard<std::mutex> lock(FN.mu);
    return FN.live;
}
FwMe fw_me()
{
    if (gallery_on()) { FwMe m; m.valid = true; m.faction = G_fw_faction; m.can_pick = G_fw_faction == FW_NONE; m.locked = G_fw_locked;
                        m.lock_note = "Crow's Team has 58% of this season's active players: the next picks go to D-Mob's Team.";
                        m.points = 64; m.place = 3; m.of = 41; m.fights = 26; m.wins = 19; m.losses = 7; return m; }
    if (!FN.me_at || GetTickCount64() - FN.me_at > 20000) fw_fetch_me();
    std::lock_guard<std::mutex> lock(FN.mu);
    return FN.me;
}
int fw_my_side() { FwMe m = fw_me(); return m.valid ? m.faction : FW_NONE; }
FwBoard fw_board()
{
    if (gallery_on()) { FwBoard b; fw_parse_board(FW_SAMPLE_BOARD, b); return b; }
    if (!FN.board_at || GetTickCount64() - FN.board_at > 30000) fw_fetch_board();
    std::lock_guard<std::mutex> lock(FN.mu);
    return FN.board;
}
// The hub tile shows while the service answers the live totals (and in the gallery).
bool faction_shown()
{
    if (gallery_on()) return G_fw_shown;
    fw_live();
    std::lock_guard<std::mutex> lock(FN.mu);
    return FN.live_status == 200;
}

// ---- the live numbers: counting up to the new total, a pulse on the side that scored ----

struct FwCounter {
    double from = 0, shown = 0;
    long long target = -1;
    ULONGLONG t0 = 0, pulse_at = 0;
    void set(long long v, ULONGLONG now)
    {
        if (target < 0) { target = v; shown = from = (double)v; return; }
        if (v == target) return;
        if (v > target) pulse_at = now;
        from = shown;
        target = v;
        t0 = now;
    }
    void step(ULONGLONG now)
    {
        if (target < 0) return;
        float t = std::min(1.0f, (float)(now - t0) / 1100.0f);
        float e = 1 - (1 - t) * (1 - t) * (1 - t);
        shown = from + (target - from) * e;
    }
    float pulse(ULONGLONG now) const     // 1 at a score, fading over 0.9 s
    {
        if (!pulse_at || now - pulse_at > 900) return 0;
        return 1.0f - (float)(now - pulse_at) / 900.0f;
    }
};

struct FactionUi {
    // live
    FwCounter pts[3], members[3];
    float share = 0.5f;             // D-Mob's part of the bar, eased
    bool share_set = false;
    ULONGLONG last_frame = 0;
    // the picker
    int sel = FW_DMOB;
    float k[3] = { 0, 0, 0 };       // each side's step forward, eased 0..1
    ULONGLONG sweep_at = 0;         // the chosen side's light sweep began
    bool confirm = false, confirm_yes = true;
    int slam = FW_NONE;             // the select moment's faction (0: none)
    ULONGLONG slam_at = 0;
    bool slam_stay = false;
    bool pick_sent = false;
    int pick_from = FW_NONE;        // the side before the pick (the banner says STAY or RIDE)
    ULONGLONG denied_at = 0;        // A on the closed side: its lock shakes, its reason flashes
    bool auto_done = false;         // the hub opened the picker by itself this session
    Page pick_back = HUB;           // where B goes from the picker
    // the page
    int mode_sel = 0;               // 0 one on one, 1 two on two
    bool board = false;
    ULONGLONG opened = 0;
    // the match's legs (the screens' thread)
    std::string acted_match;        // the match whose room this PC opened or joined
    std::string acted_code;
    bool acted_join = false;
} FU;

void fw_live_step(const FwLive &l)
{
    ULONGLONG now = GetTickCount64();
    if (l.valid) {
        for (int f = 1; f <= 2; f++) { FU.pts[f].set(l.pts[f], now); FU.members[f].set(l.members[f], now); }
    }
    for (int f = 1; f <= 2; f++) { FU.pts[f].step(now); FU.members[f].step(now); }
    double a = FU.pts[FW_DMOB].shown, b = FU.pts[FW_CROW].shown;
    float want = a + b > 0 ? (float)(a / (a + b)) : 0.5f;
    want = std::max(0.06f, std::min(0.94f, want));
    float dt = FU.last_frame ? std::min(0.1f, (now - FU.last_frame) / 1000.0f) : 0.016f;
    FU.last_frame = now;
    if (!FU.share_set) { FU.share = want; FU.share_set = true; }
    else FU.share += (want - FU.share) * std::min(1.0f, dt * 5.0f);
}

// ---- drawing ----

// A part of a texture with its own alpha at the left and the right edge (a fade into whatever is behind).
void fw_image_fade(ImDrawList *dl, const Frame &f, const Tex &t, float x, float y, float w, float h, ImU32 tint, ImVec2 uv0,
                   ImVec2 uv1, float a_left, float a_right)
{
    ImU32 cl = with_alpha(tint, a_left * ((tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
    ImU32 cr = with_alpha(tint, a_right * ((tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
    dl->PushTextureID((ImTextureID)(intptr_t)t.srv);
    dl->PrimReserve(6, 4);
    ImDrawIdx i0 = (ImDrawIdx)dl->_VtxCurrentIdx;
    dl->PrimWriteIdx(i0); dl->PrimWriteIdx((ImDrawIdx)(i0 + 1)); dl->PrimWriteIdx((ImDrawIdx)(i0 + 2));
    dl->PrimWriteIdx(i0); dl->PrimWriteIdx((ImDrawIdx)(i0 + 2)); dl->PrimWriteIdx((ImDrawIdx)(i0 + 3));
    dl->PrimWriteVtx(f.p(x, y), uv0, cl);
    dl->PrimWriteVtx(f.p(x + w, y), ImVec2(uv1.x, uv0.y), cr);
    dl->PrimWriteVtx(f.p(x + w, y + h), uv1, cr);
    dl->PrimWriteVtx(f.p(x, y + h), ImVec2(uv0.x, uv1.y), cl);
    dl->PopTextureID();
}

// A boss's large render (online-art factions/<id>.png, the retail 512x512 CHOOSE FIGHTER render). `lettering`: with the
// retail name lettering beside the head (the D-MOB plate, SNOOP DOGG AS CROW; the picker, the owner's choice), else the
// figure only (the page's small panels, where a part of the lettering would show). The picture's left edge, where the
// render itself is cut, fades out. cx is the figure's middle, bottom its foot, h its height. Without the art: the hub
// portrait.
bool fw_render(ImDrawList *dl, const Frame &f, int fac, float cx, float bottom, float h, ImU32 tint, bool lettering = false,
               bool fade = true)
{
    const Tex &t = tex(std::string("factions/") + fw_id(fac) + ".png");
    if (t.srv && t.w > 0) {
        // the figure's bands (pixels of the 512x512 render): D-Mob from row 13 to 358, his head beside the D-MOB plate
        // (x 120 on) to row 64, his fist reaching x 182 below; Crow from row 0 to 512 within x 170 (the lettering is right
        // of it)
        struct Band { float v0, v1, u1; };
        // (figure only: D-Mob's head left of his plate, which starts at x 100; Crow's hat left of SNOOP DOGG (x 92) to row 44,
        // his brim left of AS CROW (x 130) to row 76)
        static const Band dmob[] = { { 13, 64, 98 }, { 64, 358, 186 } }, crow[] = { { 0, 44, 92 }, { 44, 76, 128 }, { 76, 512, 170 } };
        static const Band dmob_full[] = { { 13, 358, 265 } }, crow_full[] = { { 0, 512, 259 } };
        const Band *b = fac == FW_DMOB ? (lettering ? dmob_full : dmob) : (lettering ? crow_full : crow);
        int n = lettering ? 1 : fac == FW_DMOB ? 2 : 3;
        float top_px = b[0].v0, bot_px = b[n - 1].v1, wide_px = fac == FW_DMOB ? 186.0f : 170.0f;
        float k = h / (bot_px - top_px), x0 = cx - wide_px * k * 0.5f, y0 = bottom - h;
        for (int i = 0; i < n; i++) {
            float bw = b[i].u1 * k, by = y0 + (b[i].v0 - top_px) * k, bh = (b[i].v1 - b[i].v0) * k;
            float fw = fade ? std::min(bw, wide_px * k * 0.16f) : 0;
            ImVec2 uv0(0, b[i].v0 / t.h), uv1(b[i].u1 / t.w, b[i].v1 / t.h);
            float um = (fw / k) / t.w;      // the fade strip's width in the picture
            if (fw > 0) fw_image_fade(dl, f, t, x0, by, fw, bh, tint, uv0, ImVec2(um, uv1.y), 0.0f, 1.0f);
            image(dl, f, t, x0 + fw, by, bw - fw, bh, tint, ImVec2(um, uv0.y), uv1);
        }
        return true;
    }
    float w = h * 101.0f / 197.0f;
    return portrait(dl, f, fw_fighter(fac), cx - w / 2, bottom - h, w, h, tint);
}

// A slanted band of light across a box at place p (0..1).
void fw_sweep(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, float p, ImU32 col)
{
    if (p < 0 || p > 1) return;
    float bw = w * 0.22f, cx = x - bw + (w + bw * 2) * p, slant = h * 0.35f;
    ImU32 clear = with_alpha(col, 0);
    dl->PushClipRect(f.p(x, y), f.p(x + w, y + h), true);
    dl->AddQuadFilled(f.p(cx - bw * 0.15f + slant, y), f.p(cx + bw * 0.15f + slant, y), f.p(cx + bw * 0.15f - slant, y + h),
                      f.p(cx - bw * 0.15f - slant, y + h), col);
    dl->AddRectFilledMultiColor(f.p(cx - bw, y), f.p(cx - bw * 0.15f, y + h), clear, with_alpha(col, 0.35f), with_alpha(col, 0.35f), clear);
    dl->AddRectFilledMultiColor(f.p(cx + bw * 0.15f, y), f.p(cx + bw, y + h), with_alpha(col, 0.35f), clear, clear, with_alpha(col, 0.35f));
    dl->PopClipRect();
}

// A padlock at cx, cy (about `s` tall) and a chain across a box: the side the balance rule closed.
void fw_lock(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, float a)
{
    ImU32 steel = with_alpha(IM_COL32(190, 192, 198, 255), a), dark = with_alpha(IM_COL32(40, 42, 48, 255), a);
    float sc = f.s();
    for (int c = 0; c < 2; c++) {       // two chains, crossing
        float x0 = x + 6, y0 = c ? y + h * 0.18f : y + h * 0.78f, x1 = x + w + 10, y1 = c ? y + h * 0.66f : y + h * 0.30f;
        float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)), ux = (x1 - x0) / len, uy = (y1 - y0) / len;
        int links = (int)(len / 13);
        for (int i = 0; i < links; i++) {
            float cx = x0 + ux * (i * 13 + 6.5f), cy = y0 + uy * (i * 13 + 6.5f);
            float rx = i % 2 ? 7.5f : 8.5f, ry = i % 2 ? 2.4f : 4.6f;
            // an oval link along the chain: four points of its outline
            ImVec2 pts[12];
            for (int k = 0; k < 12; k++) {
                float t = k * 6.2831853f / 12, ex = cosf(t) * rx, ey = sinf(t) * ry;
                pts[k] = f.p(cx + ex * ux - ey * uy, cy + ex * uy + ey * ux);
            }
            dl->AddPolyline(pts, 12, dark, ImDrawFlags_Closed, 4.2f * sc);
            dl->AddPolyline(pts, 12, steel, ImDrawFlags_Closed, 2.2f * sc);
        }
    }
    float cx = x + w / 2, cy = y + h * 0.48f, s = 46;
    dl->PathArcTo(f.p(cx, cy - s * 0.18f), s * 0.30f * sc, 3.14159f, 6.28318f, 20);
    dl->PathStroke(dark, 0, 9.0f * sc);
    dl->PathArcTo(f.p(cx, cy - s * 0.18f), s * 0.30f * sc, 3.14159f, 6.28318f, 20);
    dl->PathStroke(steel, 0, 5.5f * sc);
    rect(dl, f, cx - s * 0.42f - 2, cy - s * 0.2f - 2, s * 0.84f + 4, s * 0.66f + 4, dark, 6);
    dl->AddRectFilledMultiColor(f.p(cx - s * 0.42f, cy - s * 0.2f), f.p(cx + s * 0.42f, cy + s * 0.46f), with_alpha(IM_COL32(236, 200, 90, 255), a),
                                with_alpha(IM_COL32(200, 150, 50, 255), a), with_alpha(IM_COL32(120, 80, 20, 255), a), with_alpha(IM_COL32(170, 120, 40, 255), a));
    dl->AddCircleFilled(f.p(cx, cy + s * 0.06f), 4.5f * sc, dark);
    rect(dl, f, cx - 1.6f, cy + s * 0.08f, 3.2f, s * 0.2f, dark);
}

// The season's tug of war: D-Mob's part from the left, Crow's from the right, the split eased, both totals counting up
// and the side that scored pulsing. `big` for the page's top, else the picker's foot and the hub tile.
void fw_tug(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, bool big)
{
    ULONGLONG now = GetTickCount64();
    float split = x + w * FU.share;
    // the bar: each side's colour fading into its deep shade towards its end
    rect(dl, f, x - 2, y - 2, w + 4, h + 4, IM_COL32(0, 0, 0, 220), 3);
    dl->AddRectFilledMultiColor(f.p(x, y), f.p(split, y + h), fw_deep(FW_DMOB), fw_main(FW_DMOB), fw_main(FW_DMOB), fw_deep(FW_DMOB));
    dl->AddRectFilledMultiColor(f.p(split, y), f.p(x + w, y + h), fw_main(FW_CROW), fw_deep(FW_CROW), fw_deep(FW_CROW), fw_main(FW_CROW));
    dl->AddRectFilledMultiColor(f.p(x, y), f.p(x + w, y + h * 0.45f), IM_COL32(255, 255, 255, 46), IM_COL32(255, 255, 255, 46),
                                IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    for (int fac = 1; fac <= 2; fac++) {        // the pulse: the scoring side's bar flares
        float p = FU.pts[fac].pulse(now);
        if (p <= 0) continue;
        float x0 = fac == FW_DMOB ? x : split, x1 = fac == FW_DMOB ? split : x + w;
        rect(dl, f, x0, y, x1 - x0, h, with_alpha(fw_trim(fac), 0.45f * p));
        dl->AddRect(f.p(x0 - 3 * p, y - 3 * p), f.p(x1 + 3 * p, y + h + 3 * p), with_alpha(fw_trim(fac), p), 3 * f.s(), 0, 2.5f * f.s());
    }
    // the clash at the split: a bright seam and sparks
    float t = (float)(now % 1000) / 1000.0f;
    dl->AddRectFilled(f.p(split - 1.5f, y - 4), f.p(split + 1.5f, y + h + 4), IM_COL32(255, 250, 230, 255));
    dl->AddCircleFilled(f.p(split, y + h / 2), (h * 0.55f + 2 * sinf(t * 6.283f)) * f.s(), IM_COL32(255, 240, 200, 60));
    for (int i = 0; i < 4; i++) {
        float a = t * 6.283f + i * 1.57f, r = h * (0.6f + 0.25f * sinf(a * 3));
        dl->AddCircleFilled(f.p(split + cosf(a) * r, y + h / 2 + sinf(a) * r * 0.6f), 1.4f * f.s(), IM_COL32(255, 230, 160, 200));
    }
    // the numbers
    float ns = big ? 30 : 15;
    for (int fac = 1; fac <= 2; fac++) {
        float p = FU.pts[fac].pulse(now), size = ns * (1 + 0.22f * p);
        std::string n = fw_num((long long)(FU.pts[fac].shown + 0.5));
        bool left = fac == FW_DMOB;
        float nx = left ? x + 8 : x + w - 8, ny = y + (h - size * 1.2f) / 2 + (big ? 0 : 0);
        text(dl, f, g_numbers ? g_numbers : g_title, size, nx, ny, with_alpha(p > 0 ? fw_trim(fac) : WHITE, 1), n.c_str(), left ? LEFT : RIGHT, 1.6f);
    }
}

// ---- THE PICKER ----

void open_faction_pick(Page back)
{
    S.page = FACTION_PICK;
    FwMe m = fw_me();
    FU.sel = m.faction ? m.faction : FU.sel ? FU.sel : FW_DMOB;
    if (m.locked && FU.sel == m.locked && m.faction != m.locked) FU.sel = fw_other(m.locked);
    FU.k[1] = FU.k[2] = 0;
    FU.sweep_at = GetTickCount64();
    FU.confirm = false;
    FU.slam = FW_NONE;
    FU.pick_sent = false;
    FU.pick_back = back;
    fw_fetch_me();
    fprintf(stderr, "[FACTION] picker open (faction %s)\n", m.faction ? fw_id(m.faction) : "none");
}

void open_factions()
{
    FwMe m = fw_me();
    if (!gallery_on() && m.valid && (m.faction == FW_NONE || m.can_pick)) { open_faction_pick(HUB); return; }
    S.page = FACTIONS;
    FU.board = false;
    FU.opened = GetTickCount64();
    fw_fetch_me();
    fw_fetch_live();
    fprintf(stderr, "[FACTION] page open\n");
}

void draw_faction_pick(ImDrawList *dl, const Frame &f, const Pad &pad, ImVec2 display)
{
    ULONGLONG now = GetTickCount64();
    FwMe me = fw_me();
    FwLive live = fw_live();
    fw_live_step(live);
    float dt = 0.016f;
    {
        static ULONGLONG last;
        if (last) dt = std::min(0.1f, (now - last) / 1000.0f);
        last = now;
    }
    // input (none during the select moment's first 0.8 s)
    if (FU.slam) {
        float st = (now - FU.slam_at) / 1000.0f;
        if ((st > 0.8f && (pad.a || pad.b)) || st > 3.2f) {
            FU.slam = FW_NONE;
            open_factions();
            return;
        }
    } else if (FU.confirm) {
        if (pad.left || pad.right) FU.confirm_yes = !FU.confirm_yes;
        if (pad.b) FU.confirm = false;
        else if (pad.a) {
            if (FU.confirm_yes) { FU.pick_from = me.faction; fw_pick(FU.sel); FU.pick_sent = true; }
            FU.confirm = false;
        }
    } else if (!FU.pick_sent) {
        int was = FU.sel;
        if (pad.left) FU.sel = FW_DMOB;
        if (pad.right) FU.sel = FW_CROW;
        if (mouse_pick(f, 0, 60, 320, 330)) FU.sel = FW_DMOB;
        if (mouse_pick(f, 320, 60, 320, 330)) FU.sel = FW_CROW;
        if (FU.sel != was) {
            FU.sweep_at = now;
            towers_play_sfx(3);      // the front end's own scroll sound (selectPoster)
            fprintf(stderr, "[FACTION] picker: %s\n", fw_id(FU.sel));
        }
        bool closed = me.locked == FU.sel && me.faction != FU.sel;
        if (pad.a || mouse_click(f, FU.sel == FW_DMOB ? 0 : 320, 60, 320, 330)) {
            if (closed) {
                FU.denied_at = now;
                towers_play_sfx(5);
                fprintf(stderr, "[FACTION] picker: %s is closed for new picks (%s)\n", fw_id(FU.sel), me.lock_note.c_str());
            }
            else { FU.confirm = true; FU.confirm_yes = true; towers_play_sfx(5); }
        }
        if (pad.b) {
            S.page = FU.pick_back == FACTION_PICK ? HUB : FU.pick_back;
            if (me.faction && S.page == HUB && me.can_pick) say(std::string("You stay with ") + fw_team(me.faction) + " this season.");
            return;
        }
    }
    // the pick's answer
    {
        int status = 0;
        std::string err;
        ULONGLONG done = 0;
        {
            std::lock_guard<std::mutex> lock(FN.mu);
            status = FN.pick_status;
            err = FN.pick_error;
            done = FN.pick_done;
        }
        if (FU.pick_sent && !FN.pick_busy.load() && done) {
            FU.pick_sent = false;
            {
                std::lock_guard<std::mutex> lock(FN.mu);
                FN.pick_done = 0;
            }
            if (status == 200) {
                FU.slam = FU.sel;
                FU.slam_at = now;
                FU.slam_stay = FU.pick_from == FU.sel;
                towers_play_sfx(1);      // the front end's landing hit
                recomp_settings_set_text("online", "faction_seen", live.season.c_str());
                fprintf(stderr, "[FACTION] picked %s: the banner\n", fw_id(FU.sel));
            } else {
                say(err.empty() ? std::string("The pick did not go through (HTTP ") + std::to_string(status) + "). Try again." : err, 6000);
            }
        }
    }
    // the eased steps: the chosen side forward, the other back (about 250 ms)
    for (int s = 1; s <= 2; s++) {
        float want = s == FU.sel ? 1.0f : 0.0f;
        if (FU.slam) want = s == FU.slam ? 1.0f : 0.0f;
        FU.k[s] += (want - FU.k[s]) * std::min(1.0f, dt * 12.0f);
    }
    float st = FU.slam ? (now - FU.slam_at) / 1000.0f : -1;
    float shake_x = 0, shake_y = 0, punch = 0;
    if (st >= 0) {
        float d = std::max(0.0f, 1 - st / 0.45f);
        shake_x = sinf(st * 71) * 7 * d;
        shake_y = cosf(st * 53) * 5 * d;
        punch = st < 0.12f ? st / 0.12f : std::max(0.45f, 1 - (st - 0.12f) * 1.5f);
    }
    // the backdrop: black, each half its faction's colour rising from the floor, a slanted seam in the middle
    dl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(4, 4, 6, 255));
    Frame g = f;
    g.o.x += shake_x * f.sx;
    g.o.y += shake_y * f.sy;
    float seam = 320;
    for (int s = 1; s <= 2; s++) {
        bool left = s == FW_DMOB;
        float a = 0.35f + 0.65f * FU.k[s];
        float x0 = left ? -200 : seam, x1 = left ? seam : 840;
        dl->AddRectFilledMultiColor(g.p(x0, 0), g.p(x1, 480), fw_deep(s, 0.55f * a), fw_deep(s, 0.55f * a), fw_main(s, 0.75f * a), fw_main(s, 0.75f * a));
        // a stage light behind the boss
        float cx = left ? 170 : 470;
        for (int r = 0; r < 6; r++)
            dl->AddCircleFilled(g.p(cx, 250), (190 - r * 26) * f.s(), with_alpha(fw_main(s), (0.05f + 0.03f * r) * a), 48);
    }
    // the seam: a dark slanted band with a bright edge
    dl->AddQuadFilled(g.p(seam - 34, 0), g.p(seam + 6, 0), g.p(seam - 6, 480), g.p(seam - 46, 480), IM_COL32(4, 4, 6, 255));
    dl->AddQuadFilled(g.p(seam + 6, 0), g.p(seam + 40, 0), g.p(seam + 28, 480), g.p(seam - 6, 480), IM_COL32(4, 4, 6, 255));
    dl->AddLine(g.p(seam - 34, 0), g.p(seam - 46, 480), fw_trim(FW_DMOB, 0.8f), 2.0f * f.s());
    dl->AddLine(g.p(seam + 40, 0), g.p(seam + 28, 480), fw_main(FW_CROW, 0.9f), 2.0f * f.s());
    // the bosses: the chosen one larger, lit and nearer the middle; the other dimmed and back
    for (int pass = 0; pass < 2; pass++) {
        int s = pass == 0 ? fw_other(FU.slam ? FU.slam : FU.sel) : (FU.slam ? FU.slam : FU.sel);
        bool left = s == FW_DMOB;
        float k = FU.k[s];
        float scale = 0.90f + 0.12f * k + (FU.slam == s ? 0.14f * punch : 0.0f);
        float h = (s == FW_DMOB ? 330 : 338) * scale, cx = (left ? 128 : 490) + (left ? 18 : -18) * k;
        int lum = (int)(80 + 175 * k);
        if (FU.slam && FU.slam != s) lum = (int)(lum * std::max(0.25f, 1 - st * 2));
        fw_render(dl, g, s, cx, 412 + 6 * (1 - k), h, IM_COL32(lum, lum, lum, 255), true);
    }
    // the light sweep over the chosen side (once, 450 ms after a move)
    {
        float p = (now - FU.sweep_at) / 450.0f;
        if (p <= 1 && !FU.slam) fw_sweep(dl, g, FU.sel == FW_DMOB ? 0 : seam, 0, 320, 480, p, fw_trim(FU.sel, 0.55f));
    }
    // the title
    {
        bool season_repick = me.faction && me.can_pick;
        const char *head = season_repick ? "NEW SEASON: STAY OR SWITCH" : "CHOOSE YOUR SIDE";
        rect(dl, g, 0, 14, 640, 44, IM_COL32(0, 0, 0, 150));
        text(dl, g, g_stencil, 30, 320, 16, CREAM, head, CENTER, 1.8f);
        std::string sub = "FACTION WAR  -  SEASON " + fw_season_words(live.season) + "  -  ONE PICK PER SEASON";
        if (live.season.empty()) sub = "FACTION WAR  -  ONE PICK PER SEASON";
        text(dl, g, g_title, 11, 320, 46, with_alpha(CREAM, 0.75f), sub.c_str(), CENTER, 1.0f);
        if (!FU.slam) text(dl, g, g_stencil, 40, seam - 4, 222, with_alpha(WHITE, 0.92f), "VS", CENTER, 2.0f);
    }
    // each side's name, line and live numbers
    for (int s = 1; s <= 2; s++) {
        bool left = s == FW_DMOB;
        float k = FU.k[s], a = 0.45f + 0.55f * k;
        float x = left ? 24 : 616;
        Align al = left ? LEFT : RIGHT;
        float ty = 300 - 4 * k;
        float size = 30 + 4 * k;
        text(dl, g, g_stencil, size, x, ty, with_alpha(fw_trim(s), a), fw_team(s), al, 2.0f);
        text(dl, g, g_hand, 12, x, ty + size * 1.15f, with_alpha(CREAM, a), fw_tagline(s), al, 1.0f);
        char line[96];
        long long mem = (long long)(FU.members[s].shown + 0.5), pts = (long long)(FU.pts[s].shown + 0.5);
        snprintf(line, sizeof line, "%s %s   %s SEASON %s", fw_num(mem).c_str(), mem == 1 ? "MEMBER" : "MEMBERS",
                 fw_num(pts).c_str(), pts == 1 ? "POINT" : "POINTS");
        text(dl, g, g_title, 12, x, ty + size * 1.15f + 18, with_alpha(WHITE, a), line, al, 1.2f);
        if (me.faction == s) {
            const char *yours = "YOUR SIDE";
            float tw = text_width(g, g_title, 10, yours) + 14, bx = left ? x : x - tw;
            rect(dl, g, bx, ty - 18, tw, 15, fw_main(s, 0.9f), 3);
            text(dl, g, g_title, 10, bx + tw / 2, ty - 16.5f, WHITE, yours, CENTER, 0.8f);
        }
        // closed by the balance rule: chains, the lock, the reason
        if (me.locked == s && me.faction != s && !FU.slam) {
            float bx = left ? 0 : seam, bw = 320;
            float dt_d = FU.denied_at ? (now - FU.denied_at) / 1000.0f : 9.0f, jolt = dt_d < 0.4f ? sinf(dt_d * 60) * 6 * (1 - dt_d / 0.4f) : 0;
            rect(dl, g, bx, 64, bw, 226, IM_COL32(0, 0, 0, 120));
            fw_lock(dl, g, bx + 20 + jolt, 70, bw - 40, 200, 0.95f);
            std::string why = me.lock_note.empty() ? std::string(fw_team(s)) + " IS FULL FOR NOW" : upper(me.lock_note.c_str());
            // the reason on its plate, inside it and clear of the VS: one line where it fits, else two (split at a space)
            float px = bx + 24, pw = bw - 34, inner = pw - 12;
            std::string l1 = why, l2;
            if (text_width(g, g_title, 11, why.c_str()) > inner) {
                size_t mid = why.size() / 2, a = why.rfind(' ', mid), b = why.find(' ', mid);
                size_t cut = a == std::string::npos ? b : b == std::string::npos ? a : (mid - a <= b - mid ? a : b);
                if (cut != std::string::npos) { l1 = why.substr(0, cut); l2 = why.substr(cut + 1); }
            }
            float wsz = std::min(fit_size(g, g_title, 11, l1.c_str(), inner), l2.empty() ? 11.0f : fit_size(g, g_title, 11, l2.c_str(), inner));
            bool flash = dt_d < 1.2f && fmodf(dt_d, 0.3f) < 0.15f;
            float ph = l2.empty() ? 20.0f : 32.0f;
            rect(dl, g, px, 258, pw, ph, flash ? IM_COL32(220, 30, 40, 245) : IM_COL32(120, 10, 20, 230), 4);
            text(dl, g, g_title, wsz, px + pw / 2, 261, WHITE, l1.c_str(), CENTER, 0.8f);
            if (!l2.empty()) text(dl, g, g_title, wsz, px + pw / 2, 261 + wsz * 1.15f, WHITE, l2.c_str(), CENTER, 0.8f);
        }
    }
    // the tug of war at the foot
    fw_tug(dl, g, 40, 394, 560, 16, false);
    text(dl, g, g_title, 10, 320, 380, with_alpha(CREAM, 0.7f), "THE SEASON SO FAR", CENTER, 1.0f);
    // the hints
    if (!FU.slam && !FU.confirm) {
        float hx = 54;
        hx += hint(dl, f, hx, 428, "a", FU.pick_sent ? "JOINING..." : "PICK");
        hint(dl, f, hx, 428, "b", "BACK");
        text(dl, f, g_title, 11, 586, 430, with_alpha(CREAM, 0.7f), "LEFT / RIGHT: CHOOSE A SIDE", RIGHT, 1.0f);
    }
    // the confirm step
    if (FU.confirm) {
        rect(dl, f, 0, 0, 640, 480, IM_COL32(0, 0, 0, 140));
        glass(dl, f, 150, 168, 340, 132, 10, fw_deep(FU.sel), 0.92f);
        frame_rect(dl, f, 150, 168, 340, 132, fw_trim(FU.sel, 0.8f), 1.5f);
        std::string q = std::string("JOIN ") + fw_team(FU.sel) + "?";
        if (me.faction == FU.sel) q = std::string("STAY WITH ") + fw_team(FU.sel) + "?";
        text(dl, f, g_stencil, std::min(26.0f, fit_size(f, g_stencil, 26, q.c_str(), 316)), 320, 180, CREAM, q.c_str(), CENTER, 1.6f);
        text(dl, f, g_hand, 13, 320, 216, with_alpha(CREAM, 0.85f), "You fight for this side all season. You can switch next season.", CENTER, 0.8f, 310);
        for (int i = 0; i < 2; i++) {
            bool on = (i == 0) == FU.confirm_yes;
            float bx = i == 0 ? 186 : 330, by = 258;
            rect(dl, f, bx, by, 124, 28, on ? fw_main(FU.sel, 0.95f) : IM_COL32(30, 30, 34, 230), 5);
            text(dl, f, g_title, 15, bx + 62, by + 5, on ? WHITE : GREY, i == 0 ? "YES, I RIDE" : "NOT YET", CENTER, 1.0f);
            if (on) selector(dl, f, bx, by, 124, 28);
            if (mouse_click(f, bx, by, 124, 28)) { FU.confirm_yes = i == 0; const_cast<Pad &>(pad).a = true; }
        }
        float hx = 54;
        hx += hint(dl, f, hx, 428, "a", "CONFIRM");
        hint(dl, f, hx, 428, "b", "BACK");
    }
    // the select moment: a flash, the punch-in and shake above, then the banner slams in
    if (FU.slam) {
        float fl = std::max(0.0f, 1 - st / 0.28f);
        if (fl > 0) dl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(255, 255, 255, (int)(235 * fl)));
        if (st > 0.18f) {
            float bt = std::min(1.0f, (st - 0.18f) / 0.16f), sc = 2.6f - 1.6f * (1 - (1 - bt) * (1 - bt));
            float alpha = bt;
            float by = 196, bh = 64 * std::min(1.0f, 0.6f + 0.4f * bt);
            // the plate: across the screen, the faction's colour, gold or bone trim, a slight tilt
            ImVec2 a0 = g.p(-20, by - bh / 2 + 6), a1 = g.p(660, by - bh / 2 - 6), a2 = g.p(660, by + bh / 2 - 6), a3 = g.p(-20, by + bh / 2 + 6);
            dl->AddQuadFilled(a0, a1, a2, a3, fw_deep(FU.slam, 0.94f * alpha));
            dl->AddLine(a0, a1, fw_trim(FU.slam, alpha), 3 * f.s());
            dl->AddLine(a3, a2, fw_trim(FU.slam, alpha), 3 * f.s());
            std::string words = std::string(FU.slam_stay ? "YOU STAY WITH " : "YOU RIDE WITH ") + fw_boss(FU.slam);
            float size = 40 * sc;
            text(dl, g, g_stencil, size, 320, by - size * 0.62f, with_alpha(fw_trim(FU.slam), alpha), words.c_str(), CENTER, 2.4f);
            if (st > 0.5f) {
                std::string sub = std::string(fw_team(FU.slam)) + "  -  SEASON " + fw_season_words(live.season);
                text(dl, g, g_title, 13, 320, by + bh / 2 + 10, with_alpha(CREAM, std::min(1.0f, (st - 0.5f) * 3)), sub.c_str(), CENTER, 1.2f);
            }
        }
        if (st > 0.8f) {
            float hx = 54;
            hint(dl, f, hx, 428, "a", "CONTINUE");
        }
    }
}

// ---- THE PAGE ----

bool fw_outdated()      // a newer build is out: this one starts no new matches (as the hub's QUICK MATCH)
{
    int newer = 0;
    enum UpdaterState us = updater_state();
    if (us >= UPDATER_AVAILABLE && us <= UPDATER_STARTED) updater_info(&newer, nullptr, 0, nullptr, 0);
    return updater_local_build() > 0 && newer > updater_local_build();
}

const char *fw_queue_words(const FwQueue &q, int mine)
{
    if (q.state == "host") return "MATCH FOUND: OPENING THE ROOM...";
    if (q.state == "wait") return "MATCH FOUND: WAITING FOR THE ROOM...";
    if (q.state == "join") return "MATCH FOUND: JOINING THE ROOM...";
    if (q.state == "ready") return "EVERYONE IS IN: TO THE ROOM";
    if (q.state == "refused" || q.state == "error") return "THE SEARCH STOPPED";
    return mine == FW_DMOB ? "SEARCHING FOR CROW'S TEAM..." : "SEARCHING FOR D-MOB'S TEAM...";
}

void draw_factions(ImDrawList *dl, const Frame &f, const Pad &pad, ImVec2 display)
{
    (void)display;
    ULONGLONG now = GetTickCount64();
    FwMe me = fw_me();
    FwLive live = fw_live();
    fw_live_step(live);
    if (!FN.me_at || now - FN.me_at > 8000) fw_fetch_me();     // the points after a fight
    bool searching = fw_searching();
    FwQueue q = fw_queue();
    int mine = me.faction ? me.faction : FW_DMOB;
    page_title(dl, f, "FACTION WAR");

    // the tug of war: the bosses' heads at the ends, the bar between
    {
        glass(dl, f, 36, 66, 568, 96, 8);
        dl->PushClipRect(f.p(36, 66), f.p(604, 162), true);
        dl->AddRectFilledMultiColor(f.p(36, 66), f.p(320, 162), fw_deep(FW_DMOB, 0.85f), fw_deep(FW_DMOB, 0.2f), fw_deep(FW_DMOB, 0.2f), fw_deep(FW_DMOB, 0.85f));
        dl->AddRectFilledMultiColor(f.p(320, 66), f.p(604, 162), fw_deep(FW_CROW, 0.2f), fw_deep(FW_CROW, 0.85f), fw_deep(FW_CROW, 0.85f), fw_deep(FW_CROW, 0.2f));
        for (int s = 1; s <= 2; s++) {
            float p = FU.pts[s].pulse(now);
            int lum = (int)(200 + 55 * p);
            float hh = s == FW_DMOB ? 250.0f : 300.0f;
            fw_render(dl, f, s, s == FW_DMOB ? 80 : 560, 70 + hh, hh, IM_COL32(lum, lum, lum, 255));
        }
        dl->PopClipRect();
        text(dl, f, g_stencil, 18, 128, 74, fw_trim(FW_DMOB), "D-MOB'S TEAM", LEFT, 1.4f);
        text(dl, f, g_stencil, 18, 512, 74, fw_trim(FW_CROW), "CROW'S TEAM", RIGHT, 1.4f);
        char m1[48], m2[48];
        int n1 = (int)(FU.members[FW_DMOB].shown + 0.5), n2 = (int)(FU.members[FW_CROW].shown + 0.5);
        snprintf(m1, sizeof m1, "%d %s", n1, n1 == 1 ? "MEMBER" : "MEMBERS");
        snprintf(m2, sizeof m2, "%d %s", n2, n2 == 1 ? "MEMBER" : "MEMBERS");
        text(dl, f, g_title, 10, 128, 96, with_alpha(CREAM, 0.8f), m1, LEFT, 1.0f);
        text(dl, f, g_title, 10, 512, 96, with_alpha(CREAM, 0.8f), m2, RIGHT, 1.0f);
        fw_tug(dl, f, 128, 108, 384, 32, true);
        std::string sl = "SEASON " + fw_season_words(live.season);
        std::string e = fw_ends_words(live.ends, live.now);
        if (!e.empty()) sl += "  -  " + e;
        text(dl, f, g_title, 10, 320, 145, with_alpha(CREAM, 0.85f), sl.c_str(), CENTER, 1.0f);
        text(dl, f, g_title, 9, 320, 98, with_alpha(CREAM, 0.6f), "LIVE", CENTER, 0.8f);
        dl->AddCircleFilled(f.p(301, 103), 2.6f * f.s(), with_alpha(RED, 0.55f + 0.45f * sinf((now % 1200) / 1200.0f * 6.283f)));
    }
    // X: the board (tests: RECOMP_TEST_FACTION_BOARD_FILE=<path> opens it once that file exists)
    bool want_board = false;
    {
        static const char *bf = getenv("RECOMP_TEST_FACTION_BOARD_FILE");
        static ULONGLONG checked;
        static bool done;
        if (bf && *bf && !done && now - checked > 500) {
            checked = now;
            if (GetFileAttributesA(bf) != INVALID_FILE_ATTRIBUTES) { done = true; want_board = !FU.board; FN.board_at = 0; }
        }
    }
    if ((pad.x || want_board) && !searching) {
        FU.board = !FU.board;
        if (FU.board) fw_fetch_board();
        fprintf(stderr, "[FACTION] board %s\n", FU.board ? "open" : "closed");
    }
    if (FU.board) {
        FwBoard b = fw_board();
        glass(dl, f, 36, 170, 568, 214, 8);
        for (int s = 1; s <= 2; s++) {
            float x = s == FW_DMOB ? 48 : 330, w = 262;
            rect(dl, f, x, 178, w, 20, fw_main(s, 0.85f), 4);
            text(dl, f, g_title, 13, x + 8, 180, WHITE, fw_team(s), LEFT, 1.0f);
            text(dl, f, g_title, 13, x + w - 8, 180, fw_trim(s), (fw_num(b.pts[s]) + " PTS").c_str(), RIGHT, 1.0f);
            const auto &rows = b.rows[s];
            if (rows.empty()) text(dl, f, g_hand, 12, x + w / 2, 240, ORANGE, b.valid ? "Nobody has scored yet." : "Loading the board...", CENTER, 0.8f);
            for (size_t i = 0; i < rows.size() && i < 8; i++) {
                const FwRow &r = rows[i];
                float y = 204 + i * 19.0f;
                bool mine_row = b.me && b.me_faction == s && b.me_place == r.place;
                if (mine_row) rect(dl, f, x, y - 1, w, 18, IM_COL32(255, 255, 255, 30), 3);
                if (r.place == 1) crown(dl, f, x + 12, y + 7, 14, GOLD);
                else text(dl, f, g_title, 12, x + 12, y + 1, CREAM, std::to_string(r.place).c_str(), CENTER, 0.8f);
                text(dl, f, g_body, 12, x + 26, y + 1, mine_row ? fw_trim(s) : WHITE, r.name.c_str(), LEFT, 0.8f, 150);
                char rec[32];
                snprintf(rec, sizeof rec, "%d-%d", r.wins, r.losses);      // the numbers font: its 0 is no O
                text(dl, f, g_numbers ? g_numbers : g_body, 12, x + 196, y + 1, GREY, rec, RIGHT, 0.6f);
                text(dl, f, g_title, 13, x + w - 6, y + 1, fw_trim(s), std::to_string(r.points).c_str(), RIGHT, 0.8f);
            }
        }
        std::string you = b.me && b.me_place > 0 ? "YOU: #" + std::to_string(b.me_place) + " OF " + fw_team(b.me_faction) + ", " +
                                                       std::to_string(b.me_points) + " PTS, " + std::to_string(b.me_wins) + "W " +
                                                       std::to_string(b.me_losses) + "L"
                                                 : "YOU ARE NOT ON THE BOARD YET: WIN A FACTION FIGHT";
        text(dl, f, g_title, 12, 320, 364, CREAM, you.c_str(), CENTER, 1.0f);
        float hx = 54;
        hx += hint(dl, f, hx, 420, "x", "MODES");
        hint(dl, f, hx, 420, "b", "BACK");
        if (pad.b) FU.board = false;
        return;
    }
    // the two modes
    if (!searching) {
        if (pad.left) FU.mode_sel = 0;
        if (pad.right) FU.mode_sel = 1;
    }
    for (int i = 0; i < 2; i++) {
        float x = i == 0 ? 36 : 324, y = 170, w = 280, h = 128;
        bool sel = FU.mode_sel == i;
        if (!searching && mouse_pick(f, x, y, w, h)) FU.mode_sel = i;
        glass(dl, f, x, y, w, h, 8, sel ? fw_deep(mine) : IM_COL32(10, 10, 13, 255), sel ? 0.85f : 0.62f);
        dl->PushClipRect(f.p(x, y), f.p(x + w, y + h), true);
        // who you meet: the other side's boss, faded at the right; your side's at the left
        int k = sel ? 235 : 130;
        float oh = mine == FW_DMOB ? 300.0f : 250.0f, mh = mine == FW_DMOB ? 250.0f : 300.0f;     // heads near the top
        if (i == 1) fw_render(dl, f, fw_other(mine), x + w - 90, y + 14 + oh, oh, IM_COL32(k / 2, k / 2, k / 2, 255));
        fw_render(dl, f, fw_other(mine), x + w - 46, y + 4 + oh, oh, IM_COL32(k, k, k, 255));
        if (i == 1) fw_render(dl, f, mine, x + 84, y + 14 + mh, mh, IM_COL32(k / 2, k / 2, k / 2, 255));
        fw_render(dl, f, mine, x + 44, y + 4 + mh, mh, IM_COL32(k * 3 / 4, k * 3 / 4, k * 3 / 4, 255));
        dl->AddRectFilledMultiColor(f.p(x, y + h * 0.35f), f.p(x + w, y + h), IM_COL32(8, 8, 10, 0), IM_COL32(8, 8, 10, 0),
                                    IM_COL32(8, 8, 10, 235), IM_COL32(8, 8, 10, 235));
        dl->PopClipRect();
        text(dl, f, g_stencil, 30, x + w / 2, y + 60, sel ? CREAM : with_alpha(CREAM, 0.7f), i == 0 ? "1 ON 1" : "2 ON 2", CENTER, 1.8f);
        const char *sub = i == 0 ? (mine == FW_DMOB ? "YOU AGAINST ONE OF CROW'S TEAM" : "YOU AGAINST ONE OF D-MOB'S TEAM")
                                 : (mine == FW_DMOB ? "YOU AND A D-MOB PARTNER AGAINST TWO OF CROW'S" : "YOU AND A CROW PARTNER AGAINST TWO OF D-MOB'S");
        text(dl, f, g_hand, std::max(8.0f, fit_size(f, g_hand, 11, sub, w - 20)), x + w / 2, y + 100, sel ? ORANGE : with_alpha(ORANGE, 0.6f), sub, CENTER, 0.9f);
        int n = i == 0 ? live.searching[FW_DMOB] + live.searching[FW_CROW] : -1;
        if (n > 0) hub_live(dl, f, x + 8, y + 8, std::to_string(n) + " SEARCHING", true);
        if (sel) selector(dl, f, x, y, w, h);
        if (!searching && mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    // your line
    {
        glass(dl, f, 36, 306, 568, 78, 8);
        rect(dl, f, 36, 306, 6, 78, fw_main(mine), 3);
        std::string head = me.faction ? std::string("YOU RIDE WITH ") + fw_team(me.faction) : "PICK A SIDE FIRST";
        text(dl, f, g_title, 17, 54, 314, fw_trim(mine), head.c_str(), LEFT, 1.2f);
        char line[160];
        if (me.place > 0)
            snprintf(line, sizeof line, "%d POINTS  -  #%d OF %d IN %s  -  %d WINS  %d LOSSES", me.points, me.place, me.of, fw_team(mine),
                     me.wins, me.losses);
        else
            snprintf(line, sizeof line, "%d POINTS THIS SEASON  -  %d WINS  %d LOSSES", me.points, me.wins, me.losses);
        text(dl, f, g_title, 13, 54, 338, CREAM, line, LEFT, 1.0f);
        if (live.crew_rule && me.faction) {     // build 112: crew-only season: the variety rule's progress
            int total = me.crew_total ? me.crew_total : live.crew_total[me.faction];
            char crew[64];
            snprintf(crew, sizeof crew, "%d OF %d CREW", me.crew_progress, total);
            float cw = fw_crew_plate(dl, f, 590, 312, me.faction, 10, true);
            text(dl, f, g_title, 15, 590 - cw - 8, 313, fw_trim(mine), crew, RIGHT, 1.0f);
            char rule[200];
            snprintf(rule, sizeof rule, "Your crew only. A new fighter scores +1 extra (%d so far); one you scored with rests %d fights. "
                     "Every fighter of the crew: the title Whole Crew.", me.crew_bonus, live.crew_lock);
            text(dl, f, g_hand, 11, 54, 358, with_alpha(CREAM, 0.75f), rule, LEFT, 0.8f, 540);
        } else
        text(dl, f, g_hand, 11, 54, 358, with_alpha(CREAM, 0.75f),
             "A win gives your side 3 points, a loss 1. The side with more points at the season's end wins: its fighters get a card frame and title.",
             LEFT, 0.8f, 540);
    }
    // the search
    if (searching || q.state == "refused") {
        float x = 120, y = 186, w = 400, h = 100;
        rect(dl, f, 36, 170, 568, 128, IM_COL32(0, 0, 0, 150), 8);
        glass(dl, f, x, y, w, h, 10, fw_deep(mine), 0.92f);
        frame_rect(dl, f, x, y, w, h, fw_trim(mine, 0.7f), 1.5f);
        const char *words = fw_queue_words(q, mine);
        text(dl, f, g_title, std::min(17.0f, fit_size(f, g_title, 17, words, w - 30)), x + w / 2, y + 14, CREAM, words, CENTER, 1.2f);
        int secs = (int)((now - FN.q_since) / 1000);
        char line[96];
        snprintf(line, sizeof line, "%s  -  %d:%02d", FN.q_size == 2 ? "2 ON 2" : "1 ON 1", secs / 60, secs % 60);
        text(dl, f, g_title, 13, x + w / 2, y + 42, ORANGE, line, CENTER, 1.0f);
        if (q.state == "searching") {
            snprintf(line, sizeof line, "%d OF D-MOB'S TEAM AND %d OF CROW'S TEAM SEARCHING", q.dmob, q.crow);
            text(dl, f, g_title, 10, x + w / 2, y + 64, with_alpha(CREAM, 0.75f), line, CENTER, 0.8f);
        } else if (q.state == "refused" && !q.error.empty()) {
            text(dl, f, g_hand, 11, x + w / 2, y + 62, ORANGE, q.error.c_str(), CENTER, 0.8f, w - 20);
        }
        for (int i = 0; i < 8; i++) {      // a turning ring of dots
            float a = (now % 1000) / 1000.0f * 6.283f + i * 0.785f;
            dl->AddCircleFilled(f.p(x + 26 + cosf(a) * 9, y + 24 + sinf(a) * 9), (1.2f + i * 0.25f) * f.s(), with_alpha(fw_trim(mine), 0.3f + i * 0.09f));
        }
    }
    float hx = 54;
    if (searching) {
        hint(dl, f, hx, 420, "b", "CANCEL");
        if (pad.b) fw_queue_stop();
        return;
    }
    hx += hint(dl, f, hx, 420, "a", "SEARCH");
    hx += hint(dl, f, hx, 420, "x", "BOARD");
    hint(dl, f, hx, 420, "b", "BACK");
    if (pad.a) {
        if (fw_outdated()) say("A newer build is out: update in the launcher to play online.");
        else if (!me.faction) open_faction_pick(FACTIONS);
        else {
            FU.acted_match.clear();
            FU.acted_code.clear();
            fw_queue_start(FU.mode_sel == 1 ? 2 : 1);
        }
    }
    if (pad.b) { S.page = HUB; S.hub_sel = HUB_FACTION; }
}

// ---- the match's legs (every frame on the hub's thread, any page) ----

// Whether this PC sits in its faction match's room (the queue said ready, or its room code or slot is posted).
bool fw_seated()
{
    std::lock_guard<std::mutex> lock(FN.mu);
    return FN.q.state == "ready" || (!FN.q.match.empty() && FN.room_posted_for == FN.q.match && FN.room_post.empty());
}

std::mutex g_fw_tick;   // the screens and the tests' thread may both tick: one at a time

void fw_match_tick(const NetRoom &room)
{
    if (gallery_on()) return;
    std::unique_lock<std::mutex> once(g_fw_tick, std::try_to_lock);
    if (!once.owns_lock()) return;
    // the body's relay and pings (the service picks the host by them)
    static ULONGLONG net_at;
    ULONGLONG now = GetTickCount64();
    if (fw_searching() && now - net_at > 1000) {
        net_at = now;
        NetServerInfo sv[NETSERVER_COUNT];
        servers(sv);
        int cur = connected_server();
        std::string body = "\"relay\":\"" + std::string(cur >= 0 && sv[cur].id ? sv[cur].id : "") + "\",\"pings\":{";
        bool first = true;
        for (int i = 0; i < NETSERVER_COUNT; i++) {
            if (!sv[i].id || sv[i].state != NETSERVER_UP || sv[i].ping_ms < 0) continue;
            body += std::string(first ? "" : ",") + "\"" + sv[i].id + "\":" + std::to_string(sv[i].ping_ms);
            first = false;
        }
        body += "}";
        std::lock_guard<std::mutex> lock(FN.mu);
        FN.q_net = body;
    }
    {   // build 112: in the faction match's room the crew rule and this PC's side stay fresh (a season may turn)
        bool match_room;
        {
            std::lock_guard<std::mutex> lock(FN.mu);
            match_room = room.in_room && !FN.room_code.empty() && FN.room_code == room.code;
        }
        static int last_phase = -1;
        if (match_room) {
            bool waiting;
            {
                std::lock_guard<std::mutex> lock(FN.mu);
                waiting = !FN.pending.empty();
            }
            if (!FN.live_at || now - FN.live_at > 30000) fw_fetch_live();
            if (!FN.me_at || now - FN.me_at > (waiting ? 10000u : 60000u)) fw_fetch_me();
            int side = fw_crew_side(room);
            fw_crew_note(room, side);
            if (side && room.phase == 2 && last_phase != 2) {     // a fight starts: its character rests from now on
                for (int i = 0; i < room.player_count; i++)
                    if (room.players[i].slot == room.you) fw_crew_started(room.players[i].fighter);
            }
        }
        last_phase = match_room ? room.phase : -1;
    }
    FwQueue q = fw_queue();
    if (q.match.empty()) return;
    bool on = fw_searching() || q.state == "ready";
    if (!on) return;
    // the service called the match off (back to searching): out of the room it gave us
    if (q.state == "searching" && !FU.acted_match.empty()) {
        if (room.in_room && !FU.acted_code.empty() && !strcmp(room.code, FU.acted_code.c_str())) {
            fprintf(stderr, "[FACTION] match %s called off: leaving room %s\n", FU.acted_match.c_str(), room.code);
            netplay_lobby_leave();
        }
        FU.acted_match.clear();
        FU.acted_code.clear();
        return;
    }
    if (q.state == "host") {
        if (FU.acted_match != q.match && !room.in_room) {
            NetRoomSettings s;
            default_settings(s);
            snprintf(s.name, sizeof s.name, "FACTION WAR");
            s.is_public = 0;
            s.mode = q.mode == 1 ? 1 : 0;
            s.max = q.mode == 1 ? 4 : 2;
            netplay_lobby_create(&s);
            FU.acted_match = q.match;
            FU.acted_code.clear();
            fprintf(stderr, "[FACTION] match %s: host, opening a %s room\n", q.match.c_str(), q.mode == 1 ? "Team" : "One on One");
        } else if (FU.acted_match == q.match && room.in_room && room.code[0] && FN.room_posted_for != q.match) {
            NetServerInfo sv[NETSERVER_COUNT];
            servers(sv);
            int cur = connected_server();
            FU.acted_code = room.code;
            std::string body = "{\"match\":\"" + q.match + "\",\"code\":\"" + room.code + "\",\"relay\":\"" +
                               (cur >= 0 && sv[cur].id ? sv[cur].id : "") + "\",\"slot\":" + std::to_string(room.you) + "}";
            std::lock_guard<std::mutex> lock(FN.mu);
            FN.room_post = body;
            FN.room_posted_for = q.match;
            FN.room_code = room.code;
            fprintf(stderr, "[FACTION] match %s: room %s open (slot %d)\n", q.match.c_str(), room.code, room.you);
        }
    } else if (q.state == "join" && !q.code.empty()) {
        if (FU.acted_match != q.match) {
            NetServerInfo sv[NETSERVER_COUNT];
            servers(sv);
            int at = -1;
            for (int i = 0; i < NETSERVER_COUNT; i++) if (sv[i].id && q.relay == sv[i].id) at = i;
            if (at < 0) at = connected_server();
            FU.acted_match = q.match;
            FU.acted_code = q.code;
            netservers_join_on(at, q.code.c_str(), q.mode == 1 ? 1 : 0, hub_name().c_str());
            fprintf(stderr, "[FACTION] match %s: joining %s on %s\n", q.match.c_str(), q.code.c_str(), at >= 0 ? sv[at].id : "?");
        } else if (room.in_room && !strcmp(room.code, q.code.c_str()) && FN.room_posted_for != q.match) {
            std::string body = "{\"match\":\"" + q.match + "\",\"code\":\"" + q.code + "\",\"relay\":\"" + q.relay +
                               "\",\"slot\":" + std::to_string(room.you) + "}";
            std::lock_guard<std::mutex> lock(FN.mu);
            FN.room_post = body;
            FN.room_posted_for = q.match;
            FN.room_code = q.code;
            fprintf(stderr, "[FACTION] match %s: in room %s (slot %d)\n", q.match.c_str(), room.code, room.you);
        }
    }
}

// ---- build 112: crew-only fighters (faction_crew.h; owner-approved 2026-11-25, from the December season) ----

// This PC's side when `r` is its faction match's room and the season is crew-only (the service's crew_rule=1 with
// both lists); FW_NONE anywhere else, in the November season, or before the service answered.
int fw_crew_side(const NetRoom &r)
{
    if (gallery_on() || !r.in_room || !r.code[0] || r.you == NETPLAY_SLOT_SPECTATOR) return FW_NONE;
    std::lock_guard<std::mutex> lock(FN.mu);
    if (!FN.live.valid || !FN.live.crew_rule || !FN.me.valid || FN.room_code != r.code) return FW_NONE;
    int side = FN.me.faction;
    if ((side != FW_DMOB && side != FW_CROW) || FN.live.crew[side].empty()) return FW_NONE;
    return side;
}

// The character a fighter plays (the service's crew_versions: D-Mob 83 and 84 are D-Mob 59). Called with FN.mu held.
int fw_character_locked(int id)
{
    auto it = FN.live.crew_versions.find(id);
    return it != FN.live.crew_versions.end() ? it->second : id;
}

// A fight in the match room started with this PC on `fighter`: its character rests (until /me shows it).
void fw_crew_started(int fighter)
{
    std::lock_guard<std::mutex> lock(FN.mu);
    FN.pending.push_back({ fw_character_locked(fighter), FN.me.fights, GetTickCount64() });
    fprintf(stderr, "[FACTION] crew fight started on fighter %d (character %d): it rests %d fights\n", fighter,
            FN.pending.back().character, FN.live.crew_lock);
}

// How many more scored fights fighter `id` stays locked for this player (0 free): the service's locks (crew_locked),
// moved on by the fights started here that it has not counted yet, and those fights' own characters.
int fw_crew_locked_left(int id)
{
    if (gallery_on()) return 0;
    std::lock_guard<std::mutex> lock(FN.mu);
    int n = (int)FN.pending.size(), left = 0, ch = fw_character_locked(id);
    auto it = FN.me.crew_locked.find(id);
    if (it != FN.me.crew_locked.end()) left = it->second - n;
    for (int k = 0; k < n; k++)
        if (FN.pending[k].character == ch) left = std::max(left, FN.live.crew_lock - (n - 1 - k));
    return std::max(0, left);
}

// Whether `side`'s crew has fighter `id` (the service's list; the game carries none of its own). FW_NONE: any fighter.
bool fw_crew_has(int side, int id)
{
    if (side != FW_DMOB && side != FW_CROW) return true;
    std::lock_guard<std::mutex> lock(FN.mu);
    const std::vector<int> &c = FN.live.crew[side];
    return faction_crew_has(c.data(), (int)c.size(), id) != 0;
}

// The crew line's plate (the select's and the room's): the side's colours; returns its width. right: x is its right edge.
float fw_crew_plate(ImDrawList *dl, const Frame &f, float x, float y, int side, float size = 11, bool right = false)
{
    const char *w = faction_crew_line(side);
    float tw = text_width(f, g_title, size, w) + 14, h = size + 7;
    if (right) x -= tw;
    rect(dl, f, x, y, tw, h, fw_main(side, 0.92f), 4);
    dl->AddRect(f.p(x, y), f.p(x + tw, y + h), fw_trim(side, 0.9f), 4 * f.s(), 0, 1.0f * f.s());
    text(dl, f, g_title, size, x + tw / 2, y + 3, side == FW_DMOB ? fw_trim(side) : WHITE, w, CENTER, 0.6f);
    return tw;
}

// Once per room: what the select offers there ([FACTION] line for the tests and the logs).
void fw_crew_note(const NetRoom &r, int side)
{
    static std::mutex mu;
    static std::string told;
    std::lock_guard<std::mutex> lock(mu);
    if (!side || told == r.code) return;
    told = r.code;
    const OnlineFighter *all = nullptr;
    int n = online_fighters(&all), offered = 0;
    for (int i = 0; i < n; i++) offered += fw_crew_has(side, all[i].id) ? 1 : 0;
    fprintf(stderr, "[FACTION] crew-only room %s: %s's crew, %d fighters in CHOOSE FIGHTER (no community, created or other "
                    "crew's fighters)\n", r.code, side == FW_DMOB ? "D-Mob" : "Crow", offered);
}

// ---- the hub: the tile, and the picker the first time ----

void hub_faction_tile(ImDrawList *dl, const Frame &f, const HubRect &r, bool sel)
{
    FwLive live = fw_live();
    fw_live_step(live);
    float x = r.x, y = r.y, w = r.w, h = r.h;
    dl->AddRectFilledMultiColor(f.p(x, y), f.p(x + w / 2, y + h), fw_deep(FW_DMOB), fw_main(FW_DMOB, 0.5f), fw_main(FW_DMOB, 0.5f), fw_deep(FW_DMOB));
    dl->AddRectFilledMultiColor(f.p(x + w / 2, y), f.p(x + w, y + h), fw_main(FW_CROW, 0.5f), fw_deep(FW_CROW), fw_deep(FW_CROW), fw_main(FW_CROW, 0.5f));
    int k = sel ? 255 : 165;
    fw_render(dl, f, FW_DMOB, x + w * 0.24f, y + 6 + h * 2.2f, h * 2.2f, IM_COL32(k, k, k, 255));
    fw_render(dl, f, FW_CROW, x + w * 0.76f, y + 2 + h * 2.6f, h * 2.6f, IM_COL32(k, k, k, 255));
    text(dl, f, g_stencil, 16, x + w / 2, y + 6, with_alpha(WHITE, sel ? 0.95f : 0.6f), "VS", CENTER, 1.4f);
}

std::string hub_faction_live()
{
    FwLive l = fw_live();
    if (!l.valid) return "";
    return "D-MOB " + fw_num((long long)(FU.pts[FW_DMOB].shown + 0.5)) + "  -  " + fw_num((long long)(FU.pts[FW_CROW].shown + 0.5)) + " CROW";
}

// The hub opens the picker by itself: signed in, the service on, no side yet (or a new season: once, remembered).
void fw_hub_auto()
{
    if (gallery_on() || FU.auto_done || acc_state() != ACCOUNT_IN || !faction_shown()) return;
    FwMe m = fw_me();
    if (!m.valid) return;
    FU.auto_done = true;
    FwLive l = fw_live();
    if (m.faction == FW_NONE) { open_faction_pick(HUB); return; }
    if (m.can_pick && !l.season.empty() && setting_text("faction_seen", "") != l.season) {
        recomp_settings_set_text("online", "faction_seen", l.season.c_str());
        open_faction_pick(HUB);
    }
}

// The room card's side: a small plate in the side's colours under HOST.
void fw_pill(ImDrawList *dl, const Frame &f, float x, float y, const std::string &faction)
{
    int fac = fw_of(faction);
    if (!fac) return;
    const char *w = fw_boss(fac);
    float tw = text_width(f, g_title, 9, w) + 10;
    rect(dl, f, x, y, tw, 13, fw_main(fac, 0.92f), 3);
    dl->AddRect(f.p(x, y), f.p(x + tw, y + 13), fw_trim(fac, 0.9f), 3 * f.s(), 0, 1.0f * f.s());
    text(dl, f, g_title, 9, x + tw / 2, y + 1.5f, fac == FW_DMOB ? fw_trim(fac) : WHITE, w, CENTER, 0.6f);
}

const char *faction_shot_label()
{
    if (S.page == FACTION_PICK) return FU.slam ? "faction-picked" : FU.confirm ? "faction-confirm" : "faction-pick";
    if (S.page == FACTIONS) return FU.board ? "faction-board" : fw_searching() ? "faction-search" : "faction-war";
    if (!gallery_on()) {     // build 112: the crew-only select and room
        NetRoom r;
        lobby_room(&r);
        if (r.phase == 0 && fw_crew_side(r)) {
            if (S.page != FIGHTERS) return "faction-crew-room";
            bool locked = false;
            for (int id : fw_live().crew[fw_crew_side(r)]) locked = locked || fw_crew_locked_left(id) > 0;
            return locked ? "faction-crew-locked" : "faction-crew-fighters";
        }
    }
    return nullptr;
}
