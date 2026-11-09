# Documentation

## Install and play

| Guide | Purpose |
|---|---|
| [Install guide](install-guide.md) | Def Jam Recompiled, step by step: download, first run, Add to Steam, updates, repair and uninstall |
| [Build and play](build-and-play.md) | Windows: the installer, or a manual source build from your own dump; launch and saves |
| [Setup installers](setup-installer.md) | The older `DefJamSetup` tools on Windows, macOS and Linux/Steam Deck: folders, shortcuts, silent mode, repair and removal |
| [Build on macOS and Linux](build-macos-linux.md) | The same pipeline off Windows, and what works there |
| [Launcher and overlay](launcher-and-overlay.md) | The start-up window and the in-game settings screen (F1) |
| [Controllers, keyboard and mouse](10-input.md) | Gamepads, the keyboard player, default keys, remapping and rumble |
| [settings.ini and keybinds](settings-reference.md) | Every setting, the default keys and key names, with examples |
| [HD textures](hd-textures.md) | Optional texture packs on Windows, macOS and Linux: generation, settings and fallback |
| [Custom songs](custom-songs.md) | Your own music files (MP3, FLAC, Ogg, WAV) in the Beatbox, the menus and fights; how many the game holds |
| [Language packs](language-packs.md) | Playing in another language with a language mod, subtitles in every Story cutscene, use with texture packs, and making a pack |
| [Importing Xbox saves](importing-xbox-saves.md) | Bring profiles from an original Xbox, an emulator or shared save zips into the port |
| [Troubleshooting](troubleshooting.md) | Setup/startup recovery, saved thumbnails, pack/presentation checks and permitted report diagnostics |

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
| [Texture Modding — Getting Started](texture-modding-getting-started.md) | Find original images, edit an atlas, build a first replacement pack, set load order and share it |
| [Texture authoring boundary](hd-texture-workflow.md) | Modding Tools ownership and the frozen game-side Setup/runtime compatibility snapshot |
| [Release process](releasing.md) | Release trains (how PRs are queued and tested together), tag checks and GitHub CI |
| [Maintaining the GitHub introduction](github-introduction.md) | About wording, README structure and the documentation release pass |
| [Media capture plan](media/capture-plan.md) | Current-release screenshots/clips, export settings and capture handoff |

## Work tracking and technical evidence

Open bugs and requests live in [GitHub Issues](https://github.com/vyanhursky/DefJamFFNY-recomp/issues).
[Milestones](https://github.com/vyanhursky/DefJamFFNY-recomp/milestones) and
[open PRs](https://github.com/vyanhursky/DefJamFFNY-recomp/pulls) describe release scope;
feature requests are not promises. We do not maintain a separate roadmap/backlog file.

| Reference | Purpose |
|---|---|
| [Widescreen](widescreen/README.md) | Implemented design, element/camera references and test limits |
| [Research](research/README.md) | Curated reverse-engineering findings and dated acceptance evidence |
| [PROGRESS.md](../PROGRESS.md) | Compact current handoff and a standalone historical decision export |
| [v0.13 reproduction](regression-v0.13.0.md) | Exact release-specific commands and qualified results; not a pending RC |

Release notes: [GitHub releases](https://github.com/vyanhursky/DefJamFFNY-recomp/releases)
and the [versioned notes](releases).

Historical decisions and research evidence are dated records. Their commands
and conclusions may have been superseded; the guides above and the source are
current. They mention local logs, captures and work logs that are not
published, and the 108 numbered toolkit patches that source comments still cite
are retired: the [ledger](research/toolkit-rebase-ledger.md) maps each to the
published fork.

The [media index](media/README.md) records the approved gameplay and interface
captures. [Branding](../branding/README.md) documents the separately approved
application icons, Steam artwork and social preview. Game artwork retains its
original ownership; the MIT license does not cover it.
