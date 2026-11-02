#!/usr/bin/env python3
"""Bring Xbox profiles (original console, emulator, shared save zips) into the port's save area.

    python scripts/save-import.py saves.zip --save-dir "<data folder>/save"
    python scripts/save-import.py UDATA/45410049 --dry-run
    python scripts/save-import.py saves.zip --check          # signature status only, writes nothing

Close the game first. The save area is, in order: --save-dir, $DEFJAM_DATA/save, or the data folder of
the install named by --install (its installed.json; the macOS default install is tried last).

Why a plain copy fails. Each profile file (`<Name>` next to SaveMeta.xbx) starts with a 76-byte
header: "20CM", total size, header size (0x40), payload size, then three 20-byte HMAC-SHA1 fields:
    0x10  over file[0x4C:0x8C]   (the 64-byte identity block)
    0x24  over file[0x8C:]       (the payload)
    0x38  over file[0x00:0x38]   (the header up to here, so it covers the two above)
The game checks 0x38 first and shows "Damaged game" when it does not match. The key is the kernel's
XboxSignatureKey, which this port exports as 16 zero bytes, while saves made on a console were signed
with that console's key and cannot verify. This tool recomputes the three fields with the port's key
and leaves every other byte alone. Change the key (--key) only if the kernel ever exports another one.

What it copies: SaveMeta.xbx, the profile file and saveimage.xbx of each profile, into
UserData/45410049/<folder>. The folder is named from the profile name with the game's own hash, as
the game would create it. Files of other tools (datelinfo.xbx) and title files are not copied, except
that TitleMeta.xbx and TitleImage.xbx are added when the save area has none. An existing profile is
never overwritten unless --force, which first moves it to <save-dir>/import-backup-<stamp>/.

Standard library only. Zip members are read by name, never extracted by path.
"""
import argparse
import hmac
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import time
import zipfile
from pathlib import Path

TITLE_ID = '45410049'
MAGIC = b'20CM'
HEADER = 0x4C            # 4 + 12 + 3 * 20
IDENTITY_END = 0x8C      # 64-byte identity block starts at HEADER
PORT_KEY = bytes(16)     # XboxSignatureKey as the port's kernel exports it
MAX_MEMBER = 4 << 20     # a profile is ~22 KiB; refuse anything absurd (zip bombs)
HASH_MOD = 0xFFFFFFFFFFC5
NAME_RE = re.compile(r'^[ -~]{1,32}$')   # printable ASCII; the game's profile names


def folder_name(name):
    """The directory the game derives from a profile name (src/hooks/save_compat.c, canonical)."""
    value = 0
    for char in name:
        value = (value * 65536 + ord(char)) % HASH_MOD
    return '%012X' % value


def parse_save_meta(raw):
    """The Name= value of a SaveMeta.xbx (UTF-16LE with a BOM), or None."""
    try:
        text = raw.decode('utf-16')
    except UnicodeDecodeError:
        return None
    match = re.match(r'Name=([^\r\n]*)', text)
    return match.group(1) if match else None


def hmac_sha1(key, data):
    return hmac.new(key, data, hashlib.sha1).digest()


def header_fault(blob):
    """None when the profile file has the shape the game expects, else why not."""
    if len(blob) < IDENTITY_END + 1:
        return 'too short'
    if blob[:4] != MAGIC:
        return 'not a 20CM profile file'
    size, header, payload = struct.unpack_from('<III', blob, 4)
    if size != len(blob):
        return 'size field %d does not match the file (%d bytes)' % (size, len(blob))
    if header != 0x40:
        return 'unexpected header size 0x%X' % header
    if payload != len(blob) - IDENTITY_END:
        return 'payload size field %d does not match the file' % payload
    return None


def signatures_ok(blob, key):
    """(identity, payload, header) fields each equal to the HMAC under key."""
    return (blob[0x10:0x24] == hmac_sha1(key, blob[HEADER:IDENTITY_END]),
            blob[0x24:0x38] == hmac_sha1(key, blob[IDENTITY_END:]),
            blob[0x38:HEADER] == hmac_sha1(key, blob[:0x38]))


def resign(blob, key):
    out = bytearray(blob)
    out[0x10:0x24] = hmac_sha1(key, bytes(out[HEADER:IDENTITY_END]))
    out[0x24:0x38] = hmac_sha1(key, bytes(out[IDENTITY_END:]))
    out[0x38:HEADER] = hmac_sha1(key, bytes(out[:0x38]))
    return bytes(out)


class Profile:
    def __init__(self, name, folder, data, meta, image, source):
        self.name, self.folder, self.data, self.meta, self.image, self.source = name, folder, data, meta, image, source


def read_zip(path):
    """{profile directory: {file name: bytes}} from a zip, taking only files we know how to use."""
    groups = {}
    with zipfile.ZipFile(path) as zf:
        for info in zf.infolist():
            if info.is_dir():
                continue
            parts = [p for p in info.filename.replace('\\', '/').split('/') if p]
            if len(parts) < 2 or any(p in ('..', '.') or ':' in p for p in parts):
                continue
            if info.file_size > MAX_MEMBER:
                continue
            groups.setdefault(parts[-2], {})[parts[-1]] = zf.read(info)
    return groups


def read_folder(path):
    groups = {}
    for directory, _, names in os.walk(path):
        for name in names:
            file = Path(directory, name)
            if file.is_symlink() or file.stat().st_size > MAX_MEMBER:
                continue
            groups.setdefault(Path(directory).name, {})[name] = file.read_bytes()
    return groups


