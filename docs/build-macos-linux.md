# Build on macOS and Linux

The Windows guide is [build-and-play.md](build-and-play.md) and is the supported
route. This page is the same pipeline on macOS (Apple Silicon) and Linux, on the
branch `feat/macos-linux-port`. Status is in `PROGRESS.md`; in short, the game
boots and draws through Vulkan on macOS, with the window, sound and pads on SDL3;
no play-test is behind it yet. Linux has been compiled and
fixture-tested only.

The source contains no game executable or assets. You need your own **USA Xbox
Def Jam: Fight for NY** dump, as on Windows.

## Prerequisites

macOS, with [Homebrew](https://brew.sh):

```bash
brew install cmake ninja pkg-config sdl3 openssl@3 molten-vk vulkan-headers vulkan-loader shaderc
```

Linux (Debian or Ubuntu names):

```bash
sudo apt-get install cmake ninja-build pkg-config clang libsdl3-dev libssl-dev libvulkan-dev libshaderc-dev
```

Python 3.12 or newer with `pyxbe`, `capstone` and `pytest`. With
[uv](https://docs.astral.sh/uv/) nothing has to be installed first:

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

## Analyze, recompile and build

```bash
djpy scripts/pipeline.py analyze
djpy scripts/pipeline.py recomp
djpy scripts/pipeline.py build              # preset posix-release
```

Output: `build/posix-release/defjam_recomp`. As on Windows, the working copy of
the XBE, the generated C and the executable are local files that must not be
shared.

## Run

```bash
./build/posix-release/defjam_recomp
```

It finds `game` in the repository root and writes `settings.ini` and saves under
`DEFJAM_DATA`. F11 or Alt+Enter switches full screen. `RECOMP_HEADLESS=1` runs
without a window: frames are drawn off screen, which is what the tests use.

The start-up launcher and the in-game settings overlay (v0.4.0) are Windows-only
for now: they draw through Direct3D 11 and Win32 window messages. Here the game
starts at once and `settings.ini` is edited by hand (`docs/settings-reference.md`).

## Tests

```bash
djpy -m pytest -q tests/unit
RECOMP_HEADLESS=1 djpy scripts/regress.py --quick
```
