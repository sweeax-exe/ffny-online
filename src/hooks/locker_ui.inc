// Ranked rewards and the Locker (build 52; the owner's approved designs "Season rewards" and "Locker",
// 2026-11-03). Included by online_screen.cpp inside its anonymous namespace after social_ui.inc and crew_ui.inc.
//
//   looks           how others see a player: the emblem beside the name (24 px), the name's colour, the avatar's
//                   frame and the title, from the account service (/api/social/looks, social.cpp), drawn in chat
//                   lines, the friends list and the room's cards
//   SEASON REWARDS  the last tab of MY STATS: the season's finish per mode and each tier's rewards (name colour,
//                   frame, title, Discord role); A wears a reached tier's colour, frame and title
//   LOCKER          every unlocked reward (season rewards and the crew's), one name colour, frame, title and
//                   emblem worn; locked ones greyed with how to get them; a live preview (chat line, friends
//                   list, versus card). From MY STATS (RB, or X on SEASON REWARDS) and the chat's own name.
//
// Shown only while the social features are on (social_enabled); the ranked parts only while ranked is
// offered too (the service leaves them out of its answers while its ranked switch is off).

struct LockerUi {
    int slot = 0;                   // 0 name colour, 1 frame, 2 title, 3 emblem
    int sel = 0, top = 0;
    int rewards_mode = NETRANK_CLASSIC, rewards_sel = 0;
    Page back = STATS;
    unsigned long long asked = 0;
} LK;
const char *const k_slot_ids[4] = { "color", "frame", "title", "emblem" };
const char *const k_slot_names[4] = { "NAME COLOUR", "FRAME", "TITLE", "EMBLEM" };

// The gallery's sample (the social features are off in the gallery): Rico, Season 1 Classic at Kingpin II.
social::View LKG;
bool g_rewards_gallery = false;

const social::View &look_view() { return gallery_on() ? LKG : C.v; }

bool rewards_shown() { return gallery_on() ? g_rewards_gallery : social_enabled() != 0; }

void locker_refresh(bool force)
{
    if (gallery_on()) return;
    unsigned long long now = GetTickCount64();
    if (force || !LK.asked || now - LK.asked > 30000) {
        LK.asked = now;
        social::locker_load();
    }
}

void open_locker()
{
    if (!rewards_shown()) return;
    LK.back = S.page == LOCKER ? LK.back : S.page;
    if (LK.back == LOCKER) LK.back = STATS;
    S.page = LOCKER;
    LK.sel = LK.top = 0;
    locker_refresh(true);
    fprintf(stderr, "[LOCKER] opened\n");
}

void open_season_rewards(int mode)
{
    S.page = STATS;
    S.stats_page = 0;
    ranked_stats_tab() = 1 + NETRANK_MODES;
    LK.rewards_mode = mode < 0 || mode >= NETRANK_MODES ? NETRANK_CLASSIC : mode;
    LK.rewards_sel = 0;
    locker_refresh(true);
}

// ---- looks --------------------------------------------------------------------------------------------

const social::Look *look_of(long long id)
{
    if (id <= 0) return nullptr;
    const social::View &v = look_view();
    if (!gallery_on() && social_enabled()) social::look_want(id);
    auto it = v.looks.find(id);
    return it == v.looks.end() ? nullptr : &it->second;
}

// The emblem a look shows (-1 none): ranks only while ranked is offered.
int look_tier(const social::Look *l)
{
    if (!l || l->emblem_mode == -1 || l->emblem_tier < 0) return -1;
    if (l->emblem_mode == -2) return 99;       // the crew's crown
    return ranked_shown() ? l->emblem_tier : -1;
}

// The 24 px emblem in front of a name; returns the width it took (0 none).
float look_icon(ImDrawList *dl, const Frame &f, float x, float cy, float size, const social::Look *l, float alpha)
{
    int t = look_tier(l);
    if (t < 0) return 0;
    if (t == 99) crown_small(dl, f, x + size / 2, cy, size, alpha);
    else emblem_small(dl, f, x + size / 2, cy, size, t, alpha);
    return size + 3;
}

ImU32 look_col(const social::Look *l, ImU32 fallback)
{
    if (!l || !ranked_shown()) return fallback;
    return look_name_col(l->color, fallback);
}

void look_frame(ImDrawList *dl, const Frame &f, float cx, float cy, float r, const social::Look *l, float alpha)
{
    if (l && ranked_shown()) avatar_frame(dl, f, cx, cy, r, l->frame, l->pulse, alpha);
}

// ---- the locker's items ---------------------------------------------------------------------------------

const social::LockerItem *locker_item(const social::Locker &L, const std::string &id)
{
    for (auto &it : L.items) if (it.id == id) return &it;
    return nullptr;
}

bool item_fits(const social::LockerItem &it, int slot)
{
    return slot == 0 ? it.k_color : slot == 1 ? it.k_frame : slot == 2 ? it.k_title : it.k_emblem;
}

const std::string &equipped_id(const social::Locker &L, int slot)
{
    return slot == 0 ? L.color : slot == 1 ? L.frame : slot == 2 ? L.title : L.emblem;
}

