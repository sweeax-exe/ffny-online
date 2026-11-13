// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The launcher's Music card and dialog (included by pc_ui.cpp after the Saves dialog, so it draws with
// the same glass, fonts and buttons). The card on the start page says how many songs there are and
// what plays; its button opens the dialog: what plays (My music only / Mix with the original / Original
// only), Shuffle and New song every match at the top, the songs with a Remove each, ADD SONGS and Open music folder, and one
// line at the foot for the background work and what happened. Files dropped on the window are added.
// The work is pc_launcher_music.cpp; the files, music_library.cpp; the game's side, custom_music.cpp.
//
// Tests and pictures: RECOMP_LAUNCHER_PAGE=music opens it at start; RECOMP_TEST_MUSIC runs steps
// "<frame>:<action>[:<argument>]" separated by ';': add:<path> (as if picked or dropped), mode:<auto|
// mine|mix|original>, shuffle:<0|1>, permatch:<0|1>, remove:<row>, refresh, sort:<custom|name|artist|added>,
// move:<row>,<delta> (the row as shown).

#include "pc_launcher_music.h"

namespace {

struct MusicDialog
{
    bool opened_once = false;
    bool picking = false;
    bool open_request = false;
    struct Step { int frame; std::string action, arg; bool done; };
    std::vector<Step> steps;
};
MusicDialog g_md;

const char *const k_music_modes[] = {"auto", "mine", "mix", "original"};
const char *const k_music_sorts[] = {"custom", "name", "artist", "added"};      // musiclib::Sort

int music_mode_setting() { return recomp_settings_get("music", "mode", musiclib::MODE_AUTO); }

void music_set_mode(int mode)
{
    if (recomp_settings_set("music", "mode", mode) > 0) {
        commit();
        fprintf(stderr, "[MUSIC] launcher: mode %s\n", k_music_modes[mode]);
    }
}

size_t music_playable(const pcmusic::View &v)
{
    size_t n = 0;
    for (auto &s : v.songs) n += s.state != musiclib::BAD;
    return n;
}

std::string music_length(double seconds)
{
    if (seconds < 0) return "";
    char t[32];
    int s = (int)(seconds + 0.5);
    snprintf(t, sizeof(t), "%d:%02d", s / 60, s % 60);
    return t;
}

std::string music_kind(const musiclib::Song &s)
{
    std::string ext = musiclib::utf8(s.path.extension());
    for (auto &c : ext) c = (char)toupper((unsigned char)c);
    return ext.size() > 1 ? ext.substr(1) : "";
}

void music_test_steps()
{
    static bool parsed;
    if (!parsed) {
        parsed = true;
        const char *text = getenv("RECOMP_TEST_MUSIC");
        std::string all = text ? text : "";
        size_t at = 0;
        while (at < all.size()) {
            size_t end = all.find(';', at);
            if (end == std::string::npos) end = all.size();
            std::string item = all.substr(at, end - at);
            size_t a = item.find(':'), b = a == std::string::npos ? a : item.find(':', a + 1);
            if (a != std::string::npos)
                g_md.steps.push_back({atoi(item.c_str()), item.substr(a + 1, b == std::string::npos ? b : b - a - 1),
                                      b == std::string::npos ? "" : item.substr(b + 1), false});
            at = end + 1;
        }
    }
    for (auto &s : g_md.steps) {
        if (s.done || ImGui::GetFrameCount() < s.frame) continue;
        s.done = true;
        fprintf(stderr, "[MUSIC] launcher test step %s %s\n", s.action.c_str(), s.arg.c_str());
        if (s.action == "add") {
            std::vector<std::string> paths;
            size_t at = 0;
            while (at <= s.arg.size()) {            // several paths separated by '|'
                size_t end = s.arg.find('|', at);
                if (end == std::string::npos) end = s.arg.size();
                if (end > at) paths.push_back(s.arg.substr(at, end - at));
                at = end + 1;
            }
            pcmusic::add(paths);
        } else if (s.action == "mode") {
            for (int i = 0; i < 4; i++) if (s.arg == k_music_modes[i]) music_set_mode(i);
        } else if (s.action == "shuffle") {
            pcmusic::set_shuffle(atoi(s.arg.c_str()) != 0);
        } else if (s.action == "permatch") {
            if (recomp_settings_set("music", "new_song_per_match", atoi(s.arg.c_str()) ? 1 : 0) > 0) commit();
        } else if (s.action == "remove") {
            auto list = pcmusic::sorted(pcmusic::view());
            int row = atoi(s.arg.c_str());
            if (row >= 0 && row < (int)list.size()) pcmusic::remove(list[row]);
        } else if (s.action == "refresh") {
            pcmusic::refresh();
        } else if (s.action == "sort") {
            for (int i = 0; i < musiclib::SORT_COUNT; i++) if (s.arg == k_music_sorts[i]) pcmusic::set_sort(i);
        } else if (s.action == "move") {
            auto list = pcmusic::sorted(pcmusic::view());
            int row = atoi(s.arg.c_str()), delta = s.arg.find(',') != std::string::npos ? atoi(s.arg.c_str() + s.arg.find(',') + 1) : 1;
            if (row >= 0 && row < (int)list.size()) pcmusic::move(list[row].rel, delta);
        }
    }
}

// The start page's card: how many songs, what plays, and the button to the dialog.
void music_card(ImVec2 a, ImVec2 b, float s)
{
    if (!g_md.opened_once) {
        g_md.opened_once = true;
        pcmusic::refresh();
    }
    auto v = pcmusic::view();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    glass(dl, a, b, 14 * s);
    card_title(dl, a, s, "MUSIC");
    ImVec2 at(a.x + 20 * s, a.y + 40 * s);
    size_t n = music_playable(v);
    auto mode = musiclib::effective_mode(music_mode_setting(), n);
    char head[96], line[160];
    if (!v.loaded) snprintf(head, sizeof(head), "Your music");
    else if (!n) snprintf(head, sizeof(head), "Play to your own songs");
    else snprintf(head, sizeof(head), n == 1 ? "1 song" : "%zu songs", n);
    if (v.busy && !v.progress.empty()) snprintf(line, sizeof(line), "%s", v.progress.c_str());
    else if (!n) snprintf(line, sizeof(line), "Add MP3, OGG, FLAC or WAV files");
    else snprintf(line, sizeof(line), "%s", mode == musiclib::MODE_MINE ? "My music only" :
                                         mode == musiclib::MODE_MIX ? "Mixed with the original soundtrack" :
                                                                      "Original soundtrack only");
    status_dot(dl, at, s, n && mode != musiclib::MODE_ORIGINAL ? k_red_hi : ImVec4(0.6f, 0.6f, 0.64f, 1));
    float right = b.x - 124 * s;
    dl->PushClipRect(a, ImVec2(right, b.y), true);
    card_line(dl, ImVec2(at.x + 18 * s, at.y), s, k_white, head, true);
    card_line(dl, ImVec2(at.x, at.y + 26 * s), s, ImVec4(0.80f, 0.80f, 0.82f, 1), line, false);
    dl->PopClipRect();
    if (glass_button("##music_card", n ? "Music" : "Add songs", ImVec2(b.x - 112 * s, a.y + 40 * s),
                     ImVec2(92 * s, 30 * s), s, n ? GLASS_PLAIN : GLASS_RED, bold_font(), 14 * s))
        ImGui::OpenPopup("Music");
}

void music_add_from_picker()
{
    void *window = nullptr;
#if defined(_WIN32)
    window = ImGui::GetMainViewport()->PlatformHandleRaw;
#endif
    g_md.picking = true;
    pcmusic::pick_start(window);
}

// An arrow button (up or down) of a song's row: a small glass button with a triangle on it.
bool music_arrow(const char *id, ImVec2 a, float size, float s, bool up, bool enabled)
{
    bool pressed = glass_button(id, "", a, ImVec2(size, size), s, GLASS_PLAIN, bold_font(), 13 * s, enabled);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float cx = a.x + size / 2, cy = a.y + size / 2, r = size * 0.22f;
    ImU32 c = col(k_white, enabled ? 0.92f : 0.25f);
    if (up) dl->AddTriangleFilled(ImVec2(cx, cy - r), ImVec2(cx + r * 1.15f, cy + r * 0.8f), ImVec2(cx - r * 1.15f, cy + r * 0.8f), c);
    else dl->AddTriangleFilled(ImVec2(cx - r * 1.15f, cy - r * 0.8f), ImVec2(cx + r * 1.15f, cy - r * 0.8f), ImVec2(cx, cy + r), c);
    return pressed;
}

// One song: its name, the artist, length and kind or why it cannot be played, up and down, and Remove.
void music_rows(float w, float h, float s, const pcmusic::View &v)
{
    const std::vector<musiclib::Song> list = pcmusic::sorted(v);       // as [music] sort says
    ImGui::BeginChild("##songs", ImVec2(w, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float row_h = 50 * s, gap = 6 * s, rw = ImGui::GetContentRegionAvail().x;
    if (v.songs.empty()) {
        ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + rw - 2 * s, a.y + std::max(150 * s, h - 8 * s));
        a.x += 1 * s;
        a.y += 1 * s;
        dl->AddRectFilled(a, b, col(k_white, 0.04f), 12 * s);
        // a dashed outline: the place to drop files
        float dash = 9 * s, r = 12 * s;
        for (float x = a.x + r; x < b.x - r; x += 2 * dash) {
            dl->AddLine(ImVec2(x, a.y), ImVec2(std::min(x + dash, b.x - r), a.y), col(k_white, 0.22f), 1.2f * s);
            dl->AddLine(ImVec2(x, b.y), ImVec2(std::min(x + dash, b.x - r), b.y), col(k_white, 0.22f), 1.2f * s);
        }
        for (float y = a.y + r; y < b.y - r; y += 2 * dash) {
            dl->AddLine(ImVec2(a.x, y), ImVec2(a.x, std::min(y + dash, b.y - r)), col(k_white, 0.22f), 1.2f * s);
            dl->AddLine(ImVec2(b.x, y), ImVec2(b.x, std::min(y + dash, b.y - r)), col(k_white, 0.22f), 1.2f * s);
        }
        const char *big = v.loaded ? "NO SONGS YET" : "READING YOUR MUSIC...";
        const char *small1 = "Press ADD SONGS, or drop music files on this window.";
        const char *small2 = "MP3, OGG, FLAC and WAV. They play in menus and fights, on your PC only.";
        float cy = (a.y + b.y) / 2;
        text_center(dl, display_font(), 26 * s, ImVec2(a.x, cy - 44 * s), ImVec2(b.x, cy - 10 * s), col(k_white, 0.85f), big);
        text_center(dl, ImGui::GetFont(), 14 * s, ImVec2(a.x, cy - 2 * s), ImVec2(b.x, cy + 18 * s), col(k_white, 0.62f), small1);
        text_center(dl, ImGui::GetFont(), 13 * s, ImVec2(a.x, cy + 20 * s), ImVec2(b.x, cy + 40 * s), col(k_white, 0.45f), small2);
        ImGui::Dummy(ImVec2(rw, b.y - a.y));
        ImGui::EndChild();
        return;
    }
    ImGuiListClipper clip;
    clip.Begin((int)list.size(), row_h + gap);
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
            auto &song = list[i];
            ImVec2 a = ImGui::GetCursorScreenPos(), z(a.x + rw, a.y + row_h);
            bool bad = song.state == musiclib::BAD;
            dl->AddRectFilled(a, z, col(k_white, 0.05f), 10 * s);
            dl->AddRect(a, z, col(k_white, 0.06f), 10 * s);
            // the number in the list, like a track list
            char num[16];
            snprintf(num, sizeof(num), "%02d", i + 1);
            dl->AddText(display_font(), 18 * s, ImVec2(a.x + 14 * s, a.y + (row_h - 18 * s) / 2), col(bad ? k_gold : k_red_hi, 0.9f), num);
            std::string detail;
            if (bad) detail = "Cannot be played: " + song.why;
            else {
                detail = song.artist;
                std::string length = music_length(song.seconds);
                if (!length.empty()) detail += (detail.empty() ? "" : "  Â·  ") + length;
                std::string kind = music_kind(song);
                if (!kind.empty()) detail += (detail.empty() ? "" : "  \xC2\xB7  ") + kind;
                if (song.rel.find('/') != std::string::npos)
                    detail += "  \xC2\xB7  " + song.rel.substr(0, song.rel.rfind('/'));
                if (song.state == musiclib::UNCHECKED) detail += "  \xC2\xB7  checking...";
            }
            dl->PushClipRect(a, ImVec2(z.x - 172 * s, z.y), true);
            dl->AddText(bold_font(), 15.5f * s, ImVec2(a.x + 50 * s, a.y + 7 * s), col(k_white, bad ? 0.6f : 1.0f), song.name.c_str());
            dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(a.x + 50 * s, a.y + 28 * s), col(bad ? k_gold : ImVec4(0.80f, 0.80f, 0.82f, 1)),
                        detail.c_str());
            dl->PopClipRect();
            ImGui::PushID(i);
            // up and down: in a sorted list the shown order becomes your own order first
            if (music_arrow("##up", ImVec2(z.x - 164 * s, a.y + 11 * s), 28 * s, s, true, i > 0))
                pcmusic::move(song.rel, -1);
            if (music_arrow("##down", ImVec2(z.x - 132 * s, a.y + 11 * s), 28 * s, s, false, i + 1 < (int)list.size()))
                pcmusic::move(song.rel, 1);
            if (glass_button("##remove", "Remove", ImVec2(z.x - 92 * s, a.y + 11 * s), ImVec2(80 * s, 28 * s), s, GLASS_PLAIN,
                             bold_font(), 13 * s))
                pcmusic::remove(song);
            ImGui::PopID();
            ImGui::SetCursorScreenPos(a);
            ImGui::Dummy(ImVec2(rw, row_h + gap));
        }
    }
    ImGui::EndChild();
}

} // namespace

