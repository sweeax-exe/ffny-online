#!/usr/bin/env python3
"""Build a separate menu-only texture pack from private approved art/components; never install or upload.

Faithful HD is an optional earlier-loaded pack, not bundled. Only selected unchanged scenic
canvases needed by the approved wide layers are retained from the supplied local baseline.
Outputs are immutable, assembled in a temporary directory and atomically renamed after checks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import zipfile

REPO = Path(__file__).resolve().parents[1]

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def mappings(path, section):
    current = ''
    for line in path.read_text().splitlines():
        line = line.strip()
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1]
        elif current == section and '=' in line:
            key, value = line.split('=', 1)
            if re.fullmatch(r'[0-9a-f]{64}', key.strip()):
                yield key.strip(), value.strip()

def safe_file(root, rel):
    path = (root / rel).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError('Pack path escapes root: ' + rel)
    if not path.is_file():
        raise FileNotFoundError(path)
    return path

def build(args):
    from PIL import Image
    recipe = json.loads(args.recipe.read_text())
    catalogue = json.loads(args.catalogue.read_text())
    pins = {f['path']: f['sha256'] for rec in catalogue['approval_records'] for f in rec['files']}
    registrations_path = REPO/recipe['runtime_registrations']
    registrations = json.loads(registrations_path.read_text())
    sources = {s['source_id']: s for s in catalogue['reviewed_sources'] + registrations['sources']}
    out = args.out.resolve()
    if out.exists() or out.with_suffix('.zip').exists():
        raise FileExistsError('Immutable output exists: ' + str(out))
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.menu-build-', dir=out.parent) as temp:
        mods = Path(temp)
        pack = mods / out.name
        (pack/'textures').mkdir(parents=True)
        textures, provenance = {}, []
        def add(sid, src, origin):
            if sid not in sources:
                raise ValueError('Unregistered source identity: ' + sid)
            if sid in textures:
                raise ValueError('Duplicate native texture: ' + sid)
            native = sources[sid]['aliases'][0]
            with Image.open(src) as image:
                if image.size != (native['width']*4, native['height']*4):
                    raise ValueError('Wrong source-shaped4x canvas: ' + sid)
            rel = 'textures/' + sid + '.png'
            shutil.copy2(src, pack/rel)
            textures[sid] = rel
            provenance.append(dict(source_id=sid,origin=origin,sha256=digest(src)))
        for base, components in [(args.base_mods, recipe['base_components']),
                                 (args.approved_components, recipe['approved_components'])]:
            for component in components:
                folder = base/component
                receipt = folder/'receipt.json'
                if digest(receipt) != recipe['component_receipt_sha256'][component]:
                    raise ValueError('Component receipt pin mismatch: ' + component)
                outputs = {entry['source_id']: entry['output_sha256']
                           for entry in json.loads(receipt.read_text())['entries']}
                for sid, rel in mappings(folder/'manifest.ini', 'textures'):
                    src = safe_file(folder, rel)
                    if outputs.get(sid) != digest(src):
                        raise ValueError('Component output pin mismatch: ' + sid)
                    add(sid, src, component)
        trans = json.loads((args.art_root/recipe['transition_sources']).read_text())['parts']
        for part in trans:
            rel = f'transition-v1-20261020/image-{part["image"]}-restrained.png'
            src = safe_file(args.art_root, rel)
            if pins.get(rel) != digest(src):
                raise ValueError('Transition approval pin mismatch: ' + rel)
            add(part['source_id'], src, rel)
        baseline = {}
        # Explicit earlier-to-later precedence; directory iteration must not choose a stale pack.
        for name in recipe['retained_core_inputs']:
            component = args.base_mods/name
            for sid, rel in mappings(component/'manifest.ini', 'textures'):
                baseline[sid] = safe_file(component, rel)
        for sid in sources:
            if sid not in textures:
                if digest(baseline[sid]) != recipe['retained_core_sha256'].get(sid):
                    raise ValueError('Retained original HD canvas pin mismatch: ' + sid)
                add(sid, baseline[sid], 'selected unchanged scenic canvas; baseline library excluded')
        if len(textures) != recipe['expected_source_ids']:
            raise ValueError('Unexpected source count: ' + str(len(textures)))
        manifest = '[pack]\nschema=1\nname='+recipe['name']+' '+recipe['version']+'\npng_mips=opacity\n[textures]\n'
        manifest += '\n'.join(sid+'='+rel for sid,rel in sorted(textures.items()))+'\n'
        (pack/'manifest.ini').write_text(manifest)
        wide_cfg = json.loads((REPO/recipe['wide_table']).read_text())
        wide_cfg['anchor_packs'] = [out.name]
        config = mods/'wide.json'
        config.write_text(json.dumps(wide_cfg))
        # Exporter is also usable as a CLI; keep one implementation of registration/pin checks.
        import subprocess, sys
        side = mods/'side-component'
        subprocess.run([sys.executable,str(REPO/'scripts/export-menu-ui-wide.py'),
            '--art-root',str(args.art_root),'--mods',str(mods),'--out',str(side),
            '--layers',str(config),'--catalogue',str(args.catalogue),
            '--registrations',str(registrations_path)],check=True)
        shutil.move(str(side/'slabs'), pack/'wide')
        entries = dict(mappings(side/'manifest.ini', 'wide'))
        with (pack/'manifest.ini').open('a') as stream:
            stream.write('[wide]\n')
            for sid,value in sorted(entries.items()):
                if value != 'stretch':
                    value = ';'.join(dt+':wide/'+Path(rel).name for dt,rel in
                                     (item.split(':',1) for item in value.split(';')))
                stream.write(sid+'='+value+'\n')
        (pack/'receipt-core.json').write_text(json.dumps(dict(schema=1,recipe=recipe,
            bundles_faithful_hd=False,entries=provenance),indent=2)+'\n')
        shutil.copy2(side/'receipt.json', pack/'receipt-wide.json')
        (pack/'README.txt').write_text(recipe['name']+' '+recipe['version']+'\nSeparate menu override; Faithful HD NOT included.\nEnable after Faithful HD if used; disable superseded menu-ui test packs.\nArtwork native acceptance: '+recipe['status']+'\n')
        files=[f for f in sorted(pack.rglob('*')) if f.is_file()]
        (pack/'files.json').write_text(json.dumps(dict(schema=1,files=[dict(path=str(f.relative_to(pack)),
            bytes=f.stat().st_size,sha256=digest(f)) for f in files]),indent=2)+'\n')
        pack.rename(out)
    archive = out.with_suffix('.zip')
    files=[f for f in sorted(out.rglob('*')) if f.is_file()]
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for f in files:z.write(f,str(Path(out.name)/f.relative_to(out)))
    receipt=dict(schema=1,pack=str(out),archive=str(archive),bundles_faithful_hd=False,
        files=len(files),native_textures=len(textures),wide_entries=len(entries),
        wide_slabs=len(list((out/'wide').glob('*.png'))),installed_bytes=sum(f.stat().st_size for f in files),
        archive_bytes=archive.stat().st_size,archive_sha256=digest(archive))
    print(json.dumps(receipt,indent=2))
    return receipt

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ['art-root','base-mods','approved-components','out']:
        ap.add_argument('--'+key,type=Path,required=True)
    ap.add_argument('--recipe',type=Path,default=REPO/'config/menu-ui-release.json')
    ap.add_argument('--catalogue',type=Path,default=REPO/'config/menu-ui-approved-artwork.json')
    ap.add_argument('--receipt',type=Path)
    args=ap.parse_args()
    result=build(args)
    if args.receipt:args.receipt.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
