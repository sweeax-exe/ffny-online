#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""A test data folder with a Run the Streets map installed as the launcher would install an official map.

    python tools/openworld/rts_content_root.py <out data folder> <map.ffnypack> [--id official-knox-and-jam]
        [--game-dir game]

It unpacks the pack into <out>/mods/<id>, records it as a synced pack in <out>/mod-manager/state.json, runs
`setup/mods.py compose` (the launcher's own composer: content-files/ and content.ini), and then marks the pack's
content.ini section as an official Run the Streets map (official=1, rts_map=1, venue_code), which is what the
launcher writes for a catalogue entry of kind "official-map" with "rts_map": true (feat/official-maps,
docs/content-packs.md "Official maps"). Pass the folder to dj.py as --content-root (one per copy online).
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('out', type=Path)
    ap.add_argument('pack', type=Path)
    ap.add_argument('--id', default='official-knox-and-jam')
    ap.add_argument('--game-dir', type=Path, default=REPO / 'game')
    ap.add_argument('--bigarena', type=Path, help="the map's bigarena.json to put into <out>/mods/<id>/ (as the "
                    "official-maps publish does), e.g. built from the Studio project's spawns")
    ap.add_argument('--fighter-light', help="light the fighters like this retail venue (the new-venue op's "
                    "fighter_light, e.g. 01B: Hunt's Point after hours, night); default: the pack's own")
    a = ap.parse_args()
    out, mods, mm = a.out.resolve(), a.out.resolve() / 'mods', a.out.resolve() / 'mod-manager'
    folder = mods / a.id
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    mm.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(a.pack) as z:
        meta = json.loads(z.read('pack.json'))
        for n in z.namelist():
            if n.startswith('files/') and not n.endswith('/'):
                dest = folder / n[len('files/'):]
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(z.read(n))
    ops = json.loads((folder / 'install.json').read_text(encoding='utf-8')).get('ops') or []
    venue = next((op for op in ops if op.get('op') == 'new-venue'), None)
    if not venue:
        sys.exit('the pack has no new-venue op')
    if a.fighter_light:
        venue['fighter_light'] = a.fighter_light
        info = json.loads((folder / 'install.json').read_text(encoding='utf-8'))
        info['ops'] = ops
        (folder / 'install.json').write_text(json.dumps(info, indent=1), encoding='utf-8')
    receipt = dict(folder=a.id, name=meta.get('title', a.id), authors=(meta.get('author') or {}).get('name', ''),
                   kind='content', type='venue', version=meta.get('version', '0'), synced=True, order=1,
                   content_sha256=meta.get('content_sha256', ''), description=meta.get('description', ''),
                   bytes=sum(p.stat().st_size for p in folder.rglob('*') if p.is_file()))
    (mm / 'state.json').write_text(json.dumps({'schema': 1, 'installed': {a.id: receipt}}, indent=1), encoding='utf-8')
    r = subprocess.run([sys.executable, str(REPO / 'setup' / 'mods.py'), 'compose', '--data-dir', str(out),
                        '--game-dir', str(a.game_dir)], capture_output=True, text=True)
    print(r.stdout.strip()[-600:])
    if r.returncode:
        print(r.stderr[-2000:], file=sys.stderr)
        sys.exit('compose failed')
    if a.bigarena:
        shutil.copyfile(a.bigarena, folder / 'bigarena.json')
    ini = mm / 'content.ini'
    text = ini.read_text(encoding='utf-8')
    section = '[pack.%s]' % a.id
    if section not in text:
        sys.exit('content.ini has no %s' % section)
    extra = 'official=1\nrts_map=1\nvenue_code=%s\n' % venue.get('code', '')
    text = re.sub(re.escape(section) + r'\n', section + '\n' + extra.replace('\\', '\\\\'), text, count=1)
    ini.write_text(text, encoding='utf-8')
    print('content root %s: %s (venue %s, feID %s), content.ini marked official RTS map' %
          (out, a.id, venue.get('code'), venue.get('fe_id')))
    print('sha256 of content.ini', hashlib.sha256(ini.read_bytes()).hexdigest()[:16])


if __name__ == '__main__':
    main()
