// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// TOWERS (T0, towers.h): the climb mode's screens. Included into online_screen.cpp after music_ui.inc.
//
//   TOWERS page     the three towers side by side (UPTOWN, FIVE BOROUGHS, THE WEEKLY "coming soon"), each a
//                   drawn building with its floors lit for the saved run, the best floor as a gold line and the
//                   floor fought next pulsing. A CLIMB (or CONTINUE), X NEW CLIMB over a saved run, Y RULES.
//   the start card  after the hub's fighter grid: the tower, its floors, the climber, the rules; A starts it.
//   the climb       after every won floor (the overlay's TOWERS window, input held): about 3.5 s, A skips. The
//                   beaten fighter greys out under a BEATEN stamp, the marker climbs one floor up the tower, the
//                   next fighter slides in with the venue, then the FLOOR plate. The next fight is asked for at
//                   once, so the game loads under it; on a save point (every floor of Uptown, each chapter's end
//                   in Five Boroughs) B SAVE AND QUIT is offered for those 3.5 s and the fight is asked for after.
//   the card        the same scene, drawn only, while the next fight loads (and before the first floor).
//   the end         KNOCKED OUT ON FLOOR n (a loss ends the run) or the tower CLEARED: the numbers and the run's
//                   fighters; A BACK TO TOWERS (the hub opens on this page), B the battle menu.
// The towers' art: scripts/tower-art.py renders the port's own three buildings at setup to
// <data>/online-art/towers/tower<N>-512.png (towers.json says where the floor column goes); without them every shape
// is drawn here in the hub's look (glass, the Anton titles), original to this mode.
//
// T1 (build 97): signed in, a climb is on the account service's boards (towers.c talks to it). X on the page opens
// the BOARD of the chosen tower (LB / RB: this month or all time): the top 10 and your place. The page shows your
// place under each tower and says whether your climbs go on the board. A start asks the service for the run first
// (the card says so while it answers). The climb plays two of the front end's own sounds (Audio.Play).
//
// Tests: RECOMP_TEST_OPEN_ONLINE=towers opens the page; RECOMP_TEST_TOWERS_QUIT=<n> presses B on the climb after
// floor n (a save point); RECOMP_TEST_TOWERS_END=a|b answers the end screen after 3 s. Gallery pages: hub-towers,
// towers, towers-rules, towers-saved, towers-start, towers-climb-0..3, towers-card, towers-first, towers-draw,
// towers-lost, towers-cleared, towers-board, towers-board-all, towers-art (the drawn fallback beside the art),
// towers-starting; build 108: towers-pair, towers-pair-confirm, towers-start-team (player 2 signed in).
//
// Build 108, the Team Tower's player 2 sign-in (towers_partner.c): the start card shows PLAYER 2: GUEST (or the
// account) and Y (either controller) opens the sign-in card: the page and the code for player 2's phone, then
// player 2's name, confirmed with A on player 2's own controller (B: not me). Tests: RECOMP_TEST_TOWERS_PAIR_CARD=1
// opens the sign-in card on the Team Tower (fighters 55 and 22) with the page; RECOMP_TEST_TOWERS_P2_CONFIRM_UI=1
// is player 2's A 4 s after the name shows.

void open_fighters_for(bool towers);       // below: the hub's grid for a tower's climber
bool tower_online_card(ImDrawList *dl, const Frame &f, const Pad &pad);     // towers_online_ui.inc
void tower_online_open_card();
bool G_towers_starting = false;            // the gallery's towers-starting page
bool g_towers_close = false;               // the start card started a run: the online screens close
std::atomic<int> g_towers_hub{0};          // the end screen's A: the next opening of the screens shows TOWERS

struct TowersUi {
    int sel = 0;                    // the tower card chosen on the page, 0..3 (tw_card_tower)
    float grow[TOWER_PAGE_TOWERS] = {};     // each tower's step forward (0..1, eased)
    // build 106, the Team Tower card: LB / RB the fighter rule (STANDARD: tower 4, WITH COMMUNITY: tower 5); a new
    // climb picks player 1's fighter, waits for player 2's A on pad 2, then player 2's pick (team_stage 1, 2, 3)
    bool team_comm = false;
    int team_stage = 0;
    int pick2 = -1, pick2_skin = 0;
    bool rules = false;             // Y: the rules panel over the page
    int card = 0;                   // 0 the towers, 1 the saved run's card, 2 the start card
    int pick = -1, pick_skin = 0;   // the climber chosen in the grid
    // the window
    bool window = false, armed = false, closing = false;
    ULONGLONG win_t0 = 0, card_seen = 0;
    TowersView v;
    TowerRun run;
    // the gallery's samples
    bool gal = false;
    int gal_window = 0;             // 1 the climb, 2 the card, 3 the end
    float gal_t = 0;
    TowerRun gal_runs[TOWER_COUNT];
    bool gal_have[TOWER_COUNT] = { false, false, false };
    int gal_best[TOWER_COUNT] = { 0, 0, 0 };
    // T1: the board, the start on the service, the climb's sounds
    bool board = false;             // X: the BOARD panel over the page
    int board_all = 0;              // 0 this month, 1 all time
    std::atomic<int> starting{0};   // 1 a start is asking the service, 2 it started, 3 it could not start
    bool sfx_move = false, sfx_land = false;
    bool gal_drawn = false;         // towers-art: the drawn fallback even with the art there
    // build 108: the gallery's player 2 sign-in sample (towers-pair, towers-pair-confirm, towers-start-team)
    bool gal_pair = false;
    PartnerView gal_pv = {};
    ULONGLONG confirm_seen = 0;     // the sign-in card first showed player 2's name then (the test's A)
} TWU;

// GET /api/towers/board?format=text (server relay/accounts/towers.go TowerBoardText), per season kind.
struct TowerBoardRow { int place = 0; long long account = 0; std::string name; int floors = 0; int secs = 0; int fighter = -1; bool cleared = false;
                       std::string partner; };     // build 106: the team towers' player 2 (an account's name, or GUEST)
struct TowerBoardTower { int entries = 0; std::vector<TowerBoardRow> rows; bool me = false; int me_place = 0, me_floors = 0, me_secs = 0; };
struct TowerBoard {
    bool valid = false;
    std::string season;
    long long ends = 0, now = 0;
    TowerBoardTower t[TOWER_COUNT];
    ULONGLONG got = 0;
    bool with_session = false;      // build 99: asked with the session, so the me= lines (the player's place) are in it
};
struct TowerBoards {
    std::mutex mu;
    TowerBoard b[2];                // [0] this month, [1] all time
    int status[2] = { 0, 0 };       // the last fetch: 200, 404 (the boards are off), 0 unreachable, -1 not yet
    ULONGLONG at[2] = { 0, 0 };
    std::atomic<int> busy[2];
    TowerBoards() { busy[0] = busy[1] = 0; }
} TWB;

bool parse_tower_board(const char *text, TowerBoard &b)
{
    b = TowerBoard();
    bool any = false;
    for (auto &kv : turf_lines(text)) {
        const std::string &k = kv.first, &v = kv.second;
        auto fl = ring_fields(v);
        if (k == "enabled") any = atoi(v.c_str()) != 0;
        else if (k == "season") b.season = v;
        else if (k == "ends") b.ends = atoll(v.c_str());
        else if (k == "now") b.now = atoll(v.c_str());
        else if (k == "tower" || k == "row" || k == "me") {
            int t = (int)ring_num(fl, 0);
            if (t < 1 || t > TOWER_COUNT) continue;
            TowerBoardTower &bt = b.t[t - 1];
            if (k == "tower") bt.entries = (int)ring_num(fl, 2);
            else if (k == "row" && fl.size() >= 8) {
                TowerBoardRow r;
                r.place = (int)ring_num(fl, 1);
                r.account = ring_num(fl, 2);
                r.name = ring_str(fl, 3);
                r.floors = (int)ring_num(fl, 4);
                r.secs = (int)ring_num(fl, 5);
                r.fighter = (int)ring_num(fl, 6);
                r.cleared = ring_num(fl, 7) != 0;
                if (fl.size() >= 9) r.partner = ring_str(fl, 8);
                bt.rows.push_back(r);
            } else if (k == "me") {
                bt.me = true;
                bt.me_place = (int)ring_num(fl, 1);
                bt.me_floors = (int)ring_num(fl, 2);
                bt.me_secs = (int)ring_num(fl, 3);
            }
        }
    }
    b.valid = any;
    return any;
}

// The gallery's sample board (and RECOMP_TEST_TOWERS_BOARD_FILE for tests without a service).
const char *const TOWERS_BOARD_SAMPLE =
    "enabled=1\nseason=2026-11\nends=1764547200\nnow=1763560000\n"
    "tower=1|UPTOWN|41\n"
    "row=1|1|12|Blaze|59|2712|22|1\nrow=1|2|51|Kaze|47|2380|9|0\nrow=1|3|34|Crow|41|2101|31|0\n"
    "row=1|4|77|Rex|36|1922|4|0\nrow=1|5|90|Nia|33|1840|70|0\nrow=1|6|91|Sol|29|1701|12|0\n"
    "row=1|7|92|Oz|24|1388|60|0\nrow=1|8|93|Tia|23|1290|18|0\nrow=1|9|94|Mo|19|1104|33|0\nrow=1|10|95|Vic|17|990|44|0\n"
    "me=1|14|12|655\n"
    "tower=2|FIVE BOROUGHS|17\n"
    "row=2|1|51|Kaze|88|5120|9|0\nrow=2|2|12|Blaze|61|3302|22|0\nrow=2|3|77|Rex|30|1650|4|0\n"
    "me=2|0|0|0\n";

void towers_fetch_board(int which)
{
    if (gallery_on() || which < 0 || which > 1 || TWB.busy[which].exchange(1)) return;
    TWB.at[which] = GetTickCount64();
    std::thread([which]() {
        std::vector<char> buf(16384);
        int len = 0, status = 0;
        bool with_session = false;
        const char *file = getenv("RECOMP_TEST_TOWERS_BOARD_FILE");
        if (file && *file) {
            FILE *fp = fopen(file, "rb");
            if (fp) { len = (int)fread(buf.data(), 1, buf.size() - 1, fp); fclose(fp); buf[len] = 0; status = 200; with_session = true; }
        } else {
            // signed in: account_call sends the session, so the answer has our own place (me=). Build 99: the same rule
            // as the page's SIGNED IN (towers_signed_in); account_call also takes a session being checked, and the
            // board without the session is only the fallback (it never has the player's place).
            const char *path = which ? "/api/towers/board?format=text&n=10&season=all" : "/api/towers/board?format=text&n=10";
            status = towers_signed_in() || account_state() == ACCOUNT_CHECKING
                         ? account_call("GET", path, nullptr, buf.data(), (int)buf.size(), 8000, nullptr) : -1;
            with_session = status == 200;
            if (status < 0) status = account_fetch(path, buf.data(), (int)buf.size(), &len);
            buf[buf.size() - 1] = 0;
        }
        TowerBoard b;
        bool ok = status == 200 && parse_tower_board(buf.data(), b);
        b.with_session = ok && with_session;
        {
            std::lock_guard<std::mutex> lock(TWB.mu);
            if (ok) { b.got = GetTickCount64(); TWB.b[which] = b; }
            else if (status == 404) TWB.b[which] = TowerBoard();
            TWB.status[which] = ok ? 200 : status;
        }
        if (ok)
            fprintf(stderr, "[TOWERS] board (%s): season %s, Uptown %d entries (you #%d), Five Boroughs %d (you #%d)\n",
                    which ? "all time" : "month", b.season.c_str(), b.t[0].entries, b.t[0].me_place, b.t[1].entries, b.t[1].me_place);
        else
            fprintf(stderr, "[TOWERS] board (%s): %s (HTTP %d)\n", which ? "all time" : "month", status == 404 ? "off" : "not loaded", status);
        TWB.busy[which] = 0;
    }).detach();
}

