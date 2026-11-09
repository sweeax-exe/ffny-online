"""Compile the game's UI hotkey defaults on POSIX, without the app-only guard.

The game-free app omits the overlay/song hotkeys, so its CI build cannot catch
missing virtual-key names in those defaults (PR #206's first macOS game build).
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.skipif(os.name == 'nt', reason='POSIX virtual-key compatibility')
def test_game_hotkey_defaults_compile_with_the_posix_ui_header(tmp_path):
    compiler = shutil.which('c++')
    if not compiler:
        pytest.skip('C++ compiler required')
    text = (ROOT / 'src/hooks/pc_ui.cpp').read_text(encoding='utf-8')
    defaults = re.findall(r'^int g_(?:overlay_vk|song_vk)\b[^;]*;', text, re.M)
    assert len(defaults) == 2
    # This header's declarations need no ImGui types; avoid downloading ImGui.
    (tmp_path / 'imgui.h').write_text('')
    source = tmp_path / 'hotkeys.cpp'
    source.write_text('#include "pc_ui_internal.h"\n' + '\n'.join(defaults))
    result = subprocess.run([compiler, '-std=c++17', '-fsyntax-only',
                             '-I' + str(tmp_path), '-I' + str(ROOT / 'src/hooks'), str(source)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
