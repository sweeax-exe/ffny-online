// The chat window of the online screens (the Social Update, stage 2a; docs in the private server
// repository, the owner's approved design of 2026-11-03): tabs ROOM / GLOBAL / FRIENDS / MESSAGES /
// CREW, a PlayStation-style on-screen keyboard for the pad, the friends list with online status, and
// toasts for new private messages and friend requests. The network side is social.cpp.
//
// Included by online_screen.cpp inside its anonymous namespace (its helpers, fonts and the 640x480
// frame). Drawn only while social_enabled(): until the service's switch is on (or RECOMP_SOCIAL=on in
// a test) nothing here shows and ENTER keeps choosing as before.
//
// Opens with ENTER or the pad's BACK (not when BACK is the push-to-talk button) on every online page;
// B or ESC closes it. T is push-to-talk and is never bound here; while the text box has the keyboard,
// voice.c leaves a letter key bound to push-to-talk alone (social_typing).

enum ChatTab { TAB_ROOM, TAB_GLOBAL, TAB_FRIENDS, TAB_MESSAGES, TAB_CREW, TAB_COUNT };
const char *const k_tab_names[TAB_COUNT] = { "ROOM", "GLOBAL", "FRIENDS", "MESSAGES", "CREW" };
const char *const k_tab_shots[TAB_COUNT] = { "social-room", "social-global", "social-friends", "social-messages", "social-crew" };
const char *const k_region_ids[4] = { "", "eu", "us-east", "us-west" };
const char *const k_region_names[4] = { "ALL SERVERS", "EUROPE", "US EAST", "US WEST" };

const ImU32 GOLD_TEXT = IM_COL32(255, 205, 60, 255), BLUE_NAME = IM_COL32(142, 199, 255, 255),
            LIVE = IM_COL32(123, 224, 123, 255), DIM = IM_COL32(242, 230, 200, 115),
            BUBBLE_ME = IM_COL32(142, 16, 36, 255), LINE_SEP = IM_COL32(255, 255, 255, 20);

enum KbKind { KB_CHAT, KB_FRIEND, KB_CREW_INVITE, KB_CREW_NAME, KB_CREW_TAG, KB_FIGHT };
enum FriendAct { ACT_ACCEPT, ACT_DECLINE, ACT_JOIN, ACT_WATCH, ACT_INVITE, ACT_MESSAGE, ACT_REMOVE, ACT_SURE };

struct Chat {
    bool open = false;
    int tab = TAB_GLOBAL;
    social::View v;
    bool have = false;
    std::string draft[TAB_COUNT];
    int cursor[TAB_COUNT] = {};
    int sel[TAB_COUNT] = { -1, -1, 0, 0, -1 };
    int region = 0;                 // GLOBAL: 0 every server, 1..3 one region
    int btn = 0;                    // FRIENDS: the button of the selected row
    float scroll_y = 0;             // FRIENDS: how far the list is scrolled (menu units)
    int line_skip = 0;              // GLOBAL / CREW: the newest lines scrolled past
    long long dm_with = 0;          // MESSAGES: the conversation shown
    long long confirm_remove = 0;   // FRIENDS: REMOVE pressed once on this friend
    // the on-screen keyboard
    bool kb = false;
    int kb_kind = KB_CHAT;
    int kr = 2, kc = 7;
    bool shift = false, caps = false, symbols = false;
    unsigned long long last_dot = 0;
    std::string friend_name;
    int friend_cursor = 0;
    // the player menu (A on a chat line or a room member)
    bool menu = false;
    long long menu_id = 0;
    std::string menu_name;
    int menu_sel = 0;
    std::vector<social::Toast> toasts;
    unsigned long long opened_at = 0, ready_at = 0;
    bool mouse_off = false;         // the page under the chat takes no mouse
    bool opened_once = false;
    int popup_frame = -1;           // the keyboard or the menu opened in this frame: its press is not theirs
    // ROOM (stage 2b): the room's chat through the relay (netplay.c), the last line seen, local mutes
    unsigned room_seen = 0;
    std::vector<std::string> room_muted;        // by name (the room's list names players by name)
    std::vector<unsigned> room_muted_ids;       // by account, when the relay named one
    bool menu_room = false;                     // the player menu opened from the ROOM tab
    unsigned menu_account = 0;
    // REPORT (stage 2c): the line the menu was opened on, and the lines before it (room lines only
    // this PC saw: their text goes with the report)
    std::string menu_channel, menu_text;
    long long menu_msg = 0;
    std::vector<std::string> menu_context;
    // CREW (stage 2c): the page or its chat, LEAVE pressed once, the menu on a crew mate, a new crew's name
    int crew_view = 0;
    bool confirm_leave = false;
    bool menu_crew = false;
    std::string menu_role, crew_new_name;
} C;

bool chat_mouse_blocked() { return C.mouse_off; }

// ---- avatars (stage 2c): the players' Discord pictures, as the account service copied them ----
//
// social.cpp downloads and decodes them on a thread of its own; here they become textures on the
// overlay's thread, at most AVATAR_CAP of them (the least recently drawn goes first). A player without
// a picture (none, hidden, or not loaded yet) gets initials on a circle in a colour of their account.

const size_t AVATAR_CAP = 256;
struct AvatarTex { Tex tex; bool none = false; unsigned long long used = 0, asked = 0; };
std::map<long long, AvatarTex> g_avatars;
int g_avatar_textures = 0;          // tests: how many pictures became textures

void avatar_drop(std::map<long long, AvatarTex>::iterator it)
{
    if (it->second.tex.srv) { it->second.tex.srv->Release(); g_avatar_textures--; }
    g_avatars.erase(it);
}

// Once a frame: the pictures that arrived become textures.
void avatars_update()
{
    for (auto &px : social::avatar_take()) {
        if (px.id < 0) {        // forgotten (the player changed their setting): ask again
            auto it = g_avatars.find(-px.id);
            if (it != g_avatars.end()) avatar_drop(it);
            continue;
        }
        AvatarTex &a = g_avatars[px.id];
        a.asked = GetTickCount64();
        if (a.tex.srv) { a.tex.srv->Release(); a.tex.srv = nullptr; g_avatar_textures--; }
        a.none = !px.ok || !make_texture(px.rgba, px.w, px.h, a.tex);
        if (!a.none) g_avatar_textures++;
    }
    while (g_avatars.size() > AVATAR_CAP) {
        auto oldest = g_avatars.begin();
        for (auto it = g_avatars.begin(); it != g_avatars.end(); ++it) if (it->second.used < oldest->second.used) oldest = it;
        avatar_drop(oldest);
    }
}

// A colour of the account: the same on every PC.
ImU32 avatar_colour(long long id, const std::string &name)
{
    unsigned h = (unsigned)(id * 2654435761u);
    if (!id) for (unsigned char c : name) h = h * 31 + c;
    float hue = (h % 360) / 360.0f, r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.62f, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

std::string initials(const std::string &name)
{
    std::string out;
    bool start = true;
    for (size_t i = 0; i < name.size() && out.size() < 2; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == ' ' || c == '_' || c == '.' || c == '-') { start = true; continue; }
        if (start && c < 0x80 && isalnum(c)) out += (char)toupper(c);
        start = false;
    }
    if (out.empty() && !name.empty() && (unsigned char)name[0] < 0x80) out = (char)toupper((unsigned char)name[0]);
    return out.empty() ? "?" : out;
}

// A round picture of radius r (menu units) centred at cx, cy; id 0 (a name only): initials.
void draw_avatar(ImDrawList *dl, const Frame &f, float cx, float cy, float r, long long id, const std::string &name,
                 float alpha = 1.0f)
{
    unsigned long long now = GetTickCount64();
    AvatarTex *a = nullptr;
    if (id > 0 && social_enabled()) {      // the social features off: initials only, nothing is asked
        a = &g_avatars[id];
        a->used = now;
        // none: ask again after ten minutes (a player may turn theirs on)
        if (!a->tex.srv && (!a->asked || (a->none && now - a->asked > 600000))) {
            a->asked = now;
            a->none = false;
            social::avatar_want(id);
        }
    }
    ImVec2 c = f.p(cx, cy);
    float rs = r * f.s();
    if (a && a->tex.srv) {
        dl->AddImageRounded((ImTextureID)(intptr_t)a->tex.srv, ImVec2(c.x - rs, c.y - rs), ImVec2(c.x + rs, c.y + rs),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(255 * alpha)), rs);
    } else {
        dl->AddCircleFilled(c, rs, with_alpha(avatar_colour(id, name), alpha), 24);
        std::string ini = initials(name);
        float size = r * (ini.size() > 1 ? 0.95f : 1.15f);
        text(dl, f, g_title, size, cx, cy - size * 0.62f, with_alpha(IM_COL32(255, 245, 230, 255), alpha), ini.c_str(), CENTER, 0);
    }
    dl->AddCircle(c, rs, with_alpha(IM_COL32(255, 255, 255, 40), alpha), 24, 0.8f * f.s());
}

// ---- room chat (stage 2b): relay-only, so it works with the service's social switch off ----

// -1 no room chat here (no room, or a relay without it), 0 read only, 1 can write.
int room_chat_state(std::string *why = nullptr)
{
    char w[160];
    int s = netplay_room_chat_can_write(w, sizeof(w));
    if (why) *why = w;
    return s;
}

// Only the ROOM tab: the service's social features are off (or not ready), room chat is on.
bool room_only() { return !social_enabled(); }

bool room_muted(const NetChatLine &l)
{
    if (l.system || l.mine) return false;
    for (auto &n : C.room_muted) if (n == l.name) return true;
    for (unsigned id : C.room_muted_ids) if (id && id == l.account) return true;
    if (l.account && !room_only()) {    // muted or blocked in the social features too
        for (auto &m : C.v.muted) if (m.id == (long long)l.account) return true;
        for (auto &b : C.v.blocked) if (b.id == (long long)l.account) return true;
    }
    return false;
}

// The account behind a name in the room (its room lines carry it), else a friend's or crew mate's; 0
// unknown (the picture is then initials).
long long room_account_of(const std::string &name)
{
    static NetChatLine buf[NETPLAY_CHAT_LINES];
    int n = netplay_room_chat(buf, NETPLAY_CHAT_LINES, nullptr);
    for (int i = n - 1; i >= 0; i--) if (buf[i].account && !buf[i].system && name == buf[i].name) return buf[i].account;
    for (auto &fr : C.v.friends) if (fr.name == name) return fr.id;
    for (auto &m : C.v.crew_roster) if (m.name == name) return m.id;
    if (name == C.v.my_name) return C.v.me;
    return 0;
}

void room_mute(const std::string &name, unsigned account, bool on)
{
    auto &ns = C.room_muted;
    auto &is = C.room_muted_ids;
    ns.erase(std::remove(ns.begin(), ns.end(), name), ns.end());
    if (account) is.erase(std::remove(is.begin(), is.end(), account), is.end());
    if (on) {
        ns.push_back(name);
        if (account) is.push_back(account);
    }
    fprintf(stderr, "[SOCIAL-UI] room chat: %s %s\n", on ? "muted" : "unmuted", name.c_str());
}

bool room_name_muted(const std::string &name)
{
    for (auto &n : C.room_muted) if (n == name) return true;
    return false;
}

// The room's lines as the chat's lines (oldest first); *last: the newest one's seq.
std::vector<social::Line> room_lines(unsigned *last = nullptr)
{
    static NetChatLine buf[NETPLAY_CHAT_LINES];
    unsigned seq = 0;
    int n = netplay_room_chat(buf, NETPLAY_CHAT_LINES, &seq);
    if (last) *last = seq;
    std::vector<social::Line> out;
    for (int i = 0; i < n; i++) {
        social::Line l;
        l.seq = buf[i].seq;
        l.from = buf[i].account;
        l.at = buf[i].unix_at;
        l.channel = "room";
        l.name = buf[i].name;
        l.text = buf[i].text;
        l.system = buf[i].system != 0;
        l.mine = buf[i].mine != 0;
        l.hidden = room_muted(buf[i]);
        static unsigned logged;     // each hidden line once in the log (tests)
        if (l.hidden && buf[i].seq > logged) {
            logged = buf[i].seq;
            fprintf(stderr, "[SOCIAL-UI] room chat: line hidden (muted): %s\n", buf[i].name);
        }
        out.push_back(l);
    }
    return out;
}

// Lines of others not seen yet on the ROOM tab.
int room_unread()
{
    static NetChatLine buf[NETPLAY_CHAT_LINES];
    unsigned seq = 0;
    int n = netplay_room_chat(buf, NETPLAY_CHAT_LINES, &seq), u = 0;
    for (int i = 0; i < n; i++)
        if (buf[i].seq > C.room_seen && !buf[i].mine && !buf[i].system && !buf[i].history && !room_muted(buf[i])) u++;
    return u;
}

// ---- text helpers ----

