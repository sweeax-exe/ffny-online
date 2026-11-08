// TURF WAR (the Social Update, stage 4): the crews' map of New York's venues and the season's standings.
// Included into online_screen.cpp's anonymous namespace after crew_ui.inc (it reads the social view C.v).
//
//   MAP      the 18 venues by borough: who holds each (or OPEN), since when, protected until, an open
//            challenge and its deadline; crew officers CHALLENGE (an owned venue), CLAIM (an open one, against
//            a crew picked from the crews this PC knows), SET UP BATTLE and CANCEL on their crew's challenges.
//   SEASON   the season's standings (points, venues held) and the past seasons' top crews.
//
// The game only shows what the account service answers and sends the requests; the service decides every
// rule (who may challenge, deadlines, points) and says why not in the map's why_not field.
//   GET  /api/turf?format=text            the map (with this account's session: my_crew, can_challenge, why_not);
//                                         404 while Turf War is off: the hub hides TURF WAR
//   GET  /api/turf/season?format=text     the standings and the past winners
//   POST /api/turf/challenge, /api/turf/challenges/{id}/battle, /api/turf/challenges/{id}/cancel
// The battle itself is an ordinary crew battle (crew_ui.inc), joined from the crew page.
//
// Fetched on a thread of their own (account_fetch / account_call block); the page reads the last answer.
// RECOMP_TEST_TURF=<file>: both GETs answer that file's text instead (no server; the POSTs are not sent).

struct TurfVenue {
    int id = 0, emblem = 0, defended = 0, can = -1;     // can: -1 not said (signed out), 0 no, 1 yes
    bool boss = false;
    long long owner = 0, since = 0, protect = 0, chal = 0, chal_crew = 0, deadline = 0, chal_battle = 0, claim_battle = 0;
    std::string name, borough, otag, oname, ctag, cname, why;
};
// A turf battle under way (live=): the crews (the challenger first), its room on a relay. The score, the fight
// it is on and who watches only when the line carries them (-1 / 0: not said).
struct TurfLive {
    int venue = 0, format = 0, score[2] = { -1, -1 }, fight = 0, fights = 0, watching = -1;
    long long battle = 0, crew[2] = {}, started = 0;
    std::string tag[2], name[2], relay, room;
};
// A change of the map (event=), for the street wire.
struct TurfEv {
    long long seq = 0, at = 0, crew = 0, other = 0;
    int venue = 0;
    std::string kind, vname, ctag, cname, otag, oname, text;
};
struct TurfMap {
    bool valid = false, officer = false;
    int season = 0, members = 0, online = 0;
    std::string month, season_name;
    long long ends = 0, now = 0, my_crew = 0, started = 0;
    std::vector<TurfVenue> venues;
    std::map<long long, int> tints;     // crew -> colour (tint=)
    std::vector<TurfLive> live;
    std::vector<TurfEv> events;         // newest first
    unsigned long long got = 0;     // GetTickCount64 at the answer (now moves on from there)
};
struct TurfCrew { int place = 0, emblem = 0, points = 0, venues = 0, rating = 0; long long id = 0; std::string tag, name; };
struct TurfPast { int season = 0, place = 0, points = 0, venues = 0; long long id = 0, ended = 0; std::string month, tag, name; };
struct TurfSeason {
    bool valid = false;
    int season = 0, total = 0;
    std::string month;
    long long ends = 0;
    std::vector<TurfCrew> crews;
    std::vector<TurfPast> past;
};

struct TurfState {
    std::mutex mu;                  // map, season, map_status, note
    TurfMap map;
    TurfSeason season;
    int map_status = 0;             // the last map answer: 0 not asked yet, 200, 404 (off), other a failure
    std::string note;               // a finished request's answer, said on the page's next frame
    bool note_ok = false;
    std::atomic<int> map_busy{0}, season_busy{0}, post_busy{0};
    unsigned long long map_at = 0, season_at = 0;
    // the page
    int tab = 0, season_top = 0;
    int sel_id = 6;                 // the map: the selected venue's feID
    int region = 0;                 // 0 all five boroughs, 1..5 the Bronx, Manhattan, Queens, Brooklyn, Staten Island
    bool details = false;           // the selected venue's details and actions over the right column
    float cam[5] = { 800, 480, 1, 800, 480 };      // the camera: centre x, y, zoom, where the centre sits x, y
    bool cam_snap = true;           // the next frame takes the camera's goal at once
    bool picker = false;            // CLAIM: the crew picker is up
    int picker_sel = 0, picker_frame = -1;     // the frame it opened on takes no input (its A opened it)
    std::string armed;              // the action pressed once ("challenge:6"), sent on the second press
    unsigned long long armed_at = 0;
    Page back = HUB;
    bool gallery_entry = false;     // the gallery's hub shows TURF WAR
} TW;

const char *turf_test_file()
{
    const char *e = getenv("RECOMP_TEST_TURF");
    return e && *e ? e : nullptr;
}

bool turf_read_file(std::string &out)
{
    out.clear();
    const char *path = turf_test_file();
    FILE *fp = path ? fopen(path, "rb") : nullptr;
    if (!fp) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
    fclose(fp);
    return true;
}

// key=value lines into (key, value) pairs ('\r' dropped)
std::vector<std::pair<std::string, std::string>> turf_lines(const char *text)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        std::string l = end ? std::string(line, end - line) : std::string(line);
        line = end ? end + 1 : nullptr;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t eq = l.find('=');
        if (eq != std::string::npos) out.push_back({ l.substr(0, eq), l.substr(eq + 1) });
    }
    return out;
}

long long turf_ll(const std::string &s) { return atoll(s.c_str()); }

