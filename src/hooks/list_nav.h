/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Fast scrolling in long lists (build 96): one helper for every list the hub's pages scroll (MY MUSIC,
 * the ranked board, the turf standings, the crew ladder, PLAYERS ONLINE; reusable for any other).
 *  - Up/down held repeats with acceleration: one step at the press, then after 0.3 s 8 steps a second,
 *    rising to 30 a second one second later (list_repeat_steps).
 *  - LT/RT (and PgUp/PgDn, the mouse wheel where the page allows) jump one page; LT/RT held 0.6 s, or
 *    Home/End, go to the top or the bottom.
 *  - A single press past an end wraps where the list wraps; a repeat stops at the end (list_nav_move).
 * The pure part is C (tests/unit/test_list_nav_native.py); the ImGui glue below is for C++ files that
 * include imgui.h first. */
#ifndef DEFJAM_LIST_NAV_H
#define DEFJAM_LIST_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

#define LIST_NAV_DELAY      0.30    /* s before the first repeat */
#define LIST_NAV_RATE_START 8.0     /* steps a second at the first repeat */
#define LIST_NAV_RATE_TOP   30.0    /* ... rising to this */
#define LIST_NAV_RAMP       1.0     /* s the rise takes */
#define LIST_NAV_END_HOLD   0.60    /* s LT/RT is held to reach the top/bottom */
#define LIST_NAV_ALL        1000000 /* a delta that reaches the end of any list */

typedef struct ListRepeat {
    int dir;                /* the direction held (-1 up, 1 down, 0 none) */
    double since, last;     /* when it was pressed; the last update */
    double acc;             /* steps owed (fractions) */
    int first;              /* the steps of the last call were the press itself, not a repeat */
} ListRepeat;

/* The steps (signed) this frame for the direction held now (`dir`: -1, 0, 1) at time `now` (s). */
int list_repeat_steps(ListRepeat *r, int dir, double now);

/* The repeat rate (steps a second) `held` seconds after the press (0 before the first repeat). */
double list_repeat_rate(double held);

/* `sel` moved by `delta` in a list of `n`: clamped to the ends, or wrapped round when `wrap` and the move is
 * a single step (a press, not a repeat or a page). Returns 0 for an empty list. */
int list_nav_move(int sel, int n, int delta, int wrap);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus) && defined(IMGUI_VERSION)
/* ---- ImGui glue ---------------------------------------------------------------------------------- */

struct ListNav {
    ListRepeat rep = {};
    double trig_since[2] = { -1, -1 };      /* LT, RT went down */
    bool trig_end[2] = { false, false };    /* the hold already went to the end */
};

struct ListNavKeys {
    bool keys = true;           /* the page has the keyboard and pads (false: a text box or the chat has them) */
    bool letters = true;        /* W/S move too (the game's keyboard layout) */
    bool page_keys = true;      /* PgUp/PgDn page (false where they turn tabs or board pages) */
    bool triggers = true;       /* LT/RT page, held to the ends (false where they do something else) */
    bool wheel = false;         /* the mouse wheel pages (false where it already scrolls the view) */
    bool home_end = true;
};

/* The delta to move a list's cursor by this frame. `up`/`down`: the page's own once-a-press flags (they
 * still count when no key is held: scripted test presses). `page`: the rows one page shows. *single: the
 * move is one press (wrap allowed). */
inline int list_nav_delta(ListNav &nav, bool up, bool down, const ListNavKeys &in, int page, bool *single = nullptr)
{
    if (single) *single = false;
    if (page < 1) page = 1;
    double now = ImGui::GetTime();
    int dir = 0;
    if (in.keys) {
        bool u = ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp) || ImGui::IsKeyDown(ImGuiKey_GamepadLStickUp) ||
                 ImGui::IsKeyDown(ImGuiKey_UpArrow) || (in.letters && ImGui::IsKeyDown(ImGuiKey_W));
        bool d = ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyDown(ImGuiKey_GamepadLStickDown) ||
                 ImGui::IsKeyDown(ImGuiKey_DownArrow) || (in.letters && ImGui::IsKeyDown(ImGuiKey_S));
        dir = u == d ? 0 : u ? -1 : 1;
    }
    int delta = list_repeat_steps(&nav.rep, dir, now);
    if (dir) {
        if (single) *single = delta != 0 && nav.rep.first;
    } else if (up != down) {
        delta = up ? -1 : 1;
        if (single) *single = true;
    }
    if (!in.keys) return delta;
    if (in.page_keys) {
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true)) delta -= page;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true)) delta += page;
    }
    if (in.triggers) {
        const ImGuiKey trig[2] = { ImGuiKey_GamepadL2, ImGuiKey_GamepadR2 };
        for (int t = 0; t < 2; t++) {
            int sign = t ? 1 : -1;
            if (ImGui::IsKeyPressed(trig[t], false)) {
                delta += sign * page;
                nav.trig_since[t] = now;
                nav.trig_end[t] = false;
            }
            if (!ImGui::IsKeyDown(trig[t])) nav.trig_since[t] = -1;
            else if (nav.trig_since[t] >= 0 && !nav.trig_end[t] && now - nav.trig_since[t] >= LIST_NAV_END_HOLD) {
                delta = sign * LIST_NAV_ALL;
                nav.trig_end[t] = true;
            }
        }
    }
    if (in.home_end) {
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) delta = -LIST_NAV_ALL;
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) delta = LIST_NAV_ALL;
    }
    if (in.wheel) {
        float w = ImGui::GetIO().MouseWheel;
        if (w > 0) delta -= page;
        if (w < 0) delta += page;
    }
    return delta;
}
#endif

#endif
