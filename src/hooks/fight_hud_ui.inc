// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The fight overlays (build 76), drawn over the game with the online screens closed; included by
// online_screen.cpp inside its namespace. Drawing only: no input is read, nothing goes back to the game.
//
//   PERFECT   a round won without losing any health, offline against the CPU and online: a banner in the
//             game's stencil lettering for a few seconds from the decision ([online] perfect_banner).
//             fight_hud.c tracks the round; the banner waits a moment, so a decision that rollback takes
//             back never shows.
//   Name tags in an online fight each player's online name on the line over the fighter's name on the
//             HUD, this PC's own in orange ([online] name_tags). Offline nothing is drawn. The places
//             are fight_hud_rules.c's (2 to 4 fighters, 4:3, stretched, widescreen at the edges or centred).
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
    NetVersus match{};
    bool match_live = false;
    std::vector<std::string> test_names;
    int test_read = 0;
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

void fh_banner(ImDrawList *dl, const Frame &f, unsigned long long age, const char *who)
{
    // slammed in (large to its size in 160 ms), held, faded out
    float in = age < 160 ? (float)age / 160.0f : 1.0f;
    float out = age + FH_BANNER_FADE_MS > FH_BANNER_MS ? (float)(FH_BANNER_MS - age) / (float)FH_BANNER_FADE_MS : 1.0f;
    float alpha = std::max(0.0f, std::min(1.0f, in * 1.4f)) * std::max(0.0f, out);
    float size = 78.0f * (1.0f + (1.0f - in) * (1.0f - in) * 1.3f);
    float glow = 0.75f + 0.25f * sinf((float)(age % 700) / 700.0f * 6.2832f);
    const float cy = 142;      // over the game's own KO lettering, which sits in the middle
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
    return FH.banner || FH.tags;
}

void draw_fight_hud(ImDrawList *dl, ImVec2 display)
{
    if (FH.tags) fh_tags(dl, display);
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
