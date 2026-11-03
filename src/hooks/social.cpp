// The Social Update, stage 2a: the network side of the chat window. See social.h.
//
// The account service's social API (server: relay/accounts/social*.go) is used as it is. The game
// reads the text forms where the service has them (/api/social/me, /friends, /events with
// ?format=text) and a small JSON reader below for the rest (/messages, /global, /crews).
#define _CRT_SECURE_NO_WARNINGS
#include "social.h"
#include "account.h"
#include "netplay.h"
#include "netplay_servers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>

namespace social {
namespace {

// ---- a small JSON reader (the service's answers; no JSON parser in the game) ----------------------

struct J {
    enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;
    const J &operator[](const char *key) const
    {
        static const J none;
        for (auto &kv : o) if (kv.first == key) return kv.second;
        return none;
    }
    long long i() const { return kind == NUM ? (long long)n : kind == STR ? atoll(s.c_str()) : 0; }
    const std::string &str() const { static const std::string e; return kind == STR ? s : e; }
};

struct JsonReader {
    const char *p, *end;
    int depth = 0;
    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    static void utf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | (cp >> 6 & 0x3F));
                                 out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | cp >> 18); out += (char)(0x80 | (cp >> 12 & 0x3F));
               out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool string(std::string &out)
    {
        if (p >= end || *p != '"') return false;
        p++;
        while (p < end && *p != '"') {
            if (*p != '\\') { out += *p++; continue; }
            if (++p >= end) return false;
            char c = *p++;
            if (c == 'u') {
                unsigned cp = 0;
                if (end - p < 4 || sscanf(p, "%4x", &cp) != 1) return false;
                p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    unsigned lo = 0;
                    if (sscanf(p + 2, "%4x", &lo) == 1 && lo >= 0xDC00 && lo < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                }
                utf8(out, cp);
            } else {
                out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 'b' ? '\b' : c == 'f' ? '\f' : c;
            }
        }
        if (p >= end) return false;
        p++;
        return true;
    }
    bool value(J &v)
    {
        ws();
        if (p >= end || ++depth > 40) return false;
        bool ok = true;
        if (*p == '{') {
            v.kind = J::OBJ;
            p++;
            ws();
            if (p < end && *p == '}') p++;
            else for (;;) {
                std::string key;
                ws();
                if (!string(key)) { ok = false; break; }
                ws();
                if (p >= end || *p++ != ':') { ok = false; break; }
                v.o.emplace_back(key, J());
                if (!value(v.o.back().second)) { ok = false; break; }
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; break; }
                ok = false;
                break;
            }
        } else if (*p == '[') {
            v.kind = J::ARR;
            p++;
            ws();
            if (p < end && *p == ']') p++;
            else for (;;) {
                v.a.emplace_back();
                if (!value(v.a.back())) { ok = false; break; }
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; break; }
                ok = false;
                break;
            }
        } else if (*p == '"') {
            v.kind = J::STR;
            ok = string(v.s);
        } else if (!strncmp(p, "true", 4) && end - p >= 4) { v.kind = J::BOOL; v.b = true; p += 4; }
        else if (!strncmp(p, "false", 5) && end - p >= 5) { v.kind = J::BOOL; p += 5; }
        else if (!strncmp(p, "null", 4) && end - p >= 4) { p += 4; }
        else {
            char *e = nullptr;
            v.kind = J::NUM;
            v.n = strtod(p, &e);
            ok = e && e != p;
            if (ok) p = e;
        }
        depth--;
        return ok;
    }
};

bool parse_json(const std::string &text, J &out)
{
    JsonReader r{ text.data(), text.data() + text.size() };
    return r.value(out);
}

std::string json_text(const std::string &s)     // a JSON string literal
{
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += (char)c;
    }
    return out + "\"";
}

// RFC 3339 ("2026-11-03T21:42:05.123+01:00", Go's time.Time) as unix seconds; 0 for Go's zero time.
long long rfc3339(const std::string &t)
{
    int Y, M, D, h, m, s;
    if (sscanf(t.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) != 6 || Y < 1971) return 0;
    struct tm tm = {};
    tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D; tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = s;
    long long u = (long long)_mkgmtime(&tm);
    const char *z = t.c_str() + 19;
    while (*z == '.' || (*z >= '0' && *z <= '9')) z++;
    int oh = 0, om = 0;
    if ((*z == '+' || *z == '-') && sscanf(z + 1, "%d:%d", &oh, &om) >= 1)
        u -= (*z == '+' ? 1 : -1) * (oh * 3600LL + om * 60LL);
    return u;
}

// ---- the state ------------------------------------------------------------------------------------

std::mutex g_mu;
View g_v;                           // guarded by g_mu; g_v.version changes with every change
std::vector<Toast> g_toasts;
std::deque<std::function<void()>> g_jobs;
std::condition_variable g_cv;
std::atomic<bool> g_stop{false}, g_started{false};
std::atomic<int> g_server{0};       // the service's switch: 0 not asked yet, 1 on, -1 off (404)
std::atomic<bool> g_ready{false};   // lists loaded, the long-poll runs
std::atomic<long long> g_seq{0};
std::atomic<bool> g_dirty_friends{false}, g_dirty_convos{false}, g_dirty_crew{false}, g_resync{false};
std::atomic<unsigned long long> g_typing_until{0};
std::string g_mode_name;            // guarded by g_mu: the room's mode as the overlay names it
const bool g_test_log = getenv("RECOMP_TEST_SOCIAL") != nullptr;
void *volatile g_poll_live = nullptr;
void *volatile g_work_live = nullptr;
HANDLE g_threads[3];

