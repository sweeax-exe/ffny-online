"""Build 80: the PC player's opt-in for cross-play with the community Android port.

docs/research/crossplay-optin.md: off by default; the launcher shows the toggle; the client sends
CAPS bit 21 only while the setting is on (the relay pairs PC with Android players only then).
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _src(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_setting_defaults_off():
    row = re.search(r'\{\s*"online",\s*"crossplay_android",\s*RECOMP_SETTING_BOOL,\s*(\d+),', _src('src/hooks/pc_settings.c'))
    assert row, 'no [online] crossplay_android row in pc_settings.c'
    assert row.group(1) == '0', 'cross-play with Android must be off by default'


def test_caps_bit_21_only_when_on():
    net = _src('src/hooks/netplay.c')
    assert re.search(r'#define CAPS_XPLAY_ANDROID 2097152u', net)
    assert 2097152 == 1 << 21
    assert '#define CAPS_XPLAY_OPT (recomp_settings_get("online", "crossplay_android", 0) ? CAPS_XPLAY_ANDROID : 0u)' in net
    assert re.search(r'put32\(cb \+ n, CAPS_[A-Z_| \n]+\| CAPS_XPLAY_OPT \| CAPS_HILL_CREATED\); n \+= 4;', net)
    # every other CAPS bit is distinct from bit 21
    others = [int(v.rstrip('u')) for n, v in re.findall(r'#define (CAPS_[A-Z_]+)\s+(\d+u)', net) if n != 'CAPS_XPLAY_ANDROID']
    assert all(v != 1 << 21 for v in others)


def test_launcher_shows_the_toggle():
    ui = _src('src/hooks/pc_ui.cpp')
    assert '{"online", "crossplay_android", "Play with Android players"}' in ui
    assert ui.count('crossplay_setting();') >= 2   # Online page and the basics page
