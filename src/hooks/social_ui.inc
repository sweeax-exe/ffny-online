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

enum KbKind { KB_CHAT, KB_FRIEND };
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
} C;

bool chat_mouse_blocked() { return C.mouse_off; }

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
    if (tab == TAB_GLOBAL) return true;
    if (tab == TAB_CREW) return v.crew != 0;
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

bool tab_shown(int t) { return t != TAB_CREW || C.v.crews || C.v.crew; }

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
        int n = t == TAB_FRIENDS ? C.v.requests : t == TAB_MESSAGES ? C.v.unread : 0;
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
        bool mine = l.from && l.from == C.v.me;
        std::string body = l.hidden ? "message hidden: you muted " + l.name : l.text;
        ImU32 name_col = mine ? GOLD_TEXT : l.from && friend_of(l.from) ? BLUE_NAME : CREAM;
        float h, top;
        if (stacked) {      // CREW: the name and time above the message, as ROOM in the design
            float mh = text_height(f, g_body, 11, body.c_str(), x1 - x0);
            h = 14 + mh + 8;
            top = y - h;
            if (l.system) {
                text(dl, f, g_body, 8.5f, (x0 + x1) / 2, top + 4, DIM, l.text.c_str(), CENTER, 0, x1 - x0);
            } else {
                text(dl, f, g_hand, 14, x0, top - 3, name_col, l.name.c_str(), LEFT, 0.6f);
                text(dl, f, g_body, 8, x0 + text_width(f, g_hand, 14, l.name.c_str()) + 6, top + 1, DIM,
                     clock_text(l.at).c_str(), LEFT, 0);
                text(dl, f, g_body, 11, x0, top + 14, l.hidden ? DIM : CREAM, body.c_str(), LEFT, 0, x1 - x0);
            }
        } else {            // GLOBAL: time, crew tag, name and message in one line
            float x = x0;
            std::string t = clock_text(l.at);
            float tw = text_width(f, g_body, 8, t.c_str()) + 5;
            float lead = tw, tagw = 0, namew = 0;
            if (!l.tag.empty()) tagw = text_width(f, g_body, 7.5f, l.tag.c_str()) + 10;
            if (l.system) namew = text_width(f, g_title, 8.5f, "SERVER") + 5;
            else namew = text_width(f, g_hand, 13, l.name.c_str()) + 6;
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
            else text(dl, f, g_hand, 13, x, top - 1.5f, name_col, l.name.c_str(), LEFT, 0.6f);
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
                     C.tab == TAB_CREW ? "No crew messages yet. Say hello." : "No messages yet. Be the first.", CENTER, 0);
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
        if (!l.system) open_player_menu(l.from, l.name);
    }
    std::string where = C.region ? std::string(k_region_names[C.region]) : "";
    for (auto &c : where) c = (char)tolower((unsigned char)c);
    std::string ph = C.region == 0 ? "Message everyone on your server" : "Message everyone on " +
                     std::string(C.region == 1 ? "Europe" : C.region == 2 ? "US East" : "US West");
    input_bar(dl, f, PX, ph, "SEND");
}

void draw_crew(ImDrawList *dl, const Frame &f, const ChatKeys &k)
{
    const social::View &v = C.v;
    if (!v.crew) {
        float y = TABS_B + 14;
        text(dl, f, g_title, 9, PX + 18, y, DIM, "CREW INVITES", LEFT, 0);
        y += 16;
        int n = (int)v.crew_invites.size();
        if (k.up) C.sel[TAB_CREW] = std::max(0, C.sel[TAB_CREW] - 1);
        if (k.down) C.sel[TAB_CREW] = std::min(n - 1, C.sel[TAB_CREW] + 1);
        if (C.sel[TAB_CREW] < 0 && n) C.sel[TAB_CREW] = 0;
        if (k.left || k.right) C.btn = C.btn ? 0 : 1;
        for (int i = 0; i < n; i++) {
            const social::CrewInvite &ci = v.crew_invites[(size_t)i];
            bool on = i == C.sel[TAB_CREW];
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
            text(dl, f, g_body, 11, PX + PW / 2, TABS_B + 120, DIM,
                 "You are not in a crew yet. When a crew invites you, the invite shows here.", CENTER, 0, 380);
        return;
    }
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
        if (!l.system) open_player_menu(l.from, l.name);
    }
    input_bar(dl, f, PX, "Message your crew", "SEND");
}

