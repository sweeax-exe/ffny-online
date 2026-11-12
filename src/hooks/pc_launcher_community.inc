// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The Content page's Community tab (included by pc_launcher_content.inc): every pack players submitted, also
// the ones still in review, as the account service's gallery shows them (GET /api/gallery, server repository
// docs/content-catalogue.md; FFNY Studio's Community view reads the same). View only: an unaccepted pack has
// pictures and text, never a download; accepted ones arrive by the sync. Signed-in players vote, one vote
// per account and pack. The network work runs on a thread of its own; the pictures become textures here,
// through the front end's PNG loader. Each card says "Single" or "Pack (N mods)" (the gallery's form and
// item_count; a missing form is a single); a pack's page lists its mods (items: title, type).

#include <thread>

namespace community {

struct Item {
    std::string id, title, type, kind;
};

struct Card {
    std::string id, sid, version, title, author, kind, type, status, preview;
    bool bundle = false;                // form "bundle": a pack of several mods
    int item_count = 1;
    std::vector<Item> items;            // its mods (bundle only)
    int votes = 0;
    bool my_vote = false;
    // the page (GET /api/gallery/{id}), once asked
    bool page_loaded = false;
    std::string description, changelog, reason, live_version;
    std::vector<uint8_t> png;           // the preview's bytes until the main thread makes a texture
    ImTextureID tex = 0;
    int tex_w = 0, tex_h = 0;
    bool tex_tried = false;
};

// A small JSON reader for the gallery's answers (objects, arrays, strings, numbers, true/false/null).
struct J {
    enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
    double n = 0;
    bool b = false;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;
    const J &operator[](const char *key) const
    {
        static const J none;
        for (auto &kv : o) if (kv.first == key) return kv.second;
        return none;
    }
    std::string str() const { return kind == STR ? s : std::string(); }
    int num() const { return kind == NUM ? (int)n : 0; }
};

struct Reader {
    const char *p, *e;
    int depth = 0;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    bool string(std::string &out)
    {
        if (p >= e || *p != '"') return false;
        p++;
        while (p < e && *p != '"') {
            char c = *p++;
            if (c != '\\') { out += c; continue; }
            if (p >= e) return false;
            c = *p++;
            if (c == 'n') out += '\n';
            else if (c == 't') out += '\t';
            else if (c == 'r') {}
            else if (c == 'b' || c == 'f') {}
            else if (c == 'u') {
                if (e - p < 4) return false;
                unsigned cp = (unsigned)strtoul(std::string(p, 4).c_str(), nullptr, 16);
                p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    unsigned lo = (unsigned)strtoul(std::string(p + 2, 4).c_str(), nullptr, 16);
                    p += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                else { out += (char)(0xF0 | cp >> 18); out += (char)(0x80 | (cp >> 12 & 0x3F)); out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
            } else out += c;
        }
        if (p >= e) return false;
        p++;
        return true;
    }
    bool value(J &v)
    {
        ws();
        if (p >= e || ++depth > 32) return false;
        bool ok = true;
        if (*p == '{') {
            v.kind = J::OBJ; p++; ws();
            if (p < e && *p == '}') p++;
            else for (;;) {
                std::string key; J item;
                ws();
                if (!string(key)) { ok = false; break; }
                ws();
                if (p >= e || *p++ != ':' || !value(item)) { ok = false; break; }
                v.o.emplace_back(std::move(key), std::move(item));
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') { p++; break; }
                ok = false; break;
            }
        } else if (*p == '[') {
            v.kind = J::ARR; p++; ws();
            if (p < e && *p == ']') p++;
            else for (;;) {
                J item;
                if (!value(item)) { ok = false; break; }
                v.a.push_back(std::move(item));
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') { p++; break; }
                ok = false; break;
            }
        } else if (*p == '"') {
            v.kind = J::STR; ok = string(v.s);
        } else if (e - p >= 4 && !strncmp(p, "true", 4)) { v.kind = J::BOOL; v.b = true; p += 4; }
        else if (e - p >= 5 && !strncmp(p, "false", 5)) { v.kind = J::BOOL; p += 5; }
        else if (e - p >= 4 && !strncmp(p, "null", 4)) { p += 4; }
        else {
            char *end = nullptr;
            std::string num(p, (size_t)std::min<ptrdiff_t>(e - p, 40));
            v.kind = J::NUM; v.n = strtod(num.c_str(), &end);
            if (end == num.c_str()) ok = false;
            p += end - num.c_str();
        }
        depth--;
        return ok;
    }
};

static bool parse(const char *text, int len, J &out)
{
    Reader r{text, text + len};
    return r.value(out);
}

static std::mutex g_lock;
static std::vector<Card> g_cards;            // under g_lock
static std::string g_status;                 // under g_lock
static int g_sort = 0;                       // 0 most wanted, 1 newest
static bool g_busy, g_loaded;                // under g_lock
static int g_selected = -1;
static UiPngLoader g_loader;

static void set_status(const std::string &text)
{
    std::lock_guard<std::mutex> hold(g_lock);
    g_status = text;
}

// One GET: with the session when signed in (the answer then says my_vote), without one otherwise.
static int get(const std::string &path, std::vector<char> &buf, int *len)
{
    if (account_state() == ACCOUNT_IN) {
        int status = account_call("GET", path.c_str(), nullptr, buf.data(), (int)buf.size(), 0, nullptr);
        if (status > 0) { *len = (int)strlen(buf.data()); return status; }
    }
    return account_fetch(path.c_str(), buf.data(), (int)buf.size(), len);
}

// form, item_count and items of a gallery card or page (a single when the service does not say)
static void form_from(const J &j, Card &c)
{
    if (j["form"].kind == J::STR) c.bundle = j["form"].s == "bundle";
    if (j["items"].kind == J::ARR) {
        c.items.clear();
        for (auto &row : j["items"].a) {
            if (row.kind != J::OBJ || c.items.size() >= 32) continue;
            c.items.push_back({row["id"].str(), row["title"].str(), row["type"].str(), row["kind"].str()});
        }
    }
    if (j["item_count"].kind == J::NUM) c.item_count = std::max(1, std::min(j["item_count"].num(), 99));
    else if (!c.items.empty()) c.item_count = (int)c.items.size();
}

static std::string form_label(const Card &c)
{
    return c.bundle ? "Pack (" + std::to_string(c.item_count) + " mods)" : std::string("Single");
}

static void card_from(const J &j, Card &c)
{
    c.id = j["id"].str(); c.sid = j["submission_id"].str(); c.version = j["version"].str();
    c.title = j["title"].str(); c.author = j["author_name"].str(); c.kind = j["kind"].str();
    c.type = j["type"].str(); c.status = j["status"].str(); c.preview = j["preview_url"].str();
    c.votes = j["votes"].num(); c.my_vote = j["my_vote"].kind == J::BOOL && j["my_vote"].b;
    form_from(j, c);
}

static void load_list()
{
    std::vector<char> buf(512 * 1024);
    int len = 0;
    std::string path = std::string("/api/gallery?sort=") + (g_sort ? "new" : "votes") + "&page=1&per=24";
    int status = get(path, buf, &len);
    J doc;
    std::vector<Card> cards;
    if (status != 200 || !parse(buf.data(), len, doc) || doc["packs"].kind != J::ARR) {
        std::lock_guard<std::mutex> hold(g_lock);
        g_status = status == 404 ? "The community gallery is not open yet." : "Could not reach the community gallery.";
        g_busy = false;
        return;
    }
    for (auto &row : doc["packs"].a) {
        Card c;
        card_from(row, c);
        if (!c.id.empty()) cards.push_back(std::move(c));
    }
    {
        std::lock_guard<std::mutex> hold(g_lock);
        for (auto &old : g_cards)       // keep textures already made for the same pictures
            for (auto &c : cards)
                if (c.id == old.id && c.sid == old.sid && old.tex) { c.tex = old.tex; c.tex_w = old.tex_w; c.tex_h = old.tex_h; c.tex_tried = true; }
        g_cards = cards;
        g_status = cards.empty() ? "No packs submitted yet. Make one in FFNY Studio." :
                   std::to_string(cards.size()) + " pack(s). Packs in review are view only; accepted ones install by themselves.";
        g_loaded = true;
    }
    // the pictures, after the list shows
    std::vector<char> pic(4 * 1024 * 1024 + 1);
    for (auto &c : cards) {
        // the service gives a full address on itself; the request goes to the same service, so its path
        size_t scheme = c.preview.find("://");
        if (scheme != std::string::npos) {
            size_t slash = c.preview.find('/', scheme + 3);
            c.preview = slash == std::string::npos ? std::string() : c.preview.substr(slash);
        }
        if (c.tex || c.preview.empty() || c.preview[0] != '/') continue;
        int got = 0;
        if (account_fetch(c.preview.c_str(), pic.data(), (int)pic.size(), &got) != 200 || got <= 8) continue;
        std::lock_guard<std::mutex> hold(g_lock);
        for (auto &card : g_cards)
            if (card.id == c.id && card.sid == c.sid) card.png.assign(pic.begin(), pic.begin() + got);
    }
    std::lock_guard<std::mutex> hold(g_lock);
    g_busy = false;
}

static void load_page(std::string id)
{
    std::vector<char> buf(256 * 1024);
    int len = 0;
    J doc;
    if (get("/api/gallery/" + id, buf, &len) != 200 || !parse(buf.data(), len, doc)) return;
    std::lock_guard<std::mutex> hold(g_lock);
    for (auto &c : g_cards) {
        if (c.id != id) continue;
        c.description = doc["description"].str(); c.changelog = doc["changelog"].str();
        c.reason = doc["reason"].str(); c.live_version = doc["live_version"].str();
        c.votes = doc["votes"].num(); c.my_vote = doc["my_vote"].kind == J::BOOL && doc["my_vote"].b;
        form_from(doc, c);
        c.page_loaded = true;
    }
}

static void vote(std::string id, bool on)
{
    char buf[1024];
    int status = account_call(on ? "POST" : "DELETE", ("/api/gallery/" + id + "/vote").c_str(), on ? "{}" : nullptr, buf,
                              (int)sizeof buf, 0, nullptr);
    J doc;
    if (status != 200 || !parse(buf, (int)strlen(buf), doc)) {
        set_status(status == -1 ? "Sign in (the Account card) to vote." : "The vote did not go through; try again.");
        return;
    }
    std::lock_guard<std::mutex> hold(g_lock);
    for (auto &c : g_cards)
        if (c.id == id) { c.votes = doc["votes"].num(); c.my_vote = doc["my_vote"].kind == J::BOOL && doc["my_vote"].b; }
    g_status = on ? "Your vote is in. The team sees which packs players want most." : "Your vote is taken back.";
}

static void start(void (*fn)(std::string, bool), std::string id, bool on) { std::thread(fn, id, on).detach(); }

static void refresh()
{
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (g_busy) return;
        g_busy = true;
        g_status = "Loading the community gallery...";
    }
    std::thread(load_list).detach();
}

static const char *status_word(const std::string &status)
{
    if (status == "live") return "ACCEPTED";
    if (status == "rejected") return "REJECTED";
    return "IN REVIEW";
}

static ImVec4 status_color(const std::string &status)
{
    if (status == "live") return k_green;
    if (status == "rejected") return k_error;
    return k_gold;
}

static void forget_textures()
{
    std::lock_guard<std::mutex> hold(g_lock);
    for (auto &c : g_cards) { c.tex = 0; c.tex_tried = false; }
}

} // namespace community

