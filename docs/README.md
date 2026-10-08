# Documentation

Start with the guides below. Dated research and work logs preserve the development
history; their conclusions and old commands may have been superseded.

| Guide | Purpose |
|---|---|
| [Build and play](build-and-play.md) | Prerequisites, your own dump, Release build, launch and saves |
| [Windows setup](setup-installer.md) | Wizard/silent installation, folders, desktop shortcut, launch, repair and removal |
| [Build on macOS and Linux](build-macos-linux.md) | The same pipeline off Windows; work in progress |
| [Known issues](known-issues.md) | Current limitations and unvalidated features |
| [Contributing](../CONTRIBUTING.md) | Change boundaries, tests and reporting |
| [Workflows](03-workflows.md) | Maintainer pipeline, troubleshooting and diagnostics |
| [Runtime reference](05-reference.md) | Rendering architecture and investigation tools |
| [M6 plan](07-m6-plan.md) | PC features: slices, releases and where the code goes |
| [Launcher and overlay](launcher-and-overlay.md) | The start-up window and the in-game settings screen (F1) |
| [settings.ini and keybinds](settings-reference.md) | Every setting, the default keys and key names, with examples |
| [Controllers, keyboard and mouse](10-input.md) | Gamepads, the keyboard player, default keys, remapping, rumble and settings |
| [Gameplay test harness](09-testing-harness.md) | Scripted gameplay checks, what each proves, reports and fixtures |
| [Test plan](02-test-plan.md) | Milestone gates and regression discipline |
| [Release process](releasing.md) | Source releases, tag checks and GitHub CI |
| [v0.1.0 notes](releases/v0.1.0.md) | Scope of the first public version |

[PROGRESS.md](../PROGRESS.md) carries current decisions and the handoff.
[The roadmap](01-plan-review.md) contains the approved milestone order and exit
criteria. Its initial review is dated September 2026; historical toolkit and
emulator observations are not a current support matrix.

The [rebase acceptance](research/toolkit-rebase-acceptance.md) records the October
2026 checks and owner play-test. The [108-patch ledger](research/toolkit-rebase-ledger.md)
maps the old archive to the published fork. [The rebase plan](06-toolkit-rebase-plan.md)
preserves the migration and upstream contribution process.

`research/` contains dated investigations. `worklog/` contains archived progress
and superseded handoffs. The two owner-approved README visuals in `media/`
are the only published captures (D58). Other local logs, screenshots, PCM recordings and generated
game code referenced in those records are not shipped. Historical local paths
describe the development machine; use your own paths in the build guide.
