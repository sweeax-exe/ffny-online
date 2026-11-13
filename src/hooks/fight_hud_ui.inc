// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The fight overlays (build 76), drawn over the game with the online screens closed; included by
// online_screen.cpp inside its namespace. Drawing only: no input is read, nothing goes back to the game.
//
//   PERFECT   a round won without losing any health, offline against the CPU and online: a banner in the
//             game's stencil lettering for a few seconds from the decision ([online] perfect_banner).
//
//   The look (build 81): the game's own fight call-outs, white distressed letters on a dark worn plate.
//             FIGHT! is the game's own lettering (ui/hud_fight.png: the fight HUD's call-out texture in
//             assets/misc.viv); the numbers and PERFECT are the game's TeaChest stencil in the same off-white,
//             worn with ui/hud_grain.png (the call-outs' specks and scratches, scripts/online-art.py). Without
//             that art (online art older than 7) the gold-on-red look of builds 76..80 stays.
//             fight_hud.c tracks the round; the banner waits a moment, so a decision that rollback takes
//             back never shows.
//   Name tags in an online fight each player's online name on the line over the fighter's name on the
//             HUD, this PC's own in orange ([online] name_tags). Offline nothing is drawn. The places
//             are fight_hud_rules.c's (2 to 4 fighters, 4:3, stretched, widescreen at the edges or centred).
//
//   Countdown "3 - 2 - 1 - FIGHT!" over the first 180 steps of every fight (build 77), while every fighter's
//             input is held at rest (start_countdown.h; netplay.c applies the hold), then FIGHT! for a
//             second. The game's own grey FIGHT! lettering, up for the first second and a half of the
//             steps, sits under the numbers' dark plate. Drawn from the round's step, so it shows what
//             the simulation does on every PC, and it stands still while the steps do (a pause, a wait
//             for another player).
//
//   Big Arena (open-world phase 1): the score plate at the top (first to N, each fighter's knockouts, the leader
//             crowned, this PC's own in orange, the time left), "BACK IN 3" while this PC's fighter is down, and
//             the winner at the end. Read from openworld.c's state of the last step (ow_hud_view).
//
// Tests: RECOMP_TEST_NAME_TAGS=A,B,C,D names the fighters of an offline fight (the tags' pictures
// without four copies of the game); RECOMP_TEST_PERFECT=1 is fight_hud.c's.

