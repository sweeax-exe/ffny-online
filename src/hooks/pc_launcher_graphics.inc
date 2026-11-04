// The launcher's Graphics card and dialog: "Enhanced graphics (community)", the ReShade / DLSS pack with
// presets by Marleymarletto (included by pc_ui.cpp after the Music dialog; the same glass). The card says
// whether it is on and which preset; the dialog turns it on (download, about 420 MB, with progress),
// picks Performance (default) or Quality, and turns it off in one click, always. Only offered on an
// NVIDIA RTX adapter; elsewhere the switch is disabled with the reason. The work is graphics_mod*.cpp.
//
// Tests and pictures: RECOMP_LAUNCHER_PAGE=graphics opens it; RECOMP_TEST_GRAPHICS steps
// "<frame>:<action>[:<argument>]" separated by ';': on:<preset>, preset:<preset>, off.

#include "graphics_mod.h"

namespace {

struct GraphicsDialog
{
    bool started = false;
    struct Step { int frame; std::string action, arg; bool done; };
    std::vector<Step> steps;
};
GraphicsDialog g_gd;

std::string gfx_setting_preset() { return recomp_settings_get("graphics", "preset", 0) ? "quality" : "performance"; }
bool gfx_setting_on() { return recomp_settings_get("graphics", "enabled", 0) != 0; }

void gfx_start()
{
    if (g_gd.started) return;
    g_gd.started = true;
    gfx::launcher_starts();
}

void gfx_test_steps()
{
    static bool parsed;
    if (!parsed) {
        parsed = true;
        const char *text = getenv("RECOMP_TEST_GRAPHICS");
        std::string all = text ? text : "";
        size_t at = 0;
        while (at < all.size()) {
            size_t end = all.find(';', at);
            if (end == std::string::npos) end = all.size();
            std::string item = all.substr(at, end - at);
            size_t a = item.find(':'), b = a == std::string::npos ? a : item.find(':', a + 1);
            if (a != std::string::npos)
                g_gd.steps.push_back({atoi(item.c_str()), item.substr(a + 1, b == std::string::npos ? b : b - a - 1),
                                      b == std::string::npos ? "" : item.substr(b + 1), false});
            at = end + 1;
        }
    }
    for (auto &s : g_gd.steps) {
        if (s.done || ImGui::GetFrameCount() < s.frame) continue;
        s.done = true;
        fprintf(stderr, "[GRAPHICS] launcher test step %s %s\n", s.action.c_str(), s.arg.c_str());
        if (s.action == "on") gfx::start_enable(s.arg.empty() ? gfx_setting_preset() : s.arg);
        else if (s.action == "preset") gfx::start_switch(s.arg);
        else if (s.action == "off") gfx::turn_off();
    }
}

void graphics_card(ImVec2 a, ImVec2 b, float s)
{
    gfx_start();
    auto v = gfx::view();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    glass(dl, a, b, 14 * s);
    card_title(dl, a, s, "GRAPHICS");
    ImVec2 at(a.x + 20 * s, a.y + 40 * s);
    char head[96], line[200];
    const char *preset = (v.installed ? v.preset : gfx_setting_preset()) == "quality" ? "Quality" : "Performance";
    if (v.installed) snprintf(head, sizeof(head), "Enhanced: %s", preset);
    else if (v.busy) snprintf(head, sizeof(head), "Enhanced graphics");
    else snprintf(head, sizeof(head), "Original graphics");
    if (v.busy && !v.progress.empty()) snprintf(line, sizeof(line), "%s", v.progress.c_str());
    else if (v.restart_needed) snprintf(line, sizeof(line), "Close and reopen the launcher to apply");
    else if (v.installed) snprintf(line, sizeof(line), "Community ReShade + DLSS presets");
    else snprintf(line, sizeof(line), "Optional community presets");    // ", optional" was cut off
    status_dot(dl, at, s, v.installed ? k_red_hi : v.busy ? k_gold : ImVec4(0.6f, 0.6f, 0.64f, 1));
    float right = b.x - 124 * s;
    dl->PushClipRect(a, ImVec2(right, b.y), true);
    card_line(dl, ImVec2(at.x + 18 * s, at.y), s, k_white, head, true);
    card_line(dl, ImVec2(at.x, at.y + 26 * s), s, v.restart_needed ? k_gold : ImVec4(0.80f, 0.80f, 0.82f, 1), line, false);
    dl->PopClipRect();
    if (glass_button("##graphics_card", "Graphics", ImVec2(b.x - 112 * s, a.y + 40 * s), ImVec2(92 * s, 30 * s), s,
                     GLASS_PLAIN, bold_font(), 14 * s))
        ImGui::OpenPopup("Graphics");
}

} // namespace

