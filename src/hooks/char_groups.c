/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 96: character groups, the pure parts (char_groups.h; tests/unit/test_char_groups_native.py). */
#include "char_groups.h"

#include <string.h>

void char_group_key(const char *name, char *out, size_t size)
{
    size_t o = 0;
    if (!out || !size) return;
    for (const unsigned char *p = (const unsigned char *)(name ? name : ""); *p && o + 1 < size; p++) {
        unsigned char c = *p;
        if (c >= 'A' && c <= 'Z') out[o++] = (char)(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80) out[o++] = (char)c;
    }
    while (o && ((unsigned char)out[o - 1] & 0xC0) == 0x80 && o + 1 >= size) o--;   /* no cut character at a full buffer */
    out[o] = 0;
}

int char_groups_assign(const char *const *keys, int n, int *leader, int *count)
{
    int groups = 0;
    for (int i = 0; i < n; i++) {
        leader[i] = -1;
        if (count) count[i] = 1;
        if (!keys[i] || !keys[i][0]) continue;
        leader[i] = i;
        for (int j = 0; j < i; j++)
            if (leader[j] == j && !strcmp(keys[j], keys[i])) { leader[i] = j; break; }
    }
    for (int i = 0; i < n; i++) {
        int c = 0;
        if (leader[i] < 0) continue;
        for (int j = 0; j < n; j++) if (leader[j] == leader[i]) c++;
        if (count) count[i] = c;
        if (leader[i] == i && c >= 2) groups++;
    }
    return groups;
}

int char_group_face(const int *orders, int count, int remembered)
{
    int best = -1;
    if (count < 1) return -1;
    if (remembered >= 0 && remembered < count) return remembered;
    for (int i = 0; i < count; i++)
        if (best < 0 || orders[i] > orders[best]) best = i;
    return best;
}

const char *char_group_version_label(const char *version_name, const char *pack_name, const char *fighter_name)
{
    if (version_name && *version_name) return version_name;
    if (pack_name && *pack_name) return pack_name;
    if (fighter_name && *fighter_name) return fighter_name;
    return "?";
}

int char_groups_parse_choice(const char *line, char *key, size_t key_size, char *pack, size_t pack_size)
{
    const char *eq, *k;
    size_t kn, pn;
    if (!line || strncmp(line, "group.", 6)) return 0;
    k = line + 6;
    if (!(eq = strchr(k, '=')) || eq == k) return 0;
    kn = (size_t)(eq - k);
    pn = strcspn(eq + 1, "\r\n");
    while (pn && eq[pn] == ' ') pn--;
    if (kn >= key_size || pn >= pack_size) return 0;
    memcpy(key, k, kn);
    key[kn] = 0;
    memcpy(pack, eq + 1, pn);
    pack[pn] = 0;
    return 1;
}