// ---- ROOM: who is in it; room chat comes with the relay's part (stage 2b) ----

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
        dl->AddCircleFilled(f.p(PX + 15, y + 5.5f), 3 * f.s(), watcher ? IM_COL32(242, 230, 200, 90) : LIVE);
        text(dl, f, g_hand, 13, PX + 24, y - 2, host ? GOLD_TEXT : watcher ? IM_COL32(242, 230, 200, 205) : BLUE_NAME, name,
             LEFT, 0.6f);
        const char *what = watcher ? "WATCHING" : host ? "HOST" : you ? "YOU" : "";
        text(dl, f, g_body, 7.5f, PX + lw - 12, y + 2, host ? GOLD_TEXT : DIM, what, RIGHT, 0);
        if (mouse_click(f, PX + 7, y - 4, lw - 14, 19)) C.sel[TAB_ROOM] = i;
        if (on && (k.a || mouse_click(f, PX + 7, y - 4, lw - 14, 19)) && !you) open_player_menu(0, name);
        y += 17;
    }
    text(dl, f, g_body, 8.5f, PX + 12, PY + PH - 34, DIM, "Pick a name: add friend.", LEFT, 0, lw - 24);
    float cx = PX + lw + (PW - lw) / 2;
    text(dl, f, g_title, 13, cx, PY + 140, CREAM, "ROOM CHAT IS COMING", CENTER, 0);
    text(dl, f, g_body, 10, cx, PY + 162, DIM, "Writing to the room arrives with the next update.", CENTER, 0);
    text(dl, f, g_body, 10, cx, PY + 176, DIM, "Voice chat works in rooms today.", CENTER, 0);
    text(dl, f, g_body, 10, cx, PY + 200, DIM, "Meanwhile: GLOBAL, FRIENDS and MESSAGES.", CENTER, 0);
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
        if (w.kind == 3) {
            ImU32 dot = offline ? IM_COL32(242, 230, 200, 90) : w.fr->state == "fighting" || w.fr->state == "watching" ? ORANGE : LIVE;
            dl->AddCircleFilled(f.p(x + 3, y + h / 2), 3 * f.s(), with_alpha(dot, a));
            x += 13;
        }
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
        text(dl, f, g_hand, 13, PX + 15, y + 1, CREAM, c.with.name.c_str(), LEFT, 0.6f);
        if (c.unread) badge(dl, f, PX + lw - 26, y + 4, c.unread);
        std::string snip = (c.last_from == v.me ? "You: " : "") + c.last;
        dl->PushClipRect(f.p(PX + 14, y), f.p(PX + lw - 12, y + 32), true);
        text(dl, f, g_body, 9, PX + 15, y + 18, IM_COL32(242, 230, 200, 140), snip.c_str(), LEFT, 0);
        dl->PopClipRect();
        if (mouse_click(f, PX + 7, y, lw - 14, 32)) { sel = i; C.dm_with = c.with.id; social::open_thread(c.with.id); }
        y += 35;
    }
    const char *policy = v.dms == "everyone" ? "Anyone can message you." : v.dms == "off" ? "Your messages are off." :
                         "Only friends can message you.";
    text(dl, f, g_body, 8, PX + 12, PY + PH - 34, IM_COL32(242, 230, 200, 128), policy, LEFT, 0, lw - 24);
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
    dl->AddCircleFilled(f.p(rx + 16, hy + 7), 3 * f.s(), fr && fr->state != "offline" ? LIVE : IM_COL32(242, 230, 200, 90));
    text(dl, f, g_hand, 16, rx + 25, hy - 2, CREAM, name.c_str(), LEFT, 0.6f);
    float nx = rx + 31 + text_width(f, g_hand, 16, name.c_str());
    if (fr && !fr->tag.empty()) {
        float tw = text_width(f, g_body, 7.5f, fr->tag.c_str()) + 6;
        dl->AddRect(f.p(nx, hy + 2), f.p(nx + tw, hy + 13), IM_COL32(255, 205, 60, 150), 3 * f.s(), 0, 0.8f * f.s());
        text(dl, f, g_body, 7.5f, nx + tw / 2, hy + 2.5f, GOLD_TEXT, fr->tag.c_str(), CENTER, 0);
        nx += tw + 6;
    }
    text(dl, f, g_body, 9, nx, hy + 2, scol, state.c_str(), LEFT, 0);
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
    (void)k;
}

