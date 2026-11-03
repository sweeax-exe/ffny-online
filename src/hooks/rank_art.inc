// The rank emblems and the rewards' looks (build 52; the owner's approved designs "Ranks", "Rank up" and
// "Season rewards", 2026-11-03). Included by online_screen.cpp inside its anonymous namespace, before
// ranked_ui.inc.
//
// The emblems are the port's own art: scripts/rank-emblems.py renders scripts/rank_emblem_art.py (SVG) to
// <data>/online-art/ranks/em<N>-256.png and em<N>-64.png at setup. The division chevrons (one for III, two
// for II, three for I) and the Legend plate (the rating, or "#3 NY" for the Top 10) are drawn here, over the
// texture, in the 256 box the design uses: the art in y 8..218, the chevrons in y 224..254. Without the
// textures (no browser at setup) a simple disc in the tier's colour stands in.

// The tiers' colours (the design's: Street Rookie green .. Def Jam Legend neon red).
const ImU32 k_tier_col[8] = { IM_COL32(95, 219, 143, 255), IM_COL32(141, 184, 232, 255), IM_COL32(240, 88, 74, 255),
                              IM_COL32(227, 154, 92, 255), IM_COL32(212, 226, 244, 255), IM_COL32(255, 197, 61, 255),
                              IM_COL32(143, 235, 255, 255), IM_COL32(255, 77, 94, 255) };
const char *const k_tier_short[8] = { "ROOKIE", "HUSTLER", "BRAWLER", "CONTENDER", "ENFORCER", "KINGPIN", "DON", "LEGEND" };

ImU32 tier_col(int tier) { return k_tier_col[tier < 0 ? 0 : tier > 7 ? 7 : tier]; }
// The chevrons a division lights: III 1, II 2, I 3 (Legend: none, the plate instead).
int division_lit(int division) { return division >= NETRANK_DIVISIONS - 1 ? 0 : division % 3 + 1; }

const Tex &emblem_tex(int tier, bool big)
{
    static char rel[32];
    snprintf(rel, sizeof(rel), "ranks/em%d-%d.png", (tier < 0 ? 0 : tier > 7 ? 7 : tier) + 1, big ? 256 : 64);
    return tex(rel);
}

float pulse01(float period_s, float phase = 0)
{
    if (gallery_on()) return 0.75f;
    return 0.5f + 0.5f * sinf(((float)(GetTickCount64() % 100000) / 1000.0f / period_s + phase) * 6.2831853f);
}

// A stand-in for the texture: a disc in the tier's colour with its first letter.
void emblem_fallback(ImDrawList *dl, const Frame &f, float cx, float cy, float r, int tier, float alpha)
{
    ImU32 col = tier_col(tier);
    dl->AddCircleFilled(f.p(cx, cy), r * f.s(), with_alpha(IM_COL32(16, 14, 18, 255), alpha), 32);
    dl->AddCircle(f.p(cx, cy), r * 0.92f * f.s(), with_alpha(col, alpha), 32, std::max(1.0f, r * 0.14f) * f.s());
    char letter[2] = { k_tier_short[tier < 0 ? 0 : tier > 7 ? 7 : tier][0], 0 };
    text(dl, f, g_stencil, r * 1.1f, cx, cy - r * 0.62f, with_alpha(col, alpha), letter, CENTER, 0.8f);
}

