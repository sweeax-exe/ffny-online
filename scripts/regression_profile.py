"""Explicit HD release-test settings shared by every independently loaded harness.

The profile path is carried separately from RECOMP_* so scenario isolation cannot
silently remove it. Source-image goldens run in a separate, recorded stock lane.
"""
import configparser
import hashlib
import json
import os
import re
from pathlib import Path

PROFILE_ENV = 'DEFJAM_REGRESSION_PROFILE'
PACKS = {'menu-ui-hd-wide': '1.4.0', 'hd-fighters': '1.0.0'}


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare(root, output):
    root = Path(root).resolve()
    state = json.loads((root.parent / 'mod-manager/state.json').read_text(encoding='utf-8'))
    receipts = {}
    expected_entries = 0
    for name, version in PACKS.items():
        receipt = state['installed'].get(name, {})
        if receipt.get('version') != version or receipt.get('folder') != name:
            raise ValueError('Install %s %s with setup/mods.py first' % (name, version))
        files = receipt.get('files', {})
        if not files or any(digest(root / name / path) != sha for path, sha in files.items()):
            raise ValueError('Installed pack differs from verified receipt: ' + name)
        receipts[name] = {'version': version, 'archive_sha256': receipt['archive_sha256'],
                          'manifest_sha256': digest(root / name / 'manifest.ini'), 'files': len(files)}
        manifest = configparser.ConfigParser(interpolation=None)
        manifest.read(root / name / 'manifest.ini', encoding='utf-8-sig')
        expected_entries += len(manifest['textures']) if manifest.has_section('textures') else 0
        if manifest.has_section('wide'):
            expected_entries += sum(len(value.split(';')) for value in manifest['wide'].values() if value != 'stretch')
    profile = {'schema': 1, 'packs': receipts, 'expected_entries': expected_entries, 'environment': {
        'RECOMP_ASPECT': '16:9', 'RECOMP_TEXTURE_PACKS': ';'.join(PACKS),
        'RECOMP_TEXTURE_ROOT': str(root), 'RECOMP_TEXTURE_CACHE_MB': '2048',
        'RECOMP_GAME_LANGUAGE': 'english', 'RECOMP_LANGUAGE_PACK': '',
        'RECOMP_STORY_SUBTITLES': 'auto'}}
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(profile, indent=2) + '\n', encoding='utf-8')
    os.environ[PROFILE_ENV] = str(output)
    return profile


def current():
    path = os.environ.get(PROFILE_ENV)
    return json.loads(Path(path).read_text(encoding='utf-8')) if path else None


def faults(run):
    profile = run.identity.get('regression_profile')
    if not profile:
        return []
    actual = run.identity['environment']
    bad = [key + ' differs from HD profile' for key in
           ('RECOMP_ASPECT', 'RECOMP_TEXTURE_PACKS', 'RECOMP_TEXTURE_ROOT')
           if actual.get(key) != profile['environment'][key]]
    # A launcher-only check may stop before the game initializes these systems.
    if '[FUNCCALL]' in run.text:
        if '[WS] widescreen 1.7778' not in run.text:
            bad.append('16:9 widescreen was not initialized')
        count = re.search(r'\[TEXPACK\] schema=1 entries=(\d+)', run.text)
        if not count or int(count[1]) != profile['expected_entries']:
            bad.append('HD texture entry count does not match both verified manifests')
    return bad
