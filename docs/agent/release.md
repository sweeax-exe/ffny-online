# Release playbook (release trains)

The process and its reasons are in `docs/releasing.md`; read it first. This file is the release agent's
checklist. Version `X.Y.Z` below; the GitHub milestone `vX.Y.Z` lists the queued PRs.

## Queueing a PR (any agent)

- Run the checks for its scope (`CONTRIBUTING.md`, "Testing"): unit tests, `check-source-tree.py`,
  `regress.py --quick` on the platform the change targets, and the `--only` checks for the area:

  | Area | `--only` |
  |---|---|
  | display, presentation, frame pacing | `fight,ffa,visual` |
  | audio | `intro,combat` (and `scripts/audio-clicks.py` on the opening scene) |
  | input | `fight,versus,two-matches` |
  | launcher, overlay, settings | `launcher,overlay,unlockopt,picture` |
  | saves, profiles, Story | `crib,gym` |
  | setup | no game check; `build-setup.ps1`, setup unit tests, one real install |

- Toolkit **migration** only: one full `regress.py` on one platform before queueing.
- No `PROGRESS.md` edits on the branch. Put `## Decisions` (`D-new: <decision> | <rationale>`) and
  `## Hand-off` in the PR description; cite `PR #N` in comments, not a D-number.
- Mark the PR ready and set the milestone: `gh pr ready N` and `gh pr edit N --milestone vX.Y.Z`.
  A draft whose only open item is the owner's play-test may be queued; say so in the PR.

## Cutting the release (the release agent)

1. **List the train.** `gh pr list --search "milestone:vX.Y.Z" --state open`. Read each PR's description:
   toolkit pin, `D-new` lines, open items. Confirm the list with the owner if a PR is a draft for any reason
   other than the play-test.
2. **Worktree.** `git worktree add -b release/vX.Y.Z ../defjam-release-X.Y.Z origin/main`, then
   `git submodule update --init --recursive`, then the `game` link to the extracted dump (Windows:
   `cmd /c mklink /J game <data>\extracted`; macOS/Linux: `ln -s <data>/extracted game`).
3. **Toolkit.** In a toolkit worktree on the fork, start `defjam/release-X.Y.Z` at the newest base pin among
   the queued PRs (a migration's pin, if one is queued) and `git cherry-pick -x` every other PR's fork
   commits (`git log <their base>..<their pin>`) in merge order. Build the toolkit fixtures the commits touch.
   Push the branch to the fork before the parent records its gitlink.
4. **Merge the PRs** in order, each `git merge --no-ff origin/<branch>`. Resolve `tools/xboxrecomp` to the
   release pin every time. Resolve `PROGRESS.md` by keeping main's side, then write the release records once
   (step 5).
5. **Records.** `PROGRESS.md`: assign the next free `D<n>` to each `D-new` (and renumber any provisional
   numbers older branches used; update the references the branch added), one §6 work-log line per PR with its
   numbers, §0 rows, §7 hand-off. `CMakeLists.txt` version, `docs/releases/vX.Y.Z.md`, README "latest setup"
   link, `docs/known-issues.md`. Run `pytest -q tests/unit` and `check-source-tree.py`.
6. **Release PR.** Push the branch; `gh pr create --base main --head release/vX.Y.Z --milestone vX.Y.Z
   --body-file <file>` from `.github/PULL_REQUEST_TEMPLATE/release.md`, filled in.
7. **Build the candidate** from a clean state: analyze, recomp, build (Release preset). A toolkit pin change
   needs the full re-lift.
8. **Candidate gate.** Start the full `regress.py` in the background on this platform; ask the owner (or the
   agents on those machines) for macOS and Steam Deck runs on the release PR's head commit, and for the
   play-test. Install from the release PR's CI setup artifacts and Clean Update from the previous release.
   Record each result in the release PR's table with log names.
9. **A failure.** `docs/releasing.md` §5: confirm with `--only`, find the PR, fix on the release branch or
   revert its merge and move the PR to the next milestone. Update the release PR.
10. **Publish** when every gate is ticked and CI is green: `gh pr merge N --merge`, annotated tag at the merge
    commit, push the tag, wait for the Source release workflow's draft, verify every asset's SHA-256 against
    GitHub's digest and its provenance, then `gh release edit vX.Y.Z --notes-file docs/releases/vX.Y.Z.md
    --draft=false --latest`.
11. **After.** Fast-forward the fork branch named in `.gitmodules` to `defjam/release-X.Y.Z` (or point
    `.gitmodules` at it in the next release), close the milestone, delete the release worktree (unlink its
    `game` junction first: `cmd /c rmdir game`), and open the next milestone.

## Gotchas

- The submodule is the usual conflict: every PR moves it. Never pick either side; set it to the release pin.
- A merge commit, not squash, for both the queued PRs (into the release branch) and the release PR (into
  main). That is what makes GitHub mark the queued PRs merged.
- `Fixes #N` in a merged commit closes the issue on merge, even when the fix is unconfirmed by the reporter.
- Do not rerun a PR's full regression to "refresh" it while it waits; the candidate gate supersedes it.
