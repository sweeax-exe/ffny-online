# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""The fight overlays' game look (build 81): scripts/online-art.py cuts the game's FIGHT! lettering out of
the fight HUD's call-out texture and gathers a tile of its wear. Synthetic pictures only, no game file;
the drawing that uses them (src/hooks/fight_hud_ui.inc) keeps its old look without them."""
import importlib.util
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load_online_art():
    sys.path.insert(0, str(ROOT / 'scripts'))
    spec = importlib.util.spec_from_file_location('online_art', ROOT / 'scripts/online-art.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def callouts(fight_box=(335, 178, 502, 253)):
    """A stand-in for the call-out texture: white 'letters' with dark specks in every row of words, the
    last word of the bottom row where the game's FIGHT! is."""
    from PIL import Image, ImageDraw
    image = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    for box in ((6, 2, 300, 80), (3, 88, 200, 168), (3, 178, 330, 250), fight_box):
        d.rectangle(box, fill=(235, 235, 230, 255))
    for i in range(0, 512 * 256, 37):            # specks inside the letters
        x, y = i % 512, i // 512
        if image.getpixel((x, y))[3]:
            image.putpixel((x, y), (40, 40, 40, 255))
    return image


def test_version_and_font():
    art = load_online_art()
    assert art.VERSION >= 7        # installs made before the call-out art see their art as old
    assert 'TeaChest.ttf' in art.FONTS and 'FERRO___.TTF' in art.FONTS


def test_fight_lettering_cut_to_its_edges():
    pytest.importorskip('PIL')
    art = load_online_art()
    word = art.hud_fight(callouts())
    assert word is not None and word.size == (168, 76)
    # nothing of the word to its left (RING ends at 330, left of the box) is taken
    assert word.getchannel('A').getbbox() == (0, 0, 168, 76)


def test_unexpected_texture_is_refused():
    pytest.importorskip('PIL')
    art = load_online_art()
    assert art.hud_fight(callouts(fight_box=(340, 230, 360, 240))) is None     # too small: not FIGHT!
    from PIL import Image
    assert art.hud_fight(Image.new('RGBA', (512, 256))) is None


def test_wear_tile_covers_and_is_white():
    pytest.importorskip('PIL')
    art = load_online_art()
    tile = art.hud_grain(callouts())
    assert tile.size == (256, 256) and tile.mode == 'RGBA'
    assert tile.convert('RGB').getextrema() == ((255, 255), (255, 255), (255, 255))
    alpha = tile.getchannel('A')
    hist = alpha.histogram()
    specks = sum(hist[1:])
    assert 0.005 * 65536 < specks < 0.5 * 65536, specks
    # specks across the whole tile, not only where one copy of the letters fell
    for qx in (0, 128):
        for qy in (0, 128):
            assert alpha.crop((qx, qy, qx + 128, qy + 128)).getbbox(), (qx, qy)


def test_missing_archive_is_reported(tmp_path):
    art = load_online_art()
    assert art.write_hud_art(tmp_path, tmp_path) == ([], ['assets/misc.viv'])
