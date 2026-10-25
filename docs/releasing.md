# Source releases

A GitHub Release is a versioned source snapshot with game-free setup tools,
checksums and provenance. The setup compiles locally from the player's dump.
The locally built game uses
`win-x64-release` (optimized `RelWithDebInfo`, with diagnostic symbols). Hosted
CI builds runtime libraries and native fixtures with `CMAKE_BUILD_TYPE=Release`.
These three uses of “release” do not imply a downloadable game executable.

## How changes reach a release (release trains)

The full game regression (`python scripts/regress.py`) takes about an hour per
platform, and game runs are serial on each machine. It therefore runs **once per
platform on the release candidate**, not on every pull request. A pull request
proves its own change with cheaper, targeted checks; the release candidate proves
that the changes work together, on the toolkit pin that actually ships.

### 1. A pull request joins a release

A PR that changes the shipped game, setup or launcher is ready to queue when:

- CI is green;
- the author ran the checks for its scope ([Contributing, "Testing"](../CONTRIBUTING.md#testing)):
  unit tests, the source-tree check, `regress.py --quick` on the platform the
  change targets, and the `--only` checks for the area it touches;
- for a toolkit **migration** (a new upstream xboxrecomp version), one full
  `regress.py` on one platform. Migrations are rare and the hardest change to
  trace when a release candidate fails.

Then mark it ready for review and set its **milestone** to the release
(`v0.12.0`). The milestone is the queue. Do not rerun the full regression after
merging main into a queued PR; the candidate gate covers it.

Changes that do not alter what players run (docs, agent instructions, CI-only,
tests-only) merge to `main` directly once CI is green.

### 2. Records on a pull request

Feature branches do not edit `PROGRESS.md`; it conflicted in nearly every merge
when several PRs were open, and decision numbers collided. Instead the PR
description carries:

- `## Decisions`: one `D-new: <decision> | <rationale>` line per decision;
- `## Hand-off`: where the work stands, for the next agent on the branch;
- results with log names, as today.

Code and doc comments cite the PR (`PR #104`) rather than a decision number. Long
notes go in a `docs/research/` file on the branch. The release PR writes
`PROGRESS.md` once: it assigns the next free `D<n>` to each `D-new`, adds the work
log and status rows, and rewrites the hand-off. A branch opened before this rule
that already edits `PROGRESS.md` is resolved by the release PR the same way.

### 3. The release branch

The release agent (playbook: [docs/agent/release.md](agent/release.md)):

1. Creates `release/vX.Y.Z` from `main` and merges each queued PR into it with a
   merge commit, never a squash, so GitHub marks each PR merged when the release
   lands and its history stays bisectable.
2. Builds **one toolkit pin**. Each PR pins its own fork commit; the release pins
   fork branch `defjam/release-X.Y.Z`: the newest base pin among the queued PRs
   plus every other PR's toolkit commits, cherry-picked in merge order. After the
   release, the fork branch named in `.gitmodules` is fast-forwarded to it.
3. Bumps `project(... VERSION ...)`, writes `docs/releases/vX.Y.Z.md`,
   `PROGRESS.md` and the README link.
4. Opens **one release PR** to `main` from the release template
   (`.github/PULL_REQUEST_TEMPLATE/release.md`), listing every queued PR.

### 4. The candidate gate

On the release PR's head, every item ticked in the release PR:

| Gate | Where |
|---|---|
| CI green, including the three setup builds | GitHub |
| Full `regress.py` | Windows, macOS and Steam Deck (or Linux), each once, in parallel on separate machines |
| Setup: one real install from the release PR's CI artifacts, and a Clean Update from the previous release | Windows; others as the release touches them |
| Owner play-test of the candidate | the platforms the release changes |

The play-test replaces per-PR play-tests: a draft waiting only on the owner's
play-test can join a release and be tested there.

### 5. When the candidate fails

1. Rerun only the failing check (`regress.py --only <check>`) to confirm it.
2. Find the cause: rerun that check on each queued PR's branch, or on the release
   branch with one merge reverted at a time.
3. Fix it on the release branch, or take the PR out (revert its merge; it waits
   for the next release).
4. After a fix that touches the runtime, renderer, lifter or toolkit, the full run
   repeats on the affected platforms. Otherwise rerun the failed checks and
   `--quick`.

Keep trains small, about five PRs or a week of work, so a failure is quick to
trace. A hotfix is a release with one PR, cut from `main` (which always equals the
last release plus docs), with the same gate.

## Tagging and publishing

1. Update `project(... VERSION ...)` in CMake only for an actual version change.
   Add `docs/releases/vMAJOR.MINOR.PATCH.md` with supported input, build instructions,
   local gameplay evidence and known limits.
2. Review the source publication audit and run `python scripts/check-source-tree.py`.
   CI checks the current tree; publishing an existing private repository also
   requires auditing all reachable history. Removed files remain in old commits.
3. Merge the release PR to `main` with a merge commit once the candidate gate is
   complete and CI is green. Do not publish generated code, executables, game
   files, saves or audio. Only the explicitly approved images are allowed; CI
   verifies their exact fingerprints (D58, D98).
4. Create an annotated version tag at the merge commit and push that tag.
   The Source release workflow validates the tag against CMake, release notes,
   main ancestry and source policy, then reuses the complete CI workflow.
5. After all checks pass, the workflow creates a draft Release using those notes.
   Verify every asset's SHA-256 against GitHub's digest and its provenance, then
   publish the draft. Version tags are immutable; use a new version to correct a
   published release.

GitHub supplies the source ZIP/tar archives. They do not include submodules;
the supported command-line setup is a recursive clone at the tag. Only the
setup tools (`DefJamSetup-<version>-windows-x64.exe`, `-macos-arm64.dmg`,
`-linux-x64.tar.gz`), each with its `.sha256` and `.provenance.json`, are
attached after standalone setup inventory/tests pass. `check-setup-assets.py`
refuses development payloads and generated/game inputs. Source-tree binary bans
remain intact; a precompiled game is never published. See [setup installer](setup-installer.md)
for behavior and remaining acceptance gates.
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