namespace {

const unsigned long long FH_BANNER_WAIT_MS = 350, FH_BANNER_MS = 3200, FH_BANNER_FADE_MS = 500, FH_HUD_STALE_MS = 300;

struct FightHudUi {
    FightHudView view{};
    // the fight's steps, by this thread's clock
    uint32_t beat = 0;
    unsigned long long beat_at = 0;
    // the banner
    uint32_t round = 0, mask = 0, shown_round = 0;
    unsigned long long mask_at = 0;
    bool banner = false, tags = false;
    // the start countdown: 3, 2, 1, 0 for FIGHT!, -1 none; how far into it (0..1)
    int count = -1;
    float count_t = 0;
    NetVersus match{};
    bool match_live = false;
    std::vector<std::string> test_names;
    int test_read = 0;
    // Big Arena (openworld.c): the scores, a respawn count, the winner
    OwHudView arena{};
    bool arena_on = false;
} FH;

bool fh_setting(const char *key) { return recomp_settings_get("online", key, 1) != 0; }

void fh_test_names()
{
    if (FH.test_read) return;
    FH.test_read = 1;
    const char *v = getenv("RECOMP_TEST_NAME_TAGS");
    if (!v || !*v) return;
    std::string one;
    for (const char *p = v;; p++) {
        if (*p == ',' || !*p) { FH.test_names.push_back(one); one.clear(); if (!*p) break; }
        else one += *p;
    }
    fprintf(stderr, "[FIGHTHUD] RECOMP_TEST_NAME_TAGS (tests): %d names for the fighters of any fight\n", (int)FH.test_names.size());
}

// The game's look, when the player's online art has it (version 7): its FIGHT! lettering, the wear tile
// and the stencil the numbers and PERFECT are set in (TeaChest; the page titles' stencil without it).
struct HudArt {
    const Tex *fight = nullptr, *grain = nullptr;
    ImFont *font = nullptr;
};
const ImU32 WORN_WHITE = IM_COL32(236, 234, 228, 255), WORN_RULE = IM_COL32(150, 146, 138, 255),
            WORN_GRIT = IM_COL32(120, 118, 112, 255);

bool fh_game_look(HudArt &a)
{
    const Tex &fight = tex("ui/hud_fight.png"), &grain = tex("ui/hud_grain.png");
    if (!fight.srv || !grain.srv || fight.w <= 0 || fight.h <= 0) return false;
    a.fight = &fight;
    a.grain = &grain;
    a.font = g_worn ? g_worn : g_stencil;
    static bool told;
    if (!told) {
        told = true;
        fprintf(stderr, "[FIGHTHUD] the game's look: FIGHT! %dx%d, wear tile, %s\n", (int)fight.w, (int)fight.h,
                g_worn ? "TeaChest" : "the page titles' stencil");
    }
    return true;
}

// The wear tile over a box of the screen (tiled from `origin`, the box's corner by default, clipped to the
// box): tinted black over white lettering it is the call-outs' specks; over the dark plate a black tint
// does not show.
void fh_grain(ImDrawList *dl, const Frame &f, const Tex &g, ImVec2 a, ImVec2 b, ImU32 tint, float texel = 1.3f,
              const ImVec2 *origin = nullptr)
{
    float step = 256.0f * texel * f.s();
    if (step < 1.0f || b.x <= a.x || b.y <= a.y) return;
    ImVec2 o = origin ? *origin : a;
    float x0 = o.x + floorf((a.x - o.x) / step) * step, y0 = o.y + floorf((a.y - o.y) / step) * step;
    dl->PushClipRect(a, b, true);
    for (float y = y0; y < b.y; y += step)
        for (float x = x0; x < b.x; x += step)
            dl->AddImage((ImTextureID)(intptr_t)g.srv, ImVec2(x, y), ImVec2(x + step, y + step), ImVec2(0, 0), ImVec2(1, 1), tint);
    dl->PopClipRect();
}

// Worn lettering, centred on cx with its top at y: a dark drop shadow, the off-white letters, the wear.
void fh_worn_text(ImDrawList *dl, const Frame &f, const HudArt &a, float size, float cx, float y, float alpha, const char *s)
{
    ImFont *font = font_or_default(a.font);
    float k = size / 100.0f;
    text(dl, f, font, size, cx + 3.0f * k, y + 4.0f * k, with_alpha(BLACK, alpha * 0.85f), s, CENTER, 0);
    text(dl, f, font, size, cx, y, with_alpha(WORN_WHITE, alpha), s, CENTER, 0);
    ImVec2 sz = font->CalcTextSizeA(size * f.s(), FLT_MAX, 0, s), at = f.p(cx, y);
    at.x -= sz.x / 2;
    fh_grain(dl, f, *a.grain, at, ImVec2(at.x + sz.x, at.y + sz.y), with_alpha(BLACK, alpha * 0.95f));
}

// The game's FIGHT! lettering, `h` high, centred on (cx, cy), with the same drop shadow.
void fh_fight_art(ImDrawList *dl, const Frame &f, const HudArt &a, float h, float cx, float cy, float alpha)
{
    float w = h * a.fight->w / a.fight->h, k = h / 100.0f;
    image(dl, f, *a.fight, cx - w / 2 + 3.0f * k, cy - h / 2 + 4.0f * k, w, h, with_alpha(BLACK, alpha * 0.85f));
    image(dl, f, *a.fight, cx - w / 2, cy - h / 2, w, h, with_alpha(IM_COL32_WHITE, alpha));
}

void fh_band(ImDrawList *dl, const Frame &f, float cy, float alpha, float dense, const HudArt *worn = nullptr,
             float solid = 160);

void fh_banner(ImDrawList *dl, const Frame &f, unsigned long long age, const char *who)
{
    // slammed in (large to its size in 160 ms), held, faded out
    float in = age < 160 ? (float)age / 160.0f : 1.0f;
    float out = age + FH_BANNER_FADE_MS > FH_BANNER_MS ? (float)(FH_BANNER_MS - age) / (float)FH_BANNER_FADE_MS : 1.0f;
    float alpha = std::max(0.0f, std::min(1.0f, in * 1.4f)) * std::max(0.0f, out);
    float size = 78.0f * (1.0f + (1.0f - in) * (1.0f - in) * 1.3f);
    float glow = 0.75f + 0.25f * sinf((float)(age % 700) / 700.0f * 6.2832f);
    const float cy = 142;      // over the game's own KO lettering, which sits in the middle
    HudArt art;
    if (fh_game_look(art)) {
        // the call-outs' look: a dark worn plate, PERFECT in the worn stencil (a size up: it is narrower),
        // the winner under it
        size *= 1.2f;
        fh_band(dl, f, cy, std::max(0.0f, std::min(1.0f, in * 1.4f)) * std::max(0.0f, out), 0.95f, &art, 200);
        float ty = cy - size * 0.5f - (who && *who ? 8 : 0);
        fh_worn_text(dl, f, art, size, 320, ty, alpha, "PERFECT");
        if (who && *who && in >= 1.0f)
            text(dl, f, g_title, 15, 320, cy + 30, with_alpha(WORN_WHITE, alpha), who, CENTER, 1.2f);
        return;
    }
    // a dark band across the picture, the lettering over it, a gold rule above and below
    float bh = 96, half = 330;
    ImVec2 a = f.p(320 - half, cy - bh / 2), b = f.p(320 + half, cy + bh / 2), mid = f.p(320, cy);
    ImU32 dark = with_alpha(IM_COL32(6, 6, 8, 255), 0.62f * alpha), none = with_alpha(IM_COL32(6, 6, 8, 255), 0);
    dl->AddRectFilledMultiColor(a, ImVec2(mid.x, b.y), none, dark, dark, none);
    dl->AddRectFilledMultiColor(ImVec2(mid.x, a.y), b, dark, none, none, dark);
    ImU32 gold = with_alpha(GOLD, alpha * glow), gold0 = with_alpha(GOLD, 0);
    for (int k = 0; k < 2; k++) {
        float y = k ? b.y : a.y - 2 * f.s();
        dl->AddRectFilledMultiColor(ImVec2(a.x, y), ImVec2(mid.x, y + 2 * f.s()), gold0, gold, gold, gold0);
        dl->AddRectFilledMultiColor(ImVec2(mid.x, y), ImVec2(b.x, y + 2 * f.s()), gold, gold0, gold0, gold);
    }
    float ty = cy - size * 0.5f - (who && *who ? 8 : 0);
    text(dl, f, g_stencil, size, 320 + 2.5f, ty + 3.0f, with_alpha(RED, alpha * 0.9f), "PERFECT", CENTER, 0);
    text(dl, f, g_stencil, size, 320, ty, with_alpha(GOLD, alpha), "PERFECT", CENTER, 2.4f);
    if (who && *who && in >= 1.0f)
        text(dl, f, g_title, 15, 320, cy + 26, with_alpha(CREAM, alpha), who, CENTER, 1.2f);
}

// A dark band across the picture with gold rules (the PERFECT banner's look), opaque in the middle:
// it covers the game's own grey FIGHT! lettering, which is up under the first numbers. With the game's
// look (`worn`) the rules are worn grey and the plate gets the call-outs' grit.
void fh_band(ImDrawList *dl, const Frame &f, float cy, float alpha, float dense, const HudArt *worn, float solid)
{
    const float bh = 104, half = 330;
    ImU32 dark = with_alpha(IM_COL32(6, 6, 8, 255), dense * alpha), none = with_alpha(IM_COL32(6, 6, 8, 255), 0);
    ImVec2 a = f.p(320 - half, cy - bh / 2), b = f.p(320 + half, cy + bh / 2);
    ImVec2 sa = f.p(320 - solid, cy - bh / 2), sb = f.p(320 + solid, cy + bh / 2);
    dl->AddRectFilledMultiColor(a, ImVec2(sa.x, b.y), none, dark, dark, none);
    dl->AddRectFilled(sa, sb, dark);
    dl->AddRectFilledMultiColor(ImVec2(sb.x, a.y), b, dark, none, none, dark);
    if (worn) {
        // the grit, fading out with the plate towards its ends
        float grit = alpha * dense * 0.22f;
        fh_grain(dl, f, *worn->grain, sa, sb, with_alpha(WORN_GRIT, grit), 1.0f, &a);
        const int strips = 8;
        for (int i = 0; i < strips; i++) {
            float x0 = (sa.x - a.x) * i / strips, x1 = (sa.x - a.x) * (i + 1) / strips;
            ImU32 c = with_alpha(WORN_GRIT, grit * (i + 0.5f) / strips);
            fh_grain(dl, f, *worn->grain, ImVec2(a.x + x0, a.y), ImVec2(a.x + x1, b.y), c, 1.0f, &a);
            fh_grain(dl, f, *worn->grain, ImVec2(b.x - x1, a.y), ImVec2(b.x - x0, b.y), c, 1.0f, &a);
        }
    }
    ImU32 rule = worn ? WORN_RULE : GOLD;
    ImU32 gold = with_alpha(rule, alpha * (worn ? 0.6f : 1.0f)), gold0 = with_alpha(rule, 0);
    for (int k = 0; k < 2; k++) {
        float y = k ? b.y : a.y - 2 * f.s();
        dl->AddRectFilledMultiColor(ImVec2(a.x, y), ImVec2(sa.x, y + 2 * f.s()), gold0, gold, gold, gold0);
        dl->AddRectFilled(ImVec2(sa.x, y), ImVec2(sb.x, y + 2 * f.s()), gold);
        dl->AddRectFilledMultiColor(ImVec2(sb.x, y), ImVec2(b.x, y + 2 * f.s()), gold, gold0, gold0, gold);
    }
}

// The start countdown: each number slams in (large to its size), holds and fades as the next comes, on
// the band; FIGHT! slams in the same way, then the band and it fade out.
void fh_countdown(ImDrawList *dl, const Frame &f, int n, float t)
{
    const float cy = 228;      // where the game's own FIGHT! lettering stands
    float in = t < 0.16f ? t / 0.16f : 1.0f;
    float pop = (1.0f - in) * (1.0f - in);
    HudArt art;
    if (fh_game_look(art)) {
        // the game's look: the same slams and fades, worn off-white on the worn plate, FIGHT! the game's own
        if (n > 0) {
            fh_band(dl, f, cy, n == 3 ? std::min(1.0f, t * 30.0f) : 1.0f, 0.97f, &art);
            float out = t > 0.82f ? (1.0f - t) / 0.18f : 1.0f;
            float alpha = std::max(0.0f, std::min(1.0f, in * 1.6f)) * std::max(0.0f, out);
            float size = 136.0f * (1.0f + pop * 1.4f) * (0.92f + 0.08f * out);
            char numeral[2] = { (char)('0' + n), 0 };
            fh_worn_text(dl, f, art, size, 320, cy - size * 0.5f, alpha, numeral);
            return;
        }
        float out = t > 0.7f ? (1.0f - t) / 0.3f : 1.0f;
        float alpha = std::max(0.0f, std::min(1.0f, in * 1.4f)) * std::max(0.0f, out);
        float h = 84.0f * (1.0f + pop * 1.3f) * (1.0f + (1.0f - out) * 0.15f);
        fh_band(dl, f, cy, std::max(0.0f, out), 0.7f, &art);
        fh_fight_art(dl, f, art, h, 320, cy, alpha);
        return;
    }
    if (n > 0) {
        // the band stays through the numbers (up within the first steps of the 3)
        fh_band(dl, f, cy, n == 3 ? std::min(1.0f, t * 30.0f) : 1.0f, 0.97f);
        float out = t > 0.82f ? (1.0f - t) / 0.18f : 1.0f;
        float alpha = std::max(0.0f, std::min(1.0f, in * 1.6f)) * std::max(0.0f, out);
        float size = 128.0f * (1.0f + pop * 1.4f) * (0.92f + 0.08f * out);
        char digit[2] = { (char)('0' + n), 0 };
        float ty = cy - size * 0.5f;
        text(dl, f, g_stencil, size, 320 + 4.0f, ty + 5.0f, with_alpha(RED, alpha * 0.9f), digit, CENTER, 0);
        text(dl, f, g_stencil, size, 320, ty, with_alpha(GOLD, alpha), digit, CENTER, 3.0f);
        return;
    }
    float out = t > 0.7f ? (1.0f - t) / 0.3f : 1.0f;
    float alpha = std::max(0.0f, std::min(1.0f, in * 1.4f)) * std::max(0.0f, out);
    float size = 112.0f * (1.0f + pop * 1.3f) * (1.0f + (1.0f - out) * 0.15f);
    fh_band(dl, f, cy, std::max(0.0f, out), 0.7f);
    float ty = cy - size * 0.5f;
    text(dl, f, g_stencil, size, 320 + 3.0f, ty + 4.0f, with_alpha(RED, alpha * 0.9f), "FIGHT!", CENTER, 0);
    text(dl, f, g_stencil, size, 320, ty, with_alpha(GOLD, alpha), "FIGHT!", CENTER, 2.8f);
}

void fh_tags(ImDrawList *dl, ImVec2 display)
{
    FightPicture pic;
    pic.w = display.x;
    pic.h = display.y;
    pic.wide = ws_aspect();
    pic.stretch = pic.wide <= 0.0f && pc_display("aspect", 0) == 1;
    pic.hud_centred = ws_hud_mode() == WS_HUD_CENTERED;
    int fighters = 0;
    for (int i = 0; i < FIGHT_HUD_SLOTS; i++) fighters += (FH.view.active >> i) & 1;
    if (fighters < 2) fighters = FH.match_live ? FH.match.count : 2;
    ImFont *font = font_or_default(g_title);
    for (int slot = 0; slot < FIGHT_HUD_SLOTS; slot++) {
        const char *name = nullptr;
        if (slot < (int)FH.test_names.size()) name = FH.test_names[slot].c_str();
        else if (FH.match_live && slot < FH.match.count && !FH.match.cpu[slot]) name = FH.match.name[slot];
        if (!name || !*name || !(FH.view.active & (1u << slot))) continue;
        FightTag t;
        char shown[64];
        if (!fight_tag_place(&pic, fighters, slot, &t) || !fight_tag_text(name, 24, shown, sizeof(shown))) continue;
        float px = t.size;
        ImVec2 sz = font->CalcTextSizeA(px, FLT_MAX, 0, shown);
        if (sz.x > t.width && sz.x > 0) { px *= t.width / sz.x; sz = font->CalcTextSizeA(px, FLT_MAX, 0, shown); }
        ImVec2 at(t.right ? t.x - sz.x : t.x, t.y + (t.size - px));
        bool me = FH.test_names.empty() ? (FH.match_live && slot == FH.match.you) : slot == 0;
        float o = std::max(1.0f, px * 0.1f);
        static const float d[8][2] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
        for (auto &k : d) dl->AddText(font, px, ImVec2(at.x + k[0] * o, at.y + k[1] * o), IM_COL32(0, 0, 0, 230), shown);
        dl->AddText(font, px, at, me ? ORANGE : CREAM, shown);
    }
}

}   // namespace

