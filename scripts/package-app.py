#!/usr/bin/env python3
"""Assemble the app's macOS bundle, and optionally its release asset (development tool).

    python scripts/package-app.py --program build/app/defjam_app --payload <setup payload> --out DIR [--asset]

Writes DIR/Def Jam Recompiled.app: Contents/MacOS/defjam_app, Contents/Info.plist and the setup
payload in Contents/Resources/payload, which the app runs setup from. --asset also writes the
release asset the app updates itself from (setup/releases.py select_app):
DefJamRecompiled-<version>-macos-arm64.zip with its .sha256 and .provenance.json, the identity
taken from the payload's manifest. Nothing here is published: the release workflow does not build
the app yet, and the program still links the SDL3 it was built against.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import sys
import zipfile

BUNDLE = 'Def Jam Recompiled.app'


def bundle(program, payload, out):
    app = out / BUNDLE
    if app.exists():
        shutil.rmtree(app)
    (app / 'Contents/MacOS').mkdir(parents=True)
    shutil.copy2(program, app / 'Contents/MacOS/defjam_app')
    shutil.copytree(payload, app / 'Contents/Resources/payload', symlinks=True,
                    ignore=shutil.ignore_patterns('__pycache__', '.DS_Store'))
    manifest = json.loads((payload / 'manifest.json').read_text())
    icon = payload / 'launcher' / BUNDLE / 'Contents/Resources/defjam.icns'
    info = {'CFBundleName': 'Def Jam Recompiled', 'CFBundleDisplayName': 'Def Jam Recompiled',
            'CFBundleIdentifier': 'io.github.vyanhursky.defjamrecompiled', 'CFBundleExecutable': 'defjam_app',
            'CFBundlePackageType': 'APPL', 'CFBundleShortVersionString': manifest['version'],
            'CFBundleVersion': manifest['version'], 'LSMinimumSystemVersion': '13.0',
            'NSHighResolutionCapable': True}
    if icon.is_file():
        shutil.copy2(icon, app / 'Contents/Resources/defjam.icns')
        info['CFBundleIconFile'] = 'defjam'
    with (app / 'Contents/Info.plist').open('wb') as f:
        plistlib.dump(info, f)
    return app, manifest


def asset(app, manifest, out):
    stem = 'DefJamRecompiled-' + manifest['version'] + '-' + manifest['platform']
    archive = out / (stem + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(app.rglob('*')):
            if path.is_symlink():
                sys.exit('symbolic links are not allowed in the app asset: ' + str(path))
            name = path.relative_to(out).as_posix()
            if path.is_dir():
                continue
            item = zipfile.ZipInfo.from_file(path, name)
            item.compress_type = zipfile.ZIP_DEFLATED
            with path.open('rb') as source:
                z.writestr(item, source.read())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (out / (stem + '.sha256')).write_text(digest + '  ' + archive.name + '\n', encoding='ascii')
    provenance = {key: manifest[key] for key in ('version', 'platform', 'source_commit', 'toolkit_commit', 'files', 'updater')}
    provenance.update(kind='app', app_sha256=digest, development=bool(manifest.get('development')))
    (out / (stem + '.provenance.json')).write_text(json.dumps(provenance, indent=1), encoding='utf-8')
    return archive


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--program', type=Path, required=True)
    ap.add_argument('--payload', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asset', action='store_true')
    args = ap.parse_args()
    if not os.access(args.program, os.X_OK) or not (args.payload / 'manifest.json').is_file():
        sys.exit('--program must be the built defjam_app and --payload an unpacked setup payload')
    args.out.mkdir(parents=True, exist_ok=True)
    app, manifest = bundle(args.program, args.payload, args.out)
    print(app)
    if args.asset:
        print(asset(app, manifest, args.out))


if __name__ == '__main__':
    main()