void ui_set_png_loader(UiPngLoader loader)
{
    if (community::g_loader && loader != community::g_loader) community::forget_textures();
    community::g_loader = loader;
}

static std::string community_status()
{
    std::lock_guard<std::mutex> hold(community::g_lock);
    return community::g_status;
}

static void community_view(float lx, float top, float inner, float height, float s)
{
    using namespace community;
    static bool asked;
    if (!asked) { asked = true; refresh(); }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // sort and refresh, right of the tabs
    {
        float bx = lx + inner - 3 * 118 * s - 16 * s;
        if (glass_button("##gal_votes", "Most wanted", ImVec2(bx, top - 48 * s), ImVec2(118 * s, 34 * s), s,
                         g_sort == 0 ? GLASS_RED : GLASS_PLAIN, bold_font(), 13.5f * s) && g_sort != 0) { g_sort = 0; refresh(); }
        if (glass_button("##gal_new", "Newest", ImVec2(bx + 126 * s, top - 48 * s), ImVec2(118 * s, 34 * s), s,
                         g_sort == 1 ? GLASS_RED : GLASS_PLAIN, bold_font(), 13.5f * s) && g_sort != 1) { g_sort = 1; refresh(); }
        if (glass_button("##gal_refresh", "Refresh", ImVec2(bx + 252 * s, top - 48 * s), ImVec2(118 * s, 34 * s), s,
                         GLASS_PLAIN, bold_font(), 13.5f * s))
            refresh();
    }
    std::lock_guard<std::mutex> hold(g_lock);
    // textures for pictures that came in
    for (auto &c : g_cards) {
        if (!c.tex && !c.tex_tried && !c.png.empty() && g_loader) {
            c.tex_tried = true;
            c.tex = g_loader(c.png.data(), (int)c.png.size(), &c.tex_w, &c.tex_h);
            c.png.clear();
        }
    }
    if (g_selected >= (int)g_cards.size()) g_selected = -1;
    if (g_selected < 0 && !g_cards.empty()) g_selected = 0;

    // the list, left
    float list_w = 330 * s;
    ImGui::SetCursorScreenPos(ImVec2(lx, top));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##gal_list", ImVec2(list_w, height), ImGuiChildFlags_None);
    ImDrawList *ld = ImGui::GetWindowDrawList();
    float y = ImGui::GetCursorScreenPos().y;
    for (int i = 0; i < (int)g_cards.size(); i++) {
        Card &c = g_cards[i];
        ImVec2 ra(lx, y), rb(lx + list_w - 14 * s, y + 64 * s);
        ImGui::SetCursorScreenPos(ra);
        char id[96];
        snprintf(id, sizeof id, "##gal_card_%d", i);
        if (ImGui::InvisibleButton(id, ImVec2(rb.x - ra.x, rb.y - ra.y))) {
            g_selected = i;
            if (!c.page_loaded) std::thread(load_page, c.id).detach();
        }
        bool hot = ImGui::IsItemHovered();
        ld->AddRectFilled(ra, rb, col(k_white, i == g_selected ? 0.10f : hot ? 0.07f : 0.04f), 10 * s);
        if (i == g_selected) ld->AddRect(ra, rb, col(k_red_hi, 0.8f), 10 * s);
        float tx = ra.x + 12 * s;
        if (c.tex) {
            float th = 48 * s, tw = c.tex_h > 0 ? std::min(th * c.tex_w / c.tex_h, 72 * s) : th;
            ld->AddImageRounded(c.tex, ImVec2(ra.x + 8 * s, ra.y + 8 * s), ImVec2(ra.x + 8 * s + tw, ra.y + 8 * s + th),
                                ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6 * s);
            tx = ra.x + 16 * s + tw;
        }
        ld->PushClipRect(ra, ImVec2(rb.x - 70 * s, rb.y), true);
        ld->AddText(bold_font(), 15 * s, ImVec2(tx, ra.y + 9 * s), col(k_white), c.title.c_str());
        std::string form = form_label(c);
        float fw = content_tag(ld, ImVec2(tx, ra.y + 32 * s), form.c_str(), s, c.bundle ? k_gold : ImVec4(1, 1, 1, 0.75f));
        std::string sub = (c.bundle ? std::string() : std::string(content_type_label(c.type.c_str())) + "  \xC2\xB7  ") +
                          c.author;
        ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(tx + fw + 8 * s, ra.y + 34 * s), col(k_white, 0.6f), sub.c_str());
        ld->PopClipRect();
        ld->AddText(bold_font(), 11.5f * s, ImVec2(rb.x - 66 * s, ra.y + 10 * s), col(status_color(c.status)), status_word(c.status));
        char votes[32];
        snprintf(votes, sizeof votes, "%d vote%s", c.votes, c.votes == 1 ? "" : "s");
        ld->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(rb.x - 66 * s, ra.y + 34 * s), col(k_white, c.my_vote ? 1.0f : 0.6f), votes);
        y += 72 * s;
    }
    ImGui::SetCursorScreenPos(ImVec2(lx, y));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // the page, right
    if (g_selected < 0) return;
    Card &c = g_cards[g_selected];
    if (!c.page_loaded) {
        static std::string asked_for;
        if (asked_for != c.id + c.sid) { asked_for = c.id + c.sid; std::thread(load_page, c.id).detach(); }
    }
    float px = lx + list_w + 12 * s, pw = inner - list_w - 12 * s, py = top;
    ImVec2 pa(px, py), pb(px + pw, py + height);
    dl->AddRectFilled(pa, pb, col(k_white, 0.04f), 12 * s);
    float x = px + 18 * s;
    py += 14 * s;
    if (c.tex) {
        float iw = pw - 36 * s, ih = c.tex_w > 0 ? iw * c.tex_h / c.tex_w : 0;
        if (ih > 190 * s) { ih = 190 * s; iw = c.tex_h > 0 ? ih * c.tex_w / c.tex_h : iw; }
        dl->AddImageRounded(c.tex, ImVec2(x, py), ImVec2(x + iw, py + ih), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8 * s);
        py += ih + 12 * s;
    }
    dl->PushClipRect(pa, ImVec2(pb.x, pb.y - 64 * s), true);      // the text stops above the vote
    dl->AddText(display_font(), 22 * s, ImVec2(x, py), col(k_white), c.title.c_str());
    py += 30 * s;
    std::string meta = (c.bundle ? form_label(c) : "Single  \xC2\xB7  " + std::string(content_type_label(c.type.c_str()))) +
                       "  \xC2\xB7  " + (c.kind == "content" ? "new content" : "replacement") + "  \xC2\xB7  v" + c.version +
                       "  \xC2\xB7  by " + c.author;
    dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(x, py), col(k_white, 0.65f), meta.c_str());
    py += 22 * s;
    std::string state = std::string(status_word(c.status)) +
                        (c.status == "live" ? ": installed for every player by the sync." :
                         c.status == "rejected" ? (c.reason.empty() ? std::string(".") : ": " + c.reason) :
                                                  ": the team reviews it. View only until it is accepted.");
    float wrap = pw - 36 * s;
    dl->AddText(bold_font(), 13.5f * s, ImVec2(x, py), col(status_color(c.status)), state.c_str(), nullptr, wrap);
    py += text_size(bold_font(), 13.5f * s, state.c_str(), wrap).y + 10 * s;
    if (!c.description.empty()) {
        dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(x, py), col(k_white, 0.85f), c.description.c_str(), nullptr, wrap);
        py += text_size(ImGui::GetFont(), 13 * s, c.description.c_str(), wrap).y + 8 * s;
    }
    if (c.bundle && !c.items.empty()) {
        // the pack's mods, one line each: title and type
        dl->AddText(bold_font(), 13 * s, ImVec2(x, py), col(k_white, 0.85f), "Its mods:");
        py += 20 * s;
        for (auto &item : c.items) {
            std::string text = "\xE2\x80\xA2  " + (item.title.empty() ? item.id : item.title) + "  \xC2\xB7  " +
                               content_type_label(item.type.c_str());
            dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(x + 6 * s, py), col(k_white, 0.8f), text.c_str());
            py += 19 * s;
        }
        py += 6 * s;
    }
    if (!c.changelog.empty()) {
        std::string text = "Changes: " + c.changelog;
        dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(x, py), col(k_white, 0.6f), text.c_str(), nullptr, wrap);
    }
    dl->PopClipRect();
    // the vote, bottom right of the page
    {
        bool signed_in = account_state() == ACCOUNT_IN;
        char label[48];
        snprintf(label, sizeof label, c.my_vote ? "Voted (%d)" : "Vote (%d)", c.votes);
        ImVec2 va(pb.x - 18 * s - 150 * s, pb.y - 14 * s - 36 * s);
        if (glass_button("##gal_vote", label, va, ImVec2(150 * s, 36 * s), s, c.my_vote ? GLASS_RED : GLASS_PLAIN, bold_font(),
                         14 * s, signed_in))
            start(vote, c.id, !c.my_vote);
        if (!signed_in)
            dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(x, va.y + 10 * s), col(k_white, 0.55f), "Sign in to vote.");
    }
}