// Once a frame, with the overlay closed and no versus screen up: true when a banner or tags are to be drawn.
bool fight_hud_wanted()
{
    unsigned long long now = GetTickCount64();
    FH.banner = FH.tags = false;
    if (gallery_on()) return false;
    fh_test_names();
    fight_hud_view(&FH.view);
    if (FH.view.beat != FH.beat) { FH.beat = FH.view.beat; FH.beat_at = now; }
    // the game's HUD is up while the round is fought and its steps run (not in a special move's camera, not
    // once the round is decided, not in a paused fight)
    bool hud_up = FH.view.live && FH.beat_at && now - FH.beat_at < FH_HUD_STALE_MS;
    // the start countdown, while the round's first steps run (the phase word may still say "start" at step 1)
    bool steps_run = FH.beat_at && now - FH.beat_at < FH_HUD_STALE_MS;
    FH.count = steps_run ? start_countdown_shown(netplay_countdown_active(), FH.view.step, &FH.count_t) : -1;    if (FH.count >= 0) {
        static uint32_t logged_round;
        if (logged_round != FH.view.round) {
            logged_round = FH.view.round;
            fprintf(stderr, "[FIGHTHUD] start countdown up (round %u, step %u)\n", (unsigned)FH.view.round,
                    (unsigned)FH.view.step);
        }
    }
    // the banner: from a moment after the round is a perfect, once per round
    if (FH.view.round != FH.round || FH.view.perfect != FH.mask) {
        FH.round = FH.view.round;
        FH.mask = FH.view.perfect;
        FH.mask_at = FH.mask ? now : 0;
    }
    if (FH.mask && FH.mask_at && now >= FH.mask_at + FH_BANNER_WAIT_MS && now < FH.mask_at + FH_BANNER_WAIT_MS + FH_BANNER_MS &&
        fh_setting("perfect_banner")) {
        if (FH.shown_round != FH.round) {
            FH.shown_round = FH.round;
            fprintf(stderr, "[FIGHTHUD] PERFECT banner up (round %u, winners 0x%X)\n", (unsigned)FH.round, (unsigned)FH.mask);
        }
        FH.banner = true;
    }
    FH.match_live = false;
    if ((hud_up || FH.banner) && (FH.banner || fh_setting("name_tags"))) {
        FH.match_live = netplay_match_info(&FH.match) != 0 && FH.match.loaded;
        FH.tags = hud_up && (FH.match_live || !FH.test_names.empty()) && fh_setting("name_tags");
        if (FH.tags) {
            static uint32_t logged_round;
            if (logged_round != FH.view.round) {
                logged_round = FH.view.round;
                std::string all;
                for (int i = 0; FH.match_live && i < FH.match.count; i++)
                    all += std::string(i ? ", " : "") + (FH.match.cpu[i] ? "(CPU)" : FH.match.name[i]);
                fprintf(stderr, "[FIGHTHUD] name tags up (round %u): %s\n", (unsigned)FH.view.round,
                        FH.match_live ? all.c_str() : "test names");
            }
        }
    }
    FH.arena_on = (hud_up || steps_run) && ow_hud_view(&FH.arena) != 0;
    if (FH.arena_on) {
        static uint32_t logged_round;
        if (logged_round != FH.view.round) {
            logged_round = FH.view.round;
            fprintf(stderr, "[FIGHTHUD] Big Arena score plate up (round %u, first to %d)\n", (unsigned)FH.view.round,
                    FH.arena.score_limit);
        }
    }
    return FH.banner || FH.tags || FH.count >= 0 || FH.arena_on;
}