int env_switch()                    // RECOMP_SOCIAL: 1 on, -1 off, 0 the service decides
{
    static int v = 2;
    if (v == 2) {
        const char *e = getenv("RECOMP_SOCIAL");
        v = !e || !*e ? 0 : (!_stricmp(e, "on") || !strcmp(e, "1")) ? 1 : (!_stricmp(e, "off") || !strcmp(e, "0")) ? -1 : 0;
    }
    return v;
}

void changed() { g_v.version++; }   // callers hold g_mu

void toast(int kind, const std::string &title, const std::string &text, long long dm_from = 0)
{
    Toast t;
    t.kind = kind; t.title = title; t.text = text; t.dm_from = dm_from; t.at = GetTickCount64();
    g_toasts.push_back(t);
    if (g_toasts.size() > 8) g_toasts.erase(g_toasts.begin());
}

void note(const std::string &text)  // callers hold g_mu
{
    g_v.note = text;
    g_v.note_at = GetTickCount64();
    changed();
}

// One request; the answer in `out`. 0 unreachable or canceled, -1 not signed in.
int http(const char *method, const std::string &path, const char *json, std::string &out, int receive_ms = 0,
         void *volatile *live = nullptr)
{
    static const int SIZE = 256 * 1024;
    char *buf = (char *)malloc(SIZE);
    if (!buf) return 0;
    if (live) *live = nullptr;
    if (g_stop) { free(buf); return 0; }
    int status = account_call(method, path.c_str(), json, buf, SIZE, receive_ms, live);
    out.assign(buf);
    free(buf);
    return status;
}

std::string error_of(const std::string &body, int status)
{
    J j;
    if (parse_json(body, j) && j["error"].kind == J::STR && !j["error"].s.empty()) return j["error"].s;
    if (status == 0) return "The service did not answer. Try again.";
    char b[48];
    snprintf(b, sizeof(b), "That did not work (HTTP %d).", status);
    return b;
}

// POST with a JSON body; a refusal becomes the view's note. Returns the status.
int post(const std::string &path, const std::string &json, std::string *answer = nullptr)
{
    std::string out;
    int status = http("POST", path, json.c_str(), out, 0, &g_work_live);
    if (status < 200 || status >= 300) {
        std::lock_guard<std::mutex> lk(g_mu);
        note(error_of(out, status));
        fprintf(stderr, "[SOCIAL] POST %s: HTTP %d %s\n", path.c_str(), status, error_of(out, status).c_str());
    }
    if (answer) *answer = out;
    return status;
}