// The chevron row in a box of `size` (the 256 box scaled) whose top left is x, y. lit 0..3; `fresh` makes
// the newest lit one pulse (a promotion), `lost` shows the one above the lit ones fading (a demotion).
void emblem_chevrons(ImDrawList *dl, const Frame &f, float x, float y, float size, int tier, int lit, bool fresh, bool lost,
                     float alpha)
{
    float k = size / 256.0f;
    ImU32 col = tier_col(tier);
    for (int i = 0; i < 3; i++) {
        float cx = 128 + (i - 1) * 40.0f;
        ImVec2 pts[6] = { f.p(x + (cx - 18) * k, y + 242 * k), f.p(x + cx * k, y + 224 * k), f.p(x + (cx + 18) * k, y + 242 * k),
                          f.p(x + (cx + 18) * k, y + 254 * k), f.p(x + cx * k, y + 236 * k), f.p(x + (cx - 18) * k, y + 254 * k) };
        bool on = i < lit;
        float a = alpha;
        if (fresh && i == lit - 1) {       // the new division: a glow that breathes
            float p = pulse01(1.1f);
            for (int g = 3; g >= 1; g--) {
                ImVec2 c = f.p(x + cx * k, y + 239 * k);
                dl->AddCircleFilled(c, (14 + g * 7) * k * f.s(), with_alpha(col, alpha * (0.10f + 0.10f * p) / g), 24);
            }
        }
        if (lost && i == lit) a = alpha * (0.35f + 0.4f * pulse01(1.4f));
        if (on) {
            ImVec2 q1[4] = { pts[0], pts[1], pts[4], pts[5] }, q2[4] = { pts[1], pts[2], pts[3], pts[4] };
            dl->AddConvexPolyFilled(q1, 4, with_alpha(col, a));
            dl->AddConvexPolyFilled(q2, 4, with_alpha(col, a));
            dl->AddPolyline(pts, 6, with_alpha(IM_COL32(6, 5, 7, 255), a), ImDrawFlags_Closed, std::max(1.0f, 3 * k) * f.s());
            ImVec2 shine[3] = { f.p(x + (cx - 15) * k, y + 241 * k), f.p(x + cx * k, y + 226 * k), f.p(x + (cx + 15) * k, y + 241 * k) };
            dl->AddPolyline(shine, 3, with_alpha(IM_COL32(255, 255, 255, 190), a), 0, std::max(0.7f, 2 * k) * f.s());
        } else {
            ImU32 dark = lost && i == lit ? with_alpha(col, a * 0.6f) : with_alpha(IM_COL32(24, 22, 28, 255), a);
            ImVec2 q1[4] = { pts[0], pts[1], pts[4], pts[5] }, q2[4] = { pts[1], pts[2], pts[3], pts[4] };
            dl->AddConvexPolyFilled(q1, 4, dark);
            dl->AddConvexPolyFilled(q2, 4, dark);
            dl->AddPolyline(pts, 6, with_alpha(IM_COL32(255, 255, 255, 72), a), ImDrawFlags_Closed, std::max(0.7f, 2 * k) * f.s());
        }
    }
}

// The Legend plate under the emblem: the rating, or the Top 10 place ("#3 NY").
void legend_plate(ImDrawList *dl, const Frame &f, float x, float y, float size, const char *label, float alpha)
{
    float k = size / 256.0f;
    ImVec2 pts[4] = { f.p(x + 70 * k, y + 228 * k), f.p(x + 186 * k, y + 228 * k), f.p(x + 176 * k, y + 254 * k),
                      f.p(x + 80 * k, y + 254 * k) };
    dl->AddConvexPolyFilled(pts, 4, with_alpha(IM_COL32(18, 6, 10, 255), alpha));
    float p = pulse01(1.6f);
    ImU32 edge = IM_COL32(255, (int)(59 + 120 * p), (int)(84 - 30 * p), 255);
    dl->AddPolyline(pts, 4, with_alpha(edge, alpha), ImDrawFlags_Closed, std::max(1.0f, 2.5f * k) * f.s());
    float ts = fit_size(f, g_title, 20 * k, label, 96 * k);
    text(dl, f, g_title, ts, x + 128 * k, y + (241 - ts / k * 0.5f) * k - 1 * k, with_alpha(IM_COL32(255, 233, 176, 255), alpha),
         label, CENTER, 0.6f);
}

// The plate's text for a Legend: "#3 NY" in the Top 10, else the rating.
std::string legend_label(int top, int rating)
{
    char b[16];
    if (top > 0) snprintf(b, sizeof(b), "#%d NY", top);
    else snprintf(b, sizeof(b), "%d", rating);
    return b;
}

struct EmblemOpts {
    bool chevrons = true;   // the division row (or the Legend plate) under the art
    bool fresh = false;     // the newest chevron pulses (a promotion)
    bool lost = false;      // a demotion: the lost chevron fades
    bool glow = false;      // the tier's light behind (Kingpin and up glow in the design)
    int top = 0, rating = 0;
    float alpha = 1.0f;
};

