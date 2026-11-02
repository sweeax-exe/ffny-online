"""Source guard: C++ code must not assume std::filesystem::path::u8string() returns std::string.

CMake only asks for a MINIMUM of C++17 (cxx_std_17), so a compiler whose default is
newer (GCC 11+ on CachyOS/Arch/Fedora builds as gnu++20) compiles the project as
C++20, where u8string() returns std::u8string and does not convert to std::string.
That broke Setup on CachyOS twice (issue #83 in 0.10.6, again in 0.12.1 via pc_mods.cpp).
Use pcui::path_utf8() (pc_ui_model.h), which compiles under both.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
RAW = re.compile(r'\.u8string\s*\(')
# The one place allowed to call it: the helper that converts the result.
ALLOWED = {('src/hooks/pc_ui_model.h', 'const auto text = path.u8string();')}


def raw_u8string_uses(root=ROOT):
    found = []
    for path in sorted((root / 'src').rglob('*')):
        if path.suffix not in {'.cpp', '.cc', '.h', '.hpp'}:
            continue
        rel = path.relative_to(root).as_posix()
        for number, line in enumerate(path.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
            if RAW.search(line) and (rel, line.strip()) not in ALLOWED:
                found.append(f'{rel}:{number}: {line.strip()}')
    return found


def test_no_raw_u8string_calls():
    assert raw_u8string_uses() == [], 'use pcui::path_utf8(path) instead of path.u8string()'


def test_guard_catches_the_0_12_1_pattern(tmp_path):
    (tmp_path / 'src').mkdir()
    (tmp_path / 'src/bad.cpp').write_text('if (ok) interpreter = python.u8string();\n')
    assert raw_u8string_uses(tmp_path)