std::vector<std::string> split(const std::string &s, char sep)
{
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = s.find(sep, a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

std::string key_value(const std::string &text, const char *key)     // "key=value" lines
{
    for (auto &line : split(text, '\n')) {
        size_t eq = line.find('=');
        if (eq != std::string::npos && line.compare(0, eq, key) == 0) return line.substr(eq + 1);
    }
    return "";
}

Line line_of(const J &e)
{
    Line l;
    l.seq = e["seq"].i();
    l.from = e["from"].i();
    l.channel = e["channel"].str();
    l.name = e["name"].str();
    l.tag = e["tag"].str();
    l.text = e["text"].str();
    l.hidden = e["hidden"].b;
    l.system = e["system"].b;
    l.at = rfc3339(e["at"].str());
    return l;
}

void add_line(std::vector<Line> &list, const Line &l, size_t cap)  // callers hold g_mu; by seq, no repeats
{
    for (auto it = list.rbegin(); it != list.rend(); ++it) if (it->seq == l.seq) return;
    auto at = std::upper_bound(list.begin(), list.end(), l.seq, [](long long s, const Line &x) { return s < x.seq; });
    list.insert(at, l);
    if (list.size() > cap) list.erase(list.begin(), list.begin() + (list.size() - cap));
}

Thread *thread_of(long long with)   // callers hold g_mu
{
    for (auto &t : g_v.threads) if (t.with == with) return &t;
    return nullptr;
}

void count_unread()                 // callers hold g_mu
{
    int n = 0;
    for (auto &c : g_v.convos) n += c.unread;
    g_v.unread = n;
}

// ---- loading the lists ------------------------------------------------------------------------------

// /api/social/me: 404 while the service's switch is off (then nothing else is asked), else the
// cursor and the counts; then /api/me for this account's id (its own lines and messages).
bool probe()
{
    std::string out, me;
    int status = http("GET", "/api/social/me?format=text", nullptr, out, 0, &g_work_live);
    if (status == 404) {
        if (g_server != -1) fprintf(stderr, "[SOCIAL] the service has the social features off\n");
        g_server = -1;
        return false;
    }
    if (status != 200) return false;
    if (http("GET", "/api/me", nullptr, me, 0, &g_work_live) == 200) {
        J j;
        if (parse_json(me, j)) {
            std::lock_guard<std::mutex> lk(g_mu);
            g_v.me = j["id"].i();
            g_v.my_name = j["name"].str();
            changed();
        }
    }
    if (g_server != 1) fprintf(stderr, "[SOCIAL] on (the service's switch)%s\n", env_switch() > 0 ? ", RECOMP_SOCIAL=on" : "");
    g_server = 1;
    g_seq = atoll(key_value(out, "seq").c_str());
    std::lock_guard<std::mutex> lk(g_mu);
    g_v.slow_mode = atoi(key_value(out, "slow_mode").c_str());
    int mc = atoi(key_value(out, "max_chars").c_str());
    g_v.max_chars = mc > 0 ? mc : 200;
    g_v.dms = key_value(out, "dms");
    g_v.show_avatar = key_value(out, "show_avatar") != "0";
    changed();
    return true;
}

void load_friends()
{
    std::string out;
    if (http("GET", "/api/social/friends?format=text", nullptr, out, 0, &g_work_live) != 200) return;
    std::vector<Friend> friends;
    std::vector<Ref> in, outl, blocked, muted;
    std::string dms;
    for (auto &line : split(out, '\n')) {
        if (!line.compare(0, 4, "dms=")) { dms = line.substr(4); continue; }
        auto f = split(line, '\t');
        if (f[0] == "friend" && f.size() >= 11) {
            Friend x;
            x.id = atoll(f[1].c_str()); x.name = f[2]; x.tag = f[3]; x.state = f[4]; x.relay = f[5]; x.room = f[6];
            x.mode = f[7]; x.seats = f[8]; x.joinable = f[9] == "1"; x.last_online = atoll(f[10].c_str());
            friends.push_back(x);
        } else if (f.size() >= 4 && (f[0] == "in" || f[0] == "out" || f[0] == "blocked" || f[0] == "muted")) {
            Ref r;
            r.id = atoll(f[1].c_str()); r.name = f[2]; r.tag = f[3];
            (f[0] == "in" ? in : f[0] == "out" ? outl : f[0] == "blocked" ? blocked : muted).push_back(r);
        }
    }
    std::lock_guard<std::mutex> lk(g_mu);
    g_v.friends = friends;
    g_v.in = in; g_v.out = outl; g_v.blocked = blocked; g_v.muted = muted;
    g_v.dms = dms;
    g_v.requests = (int)in.size();
    int online = 0;
    for (auto &f : friends) online += f.state != "offline";
    g_v.friends_online = online;
    changed();
}

void load_convos()
{
    std::string out;
    J j;
    if (http("GET", "/api/social/messages", nullptr, out, 0, &g_work_live) != 200 || !parse_json(out, j)) return;
    std::vector<Convo> list;
    for (auto &c : j["conversations"].a) {
        Convo x;
        x.with.id = c["with"]["id"].i(); x.with.name = c["with"]["name"].str(); x.with.tag = c["with"]["tag"].str();
        x.last = c["last"].str(); x.last_from = c["last_from"].i(); x.last_at = rfc3339(c["last_at"].str());
        x.unread = (int)c["unread"].i();
        list.push_back(x);
    }
    std::lock_guard<std::mutex> lk(g_mu);
    g_v.convos = list;
    count_unread();
    changed();
}

void load_thread(long long with)
{
    std::string out;
    J j;
    char path[96];
    snprintf(path, sizeof(path), "/api/social/messages/%lld?limit=60", with);
    if (http("GET", path, nullptr, out, 0, &g_work_live) != 200 || !parse_json(out, j)) return;
    Thread t;
    t.with = with;
    t.loaded = true;
    t.can_message = j["can_message"].b;
    t.seen_up_to = j["seen_up_to"].i();
    t.seen_at = rfc3339(j["seen_at"].str());
    for (auto &m : j["messages"].a) {
        Dm d;
        d.id = m["id"].i(); d.from = m["from"].i(); d.text = m["text"].str(); d.at = rfc3339(m["at"].str());
        t.msgs.push_back(d);
    }
    std::lock_guard<std::mutex> lk(g_mu);
    Thread *have = thread_of(with);
    if (have) {     // keep lines the long-poll brought meanwhile
        for (auto &d : have->msgs)
            if (std::none_of(t.msgs.begin(), t.msgs.end(), [&](const Dm &x) { return x.id == d.id; })) t.msgs.push_back(d);
        std::sort(t.msgs.begin(), t.msgs.end(), [](const Dm &a, const Dm &b) { return a.id < b.id; });
        *have = t;
    } else {
        g_v.threads.push_back(t);
    }
    bool known = false;
    for (auto &c : g_v.convos) known |= c.with.id == with;
    if (!known) {   // a conversation that has no message yet: the name from the thread
        Convo c;
        c.with.id = with; c.with.name = j["with"]["name"].str(); c.with.tag = j["with"]["tag"].str();
        g_v.convos.push_back(c);
    }
    changed();
}

void load_global()
{
    std::string out;
    J j;
    if (http("GET", "/api/social/global?regions=all&limit=100", nullptr, out, 0, &g_work_live) != 200 ||
        !parse_json(out, j)) return;
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto &e : j["lines"].a) add_line(g_v.global, line_of(e), 300);
    g_v.online = (int)j["online"].i();
    g_v.slow_mode = (int)j["slow_mode"].i();
    changed();
}

void load_crew()
{
    std::string out;
    J j;
    int status = http("GET", "/api/social/crews/mine", nullptr, out, 0, &g_work_live);
    if (status == 404) {
        bool off = error_of(out, status) == "not available";    // crews=off in the switch file
        std::lock_guard<std::mutex> lk(g_mu);
        g_v.crews = !off;
        g_v.crew = 0;
        g_v.crew_chat.clear();
        changed();
        return;
    }
    if (status != 200 || !parse_json(out, j)) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_v.crews = true;
        if (g_v.crew != j["id"].i()) g_v.crew_chat.clear();
        g_v.crew = j["id"].i();
        g_v.crew_tag = j["tag"].str();
        g_v.crew_name = j["name"].str();
        g_v.crew_motto = j["motto"].str();
        g_v.crew_members = (int)j["member_count"].i();
        g_v.crew_online = (int)j["online"].i();
        g_v.crew_invites.erase(std::remove_if(g_v.crew_invites.begin(), g_v.crew_invites.end(),
                                              [](const CrewInvite &c) { return c.crew == g_v.crew; }), g_v.crew_invites.end());
        changed();
    }
    if (http("GET", "/api/social/crews/chat?limit=60", nullptr, out, 0, &g_work_live) != 200 || !parse_json(out, j)) return;
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto &e : j["lines"].a) add_line(g_v.crew_chat, line_of(e), 200);
    changed();
}

void load_all()
{
    load_friends();
    load_convos();
    load_global();
    load_crew();
    std::vector<long long> open;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto &t : g_v.threads) open.push_back(t.with);
    }
    for (long long w : open) load_thread(w);
}