// The map's text form: season=, month=, ends=, now=, my_crew=id|officer[|members|online], rules=, then a venue=
// line per venue (fields only ever appended; can_challenge and why_not only with a session), then the map
// page's lines: started=, season_name=, tint=crew|colour, live= (a turf battle under way) and event= (the
// street wire, newest first). False without venues.
bool parse_turf_map(const char *text, TurfMap &m)
{
    m = TurfMap();
    for (auto &kv : turf_lines(text)) {
        const std::string &k = kv.first, &v = kv.second;
        if (k == "season") m.season = atoi(v.c_str());
        else if (k == "month") m.month = v;
        else if (k == "ends") m.ends = turf_ll(v);
        else if (k == "now") m.now = turf_ll(v);
        else if (k == "my_crew") {
            auto f = split_bar(v);
            m.my_crew = turf_ll(f[0]);
            m.officer = f.size() > 1 && f[1] == "1";
            if (f.size() > 3) { m.members = atoi(f[2].c_str()); m.online = atoi(f[3].c_str()); }
        } else if (k == "started") m.started = turf_ll(v);
        else if (k == "season_name") m.season_name = v;
        else if (k == "tint") {
            auto f = split_bar(v);
            if (f.size() >= 2) m.tints[turf_ll(f[0])] = atoi(f[1].c_str());
        } else if (k == "live") {
            auto f = split_bar(v);
            if (f.size() < 12) continue;
            TurfLive l;
            l.venue = atoi(f[0].c_str()); l.battle = turf_ll(f[1]);
            l.crew[0] = turf_ll(f[2]); l.tag[0] = f[3]; l.name[0] = f[4];
            l.crew[1] = turf_ll(f[5]); l.tag[1] = f[6]; l.name[1] = f[7];
            l.format = atoi(f[8].c_str()); l.started = turf_ll(f[9]); l.relay = f[10]; l.room = f[11];
            if (f.size() > 13 && !f[12].empty()) { l.score[0] = atoi(f[12].c_str()); l.score[1] = atoi(f[13].c_str()); }
            if (f.size() > 15) { l.fight = atoi(f[14].c_str()); l.fights = atoi(f[15].c_str()); }
            if (f.size() > 16 && !f[16].empty()) l.watching = atoi(f[16].c_str());
            m.live.push_back(l);
        } else if (k == "event") {
            auto f = split_bar(v);
            if (f.size() < 11) continue;
            TurfEv e;
            e.seq = turf_ll(f[0]); e.kind = f[1]; e.at = turf_ll(f[2]); e.venue = atoi(f[3].c_str()); e.vname = f[4];
            e.crew = turf_ll(f[5]); e.ctag = f[6]; e.cname = f[7]; e.other = turf_ll(f[8]); e.otag = f[9]; e.oname = f[10];
            if (f.size() > 11) e.text = f[11];
            m.events.push_back(e);
        } else if (k == "venue") {
            auto f = split_bar(v);
            if (f.size() < 18) continue;
            TurfVenue t;
            t.id = atoi(f[0].c_str());
            t.name = f[1];
            t.borough = f[2];
            t.boss = f[3] == "1";
            t.owner = turf_ll(f[4]);
            t.otag = f[5];
            t.oname = f[6];
            t.emblem = atoi(f[7].c_str());
            t.since = turf_ll(f[8]);
            t.protect = turf_ll(f[9]);
            t.defended = atoi(f[10].c_str());
            t.chal = turf_ll(f[11]);
            t.chal_crew = turf_ll(f[12]);
            t.ctag = f[13];
            t.cname = f[14];
            t.deadline = turf_ll(f[15]);
            t.chal_battle = turf_ll(f[16]);
            t.claim_battle = turf_ll(f[17]);
            if (f.size() > 18 && !f[18].empty()) t.can = f[18] == "1" ? 1 : 0;
            if (f.size() > 19) t.why = f[19];
            m.venues.push_back(t);
        }
    }
    m.valid = !m.venues.empty();
    return m.valid;
}

// The season's text form: season=, month=, ends=, total=, crew= (place|id|tag|name|emblem|points|venues|
// rating) and past= (season|month|ended|place|id|tag|name|points|venues) lines.
bool parse_turf_season(const char *text, TurfSeason &s)
{
    s = TurfSeason();
    bool any = false;
    for (auto &kv : turf_lines(text)) {
        const std::string &k = kv.first, &v = kv.second;
        if (k == "season") { s.season = atoi(v.c_str()); any = true; }
        else if (k == "month") s.month = v;
        else if (k == "ends") s.ends = turf_ll(v);
        else if (k == "total") s.total = atoi(v.c_str());
        else if (k == "crew") {
            auto f = split_bar(v);
            if (f.size() < 7) continue;
            TurfCrew c;
            c.place = atoi(f[0].c_str()); c.id = turf_ll(f[1]); c.tag = f[2]; c.name = f[3]; c.emblem = atoi(f[4].c_str());
            c.points = atoi(f[5].c_str()); c.venues = atoi(f[6].c_str()); c.rating = f.size() > 7 ? atoi(f[7].c_str()) : 0;
            s.crews.push_back(c);
            any = true;
        } else if (k == "past") {
            auto f = split_bar(v);
            if (f.size() < 8) continue;
            TurfPast p;
            p.season = atoi(f[0].c_str()); p.month = f[1]; p.ended = turf_ll(f[2]); p.place = atoi(f[3].c_str());
            p.id = turf_ll(f[4]); p.tag = f[5]; p.name = f[6]; p.points = atoi(f[7].c_str());
            p.venues = f.size() > 8 ? atoi(f[8].c_str()) : 0;
            s.past.push_back(p);
            any = true;
        }
    }
    s.valid = any;
    return any;
}

// A GET: with this account's session when signed in (the map then says what this crew may do), else public.
int turf_get(const char *path, std::vector<char> &buf, int *len)
{
    *len = 0;
    if (turf_test_file()) {
        std::string t;
        if (!turf_read_file(t)) return 0;
        size_t n = std::min(t.size(), buf.size() - 1);
        memcpy(buf.data(), t.data(), n);
        buf[n] = 0;
        *len = (int)n;
        return 200;
    }
    if (account_state() == ACCOUNT_IN) {
        int status = account_call("GET", path, nullptr, buf.data(), (int)buf.size(), 0, nullptr);
        if (status > 0) {
            buf[buf.size() - 1] = 0;
            *len = (int)strlen(buf.data());
            return status;
        }
    }
    int status = account_fetch(path, buf.data(), (int)buf.size(), len);
    buf[std::max(0, std::min(*len, (int)buf.size() - 1))] = 0;
    return status;
}

void turf_fetch_map()
{
    if (gallery_on() || TW.map_busy.exchange(1)) return;
    TW.map_at = GetTickCount64();
    std::thread([]() {
        std::vector<char> buf(32768);
        int len = 0, status = turf_get("/api/turf?format=text", buf, &len);
        TurfMap m;
        bool ok = status == 200 && len > 0 && parse_turf_map(buf.data(), m);
        int before;
        {
            std::lock_guard<std::mutex> lock(TW.mu);
            before = TW.map_status;
            if (ok) { m.got = GetTickCount64(); TW.map = m; }
            else if (status == 404) TW.map = TurfMap();     // off: nothing to show
            TW.map_status = ok ? 200 : status == 200 ? -2 : status;
        }
        if (ok) fprintf(stderr, "[ONLINE] turf map: season %d (%s), %d venues, my crew %lld%s\n", m.season, m.month.c_str(),
                        (int)m.venues.size(), m.my_crew, m.officer ? " (officer)" : "");
        else if (before != status) fprintf(stderr, "[ONLINE] turf map: %s (HTTP %d)\n", status == 404 ? "off" : "not loaded", status);
        TW.map_busy = 0;
    }).detach();
}

void turf_fetch_season()
{
    if (gallery_on() || TW.season_busy.exchange(1)) return;
    TW.season_at = GetTickCount64();
    std::thread([]() {
        std::vector<char> buf(32768);
        int len = 0, status = turf_get("/api/turf/season?format=text", buf, &len);
        TurfSeason s;
        bool ok = status == 200 && len > 0 && parse_turf_season(buf.data(), s);
        {
            std::lock_guard<std::mutex> lock(TW.mu);
            if (ok) TW.season = s;
        }
        if (ok) fprintf(stderr, "[ONLINE] turf season %d: %d crews, %d past places\n", s.season, (int)s.crews.size(), (int)s.past.size());
        else fprintf(stderr, "[ONLINE] turf season: not loaded (HTTP %d)\n", status);
        TW.season_busy = 0;
    }).detach();
}