int utf8_runes(const std::string &s)
{
    int n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

int utf8_prev(const std::string &s, int i)
{
    if (i <= 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

int utf8_next(const std::string &s, int i)
{
    if (i >= (int)s.size()) return (int)s.size();
    i++;
    while (i < (int)s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

void utf8_put(std::string &out, unsigned cp)
{
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

void insert_text(std::string &s, int &cur, const std::string &add, int max_runes)
{
    cur = std::max(0, std::min(cur, (int)s.size()));
    if (utf8_runes(s) + utf8_runes(add) > max_runes) return;
    s.insert((size_t)cur, add);
    cur += (int)add.size();
}

void erase_back(std::string &s, int &cur)
{
    cur = std::max(0, std::min(cur, (int)s.size()));
    if (!cur) return;
    int p = utf8_prev(s, cur);
    s.erase((size_t)p, (size_t)(cur - p));
    cur = p;
}

// The characters typed on the keyboard this frame, as UTF-8 (printable only).
std::string typed_chars()
{
    std::string out;
    ImGuiIO &io = ImGui::GetIO();
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        unsigned c = io.InputQueueCharacters[i];
        if (c >= 0x20 && c != 0x7F && (c < 0xD800 || c > 0xDFFF)) utf8_put(out, c);
    }
    return out;
}

std::string clock_text(long long unix_s)
{
    if (unix_s <= 0) return "";
    time_t t = (time_t)unix_s;
    struct tm lt;
    localtime_s(&lt, &t);
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d", lt.tm_hour, lt.tm_min);
    return b;
}

std::string ago_text(long long unix_s)
{
    if (unix_s <= 0) return "offline";
    long long d = (long long)time(nullptr) - unix_s;
    char b[48];
    if (d < 3600) return "last online less than an hour ago";
    if (d < 86400) { snprintf(b, sizeof(b), "last online %lld h ago", d / 3600); return b; }
    if (d < 2 * 86400) return "last online yesterday";
    snprintf(b, sizeof(b), "last online %lld days ago", d / 86400);
    return b;
}

std::string region_label(const std::string &relay)
{
    return relay == "eu" ? "Europe" : relay == "us-east" ? "US East" : relay == "us-west" ? "US West" : "";
}

int server_of_region(const std::string &relay)
{
    return relay == "eu" ? NETSERVER_EU : relay == "us-east" ? NETSERVER_NA : relay == "us-west" ? NETSERVER_WEST : -1;
}

// What a friend does, as the FRIENDS list says it, and its colour.
std::string friend_state(const social::Friend &fr, ImU32 *col)
{
    std::string where = region_label(fr.relay);
    *col = LIVE;
    if (fr.state == "offline") { *col = DIM; return ago_text(fr.last_online); }
    if (fr.state == "menu") return "In the menu";
    if (fr.state == "room") {
        std::string s = "In a room";
        if (!fr.mode.empty()) s += " - " + fr.mode;
        int a = 0, b = 0;
        if (sscanf(fr.seats.c_str(), "%d/%d", &a, &b) == 2) s += " - " + std::to_string(a) + " of " + std::to_string(b);
        return s;
    }
    *col = ORANGE;
    std::string s = fr.state == "fighting" ? "Fighting" : "Watching";
    if (!fr.mode.empty()) s += " - " + fr.mode;
    if (!where.empty()) s += " - " + where;
    return s;
}

float text_height(const Frame &f, ImFont *font, float size, const char *s, float wrap)
{
    font = font_or_default(font);
    return font->CalcTextSizeA(size * f.s(), FLT_MAX, wrap * f.sx, s).y / f.sy;
}

// A grey plate with a word on it (ENTER, L2, START), then its meaning, as the hints write it.
float plate_hint(ImDrawList *dl, const Frame &f, float x, float y, const char *plate, const char *word)
{
    float w = text_width(f, g_body, 10, plate) + 8;
    rect(dl, f, x - 2, y + 1, w, 14, IM_COL32(70, 70, 70, 255), 4);
    text(dl, f, g_body, 10, x - 2 + w / 2, y + 2, WHITE, plate, CENTER, 0);
    if (!word || !*word) return w + 4;
    text(dl, f, g_title, 13, x + w + 2, y + 1, WHITE, word, LEFT, 1.0f);
    return w + 4 + text_width(f, g_title, 13, word) + 12;
}

const char *back_button_name() { return ps_icons() ? "SHARE" : "BACK"; }
const char *start_button_name() { return ps_icons() ? "OPTIONS" : "START"; }
bool back_is_ptt() { return recomp_settings_get("online", "voice_pad", 0) == 1; }

// ---- the input of the chat window ----

struct ChatKeys { bool up, down, left, right, a, b, x, y, lb, rb, lt, rt, start, enter, esc, bksp, del, home, end_; };

ChatKeys chat_keys(bool typing)
{
    ChatKeys k = {};
    if (gallery_on()) return k;
    auto any = [](std::initializer_list<ImGuiKey> keys, bool repeat) {
        for (ImGuiKey key : keys) if (key != ImGuiKey_None && ImGui::IsKeyPressed(key, repeat)) return true;
        return false;
    };
    // While the text box takes typing, letters type: no WASD; Space types a space.
    auto letter = [&](ImGuiKey key) { return typing ? ImGuiKey_None : key; };
    bool shift = ImGui::GetIO().KeyShift;
    k.up = any({ ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, ImGuiKey_UpArrow, letter(ImGuiKey_W) }, true);
    k.down = any({ ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, ImGuiKey_DownArrow, letter(ImGuiKey_S) }, true);
    k.left = any({ ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, ImGuiKey_LeftArrow, letter(ImGuiKey_A) }, true);
    k.right = any({ ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, ImGuiKey_RightArrow, letter(ImGuiKey_D) }, true);
    k.a = any({ ImGuiKey_GamepadFaceDown, letter(ImGuiKey_Space), letter(ImGuiKey_K) }, false);
    k.b = any({ ImGuiKey_GamepadFaceRight, letter(ImGuiKey_L) }, false);
    k.x = any({ ImGuiKey_GamepadFaceLeft, letter(ImGuiKey_J) }, false);
    k.y = any({ ImGuiKey_GamepadFaceUp, letter(ImGuiKey_I) }, false);
    k.lb = any({ ImGuiKey_GamepadL1, ImGuiKey_PageUp }, true) || (ImGui::IsKeyPressed(ImGuiKey_Tab, true) && shift);
    k.rb = any({ ImGuiKey_GamepadR1, ImGuiKey_PageDown }, true) || (ImGui::IsKeyPressed(ImGuiKey_Tab, true) && !shift);
    k.lt = any({ ImGuiKey_GamepadL2 }, false);
    k.rt = any({ ImGuiKey_GamepadR2 }, false);
    k.start = any({ ImGuiKey_GamepadStart }, false);
    k.enter = any({ ImGuiKey_Enter, ImGuiKey_KeypadEnter }, false);
    k.esc = any({ ImGuiKey_Escape }, false);
    k.bksp = any({ ImGuiKey_Backspace }, true);
    k.del = any({ ImGuiKey_Delete }, true);
    k.home = any({ ImGuiKey_Home }, false);
    k.end_ = any({ ImGuiKey_End }, false);
    return k;
}

// The tab has a text box (and so takes typing).
bool tab_types(int tab)
{
    const social::View &v = C.v;
    if (tab == TAB_ROOM) return room_chat_state() == 1;
    if (tab == TAB_GLOBAL) return true;
    if (tab == TAB_CREW) return v.crew != 0 && C.crew_view == 1;     // the crew's chat, not its page
    if (tab == TAB_MESSAGES) {
        for (auto &t : v.threads) if (t.with == C.dm_with) return t.can_message;
        return C.dm_with != 0;
    }
    return false;
}

std::string convo_name(long long id)
{
    for (auto &c : C.v.convos) if (c.with.id == id) return c.with.name;
    for (auto &f : C.v.friends) if (f.id == id) return f.name;
    return "";
}

const social::Friend *friend_of(long long id)
{
    for (auto &f : C.v.friends) if (f.id == id) return &f;
    return nullptr;
}

void chat_send()
{
    int t = C.tab;
    std::string text = C.draft[t];
    while (!text.empty() && text.back() == ' ') text.pop_back();
    size_t lead = text.find_first_not_of(' ');
    if (lead == std::string::npos) return;
    text = text.substr(lead);
    if (t == TAB_GLOBAL) {
        std::string region = k_region_ids[C.region];
        if (region.empty()) region = social::region_of_server(connected_server());
        social::send_global(region.empty() ? "eu" : region.c_str(), text);
    } else if (t == TAB_MESSAGES && C.dm_with) {
        social::send_dm(C.dm_with, text);
    } else if (t == TAB_CREW && C.v.crew) {
        social::send_crew(text);
    } else if (t == TAB_ROOM) {
        if (text.size() > 200) {        // the relay's cap is 200 bytes: cut at a character's start
            size_t cut = 200;
            while (cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80) cut--;
            text.resize(cut);
        }
        if (!netplay_room_chat_send(text.c_str())) return;
        fprintf(stderr, "[SOCIAL-UI] room chat sent: %s\n", text.c_str());
    } else {
        return;
    }
    C.draft[t].clear();
    C.cursor[t] = 0;
    C.line_skip = 0;
    C.sel[t] = t == TAB_MESSAGES ? C.sel[t] : -1;
}

void open_dm(long long with)
{
    C.tab = TAB_MESSAGES;
    C.dm_with = with;
    social::open_thread(with);
    social::mark_read(with);
    for (size_t i = 0; i < C.v.convos.size(); i++)
        if (C.v.convos[i].with.id == with) C.sel[TAB_MESSAGES] = (int)i;
}

void join_friend(const social::Friend &fr, bool watch)
{
    if (fr.room.empty()) return;
    int server = server_of_region(fr.relay), here = connected_server();
    if (watch) {
        if (server >= 0 && server != here) { say("Watching works on your server only for now."); return; }
        netplay_lobby_spectate(fr.room.c_str());
        say("Watching " + fr.name + "...");
    } else if (server < 0 || server == here) {
        fprintf(stderr, "[SOCIAL-UI] joining %s's room %s\n", fr.name.c_str(), fr.room.c_str());
        netplay_lobby_join(fr.room.c_str());
        say("Joining " + fr.name + "...");
    } else {
        netservers_join_on(server, fr.room.c_str(), 0, hub_name().c_str());
        say("Joining " + fr.name + " on " + region_label(fr.relay) + "...");
    }
    C.open = false;
}

// ---- the window's frame: backdrop, title, panel, tabs ----

const float PX = 48, PY = 75, PW = 544, PH = 340, TABS_B = 108, BAR_Y = 373;

void chat_backdrop(ImDrawList *dl, ImVec2 display, float alpha)
{
    dl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(10, 6, 6, (int)(255 * alpha)));
    // the design's haze: a warm glow low right, a red one high left (many faint discs: a soft edge)
    for (int i = 0; i < 40; i++) {
        float k = 1.0f - i / 40.0f;
        dl->AddCircleFilled(ImVec2(display.x * 0.78f, display.y * 0.62f), display.y * 0.60f * k,
                            IM_COL32(214, 92, 24, (int)(3 * alpha)), 64);
        dl->AddCircleFilled(ImVec2(display.x * 0.20f, display.y * 0.30f), display.y * 0.50f * k,
                            IM_COL32(160, 20, 30, (int)(2.5f * alpha)), 64);
    }
}

void chat_panel(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, float alpha = 0.66f)
{
    glass(dl, f, x, y, w, h, 8, IM_COL32(18, 12, 14, 255), alpha);
}

void badge(ImDrawList *dl, const Frame &f, float x, float y, int n)
{
    char b[12];
    snprintf(b, sizeof(b), "%d", std::min(n, 99));
    float w = std::max(11.0f, text_width(f, g_body, 8.5f, b) + 7);
    rect(dl, f, x, y, w, 11, RED, 5.5f);
    text(dl, f, g_body, 8.5f, x + w / 2, y + 0.5f, IM_COL32(255, 245, 230, 255), b, CENTER, 0);
}

bool tab_shown(int t)
{
    if (room_only()) return t == TAB_ROOM;     // the service's social features off: room chat only
    return t != TAB_CREW || C.v.crews || C.v.crew;
}

void step_tab(int dir)
{
    for (int i = 0; i < TAB_COUNT; i++) {
        C.tab = (C.tab + dir + TAB_COUNT) % TAB_COUNT;
        if (tab_shown(C.tab)) break;
    }
    C.menu = false;
    C.line_skip = 0;
    C.confirm_remove = 0;
}

void chat_tabs(ImDrawList *dl, const Frame &f)
{
    float x = PX + 14, y = PY + 11;
    x += plate_hint(dl, f, x, y + 1, ps_icons() ? "L1" : "LB", "") + 8;
    for (int t = 0; t < TAB_COUNT; t++) {
        if (!tab_shown(t)) continue;
        bool on = t == C.tab;
        float w = text_width(f, g_title, 13, k_tab_names[t]);
        text(dl, f, g_title, 13, x, y, on ? CREAM : DIM, k_tab_names[t], LEFT, 0);
        int n = t == TAB_FRIENDS ? C.v.requests : t == TAB_MESSAGES ? C.v.unread : t == TAB_ROOM && !on ? room_unread() : 0;
        float bw = 0;
        if (n > 0) { badge(dl, f, x + w + 4, y + 2.5f, n); bw = 18; }
        if (on) rect(dl, f, x - 2, TABS_B - 2.5f, w + bw + 4, 2, RED);
        if (mouse_click(f, x - 4, PY + 4, w + bw + 8, TABS_B - PY - 4)) { C.tab = t; C.menu = false; C.line_skip = 0; }
        x += w + bw + 15;
    }
    plate_hint(dl, f, x, y + 1, ps_icons() ? "R1" : "RB", "");
    rect(dl, f, PX, TABS_B, PW, 0.5f, LINE_SEP);
}

// The text box and its button at the bottom of a tab, from x0 to the panel's right edge.
void input_bar(ImDrawList *dl, const Frame &f, float x0, const std::string &placeholder, const char *button)
{
    float x1 = PX + PW - 11, bw = text_width(f, g_title, 11, button) + 22, by = BAR_Y + 8, bh = 26;
    rect(dl, f, x0, BAR_Y, PX + PW - x0, 0.5f, LINE_SEP);
    const std::string &d = C.draft[C.tab];
    bool live = !d.empty() || C.sel[C.tab] < 0 || C.tab == TAB_MESSAGES;
    float boxw = x1 - x0 - 11 - bw - 7;
    rect(dl, f, x0 + 11, by, boxw, bh, IM_COL32(0, 0, 0, 115), 6);
    ImU32 edge = live ? RED : IM_COL32(255, 255, 255, 31);
    dl->AddRect(f.p(x0 + 11, by), f.p(x0 + 11 + boxw, by + bh), edge, 6 * f.s(), 0, 1.0f * f.s());
    if (live) dl->AddRect(f.p(x0 + 10, by - 1), f.p(x0 + 12 + boxw, by + bh + 1), with_alpha(RED, 0.25f), 7 * f.s(), 0, 2.0f * f.s());
    dl->PushClipRect(f.p(x0 + 13, by), f.p(x0 + 9 + boxw, by + bh), true);
    float tx = x0 + 19, ty = by + 6.5f;
    if (d.empty()) {
        text(dl, f, g_body, 11, tx, ty, DIM, placeholder.c_str(), LEFT, 0);
    } else {
        // the end of a long line stays in view
        std::string before = d.substr(0, (size_t)std::max(0, std::min(C.cursor[C.tab], (int)d.size())));
        float cw = text_width(f, g_body, 11, before.c_str()), shift = std::max(0.0f, cw - (boxw - 20));
        text(dl, f, g_body, 11, tx - shift, ty, CREAM, d.c_str(), LEFT, 0);
        if (live && (GetTickCount64() / 500) % 2 == 0) rect(dl, f, tx - shift + cw + 0.5f, by + 5, 1.2f, 16, GOLD_TEXT);
    }
    if (d.empty() && live && (GetTickCount64() / 500) % 2 == 0) rect(dl, f, tx - 1, by + 5, 1.2f, 16, GOLD_TEXT);
    dl->PopClipRect();
    float bx = x1 - bw;
    bool ready = !d.empty();
    rect(dl, f, bx, by, bw, bh, ready ? RED : IM_COL32(255, 255, 255, 20), 6);
    text(dl, f, g_title, 11, bx + bw / 2, by + 6.5f, ready ? IM_COL32(255, 245, 230, 255) : DIM, button, CENTER, 0);
    if (mouse_click(f, bx, by, bw, bh)) chat_send();
}

// ---- GLOBAL and CREW: lines, newest at the bottom ----

bool line_shown(const social::Line &l)
{
    if (C.tab != TAB_GLOBAL || C.region == 0) return true;
    return l.channel == k_region_ids[C.region];
}

// Draws the lines from the bottom of the box up; sel (from the newest, -1 none) gets the marker.
// Returns the line the mouse clicked (from the newest), -1 none.
int draw_lines(ImDrawList *dl, const Frame &f, const std::vector<const social::Line *> &lines, float x0, float y0,
               float x1, float y1, bool stacked)
{
    int clicked = -1;
    int n = (int)lines.size(), sel = C.sel[C.tab];
    if (sel >= n) sel = C.sel[C.tab] = n - 1;
    if (sel >= 0 && sel < C.line_skip) C.line_skip = sel;
    C.line_skip = std::max(0, std::min(C.line_skip, std::max(0, n - 1)));
    dl->PushClipRect(f.p(x0 - 4, y0), f.p(x1 + 4, y1), true);
    float y = y1 - 6;
    bool sel_drawn = sel < 0;
    for (int i = C.line_skip; i < n && y > y0; i++) {
        const social::Line &l = *lines[(size_t)(n - 1 - i)];
        bool mine = l.mine || (l.from && l.from == C.v.me);
        std::string body = l.hidden ? "message hidden: you muted " + l.name : l.text;
        ImU32 name_col = mine ? GOLD_TEXT : l.from && friend_of(l.from) ? BLUE_NAME : CREAM;
        float h, top;
        if (stacked) {      // CREW: the picture, the name and time above the message, as ROOM in the design
            const float ax = room_only() ? 0.0f : 21.0f;     // the picture's column (none with the social features off)
            float mh = text_height(f, g_body, 11, body.c_str(), x1 - x0 - ax);
            h = 14 + mh + 8;
            top = y - h;
            if (l.system) {
                text(dl, f, g_body, 8.5f, (x0 + x1) / 2, top + 4, DIM, l.text.c_str(), CENTER, 0, x1 - x0);
            } else {
                if (ax > 0) draw_avatar(dl, f, x0 + 8, top + 8, 8, l.from, l.name, l.hidden ? 0.5f : 1.0f);
                text(dl, f, g_hand, 14, x0 + ax, top - 3, name_col, l.name.c_str(), LEFT, 0.6f);
                text(dl, f, g_body, 8, x0 + ax + text_width(f, g_hand, 14, l.name.c_str()) + 6, top + 1, DIM,
                     clock_text(l.at).c_str(), LEFT, 0);
                text(dl, f, g_body, 11, x0 + ax, top + 14, l.hidden ? DIM : CREAM, body.c_str(), LEFT, 0, x1 - x0 - ax);
            }
        } else {            // GLOBAL: time, crew tag, name and message in one line
            float x = x0;
            std::string t = clock_text(l.at);
            float tw = text_width(f, g_body, 8, t.c_str()) + 5;
            float lead = tw, tagw = 0, namew = 0;
            if (!l.tag.empty()) tagw = text_width(f, g_body, 7.5f, l.tag.c_str()) + 10;
            if (l.system) namew = text_width(f, g_title, 8.5f, "SERVER") + 5;
            else namew = text_width(f, g_hand, 13, l.name.c_str()) + 6 + 15;      // with the picture
            lead += tagw + namew;
            float mh = text_height(f, g_body, 11, body.c_str(), x1 - x0 - lead);
            h = std::max(14.0f, mh) + 7;
            top = y - h;
            if (l.system) rect(dl, f, x0 - 6, top - 2, x1 - x0 + 12, h + 1, IM_COL32(255, 205, 60, 20), 5);
            text(dl, f, g_body, 8, x, top + 3, DIM, t.c_str(), LEFT, 0);
            x += tw;
            if (tagw > 0) {
                dl->AddRect(f.p(x, top + 2), f.p(x + tagw - 4, top + 12.5f), IM_COL32(255, 205, 60, 150), 3 * f.s(), 0, 0.8f * f.s());
                text(dl, f, g_body, 7.5f, x + (tagw - 4) / 2, top + 2.5f, GOLD_TEXT, l.tag.c_str(), CENTER, 0);
                x += tagw;
            }
            if (l.system) text(dl, f, g_title, 8.5f, x, top + 2.5f, GOLD_TEXT, "SERVER", LEFT, 0);
            else {
                draw_avatar(dl, f, x + 6, top + 7, 6, l.from, l.name, l.hidden ? 0.5f : 1.0f);
                text(dl, f, g_hand, 13, x + 15, top - 1.5f, name_col, l.name.c_str(), LEFT, 0.6f);
            }
            x += namew;
            text(dl, f, g_body, 11, x, top, l.system ? IM_COL32(255, 231, 166, 255) : l.hidden ? DIM : CREAM, body.c_str(),
                 LEFT, 0, x1 - x);
        }
        if (i == sel) {
            dl->AddRect(f.p(x0 - 5, top - 3), f.p(x1 + 3, top + h - 3), with_alpha(RED, 0.8f), 5 * f.s(), 0, 1.2f * f.s());
            sel_drawn = true;
        }
        if (mouse_click(f, x0 - 5, top - 3, x1 - x0 + 8, h)) clicked = i;
        y = top;
    }
    dl->PopClipRect();
    if (!sel_drawn && sel >= 0) C.line_skip++;      // keep the marked line in view
    if (n == 0) text(dl, f, g_body, 10, (x0 + x1) / 2, (y0 + y1) / 2 - 6, DIM,
                     C.tab == TAB_CREW ? "No crew messages yet. Say hello." :
                     C.tab == TAB_ROOM ? "Nothing said in this room yet." : "No messages yet. Be the first.", CENTER, 0);
    // the mouse wheel scrolls
    if (mouse_in(f, x0, y0, x1 - x0, y1 - y0)) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0) C.line_skip = std::min(n - 1, C.line_skip + 2);
        if (wheel < 0) C.line_skip = std::max(0, C.line_skip - 2);
    }
    return clicked;
}

void open_player_menu(long long id, const std::string &name)
{
    if (!id && name.empty()) return;
    if (id && id == C.v.me) return;
    C.menu = true;
    C.menu_id = id;
    C.menu_name = name;
    C.menu_sel = 0;
    C.popup_frame = ImGui::GetFrameCount();
    C.menu_room = false;
    C.menu_account = 0;
    C.menu_channel.clear();
    C.menu_text.clear();
    C.menu_msg = 0;
    C.menu_context.clear();
    C.menu_crew = false;
    C.menu_role.clear();
}

// What REPORT sends for the line at index i (oldest first) of a chat: its id where the service has the
// line, else (room) its text, and up to five lines before it.
void menu_report_line(const std::vector<const social::Line *> &lines, size_t i, const std::string &channel)
{
    if (i >= lines.size()) return;
    const social::Line &l = *lines[i];
    C.menu_channel = channel;
    C.menu_msg = channel == "room" ? 0 : l.seq;
    C.menu_text = channel == "room" ? l.text : "";
    C.menu_context.clear();
    if (channel == "room")
        for (size_t k = i >= 5 ? i - 5 : 0; k < i; k++)
            if (!lines[k]->system) C.menu_context.push_back(clock_text(lines[k]->at) + " " + lines[k]->name + ": " + lines[k]->text);
}

// REPORT on a player whose menu came from a list (the room's names, a conversation): their newest line.
void menu_report_newest(long long id, const std::string &name, bool room)
{
    if (room) {
        std::vector<social::Line> lines = room_lines();
        std::vector<const social::Line *> ptrs;
        for (auto &l : lines) ptrs.push_back(&l);
        for (size_t i = lines.size(); i-- > 0;)
            if (!lines[i].system && !lines[i].mine && (id ? lines[i].from == id : lines[i].name == name)) {
                menu_report_line(ptrs, i, "room");
                return;
            }
        return;
    }
    for (auto &t : C.v.threads)
        if (t.with == id)
            for (auto &m : t.msgs) if (m.from == id) { C.menu_channel = "dm:" + std::to_string(id); C.menu_msg = m.id; }
}

void draw_global(ImDrawList *dl, const Frame &f, const ChatKeys &k)
{
    // the server chips
    float x = PX + 14, y = TABS_B + 8;
    for (int i = 0; i < 4; i++) {
        float w = text_width(f, g_title, 8.5f, k_region_names[i]) + 14;
        bool on = C.region == i;
        rect(dl, f, x, y, w, 17, on ? RED : IM_COL32(255, 255, 255, 18), 8.5f);
        text(dl, f, g_title, 8.5f, x + w / 2, y + 3.5f, on ? IM_COL32(255, 245, 230, 255) : DIM, k_region_names[i], CENTER, 0);
        if (mouse_click(f, x, y, w, 17)) C.region = i;
        x += w + 5;
    }
    char info[96];
    if (C.v.slow_mode > 0) snprintf(info, sizeof(info), "%d online - slow mode %d s", C.v.online, C.v.slow_mode);
    else snprintf(info, sizeof(info), "%d online", C.v.online);
    text(dl, f, g_body, 9, PX + PW - 14, y + 3, DIM, info, RIGHT, 0);
    if (k.lt) C.region = (C.region + 3) % 4;
    if (k.rt) C.region = (C.region + 1) % 4;
    if (C.draft[TAB_GLOBAL].empty() && C.sel[TAB_GLOBAL] < 0) {
        if (k.left) C.region = (C.region + 3) % 4;
        if (k.right) C.region = (C.region + 1) % 4;
    }
    std::vector<const social::Line *> lines;
    for (auto &l : C.v.global) if (line_shown(l)) lines.push_back(&l);
    int n = (int)lines.size();
    if (k.up) C.sel[TAB_GLOBAL] = std::min(n - 1, C.sel[TAB_GLOBAL] + 1);
    if (k.down) C.sel[TAB_GLOBAL] = std::max(-1, C.sel[TAB_GLOBAL] - 1);
    int clicked = draw_lines(dl, f, lines, PX + 15, y + 22, PX + PW - 15, BAR_Y, false);
    if (clicked >= 0) C.sel[TAB_GLOBAL] = clicked;
    int s = C.sel[TAB_GLOBAL];
    if ((k.a || clicked >= 0) && s >= 0 && s < n) {
        const social::Line &l = *lines[(size_t)(n - 1 - s)];
        if (!l.system) {
            open_player_menu(l.from, l.name);
            menu_report_line(lines, (size_t)(n - 1 - s), l.channel);
        }
    }
    std::string where = C.region ? std::string(k_region_names[C.region]) : "";
    for (auto &c : where) c = (char)tolower((unsigned char)c);
    std::string ph = C.region == 0 ? "Message everyone on your server" : "Message everyone on " +
                     std::string(C.region == 1 ? "Europe" : C.region == 2 ? "US East" : "US West");
    input_bar(dl, f, PX, ph, "SEND");
}

// ---- CREW (stage 2c): the crew's page as the design has it, its chat, and making or joining one ----

void open_keyboard(int kind);       // the on-screen keyboard, below

std::string region_name(const std::string &r)
{
    return r == "eu" ? "Europe" : r == "us-east" ? "US East" : r == "us-west" ? "US West" : r;
}

std::string month_year(long long unix_s)
{
    if (unix_s <= 0) return "";
    time_t t = (time_t)unix_s;
    struct tm tm;
    if (gmtime_s(&tm, &t)) return "";
    static const char *const m[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    return std::string(m[tm.tm_mon]) + " " + std::to_string(1900 + tm.tm_year);
}

void tag_chip(ImDrawList *dl, const Frame &f, float x, float y, float size, const std::string &tag)
{
    float tw = text_width(f, g_title, size, tag.c_str()) + size * 0.9f;
    dl->AddRect(f.p(x, y), f.p(x + tw, y + size * 1.35f), IM_COL32(255, 205, 60, 220), 3 * f.s(), 0, 1.0f * f.s());
    text(dl, f, g_title, size, x + tw / 2, y + size * 0.12f, GOLD_TEXT, tag.c_str(), CENTER, 0);
}

// The crew chat (the CREW CHAT button): the lines and the box, B back to the page.
void draw_crew_chat(ImDrawList *dl, const Frame &f, const ChatKeys &k)
{
    const social::View &v = C.v;
    char head[160];
    snprintf(head, sizeof(head), "%d MEMBERS - %d ONLINE", v.crew_members, v.crew_online);
    float y = TABS_B + 9;
    float tw = text_width(f, g_body, 9, v.crew_tag.c_str()) + 10;
    dl->AddRect(f.p(PX + 16, y + 1), f.p(PX + 16 + tw, y + 13), IM_COL32(255, 205, 60, 150), 3 * f.s(), 0, 0.8f * f.s());
    text(dl, f, g_body, 9, PX + 16 + tw / 2, y + 1.5f, GOLD_TEXT, v.crew_tag.c_str(), CENTER, 0);
    text(dl, f, g_hand, 15, PX + 22 + tw, y - 3, CREAM, v.crew_name.c_str(), LEFT, 0.6f);
    text(dl, f, g_title, 9, PX + PW - 14, y + 2, DIM, head, RIGHT, 0);
    rect(dl, f, PX, y + 20, PW, 0.5f, LINE_SEP);
    std::vector<const social::Line *> lines;
    for (auto &l : v.crew_chat) lines.push_back(&l);
    int n = (int)lines.size();
    if (k.up) C.sel[TAB_CREW] = std::min(n - 1, C.sel[TAB_CREW] + 1);
    if (k.down) C.sel[TAB_CREW] = std::max(-1, C.sel[TAB_CREW] - 1);
    int clicked = draw_lines(dl, f, lines, PX + 22, y + 24, PX + PW - 22, BAR_Y, true);
    if (clicked >= 0) C.sel[TAB_CREW] = clicked;
    int s = C.sel[TAB_CREW];
    if ((k.a || clicked >= 0) && s >= 0 && s < n) {
        const social::Line &l = *lines[(size_t)(n - 1 - s)];
        if (!l.system) {
            open_player_menu(l.from, l.name);
            menu_report_line(lines, (size_t)(n - 1 - s), "crew:" + std::to_string(v.crew));
        }
    }
    input_bar(dl, f, PX, "Message your crew", "SEND");
}

// Not in a crew: the invites, and CREATE A CREW.
void draw_crew_none(ImDrawList *dl, const Frame &f, const ChatKeys &k)
{
    const social::View &v = C.v;
    float y = TABS_B + 14;
    text(dl, f, g_title, 9, PX + 18, y, DIM, "CREW INVITES", LEFT, 0);
    y += 16;
    int n = (int)v.crew_invites.size();
    int &sel = C.sel[TAB_CREW];
    if (k.up) sel = std::max(0, sel - 1);
    if (k.down) sel = std::min(n, sel + 1);       // n: the CREATE A CREW button
    sel = std::max(0, std::min(sel, n));
    if (k.left || k.right) C.btn = C.btn ? 0 : 1;
    for (int i = 0; i < n; i++) {
        const social::CrewInvite &ci = v.crew_invites[(size_t)i];
        bool on = i == sel;
        rect(dl, f, PX + 13, y, PW - 26, 32, on ? IM_COL32(200, 16, 46, 26) : IM_COL32(255, 205, 60, 18), 6);
        if (on) dl->AddRect(f.p(PX + 13, y), f.p(PX + PW - 13, y + 32), with_alpha(RED, 0.75f), 6 * f.s(), 0, 1.0f * f.s());
        std::string line = (ci.tag.empty() ? "" : "[" + ci.tag + "] ") + (ci.text.empty() ? ci.from + " invites you" : ci.text);
        text(dl, f, g_body, 10, PX + 24, y + 9, CREAM, line.c_str(), LEFT, 0, 330);
        const char *labels[2] = { "JOIN", "DISMISS" };
        float bx = PX + PW - 22;
        for (int b = 1; b >= 0; b--) {
            float bw = text_width(f, g_title, 8.5f, labels[b]) + 14;
            bx -= bw;
            bool hot = on && C.btn == b;
            rect(dl, f, bx, y + 6, bw, 20, hot || b == 0 ? RED : IM_COL32(255, 255, 255, 20), 5);
            text(dl, f, g_title, 8.5f, bx + bw / 2, y + 11, CREAM, labels[b], CENTER, 0);
            bool click = mouse_click(f, bx, y + 6, bw, 20);
            if ((on && k.a && C.btn == b) || click) {
                if (b == 0) social::crew_join(ci.crew);
                else social::crew_dismiss(ci.crew);
            }
            bx -= 5;
        }
        y += 36;
    }
    if (!n)
        text(dl, f, g_body, 11, PX + PW / 2, y + 30, DIM,
             "You are not in a crew yet. When a crew invites you, the invite shows here.", CENTER, 0, 380);
    // CREATE A CREW: a name, then a tag of 2 to 4 letters (the on-screen keyboard twice)
    float by = std::max(y + 70, TABS_B + 150.0f), bw = 220, bx = PX + PW / 2 - bw / 2;
    bool on = sel == n;
    rect(dl, f, bx, by, bw, 30, on ? RED : IM_COL32(200, 16, 46, 150), 7);
    if (on) dl->AddRect(f.p(bx - 1, by - 1), f.p(bx + bw + 1, by + 31), IM_COL32(255, 90, 110, 255), 7 * f.s(), 0, 1.2f * f.s());
    text(dl, f, g_title, 13, bx + bw / 2, by + 7, IM_COL32(255, 245, 230, 255), "CREATE A CREW", CENTER, 0);
    text(dl, f, g_body, 9, PX + PW / 2, by + 38, DIM, "Pick a name and a tag. You become its Boss; invite your friends.",
         CENTER, 0, 380);
    if ((on && k.a) || mouse_click(f, bx, by, bw, 30)) open_keyboard(KB_CREW_NAME);
}

void crew_member_menu(const social::CrewMember &m)
{
    open_player_menu(m.id, m.name);
    C.menu_crew = true;
    C.menu_role = m.role;
}

void draw_crew(ImDrawList *dl, const Frame &f, const ChatKeys &k)
{
    const social::View &v = C.v;
    if (!v.crew) { C.crew_view = 0; draw_crew_none(dl, f, k); return; }
    if (C.crew_view == 1) { draw_crew_chat(dl, f, k); return; }
    bool officer = v.crew_role == "boss" || v.crew_role == "lieutenant";

    // the left: emblem, name, tag, motto, the cards, the level and the rewards
    float lx = PX + 18, ly = TABS_B + 12, rx = PX + 372;
    rect(dl, f, rx - 10, TABS_B, 0.5f, PY + PH - TABS_B, LINE_SEP);
    float eb = 62;
    rect(dl, f, lx, ly, eb, eb, IM_COL32(0, 0, 0, 90), 9);
    dl->AddRect(f.p(lx, ly), f.p(lx + eb, ly + eb), IM_COL32(255, 205, 60, 230), 9 * f.s(), 0, 2.0f * f.s());
    {   // the crown (the default emblem)
        float cx = lx + eb / 2, cy = ly + eb / 2 + 2, u = 12;
        ImVec2 pts[7] = { f.p(cx - u, cy + u * 0.5f), f.p(cx - u, cy - u * 0.6f), f.p(cx - u * 0.45f, cy - u * 0.05f),
                          f.p(cx, cy - u * 0.85f), f.p(cx + u * 0.45f, cy - u * 0.05f), f.p(cx + u, cy - u * 0.6f),
                          f.p(cx + u, cy + u * 0.5f) };
        dl->AddTriangleFilled(pts[0], pts[1], pts[2], RED);
        dl->AddTriangleFilled(pts[2], pts[3], pts[4], RED);
        dl->AddTriangleFilled(pts[4], pts[5], pts[6], RED);
        dl->AddRectFilled(f.p(cx - u, cy - u * 0.05f), f.p(cx + u, cy + u * 0.5f), RED);
        dl->AddRectFilled(f.p(cx - u, cy + u * 0.65f), f.p(cx + u, cy + u * 0.95f), IM_COL32(255, 205, 60, 255), 1.5f * f.s());
    }
    float nx = lx + eb + 12;
    text(dl, f, g_title, 21, nx, ly - 2, CREAM, v.crew_name.c_str(), LEFT, 0);
    tag_chip(dl, f, nx + text_width(f, g_title, 21, v.crew_name.c_str()) + 8, ly + 3, 11, v.crew_tag);
    if (!v.crew_motto.empty()) text(dl, f, g_hand, 12, nx, ly + 24, CREAM, ("\"" + v.crew_motto + "\"").c_str(), LEFT, 0.5f);
    std::string info = std::to_string(v.crew_members) + (v.crew_members == 1 ? " member" : " members");
    if (!v.crew_region.empty()) info += " - " + region_name(v.crew_region);
    if (v.crew_founded) info += " - founded " + month_year(v.crew_founded);
    text(dl, f, g_body, 9.5f, nx, ly + 42, DIM, info.c_str(), LEFT, 0);
    // the cards
    float cy = ly + eb + 12, cw = (rx - 10 - lx - 16 - 12) / 3, ch = 66;
    char battles[32], streak[48], turf[32];
    snprintf(battles, sizeof(battles), "%d - %d", v.crew_wins, v.crew_losses);
    if (v.crew_streak > 1) snprintf(streak, sizeof(streak), "won %d in a row", v.crew_streak);
    else snprintf(streak, sizeof(streak), "%s", v.crew_wins + v.crew_losses ? "" : "no battles yet");
    snprintf(turf, sizeof(turf), "%d", v.crew_turf);
    char rating[32];
    snprintf(rating, sizeof(rating), "rating %d", v.crew_rating);
    struct Card { const char *head; std::string big, sub; };
    Card cards[3] = { { "LEAGUE", v.crew_league.empty() ? std::string("STREET") : upper(v.crew_league.c_str()), rating },
                      { "CREW BATTLES", battles, streak },
                      { "TURF", v.crew_turf ? std::string(turf) + (v.crew_turf == 1 ? " VENUE" : " VENUES") : "NONE YET",
                        "crew battles: soon" } };
    for (int i = 0; i < 3; i++) {
        float x = lx + i * (cw + 6);
        rect(dl, f, x, cy, cw, ch, IM_COL32(255, 255, 255, 14), 8);
        text(dl, f, g_title, 8.5f, x + 9, cy + 7, DIM, cards[i].head, LEFT, 0);
        text(dl, f, g_title, 15, x + 9, cy + 20, i == 0 ? GOLD_TEXT : CREAM, cards[i].big.c_str(), LEFT, 0, cw - 14);
        text(dl, f, g_body, 8.5f, x + 9, cy + ch - 15, DIM, cards[i].sub.c_str(), LEFT, 0);
    }
    // the level
    float vy = cy + ch + 12;
    char lv[32], xp[48];
    snprintf(lv, sizeof(lv), "CREW LEVEL %d", std::max(1, v.crew_level));
    snprintf(xp, sizeof(xp), "%d / %d XP", v.crew_xp, v.crew_next_xp);
    text(dl, f, g_title, 11, lx, vy, CREAM, lv, LEFT, 0);
    text(dl, f, g_body, 9, lx + text_width(f, g_title, 11, lv) + 8, vy + 2, DIM, xp, LEFT, 0);
    float bw = rx - 10 - lx - 16;
    float span = (float)std::max(1, v.crew_next_xp - v.crew_level_xp);
    float part = std::max(0.0f, std::min(1.0f, (v.crew_xp - v.crew_level_xp) / span));
    rect(dl, f, lx, vy + 17, bw, 6, IM_COL32(255, 255, 255, 22), 3);
    if (part > 0) rect(dl, f, lx, vy + 17, bw * part, 6, GOLD_TEXT, 3);
    // the rewards
    float wy = vy + 32;
    text(dl, f, g_title, 9, lx, wy, DIM, "REWARDS", LEFT, 0);
    static const struct { const char *name; int level; } rewards[] = {
        { "GOLD TAG", 5 }, { "CROWN EMBLEM", 6 }, { "VERSUS CARD", 8 }, { "ROOM THEME", 10 } };
    float qw = (bw - 3 * 5) / 4;
    for (int i = 0; i < 4; i++) {
        float x = lx + i * (qw + 5);
        bool got = v.crew_level >= rewards[i].level;
        rect(dl, f, x, wy + 13, qw, 30, IM_COL32(255, 255, 255, got ? 22 : 12), 6);
        if (got) dl->AddRect(f.p(x, wy + 13), f.p(x + qw, wy + 43), IM_COL32(255, 205, 60, 160), 6 * f.s(), 0, 1.0f * f.s());
        text(dl, f, g_title, 8, x + qw / 2, wy + 17, got ? GOLD_TEXT : DIM, rewards[i].name, CENTER, 0);
        char l[16];
        snprintf(l, sizeof(l), "LEVEL %d", rewards[i].level);
        text(dl, f, g_title, 7.5f, x + qw / 2, wy + 29, DIM, l, CENTER, 0);
    }

    // the right: the members (and applications for officers), then the buttons
    struct Row { int kind; const social::CrewMember *m; const social::Ref *r; };   // 0 member, 1 application
    std::vector<Row> rows;
    for (auto &m : v.crew_roster) rows.push_back({ 0, &m, nullptr });
    if (officer) for (auto &a : v.crew_applied) rows.push_back({ 1, nullptr, &a });
    enum { B_CHALLENGE, B_CHAT, B_INVITE, B_LEAVE };
    std::vector<int> buttons = { B_CHALLENGE, B_CHAT };
    if (officer) buttons.push_back(B_INVITE);
    buttons.push_back(B_LEAVE);
    int total = (int)rows.size() + (int)buttons.size();
    int &sel = C.sel[TAB_CREW];
    if (sel < 0 || sel >= total) sel = (int)rows.size() + 1;      // CREW CHAT
    int before = sel;
    if (k.up) sel = std::max(0, sel - 1);
    if (k.down) sel = std::min(total - 1, sel + 1);
    if (sel != before) C.confirm_leave = false;
    char mh[48];
    snprintf(mh, sizeof(mh), "MEMBERS - %d ONLINE", v.crew_online);
    text(dl, f, g_title, 10, rx, TABS_B + 12, DIM, mh, LEFT, 0);
    const int shown = 8;
    int first = std::max(0, std::min((int)rows.size() - shown, (sel < (int)rows.size() ? sel : 0) - shown + 2));
    float ry = TABS_B + 30, rw = PX + PW - 12 - rx;
    for (int i = first; i < (int)rows.size() && i < first + shown; i++) {
        const Row &w = rows[(size_t)i];
        bool on = i == sel;
        if (on) dl->AddRect(f.p(rx - 5, ry - 3), f.p(rx + rw, ry + 17), with_alpha(RED, 0.8f), 5 * f.s(), 0, 1.2f * f.s());
        long long id = w.kind ? w.r->id : w.m->id;
        const std::string &name = w.kind ? w.r->name : w.m->name;
        std::string state = w.kind ? "wants in" : w.m->state;
        bool offline = !w.kind && state == "offline";
        draw_avatar(dl, f, rx + 8, ry + 7, 7.5f, id, name, offline ? 0.55f : 1.0f);
        ImU32 dot = offline ? IM_COL32(120, 112, 100, 255) : state == "fighting" || state == "watching" ? ORANGE : LIVE;
        if (!w.kind) {
            dl->AddCircleFilled(f.p(rx + 14, ry + 12.5f), 2.9f * f.s(), IM_COL32(18, 12, 14, 255));
            dl->AddCircleFilled(f.p(rx + 14, ry + 12.5f), 2.1f * f.s(), dot);
        }
        text(dl, f, g_hand, 13, rx + 21, ry - 2, with_alpha(id == v.me ? GOLD_TEXT : CREAM, offline ? 0.6f : 1.0f), name.c_str(),
             LEFT, 0.6f);
        if (w.kind) {
            const char *acc = "ACCEPT";
            float aw = text_width(f, g_title, 8, acc) + 10;
            rect(dl, f, rx + rw - aw - 3, ry, aw, 14, on ? RED : IM_COL32(200, 16, 46, 150), 4);
            text(dl, f, g_title, 8, rx + rw - 3 - aw / 2, ry + 2.5f, CREAM, acc, CENTER, 0);
        } else if (w.m->role == "boss" || w.m->role == "lieutenant") {
            bool boss = w.m->role == "boss";
            const char *label = boss ? "BOSS" : "LIEUTENANT";
            float tw = text_width(f, g_title, 8, label) + 9;
            rect(dl, f, rx + rw - tw - 3, ry + 0.5f, tw, 13, boss ? GOLD_TEXT : IM_COL32(142, 16, 36, 255), 3);
            text(dl, f, g_title, 8, rx + rw - 3 - tw / 2, ry + 2.5f, boss ? IM_COL32(26, 18, 8, 255) : CREAM, label, CENTER, 0);
        } else if (state == "fighting" || state == "watching" || state == "room") {
            text(dl, f, g_title, 8, rx + rw - 3, ry + 2.5f, state == "room" ? LIVE : ORANGE, upper(state.c_str()).c_str(), RIGHT, 0);
        }
        bool click = mouse_click(f, rx - 5, ry - 3, rw + 5, 20);
        if (click) sel = i;
        if ((on && k.a) || click) {
            if (w.kind) social::crew_accept(w.r->id);
            else if (w.m->id != v.me) crew_member_menu(*w.m);
        }
        ry += 21;
    }
    if ((int)rows.size() > shown) {
        char more[32];
        snprintf(more, sizeof(more), "%d of %d", std::min(first + shown, (int)rows.size()), (int)rows.size());
        text(dl, f, g_body, 8, rx + rw, TABS_B + 13, DIM, more, RIGHT, 0);
    }
    float by = PY + PH - 12 - (float)buttons.size() * 27;
    for (size_t b = 0; b < buttons.size(); b++) {
        int idx = (int)rows.size() + (int)b;
        bool on = sel == idx, soon = buttons[b] == B_CHALLENGE;
        const char *label = buttons[b] == B_CHALLENGE ? "CHALLENGE A CREW" : buttons[b] == B_CHAT ? "CREW CHAT" :
                            buttons[b] == B_INVITE ? "INVITE A PLAYER" : C.confirm_leave ? "SURE? LEAVE" : "LEAVE CREW";
        ImU32 fill = on ? RED : b == 0 ? IM_COL32(200, 16, 46, soon ? 70 : 200) : IM_COL32(255, 255, 255, 22);
        rect(dl, f, rx - 4, by, rw + 4, 23, fill, 6);
        if (on) dl->AddRect(f.p(rx - 5, by - 1), f.p(rx + rw + 1, by + 24), IM_COL32(255, 90, 110, 255), 6 * f.s(), 0, 1.2f * f.s());
        text(dl, f, g_title, 11, rx - 2 + rw / 2, by + 5, soon && !on ? DIM : IM_COL32(255, 245, 230, 255), label, CENTER, 0);
        bool click = mouse_click(f, rx - 4, by, rw + 4, 23);
        if (click) sel = idx;
        if ((on && k.a) || click) {
            switch (buttons[b]) {
            case B_CHALLENGE: say("Crew battles come with the next stage of the Social Update."); break;
            case B_CHAT: C.crew_view = 1; C.sel[TAB_CREW] = -1; C.line_skip = 0; break;
            case B_INVITE: open_keyboard(KB_CREW_INVITE); break;
            case B_LEAVE:
                if (!C.confirm_leave) C.confirm_leave = true;
                else { social::crew_leave(); C.confirm_leave = false; fprintf(stderr, "[SOCIAL-UI] crew: leave\n"); }
                break;
            }
        }
        by += 27;
    }
}

// ---- ROOM: who is in it, and the room's chat (stage 2b: through the relay, netplay.c) ----

void draw_room_tab(ImDrawList *dl, const Frame &f, const ChatKeys &k, const NetRoom &r)
{
    float lw = 150;
    rect(dl, f, PX + lw, TABS_B, 0.5f, PY + PH - TABS_B, LINE_SEP);
    int count = r.in_room ? r.player_count + r.spectator_count : 0;
    char head[48];
    snprintf(head, sizeof(head), "IN THIS ROOM - %d", count);
    text(dl, f, g_title, 9, PX + 12, TABS_B + 12, DIM, r.in_room ? head : "NOT IN A ROOM", LEFT, 0);
    if (k.up) C.sel[TAB_ROOM] = std::max(0, C.sel[TAB_ROOM] - 1);
    if (k.down) C.sel[TAB_ROOM] = std::min(count - 1, C.sel[TAB_ROOM] + 1);
    if (C.sel[TAB_ROOM] >= count) C.sel[TAB_ROOM] = count - 1;
    float y = TABS_B + 32;
    for (int i = 0; i < count; i++) {
        bool watcher = i >= r.player_count;
        const char *name = watcher ? r.spectators[i - r.player_count] : r.players[i].name;
        bool host = !watcher && r.players[i].slot == r.host, you = !watcher && r.players[i].slot == r.you;
        bool on = i == C.sel[TAB_ROOM];
        if (on) dl->AddRect(f.p(PX + 7, y - 4), f.p(PX + lw - 7, y + 15), with_alpha(RED, 0.8f), 5 * f.s(), 0, 1.2f * f.s());
        if (room_only()) dl->AddCircleFilled(f.p(PX + 15, y + 5.5f), 3 * f.s(), watcher ? IM_COL32(242, 230, 200, 90) : LIVE);
        else draw_avatar(dl, f, PX + 16, y + 5.5f, 6.5f, room_account_of(name), name, watcher ? 0.7f : 1.0f);
        text(dl, f, g_hand, 13, PX + (room_only() ? 24 : 26), y - 2, host ? GOLD_TEXT : watcher ? IM_COL32(242, 230, 200, 205) : BLUE_NAME, name,
             LEFT, 0.6f);
        const char *what = room_name_muted(name) ? "MUTED" : watcher ? "WATCHING" : host ? "HOST" : you ? "YOU" : "";
        text(dl, f, g_body, 7.5f, PX + lw - 12, y + 2, host && !room_name_muted(name) ? GOLD_TEXT : DIM, what, RIGHT, 0);
        bool click = mouse_click(f, PX + 7, y - 4, lw - 14, 19);
        if (click) C.sel[TAB_ROOM] = i;
        if (on && (k.a || click) && !you && !(watcher && r.you == NETPLAY_SLOT_SPECTATOR && !strcmp(name, hub_name().c_str()))) {
            long long acc = room_only() ? 0 : room_account_of(name);
            open_player_menu(acc, name);
            C.menu_room = true;
            C.menu_account = (unsigned)acc;
            menu_report_newest(acc, name, true);
        }
        y += 17;
    }
    text(dl, f, g_body, 8.5f, PX + 12, PY + PH - 34, DIM, room_only() ? "Pick a name: mute in this room." :
         "Pick a name: add friend or mute.", LEFT, 0, lw - 24);

    // the room's chat, newest at the bottom, the name and time above each line as the design has it
    std::string why;
    int state = room_chat_state(&why);
    unsigned last = 0;
    std::vector<social::Line> lines = room_lines(&last);
    C.room_seen = last;     // shown: read
    float x0 = PX + lw + 22, x1 = PX + PW - 22;
    if (state < 0) {
        float cx = PX + lw + (PW - lw) / 2;
        text(dl, f, g_title, 13, cx, PY + 140, CREAM, r.in_room ? "NO ROOM CHAT HERE" : "NOT IN A ROOM", CENTER, 0);
        text(dl, f, g_body, 10, cx, PY + 162, DIM, r.in_room ? (why.empty() ? "This server has no room chat yet." : why.c_str())
                                                             : "Join or open a room to talk with its players.", CENTER, 0, PW - lw - 40);
        text(dl, f, g_body, 10, cx, PY + 186, DIM, "Voice chat works in rooms.", CENTER, 0);
        return;
    }
    std::vector<const social::Line *> ptrs;
    for (auto &l : lines) ptrs.push_back(&l);
    int keep = C.sel[TAB_ROOM];
    C.sel[TAB_ROOM] = -1;           // the marker belongs to the list on the left
    int clicked = draw_lines(dl, f, ptrs, x0, TABS_B + 6, x1, BAR_Y, true);
    C.sel[TAB_ROOM] = keep;
    if (clicked >= 0 && clicked < (int)ptrs.size()) {
        const social::Line &l = *ptrs[ptrs.size() - 1 - (size_t)clicked];
        if (!l.system && !l.mine) {
            open_player_menu(room_only() ? 0 : l.from, l.name);
            C.menu_room = true;
            C.menu_account = (unsigned)l.from;
            menu_report_line(ptrs, ptrs.size() - 1 - (size_t)clicked, "room");
        }
    }
    input_bar(dl, f, PX + lw, state == 1 ? std::string("Message the room") : why, "SEND");
}

// ---- FRIENDS ----

struct FriendRow {
    int kind = 0;                   // 0 a section title, 1 a request to you, 2 a request you sent, 3 a friend
    std::string title;
    social::Ref ref;
    const social::Friend *fr = nullptr;
    std::vector<int> acts;
};

std::vector<FriendRow> friend_rows(const NetRoom &r)
{
    const social::View &v = C.v;
    std::vector<FriendRow> rows;
    auto section = [&](const char *t, size_t n) {
        FriendRow h;
        h.title = std::string(t) + " - " + std::to_string(n);
        rows.push_back(h);
    };
    if (!v.in.empty()) {
        section("REQUESTS", v.in.size());
        for (auto &x : v.in) { FriendRow w; w.kind = 1; w.ref = x; w.acts = { ACT_ACCEPT, ACT_DECLINE }; rows.push_back(w); }
    }
    std::vector<const social::Friend *> on, off;
    for (auto &x : v.friends) (x.state == "offline" ? off : on).push_back(&x);
    auto by_name = [](const social::Friend *a, const social::Friend *b) { return _stricmp(a->name.c_str(), b->name.c_str()) < 0; };
    std::sort(on.begin(), on.end(), by_name);
    std::sort(off.begin(), off.end(), [](const social::Friend *a, const social::Friend *b) { return a->last_online > b->last_online; });
    if (!on.empty()) section("ONLINE", on.size());
    for (auto *x : on) {
        FriendRow w;
        w.kind = 3; w.fr = x; w.ref.id = x->id; w.ref.name = x->name; w.ref.tag = x->tag;
        if (x->joinable && !x->room.empty() && x->state == "room" && !(r.in_room && x->room == r.code)) w.acts.push_back(ACT_JOIN);
        else if ((x->state == "fighting" || x->state == "room") && !x->room.empty() && can_watch()) w.acts.push_back(ACT_WATCH);
        else if (x->state == "menu" && r.in_room) w.acts.push_back(ACT_INVITE);
        w.acts.push_back(ACT_MESSAGE);
        w.acts.push_back(C.confirm_remove == x->id ? ACT_SURE : ACT_REMOVE);
        rows.push_back(w);
    }
    if (!off.empty()) section("OFFLINE", off.size());
    for (auto *x : off) {
        FriendRow w;
        w.kind = 3; w.fr = x; w.ref.id = x->id; w.ref.name = x->name; w.ref.tag = x->tag;
        w.acts = { ACT_MESSAGE, C.confirm_remove == x->id ? ACT_SURE : ACT_REMOVE };
        rows.push_back(w);
    }
    if (!v.out.empty()) {
        section("SENT", v.out.size());
        for (auto &x : v.out) { FriendRow w; w.kind = 2; w.ref = x; rows.push_back(w); }
    }
    return rows;
}

const char *act_label(int a)
{
    static const char *const l[] = { "ACCEPT", "DECLINE", "JOIN ROOM", "WATCH", "INVITE", "MESSAGE", "REMOVE", "SURE?" };
    return l[a];
}

void do_friend_act(const FriendRow &row, int act, const NetRoom &r)
{
    long long id = row.ref.id;
    switch (act) {
    case ACT_ACCEPT: social::friend_accept(id); break;
    case ACT_DECLINE: social::friend_decline(id); break;
    case ACT_JOIN: if (row.fr) join_friend(*row.fr, false); break;
    case ACT_WATCH: if (row.fr) join_friend(*row.fr, true); break;
    case ACT_INVITE:
        if (r.in_room) {
            social::send_dm(id, std::string("Join my room: code ") + r.code);
            say("Invite sent to " + row.ref.name + ".");
        }
        break;
    case ACT_MESSAGE: open_dm(id); break;
    case ACT_REMOVE: C.confirm_remove = id; break;
    case ACT_SURE: social::friend_remove(id); C.confirm_remove = 0; say(row.ref.name + " is no longer your friend."); break;
    }
}

void draw_friends(ImDrawList *dl, const Frame &f, const ChatKeys &k, const NetRoom &r)
{
    std::vector<FriendRow> rows = friend_rows(r);
    std::vector<int> pick;          // the rows that can be chosen
    for (int i = 0; i < (int)rows.size(); i++) if (rows[(size_t)i].kind) pick.push_back(i);
    int &sel = C.sel[TAB_FRIENDS];
    int before = sel;
    if (k.up) sel--;
    if (k.down) sel++;
    sel = pick.empty() ? 0 : std::max(0, std::min(sel, (int)pick.size() - 1));
    if (sel != before) { C.btn = 0; C.confirm_remove = 0; }
    const FriendRow *cur = pick.empty() ? nullptr : &rows[(size_t)pick[(size_t)sel]];
    if (cur && !cur->acts.empty()) {
        if (k.left) C.btn = std::max(0, C.btn - 1);
        if (k.right) C.btn = std::min((int)cur->acts.size() - 1, C.btn + 1);
        C.btn = std::min(C.btn, (int)cur->acts.size() - 1);
    }
    // the list, scrolled so the chosen row stays in view
    float top = TABS_B + 6, bottom = PY + PH - 8, total = 0, sel_y = 0, sel_h = 0;
    for (int i = 0; i < (int)rows.size(); i++) {
        float h = rows[(size_t)i].kind ? 32 : 18;
        if (cur && &rows[(size_t)i] == cur) { sel_y = total; sel_h = h; }
        total += h + 2;
    }
    if (sel_y - C.scroll_y < 0) C.scroll_y = sel_y - 20;
    if (sel_y + sel_h - C.scroll_y > bottom - top) C.scroll_y = sel_y + sel_h - (bottom - top);
    C.scroll_y = std::max(0.0f, std::min(C.scroll_y, std::max(0.0f, total - (bottom - top))));
    if (mouse_in(f, PX, top, PW, bottom - top)) C.scroll_y = std::max(0.0f, std::min(C.scroll_y - ImGui::GetIO().MouseWheel * 24,
                                                                                    std::max(0.0f, total - (bottom - top))));
    dl->PushClipRect(f.p(PX, top), f.p(PX + PW, bottom), true);
    float y = top - C.scroll_y;
    int pi = 0;
    for (int i = 0; i < (int)rows.size(); i++) {
        const FriendRow &w = rows[(size_t)i];
        if (!w.kind) {
            text(dl, f, g_title, 9, PX + 22, y + 6, DIM, w.title.c_str(), LEFT, 0);
            y += 20;
            continue;
        }
        bool on = &w == cur, offline = w.kind == 3 && w.fr->state == "offline";
        float x0 = PX + 13, x1 = PX + PW - 13, h = 32;
        if (w.kind == 1) rect(dl, f, x0, y, x1 - x0, h, IM_COL32(255, 205, 60, 18), 6);
        if (on) {
            rect(dl, f, x0, y, x1 - x0, h, IM_COL32(200, 16, 46, 26), 6);
            dl->AddRect(f.p(x0, y), f.p(x1, y + h), with_alpha(RED, 0.75f), 6 * f.s(), 0, 1.0f * f.s());
        }
        float a = offline && !on ? 0.55f : 1.0f, x = x0 + 9;
        draw_avatar(dl, f, x + 10, y + h / 2, 10, w.ref.id, w.ref.name, a);
        if (w.kind == 3) {      // the state's dot on the picture's edge
            ImU32 dot = offline ? IM_COL32(120, 112, 100, 255) : w.fr->state == "fighting" || w.fr->state == "watching" ? ORANGE : LIVE;
            dl->AddCircleFilled(f.p(x + 17, y + h / 2 + 7), 3.6f * f.s(), IM_COL32(18, 12, 14, 255));
            dl->AddCircleFilled(f.p(x + 17, y + h / 2 + 7), 2.6f * f.s(), with_alpha(dot, a));
        }
        x += 26;
        text(dl, f, g_hand, 14, x, y + 7, with_alpha(CREAM, a), w.ref.name.c_str(), LEFT, 0.6f);
        x += text_width(f, g_hand, 14, w.ref.name.c_str()) + 8;
        if (!w.ref.tag.empty()) {
            float tw = text_width(f, g_body, 7.5f, w.ref.tag.c_str()) + 6;
            dl->AddRect(f.p(x, y + 10), f.p(x + tw, y + 21), with_alpha(IM_COL32(255, 205, 60, 150), a), 3 * f.s(), 0, 0.8f * f.s());
            text(dl, f, g_body, 7.5f, x + tw / 2, y + 10.5f, with_alpha(GOLD_TEXT, a), w.ref.tag.c_str(), CENTER, 0);
            x += tw + 7;
        }
        std::string state;
        ImU32 scol = DIM;
        if (w.kind == 1) state = "wants to be your friend";
        else if (w.kind == 2) state = "request sent";
        else state = friend_state(*w.fr, &scol);
        text(dl, f, g_body, 9.5f, x, y + 10, with_alpha(scol, a), state.c_str(), LEFT, 0);
        // the buttons, right to left; an offline friend's only on the chosen row
        if (!w.acts.empty() && (on || !offline)) {
            float bx = x1 - 7;
            for (int b = (int)w.acts.size() - 1; b >= 0; b--) {
                const char *label = act_label(w.acts[(size_t)b]);
                float bw = text_width(f, g_title, 8.5f, label) + 14;
                bx -= bw;
                bool hot = on && C.btn == b, primary = w.acts[(size_t)b] == ACT_ACCEPT || w.acts[(size_t)b] == ACT_JOIN ||
                                                       w.acts[(size_t)b] == ACT_SURE;
                ImU32 fill = hot ? RED : primary ? IM_COL32(200, 16, 46, 170) : IM_COL32(255, 255, 255, 20);
                rect(dl, f, bx, y + 6, bw, 20, fill, 5);
                if (hot) dl->AddRect(f.p(bx - 1, y + 5), f.p(bx + bw + 1, y + 27), IM_COL32(255, 90, 110, 255), 5 * f.s(), 0,
                                     1.0f * f.s());
                text(dl, f, g_title, 8.5f, bx + bw / 2, y + 11, hot || primary ? IM_COL32(255, 245, 230, 255) : CREAM, label,
                     CENTER, 0);
                if (mouse_click(f, bx, y + 6, bw, 20)) { sel = pi; C.btn = b; do_friend_act(w, w.acts[(size_t)b], r); }
                bx -= 5;
            }
        }
        if (mouse_click(f, x0, y, x1 - x0, h) && sel != pi) { sel = pi; C.btn = 0; C.confirm_remove = 0; }
        y += h + 2;
        pi++;
    }
    dl->PopClipRect();
    if (rows.empty())
        text(dl, f, g_body, 11, PX + PW / 2, PY + 150, DIM, pad_text("No friends yet. Press {y} to add one by name.").c_str(),
             CENTER, 0, 380);
    if (cur && k.a && !cur->acts.empty()) do_friend_act(*cur, cur->acts[(size_t)C.btn], r);
    if (cur && k.x && cur->kind == 3) open_dm(cur->ref.id);
}

// ---- MESSAGES ----

void draw_messages(ImDrawList *dl, const Frame &f, const ChatKeys &k, const NetRoom &r)
{
    const social::View &v = C.v;
    float lw = 165;
    rect(dl, f, PX + lw, TABS_B, 0.5f, PY + PH - TABS_B, LINE_SEP);
    int n = (int)v.convos.size();
    int &sel = C.sel[TAB_MESSAGES];
    // the shown conversation stays chosen when the list's order changes (or is not in it yet: a first
    // message to a friend)
    sel = -1;
    for (int i = 0; i < n; i++) if (C.dm_with && v.convos[(size_t)i].with.id == C.dm_with) sel = i;
    bool moved = k.up || k.down;
    if (k.up) sel = sel < 0 ? 0 : sel - 1;
    if (k.down) sel++;
    sel = n ? std::max(0, std::min(sel, n - 1)) : 0;
    if (n && (moved || !C.dm_with) && v.convos[(size_t)sel].with.id != C.dm_with) {
        C.dm_with = v.convos[(size_t)sel].with.id;
        social::open_thread(C.dm_with);
    }
    float y = TABS_B + 7;
    for (int i = 0; i < n && y < PY + PH - 50; i++) {
        const social::Convo &c = v.convos[(size_t)i];
        bool on = i == sel;
        if (on) {
            rect(dl, f, PX + 7, y, lw - 14, 32, IM_COL32(200, 16, 46, 26), 6);
            dl->AddRect(f.p(PX + 7, y), f.p(PX + lw - 7, y + 32), with_alpha(RED, 0.75f), 6 * f.s(), 0, 1.0f * f.s());
        }
        draw_avatar(dl, f, PX + 23, y + 16, 9, c.with.id, c.with.name);
        text(dl, f, g_hand, 13, PX + 37, y + 1, CREAM, c.with.name.c_str(), LEFT, 0.6f);
        if (c.unread) badge(dl, f, PX + lw - 26, y + 4, c.unread);
        std::string snip = (c.last_from == v.me ? "You: " : "") + c.last;
        dl->PushClipRect(f.p(PX + 36, y), f.p(PX + lw - 12, y + 32), true);
        text(dl, f, g_body, 9, PX + 37, y + 18, IM_COL32(242, 230, 200, 140), snip.c_str(), LEFT, 0);
        dl->PopClipRect();
        if (mouse_click(f, PX + 7, y, lw - 14, 32)) { sel = i; C.dm_with = c.with.id; social::open_thread(c.with.id); }
        y += 35;
    }
    // who may message you (stage 2c): LT/RT or a click change it; the service keeps it
    {
        static const char *const ids[3] = { "friends", "everyone", "off" };
        static const char *const names[3] = { "FRIENDS", "EVERYONE", "NOBODY" };
        int cur = v.dms == "everyone" ? 1 : v.dms == "off" ? 2 : 0;
        float py = PY + PH - 44;
        text(dl, f, g_title, 8, PX + 12, py, DIM, "MESSAGES FROM", LEFT, 0);
        float cw = text_width(f, g_title, 9, names[cur]) + 26;
        rect(dl, f, PX + 12, py + 12, cw, 16, IM_COL32(255, 255, 255, 20), 8);
        text(dl, f, g_title, 9, PX + 18, py + 15, CREAM, "<", LEFT, 0);
        text(dl, f, g_title, 9, PX + 12 + cw / 2, py + 15, GOLD_TEXT, names[cur], CENTER, 0);
        text(dl, f, g_title, 9, PX + 6 + cw, py + 15, CREAM, ">", RIGHT, 0);
        plate_hint(dl, f, PX + 18 + cw, py + 14, ps_icons() ? "L2/R2" : "LT/RT", "");
        int next = -1;
        if (k.lt) next = (cur + 2) % 3;
        if (k.rt || mouse_click(f, PX + 12, py + 12, cw, 16)) next = (cur + 1) % 3;
        if (next >= 0) {
            social::set_dms(ids[next]);
            fprintf(stderr, "[SOCIAL-UI] messages from: %s\n", ids[next]);
        }
        const char *policy = cur == 1 ? "Anyone can message you." : cur == 2 ? "Nobody can message you." :
                             "Only friends can message you.";
        text(dl, f, g_body, 7.5f, PX + 12, py + 31, IM_COL32(242, 230, 200, 110), policy, LEFT, 0, lw - 24);
    }
    float rx = PX + lw, rw = PW - lw;
    if (!C.dm_with) {
        text(dl, f, g_body, 11, rx + rw / 2, PY + 150, DIM,
             pad_text("No messages yet. Pick a friend in FRIENDS and press {x} to write.").c_str(), CENTER, 0, 300);
        return;
    }
    // the header: who, and what they do
    const social::Friend *fr = friend_of(C.dm_with);
    std::string name = convo_name(C.dm_with);
    float hy = TABS_B + 8;
    ImU32 scol = DIM;
    std::string state = fr ? friend_state(*fr, &scol) : "not your friend";
    draw_avatar(dl, f, rx + 20, hy + 7, 8.5f, C.dm_with, name);
    dl->AddCircleFilled(f.p(rx + 26, hy + 13), 3.2f * f.s(), IM_COL32(18, 12, 14, 255));
    dl->AddCircleFilled(f.p(rx + 26, hy + 13), 2.3f * f.s(), fr && fr->state != "offline" ? LIVE : IM_COL32(120, 112, 100, 255));
    text(dl, f, g_hand, 16, rx + 33, hy - 2, CREAM, name.c_str(), LEFT, 0.6f);
    float nx = rx + 39 + text_width(f, g_hand, 16, name.c_str());
    if (fr && !fr->tag.empty()) {
        float tw = text_width(f, g_body, 7.5f, fr->tag.c_str()) + 6;
        dl->AddRect(f.p(nx, hy + 2), f.p(nx + tw, hy + 13), IM_COL32(255, 205, 60, 150), 3 * f.s(), 0, 0.8f * f.s());
        text(dl, f, g_body, 7.5f, nx + tw / 2, hy + 2.5f, GOLD_TEXT, fr->tag.c_str(), CENTER, 0);
        nx += tw + 6;
    }
    text(dl, f, g_body, 9, nx, hy + 2, scol, state.c_str(), LEFT, 0);
    // BLOCK - REPORT, as the design has it: X or a click opens the player's menu with both
    {
        const char *opts = "BLOCK - REPORT";
        float ow = text_width(f, g_title, 9, opts);
        bool hot = mouse_in(f, rx + rw - 12 - ow, hy, ow, 14);
        text(dl, f, g_title, 9, rx + rw - 12, hy + 2, hot ? CREAM : DIM, opts, RIGHT, 0);
        if (k.x || mouse_click(f, rx + rw - 12 - ow, hy, ow, 14)) {
            open_player_menu(C.dm_with, name);
            menu_report_newest(C.dm_with, name, false);
        }
    }
    rect(dl, f, rx, hy + 21, rw, 0.5f, IM_COL32(255, 255, 255, 15));
    // the bubbles, newest at the bottom
    const social::Thread *t = nullptr;
    for (auto &x : v.threads) if (x.with == C.dm_with) t = &x;
    float top = hy + 24, y1 = BAR_Y - 6, by = y1;
    dl->PushClipRect(f.p(rx, top), f.p(rx + rw, BAR_Y), true);
    if (t && t->loaded) {
        // "Seen 21:44" under my newest message once they read it
        long long last_mine = 0;
        for (auto &m : t->msgs) if (m.from == v.me) last_mine = m.id;
        bool seen = last_mine && t->seen_up_to >= last_mine && (t->msgs.empty() || t->msgs.back().from == v.me);
        if (seen) {
            std::string s = "Seen " + clock_text(t->seen_at);
            by -= 12;
            text(dl, f, g_body, 8, rx + rw / 2, by, DIM, s.c_str(), CENTER, 0);
            by -= 3;
        }
        for (int i = (int)t->msgs.size() - 1; i >= 0 && by > top; i--) {
            const social::Dm &m = t->msgs[(size_t)i];
            bool mine = m.from == v.me;
            float maxw = 230, tw = std::min(maxw - 16, text_width(f, g_body, 10.5f, m.text.c_str()) + 1);
            float th = text_height(f, g_body, 10.5f, m.text.c_str(), tw + 0.5f);
            float bw = tw + 16, bh = th + 12, bx = mine ? rx + rw - 13 - bw : rx + 13;
            by -= bh;
            // round but for the speaker's lower corner, as in the design
            dl->AddRectFilled(f.p(bx, by), f.p(bx + bw, by + bh), mine ? BUBBLE_ME : IM_COL32(255, 255, 255, 20), 8 * f.s(),
                              ImDrawFlags_RoundCornersTop | (mine ? ImDrawFlags_RoundCornersBottomLeft : ImDrawFlags_RoundCornersBottomRight));
            text(dl, f, g_body, 10.5f, bx + 8, by + 6, mine ? IM_COL32(255, 245, 230, 255) : CREAM, m.text.c_str(), LEFT, 0,
                 tw + 0.5f);
            by -= 5;
        }
        if (t->msgs.empty())
            text(dl, f, g_body, 10, rx + rw / 2, PY + 160, DIM, ("Say hi to " + name + ".").c_str(), CENTER, 0);
    } else {
        text(dl, f, g_body, 10, rx + rw / 2, PY + 160, DIM, "Loading...", CENTER, 0);
    }
    dl->PopClipRect();
    // viewing it reads it
    for (auto &c : v.convos) if (c.with.id == C.dm_with && c.unread) social::mark_read(C.dm_with);
    if (t && t->loaded && !t->can_message) {
        rect(dl, f, rx, BAR_Y, rw, 0.5f, LINE_SEP);
        text(dl, f, g_body, 10, rx + rw / 2, BAR_Y + 14, DIM, (name + " takes no messages from you right now.").c_str(), CENTER, 0);
        return;
    }
    // with a room and an empty box the button invites (the design's INVITE TO ROOM; chat_draw sends it)
    input_bar(dl, f, rx, "Message " + name, r.in_room && C.draft[TAB_MESSAGES].empty() ? "INVITE TO ROOM" : "SEND");
}

// ---- the player menu: A on a name ----

void draw_player_menu(ImDrawList *dl, const Frame &f, const ChatKeys &k)
{
    // 0 add friend, 1 message, 2 mute, 3 unmute, 4/5 room mute/unmute, 6/7 block/unblock, 8 report,
    // 9 invite to my crew
    std::vector<std::pair<std::string, int>> items;
    bool is_friend = C.menu_id && friend_of(C.menu_id), muted = false, asked = false, blocked = false;
    for (auto &m : C.v.muted) muted |= m.id == C.menu_id;
    for (auto &b : C.v.blocked) blocked |= C.menu_id && b.id == C.menu_id;
    for (auto &o : C.v.out) asked |= C.menu_id ? o.id == C.menu_id : o.name == C.menu_name;
    if (C.menu_room) {      // the ROOM tab: hide this player's room lines on this PC
        bool rm = room_name_muted(C.menu_name);
        for (unsigned id : C.room_muted_ids) rm |= C.menu_account && id == C.menu_account;
        items.push_back({ rm ? "UNMUTE IN ROOM" : "MUTE IN ROOM", rm ? 5 : 4 });
    }
    if (!room_only()) {
        if (!is_friend && !asked && !blocked) items.push_back({ "ADD FRIEND", 0 });
        if (C.menu_id && !blocked && C.tab != TAB_MESSAGES) items.push_back({ "MESSAGE", 1 });
        if (C.menu_id && !C.menu_room && !blocked && C.tab != TAB_MESSAGES) items.push_back({ muted ? "UNMUTE" : "MUTE", muted ? 3 : 2 });
        bool officer = C.v.crew_role == "boss" || C.v.crew_role == "lieutenant";
        bool in_mine = false;
        for (auto &m : C.v.crew_roster) in_mine |= m.id == C.menu_id;
        if (C.menu_id && officer && !in_mine && !blocked) items.push_back({ "INVITE TO MY CREW", 9 });
        if (C.menu_crew && C.v.crew_role == "boss") {       // 10 lieutenant, 11 member, 12 boss, 13 remove
            if (C.menu_role == "member") items.push_back({ "MAKE LIEUTENANT", 10 });
            if (C.menu_role == "lieutenant") items.push_back({ "MAKE MEMBER", 11 });
            items.push_back({ "MAKE BOSS", 12 });
            items.push_back({ "REMOVE FROM CREW", 13 });
        } else if (C.menu_crew && C.v.crew_role == "lieutenant" && C.menu_role == "member") {
            items.push_back({ "REMOVE FROM CREW", 13 });
        }
        if (C.menu_id) items.push_back({ blocked ? "UNBLOCK" : "BLOCK", blocked ? 7 : 6 });
        if (C.menu_id && !C.menu_channel.empty() && (C.menu_msg || !C.menu_text.empty())) items.push_back({ "REPORT", 8 });
    }
    if (items.empty()) items.push_back({ "FRIEND REQUEST SENT", -1 });
    float w = 160, h = 34 + items.size() * 22.0f, x = 320 - w / 2, y = 200 - h / 2;
    rect(dl, f, PX, PY, PW, PH, IM_COL32(0, 0, 0, 120), 8);
    chat_panel(dl, f, x, y, w, h, 0.95f);
    text(dl, f, g_hand, 15, x + w / 2, y + 6, CREAM, C.menu_name.c_str(), CENTER, 0.6f);
    if (k.up) C.menu_sel = std::max(0, C.menu_sel - 1);
    if (k.down) C.menu_sel = std::min((int)items.size() - 1, C.menu_sel + 1);
    for (int i = 0; i < (int)items.size(); i++) {
        float iy = y + 28 + i * 22.0f;
        bool on = i == C.menu_sel;
        rect(dl, f, x + 10, iy, w - 20, 19, on ? RED : IM_COL32(255, 255, 255, 20), 5);
        text(dl, f, g_title, 9.5f, x + w / 2, iy + 4, on ? IM_COL32(255, 245, 230, 255) : CREAM, items[(size_t)i].first.c_str(),
             CENTER, 0);
        bool click = mouse_click(f, x + 10, iy, w - 20, 19);
        if (click) C.menu_sel = i;
        if ((on && k.a) || click) {
            int what = items[(size_t)i].second;
            if (what == 0) social::friend_request(C.menu_id, C.menu_name);
            else if (what == 1) open_dm(C.menu_id);
            else if (what == 2 || what == 3) social::mute(C.menu_id, what == 2);
            else if (what == 4 || what == 5) room_mute(C.menu_name, C.menu_account, what == 4);
            else if (what == 6 || what == 7) {
                social::block(C.menu_id, C.menu_name, what == 6);
                fprintf(stderr, "[SOCIAL-UI] %s %s\n", what == 6 ? "block" : "unblock", C.menu_name.c_str());
            } else if (what == 8) {
                social::report(C.menu_id, C.menu_channel, C.menu_msg, C.menu_text, C.menu_context);
                fprintf(stderr, "[SOCIAL-UI] report %s (%s)\n", C.menu_name.c_str(), C.menu_channel.c_str());
            } else if (what == 9) social::crew_invite(C.menu_id, C.menu_name);
            else if (what >= 10 && what <= 12) {
                const char *role = what == 10 ? "lieutenant" : what == 11 ? "member" : "boss";
                social::crew_role(C.menu_id, role);
                fprintf(stderr, "[SOCIAL-UI] crew: %s -> %s\n", C.menu_name.c_str(), role);
            } else if (what == 13) {
                social::crew_kick(C.menu_id);
                fprintf(stderr, "[SOCIAL-UI] crew: remove %s\n", C.menu_name.c_str());
            }
            C.menu = false;
        }
    }
    if (k.b || k.esc) C.menu = false;
}

// ---- the on-screen keyboard ----

struct Key { std::string label; int kind; float span; };     // kind: 0 a character (label), 1.. the specials below
enum { K_CHAR, K_SHIFT, K_BKSP, K_SYM, K_DOT, K_SPACE, K_GG, K_SEND };

const std::vector<std::vector<Key>> &key_rows(bool sym)
{
    static std::vector<std::vector<Key>> letters, symbols;
    if (letters.empty()) {
        auto row = [](const char *chars) {
            std::vector<Key> r;
            for (const char *c = chars; *c; c++) r.push_back({ std::string(1, *c), K_CHAR, 1 });
            return r;
        };
        auto bottom = [](const char *sym_label) {
            return std::vector<Key>{ { sym_label, K_SYM, 1.2f }, { ",  .", K_DOT, 1 }, { "SPACE", K_SPACE, 4 },
                                     { "GG", K_GG, 1 }, { "SEND", K_SEND, 1.6f } };
        };
        letters.push_back(row("1234567890"));
        letters.push_back(row("QWERTYUIOP"));
        letters.push_back(row("ASDFGHJKL'"));
        std::vector<Key> r3 = row("ZXCVBNM?");
        r3.insert(r3.begin(), Key{ "SHIFT", K_SHIFT, 1 });
        r3.push_back(Key{ "DEL", K_BKSP, 1 });
        letters.push_back(r3);
        letters.push_back(bottom("@#!"));
        symbols.push_back(row("1234567890"));
        symbols.push_back(row("!@#$%&*()-"));
        symbols.push_back(row("_+=/:;\"<>'"));
        std::vector<Key> s3 = row("[]{}^|~?");
        s3.insert(s3.begin(), Key{ "SHIFT", K_SHIFT, 1 });
        s3.push_back(Key{ "DEL", K_BKSP, 1 });
        symbols.push_back(s3);
        symbols.push_back(bottom("ABC"));
    }
    return sym ? symbols : letters;
}

std::string &kb_text() { return C.kb_kind == KB_FIGHT ? fight_draft() : C.kb_kind != KB_CHAT ? C.friend_name : C.draft[C.tab]; }
int &kb_cursor() { return C.kb_kind == KB_FIGHT ? fight_cursor() : C.kb_kind != KB_CHAT ? C.friend_cursor : C.cursor[C.tab]; }
int kb_max()
{
    return C.kb_kind == KB_FIGHT ? 200 : C.kb_kind == KB_CREW_TAG ? 4 : C.kb_kind == KB_CREW_NAME ? 24 :
           C.kb_kind != KB_CHAT ? 32 : std::max(1, C.v.max_chars);
}

void open_keyboard(int kind)
{
    C.kb = true;
    C.popup_frame = ImGui::GetFrameCount();
    C.kb_kind = kind;
    C.kr = 2;
    C.kc = 7;
    C.symbols = false;
    if (kind != KB_CHAT && kind != KB_FIGHT) { C.friend_name.clear(); C.friend_cursor = 0; }
    if (kind == KB_CREW_TAG) C.caps = true;     // tags are capitals
}

void kb_submit()
{
    if (C.kb_kind != KB_CHAT) {
        std::string n = C.friend_name;
        while (!n.empty() && n.back() == ' ') n.pop_back();
        while (!n.empty() && n.front() == ' ') n.erase(n.begin());
        int kind = C.kb_kind;
        C.kb = false;
        if (n.empty()) return;
        if (kind == KB_FRIEND) social::friend_request(0, n);
        else if (kind == KB_CREW_INVITE) {
            social::crew_invite(0, n);
            fprintf(stderr, "[SOCIAL-UI] crew: invite %s\n", n.c_str());
        } else if (kind == KB_CREW_NAME) {      // then the tag
            C.crew_new_name = n;
            open_keyboard(KB_CREW_TAG);
        } else if (kind == KB_CREW_TAG) {
            for (auto &c : n) c = (char)toupper((unsigned char)c);
            std::string region = social::region_of_server(connected_server());
            social::crew_create(C.crew_new_name, n, region);
            fprintf(stderr, "[SOCIAL-UI] crew: create %s [%s] %s\n", C.crew_new_name.c_str(), n.c_str(), region.c_str());
            C.caps = false;
        }
        return;
    }
    chat_send();
    C.kb = false;
}

// Word completions from what the chat shows, then quick phrases.
std::vector<std::string> suggestions()
{
    const std::string &s = kb_text();
    int cur = std::min(kb_cursor(), (int)s.size());
    int a = cur;
    while (a > 0 && s[(size_t)a - 1] != ' ') a--;
    std::string part = s.substr((size_t)a, (size_t)(cur - a));
    std::vector<std::string> out;
    if (!part.empty()) {
        std::string lp = part;
        for (auto &c : lp) c = (char)tolower((unsigned char)c);
        auto offer = [&](const std::string &text) {
            for (size_t i = 0; i < text.size() && out.size() < 2;) {
                size_t j = text.find(' ', i);
                std::string w = text.substr(i, j == std::string::npos ? std::string::npos : j - i);
                std::string lw = w;
                for (auto &c : lw) c = (char)tolower((unsigned char)c);
                while (!lw.empty() && strchr(",.!?", lw.back())) { lw.pop_back(); w.pop_back(); }
                if (lw.size() > lp.size() && !lw.compare(0, lp.size(), lp) &&
                    std::find(out.begin(), out.end(), w) == out.end())
                    out.push_back(w);
                if (j == std::string::npos) break;
                i = j + 1;
            }
        };
        for (auto &fr : C.v.friends) offer(fr.name);
        for (int i = (int)C.v.global.size() - 1; i >= 0 && out.size() < 2; i--) offer(C.v.global[(size_t)i].text);
        for (int i = (int)C.v.crew_chat.size() - 1; i >= 0 && out.size() < 2; i--) offer(C.v.crew_chat[(size_t)i].text);
        // then the words a fight chat uses most
        static const char *const common[] = {
            "back", "backup", "again", "anyone", "around", "best", "better", "came", "close", "come", "crew", "down",
            "every", "fight", "first", "friend", "from", "game", "good", "great", "have", "here", "join", "just",
            "king", "know", "later", "lets", "like", "match", "maybe", "more", "next", "nice", "night", "online",
            "play", "player", "please", "quick", "ready", "really", "rematch", "right", "room", "round", "run",
            "server", "show", "sorry", "start", "still", "thanks", "that", "there", "think", "time", "today",
            "tomorrow", "tonight", "wait", "want", "watch", "well", "what", "when", "where", "winner", "with",
            "would", "yeah", "your" };
        for (const char *w : common) if (out.size() < 2) offer(w);
    }
    if (C.kb_kind == KB_CHAT)
        for (const char *p : { "GG", "Rematch?", "One more", "Ready?", "Nice" })
            if (out.size() < 5) out.push_back(p);
    return out;
}

void apply_suggestion(const std::string &word, bool completion)
{
    std::string &s = kb_text();
    int &cur = kb_cursor();
    cur = std::min(cur, (int)s.size());
    if (completion) {
        int a = cur;
        while (a > 0 && s[(size_t)a - 1] != ' ') a--;
        s.erase((size_t)a, (size_t)(cur - a));
        cur = a;
        insert_text(s, cur, word + " ", kb_max());
    } else {
        std::string add = word;
        if (cur > 0 && s[(size_t)cur - 1] != ' ') add = " " + add;
        insert_text(s, cur, add, kb_max());
    }
}

void kb_press(const Key &key)
{
    std::string &s = kb_text();
    int &cur = kb_cursor();
    switch (key.kind) {
    case K_CHAR: {
        std::string c = key.label;
        if (!C.symbols && isalpha((unsigned char)c[0]) && !(C.shift || C.caps)) c[0] = (char)tolower((unsigned char)c[0]);
        insert_text(s, cur, c, kb_max());
        C.shift = false;
        break;
    }
    case K_SHIFT: C.shift = !C.shift; break;
    case K_BKSP: erase_back(s, cur); break;
    case K_SYM: C.symbols = !C.symbols; break;
    case K_DOT: {
        unsigned long long now = GetTickCount64();
        if (now - C.last_dot < 800 && cur > 0 && s[(size_t)cur - 1] == '.') { s[(size_t)cur - 1] = ','; C.last_dot = 0; }
        else { insert_text(s, cur, ".", kb_max()); C.last_dot = now; }
        break;
    }
    case K_SPACE: insert_text(s, cur, " ", kb_max()); break;
    case K_GG: insert_text(s, cur, cur > 0 && s[(size_t)cur - 1] != ' ' ? " GG" : "GG", kb_max()); break;
    case K_SEND: kb_submit(); break;
    }
}

void draw_keyboard(ImDrawList *dl, const Frame &f, ImVec2 display)
{
    bool fresh = C.popup_frame == ImGui::GetFrameCount();
    ChatKeys k = fresh ? ChatKeys() : chat_keys(true);
    std::string &s = kb_text();
    int &cur = kb_cursor();
    // typing on a real keyboard works here too
    std::string typed = fresh ? std::string() : typed_chars();
    if (!typed.empty()) insert_text(s, cur, typed, kb_max());
    if (k.bksp) erase_back(s, cur);
    if (k.home) cur = 0;
    if (k.end_) cur = (int)s.size();
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, true)) cur = utf8_prev(s, cur);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, true)) cur = utf8_next(s, cur);
    if (k.x) erase_back(s, cur);
    if (k.y) insert_text(s, cur, " ", kb_max());
    if (k.lt) C.caps = !C.caps;
    const auto &rows = key_rows(C.symbols);
    // moving: up and down keep the column under the same place
    auto centre = [&](int r, int c) {
        float total = 0, x = 0;
        for (auto &key : rows[(size_t)r]) total += key.span;
        for (int i = 0; i < c; i++) x += rows[(size_t)r][(size_t)i].span;
        return (x + rows[(size_t)r][(size_t)c].span / 2) / total;
    };
    auto nearest = [&](int r, float at) {
        int best = 0;
        float bd = 2;
        for (int c = 0; c < (int)rows[(size_t)r].size(); c++) {
            float d = fabsf(centre(r, c) - at);
            if (d < bd) { bd = d; best = c; }
        }
        return best;
    };
    C.kr = std::max(0, std::min(C.kr, (int)rows.size() - 1));
    C.kc = std::max(0, std::min(C.kc, (int)rows[(size_t)C.kr].size() - 1));
    if (k.left) C.kc = (C.kc + (int)rows[(size_t)C.kr].size() - 1) % (int)rows[(size_t)C.kr].size();
    if (k.right) C.kc = (C.kc + 1) % (int)rows[(size_t)C.kr].size();
    if (k.up || k.down) {
        float at = centre(C.kr, C.kc);
        C.kr = (C.kr + (k.up ? (int)rows.size() - 1 : 1)) % (int)rows.size();
        C.kc = nearest(C.kr, at);
    }

    // the picture: the chat dimmed under a glass panel
    dl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 140));
    float x = 75, y = 66, w = 490, h = 318, in = 17;
    glass(dl, f, x, y, w, h, 9, IM_COL32(18, 12, 14, 255), 0.85f);
    std::string who, head;
    if (C.kb_kind == KB_FRIEND) { head = "ADD FRIEND"; who = ""; }
    else if (C.kb_kind == KB_CREW_INVITE) { head = "INVITE TO"; who = C.v.crew_name; }
    else if (C.kb_kind == KB_CREW_NAME) { head = "NEW CREW - ITS NAME"; who = ""; }
    else if (C.kb_kind == KB_CREW_TAG) { head = "NEW CREW - ITS TAG"; who = C.crew_new_name; }
    else if (C.tab == TAB_MESSAGES) { head = "MESSAGE TO"; who = convo_name(C.dm_with); }
    else if (C.tab == TAB_CREW) { head = "MESSAGE TO"; who = "your crew"; }
    else { head = "MESSAGE TO"; who = C.region ? k_region_names[C.region] : "everyone"; }
    text(dl, f, g_title, 13, x + in, y + 14, IM_COL32(242, 230, 200, 180), head.c_str(), LEFT, 0);
    if (!who.empty())
        text(dl, f, g_hand, 17, x + in + text_width(f, g_title, 13, head.c_str()) + 6, y + 12, CREAM, who.c_str(), LEFT, 0.6f);
    char count[24];
    snprintf(count, sizeof(count), "%d / %d", utf8_runes(s), kb_max());
    text(dl, f, g_body, 9, x + w - in, y + 17, DIM, count, RIGHT, 0);
    // the text and its cursor
    float fy = y + 37, fw = w - 2 * in;
    rect(dl, f, x + in, fy, fw, 32, IM_COL32(0, 0, 0, 128), 6);
    dl->AddRect(f.p(x + in, fy), f.p(x + in + fw, fy + 32), with_alpha(RED, 0.8f), 6 * f.s(), 0, 1.0f * f.s());
    cur = std::max(0, std::min(cur, (int)s.size()));
    std::string before = s.substr(0, (size_t)cur);
    float cw = text_width(f, g_body, 14, before.c_str()), shift = std::max(0.0f, cw - (fw - 24));
    dl->PushClipRect(f.p(x + in + 4, fy), f.p(x + in + fw - 4, fy + 32), true);
    if (s.empty() && C.kb_kind != KB_CHAT)
        text(dl, f, g_body, 14, x + in + 9, fy + 7, DIM, C.kb_kind == KB_CREW_NAME ? "3 to 24 letters, digits and spaces" :
             C.kb_kind == KB_CREW_TAG ? "2 to 4 letters or digits" : "Their name, exactly", LEFT, 0);
    text(dl, f, g_body, 14, x + in + 9 - shift, fy + 7, CREAM, s.c_str(), LEFT, 0);
    rect(dl, f, x + in + 9 - shift + cw + 1, fy + 8, 1.5f, 16, GOLD_TEXT);
    dl->PopClipRect();
    // suggestions: R2 takes the first
    std::vector<std::string> sug = suggestions();
    float sx = x + in, sy = fy + 41;
    int completions = 0;
    {
        const std::string &t = s;
        int a = std::min(cur, (int)t.size());
        while (a > 0 && t[(size_t)a - 1] != ' ') a--;
        if (a < cur) for (auto &w2 : sug) {
            std::string lw = w2;
            for (auto &c : lw) c = (char)tolower((unsigned char)c);
            std::string part = t.substr((size_t)a, (size_t)(cur - a));
            for (auto &c : part) c = (char)tolower((unsigned char)c);
            if (lw.size() > part.size() && !lw.compare(0, part.size(), part)) completions++;
        }
    }
    if (!sug.empty()) sx += plate_hint(dl, f, sx, sy + 2, ps_icons() ? "R2" : "RT", "") + 2;
    for (int i = 0; i < (int)sug.size(); i++) {
        float cwid = text_width(f, g_title, 9.5f, sug[(size_t)i].c_str()) + 16;
        if (sx + cwid > x + w - in) break;
        bool first = i == 0;
        rect(dl, f, sx, sy, cwid, 20, first ? GOLD_TEXT : IM_COL32(255, 205, 60, 30), 10);
        text(dl, f, g_title, 9.5f, sx + cwid / 2, sy + 4.5f, first ? IM_COL32(26, 18, 8, 255) : IM_COL32(255, 231, 166, 255),
             sug[(size_t)i].c_str(), CENTER, 0);
        if (mouse_click(f, sx, sy, cwid, 20)) apply_suggestion(sug[(size_t)i], i < completions);
        sx += cwid + 5;
    }
    if (k.rt && !sug.empty()) apply_suggestion(sug[0], completions > 0);
    // the keys
    float ky = sy + 31, kh = 31, gap = 4;
    for (int r = 0; r < (int)rows.size(); r++) {
        float total = 0;
        for (auto &key : rows[(size_t)r]) total += key.span;
        float unit = (fw - gap * (rows[(size_t)r].size() - 1)) / total, kx = x + in;
        for (int c = 0; c < (int)rows[(size_t)r].size(); c++) {
            const Key &key = rows[(size_t)r][(size_t)c];
            float kw = unit * key.span;
            bool on = r == C.kr && c == C.kc, wide = key.kind != K_CHAR;
            ImU32 fill = on ? RED : key.kind == K_SEND ? BUBBLE_ME : wide ? IM_COL32(255, 255, 255, 31) : IM_COL32(255, 255, 255, 20);
            if (key.kind == K_SHIFT && (C.shift || C.caps)) fill = on ? RED : IM_COL32(255, 205, 60, 70);
            if (on) {
                for (int g = 3; g >= 1; g--)
                    dl->AddRect(f.p(kx - g, ky + r * (kh + gap) - g), f.p(kx + kw + g, ky + r * (kh + gap) + kh + g),
                                IM_COL32(200, 16, 46, 40 * (4 - g)), (5 + g) * f.s(), 0, 1.5f * f.s());
            }
            rect(dl, f, kx, ky + r * (kh + gap), kw, kh, fill, 5);
            if (on) dl->AddRect(f.p(kx, ky + r * (kh + gap)), f.p(kx + kw, ky + r * (kh + gap) + kh), IM_COL32(255, 90, 110, 255),
                                5 * f.s(), 0, 1.5f * f.s());
            std::string label = key.label;      // capitals on the keys, as the design draws them
            if (key.kind == K_SEND && C.kb_kind == KB_FRIEND) label = "ADD";
            if (key.kind == K_SEND && C.kb_kind == KB_CREW_INVITE) label = "INVITE";
            if (key.kind == K_SEND && C.kb_kind == KB_CREW_NAME) label = "NEXT";
            if (key.kind == K_SEND && C.kb_kind == KB_CREW_TAG) label = "CREATE";
            if (key.kind == K_SHIFT && C.caps) label = "CAPS";
            float size = wide ? 10 : 13;
            if (key.kind == K_BKSP) {       // a backspace arrow
                ImVec2 c0 = f.p(kx + kw / 2, ky + r * (kh + gap) + kh / 2);
                float u = 4.5f * f.s();
                dl->AddTriangleFilled(ImVec2(c0.x - u * 1.6f, c0.y), ImVec2(c0.x - u * 0.6f, c0.y - u), ImVec2(c0.x - u * 0.6f, c0.y + u), CREAM);
                dl->AddRectFilled(ImVec2(c0.x - u * 0.6f, c0.y - u), ImVec2(c0.x + u * 1.6f, c0.y + u), CREAM, 1.5f * f.s());
                dl->AddLine(ImVec2(c0.x, c0.y - u * 0.5f), ImVec2(c0.x + u, c0.y + u * 0.5f), on ? RED : IM_COL32(40, 30, 30, 255), 1.2f * f.s());
                dl->AddLine(ImVec2(c0.x, c0.y + u * 0.5f), ImVec2(c0.x + u, c0.y - u * 0.5f), on ? RED : IM_COL32(40, 30, 30, 255), 1.2f * f.s());
            } else if (key.kind == K_SHIFT && !C.caps) {     // an up arrow
                ImVec2 c0 = f.p(kx + kw / 2, ky + r * (kh + gap) + kh / 2);
                float u = 5 * f.s();
                dl->AddTriangle(ImVec2(c0.x, c0.y - u), ImVec2(c0.x - u, c0.y), ImVec2(c0.x + u, c0.y), CREAM, 1.3f * f.s());
                dl->AddRect(ImVec2(c0.x - u * 0.45f, c0.y), ImVec2(c0.x + u * 0.45f, c0.y + u * 0.9f), CREAM, 0, 0, 1.3f * f.s());
            } else {
                text(dl, f, g_title, size, kx + kw / 2, ky + r * (kh + gap) + (kh - size * 1.15f) / 2, on ? IM_COL32(255, 245, 230, 255) : CREAM,
                     label.c_str(), CENTER, 0);
            }
            if (mouse_click(f, kx, ky + r * (kh + gap), kw, kh)) { C.kr = r; C.kc = c; kb_press(key); }
            kx += kw + gap;
        }
    }
    if (k.a) kb_press(rows[(size_t)C.kr][(size_t)C.kc]);
    if (k.start || k.enter) kb_submit();
    // the hints, as the design has them
    float hx = 75, hy = 420;
    hx += hint(dl, f, hx, hy, "a", "TYPE");
    hx += hint(dl, f, hx, hy, "x", "DELETE");
    hx += hint(dl, f, hx, hy, "y", "SPACE");
    hx += plate_hint(dl, f, hx, hy, ps_icons() ? "L2" : "LT", C.caps ? "CAPS OFF" : "CAPS");
    hx += plate_hint(dl, f, hx, hy, ps_icons() ? "L1" : "LB", "") - 2;
    hx += plate_hint(dl, f, hx, hy, ps_icons() ? "R1" : "RB", "CURSOR");
    hx += plate_hint(dl, f, hx, hy, start_button_name(), C.kb_kind == KB_FRIEND ? "ADD" : C.kb_kind == KB_CREW_INVITE ? "INVITE" :
                     C.kb_kind == KB_CREW_NAME ? "NEXT" : C.kb_kind == KB_CREW_TAG ? "CREATE" : "SEND");
    hint(dl, f, hx, hy, "b", "CLOSE");
    if (k.b || k.esc) C.kb = false;
}

