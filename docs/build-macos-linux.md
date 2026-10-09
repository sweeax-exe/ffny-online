# Build on macOS and Linux

On an Apple Silicon Mac you do not need this page: the
[macOS setup](setup-installer.md#macos-setup-apple-silicon) builds and installs the game
from your own dump without Terminal. Follow this guide to build from source yourself,
or for Linux, where an installer is still a to-do.

The Windows guide is [build-and-play.md](build-and-play.md). This page is the
same pipeline off Windows, since v0.5.0.

| Host | State |
|---|---|
| macOS on Apple Silicon (arm64) | Playable. Built, regression-tested and play-tested on macOS 26 with a DualSense. |
| macOS on Intel | Not tried. |
| Linux x86-64 | The runtime, its fixtures and the Vulkan renderer build and pass in CI. **The game has not been built or run on Linux**; the steps below are the intended ones and may need fixing. Reports are welcome. |
| Steam Deck | Not tried, natively or through Proton. |

What is different from Windows:

- The picture is drawn through Vulkan (MoltenVK on macOS) instead of Direct3D 11.
- There is **no launcher and no in-game overlay** yet. The game starts at once;
  settings are changed by editing `settings.ini`
  ([settings reference](settings-reference.md)).
- The game does not write its own log file; redirect it yourself (see Run).

The source contains no game executable or assets. You need your own **USA Xbox
Def Jam: Fight for NY** dump matching the
[dump manifest](../config/dump-manifest.json), as on Windows. PS2 and GameCube
copies cannot be used.

## Get the source

```bash
git clone --recursive https://github.com/vyanhursky/DefJamFFNY-recomp.git
cd DefJamFFNY-recomp
```

The toolkit is a submodule; without `--recursive`, run
`git submodule update --init --recursive`.

## Prerequisites

macOS, with [Homebrew](https://brew.sh):

```bash
brew install cmake ninja pkg-config sdl3 openssl@3 molten-vk vulkan-headers vulkan-loader shaderc
```

Xcode's command-line tools supply the compiler (`xcode-select --install`).

Linux (Debian or Ubuntu names):

```bash
sudo apt-get install cmake ninja-build pkg-config clang libsdl3-dev libssl-dev libvulkan-dev libshaderc-dev
```

The game needs SDL **3** for its window, sound and pads. Distributions that do
not package it yet (Ubuntu 24.04 among them) need SDL3 built from source first.
CMake 3.20 or newer.

Python 3.12 or newer with `pyxbe`, `capstone` and `pytest`. With
[uv](https://docs.astral.sh/uv/) (`brew install uv`) nothing else has to be
installed; the rest of this page uses this alias:

```bash
alias djpy='uv run --no-project --with pyxbe --with capstone --with pytest python'
```

## Data folder

Pick a folder outside the repository and put the extracted dump in `extracted`
below it. The toolkit can unpack an XISO image itself:

```bash
export DEFJAM_DATA=~/Games/DJFFNY-data
mkdir -p "$DEFJAM_DATA/extracted"
(cd tools/xboxrecomp && python3 -m tools.xiso unpack /path/to/your.xiso.iso -o "$DEFJAM_DATA/extracted")
djpy scripts/verify-dump.py "$DEFJAM_DATA/extracted"
ln -s "$DEFJAM_DATA/extracted" game
```

`verify-dump.py` must report a match before anything else is worth running.
Set `DEFJAM_DATA` in each new shell, or put the `export` in your shell profile.
About 10 GB free is enough for the dump, the generated code and the build.

## Analyze, recompile and build

```bash
djpy scripts/pipeline.py analyze
djpy scripts/pipeline.py recomp
djpy scripts/pipeline.py build              # preset posix-release
```

On an M-series Mac the analysis takes a few minutes, the lift about a minute
and the first build a few more. After pulling a newer version, run
`git submodule update --init --recursive` and then all three steps again:
generated code from an older version must be regenerated.

Output: `build/posix-release/defjam_recomp`. As on Windows, the working copy of
the XBE, the generated C and the executable are local files that must not be
shared.

## Run

From the repository root:

```bash
mkdir -p logs
t=$(date +%Y%m%d-%H%M%S); ./build/posix-release/defjam_recomp > logs/play-$t.log 2> logs/play-$t.log.err
```

The redirection keeps a log of the session; the game prints a great deal, and
`logs/play-*.log.err` is what a problem report needs. Plain
`./build/posix-release/defjam_recomp` works too.

- The game finds `game` in the repository root, and writes `settings.ini` and
  the saves under `DEFJAM_DATA` (`save/`). The first lines of the log say which
  folders were used.
- Gamepads (Xbox, DualSense, Switch Pro and most others) work when plugged in
  or paired, before or after the game starts. The keyboard and mouse are a
  player of their own. Defaults and remapping:
  [Controllers, keyboard and mouse](10-input.md).
- F11 or Alt+Enter switches full screen. Cmd+Q (macOS) or closing the window
  quits.
- Display settings (window size, render scale, gamma) are in `settings.ini`;
  see the [settings reference](settings-reference.md). There is no launcher or
  overlay to change them from.
- `RECOMP_HEADLESS=1` runs without a window or sound device: frames are drawn
  off screen. The tests use it.

## Known limits

- No launcher or overlay (see above); they come in v0.6.2.
- HD textures work since v0.6.1 through the same `[textures]` settings as on Windows
  ([guide](hd-textures.md)); the packs are made by the macOS setup's HD option or by
  `scripts/build-hd-pack.py`. They use more memory (`cache_mb`, 512 MiB by default) and
  add a short hitch the first time a texture is shown.
- No persistent shader cache: the first time an effect appears, the frame can
  hitch while its shader is compiled.
- macOS cannot pin threads to a core, and the game relies on its console's
  single core, so game threads take turns instead. A thread that has just woken
  can wait a few milliseconds for its turn. `RECOMP_GUEST_CORES=all` turns the
  rule off for diagnosis; the game can then freeze, typically at a movie.
- - See also [known issues](known-issues.md), which apply to every host.

## If something goes wrong

- `verify-dump.py` reports a mismatch: the dump is not the supported USA Xbox
  version, or the extraction is incomplete.
- "The game files were not found": the `game` link is missing or does not point
  at the folder that holds `default.xbe`.
- A build step refuses with a freshness error: rerun the step it names
  (`analyze`, `recomp` or `build`).
- No picture on macOS: check that `molten-vk` and `vulkan-loader` are installed;
  a `[D3D8-VK]` line near the top of the log names the device it found.
- The game stops responding but sound continues: keep the log, and if you can,
  take the thread stacks before quitting with
  `sample $(pgrep -f posix-release/defjam_recomp) 3 -file logs/sample.txt`.
- No sound: the log has an `[XA2]` line naming the audio driver and device.

When reporting a problem, give the version or commit, the machine and OS, the
controller, what you did and the relevant lines of `logs/play-*.log.err`.
Review logs for personal paths. Do not attach game data, saves, generated
source or memory dumps.

## Tests

```bash
djpy -m pytest -q tests/unit
RECOMP_HEADLESS=1 djpy scripts/regress.py --quick
```

The regression drives the game with scripted input and needs a save area with
particular profiles in it; without one, the fight routes run and the
saved-profile Story routes fail. [The testing harness](09-testing-harness.md)
has the details. The full `regress.py` takes about an hour on an M-series Mac.
