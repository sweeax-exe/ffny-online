/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
#include "text_fold.h"

#include <stdint.h>
#include <string.h>

/* ---- decoding ------------------------------------------------------------------------------------------- */

/* One UTF-8 sequence at p: its length (>= 1) and the code point, or cp = -1 for a bad byte (length 1). */
static int decode(const unsigned char *p, int32_t *cp)
{
    unsigned c = p[0];
    int len, k;
    uint32_t v;
    if (c < 0x80) { *cp = (int32_t)c; return 1; }
    if (c >= 0xC2 && c <= 0xDF) { len = 2; v = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { len = 3; v = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { len = 4; v = c & 0x07; }
    else { *cp = -1; return 1; }
    for (k = 1; k < len; k++) {
        if ((p[k] & 0xC0) != 0x80) { *cp = -1; return 1; }
        v = (v << 6) | (p[k] & 0x3F);
    }
    if ((len == 3 && (v < 0x800 || (v >= 0xD800 && v <= 0xDFFF))) || (len == 4 && (v < 0x10000 || v > 0x10FFFF))) {
        *cp = -1;
        return 1;
    }
    *cp = (int32_t)v;
    return len;
}

/* ---- what the fonts can draw ----------------------------------------------------------------------------- */

/* The overlay's fonts (TradeBld, TradeCon, FERRO, SchwartzHand): ASCII, Latin-1 and these few. */
static int draws_narrow(int32_t c)
{
    if (c >= 0x20 && c <= 0x7E) return 1;
    if (c >= 0xA0 && c <= 0xFF) return 1;
    switch (c) {
    case 0x131: case 0x141: case 0x142: case 0x152: case 0x153: case 0x160: case 0x161: case 0x178:
    case 0x17D: case 0x17E: case 0x192:
        return 1;
    }
    return 0;
}

/* Segoe UI (the launcher): Latin, Latin Extended, Greek, Cyrillic, Latin Extended Additional, common punctuation. */
static int draws_wide(int32_t c)
{
    if (c >= 0x20 && c <= 0x7E) return 1;
    if (c >= 0xA0 && c <= 0x24F) return 1;
    if (c >= 0x370 && c <= 0x3FF) return 1;
    if (c >= 0x400 && c <= 0x52F) return 1;
    if (c >= 0x1E00 && c <= 0x1EFF) return 1;
    switch (c) {
    case 0x2013: case 0x2014: case 0x2018: case 0x2019: case 0x201C: case 0x201D: case 0x2022: case 0x2026:
    case 0x20AC: case 0x2122:
        return 1;
    }
    return 0;
}

/* ---- tables ----------------------------------------------------------------------------------------------- */

/* Invisible or decorative: dropped. */
static int is_dropped(int32_t c)
{
    return c == 0xAD || c == 0x34F || c == 0x180E || c == 0x115F || c == 0x1160 || c == 0x3164 || c == 0xFEFF ||
           (c >= 0x300 && c <= 0x36F) || (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2064) ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xFE20 && c <= 0xFE2F) ||
           (c >= 0xE0000 && c <= 0xE007F);
}

typedef struct { int32_t cp; const char *to; } Fold;

/* Small caps become capitals; superscript, subscript and modifier letters become their letter; letterlike
 * symbols, ligatures, roman numerals, typographic marks. */
static const Fold k_folds[] = {
    /* small capitals */
    {0x1D00,"A"},{0x299,"B"},{0x1D04,"C"},{0x1D05,"D"},{0x1D07,"E"},{0xA730,"F"},{0x262,"G"},{0x29C,"H"},
    {0x26A,"I"},{0x1D0A,"J"},{0x1D0B,"K"},{0x29F,"L"},{0x1D0D,"M"},{0x274,"N"},{0x1D0F,"O"},{0x1D18,"P"},
    {0xA7AF,"Q"},{0x280,"R"},{0xA731,"S"},{0x1D1B,"T"},{0x1D1C,"U"},{0x1D20,"V"},{0x1D21,"W"},{0x28F,"Y"},
    {0x1D22,"Z"},{0x1D06,"D"},{0x1D19,"R"},{0x1D1F,"U"},{0x1D1D,"U"},
    /* modifier capital letters (superscript) */
    {0x1D2C,"A"},{0x1D2E,"B"},{0x1D30,"D"},{0x1D31,"E"},{0x1D33,"G"},{0x1D34,"H"},{0x1D35,"I"},{0x1D36,"J"},
    {0x1D37,"K"},{0x1D38,"L"},{0x1D39,"M"},{0x1D3A,"N"},{0x1D3C,"O"},{0x1D3E,"P"},{0x1D3F,"R"},{0x1D40,"T"},
    {0x1D41,"U"},{0x1D42,"W"},{0x2C7D,"V"},{0x1D2F,"B"},{0x1D32,"E"},{0x1D3B,"N"},
    /* modifier small letters */
    {0x1D43,"a"},{0x1D47,"b"},{0x1D9C,"c"},{0x1D48,"d"},{0x1D49,"e"},{0x1DA0,"f"},{0x1D4D,"g"},{0x2B0,"h"},
    {0x2071,"i"},{0x2B2,"j"},{0x1D4F,"k"},{0x2E1,"l"},{0x1D50,"m"},{0x207F,"n"},{0x1D52,"o"},{0x1D56,"p"},
    {0x2B3,"r"},{0x2E2,"s"},{0x1D57,"t"},{0x1D58,"u"},{0x1D5B,"v"},{0x2B7,"w"},{0x2E3,"x"},{0x2B8,"y"},
    {0x1DBB,"z"},{0x1D62,"i"},{0x1D63,"r"},{0x1D64,"u"},{0x1D65,"v"},
    {0x2090,"a"},{0x2091,"e"},{0x2092,"o"},{0x2093,"x"},{0x2095,"h"},{0x2096,"k"},{0x2097,"l"},{0x2098,"m"},
    {0x2099,"n"},{0x209A,"p"},{0x209B,"s"},{0x209C,"t"},
    /* superscript and subscript digits (the Latin-1 1 2 3 stay as they are) */
    {0x2070,"0"},{0x2074,"4"},{0x2075,"5"},{0x2076,"6"},{0x2077,"7"},{0x2078,"8"},{0x2079,"9"},
    {0x2080,"0"},{0x2081,"1"},{0x2082,"2"},{0x2083,"3"},{0x2084,"4"},{0x2085,"5"},{0x2086,"6"},{0x2087,"7"},
    {0x2088,"8"},{0x2089,"9"},{0x2B9,"'"},{0x2BC,"'"},
    /* letterlike symbols (the double-struck, script, fraktur letters outside the math block) */
    {0x2102,"C"},{0x210A,"g"},{0x210B,"H"},{0x210C,"H"},{0x210D,"H"},{0x210E,"h"},{0x2110,"I"},{0x2111,"I"},
    {0x2112,"L"},{0x2113,"l"},{0x2115,"N"},{0x2119,"P"},{0x211A,"Q"},{0x211B,"R"},{0x211C,"R"},{0x211D,"R"},
    {0x2124,"Z"},{0x2128,"Z"},{0x212A,"K"},{0x212B,"\xC3\x85"},{0x212C,"B"},{0x212D,"C"},{0x212F,"e"},
    {0x2130,"E"},{0x2131,"F"},{0x2133,"M"},{0x2134,"o"},{0x2139,"i"},{0x2145,"D"},{0x2146,"d"},{0x2147,"e"},
    {0x2148,"i"},{0x2149,"j"},{0x2122,"TM"},{0x2116,"No"},{0x2120,"SM"},{0x2103,"\xC2\xB0""C"},
    /* ligatures */
    {0xFB00,"ff"},{0xFB01,"fi"},{0xFB02,"fl"},{0xFB03,"ffi"},{0xFB04,"ffl"},{0xFB05,"st"},{0xFB06,"st"},
    {0x1E9E,"SS"},{0x17F,"s"},
    /* roman numerals */
    {0x2160,"I"},{0x2161,"II"},{0x2162,"III"},{0x2163,"IV"},{0x2164,"V"},{0x2165,"VI"},{0x2166,"VII"},
    {0x2167,"VIII"},{0x2168,"IX"},{0x2169,"X"},{0x216A,"XI"},{0x216B,"XII"},{0x216C,"L"},{0x216D,"C"},
    {0x216E,"D"},{0x216F,"M"},{0x2170,"i"},{0x2171,"ii"},{0x2172,"iii"},{0x2173,"iv"},{0x2174,"v"},
    {0x2175,"vi"},{0x2176,"vii"},{0x2177,"viii"},{0x2178,"ix"},{0x2179,"x"},{0x217A,"xi"},{0x217B,"xii"},
    {0x217C,"l"},{0x217D,"c"},{0x217E,"d"},{0x217F,"m"},
    /* typographic marks */
    {0x2018,"'"},{0x2019,"'"},{0x201A,","},{0x201B,"'"},{0x201C,"\""},{0x201D,"\""},{0x201E,"\""},{0x201F,"\""},
    {0x2013,"-"},{0x2014,"-"},{0x2010,"-"},{0x2011,"-"},{0x2012,"-"},{0x2015,"-"},{0x2212,"-"},{0x2026,"..."},
    {0x2022,"*"},{0x2024,"."},{0x2044,"/"},{0x2215,"/"},{0x2223,"|"},{0x2502,"|"},{0x2039,"<"},{0x203A,">"},
    {0x2024,"."},{0x2025,".."},{0x2060,""},
};

/* U+0100..U+017F (Latin Extended-A) and U+1E00..U+1EFF (Latin Extended Additional) to their base letter;
 * '\1' = none. Kept as they are where the font has them (draws_narrow runs first). */
static const char k_ext_a[] =
    "AaAaAaCcCcCcCcDd" "DdEeEeEeEeEeGgGg" "GgGgHhHhIiIiIiIi" "IiIiJjKkkLlLlLlL"
    "lLlNnNnNnnNnOoOo" "OoOoRrRrRrSsSsSs" "SsTtTtTtUuUuUuUu" "UuUuWwYyYZzZzZzs";
static const char k_ext_add[] =
    "AaBbBbBbCcDdDdDd" "DdDdEeEeEeEeEeFf" "GgHhHhHhHhHhIiIi" "KkKkKkLlLlLlLlMm"
    "MmMmNnNnNnNnOoOo" "OoOoPpPpRrRrRrRr" "SsSsSsSsSsTtTtTt" "TtUuUuUuUuUuVvVv"
    "WwWwWwWwWwXxXxYy" "ZzZzZzhtwya" "sss" "S\1"
    "AaAaAaAaAaAaAaAa" "AaAaAaAaEeEeEeEe" "EeEeEeEeIiIiOoOo" "OoOoOoOoOoOoOoOo"
    "OoOoUuUuUuUuUuUu" "UuYyYyYyYyLlVvYy";

/* The letter behind a styled/enclosed code point as one char, 0 when it is none of those. Digits as '0'..'9';
 * numbers above 9 (circled 10..20) go through `num`. */
static int styled(int32_t c, char *out, int *num)
{
    int32_t i;
    *num = 0;
    if (c >= 0x1D400 && c <= 0x1D6A3) {             /* 13 styles of 52 letters (A-Z a-z) */
        i = (c - 0x1D400) % 52;
        *out = (char)(i < 26 ? 'A' + i : 'a' + i - 26);
        return 1;
    }
    if (c == 0x1D6A4) { *out = 'i'; return 1; }
    if (c == 0x1D6A5) { *out = 'j'; return 1; }
    if (c >= 0x1D7CE && c <= 0x1D7FF) { *out = (char)('0' + (c - 0x1D7CE) % 10); return 1; }
    if (c >= 0xFF01 && c <= 0xFF5E) { *out = (char)(c - 0xFEE0); return 1; }     /* fullwidth ASCII */
    if (c >= 0x24B6 && c <= 0x24CF) { *out = (char)('A' + c - 0x24B6); return 1; }   /* circled */
    if (c >= 0x24D0 && c <= 0x24E9) { *out = (char)('a' + c - 0x24D0); return 1; }
    if (c >= 0x249C && c <= 0x24B5) { *out = (char)('a' + c - 0x249C); return 1; }   /* parenthesised */
    if (c >= 0x1F110 && c <= 0x1F129) { *out = (char)('A' + c - 0x1F110); return 1; }
    if (c >= 0x1F130 && c <= 0x1F149) { *out = (char)('A' + c - 0x1F130); return 1; }    /* squared */
    if (c >= 0x1F150 && c <= 0x1F169) { *out = (char)('A' + c - 0x1F150); return 1; }    /* negative circled */
    if (c >= 0x1F170 && c <= 0x1F189) { *out = (char)('A' + c - 0x1F170); return 1; }    /* negative squared */
    if (c >= 0x1F1E6 && c <= 0x1F1FF) { *out = (char)('A' + c - 0x1F1E6); return 1; }   /* regional indicators */
    if (c == 0x24EA || c == 0x24FF) { *out = '0'; return 1; }
    if (c >= 0x2460 && c <= 0x2473) { *num = (int)(c - 0x2460) + 1; return 2; }          /* circled 1..20 */
    if (c >= 0x2474 && c <= 0x2487) { *num = (int)(c - 0x2474) + 1; return 2; }          /* parenthesised 1..20 */
    if (c >= 0x2488 && c <= 0x249B) { *num = (int)(c - 0x2488) + 1; return 2; }          /* with a full stop */
    if (c >= 0x24EB && c <= 0x24F4) { *num = (int)(c - 0x24EB) + 11; return 2; }         /* negative circled 11..20 */
    if (c >= 0x24F5 && c <= 0x24FE) { *num = (int)(c - 0x24F5) + 1; return 2; }          /* double circled 1..10 */
    if (c >= 0x2776 && c <= 0x277F) { *num = (int)(c - 0x2776) + 1; return 2; }          /* dingbat circled */
    if (c >= 0x2780 && c <= 0x2789) { *num = (int)(c - 0x2780) + 1; return 2; }
    if (c >= 0x278A && c <= 0x2793) { *num = (int)(c - 0x278A) + 1; return 2; }
    return 0;
}

/* The ASCII (or Latin-1) text a code point folds to, written to `buf`; its length, or 0 when there is none. */
static int fold_one(int32_t c, unsigned flags, char *buf)
{
    char ch;
    int num, kind;
    size_t i;
    if (c == 0x1680 || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 ||
        (c >= 0x2000 && c <= 0x200A)) {
        buf[0] = ' ';
        return 1;
    }
    kind = styled(c, &ch, &num);
    if (kind == 1) { buf[0] = ch; return 1; }
    if (kind == 2) {
        if (num >= 10) { buf[0] = (char)('0' + num / 10); buf[1] = (char)('0' + num % 10); return 2; }
        buf[0] = (char)('0' + num);
        return 1;
    }
    for (i = 0; i < sizeof k_folds / sizeof k_folds[0]; i++)
        if (k_folds[i].cp == c) {
            size_t n = strlen(k_folds[i].to);
            memcpy(buf, k_folds[i].to, n);
            return (int)n;
        }
    if (!(flags & TEXTFOLD_WIDE)) {
        ch = 0;
        if (c >= 0x100 && c <= 0x17F) ch = k_ext_a[c - 0x100];
        else if (c >= 0x1E00 && c <= 0x1EFF && (size_t)(c - 0x1E00) < sizeof k_ext_add - 1) ch = k_ext_add[c - 0x1E00];
        if (ch > 1) { buf[0] = ch; return 1; }
    }
    return 0;
}

int textfold_needed(const char *s)
{
    if (!s) return 0;
    for (; *s; s++)
        if ((unsigned char)*s >= 0x80) return 1;
    return 0;
}

size_t textfold_utf8(const char *in, char *out, size_t cap, unsigned flags)
{
    const unsigned char *p = (const unsigned char *)in;
    size_t n = 0;
    int in_run = 0;     /* inside a run of characters nothing here can draw: one "?" for the run */
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (!in) return 0;
    while (*p) {
        int32_t c;
        int len = decode(p, &c), w;
        char buf[8];
        const unsigned char *src = p;
        p += len;
        if (c >= 0 && is_dropped(c)) continue;      /* does not end a run either */
        if (c >= 0 && ((flags & TEXTFOLD_WIDE) ? draws_wide(c) : draws_narrow(c))) {
            if (n + (size_t)len + 1 > cap) break;
            memcpy(out + n, src, (size_t)len);
            n += (size_t)len;
            in_run = 0;
            continue;
        }
        w = c >= 0 ? fold_one(c, flags, buf) : 0;
        if (w > 0) {
            if (n + (size_t)w + 1 > cap) break;
            memcpy(out + n, buf, (size_t)w);
            n += (size_t)w;
            in_run = 0;
            continue;
        }
        if (c >= 0 && c < 0x20) continue;           /* control characters: nothing to show */
        if (!in_run) {
            if (n + 2 > cap) break;
            out[n++] = '?';
            in_run = 1;
        }
    }
    out[n] = 0;
    return n;
}
