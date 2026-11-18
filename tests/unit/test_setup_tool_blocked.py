"""Windows refusing a setup tool (error 4551 Smart App Control, 1260, 225) maps to a clear notice."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include "tool_blocked.h"
#include <cstdio>
#include <string>
static std::string n(const std::wstring& w) { std::string s; for (wchar_t c : w) s += c < 128 ? (char)c : '?'; return s; }
int main() {
    for (unsigned long e : {4551ul, 1260ul, 225ul, 5ul}) {
        auto p = describeToolStartFailure(e, L"C:\\tmp\\payload\\python\\python.exe");
        std::printf("E %lu %d\n M %s\n", e, p.blocked ? 1 : 0, n(p.message).c_str());
        for (auto& a : p.advice) std::printf(" A %s\n", n(a).c_str());
    }
    return 0;
}
'''


def _run(tmp_path):
    compiler = shutil.which('cl' if os.name == 'nt' else 'c++')
    if not compiler:
        pytest.skip('native C++ compiler required')
    src = tmp_path / 'driver.cpp'
    src.write_text(DRIVER)
    exe = tmp_path / 'driver.exe'
    inc = str(ROOT / 'setup')
    if os.name == 'nt':
        cmd = [compiler, '/nologo', '/std:c++17', '/EHsc', '/utf-8', '/I' + inc, str(src), '/Fe' + str(exe), '/Fo' + str(tmp_path) + os.sep]
    else:
        cmd = [compiler, '-std=c++17', '-I' + inc, str(src), '-o', str(exe)]
    build = subprocess.run(cmd, capture_output=True, text=True, cwd=tmp_path)
    assert build.returncode == 0, build.stdout + build.stderr
    return subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout


def test_error_mapping(tmp_path):
    out = _run(tmp_path)
    blocks = {}
    for chunk in out.split('E ')[1:]:
        head, *lines = chunk.splitlines()
        code, blocked = head.split()
        blocks[int(code)] = (blocked == '1', lines[0], lines[1:])
    blocked, message, advice = blocks[4551]
    assert blocked and 'error 4551' in message and 'python.exe' in message and 'C:' not in message
    text = ' '.join(advice)
    assert 'Smart App Control settings > Off' in text
    assert 'cannot turn it back on' in text and 'Defender keeps protecting' in text
    blocked, message, advice = blocks[1260]
    assert blocked and 'policy' in message and advice
    blocked, message, advice = blocks[225]
    assert blocked and 'antivirus' in message and 'python.exe' in ' '.join(advice)
    blocked, message, advice = blocks[5]
    assert not blocked and 'Could not start a setup tool (Windows error 5)' in message and not advice
