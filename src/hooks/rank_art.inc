// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
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

// ---- a player card's rank (build 52: the versus screen and the ranked room's cards) ----------------------
//
// The emblem as a patch over the card's top edge, centred, and under it the rank plate in the tier's colour:
// the tier's name in the stencil font on a band of the emblem's metal (a Legend: the neon fire), and under it
// on a dark band the division chevrons and the rating (a Legend: its Top 10 place, "#3 NY", and the rating).
// A player still placing gets the same plate in steel, PLACEMENT and a dot per placement set (filled for the
// sets played), and a "?" patch; a player without a rank a plain UNRANKED plate. Sizes are in units of `s`
// (1 = the room card, 128 wide; the versus card uses 1.4): the patch's art is 50 tall, centred 2 below
// `top`; the plate is 112 wide, from top + 27 to top + 58 (CARD_RANK_H), so a card's portrait starts lower.

const float CARD_RANK_H = 58.0f;

struct CardRank {
    int kind = 0;           // 0 no rank, 1 placing, 2 ranked
    int tier = 0, division = 0, rating = 0, top = 0;
    int placed = 0, of = 5; // placing: the sets played of the placement sets
    bool provisional = false;
};

ImU32 mix_col(ImU32 a, ImU32 b, float t)
{
    auto ch = [&](int sh) {
        float x = (float)((a >> sh) & 0xFF), y = (float)((b >> sh) & 0xFF);
        return (ImU32)(x + (y - x) * t + 0.5f) << sh;
    };
    return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) | (a & IM_COL32_A_MASK);
}

// A small division chevron centred at cx, cy, `w` wide: lit in the tier's colour, or dark.
void mini_chevron(ImDrawList *dl, const Frame &f, float cx, float cy, float w, ImU32 col, bool on, float alpha)
{
    float h = w * 0.62f, t = w * 0.34f;
    ImVec2 a = f.p(cx - w / 2, cy + h / 2), b = f.p(cx, cy - h / 2), c = f.p(cx + w / 2, cy + h / 2);
    ImVec2 c2 = f.p(cx + w / 2, cy + h / 2 + t * 0.55f), b2 = f.p(cx, cy - h / 2 + t), a2 = f.p(cx - w / 2, cy + h / 2 + t * 0.55f);
    ImVec2 q1[4] = { a, b, b2, a2 }, q2[4] = { b, c, c2, b2 };
    ImU32 fill = on ? with_alpha(col, alpha) : with_alpha(IM_COL32(52, 48, 56, 255), alpha);
    dl->AddConvexPolyFilled(q1, 4, fill);
    dl->AddConvexPolyFilled(q2, 4, fill);
    if (on) {
        ImVec2 shine[3] = { f.p(cx - w * 0.42f, cy + h * 0.42f), f.p(cx, cy - h * 0.42f), f.p(cx + w * 0.42f, cy + h * 0.42f) };
        dl->AddPolyline(shine, 3, with_alpha(IM_COL32(255, 255, 255, 150), alpha), 0, std::max(0.6f, w * 0.06f) * f.s());
    }
}