// The board as the page knows it now (the gallery's sample, else the last fetch, fetched again after a minute).
TowerBoard tw_board(int which)
{
    if (gallery_on()) {
        TowerBoard b;
        parse_tower_board(TOWERS_BOARD_SAMPLE, b);
        if (which) b.season = "all";
        return b;
    }
    bool stale = false;
    {
        std::lock_guard<std::mutex> lock(TWB.mu);
        // build 99: a board asked without the session (before the sign-in was through) has no place for the player:
        // signed in now, it is asked again at once (at most every 5 s), not after the minute
        stale = TWB.b[which].valid && !TWB.b[which].with_session && towers_signed_in() && GetTickCount64() - TWB.at[which] > 5000;
    }
    if (stale || GetTickCount64() - TWB.at[which] > 60000 || !TWB.at[which]) towers_fetch_board(which);
    std::lock_guard<std::mutex> lock(TWB.mu);
    return TWB.b[which];
}

const ImU32 TOWER_COL[TOWER_COUNT] = { IM_COL32(255, 150, 40, 255), IM_COL32(80, 190, 255, 255),
                                       IM_COL32(255, 60, 170, 255), IM_COL32(90, 220, 120, 255), IM_COL32(170, 130, 255, 255) };

// The tower of page card k: Uptown, Five Boroughs, the Weekly, the Team Tower (its fighter rule picks 4 or 5).
int tw_card_tower(int k) { return k < 3 ? k + 1 : TWU.team_comm ? TOWER_TEAM_COMMUNITY : TOWER_TEAM; }

// Build 106: player 2 presses A on the second controller (pad slot 1). Tests: RECOMP_TEST_TOWERS_P2_JOIN=1.
// Build 108: a fresh press of one of player 2's buttons (INPUT_SRC_*) on the second controller. The first read
// after a pause (the card not drawn last frame) only learns the state, so a button held from an earlier screen is
// no press.
bool tw_p2_edge(int src)
{
    static bool down[INPUT_SRC_COUNT];
    static int last_frame[INPUT_SRC_COUNT];
    InputRaw raw;
    if (src < 0 || src >= INPUT_SRC_COUNT) return false;
    int frame = ImGui::GetFrameCount();
    bool now = xbox_HostInputRawPad(1, &raw) && raw.src[src] >= 16384;
    bool fresh = last_frame[src] >= frame - 1 && now && !down[src];
    down[src] = now;
    last_frame[src] = frame;
    return fresh;
}

// The partner view (the gallery's sample in the gallery).
PartnerView tw_partner()
{
    PartnerView pv = {};
    if (gallery_on()) return TWU.gal_pair ? TWU.gal_pv : pv;
    towers_partner_view(&pv);
    return pv;
}

bool tw_p2_pressed_a()
{
    InputRaw raw;
    const char *t = getenv("RECOMP_TEST_TOWERS_P2_JOIN");
    if (t && *t == '1') return true;
    return xbox_HostInputRawPad(1, &raw) && raw.src[INPUT_SRC_SOUTH] >= 16384;
}

// ---- what the page shows (the gallery's samples, or towers.c's file) ----

bool tw_saved(int tower, TowerRun *out)
{
    if (gallery_on()) {
        if (!TWU.gal_have[tower - 1]) return false;
        if (out) *out = TWU.gal_runs[tower - 1];
        return true;
    }
    return towers_saved(tower, out) != 0;
}
int tw_best(int tower) { return gallery_on() ? TWU.gal_best[tower - 1] : towers_best(tower); }
int tw_floors(int tower)
{
    if (gallery_on()) return tower == TOWER_UPTOWN ? 59 : 87;
    return towers_floors_now(tower);
}

float ease_out_cubic(float t) { t = std::max(0.0f, std::min(1.0f, t)); return 1 - (1 - t) * (1 - t) * (1 - t); }
float ease_in_out_cubic(float t)
{
    t = std::max(0.0f, std::min(1.0f, t));
    return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}
float ease_out_back(float t)
{
    t = std::max(0.0f, std::min(1.0f, t));
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}
float clamp01(float t) { return std::max(0.0f, std::min(1.0f, t)); }

ImU32 shade_col(ImU32 c, float k)
{
    int r = (int)(((c >> IM_COL32_R_SHIFT) & 0xFF) * k), g = (int)(((c >> IM_COL32_G_SHIFT) & 0xFF) * k),
        b = (int)(((c >> IM_COL32_B_SHIFT) & 0xFF) * k), a = (int)((c >> IM_COL32_A_SHIFT) & 0xFF);
    return IM_COL32(std::min(255, r), std::min(255, g), std::min(255, b), a);
}

// a light in a window: lit or dark, the same every frame (a hash of its place)
bool window_lit(int kind, int col, int row)
{
    uint32_t h = (uint32_t)(kind * 7919 + col * 104729 + row * 1299709) * 2654435761u;
    return ((h >> 13) % 5) < 2;
}

// ---- the three towers, drawn ----
//
// Each in the 640x480 space: centre cx, ground at `base`, scale s (1 the page's size), dim 0..1 (1 full light).
// Returns the floor column's box (x, top, bottom) through col_x / col_top / col_bottom.

// The art's floor column (scripts/tower-art.py COLUMN, fractions of the 512 x 1024 picture: x centre, top, bottom,
// width) and the size the picture is drawn at on the page (its ground line at the picture's bottom).
const float TOWER_ART_COLUMN[TOWER_COUNT][4] = { { 0.50f, 0.20f, 0.90f, 0.13f }, { 0.50f, 0.16f, 0.96f, 0.11f },
                                                 { 0.43f, 0.15f, 0.88f, 0.12f } };
const float TOWER_ART_W = 128, TOWER_ART_H = 256;

const Tex &tower_tex(int kind)
{
    static const char *const rel[TOWER_ART_COUNT] = { "towers/tower1-512.png", "towers/tower2-512.png", "towers/tower3-512.png" };
    if (kind < 1 || kind > TOWER_ART_COUNT || TWU.gal_drawn) return no_tex();
    return tex(rel[kind - 1]);
}

void draw_tower_shape(ImDrawList *dl, const Frame &f, int kind, float cx, float base, float s, float dim, float *col_top,
                      float *col_bottom, float *col_w)
{
    ImU32 night = shade_col(IM_COL32(18, 16, 20, 255), 1);
    double now = ImGui::GetTime();
    const Tex &art = tower_tex(kind);
    if (art.srv) {      // the rendered building; the floor column is drawn over its marked zone
        float w = TOWER_ART_W * s, h = TOWER_ART_H * s, x = cx - w / 2, y = base - h;
        int k = (int)(255 * (0.45f + 0.55f * dim));
        image_fit(dl, f, art, x, y, w, h, IM_COL32(k, k, k, 255));
        const float *c = TOWER_ART_COLUMN[kind - 1];
        *col_top = y + c[1] * h;
        *col_bottom = y + c[2] * h;
        *col_w = c[3] * w;
        (void)night; (void)now;
        return;
    }
    if (kind == TOWER_UPTOWN) {
        // a brownstone tenement: brick, a cornice, a fire escape zigzag, a water tank on the roof, the stoop
        float w = 112 * s, h = 214 * s, x = cx - w / 2, y = base - h;
        ImU32 brick = shade_col(IM_COL32(128, 58, 42, 255), 0.45f + 0.55f * dim);
        ImU32 trim = shade_col(IM_COL32(70, 40, 32, 255), 0.45f + 0.55f * dim);
        rect(dl, f, x, y, w, h, brick, 2 * s);
        for (float by = y + 10 * s; by < base - 4 * s; by += 9 * s)        // brick courses
            dl->AddLine(f.p(x + 2 * s, by), f.p(x + w - 2 * s, by), with_alpha(BLACK, 0.18f), 1.0f);
        rect(dl, f, x - 6 * s, y - 8 * s, w + 12 * s, 10 * s, trim, 2 * s);  // the cornice
        rect(dl, f, x - 3 * s, y - 2 * s, w + 6 * s, 3 * s, shade_col(trim, 0.7f));
        // the water tank: legs, the barrel, the cone
        float tx = x + w * 0.70f, ty = y - 8 * s;
        dl->AddLine(f.p(tx + 3 * s, ty), f.p(tx + 3 * s, ty - 10 * s), trim, 1.5f * f.s());
        dl->AddLine(f.p(tx + 19 * s, ty), f.p(tx + 19 * s, ty - 10 * s), trim, 1.5f * f.s());
        rect(dl, f, tx, ty - 30 * s, 22 * s, 20 * s, shade_col(IM_COL32(96, 70, 48, 255), 0.45f + 0.55f * dim), 2 * s);
        dl->AddTriangleFilled(f.p(tx - 2 * s, ty - 30 * s), f.p(tx + 24 * s, ty - 30 * s), f.p(tx + 11 * s, ty - 40 * s), trim);
        // windows: 4 across, 11 up, some lit
        for (int r = 0; r < 11; r++)
            for (int c = 0; c < 4; c++) {
                if (c == 1 || c == 2) continue;                   // the middle stays free for the floor column
                float wx = x + 10 * s + c * 25 * s, wy = y + 10 * s + r * 17.5f * s;
                bool lit = window_lit(kind, c, r);
                rect(dl, f, wx, wy, 14 * s, 10 * s, lit ? shade_col(IM_COL32(255, 196, 110, 255), 0.35f + 0.65f * dim) : night, 1);
                rect(dl, f, wx - 1 * s, wy + 10 * s, 16 * s, 2 * s, trim);
            }
        // the fire escape on the left: landings and a zigzag of ladders
        ImU32 iron = shade_col(IM_COL32(40, 40, 44, 255), 0.6f + 0.4f * dim);
        for (int r = 0; r < 5; r++) {
            float ly = y + 28 * s + r * 36 * s;
            dl->AddLine(f.p(x - 4 * s, ly), f.p(x + 30 * s, ly), iron, 2.0f * f.s());
            if (r < 4) dl->AddLine(f.p(r % 2 ? x - 2 * s : x + 28 * s, ly), f.p(r % 2 ? x + 28 * s : x - 2 * s, ly + 36 * s),
                                   iron, 1.4f * f.s());
        }
        // the stoop
        for (int k = 0; k < 3; k++) rect(dl, f, cx - (16 + 5 * k) * s, base - (3 - k) * 4 * s, (32 + 10 * k) * s, 4 * s, trim);
        *col_top = y + 8 * s;
        *col_bottom = base - 16 * s;
        *col_w = 26 * s;
    } else if (kind == TOWER_FIVE_BOROUGHS) {
        // a skyline stack: neighbours left and right, then the tower in setbacks with a spire
        ImU32 steel = shade_col(IM_COL32(52, 74, 104, 255), 0.45f + 0.55f * dim);
        ImU32 dark = shade_col(IM_COL32(30, 40, 58, 255), 0.45f + 0.55f * dim);
        rect(dl, f, cx - 74 * s, base - 92 * s, 34 * s, 92 * s, dark);
        rect(dl, f, cx + 42 * s, base - 118 * s, 30 * s, 118 * s, dark);
        rect(dl, f, cx + 60 * s, base - 70 * s, 26 * s, 70 * s, shade_col(dark, 0.8f));
        static const float steps[4][2] = { { 104, 86 }, { 84, 66 }, { 62, 52 }, { 40, 40 } };    // width, height
        float y = base;
        for (int k = 0; k < 4; k++) {
            float w = steps[k][0] * s, h = steps[k][1] * s;
            y -= h;
            rect(dl, f, cx - w / 2, y, w, h, steel);
            dl->AddLine(f.p(cx - w / 2, y), f.p(cx + w / 2, y), shade_col(IM_COL32(150, 190, 230, 255), 0.4f + 0.6f * dim), 1.2f * f.s());
            for (float sx = cx - w / 2 + 6 * s; sx < cx + w / 2 - 4 * s; sx += 9 * s) {      // window strips
                if (fabsf(sx + 2 * s - cx) < 16 * s) continue;
                for (float sy = y + 5 * s; sy < y + h - 4 * s; sy += 8 * s) {
                    bool lit = window_lit(kind, (int)(sx / s), (int)(sy / s));
                    rect(dl, f, sx, sy, 4 * s, 4.5f * s,
                         lit ? shade_col(IM_COL32(170, 215, 255, 255), 0.35f + 0.65f * dim) : shade_col(night, 1.2f));
                }
            }
        }
        dl->AddTriangleFilled(f.p(cx - 9 * s, y), f.p(cx + 9 * s, y), f.p(cx, y - 34 * s), steel);
        float blink = 0.5f + 0.5f * (float)sin(now * 3.0);
        dl->AddCircleFilled(f.p(cx, y - 36 * s), 2.4f * s * f.s(), with_alpha(RED, (0.3f + 0.7f * blink) * dim));
        *col_top = base - 244 * s + 10 * s;
        *col_bottom = base - 8 * s;
        *col_w = 24 * s;
    } else {
        // a warehouse tower at night: dark iron, rivet lines, a neon sign down its side that flickers
        float w = 100 * s, h = 222 * s, x = cx - w / 2, y = base - h;
        ImU32 iron = shade_col(IM_COL32(34, 30, 40, 255), 0.6f + 0.4f * dim);
        rect(dl, f, x, y, w, h, iron, 3 * s);
        for (float ry = y + 12 * s; ry < base; ry += 22 * s)
            dl->AddLine(f.p(x + 3 * s, ry), f.p(x + w - 3 * s, ry), with_alpha(IM_COL32(90, 80, 100, 255), 0.5f), 1.0f);
        rect(dl, f, x - 4 * s, y - 6 * s, w + 8 * s, 8 * s, shade_col(iron, 1.4f), 2 * s);
        rect(dl, f, cx - 18 * s, base - 30 * s, 36 * s, 30 * s, shade_col(iron, 0.6f));      // the loading door
        for (float d = base - 28 * s; d < base; d += 4 * s)
            dl->AddLine(f.p(cx - 16 * s, d), f.p(cx + 16 * s, d), with_alpha(BLACK, 0.5f), 1.0f);
        ImU32 neon = TOWER_COL[2];
        int beat = (int)(now * 7.0) % 23;
        float flick = (beat == 3 || beat == 4 || beat == 11) ? 0.35f : 1.0f;
        float sx = x + w + 6 * s, sy = y + 18 * s;
        glass(dl, f, sx - 2 * s, sy - 4 * s, 20 * s, 132 * s, 3 * s, IM_COL32(20, 6, 18, 255), 0.85f);
        static const char *const letters[] = { "W", "E", "E", "K", "L", "Y" };
        for (int k = 0; k < 6; k++)
            text(dl, f, g_title, 15 * s, sx + 8 * s, sy + k * 21 * s, with_alpha(neon, (0.25f + 0.75f * flick) * (0.4f + 0.6f * dim)),
                 letters[k], CENTER, 0.6f);
        dl->AddRect(f.p(sx - 2 * s, sy - 4 * s), f.p(sx + 18 * s, sy + 128 * s), with_alpha(neon, 0.6f * flick * dim), 3 * s * f.s(), 0, 1.5f * f.s());
        *col_top = y + 12 * s;
        *col_bottom = base - 38 * s;
        *col_w = 24 * s;
    }
}

