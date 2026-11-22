// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Towers rewards (build 104; the owner's approved designs "Tower Player Card Frames" v2, 2026-11-20): the player-card
// frames and titles earned in the Towers, drawn with ImGui shapes. Included by online_screen.cpp inside its anonymous
// namespace after rank_art.inc. Which reward an id is, its rarity and its effects: tower_rewards.c.
//
//   RARE       Floor 25 (steel) / Floor 50 (bronze): brushed metal, rivets, the floor's plate; no motion
//   EPIC       Uptown Champion: gold, a skyline crest, a slow light sweep, the title's shine
//              Untouchable: chrome with cut corners and a shield, a fast glint
//              Weekly Conqueror: neon tubes that breathe, the sign and the title flicker
//   LEGENDARY  King of the Five Boroughs: platinum, a pulsing crown, five borough gems, sweep, shine
//   MYTHIC     Tower Legend: red lacquer with gold trim, laurel, the month's ribbon, rising embers, a pulsing
//              crown, sweep, shine
//
// Build 108, the Team Tower's own: each is its solo look (tower_reward_base) with the TAG TEAM plate on the card's
// foot (two linked rings and the words): Tag Team 25 (steel, the "25" plate), Untouchable Duo (chrome), Tag Team
// Champions (LEGENDARY: gold with the pulsing crown, sweep and shine) and Tag Team Legend (the legend's lacquer,
// laurel, embers and the team tower's month on the ribbon).
//
// Everything works on any card size (the room's 128 x 270, the watch screens', the hill's king, the Locker's
// preview). Visual only and light: fixed arrays, no allocation, the time from GetTickCount64 (a fixed moment in the
// gallery, so its captures match).

#include "tower_rewards.h"

struct TrwStyle { ImU32 hi, mid, lo, trim, title, title_hi; };