static void graphics_dialog(ImVec2 o, float w, float h, float s)
{
    {
        static bool asked;
        const char *want = getenv("RECOMP_LAUNCHER_PAGE");
        if (!asked && want && !strcmp(want, "graphics") && !ImGui::IsPopupOpen("Graphics")) ImGui::OpenPopup("Graphics");
        asked = true;
    }
    ImVec2 size(std::min(w - 40 * s, 760 * s), std::min(h - 40 * s, 600 * s));
    ImGui::SetNextWindowPos(ImVec2(o.x + (w - size.x) / 2, o.y + (h - size.y) / 2));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginPopupModal("Graphics", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) return;
    gfx_test_steps();
    auto v = gfx::view();
    std::string adapter, why;
    static int gpu = -1;
    static std::string gpu_why, gpu_name;
    if (gpu < 0) gpu = gfx::gpu_supported(&gpu_name, &gpu_why) ? 1 : 0;
    bool on = v.installed || (v.busy && gfx_setting_on());
    std::string preset = v.installed ? v.preset : gfx_setting_preset();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), b(a.x + size.x, a.y + size.y);
    glass(dl, a, b, 16 * s, k_glass, 0.80f);
    dl->AddText(display_font(), 30 * s, ImVec2(a.x + 30 * s, a.y + 18 * s), col(k_white), "GRAPHICS");
    dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(a.x + 30 * s, a.y + 58 * s), col(k_white, 0.6f),
                "Optional community presets: ReShade and DLSS on top of the game. Visual only; online play is unaffected.");
    float lx = a.x + 30 * s, inner = size.x - 60 * s, y = a.y + 96 * s;

    // The switch: a row like a setting, ON / OFF on the right.
    {
        ImVec2 ra(lx, y), rb(lx + inner, y + 64 * s);
        dl->AddRectFilled(ra, rb, col(k_white, 0.05f), 12 * s);
        dl->AddRect(ra, rb, col(k_white, 0.06f), 12 * s);
        dl->AddText(bold_font(), 17 * s, ImVec2(ra.x + 18 * s, ra.y + 11 * s), col(gpu ? k_white : ImVec4(1, 1, 1, 0.5f)),
                    "Enhanced graphics (community)");
        std::string sub = gpu ? (on ? "On  \xC2\xB7  " + std::string(preset == "quality" ? "Quality" : "Performance") +
                                          (v.version.empty() ? "" : "  \xC2\xB7  pack " + v.version)
                                    : "Off  \xC2\xB7  turning it on downloads about 420 MB once")
                              : gpu_why;
        dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(ra.x + 18 * s, ra.y + 36 * s), col(gpu ? ImVec4(0.80f, 0.80f, 0.82f, 1) : k_gold),
                    sub.c_str());
        ImVec2 ba(rb.x - 150 * s, ra.y + 14 * s);
        if (on || v.installed) {
            // Turn off works always: also without an RTX card, while a download runs, or with the game
            // having started without the pack.
            if (glass_button("##gfx_off", "Turn off", ba, ImVec2(136 * s, 36 * s), s, GLASS_PLAIN, bold_font(), 14.5f * s))
                gfx::turn_off();
        } else if (glass_button("##gfx_on", "Turn on", ba, ImVec2(136 * s, 36 * s), s, GLASS_RED, bold_font(), 14.5f * s,
                                gpu && !v.busy)) {
            gfx::start_enable(preset);
        }
        y += 74 * s;
    }
    dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(lx, y), col(k_gold),
                "Heavy on the GPU: made for strong NVIDIA RTX cards. If the game stutters, switch to Performance or turn it off.");
    y += 34 * s;

    // The preset.
    dl->AddText(display_font(), 17 * s, ImVec2(lx, y), col(ImVec4(0.86f, 0.84f, 0.80f, 1)), "PRESET");
    y += 28 * s;
    {
        static const char *const ids[2] = {"performance", "quality"};
        static const char *const labels[2] = {"Performance", "Quality"};
        static const char *const hints[2] = {"50% resolution, lighter. For RTX 30 and 40 series.",
                                             "Full detail at 1080p. For RTX 5070-class cards."};
        float bw = (inner - 16 * s) / 2;
        for (int i = 0; i < 2; i++) {
            ImVec2 pa(lx + i * (bw + 16 * s), y);
            char id[24];
            snprintf(id, sizeof(id), "##gfx_preset%d", i);
            if (glass_button(id, labels[i], pa, ImVec2(bw, 40 * s), s, preset == ids[i] ? GLASS_RED : GLASS_PLAIN, bold_font(),
                             15 * s, gpu && !v.busy) && preset != ids[i])
                gfx::start_switch(ids[i]);
            dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(pa.x + 4 * s, y + 48 * s), col(k_white, 0.55f), hints[i]);
        }
        y += 80 * s;
    }

    // The work in progress.
    if (v.busy) {
        dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(lx, y), col(k_white, 0.85f), v.progress.c_str());
        ImVec2 pa(lx, y + 24 * s), pb(lx + inner, y + 30 * s);
        dl->AddRectFilled(pa, pb, col(k_white, 0.10f), 3 * s);
        if (v.fraction > 0)
            dl->AddRectFilled(pa, ImVec2(pa.x + (pb.x - pa.x) * (float)std::min(v.fraction, 1.0), pb.y), col(k_red_hi), 3 * s);
    }
    y += 48 * s;

    // Credits and the notices.
    dl->AddText(display_font(), 17 * s, ImVec2(lx, y), col(ImVec4(0.86f, 0.84f, 0.80f, 1)), "CREDITS");
    y += 26 * s;
    dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(lx, y), col(k_white, 0.75f), v.credits.c_str(), nullptr, inner - 170 * s);
    {
        std::error_code ec;
        std::string notices = v.notices.empty() ? "THIRD-PARTY-NOTICES.txt" : v.notices;
        gfx::fs::path p = gfx::bin_folder() / gfx::fs::u8path(notices);
        bool have = gfx::fs::exists(p, ec);
        if (glass_button("##gfx_notices", "Open notices", ImVec2(lx + inner - 150 * s, y - 6 * s), ImVec2(150 * s, 32 * s), s,
                         GLASS_PLAIN, bold_font(), 13.5f * s, have)) {
            pcui::open_path(pcui::path_utf8(p));
        }
    }

    // The foot: what happened, or that it applies at the next start.
    {
        float foot = 64 * s;
        std::string text = v.busy ? std::string() : v.message;
        ImVec4 c = v.tone == 1 ? k_green : v.tone == 2 ? k_gold : ImVec4(1, 1, 1, 0.75f);
        if (text.empty() && v.restart_needed) { text = "Applies the next time you start FFNY Online."; c = k_gold; }
        if (!text.empty()) {
            float wrap = size.x - 60 * s - 170 * s;
            dl->PushClipRect(a, ImVec2(b.x - 170 * s, b.y), true);
            float th = text_size(ImGui::GetFont(), 13.5f * s, text.c_str(), wrap).y;
            dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(a.x + 30 * s, b.y - foot / 2 - th / 2), col(c), text.c_str(),
                        nullptr, wrap);
            dl->PopClipRect();
        }
        if (glass_button("##gfx_done", "DONE", ImVec2(b.x - 30 * s - 130 * s, b.y - foot / 2 - 19 * s), ImVec2(130 * s, 38 * s),
                         s, GLASS_RED, display_font(), 18 * s) ||
            (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)))
            ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
