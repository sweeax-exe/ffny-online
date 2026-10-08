# Contributing

Start with the [build guide](docs/build-and-play.md), [workflows](docs/03-workflows.md)
and [test plan](docs/02-test-plan.md). Read [PROGRESS.md](PROGRESS.md) before
changing behavior. Historical investigations do not override current source or
later decisions.

## Change boundaries

Game integration belongs in `src/`. Generated `src/recomp/gen` is local output,
never a permanent fix. Generic runtime/recompiler changes belong in the pinned
[toolkit fork](https://github.com/vyanhursky/xboxrecomp), as one logical topic with
focused tests. Publish the tested commit before changing the parent gitlink.
The 108 old patches are an archive; do not replay them.

Preserve CPU semantics, entry hooks, save compatibility, render ordering, threading
and timing contracts. Describe the failing behavior and how the fix changes it.
Avoid broad refactors during a regression investigation.

## Validation

Source-only checks need no game data:

```powershell
python -m pytest -q tests/unit
python scripts/check-source-tree.py
```

Runtime, renderer and recompiler changes require the full local game regression;
lifter changes also require the toolkit's compiled CPU tests. Use disposable saves.
The harness backs up/restores the complete save/cache root and retains failed
restore backups. Check the runner's options with `python scripts/regress.py --help`.
Both presets and a boot soak are required for toolkit migrations.

GitHub CI checks source hygiene, project tests, native fixtures and Release runtime
libraries. It has no game input, so it does not build or play the complete game.
Green CI does not replace local gameplay checks.

The standalone Windows setup is built without game data (`scripts/build-setup.ps1`).
Its wizard/silent engine uses the same local pipeline as the build guide. Setup
changes need synthetic recovery/input tests, payload inventory checks and local
installation validation. Only its named game-free release artifact/checksum/
provenance are permitted; never stage binaries or generated game files. See
[installer behavior and acceptance gates](docs/setup-installer.md).

## Submissions

Keep commits focused, with the symptom, change and validation. Retain notices and
disclose AI assistance where relevant. Do not submit disc data, binary excerpts,
generated guest bodies, game executables, saves, captures, PCM or memory dumps,
including inside Markdown or patches. Use addresses and behavior summaries.

Update current guides with behavior changes. Preserve dated history unless a
deliberate publication cleanup requires redaction. Use `git add -A --dry-run`
before committing. Upstream PRs on the maintainer's behalf require separate
approval for each PR or a named batch.

The only capture exception is the two owner-approved README visuals in
`docs/media/` (D58). CI checks their exact fingerprints; additional media
requires owner review. Game artwork is excluded from the project MIT license.
