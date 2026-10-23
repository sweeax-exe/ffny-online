# Issue #83: CachyOS installation compile failure

Investigated 2026-10-23 on public main `dcd0df0`, branch `fix/issue83-cachyos`.
Report: [issue #83](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/83),
including the reporter's v0.10.6 setup log.

## Cause

The C stages complete, but `/usr/bin/c++` fails compiling `pc_ui.cpp` and
`pc_ui_actions.cpp`. The log uses libstdc++ 16, no explicit `-std` flag, and
reports conversions from `std::u8string` to `std::string` as errors.
[GCC 16 defaults to C++20](https://gcc.gnu.org/gcc-16/changes.html).
In that mode `std::filesystem::path::u8string()` returns `std::u8string`
(`char8_t`), while our launcher interfaces use UTF-8 `std::string` (`char`).
The CMake `cxx_std_17` requirement is a minimum, so it allows this compiler default.
The pre-existing native tests explicitly selected C++17 and missed the problem.
This is independent of the player's dump and Vulkan driver.

## Fix

`pcui::path_utf8` copies the UTF-8 representation into a byte string, accepting
both C++17's `std::string` and C++20's `std::u8string`. All ten launcher path
conversion sites use it: inventory names, Windows environment paths, settings,
data/save/mod/install/log folders, Linux file reveal, and POSIX setup launching.
The conversion reads through `const char *`, which can access character object
representations, and uses the byte count rather than a null-terminated scan.
It preserves UTF-8 on Windows too; `path::string()` would use native encoding.
No compiler downgrade, language restriction, setup dependency or toolkit pin change.

Both native launcher test fixtures now compile in C++17 and C++20. Additional
inventory tests cover Unicode parent directories and preservation of non-ASCII
names in validation messages. The Windows inventory driver takes wide arguments;
its output is decoded as UTF-8.

## Validation

- Before fix: GCC 13.3 on Ubuntu 24.04 WSL, explicitly selecting C++20,
  reproduced the exact `push_back(std::u8string)` failure in the issue. C++17
  passed 8 tests; all 8 C++20 cases failed to compile.
- After fix: Linux GCC 13.3, SDL 3.4.18: **30 passed**, no skips,
  `python3 -m pytest -q -p no:cacheprovider --tb=short
  tests/unit/test_launcher_model.py tests/unit/test_launcher_actions.py`.
- Windows VS 2019 / MSVC 14.29: **30 passed**, no skips, same two files.
- Both reported translation units (`pc_ui.cpp`, `pc_ui_actions.cpp`) pass
  `g++ -std=c++20 -fsyntax-only` with real SDL3, ImGui 1.92.9b, Vulkan and
  toolkit headers. Existing C++20 `u8path` deprecation warnings remain.
- Tooling checks (`test_progress.py`, `test_source_release.py`,
  `test_compile_game_sources.py`): **51 passed**. Source publication policy:
  **435 tracked files, zero failures**; staged diff passes whitespace checks.
- No game build/run or end-to-end Setup install on CachyOS; the exact GCC 16
  installation environment is not available here. The language-mode failure
  was reproduced and fixed with GCC on Linux, and Windows paths were checked.

## Hand-off

The fix is local, not released. Publish/review this branch when authorized;
then a game-free Linux setup built from the fixed source can be tested by the
reporter. Updating to unmodified v0.10.7 alone does not include this change.
A source-build workaround on the affected release is to reconfigure its existing
source/build folder with `cmake --preset posix-release -DCMAKE_CXX_STANDARD=17`,
then `cmake --build --preset posix-release --target defjam_recomp`.
Setup sanitizes compiler environment variables, so setting `CXXFLAGS` outside
Setup is not a reliable workaround. Do not close #83 as reporter-confirmed yet.
