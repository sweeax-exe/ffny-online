#!/usr/bin/env python3
"""Assemble the app's Linux AppImage from the built program and the setup payload.

    python scripts/package-appimage.py --program build/app-linux/defjam_app \\
        --payload <unpacked setup payload> --out DefJamRecompiled.AppImage

The image (a gzip squashfs, made with mksquashfs) holds:

    defjam_app                   the program (scripts/build-app-linux.sh)
    AppRun -> defjam_app         what the AppImage runtime starts
    payload/                     the setup payload, file for file as the setup archive carries it
    DefJamRecompiled.desktop     the name and icon desktops show for the file
    DefJamRecompiled.png, .DirIcon -> DefJamRecompiled.png

It is appended to the AppImage type 2 runtime pinned in setup/dependencies-linux.json
(appimage_runtime, checked by SHA-256). The program finds the payload beside itself. gzip, not
zstd, so setup/releases.py (read_appimage) can read the image in Python, with nothing run.
scripts/check-setup-assets.py --app then checks the result file by file against the inspected
payload before writing the release asset DefJamRecompiled-<version>-linux-x86_64.AppImage.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


releases = load('package_appimage_releases', ROOT / 'setup/releases.py')
shared = load('package_appimage_shared', ROOT / 'scripts/macos_payload.py')

# The only libraries the program may load itself: the C library's parts. SDL3 loads the display,
# graphics and D-Bus libraries of the player's computer with dlopen.
SYSTEM_LIBRARIES = {'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0', 'librt.so.1',
                    'ld-linux-x86-64.so.2'}

DESKTOP = """[Desktop Entry]
Type=Application
Name=Def Jam Recompiled
Comment=Def Jam: Fight for NY, built on this computer from your own copy of the game
Exec=defjam_app
Icon=DefJamRecompiled
Categories=Game;
Terminal=false
X-AppImage-Version={version}
"""


def needed(program):
    """The libraries `program` names in its dynamic section (readelf -d)."""
    out = subprocess.run(['readelf', '-d', str(program)], check=True, capture_output=True, text=True).stdout
    return re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', out)


def check_program(program):
    data = Path(program).read_bytes()
    if data[:4] != b'\x7fELF':
        raise ValueError('The app program is not a Linux program')
    if re.search(rb'sub_[0-9A-Fa-f]{8}', data) or b'recomp_dispatch' in data:
        raise ValueError('The app program contains lifted game code')
    for name in needed(program):
        if name not in SYSTEM_LIBRARIES:
            raise ValueError('The app program loads a library it does not carry: ' + name)


def runtime(cache):
    pin = json.loads((ROOT / 'setup/dependencies-linux.json').read_text(encoding='utf-8'))['appimage_runtime']
    return shared.download(pin['url'], Path(cache) / ('appimage-runtime-' + pin['sha256'][:12]), pin['sha256'])


def package(program, payload, out, cache):
    payload = Path(payload)
    manifest = json.loads((payload / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('platform') != 'linux-x64':
        raise ValueError('Not a Linux setup payload')
    check_program(program)
    head = runtime(cache).read_bytes()
    with tempfile.TemporaryDirectory() as temporary:
        appdir = Path(temporary) / 'AppDir'
        appdir.mkdir()
        shutil.copyfile(program, appdir / 'defjam_app')
        (appdir / 'defjam_app').chmod(0o755)
        os.symlink('defjam_app', appdir / 'AppRun')
        shutil.copytree(payload, appdir / 'payload', symlinks=True)
        for path in (appdir / 'payload').rglob('*'):
            if path.is_symlink():
                raise ValueError('The setup payload holds a link: ' + str(path))
        (appdir / 'DefJamRecompiled.desktop').write_text(DESKTOP.format(version=manifest['version']), encoding='utf-8')
        shutil.copyfile(ROOT / 'branding/icon/png/defjam-256.png', appdir / 'DefJamRecompiled.png')
        os.symlink('DefJamRecompiled.png', appdir / '.DirIcon')
        image = Path(temporary) / 'image.squashfs'
        # Root-owned, no extended attributes and fixed times: the same inputs give the same image.
        subprocess.run(['mksquashfs', str(appdir), str(image), '-comp', 'gzip', '-root-owned', '-noappend',
                        '-no-xattrs', '-mkfs-time', '0', '-all-time', '0', '-quiet', '-no-progress'],
                       check=True, stdout=subprocess.DEVNULL)
        out = Path(out)
        out.parent.mkdir(parents=True, exist_ok=True)
        pending = out.with_name(out.name + '.pending')
        with pending.open('wb') as target:
            target.write(head)
            with image.open('rb') as source:
                shutil.copyfileobj(source, target)
        pending.chmod(0o755)
        pending.replace(out)
    releases.read_appimage(out.read_bytes())             # the shape the updater will check
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--program', type=Path, required=True)
    parser.add_argument('--payload', type=Path, required=True, help='the unpacked setup payload')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/setup-downloads/linux')
    args = parser.parse_args(argv)
    if shutil.which('mksquashfs') is None or shutil.which('readelf') is None:
        print('mksquashfs (squashfs-tools) and readelf (binutils) are needed', file=sys.stderr)
        return 1
    out = package(args.program, args.payload, args.out, args.cache)
    print('Built', out, '(%d MB)' % (out.stat().st_size // 1024**2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