const TrwStyle &trw_style(int kind)
{
    static const TrwStyle k[TRW_KINDS] = {
        { 0, 0, 0, 0, IM_COL32(242, 234, 216, 255), IM_COL32(255, 255, 255, 255) },                       // none
        { IM_COL32(232, 238, 244, 255), IM_COL32(138, 148, 160, 255), IM_COL32(58, 64, 74, 255),          // steel
          IM_COL32(196, 204, 214, 255), IM_COL32(176, 188, 200, 255), IM_COL32(255, 255, 255, 255) },
        { IM_COL32(231, 176, 122, 255), IM_COL32(154, 95, 43, 255), IM_COL32(90, 52, 20, 255),            // bronze
          IM_COL32(240, 192, 140, 255), IM_COL32(208, 138, 75, 255), IM_COL32(255, 220, 180, 255) },
        { IM_COL32(255, 243, 184, 255), IM_COL32(217, 171, 50, 255), IM_COL32(122, 84, 16, 255),          // gold
          IM_COL32(201, 154, 46, 255), IM_COL32(217, 171, 50, 255), IM_COL32(255, 246, 200, 255) },
        { IM_COL32(255, 255, 255, 255), IM_COL32(125, 135, 148, 255), IM_COL32(29, 35, 43, 255),          // chrome
          IM_COL32(230, 237, 243, 255), IM_COL32(207, 214, 222, 255), IM_COL32(255, 255, 255, 255) },
        { IM_COL32(255, 79, 216, 255), IM_COL32(63, 228, 255, 255), IM_COL32(20, 15, 28, 255),            // neon
          IM_COL32(63, 228, 255, 255), IM_COL32(63, 228, 255, 255), IM_COL32(220, 250, 255, 255) },
        { IM_COL32(255, 255, 255, 255), IM_COL32(185, 189, 199, 255), IM_COL32(93, 99, 110, 255),         // platinum
          IM_COL32(230, 233, 239, 255), IM_COL32(232, 195, 74, 255), IM_COL32(255, 255, 255, 255) },
        { IM_COL32(255, 90, 67, 255), IM_COL32(168, 24, 15, 255), IM_COL32(86, 8, 5, 255),                // lacquer
          IM_COL32(212, 166, 42, 255), IM_COL32(240, 179, 58, 255), IM_COL32(255, 243, 200, 255) },
        // build 108, Faction War: D-Mob's green and gold, Crow's red and black (the winners' frames, the top scorers' too)
        { IM_COL32(120, 230, 160, 255), IM_COL32(28, 150, 84, 255), IM_COL32(6, 52, 30, 255),             // D-Mob
          IM_COL32(255, 206, 72, 255), IM_COL32(255, 206, 72, 255), IM_COL32(255, 246, 200, 255) },
        { IM_COL32(255, 96, 96, 255), IM_COL32(190, 20, 36, 255), IM_COL32(30, 4, 8, 255),                // Crow
          IM_COL32(232, 228, 224, 255), IM_COL32(236, 60, 72, 255), IM_COL32(255, 230, 230, 255) },
        { IM_COL32(120, 230, 160, 255), IM_COL32(28, 150, 84, 255), IM_COL32(6, 52, 30, 255),             // D-Mob, top
          IM_COL32(255, 206, 72, 255), IM_COL32(255, 206, 72, 255), IM_COL32(255, 246, 200, 255) },
        { IM_COL32(255, 96, 96, 255), IM_COL32(190, 20, 36, 255), IM_COL32(30, 4, 8, 255),                // Crow, top
          IM_COL32(232, 228, 224, 255), IM_COL32(236, 60, 72, 255), IM_COL32(255, 230, 230, 255) },
        // build 108, the team kinds
        { IM_COL32(232, 238, 244, 255), IM_COL32(138, 148, 160, 255), IM_COL32(58, 64, 74, 255),          // steel
          IM_COL32(196, 204, 214, 255), IM_COL32(176, 188, 200, 255), IM_COL32(255, 255, 255, 255) },
        { IM_COL32(255, 255, 255, 255), IM_COL32(125, 135, 148, 255), IM_COL32(29, 35, 43, 255),          // chrome
          IM_COL32(230, 237, 243, 255), IM_COL32(207, 214, 222, 255), IM_COL32(255, 255, 255, 255) },
        { IM_COL32(255, 243, 184, 255), IM_COL32(217, 171, 50, 255), IM_COL32(122, 84, 16, 255),          // gold
          IM_COL32(255, 226, 120, 255), IM_COL32(232, 190, 64, 255), IM_COL32(255, 250, 215, 255) },
        { IM_COL32(255, 90, 67, 255), IM_COL32(168, 24, 15, 255), IM_COL32(86, 8, 5, 255),                // lacquer
          IM_COL32(212, 166, 42, 255), IM_COL32(240, 179, 58, 255), IM_COL32(255, 243, 200, 255) },
    };
    return k[kind > 0 && kind < TRW_KINDS ? kind : 0];
}

ImU32 rarity_col(int rarity)
{
    switch (rarity) {
    case TRW_RARE: return IM_COL32(90, 176, 255, 255);
    case TRW_EPIC: return IM_COL32(185, 140, 255, 255);
    case TRW_LEGENDARY: return IM_COL32(255, 207, 90, 255);
    case TRW_MYTHIC: return IM_COL32(255, 107, 74, 255);
    default: return CREAM;
    }
}

// Build 108: the team kinds' TAG TEAM plate, centred at cx on the card's foot line y: a dark pill in the frame's
// trim with two linked rings (the two players) and the words.
void trw_team_plate(ImDrawList *dl, const Frame &f, float cx, float y, float u, const TrwStyle &s, float a)
{
    float size = std::max(7.0f, std::min(11.0f, u * 0.075f)), r = size * 0.42f;
    float tw = text_width(f, g_title, size, "TAG TEAM"), w = tw + r * 4.6f + size * 1.4f, h = size * 1.55f;
    float x = cx - w / 2, top = y - h / 2;
    dl->AddRectFilled(f.p(x, top), f.p(x + w, top + h), with_alpha(IM_COL32(12, 10, 14, 255), a * 0.92f), h / 2 * f.s());
    dl->AddRect(f.p(x, top), f.p(x + w, top + h), with_alpha(s.trim, a), h / 2 * f.s(), 0, 1.3f * f.s());
    float rx = x + size * 0.7f + r, ry = top + h / 2;
    dl->AddCircle(f.p(rx, ry), r * f.s(), with_alpha(s.hi, a), 14, 1.4f * f.s());
    dl->AddCircle(f.p(rx + r * 1.25f, ry), r * f.s(), with_alpha(s.mid, a), 14, 1.4f * f.s());
    text(dl, f, g_title, size, rx + r * 2.6f + tw / 2, top + (h - size) / 2 - 0.5f, with_alpha(s.title_hi, a), "TAG TEAM", CENTER, 0.4f);
}

