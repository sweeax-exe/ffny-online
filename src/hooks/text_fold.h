/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Fancy-font names and chat text, folded to what the overlay's fonts can draw (build 94).
 *
 * Discord display names and chat lines often use "fancy font" letters: Mathematical Alphanumeric Symbols
 * (bold, script, fraktur ... letters), fullwidth letters, small caps, superscript and modifier letters,
 * circled / squared / parenthesised letters, roman numerals, ligatures. The game's UI fonts (Latin-1 and a few
 * extras) and the launcher's Segoe UI do not have them, so every such letter was drawn as "?". For DISPLAY only,
 * textfold_utf8 maps them to their plain letters and digits (the NFKC idea plus a table for what NFKC does not
 * cover: small caps -> capitals, superscript / subscript / modifier letters, the Latin Extended-A and
 * Latin Extended Additional letters the overlay fonts lack, to their base letter). Letters the font CAN draw
 * (Latin-1 accents and a few more) stay as they are. Zero-width characters, variation selectors and combining
 * marks (the strike-through and underline tricks) are dropped. Whatever is left that no font here has (CJK,
 * emoji, Cyrillic in the overlay) becomes ONE "?" per run instead of one per character.
 *
 * The same function folds the server's chat word filter input (server/ has a Go twin with the same table).
 * Pure logic, no Windows. UTF-8 in, UTF-8 out, never longer than `cap - 1` bytes. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The font behind the text also has Latin Extended, Greek and Cyrillic (the launcher's Segoe UI): keep those. */
#define TEXTFOLD_WIDE 1u

/* Folds `in` into `out` (NUL-terminated, at most cap-1 bytes, never cut inside a character). Returns the length
 * written. `out` may not overlap `in`. */
size_t textfold_utf8(const char *in, char *out, size_t cap, unsigned flags);

/* 1 when `s` has a byte >= 0x80, i.e. folding could change it. */
int textfold_needed(const char *s);

#ifdef __cplusplus
}

#include <string>
#include <stdlib.h>
#include <string.h>

/* For drawing: `s` itself when it is plain ASCII (the usual case, no copy), else a folded copy that lives until
 * the 16th later call on this thread. */
inline const char *textfold_display(const char *s, unsigned flags = 0)
{
    static const bool off = getenv("RECOMP_TEXTFOLD_OFF") != nullptr;      // tests: the "before" of a screenshot
    if (!s || off || !textfold_needed(s)) return s;
    static thread_local std::string ring[16];
    static thread_local unsigned next = 0;
    std::string &r = ring[next++ & 15];
    r.assign(strlen(s) * 4 + 8, '\0');
    r.resize(textfold_utf8(s, &r[0], r.size(), flags));
    return r.c_str();
}
#endif
