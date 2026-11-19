// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// [debug] unlock_all on the current profile in memory: save_unlock.h (saves_unlock_profile_slot).
#include "save_unlock.h"
#include "pc_launcher_saves.h"

#include <cstdio>

extern "C" int saves_unlock_profile_slot(uint8_t *slot, uint32_t size, const uint32_t items[6], const uint32_t states[6])
{
    static int ready = -1;              // -1 not read yet, 0 no usable database, 1 layout read
    static saves::UnlockLayout layout;
    static bool refused_logged;
    if (ready < 0) {
        saves::Bytes sod;
        std::string where, why;
        ready = pcsaves::game_database(sod, &where) && saves::unlock_layout(sod, layout, &why);
        if (!ready)
            fprintf(stderr, "[UNLOCK] profile items: no usable database (%s)\n", where.empty() ? "assets/djv2.sod not found" : why.c_str());
    }
    if (!ready) return -1;
    saves::UnlockCounts counts;
    std::string why;
    if (!saves::unlock_profile_slot(slot, size, items, states, layout, counts, &why)) {
        if (!refused_logged) fprintf(stderr, "[UNLOCK] profile items not unlocked: %s\n", why.c_str());
        refused_logged = true;
        return -1;
    }
    int n = 0;
    for (int k : counts.items) n += k;
    return n;
}