// ---- the long-poll's events -------------------------------------------------------------------------

// "ev\tseq\tkind\tchannel\tfrom\tname\ttag\tunix\tflags\tstate\ttext"
void apply_event(const std::vector<std::string> &f)
{
    if (f.size() < 11) return;
    long long seq = atoll(f[1].c_str()), from = atoll(f[4].c_str()), at = atoll(f[7].c_str());
    const std::string &kind = f[2], &channel = f[3], &name = f[5], &tag = f[6], &flags = f[8], &state = f[9];
    std::string text = f[10];
    for (size_t k = 11; k < f.size(); k++) text += " " + f[k];
    std::lock_guard<std::mutex> lk(g_mu);
    long long me = g_v.me;
    auto make_line = [&]() {
        Line l;
        l.seq = seq; l.from = from; l.at = at; l.channel = channel; l.name = name; l.tag = tag; l.text = text;
        l.hidden = flags.find('h') != std::string::npos;
        l.system = flags.find('s') != std::string::npos;
        return l;
    };
    if (kind == "global") {
        add_line(g_v.global, make_line(), 300);
        if (g_test_log)     // tests only: a busy global chat stays out of the players' logs
            fprintf(stderr, "[SOCIAL] global %s from %s: %s\n", channel.c_str(), name.c_str(), text.c_str());
    } else if (kind == "crew") {
        add_line(g_v.crew_chat, make_line(), 200);
    } else if (kind == "dm") {
        long long other = channel.compare(0, 3, "dm:") == 0 ? atoll(channel.c_str() + 3) : from;
        bool incoming = from != me;
        if (Thread *t = thread_of(other)) {
            if (std::none_of(t->msgs.begin(), t->msgs.end(), [&](const Dm &d) { return d.id == seq; })) {
                Dm d;
                d.id = seq; d.from = from; d.text = text; d.at = at;
                t->msgs.push_back(d);
            }
        }
        bool found = false;
        for (auto &c : g_v.convos) {
            if (c.with.id != other) continue;
            found = true;
            c.last = text; c.last_from = from; c.last_at = at;
            if (incoming) c.unread++;
        }
        if (!found) {
            Convo c;
            c.with.id = other;
            if (incoming) { c.with.name = name; c.with.tag = tag; c.unread = 1; }
            else g_dirty_convos = true;     // the other side's name comes with the list
            c.last = text; c.last_from = from; c.last_at = at;
            g_v.convos.push_back(c);
        }
        std::stable_sort(g_v.convos.begin(), g_v.convos.end(), [](const Convo &a, const Convo &b) { return a.last_at > b.last_at; });
        count_unread();
        if (incoming) {
            toast(TOAST_DM, name, text, from);
            fprintf(stderr, "[SOCIAL] dm from %s (%lld): %s\n", name.c_str(), from, text.c_str());
        }
    } else if (kind == "friend_request") {
        toast(TOAST_REQUEST, name, "wants to be your friend");
        fprintf(stderr, "[SOCIAL] friend request from %s (%lld)\n", name.c_str(), from);
        g_dirty_friends = true;
    } else if (kind == "friend_added") {
        toast(TOAST_FRIEND, name, "is now your friend");
        fprintf(stderr, "[SOCIAL] friend added: %s (%lld)\n", name.c_str(), from);
        g_dirty_friends = true;
    } else if (kind == "friend_removed") {
        g_dirty_friends = true;
    } else if (kind == "presence") {
        for (auto &fr : g_v.friends) if (fr.id == from) fr.state = state;
        fprintf(stderr, "[SOCIAL] presence %lld: %s %s\n", from, state.c_str(), text.c_str());
        g_dirty_friends = true;
    } else if (kind == "read") {
        long long other = channel.compare(0, 3, "dm:") == 0 ? atoll(channel.c_str() + 3) : from;
        if (Thread *t = thread_of(other)) { t->seen_up_to = atoll(text.c_str()); t->seen_at = at; }
    } else if (kind == "crew_invite") {
        CrewInvite c;
        c.crew = channel.compare(0, 5, "crew:") == 0 ? atoll(channel.c_str() + 5) : 0;
        c.tag = tag; c.text = text; c.from = name;
        if (c.crew && std::none_of(g_v.crew_invites.begin(), g_v.crew_invites.end(), [&](const CrewInvite &x) { return x.crew == c.crew; }))
            g_v.crew_invites.push_back(c);
        toast(TOAST_CREW, "CREW INVITE", text.empty() ? name + " invites you to " + tag : text);
    } else if (kind == "crew_update") {
        if (!text.empty()) toast(TOAST_CREW, tag.empty() ? "CREW" : tag, text);
        g_dirty_crew = true;
    } else if (kind == "notice") {
        toast(TOAST_NOTICE, "NOTICE", text);
    } else if (kind == "removed") {
        long long id = atoll(text.c_str());
        auto drop = [&](std::vector<Line> &list) {
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Line &l) { return l.seq == id; }), list.end());
        };
        drop(g_v.global);
        drop(g_v.crew_chat);
    }
    changed();
}