// ---- the toasts ----

void draw_toasts(ImDrawList *dl, const Frame &f)
{
    for (auto &t : social::take_toasts()) {
        // the conversation on screen needs no toast
        if (t.kind == social::TOAST_DM && C.open && C.tab == TAB_MESSAGES && C.dm_with == t.dm_from) continue;
        if (gallery_on()) continue;
        C.toasts.push_back(t);
    }
    unsigned long long now = GetTickCount64();
    C.toasts.erase(std::remove_if(C.toasts.begin(), C.toasts.end(), [&](const social::Toast &t) { return now - t.at > 6000; }),
                   C.toasts.end());
    while (C.toasts.size() > 3) C.toasts.erase(C.toasts.begin());
    float y = 50;
    for (auto &t : C.toasts) {
        float age = (now - t.at) / 1000.0f, a = age < 0.25f ? age / 0.25f : age > 5.4f ? (6 - age) / 0.6f : 1;
        float w = 190, x = 606 - w + (1 - std::min(1.0f, age / 0.25f)) * 30;
        const char *kind = t.kind == social::TOAST_DM ? "MESSAGE" : t.kind == social::TOAST_REQUEST ? "FRIEND REQUEST" :
                           t.kind == social::TOAST_FRIEND ? "NEW FRIEND" : t.kind == social::TOAST_CREW ? "CREW" : "NOTICE";
        glass(dl, f, x, y, w, 38, 6, IM_COL32(18, 12, 14, 255), 0.92f * a);
        rect(dl, f, x, y, 3, 38, with_alpha(t.kind == social::TOAST_DM ? RED : GOLD_TEXT, a), 1.5f);
        text(dl, f, g_title, 8, x + 10, y + 4, with_alpha(t.kind == social::TOAST_DM ? IM_COL32(255, 110, 120, 255) : GOLD_TEXT, a),
             kind, LEFT, 0);
        text(dl, f, g_hand, 12, x + 10 + text_width(f, g_title, 8, kind) + 6, y + 1, with_alpha(CREAM, a), t.title.c_str(), LEFT, 0.5f);
        dl->PushClipRect(f.p(x + 8, y), f.p(x + w - 6, y + 38), true);
        text(dl, f, g_body, 9.5f, x + 10, y + 19, with_alpha(CREAM, a), t.text.c_str(), LEFT, 0);
        dl->PopClipRect();
        if (t.kind == social::TOAST_DM && S.page != CODE && S.page != NAME && mouse_click(f, x, y, w, 38)) {
            C.open = true;
            C.opened_at = GetTickCount64();
            open_dm(t.dm_from);
        }
        y += 43;
    }
}

