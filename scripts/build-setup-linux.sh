#!/bin/bash
# Build the game-free Linux setup (x86-64, Steam Deck included):
# DefJamSetup-<version>-linux-x64.tar.gz, a folder holding DefJamSetup.sh, the
# unpacked payload, a short "READ ME FIRST" and the third-party licenses; and the
# app, DefJamRecompiled-<version>-linux-x86_64.AppImage, which carries the same payload.
# Since 1.0.0 only the AppImage is a release asset (build/setup-linux/setup-assets).
# Never analyzes or builds a dump. Needs Python 3.11+ with pip, podman or docker (the
# app is built in a pinned Ubuntu 22.04 container), mksquashfs and readelf; pass
# --development for a local prototype from a dirty checkout (release workflows must not).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEVELOPMENT=()
[ "${1:-}" = "--development" ] && DEVELOPMENT=(--development)
VERSION=$(sed -n 's/^project(defjam_recomp VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
OUT="$ROOT/build/setup-linux"
NAME="DefJamSetup-$VERSION-linux-x64"
rm -rf "$OUT"
mkdir -p "$OUT/image/$NAME"
PYTHON="${PYTHON:-python3}"
"$PYTHON" -c 'import sys; sys.exit(sys.version_info < (3, 11))' || { echo "Python 3.11 or newer is required" >&2; exit 1; }

# 1. The payload: source, Python, the wizard, the engine and the launcher template.
"$PYTHON" scripts/package-setup.py --platform linux-x64 --output "$OUT/setup-payload.zip" \
    "${DEVELOPMENT[@]+"${DEVELOPMENT[@]}"}"

# 2. The folder a player unpacks: the payload unpacked (execute bits kept), the
#    entry script, the notes and the licenses.
"$PYTHON" - "$OUT/setup-payload.zip" "$OUT/image/$NAME/payload" <<'EOF'
import importlib.util, sys
spec = importlib.util.spec_from_file_location('macos_payload', 'scripts/macos_payload.py')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
helpers.unpack(sys.argv[1], sys.argv[2])
EOF
install -m 755 setup/linux/DefJamSetup.sh "$OUT/image/$NAME/DefJamSetup.sh"
cp setup/linux/README-FIRST.txt "$OUT/image/$NAME/READ ME FIRST.txt"
mkdir "$OUT/image/$NAME/Licenses"
cp "$OUT/image/$NAME"/payload/licenses/* "$OUT/image/$NAME/Licenses/"
cp LICENSE "$OUT/image/$NAME/Licenses/Def-Jam-Recompiled-MIT.txt"
tar -C "$OUT/image" --owner=0 --group=0 --numeric-owner --sort=name -czf "$OUT/$NAME.tar.gz" "$NAME"

# 3. The app (src/app): built in the pinned container against that payload's Dear ImGui source,
#    then packed with the payload, file for file as the archive carries it, into the AppImage.
./scripts/build-app-linux.sh "$OUT/image/$NAME/payload" "$OUT/app"
"$PYTHON" scripts/package-appimage.py --program "$OUT/app/defjam_app" --payload "$OUT/image/$NAME/payload" \
    --out "$OUT/DefJamRecompiled.AppImage"

# 4. Inventory, checksum and provenance of exactly what a release would attach: the app only. The
#    setup archive is checked against the payload but is not a release asset since 1.0.0; it stays
#    in $OUT for the wizard smoke test and command-line use.
"$PYTHON" scripts/check-setup-assets.py --payload "$OUT/setup-payload.zip" --installer "$OUT/$NAME.tar.gz" \
    --app "$OUT/DefJamRecompiled.AppImage" --output-dir "$OUT/setup-assets" "${DEVELOPMENT[@]+"${DEVELOPMENT[@]}"}"
echo "Built $OUT/setup-assets"