// The floor column over a tower: one bar per floor (grouped when the tower is taller than 24), lit up to the floors
// beaten, the best ever as a gold line, the floor fought next pulsing; Five Boroughs' chapters as notches.
void draw_floor_column(ImDrawList *dl, const Frame &f, int tower, float cx, float top, float bottom, float w, int floors,
                       int beaten, int best, int current, float dim)
{
    if (floors <= 0) return;
    int marks = std::min(floors, 24), per = (floors + marks - 1) / marks;
    marks = (floors + per - 1) / per;
    float step = (bottom - top) / marks, gap = std::min(2.0f, step * 0.25f);
    ImU32 col = TOWER_COL[tower - 1];
    float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 5.0);
    rect(dl, f, cx - w / 2 - 3, top - 3, w + 6, bottom - top + 6, with_alpha(BLACK, 0.55f * dim), 3);
    for (int i = 0; i < marks; i++) {
        int lo = i * per + 1, hi = std::min(floors, (i + 1) * per);
        float y1 = bottom - i * step, y0 = y1 - step + gap;
        bool lit = beaten >= hi, part = !lit && beaten >= lo, cur = current >= lo && current <= hi;
        ImU32 c = lit ? with_alpha(col, dim) : part ? with_alpha(col, 0.55f * dim) : with_alpha(IM_COL32(70, 70, 76, 255), 0.8f * dim);
        if (cur) c = with_alpha(WHITE, (0.35f + 0.65f * pulse) * dim);
        rect(dl, f, cx - w / 2, y0, w, y1 - y0, c, 1);
    }
    if (best > 0) {
        float y = bottom - std::min(1.0f, (float)best / floors) * (bottom - top);
        dl->AddLine(f.p(cx - w / 2 - 5, y), f.p(cx + w / 2 + 5, y), with_alpha(GOLD, dim), 1.6f * f.s());
    }
    if (tower == TOWER_FIVE_BOROUGHS || TOWER_IS_TEAM(tower))
        for (int c = TOWER_CHAPTER; c < floors; c += TOWER_CHAPTER) {
            float y = bottom - (float)c / floors * (bottom - top);
            dl->AddLine(f.p(cx - w / 2 - 6, y), f.p(cx - w / 2 - 1, y), with_alpha(CREAM, 0.8f * dim), 1.2f * f.s());
            dl->AddLine(f.p(cx + w / 2 + 1, y), f.p(cx + w / 2 + 6, y), with_alpha(CREAM, 0.8f * dim), 1.2f * f.s());
        }
}

const char *tower_sub(int tower)
{
    return tower == TOWER_UPTOWN ? "EVERY FIGHTER, ONE BY ONE" : tower == TOWER_FIVE_BOROUGHS ? "THE WHOLE CITY, COMMUNITY TOO"
           : TOWER_IS_TEAM(tower) ? "TWO PLAYERS, TWO CONTROLLERS" : "A NEW TOWER EVERY WEEK";
}

std::string tower_desc(int tower)
{
    if (tower == TOWER_UPTOWN)
        return "Every original fighter of the game, one per floor, in an order of your own: the run's seed shuffles "
               "them and picks a venue for each fight. The CPU starts on Easy and gets stronger up the tower. One loss "
               "and the climb is over. Your climb is saved after every floor. Standard fighters only.";
    if (tower == TOWER_FIVE_BOROUGHS)
        return "Uptown's fighters plus every community fighter and venue you have, as tall as your roster. Chapters of "
               "10 floors, each ending on a boss. Your climb is saved after every floor. One loss and the climb is over.";
    if (TOWER_IS_TEAM(tower))
        return std::string("Two players on this PC, two controllers, 2 vs 2 against a CPU team on every floor. Both CPUs get "
               "stronger up the tower; boss pairs every 10 floors, Crow and Blaze at the top. Saved after every floor, one loss "
               "and the climb is over. ") + (tower == TOWER_TEAM ? "STANDARD: the game's own fighters only (LB / RB to change)."
               : "WITH COMMUNITY: community fighters too, its own board (LB / RB to change).");
    return "Every week a new tower of 20 floors with its own twist, the same for everyone. Coming soon.";
}

// ---- the TOWERS page ----

void open_towers()
{
    S.page = TOWERS;
    TWU.card = 0;
    TWU.rules = false;
    TWU.board = false;
    S.towers_pick = false;
    int t = 0, floor = 0;
    if (!gallery_on() && towers_resolved_take(&t, &floor)) {     // the anti-quit rule (towers.c file_load)
        char line[160];
        snprintf(line, sizeof line, "Your %s climb ended on floor %d: the game closed during that fight, which counts as a loss.",
                 towers_name(t), floor);
        say(line, 8000);
    }
    if (!gallery_on() && towers_signed_in()) { TWB.at[0] = TWB.at[1] = 0; }     // fetched again on the page
}

void towers_rules_panel(ImDrawList *dl, const Frame &f, int tower)
{
    glass(dl, f, 120, 96, 400, 280, 14, IM_COL32(10, 10, 13, 255), 0.92f);
    text(dl, f, g_title, 22, 320, 108, CREAM, (std::string(towers_name(tower)) + "  -  RULES").c_str(), CENTER, 1.4f);
    static const char *const common[] = {
        "One on One against a CPU fighter on every floor.",
        "Full health at the start of every fight.",
        "No continues: a loss ends the climb. Your best floor counts.",
        "The CPU climbs with you: Easy, Normal, then Hard. Past Hard it fights smarter, punishes the same trick "
        "thrown again and again, hits harder, starts with momentum and takes more to put down.",
        "Bosses are the story's own, the same every climb, and Crow waits at the top. They hit hardest.",
        "Leaving a fight, or closing the game in one, counts as a loss. A draw is fought again once.",
        "Signed in, your climbs go on the monthly and all-time boards. Rewards are looks only.",
    };
    float y = 140;
    for (const char *line : common) {
        text(dl, f, g_hand, 13, 140, y, ORANGE, "-", LEFT, 0.8f);
        text(dl, f, g_title, 12.5f, 154, y, WHITE, line, LEFT, 0.8f, 350);
        y += text_width(f, g_title, 12.5f, line) > 350 ? 34 : 20;
    }
    const char *own = tower == TOWER_UPTOWN ? "Uptown: the original fighters; saved after every floor. Standard fighters only: no created or community fighter climbs it."
                    : tower == TOWER_FIVE_BOROUGHS ? "Five Boroughs: community fighters and venues too. Saved after every floor; one loss and the climb starts over. The board counts official packs only."
                    : TOWER_IS_TEAM(tower) ? "Team Tower: 2 vs 2, player 2 on the second controller. Both CPUs climb with you. Standard and With community have their own boards."
                    : "The Weekly: 20 floors and one or two twists a week, from the server. Coming soon.";
    text(dl, f, g_title, 12, 140, y + 4, CREAM, own, LEFT, 0.8f, 370);
    float hx = 254;
    hint(dl, f, hx, 352, "y", "CLOSE");
}