// The hub asks for the map now and then: TURF WAR shows while the service answers it (every minute while
// on; every five minutes while off or unreachable).
void turf_poll()
{
    if (gallery_on()) return;
    int status;
    {
        std::lock_guard<std::mutex> lock(TW.mu);
        status = TW.map_status;
    }
    unsigned long long now = GetTickCount64();
    unsigned long long every = status == 200 ? 60000ull : 300000ull;
    if (!TW.map_at || now - TW.map_at > every) turf_fetch_map();
}

bool turf_shown()
{
    if (gallery_on()) return TW.gallery_entry;
    std::lock_guard<std::mutex> lock(TW.mu);
    return TW.map_status == 200 && TW.map.valid;
}

void open_turf(Page back)
{
    S.page = TURF;
    TW.back = back;
    TW.tab = 0;
    TW.picker = false;
    TW.details = false;
    TW.region = 0;
    TW.cam_snap = true;
    TW.armed.clear();
    turf_fetch_map();
    turf_fetch_season();
}

// The service's {"error": "..."} as text ("" none).
std::string turf_error(const char *body)
{
    const char *k = body ? strstr(body, "\"error\"") : nullptr;
    if (!k) return "";
    const char *q = strchr(k + 7, '"');
    if (!q) return "";
    std::string out;
    for (const char *p = q + 1; *p && *p != '"'; p++) {
        if (*p == '\\' && p[1]) p++;
        out += *p;
    }
    if (!out.empty()) out[0] = (char)toupper((unsigned char)out[0]);
    return out;
}

// A POST on the service's turf routes; its answer comes back as TW.note, then the map (and the crew's
// battle, for the crew page) are asked again.
void turf_post(const std::string &path, const std::string &body, const std::string &done)
{
    if (gallery_on()) return;
    if (turf_test_file()) {
        fprintf(stderr, "[ONLINE] turf (test, not sent): POST %s %s\n", path.c_str(), body.c_str());
        std::lock_guard<std::mutex> lock(TW.mu);
        TW.note = "Test data: nothing was sent.";
        TW.note_ok = true;
        return;
    }
    if (TW.post_busy.exchange(1)) return;
    std::thread([path, body, done]() {
        std::vector<char> buf(8192);
        buf[0] = 0;
        int status = account_call("POST", path.c_str(), body.c_str(), buf.data(), (int)buf.size(), 0, nullptr);
        buf[buf.size() - 1] = 0;
        bool ok = status / 100 == 2;
        std::string note = ok ? done : status == -1 ? "Sign in with Discord first." : status == 0 ? "The service could not be reached right now." : "";
        if (note.empty()) {
            note = turf_error(buf.data());
            if (note.empty()) note = "Not possible right now (HTTP " + std::to_string(status) + ").";
        }
        fprintf(stderr, "[ONLINE] turf: POST %s %s -> HTTP %d%s%s\n", path.c_str(), body.c_str(), status, ok ? "" : ": ",
                ok ? "" : note.c_str());
        {
            std::lock_guard<std::mutex> lock(TW.mu);
            TW.note = note;
            TW.note_ok = ok;
        }
        TW.post_busy = 0;
        if (ok) social::reload();       // the crew page shows the battle
        TW.map_busy = 0;                // (a map fetch under way would answer from before the change)
        turf_fetch_map();
        turf_fetch_season();
    }).detach();
}

// ---- drawing ----

std::string turf_month(const std::string &m)
{
    static const char *const names[] = { "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST",
                                         "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER" };
    int y = 0, mo = 0;
    if (sscanf(m.c_str(), "%d-%d", &y, &mo) == 2 && mo >= 1 && mo <= 12) return std::string(names[mo - 1]) + " " + std::to_string(y);
    return upper(m.c_str());
}

std::string turf_borough(const std::string &b)
{
    if (b.empty()) return "AROUND THE CITY";
    std::string out = b == "bronx" ? "THE BRONX" : upper(b.c_str());
    for (char &c : out) if (c == '_') c = ' ';
    return out;
}

// "3 DAYS", "5 H", "12 MIN" (at least a minute)
std::string turf_span(long long secs)
{
    if (secs < 0) secs = 0;
    char buf[32];
    if (secs >= 2 * 86400) snprintf(buf, sizeof(buf), "%lld DAYS", secs / 86400);
    else if (secs >= 86400) snprintf(buf, sizeof(buf), "1 DAY %lld H", (secs % 86400) / 3600);
    else if (secs >= 3600) snprintf(buf, sizeof(buf), "%lld H %lld MIN", secs / 3600, (secs % 3600) / 60);
    else snprintf(buf, sizeof(buf), "%lld MIN", std::max(1ll, secs / 60));
    return buf;
}

long long turf_now(const TurfMap &m)
{
    if (gallery_on() || !m.got) return m.now;
    return m.now + (long long)((GetTickCount64() - m.got) / 1000);
}

// A crew's tag in a small box (the crew page's chip), in `col`; its width.
float turf_tag(ImDrawList *dl, const Frame &f, float x, float y, float size, const std::string &tag, ImU32 col, Align align = LEFT)
{
    float tw = text_width(f, g_title, size, tag.c_str()) + size * 0.9f;
    if (align == RIGHT) x -= tw;
    dl->AddRect(f.p(x, y), f.p(x + tw, y + size * 1.35f), with_alpha(col, 0.86f), 3 * f.s(), 0, 1.0f * f.s());
    text(dl, f, g_title, size, x + tw / 2, y + size * 0.12f, col, tag.c_str(), CENTER, 0);
    return tw;
}

const ImU32 TURF_BLUE = IM_COL32(118, 170, 255, 255);

// The header over both tabs: the season and its days left, this player's crew and its standing.
void turf_header(ImDrawList *dl, const Frame &f, const TurfMap &m, const TurfSeason &s, bool signed_in)
{
    glass(dl, f, 34, 76, 572, 42);
    std::string head = "SEASON " + std::to_string(m.season ? m.season : s.season) + "  -  " + turf_month(m.month.empty() ? s.month : m.month);
    text(dl, f, g_title, 16, 48, 80, ORANGE, head.c_str(), LEFT, 1.3f);
    long long ends = m.ends ? m.ends : s.ends, now = turf_now(m);
    if (ends && now) {
        long long left = ends - now;
        std::string l = left <= 0 ? "THE SEASON ENDS NOW" : left >= 2 * 86400 ? std::to_string(left / 86400) + " DAYS LEFT"
                                                                              : turf_span(left) + " LEFT";
        text(dl, f, g_title, 16, 594, 80, CREAM, l.c_str(), RIGHT, 1.3f);
    }
    std::string mine;
    if (!signed_in) mine = "Sign in with Discord to see your crew's turf.";
    else if (!m.my_crew) mine = "Not in a crew yet: join or start one in the chat's CREW tab to fight for turf.";
    if (!mine.empty()) {
        text(dl, f, g_body, 10, 48, 101, GREY, mine.c_str(), LEFT, 0);
        return;
    }
    const TurfCrew *c = nullptr;
    for (auto &e : s.crews) if (e.id == m.my_crew) c = &e;
    std::string tag, name;
    int held = 0;
    for (auto &v : m.venues) if (v.owner == m.my_crew) { held++; tag = v.otag; name = v.oname; }
    if (c) { tag = c->tag; name = c->name; held = c->venues; }
    if (tag.empty() && C.v.crew == m.my_crew) { tag = C.v.crew_tag; name = C.v.crew_name; }
    float x = 48;
    text(dl, f, g_title, 10, x, 102, GREY, "YOUR CREW", LEFT, 0.6f);
    x += text_width(f, g_title, 10, "YOUR CREW") + 8;
    if (!tag.empty()) x += turf_tag(dl, f, x, 100, 10, tag, GOLD) + 6;
    if (!name.empty()) text(dl, f, g_title, fit_size(f, g_title, 12, upper(name.c_str()).c_str(), 220), x, 100, CREAM,
                            upper(name.c_str()).c_str(), LEFT, 0.8f);
    char line[96];
    if (c && c->place > 0)
        snprintf(line, sizeof(line), "#%d  -  %d VENUE%s  -  %d POINTS", c->place, held, held == 1 ? "" : "S", c->points);
    else
        snprintf(line, sizeof(line), "%d VENUE%s  -  NO POINTS YET", held, held == 1 ? "" : "S");
    text(dl, f, g_title, 12, 594, 100, GOLD, line, RIGHT, 0.8f);
}

