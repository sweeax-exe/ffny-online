"""The rank emblems are the port's own SVG art, rendered to PNG on the player's computer (scripts/rank-emblems.py).

No image is committed: the source is scripts/rank_emblem_art.py. These checks need no browser: the sheet page
holds all sixteen emblems, refers to nothing outside itself, and the textures' names are the ones the game
loads (src/hooks/rank_art.inc)."""
import importlib.util
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def load():
    spec = importlib.util.spec_from_file_location('rank_emblems', ROOT / 'scripts/rank-emblems.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_sheet_holds_every_emblem_and_nothing_remote():
    mod = load()
    page = mod.sheet_html()
    for i in range(1, 9):
        assert 'href="#em-%d"' % i in page
        assert 'href="#sm-%d"' % i in page
        assert 'id="em-%d"' % i in page and 'id="sm-%d"' % i in page
    # offline: no fetch from the network (the SVG namespace is not a fetch)
    remote = [u for u in re.findall(r'(?:src|href)=["\'](https?:[^"\']+)', page)]
    assert remote == []
    assert 'url(http' not in page


def test_sheet_size_matches_the_crop():
    mod = load()
    w, h = mod.sheet_size()
    assert w == 8 * (mod.BIG + 2 * mod.PAD)
    assert h == mod.BIG + 2 * mod.PAD + mod.SMALL + 2 * mod.PAD


def test_game_loads_the_names_the_script_writes():
    inc = (ROOT / 'src/hooks/rank_art.inc').read_text(encoding='utf-8')
    assert 'ranks/em%d-%d.png' in inc
    mod = load()
    assert (mod.BIG, mod.SMALL) == (256, 64)


def test_no_emblem_image_is_tracked():
    assert not list((ROOT / 'scripts').glob('*.png'))