// X: the chosen tower's board, this month or all time (LB / RB), the top 10 and the player's own place.
void towers_board_panel(ImDrawList *dl, const Frame &f, int tower, int which, bool signed_now)
{
    glass(dl, f, 110, 78, 420, 324, 14, IM_COL32(10, 10, 13, 255), 0.94f);
    ImU32 col = TOWER_COL[tower - 1];
    text(dl, f, g_stencil, 22, 130, 88, CREAM, towers_name(tower), LEFT, 1.4f);
    text(dl, f, g_title, 13, 510, 92, col, which ? "BOARD - ALL TIME" : "BOARD - THIS MONTH", RIGHT, 0.8f);
    rect(dl, f, 130, 116, 380, 2, col);
    TowerBoard b = tw_board(which);
    int status;
    {
        std::lock_guard<std::mutex> lock(TWB.mu);
        status = TWB.status[which];
    }
    if (tower == TOWER_WEEKLY) {
        text(dl, f, g_hand, 14, 320, 220, ORANGE, "The Weekly's boards open with The Weekly.", CENTER, 0.8f);
        return;
    }
    if (!b.valid) {
        const char *why = gallery_on() ? "" : status == 404 ? "The boards are not open yet." :
                          (status == 0 && TWB.at[which] && !TWB.busy[which]) ? "The boards could not be reached." : "Loading the board...";
        text(dl, f, g_hand, 14, 320, 220, ORANGE, why, CENTER, 0.8f);
        return;
    }
    const TowerBoardTower &bt = b.t[tower - 1];
    char line[96];
    text(dl, f, g_title, 9.5f, 150, 124, GREY, "#", CENTER, 0.6f);
    text(dl, f, g_title, 9.5f, 168, 124, GREY, "CLIMBER", LEFT, 0.6f);
    text(dl, f, g_title, 9.5f, 420, 124, GREY, "FLOORS", RIGHT, 0.6f);
    text(dl, f, g_title, 9.5f, 500, 124, GREY, "TIME", RIGHT, 0.6f);
    float y = 140;
    if (bt.rows.empty()) text(dl, f, g_hand, 13, 320, 200, ORANGE, "Nobody on this board yet. Be the first.", CENTER, 0.8f);
    for (const auto &r : bt.rows) {
        bool mine = bt.me && bt.me_place == r.place;
        if (mine) rect(dl, f, 132, y - 2, 376, 20, with_alpha(col, 0.22f), 3);
        snprintf(line, sizeof line, "%d", r.place);
        text(dl, f, g_title, 13, 150, y, r.place <= 3 ? GOLD : WHITE, line, CENTER, 0.8f);
        if (r.fighter >= 0) portrait(dl, f, r.fighter, 166, y - 1, 14, 18);
        std::string who = r.partner.empty() ? r.name : r.name + " + " + r.partner;
        text(dl, f, g_title, 13, 186, y, mine ? CREAM : WHITE, who.c_str(), LEFT, 0.8f, 180);
        snprintf(line, sizeof line, r.cleared ? "%d  CLEARED" : "%d", r.floors);
        text(dl, f, g_title, 13, 420, y, r.cleared ? GOLD : col, line, RIGHT, 0.8f);
        snprintf(line, sizeof line, "%d:%02d", r.secs / 60, r.secs % 60);
        text(dl, f, g_body, 11, 500, y + 1, GREY, line, RIGHT, 0);
        y += 21;
    }
    // the player's own line
    float my = 362;
    rect(dl, f, 130, my - 6, 380, 1, with_alpha(GREY, 0.5f));
    if (!signed_now) text(dl, f, g_hand, 12, 320, my, ORANGE, pad_text("Sign in to put your climb on the board: press {y} in the hub.").c_str(), CENTER, 0.8f);
    else if (!b.with_session && !gallery_on()) text(dl, f, g_hand, 12, 320, my, GREY, "Loading your place...", CENTER, 0.8f);
    else if (bt.me && bt.me_place > 0) {
        snprintf(line, sizeof line, "YOU: #%d OF %d  -  %d FLOORS  -  %d:%02d", bt.me_place, bt.entries, bt.me_floors, bt.me_secs / 60, bt.me_secs % 60);
        text(dl, f, g_title, 13, 320, my, CREAM, line, CENTER, 0.8f);
    } else text(dl, f, g_hand, 12, 320, my, GREY, "You are not on this board yet: climb while signed in.", CENTER, 0.8f);
    float hx = 214;
    hx += hint(dl, f, hx, 382, "lb", which ? "THIS MONTH" : "ALL TIME");
    hint(dl, f, hx, 382, "x", "CLOSE");
}

// The saved run's card (A CONTINUE, X NEW CLIMB) or the start card (A START THE CLIMB).
void towers_run_card(ImDrawList *dl, const Frame &f, int tower, const TowerRun *saved, int fighter, int skin)
{
    glass(dl, f, 150, 110, 340, 250, 14, IM_COL32(12, 10, 14, 255), 0.9f);
    ImU32 col = TOWER_COL[tower - 1];
    text(dl, f, g_stencil, 26, 320, 120, CREAM, towers_name(tower), CENTER, 1.6f);
    rect(dl, f, 250, 152, 140, 2, col);
    portrait(dl, f, fighter, 172, 172, 96, 120);
    frame_rect(dl, f, 172, 172, 96, 120, with_alpha(col, 0.8f), 1.5f);
    std::string name = fighter_label(fighter);
    FighterSkin look;
    if (skin > 0 && fighter_skin_get(fighter, skin, &look)) name += std::string(" (") + look.name + ")";
    if (TOWER_IS_TEAM(tower)) {     // build 106: player 2 beside player 1
        int f2 = saved ? saved->fighter2 : TWU.pick2;
        if (f2 >= 0) {
            portrait(dl, f, f2, 226, 230, 50, 62);
            frame_rect(dl, f, 226, 230, 50, 62, with_alpha(col, 0.9f), 1.5f);
            name += "  +  " + fighter_label(f2);
        }
    }
    text(dl, f, g_title, 11, 220, 296, GREY, TOWER_IS_TEAM(tower) ? "THE TEAM" : "THE CLIMBER", CENTER, 0.8f);
    text(dl, f, g_title, std::min(15.0f, fit_size(f, g_title, 15, name.c_str(), 110)), 220, 308, WHITE, name.c_str(), CENTER, 1.0f);
    char line[96];
    float x = 290, y = 172;
    if (saved) {
        text(dl, f, g_title, 12, x, y, GREY, "SAVED CLIMB", LEFT, 0.8f);
        snprintf(line, sizeof line, "FLOOR %d OF %d", saved->floor, saved->floors);
        text(dl, f, g_title, 20, x, y + 14, col, line, LEFT, 1.2f);
        snprintf(line, sizeof line, "%d BEATEN  -  %d MIN CLIMBED", saved->floor - 1, (int)(saved->climb_ms / 60000));
        text(dl, f, g_title, 11, x, y + 42, CREAM, line, LEFT, 0.8f);
        int next = towers_opponent(saved, saved->floor), next2 = towers_opponent2(saved, saved->floor);
        if (next >= 0) {
            std::string up = fighter_label(next);
            if (next2 >= 0) up += " + " + fighter_label(next2);
            text(dl, f, g_title, 11, x, y + 64, GREY, "NEXT UP", LEFT, 0.8f);
            text(dl, f, g_title, 15, x, y + 77, WHITE, up.c_str(), LEFT, 1.0f, 180);
        }
        snprintf(line, sizeof line, "SEED %08X", saved->seed);
        text(dl, f, g_body, 10, x, y + 104, with_alpha(GREY, 0.9f), line, LEFT, 0);
    } else {
        int floors = tw_floors(tower);
        text(dl, f, g_title, 12, x, y, GREY, "THE TOWER", LEFT, 0.8f);
        snprintf(line, sizeof line, "%d FLOORS", floors);
        text(dl, f, g_title, 20, x, y + 14, col, line, LEFT, 1.2f);
        text(dl, f, g_title, 11, x, y + 42, CREAM, tower == TOWER_FIVE_BOROUGHS ? "CHAPTERS OF 10, A BOSS AT THE END OF EACH"
                                                                               : "SAVED AFTER EVERY FLOOR", LEFT, 0.8f, 180);
        text(dl, f, g_title, 11, x, y + 64, ORANGE, "NO CONTINUES: ONE LOSS ENDS IT", LEFT, 0.8f, 180);
        if (!TOWER_IS_TEAM(tower) || !(gallery_on() || towers_signed_in()))     // build 108: the team's player 2 line there
            text(dl, f, g_hand, 11, x, y + 86, GREY, "A fresh seed for every climb: nobody gets the same order.", LEFT, 0.8f, 180);
    }
}

// Build 108: the sign-in card of player 2 (towers_partner.c): the page and the code, then the name to confirm.
void towers_pair_card(ImDrawList *dl, const Frame &f, int tower, const Pad &pad)
{
    ImU32 col = TOWER_COL[tower - 1];
    PartnerView pv = tw_partner();
    float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 4.0);
    glass(dl, f, 140, 100, 360, 270, 14, IM_COL32(12, 10, 14, 255), 0.94f);
    text(dl, f, g_stencil, 22, 320, 110, CREAM, "PLAYER 2: SIGN IN", CENTER, 1.4f);
    rect(dl, f, 250, 138, 140, 2, col);
    bool p2a = !gallery_on() && tw_p2_edge(INPUT_SRC_SOUTH), p2b = !gallery_on() && tw_p2_edge(INPUT_SRC_EAST);
    float hx = 230;
    switch (pv.state) {
    case PARTNER_STARTING:
        text(dl, f, g_title, 12, 320, 220, with_alpha(CREAM, 0.5f + 0.5f * pulse), "ASKING THE SERVER FOR A CODE...", CENTER, 0.8f);
        hint(dl, f, 280, 344, "b", "BACK");
        break;
    case PARTNER_WAITING: {
        std::string url = pv.url;
        if (!url.compare(0, 8, "https://")) url = url.substr(8);
        else if (!url.compare(0, 7, "http://")) url = url.substr(7);
        text(dl, f, g_title, 11, 170, 152, GREY, "1   ON YOUR PHONE, OPEN", LEFT, 0.8f);
        text(dl, f, g_title, std::min(19.0f, fit_size(f, g_title, 19, url.c_str(), 300)), 320, 168, WHITE, url.c_str(), CENTER, 1.0f);
        text(dl, f, g_title, 11, 170, 200, GREY, "2   TYPE THIS CODE", LEFT, 0.8f);
        glass(dl, f, 220, 216, 200, 52, 10, IM_COL32(20, 16, 12, 255), 0.9f);
        frame_rect(dl, f, 220, 216, 200, 52, with_alpha(col, 0.9f), 1.5f);
        text(dl, f, g_stencil, 34, 320, 222, col, pv.code, CENTER, 2.0f);
        text(dl, f, g_title, 11, 170, 280, GREY, "3   SIGN IN WITH DISCORD OR YOUR FFNY USERNAME", LEFT, 0.8f, 320);
        char line[96];
        snprintf(line, sizeof line, "Waiting for player 2...  The code runs out in %d:%02d.", pv.secs_left / 60, pv.secs_left % 60);
        text(dl, f, g_hand, 11, 320, 304, with_alpha(CREAM, 0.6f + 0.4f * pulse), line, CENTER, 0.8f);
        text(dl, f, g_hand, 10, 320, 322, GREY, "This signs player 2 in for the Team Tower only, on this PC.", CENTER, 0.6f);
        hx = 236;
        hx += hint(dl, f, hx, 344, "y", "NEW CODE");
        hint(dl, f, hx, 344, "b", "GUEST");
        break;
    }
    case PARTNER_CONFIRM: {
        if (!TWU.confirm_seen) TWU.confirm_seen = GetTickCount64();
        text(dl, f, g_title, 12, 320, 170, GREY, "PLAYER 2 SIGNED IN AS", CENTER, 0.8f);
        text(dl, f, g_stencil, std::min(30.0f, fit_size(f, g_stencil, 30, pv.name, 320)), 320, 190, GOLD, pv.name, CENTER, 1.6f);
        glass(dl, f, 170, 252, 300, 30, 8, IM_COL32(14, 30, 18, 255), 0.9f);
        text(dl, f, g_title, 13, 320, 259, with_alpha(col, 0.55f + 0.45f * pulse), "PLAYER 2: IS THAT YOU? PRESS A", CENTER, 1.0f);
        text(dl, f, g_hand, 10, 320, 292, GREY, "A on player 2's own controller confirms. Not you? B: player 2 plays as a guest.",
             CENTER, 0.6f, 320);
        hx = 250;
        hx += hint(dl, f, hx, 344, "a", "IT'S ME (P2)");
        hint(dl, f, hx, 344, "b", "NOT ME");
        const char *t = getenv("RECOMP_TEST_TOWERS_P2_CONFIRM_UI");
        if (t && *t == '1' && GetTickCount64() - TWU.confirm_seen > 4000) p2a = true;     // tests: player 2's A
        if (p2a && towers_partner_confirm()) {
            say(std::string("Player 2 is ") + pv.name + ": both names go on the board.", 5000);
            TWU.card = 2;
            return;
        }
        if (p2b || pad.b) { towers_partner_reject(); say("Player 2 plays as a guest.", 4000); TWU.card = 2; return; }
        return;
    }
    case PARTNER_SIGNED:
        TWU.card = 2;
        return;
    case PARTNER_FAILED:
        text(dl, f, g_title, 12, 320, 190, ORANGE, "PLAYER 2 COULD NOT SIGN IN", CENTER, 0.8f);
        text(dl, f, g_hand, 12, 320, 214, CREAM, pv.why, CENTER, 0.8f, 320);
        hx = 236;
        hx += hint(dl, f, hx, 344, "y", "TRY AGAIN");
        hint(dl, f, hx, 344, "b", "GUEST");
        break;
    default:
        TWU.card = 2;
        return;
    }
    if (gallery_on()) return;
    if (pad.y || tw_p2_edge(INPUT_SRC_NORTH)) { towers_partner_begin(); return; }
    if (pad.b || p2b) { towers_partner_cancel(); TWU.card = 2; }
}