// The effects' clock in seconds (the gallery: one fixed moment, a sweep half way across).
float trw_now()
{
    if (gallery_on()) return 1.0f;
    return (float)(GetTickCount64() % 3600000ULL) / 1000.0f;
}

// A metal border of thickness b inside the card: four strips, each a gradient hi -> lo across the metal.
void trw_metal(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, float b, const TrwStyle &s, float a)
{
    ImU32 hi = with_alpha(s.hi, a), mid = with_alpha(s.mid, a), lo = with_alpha(s.lo, a);
    dl->AddRectFilledMultiColor(f.p(x, y), f.p(x + w, y + b), hi, mid, lo, mid);                  // top
    dl->AddRectFilledMultiColor(f.p(x, y + h - b), f.p(x + w, y + h), mid, hi, mid, lo);          // bottom
    dl->AddRectFilledMultiColor(f.p(x, y + b), f.p(x + b, y + h - b), hi, mid, lo, lo);           // left
    dl->AddRectFilledMultiColor(f.p(x + w - b, y + b), f.p(x + w, y + h - b), mid, hi, mid, mid); // right
    dl->AddRect(f.p(x, y), f.p(x + w, y + h), with_alpha(IM_COL32(0, 0, 0, 200), a), 0, 0, 1.0f * f.s());
    dl->AddRect(f.p(x + b, y + b), f.p(x + w - b, y + h - b), with_alpha(IM_COL32(10, 8, 6, 230), a), 0, 0, 1.2f * f.s());
    dl->AddRect(f.p(x + b + 1.5f, y + b + 1.5f), f.p(x + w - b - 1.5f, y + h - b - 1.5f), with_alpha(s.trim, a * 0.55f), 0, 0,
                0.8f * f.s());
}

// A light band crossing the card at place p (0..1), slanted 18 degrees; `narrow` is the chrome glint.
void trw_sweep(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, float p, bool narrow, float a)
{
    if (p < 0) return;
    float cx = x - 0.6f * w + p * 1.9f * w, dx = h * 0.325f;
    static const float widths[3] = { 1.0f, 0.55f, 0.22f };
    float base = narrow ? 0.18f : 0.40f, peak = narrow ? 0.30f : 0.14f;
    dl->PushClipRect(f.p(x, y), f.p(x + w, y + h), true);
    for (int i = 0; i < 3; i++) {
        float bw = w * base * widths[i] * 0.5f;
        dl->AddQuadFilled(f.p(cx - bw + dx / 2, y), f.p(cx + bw + dx / 2, y), f.p(cx + bw - dx / 2, y + h),
                          f.p(cx - bw - dx / 2, y + h), with_alpha(IM_COL32(255, 255, 255, 255), a * peak * (0.6f + 0.4f * i)));
    }
    dl->PopClipRect();
}

void trw_rivet(ImDrawList *dl, const Frame &f, float cx, float cy, float r, const TrwStyle &s, float a)
{
    dl->AddCircleFilled(f.p(cx, cy), r * f.s(), with_alpha(s.trim, a), 12);
    dl->AddCircle(f.p(cx, cy), r * f.s(), with_alpha(s.lo, a), 12, 0.8f * f.s());
    dl->AddCircleFilled(f.p(cx - r * 0.3f, cy - r * 0.3f), r * 0.35f * f.s(), with_alpha(IM_COL32(255, 255, 255, 200), a), 8);
}