// This account's look from what it wears (or would wear with `try_item` in `try_slot`): the preview.
social::Look look_from_locker(const social::Locker &L, int try_slot = -1, const social::LockerItem *try_item = nullptr)
{
    social::Look l;
    for (int s = 0; s < 4; s++) {
        const social::LockerItem *it = s == try_slot && try_item ? try_item : locker_item(L, equipped_id(L, s));
        if (!it) continue;
        if (s == 0) l.color = it->tier;
        else if (s == 1) { l.frame = it->tier; l.pulse = it->tier == 7; }
        else if (s == 2) l.title = it->title;
        else if (it->id == "crew:crown_emblem") { l.emblem_mode = -2; l.emblem_tier = 0; }
        else if (it->tier >= 0) { l.emblem_mode = 0; l.emblem_tier = it->tier; l.emblem_division = it->division; }
    }
    return l;
}

std::string mode_title_of(const std::string &mode)
{
    for (int m = 0; m < NETRANK_MODES; m++) if (mode == netranked_mode_id(m)) return netranked_mode_title(m);
    return upper(mode.c_str());
}

// The rows of a slot: headers ("" id) and items, in the service's order (current ranks, past seasons newest first,
// this season's next goal per mode, the crew's).
struct LockerRow { std::string header, right; const social::LockerItem *item = nullptr; };
std::vector<LockerRow> locker_rows(const social::Locker &L, int slot)
{
    std::vector<LockerRow> rows;
    std::string group;
    std::map<std::string, int> cur_tier;            // a mode's current tier, from its rank:<mode> item
    for (auto &it : L.items) if (it.current) cur_tier[it.mode] = it.tier;
    for (auto &it : L.items) {
        std::string g;
        bool crew = it.id.compare(0, 5, "crew:") == 0, pre = it.id.compare(0, 4, "pre-") == 0;
        if (pre) {                                  // the preseason's title (build 52)
            if (!item_fits(it, slot)) continue;
            g = "PRESEASON";
        } else if (it.current) {
            if (slot != 3) continue;
            char b[64];
            snprintf(b, sizeof(b), "SEASON %d - YOUR RANK NOW", L.season);
            g = b;
        } else if (crew) {
            g = "CREW REWARDS";
        } else if (it.season >= L.season) {        // this season: only the goal of each mode it is placed in
            auto c = cur_tier.find(it.mode);
            if (c == cur_tier.end() || it.tier != c->second || !item_fits(it, slot)) continue;
            char b[64];
            snprintf(b, sizeof(b), "SEASON %d - NOW", it.season);
            g = b;
        } else {
            if (!item_fits(it, slot)) continue;
            char b[96];
            snprintf(b, sizeof(b), "SEASON %d - %s", it.season, upper(mode_title_of(it.mode).c_str()).c_str());
            g = b;
        }
        if (g != group) {
            LockerRow h;
            h.header = g;
            if (crew) h.right = "FOR THE WHOLE CREW";
            else if (pre) h.right = "ONE RANKED SET IN THE TEST WEEK";
            else if (it.season >= L.season && !it.current) h.right = "FINISH TO UNLOCK";
            rows.push_back(h);
            group = g;
        }
        LockerRow r;
        r.item = &it;
        rows.push_back(r);
    }
    return rows;
}

// The row's icon: the tier's emblem, or a crew reward's mark.
void item_icon(ImDrawList *dl, const Frame &f, float cx, float cy, float size, const social::LockerItem &it, float alpha)
{
    if (it.id == "crew:gold_tag") {
        frame_rect(dl, f, cx - size * 0.45f, cy - size * 0.25f, size * 0.9f, size * 0.5f, with_alpha(GOLD, alpha), 1.2f);
        const std::string &tag = look_view().crew_tag;
        text(dl, f, g_title, size * 0.3f, cx, cy - size * 0.19f, with_alpha(GOLD, alpha), tag.empty() ? "TAG" : tag.c_str(), CENTER, 0);
    } else if (it.id == "crew:crown_emblem") {
        crown_small(dl, f, cx, cy, size * 0.85f, alpha);
    } else if (it.id == "crew:versus_card" || it.id == "crew:room_theme") {
        rect(dl, f, cx - size * 0.28f, cy - size * 0.38f, size * 0.56f, size * 0.76f, with_alpha(IM_COL32(42, 10, 16, 255), alpha), 3);
        frame_rect(dl, f, cx - size * 0.28f, cy - size * 0.38f, size * 0.56f, size * 0.76f, with_alpha(GOLD, alpha), 1.2f);
    } else if (it.tier >= 0) {
        emblem_small(dl, f, cx, cy, size, it.tier, alpha);
    }
}

std::string item_name(const social::LockerItem &it)
{
    if (!it.title.empty()) return upper(it.title.c_str());
    if (it.current && !it.label.empty()) return upper(it.label.c_str());
    return upper((it.name.empty() ? it.id : it.name).c_str());
}