// ---- tests: RECOMP_TEST_SOCIAL_UI ----
//
// "SECONDS:action;..." from the moment the social lists are loaded: chat=TAB (open the chat on that
// tab: room, global, friends, messages, crew), keyboard=TEXT (the on-screen keyboard over the tab with
// that text), friend=NAME (the keyboard of ADD FRIEND with that name), dm=NAME (MESSAGES with that
// friend), type=TEXT (the tab's text box), send, close, mute=NAME / unmute=NAME (MUTE IN ROOM); pad=BUTTON (a, b, x, y, lb, rb, start, back,
// up, down, left, right) and key=KEY (enter, escape, backspace, tab) press it for a frame as the pad's
// or keyboard's own press arrives, text=TEXT types it; room=create opens a room. Each step writes a
// [SOCIAL-UI] line.

ImGuiKey test_key(const std::string &name)
{
    static const struct { const char *name; ImGuiKey key; } keys[] = {
        { "a", ImGuiKey_GamepadFaceDown }, { "b", ImGuiKey_GamepadFaceRight }, { "x", ImGuiKey_GamepadFaceLeft },
        { "y", ImGuiKey_GamepadFaceUp }, { "lb", ImGuiKey_GamepadL1 }, { "rb", ImGuiKey_GamepadR1 },
        { "start", ImGuiKey_GamepadStart }, { "back", ImGuiKey_GamepadBack }, { "up", ImGuiKey_GamepadDpadUp },
        { "down", ImGuiKey_GamepadDpadDown }, { "left", ImGuiKey_GamepadDpadLeft }, { "right", ImGuiKey_GamepadDpadRight },
        { "enter", ImGuiKey_Enter }, { "escape", ImGuiKey_Escape }, { "backspace", ImGuiKey_Backspace }, { "tab", ImGuiKey_Tab },
    };
    for (auto &k : keys) if (name == k.name) return k.key;
    return ImGuiKey_None;
}

