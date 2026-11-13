// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// MY MUSIC (build 82; included by online_screen.cpp): the player's own songs in the game, in the order they
// play with shuffle off. RB on the online menu opens it. Rows: SORT (your order, name, artist, newest added),
// SHUFFLE, then the songs. A on a song picks it up and up/down moves it (A or B puts it down); LB/RB move
// the chosen song one place; the mouse picks rows and moves songs with the arrows at a row's right. Moving
// a song in a sorted list makes the shown order your own order (the sort goes to YOUR ORDER).
//
// The list is the launcher's (pcmusic: the music folder, read in the background) and the order is saved in
// the music folder's order.txt at once; the game's music player takes the new order for its next song
// (custom_music_order_changed). Host only: nothing here reaches the game, a fight or the other players.
//
// Gallery: "music" (sorted by artist), "music-own" (your order, a song picked up), "music-empty".

struct MusicPage
{
    int sel = 2;                    // 0 SORT, 1 SHUFFLE, 2.. the songs
    int top = 0;                    // the first song shown
    bool grabbed = false;           // A: the chosen song moves with up and down
    unsigned long long opened = 0;
    std::string sel_rel;            // the chosen song, kept across re-sorts and reloads
    // the gallery's sample list and settings (no folder there)
    bool sample = false;
    std::vector<musiclib::Song> songs;
    int sort = 0;
    bool shuffle = false;
};
MusicPage g_mp;

const int MUSIC_ROWS = 8;           // songs shown at once

int music_sort_now() { return g_mp.sample ? g_mp.sort : pcmusic::sort_setting(); }
bool music_shuffle_now() { return g_mp.sample ? g_mp.shuffle : recomp_settings_get("music", "shuffle", 1) != 0; }

std::vector<musiclib::Song> music_list(bool *loaded)
{
    if (g_mp.sample) {
        *loaded = true;
        std::vector<musiclib::Song> v = g_mp.songs;
        musiclib::sort_songs(v, g_mp.sort);
        return v;
    }
    pcmusic::View v = pcmusic::view();
    *loaded = v.loaded;
    return pcmusic::sorted(v);
}

void open_music()
{
    S.page = MUSIC;
    g_mp.grabbed = false;
    g_mp.opened = GetTickCount64();
    if (!g_mp.sample) pcmusic::refresh();       // songs added in the launcher or the folder meanwhile
}

void music_set_sort(int sort)
{
    sort = (sort + musiclib::SORT_COUNT) % musiclib::SORT_COUNT;
    if (g_mp.sample) g_mp.sort = sort;
    else pcmusic::set_sort(sort);
}

void music_set_shuffle(bool on)
{
    if (g_mp.sample) g_mp.shuffle = on;
    else pcmusic::set_shuffle(on);
    say(on ? "Shuffle on: your songs play in a random order." : "Shuffle off: your songs play in this order.");
}

// Moves the chosen song; false at the list's ends.
bool music_move(const std::string &rel, int delta)
{
    if (!g_mp.sample) return pcmusic::move(rel, delta);
    std::vector<musiclib::Song> v = g_mp.songs;
    musiclib::sort_songs(v, g_mp.sort);
    size_t at = 0;
    while (at < v.size() && v[at].rel != rel) at++;
    if (at >= v.size() || musiclib::move_song(v, at, delta) == at) return false;
    g_mp.songs = v;
    g_mp.sort = musiclib::SORT_CUSTOM;
    return true;
}

void music_arrow(ImDrawList *dl, const Frame &f, float cx, float cy, bool up, ImU32 c)
{
    const float r = 4.5f;
    if (up) dl->AddTriangleFilled(f.p(cx, cy - r), f.p(cx + r * 1.2f, cy + r * 0.8f), f.p(cx - r * 1.2f, cy + r * 0.8f), c);
    else dl->AddTriangleFilled(f.p(cx - r * 1.2f, cy - r * 0.8f), f.p(cx + r * 1.2f, cy - r * 0.8f), f.p(cx, cy + r), c);
}