DWORD WINAPI poll_thread(LPVOID)
{
    int misses = 0;
    while (!g_stop) {
        if (!g_ready) { Sleep(250); continue; }
        char path[128];
        snprintf(path, sizeof(path), "/api/social/events?after=%lld&wait=25&regions=all&format=text", g_seq.load());
        std::string out;
        int status = http("GET", path, nullptr, out, 40000, &g_poll_live);
        if (g_stop) break;
        if (status == 200) {
            misses = 0;
            long long seq = atoll(key_value(out, "seq").c_str());
            bool resync = key_value(out, "resync") == "1";
            for (auto &line : split(out, '\n'))
                if (!line.compare(0, 3, "ev\t")) apply_event(split(line, '\t'));
            if (seq > 0) g_seq = seq;
            if (resync) {
                fprintf(stderr, "[SOCIAL] resync: the lists again\n");
                g_resync = true;
            }
            g_cv.notify_all();
            continue;
        }
        if (status == 404) {        // switched off while the game runs
            fprintf(stderr, "[SOCIAL] the service turned the social features off\n");
            g_server = -1;
            g_ready = false;
            continue;
        }
        if (status < 0) { g_ready = false; continue; }      // signed out
        // unreachable: wait a little longer each time, up to 30 s
        misses++;
        for (int i = 0; i < std::min(30, 2 * misses) * 10 && !g_stop; i++) Sleep(100);
    }
    return 0;
}

// ---- the presence heartbeat --------------------------------------------------------------------------

std::string presence_json()
{
    const char *state = "menu";
    NetRoom r;
    memset(&r, 0, sizeof(r));
    if (netplay_in_lobby()) netplay_lobby_room(&r);
    if (netplay_spectating()) state = "watching";
    else if (netplay_state() == NETPLAY_CONNECTED) state = "fighting";
    else if (r.in_room) state = "room";
    std::string region = region_of_server(netservers_connected());
    std::string j = std::string("{\"state\":\"") + state + "\"";
    if (!region.empty()) j += ",\"relay\":\"" + region + "\"";
    if (r.in_room && strcmp(state, "menu")) {
        std::string mode;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            mode = g_mode_name;     // the overlay's name for the room's mode (online_data reads the dump)
        }
        int max = r.settings.max > 0 ? r.settings.max : 2;
        char seats[16];
        snprintf(seats, sizeof(seats), "%d/%d", r.player_count, max);
        // a King of the Hill room takes players at any time; the others while nobody plays
        bool joinable = r.player_count < max && (r.phase == 0 || NETPLAY_IS_KOTH(r.settings.mode));
        j += ",\"room\":" + json_text(r.code) + ",\"mode\":" + json_text(mode) + ",\"seats\":\"" + seats + "\"";
        if (joinable) j += ",\"joinable\":true";
    }
    return j + "}";
}

// ---- tests: RECOMP_TEST_SOCIAL ------------------------------------------------------------------------
//
// "step;step;...", run in order once the lists are loaded: wait:S, friend:NAME (a request by name),
// accept (the first request that comes, waiting up to 60 s), dm:NAME:TEXT (to a friend, waiting up to
// 60 s for the friendship), global:REGION:TEXT, remove:NAME, log (the state, one line). Each step
// writes a [SOCIAL-TEST] line.

struct TestRun {
    std::vector<std::string> steps;
    size_t next = 0;
    unsigned long long since = 0, wait_until = 0;
} g_test;

long long friend_id(const std::string &name)    // callers hold g_mu
{
    for (auto &f : g_v.friends) if (f.name == name) return f.id;
    return 0;
}

void test_tick()
{
    if (g_test.next >= g_test.steps.size()) return;
    unsigned long long now = GetTickCount64();
    if (!g_test.since) g_test.since = now;
    if (now < g_test.wait_until) return;
    const std::string &step = g_test.steps[g_test.next];
    bool done = true, timed_out = now - g_test.since > 60000;
    std::string what = step.substr(0, step.find(':')), arg = step.find(':') == std::string::npos ? "" : step.substr(step.find(':') + 1);
    if (what == "wait") {
        g_test.wait_until = now + (unsigned long long)(atof(arg.c_str()) * 1000);
    } else if (what == "friend") {
        friend_request(0, arg);
    } else if (what == "accept") {
        long long id = 0;
        { std::lock_guard<std::mutex> lk(g_mu); if (!g_v.in.empty()) id = g_v.in[0].id; }
        if (id) friend_accept(id);
        else { done = timed_out; g_dirty_friends = true; }
    } else if (what == "dm" || what == "remove") {
        std::string name = what == "dm" ? arg.substr(0, arg.find(':')) : arg;
        std::string text = what == "dm" && arg.find(':') != std::string::npos ? arg.substr(arg.find(':') + 1) : "";
        long long id;
        { std::lock_guard<std::mutex> lk(g_mu); id = friend_id(name); }
        if (id && what == "dm") send_dm(id, text);
        else if (id) friend_remove(id);
        else { done = timed_out; g_dirty_friends = true; }
    } else if (what == "global") {
        std::string region = arg.substr(0, arg.find(':'));
        send_global(region.c_str(), arg.find(':') != std::string::npos ? arg.substr(arg.find(':') + 1) : "");
    } else if (what == "log") {
        std::lock_guard<std::mutex> lk(g_mu);
        std::string fl;
        for (auto &f : g_v.friends) fl += f.name + "=" + f.state + (f.room.empty() ? "" : "@" + f.room) + " ";
        fprintf(stderr, "[SOCIAL-TEST] state: me %lld, friends [%s], in %d, out %d, convos %d, unread %d, global %d, crew %lld\n",
                g_v.me, fl.c_str(), (int)g_v.in.size(), (int)g_v.out.size(), (int)g_v.convos.size(), g_v.unread,
                (int)g_v.global.size(), g_v.crew);
    }
    if (!done) return;
    fprintf(stderr, "[SOCIAL-TEST] step %d %s%s\n", (int)g_test.next + 1, step.c_str(), timed_out && what != "wait" ? " (timed out)" : "");
    g_test.next++;
    g_test.since = 0;
}

// ---- the worker thread -----------------------------------------------------------------------------