void chat_test_step()
{
    static std::vector<std::pair<float, std::string>> steps;
    static size_t next = 0;
    static bool parsed = false;
    static ImGuiKey held = ImGuiKey_None;      // a test press: down one frame, up the next
    if (held != ImGuiKey_None) { ImGui::GetIO().AddKeyEvent(held, false); held = ImGuiKey_None; }
    if (!parsed) {
        parsed = true;
        if (const char *e = getenv("RECOMP_TEST_SOCIAL_UI")) {
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
    if (next >= steps.size() || !C.ready_at) return;
    if ((GetTickCount64() - C.ready_at) / 1000.0f < steps[next].first) return;
    std::string step = steps[next++].second;
    std::string what = step.substr(0, step.find('=')), arg = step.find('=') == std::string::npos ? "" : step.substr(step.find('=') + 1);
    if (what == "chat") {
        C.open = C.opened_once = true;
        C.kb = C.menu = false;
        for (int t = 0; t < TAB_COUNT; t++) {
            std::string n = k_tab_names[t];
            for (auto &c : n) c = (char)tolower((unsigned char)c);
            if (n == arg) C.tab = t;
        }
    } else if (what == "keyboard") {
        C.open = true;
        open_keyboard(KB_CHAT);
        C.draft[C.tab] = arg;
        C.cursor[C.tab] = (int)arg.size();
    } else if (what == "friend") {
        C.open = true;
        C.tab = TAB_FRIENDS;
        open_keyboard(KB_FRIEND);
        C.friend_name = arg;
        C.friend_cursor = (int)arg.size();
    } else if (what == "dm") {
        for (auto &fr : C.v.friends) if (fr.name == arg) { C.open = true; C.kb = false; open_dm(fr.id); }
    } else if (what == "type") {
        C.draft[C.tab] = arg;
        C.cursor[C.tab] = (int)arg.size();
    } else if (what == "send") {
        if (C.kb) kb_submit(); else chat_send();
    } else if (what == "close") {
        C.open = C.kb = C.menu = false;
    } else if (what == "pad" || what == "key") {    // as the pad's or the keyboard's own press arrives
        held = test_key(arg);
        if (held != ImGuiKey_None) ImGui::GetIO().AddKeyEvent(held, true);
    } else if (what == "text") {
        ImGui::GetIO().AddInputCharactersUTF8(arg.c_str());
    } else if (what == "mute" || what == "unmute") {    // the ROOM tab's MUTE IN ROOM on that player
        room_mute(arg, 0, what == "mute");
    } else if (what == "room") {                    // a room of this copy's own (a presence other than the menu)
        NetRoomSettings rs;
        default_settings(rs);
        netplay_lobby_create(&rs);
    }
    fprintf(stderr, "[SOCIAL-UI] %s (tab %s, keyboard %d)\n", step.c_str(), k_tab_names[C.tab], (int)C.kb);
}

// ---- the window ----

// Every frame of the online screens, before the page: the copy of the social state, and ENTER or BACK
// opening the window. Returns true while the window has the input (the page then gets none).
bool chat_begin()
{
    if (!social_enabled()) {
        // room chat is the relay's: with the service's social features off the window still opens
        // in a room whose relay carries it, with the ROOM tab alone
        C.have = false;
        C.v = social::View();
        if (room_chat_state() < 0) {
            C.open = C.kb = C.menu = false;
            C.ready_at = 0;
            return false;
        }
        C.tab = TAB_ROOM;
        if (!C.ready_at) C.ready_at = GetTickCount64();
    } else {
        if (social::snapshot(C.v, C.have ? C.v.version : 0xFFFFFFFFu)) C.have = true;
        if (C.v.ready && !C.ready_at) C.ready_at = GetTickCount64();
        if (!C.v.ready) C.ready_at = 0;
        avatars_update();
    }
    chat_test_step();
    NetRoom r;
    lobby_room(&r);
    if (r.in_room) {        // the presence heartbeat names the room's mode (social.cpp)
        const char *mode = NETPLAY_IS_KOTH(r.settings.mode) ? "King of the Hill" : mode_name(r.settings.mode);
        social::set_mode_name(mode ? mode : "");
    }
    bool text_page = S.page == CODE || S.page == NAME;
    if (!C.open && !gallery_on() && !text_page) {
        bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        bool back = !back_is_ptt() && ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false);
        if (enter || back) {
            C.open = true;
            C.kb = C.menu = false;
            C.opened_at = GetTickCount64();
            if (r.in_room && !C.opened_once) C.tab = TAB_ROOM;      // the first time in a room: the room's tab
            C.opened_once = true;
            fprintf(stderr, "[SOCIAL-UI] chat opened (%s)\n", enter ? "Enter" : "Back");
        }
    }
    C.mouse_off = C.open;
    return C.open;
}

// The small CHAT chip on every online page while the window is shut: what opens it, and what is new.
void chat_chip(ImDrawList *dl, const Frame &f)
{
    // top centre: the corners carry the page title, the room's code and the vote's clock
    int n = C.v.unread + C.v.requests + room_unread();
    const char *key = "ENTER";
    float w = text_width(f, g_title, 10, "CHAT") + text_width(f, g_body, 9, key) + 26 + (n ? 16 : 0);
    float x = 320 - w / 2, y = 5;
    glass(dl, f, x, y, w, 17, 8, IM_COL32(10, 10, 13, 255), 0.62f);
    float px = x + 6;
    float pw = text_width(f, g_body, 9, key) + 6;
    rect(dl, f, px, y + 3, pw, 11, IM_COL32(70, 70, 70, 255), 3);
    text(dl, f, g_body, 9, px + pw / 2, y + 3.5f, WHITE, key, CENTER, 0);
    text(dl, f, g_title, 10, px + pw + 5, y + 3, CREAM, "CHAT", LEFT, 0.8f);
    if (n) badge(dl, f, px + pw + 9 + text_width(f, g_title, 10, "CHAT"), y + 3, n);
    if (!C.open && mouse_click(f, x, y, w, 17)) { C.open = true; C.opened_at = GetTickCount64(); }
}

// After the page: the window over it (when open), the toasts and the chip. Returns false when the
// player closed the online screens from here (never: B closes the window only).
void chat_draw(ImDrawList *dl, const Frame &f, ImVec2 display)
{
    if (!social_enabled() && room_chat_state() < 0) return;
    C.mouse_off = false;
    if (!C.open) {
        social_set_typing(0);
        if (S.page != CODE && S.page != NAME) chat_chip(dl, f);     // those pages type: no chat over them
        draw_toasts(dl, f);
        return;
    }
    NetRoom r;
    lobby_room(&r);
    if (!tab_shown(C.tab)) C.tab = TAB_GLOBAL;
    bool typing = (tab_types(C.tab) && !C.menu) || C.kb;
    social_set_typing(typing);
    ChatKeys k = C.kb || C.menu ? ChatKeys() : chat_keys(tab_types(C.tab));
    // the first frame takes no press: the ENTER that opened the window is still down
    if (GetTickCount64() - C.opened_at < 150) k = ChatKeys();

    chat_backdrop(dl, display, 1.0f);
    // the title: the room's code and server on ROOM, else ONLINE
    if (C.tab == TAB_ROOM && r.in_room) {
        page_title(dl, f, "ROOM");
        char code[32];
        snprintf(code, sizeof(code), "CODE %s", r.code);
        text(dl, f, g_title, 13, 592, 46, ORANGE, code, RIGHT, 0.8f);
        int cs = connected_server();
        NetServerInfo sv[NETSERVER_COUNT];
        servers(sv);
        if (cs >= 0) text(dl, f, g_title, 9, 592, 62, DIM, (upper(sv[cs].name) + " SERVER").c_str(), RIGHT, 0);
    } else {
        page_title(dl, f, "ONLINE");
    }
    chat_panel(dl, f, PX, PY, PW, PH);
    C.mouse_off = C.kb || C.menu;       // the keyboard or the player menu is over the tab
    if (!C.v.ready && !room_only()) {
        chat_tabs(dl, f);
        text(dl, f, g_body, 11, PX + PW / 2, PY + 160, DIM, "Connecting to the chat...", CENTER, 0);
    } else {
        if (k.lb) step_tab(-1);
        if (k.rb) step_tab(1);
        chat_tabs(dl, f);
        // typing into the tab's box
        if (tab_types(C.tab) && !C.kb && !C.menu) {
            std::string typed = typed_chars();
            std::string &d = C.draft[C.tab];
            int &cur = C.cursor[C.tab];
            if (!typed.empty()) { insert_text(d, cur, typed, std::max(1, C.v.max_chars)); C.sel[C.tab] = C.tab == TAB_MESSAGES ? C.sel[C.tab] : -1; }
            if (k.bksp) erase_back(d, cur);
            if (k.del && cur < (int)d.size()) d.erase((size_t)cur, (size_t)(utf8_next(d, cur) - cur));
            if (k.home) cur = 0;
            if (k.end_) cur = (int)d.size();
            if (!d.empty() && (C.sel[C.tab] < 0 || C.tab == TAB_MESSAGES)) {    // the arrows move the cursor in the text
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) { cur = utf8_prev(d, cur); k.left = false; }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) { cur = utf8_next(d, cur); k.right = false; }
            }
            if (k.enter) {
                if (!d.empty()) chat_send();
                else C.open = false;        // ENTER on an empty box closes, as in most games
                k.enter = false;
            }
        } else if (k.enter && !C.kb && !C.menu) {
            k.a = true;     // without a box ENTER chooses
        }
        // MESSAGES: an empty box with a room: the button invites
        if (C.tab == TAB_MESSAGES && C.dm_with && r.in_room && C.draft[C.tab].empty() && !C.kb && !C.menu &&
            mouse_click(f, PX + PW - 11 - (text_width(f, g_title, 11, "INVITE TO ROOM") + 22), BAR_Y + 8,
                        text_width(f, g_title, 11, "INVITE TO ROOM") + 22, 26)) {
            social::send_dm(C.dm_with, std::string("Join my room: code ") + r.code);
            say("Invite sent.");
        }
        switch (C.tab) {
        case TAB_ROOM: draw_room_tab(dl, f, k, r); break;
        case TAB_GLOBAL: draw_global(dl, f, k); break;
        case TAB_FRIENDS: draw_friends(dl, f, k, r); break;
        case TAB_MESSAGES: draw_messages(dl, f, k, r); break;
        default: draw_crew(dl, f, k); break;
        }
        C.mouse_off = false;
        if (C.menu) draw_player_menu(dl, f, C.popup_frame == ImGui::GetFrameCount() ? ChatKeys() : chat_keys(false));
        // the keyboard: Y, or A on the text box
        if (!C.kb && !C.menu) {
            if (k.y && C.tab == TAB_FRIENDS) open_keyboard(KB_FRIEND);
            else if (k.y && tab_types(C.tab)) open_keyboard(KB_CHAT);
            else if (k.a && tab_types(C.tab) && (C.sel[C.tab] < 0 || C.tab == TAB_MESSAGES)) open_keyboard(KB_CHAT);
        }
    }
    // the service's refusal ("slow mode: ..."), a few seconds
    if (!C.v.note.empty() && GetTickCount64() - C.v.note_at < 5000)
        text(dl, f, g_hand, 12, PX + 6, PY + PH + 4, ORANGE, C.v.note.c_str(), LEFT, 0.8f, PW);
    // the hints, per tab, as the design has them (the keyboard has its own)
    float hx = PX, hy = 438;
    if (C.kb) {
    } else if (C.tab == TAB_FRIENDS) {
        hx += hint(dl, f, hx, hy, "a", "JOIN / ACCEPT");
        hx += hint(dl, f, hx, hy, "x", "MESSAGE");
        hx += hint(dl, f, hx, hy, "y", "ADD FRIEND");
        hint(dl, f, hx, hy, "b", "CLOSE");
    } else if (C.tab == TAB_MESSAGES) {
        hx += hint(dl, f, hx, hy, "a", "OPEN");
        hx += hint(dl, f, hx, hy, "y", "KEYBOARD");
        if (C.dm_with) hx += hint(dl, f, hx, hy, "x", "BLOCK / REPORT");
        hx += plate_hint(dl, f, hx, hy, ps_icons() ? "L2/R2" : "LT/RT", "WHO CAN MESSAGE YOU");
        hint(dl, f, hx, hy, "b", "CLOSE");
    } else if (C.tab == TAB_GLOBAL) {
        hx += plate_hint(dl, f, hx, hy, "ENTER", "TYPE");
        hx += hint(dl, f, hx, hy, "a", "PLAYER");
        hx += plate_hint(dl, f, hx, hy, ps_icons() ? "L2/R2" : "LT/RT", "SERVER");
        hx += hint(dl, f, hx, hy, "y", "KEYBOARD");
        hint(dl, f, hx, hy, "b", "CLOSE");
        text(dl, f, g_body, 9, 592, hy + 3, IM_COL32(242, 230, 200, 128), "Signed-in players only - reports go to the team",
             RIGHT, 0);
    } else {
        if (tab_types(C.tab)) hx += plate_hint(dl, f, hx, hy, "ENTER", "TYPE");
        hx += hint(dl, f, hx, hy, "a", "SELECT");
        hx += hint(dl, f, hx, hy, "b", "CLOSE");
        if (tab_types(C.tab)) hint(dl, f, hx, hy, "y", "KEYBOARD");
        std::string how = std::string("Chat opens with ENTER") + (back_is_ptt() ? "" : std::string(" or ") + back_button_name()) +
                          ", in every online screen";
        if (!tab_types(C.tab)) text(dl, f, g_body, 9, 592, hy + 3, IM_COL32(242, 230, 200, 128), how.c_str(), RIGHT, 0);
    }
    if (C.kb) draw_keyboard(dl, f, display);
    draw_toasts(dl, f);
    if (!C.kb && !C.menu && (k.b || k.esc)) {
        if (C.tab == TAB_CREW && C.crew_view == 1 && C.v.crew) { C.crew_view = 0; C.sel[TAB_CREW] = -1; }
        else C.open = false;
    }
    if (!C.open) social_set_typing(0);
}

