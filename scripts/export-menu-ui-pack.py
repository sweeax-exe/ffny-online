#!/usr/bin/env python3
"""Export the approved menu artwork as sparse source-shaped schema-1 texture packs; never installs.

Each component in config/menu-ui-source-shaped-exports.json becomes <out>/<component>/ with
manifest.ini, textures/<source-id>.png and receipt.json (input pins, crops, output hashes).
Inputs must match their SHA-256 pins in config/menu-ui-approved-artwork.json. Existing
component folders are never overwritten: packs are immutable, export a new version instead.
Needs Pillow (for example the DefJam-Modding-Tools venv).

python scripts/export-menu-ui-pack.py --art-root /private/path/pilot-v1 --out <data>/mods
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

REPO = Path(__file__).resolve().parents[1]


def sha256(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def plan(exports, catalogue):
    """Check the export table against the catalogue; return {component: [entry, ...]}."""
    if exports.get('schema') != 1 or catalogue.get('schema') != 1:
        raise ValueError('Unsupported schema')
    pins = {f['path']: f['sha256'] for r in catalogue['approval_records'] for f in r['files']}
    sizes = {s['source_id']: (s['aliases'][0]['width'], s['aliases'][0]['height']) for s in catalogue['reviewed_sources']}
    seen, out = set(), {}
    for name, component in exports['components'].items():
        if not re.fullmatch(r'[A-Za-z0-9_.-]+', name):
            raise ValueError('Invalid component name: ' + name)
        if component.get('png_mips', 'opacity') not in ('opacity', 'channels'):
            raise ValueError('Unknown png_mips: ' + name)
        for e in component['entries']:
            sid = e['source_id']
            if sid not in sizes:
                raise ValueError('Source ID not in the approved catalogue: ' + sid)
            if sid in seen:
                raise ValueError('Source ID exported twice: ' + sid)
            if e['art'] not in pins:
                raise ValueError('Artwork is not an approved pin: ' + e['art'])
            w, h = sizes[sid]
            sw, sh = e['size']
            if sw % w or sh % h or sw // w != sh // h or not 1 <= sw // w <= 8:
                raise ValueError(f'{sid}: {sw}x{sh} is not a uniform 1-8x scale of {w}x{h}')
            seen.add(sid)
            out.setdefault(name, []).append(dict(e, pin=pins[e['art']], scale=sw // w))
    return out


def export(exports, catalogue, art_root, out_root):
    from PIL import Image
    work = plan(exports, catalogue)
    art_root, out_root = Path(art_root).resolve(), Path(out_root)
    for name in work:
        if (out_root/name).exists():
            raise FileExistsError(f'{out_root/name} exists; packs are immutable, export a new version')
    for name, entries in work.items():
        component = exports['components'][name]
        folder = out_root/name
        (folder/'textures').mkdir(parents=True)
        receipt = {'schema': 1, 'component': name, 'status': component['status'], 'entries': []}
        lines = ['[pack]', 'schema=1', 'name=' + component['name'], 'png_mips=' + component.get('png_mips', 'opacity'), '[textures]']
        for e in entries:
            src = art_root/e['art']
            if sha256(src) != e['pin']:
                raise ValueError(f'{e["art"]}: SHA-256 does not match its approval pin')
            with Image.open(src) as image:
                image = image.convert('RGBA')
                if e['crop']:
                    image = image.crop(tuple(e['crop']))
                if list(image.size) != e['size']:
                    raise ValueError(f'{e["label"]}: exported {image.size}, expected {e["size"]}')
                rel = f'textures/{e["source_id"]}.png'
                image.save(folder/rel, optimize=False)
            lines.append(f'{e["source_id"]}={rel}')
            receipt['entries'].append({'source_id': e['source_id'], 'label': e['label'], 'art': e['art'], 'art_sha256': e['pin'],
                                       'crop': e['crop'], 'scale': e['scale'], 'output': rel, 'output_sha256': sha256(folder/rel)})
        (folder/'manifest.ini').write_text('\n'.join(lines) + '\n')
        (folder/'receipt.json').write_text(json.dumps(receipt, indent=1) + '\n')
        print(f'{name}: {len(entries)} textures -> {folder}')
    return work


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--art-root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True, help='mods folder to create the component folders in')
    parser.add_argument('--exports', type=Path, default=REPO/'config/menu-ui-source-shaped-exports.json')
    parser.add_argument('--catalogue', type=Path, default=REPO/'config/menu-ui-approved-artwork.json')
    parser.add_argument('--check', action='store_true', help='validate the export table only')
    args = parser.parse_args()
    exports, catalogue = json.loads(args.exports.read_text()), json.loads(args.catalogue.read_text())
    if args.check:
        work = plan(exports, catalogue)
        print(', '.join(f'{k}: {len(v)}' for k, v in work.items()), 'source IDs; table valid')
        return 0
    export(exports, catalogue, args.art_root, args.out)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