static void music_dialog(ImVec2 o, float w, float h, float s)
{
    {
        std::vector<std::string> paths;
        if (pcmusic::take_dropped(paths)) {
            fprintf(stderr, "[MUSIC] launcher: %zu file(s) dropped on the window\n", paths.size());
            pcmusic::add(paths);
            g_md.open_request = true;
        }
        static bool asked;
        const char *want = getenv("RECOMP_LAUNCHER_PAGE");
        if (!asked && want && !strcmp(want, "music")) g_md.open_request = true;
        asked = true;
        if (g_md.open_request && !ImGui::IsPopupOpen("Music")) ImGui::OpenPopup("Music");
        g_md.open_request = false;
    }
    ImVec2 size(std::min(w - 40 * s, 860 * s), std::min(h - 40 * s, 620 * s));
    ImGui::SetNextWindowPos(ImVec2(o.x + (w - size.x) / 2, o.y + (h - size.y) / 2));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginPopupModal("Music", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) return;
    music_test_steps();
    auto v = pcmusic::view();
    size_t playable = music_playable(v);
    int setting = music_mode_setting();
    auto mode = musiclib::effective_mode(setting, playable);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), b(a.x + size.x, a.y + size.y);
    glass(dl, a, b, 16 * s, k_glass, 0.80f);
    dl->AddText(display_font(), 30 * s, ImVec2(a.x + 30 * s, a.y + 18 * s), col(k_white), "MUSIC");
    dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(a.x + 30 * s, a.y + 58 * s), col(k_white, 0.6f),
                "Your own songs as the game's soundtrack. Only you hear them, online too.");
    if (glass_button("##music_folder", "Open music folder", ImVec2(b.x - 30 * s - 170 * s, a.y + 22 * s), ImVec2(170 * s, 32 * s),
                     s, GLASS_PLAIN, bold_font(), 14 * s)) {
        std::error_code ec;
        musiclib::fs::create_directories(pcmusic::folder(), ec);
        pcui::open_path(musiclib::utf8(pcmusic::folder()));
        pcmusic::refresh();         // files put there meanwhile show when the dialog is looked at again
    }

    push_glass_style();
    float lx = a.x + 30 * s, inner = size.x - 60 * s, top = a.y + 92 * s;
    // What plays: three choices side by side, Shuffle, and under it New song every match.
    dl->AddText(display_font(), 17 * s, ImVec2(lx, top), col(ImVec4(0.86f, 0.84f, 0.80f, 1)), "WHAT PLAYS");
    {
        static const char *const labels[3] = {"My music only", "Mix with the original", "Original only"};
        static const int modes[3] = {musiclib::MODE_MINE, musiclib::MODE_MIX, musiclib::MODE_ORIGINAL};
        float tw = 190 * s, bw = (inner - tw - 3 * 10 * s) / 3, by = top + 28 * s;
        for (int i = 0; i < 3; i++) {
            bool on = mode == modes[i];
            char id[24];
            snprintf(id, sizeof(id), "##music_mode%d", i);
            if (glass_button(id, labels[i], ImVec2(lx + i * (bw + 10 * s), by), ImVec2(bw, 36 * s), s, on ? GLASS_RED : GLASS_PLAIN,
                             bold_font(), 14 * s))
                music_set_mode(modes[i]);
        }
        bool shuffle = recomp_settings_get("music", "shuffle", 1) != 0;
        if (glass_button("##music_shuffle", shuffle ? "Shuffle: on" : "Shuffle: off", ImVec2(lx + inner - tw, by),
                         ImVec2(tw, 36 * s), s, shuffle ? GLASS_RED : GLASS_PLAIN, bold_font(), 14 * s)) {
            pcmusic::set_shuffle(!shuffle);
            commit();
        }
        bool per_match = recomp_settings_get("music", "new_song_per_match", 1) != 0;
        if (glass_button("##music_per_match", per_match ? "New song every match: on" : "New song every match: off",
                         ImVec2(lx + inner - tw, by + 40 * s), ImVec2(tw, 28 * s), s, per_match ? GLASS_RED : GLASS_PLAIN,
                         bold_font(), 12.5f * s)) {
            recomp_settings_set("music", "new_song_per_match", per_match ? 0 : 1);
            commit();
        }
        const char *hint = setting == musiclib::MODE_AUTO
                               ? "Automatic: your songs mixed with the original once you add some."
                               : mode == musiclib::MODE_MIX ? "Your songs, with one of the game's own tracks between them."
                               : mode == musiclib::MODE_MINE ? "Only your songs, in menus and in every fight."
                               : playable ? "The game's own soundtrack. Your songs stay in the list."
                                          : "The game's own soundtrack. Add songs to play your own.";
        dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(lx, by + 42 * s), col(k_white, 0.5f), hint);
    }
    // The songs.
    float list_top = top + 104 * s, foot = 64 * s, button_h = 40 * s;
    {
        char title[64];
        if (v.songs.empty()) snprintf(title, sizeof(title), "YOUR SONGS");
        else if (playable == v.songs.size()) snprintf(title, sizeof(title), "YOUR SONGS  \xC2\xB7  %zu", playable);
        else snprintf(title, sizeof(title), "YOUR SONGS  \xC2\xB7  %zu  (%zu cannot play)", playable, v.songs.size() - playable);
        dl->AddText(display_font(), 17 * s, ImVec2(lx, list_top), col(ImVec4(0.86f, 0.84f, 0.80f, 1)), title);
        // the order of the list (and, shuffle off, of the songs): four small buttons on the right
        static const char *const sort_labels[musiclib::SORT_COUNT] = {"Your order", "Name", "Artist", "Added"};
        float sbw = 84 * s, sgap = 6 * s, sx = lx + inner - musiclib::SORT_COUNT * sbw - (musiclib::SORT_COUNT - 1) * sgap;
        int sort = pcmusic::sort_setting();
        for (int i = 0; i < musiclib::SORT_COUNT; i++) {
            char id[24];
            snprintf(id, sizeof(id), "##music_sort%d", i);
            if (glass_button(id, sort_labels[i], ImVec2(sx + i * (sbw + sgap), list_top - 5 * s), ImVec2(sbw, 26 * s), s,
                             sort == i ? GLASS_RED : GLASS_PLAIN, bold_font(), 12.5f * s))
                pcmusic::set_sort(i);
        }
        const char *sort_word = "SORT";
        float sw = text_size(ImGui::GetFont(), 12.5f * s, sort_word).x;
        dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(sx - sw - 10 * s, list_top + 1 * s), col(k_white, 0.5f), sort_word);
        if (v.more) {
            char more[96];
            snprintf(more, sizeof(more), "The first %zu are used; %zu more are left out.", v.songs.size(), v.more);
            float mw = text_size(ImGui::GetFont(), 12.5f * s, more).x;
            dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(sx - sw - 24 * s - mw, list_top + 4 * s), col(k_gold), more);
        }
    }
    float list_h = b.y - foot - button_h - 16 * s - (list_top + 28 * s);
    ImGui::SetCursorScreenPos(ImVec2(lx, list_top + 28 * s));
    music_rows(inner, list_h, s, v);
    float by = list_top + 28 * s + list_h + 12 * s;
    if (glass_button("##music_add", "ADD SONGS", ImVec2(lx, by), ImVec2(200 * s, button_h), s, GLASS_RED, display_font(), 19 * s))
        music_add_from_picker();
    dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(lx + 214 * s, by + (button_h - 12.5f * s) / 2 - 1 * s), col(k_white, 0.45f),
                "or drop files on this window  \xC2\xB7  MP3, OGG, FLAC, WAV");
    {
        std::vector<std::string> paths;
        if (g_md.picking && pcmusic::pick_poll(paths)) {
            g_md.picking = false;
            if (!paths.empty()) pcmusic::add(paths);
        }
    }
    pop_glass_style();

    // The foot: the background work while it runs, else what happened last.
    {
        std::string text = v.busy && !v.progress.empty() ? v.progress : v.message;
        ImVec4 c = v.busy ? k_gold : v.tone == 1 ? k_green : v.tone == 2 ? k_gold : ImVec4(1, 1, 1, 0.75f);
        if (!text.empty()) {
            float wrap = size.x - 60 * s - 170 * s;
            dl->PushClipRect(a, ImVec2(b.x - 170 * s, b.y), true);
            if (v.busy) {           // a small turning mark before the progress line
                float t = (float)ImGui::GetTime() * 6.0f;
                ImVec2 cc(a.x + 36 * s, b.y - foot / 2);
                dl->PathArcTo(cc, 6 * s, t, t + 4.2f, 16);
                dl->PathStroke(col(k_gold), 0, 2 * s);
            }
            float tx = a.x + (v.busy ? 50 : 30) * s;
            float th = text_size(ImGui::GetFont(), 13.5f * s, text.c_str(), wrap).y;
            dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(tx, b.y - foot / 2 - th / 2), col(c), text.c_str(), nullptr, wrap);
            dl->PopClipRect();
        }
    }
    if (glass_button("##music_done", "DONE", ImVec2(b.x - 30 * s - 130 * s, b.y - foot / 2 - 19 * s), ImVec2(130 * s, 38 * s),
                     s, GLASS_RED, display_font(), 18 * s) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