// Tests (build 108): RECOMP_TEST_TOWERS_PAIR_CARD=1: the Team Tower's sign-in card with the page open.
void towers_ui_test_pair()
{
    TWU.sel = 3;
    TWU.team_comm = false;
    TWU.pick = 55; TWU.pick_skin = 0;
    TWU.pick2 = 22; TWU.pick2_skin = 0;
    TWU.team_stage = 0;
    TWU.card = towers_partner_begin() ? 5 : 2;
    TWU.confirm_seen = 0;
    fprintf(stderr, "[TOWERS] test: the sign-in card of player 2 (%s)\n", TWU.card == 5 ? "open" : "player 1 is signed out");
}

void draw_towers_page(ImDrawList *dl, const Frame &f, const Pad &pad, ImVec2 display)
{
    backdrop(dl, f, display, true, LOOK_STATS);
    page_title(dl, f, "TOWERS");
    text(dl, f, g_hand, 14, 590, 34, ORANGE, "CLIMB NEW YORK, ONE FIGHTER AT A TIME", RIGHT, 1.0f);
    int tower = tw_card_tower(TWU.sel);
    TowerRun saved;
    bool have = tw_saved(tower, &saved);
    if (TWU.card == 0 && !TWU.rules && !TWU.board) {
        if (pad.left) TWU.sel = std::max(0, TWU.sel - 1);
        if (pad.right) TWU.sel = std::min(TOWER_PAGE_TOWERS - 1, TWU.sel + 1);
        if (TWU.sel == 3 && (pad.lb || pad.rb)) TWU.team_comm = !TWU.team_comm;     // build 106: the fighter rule
    }
    tower = tw_card_tower(TWU.sel);
    have = tw_saved(tower, &saved);
    float dt = std::min(0.1f, ImGui::GetIO().DeltaTime);
    bool signed_now = gallery_on() || towers_signed_in();     // build 99: the rule towers.c posts runs by
    TowerBoard month = signed_now ? tw_board(0) : TowerBoard();
    for (int k = 0; k < TOWER_PAGE_TOWERS; k++) {
        float want = k == TWU.sel ? 1.0f : 0.0f;
        TWU.grow[k] += (want - TWU.grow[k]) * std::min(1.0f, dt / 0.05f);     // about 150 ms to settle
        if (gallery_on()) TWU.grow[k] = want;
    }
    for (int k = 0; k < TOWER_PAGE_TOWERS; k++) {
        int t = tw_card_tower(k);
        float cx = 95 + k * 150, base = 326;
        float s = 1.0f + 0.08f * ease_out_cubic(TWU.grow[k]);
        float dim = 0.6f + 0.4f * TWU.grow[k];
        if (mouse_pick(f, cx - 80, 80, 160, 300) && TWU.card == 0) TWU.sel = k;
        if (mouse_click(f, cx - 80, 80, 160, 300) && TWU.card == 0) const_cast<Pad &>(pad).a = true;
        // the light behind the chosen one
        if (TWU.grow[k] > 0.01f)
            dl->AddCircleFilled(f.p(cx, base - 120), 120 * f.s(), with_alpha(TOWER_COL[t - 1], 0.10f * TWU.grow[k]), 48);
        float top, bottom, w;
        draw_tower_shape(dl, f, t, cx, base, s, dim, &top, &bottom, &w);
        float ccx = tower_tex(t).srv ? cx - TOWER_ART_W * s / 2 + TOWER_ART_COLUMN[k][0] * TOWER_ART_W * s : cx;
        TowerRun r;
        bool hr = t != TOWER_WEEKLY && tw_saved(t, &r);
        int floors = hr ? r.floors : tw_floors(t), best = tw_best(t);
        if (t == TOWER_WEEKLY) floors = 20;
        draw_floor_column(dl, f, t, ccx, top, bottom, w, floors, hr ? r.floor - 1 : 0, best, hr ? r.floor : 0, dim);
        if (t == TOWER_WEEKLY) {    // the banner across it
            float by = base - 130;
            rect(dl, f, cx - 64, by, 128, 24, with_alpha(IM_COL32(150, 14, 30, 255), 0.92f * dim), 3);
            text(dl, f, g_title, 15, cx, by + 3, with_alpha(WHITE, dim), "COMING SOON", CENTER, 1.0f);
        }
        // the name plate
        float plate_h = signed_now && t != TOWER_WEEKLY ? 56.0f : 44.0f;     // T1: room for the board place
        glass(dl, f, cx - 72, base + 8, 144, plate_h, 8, k == TWU.sel ? IM_COL32(44, 32, 22, 255) : IM_COL32(10, 10, 13, 255),
              k == TWU.sel ? 0.8f : 0.55f);
        if (k == TWU.sel && TWU.card == 0) selector(dl, f, cx - 72, base + 8, 144, plate_h);
        text(dl, f, g_title, TOWER_IS_TEAM(t) ? 15.0f : 17.0f, cx, base + 10, with_alpha(k == TWU.sel ? CREAM : GREY, dim),
             TOWER_IS_TEAM(t) ? "TEAM TOWER" : towers_name(t), CENTER, 1.2f);
        if (TOWER_IS_TEAM(t))       // build 106: the fighter rule, LB / RB
            text(dl, f, g_title, 9.5f, cx, base + plate_h + 12, with_alpha(k == TWU.sel ? TOWER_COL[t - 1] : GREY, dim),
                 t == TOWER_TEAM ? "<  STANDARD  >" : "<  WITH COMMUNITY  >", CENTER, 0.6f);
        char line[64];
        if (t == TOWER_WEEKLY) snprintf(line, sizeof line, "20 FLOORS  -  SOON");
        else if (hr) snprintf(line, sizeof line, "FLOOR %d / %d", r.floor, r.floors);
        else if (best > 0) snprintf(line, sizeof line, "BEST %d  -  %d FLOORS", best, floors);
        else snprintf(line, sizeof line, "%d FLOORS", floors);
        text(dl, f, g_title, 11, cx, base + 32, with_alpha(hr ? TOWER_COL[t - 1] : ORANGE, dim), line, CENTER, 0.8f);
        if (t != TOWER_WEEKLY && signed_now) {     // T1: the player's place on this month's board
            const TowerBoardTower &bt = month.t[t - 1];
            if (month.valid && bt.me && bt.me_place > 0) snprintf(line, sizeof line, "#%d OF %d THIS MONTH", bt.me_place, bt.entries);
            else if (month.valid) snprintf(line, sizeof line, "%d ON THE BOARD", bt.entries);
            else line[0] = 0;
            if (line[0]) text(dl, f, g_title, 9.5f, cx, base + 47, with_alpha(k == TWU.sel ? GOLD : GREY, dim), line, CENTER, 0.6f);
        }
    }
    // what the chosen tower is
    text(dl, f, g_hand, 11, 320, 394, with_alpha(CREAM, 0.95f), tower_desc(tower).c_str(), CENTER, 0.8f, 560);
    if (tower_online_card(dl, f, pad)) return;      // Team Tower Online's host / join card (towers_online_ui.inc)
    if (TWU.rules) {
        towers_rules_panel(dl, f, tower);
        if (pad.y || pad.b) TWU.rules = false;
        return;
    }
    if (TWU.board) {
        towers_board_panel(dl, f, tower, TWU.board_all, signed_now);
        if (pad.lb || pad.rb) TWU.board_all ^= 1;
        if (pad.x || pad.b) TWU.board = false;
        return;
    }
    if (TWU.card == 1 && have) {
        towers_run_card(dl, f, tower, &saved, saved.fighter, saved.skin);
        float hx = 186;
        hx += hint(dl, f, hx, 336, "a", "CONTINUE");
        hx += hint(dl, f, hx, 336, "x", "NEW CLIMB");
        hint(dl, f, hx, 336, "b", "BACK");
        int st = TWU.starting.load();      // T1: a resume may ask the service first (towers_resume blocks)
        if (st == 1) return;
        if (st == 2) { TWU.starting = 0; say("Back to the tower..."); g_towers_close = true; return; }
        if (st == 3) {
            TWU.starting = 0;
            const char *why = towers_start_error();
            say(why && *why ? why : "That climb could not be resumed.", 8000);
        }
        if (pad.b) TWU.card = 0;
        else if (pad.x) { TWU.team_stage = TOWER_IS_TEAM(tower) ? 1 : 0; open_fighters_for(true); }
        else if (pad.a && !gallery_on()) {
            int t = tower;
            TWU.starting = 1;
            std::thread([t]() { TWU.starting = towers_resume(t) ? 2 : 3; }).detach();
        }
        return;
    }
    if (TWU.card == 5) {    // build 108: player 2 signs in with their own account, on their phone
        towers_pair_card(dl, f, tower, pad);
        return;
    }
    if (TWU.card == 3) {    // build 106: player 2 joins on the second controller
        towers_run_card(dl, f, tower, nullptr, TWU.pick, TWU.pick_skin);
        float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 4.0);
        glass(dl, f, 170, 300, 300, 30, 8, IM_COL32(14, 30, 18, 255), 0.9f);
        text(dl, f, g_title, 14, 320, 306, with_alpha(TOWER_COL[tower - 1], 0.55f + 0.45f * pulse),
             "PLAYER 2: PRESS A ON YOUR CONTROLLER", CENTER, 1.0f);
        hint(dl, f, 280, 336, "b", "BACK");
        if (pad.b) { TWU.card = 0; TWU.team_stage = 0; return; }
        if (!gallery_on() && tw_p2_pressed_a()) {
            TWU.team_stage = 3;
            say("Player 2: pick your fighter.", 4000);
            open_fighters_for(true);
        }
        return;
    }
    if (TWU.card == 2) {
        towers_run_card(dl, f, tower, nullptr, TWU.pick, TWU.pick_skin);
        int st = TWU.starting.load();
        if (st == 1 || (gallery_on() && G_towers_starting)) {    // T1: the service deals the run (towers_start blocks)
            float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 5.0);
            text(dl, f, g_title, 12, 320, 336, with_alpha(CREAM, 0.5f + 0.5f * pulse), "ASKING THE SERVER FOR YOUR CLIMB...", CENTER, 0.8f);
            return;
        }
        if (st == 2) { TWU.starting = 0; say("Into the tower..."); g_towers_close = true; return; }
        if (st == 3) {
            TWU.starting = 0;
            const char *why = towers_start_error();
            say(why && *why ? why : "This tower cannot be built on this PC: no fighters or venues were read from the game.", 8000);
        }
        bool signed_now = gallery_on() || towers_signed_in();     // build 99: the rule towers.c posts runs by
        bool team = TOWER_IS_TEAM(tower) && signed_now;         // build 108: player 2's own account
        text(dl, f, g_hand, 11, 320, 318, signed_now ? GOLD : GREY, signed_now ? "Signed in: this climb goes on the board." :
             "Signed out: an offline climb, not on the board.", CENTER, 0.8f);
        if (team) {
            PartnerView pv = tw_partner();
            bool p2 = pv.state == PARTNER_SIGNED;
            std::string who = p2 ? std::string("PLAYER 2: ") + pv.name : "PLAYER 2: GUEST";
            text(dl, f, g_title, 11, 290, 258, p2 ? GOLD : CREAM, who.c_str(), LEFT, 0.8f, 180);
            text(dl, f, g_hand, 10, 290, 273, GREY, p2 ? "Both names on the board, both earn the team rewards." :
                 "Sign in so player 2 earns the team rewards too.", LEFT, 0.6f, 190);
            if (!gallery_on() && towers_partner_dropped())
                say("Player 2's sign-in ran out: player 2 played as a guest. Press Y to sign in again.", 8000);
        }
        float hx = team ? 158 : 196;
        hx += hint(dl, f, hx, 336, "a", "START THE CLIMB");
        if (team) hx += hint(dl, f, hx, 336, "y", "PLAYER 2 SIGN IN");
        hint(dl, f, hx, 336, "b", "BACK");
        if (team && !gallery_on() && (pad.y || tw_p2_edge(INPUT_SRC_NORTH))) {     // either controller's Y
            if (towers_partner_begin()) { TWU.card = 5; TWU.confirm_seen = 0; }
            else say("Player 1 signs in first (Y in the hub), then player 2 can.", 5000);
            return;
        }
        if (pad.b) { TWU.card = 0; TWU.team_stage = 0; }
        else if (pad.a && !gallery_on()) {
            int t = tower, pick = TWU.pick, skin = TWU.pick_skin, pick2 = TWU.pick2, skin2 = TWU.pick2_skin;
            TWU.starting = 1;
            if (TOWER_IS_TEAM(t))
                std::thread([t, pick, skin, pick2, skin2]() { TWU.starting = towers_start_team(t, pick, skin, pick2, skin2) ? 2 : 3; }).detach();
            else
                std::thread([t, pick, skin]() { TWU.starting = towers_start(t, pick, skin) ? 2 : 3; }).detach();
        }
        return;
    }
    TWU.card = 0;
    float hx = 54;
    if (tower == TOWER_WEEKLY) {
        hx += hint(dl, f, hx, 424, "y", "RULES");
        if (TOWER_IS_TEAM(tower)) hx += hint(dl, f, hx, 424, "lb", "RULE");
        hint(dl, f, hx, 424, "b", "BACK");
    } else {
        hx += hint(dl, f, hx, 424, "a", have ? "CONTINUE" : "CLIMB");
        if (tower == TOWER_TEAM) hx += hint(dl, f, hx, 424, "rt", "ONLINE");     // Team Tower Online
        hx += hint(dl, f, hx, 424, "x", "BOARD");
        hx += hint(dl, f, hx, 424, "y", "RULES");
        hint(dl, f, hx, 424, "b", "BACK");
    }
    text(dl, f, g_hand, 10, 590, 428, with_alpha(signed_now ? GOLD : GREY, 0.9f),
         signed_now ? "SIGNED IN  -  YOUR CLIMBS GO ON THE BOARD" : "SIGN IN TO PUT YOUR CLIMB ON THE BOARD", RIGHT, 0.8f);
    if (pad.y) { TWU.rules = true; return; }
    if (pad.b) { S.page = HUB; return; }
    if (pad.rt && tower == TOWER_TEAM) { tower_online_open_card(); return; }
    if (pad.x && tower != TOWER_WEEKLY) {     // build 99: the board as it is now (a climb may have gone on it since)
        TWU.board = true; TWU.board_all = 0;
        if (!gallery_on() && towers_signed_in()) TWB.at[0] = TWB.at[1] = 0;
        return;
    }
    if (tower == TOWER_WEEKLY) {
        if (pad.a) say("The Weekly opens soon: a new tower every week, the same for everyone.");
        return;
    }
    if (pad.a) {
        TWU.team_stage = 0;
        if (have) TWU.card = 1;
        else { if (TOWER_IS_TEAM(tower)) TWU.team_stage = 1; open_fighters_for(true); }
    }
}

