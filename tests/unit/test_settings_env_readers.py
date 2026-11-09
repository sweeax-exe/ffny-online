"""Every variable pc_settings.c exports from settings.ini must be read somewhere.

1.0.0-beta.1 exported [textures] cache_mb as RECOMP_PC_TEXTURE_CACHE_MB, a name nothing reads, so
the texture loader kept its own 512 MiB default and evicted textures mid-scene (black widescreen
borders) whatever the setting said.
"""
import re
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
SETTINGS = REPO / 'src' / 'hooks' / 'pc_settings.c'
TOOLKIT = REPO / 'tools' / 'xboxrecomp' / 'src'


def readers():
    names = set()
    roots = [REPO / 'src', TOOLKIT]
    for root in roots:
        for path in root.rglob('*'):
            if path.suffix not in ('.c', '.cpp', '.h') or path == SETTINGS or 'gen' in path.parts:
                continue
            names.update(re.findall(r'getenv\("(RECOMP_[A-Z0-9_]+)"\)', path.read_text(encoding='utf-8', errors='replace')))
    return names


def test_every_exported_setting_has_a_reader():
    if not any(TOOLKIT.glob('*')):
        pytest.skip('tools/xboxrecomp is not checked out')
    exported = set(re.findall(r'_putenv_s\("(RECOMP_[A-Z0-9_]+)"', SETTINGS.read_text(encoding='utf-8')))
    assert 'RECOMP_TEXTURE_CACHE_MB' in exported
    assert sorted(exported - readers()) == []