// The plate alone, centred at cx, its top at y.
void card_plate(ImDrawList *dl, const Frame &f, float cx, float y, float s, const CardRank &r, float alpha = 1.0f)
{
    const float W = 112 * s, H1 = 17 * s, H2 = 14 * s, H = H1 + H2, x = cx - W / 2;
    bool legend = r.kind == 2 && r.tier >= 7;
    float p = legend ? pulse01(1.6f) : 0.6f;
    ImU32 col = r.kind == 2 ? tier_col(r.tier) : r.kind == 1 ? IM_COL32(176, 184, 198, 255) : IM_COL32(110, 110, 116, 255);
    ImU32 edge = legend ? IM_COL32(255, (int)(59 + 120 * p), (int)(84 - 30 * p), 255) : col;
    if (r.kind == 2 && r.tier >= 5) {       // Kingpin and up glow (the design); the Legend's breathes
        for (int g = 3; g >= 1; g--) {
            float e = g * 1.6f * s;
            dl->AddRectFilled(f.p(x - e, y - e), f.p(x + W + e, y + H + e), with_alpha(edge, alpha * (legend ? 0.05f + 0.07f * p : 0.06f)),
                              (4 * s + e) * f.s());
        }
    }
    dl->AddRectFilled(f.p(x, y), f.p(x + W, y + H), with_alpha(IM_COL32(12, 10, 13, 245), alpha), 4 * s * f.s());
    // the metal band: the tier's colour, lit from above (the Legend: fire from gold to neon red)
    ImU32 top_c = legend ? IM_COL32(255, 196, 70, 255) : mix_col(col, IM_COL32(255, 255, 255, 255), 0.30f);
    ImU32 bot_c = legend ? IM_COL32(214, 28, 64, 255) : mix_col(col, IM_COL32(0, 0, 0, 255), 0.42f);
    float in = 1.2f * s;
    dl->AddRectFilledMultiColor(f.p(x + in, y + in), f.p(x + W - in, y + H1), with_alpha(top_c, alpha), with_alpha(top_c, alpha),
                                with_alpha(bot_c, alpha), with_alpha(bot_c, alpha));
    dl->AddLine(f.p(x + 3 * s, y + in + 0.6f * s), f.p(x + W - 3 * s, y + in + 0.6f * s), with_alpha(IM_COL32(255, 255, 255, 140), alpha),
                std::max(0.6f, 0.8f * s) * f.s());
    dl->AddLine(f.p(x + in, y + H1), f.p(x + W - in, y + H1), with_alpha(IM_COL32(0, 0, 0, 200), alpha), std::max(0.8f, 1.0f * s) * f.s());
    std::string name = r.kind == 2 ? upper(netranked_tier_name(r.tier)) : r.kind == 1 ? std::string("PLACEMENT") : std::string("UNRANKED");
    float ts = fit_size(f, g_stencil, 12.5f * s, name.c_str(), W - 10 * s);
    text(dl, f, g_stencil, ts, cx, y + (H1 - ts) * 0.5f + 0.4f * s, with_alpha(IM_COL32(20, 14, 12, 255), alpha), name.c_str(), CENTER, 0);
    // the dark band: chevrons and the rating, the Legend's place, the placement dots
    float by = y + H1, mid = by + H2 * 0.5f, fs = 10 * s;
    char rating[24];
    snprintf(rating, sizeof(rating), r.provisional ? "%d ?" : "%d", r.rating);
    if (r.kind == 2 && !legend) {
        int lit = division_lit(r.division);
        float cw = 10 * s, gap = 2.5f * s, rw = text_width(f, g_title, fs, rating);
        float total = 3 * cw + 2 * gap + 6 * s + rw, x0 = cx - total / 2;
        for (int i = 0; i < 3; i++) mini_chevron(dl, f, x0 + cw / 2 + i * (cw + gap), mid - 0.8f * s, cw, col, i < lit, alpha);
        text(dl, f, g_title, fs, x0 + 3 * cw + 2 * gap + 6 * s, mid - fs * 0.56f, with_alpha(CREAM, alpha), rating, LEFT, 0);
    } else if (legend) {
        char place[16];
        snprintf(place, sizeof(place), "#%d NY", r.top);
        bool top10 = r.top >= 1 && r.top <= 10;
        float pw = top10 ? text_width(f, g_title, fs, place) + 7 * s : 0, rw = text_width(f, g_title, fs, rating);
        float x0 = cx - (pw + rw) / 2;
        if (top10) text(dl, f, g_title, fs, x0, mid - fs * 0.56f, with_alpha(IM_COL32(255, 197, 61, 255), alpha), place, LEFT, 0);
        text(dl, f, g_title, fs, x0 + pw, mid - fs * 0.56f, with_alpha(CREAM, alpha), rating, LEFT, 0);
    } else if (r.kind == 1) {
        int n = std::max(1, std::min(10, r.of));
        float step = 9 * s, rad = 2.7f * s, x0 = cx - (n - 1) * step / 2;
        for (int i = 0; i < n; i++) {
            ImVec2 c = f.p(x0 + i * step, mid);
            if (i < r.placed) dl->AddCircleFilled(c, rad * f.s(), with_alpha(IM_COL32(255, 168, 26, 255), alpha), 16);
            else dl->AddCircle(c, (rad - 0.4f * s) * f.s(), with_alpha(IM_COL32(200, 204, 214, 200), alpha), 16, std::max(0.8f, 1.0f * s) * f.s());
        }
    } else {
        text(dl, f, g_title, 8.5f * s, cx, mid - 8.5f * s * 0.56f, with_alpha(GREY, alpha), "NO RANK YET", CENTER, 0);
    }
    dl->AddRect(f.p(x, y), f.p(x + W, y + H), with_alpha(edge, alpha * 0.95f), 4 * s * f.s(), 0, std::max(1.0f, 1.3f * s) * f.s());
}