ImU32 item_col(const social::LockerItem &it)
{
    if (it.id.compare(0, 5, "crew:") == 0) return GOLD;
    return it.tier >= 0 ? tier_col(it.tier) : CREAM;
}

// ---- the live preview: a chat line, a friends-list row, a versus card -------------------------------------

void draw_look_preview(ImDrawList *dl, const Frame &f, float x, float y, float w, const social::Look &l)
{
    const social::View &v = look_view();
    std::string name = v.my_name.empty() ? std::string("YOU") : v.my_name;
    long long me = v.me;
    ImU32 col = look_name_col(l.color, CREAM);
    text(dl, f, g_title, 9, x, y, GREY, "LIVE PREVIEW - HOW OTHERS SEE YOU", LEFT, 0.5f);
    // chat
    float cy = y + 14;
    rect(dl, f, x, cy, w, 34, IM_COL32(0, 0, 0, 100), 6);
    text(dl, f, g_title, 7.5f, x + 6, cy + 3, GREY, "CHAT", LEFT, 0);
    float lx = x + 6;
    text(dl, f, g_body, 8, lx, cy + 18, DIM, "21:42", LEFT, 0);
    lx += 22;
    draw_avatar(dl, f, lx + 6, cy + 23, 6, me, name);
    avatar_frame(dl, f, lx + 6, cy + 23, 6, l.frame, l.pulse);
    lx += 16;
    lx += look_icon(dl, f, lx, cy + 23, 12, &l);
    text(dl, f, g_hand, 13, lx, cy + 14, col, name.c_str(), LEFT, 0.6f);
    lx += text_width(f, g_hand, 13, name.c_str()) + 6;
    text(dl, f, g_body, 11, lx, cy + 15, CREAM, "set 2-1, good fight", LEFT, 0);
    // friends list
    float fy = cy + 42;
    rect(dl, f, x, fy, w, 52, IM_COL32(0, 0, 0, 100), 6);
    text(dl, f, g_title, 7.5f, x + 6, fy + 3, GREY, "FRIENDS LIST", LEFT, 0);
    draw_avatar(dl, f, x + 22, fy + 31, 13, me, name);
    avatar_frame(dl, f, x + 22, fy + 31, 13, l.frame, l.pulse);
    std::string up = upper(name.c_str());
    text(dl, f, g_title, 14, x + 42, fy + 14, col, up.c_str(), LEFT, 0.6f);
    look_icon(dl, f, x + 46 + text_width(f, g_title, 14, up.c_str()), fy + 22, 12, &l);
    if (!l.title.empty()) text(dl, f, g_title, 9, x + 42, fy + 30, col, upper(l.title.c_str()).c_str(), LEFT, 0.4f);
    text(dl, f, g_body, 9, x + 42, fy + 40, LIVE, "Online, in the menu", LEFT, 0);
    // versus card
    float vy = fy + 60, vh = 104;
    rect(dl, f, x, vy, w, vh, IM_COL32(40, 8, 14, 230), 8);
    frame_rect(dl, f, x, vy, w, vh, with_alpha(l.frame >= 0 ? tier_col(l.frame) : GOLD, 0.6f), 1.2f);
    text(dl, f, g_title, 7.5f, x + 6, vy + 4, GREY, "VERSUS CARD", LEFT, 0);
    draw_avatar(dl, f, x + 42, vy + 58, 28, me, name);
    avatar_frame(dl, f, x + 42, vy + 58, 28, l.frame, l.pulse);
    float tx = x + 82;
    int t = look_tier(&l);
    if (t >= 0) {
        look_icon(dl, f, tx, vy + 28, 24, &l);
        if (t != 99) {
            std::string lab = division_label(l.emblem_division);
            rank_plate(dl, f, tx + 30 + 44, vy + 20, 88, lab.c_str(), t, false);
        }
    }
    text(dl, f, g_title, fit_size(f, g_title, 20, up.c_str(), w - 90), tx, vy + 44, col, up.c_str(), LEFT, 0.8f);
    if (!l.title.empty()) text(dl, f, g_title, 10, tx, vy + 68, col, upper(l.title.c_str()).c_str(), LEFT, 0.5f);
    text(dl, f, g_body, 8.5f, tx, vy + 84, GREY, "The versus screen, the room's cards, the results.", LEFT, 0, w - 90);
}

// ---- LOCKER -----------------------------------------------------------------------------------------------

