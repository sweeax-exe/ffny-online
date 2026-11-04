"""Builds the launcher's "Enhanced graphics (community)" download from the two cleaned preset zips.

    python scripts/make-graphics-pack.py ffny-graphics-quality.zip ffny-graphics-performance.zip \
        --out out/graphics --version 2026-11-04.1

The two zips hold the same files except the preset's configuration (PRESET_FILES). Out come:
  graphics-base-<sha8>.zip         every shared file
  graphics-quality-<sha8>.zip      the preset files of the Quality zip
  graphics-performance-<sha8>.zip  the preset files of the Performance zip
  graphics.json                    the manifest the launcher reads (below)
<sha8> is the first 8 hex digits of the archive's SHA-256, so a new pack never reuses a name. The
archives are written STORED (no compression; the launcher's reader takes only that) with sorted names
and a fixed time, so the same input gives the same bytes.

graphics.json is then signed exactly like latest.json, with the same tool (updatesign) and key, giving
graphics.json.sig (ECDSA P-256 over the file's bytes, r||s as 128 hex digits, as latest.json.sig), and the folder is published as updates/graphics/ next to latest.json: the launcher asks the update port
for /graphics/graphics.json (and /updates/graphics/... over HTTPS, as it does for latest.json).
--sign-test-key PEM writes the .sig with a TEST key; it is for tests only and the launcher accepts a
test key only from a server on this PC.

graphics.json, format 1:
{
  "format": 1,
  "version": "<--version>",
  "notices": "THIRD-PARTY-NOTICES.txt",
  "credits": "Presets by Marleymarletto · ReShade by crosire · ...",
  "archives": {
    "base":        {"path": "graphics-base-<sha8>.zip", "size": <bytes>, "sha256": "<hex>"},
    "quality":     {...},
    "performance": {...}
  },
  "installs": {
    "base": ["dxgi.dll", "reshade-shaders/Shaders/x.fx", ...],
    "quality": ["ReShade.ini", ...], "performance": [...]
  }
}
Paths are relative to the game's bin folder, '/' separated; "path" is relative to graphics.json.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import zipfile

PRESET_FILES = ('deep-fried-chicken-presets/defjam_recomp.cfg', 'deep-fried-chicken.cfg', 'ReShade.ini',
                'ReShadePreset.ini')
CREDITS = ('Presets by Marleymarletto · ReShade by crosire · DLSS by NVIDIA · '
           'DLSS 5 Manager / shader authors (see notices)')
NOTICES = 'THIRD-PARTY-NOTICES.txt'
FIXED_TIME = (2026, 1, 1, 0, 0, 0)


def read(zip_path):
    out = {}
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            name = info.filename.replace('\\', '/')
            p = PurePosixPath(name)
            if p.is_absolute() or '..' in p.parts or ':' in name or not name.strip('/'):
                raise SystemExit(f'{zip_path}: unsafe name {name!r}')
            if name in out:
                raise SystemExit(f'{zip_path}: {name} twice')
            out[name] = z.read(info)
    return out


def write_zip(path, files):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_STORED) as z:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_STORED
            info.external_attr = 0o644 << 16
            z.writestr(info, files[name])


def archive(out, kind, files):
    tmp = out / f'graphics-{kind}.zip.tmp'
    write_zip(tmp, files)
    data = tmp.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    final = out / f'graphics-{kind}-{sha[:8]}.zip'
    tmp.replace(final)
    return {'path': final.name, 'size': len(data), 'sha256': sha}


def build(quality_zip, performance_zip, out, version):
    quality, performance = read(quality_zip), read(performance_zip)
    if set(quality) != set(performance):
        raise SystemExit('the two zips do not hold the same file names: '
                         + ', '.join(sorted(set(quality) ^ set(performance))[:10]))
    differ = sorted(n for n in quality if quality[n] != performance[n])
    unexpected = [n for n in differ if n not in PRESET_FILES]
    if unexpected:
        raise SystemExit('files differ between the presets beyond the preset files: ' + ', '.join(unexpected))
    missing = [n for n in PRESET_FILES if n not in quality]
    if missing:
        raise SystemExit('preset files missing: ' + ', '.join(missing))
    if NOTICES not in quality:
        raise SystemExit(f'{NOTICES} missing')
    out.mkdir(parents=True, exist_ok=True)
    base = {n: d for n, d in quality.items() if n not in PRESET_FILES}
    manifest = {
        'format': 1,
        'version': version,
        'notices': NOTICES,
        'credits': CREDITS,
        'archives': {
            'base': archive(out, 'base', base),
            'quality': archive(out, 'quality', {n: quality[n] for n in PRESET_FILES}),
            'performance': archive(out, 'performance', {n: performance[n] for n in PRESET_FILES}),
        },
        'installs': {'base': sorted(base), 'quality': sorted(PRESET_FILES), 'performance': sorted(PRESET_FILES)},
    }
    text = json.dumps(manifest, indent=1, ensure_ascii=False) + '\n'
    (out / 'graphics.json').write_bytes(text.encode('utf-8'))
    return manifest


def sign_test(path, pem):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    key = serialization.load_pem_private_key(Path(pem).read_bytes(), None)
    r, s = decode_dss_signature(key.sign(Path(path).read_bytes(), ec.ECDSA(hashes.SHA256())))
    Path(str(path) + '.sig').write_text('%064x%064x' % (r, s))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('quality_zip', type=Path)
    ap.add_argument('performance_zip', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--version', required=True, help='the pack version, e.g. 2026-11-04.1')
    ap.add_argument('--sign-test-key', help='tests only: sign graphics.json with this TEST key (PEM)')
    a = ap.parse_args(argv)
    m = build(a.quality_zip, a.performance_zip, a.out, a.version)
    if a.sign_test_key:
        sign_test(a.out / 'graphics.json', a.sign_test_key)
    for kind, info in m['archives'].items():
        print(f"{kind:12} {info['path']}  {info['size']} bytes  {info['sha256']}")
    print(f"{len(m['installs']['base'])} shared files, {len(m['installs']['quality'])} preset files; "
          f"{a.out / 'graphics.json'} written; sign it with updatesign, then publish the folder as updates/graphics/")
    return 0


if __name__ == '__main__':
    sys.exit(main())
