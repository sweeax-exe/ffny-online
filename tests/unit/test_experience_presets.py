"""The Modern and Original experiences: the values players are promised, read from the shared catalogue."""
import re
from pathlib import Path

CATALOG = (Path(__file__).resolve().parents[2] / 'src/hooks/pc_settings_catalog.c').read_text(encoding='utf-8')


def choices(name):
    body = re.search(r'k_%s\[\] = \{(.*?)\}' % name, CATALOG).group(1)
    return re.findall(r'"(\w+)"', body)


def preset(key):
    original, modern = re.search(r'\{"%s", (\d+), (\d+)\}' % key, CATALOG).groups()
    return int(original), int(modern)


def test_modern_uses_2x_msaa_and_16x_anisotropic_filtering():
    msaa, aniso = choices('msaa'), choices('aniso')
    original, modern = preset('msaa')
    assert (msaa[original], msaa[modern]) == ('2x', '2x')
    original, modern = preset('anisotropy')
    assert (aniso[original], aniso[modern]) == ('off', '16x')


def test_modern_description_names_its_filtering():
    ui = (Path(__file__).resolve().parents[2] / 'src/hooks/pc_ui.cpp').read_text(encoding='utf-8')
    assert '2x anti-aliasing and 16x texture filtering' in ui