// The floor plate on the top edge: "25" or "50".
void trw_plate(ImDrawList *dl, const Frame &f, float cx, float top, float w, const char *label, const TrwStyle &s, float a)
{
    float pw = w * 0.27f, ph = pw * 0.53f, x = cx - pw / 2, y = top - ph * 0.6f;
    dl->AddRectFilledMultiColor(f.p(x, y), f.p(x + pw, y + ph), with_alpha(s.hi, a), with_alpha(s.hi, a), with_alpha(s.lo, a),
                                with_alpha(s.lo, a));
    dl->AddRect(f.p(x, y), f.p(x + pw, y + ph), with_alpha(IM_COL32(30, 18, 6, 255), a), 2 * f.s(), 0, 1.2f * f.s());
    text(dl, f, g_stencil, ph * 0.78f, cx, y + ph * 0.08f, with_alpha(IM_COL32(40, 22, 8, 255), a), label, CENTER, 0);
}

// Uptown's skyline crest over the top edge.
void trw_skyline(ImDrawList *dl, const Frame &f, float cx, float top, float w, const TrwStyle &s, float a)
{
    static const float bars[][3] = {      // x (of the crest's width, -0.5..0.5), width, height (of the crest's height)
        { -0.48f, 0.09f, 0.35f }, { -0.38f, 0.08f, 0.6f }, { -0.29f, 0.10f, 0.45f }, { -0.18f, 0.08f, 0.8f },
        { -0.09f, 0.07f, 0.55f }, { -0.025f, 0.05f, 1.0f }, { 0.03f, 0.08f, 0.62f }, { 0.12f, 0.09f, 0.85f },
        { 0.22f, 0.08f, 0.5f }, { 0.31f, 0.09f, 0.7f }, { 0.41f, 0.08f, 0.4f },
    };
    float cw = w * 0.5f, ch = w * 0.16f;
    for (auto &b : bars) {
        float bx = cx + b[0] * cw, bh = b[2] * ch;
        dl->AddRectFilledMultiColor(f.p(bx, top - bh), f.p(bx + b[1] * cw, top + 2), with_alpha(s.hi, a), with_alpha(s.hi, a),
                                    with_alpha(s.lo, a), with_alpha(s.lo, a));
    }
    float sx = cx;      // the spire on the tallest
    dl->AddTriangleFilled(f.p(sx - 0.025f * cw, top - ch), f.p(sx, top - ch * 1.3f), f.p(sx + 0.025f * cw, top - ch),
                          with_alpha(s.hi, a));
}

// A crown over the top edge, pulsing: bigger and glowing at the peak.
void trw_crown(ImDrawList *dl, const Frame &f, float cx, float top, float cw, float pulse, float a)
{
    float k = 1.0f + 0.06f * pulse;
    if (pulse > 0.02f)
        for (int g = 3; g >= 1; g--)
            dl->AddCircleFilled(f.p(cx, top - cw * 0.2f), cw * (0.35f + 0.12f * g) * f.s(),
                                with_alpha(IM_COL32(255, 200, 80, 255), a * 0.10f * pulse), 24);
    crown(dl, f, cx, top - cw * 0.62f * k * 0.3f, cw * k, with_alpha(IM_COL32(232, 190, 64, 255), a));
}

void trw_gems(ImDrawList *dl, const Frame &f, float cx, float bottom, float w, float a)
{
    static const ImU32 gems[5] = { IM_COL32(47, 111, 214, 255), IM_COL32(210, 58, 58, 255), IM_COL32(232, 195, 74, 255),
                                   IM_COL32(47, 163, 107, 255), IM_COL32(138, 79, 208, 255) };
    float span = w * 0.62f, r = w * 0.028f;
    for (int i = 0; i < 5; i++) {
        float gx = cx - span / 2 + span * i / 4, rr = i == 2 ? r * 1.1f : r;
        dl->AddCircleFilled(f.p(gx, bottom), rr * f.s(), with_alpha(gems[i], a), 14);
        dl->AddCircle(f.p(gx, bottom), rr * f.s(), with_alpha(IM_COL32(26, 28, 34, 255), a), 14, 1.0f * f.s());
        dl->AddCircleFilled(f.p(gx - rr * 0.3f, bottom - rr * 0.3f), rr * 0.3f * f.s(), with_alpha(IM_COL32(255, 255, 255, 170), a), 8);
    }
}