void draw_music(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    static const char *const sort_names[musiclib::SORT_COUNT] = { "YOUR ORDER", "NAME", "ARTIST", "NEWEST ADDED" };
    bool loaded = false;
    std::vector<musiclib::Song> list = music_list(&loaded);
    int sort = music_sort_now();
    bool shuffle = music_shuffle_now();
    int n = (int)list.size(), rows = 2 + n;
    // the chosen song follows a re-sort, a move or a reload
    if (g_mp.sel >= 2 && !g_mp.sel_rel.empty()) {
        for (int i = 0; i < n; i++)
            if (list[i].rel == g_mp.sel_rel) g_mp.sel = 2 + i;
    }
    if (g_mp.sel >= rows) g_mp.sel = rows - 1;
    if (g_mp.sel < 0) g_mp.sel = 0;
    bool armed = GetTickCount64() - g_mp.opened > 400;     // the RB that opened the page moves nothing
    auto chosen = [&]() -> const musiclib::Song * { return g_mp.sel >= 2 && g_mp.sel - 2 < n ? &list[g_mp.sel - 2] : nullptr; };
    if (!chosen()) g_mp.grabbed = false;

    // the pad
    if (g_mp.grabbed && chosen()) {
        int delta = pad.up ? -1 : pad.down ? 1 : 0;
        if (delta) music_move(chosen()->rel, delta);
        if (pad.a || pad.b) {
            g_mp.grabbed = false;
            const_cast<Pad &>(pad).b = false;           // B puts the song down; it does not leave the page
        }
    } else {
        if (pad.up) g_mp.sel = (g_mp.sel + rows - 1) % rows;
        if (pad.down) g_mp.sel = (g_mp.sel + 1) % rows;
        if (g_mp.sel == 0 && (pad.left || pad.right || pad.a)) music_set_sort(sort + (pad.left ? -1 : 1));
        if (g_mp.sel == 1 && (pad.left || pad.right || pad.a)) music_set_shuffle(!shuffle);
        if (g_mp.sel >= 2 && pad.a && chosen()) g_mp.grabbed = true;
        if (armed && chosen() && (pad.lb || pad.rb)) music_move(chosen()->rel, pad.lb ? -1 : 1);
    }
    g_mp.sel_rel = chosen() ? chosen()->rel : "";
    // the chosen song re-found after a move
    if (!g_mp.sel_rel.empty()) {
        list = music_list(&loaded);
        sort = music_sort_now();
        for (int i = 0; i < n; i++)
            if (list[i].rel == g_mp.sel_rel) g_mp.sel = 2 + i;
    }
    // keep the chosen song in view; the mouse wheel scrolls
    if (g_mp.sel >= 2) {
        int si = g_mp.sel - 2;
        if (si < g_mp.top) g_mp.top = si;
        if (si >= g_mp.top + MUSIC_ROWS) g_mp.top = si - MUSIC_ROWS + 1;
    }
    if (mouse_in(f, 36, 154, 290, MUSIC_ROWS * 29.0f)) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) g_mp.top -= wheel > 0 ? 1 : -1;
    }
    g_mp.top = std::max(0, std::min(g_mp.top, std::max(0, n - MUSIC_ROWS)));

    page_title(dl, f, "MY MUSIC");
    // SORT and SHUFFLE
    for (int i = 0; i < 2; i++) {
        float x = 50, y = 80 + i * 36, w = 270, h = 32;
        if (mouse_pick(f, x, y, w, h)) { g_mp.sel = i; g_mp.grabbed = false; }
        bool sel = g_mp.sel == i;
        glass(dl, f, x, y, w, h, 7, sel ? IM_COL32(44, 32, 22, 255) : IM_COL32(10, 10, 13, 255), sel ? 0.78f : 0.6f);
        if (sel) selector(dl, f, x, y, w, h);
        text(dl, f, g_title, 10, x + 12, y + 2, GREY, i == 0 ? "SORT" : "SHUFFLE");
        const char *value = i == 0 ? sort_names[sort] : shuffle ? "ON  (RANDOM ORDER)" : "OFF  (THIS ORDER)";
        text(dl, f, g_title, 14, x + 12, y + 13, sel ? WHITE : GREY, value);
        if (sel) {
            text(dl, f, g_title, 17, x + w - 26, y + 7, ORANGE, "<", CENTER);
            text(dl, f, g_title, 17, x + w - 10, y + 7, ORANGE, ">", CENTER);
        }
        if (mouse_click(f, x, y, w, h)) {
            bool left = ImGui::GetMousePos().x < f.p(x + w - 18, 0).x && ImGui::GetMousePos().x > f.p(x + w - 34, 0).x;
            if (i == 0) music_set_sort(sort + (left ? -1 : 1));
            else music_set_shuffle(!shuffle);
        }
    }
    // the songs
    const float lx = 50, lw = 270, ly = 156, step = 29, rh = 26;
    if (!n) {
        glass(dl, f, lx, ly, lw, 120, 7, IM_COL32(10, 10, 13, 255), 0.5f);
        text(dl, f, g_title, 16, lx + lw / 2, ly + 34, CREAM, loaded ? "NO SONGS YET" : "READING YOUR MUSIC...", CENTER);
        text(dl, f, g_hand, 12, lx + lw / 2, ly + 60, ORANGE, "ADD THEM IN THE LAUNCHER'S MUSIC", CENTER, 0.8f);
    }
    for (int k = 0; k < MUSIC_ROWS && g_mp.top + k < n; k++) {
        int i = g_mp.top + k;
        const musiclib::Song &song = list[i];
        float x = lx, y = ly + k * step, w = lw, h = rh;
        bool sel = g_mp.sel == 2 + i, bad = song.state == musiclib::BAD, held = sel && g_mp.grabbed;
        // the arrows at the right of the chosen (or hovered) row: the mouse's move
        bool hover = mouse_in(f, x, y, w, h);
        bool arrows = sel || hover;
        float ax = x + w - 34;
        if (arrows && mouse_click(f, ax - 2, y, 16, h)) { g_mp.sel = 2 + i; music_move(song.rel, -1); continue; }
        if (arrows && mouse_click(f, ax + 14, y, 18, h)) { g_mp.sel = 2 + i; music_move(song.rel, 1); continue; }
        bool was_sel = g_mp.sel == 2 + i;          // a click picks the row; a click on the chosen row picks the song up
        if (mouse_pick(f, x, y, w - 36, h) && !g_mp.grabbed) g_mp.sel = 2 + i;
        glass(dl, f, x, y, w, h, 6, held ? IM_COL32(90, 52, 12, 255) : sel ? IM_COL32(44, 32, 22, 255) : IM_COL32(10, 10, 13, 255),
              held ? 0.9f : sel ? 0.78f : 0.55f);
        if (sel) selector(dl, f, x, y, w, h);
        char num[8];
        snprintf(num, sizeof(num), "%02d", i + 1);
        text(dl, f, g_title, 13, x + 9, y + 6, bad ? GREY : held ? WHITE : ORANGE, num);
        std::string name = upper(song.name.c_str());
        std::string sub = bad ? "CANNOT BE PLAYED" : song.artist.empty() ? "" : upper(song.artist.c_str());
        float tw = w - 36 - (arrows ? 40 : 6);
        float ns = fit_size(f, g_title, 12, name.c_str(), tw);
        text(dl, f, g_title, ns, x + 34, y + (sub.empty() ? 6 : 1), bad ? GREY : sel ? WHITE : CREAM, name.c_str(), LEFT, 1.0f);
        if (!sub.empty()) {
            float ss = fit_size(f, g_hand, 9, sub.c_str(), tw);
            text(dl, f, g_hand, ss, x + 35, y + 14, bad ? GREY : with_alpha(ORANGE, sel ? 1.0f : 0.6f), sub.c_str(), LEFT, 0.6f);
        }
        if (arrows) {
            ImU32 on = held ? WHITE : ORANGE, off = with_alpha(GREY, 0.35f);
            music_arrow(dl, f, ax + 5, y + h / 2, true, i > 0 ? on : off);
            music_arrow(dl, f, ax + 22, y + h / 2, false, i + 1 < n ? on : off);
        }
        if (sel && was_sel && mouse_click(f, x, y, w - 36, h)) g_mp.grabbed = !g_mp.grabbed;
    }
    if (n > MUSIC_ROWS) {   // where the window is in the list
        float total = MUSIC_ROWS * step - 3, bar = std::max(16.0f, total * MUSIC_ROWS / n);
        float at = (total - bar) * g_mp.top / std::max(1, n - MUSIC_ROWS);
        rect(dl, f, lx + lw + 4, ly, 3, total, IM_COL32(255, 255, 255, 30), 2);
        rect(dl, f, lx + lw + 4, ly + at, 3, bar, ORANGE, 2);
    }

    // the door: what the order means
    door_panel(dl, f);
    char sub[64];
    snprintf(sub, sizeof(sub), n == 1 ? "1 SONG" : "%d SONGS", n);
    const char *desc;
    if (!n) desc = "Add MP3, OGG, FLAC or WAV songs in the launcher's Music. They play in menus and fights, only on your PC.";
    else if (shuffle) desc = "Shuffle is on: your songs play in a random order. Turn it off to play them from the top of this list.";
    else if (g_mp.grabbed) desc = "Move the song with up and down. A or B puts it down. The order is saved at once.";
    else desc = "With shuffle off your songs play from the top of this list. Press A on a song to move it, or LB and RB.";
    door_text(dl, f, &tex("ui/online_icon.png"), "MY MUSIC", sub, desc);

    float hx = 54;
    if (g_mp.grabbed) {
        hx += hint(dl, f, hx, 420, "a", "PUT DOWN");
    } else if (g_mp.sel >= 2) {
        hx += hint(dl, f, hx, 420, "a", "MOVE");
        hx += hint(dl, f, hx, 420, "lb", "UP");
        hx += hint(dl, f, hx, 420, "rb", "DOWN");
    } else {
        hx += hint(dl, f, hx, 420, "a", "CHANGE");
    }
    hint(dl, f, hx, 420, "b", "BACK");
    if (pad.b && !g_mp.grabbed) S.page = HUB;
}

