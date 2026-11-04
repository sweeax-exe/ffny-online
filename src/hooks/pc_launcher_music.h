// The launcher's Music dialog underneath (custom music): the folder, the background work (copying added
// songs, checking new ones) with a progress line, the multi-file picker and files dropped on the
// window. The dialog is pc_launcher_music.inc (drawn by pc_ui.cpp); the files, music_library.cpp.
#pragma once

#include <string>
#include <vector>
#include "music_library.h"

namespace pcmusic
{
musiclib::fs::path folder();            // RECOMP_MUSIC_DIR, or <data>/music

struct View
{
    std::vector<musiclib::Song> songs;
    size_t more = 0;                    // songs over the limit, left out
    bool busy = false;
    bool loaded = false;
    std::string progress;               // what the background work does now
    std::string message;                // what happened last
    int tone = 0;                       // 0 plain, 1 done, 2 a problem
    unsigned generation = 0;            // changes whenever the list does
};
View view();                            // a copy, for drawing

void refresh();                         // read the folder again and check new files (background)
void add(const std::vector<std::string> &paths);      // copy these in, then refresh (background)
void remove(const musiclib::Song &song);
void say(const std::string &text, int tone);

// The picker (several files at once). Done when pick_poll returns true; empty when cancelled.
void pick_start(void *window);
bool pick_poll(std::vector<std::string> &paths);

// Files dropped on the launcher window (pc_launcher.cpp); the dialog takes them.
void dropped(const std::vector<std::string> &paths);
bool take_dropped(std::vector<std::string> &paths);
} // namespace pcmusic