def profiles_from(groups, say):
    """Turn each directory that has a SaveMeta.xbx and one matching profile file into a Profile."""
    found = []
    for folder, files in sorted(groups.items()):
        lowered = {n.lower(): n for n in files}
        if 'savemeta.xbx' not in lowered:
            continue
        meta = files[lowered['savemeta.xbx']]
        name = parse_save_meta(meta)
        if not name or not NAME_RE.match(name):
            say('skip   %s: SaveMeta.xbx has no usable Name' % folder)
            continue
        if name not in files:
            say('skip   %s (%s): no profile file named %r beside SaveMeta.xbx' % (folder, name, name))
            continue
        image = files[lowered['saveimage.xbx']] if 'saveimage.xbx' in lowered else None
        found.append(Profile(name, folder, files[name], meta, image, folder))
    return found


def resolve_save_dir(args):
    if args.save_dir:
        return Path(args.save_dir)
    if os.environ.get('DEFJAM_DATA'):
        return Path(os.environ['DEFJAM_DATA']) / 'save'
    installs = [Path(args.install)] if args.install else []
    installs.append(Path.home() / 'Library/Application Support/DefJamRecompiled/app')
    for install in installs:
        receipt = install / 'installed.json'
        if receipt.is_file():
            try:
                return Path(json.loads(receipt.read_text(encoding='utf-8'))['data']) / 'save'
            except (OSError, ValueError, KeyError):
                pass
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source', help='a .zip of saves, or a folder holding UDATA/45410049 or the profile folders')
    ap.add_argument('--save-dir', help='the port save area (the folder that holds UserData)')
    ap.add_argument('--install', help='an install folder with installed.json, to find the data folder')
    ap.add_argument('--key', help='32 hex digits: sign with this key instead of the port key (all zero)')
    ap.add_argument('--dry-run', action='store_true', help='say what would be done; write nothing')
    ap.add_argument('--check', action='store_true', help='report signature status of the source; write nothing')
    ap.add_argument('--force', action='store_true', help='replace a profile that already exists (backed up first)')
    args = ap.parse_args(argv)
    say = print

    key = PORT_KEY
    if args.key:
        if not re.fullmatch(r'[0-9a-fA-F]{32}', args.key):
            ap.error('--key must be 32 hex digits')
        key = bytes.fromhex(args.key)

    source = Path(args.source)
    if not source.exists():
        say('error: %s does not exist' % source)
        return 2
    try:
        groups = read_zip(source) if zipfile.is_zipfile(source) else read_folder(source)
    except (OSError, zipfile.BadZipFile) as err:
        say('error: cannot read %s: %s' % (source, err))
        return 2
    profiles = profiles_from(groups, say)
    if not profiles:
        say('error: no profiles found (each needs a folder with SaveMeta.xbx and a file named like the profile)')
        return 1

    usable = []
    for prof in profiles:
        fault = header_fault(prof.data)
        if fault:
            say('skip   %s (%s): %s' % (prof.folder, prof.name, fault))
            continue
        ok = signatures_ok(prof.data, key)
        wanted = folder_name(prof.name)
        note = '' if wanted == prof.folder.upper() else ' [folder will be named %s]' % wanted
        say('found  %s -> %s  %s  signatures %s%s' % (
            prof.folder, prof.name, '%d bytes' % len(prof.data), 'already valid' if all(ok) else 'need re-signing', note))
        usable.append((prof, wanted, ok))
    if not usable or args.check:
        return 0 if usable else 1

    save_dir = resolve_save_dir(args)
    if save_dir is None:
        say('error: cannot tell where the save area is; pass --save-dir "<data folder>/save"')
        return 2
    title_dir = save_dir / 'UserData' / TITLE_ID
    stamp = time.strftime('%Y%m%d-%H%M%S')
    done = 0
    for prof, wanted, _ in usable:
        target = title_dir / wanted
        if target.exists():
            if not args.force:
                say('keep   %s (%s): already in the save area; use --force to replace it' % (wanted, prof.name))
                continue
            say('backup %s -> %s' % (target, save_dir / ('import-backup-' + stamp) / wanted))
            if not args.dry_run:
                backup = save_dir / ('import-backup-' + stamp)
                backup.mkdir(parents=True, exist_ok=True)
                shutil.move(str(target), str(backup / wanted))
        say('import %s -> %s' % (prof.name, target))
        if args.dry_run:
            done += 1
            continue
        target.mkdir(parents=True, exist_ok=True)
        (target / 'SaveMeta.xbx').write_bytes(prof.meta)
        (target / prof.name).write_bytes(resign(prof.data, key))
        if prof.image is not None:
            (target / 'saveimage.xbx').write_bytes(prof.image)
        done += 1

    if done and not args.dry_run:
        for name in ('TitleMeta.xbx', 'TitleImage.xbx'):
            blob = groups.get(TITLE_ID, {}).get(name)
            if blob is not None and not (title_dir / name).exists():
                (title_dir / name).write_bytes(blob)
    say('%s %d of %d profile(s)%s' % ('would import' if args.dry_run else 'imported', done, len(usable),
                                      ' (nothing written)' if args.dry_run else ''))
    return 0 if done else 1


if __name__ == '__main__':
    sys.exit(main())
