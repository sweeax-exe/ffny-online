#!/bin/bash
# Build the game-free app (src/app, defjam_app) for the Linux AppImage.
#
#   scripts/build-app-linux.sh <unpacked setup payload> <output folder>
#
# Writes <output folder>/defjam_app. It is built in the container pinned in
# setup/dependencies-linux.json (app_build: Ubuntu 22.04, run with podman or docker), so it runs on
# glibc 2.35 and newer whatever builds it, and with that payload's Dear ImGui source. SDL3 is the
# toolkit's pinned release (tools/xboxrecomp/cmake/xbox_sdl3.cmake, checked by SHA-256) built
# statically with video, its renderer and file dialogs and no sound; it loads X11 or Wayland,
# OpenGL or Vulkan and D-Bus from the player's computer when it runs. The C++ runtime is linked in
# too, so the program needs only the C library. It holds no game code. scripts/build-setup-linux.sh
# calls this, then scripts/package-appimage.py.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
[ $# -eq 2 ] || { echo "usage: $0 <unpacked setup payload> <output folder>" >&2; exit 2; }
PAYLOAD=$(readlink -f "$1")
OUT=$(readlink -m "$2")
pin() { python3 -c 'import json,sys; d=json.load(open("setup/dependencies-linux.json"))["app_build"]; print(d[sys.argv[1]] if sys.argv[1]!="packages" else " ".join(d["packages"]))' "$1"; }

if [ -z "${DEFJAM_APP_BUILD_INSIDE:-}" ]; then
    ENGINE=$(command -v podman || command -v docker || true)
    [ -n "$ENGINE" ] || { echo "podman or docker is needed to build the app" >&2; exit 1; }
    mkdir -p "$OUT"
    # Rootless podman's root is this user, so the output is ours; docker (CI) writes as root.
    exec "$ENGINE" run --rm -v "$ROOT:$ROOT" -v "$PAYLOAD:$PAYLOAD:ro" -v "$OUT:$OUT" -w "$ROOT" \
        -e DEFJAM_APP_BUILD_INSIDE=1 -e DEFJAM_APP_PACKAGES="$(pin packages)" "$(pin image)" \
        bash scripts/build-app-linux.sh "$PAYLOAD" "$OUT"
fi

# ---- inside the container ----
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
# shellcheck disable=SC2086
apt-get install -y -qq --no-install-recommends $DEFJAM_APP_PACKAGES > /dev/null
WORK="$OUT/work"
rm -rf "$WORK"
mkdir -p "$WORK"
# The steps below write to logs in $WORK; a failing one shows the end of the newest.
trap 'log=$(ls -t "$WORK"/*.log 2> /dev/null | head -1); [ -n "$log" ] && tail -n 30 "$log" >&2' ERR

SDL_VERSION=$(sed -n 's/^set(XBOX_SDL3_VERSION "\(.*\)")/\1/p' tools/xboxrecomp/cmake/xbox_sdl3.cmake)
SDL_SHA256=$(sed -n 's/^set(XBOX_SDL3_SHA256 "\(.*\)")/\1/p' tools/xboxrecomp/cmake/xbox_sdl3.cmake)
[ -n "$SDL_VERSION" ] && [ -n "$SDL_SHA256" ] || { echo "Could not read the toolkit's SDL3 pin" >&2; exit 1; }
curl -fsSL -o "$WORK/sdl3.tar.gz" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
echo "$SDL_SHA256  $WORK/sdl3.tar.gz" | sha256sum -c - > /dev/null
tar -C "$WORK" -xzf "$WORK/sdl3.tar.gz"
cmake -S "$WORK/SDL3-$SDL_VERSION" -B "$WORK/sdl3-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$WORK/sdl3" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_KMSDRM=OFF -DSDL_VIDEO=ON -DSDL_RENDER=ON -DSDL_DIALOG=ON \
    -DSDL_HAPTIC=ON -DSDL_DBUS=ON -DSDL_X11_SHARED=ON -DSDL_WAYLAND_SHARED=ON -DSDL_WAYLAND_LIBDECOR_SHARED=ON \
    > "$WORK/sdl3-configure.log"
cmake --build "$WORK/sdl3-build" > "$WORK/sdl3-build.log"
cmake --install "$WORK/sdl3-build" > /dev/null

IMGUI=$(cd "$PAYLOAD/source" && ls -d setup-deps/imgui/imgui-*)
# The toolkit's build configures its Vulkan Direct3D device whenever SDL3 is on, which needs
# shaderc to be found. The app never builds or links that device, and Ubuntu 22.04 has no shaderc
# package, so an empty placeholder stands in for it at configure time only.
mkdir -p "$WORK/shaderc-placeholder/include/shaderc"
: > "$WORK/shaderc-placeholder/include/shaderc/shaderc.h"
: > "$WORK/shaderc-placeholder/libshaderc_shared.so"
cmake -S "$ROOT" -B "$WORK/app-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDEFJAM_BUILD_GAME=OFF \
    -DDEFJAM_APP_STATIC_RUNTIME=ON -DCMAKE_PREFIX_PATH="$WORK/sdl3" \
    -DSHADERC_LIBRARY="$WORK/shaderc-placeholder/libshaderc_shared.so" \
    -DSHADERC_INCLUDE_DIR="$WORK/shaderc-placeholder/include" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_IMGUI="$PAYLOAD/source/$IMGUI" \
    > "$WORK/app-configure.log"
cmake --build "$WORK/app-build" --target defjam_app > "$WORK/app-build.log"
strip -o "$OUT/defjam_app" "$WORK/app-build/defjam_app"
chmod 755 "$OUT/defjam_app"
echo "Libraries the app loads:"
readelf -d "$OUT/defjam_app" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/  \1/p'
echo "Newest glibc symbol: $(objdump -T "$OUT/defjam_app" | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1)"
rm -rf "$WORK"
