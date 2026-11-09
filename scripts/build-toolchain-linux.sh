#!/bin/bash
# Build the libraries the game links on Linux, with the payload's own compiler.
#
#   scripts/build-toolchain-linux.sh <payload tools folder> <sources folder> <output prefix>
#
# <payload tools folder> is the staged tools/ of the setup payload: zig/ and the bin/zig-cc,
# zig-c++, zig-ar and zig-ranlib wrappers (x86_64-linux-gnu, glibc 2.31; scripts/linux_payload.py).
# <sources folder> holds what scripts/linux_payload.py fetched and checked against
# setup/dependencies-linux.json: SDL3-<version>.tar.gz, shaderc/ (its DEPS synced) and
# vulkan-headers/. Writes into <output prefix>:
#   lib/libSDL3.a, lib/cmake/SDL3/   SDL3 (the toolkit's pin), static, every video and sound backend
#                                    it can load at run time (X11, Wayland, PipeWire, PulseAudio, ALSA)
#   lib/libshaderc_combined.a        shaderc with glslang and SPIRV-Tools, static
#   include/                         their headers and the Vulkan headers
#   link/libvulkan.so                a stub with the loader's entry points, for linking only: the
#                                    game names libvulkan.so.1 and the player's own loader is used
# It runs in the container pinned in setup/dependencies-linux.json (app_build), which supplies
# only headers (X11, Wayland, the sound servers, D-Bus, udev): libc and the C++ library are Zig's.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
[ $# -eq 3 ] || { echo "usage: $0 <payload tools folder> <sources folder> <output prefix>" >&2; exit 2; }
TOOLS=$(readlink -f "$1")
SOURCES=$(readlink -f "$2")
OUT=$(readlink -m "$3")
pin() { python3 -c 'import json,sys; d=json.load(open("setup/dependencies-linux.json"))["app_build"]; print(d[sys.argv[1]] if sys.argv[1]!="packages" else " ".join(d["packages"]))' "$1"; }

if [ -z "${DEFJAM_TOOLCHAIN_BUILD_INSIDE:-}" ]; then
    ENGINE=$(command -v podman || command -v docker || true)
    [ -n "$ENGINE" ] || { echo "podman or docker is needed to build the toolchain libraries" >&2; exit 1; }
    mkdir -p "$OUT"
    exec "$ENGINE" run --rm -v "$ROOT:$ROOT:ro" -v "$TOOLS:$TOOLS:ro" -v "$SOURCES:$SOURCES:ro" -v "$OUT:$OUT" \
        -w "$ROOT" -e DEFJAM_TOOLCHAIN_BUILD_INSIDE=1 -e DEFJAM_APP_PACKAGES="$(pin packages)" "$(pin image)" \
        bash scripts/build-toolchain-linux.sh "$TOOLS" "$SOURCES" "$OUT"
fi

# ---- inside the container ----
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
# shellcheck disable=SC2086
apt-get install -y -qq --no-install-recommends $DEFJAM_APP_PACKAGES > /dev/null
WORK=$(mktemp -d)
# The steps below write to logs in $WORK; a failing one shows the end of the newest.
trap 'log=$(ls -t "$WORK"/*.log 2> /dev/null | head -1); [ -n "$log" ] && tail -n 30 "$log" >&2' ERR
export ZIG_GLOBAL_CACHE_DIR="$WORK/zig-cache" ZIG_LOCAL_CACHE_DIR="$WORK/zig-cache"
rm -rf "${OUT:?}"/*
# The image's headers come after Zig's C library headers: they supply X11, Wayland, the sound
# servers and the rest, never libc. Ubuntu keeps libraries in its multiarch folder, which CMake
# only searches when told the architecture (Zig's clang does not report one). -g0: Zig's clang
# writes debug information by default, which would make shaderc ten times larger.
FLAGS="-g0 -idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu"
CONFIGURE=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_C_COMPILER="$TOOLS/bin/zig-cc" -DCMAKE_CXX_COMPILER="$TOOLS/bin/zig-c++"
    -DCMAKE_AR="$TOOLS/bin/zig-ar" -DCMAKE_RANLIB="$TOOLS/bin/zig-ranlib"
    "-DCMAKE_C_FLAGS=$FLAGS" "-DCMAKE_CXX_FLAGS=$FLAGS" -DCMAKE_LIBRARY_ARCHITECTURE=x86_64-linux-gnu
    -DCMAKE_INSTALL_PREFIX="$OUT")

SDL_VERSION=$(sed -n 's/^set(XBOX_SDL3_VERSION "\(.*\)")/\1/p' tools/xboxrecomp/cmake/xbox_sdl3.cmake)
SDL_SHA256=$(sed -n 's/^set(XBOX_SDL3_SHA256 "\(.*\)")/\1/p' tools/xboxrecomp/cmake/xbox_sdl3.cmake)
echo "$SDL_SHA256  $SOURCES/SDL3-$SDL_VERSION.tar.gz" | sha256sum -c - > /dev/null
tar -C "$WORK" -xzf "$SOURCES/SDL3-$SDL_VERSION.tar.gz"
cmake -S "$WORK/SDL3-$SDL_VERSION" -B "$WORK/sdl3-build" "${CONFIGURE[@]}" -DSDL_SHARED=OFF -DSDL_STATIC=ON \
    -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF > "$WORK/sdl3-configure.log"
# Every backend a player's computer may need, each loaded at run time: fail rather than ship without one.
for option in SDL_X11_SHARED SDL_WAYLAND_SHARED SDL_WAYLAND_LIBDECOR_SHARED SDL_PIPEWIRE_SHARED \
              SDL_PULSEAUDIO_SHARED SDL_ALSA_SHARED SDL_DBUS SDL_LIBUDEV SDL_HIDAPI_LIBUSB_SHARED SDL_IBUS; do
    grep -Eq "^--   $option +\(Wanted: ON\): ON" "$WORK/sdl3-configure.log" ||
        { grep -E "^--   $option " "$WORK/sdl3-configure.log" >&2; echo "SDL3 was configured without $option" >&2; exit 1; }
done
cmake --build "$WORK/sdl3-build" > "$WORK/sdl3-build.log"
cmake --install "$WORK/sdl3-build" > /dev/null

cmake -S "$SOURCES/shaderc" -B "$WORK/shaderc-build" "${CONFIGURE[@]}" -DSHADERC_SKIP_TESTS=ON \
    -DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON -DSPIRV_SKIP_EXECUTABLES=ON -DSPIRV_SKIP_TESTS=ON \
    -DENABLE_GLSLANG_BINARIES=OFF > "$WORK/shaderc-configure.log"
cmake --build "$WORK/shaderc-build" --target shaderc_combined > "$WORK/shaderc-build.log"
cp "$(find "$WORK/shaderc-build" -name libshaderc_combined.a | head -1)" "$OUT/lib/"
cp -r "$SOURCES/shaderc/libshaderc/include/shaderc" "$OUT/include/"

cp -r "$SOURCES/vulkan-headers/include/vulkan" "$SOURCES/vulkan-headers/include/vk_video" "$OUT/include/"
# Every core and window-system entry point the loader exports, as empty functions: the linker
# needs a library that defines what the game calls; the game itself only names libvulkan.so.1.
mkdir -p "$OUT/link" "$WORK/vulkan"
grep -ohE 'VKAPI_CALL vk[A-Za-z0-9]+\(' "$OUT"/include/vulkan/vulkan_{core,wayland,xlib,xcb}.h |
    sed 's/VKAPI_CALL \(.*\)(/void \1(void) {}/' | sort -u > "$WORK/vulkan/stub.c"
"$TOOLS/bin/zig-cc" -g0 -shared -fPIC -Wl,-soname,libvulkan.so.1 -o "$OUT/link/libvulkan.so" "$WORK/vulkan/stub.c"

# Only what the game's build reads: the static libraries, headers and SDL's CMake package.
rm -rf "$OUT/share" "$OUT/bin" "$OUT/lib/pkgconfig" "$OUT/include/SDL3/SDL_test"*.h "$OUT/lib/libSDL3_test.a"
mkdir -p "$OUT/licenses"
cp "$WORK/SDL3-$SDL_VERSION/LICENSE.txt" "$OUT/licenses/SDL3-zlib.txt"
cp "$SOURCES/shaderc/LICENSE" "$OUT/licenses/shaderc-Apache-2.0.txt"
cp "$SOURCES/shaderc/third_party/glslang/LICENSE.txt" "$OUT/licenses/glslang.txt"
cp "$SOURCES/shaderc/third_party/spirv-tools/LICENSE" "$OUT/licenses/SPIRV-Tools-Apache-2.0.txt"
cp "$SOURCES/shaderc/third_party/spirv-headers/LICENSE" "$OUT/licenses/SPIRV-Headers-MIT.txt"
cp "$SOURCES/vulkan-headers/LICENSE.md" "$OUT/licenses/Vulkan-Headers.md"
rm -rf "$WORK"
echo "Built the game's libraries with $("$TOOLS/zig/zig" version): $(cd "$OUT" && find lib link -type f | sort | tr '\n' ' ')"
