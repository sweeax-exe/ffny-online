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

Published [PR #90](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/90)
on the owner's request. It links #83. [CI run 39981681675](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/39981681675)
passed all 28 jobs after retrying a Ninja download timeout in `kernel_directory`
(the failed attempt never reached compilation).

The reporter's [Linux candidate artifact](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/39981681675/artifacts/12512749441)
is `linux-setup-assets`. Its setup is version 0.10.7 with source `6c9563e`
(the PR merge containing fix `8bf8fbf`), toolkit `8589f88`, development false.
Downloaded and verified the artifact ZIP against GitHub's digest, the installer
against its checksum/provenance, all 3,248 payload files against the inventory,
and both patched launcher source files byte for byte against the tested fix.
Installer SHA-256: `9d8866e267e30e8f0fb5563cae6af3c991f0a3ce349ade5c915ac3cecc4e813a`.
Local receipt: `build/issue83-artifact-verification.json`.

[Test instructions and download link posted on #83](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/83#issuecomment-6369126220).
They specify Bundled release (offline) so the candidate's fixed source is used,
Install for a failed first install, Clean Update from a working 0.10.6, or
Clean Rebuild for an existing 0.10.7. The [reporter confirmed on 2026-10-23](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/83#issuecomment-6369284367):
"It is working now. I'm watching the intro." The owner also reports a second
successful test from a Discord user. The owner authorized merging PR #90 and
closing #83, with the fix to ship in the next release the owner makes. All 29
checks on PR head `8d5ddf5` passed. No new release or tag is requested. Updating to the published v0.10.7 alone does not include this change.
A source-build workaround on the affected release is to reconfigure its existing
source/build folder with `cmake --preset posix-release -DCMAKE_CXX_STANDARD=17`,
then `cmake --build --preset posix-release --target defjam_recomp`.
Setup sanitizes compiler environment variables, so setting `CXXFLAGS` outside
Setup is not a reliable workaround. Issue #83 can now be closed as fixed with PR #90; reporter confirmation is recorded above.