// The patch over the card's top edge: the emblem (a "?" while placing, nothing without a rank).
void card_patch(ImDrawList *dl, const Frame &f, float cx, float top, float s, const CardRank &r, float alpha = 1.0f)
{
    if (r.kind == 0) return;
    float E = 50 * s, ecy = top + 2 * s;
    // a soft shadow under the patch, so it sits on the card
    for (int g = 3; g >= 1; g--)
        dl->AddCircleFilled(f.p(cx, ecy + 2 * s), (E * 0.40f + g * 2.2f * s) * f.s(), with_alpha(IM_COL32(0, 0, 0, 255), alpha * 0.16f), 32);
    if (r.kind == 1) {
        float rr = E * 0.40f;
        dl->AddCircleFilled(f.p(cx, ecy), rr * f.s(), with_alpha(IM_COL32(18, 16, 21, 255), alpha), 32);
        dl->AddCircle(f.p(cx, ecy), rr * f.s(), with_alpha(IM_COL32(176, 184, 198, 255), alpha), 32, 2.4f * s * f.s());
        dl->AddCircle(f.p(cx, ecy), (rr - 3.2f * s) * f.s(), with_alpha(IM_COL32(176, 184, 198, 90), alpha), 32, 1.0f * s * f.s());
        text(dl, f, g_stencil, 22 * s, cx, ecy - 11 * s, with_alpha(CREAM, alpha), "?", CENTER, 0.8f);
        return;
    }
    if (r.tier >= 5) {      // Kingpin and up glow; the Legend breathes
        float p = r.tier >= 7 ? pulse01(1.6f) : 0.6f;
        for (int g = 4; g >= 1; g--)
            dl->AddCircleFilled(f.p(cx, ecy), (E * 0.42f + g * 4 * s) * f.s(), with_alpha(tier_col(r.tier), alpha * (0.04f + 0.03f * p)), 32);
    }
    float B = E * 256.0f / 210.0f;
    const Tex &t = emblem_tex(r.tier, B * f.s() > 70);
    if (t.srv) image(dl, f, t, cx - B / 2, ecy - B * 113.0f / 256.0f, B, B, with_alpha(IM_COL32_WHITE, alpha));
    else emblem_fallback(dl, f, cx, ecy, E * 0.44f, r.tier, alpha);
}

// The whole rank of a card whose top edge is `top`, centred at cx.
void card_rank(ImDrawList *dl, const Frame &f, float cx, float top, float s, const CardRank &r, float alpha = 1.0f)
{
    card_patch(dl, f, cx, top, s, r, alpha);
    card_plate(dl, f, cx, top + 27 * s, s, r, alpha);
}

// A card's border in its player's avatar frame colour (the Locker's equipped frame; -1 none), the Legend's
// glowing softly.
void card_border(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, float round, int frame, float alpha)
{
    if (frame < 0 || frame > 7) return;
    ImU32 col = tier_col(frame);
    if (frame == 7) {
        float p = pulse01(1.6f);
        col = IM_COL32(255, (int)(77 + 120 * p), (int)(94 - 30 * p), 255);
        for (int g = 3; g >= 1; g--) {
            float e = g * 1.8f;
            dl->AddRect(f.p(x - e, y - e), f.p(x + w + e, y + h + e), with_alpha(col, alpha * (0.10f + 0.12f * p) / g),
                        (round + e) * f.s(), 0, 2.0f * f.s());
        }
    }
    dl->AddRect(f.p(x, y), f.p(x + w, y + h), with_alpha(col, alpha * (frame >= 5 ? 0.95f : 0.8f)), round * f.s(), 0, 1.8f * f.s());
}