// A big emblem (results, stats, the mode cards, the versus screen): `size` menu units for the 256 box,
// top left at x, y.
void emblem_big(ImDrawList *dl, const Frame &f, float x, float y, float size, int tier, int division, const EmblemOpts &o = EmblemOpts())
{
    float k = size / 256.0f;
    float cx = x + 128 * k, cy = y + 113 * k;
    if (o.glow || tier >= 5) {
        float p = tier == 7 ? pulse01(1.6f) : 0.6f;
        ImU32 col = tier_col(tier);
        for (int g = 5; g >= 1; g--)
            dl->AddCircleFilled(f.p(cx, cy), (60 + g * 16) * k * f.s(), with_alpha(col, o.alpha * (0.035f + 0.02f * p)), 40);
    }
    const Tex &t = emblem_tex(tier, size * f.s() > 96);
    if (t.srv) image(dl, f, t, x, y, size, size, with_alpha(IM_COL32_WHITE, o.alpha));
    else emblem_fallback(dl, f, cx, cy, 92 * k, tier, o.alpha);
    if (!o.chevrons) return;
    if (tier >= 7) {
        std::string l = legend_label(o.top, o.rating);
        legend_plate(dl, f, x, y, size, l.c_str(), o.alpha);
    } else {
        emblem_chevrons(dl, f, x, y, size, tier, division_lit(division), o.fresh, o.lost, o.alpha);
    }
}

// A small emblem (48 on cards, 24 beside a name): the 64 px texture, no chevrons; centred at cx, cy.
void emblem_small(ImDrawList *dl, const Frame &f, float cx, float cy, float size, int tier, float alpha = 1.0f)
{
    if (tier < 0) return;
    const Tex &t = emblem_tex(tier, size * f.s() > 70);
    if (t.srv) image(dl, f, t, cx - size / 2, cy - size / 2, size, size, with_alpha(IM_COL32_WHITE, alpha));
    else emblem_fallback(dl, f, cx, cy, size * 0.45f, tier, alpha);
}

// The crew's crown emblem (a crew reward, level 6): a small gold crown.
void crown_small(ImDrawList *dl, const Frame &f, float cx, float cy, float size, float alpha = 1.0f)
{
    float s = size / 24.0f;
    ImVec2 p[7] = { f.p(cx - 9 * s, cy + 6 * s), f.p(cx - 10 * s, cy - 6 * s), f.p(cx - 4 * s, cy - 1 * s), f.p(cx, cy - 9 * s),
                    f.p(cx + 4 * s, cy - 1 * s), f.p(cx + 10 * s, cy - 6 * s), f.p(cx + 9 * s, cy + 6 * s) };
    dl->AddConcavePolyFilled(p, 7, with_alpha(IM_COL32(255, 197, 61, 255), alpha));
    dl->AddPolyline(p, 7, with_alpha(IM_COL32(6, 5, 7, 255), alpha), ImDrawFlags_Closed, 1.2f * s * f.s());
}

// A name's colour from a look's equipped colour (a tier; -1 none: `fallback`).
ImU32 look_name_col(int color, ImU32 fallback)
{
    if (color < 0 || color > 7) return fallback;
    if (color == 7) {       // Legend: the name pulses between the neon red and the fire's gold
        float p = pulse01(1.6f);
        return IM_COL32(255, (int)(77 + 120 * p), (int)(94 - 30 * p), 255);
    }
    return k_tier_col[color];
}

// An avatar frame (a tier; -1 none) around a picture of radius r centred at cx, cy. The Legend frame pulses.
void avatar_frame(ImDrawList *dl, const Frame &f, float cx, float cy, float r, int frame, bool pulse, float alpha = 1.0f)
{
    if (frame < 0 || frame > 7) return;
    ImU32 col = tier_col(frame);
    ImVec2 c = f.p(cx, cy);
    float rs = r * f.s();
    if (frame == 7 || pulse) {
        float p = pulse01(1.6f);
        for (int g = 3; g >= 1; g--) dl->AddCircle(c, rs + (1.5f + g * 1.6f * p) * f.s(), with_alpha(col, alpha * 0.18f * p / g * 3), 32,
                                                   1.6f * f.s());
    }
    dl->AddCircle(c, rs + 0.6f * f.s(), with_alpha(IM_COL32(6, 5, 7, 255), alpha), 32, 2.6f * f.s());
    dl->AddCircle(c, rs + 0.6f * f.s(), with_alpha(col, alpha), 32, std::max(1.2f, r * 0.16f) * f.s());
    if (frame >= 5) {       // Kingpin and up: studs on the ring
        for (int i = 0; i < 8; i++) {
            float a = i * 0.785398f;
            dl->AddCircleFilled(ImVec2(c.x + cosf(a) * (rs + 0.6f * f.s()), c.y + sinf(a) * (rs + 0.6f * f.s())),
                                std::max(0.8f, r * 0.08f) * f.s(), with_alpha(IM_COL32(255, 250, 225, 255), alpha), 8);
        }
    }
}
