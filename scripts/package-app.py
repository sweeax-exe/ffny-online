#!/usr/bin/env python3
"""Assemble the app's macOS bundle from the built program and the setup payload.

    python scripts/package-app.py --program build/app/defjam_app --payload <unpacked setup payload> \\
        --out DIR [--zip FILE]

Writes DIR/Def Jam Recompiled.app: Contents/MacOS/defjam_app, Contents/Info.plist, the icon and
the setup payload in Contents/Resources/payload, which the app runs setup from. The program uses
the SDL3 inside that payload (Contents/Resources/payload/deps/lib, through its rpath), so the
bundle needs nothing installed; any other library that is not the system's is refused. The bundle
is signed ad hoc, as the setup's own apps are. --zip also writes the archive that
scripts/check-setup-assets.py --app turns into the release asset, with its checksum and provenance
(scripts/build-setup.sh does both).
"""
import argparse
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUNDLE = 'Def Jam Recompiled.app'
PROGRAM = 'defjam_app'
SDL = 'libSDL3.0.dylib'
RPATH = '@executable_path/../Resources/payload/deps/lib'
SYSTEM = ('/usr/lib/', '/System/Library/')


def tool(*argv):
    return subprocess.run([str(a) for a in argv], check=True, capture_output=True, text=True).stdout


def libraries(program):
    """The libraries `program` loads (otool -L), without its own name line."""
    return [line.split(' (')[0].strip() for line in tool('/usr/bin/otool', '-L', program).splitlines()[1:]]


def rpaths(program):
    return re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+?) \(offset', tool('/usr/bin/otool', '-l', program))


def use_bundled_sdl(program):
    """Point the program at the payload's SDL3 and nothing outside the bundle but the system."""
    for library in libraries(program):
        if library.startswith(SYSTEM):
            continue
        if Path(library).name != SDL:
            sys.exit(f'{program} links {library}, which the bundle does not carry')
        if library != '@rpath/' + SDL:
            tool('/usr/bin/install_name_tool', '-change', library, '@rpath/' + SDL, program)
    for path in rpaths(program):
        tool('/usr/bin/install_name_tool', '-delete_rpath', path, program)
    tool('/usr/bin/install_name_tool', '-add_rpath', RPATH, program)


def bundle(program, payload, out):
    app = out / BUNDLE
    if app.exists():
        shutil.rmtree(app)
    (app / 'Contents/MacOS').mkdir(parents=True)
    shutil.copy2(program, app / 'Contents/MacOS' / PROGRAM)
    shutil.copytree(payload, app / 'Contents/Resources/payload', symlinks=True,
                    ignore=shutil.ignore_patterns('__pycache__', '.DS_Store'))
    if not (app / 'Contents/Resources/payload/deps/lib' / SDL).is_file():
        sys.exit('the setup payload has no ' + SDL + ' for the app to use')
    manifest = json.loads((payload / 'manifest.json').read_text())
    info = {'CFBundleName': 'Def Jam Recompiled', 'CFBundleDisplayName': 'Def Jam Recompiled',
            'CFBundleIdentifier': 'io.github.vyanhursky.defjamrecompiled', 'CFBundleExecutable': PROGRAM,
            'CFBundlePackageType': 'APPL', 'CFBundleShortVersionString': manifest['version'],
            'CFBundleVersion': manifest['version'], 'LSMinimumSystemVersion': '11.0',
            'NSHighResolutionCapable': True, 'CFBundleIconFile': 'defjam'}
    shutil.copy2(ROOT / 'branding/icon/macos/defjam.icns', app / 'Contents/Resources/defjam.icns')
    with (app / 'Contents/Info.plist').open('wb') as f:
        plistlib.dump(info, f)
    use_bundled_sdl(app / 'Contents/MacOS' / PROGRAM)
    tool('/usr/bin/codesign', '--force', '--sign', '-', app)
    return app


def archive(app, path):
    """A ZIP of the bundle with each file's mode, and no links (the updater refuses them)."""
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for item in sorted(app.rglob('*')):
            if item.is_symlink():
                sys.exit('symbolic links are not allowed in the app archive: ' + str(item))
            if item.is_dir():
                continue
            info = zipfile.ZipInfo.from_file(item, item.relative_to(app.parent).as_posix())
            info.compress_type = zipfile.ZIP_DEFLATED
            with item.open('rb') as source:
                z.writestr(info, source.read())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--program', type=Path, required=True)
    ap.add_argument('--payload', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--zip', type=Path)
    args = ap.parse_args()
    if sys.platform != 'darwin':
        sys.exit('The app bundle is assembled on macOS')
    if not os.access(args.program, os.X_OK) or not (args.payload / 'manifest.json').is_file():
        sys.exit('--program must be the built defjam_app and --payload an unpacked setup payload')
    args.out.mkdir(parents=True, exist_ok=True)
    app = bundle(args.program, args.payload, args.out)
    print(app)
    if args.zip:
        archive(app, args.zip)
        print(args.zip)


if __name__ == '__main__':
    main()