void draw_locker(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    locker_refresh(false);
    const social::View &v = look_view();
    const social::Locker &L = v.locker;
    page_title(dl, f, "LOCKER");
    dl->AddLine(f.p(50, 60), f.p(140, 60), RED, 2.5f * f.s());
    if (pad.lb_once) { LK.slot = (LK.slot + 3) % 4; LK.sel = LK.top = 0; }
    if (pad.rb) { LK.slot = (LK.slot + 1) % 4; LK.sel = LK.top = 0; }
    // the slots: what each wears
    float sx = 616;
    for (int s = 3; s >= 0; s--) {
        const social::LockerItem *eq = locker_item(L, equipped_id(L, s));
        std::string val = eq ? (s == 2 ? eq->title : eq->tier >= 0 && s != 3 ? title_case(k_tier_short[eq->tier]) : item_name(*eq)) : "None";
        if (s == 3 && eq && eq->tier >= 0 && eq->id.compare(0, 5, "crew:") != 0)
            val = eq->current ? title_case(eq->label) : title_case(k_tier_short[eq->tier]);
        float nw = text_width(f, g_title, 11, k_slot_names[s]), vw = text_width(f, g_body, 8.5f, val.c_str());
        float w = nw + vw + 18;
        sx -= w;
        bool on = s == LK.slot;
        rect(dl, f, sx, 50, w, 20, on ? IM_COL32(163, 16, 31, 240) : IM_COL32(20, 16, 18, 220), 5);
        text(dl, f, g_title, 11, sx + 6, 53, on ? WHITE : with_alpha(CREAM, 0.6f), k_slot_names[s], LEFT, 0.6f);
        text(dl, f, g_body, 8.5f, sx + 10 + nw, 55, on ? with_alpha(WHITE, 0.8f) : GREY, val.c_str(), LEFT, 0);
        if (mouse_click(f, sx, 50, w, 20)) { LK.slot = s; LK.sel = LK.top = 0; }
        sx -= 4;
    }
    text(dl, f, g_body, 8.5f, sx - 2, 55, GREY, "LB / RB", RIGHT, 0);

    glass(dl, f, 20, 80, 360, 322, 10, IM_COL32(10, 9, 11, 255), 0.72f);
    std::vector<LockerRow> rows = locker_rows(L, LK.slot);
    std::vector<int> items;
    for (int i = 0; i < (int)rows.size(); i++) if (rows[(size_t)i].item) items.push_back(i);
    if (!L.loaded) {
        text(dl, f, g_title, 14, 200, 220, GREY, "LOADING YOUR REWARDS...", CENTER, 0.8f);
    } else if (items.empty()) {
        text(dl, f, g_title, 14, 200, 210, CREAM, "NOTHING HERE YET", CENTER, 0.8f);
        text(dl, f, g_body, 10, 200, 232, GREY,
             L.ranked ? "Finish a ranked season at a tier to unlock its rewards and every tier below it; level your crew up for its rewards."
                      : "Level your crew up for its rewards.", CENTER, 0, 300);
    }
    if (!items.empty()) {
        if (pad.up) LK.sel = std::max(0, LK.sel - 1);
        if (pad.down) LK.sel = std::min((int)items.size() - 1, LK.sel + 1);
        LK.sel = std::min(LK.sel, (int)items.size() - 1);
    }
    // the list scrolls to keep the selected item in view: rows are 31 high, headers 16
    const float top = 86, bottom = 398;
    int sel_row = items.empty() ? -1 : items[(size_t)LK.sel];
    if (sel_row >= 0 && sel_row - 1 < LK.top) LK.top = std::max(0, sel_row - 1);
    for (;;) {
        float y = top;
        for (int i = LK.top; i <= sel_row && i < (int)rows.size(); i++) y += rows[(size_t)i].item ? 31 : 16;
        if (y <= bottom || LK.top >= sel_row) break;
        LK.top++;
    }
    dl->PushClipRect(f.p(20, top - 2), f.p(380, bottom + 2), true);
    float y = top;
    const social::LockerItem *selected = nullptr;
    for (int i = LK.top; i < (int)rows.size() && y < bottom; i++) {
        const LockerRow &r = rows[(size_t)i];
        if (!r.item) {
            text(dl, f, g_title, 8.5f, 32, y + 3, with_alpha(CREAM, 0.5f), r.header.c_str(), LEFT, 0);
            if (!r.right.empty()) text(dl, f, g_title, 8.5f, 368, y + 3, with_alpha(CREAM, 0.5f), r.right.c_str(), RIGHT, 0);
            y += 16;
            continue;
        }
        const social::LockerItem &it = *r.item;
        bool on = i == sel_row, eq = equipped_id(L, LK.slot) == it.id, fits = item_fits(it, LK.slot);
        float a = it.unlocked ? 1.0f : 0.42f;
        if (on) {
            selected = &it;
            rect(dl, f, 26, y, 348, 29, IM_COL32(200, 16, 46, 60), 6);
            frame_rect(dl, f, 26, y, 348, 29, IM_COL32(224, 48, 63, 255), 1.4f);
        }
        if (mouse_click(f, 26, y, 348, 29)) {
            for (int k = 0; k < (int)items.size(); k++) if (items[(size_t)k] == i) LK.sel = k;
        }
        item_icon(dl, f, 44, y + 14.5f, 24, it, a);
        std::string nm = item_name(it);
        text(dl, f, g_title, fit_size(f, g_title, 13, nm.c_str(), 220), 62, y + 1, with_alpha(item_col(it), a), nm.c_str(), LEFT, 0.6f);
        std::string sub = it.unlocked ? (it.finish ? "Your finish: colour, frame, title, Discord role"
                                         : it.current ? "Your rank now: shows until the season ends"
                                         : it.id.compare(0, 5, "crew:") == 0 ? it.how
                                         : "Unlocked with your finish")
                                      : (std::string("Locked: ") + it.how);
        text(dl, f, g_body, 8.5f, 62, y + 17, with_alpha(GREY, std::max(a, 0.8f)), sub.c_str(), LEFT, 0, 250);
        const char *st = eq ? nullptr : !it.unlocked ? "LOCKED" : it.k_crew ? "ALWAYS ON" : fits ? "UNLOCKED" : "";
        if (eq) {
            rect(dl, f, 316, y + 8, 54, 13, GOLD, 3);
            text(dl, f, g_title, 8.5f, 343, y + 9, BLACK, "EQUIPPED", CENTER, 0);
        } else if (st) {
            text(dl, f, g_title, 9, 368, y + 9, with_alpha(CREAM, 0.55f), st, RIGHT, 0);
        }
        y += 31;
    }
    dl->PopClipRect();

    // the preview: what is worn, with the selected item tried on
    glass(dl, f, 390, 80, 230, 322, 10, IM_COL32(10, 9, 11, 255), 0.72f);
    social::Look preview = look_from_locker(L, selected && item_fits(*selected, LK.slot) ? LK.slot : -1, selected);
    draw_look_preview(dl, f, 400, 90, 210, preview);
    text(dl, f, g_body, 8.5f, 400, 372, GREY, "Saved on the server: every player sees what you wear.", LEFT, 0, 210);
    if (!v.note.empty() && GetTickCount64() - v.note_at < 5000) text(dl, f, g_body, 9, 400, 356, RED, v.note.c_str(), LEFT, 0, 210);

    bool can = selected && selected->unlocked && item_fits(*selected, LK.slot) && !selected->k_crew;
    float hx = 54;
    if (can) hx += hint(dl, f, hx, 420, "a", "EQUIP");
    if (!equipped_id(L, LK.slot).empty()) hx += hint(dl, f, hx, 420, "x", "TAKE OFF");
    hx += hint(dl, f, hx, 420, "rb", "SLOT");
    hint(dl, f, hx, 420, "b", "BACK");
    if (pad.a && can && !gallery_on()) {
        social::equip(k_slot_ids[LK.slot], selected->id);
        fprintf(stderr, "[LOCKER] equip %s=%s\n", k_slot_ids[LK.slot], selected->id.c_str());
        say("Equipped: " + item_name(*selected));
    } else if (pad.x && !equipped_id(L, LK.slot).empty() && !gallery_on()) {
        social::equip(k_slot_ids[LK.slot], "");
        say(std::string("Taken off: ") + k_slot_names[LK.slot]);
    } else if (pad.b) {
        S.page = LK.back == LOCKER ? STATS : LK.back;
    }
}

