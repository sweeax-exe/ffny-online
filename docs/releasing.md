# Source releases

A GitHub Release is a versioned source snapshot with a game-free Windows setup
tool, checksum and provenance. The setup compiles locally from the player's dump.
The locally built game uses
`win-x64-release` (optimized `RelWithDebInfo`, with diagnostic symbols). Hosted
CI builds runtime libraries and native fixtures with `CMAKE_BUILD_TYPE=Release`.
These three uses of “release” do not imply a downloadable game executable.

## Release workflow

1. Update `project(... VERSION ...)` in CMake only for an actual version change.
   Add `docs/releases/vMAJOR.MINOR.PATCH.md` with supported input, build instructions,
   local gameplay evidence and known limits.
2. Review the source publication audit and run `python scripts/check-source-tree.py`.
   CI checks the current tree; publishing an existing private repository also
   requires auditing all reachable history. Removed files remain in old commits.
3. Merge the reviewed source to `main` and wait for green CI. Do not publish
   generated code, executables, game files, saves or audio. Only the two
   explicitly approved README visuals in `docs/media/` are allowed; CI verifies
   their exact fingerprints (D58).
4. Create an annotated version tag at the reviewed main commit and push that tag.
   The Source release workflow validates the tag against CMake, release notes,
   main ancestry and source policy, then reuses the complete CI workflow.
5. After all checks pass, the workflow creates a draft Release using those notes.
   Inspect its tag and contents, then publish the draft. Version tags are immutable;
   use a new version to correct a published release.

GitHub supplies the source ZIP/tar archives. They do not include submodules;
the supported command-line setup is a recursive clone at the tag. Only
`DefJamSetup-<version>-windows-x64.exe`, its `.sha256` and `.provenance.json` are
attached after standalone setup inventory/tests pass. `check-setup-assets.py`
refuses development payloads and generated/game inputs. Source-tree binary bans
remain intact; a precompiled game is never published. See [setup installer](setup-installer.md)
for behavior and remaining Windows acceptance gates.
The fork URL and exact gitlink make dependency source
and license notices available without game data.

## CI coverage

CI runs project tests on Ubuntu, native save compatibility and toolkit CPU tests
under MSVC, the runtime Release build, and separate Release kernel bridge,
directory, USB and vertex-program fixtures. WARP exercises D3D11 without a physical
GPU. Each native project has registered tests; empty CTest suites fail.

`scripts/test-toolkit-msvc.py` selects `cl` for the pinned toolkit's shared
compiled-fixture helper, which otherwise prefers an installed Clang. It leaves
test sources, assertions and `/O2` unchanged and validates a reviewed helper
source fingerprint before replacement. Other compiler-dependent tests retain their own gates. This checks
Windows/MSVC; it does not establish Clang portability.

Game regression and captures require a verified user dump on a local machine.
They remain separate from hosted CI and must precede a gameplay-affecting release.
See [Contributing](../CONTRIBUTING.md) and the [test plan](02-test-plan.md).