DWORD WINAPI work_thread(LPVOID)
{
    unsigned long long next_probe = 0, last_presence = 0, last_friends = 0, last_global = 0;
    std::string presence_sent;
    if (const char *t = getenv("RECOMP_TEST_SOCIAL")) {
        for (auto &s : split(t, ';')) if (!s.empty()) g_test.steps.push_back(s);
    }
    while (!g_stop) {
        unsigned long long now = GetTickCount64();
        enum AccountState as = account_state();
        if (as != ACCOUNT_IN && as != ACCOUNT_CHECKING) {
            if (g_ready || g_server) {
                g_ready = false;
                g_server = 0;
                std::lock_guard<std::mutex> lk(g_mu);
                unsigned v = g_v.version;
                g_v = View();
                g_v.version = v + 1;    // the overlay's copy changes too
            }
            next_probe = 0;
        } else if (!g_ready) {
            if (now >= next_probe) {
                bool on = probe();
                if (on) {
                    load_all();
                    g_ready = true;
                    g_dirty_friends = g_dirty_convos = g_dirty_crew = g_resync = false;
                    last_friends = last_global = now;
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_v.ready = true;
                    changed();
                    fprintf(stderr, "[SOCIAL] ready: %d friends, %d requests, %d conversations, %d global lines\n",
                            (int)g_v.friends.size(), (int)g_v.in.size(), (int)g_v.convos.size(), (int)g_v.global.size());
                }
                // off: ask again in five minutes (the owner may switch it on); unreachable: in 30 s
                next_probe = now + (g_server < 0 ? 300000 : 30000);
            }
        } else {
            for (;;) {      // the queued actions
                std::function<void()> job;
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    if (g_jobs.empty()) break;
                    job = std::move(g_jobs.front());
                    g_jobs.pop_front();
                }
                job();
                if (g_stop) break;
            }
            if (g_resync.exchange(false)) { probe(); load_all(); last_friends = now; }
            if (g_dirty_friends && now - last_friends > 1500) { g_dirty_friends = false; last_friends = now; load_friends(); }
            if (g_dirty_convos.exchange(false)) load_convos();
            if (g_dirty_crew.exchange(false)) load_crew();
            if (now - last_global > 60000) {        // the online count beside GLOBAL
                last_global = now;
                std::string out;
                J j;
                if (http("GET", "/api/social/global?regions=eu&limit=1", nullptr, out, 0, &g_work_live) == 200 &&
                    parse_json(out, j)) {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_v.online = (int)j["online"].i();
                    changed();
                }
            }
            std::string p = presence_json();
            if (p != presence_sent || now - last_presence > 60000) {
                if (post("/api/social/presence", p) / 100 == 2) presence_sent = p;
                last_presence = now;
            }
            test_tick();
        }
        std::unique_lock<std::mutex> lk(g_mu);
        if (g_jobs.empty() && !g_stop) g_cv.wait_for(lk, std::chrono::milliseconds(400));
    }
    return 0;
}

void queue(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_jobs.size() > 64) return;     // nobody types that fast: something is stuck
        g_jobs.push_back(std::move(job));
    }
    g_cv.notify_all();
}


// ---- avatars (stage 2c) ---------------------------------------------------------------------------------
//
// One thread of their own, so a slow picture never holds a chat line back: GET /api/avatar/N.png from
// the account service (public, no token; the service fetched it from Discord once, the game never talks
// to Discord), decoded with WIC to 64x64 RGBA. The overlay turns them into textures and keeps at most
// 256 (social_ui.inc). A 404 (no picture, or the player hid it) comes back as "none".

const int AVATAR_SIDE = 64;
std::mutex g_av_mu;
std::condition_variable g_av_cv;
std::deque<long long> g_av_queue;
std::set<long long> g_av_asked;     // queued or done: asked once until avatar_forget
std::vector<AvatarPixels> g_av_done;

bool decode_png(const unsigned char *data, int len, AvatarPixels &out)
{
    IWICImagingFactory *factory = nullptr;
    IWICStream *stream = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICBitmapScaler *scaler = nullptr;
    IWICFormatConverter *conv = nullptr;
    UINT w = 0, h = 0;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory((BYTE *)data, (DWORD)len)) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h && w <= 1024 && h <= 1024 &&
        SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(frame, AVATAR_SIDE, AVATAR_SIDE, WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
        out.rgba.assign((size_t)AVATAR_SIDE * AVATAR_SIDE * 4, 0);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, AVATAR_SIDE * 4, (UINT)out.rgba.size(), out.rgba.data()));
        out.w = out.h = AVATAR_SIDE;
    }
    if (conv) conv->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    return ok;
}

DWORD WINAPI avatar_thread(LPVOID)
{
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    static const int SIZE = 512 * 1024;
    unsigned char *buf = (unsigned char *)malloc(SIZE);
    int fetched = 0, missing = 0;
    while (!g_stop && buf) {
        long long id = 0;
        {
            std::unique_lock<std::mutex> lk(g_av_mu);
            if (g_av_queue.empty()) g_av_cv.wait_for(lk, std::chrono::milliseconds(500));
            if (g_av_queue.empty()) continue;
            id = g_av_queue.front();
            g_av_queue.pop_front();
        }
        if (!social_enabled()) {        // signed out or switched off meanwhile: ask again later
            std::lock_guard<std::mutex> lk(g_av_mu);
            g_av_asked.erase(id);
            continue;
        }
        char path[64];
        snprintf(path, sizeof(path), "/api/avatar/%lld.png", id);
        int len = 0;
        int status = account_fetch(path, buf, SIZE, &len);
        AvatarPixels px;
        px.id = id;
        px.ok = status == 200 && len > 8 && len < SIZE - 1 && decode_png(buf, len, px);
        if (px.ok) fetched++; else missing++;
        if (g_test_log || fetched + missing <= 3)
            fprintf(stderr, "[SOCIAL] avatar %lld: %s (HTTP %d, %d bytes)\n", id, px.ok ? "picture" : "none, initials", status, len);
        std::lock_guard<std::mutex> lk(g_av_mu);
        if (status == 0) g_av_asked.erase(id);  // unreachable: the overlay may ask again
        g_av_done.push_back(std::move(px));
        if (g_av_done.size() > 64) g_av_done.erase(g_av_done.begin());
    }
    free(buf);
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

}   // namespace

