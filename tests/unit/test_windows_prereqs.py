"""Build 112's Windows start-up and setup checks: the hybrid-GPU exports (#220), Windows N's missing Media
Foundation (#193)."""
import importlib.util
import os
from pathlib import Path
import re

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


engine = load('setup_engine_prereqs', ROOT / 'setup/engine.py')


def c_string(text, name):
    """The string of a C macro made of adjacent literals."""
    body = re.search(r'#define ' + name + r'((?:\s*\\?\s*"[^"]*")+)', text).group(1)
    return ''.join(re.findall(r'"([^"]*)"', body))


def test_media_feature_pack_message_is_the_same_everywhere():
    header = c_string((ROOT / 'src/hooks/media_check.h').read_text(encoding='utf-8'), 'MEDIA_CHECK_MESSAGE')
    assert header == engine.MEDIA_FEATURE_PACK_MESSAGE
    assert header.startswith('Your Windows is an N edition without media components. Install the Media Feature Pack:')
    assert 'Settings > Apps > Optional features > Add a feature > Media Feature Pack, then restart.' in header
    launcher = (ROOT / 'setup/main.cpp').read_text(encoding='utf-8')
    block = re.search(r'static void requireMediaFoundation\(\)(.*?)\n}', launcher, re.S).group(1)
    assert ''.join(re.findall(r'"([^"]*)"', block.split('runtime_error(', 1)[1])) == header
    assert 'LOAD_LIBRARY_SEARCH_SYSTEM32' in block
    assert re.search(r'static void launchInstalled\(\) \{\s*requireMediaFoundation\(\);', launcher)


def test_game_loads_media_foundation_late_and_checks_first():
    cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
    assert '/DELAYLOAD:MFPlat.dll /DELAYLOAD:MFReadWrite.dll' in cmake and 'delayimp' in cmake
    main = (ROOT / 'src/main.c').read_text(encoding='utf-8')
    entry = main.index('int WINAPI WinMain(')
    first = main.index('media_check_startup()', entry)
    assert first < main.index('pc_settings_capture_start();', entry)


def test_hybrid_gpu_exports():
    main = (ROOT / 'src/main.c').read_text(encoding='utf-8')
    assert re.search(r'__declspec\(dllexport\) DWORD NvOptimusEnablement = 0x00000001;', main)
    assert re.search(r'__declspec\(dllexport\) int AmdPowerXpressRequestHighPerformance = 1;', main)


@pytest.mark.skipif(os.name != 'nt', reason='Windows DLL loading')
def test_media_foundation_present_on_this_pc_and_a_missing_dll():
    assert engine.media_foundation_present() is True
    assert engine.media_foundation_present(('ffny-no-such-media.dll',)) is False
