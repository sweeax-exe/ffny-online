# Build on macOS and Linux

On an Apple Silicon Mac or on x86-64 Linux, including the Steam Deck, you do not need
this page: the [macOS setup](setup-installer.md#macos-setup-apple-silicon) and the
[Linux setup](setup-installer.md#linux-setup) build and install the game from your own
dump. Follow this guide to build from source yourself.

The Windows guide is [build-and-play.md](build-and-play.md). This page is the
same pipeline off Windows, since v0.5.0.

| Host | State |
|---|---|
| macOS on Apple Silicon (arm64) | Playable. Built, regression-tested and play-tested on macOS 26 with a DualSense. |
| macOS on Intel | Not tried. |
| Linux x86-64 | Builds and runs natively (D95). Built with Clang 20 and run on a Steam Deck; 12 of the 14 regression checks pass on the Deck; combat and versus fail only on a few audio-queue underruns ([#11](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/11)). Play-tested by the owner on the Deck. |
| Steam Deck | Native, through Vulkan on the Deck's GPU, with PipeWire sound and the Deck's own controls as a pad. SteamOS has no compiler: build in a container (below) and run the game on SteamOS itself. Proton not tried. |

What is different from Windows:

- The picture is drawn through Vulkan (MoltenVK on macOS) instead of Direct3D 11.
- The launcher (a window before the game starts) and the in-game overlay (F1) are there since v0.6.2, as
  on Windows ([guide](launcher-and-overlay.md)); `settings.ini` still works too
  ([settings reference](settings-reference.md)). Hold Shift while the game starts to see
  the launcher when it is skipped.
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

Arch Linux, or an Arch container (`sudo pacman -S cmake ninja clang pkgconf sdl3 shaderc
vulkan-headers vulkan-icd-loader openssl`).

The game needs SDL **3** for its window, sound and pads. Distributions that do
not package it yet (Ubuntu 24.04 among them) need SDL3 built from source first.
CMake 3.20 or newer. Build with Clang (`export CC=clang CXX=clang++` before the
first build), the compiler the port is tested with; GCC also builds it.

### Steam Deck

SteamOS has no compiler and a read-only system, but it has podman and
distrobox, and it ships the libraries the game needs at run time (SDL3, the
Vulkan loader, shaderc). Build in a container and run the game on SteamOS.

The game must be built against libraries no newer than SteamOS's own, or it
will not start there (a current Arch Linux container links Dear ImGui's maths
against a glibc SteamOS does not have). So the build container is Arch Linux as
of an archive date whose glibc, SDL3, shaderc and Vulkan loader match SteamOS
3.8, the same pins the Linux setup uses. Use podman directly: distrobox would
upgrade the container to today's packages on its first start.

```bash
mkdir -p ~/defjam-build-image && cd ~/defjam-build-image
cat > Containerfile <<'EOF'
FROM docker.io/library/archlinux@sha256:af82cf8d6d8e461701c28e339bb39de2f22376f06382c205fe22f86175889f4d
RUN echo 'Server = https://archive.archlinux.org/repos/2025/07/27/$repo/os/$arch' > /etc/pacman.d/mirrorlist \
 && echo 'faked-system-time 20250727T120000' >> /etc/pacman.d/gnupg/gpg.conf \
 && pacman -Syy --noconfirm --needed cmake ninja clang pkgconf sdl3 shaderc vulkan-headers \
        vulkan-icd-loader openssl python git \
 && sed -i '/^faked-system-time/d' /etc/pacman.d/gnupg/gpg.conf && pacman -Scc --noconfirm \
 && python -m venv /opt/djpy && /opt/djpy/bin/pip install -q pyxbe capstone pytest numpy
ENV CC=clang CXX=clang++
EOF
podman build -t localhost/defjam-dev .
alias djc='podman run --rm --init -it --userns=keep-id --security-opt label=disable -v "$HOME:$HOME" -w "$PWD" -e DEFJAM_DATA localhost/defjam-dev'
alias djpy='djc /opt/djpy/bin/python'   # instead of the uv alias below
```

The `faked-system-time` line lets pacman check the archive's signatures as of
that date: some packagers' keys have expired since. The container sees your
home folder at the same path, so the repository and the data folder are the
same inside and out. Use this `djpy` for the pipeline steps below (and `djc` in
front of any other build command), and start the game itself on SteamOS, from a
Desktop Mode terminal:
`ldd -r build/posix-release/defjam_recomp` there lists anything the build
needs that SteamOS lacks.

`--init` matters: without it the setup engine's process-tree test
(`tests/unit/test_setup_engine.py`) fails, because the container's first process
does not reap children. The image has no SDL2, so a native fixture that builds the
audio library on its own (`memory_layout_posix`, `kernel_memory_posix`,
`kernel_path_posix`, `kernel_bridge`) needs `-DXBOXRECOMP_SDL3=ON` on its
`cmake -S` line, as the game build has.

The scripted regression (`scripts/regress.py`) runs on SteamOS itself, not in the container, from a
venv that has `pytest`, `numpy`, `pillow`, `pyxbe` and `capstone`. Start it in `gamescope --backend
headless`, which needs no screen and runs the whole table in one go (40 minutes):

```bash
export DEFJAM_DATA=~/Games/DefJamRecompiled/Data
gamescope --backend headless -W 1280 -H 800 -r 90 -- ~/djvenv/bin/python -B scripts/regress.py --preset posix-release
```

Plain `gamescope` (nested in the desktop session) aborts on a second game launch, which kills the
harness. A run killed that way leaves the save changed: the next run refuses to start, and
`python scripts/harness.py save-recover` shows the copy to restore (`--restore`) or keep (`--accept`).

Python 3.12 or newer with `pyxbe`, `capstone` and `pytest`; the unit tests and the audio scripts also
need `numpy`, `pillow` and `cryptography` (the mod manager). With
[uv](https://docs.astral.sh/uv/) (`brew install uv`) nothing else has to be
installed; the rest of this page uses this alias:

```bash
alias djpy='uv run --no-project --with pyxbe --with capstone --with pytest --with numpy --with pillow --with cryptography python'
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

- The game finds `game` in the repository root, and writes `settings.ini`, the
  saves (`save/`) and its shader cache (`cache/vulkan/`) under `DEFJAM_DATA`. The
  first lines of the log say which folders were used.
- Gamepads (Xbox, DualSense, Switch Pro and most others) work when plugged in
  or paired, before or after the game starts. The keyboard and mouse are a
  player of their own. Defaults and remapping:
  [Controllers, keyboard and mouse](10-input.md).
- F11 or Alt+Enter switches full screen. Cmd+Q (macOS) or closing the window
  quits.
- Display settings (window size, render scale, gamma) are in the launcher, the F1
  overlay and `settings.ini`; see the [settings reference](settings-reference.md).
- `RECOMP_HEADLESS=1` runs without a window or sound device: frames are drawn
  off screen. The tests use it.

## Known limits

- The window follows the display's pixel density (2560x1920 pixels for the default 1280x960 window on a Retina
  display, as `[HOST] window ...` in the log says), and the overlay is drawn at that resolution. Window width and
  height changed in the overlay resize the window at once.
- HD textures work through the same `[textures]` settings as on Windows (macOS since
  v0.6.1, Linux since v0.7.2; [guide](hd-textures.md)); the packs are made by the macOS
  or Linux setup's HD option or by `scripts/build-hd-pack.py`. They use more memory
  (`cache_mb`, 2048 MiB by default: a four-fighter match holds about 2 GiB of them). A
  replacement is decoded on background threads and the original texture is drawn until it is
  ready, so the first showing of a screen no longer stalls the game; the art arrives a moment
  after the screen does.
- Shaders are compiled the first time an effect appears, and that frame can hitch.
  The compiled shaders and the graphics driver's pipeline cache are kept in
  `DEFJAM_DATA/cache/vulkan`, so this happens once per effect, not once per launch.
  Deleting that folder is safe (it is rebuilt); `RECOMP_SHADER_CACHE=off` runs
  without it.
- Vsync (`settings.ini` `vsync`) is only used on a display refreshing at a
  multiple of 60 Hz, as on Windows; elsewhere the game paces itself. The Steam
  Deck OLED's panel runs at 90 Hz, so it plays without vsync there; the log's
  `[HOST] display refresh` line says what was found.
- macOS cannot pin threads to a core, and the game relies on its console's
  single core, so game threads take turns instead. A thread that has just woken
  can wait a few milliseconds for its turn. `RECOMP_GUEST_CORES=all` turns the
  rule off for diagnosis; the game can then freeze, typically at a movie.
- See also [known issues](known-issues.md), which apply to every host.

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
  `python3 scripts/native-stacks.py > logs/stacks.txt` (lldb on macOS; gdb or
  elfutils' `eu-stack` on Linux, as root or with `kernel.yama.ptrace_scope` at 0).
  `sample $(pgrep -x defjam_recomp) 3 -file logs/sample.txt` also works on macOS. The
  other hang tools in `scripts/` (`sample-threads.py`, `guest-stack.py`) are
  Windows-only.
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
has the details. The full `regress.py` takes about an hour on an M-series Mac
and about 70 minutes on a Steam Deck.
