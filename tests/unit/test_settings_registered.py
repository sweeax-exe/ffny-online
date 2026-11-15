"""Every setting the game reads must be in a table the game registers.

recomp_settings_get() of a setting nobody registered quietly returns the caller's default, and
recomp_settings_set_text() refuses it. Build 82 shipped two such gaps: the [cheats] table was built
only into the game-free app (pc_settings_catalog.c), so every cheat read "off" in the game and
cheats_apply_settings() had no caller; and [content] picked (the NEW badges) was never registered,
so picking a new fighter or venue did not clear its badge.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
READ = re.compile(r'recomp_settings_(?:get|get_text|set|set_text)\(\s*"([a-z_]+)"\s*,\s*"([a-z_0-9]+)"')
DEFINED = re.compile(r'(?:\{|ADD\()\s*"([a-z_]+)"\s*,\s*"([a-z_0-9]+)"\s*,\s*RECOMP_SETTING_')


def game_sources():
    """src/*.c, src/hooks and src/patches as the game target globs them, minus CMakeLists.txt's exclusions."""
    cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
    excluded = set()
    for m in re.finditer(r'list\(FILTER (?:HOOK|PATCH)_SOURCES EXCLUDE REGEX "/\(([^)]*)\)\$"\)', cmake):
        excluded.update(name.replace('\\', '') for name in m.group(1).split('|'))
    files = [p for folder in ('src/hooks', 'src/patches') for p in (ROOT / folder).iterdir()
             if p.suffix in ('.c', '.cpp', '.inc') and p.name not in excluded]
    return files + list((ROOT / 'src').glob('*.c'))


def test_cheats_settings_built_into_the_game():
    names = {p.name for p in game_sources()}
    assert 'cheats_settings.c' in names and 'cheats.c' in names


def test_every_setting_the_game_reads_is_registered():
    reads, defined = set(), set()
    for path in game_sources():
        text = path.read_text(encoding='utf-8', errors='replace')
        reads.update(READ.findall(text))
        defined.update(DEFINED.findall(text))
    assert ('cheats', 'max_blazin') in reads
    assert sorted(reads - defined) == []


def test_game_registers_and_applies_the_cheats():
    text = (ROOT / 'src' / 'hooks' / 'pc_settings.c').read_text(encoding='utf-8')
    init = text[text.index('int pc_settings_init(void)'):]
    init = init[:init.index('recomp_settings_register(')]
    assert 'cheats_settings(g_settings + g_setting_count' in init
    assert 'pc_language_settings(g_settings + g_setting_count, 5)' in init
    apply = text[text.index('void pc_settings_apply_display(void)'):]
    apply = apply[:apply.index('\n}')]
    assert 'cheats_apply_settings();' in apply