// ---- the C++ side for the overlay ------------------------------------------------------------------------

bool snapshot(View &out, unsigned have)
{
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_v.version == have) return false;
    out = g_v;
    out.enabled = social_enabled() != 0;
    return true;
}

std::vector<Toast> take_toasts()
{
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Toast> out;
    out.swap(g_toasts);
    return out;
}

std::string region_of_server(int index)
{
    return index == NETSERVER_EU ? "eu" : index == NETSERVER_NA ? "us-east" : index == NETSERVER_WEST ? "us-west" : "";
}

void send_global(const char *region, const std::string &text)
{
    std::string r = region && *region ? region : "eu";
    queue([r, text]() {
        if (post("/api/social/global", "{\"region\":" + json_text(r) + ",\"text\":" + json_text(text) + "}") / 100 == 2)
            fprintf(stderr, "[SOCIAL] sent to global %s: %s\n", r.c_str(), text.c_str());
    });
}

void send_dm(long long to, const std::string &text)
{
    queue([to, text]() {
        char path[64];
        snprintf(path, sizeof(path), "/api/social/messages/%lld", to);
        if (post(path, "{\"text\":" + json_text(text) + "}") / 100 == 2)
            fprintf(stderr, "[SOCIAL] dm sent to %lld: %s\n", to, text.c_str());
    });
}

void send_crew(const std::string &text)
{
    queue([text]() { post("/api/social/crews/chat", "{\"text\":" + json_text(text) + "}"); });
}

void friend_request(long long id, const std::string &name)
{
    queue([id, name]() {
        std::string body = id ? "{\"account\":" + std::to_string(id) + "}" : "{\"name\":" + json_text(name) + "}";
        std::string out;
        int status = post("/api/social/friends/request", body, &out);
        if (status / 100 == 2) {
            J j;
            bool now_friends = parse_json(out, j) && j["friends"].b;
            fprintf(stderr, "[SOCIAL] friend request sent to %s%s\n", id ? std::to_string(id).c_str() : name.c_str(),
                    now_friends ? " (friends now)" : "");
            std::lock_guard<std::mutex> lk(g_mu);
            note(now_friends ? "You are friends now." : "Friend request sent.");
        }
        load_friends();
    });
}

static void friend_op(const char *op, long long id)
{
    std::string path = std::string("/api/social/friends/") + op;
    queue([path, id, op]() {
        if (post(path, "{\"account\":" + std::to_string(id) + "}") / 100 == 2)
            fprintf(stderr, "[SOCIAL] friend %s %lld\n", op, id);
        load_friends();
    });
}

void friend_accept(long long id) { friend_op("accept", id); }
void friend_decline(long long id) { friend_op("decline", id); }
void friend_remove(long long id) { friend_op("remove", id); }

void mute(long long id, bool on)
{
    queue([id, on]() {
        post(on ? "/api/social/mute" : "/api/social/unmute", "{\"account\":" + std::to_string(id) + "}");
        load_friends();
        load_global();
    });
}

void open_thread(long long with)
{
    bool need;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Thread *t = thread_of(with);
        need = !t || !t->loaded;
        if (!t) {
            Thread n;
            n.with = with;
            g_v.threads.push_back(n);
            changed();
        }
    }
    if (need) queue([with]() { load_thread(with); });
}

void mark_read(long long with)
{
    {
        std::lock_guard<std::mutex> lk(g_mu);
        bool any = false;
        for (auto &c : g_v.convos) if (c.with.id == with && c.unread) { c.unread = 0; any = true; }
        if (!any) return;
        count_unread();
        changed();
    }
    queue([with]() {
        char path[80];
        snprintf(path, sizeof(path), "/api/social/messages/%lld/read", with);
        post(path, "{\"up_to\":0}");
    });
}

void crew_join(long long crew)
{
    queue([crew]() {
        char path[80];
        snprintf(path, sizeof(path), "/api/social/crews/%lld/join", crew);
        post(path, "{}");
        load_crew();
    });
}

void crew_dismiss(long long crew)
{
    std::lock_guard<std::mutex> lk(g_mu);
    g_v.crew_invites.erase(std::remove_if(g_v.crew_invites.begin(), g_v.crew_invites.end(),
                                          [&](const CrewInvite &c) { return c.crew == crew; }), g_v.crew_invites.end());
    changed();
}

void reload() { g_resync = true; g_cv.notify_all(); }

void block(long long id, const std::string &name, bool on)
{
    queue([id, name, on]() {
        std::string body = id ? "{\"account\":" + std::to_string(id) + "}" : "{\"name\":" + json_text(name) + "}";
        if (post(on ? "/api/social/block" : "/api/social/unblock", body) / 100 == 2) {
            fprintf(stderr, "[SOCIAL] %s %s\n", on ? "blocked" : "unblocked", id ? std::to_string(id).c_str() : name.c_str());
            std::lock_guard<std::mutex> lk(g_mu);
            note(on ? "Blocked. You won't see each other's messages." : "Unblocked.");
        }
        load_friends();
        load_convos();
        load_global();
    });
}

