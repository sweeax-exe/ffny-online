// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The input delay line (build 76, [online] delay_display), included by online_screen.cpp inside its
// file-local namespace (online_screen::delay_hud_wanted / draw_delay_hud call in). In an online fight a
// small line at the bottom centre ("Rollback · 3 frames", or "No rollback (path 140 ms) · 10 frames"); the room screen draws the same line, as an estimate, before READY
// (delay_room_line). Drawing only: nothing is read from the pads, nothing goes back to the game. The
// bottom centre stays clear of the HUD's rows (the top, and the bottom corners with 3 or 4 fighters) and
// of the name tags over them.
//
// Tests: RECOMP_TEST_DELAY_LINE=<rollback>,<frames>,<path ms> draws that line in any fight, offline too
// (the line's picture without two copies).

struct DelayHud {
    NetDelayInfo info{};
    bool up = false;
    int test = -1;          // -1 not read, 0 none, 1 the test line
    NetDelayInfo test_info{};
} DH;

bool dh_setting() { return recomp_settings_get("online", "delay_display", 1) != 0; }

void dh_test_read()
{
    if (DH.test >= 0) return;
    DH.test = 0;
    const char *v = getenv("RECOMP_TEST_DELAY_LINE");
    int rb = 0, frames = 0, path = 0;
    if (v && sscanf(v, "%d,%d,%d", &rb, &frames, &path) >= 2) {
        DH.test = 1;
        DH.test_info.in_fight = 1;
        DH.test_info.rollback = rb != 0;
        DH.test_info.frames = frames;
        DH.test_info.path_ms = path;
        fprintf(stderr, "[DELAYHUD] RECOMP_TEST_DELAY_LINE (tests): rollback %d, %d frames, path %d ms\n", rb, frames, path);
    }
}

// The line in the menu's 640x480 space: a faint dark pill, the words in the game's condensed face.
void dh_line(ImDrawList *dl, const Frame &f, float cx, float y, const NetDelayInfo &i, float alpha)
{
    char line[96];
    netdelay_line(line, (int)sizeof(line), i.rollback, i.frames, i.path_ms, i.estimate);
    ImFont *font = font_or_default(g_cond);
    const float size = 12;
    float w = font->CalcTextSizeA(size * f.s(), FLT_MAX, 0, line).x / f.sx;
    rect(dl, f, cx - w / 2 - 9, y - 2, w + 18, size + 5, with_alpha(IM_COL32(8, 8, 11, 255), 0.45f * alpha), 6);
    ImU32 dot = i.rollback ? GREEN : ORANGE;
    rect(dl, f, cx - w / 2 - 4, y + size / 2 - 1.0f, 3, 3, with_alpha(dot, alpha), 1.5f);
    text(dl, f, font, size, cx + 3, y, with_alpha(CREAM, 0.85f * alpha), line, CENTER, 0.8f);
}

bool dh_wanted()
{
    dh_test_read();
    DH.up = false;
    if (!dh_setting() || gallery_on()) return false;
    if (DH.test == 1) {
        DH.info = DH.test_info;
        DH.up = netplay_in_fight() != 0;
        return DH.up;
    }
    if (!netplay_in_fight() || !netplay_delay_info(&DH.info) || !DH.info.in_fight) return false;
    DH.up = true;
    return true;
}

void dh_draw(ImDrawList *dl, ImVec2 display)
{
    if (!DH.up) return;
    dh_line(dl, menu_frame(display), 320, 459, DH.info, 1.0f);
}

// The room screen, before READY (the estimate) or while the match is being agreed: right under the hints.
void delay_room_line(ImDrawList *dl, const Frame &f)
{
    NetDelayInfo i;
    if (!dh_setting() || !netplay_delay_info(&i) || i.in_fight) return;
    dh_line(dl, f, 320, 420, i, 0.9f);
}
