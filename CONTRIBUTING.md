# Contributing

Thank you for helping with the port. This page covers how to report a problem,
how to set up for development, where each kind of change belongs, which tests
to run and what a pull request needs.

One rule comes before the rest: **this repository never contains game data.**
Do not submit disc data, game files, translated game code, game executables,
saves, captures, audio or memory dumps, in files, issues, pull requests or
pasted into Markdown. Refer to game code by address and describe its behavior.

## Reporting a problem

Check [known issues](docs/known-issues.md) and the open
[issues](https://github.com/vyanhursky/DefJamFFNY-recomp/issues) first, then open
a new issue with the form that fits:

- **Bug report**: something goes wrong in the game.
- **Build or setup problem**: the installer or a source build fails.
- **Feature request**: something the port should do.

A report that can be acted on has the version or commit, how you installed
(installer or source build), your operating system, CPU, GPU and controller, the
steps to reproduce, and the relevant log lines. A source build writes
`logs/run-*.log.err`; the installer writes to `%LOCALAPPDATA%\DefJamSetup\logs`.
Remove personal paths before posting. Screenshots of the problem are welcome.

Report security problems privately: see [SECURITY.md](SECURITY.md).

## Development setup

1. Build the game from source with the [build guide](docs/build-and-play.md)
   (or [macOS and Linux](docs/build-macos-linux.md)). You need your own dump of
   the supported USA Xbox version.
2. Install the test dependencies: `python -m pip install pytest pyxbe capstone Pillow numpy`.
3. Read [workflows](docs/03-workflows.md) for the pipeline, diagnostics and
   troubleshooting, and the [runtime reference](docs/05-reference.md) for how
   rendering and the investigation tools work.

[PROGRESS.md](PROGRESS.md) is the maintainer's running log of status and
decisions. Dated research notes do not override current source or later decisions.

## Where changes go

| Change | Place |
|---|---|
| Game-specific integration, hooks, settings, launcher and overlay | `src/` |
| Hand-written replacements for single game functions | `src/recomp_manual.c` |
| Generic runtime or translator fixes | the [toolkit fork](https://github.com/vyanhursky/xboxrecomp), one topic per commit with focused tests; publish the tested commit, then update the submodule pointer here |
| Build, test and diagnostic tools | `scripts/`, with a unit test in `tests/unit` where there is logic to break |
| Windows installer | `setup/` and `scripts/build-setup.ps1`; see [installer behavior](docs/setup-installer.md) |
| Guides | `docs/`; update the guide in the same pull request as the behavior it describes |

`src/recomp/gen` is generated on your machine and is never committed or edited
by hand as a fix. Keep CPU semantics, entry hooks, save compatibility, render
ordering, threading and timing as they are unless changing them is the point of
the pull request. Avoid broad refactors while investigating a regression.

## Testing

### Without game data

These run anywhere and are what GitHub CI runs. Run them before every pull request:

```powershell
python -m pytest -q tests/unit
python scripts/check-source-tree.py
```

`check-source-tree.py` fails when a tracked file looks like game data, a binary
or copied game code. Run `git add -A --dry-run` before committing to see what
would be staged.

CI also builds the runtime libraries and the toolkit's native test fixtures on
Windows, Linux and macOS, and builds the game-free installer. CI has no game
dump, so it cannot build or play the game. A green CI run does not replace the
checks below.

### With a local build

| Change touches | Run |
|---|---|
| Docs, unit-tested scripts | the two commands above |
| `src/`, settings, input, launcher or overlay | `python scripts/regress.py --quick` (about 10 minutes) |
| Runtime, renderer, translator or generated code; a toolkit pin change | `python scripts/regress.py` (about an hour), on the Release preset |
| Translator (lifter) changes | the full run, plus the toolkit's compiled CPU tests: `python scripts/test-toolkit-msvc.py` |
| A toolkit migration | the full run on both Debug and Release, plus a boot soak (`--only soak --soak 20`) |
| Installer | `scripts/build-setup.ps1`, the setup unit tests, and one real installation from a dump |

`python scripts/regress.py --help` lists the checks, and `--only a,b` runs a
subset. `python scripts/scenario_suite.py run <route> --fixture <folder>` plays
one scenario and writes a report. The
[test harness guide](docs/09-testing-harness.md) explains every check, and the
[test plan](docs/02-test-plan.md) defines the milestone gates.

Rules for game tests:

- Run one at a time. They share the save area, the window and the frame-rate
  measurements.
- Use a disposable save. The harness backs up and restores the whole save folder
  and keeps the backup when a restore fails, but do not point it at saves you
  care about.
- Let a run finish. Stopping one midway can leave the save folder unrestored.
- Reports and captures stay under `logs/`, which git ignores. They contain game
  artwork and are never committed.

Say in the pull request which of these you ran and what the result was. If you
could not run a game test, say so; the maintainer will run it.

## Pull requests

- Keep each pull request to one topic, with focused commits.
- Describe the symptom, the change and how you tested it.
- Use commit subjects such as `fix(audio): ...`, `feat(input): ...`, `docs: ...`.
- Keep existing license notices, and disclose AI assistance where it was used.
- Update the guides your change affects, and add a line to
  [known issues](docs/known-issues.md) if you leave a limitation behind.

The only images in the repository are the README visuals in `docs/media/`,
approved by the owner: two of gameplay and two of the port's own interface. CI
checks their exact fingerprints; new media needs the owner's review. Game artwork is not covered by the MIT license.

Upstream pull requests to `sp00nznet/xboxrecomp` on the project's behalf are
opened by the maintainer, or with the maintainer's approval for each one.

## Conduct and license

Participation is covered by the [code of conduct](CODE_OF_CONDUCT.md).
Contributions are accepted under the repository's [MIT license](LICENSE).