void report(long long id, const std::string &channel, long long msg, const std::string &text,
            const std::vector<std::string> &context)
{
    queue([=]() {
        std::string body = "{\"account\":" + std::to_string(id) + ",\"channel\":" + json_text(channel) +
                           ",\"message\":" + std::to_string(msg) + ",\"reason\":\"player report\"";
        if (!text.empty()) body += ",\"text\":" + json_text(text);
        if (!context.empty()) {
            body += ",\"context\":[";
            for (size_t i = 0; i < context.size(); i++) body += (i ? "," : "") + json_text(context[i]);
            body += "]";
        }
        body += "}";
        if (post("/api/social/report", body) / 100 == 2) {
            fprintf(stderr, "[SOCIAL] reported %lld (%s, message %lld, %d lines of context)\n", id, channel.c_str(), msg,
                    (int)context.size());
            std::lock_guard<std::mutex> lk(g_mu);
            note("Reported. The team will look at it.");
        }
    });
}

void set_dms(const std::string &policy)
{
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_v.dms = policy == "friends" ? "" : policy;
        changed();
    }
    queue([policy]() {
        if (post("/api/social/settings", "{\"dms\":" + json_text(policy) + "}") / 100 == 2)
            fprintf(stderr, "[SOCIAL] messages from: %s\n", policy.c_str());
        load_friends();     // the service's own word on it (dms=)
    });
}

void set_show_avatar(bool on)
{
    long long me;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_v.show_avatar = on;
        me = g_v.me;
        changed();
    }
    queue([on, me]() {
        if (post("/api/me/settings", std::string("{\"show_avatar\":") + (on ? "true" : "false") + "}") / 100 == 2)
            fprintf(stderr, "[SOCIAL] show my Discord picture: %s\n", on ? "on" : "off");
        avatar_forget(me);
    });
}

void avatar_want(long long id)
{
    if (id <= 0) return;
    {
        std::lock_guard<std::mutex> lk(g_av_mu);
        if (!g_av_asked.insert(id).second) return;
        g_av_queue.push_back(id);
    }
    g_av_cv.notify_all();
}

std::vector<AvatarPixels> avatar_take()
{
    std::lock_guard<std::mutex> lk(g_av_mu);
    std::vector<AvatarPixels> out;
    out.swap(g_av_done);
    return out;
}

void avatar_forget(long long id)
{
    std::lock_guard<std::mutex> lk(g_av_mu);
    g_av_asked.erase(id);
    AvatarPixels px;        // id < 0: the overlay drops its copy of -id and asks again
    px.id = -id;
    g_av_done.push_back(px);
}

void set_mode_name(const std::string &name)
{
    std::lock_guard<std::mutex> lk(g_mu);
    g_mode_name = name;
}

}   // namespace social

// ---- the C side ------------------------------------------------------------------------------------------

using namespace social;

extern "C" void social_start(void)
{
    if (env_switch() < 0 || g_started.exchange(true)) return;
    g_threads[0] = CreateThread(nullptr, 0, work_thread, nullptr, 0, nullptr);
    g_threads[1] = CreateThread(nullptr, 0, poll_thread, nullptr, 0, nullptr);
    g_threads[2] = CreateThread(nullptr, 0, avatar_thread, nullptr, 0, nullptr);
    atexit(social_shutdown);
    fprintf(stderr, "[SOCIAL] started (RECOMP_SOCIAL %s)\n", env_switch() > 0 ? "on" : "unset: the service decides");
}

extern "C" void social_shutdown(void)
{
    if (!g_started || g_stop.exchange(true)) return;
    g_cv.notify_all();
    g_av_cv.notify_all();
    account_cancel_call(&g_poll_live);
    account_cancel_call(&g_work_live);
    HANDLE live[3];
    int n = 0;
    for (HANDLE h : g_threads) if (h) live[n++] = h;
    DWORD r = n ? WaitForMultipleObjects(n, live, TRUE, 3000) : WAIT_OBJECT_0;
    for (int i = 0; i < n; i++) CloseHandle(live[i]);
    g_threads[0] = g_threads[1] = g_threads[2] = nullptr;
    fprintf(stderr, "[SOCIAL] stopped%s\n", r == WAIT_TIMEOUT ? " (a request was still running)" : "");
}

extern "C" int social_enabled(void)
{
    int e = env_switch();
    if (e < 0 || !g_started) return 0;
    enum AccountState as = account_state();
    if (as != ACCOUNT_IN && as != ACCOUNT_CHECKING) return 0;
    return e > 0 || g_server.load() > 0;
}

extern "C" void social_set_typing(int on)
{
    g_typing_until = on ? GetTickCount64() + 200 : 0;
}

extern "C" int social_typing(void)
{
    return GetTickCount64() < g_typing_until.load();
}

#else   // Windows only for now, like the account

extern "C" void social_start(void) {}
extern "C" void social_shutdown(void) {}
extern "C" int social_enabled(void) { return 0; }
extern "C" void social_set_typing(int on) { (void)on; }
extern "C" int social_typing(void) { return 0; }
namespace social {
bool snapshot(View &, unsigned) { return false; }
std::vector<Toast> take_toasts() { return {}; }
std::string region_of_server(int) { return ""; }
void send_global(const char *, const std::string &) {}
void send_dm(long long, const std::string &) {}
void send_crew(const std::string &) {}
void friend_request(long long, const std::string &) {}
void friend_accept(long long) {}
void friend_decline(long long) {}
void friend_remove(long long) {}
void mute(long long, bool) {}
void open_thread(long long) {}
void mark_read(long long) {}
void crew_join(long long) {}
void crew_dismiss(long long) {}
void reload() {}
void block(long long, const std::string &, bool) {}
void report(long long, const std::string &, long long, const std::string &, const std::vector<std::string> &) {}
void set_dms(const std::string &) {}
void set_show_avatar(bool) {}
void avatar_want(long long) {}
std::vector<AvatarPixels> avatar_take() { return {}; }
void avatar_forget(long long) {}
void set_mode_name(const std::string &) {}
}
#endif
