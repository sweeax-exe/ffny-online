# Troubleshooting

Start with the [latest release](https://github.com/vyanhursky/DefJamFFNY-recomp/releases/latest),
the [setup guide](setup-installer.md) and the guide for the affected feature.
Search [GitHub Issues](https://github.com/vyanhursky/DefJamFFNY-recomp/issues)
before reporting a problem. Bugs and planned work are tracked there; release-specific
test failures and omissions stay in the [versioned release notes](releases).

## Setup or startup fails

Keep the install folder and data folder selected in Setup consistent with the
installation you intend to repair or update. Leave the game-copy field blank to
reuse an existing verified extraction. Use **Clean Update** when changing versions;
**Clean Rebuild** rebuilds the selected version. An operation refused before the
build is not evidence of a new runtime crash. See [update and recovery](setup-installer.md#update-repair-and-uninstall).

- **macOS: “python3” Not Opened.** Setups through v0.10.3 inherited download quarantine
  on the bundled Python. Use a current Setup; v0.10.4 corrected extraction. For a
  historical copy, the original recovery procedure is in the
  [v0.10.4 notes](releases/v0.10.4.md).
- **Windows: LNK1000 / IncrCalcPtrs at the final link.** v0.10.7 disables incremental
  linking for this large image. Use a current Setup and rebuild. The resource file
  named in the linker error does not establish that the icon is corrupt.
- **Windows: closes immediately after setup.** v0.10.7 fixed incomplete guest-memory
  placement. For a failure on a current build, supply its version and newest crash
  text; see [startup diagnosis](research/issue72-windows-startup-memory-layout.md).
- **Linux: game started inside a build container has no sound.** Start the installed
  launcher on the host, where the audio service/libraries are available. The
  container is for building; see [Linux builds](build-macos-linux.md).

## Created-fighter thumbnail remains blank

v0.12.1 fixes creation of new snapshots, but a profile previously saved with an
empty snapshot needs a fresh one: load Story, enter and leave the Wardrobe, then
Crib → Exit → Yes to save. See the [fix and coverage](research/v0121-created-fighter-thumbnail.md).

## HD packs, side bars or uneven presentation

Check pack enablement and priority in [Mods](mod-manager.md), then restart after
changing texture selections. The replacement cache budget is separate from total
process memory. The v0.13 tests used 2048 MiB; their results do not resolve the
older 512 MiB thrashing report or establish that memory growth is a leak. See
[HD settings](hd-textures.md) and the [release qualifications](releases/v0.13.0.md).

For a presentation comparison, try a 60 Hz display mode and render scale2, then
record the mode and the log's `[DISPLAY]` line. A steady frame counter alone does
not establish even presentation. [Display settings](settings-reference.md#display)
describe the available controls; ongoing reports belong in Issues.

## What to include in a report

Give the release/commit, installation route, operating system, CPU/GPU, controller,
exact steps and relevant textual log lines. Source runs write `logs/run-*.log.err`;
Setup's **Open logs** opens its current log. Remove personal paths. If a Windows
crash is too early for a run log, include the Application Error text from Event
Viewer and the output of launching from PowerShell in the install folder.

The [play-test recorder](playtest-recorder.md) and F9 bundle support local analysis.
Raw bundles contain game pictures/audio: keep them local and share only permitted
textual diagnostics. Do not attach game files, saves, translated code, captures,
audio or memory dumps. See [Contributing](../CONTRIBUTING.md).
