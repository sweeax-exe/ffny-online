## What and why

<!-- The symptom or need, and what this change does about it. Link the issue if there is one. -->

## Testing

<!-- Say what you ran and the result. See CONTRIBUTING.md, "Testing". The full regression runs once
     per platform on the release candidate (docs/releasing.md), not on each pull request. -->

- [ ] `python -m pytest -q tests/unit`
- [ ] `python scripts/check-source-tree.py`
- [ ] `python scripts/regress.py --quick` on the platform this targets (changes under `src/` or the toolkit)
- [ ] `python scripts/regress.py --only <checks for this area>` (see `docs/agent/release.md`)
- [ ] Toolkit migration only: one full `python scripts/regress.py`
- [ ] Not run locally: <!-- say which, and why -->

## Decisions

<!-- One line each, numbered when the release lands: D-new: <decision> | <rationale>.
     Feature branches do not edit PROGRESS.md. -->

## Release

<!-- Changes to the game, setup or launcher join a release: set the milestone (vX.Y.Z).
     Docs, agent instructions, CI-only and tests-only changes merge to main directly. -->

## Checklist

- [ ] One topic; no unrelated refactoring.
- [ ] No game data, saves, captures, memory dumps or translated game code, in files or in text.
- [ ] Guides updated where behavior changed.
- [ ] AI assistance disclosed, if used.