// The legend's laurel up the portrait's sides: leaves along two arcs.
void trw_laurel(ImDrawList *dl, const Frame &f, float x, float y, float w, float foot, float a)
{
    ImU32 col = with_alpha(IM_COL32(212, 166, 42, 255), a * 0.55f);
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < 6; i++) {
            float t = i / 5.0f, ang = 0.2f + t * 1.1f;
            float lx = side ? x + w - w * (0.10f + 0.05f * sinf(ang)) : x + w * (0.10f + 0.05f * sinf(ang));
            float ly = y + foot - t * foot * 0.5f;
            dl->AddCircleFilled(f.p(lx, ly), w * 0.022f * f.s(), col, 8);
            dl->AddCircleFilled(f.p(lx + (side ? -1 : 1) * w * 0.03f, ly - w * 0.02f), w * 0.016f * f.s(), col, 8);
        }
    }
}

// The legend's ribbon across the portrait's foot.
void trw_ribbon(ImDrawList *dl, const Frame &f, float x, float y, float w, const char *line, float a)
{
    float h = std::max(11.0f, w * 0.06f), l = x + w * 0.04f, r = x + w * 0.96f, n = h * 0.45f;
    ImVec2 pts[6] = { f.p(l, y), f.p(r, y), f.p(r - n, y + h / 2), f.p(r, y + h), f.p(l, y + h), f.p(l + n, y + h / 2) };
    ImU32 red = with_alpha(IM_COL32(181, 28, 18, 255), a);
    dl->AddRectFilled(f.p(l + n, y), f.p(r - n, y + h), red);      // the notched ends: two triangles each side
    dl->AddTriangleFilled(pts[0], f.p(l + n, y), pts[5], red);
    dl->AddTriangleFilled(pts[4], pts[5], f.p(l + n, y + h), red);
    dl->AddTriangleFilled(f.p(r - n, y), pts[1], pts[2], red);
    dl->AddTriangleFilled(pts[2], pts[3], f.p(r - n, y + h), red);
    dl->AddPolyline(pts, 6, with_alpha(IM_COL32(212, 166, 42, 255), a), ImDrawFlags_Closed, 1.2f * f.s());
    float size = fit_size(f, g_title, h * 0.72f, line, (r - l) - 2 * n - 4);
    text(dl, f, g_title, size, x + w / 2, y + (h - size) * 0.45f, with_alpha(IM_COL32(246, 226, 162, 255), a), line, CENTER, 0);
}

void trw_shield(ImDrawList *dl, const Frame &f, float cx, float cy, float s, float a)
{
    ImVec2 pts[7] = { f.p(cx, cy - s * 0.5f), f.p(cx + s * 0.42f, cy - s * 0.35f), f.p(cx + s * 0.42f, cy),
                      f.p(cx + s * 0.25f, cy + s * 0.35f), f.p(cx, cy + s * 0.5f), f.p(cx - s * 0.25f, cy + s * 0.35f),
                      f.p(cx - s * 0.42f, cy) };
    ImVec2 left = f.p(cx - s * 0.42f, cy - s * 0.35f);
    ImVec2 all[8] = { pts[0], pts[1], pts[2], pts[3], pts[4], pts[5], pts[6], left };
    dl->AddConvexPolyFilled(all, 8, with_alpha(IM_COL32(17, 22, 28, 255), a));
    dl->AddPolyline(all, 8, with_alpha(IM_COL32(230, 237, 243, 255), a), ImDrawFlags_Closed, 1.3f * f.s());
    ImVec2 chk[3] = { f.p(cx - s * 0.2f, cy), f.p(cx - s * 0.05f, cy + s * 0.15f), f.p(cx + s * 0.22f, cy - s * 0.15f) };
    dl->AddPolyline(chk, 3, with_alpha(IM_COL32(230, 237, 243, 255), a), 0, 1.6f * f.s());
}