// What this player's crew may do at the venue: up to two actions, else a line saying why not.
struct TurfAction { std::string id, label, confirm; };
void turf_actions(const TurfMap &m, const TurfVenue &v, bool signed_in, std::vector<TurfAction> &acts, std::string &why)
{
    acts.clear();
    why.clear();
    if (!signed_in) { why = "Sign in with Discord to fight for turf."; return; }
    if (!m.my_crew) { why = "Join a crew to fight for turf."; return; }
    bool ours = v.owner && v.owner == m.my_crew, theirs_ours = v.chal && v.chal_crew == m.my_crew;
    if (!m.officer) {
        why = ours ? "Your crew holds this venue." : "Your Boss or a Lieutenant can challenge for turf.";
        if (v.chal && (ours || theirs_ours)) why = "Your Boss or a Lieutenant sets up the battle. Join it from the crew page.";
        return;
    }
    if (v.chal && (ours || theirs_ours)) {
        if (!v.chal_battle) acts.push_back({ "battle", "SET UP BATTLE", "AGAIN TO SET UP 3 V 3" });
        else why = "The battle is set up: accept and join it from the crew page.";
        if (theirs_ours) acts.push_back({ "cancel", "CANCEL CHALLENGE", "AGAIN TO CANCEL IT" });
        return;
    }
    if (ours) { why = "Your crew holds this venue."; return; }
    if (v.can == 1) {
        if (v.owner) acts.push_back({ "challenge", "CHALLENGE " + upper(v.otag.c_str()), "AGAIN TO CHALLENGE" });
        else acts.push_back({ "claim", "CLAIM: PICK A CREW", "" });
        return;
    }
    why = !v.why.empty() ? v.why : v.can == 0 ? "Not open to a challenge right now." : "";
    if (!why.empty()) why[0] = (char)toupper((unsigned char)why[0]);     // the service's reasons start lower-case
}

void turf_do(const TurfMap &m, const TurfVenue &v, const std::string &act)
{
    char body[96];
    if (act == "challenge") {
        snprintf(body, sizeof(body), "{\"venue\":%d}", v.id);
        turf_post("/api/turf/challenge", body, "Challenge sent: " + v.otag + " has to answer before the deadline.");
    } else if (act == "battle") {
        turf_post("/api/turf/challenges/" + std::to_string(v.chal) + "/battle", "{\"format\":3}",
                  "Battle set up: the other crew accepts it, then join it from the crew page.");
    } else if (act == "cancel") {
        turf_post("/api/turf/challenges/" + std::to_string(v.chal) + "/cancel", "{}", "Challenge called off.");
    } else if (act == "claim") {
        TW.picker = true;
        TW.picker_sel = 0;
        TW.picker_frame = ImGui::GetFrameCount();
        if (!C.v.ladder_loaded) social::ladder();
    }
    fprintf(stderr, "[ONLINE] turf: %s at venue %d (%s)\n", act.c_str(), v.id, v.name.c_str());
    (void)m;
}

// CLAIM's crews: the crews this PC knows of (the season's standings, the holders and challengers on the map,
// the crew ladder), without this player's own.
std::vector<TurfCrew> turf_picker_crews(const TurfMap &m, const TurfSeason &s)
{
    std::vector<TurfCrew> out;
    auto add = [&](long long id, const std::string &tag, const std::string &name, int rating) {
        if (!id || id == m.my_crew || tag.empty()) return;
        for (auto &c : out) if (c.id == id) return;
        TurfCrew c;
        c.id = id; c.tag = tag; c.name = name; c.rating = rating;
        out.push_back(c);
    };
    for (auto &c : s.crews) add(c.id, c.tag, c.name, c.rating);
    for (auto &v : m.venues) { add(v.owner, v.otag, v.oname, 0); add(v.chal_crew, v.ctag, v.cname, 0); }
    for (auto &r : C.v.ladder) add(r.id, r.tag, r.name, r.rating);
    return out;
}