const char *chat_shot_label()
{
    if (!C.open || (!social_enabled() && room_chat_state() < 0)) return nullptr;
    return C.kb ? "social-keyboard" : k_tab_shots[C.tab];
}

// ---- chat during a fight (stage 2b; the design's "Chat during a fight") ----
//
// Over a room's match (the online screens closed): the room's last lines at the bottom left for a
// few seconds each, the newest brightest. Who may type, and why: only a spectator. A fighter's pad
// and keyboard feed the lockstep and rollback input every step, so a text box over the fight would
// have to take that input away from the fight mid-match, and any slip would put key presses into the
// fight or change what the match's input is. A spectator's PC sends nothing into the match (the relay
// forwards no spectator DATA), so its keys can go to a text box: ENTER opens the bar, ENTER sends, ESC
// cancels, and while it is open the game gets no input on that PC (pc_ui.cpp holds it, as for the
// overlay). Fighters see the lines and write in the room once the match is over. Nothing here is in
// the match's input or its checksums: the lines arrive on the network thread as T_CHAT and are only
// read here.

const unsigned long long FIGHT_LINE_MS = 8000;     // a line stays this long
const int FIGHT_LINES = 3;

struct FightChat {
    bool typing = false;
    std::string draft;
    int cursor = 0;
    unsigned long long opened = 0, fight_at = 0;
    // RECOMP_TEST_FIGHT_CHAT="S:TEXT": a spectator opens the bar S seconds into its fight, types TEXT
    // and sends it 1.5 s later (tests: the real bar and the held input)
    float test_s = -1;
    std::string test_text;
    int test_step = 0;
    bool parsed = false;
} FC;

