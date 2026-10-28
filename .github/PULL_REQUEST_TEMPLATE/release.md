## Release vX.Y.Z

<!-- Release PR for a release train (docs/releasing.md). Open with ?template=release.md, or
     gh pr create --body-file from a filled copy. Merge with a merge commit, never squash. -->

Milestone: vX.Y.Z

### Contents (merged into this branch with merge commits)

| PR | Title | Toolkit commits | Decisions |
|---|---|---|---|
| # |  |  | D-new → D |

Toolkit pin: `<sha>` on fork branch `defjam/release-X.Y.Z` (base `<sha>` + cherry-picks).

Not included (and why):

### Candidate gate

Head commit tested: `<sha>`

| Gate | Platform | Result | Log / run |
|---|---|---|---|
| CI (incl. three setup builds) | GitHub | | |
| Full `regress.py` | Windows | | |
| Full `regress.py` | macOS | | |
| Full `regress.py` | Steam Deck / Linux | | |
| Setup install from CI artifacts | Windows | | |
| Clean Update from vPREV | Windows | | |
| Owner play-test | | | |

A result for an older head counts only if every later commit is records or docs.

### Records

- [ ] `PROGRESS.md`: decisions numbered, work log, §0, §7 hand-off
- [ ] `CMakeLists.txt` version, `docs/releases/vX.Y.Z.md`, README setup link, known issues
- [ ] `python -m pytest -q tests/unit` and `python scripts/check-source-tree.py`

### Checklist

- [ ] No game data, saves, captures, memory dumps or translated game code, in files or in text.
- [ ] AI assistance disclosed, if used.