bool towers_ui_originals_only()
{
    int t = tw_card_tower(TWU.sel);
    return t == TOWER_UPTOWN || t == TOWER_TEAM;
}

void open_fighters_for(bool towers)
{
    S.towers_pick = towers;
    S.rts_pick = S.ranked_pick = false;
    S.page = FIGHTERS;
}

// The grid's pick for a climber: the start card.
void towers_fighter_chosen(int fighter)
{
    int tower = tw_card_tower(TWU.sel);
    if (!towers_climber_ok(tower, fighter, ONLINE_NEW_FIGHTER(fighter))) {     // build 98: Uptown, standard only
        say(TOWER_IS_TEAM(tower) ? "The standard Team Tower takes standard fighters only." : "Uptown takes standard fighters only.", 4000);
        return;
    }
    S.towers_pick = false;
    if (TOWER_IS_TEAM(tower) && TWU.team_stage == 3) {     // build 106: player 2's pick, then the start card
        TWU.pick2 = fighter;
        TWU.pick2_skin = S.pick_skin;
        TWU.team_stage = 0;
        TWU.card = 2;
        S.page = TOWERS;
        return;
    }
    if (TOWER_IS_TEAM(tower)) {     // player 1's pick: player 2 joins next
        TWU.pick = fighter;
        TWU.pick_skin = S.pick_skin;
        TWU.pick2 = -1;
        TWU.team_stage = 2;
        TWU.card = 3;
        S.page = TOWERS;
        return;
    }
    TWU.pick = fighter;
    TWU.pick_skin = S.pick_skin;
    TWU.card = 2;
    S.page = TOWERS;
}

// ---- the climb between floors, the card over the load ----

const char *cpu_level_word(int level) { return level <= 0 ? "EASY" : level == 1 ? "NORMAL" : "HARD"; }

const ImU32 BOSS_FILL = IM_COL32(120, 90, 10, 255);     // the boss frame's gold, darkened (the floor plate's boss colour)

// Build 101: the BOSS FIGHT banner before a boss floor's fight (the climb's end and the card over the load): a gold band
// under the next fighter, slammed in as the floor plate is (pt 0..1), the boss's name and floor on it.
void towers_boss_banner(ImDrawList *dl, const Frame &f, const TowersView &v, float pt)
{
    float sc = 0.85f + 0.15f * ease_out_back(pt), a = clamp01(pt * 1.4f);
    float x = 118, w = 512, h = 58 * sc, y = 400 - h / 2;
    float pulse = 0.75f + 0.25f * (float)sin(ImGui::GetTime() * 4.0);
    rect(dl, f, x, y, w, h, with_alpha(BOSS_FILL, 0.95f * a), 5);
    rect(dl, f, x, y, w, 3, with_alpha(GOLD, a), 2);
    rect(dl, f, x, y + h - 3, w, 3, with_alpha(GOLD, a), 2);
    frame_rect(dl, f, x + 4, y + 6, w - 8, h - 12, with_alpha(GOLD, 0.55f * a * pulse), 1.2f);
    text(dl, f, g_stencil, 34 * sc, x + 18, y + 8 * sc, with_alpha(WHITE, a), "BOSS FIGHT", LEFT, 1.4f);
    std::string name = upper(fighter_label(v.next_opponent).c_str());
    while (name.size() > 1 && name.back() >= '0' && name.back() <= '9') name.pop_back();     // the story versions: "CROW", not "CROW1"
    text(dl, f, g_title, std::min(20.0f, fit_size(f, g_title, 20, name.c_str(), 230)), x + w - 18, y + 9 * sc,
         with_alpha(GOLD, a), name.c_str(), RIGHT, 1.0f, 230);
    char line[48];
    snprintf(line, sizeof line, "FLOOR %d", v.floor);
    text(dl, f, g_title, 11, x + w - 18, y + h - 22 * sc, with_alpha(CREAM, a), line, RIGHT, 0.6f);
    static int logged_floor = -1;
    if (logged_floor != v.floor) {
        logged_floor = v.floor;
        fprintf(stderr, "[TOWERS] BOSS FIGHT banner: %s, floor %d\n", name.c_str(), v.floor);
    }
}

