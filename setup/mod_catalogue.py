#!/usr/bin/env python3
"""Strict draft metadata validation and deterministic UNSIGNED preview builder."""
import argparse
import datetime as dt
import json
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
SLUG = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*\Z")
VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?\Z")


def require(value, message):
    if not value:
        raise ValueError(message)


def fields(value, expected):
    require(isinstance(value, dict) and set(value) == set(expected.split()),
            'Missing or unknown fields: expected ' + expected)


def text(value):
    require(isinstance(value, str) and bool(value.strip()) and len(value) <= 4096
            and not any(ord(c) < 32 for c in value), 'Invalid text')


def slug(value):
    text(value)
    require(len(value) <= 80 and SLUG.fullmatch(value), 'Invalid ID/folder')


def version(value):
    text(value)
    match = VERSION.fullmatch(value)
    require(match, 'Invalid version')
    if match[4]:
        require(all(not (p.isdigit() and len(p) > 1 and p[0] == '0')
                    for p in match[4].split('.')), 'Invalid numeric prerelease')
    return match


def strings(value, check=text, nonempty=False):
    require(isinstance(value, list) and (bool(value) or not nonempty), 'Invalid list')
    for item in value:
        check(item)
    require(len(value) == len(set(value)), 'Duplicate list item')


def https(value):
    text(value)
    parsed = urlsplit(value)
    require(parsed.scheme == 'https' and parsed.hostname and not parsed.username
            and not parsed.password and not parsed.fragment and not any(c.isspace() for c in value)
            and not any(c in value for c in '\\'), 'Expected credential-free HTTPS URL')
    try:
        parsed.port
    except ValueError as error:
        raise ValueError('Invalid URL port') from error


def positive(value):
    require(type(value) is int and 0 < value <= 2**53 - 1, 'Invalid positive integer')


def validate_mod(mod):
    fields(mod, 'id name description authors class licence homepage versions revoked_versions')
    slug(mod['id'])
    for key in ('name', 'description', 'licence'):
        text(mod[key])
    strings(mod['authors'], nonempty=True)
    require(mod['class'] in ('linked', 'original'), 'Invalid content class')
    https(mod['homepage'])
    require(isinstance(mod['versions'], list) and mod['versions'], 'Missing versions')
    seen = set()
    for row in mod['versions']:
        fields(row, 'version channel min_recomp capabilities reviewed depends conflicts load_after archive')
        parsed = version(row['version'])
        require(row['version'] not in seen, 'Duplicate version')
        seen.add(row['version'])
        require(row['channel'] == ('prerelease' if parsed[4] else 'stable'), 'Channel mismatch')
        require(not version(row['min_recomp'])[4], 'Minimum runtime must be a stable version')
        strings(row['capabilities'], slug)
        text(row['reviewed'])
        require(dt.date.fromisoformat(row['reviewed']).isoformat() == row['reviewed'], 'Invalid review date')
        for key in ('depends', 'conflicts', 'load_after'):
            strings(row[key], slug)
            require(mod['id'] not in row[key], 'Self relationship')
        require(not set(row['depends']) & set(row['conflicts']), 'Dependency/conflict overlap')
        archive = row['archive']
        fields(archive, 'urls size unpacked_size file_count sha256 root')
        strings(archive['urls'], https, nonempty=True)
        for key in ('size', 'unpacked_size', 'file_count'):
            positive(archive[key])
        text(archive['sha256'])
        require(re.fullmatch(r'[0-9a-f]{64}', archive['sha256']), 'Invalid SHA-256')
        slug(archive['root'])
    strings(mod['revoked_versions'], version)
    require(set(mod['revoked_versions']) <= seen, 'Unknown revoked version')
    return mod


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON key')
        result[key] = value
    return result


def load_mods(folder):
    rows = []
    for path in sorted(Path(folder).glob('*.json')):
        require(not path.is_symlink() and path.stat().st_size <= 1024 * 1024, 'Unsafe metadata file')
        row = validate_mod(json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_object))
        require(path.stem == row['id'], 'Filename/ID mismatch')
        rows.append(row)
    require(len(rows) == len({row['id'] for row in rows}), 'Duplicate mod ID')
    return rows


def timestamp(value):
    require(isinstance(value, str) and re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ', value),
            'Timestamp must be UTC YYYY-MM-DDTHH:MM:SSZ')
    return dt.datetime.strptime(value, '%Y-%m-%dT%H:%M:%SZ')


def build(rows, revision, issued, expires):
    positive(revision)
    require(timestamp(expires) > timestamp(issued), 'Expiry must follow issue time')
    for row in rows:
        validate_mod(row)
    require(len(rows) == len({row['id'] for row in rows}), 'Duplicate mod ID')
    index = dict(schema=1, game='defjam-ffny-recomp', revision=revision, issued=issued,
                 expires=expires, mods=sorted(rows, key=lambda row: row['id']))
    return (json.dumps(index, sort_keys=True, ensure_ascii=False, separators=(',', ':'), allow_nan=False) + '\n').encode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('validate', 'build'))
    parser.add_argument('--mods', type=Path, default=ROOT / 'mods')
    parser.add_argument('--revision', type=int)
    parser.add_argument('--issued')
    parser.add_argument('--expires')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    try:
        require(args.mods.is_dir() and not args.mods.is_symlink(), 'Missing/unsafe mods directory')
        rows = load_mods(args.mods)
        if args.command == 'build':
            require(args.out is not None, 'Missing --out')
            data = build(rows, args.revision, args.issued, args.expires)
            with args.out.open('xb') as stream:
                stream.write(data)
            print('Wrote UNSIGNED development preview; not a trusted release')
        else:
            print(f'Validated {len(rows)} mod source records; no download/content verification')
    except (ValueError, OSError, TypeError) as error:
        parser.exit(1, f'Catalogue error: {error}\n')


if __name__ == '__main__':
    main()