bool fight_spectator(const NetRoom &r) { return r.in_room && r.you == NETPLAY_SLOT_SPECTATOR; }

// In a room's match, the online screens closed.
bool fight_chat_live()
{
    if (gallery_on()) return false;
    if (!(net_state() == NETPLAY_CONNECTED && net_in_lobby())) return false;
    return room_chat_state() >= 0;
}

bool fight_chat_wanted_impl()
{
    if (!fight_chat_live()) {
        FC.typing = false;
        FC.fight_at = 0;
        return false;
    }
    if (FC.typing) return true;
    NetRoom r;
    lobby_room(&r);
    if (fight_spectator(r) && room_chat_state() == 1) return true;     // its ENTER opens the bar
    static NetChatLine buf[NETPLAY_CHAT_LINES];
    int n = netplay_room_chat(buf, NETPLAY_CHAT_LINES, nullptr);
    unsigned long long now = GetTickCount64();
    for (int i = n - 1; i >= 0; i--)
        if (now - buf[i].at < FIGHT_LINE_MS && !room_muted(buf[i])) return true;
    return false;
}

void fight_chat_test(const NetRoom &r)
{
    if (!FC.parsed) {
        FC.parsed = true;
        if (const char *e = getenv("RECOMP_TEST_FIGHT_CHAT")) {
            const char *c = strchr(e, ':');
            if (c) { FC.test_s = (float)atof(e); FC.test_text = c + 1; }
        }
    }
    if (FC.test_s < 0 || FC.test_step >= 2 || !fight_spectator(r)) return;
    if (netplay_in_fight() && !FC.fight_at) FC.fight_at = GetTickCount64();
    if (!FC.fight_at) return;
    float t = (GetTickCount64() - FC.fight_at) / 1000.0f;
    if (FC.test_step == 0 && t >= FC.test_s && room_chat_state() == 1) {
        FC.typing = true;
        FC.opened = GetTickCount64();
        FC.draft = FC.test_text;
        FC.cursor = (int)FC.draft.size();
        FC.test_step = 1;
        fprintf(stderr, "[SOCIAL-UI] fight chat: test opened the bar\n");
    } else if (FC.test_step == 1 && t >= FC.test_s + 1.5f) {
        if (netplay_room_chat_send(FC.draft.c_str())) fprintf(stderr, "[SOCIAL-UI] fight chat sent: %s\n", FC.draft.c_str());
        FC.draft.clear();
        FC.cursor = 0;
        FC.typing = false;
        FC.test_step = 2;
    }
}