// The scene at time t (seconds since the window opened; 3.5 and later: all of it). card: drawn only, over the load.
void towers_scene(ImDrawList *dl, const Frame &f, ImVec2 display, const TowersView &v, const TowerRun &run, float t, bool card)
{
    backdrop(dl, f, display, true, LOOK_MATCH);
    int tower = v.tower >= 1 && v.tower <= TOWER_COUNT ? v.tower : 1;
    ImU32 col = TOWER_COL[tower - 1];
    bool first = v.last_floor <= 0, draw = v.state == TOWERS_DRAW || (card && v.last_floor == v.floor);
    bool won = !first && !draw;
    text(dl, f, g_stencil, 26, 50, 22, CREAM, towers_name(tower), LEFT, 1.6f);
    char line[96];
    snprintf(line, sizeof line, "SEED %08X", v.seed);
    text(dl, f, g_body, 10, 590, 30, with_alpha(GREY, 0.9f), line, RIGHT, 0);
    if (first) text(dl, f, g_hand, 15, 590, 44, ORANGE, "THE CLIMB BEGINS", RIGHT, 1.0f);
    else if (draw) text(dl, f, g_hand, 15, 590, 44, ORANGE, "A DRAW: THIS FLOOR AGAIN", RIGHT, 1.0f);

    // the tower's column on the left: nine floors around the climber, the marker climbing one
    float colx = 70, top = 70, bottom = 392, slot = (bottom - top) / 9;
    float climb = won ? ease_in_out_cubic((t - 0.5f) / 1.1f) : 1.0f;
    float at = (won ? (float)v.last_floor + climb : (float)v.floor);     // the marker's floor, fractional
    float centre = std::max(5.0f, at);                                  // the column scrolls with it
    glass(dl, f, colx - 34, top - 10, 68, bottom - top + 20, 10, IM_COL32(10, 10, 13, 255), 0.7f);
    for (int k = -5; k <= 5; k++) {
        int floor = (int)floorf(centre) + k;
        float y = bottom - (floor - centre + 4.5f) * slot - slot * 0.5f;
        if (floor < 1 || floor > v.floors || y < top - 2 || y > bottom - slot + 2) continue;
        bool beaten = floor < v.floor || (won && floor == v.last_floor && climb >= 0.99f);
        bool lit = floor < v.last_floor || (won && floor == v.last_floor && t > 1.2f) || (!won && floor < v.floor);
        bool boss = towers_is_boss(tower, floor, v.floors) != 0;
        ImU32 c = lit ? col : boss ? BOSS_FILL : IM_COL32(64, 64, 70, 255);
        rect(dl, f, colx - 24, y + 2, 48, slot - 4, with_alpha(c, lit ? 0.85f : boss ? 0.95f : 0.6f), 3);
        if (boss) {     // build 101: a boss floor in gold, wider, with its tag on the left
            float pulse = 0.65f + 0.35f * (float)sin(ImGui::GetTime() * 3.0);
            frame_rect(dl, f, colx - 27, y + 1, 54, slot - 2, with_alpha(GOLD, beaten ? 0.6f : pulse), 2.0f);
            text(dl, f, g_title, 8, colx - 30, y + slot / 2 - 5, with_alpha(GOLD, beaten ? 0.5f : 0.95f), "BOSS", RIGHT, 0.6f);
        }
        snprintf(line, sizeof line, "%d", floor);
        text(dl, f, g_title, 13, colx, y + slot / 2 - 8, beaten ? BLACK : boss ? GOLD : WHITE, line, CENTER, beaten ? 0 : 0.8f);
    }
    {   // the marker: a chevron at the climber's floor
        float y = bottom - (at - centre + 4.5f) * slot - slot * 0.5f + slot / 2;
        float pulse = 0.6f + 0.4f * (float)sin(ImGui::GetTime() * 6.0);
        dl->AddTriangleFilled(f.p(colx + 30, y), f.p(colx + 40, y - 7), f.p(colx + 40, y + 7), with_alpha(WHITE, pulse));
        dl->AddTriangleFilled(f.p(colx - 30, y), f.p(colx - 40, y - 7), f.p(colx - 40, y + 7), with_alpha(WHITE, pulse));
    }
    snprintf(line, sizeof line, "%d / %d", std::min(v.floor, v.floors), v.floors);
    text(dl, f, g_title, 11, colx, bottom + 14, GREY, line, CENTER, 0.8f);

    // the beaten fighter: grey under the BEATEN stamp
    if (won && v.last_opponent >= 0) {
        float g = clamp01(t / 0.5f);
        int k = (int)(255 - 150 * g);
        float x = 150, y = 96, w = 132, h = 168;
        glass(dl, f, x - 8, y - 8, w + 16, h + 52, 10, IM_COL32(10, 10, 13, 255), 0.7f);
        portrait(dl, f, v.last_opponent, x, y, w, h, IM_COL32(k, k, k, 255));
        rect(dl, f, x, y, w, h, with_alpha(BLACK, 0.25f * g));
        text(dl, f, g_title, 14, x + w / 2, y + h + 8, with_alpha(GREY, 0.9f), fighter_label(v.last_opponent).c_str(), CENTER, 0.8f, w);
        snprintf(line, sizeof line, "FLOOR %d", v.last_floor);
        text(dl, f, g_title, 10, x + w / 2, y + h + 26, with_alpha(GREY, 0.8f), line, CENTER, 0.6f);
        float st = ease_out_back(clamp01(t / 0.5f)), sc = 1.4f - 0.4f * st;
        if (t > 0.05f) {
            float sw = 116 * sc, sh = 34 * sc, sx = x + w / 2 - sw / 2, sy = y + h * 0.42f - sh / 2;
            float a = clamp01(t / 0.25f);
            rect(dl, f, sx, sy, sw, sh, with_alpha(IM_COL32(150, 14, 30, 255), 0.82f * a), 3 * sc);
            frame_rect(dl, f, sx + 3 * sc, sy + 3 * sc, sw - 6 * sc, sh - 6 * sc, with_alpha(WHITE, 0.85f * a), 1.4f * sc);
            text(dl, f, g_stencil, 22 * sc, x + w / 2, sy + 4 * sc, with_alpha(WHITE, a), "BEATEN", CENTER, 0.8f);
        }
    }

    // the next fighter slides in from the right, the venue under him
    if (v.next_opponent >= 0) {
        float in = won ? ease_out_cubic((t - 1.6f) / 1.2f) : 1.0f;
        float x = 330 + (1 - in) * 330, y = 84, w = 270, h = 280;
        float a = clamp01(in * 1.5f);
        glass(dl, f, x, y, w, h, 12, IM_COL32(14, 10, 12, 255), 0.82f * a);
        rect(dl, f, x, y, 4, h, with_alpha(col, a), 2);
        text(dl, f, g_title, 11, x + 16, y + 10, with_alpha(GREY, a), v.next_rules.boss ? "BOSS FLOOR - NEXT UP" : "NEXT UP", LEFT, 0.8f);
        portrait(dl, f, v.next_opponent, x + 16, y + 28, 110, 138, with_alpha(IM_COL32_WHITE, a));
        frame_rect(dl, f, x + 16, y + 28, 110, 138, with_alpha(v.next_rules.boss ? GOLD : col, 0.8f * a), 1.5f);
        std::string name = fighter_label(v.next_opponent);
        if (v.next_opponent2 >= 0) {    // build 106: the CPU team
            portrait(dl, f, v.next_opponent2, x + 74, y + 112, 52, 64, with_alpha(IM_COL32_WHITE, a));
            frame_rect(dl, f, x + 74, y + 112, 52, 64, with_alpha(v.next_rules.boss ? GOLD : col, 0.8f * a), 1.5f);
            name += " + " + fighter_label(v.next_opponent2);
        }
        text(dl, f, g_title, std::min(22.0f, fit_size(f, g_title, 22, name.c_str(), 124)), x + 138, y + 30, with_alpha(WHITE, a),
             name.c_str(), LEFT, 1.2f);
        const char *style = online_fighter_style(v.next_opponent);
        if (v.next_opponent >= FB_FIRST_LOGICAL) style = "Community fighter";
        if (style && *style) text(dl, f, g_hand, 12, x + 138, y + 58, with_alpha(ORANGE, a), upper(style).c_str(), LEFT, 0.8f, 124);
        snprintf(line, sizeof line, "CPU %s", cpu_level_word(v.next_rules.cpu_level));
        text(dl, f, g_title, 11, x + 138, y + 84, with_alpha(CREAM, a), line, LEFT, 0.8f);
        float cy = y + 100;
        if (v.next_rules.cpu_damage > 1.005f) {
            snprintf(line, sizeof line, "HITS +%d%%", (int)((v.next_rules.cpu_damage - 1) * 100 + 0.5f));
            text(dl, f, g_title, 10, x + 138, cy, with_alpha(RED, a), line, LEFT, 0.6f); cy += 13;
        }
        if (v.next_rules.cpu_momentum > 0.005f) {
            snprintf(line, sizeof line, "MOMENTUM %d%%", (int)(v.next_rules.cpu_momentum * 100 + 0.5f));
            text(dl, f, g_title, 10, x + 138, cy, with_alpha(RED, a), line, LEFT, 0.6f, 124); cy += 13;
        }
        if (v.next_rules.cpu_health > 1.005f) {
            snprintf(line, sizeof line, "TOUGHER +%d%%", (int)((v.next_rules.cpu_health - 1) * 100 + 0.5f));
            text(dl, f, g_title, 10, x + 138, cy, with_alpha(RED, a), line, LEFT, 0.6f);
        }
        // the venue
        float vy = y + 178;
        text(dl, f, g_title, 10, x + 16, vy, with_alpha(GREY, a), "AT", LEFT, 0.6f);
        const Tex &vt = venue_tex(v.next_venue);
        if (vt.srv) image_fit(dl, f, vt, x + 16, vy + 13, 110, 84, with_alpha(IM_COL32_WHITE, a));
        else {      // a community venue has no picture: its name on a plate
            rect(dl, f, x + 16, vy + 13, 110, 84, with_alpha(IM_COL32(30, 26, 30, 255), a), 4);
            text(dl, f, g_title, 10, x + 71, vy + 46, with_alpha(GREY, a), "COMMUNITY VENUE", CENTER, 0.6f, 100);
        }
        text(dl, f, g_title, 14, x + 138, vy + 30, with_alpha(WHITE, a), upper(online_venue_name(v.next_venue)).c_str(), LEFT, 0.8f, 124);
    }

    // the floor plate; build 101: before a boss floor's fight the BOSS FIGHT banner with the boss's name instead
    float pt = won ? clamp01((t - 2.8f) / 0.4f) : 1.0f;
    if (pt > 0 && v.next_rules.boss && v.next_opponent >= 0) {
        towers_boss_banner(dl, f, v, pt);
    } else if (pt > 0) {
        float sc = 0.8f + 0.2f * ease_out_back(pt);
        snprintf(line, sizeof line, "FLOOR %d", v.floor);
        float w = 190 * sc, h = 40 * sc, x = 465 - w / 2, y = 376;
        rect(dl, f, x, y, w, h, with_alpha(v.next_rules.boss ? IM_COL32(120, 90, 10, 255) : IM_COL32(150, 14, 30, 255), 0.92f * pt), 4);
        text(dl, f, g_stencil, 28 * sc, 465, y + 4 * sc, with_alpha(WHITE, pt), line, CENTER, 1.0f);
    }
    if (card) {
        float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 4.0);
        text(dl, f, g_title, 13, 590, 452, with_alpha(CREAM, 0.4f + 0.6f * pulse), "GET READY...", RIGHT, 1.0f);
    }
}

// The run's end: KNOCKED OUT (or left), or CLEARED. Returns 1 for A (back to TOWERS), 2 for B, else 0.
int towers_end_screen(ImDrawList *dl, const Frame &f, ImVec2 display, const TowersView &v, const TowerRun &run, const Pad &pad)
{
    backdrop(dl, f, display, true, LOOK_STATS);
    int tower = v.tower >= 1 && v.tower <= TOWER_COUNT ? v.tower : 1;
    bool cleared = v.state == TOWERS_CLEARED;
    ImU32 col = TOWER_COL[tower - 1];
    char line[96];
    text(dl, f, g_stencil, 22, 50, 22, CREAM, towers_name(tower), LEFT, 1.4f);
    if (cleared) snprintf(line, sizeof line, "TOWER CLEARED");
    else snprintf(line, sizeof line, v.left_fight ? "LEFT THE FIGHT ON FLOOR %d" : v.draw_loss ? "A SECOND DRAW ON FLOOR %d" :
                                                                                  "KNOCKED OUT ON FLOOR %d", v.last_floor);
    text(dl, f, g_stencil, 34, 320, 58, cleared ? GOLD : WHITE, line, CENTER, 1.8f);
    int beaten = cleared ? v.floors : std::max(0, v.last_floor - 1);
    glass(dl, f, 120, 112, 400, 120, 12, IM_COL32(12, 10, 14, 255), 0.85f);
    struct { const char *k; std::string val; } facts[4];
    facts[0] = { "FLOORS BEATEN", std::to_string(beaten) + " / " + std::to_string(v.floors) };
    facts[1] = { "TIME CLIMBED", std::to_string(v.climb_ms / 60000) + ":" + (((v.climb_ms / 1000) % 60) < 10 ? "0" : "") +
                                 std::to_string((v.climb_ms / 1000) % 60) };
    facts[2] = { "BEST", std::to_string(v.best) };
    facts[3] = { "SEED", "" };
    snprintf(line, sizeof line, "%08X", v.seed);
    facts[3].val = line;
    for (int k = 0; k < 4; k++) {
        float x = 145 + k * 95;
        text(dl, f, g_title, 10, x + 38, 124, GREY, facts[k].k, CENTER, 0.6f);
        text(dl, f, g_title, k == 3 ? 15 : 22, x + 38, 140, k == 2 && v.new_best ? GOLD : WHITE, facts[k].val.c_str(), CENTER, 1.0f);
    }
    {   // T1: whether the climb is on the board
        char net[64];
        if (gallery_on()) snprintf(net, sizeof net, "ON THE BOARD");
        else towers_net_text(net, sizeof net);
        if (net[0]) text(dl, f, g_title, 9, 320, 236, v.online && v.board ? GOLD : GREY, net, CENTER, 0.6f);
    }
    if (v.new_best) text(dl, f, g_hand, 15, 320, 184, GOLD, "NEW BEST!", CENTER, 1.0f);
    else if (!cleared) text(dl, f, g_hand, 13, 320, 186, ORANGE, "One loss ends the climb. The next one starts from the bottom.", CENTER, 0.8f);
    if (!cleared && v.last_opponent >= 0) {
        snprintf(line, sizeof line, "PUT DOWN BY %s", upper(fighter_label(v.last_opponent).c_str()).c_str());
        text(dl, f, g_title, 12, 320, 208, RED, line, CENTER, 0.8f, 380);
    }
    // the run's fighters: the last floors, beaten ones grey with a cross, the one who ended it framed red
    int shown = std::min(beaten + (cleared ? 0 : 1), 12), from = std::max(1, (cleared ? beaten : v.last_floor) - shown + 1);
    float pw = 40, ph = 50, gap = 6, x0 = 320 - (shown * (pw + gap) - gap) / 2;
    for (int i = 0; i < shown; i++) {
        int floor = from + i;
        if (floor < 1 || floor > run.floors) continue;
        int id = run.roster[floor - 1];
        float x = x0 + i * (pw + gap), y = 250;
        bool lost_here = !cleared && floor == v.last_floor;
        portrait(dl, f, id, x, y, pw, ph, lost_here ? IM_COL32_WHITE : IM_COL32(110, 110, 110, 255));
        if (!lost_here) {
            dl->AddLine(f.p(x + 6, y + 6), f.p(x + pw - 6, y + ph - 6), with_alpha(RED, 0.75f), 2.0f * f.s());
            dl->AddLine(f.p(x + pw - 6, y + 6), f.p(x + 6, y + ph - 6), with_alpha(RED, 0.75f), 2.0f * f.s());
        }
        frame_rect(dl, f, x, y, pw, ph, lost_here ? RED : with_alpha(col, 0.6f), lost_here ? 2.0f : 1.0f);
        snprintf(line, sizeof line, "%d", floor);
        text(dl, f, g_title, 9, x + pw / 2, y + ph + 2, GREY, line, CENTER, 0.6f);
    }
    float hx = 214;
    hx += hint(dl, f, hx, 400, "a", "BACK TO TOWERS");    // (TOWERS' X shows the board)
    hint(dl, f, hx, 400, "b", "CLOSE");
    if (pad.a) return 1;
    if (pad.b) return 2;
    return 0;
}

// ---- the overlay's entry points (online_screen.h) ----

