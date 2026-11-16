/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 86, UPDATE NOW: the rules (content_update_logic.h). Nothing here reads the game: the unit tests build
 * this file alone (tests/unit/test_content_update.py). */
#include <string.h>
#include <wchar.h>
#include "content_update_logic.h"

int cu_gate(const CuGate *g)
{
    if (g->already) return CU_BUSY;
    if (g->in_fight) return CU_IN_FIGHT;
    if (g->match_locked || g->lockstep || g->room_phase == 2) return CU_IN_MATCH;
    if (g->room_phase == 1 || g->hill_busy) return CU_COUNTDOWN;
    if (g->news <= 0) return CU_NO_NEWS;
    return CU_OK;
}

const char *cu_refusal_text(int why)
{
    switch (why) {
    case CU_OK: return "";
    case CU_NO_NEWS: return "You already have all the community content.";
    case CU_IN_FIGHT: return "Not in a fight: update after it.";
    case CU_IN_MATCH: return "Not during a match: update after it.";
    case CU_COUNTDOWN: return "Your room's match is about to start: update after it.";
    case CU_BUSY: return "Updating: the game closes and starts again by itself.";
    }
    return "Not now.";
}

void cu_offer_frame(CuOffer *o, int in_fight)
{
    if (in_fight) o->fight_since_menu = 1;
}

int cu_offer_popup_due(CuOffer *o, int news, int on_main_menu, unsigned long long now)
{
    if (!on_main_menu) return 0;
    if (news <= 0) {                    /* nothing to offer: a fight before it does not count later */
        o->fight_since_menu = 0;
        return 0;
    }
    if (now < o->snooze_until) {        /* LATER: a return from a fight meanwhile is not asked about */
        o->fight_since_menu = 0;
        return 0;
    }
    if (o->fight_since_menu || o->offered_news != news) return 1;
    return 0;
}

void cu_offer_answered(CuOffer *o, int news, int later, unsigned long long now)
{
    o->offered_news = news;
    o->toasted_news = news;             /* the popup said it: no corner notice for the same news */
    o->fight_since_menu = 0;
    o->toast_until = 0;
    if (later) o->snooze_until = now + CU_SNOOZE_MS;
}

int cu_offer_toast(CuOffer *o, int news, int in_menu, unsigned long long now)
{
    if (news <= 0) {
        o->toast_until = 0;
        return 0;
    }
    if (now < o->snooze_until) return 0;
    {
        int fresh = o->toasted_news != news;
        int after_snooze = o->snooze_until && o->toast_at < o->snooze_until;   /* LATER ran out */
        if (in_menu && (fresh || after_snooze)) {
            o->toasted_news = news;
            o->toast_at = now;
            o->toast_until = now + CU_TOAST_MS;
        }
    }
    return in_menu && now < o->toast_until;
}

int cu_check_due(unsigned long long last, unsigned long long now, int in_fight, int signed_in, int online_page)
{
    if (in_fight) return 0;
    if (!signed_in && !online_page) return 0;
    return !last || now - last >= CU_CHECK_MS;
}

int cu_after_child_exit(unsigned long code)
{
    return code == CU_EXIT_CODE;
}

static int is_space(wchar_t c) { return c == L' ' || c == L'\t'; }

size_t cu_strip_flag(wchar_t *line, const wchar_t *flag)
{
    size_t n = wcslen(flag), count = 0;
    wchar_t *p = line;
    int quoted = 0;
    if (!n) return 0;
    while (*p) {
        if (*p == L'"') { quoted = !quoted; p++; continue; }
        if (!quoted && (p == line || is_space(p[-1])) && !wcsncmp(p, flag, n) && (!p[n] || is_space(p[n]))) {
            wchar_t *from = p + n, *to = p;
            /* the flag and the blanks before it (or after it, at the start) */
            while (to > line && is_space(to[-1])) to--;
            if (to == line) while (is_space(*from)) from++;
            memmove(to, from, (wcslen(from) + 1) * sizeof(wchar_t));
            p = to;
            count++;
            continue;
        }
        p++;
    }
    return count;
}

static int strip_and_add(const wchar_t *line, wchar_t *out, size_t size, const wchar_t *const *strip, int nstrip,
                         const wchar_t *tail)
{
    int i;
    size_t n;
    if (!out || !size || wcslen(line) + 1 > size) return 0;
    memcpy(out, line, (wcslen(line) + 1) * sizeof(wchar_t));
    for (i = 0; i < nstrip; i++) cu_strip_flag(out, strip[i]);
    n = wcslen(out);
    if (n + wcslen(tail) + 1 > size) return 0;
    memcpy(out + n, tail, (wcslen(tail) + 1) * sizeof(wchar_t));
    return 1;
}

int cu_launcher_command(const wchar_t *line, wchar_t *out, size_t size)
{
    static const wchar_t *const strip[] = { L"--no-launcher", L"--launcher-child", L"--launcher", CU_FLAG };
    return strip_and_add(line, out, size, strip, 4, L" --launcher " CU_FLAG);
}

int cu_reopen_command(const wchar_t *line, wchar_t *out, size_t size)
{
    static const wchar_t *const strip[] = { CU_FLAG };
    return strip_and_add(line, out, size, strip, 1, L" --launcher");
}
