"""Fancy-font names and chat text folded for display (src/hooks/text_fold.c), compiled natively.

Discord display names and chat lines with Mathematical Alphanumeric letters, fullwidth letters, small caps,
superscripts, circled letters and the like drew as "?" per character in the overlay (its fonts are Latin-1 plus a
few). textfold_utf8 maps them to plain letters for display, keeps what the font can draw (accents), drops zero-width
and combining decoration, and turns whatever nothing can draw (CJK, emoji) into one "?" per run.
Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1) on Windows or `cc` elsewhere; skips without it.
"""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hooks/text_fold.h"

/* stdin (UTF-8, one line) -> folded line. argv[1] "wide" keeps the launcher's wider set; argv[2] = output capacity. */
int main(int argc, char **argv)
{
    char in[4096], out[4096];
    unsigned flags = argc > 1 && !strcmp(argv[1], "wide") ? TEXTFOLD_WIDE : 0;
    size_t cap = argc > 2 ? (size_t)atoi(argv[2]) : sizeof out;
    size_t n = fread(in, 1, sizeof in - 1, stdin);
    in[n] = 0;
    n = textfold_utf8(in, out, cap, flags);
    fwrite(out, 1, n, stdout);
    return 0;
}
'''


@pytest.fixture(scope='module')
def exe(tmp_path_factory):
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('the native fixture needs a C compiler (MSVC on Windows)')
    folder = tmp_path_factory.mktemp('textfold')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    src = str(ROOT / 'src/hooks/text_fold.c')
    if os.name == 'nt':
        out = folder / 'textfold.exe'
        cmd = [compiler, '/nologo', '/O2', '/TC', '/I' + str(ROOT / 'src'), str(folder / 'driver.c'), src, '/Fe:' + str(out)]
    else:
        out = folder / 'textfold'
        cmd = [compiler, '-O2', '-std=c11', '-I' + str(ROOT / 'src'), str(folder / 'driver.c'), src, '-o', str(out)]
    built = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def fold(exe, text, mode='narrow', cap=4096):
    return subprocess.run([str(exe), mode, str(cap)], input=text.encode('utf-8'), capture_output=True,
                          check=True).stdout.decode('utf-8')


def mathstyle(base, letters='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'):
    return lambda word: ''.join(chr(base + letters.index(c)) if c in letters else c for c in word)


MATH = [0x1D400, 0x1D434, 0x1D468, 0x1D49C, 0x1D4D0, 0x1D504, 0x1D538, 0x1D56C, 0x1D5A0, 0x1D5D4, 0x1D608, 0x1D63C,
        0x1D670]


@pytest.mark.parametrize('base', MATH)
def test_math_alphanumeric_styles_fold_to_plain_letters(exe, base):
    assert fold(exe, mathstyle(base)('Blick')) == 'Blick'


def test_the_examples_of_the_report(exe):
    assert fold(exe, '\U0001D4D1\U0001D4F5\U0001D4F2\U0001D4EC\U0001D4F4') == 'Blick'      # bold script
    assert fold(exe, '\U0001D401\U0001D425\U0001D422\U0001D41C\U0001D424') == 'Blick'      # bold
    assert fold(exe, 'Ｂｌｉｃｋ') == 'Blick'                          # fullwidth
    assert fold(exe, 'ᴮᴸᴵᶜᴷ') == 'BLIcK'                      # modifier letters
    assert fold(exe, 'ʙʟɪᴄᴋ') == 'BLICK'                          # small caps
    assert fold(exe, 'Ⓑⓛⓘⓒⓚ') == 'Blick'                          # circled


def test_letterlike_symbols_and_digits(exe):
    assert fold(exe, 'ℬℍℕℝ') == 'BHNR'
    assert fold(exe, '\U0001D7D8\U0001D7D9\U0001D7DA') == '012'
    assert fold(exe, '１２３') == '123'
    assert fold(exe, 'x² ⁴⁵ ₁₂') == 'x² 45 12'
    assert fold(exe, '①⑳') == '120'


def test_superscript_and_subscript_letters(exe):
    assert fold(exe, 'ʰⁱᵍʰ') == 'high'
    assert fold(exe, 'ₐₑₒ') == 'aeo'


def test_squared_and_negative_letters(exe):
    assert fold(exe, '\U0001F131\U0001F13B\U0001F138') == 'BLI'
    assert fold(exe, '\U0001F171\U0001F17B') == 'BL'
    assert fold(exe, '\U0001F151\U0001F15B') == 'BL'


def test_ordinary_names_are_unchanged(exe):
    for name in ['Blick', 'xX_Killer_Xx', 'José Ångström', 'Straße ½ Øre', 'a-b.c [1] (2)',
                 'łódž', 'Œuvre š']:
        assert fold(exe, name) == name


def test_accents_are_kept_where_the_font_has_them(exe):
    assert fold(exe, 'Renée') == 'Renée'
    assert fold(exe, 'üñçà') == 'üñçà'


def test_latin_extended_letters_the_font_lacks_fold_to_the_base_letter(exe):
    assert fold(exe, 'Āćčęő') == 'Acceo'
    assert fold(exe, 'Nguyễn') == 'Nguyen'


def test_wide_mode_keeps_what_segoe_draws(exe):
    assert fold(exe, 'Āč', 'wide') == 'Āč'
    assert fold(exe, 'Влад', 'wide') == 'Влад'     # Cyrillic
    assert fold(exe, '\U0001D401ｌ', 'wide') == 'Bl'                                    # still folded
    assert fold(exe, 'В', 'narrow') == '?'


def test_unsupported_runs_become_one_placeholder(exe):
    assert fold(exe, '中文名') == '?'
    assert fold(exe, 'A中文B') == 'A?B'
    assert fold(exe, '\U0001F600\U0001F602 hi \U0001F525') == '? hi ?'
    assert fold(exe, '中 a 文') == '? a ?'


def test_emoji_with_joiners_and_variation_selectors_is_one_run(exe):
    assert fold(exe, 'x\U0001F468‍\U0001F469️y') == 'x?y'


def test_zero_width_and_combining_decoration_is_dropped(exe):
    assert fold(exe, 'B̶l̶i̶c̶k̶') == 'Blick'
    assert fold(exe, 'Bl​ic‍k﻿') == 'Blick'
    assert fold(exe, 'B̲l̲') == 'Bl'


def test_ligatures_numerals_and_marks(exe):
    assert fold(exe, 'ﬁsh ﬃ') == 'fish ffi'
    assert fold(exe, 'Ⅷ Ⅹ') == 'VIII X'
    assert fold(exe, '‘hi’ “yo” …') == '\'hi\' "yo" ...'
    assert fold(exe, 'a—b') == 'a-b'
    assert fold(exe, 'x™') == 'xTM'


def test_wide_spaces_become_a_plain_space(exe):
    assert fold(exe, 'a　b c d') == 'a b c d'


def test_bad_utf8_and_control_characters_do_not_break(exe):
    out = subprocess.run([str(exe), 'narrow', '4096'], input=b'a\xff\xfeb\xc3(c\x01d', capture_output=True,
                         check=True).stdout
    assert out.decode('ascii') == 'a?b?(cd'


def test_the_output_is_cut_at_the_capacity_without_splitting_a_character(exe):
    assert fold(exe, 'abcdef', cap=4) == 'abc'
    assert fold(exe, 'abé', cap=4) == 'ab'      # é needs two bytes and the terminator
    assert fold(exe, '\U0001D401\U0001D425\U0001D422', cap=3) == 'Bl'
