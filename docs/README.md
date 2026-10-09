# Documentation

## Install and play

| Guide | Purpose |
|---|---|
| [Build and play](build-and-play.md) | Windows: the installer, or a manual source build from your own dump; launch and saves |
| [Windows setup](setup-installer.md) | The installer in detail: folders, shortcuts, silent mode, repair and removal |
| [Build on macOS and Linux](build-macos-linux.md) | The same pipeline off Windows, and what works there |
| [Launcher and overlay](launcher-and-overlay.md) | The start-up window and the in-game settings screen (F1) |
| [Controllers, keyboard and mouse](10-input.md) | Gamepads, the keyboard player, default keys, remapping and rumble |
| [settings.ini and keybinds](settings-reference.md) | Every setting, the default keys and key names, with examples |
| [HD textures](hd-textures.md) | Optional Windows texture packs: generation, settings and fallback |
| [Known issues](known-issues.md) | Current limitations and what has not been tested |

## Develop and test

| Guide | Purpose |
|---|---|
| [Contributing](../CONTRIBUTING.md) | Reporting problems, where changes go, tests and pull requests |
| [Workflows](03-workflows.md) | The build pipeline, troubleshooting and diagnostics |
| [Runtime reference](05-reference.md) | Rendering architecture and investigation tools |
| [Gameplay test harness](09-testing-harness.md) | Scripted gameplay checks, what each proves, reports and fixtures |
| [Test plan](02-test-plan.md) | Milestone gates and regression discipline |
| [Texture processing and review](hd-texture-workflow.md) | Reproducing the HD texture census, upscale and review galleries |
| [Release process](releasing.md) | Source releases, tag checks and GitHub CI |

## Plans and history

| Document | Purpose |
|---|---|
| [Roadmap](01-plan-review.md#corrected-roadmap-milestones-with-exit-criteria) | Milestone order and exit criteria, inside the September 2026 review of the original plan |
| [PC features plan](07-m6-plan.md) | The display, input, launcher and texture releases, and where their code lives |
| [Testing roadmap](08-testing-roadmap.md) | What the test harness covers and what is still open |
| [Improvement backlog](04-improvement-backlog.md) | Deferred structural work |
| [PROGRESS.md](../PROGRESS.md) | The maintainer's running status, decisions log and hand-off notes |
| [Research notes](research/README.md) | Selected investigations into the game and the runtime |

Release notes: [v0.6.0](releases/v0.6.0.md) · [v0.5.1](releases/v0.5.1.md) ·
[v0.5.0](releases/v0.5.0.md) · [v0.4.1](releases/v0.4.1.md) · [v0.4.0](releases/v0.4.0.md) ·
[v0.3.0](releases/v0.3.0.md) · [v0.2.4](releases/v0.2.4.md) · [v0.2.3](releases/v0.2.3.md) ·
[v0.2.2](releases/v0.2.2.md) · [v0.2.1](releases/v0.2.1.md) · [v0.2.0](releases/v0.2.0.md) ·
[v0.1.0](releases/v0.1.0.md)

Plans, the progress log and research notes are dated records. Their commands
and conclusions may have been superseded; the guides above and the source are
current. They mention local logs, captures and work logs that are not
published, and the 108 numbered toolkit patches that source comments still cite
are retired: the [ledger](research/toolkit-rebase-ledger.md) maps each to the
published fork.

The README visuals in `media/` are the only published images: two gameplay
captures and two screenshots of the port's own interface. See
[media/README.md](media/README.md).