void draw_turf_picker(ImDrawList *dl, const Frame &f, const Pad &pad_in, const TurfMap &m, const TurfSeason &s, const TurfVenue &v)
{
    std::vector<TurfCrew> crews = turf_picker_crews(m, s);
    Pad none = Pad();
    const Pad &pad = ImGui::GetFrameCount() == TW.picker_frame ? none : pad_in;
    float x = 150, y = 112, w = 340, h = 290;
    rect(dl, f, 0, 0, 640, 480, IM_COL32(0, 0, 0, 120));
    glass(dl, f, x, y, w, h, 12, IM_COL32(24, 12, 14, 255), 0.94f);
    text(dl, f, g_title, 10, x + w / 2, y + 12, GREY, "CLAIM AN OPEN VENUE", CENTER, 0);
    text(dl, f, g_title, fit_size(f, g_title, 20, upper(v.name.c_str()).c_str(), w - 30), x + w / 2, y + 26, CREAM,
         upper(v.name.c_str()).c_str(), CENTER, 1.2f);
    text(dl, f, g_body, 9.5f, x + w / 2, y + 54, GREY, "Pick the crew to fight for it: a 3 v 3 crew battle here. Their Boss or a "
         "Lieutenant accepts it.", CENTER, 0, w - 40);
    const int visible = 7;
    int n = (int)crews.size();
    if (pad.down && n) TW.picker_sel = std::min(n - 1, TW.picker_sel + 1);
    if (pad.up && n) TW.picker_sel = std::max(0, TW.picker_sel - 1);
    TW.picker_sel = std::max(0, std::min(TW.picker_sel, n - 1));
    int first = std::max(0, std::min(TW.picker_sel - visible / 2, n - visible));
    float ry = y + 86;
    if (!n) text(dl, f, g_title, 13, x + w / 2, ry + 50, GREY, "No other crews found yet.", CENTER, 0.8f);
    for (int i = first; i < n && i < first + visible; i++) {
        const TurfCrew &c = crews[i];
        bool on = i == TW.picker_sel;
        float yy = ry + (i - first) * 26;
        if (mouse_pick(f, x + 16, yy, w - 32, 24)) TW.picker_sel = i;
        rect(dl, f, x + 16, yy, w - 32, 24, on ? IM_COL32(200, 16, 46, 200) : IM_COL32(255, 255, 255, 16), 6);
        float tw = turf_tag(dl, f, x + 26, yy + 5, 10, c.tag, on ? WHITE : CREAM);
        text(dl, f, g_title, fit_size(f, g_title, 13, upper(c.name.c_str()).c_str(), w - 140), x + 34 + tw, yy + 4, on ? WHITE : CREAM,
             upper(c.name.c_str()).c_str(), LEFT, 0.8f);
        if (c.rating) text(dl, f, g_title, 11, x + w - 26, yy + 6, on ? WHITE : GREY, std::to_string(c.rating).c_str(), RIGHT, 0);
        if (mouse_click(f, x + 16, yy, w - 32, 24)) const_cast<Pad &>(pad).a = true;
    }
    float hx = x + 20;
    if (n) hx += hint(dl, f, hx, y + h - 24, "a", "CHALLENGE");
    hint(dl, f, hx, y + h - 24, "b", "BACK");
    if (pad.a && n) {
        const TurfCrew &c = crews[TW.picker_sel];
        char body[128];
        snprintf(body, sizeof(body), "{\"venue\":%d,\"crew\":%lld,\"format\":3}", v.id, c.id);
        turf_post("/api/turf/challenge", body, "Challenge sent to " + c.tag + ": once they accept, join it from the crew page.");
        fprintf(stderr, "[ONLINE] turf: claim venue %d against crew %lld (%s)\n", v.id, c.id, c.tag.c_str());
        TW.picker = false;
    }
    if (pad.b) TW.picker = false;
}

// The chosen venue on the right: holder, since, protection, the challenge, what this crew may do.
void turf_detail(ImDrawList *dl, const Frame &f, const Pad &pad, const TurfMap &m, const TurfVenue &v, bool signed_in)
{
    const float x = 346, y = 124, w = 260, h = 286;
    glass(dl, f, x, y, w, h);
    long long now = turf_now(m);
    std::string name = upper(v.name.c_str());
    text(dl, f, g_title, fit_size(f, g_title, 19, name.c_str(), w - 24), x + 12, y + 8, CREAM, name.c_str(), LEFT, 1.3f);
    std::string sub = turf_borough(v.borough);
    text(dl, f, g_hand, 13, x + 12, y + 32, ORANGE, sub.c_str(), LEFT, 0.8f);
    if (v.boss) {
        float bw = text_width(f, g_title, 10, "BOSS VENUE") + 12;
        rect(dl, f, x + w - 12 - bw, y + 33, bw, 16, IM_COL32(120, 90, 10, 220), 4);
        text(dl, f, g_title, 10, x + w - 12 - bw / 2, y + 35, GOLD, "BOSS VENUE", CENTER, 0);
    }
    float yy = y + 56;
    dl->AddLine(f.p(x + 10, yy), f.p(x + w - 10, yy), IM_COL32(255, 255, 255, 30), 1.0f);
    yy += 6;
    bool ours = v.owner && v.owner == m.my_crew;
    if (v.owner) {
        text(dl, f, g_title, 10, x + 12, yy, GREY, "HELD BY", LEFT, 0);
        float tw = turf_tag(dl, f, x + 12, yy + 15, 12, v.otag, ours ? GOLD : CREAM);
        std::string on = upper(v.oname.c_str());
        text(dl, f, g_title, fit_size(f, g_title, 15, on.c_str(), w - 34 - tw), x + 20 + tw, yy + 14, ours ? GOLD : WHITE, on.c_str(), LEFT, 1.0f);
        yy += 38;
        std::string held = v.since ? "HELD FOR " + turf_span(now - v.since) : "HELD";
        if (v.defended > 0) held += "  -  DEFENDED " + std::to_string(v.defended) + (v.defended == 1 ? " TIME" : " TIMES");
        text(dl, f, g_title, fit_size(f, g_title, 11, held.c_str(), w - 24), x + 12, yy, CREAM, held.c_str(), LEFT, 0.6f);
        yy += 18;
    } else {
        text(dl, f, g_title, 18, x + 12, yy + 2, GREY, "OPEN", LEFT, 1.0f);
        text(dl, f, g_body, 9.5f, x + 12, yy + 26, GREY, "No crew holds this venue.", LEFT, 0);
        yy += 44;
    }
    if (v.protect > now) {
        std::string p = "PROTECTED FOR " + turf_span(v.protect - now);
        text(dl, f, g_title, 11, x + 12, yy, TURF_BLUE, p.c_str(), LEFT, 0.6f);
        yy += 18;
    }
    if (v.chal) {       // the open challenge
        yy += 4;
        rect(dl, f, x + 8, yy, w - 16, 58, IM_COL32(120, 12, 26, 140), 6);
        text(dl, f, g_title, 10, x + 16, yy + 5, ORANGE, "CHALLENGED BY", LEFT, 0);
        float tw = turf_tag(dl, f, x + 16, yy + 19, 11, v.ctag, v.chal_crew == m.my_crew ? GOLD : CREAM);
        std::string cn = upper(v.cname.c_str());
        text(dl, f, g_title, fit_size(f, g_title, 13, cn.c_str(), w - 46 - tw), x + 24 + tw, yy + 18, WHITE, cn.c_str(), LEFT, 0.8f);
        std::string d = v.chal_battle ? "BATTLE SET UP" : v.deadline > now ? "BATTLE DUE IN " + turf_span(v.deadline - now) : "DEADLINE REACHED";
        if (v.chal_battle && v.deadline > now) d += "  -  " + turf_span(v.deadline - now) + " LEFT";
        text(dl, f, g_title, fit_size(f, g_title, 11, d.c_str(), w - 32), x + 16, yy + 40, CREAM, d.c_str(), LEFT, 0.6f);
        yy += 64;
    } else if (v.claim_battle) {
        yy += 4;
        rect(dl, f, x + 8, yy, w - 16, 24, IM_COL32(120, 12, 26, 140), 6);
        text(dl, f, g_title, 11, x + 16, yy + 5, ORANGE, "A BATTLE FOR IT IS UNDER WAY", LEFT, 0);
        yy += 30;
    }
    // what this crew may do: the buttons at the bottom, else why not
    std::vector<TurfAction> acts;
    std::string why;
    turf_actions(m, v, signed_in, acts, why);
    float by = y + h - 10 - (float)acts.size() * 30;
    if (!why.empty()) text(dl, f, g_body, 11, x + 12, yy + 8, with_alpha(CREAM, 0.8f), why.c_str(), LEFT, 0, w - 24);
    unsigned long long tick = GetTickCount64();
    if (!TW.armed.empty() && tick - TW.armed_at > 4000) TW.armed.clear();
    bool busy = TW.post_busy != 0;
    for (size_t i = 0; i < acts.size(); i++) {
        const TurfAction &a = acts[i];
        float bx = x + 12, bw = w - 24, yb = by + i * 30;
        std::string key = a.id + ":" + std::to_string(v.id);
        bool armed = TW.armed == key;
        bool click = mouse_click(f, bx, yb, bw, 26);
        rect(dl, f, bx, yb, bw, 26, armed ? IM_COL32(255, 168, 26, 220) : i == 0 ? IM_COL32(200, 16, 46, 220) : IM_COL32(255, 255, 255, 30), 6);
        // the button's glyph and its word, centred (A the first action, Y the second)
        std::string label = armed ? a.confirm : a.label;
        float lw = 20 + text_width(f, g_title, 13, label.c_str());
        hint(dl, f, bx + std::max(6.0f, (bw - lw) / 2), yb + 5, i == 0 ? "a" : "y", label.c_str());
        bool pressed = click || (i == 0 ? pad.a : pad.y);
        if (!pressed || busy || gallery_on()) continue;
        if (a.confirm.empty() || armed) { TW.armed.clear(); turf_do(m, v, a.id); }
        else { TW.armed = key; TW.armed_at = tick; }
    }
}

