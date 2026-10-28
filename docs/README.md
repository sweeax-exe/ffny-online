# Documentation

## Install and play

| Guide | Purpose |
|---|---|
| [Build and play](build-and-play.md) | Windows: the installer, or a manual source build from your own dump; launch and saves |
| [Setup installers](setup-installer.md) | Windows, macOS and Linux/Steam Deck: folders, shortcuts, silent mode, repair and removal |
| [Build on macOS and Linux](build-macos-linux.md) | The same pipeline off Windows, and what works there |
| [Launcher and overlay](launcher-and-overlay.md) | The start-up window and the in-game settings screen (F1) |
| [Controllers, keyboard and mouse](10-input.md) | Gamepads, the keyboard player, default keys, remapping and rumble |
| [settings.ini and keybinds](settings-reference.md) | Every setting, the default keys and key names, with examples |
| [HD textures](hd-textures.md) | Optional texture packs on Windows, macOS and Linux: generation, settings and fallback |
| [Known issues](known-issues.md) | Current limitations and what has not been tested |

## Develop and test

| Guide | Purpose |
|---|---|
| [Contributing](../CONTRIBUTING.md) | Reporting problems, where changes go, tests and pull requests |
| [Workflows](03-workflows.md) | The build pipeline, troubleshooting and diagnostics |
| [Runtime reference](05-reference.md) | Rendering architecture and investigation tools |
| [Gameplay test harness](09-testing-harness.md) | Scripted gameplay checks, what each proves, reports and fixtures |
| [Play-test recorder](playtest-recorder.md) | Recording ordinary play (frame times, events, sound, the last seconds of pictures), F9 (or View + left stick click) bug bundles and reading them with `bug-bundle.py` |
| [The developer menu](developer-menu.md) | EA's debug menu, restored: any match (fighters, venue, match type), any Story scene, the venue viewer; its controls and each option |
| [Playing a chosen Story scene](scene-selector.md) | Any in-engine Story cutscene by name through the game's revived developer module: the overlay's Diagnostics tab, `scenarios.py scene`, or `RECOMP_SCENE`; no save or menus |
| [Test plan](02-test-plan.md) | Milestone gates and regression discipline |
| [Agent playbooks](agent/README.md) | Reading a failed run (`triage.py`), diagnostics, the harness, sub-agent briefs; what `AGENTS.md` points to |
| [Texture processing and review](hd-texture-workflow.md) | Reproducing the HD texture census, upscale and review galleries |
| [Release process](releasing.md) | Release trains (how PRs are queued and tested together), tag checks and GitHub CI |
| [Maintaining the GitHub introduction](github-introduction.md) | About wording, README structure and the documentation release pass |
| [Media capture plan](media/capture-plan.md) | Current-release screenshots/clips, export settings and capture handoff |

## Plans and history

| Document | Purpose |
|---|---|
| [Roadmap](01-plan-review.md#corrected-roadmap-milestones-with-exit-criteria) | Milestone order and exit criteria, inside the September 2026 review of the original plan |
| [PC features plan](07-m6-plan.md) | The display, input, launcher and texture releases, and where their code lives |
| [Widescreen implementation pack](widescreen/README.md) | Findings, design, element catalog, work packages and play-test plan for 16:9, 16:10 and ultrawide |
| [Testing roadmap](08-testing-roadmap.md) | What the test harness covers and what is still open |
| [Improvement backlog](04-improvement-backlog.md) | Deferred structural work |
| [PROGRESS.md](../PROGRESS.md) | The maintainer's running status, decisions log and hand-off notes |
| [Research notes](research/README.md) | Selected investigations into the game and the runtime |

Release notes: [GitHub releases](https://github.com/vyanhursky/DefJamFFNY-recomp/releases)
and the [versioned notes](releases).

Plans, the progress log and research notes are dated records. Their commands
and conclusions may have been superseded; the guides above and the source are
current. They mention local logs, captures and work logs that are not
published, and the 108 numbered toolkit patches that source comments still cite
are retired: the [ledger](research/toolkit-rebase-ledger.md) maps each to the
published fork.

The [media index](media/README.md) records the approved gameplay and interface
captures. [Branding](../branding/README.md) documents the separately approved
application icons, Steam artwork and social preview. Game artwork retains its
original ownership; the MIT license does not cover it.