// ---- MY STATS: SEASON REWARDS -------------------------------------------------------------------------------

void draw_season_rewards(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    locker_refresh(false);
    const social::View &v = look_view();
    const social::Locker &L = v.locker;
    if (pad.lb_once) LK.rewards_mode = (LK.rewards_mode + NETRANK_MODES - 1) % NETRANK_MODES;
    if (pad.rb) LK.rewards_mode = (LK.rewards_mode + 1) % NETRANK_MODES;
    std::string mode = netranked_mode_id(LK.rewards_mode);
    const social::SeasonFinish *fin = nullptr;
    for (auto &fs : L.finishes) if (fs.mode == mode) { fin = &fs; break; }
    char line[160];

    // the banner
    float bx = 206, bw = 410;
    rect(dl, f, bx, 80, bw, 26, IM_COL32(120, 40, 20, 170), 7);
    rect(dl, f, bx + bw / 2, 80, bw / 2, 26, IM_COL32(150, 14, 40, 90), 7);
    if (fin) {
        snprintf(line, sizeof(line), "SEASON %d IS OVER", fin->season);
        text(dl, f, g_hand, 15, bx + 10, 83, IM_COL32(255, 245, 230, 255), line, LEFT, 0.8f);
        snprintf(line, sizeof(line), "Your rewards are unlocked for good. Season %d opens with 3 placement sets.", fin->season + 1);
        text(dl, f, g_body, 8.5f, bx + 14 + text_width(f, g_hand, 15, "SEASON 1 IS OVER"), 89, CREAM, line, LEFT, 0, 250);
    } else if (read_season().pre) {
        SeasonInfo si = read_season();
        text(dl, f, g_hand, 15, bx + 10, 83, IM_COL32(255, 245, 230, 255), "PRESEASON", LEFT, 0.8f);
        std::string l = "Test week, " + upper(season_ends_in(si).c_str()) + ": no tier rewards, ranks reset; one set earns the Tester title.";
        text(dl, f, g_body, 8.5f, bx + 92, 89, CREAM, l.c_str(), LEFT, 0, 310);
    } else {
        snprintf(line, sizeof(line), "SEASON %d", L.season > 0 ? L.season : 1);
        text(dl, f, g_hand, 15, bx + 10, 83, IM_COL32(255, 245, 230, 255), line, LEFT, 0.8f);
        text(dl, f, g_body, 8.5f, bx + 90, 89, CREAM, "Finish the season at a tier: its rewards and every lower tier's unlock.",
             LEFT, 0, 310);
    }

    // the left panel: the finish
    glass(dl, f, 24, 80, 172, 326, 10, IM_COL32(10, 9, 11, 255), 0.72f);
    text(dl, f, g_title, 8, 34, 88, GREY, "LB", LEFT, 0);
    text(dl, f, g_title, 8, 186, 88, GREY, "RB", RIGHT, 0);
    snprintf(line, sizeof(line), "RANKED %s", netranked_mode_title(LK.rewards_mode));
    text(dl, f, g_title, 11, 110, 87, ORANGE, line, CENTER, 0.6f);
    if (fin) {
        EmblemOpts eo;
        eo.rating = fin->rating;
        eo.top = fin->top;
        emblem_big(dl, f, 110 - 52, 102, 104, fin->tier, fin->division, eo);
        std::string lab = division_label(fin->division);
        text(dl, f, g_stencil, fit_size(f, g_stencil, 20, lab.c_str(), 160), 110, 206, CREAM, lab.c_str(), CENTER, 1.2f);
        snprintf(line, sizeof(line), "Your finish in Season %d", fin->season);
        text(dl, f, g_body, 9, 110, 230, GREY, line, CENTER, 0);
        float ry = 250;
        auto row = [&](const char *k, const char *val, ImU32 col) {
            dl->AddLine(f.p(32, ry - 3), f.p(188, ry - 3), IM_COL32(255, 255, 255, 22), 1.0f);
            text(dl, f, g_body, 9, 32, ry, CREAM, k, LEFT, 0);
            text(dl, f, g_title, 11, 188, ry - 1, col, val, RIGHT, 0.5f);
            ry += 20;
        };
        snprintf(line, sizeof(line), "%d", fin->rating);
        row("Final rating", line, CREAM);
        snprintf(line, sizeof(line), "%d, %s", fin->peak, title_case(fin->peak_rank).c_str());
        row("Peak", line, CREAM);
        if (fin->top > 0) snprintf(line, sizeof(line), "#%d of NY", fin->top);
        else if (fin->place > 0) snprintf(line, sizeof(line), "#%d of %d", fin->place, fin->of);
        else snprintf(line, sizeof(line), "-");
        std::string ladder = title_case(netranked_mode_title(LK.rewards_mode)) + " ladder";
        row(ladder.c_str(), line, CREAM);
        snprintf(line, sizeof(line), "%d W  %d L", fin->set_wins, fin->set_losses);
        row("Sets", line, GREEN);
        snprintf(line, sizeof(line), "%d, %s", fin->next_rating, title_case(fin->next_rank).c_str());
        char k2[40];
        snprintf(k2, sizeof(k2), "Season %d start", fin->season + 1);
        row(k2, line, CREAM);
        text(dl, f, g_body, 8, 32, ry + 2, GREY, "Ratings move 40% back toward 1500 at the reset; 3 placement sets then set your emblem.",
             LEFT, 0, 156);
    } else {
        emblem_fallback(dl, f, 110, 150, 40, 0, 0.25f);
        text(dl, f, g_stencil, 34, 110, 132, with_alpha(CREAM, 0.9f), "?", CENTER, 1.0f);
        text(dl, f, g_title, 12, 110, 206, CREAM, "NO FINISH YET", CENTER, 0.6f);
        text(dl, f, g_body, 9, 110, 226, GREY, "Play the mode this season: where you stand at its end is your finish.", CENTER, 0, 150);
    }

    // the table: a row per tier, Legend first
    glass(dl, f, 206, 112, 410, 294, 10);
    static const float cx_tier = 216, cx_name = 314, cx_frame = 392, cx_title = 410, cx_role = 538;
    text(dl, f, g_title, 7.5f, cx_tier + 22, 117, GREY, "FINISH IN", LEFT, 0);
    text(dl, f, g_title, 7.5f, cx_name, 117, GREY, "NAME IN CHAT", LEFT, 0);
    text(dl, f, g_title, 7.5f, cx_frame, 117, GREY, "FRAME", CENTER, 0);
    text(dl, f, g_title, 7.5f, cx_title, 117, GREY, "TITLE", LEFT, 0);
    text(dl, f, g_title, 7.5f, cx_role, 117, GREY, "ROLE", LEFT, 0);
    if (pad.up) LK.rewards_sel = std::max(0, LK.rewards_sel - 1);
    if (pad.down) LK.rewards_sel = std::min(7, LK.rewards_sel + 1);
    std::string name = upper((v.my_name.empty() ? std::string("You") : v.my_name).c_str());
    int season = fin ? fin->season : (L.season > 0 ? L.season : 1);
    const social::LockerItem *sel_item = nullptr;
    int sel_tier = -1;
    for (int r = 0; r < 8; r++) {
        int t = 7 - r;
        float y = 128 + r * 34.5f;
        char id[48];
        snprintf(id, sizeof(id), "s%d-%s-%d", season, mode.c_str(), t);
        const social::LockerItem *it = locker_item(L, id);
        bool unlocked = it && it->unlocked, mine = fin && fin->tier == t, on = r == LK.rewards_sel;
        if (on) { sel_item = it; sel_tier = t; }
        float a = unlocked ? 1.0f : 0.42f;
        ImU32 col = tier_col(t);
        if (mine) {
            rect(dl, f, 210, y, 402, 32, IM_COL32(255, 197, 61, 40), 6);
            frame_rect(dl, f, 210, y, 402, 32, on ? GOLD : with_alpha(GOLD, 0.7f), on ? 1.6f : 1.0f);
        } else if (on) {
            frame_rect(dl, f, 210, y, 402, 32, IM_COL32(224, 48, 63, 255), 1.4f);
        }
        if (mouse_click(f, 210, y, 402, 32)) LK.rewards_sel = r;
        emblem_small(dl, f, cx_tier + 10, y + 16, 22, t, a);
        std::string tn = netranked_tier_name(t) ? upper(netranked_tier_name(t)) : std::string(k_tier_short[t]);
        text(dl, f, g_title, fit_size(f, g_title, 11.5f, tn.c_str(), 70), cx_tier + 24, y + 3, with_alpha(col, a), tn.c_str(), LEFT, 0.5f);
        std::string sub;
        if (mine) sub = equipped_id(L, 2) == id ? "YOUR FINISH - Equipped" : "YOUR FINISH";
        else if (unlocked) sub = "Reached";
        else if (t == 7) sub = "Locked: 2100 and up";
        else {
            snprintf(line, sizeof(line), "Locked: %d and up", netranked_division_min(t * 3) < 0 ? 0 : netranked_division_min(t * 3));
            sub = line;
        }
        text(dl, f, g_body, fit_size(f, g_body, 7.5f, sub.c_str(), 70), cx_tier + 24, y + 18, mine ? GOLD : with_alpha(GREY, std::max(a, 0.8f)), sub.c_str(), LEFT, 0);
        text(dl, f, g_title, fit_size(f, g_title, 11, name.c_str(), 64), cx_name, y + 9, with_alpha(look_name_col(t, CREAM), a),
             name.c_str(), LEFT, 0.5f);
        draw_avatar(dl, f, cx_frame, y + 16, 10, v.me, v.my_name, a);
        avatar_frame(dl, f, cx_frame, y + 16, 10, t, t == 7, a);
        std::string title = it && !it->title.empty() ? upper(it->title.c_str()) : std::string();
        if (title.empty()) {
            snprintf(line, sizeof(line), "S%d %s %s", season, upper(netranked_mode_title(LK.rewards_mode)).c_str(), k_tier_short[t]);
            title = line;
        }
        rect(dl, f, cx_title, y + 8, 2, 16, with_alpha(col, a), 0);
        rect(dl, f, cx_title + 2, y + 8, std::min(126.0f, text_width(f, g_title, 9.5f, title.c_str()) + 10), 16, IM_COL32(0, 0, 0, 130), 0);
        text(dl, f, g_title, fit_size(f, g_title, 9.5f, title.c_str(), 118), cx_title + 7, y + 10, with_alpha(col, a), title.c_str(), LEFT, 0);
        std::string role = it && !it->role.empty() ? it->role : "S" + std::to_string(season) + " " + title_case(k_tier_short[t]);
        float rw = text_width(f, g_body, 9, role.c_str()) + 22;
        rect(dl, f, cx_role, y + 8, rw, 16, IM_COL32(255, 255, 255, 16), 8);
        dl->AddCircleFilled(f.p(cx_role + 9, y + 16), 3 * f.s(), with_alpha(col, a), 10);
        text(dl, f, g_body, 9, cx_role + 16, y + 10, with_alpha(CREAM, a), role.c_str(), LEFT, 0);
    }
    bool can = sel_item && sel_item->unlocked;
    float hx = 54;
    if (can) hx += hint(dl, f, hx, 420, "a", "EQUIP");
    hx += hint(dl, f, hx, 420, "x", "LOCKER");
    hx += hint(dl, f, hx, 420, "rb", "MODE");
    hx += hint(dl, f, hx, 420, "y", "CASUAL");
    hint(dl, f, hx, 420, "b", "BACK");
    if (pad.a && can && !gallery_on()) {      // the tier's colour, frame and title together
        for (int s = 0; s < 3; s++) social::equip(k_slot_ids[s], sel_item->id);
        fprintf(stderr, "[LOCKER] season rewards: equip %s (tier %d)\n", sel_item->id.c_str(), sel_tier);
        say("Equipped: " + item_name(*sel_item));
    }
    if (pad.x) open_locker();
}