void draw_turf_season(ImDrawList *dl, const Frame &f, const Pad &pad, const TurfMap &m, const TurfSeason &s)
{
    // the standings
    const float x = 34, w = 384;
    glass(dl, f, x, 124, w, 286);
    text(dl, f, g_title, 9, x + 30, 130, GREY, "#", RIGHT, 0.5f);
    text(dl, f, g_title, 9, x + 42, 130, GREY, "CREW", LEFT, 0.5f);
    text(dl, f, g_title, 9, x + w - 96, 130, GREY, "VENUES", RIGHT, 0.5f);
    text(dl, f, g_title, 9, x + w - 16, 130, GREY, "POINTS", RIGHT, 0.5f);
    dl->AddLine(f.p(x + 10, 143), f.p(x + w - 10, 143), IM_COL32(255, 255, 255, 30), 1.0f);
    const int visible = 11;
    int n = (int)s.crews.size();
    if (pad.down) TW.season_top = std::min(std::max(0, n - visible), TW.season_top + 1);
    if (pad.up) TW.season_top = std::max(0, TW.season_top - 1);
    TW.season_top = std::max(0, std::min(TW.season_top, std::max(0, n - visible)));
    if (!s.valid) text(dl, f, g_title, 16, x + w / 2, 240, CREAM, "LOADING THE STANDINGS...", CENTER, 1.0f);
    else if (!n) {
        text(dl, f, g_title, 14, x + w / 2, 230, GREY, "No crew holds turf yet this season.", CENTER, 0.8f);
        text(dl, f, g_title, 11, x + w / 2, 252, GREY, "Held venues earn their crew points every day.", CENTER, 0.8f);
    }
    for (int i = TW.season_top; i < n && i < TW.season_top + visible; i++) {
        const TurfCrew &c = s.crews[i];
        float y = 148 + (i - TW.season_top) * 23;
        bool mine = m.my_crew && c.id == m.my_crew;
        if (mine) rect(dl, f, x + 8, y - 1, w - 16, 21, IM_COL32(255, 205, 60, 40), 4);
        ImU32 col = c.place == 1 ? GOLD : mine ? GOLD : WHITE;
        text(dl, f, g_title, 15, x + 30, y + 1, col, std::to_string(c.place).c_str(), RIGHT, 1.0f);
        float tw = turf_tag(dl, f, x + 42, y + 3, 11, c.tag, mine ? GOLD : CREAM);
        std::string cn = upper(c.name.c_str());
        text(dl, f, g_title, fit_size(f, g_title, 14, cn.c_str(), w - 180 - tw), x + 50 + tw, y + 2, col, cn.c_str(), LEFT, 1.0f);
        text(dl, f, g_title, 14, x + w - 96, y + 2, CREAM, std::to_string(c.venues).c_str(), RIGHT, 1.0f);
        text(dl, f, g_title, 14, x + w - 16, y + 2, with_alpha(GREEN, 0.9f), std::to_string(c.points).c_str(), RIGHT, 1.0f);
    }
    if (n > visible) {
        float bh = 250.0f * visible / n, by = 148 + 250.0f * TW.season_top / n;
        rect(dl, f, x + w - 5, 148, 3, 250, IM_COL32(255, 255, 255, 25), 1.5f);
        rect(dl, f, x + w - 5, by, 3, bh, with_alpha(ORANGE, 0.8f), 1.5f);
    }
    // the past seasons' top crews, newest first
    const float px = 430, pw = 176;
    glass(dl, f, px, 124, pw, 286);
    text(dl, f, g_title, 12, px + pw / 2, 130, ORANGE, "PAST WINNERS", CENTER, 0.8f);
    float y = 150;
    int last = -1;
    if (s.past.empty()) text(dl, f, g_body, 10, px + pw / 2, 200, GREY, "The first season is still on.", CENTER, 0, pw - 20);
    for (const auto &p : s.past) {
        if (y > 390) break;
        if (p.season != last) {
            last = p.season;
            std::string h = "SEASON " + std::to_string(p.season) + "  -  " + turf_month(p.month);
            text(dl, f, g_hand, 12, px + 10, y, CREAM, h.c_str(), LEFT, 0.6f, pw - 16);
            y += 18;
        }
        ImU32 col = p.place == 1 ? GOLD : p.place == 2 ? IM_COL32(205, 210, 220, 255) : IM_COL32(210, 150, 90, 255);
        text(dl, f, g_title, 12, px + 20, y, col, std::to_string(p.place).c_str(), RIGHT, 0.8f);
        float tw = turf_tag(dl, f, px + 26, y + 1, 9, p.tag, col);
        text(dl, f, g_title, 11, px + pw - 10, y + 1, CREAM, std::to_string(p.points).c_str(), RIGHT, 0.6f);
        std::string cn = upper(p.name.c_str());
        text(dl, f, g_title, fit_size(f, g_title, 10, cn.c_str(), pw - 76 - tw), px + 32 + tw, y + 2, CREAM, cn.c_str(), LEFT, 0.6f);
        y += 19;
    }
}

#include "turf_map.inc"

// The map's page is the city (turf_map.inc); the season's standings and past winners keep the menu's page.
bool turf_board_up()
{
    std::lock_guard<std::mutex> lock(TW.mu);
    return S.page == TURF && TW.tab == 0 && TW.map.valid;
}