// The tower frame of item id around a card at x, y (w x h); `foot` is the portrait's foot below y (the ribbon and
// the embers' start; <= 0: two thirds down). Returns false when id is no tower frame (nothing drawn).
bool tower_card_frame(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, const char *id, float foot,
                      float a)
{
    int kind = tower_reward_kind(id, nullptr, nullptr, nullptr), base = tower_reward_base(kind);
    if (kind == TRW_NONE) return false;
    if (foot <= 0) foot = h * 0.66f;
    const TrwStyle &s = trw_style(kind);
    bool team = tower_reward_team(kind) != 0;
    unsigned fx = tower_reward_effects(kind);
    // u: the ornaments' measure, a portrait card's width (a wide card, as the Locker's versus preview, keeps them small)
    float t = trw_now(), u = std::min(w, h * 0.66f), b = std::max(3.0f, std::min(7.0f, u * 0.03f)), cx = x + w / 2;
    float pulse = fx & TRW_FX_PULSE ? tower_fx_pulse(t, 2.4f) : 0;
    if (fx & TRW_FX_EMBERS) {       // under the frame, over the portrait
        dl->PushClipRect(f.p(x, y), f.p(x + w, y + h), true);
        for (int i = 0; i < TRW_EMBERS; i++) {
            float ex, rise, ea;
            tower_fx_ember(i, t, &ex, &rise, &ea);
            ImVec2 c = f.p(x + ex * w, y + foot + 4 - rise * foot * 0.95f);
            dl->AddCircleFilled(c, 3.2f * f.s(), with_alpha(IM_COL32(255, 120, 40, 255), a * ea * 0.35f), 10);
            dl->AddCircleFilled(c, 1.5f * f.s(), with_alpha(IM_COL32(255, 179, 71, 255), a * ea), 8);
        }
        dl->PopClipRect();
    }
    switch (base) {
    case TRW_WEEKLY: {      // neon: a pink tube, a dark gap, a cyan tube, each with its glow
        float g = fx & TRW_FX_GLOW ? tower_fx_pulse(t, 3.0f) : 0.5f;
        for (int k = 4; k >= 1; k--) {
            float e = k * (1.2f + 0.6f * g);
            dl->AddRect(f.p(x - e, y - e), f.p(x + w + e, y + h + e), with_alpha(s.hi, a * (0.10f + 0.10f * g) / k), 10 * f.s(), 0,
                        2.0f * f.s());
            dl->AddRect(f.p(x - 6 - e, y - 6 - e), f.p(x + w + 6 + e, y + h + 6 + e), with_alpha(s.mid, a * (0.08f + 0.10f * g) / k),
                        12 * f.s(), 0, 2.0f * f.s());
        }
        dl->AddRect(f.p(x, y), f.p(x + w, y + h), with_alpha(s.hi, a), 10 * f.s(), 0, 2.0f * f.s());
        dl->AddRect(f.p(x - 6, y - 6), f.p(x + w + 6, y + h + 6), with_alpha(s.mid, a), 12 * f.s(), 0, 2.0f * f.s());
        float fl = fx & TRW_FX_FLICKER ? tower_fx_flicker(t) : 1.0f;
        float size = fit_size(f, g_stencil, std::max(10.0f, u * 0.12f), "WEEKLY", w - 16);
        text(dl, f, g_stencil, size, cx, y + h * 0.05f, with_alpha(s.mid, a * 0.9f * fl), "WEEKLY", CENTER, 0);
        return true;
    }
    case TRW_UNTOUCHABLE: {     // chrome with cut corners: a thick outline of the cut shape
        float c = b * 2.6f, m = b / 2;
        ImVec2 pts[6] = { f.p(x + c, y + m), f.p(x + w - m, y + m), f.p(x + w - m, y + h - c), f.p(x + w - c, y + h - m),
                          f.p(x + m, y + h - m), f.p(x + m, y + c) };
        dl->AddPolyline(pts, 6, with_alpha(s.lo, a), ImDrawFlags_Closed, (b + 1) * f.s());
        dl->AddPolyline(pts, 6, with_alpha(s.mid, a), ImDrawFlags_Closed, b * 0.7f * f.s());
        dl->AddPolyline(pts, 6, with_alpha(s.hi, a * 0.9f), ImDrawFlags_Closed, b * 0.25f * f.s());
        trw_shield(dl, f, x + w - b - u * 0.09f, y + b + u * 0.1f, u * 0.13f, a);
        if (fx & TRW_FX_GLINT) trw_sweep(dl, f, x, y, w, h, tower_fx_sweep(t, 2.4f), true, a);
        if (team) trw_team_plate(dl, f, cx, y + h - b / 2, u, s, a);
        return true;
    }
    case TRW_LEGEND:        // red lacquer inside a gold line, the gold trim inside
        dl->AddRect(f.p(x - 2.5f, y - 2.5f), f.p(x + w + 2.5f, y + h + 2.5f), with_alpha(s.trim, a), 0, 0, 1.6f * f.s());
        trw_metal(dl, f, x, y, w, h, b, s, a);
        dl->AddRect(f.p(x + b + 0.5f, y + b + 0.5f), f.p(x + w - b - 0.5f, y + h - b - 0.5f), with_alpha(s.trim, a), 0, 0,
                    1.4f * f.s());
        trw_laurel(dl, f, x, y, w, foot, a);
        {
            char line[48];
            if (!tower_reward_ribbon(id, line, (int)sizeof(line))) snprintf(line, sizeof(line), "#1 OF THE MONTH");
            trw_ribbon(dl, f, x, y + foot - std::max(11.0f, w * 0.06f) - 4, w, line, a);
        }
        trw_crown(dl, f, cx, y, u * 0.3f, pulse, a);
        break;
    case TRW_FW_DMOB: case TRW_FW_CROW: case TRW_FW_TOP_DMOB: case TRW_FW_TOP_CROW: {
        // Faction War: the side's metal inside its trim line, the trim inside, the ribbon (who won, which month); the top
        // scorers wear the crown too
        bool top = kind == TRW_FW_TOP_DMOB || kind == TRW_FW_TOP_CROW;
        if (!top)       // the winners' side glow, breathing (the pulse)
            for (int k = 3; k >= 1; k--)
                dl->AddRect(f.p(x - 2.5f - k * 1.6f, y - 2.5f - k * 1.6f), f.p(x + w + 2.5f + k * 1.6f, y + h + 2.5f + k * 1.6f),
                            with_alpha(s.mid, a * (0.10f + 0.22f * pulse) / k), 2 * f.s(), 0, 2.0f * f.s());
        dl->AddRect(f.p(x - 2.5f, y - 2.5f), f.p(x + w + 2.5f, y + h + 2.5f), with_alpha(s.trim, a), 0, 0, 1.6f * f.s());
        trw_metal(dl, f, x, y, w, h, b, s, a);
        dl->AddRect(f.p(x + b + 0.5f, y + b + 0.5f), f.p(x + w - b - 0.5f, y + h - b - 0.5f), with_alpha(s.trim, a), 0, 0,
                    1.4f * f.s());
        {
            char line[64];
            if (!tower_reward_ribbon(id, line, (int)sizeof(line))) snprintf(line, sizeof(line), "FACTION WAR");
            trw_ribbon(dl, f, x, y + foot - std::max(11.0f, w * 0.06f) - 4, w, line, a);
        }
        if (top) trw_crown(dl, f, cx, y, u * 0.3f, pulse, a);
        else trw_plate(dl, f, cx, y, u * 1.4f, kind == TRW_FW_DMOB ? "D-MOB" : "CROW", s, a);
        break;
    }
    case TRW_BOROUGHS:
        trw_metal(dl, f, x, y, w, h, b, s, a);
        if (!team) trw_gems(dl, f, cx, y + h, u, a);     // Tag Team Champions: the TAG TEAM plate in their place
        trw_crown(dl, f, cx, y, u * 0.38f, pulse, a);
        break;
    case TRW_UPTOWN:
        trw_metal(dl, f, x, y, w, h, b, s, a);
        trw_skyline(dl, f, cx, y, u, s, a);
        for (int k = 0; k < 2; k++) {       // the corner brackets
            float px = k ? x + w + 2 : x - 2, py = k ? y + h + 2 : y - 2, d = k ? -1.0f : 1.0f, l = u * 0.08f;
            ImVec2 br[3] = { f.p(px, py + d * l), f.p(px, py), f.p(px + d * l, py) };
            dl->AddPolyline(br, 3, with_alpha(s.hi, a), 0, 2.2f * f.s());
        }
        break;
    default: {          // the floors: brushed metal, rivets, the plate
        trw_metal(dl, f, x, y, w, h, b, s, a);
        float r = std::max(1.8f, b * 0.55f), o = b / 2;
        const float rx[2] = { x + o, x + w - o }, ry[3] = { y + o, y + h / 2, y + h - o };
        for (float px : rx)
            for (float py : ry) trw_rivet(dl, f, px, py, py == y + h / 2 ? r * 0.8f : r, s, a);
        trw_plate(dl, f, cx, y, u, base == TRW_FLOOR50 ? "50" : "25", s, a);
        break;
    }
    }
    if (fx & TRW_FX_SWEEP) trw_sweep(dl, f, x, y, w, h, tower_fx_sweep(t, base == TRW_UPTOWN ? 5.0f : 3.2f), false, a);
    if (team) trw_team_plate(dl, f, cx, y + h - b / 2, u, s, a);
    return true;
}

