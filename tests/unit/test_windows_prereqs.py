"""Build 112's Windows start-up and setup checks: the hybrid-GPU exports (#220), Windows N's missing Media
Foundation (#193) and the setup's minimum Windows SDK, with synthetic folders only."""
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


def test_sdk_version_reads_vsdevcmd_and_folder_names():
    assert engine.sdk_version('10.0.16299.0\\') == (10, 0, 16299)
    assert engine.sdk_version('10.0.26100.0') == (10, 0, 26100)
    assert engine.sdk_version('10.0.20348') == (10, 0, 20348)
    assert engine.sdk_version('') is None and engine.sdk_version('wdf') is None


def test_minimum_sdk_is_the_c11_one():
    # cl /std:c11 (CMAKE_C_STANDARD 11) needs the SDK headers from 10.0.20348.0 (version 2104) on
    assert 'CMAKE_C_STANDARD 11' in (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
    assert engine.WINDOWS_SDK_MINIMUM == (10, 0, 20348)
    assert engine.sdk_version('10.0.16299.0\\') < engine.WINDOWS_SDK_MINIMUM
    assert engine.sdk_version('10.0.19041.0') < engine.WINDOWS_SDK_MINIMUM
    assert engine.sdk_version('10.0.22621.0') >= engine.WINDOWS_SDK_MINIMUM


def make_sdk(kits, version, ucrt=True, libs=True):
    inc, lib = kits / 'Include' / version, kits / 'Lib' / version
    files = [inc / 'um/Windows.h', inc / 'shared/sdkddkver.h']
    if ucrt:
        files.append(inc / 'ucrt/stdio.h')
    if libs:
        files += [lib / 'um/x64/kernel32.lib'] + ([lib / 'ucrt/x64/ucrt.lib'] if ucrt else [])
    for f in files:
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text('')


def test_complete_sdks_newest_last_and_incomplete_ones_left_out(tmp_path):
    make_sdk(tmp_path, '10.0.16299.0')
    make_sdk(tmp_path, '10.0.22621.0')
    make_sdk(tmp_path, '10.0.26100.0', ucrt=False)        # lost its Universal CRT part
    make_sdk(tmp_path, '10.0.19041.0', libs=False)        # headers only
    (tmp_path / 'Include/wdf').mkdir(parents=True)
    found = engine.complete_windows_sdks(tmp_path)
    assert [name for _, name in found] == ['10.0.16299.0', '10.0.22621.0']
    assert found[-1][0] == (10, 0, 22621)


def test_only_old_sdks_and_none(tmp_path):
    make_sdk(tmp_path, '10.0.16299.0')
    found = engine.complete_windows_sdks(tmp_path)
    assert found[-1][0] < engine.WINDOWS_SDK_MINIMUM
    message = engine.windows_sdk_too_old(found[-1][1])
    assert '10.0.16299.0' in message and '10.0.20348.0' in message
    for step in ('Visual Studio Installer', 'Modify', 'Individual components', 'Windows 11 SDK', 'run setup again'):
        assert step in message
    assert engine.complete_windows_sdks(tmp_path / 'missing') == []


def test_toolchain_batch_passes_the_chosen_sdk():
    text = (ROOT / 'setup/engine.py').read_text(encoding='utf-8')
    assert '-no_logo%DEFJAM_SETUP_WINSDK%' in text
    assert "' -winsdk=' + sdks[-1][1]" in text

def test_old_sdk_stops_before_the_extraction():
    text = (ROOT / 'setup/engine.py').read_text(encoding='utf-8')
    execute = text.index('    def execute(self):')
    early = text.index('raise SetupError(windows_sdk_too_old(sdks[-1][1]), 3)', execute)
    assert early < text.index('self.prepare_source()', execute) < text.index('self.extract()', execute)


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