// Draws the lines (and a spectator's bar); returns true while the bar takes the keyboard.
bool fight_chat_draw_impl(ImDrawList *dl, ImVec2 display)
{
    Frame f = menu_frame(display);
    NetRoom r;
    lobby_room(&r);
    bool spectator = fight_spectator(r);
    fight_chat_test(r);
    if (!spectator || room_chat_state() != 1) FC.typing = false;
    if (spectator && !FC.typing && room_chat_state() == 1 && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        FC.typing = true;
        FC.opened = GetTickCount64();
        FC.draft.clear();
        FC.cursor = 0;
        fprintf(stderr, "[SOCIAL-UI] fight chat: bar opened (spectator)\n");
    }
    if (FC.typing && GetTickCount64() - FC.opened > 150) {
        std::string typed = typed_chars();
        if (!typed.empty()) insert_text(FC.draft, FC.cursor, typed, 200);
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) erase_back(FC.draft, FC.cursor);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            FC.typing = false;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            std::string t = FC.draft;
            while (!t.empty() && t.back() == ' ') t.pop_back();
            if (!t.empty() && t.size() <= 200 && netplay_room_chat_send(t.c_str()))
                fprintf(stderr, "[SOCIAL-UI] fight chat sent: %s\n", t.c_str());
            FC.draft.clear();
            FC.cursor = 0;
            FC.typing = false;
        }
    }
    social_set_typing(FC.typing);

    // the lines: the newest at the bottom, each a few seconds
    static NetChatLine buf[NETPLAY_CHAT_LINES];
    int n = netplay_room_chat(buf, NETPLAY_CHAT_LINES, nullptr);
    unsigned long long now = GetTickCount64();
    std::vector<const NetChatLine *> show;
    for (int i = n - 1; i >= 0 && (int)show.size() < (FC.typing ? 5 : FIGHT_LINES); i--) {
        if (room_muted(buf[i])) continue;
        if (!FC.typing && now - buf[i].at >= FIGHT_LINE_MS) break;
        show.push_back(&buf[i]);
    }
    float y = FC.typing ? 418 : 440;
    for (size_t i = 0; i < show.size(); i++) {
        const NetChatLine &l = *show[i];
        float age = (float)(now - l.at);
        float a = i == 0 ? 1.0f : i == 1 ? 0.75f : 0.45f;
        if (!FC.typing && age > FIGHT_LINE_MS - 1500) a *= std::max(0.0f, (FIGHT_LINE_MS - age) / 1500.0f);
        if (a <= 0.01f) continue;
        const char *name = l.system ? "" : l.name;
        float nw = l.system ? 0 : text_width(f, g_hand, 11, name) + 5;
        float tw = std::min(250.0f, text_width(f, g_body, 12, l.text));
        float th = std::max(14.0f, text_height(f, g_body, 12, l.text, 250));
        float h = th + 7, w = nw + tw + 14;
        y -= h;
        rect(dl, f, 20, y, w, h, IM_COL32(0, 0, 0, (int)(107 * a)), 5);
        ImU32 ncol = l.mine ? GOLD_TEXT : l.slot >= 0 && l.slot < 32 ? BLUE_NAME : IM_COL32(242, 230, 200, 217);
        if (!l.system) text(dl, f, g_hand, 11, 27, y + 2, with_alpha(ncol, a), name, LEFT, 0);
        text(dl, f, g_body, 12, 27 + nw, y + 2.5f, with_alpha(l.system ? ORANGE : CREAM, a), l.text, LEFT, 0, 250);
        y -= 4;
    }
    if (FC.typing) {    // the bar, as the design has it
        float bx = 20, by = 430, bw = 280, bh = 27;
        rect(dl, f, bx, by, bw, bh, IM_COL32(0, 0, 0, 166), 6);
        dl->AddRect(f.p(bx, by), f.p(bx + bw, by + bh), with_alpha(RED, 0.85f), 6 * f.s(), 0, 1.0f * f.s());
        text(dl, f, g_title, 9, bx + 8, by + 8, GOLD_TEXT, "ROOM", LEFT, 0);
        float lx = bx + 12 + text_width(f, g_title, 9, "ROOM");
        const char *keys = "ENTER SENDS - ESC CANCELS";
        float kw = text_width(f, g_body, 7.5f, keys);
        dl->PushClipRect(f.p(lx, by), f.p(bx + bw - kw - 14, by + bh), true);
        std::string before = FC.draft.substr(0, (size_t)std::max(0, std::min(FC.cursor, (int)FC.draft.size())));
        float cw = text_width(f, g_body, 11, before.c_str()), shift = std::max(0.0f, cw - (bw - kw - 40 - (lx - bx)));
        text(dl, f, g_body, 11, lx - shift, by + 6.5f, CREAM, FC.draft.c_str(), LEFT, 0);
        if ((GetTickCount64() / 500) % 2 == 0) rect(dl, f, lx - shift + cw + 1, by + 6, 1.5f, 14, GOLD_TEXT);
        dl->PopClipRect();
        text(dl, f, g_body, 7.5f, bx + bw - 8, by + 10, IM_COL32(242, 230, 200, 153), keys, RIGHT, 0);
        text(dl, f, g_body, 8.5f, bx + bw + 10, by + 9, IM_COL32(242, 230, 200, 140),
             "While you type, the keyboard does not control the game", LEFT, 0);
    } else if (spectator && room_chat_state() == 1) {
        float x = 20, yy = 452;
        float pw = text_width(f, g_body, 8, "ENTER") + 6;
        rect(dl, f, x, yy, pw + text_width(f, g_title, 8, "ROOM CHAT") + 12, 13, IM_COL32(0, 0, 0, 110), 4);
        rect(dl, f, x + 3, yy + 2, pw, 9, IM_COL32(70, 70, 70, 220), 2);
        text(dl, f, g_body, 8, x + 3 + pw / 2, yy + 2, WHITE, "ENTER", CENTER, 0);
        text(dl, f, g_title, 8, x + 7 + pw, yy + 2.5f, IM_COL32(242, 230, 200, 200), "ROOM CHAT", LEFT, 0);
    }
    return FC.typing;
}

const char *fight_chat_shot_label() { return FC.typing ? "fightchat-typing" : "fightchat"; }