bool towers_take_request_impl()
{
    if (!towers_window_take()) return false;
    towers_view(&TWU.v);
    TWU.run = *towers_run();
    TWU.window = true;
    TWU.armed = false;
    TWU.closing = false;
    TWU.win_t0 = GetTickCount64();
    TWU.sfx_move = TWU.sfx_land = false;
    fprintf(stderr, "[TOWERS] window: state %d, floor %d of %d%s\n", TWU.v.state, TWU.v.floor, TWU.v.floors,
            TWU.v.save_point ? " (a save point)" : "");
    return true;
}

bool towers_window_impl(ImVec2 display)
{
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    Frame f = menu_frame(display);
    ULONGLONG now = GetTickCount64();
    float t = (now - TWU.win_t0) / 1000.0f;
    Pad idle = {};
    Pad pad = t > 0.35f ? read_pad() : idle;        // not the A that went through the results
    const TowersView &v = TWU.v;
    static const char *quit_at = getenv("RECOMP_TEST_TOWERS_QUIT");
    static const char *end_answer = getenv("RECOMP_TEST_TOWERS_END");
    if (TWU.armed && (t >= 3.5f || v.state == TOWERS_ARMED)) {
        // the fight is asked for: the card holds the input over the load (an A on the menu under it went into the
        // game's own screens) until the fight is set up; the draw-only card fades after that
        int st = towers_state();
        if (st != TOWERS_ARMED || t > 120.0f) { TWU.window = false; return true; }
        TowersView live;
        towers_view(&live);
        towers_scene(dl, f, display, live, *towers_run(), 3.5f, true);
        TWU.card_seen = now;
        return false;
    }
    if (v.state == TOWERS_ARMED) { TWU.armed = true; return false; }   // a run just started: its first card
    if (v.state == TOWERS_WON || v.state == TOWERS_DRAW) {
        if (!TWU.armed && !(v.state == TOWERS_WON && v.save_point)) {
            towers_climb_on();          // the next fight loads under the climb
            TWU.armed = true;
        }
        if (pad.a) TWU.win_t0 = now - 3500;     // A: to the end
        // the front end's own sounds: the marker climbs (0.5 s), the floor plate lands (2.8 s); RECOMP_TOWERS_SFX=a,b
        if (v.state == TOWERS_WON) {
            static int sfx[2] = { -2, -2 };
            if (sfx[0] == -2) {
                const char *e = getenv("RECOMP_TOWERS_SFX");
                sfx[0] = 5; sfx[1] = 1;
                if (e && *e) { sfx[0] = atoi(e); sfx[1] = strchr(e, ',') ? atoi(strchr(e, ',') + 1) : -1; }
            }
            if (!TWU.sfx_move && t >= 0.5f) { TWU.sfx_move = true; if (t < 1.5f) towers_play_sfx(sfx[0]); }
            if (!TWU.sfx_land && t >= 2.8f) { TWU.sfx_land = true; if (t < 3.4f) towers_play_sfx(sfx[1]); }
        }
        bool quit = v.state == TOWERS_WON && v.save_point && !TWU.armed &&
                    (pad.b || (quit_at && atoi(quit_at) == v.last_floor && t > 1.0f));
        towers_scene(dl, f, display, v, TWU.run, std::min(t, 3.5f), false);
        float hx = 54;
        if (t < 3.5f) hx += hint(dl, f, hx, 448, "a", "SKIP");
        if (v.state == TOWERS_WON && v.save_point && !TWU.armed) hint(dl, f, hx, 448, "b", "SAVE AND QUIT");
        if (quit) {
            towers_save_quit();
            g_towers_hub = 1;
            g_open_req = 1;     // the online screens open on TOWERS
            TWU.window = false;
            return true;
        }
        if (t >= 3.5f && !TWU.armed) { towers_climb_on(); TWU.armed = true; }
        return false;
    }
    int r = towers_end_screen(dl, f, display, v, TWU.run, pad);
    if (!r && end_answer && t > 3.0f) r = *end_answer == 'a' ? 1 : 2;
    if (r) {
        towers_done();
        if (r == 1) { g_towers_hub = 1; g_open_req = 1; }
        TWU.window = false;
        return true;
    }
    return false;
}

void towers_card_impl(ImDrawList *dl, ImVec2 display)
{
    TowersView v;
    towers_view(&v);
    Frame f = menu_frame(display);
    TWU.card_seen = GetTickCount64();
    towers_scene(dl, f, display, v, *towers_run(), 3.5f, true);
}

// ---- the gallery's pages ----

void towers_gallery_samples()
{
    static const int roster[] = { 22, 9, 31, 4, 70, 12, 60, 18, 33, 44, 2, 75, 57, 26, 81, 47 };
    TowerRun &a = TWU.gal_runs[0];
    a = TowerRun();
    a.tower = TOWER_UPTOWN;
    a.seed = 0x4F2A91C3u;
    a.fighter = 55;
    a.floors = 59;
    a.floor = 13;
    for (int i = 0; i < a.floors; i++) a.roster[i] = roster[i % 16];
    a.venues[0] = 1;
    a.venue_n = 1;
    a.health_pct = 100;
    a.climb_ms = 1000u * (19 * 60 + 42);
    TWU.gal_have[0] = true;
    TWU.gal_have[1] = TWU.gal_have[2] = false;
    TWU.gal_best[0] = 23;
    TWU.gal_best[1] = 7;
    TWU.gal_best[2] = 0;
}

void towers_gallery_view(TowersView &v, int state, int last_floor)
{
    v = TowersView();
    v.state = state;
    v.tower = TOWER_UPTOWN;
    v.floors = 59;
    v.floor = state == TOWERS_DRAW ? last_floor : last_floor + 1;
    v.last_floor = last_floor;
    v.last_opponent = TWU.gal_runs[0].roster[(last_floor - 1) % 16];
    v.last_venue = 3;
    v.next_opponent = TWU.gal_runs[0].roster[(v.floor - 1) % 16];
    v.next_venue = 5;
    towers_floor_rules(TOWER_UPTOWN, v.floor, 59, &v.next_rules);
    v.save_point = 1;
    v.best = 23;
    v.seed = 0x4F2A91C3u;
    v.climb_ms = 1000u * (19 * 60 + 42);
    v.fighter = 55;
    v.online = v.board = 1;
}

// Sets the page up; true when it is one of the TOWERS pages.
bool towers_gallery(const std::string &p)
{
    TWU.gal_window = 0;
    if (p.compare(0, 6, "towers") && p != "hub-towers") return false;
    towers_gallery_samples();
    TWU.rules = false;
    TWU.card = 0;
    TWU.board = false;
    TWU.gal_drawn = false;
    G_towers_starting = false;
    TWU.gal_pair = false;
    S.towers_pick = false;
    if (p == "hub-towers") { S.page = HUB; S.hub_sel = HUB_TOWERS; return true; }
    S.page = TOWERS;
    TWU.sel = 0;
    if (p == "towers-t2") TWU.sel = 1;
    else if (p == "towers-weekly") TWU.sel = 2;
    else if (p == "towers-rules") TWU.rules = true;
    else if (p == "towers-board" || p == "towers-board-all") { TWU.board = true; TWU.board_all = p == "towers-board-all"; }
    else if (p == "towers-art") TWU.gal_drawn = true;      // the drawn fallback (no art files)
    else if (p == "towers-starting") { TWU.sel = 0; TWU.card = 2; TWU.pick = 22; TWU.pick_skin = 0; G_towers_starting = true; }
    else if (p == "towers-saved") TWU.card = 1;
    else if (p == "towers-start") { TWU.sel = 1; TWU.card = 2; TWU.pick = 22; TWU.pick_skin = 0; }
    else if (p == "towers-pair" || p == "towers-pair-confirm" || p == "towers-start-team") {     // build 108
        TWU.sel = 3; TWU.team_comm = false; TWU.pick = 55; TWU.pick_skin = 0; TWU.pick2 = 22; TWU.pick2_skin = 0;
        TWU.card = p == "towers-start-team" ? 2 : 5;
        TWU.gal_pair = true;
        TWU.gal_pv = PartnerView();
        TWU.gal_pv.state = p == "towers-pair" ? PARTNER_WAITING : p == "towers-pair-confirm" ? PARTNER_CONFIRM : PARTNER_SIGNED;
        snprintf(TWU.gal_pv.code, sizeof TWU.gal_pv.code, "K7Q-2MX");
        snprintf(TWU.gal_pv.url, sizeof TWU.gal_pv.url, "https://defjam.online/pair");
        snprintf(TWU.gal_pv.name, sizeof TWU.gal_pv.name, "MASA");
        TWU.gal_pv.secs_left = 9 * 60 + 41;
    }
    else if (!p.compare(0, 13, "towers-climb-")) {      // the climb at 0.3, 1.0, 2.2 and 3.5 s
        static const float at[] = { 0.3f, 1.0f, 2.2f, 3.5f };
        int k = std::max(0, std::min(3, atoi(p.c_str() + 13)));
        TWU.gal_window = 1;
        TWU.gal_t = at[k];
        towers_gallery_view(TWU.v, TOWERS_WON, 12);
    } else if (p == "towers-boss") {
        TWU.gal_window = 1;
        TWU.gal_t = 3.5f;
        towers_gallery_view(TWU.v, TOWERS_WON, 58);
        TWU.v.floor = 59;
        TWU.v.next_opponent = 58;       // Crow, the fixed top boss (build 101)
        towers_floor_rules(TOWER_UPTOWN, 59, 59, &TWU.v.next_rules);
    } else if (p == "towers-card" || p == "towers-first") {
        TWU.gal_window = 2;
        towers_gallery_view(TWU.v, TOWERS_ARMED, p == "towers-first" ? 0 : 12);
        if (p == "towers-first") { TWU.v.floor = 1; TWU.v.last_floor = 0; TWU.v.next_opponent = 22; towers_floor_rules(1, 1, 59, &TWU.v.next_rules); }
    } else if (p == "towers-draw") {
        TWU.gal_window = 1;
        TWU.gal_t = 3.5f;
        towers_gallery_view(TWU.v, TOWERS_DRAW, 12);
    } else if (p == "towers-lost" || p == "towers-cleared") {
        TWU.gal_window = 3;
        towers_gallery_view(TWU.v, p == "towers-lost" ? TOWERS_LOST : TOWERS_CLEARED, p == "towers-lost" ? 24 : 59);
        if (p == "towers-lost") { TWU.v.new_best = 1; TWU.v.best = 23; }
        else TWU.v.climb_ms = 1000u * (71 * 60 + 5);
    }
    TWU.run = TWU.gal_runs[0];
    return true;
}

// Draws a gallery window page over the backdrop; true when it did.
bool towers_gallery_window(ImVec2 display)
{
    if (!gallery_on() || !TWU.gal_window) return false;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    Frame f = menu_frame(display);
    Pad none = {};
    if (TWU.gal_window == 1) {
        towers_scene(dl, f, display, TWU.v, TWU.run, TWU.gal_t, false);
        float hx = 54;
        if (TWU.gal_t < 3.5f) hx += hint(dl, f, hx, 448, "a", "SKIP");
        if (TWU.v.state == TOWERS_WON) hint(dl, f, hx, 448, "b", "SAVE AND QUIT");
    } else if (TWU.gal_window == 2) {
        towers_scene(dl, f, display, TWU.v, TWU.run, 3.5f, true);
    } else {
        towers_end_screen(dl, f, display, TWU.v, TWU.run, none);
    }
    return true;
}

const char *towers_shot_label_impl()
{
    if (gallery_on()) return nullptr;
    if (TWU.window) {
        if (TWU.armed && (TWU.v.state == TOWERS_ARMED || GetTickCount64() - TWU.win_t0 >= 3500)) return "towers-card";
        return TWU.v.state == TOWERS_WON || TWU.v.state == TOWERS_DRAW ? "towers-climb" : "towers-end";
    }
    if (GetTickCount64() - TWU.card_seen < 200) return "towers-card";
    if (S.page == TOWERS) return TWU.board ? "towers-board" : TWU.card == 5 ? "towers-pair" : TWU.card == 2 ? "towers-start" :
                                 TWU.card == 1 ? "towers-saved" : "towers";
    return nullptr;
}