// ---- the gallery: locker, locker-emblem, stats-rewards ----------------------------------------------------

void locker_sample()
{
    LKG = social::View();
    LKG.enabled = LKG.ready = true;
    LKG.me = 7;
    LKG.my_name = "Rico Vance";
    LKG.crew_tag = "BX";
    social::Locker &L = LKG.locker;
    L.loaded = L.ranked = true;
    L.season = 2;
    L.color = L.frame = L.title = "s1-classic-5";
    L.emblem = "rank:classic";
    auto add = [&](const std::string &id, int season, const char *mode, int tier, bool unlocked, bool finish) {
        social::LockerItem it;
        it.id = id; it.season = season; it.mode = mode; it.tier = tier; it.division = tier * 3;
        it.unlocked = unlocked; it.finish = finish;
        char b[64];
        snprintf(b, sizeof(b), "S%d %s %s", season, mode[0] == 'c' ? "Classic" : mode[0] == 'o' ? "Open" : "Draft",
                 title_case(k_tier_short[tier]).c_str());
        it.title = b;
        snprintf(b, sizeof(b), "S%d %s", season, title_case(k_tier_short[tier]).c_str());
        it.role = b;
        snprintf(b, sizeof(b), "Finish Season %d %s at %s or higher", season, mode[0] == 'c' ? "Classic" : "Open",
                 title_case(k_tier_short[tier]).c_str());
        it.how = b;
        it.k_color = it.k_frame = it.k_title = it.k_emblem = true;
        L.items.push_back(it);
    };
    social::LockerItem cur;
    cur.id = "rank:classic"; cur.mode = "classic"; cur.tier = 4; cur.division = 13; cur.label = "Enforcer II";
    cur.unlocked = cur.current = cur.k_emblem = true;
    L.items.push_back(cur);
    for (int t = 7; t >= 0; t--) add("s1-classic-" + std::to_string(t), 1, "classic", t, t <= 5, t == 5);
    L.items.back().division = 0;
    for (auto &it : L.items) if (it.id == "s1-classic-5") it.division = 16;
    for (int t = 7; t >= 0; t--) add("s2-classic-" + std::to_string(t), 2, "classic", t, false, false);
    auto crew = [&](const char *id, const char *name, const char *how, bool unlocked, bool emblem) {
        social::LockerItem it;
        it.id = id; it.name = name; it.how = how; it.unlocked = unlocked;
        it.k_emblem = emblem; it.k_crew = !emblem;
        L.items.push_back(it);
    };
    {
        social::LockerItem it;
        it.id = "pre-1"; it.name = "Preseason Tester"; it.title = "Preseason Tester"; it.how = "Play one ranked set in the Preseason";
        it.unlocked = it.k_title = true;
        L.items.push_back(it);
    }
    crew("crew:gold_tag", "Gold tag", "Crew level 5", true, false);
    crew("crew:crown_emblem", "Crown emblem", "Crew level 6", true, true);
    crew("crew:versus_card", "Versus card", "Crew level 8", false, false);
    crew("crew:room_theme", "Room theme", "Crew level 10", false, false);
    social::SeasonFinish fs;
    fs.mode = "classic"; fs.season = 1; fs.tier = 5; fs.division = 16; fs.rating = 1872; fs.peak = 1903; fs.peak_rank = "Kingpin I";
    fs.place = 148; fs.of = 2310; fs.set_wins = 41; fs.set_losses = 23; fs.next_rating = 1723; fs.next_rank = "Enforcer II";
    L.finishes.push_back(fs);
    // the looks of the sample's players: Rico (Kingpin, his finish worn), Tash (Hustler), Vin (Legend #3)
    social::Look l;
    l.id = 7; l.emblem_mode = 1; l.emblem_tier = 4; l.emblem_division = 13; l.color = l.frame = 5; l.title = "S1 Classic Kingpin";
    LKG.looks[7] = l;
    l = social::Look(); l.id = 8; l.emblem_mode = 1; l.emblem_tier = 1; l.emblem_division = 4; l.color = 1; l.frame = 1;
    LKG.looks[8] = l;
    l = social::Look(); l.id = 9; l.emblem_mode = 1; l.emblem_tier = 7; l.emblem_division = 21; l.emblem_top = 3; l.color = 7; l.frame = 7;
    l.pulse = true; l.title = "S1 Classic Legend #3";
    LKG.looks[9] = l;
}

bool locker_gallery(const std::string &p)
{
    g_rewards_gallery = false;
    if (p != "locker" && p != "locker-emblem" && p != "stats-rewards" && p.compare(0, 14, "ranked-results") != 0 && p != "stats-ranked")
        return false;
    locker_sample();
    G.ranked = true;
    g_rewards_gallery = true;
    if (p == "locker" || p == "locker-emblem") {
        S.page = LOCKER;
        LK.slot = p == "locker" ? 2 : 3;
        LK.sel = p == "locker" ? 2 : 0;
        LK.top = 0;
        LK.back = STATS;
        return true;
    }
    if (p == "stats-rewards") {
        S.page = STATS;
        S.stats_page = 0;
        ranked_stats_tab() = 1 + NETRANK_MODES;
        LK.rewards_mode = NETRANK_CLASSIC;
        LK.rewards_sel = 2;
        return true;
    }
    return false;       // the ranked pages: their own setup (ranked_gallery), the rewards on
}