void draw_turf(ImDrawList *dl, const Frame &f, const Pad &pad, ImVec2 display)
{
    TurfMap m;
    TurfSeason s;
    int status;
    std::string note;
    bool note_ok = false;
    {
        std::lock_guard<std::mutex> lock(TW.mu);
        m = TW.map;
        s = TW.season;
        status = TW.map_status;
        note.swap(TW.note);
        note_ok = TW.note_ok;
    }
    if (!note.empty()) say(note, note_ok ? 4500 : 7000);
    bool signed_in = acc_state() == ACCOUNT_IN || turf_test_file() || gallery_on();
    unsigned long long now = GetTickCount64();
    if (!gallery_on()) {
        if (TW.map_busy == 0 && now - TW.map_at > 30000) turf_fetch_map();
        if (TW.tab == 1 && TW.season_busy == 0 && now - TW.season_at > 60000) turf_fetch_season();
    }
    if (TW.tab == 0 && m.valid) {
        draw_turf_board(dl, display, f, pad, m, s, signed_in);
        return;
    }
    page_title(dl, f, "TURF WAR");
    // the tabs: MAP, SEASON (LB / RB, left / right or a click; Y and B go back to the map too)
    static const char *const tabs[] = { "MAP", "SEASON" };
    int want = TW.tab;
    if (!TW.picker && m.valid) {
        if (pad.rb || pad.lb || pad.left || pad.right || pad.y) want = 1 - TW.tab;
    }
    {
        float tx = 262;
        for (int i = 0; i < 2; i++) {
            float tw = text_width(f, g_title, 13, tabs[i]) + 20;
            if (!TW.picker && m.valid && mouse_click(f, tx, 52, tw, 20)) want = i;
            rect(dl, f, tx, 52, tw, 20, i == TW.tab ? IM_COL32(120, 12, 26, 230) : IM_COL32(0, 0, 0, 150), 5);
            text(dl, f, g_title, 13, tx + tw / 2, 54, i == TW.tab ? WHITE : GREY, tabs[i], CENTER, 0.8f);
            tx += tw + 4;
        }
    }
    if (want != TW.tab) {
        TW.tab = want;
        TW.armed.clear();
        if (want == 1) turf_fetch_season();
        return;
    }
    if (!gallery_on() && pad.x) { turf_fetch_map(); turf_fetch_season(); say("Refreshing..."); }
    if (!m.valid) {
        glass(dl, f, 34, 76, 572, 334);
        bool off = status == 404, failed = status != 0 && status != 200 && TW.map_busy == 0;
        const char *msg = off ? "Turf War is not on right now." : failed ? "The map could not be loaded right now." : "LOADING THE MAP...";
        text(dl, f, g_title, failed || off ? 15 : 18, 320, 220, failed || off ? GREY : CREAM, msg, CENTER, 1.0f);
        if (failed && !off) text(dl, f, g_hand, 14, 320, 246, ORANGE, pad_text("PRESS {X} TO TRY AGAIN").c_str(), CENTER, 1.0f);
    } else {
        turf_header(dl, f, m, s, signed_in);
        draw_turf_season(dl, f, pad, m, s);
    }
    float hx = 54;
    hx += hint(dl, f, hx, 420, "x", "REFRESH");
    hint(dl, f, hx, 420, "b", m.valid ? "MAP" : "BACK");
    if (pad.b) {
        TW.armed.clear();
        if (m.valid && TW.tab == 1) TW.tab = 0;
        else S.page = TW.back;
    }
}

// ---- tests: RECOMP_TEST_SOCIAL's ui: steps (social_ui.inc chat_test_step) ----
//
// turfhub: the hub's TURF WAR entry picked when the hub shows it (a pad=a step then opens it the player's way),
// else a line saying it is hidden; turf: the page; turfsel=ID: the map's venue of that feID (its details open); turfpick=TAG:
// CLAIM's crew; turflog=ID: that venue as the map has it and what this player may do there. False: not a
// turf step.
bool turf_test_action(const std::string &what, const std::string &arg)
{
    if (what == "turfhub") {
        int status;
        {
            std::lock_guard<std::mutex> lock(TW.mu);
            status = TW.map_status;
        }
        bool shown = turf_shown();
        if (shown && S.page == HUB) S.hub_sel = HUB_TURF;
        fprintf(stderr, "[SOCIAL-UI] turf: hub entry %s (map HTTP %d, page %d)\n", shown ? "shown" : "hidden", status, (int)S.page);
        return true;
    }
    if (what == "turf") {
        open_turf(S.page == TURF ? TW.back : S.page);
        return true;
    }
    TurfMap m;
    {
        std::lock_guard<std::mutex> lock(TW.mu);
        m = TW.map;
    }
    int id = atoi(arg.c_str());
    if (what == "turfsel") {
        bool found = false;
        for (auto &v : m.venues)
            if (v.id == id) { TW.sel_id = id; TW.tab = 0; TW.details = true; found = true; }
        TW.picker = false;
        TW.armed.clear();
        fprintf(stderr, "[SOCIAL-UI] turf: venue %d %s\n", id, found ? "selected" : "not on the map");
        return true;
    }
    if (what == "turfpick") {
        TurfSeason s;
        {
            std::lock_guard<std::mutex> lock(TW.mu);
            s = TW.season;
        }
        auto crews = turf_picker_crews(m, s);
        int at = -1;
        std::string all;
        for (int i = 0; i < (int)crews.size(); i++) {
            all += crews[i].tag + " ";
            if (crews[i].tag == arg) at = i;
        }
        if (at >= 0) TW.picker_sel = at;
        fprintf(stderr, "[SOCIAL-UI] turf: picker %s, crews [%s], %s %s\n", TW.picker ? "up" : "not up", all.c_str(), arg.c_str(),
                at >= 0 ? "picked" : "not there");
        return true;
    }
    if (what == "turflog") {
        const TurfVenue *v = nullptr;
        for (auto &e : m.venues) if (e.id == id) v = &e;
        if (!v) {
            fprintf(stderr, "[SOCIAL-UI] turf venue %d: not on the map (map %s)\n", id, m.valid ? "loaded" : "not loaded");
            return true;
        }
        std::vector<TurfAction> acts;
        std::string why, labels;
        bool signed_in = acc_state() == ACCOUNT_IN || turf_test_file();
        turf_actions(m, *v, signed_in, acts, why);
        for (auto &a : acts) labels += a.label + "/";
        long long now = turf_now(m);
        fprintf(stderr, "[SOCIAL-UI] turf venue %d (%s): owner %lld [%s], protected %s, challenge %lld [%s], claim battle %lld, "
                        "my crew %lld officer %d, can %d, actions [%s], why: %s\n",
                v->id, v->name.c_str(), v->owner, v->otag.c_str(), v->protect > now ? "yes" : "no", v->chal, v->ctag.c_str(),
                v->claim_battle, m.my_crew, (int)m.officer, v->can, labels.c_str(), why.c_str());
        return true;
    }
    return false;
}

// ---- the gallery ----

