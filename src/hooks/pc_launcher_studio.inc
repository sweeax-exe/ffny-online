// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The launcher's FFNY Studio card (included by pc_ui.cpp after the Content page; the same glass). It sits in
// the left column between the logo and PLAY and shows one button for whichever applies: "Get FFNY Studio" with
// the download's size, the download's progress, "Open FFNY Studio", or "Update Studio" when the published build
// is newer; a failure is one line in the card. The work is studio_app.cpp (studio_app.h).
//
// The card stays hidden until the update server publishes Studio (a signed studio-latest.json) or Studio is
// installed; RECOMP_STUDIO_BUTTON=1 shows it anyway (pictures of the empty state). Windows only.
// Tests: RECOMP_TEST_STUDIO steps "<frame>:<action>" separated by ';': press, check.

#include "studio_app.h"

namespace {

void studio_test_steps()
{
    struct Step { int frame; std::string action; bool done; };
    static std::vector<Step> steps;
    static bool parsed;
    if (!parsed) {
        parsed = true;
        const char *text = getenv("RECOMP_TEST_STUDIO");
        std::string all = text ? text : "";
        size_t at = 0;
        while (at < all.size()) {
            size_t end = all.find(';', at);
            if (end == std::string::npos) end = all.size();
            std::string item = all.substr(at, end - at);
            size_t colon = item.find(':');
            if (colon != std::string::npos) steps.push_back({atoi(item.c_str()), item.substr(colon + 1), false});
            at = end + 1;
        }
    }
    for (auto &st : steps) {
        if (st.done || ImGui::GetFrameCount() < st.frame) continue;
        st.done = true;
        fprintf(stderr, "[STUDIO] launcher test step %s\n", st.action.c_str());
        if (st.action == "press") studio::press();
        else if (st.action == "check") studio::check();
    }
}

// The card's icon: a pencil over a small red tile.
void studio_icon(ImDrawList *dl, ImVec2 a, float size, float s)
{
    ImVec2 b(a.x + size, a.y + size);
    glass(dl, a, b, 9 * s, k_red, 0.86f);
    ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
    float r = size * 0.36f, w = r * 0.24f;
    ImVec2 u(0.7071f, -0.7071f), n(0.7071f, 0.7071f);
    auto at = [&](float along, float side) { return ImVec2(c.x + u.x * along * r + n.x * side * w, c.y + u.y * along * r + n.y * side * w); };
    ImU32 ink = col(k_white), soft = col(k_white, 0.55f);
    dl->AddQuadFilled(at(-0.42f, -1), at(0.62f, -1), at(0.62f, 1), at(-0.42f, 1), ink);            // the body
    dl->AddQuadFilled(at(0.70f, -1), at(0.95f, -1), at(0.95f, 1), at(0.70f, 1), ink);              // the end
    dl->AddTriangleFilled(at(-0.52f, -1), at(-0.52f, 1), at(-0.95f, 0), soft);                     // the tip
    dl->AddLine(ImVec2(a.x + size * 0.24f, b.y - size * 0.2f), ImVec2(a.x + size * 0.56f, b.y - size * 0.2f), soft, 1.4f * s);
}

void studio_card(ImVec2 a, ImVec2 b, float s)
{
    studio_test_steps();
    studio::launcher_starts();
    studio::View v = studio::view();
    const char *forced = getenv("RECOMP_STUDIO_BUTTON");
    bool force = forced && !strcmp(forced, "1");
    if (!v.supported || (!force && !v.installed && !v.available && !v.busy)) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    glass(dl, a, b, 14 * s);
    float icon = 44 * s;
    studio_icon(dl, ImVec2(a.x + 16 * s, a.y + 16 * s), icon, s);
    float x = a.x + 16 * s + icon + 14 * s, right = b.x - 14 * s;
    dl->AddText(display_font(), 17 * s, ImVec2(x, a.y + 11 * s), col(ImVec4(0.86f, 0.84f, 0.80f, 1)), "FFNY STUDIO");

    char line[200];
    ImVec4 tone(0.80f, 0.80f, 0.82f, 1);
    if (v.busy) {
        snprintf(line, sizeof(line), "%s", v.progress.empty() ? "Working..." : v.progress.c_str());
    } else if (!v.error.empty()) {
        snprintf(line, sizeof(line), "%s", v.error.c_str());
        tone = k_error;
    } else if (!v.message.empty()) {
        snprintf(line, sizeof(line), "%s", v.message.c_str());
        tone = k_green;
    } else if (v.update) {
        snprintf(line, sizeof(line), "%s is out (you have %s)", v.version.c_str(), v.installed_version.c_str());
        tone = k_gold;
    } else if (v.installed) {
        snprintf(line, sizeof(line), "Version %s \xC2\xB7 fighters, clothing, packs", v.installed_version.c_str());
    } else {
        snprintf(line, sizeof(line), "Make fighters, clothing and packs");
    }
    dl->PushClipRect(ImVec2(x, a.y), ImVec2(right, b.y), true);
    dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(x, a.y + 33 * s), col(tone), line);
    dl->PopClipRect();

    ImVec2 ba(x, a.y + 54 * s), bs(right - x, 28 * s);
    if (v.busy) {
        // the progress instead of the button
        ImVec2 ta(x, ba.y + 9 * s), tb(right, ba.y + 17 * s);
        dl->AddRectFilled(ta, tb, col(k_white, 0.10f), 4 * s);
        if (v.fraction >= 0) {
            float f = (float)std::min(1.0, std::max(0.0, v.fraction));
            dl->AddRectFilled(ta, ImVec2(ta.x + (tb.x - ta.x) * f, tb.y), col(k_red_hi), 4 * s);
        } else {
            float t = (float)fmod(ImGui::GetTime() * 0.8, 1.0), wseg = (tb.x - ta.x) * 0.3f;
            float x0 = ta.x + (tb.x - ta.x + wseg) * t - wseg;
            dl->PushClipRect(ta, tb, true);
            dl->AddRectFilled(ImVec2(x0, ta.y), ImVec2(x0 + wseg, tb.y), col(k_red_hi), 4 * s);
            dl->PopClipRect();
        }
        return;
    }
    char label[96];
    GlassKind kind = GLASS_PLAIN;
    if (v.update) { snprintf(label, sizeof(label), "Update Studio"); kind = GLASS_RED; }
    else if (v.installed) snprintf(label, sizeof(label), "Open FFNY Studio");
    else if (v.available) {
        snprintf(label, sizeof(label), "Get FFNY Studio (%.0f MB)", v.size / 1048576.0 < 1 ? 1.0 : v.size / 1048576.0);
    } else snprintf(label, sizeof(label), "Get FFNY Studio");
    bool pressed = glass_button("##studio_card", label, ba, bs, s, kind, bold_font(), 13.5f * s);
    if (ImGui::IsItemHovered()) studio::check();        // throttled: at most every two minutes
    if (pressed) studio::press();
}

} // namespace