std::string fh_arena_name(int slot)
{
    if (FH.match_live && slot < FH.match.count && !FH.match.cpu[slot] && FH.match.name[slot][0])
        return upper(FH.match.name[slot]);
    if (slot < (int)FH.test_names.size()) return upper(FH.test_names[slot].c_str());
    char b[8];
    snprintf(b, sizeof b, "P%d", slot + 1);
    return b;
}

void fh_arena(ImDrawList *dl, ImVec2 display)
{
    const OwHudView &a = FH.arena;
    Frame f = menu_frame(display);
    int n = 0;
    for (int s = 0; s < 4; s++) n += (a.present >> s) & 1;
    if (n < 1) return;
    // the plate: "FIRST TO 5" over a chip per fighter, the time left at the right
    const float chip = 58, top = 6;
    float w = n * chip + 16 + (a.seconds_left >= 0 ? 44 : 0), x0 = 320 - w / 2;
    ImU32 dark = IM_COL32(12, 10, 8, 200);
    dl->AddRectFilled(f.p(x0, top), f.p(x0 + w, top + 38), dark, 4 * f.s());
    dl->AddRectFilled(f.p(x0, top + 36), f.p(x0 + w, top + 38), with_alpha(GOLD, 0.85f));
    char line[48];
    snprintf(line, sizeof line, "BIG ARENA  -  FIRST TO %d", a.score_limit);
    text(dl, f, g_body, 8.5f, 320, top + 2, with_alpha(CREAM, 0.8f), line, CENTER, 0);
    float x = x0 + 8;
    for (int s = 0; s < 4; s++) {
        if (!((a.present >> s) & 1)) continue;
        bool me = s == a.local, lead = s == a.leader, down = (a.down >> s) & 1;
        ImU32 col = down ? GREY : me ? ORANGE : CREAM;
        std::string name = fh_arena_name(s);
        if (name.size() > 7) name.resize(7);
        text(dl, f, g_title, 9.5f, x + chip / 2, top + 12, col, name.c_str(), CENTER, 1.0f);
        char num[8];
        snprintf(num, sizeof num, "%d", a.score[s]);
        text(dl, f, g_stencil, 14, x + chip / 2, top + 21, lead ? GOLD : col, num, CENTER, 1.2f);
        if (lead) crown(dl, f, x + chip / 2 + 13, top + 26, 9);
        x += chip;
    }
    if (a.seconds_left >= 0) {
        snprintf(line, sizeof line, "%d:%02d", a.seconds_left / 60, a.seconds_left % 60);
        text(dl, f, g_title, 12, x + 20, top + 17, a.seconds_left <= 30 ? RED : CREAM, line, CENTER, 1.0f);
    }
    // this PC's fighter is down: the count back
    int me = a.local;
    if (!a.ending && ((a.down >> me) & 1)) {
        int secs = (a.back_in[me] + 59) / 60;
        fh_band(dl, f, 228, 0.85f, 0.7f);
        text(dl, f, g_body, 13, 320, 196, CREAM, "KNOCKED OUT", CENTER, 1.2f);
        snprintf(line, sizeof line, secs > 0 ? "BACK IN %d" : "GET UP", secs);
        text(dl, f, g_stencil, 40, 320, 210, GOLD, line, CENTER, 2.0f);
    }
    // the end: the winner
    if (a.ending && a.winner >= 0) {
        std::string who = a.winner == me ? std::string("YOU WIN") : fh_arena_name(a.winner) + " WINS";
        fh_band(dl, f, 228, 0.9f, 0.8f);
        text(dl, f, g_stencil, 46, 320 + 3, 206, with_alpha(RED, 0.9f), who.c_str(), CENTER, 0);
        text(dl, f, g_stencil, 46, 320, 203, GOLD, who.c_str(), CENTER, 2.4f);
    }
}

void draw_fight_hud(ImDrawList *dl, ImVec2 display)
{
    if (FH.arena_on) fh_arena(dl, display);
    if (FH.tags) fh_tags(dl, display);
    if (FH.count >= 0) fh_countdown(dl, menu_frame(display), FH.count, FH.count_t);
    if (FH.banner) {
        // online: whose perfect it is (the winner's name; a Team side's two)
        std::string who;
        for (int i = 0; FH.match_live && i < FH.match.count && i < FIGHT_HUD_SLOTS; i++)
            if ((FH.mask & (1u << i)) && !FH.match.cpu[i] && FH.match.name[i][0]) who += (who.empty() ? "" : "  &  ") + upper(FH.match.name[i]);
        for (int i = 0; !FH.match_live && i < (int)FH.test_names.size() && i < FIGHT_HUD_SLOTS; i++)
            if (FH.mask & (1u << i)) who += (who.empty() ? "" : "  &  ") + upper(FH.test_names[i].c_str());
        fh_banner(dl, menu_frame(display), GetTickCount64() - (FH.mask_at + FH_BANNER_WAIT_MS), who.c_str());
    }
}