// ---- the player menu: A on a name ----

void draw_player_menu(ImDrawList *dl, const Frame &f, const ChatKeys &k)
{
    std::vector<std::pair<std::string, int>> items;     // 0 add friend, 1 message, 2 mute, 3 unmute
    bool is_friend = C.menu_id && friend_of(C.menu_id), muted = false, asked = false;
    for (auto &m : C.v.muted) muted |= m.id == C.menu_id;
    for (auto &o : C.v.out) asked |= C.menu_id ? o.id == C.menu_id : o.name == C.menu_name;
    if (!is_friend && !asked) items.push_back({ "ADD FRIEND", 0 });
    if (C.menu_id) items.push_back({ "MESSAGE", 1 });
    if (C.menu_id) items.push_back({ muted ? "UNMUTE" : "MUTE", muted ? 3 : 2 });
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

std::string &kb_text() { return C.kb_kind == KB_FRIEND ? C.friend_name : C.draft[C.tab]; }
int &kb_cursor() { return C.kb_kind == KB_FRIEND ? C.friend_cursor : C.cursor[C.tab]; }
int kb_max() { return C.kb_kind == KB_FRIEND ? 32 : std::max(1, C.v.max_chars); }

void open_keyboard(int kind)
{
    C.kb = true;
    C.popup_frame = ImGui::GetFrameCount();
    C.kb_kind = kind;
    C.kr = 2;
    C.kc = 7;
    C.symbols = false;
    if (kind == KB_FRIEND) { C.friend_name.clear(); C.friend_cursor = 0; }
}

void kb_submit()
{
    if (C.kb_kind == KB_FRIEND) {
        std::string n = C.friend_name;
        while (!n.empty() && n.back() == ' ') n.pop_back();
        if (!n.empty()) social::friend_request(0, n);
        C.kb = false;
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
    if (s.empty() && C.kb_kind == KB_FRIEND) text(dl, f, g_body, 14, x + in + 9, fy + 7, DIM, "Their name, exactly", LEFT, 0);
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
    hx += plate_hint(dl, f, hx, hy, start_button_name(), C.kb_kind == KB_FRIEND ? "ADD" : "SEND");
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
// friend), type=TEXT (the tab's text box), send, close; pad=BUTTON (a, b, x, y, lb, rb, start, back,
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
        C.open = C.kb = C.menu = false;
        C.have = false;
        return false;
    }
    if (social::snapshot(C.v, C.have ? C.v.version : 0xFFFFFFFFu)) C.have = true;
    if (C.v.ready && !C.ready_at) C.ready_at = GetTickCount64();
    if (!C.v.ready) C.ready_at = 0;
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
    int n = C.v.unread + C.v.requests;
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
    if (!social_enabled()) return;
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
    if (!C.v.ready) {
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
    if (!C.kb && !C.menu && (k.b || k.esc)) C.open = false;
    if (!C.open) social_set_typing(0);
}

const char *chat_shot_label()
{
    if (!C.open || !social_enabled()) return nullptr;
    return C.kb ? "social-keyboard" : k_tab_shots[C.tab];
}