// The sample map and season (as tests/fixtures/turf-sample.txt): the player's crew HHT (Boss) and four more,
// every mark the map has: a challenge by the player's crew (the dashed line), a turf battle under way with its
// score and watchers (LIVE), a hot streak, a newly taken venue, a contested borough, a front line, the boss
// venue, open turf, the street wire's events, the standings and past winners.
const char *const TURF_SAMPLE =
    "season=2\n"
    "month=2025-11\n"
    "ends=1764547200\n"
    "now=1762600000\n"
    "my_crew=11|1|9|6\n"
    "rules=72|24|2|0|24|3|24\n"
    "venue=6|The Terror Dome|bronx|0|3|BXK|Bronx Kings|1|1762300000|0|2|46|11|HHT|Harlem Heat|1762609670|0|0|0|Your crew already challenged it\n"
    "venue=14|Hunt's Point Scrapyard|bronx|0|3|BXK|Bronx Kings|1|1762400000|0|1|0|0|||0|0|0|1|\n"
    "venue=17|Gun Hill Garage|bronx|0|3|BXK|Bronx Kings|1|1761990000|0|5|0|0|||0|0|0|1|\n"
    "venue=18|The Heights|manhattan|0|11|HHT|Harlem Heat|6|1762100000|0|3|0|0|||0|0|0|0|Your crew holds it\n"
    "venue=11|125th Street Station|manhattan|0|11|HHT|Harlem Heat|6|1762450000|0|1|0|0|||0|0|0|0|Your crew holds it\n"
    "venue=4|Club 357|manhattan|0|9|QNF|Queens Finest|2|1762200000|1762646400|2|0|0|||0|0|0|0|Protected for 12 more hours\n"
    "venue=8|The Dragon House|manhattan|0|9|QNF|Queens Finest|2|1762150000|0|0|44|7|RHR|Red Hook Riders|1762790000|52|0|0|A battle for it is under way\n"
    "venue=26|Crow's Office|manhattan|1|0||||0|0|0|0|0|||0|0|0|0|Hold 3 other venues to challenge for the boss venue\n"
    "venue=16|7th Heaven|queens|0|9|QNF|Queens Finest|2|1761990000|0|4|0|0|||0|0|0|1|\n"
    "venue=1|The Foundation|queens|0|0||||0|0|0|0|0|||0|0|0|1|\n"
    "venue=24|Red Hook Tire Co.|brooklyn|0|7|RHR|Red Hook Riders|4|1762000000|0|3|0|0|||0|0|0|1|\n"
    "venue=19|The Pit|staten_island|0|12|SSM|Shoreside Mob|3|1762300000|0|2|0|0|||0|0|0|1|\n"
    "venue=2|The Limit|bronx|0|0||||0|0|0|0|0|||0|0|0|1|\n"
    "venue=7|Syn Energy Power Plant|queens|0|9|QNF|Queens Finest|2|1762300000|0|1|0|0|||0|0|0|1|\n"
    "venue=10|Club Murder|brooklyn|0|7|RHR|Red Hook Riders|4|1762599880|1762686280|0|0|0|||0|0|0|0|Protected for 23 more hours\n"
    "venue=12|Babylon|brooklyn|0|7|RHR|Red Hook Riders|4|1762250000|0|1|0|0|||0|0|0|1|\n"
    "venue=13|Chopshop|brooklyn|0|0||||0|0|0|0|0|||0|0|0|1|\n"
    "venue=21|Gauntlet|queens|0|7|RHR|Red Hook Riders|4|1762350000|0|0|0|0|||0|0|0|1|\n"
    "started=1761955200\n"
    "season_name=Cold Steel\n"
    "tint=3|0\n"
    "tint=7|2\n"
    "tint=9|1\n"
    "tint=11|3\n"
    "tint=12|4\n"
    "live=8|52|7|RHR|Red Hook Riders|9|QNF|Queens Finest|5|1762598000|eu|K7QX|1|2|4|9|38\n"
    "event=49|taken|1762599880|10|Club Murder|7|RHR|Red Hook Riders|9|QNF|Queens Finest|Red Hook Riders [RHR] take Club Murder from Queens Finest.\n"
    "event=48|defended|1762599160|17|Gun Hill Garage|3|BXK|Bronx Kings|12|SSM|Shoreside Mob|Bronx Kings [BXK] hold Gun Hill Garage.\n"
    "event=47|challenged|1762596400|6|The Terror Dome|11|HHT|Harlem Heat|3|BXK|Bronx Kings|Harlem Heat [HHT] challenge Bronx Kings for The Terror Dome.\n"
    "event=46|challenged|1762590000|8|The Dragon House|7|RHR|Red Hook Riders|9|QNF|Queens Finest|Red Hook Riders [RHR] challenge Queens Finest for The Dragon House.\n"
    "event=45|freed|1762589200|1|The Foundation|0|||0|||The Foundation is open again.\n"
    "event=44|claimed|1762300000|19|The Pit|12|SSM|Shoreside Mob|7|RHR|Red Hook Riders|Shoreside Mob [SSM] claim The Pit, beating Red Hook Riders.\n"
    "total=5\n"
    "crew=1|3|BXK|Bronx Kings|1|412|3|1388\n"
    "crew=2|9|QNF|Queens Finest|2|366|4|1342\n"
    "crew=3|7|RHR|Red Hook Riders|4|301|4|1296\n"
    "crew=4|11|HHT|Harlem Heat|6|240|2|1250\n"
    "crew=5|12|SSM|Shoreside Mob|3|98|1|1210\n"
    "past=1|2025-10|1761955200|1|7|RHR|Red Hook Riders|980|6\n"
    "past=1|2025-10|1761955200|2|3|BXK|Bronx Kings|744|4\n"
    "past=1|2025-10|1761955200|3|9|QNF|Queens Finest|512|3\n";

// The gallery's pages: hub-turf (TURF WAR on the hub), turf (the map, all five boroughs, the player's challenged
// venue picked), turf-zoom (flown to Manhattan, the live battle picked), turf-detail (the picked venue's details
// and actions), turf-open (an open venue: CLAIM), turf-claim (the crew picker), turf-member (a member: read-only),
// turf-why (why not, from the service), turf-season (the standings and past winners).
// RECOMP_TEST_TURF=<file> takes that file's text instead of the sample.
bool turf_gallery(const std::string &p)
{
    TW.gallery_entry = p.compare(0, 8, "hub-turf") == 0 || p.compare(0, 4, "turf") == 0;
    if (!TW.gallery_entry) return false;
    std::string text = TURF_SAMPLE;
    std::string file;
    if (turf_read_file(file)) text = file;
    TurfMap m;
    TurfSeason s;
    parse_turf_map(text.c_str(), m);
    parse_turf_season(text.c_str(), s);
    if (p == "turf-member") m.officer = false;
    {
        std::lock_guard<std::mutex> lock(TW.mu);
        TW.map = m;
        TW.season = s;
        TW.map_status = 200;
    }
    if (p == "hub-turf") { S.page = HUB; S.hub_sel = HUB_TURF; G.ranked = true; return true; }
    S.page = TURF;
    TW.back = HUB;
    TW.tab = p == "turf-season" ? 1 : 0;
    TW.picker = p == "turf-claim";
    TW.picker_sel = 1;
    TW.armed.clear();
    TW.season_top = 0;
    TW.region = p == "turf-zoom" ? 2 : 0;
    TW.cam_snap = true;
    TW.details = p == "turf-detail" || p == "turf-open" || p == "turf-claim" || p == "turf-member" || p == "turf-why";
    TW.sel_id = p == "turf-open" || p == "turf-claim" ? 13 : p == "turf-why" ? 26 : p == "turf-member" ? 18 : p == "turf-zoom" ? 8 : 6;
    return true;
}