// A title worn from the Towers, centred at cx (top y), with its rarity's effect: the shine runs along it (a bright
// copy clipped to a moving band), the neon flickers. Returns false when id is no tower title (nothing drawn).
bool tower_title_text(ImDrawList *dl, const Frame &f, float cx, float y, float size, const char *title, const char *id,
                      float maxw, float a)
{
    int kind = tower_reward_kind(id, nullptr, nullptr, nullptr);
    if (kind == TRW_NONE || !title || !title[0]) return false;
    char up[96];
    int n = 0;
    for (const char *p = title; *p && n < (int)sizeof(up) - 1; p++) up[n++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
    up[n] = 0;
    if (kind == TRW_LEGEND) snprintf(up, sizeof(up), "TOWER LEGEND");       // the ribbon carries the month
    if (kind == TRW_TEAM_LEGEND) snprintf(up, sizeof(up), "TAG TEAM LEGEND");
    const TrwStyle &s = trw_style(kind);
    unsigned fx = tower_reward_effects(kind);
    float t = trw_now();
    if (fx & TRW_FX_FLICKER) a *= tower_fx_flicker(t);
    size = fit_size(f, g_title, size, up, maxw);
    text(dl, f, g_title, size, cx, y, with_alpha(s.title, a), up, CENTER, 0.6f);
    if (fx & TRW_FX_SHINE) {
        float tw = text_width(f, g_title, size, up), p = t / 3.2f - floorf(t / 3.2f);
        float bx = cx + tw * 0.5f + tw * 0.3f - p * tw * 1.6f;     // right to left, as the design's
        for (int k = 0; k < 2; k++) {
            float half = tw * (k ? 0.06f : 0.16f);
            dl->PushClipRect(f.p(bx - half, y - 2), f.p(bx + half, y + size + 4), true);
            text(dl, f, g_title, size, cx, y, with_alpha(s.title_hi, a * (k ? 1.0f : 0.55f)), up, CENTER, 0);
            dl->PopClipRect();
        }
    }
    return true;
}

// A card's frame from a look: the tower frame when one is worn (ranked on or off), else the season tier's border
// while ranked is offered.
void look_card(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, float round, const social::Look *lk,
               float foot, float a)
{
    if (!lk) return;
    if (tower_card_frame(dl, f, x, y, w, h, lk->frame_id.c_str(), foot, a)) return;
    if (ranked_shown()) card_border(dl, f, x, y, w, h, round, lk->frame, a);
}

// The tower title of a look on a card (nothing for other titles: the card had none before build 104).
void look_card_title(ImDrawList *dl, const Frame &f, float cx, float y, float size, const social::Look *lk, float maxw,
                     float a)
{
    if (lk) tower_title_text(dl, f, cx, y, size, lk->title.c_str(), lk->title_id.c_str(), maxw, a);
}