// The gallery's MY MUSIC pages: sample songs, no folder.
bool music_gallery(const std::string &p)
{
    if (p.compare(0, 5, "music") != 0) {
        g_mp.sample = false;
        return false;
    }
    static const char *const names[][3] = {     // made-up songs and artists
        { "Concrete Crown", "Borough Kings", "1" }, { "Fulton Street Freestyle", "Lady Brick", "4" },
        { "Train Yard Echoes", "Nine Avenue", "2" }, { "Last Stop Bronx", "MC Lockjaw", "7" },
        { "Hydrant Summer", "MC Lockjaw", "8" }, { "Steel and Velvet", "Quiet Storm", "3" },
        { "Rooftop Cypher", "Borough Kings", "5" }, { "Midnight Ferry", "Delancey", "9" },
        { "Boom Bap Prayer", "Nine Avenue", "6" }, { "intro-loop", "", "10" } };
    g_mp.sample = true;
    g_mp.songs.clear();
    if (p != "music-empty") {
        for (auto &s : names) {
            musiclib::Song song;
            song.name = s[0];
            song.artist = s[1];
            song.rel = std::string(s[1][0] ? s[1] : "loops") + "/" + s[0] + ".mp3";
            song.added = 1700000000 + 86400 * atoi(s[2]);
            song.state = musiclib::PLAYS;
            g_mp.songs.push_back(song);
        }
    }
    g_mp.sort = p == "music" ? musiclib::SORT_ARTIST : musiclib::SORT_CUSTOM;
    g_mp.shuffle = false;
    g_mp.top = 0;
    g_mp.sel = p == "music-own" ? 4 : 2;
    g_mp.sel_rel.clear();
    g_mp.grabbed = p == "music-own";
    S.page = MUSIC;
    return true;
}
