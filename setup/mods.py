#!/usr/bin/env python3
"""Offline-first texture mod helper. Network, verification and mutations stay here.

The launcher consumes only the bounded INI view; settings own the load order.
All commands hold the writer lock; mutations also exclude a running game.
"""
import argparse
import base64
import configparser
from contextlib import contextmanager
import datetime as dt
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid
import zipfile

# Windows' bundled Python uses ._pth isolation and does not add the script's
# directory to sys.path. Resolve the sibling contract from this installation.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import mod_catalogue as contract

CATALOGUE_URL = 'https://vyanhursky.github.io/DefJamFFNY-mods/index.json'
KEYS = Path(__file__).resolve().parents[1] / 'config/mods/catalogue-keys.json'
MAX_INDEX = 8 * 1024**2
MAX_FILE = 512 * 1024**2
MAX_ARCHIVE = 8 * 1024**3
MAX_FILES = 65536
ALLOWED = {'.ini', '.png', '.dds', '.json', '.txt'}


class ModError(ValueError):
    pass


def check(value, message):
    if not value:
        raise ModError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def linked(path):
    path = Path(path)
    return path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction())


def read_json(path, default=None, maximum=MAX_INDEX):
    path = Path(path)
    if not path.exists() and not linked(path):
        return default
    check(not linked(path) and path.is_file() and path.stat().st_size <= maximum,
          'Unsafe or oversized state file')
    return json.loads(path.read_bytes(), object_pairs_hook=contract.unique_object)


def sync_directory(path):
    if os.name != 'nt':
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def durable_rename(source, target):
    source, target = Path(source), Path(target)
    if os.name == 'nt':
        import ctypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.MoveFileExW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint]
        kernel.MoveFileExW.restype = ctypes.c_int
        # Launcher polling and Windows file scanners can briefly deny replacing
        # a snapshot. Retry only sharing/access errors, for at most 0.9 seconds;
        # permanent errors still fail and preserve the previous installation.
        for attempt in range(10):
            if kernel.MoveFileExW(str(source), str(target), 0x1 | 0x8):
                break
            error = ctypes.get_last_error()
            if error not in (5, 32, 33) or attempt == 9:
                raise ctypes.WinError(error)
            time.sleep(0.02 * (attempt + 1))
    else:
        os.replace(source, target)
        sync_directory(target.parent)
        if source.parent != target.parent:
            sync_directory(source.parent)


def atomic_bytes(path, data):
    path = Path(path)
    check(not linked(path), 'Refusing state symlink')
    fd, name = tempfile.mkstemp(prefix='.pending-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        durable_rename(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def atomic_json(path, value):
    atomic_bytes(path, json_bytes(value))


def json_bytes(value):
    data = (json.dumps(value, sort_keys=True, ensure_ascii=False) + '\n').encode()
    check(len(data) <= MAX_INDEX, 'Manager metadata exceeds size limit')
    return data


def safe_dir(path):
    path = Path(path)
    check(not linked(path), 'Refusing directory symlink: ' + str(path))
    path.mkdir(parents=True, exist_ok=True)
    check(path.is_dir(), 'Not a directory')
    return path


@contextmanager
def file_lock(path, shared=False):
    check(not linked(path), 'Refusing lock symlink')
    with Path(path).open('a+b') as stream:
        try:
            if os.name == 'nt':
                import msvcrt
                # Byte-range locks may cover EOF. Do not initialize the byte:
                # a running game holds it, and buffered writes can fail again
                # during close, escaping the normal busy-error handler.
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBRLCK if shared else msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream, (fcntl.LOCK_SH if shared else fcntl.LOCK_EX) | fcntl.LOCK_NB)
        except OSError as error:
            raise ModError('Mods are busy or the game is running; try again after it closes') from error
        try:
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            # Closing releases the OS-held lock, including on process exit.


def safe_relative(value):
    check(isinstance(value, str) and 0 < len(value) < 900 and '..' not in value,
          'Unsafe relative path')
    check(re.fullmatch(r'[A-Za-z0-9_./-]+', value), 'Unsupported path characters')
    parts = PurePosixPath(value).parts
    check(parts and not value.startswith('/') and all(p not in ('.', '..') and not p.startswith('.') for p in parts),
          'Unsafe relative path')
    # Windows aliases, device names, trailing dots/spaces and case collisions are unsafe on every host.
    reserved = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)), *(f'LPT{i}' for i in range(1, 10))}
    check(all(not p.endswith('.') and p.split('.')[0].upper() not in reserved for p in parts), 'Reserved path')
    check('/'.join(parts) == value, 'Noncanonical path')
    return parts


def image_shape(path, png_only=False):
    with Path(path).open('rb') as stream:
        head = stream.read(128)
    if Path(path).suffix.lower() == '.png':
        check(head[:8] == b'\x89PNG\r\n\x1a\n' and len(head) >= 33 and head[12:16] == b'IHDR', 'Invalid PNG')
        from PIL import Image
        with Image.open(path) as picture:
            check(picture.format == 'PNG' and not getattr(picture, 'is_animated', False), 'Unsupported PNG')
            w, h = picture.size
            check(0 < w <= 16384 and 0 < h <= 16384 and w * h <= 64 * 1024**2, 'Image dimensions exceed limits')
            picture.verify()
        return w, h
    check(not png_only and Path(path).suffix.lower() == '.dds' and len(head) == 128 and head[:4] == b'DDS ', 'Unsupported image')
    size, flags, h, w = struct.unpack_from('<4I', head, 4)
    check(size == 124 and 0 < w <= 16384 and 0 < h <= 16384 and w * h <= 64 * 1024**2, 'Invalid DDS dimensions')
    check(struct.unpack_from('<I', head, 76)[0] == 32 and head[84:88] in (b'DXT1', b'DXT3', b'DXT5'), 'Unsupported DDS format')
    mips = max(1, struct.unpack_from('<I', head, 28)[0])
    check(mips <= 15 and mips <= max(w, h).bit_length(), 'Invalid DDS mip count')
    check(struct.unpack_from('<I', head, 112)[0] == 0 and struct.unpack_from('<I', head, 24)[0] <= 1 and struct.unpack_from('<I', head, 80)[0] & 4, 'DDS cube/volume unsupported')
    block = 8 if head[84:88] == b'DXT1' else 16
    expected = 128
    for _ in range(mips):
        expected += max(1, (w + 3)//4) * max(1, (h + 3)//4) * block
        w, h = max(1, w//2), max(1, h//2)
    check(Path(path).stat().st_size == expected, 'Invalid DDS payload size')
    return struct.unpack_from('<I', head, 16)[0], struct.unpack_from('<I', head, 12)[0]


def validate_pack(folder):
    manifest = folder / 'manifest.ini'
    check(manifest.is_file() and not manifest.is_symlink() and manifest.stat().st_size <= 16 * 1024**2,
          'Missing/oversized manifest')
    raw = manifest.read_text(encoding='utf-8-sig')
    check(all(len(line.encode('utf-8')) < 1900 for line in raw.splitlines()), 'Manifest line too long')
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str
    parser.read_string(raw)
    check(not parser.defaults() and set(parser.sections()) <= {'pack', 'textures', 'wide'}, 'Unsupported manifest section')
    check(parser.get('pack', 'schema', fallback='') == '1', 'Unsupported manifest schema')
    check(parser.get('pack', 'png_mips', fallback='opacity') in ('channels', 'opacity'), 'Invalid PNG mip policy')
    check(parser.has_section('textures') and 0 < len(parser['textures']) <= MAX_FILES, 'Missing texture entries')
    refs, dimensions = set(), {}
    def reference(value, png_only=False):
        safe_relative(value)
        path = folder.joinpath(*PurePosixPath(value).parts)
        check(path.is_file() and not linked(path), 'Missing image: ' + value)
        dims = image_shape(path, png_only)
        refs.add(value)
        return dims
    for key, value in parser['textures'].items():
        check(re.fullmatch('[0-9a-f]{64}', key), 'Invalid texture ID')
        dimensions[key] = reference(value)
    if parser.has_section('wide'):
        for key, value in parser['wide'].items():
            check(re.fullmatch('[0-9a-f]{64}', key), 'Invalid wide anchor ID')
            if value == 'stretch':
                continue  # Runtime can stretch a live/original overlay without replacing its pixels.
            check(key in dimensions, 'Wide slab anchor has no texture entry')
            slabs = value.split(';')
            check(0 < len(slabs) <= 16, 'Too many wide slabs')
            for slab in slabs:
                shift, separator, relative = slab.partition(':')
                check(separator and (shift == 'inner' or re.fullmatch(r'-?[0-9]+', shift)), 'Invalid wide shift')
                check(shift == 'inner' or -16384 <= int(shift) <= 16384, 'Wide shift exceeds limits')
                check(reference(relative, True) == dimensions[key], 'Wide slab/anchor dimensions differ')
    return {'capabilities': ['textures-v1'] + (['wide-v1'] if parser.has_section('wide') else []),
            'texture_ids': list(dimensions), 'wide_ids': list(parser['wide']) if parser.has_section('wide') else []}


def unpack_archive(archive, descriptor, destination, cancelled=lambda: None):
    check(Path(archive).stat().st_size == descriptor['size'] and sha(archive) == descriptor['sha256'],
          'Package size or SHA-256 mismatch')
    check(descriptor['size'] <= MAX_ARCHIVE and descriptor['unpacked_size'] <= 16 * 1024**3
          and descriptor['file_count'] <= MAX_FILES, 'Package exceeds installer limits')
    root = descriptor['root']
    contract.slug(root)
    files, seen, size = [], set(), 0
    with zipfile.ZipFile(archive) as bundle:
        check(len(bundle.infolist()) <= MAX_FILES * 2, 'Too many archive entries')
        for row in bundle.infolist():
            cancelled()
            name = row.filename[:-1] if row.is_dir() else row.filename
            parts = safe_relative(name)
            check(parts[0] == root, 'Unexpected archive root')
            folded = name.casefold()
            check(folded not in seen, 'Duplicate/case-colliding archive path')
            seen.add(folded)
            mode = row.external_attr >> 16
            check(not row.flag_bits & 1 and stat.S_IFMT(mode) in (0, stat.S_IFREG, stat.S_IFDIR), 'Links/encrypted/special entries forbidden')
            if row.is_dir():
                continue
            check(len(parts) > 1 and PurePosixPath(name).suffix.lower() in ALLOWED
                  and row.file_size <= MAX_FILE, 'Unsupported/oversized archive file')
            size += row.file_size
            files.append((row, parts[1:]))
        check(len(files) == descriptor['file_count'] and size == descriptor['unpacked_size'], 'Unpacked size/file-count mismatch')
        check(shutil.disk_usage(destination.parent).free >= size + 32 * 1024**2, 'Not enough free space to install')
        destination.mkdir()
        hashes = {}
        for row, parts in files:
            cancelled()
            target = destination.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with bundle.open(row) as source, target.open('xb') as output:
                copied = 0
                while chunk := source.read(1024 * 1024):
                    cancelled()
                    copied += len(chunk)
                    check(copied <= row.file_size, 'Archive expansion exceeds declared size')
                    output.write(chunk)
                output.flush()
                os.fsync(output.fileno())
            check(copied == row.file_size, 'Truncated archive entry')
            hashes['/'.join(parts)] = sha(target)
        for directory in sorted((p for p in destination.rglob('*') if p.is_dir()),
                                key=lambda p: len(p.parts), reverse=True):
            sync_directory(directory)
        sync_directory(destination)
        pack = validate_pack(destination)
    return dict(files=hashes, **pack)


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        contract.https(newurl)
        return super().redirect_request(request, fp, code, msg, headers, newurl)


def open_url(url, headers=None):
    contract.https(url)
    return urllib.request.build_opener(SafeRedirect()).open(
        urllib.request.Request(url, headers={'User-Agent': 'DefJamMods/1', 'Accept-Encoding': 'identity', **(headers or {})}), timeout=20)


def fetch_small(url, limit):
    with open_url(url) as response:
        check(response.status == 200, 'Unexpected catalogue response')
        data = response.read(limit + 1)
        check(len(data) <= limit, 'Catalogue response too large')
        return data


def semver(value):
    match = contract.version(value.removeprefix('v'))
    pre = match[4]
    return (int(match[1]), int(match[2]), int(match[3]), pre is None,
            tuple((0, int(p)) if p.isdigit() else (1, p) for p in pre.split('.')) if pre else ())


def verify_index(raw, signature, keys, highest=0, digest=None, now=None, fresh=False):
    check(len(raw) <= MAX_INDEX and len(signature) == 64, 'Invalid catalogue/signature length')
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    verified = False
    for key in keys:
        try:
            Ed25519PublicKey.from_public_bytes(bytes.fromhex(key)).verify(signature, raw)
            verified = True
            break
        except InvalidSignature:
            pass
    check(verified, 'Catalogue signature is invalid or untrusted')
    index = json.loads(raw, object_pairs_hook=contract.unique_object)
    contract.fields(index, 'schema game revision issued expires mods')
    check(index['schema'] == 1 and index['game'] == 'defjam-ffny-recomp', 'Unsupported catalogue')
    # Reuse the exact source contract but do not canonicalize before signature verification.
    contract.build(index['mods'], index['revision'], index['issued'], index['expires'])
    check(index['revision'] >= highest, 'Catalogue rollback rejected')
    check(index['revision'] != highest or not digest or hashlib.sha256(raw).hexdigest() == digest,
          'Catalogue changed without a new revision')
    now = now or dt.datetime.now(dt.timezone.utc).replace(tzinfo=None)
    check(contract.timestamp(index['issued']) <= now + dt.timedelta(minutes=5), 'Catalogue issued in the future')
    if fresh:
        check(contract.timestamp(index['expires']) > now, 'Catalogue expired; refresh before installing')
    return index


class Manager:
    def __init__(self, data, runtime='0.0.0', capabilities=('textures-v1',), keys=None, cancel=None, prerelease=False):
        self.data = Path(data).resolve()
        safe_dir(self.data)
        self.root = safe_dir(self.data / 'mod-manager')
        self.mods = safe_dir(self.data / 'mods')
        self.cache = safe_dir(self.root / 'downloads')
        self.staging = safe_dir(self.root / 'staging')
        self.backups = safe_dir(self.root / 'rollback')
        self.runtime, self.capabilities = runtime, set(capabilities)
        key_config = read_json(KEYS, {}) if keys is None else {'keys': keys}
        self.keys = key_config.get('keys', [])
        self.cancel_file = Path(cancel) if cancel else None
        self.message = ''
        self.prerelease = prerelease

    def cancelled(self):
        check(not (self.cancel_file and self.cancel_file.exists()), 'Operation cancelled; previous installation preserved')

    def progress(self, text, done=0, total=0):
        self.message = text
        clean = text.replace('\n', ' ').replace('\r', ' ')[:2000]
        atomic_bytes(self.root / 'progress.ini', f'[progress]\nmessage={clean}\ndone={done}\ntotal={total}\n'.encode())
        print(json.dumps(dict(event='progress', message=text, done=done, total=total)), flush=True)

    def state(self):
        state = read_json(self.root / 'state.json', {'schema': 1, 'installed': {}})
        check(state.get('schema') == 1 and isinstance(state.get('installed'), dict), 'Invalid manager state')
        return state

    def trusted(self, fresh=False):
        saved = read_json(self.root / 'catalogue.json')
        check(saved, 'No verified catalogue cached; use Refresh catalogue when online')
        return verify_index(base64.b64decode(saved['raw'], validate=True), base64.b64decode(saved['signature'], validate=True),
                            self.keys, saved['revision'], saved['sha256'], fresh=fresh)

    def refresh(self, url=CATALOGUE_URL):
        check(self.keys, 'Catalogue signing key has not been provisioned; local mods still work')
        self.progress('Checking project catalogue')
        raw = fetch_small(url, MAX_INDEX)
        signature = fetch_small(url + '.sig', 64)
        previous = read_json(self.root / 'catalogue.json', {})
        index = verify_index(raw, signature, self.keys, previous.get('revision', 0), previous.get('sha256'), fresh=True)
        atomic_json(self.root / 'catalogue.json', dict(raw=base64.b64encode(raw).decode(),
                    signature=base64.b64encode(signature).decode(), revision=index['revision'],
                    sha256=hashlib.sha256(raw).hexdigest(), fetched=dt.datetime.now(dt.timezone.utc).isoformat()))
        self.progress('Catalogue verified and refreshed')

    def choose(self, mod_id, requested=None, prerelease=None):
        prerelease = self.prerelease if prerelease is None else prerelease
        index = self.trusted(fresh=True)
        mod = next((m for m in index['mods'] if m['id'] == mod_id), None)
        check(mod, 'Mod not found in verified catalogue')
        candidates = [v for v in mod['versions'] if v['version'] not in mod['revoked_versions']
                      and (prerelease or v['channel'] == 'stable')
                      and semver(v['min_recomp']) <= semver(self.runtime)
                      and set(v['capabilities']) <= self.capabilities
                      and (requested is None or v['version'] == requested)]
        check(candidates, 'No compatible, nonrevoked version available for this runtime/channel')
        return mod, max(candidates, key=lambda v: semver(v['version']))

    def download(self, archive):
        check(archive['size'] <= MAX_ARCHIVE, 'Archive exceeds download limit')
        complete = self.cache / (archive['sha256'] + '.zip')
        partial = self.cache / (archive['sha256'] + '.part')
        check(not linked(complete) and not linked(partial), 'Unsafe download cache link')
        if complete.is_file() and complete.stat().st_size == archive['size'] and sha(complete) == archive['sha256']:
            return complete
        last = None
        for url in archive['urls']:
            self.cancelled()
            start = partial.stat().st_size if partial.exists() else 0
            if start >= archive['size']:
                partial.unlink()
                start = 0
            try:
                with open_url(url, {'Range': f'bytes={start}-'} if start else {}) as response:
                    if response.status == 206:
                        expected = f'bytes {start}-{archive["size"]-1}/{archive["size"]}'
                        check(response.headers.get('Content-Range') == expected, 'Invalid range response')
                    elif response.status == 200:
                        start = 0
                    else:
                        raise ModError('Unexpected download status')
                    check(response.headers.get('Content-Encoding', 'identity') == 'identity', 'Encoded download unsupported')
                    length = response.headers.get('Content-Length')
                    check(length is not None and int(length) == archive['size'] - start, 'Invalid download length')
                    check(shutil.disk_usage(self.cache).free >= archive['size'] - start + 32 * 1024**2, 'Not enough free download space')
                    with partial.open('ab' if start else 'wb') as output:
                        count = start
                        while chunk := response.read(1024 * 1024):
                            self.cancelled()
                            count += len(chunk)
                            check(count <= archive['size'], 'Download exceeds signed size')
                            output.write(chunk)
                            self.progress('Downloading package', count, archive['size'])
                check(partial.stat().st_size == archive['size'] and sha(partial) == archive['sha256'], 'Download SHA-256/size mismatch')
                os.replace(partial, complete)
                return complete
            except (OSError, ValueError, urllib.error.URLError) as error:
                last = error
                if isinstance(error, ModError) and 'cancelled' in str(error):
                    raise
                if partial.exists() and partial.stat().st_size >= archive['size']:
                    partial.unlink()
        raise ModError('Download failed: ' + str(last))

    def unchanged(self, folder, receipt):
        if not folder.is_dir() or linked(folder):
            return False
        actual = {}
        for path in folder.rglob('*'):
            if linked(path):
                return False
            if path.is_file():
                actual[path.relative_to(folder).as_posix()] = sha(path)
        return actual == receipt['files']

    def recover(self):
        path = self.root / 'journal.json'
        journal = read_json(path)
        if not journal:
            return
        contract.slug(journal['id'])
        target, backup = self.mods / journal['id'], self.backups / journal['id']
        old, new = journal['old'], journal['new']
        state = self.state()
        if new and self.unchanged(target, new):
            state['installed'][journal['id']] = new
        elif old and self.unchanged(target, old):
            state['installed'][journal['id']] = old
        elif not target.exists() and not linked(target) and old and self.unchanged(backup, old):
            if new is None:  # Removal reached its filesystem commit.
                state['installed'].pop(journal['id'], None)
            else:
                durable_rename(backup, target)
                state['installed'][journal['id']] = old
        elif not old and not target.exists() and not linked(target):
            state['installed'].pop(journal['id'], None)
        else:
            raise ModError('Interrupted mod install needs recovery; files differ, preserved for inspection')
        atomic_json(self.root / 'state.json', state)
        path.unlink()
        sync_directory(self.root)
        self.progress('Recovered interrupted mod transaction')

    def commit(self, mod_id, stage, new):
        contract.slug(mod_id)
        with file_lock(self.root / 'runtime.lock'):
            self.recover()
            state = self.state()
            old = state['installed'].get(mod_id)
            target, backup = self.mods / mod_id, self.backups / mod_id
            if old:
                check(self.unchanged(target, old), 'Managed mod has local edits/missing files; preserve them before replacing')
            else:
                check(not target.exists() and not linked(target), 'A local folder already uses this mod ID; it will not be overwritten')
            check(not linked(backup), 'Unsafe rollback folder')
            if backup.exists():
                check(old and old.get('previous') and self.unchanged(backup, old['previous']),
                      'Rollback folder has unowned or edited data; preserve it before replacing')
            if new and old:
                new['previous'] = {k: v for k, v in old.items() if k != 'previous'}
            if new:
                state['installed'][mod_id] = new
            else:
                state['installed'].pop(mod_id, None)
            # Refuse a receipt the bounded reader could not reopen, before any
            # pack moves. The journal's own bound is checked by atomic_json.
            json_bytes(state)
            atomic_json(self.root / 'journal.json', dict(id=mod_id, old=old, new=new))
            if backup.exists():
                shutil.rmtree(backup)
                sync_directory(self.backups)
            if old:
                durable_rename(target, backup)
            if new:
                durable_rename(stage, target)
                state['installed'][mod_id] = new
            else:
                state['installed'].pop(mod_id, None)
            atomic_json(self.root / 'state.json', state)
            (self.root / 'journal.json').unlink()
            sync_directory(self.root)
            if new is None and backup.exists():
                shutil.rmtree(backup)
                sync_directory(self.backups)

    def install(self, mod_id, requested=None, prerelease=None, archive_path=None):
        mod, version = self.choose(mod_id, requested, prerelease)
        state = self.state()
        check(all(dep in state['installed'] for dep in version['depends']), 'Install required dependencies first')
        check(not any(dep in state['installed'] for dep in version['conflicts']), 'Conflicting managed mod installed')
        for other_id, other in state['installed'].items():
            check(other_id == mod_id or mod_id not in other.get('conflicts', []), 'Installed mod declares a conflict')
        package = Path(archive_path) if archive_path else self.download(version['archive'])
        stage = self.staging / uuid.uuid4().hex
        try:
            self.progress('Verifying and staging package')
            receipt = unpack_archive(package, version['archive'], stage, self.cancelled)
            check(set(receipt['capabilities']) <= self.capabilities, 'Pack requires unsupported runtime features')
            check(set(receipt['capabilities']) <= set(version['capabilities']), 'Catalogue omits a required pack capability')
            receipt.update(id=mod_id, folder=mod_id, version=version['version'], name=mod['name'],
                           archive_sha256=version['archive']['sha256'], conflicts=version['conflicts'])
            self.cancelled()
            self.progress('Installing verified package')
            self.commit(mod_id, stage, receipt)
            self.progress('Installed; enable this pack in the Installed mods table, then set its priority in Load Order')
        finally:
            if stage.exists():
                shutil.rmtree(stage)

    def remove(self, mod_id):
        check(mod_id in self.state()['installed'], 'Only managed mods can be removed here')
        self.commit(mod_id, None, None)
        self.progress('Removed managed mod; local folders were preserved')

    def rollback(self, mod_id):
        contract.slug(mod_id)
        receipt = self.state()['installed'].get(mod_id, {}).get('previous')
        check(receipt and self.unchanged(self.backups / mod_id, receipt), 'No intact prior version available')
        stage = self.staging / uuid.uuid4().hex
        try:
            shutil.copytree(self.backups / mod_id, stage)
            self.cancelled()
            self.commit(mod_id, stage, receipt)
            self.progress('Previous version restored')
        finally:
            if stage.exists():
                shutil.rmtree(stage)

    def view(self):
        state = self.state()
        rows, info = [], 'Catalogue has not been published yet; local mods remain available'
        try:
            index = self.trusted()
            stale = contract.timestamp(index['expires']) <= dt.datetime.now(dt.timezone.utc).replace(tzinfo=None)
            info = ('Cached catalogue expired; refresh before installing' if stale else
                    f'Verified catalogue revision {index["revision"]}; updates are your choice')
            for mod in index['mods']:
                installed = state['installed'].get(mod['id'], {})
                choices = [v for v in mod['versions'] if (self.prerelease or v['channel'] == 'stable') and v['version'] not in mod['revoked_versions']
                           and semver(v['min_recomp']) <= semver(self.runtime) and set(v['capabilities']) <= self.capabilities]
                latest = max(choices, key=lambda v: semver(v['version'])) if choices else None
                revoked = installed.get('version') in mod['revoked_versions']
                rows.append(dict(id=mod['id'], name=mod['name'], authors=', '.join(mod['authors']), description=mod['description'],
                    installed=installed.get('version', ''), latest=latest['version'] if latest else '',
                    bytes=latest['archive']['size'] if latest else 0,
                    channel=latest['channel'] if latest else '', compatible=int(bool(latest) and not stale),
                    revoked=int(revoked), update=int(bool(latest) and bool(installed) and
                        semver(latest['version']) > semver(installed['version'])),
                    load_after=','.join(latest['load_after']) if latest else ''))
        except (ValueError, OSError, ImportError) as error:
            info = str(error)
        lines = ['[manager]', 'message=' + self.message, 'catalogue=' + info]
        def section(name, values):
            lines.append('[' + name + ']')
            for key, value in values.items():
                lines.append(key + '=' + str(value).replace('\n', ' ').replace('\r', ' ')[:4000])
        for row in rows:
            section('catalogue.' + row['id'], row)
        for mod_id, receipt in state['installed'].items():
            contract.slug(mod_id)
            section('installed.' + mod_id, dict(id=mod_id, folder=receipt['folder'], name=receipt['name'],
                    version=receipt['version'], rollback=int(bool(receipt.get('previous')))))
        atomic_bytes(self.root / 'view.ini', ('\n'.join(lines) + '\n').encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('status', 'refresh', 'install', 'remove', 'rollback', 'recover'))
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--runtime', default='0.0.0')
    parser.add_argument('--capability', action='append', default=['textures-v1'])
    parser.add_argument('--id')
    parser.add_argument('--version')
    parser.add_argument('--prerelease', action='store_true')
    parser.add_argument('--cancel-file')
    args = parser.parse_args()
    manager = None
    def failure(error):
        print(json.dumps(dict(event='error', message=str(error))), flush=True)
        return 1
    try:
        manager = Manager(args.data_dir, args.runtime, args.capability, cancel=args.cancel_file, prerelease=args.prerelease)
        with file_lock(manager.root / 'writer.lock'):
            try:
                if (manager.root / 'journal.json').exists():
                    with file_lock(manager.root / 'runtime.lock'):
                        manager.recover()
                if args.command == 'refresh':
                    manager.refresh()
                elif args.command == 'install':
                    manager.install(args.id, args.version, args.prerelease)
                elif args.command == 'remove':
                    manager.remove(args.id)
                elif args.command == 'rollback':
                    manager.rollback(args.id)
                manager.view()
            except (ValueError, OSError, ImportError, KeyError, TypeError, configparser.Error, zipfile.BadZipFile) as error:
                manager.progress(str(error))
                manager.view()
                return failure(error)
        print(json.dumps(dict(event='complete', message=manager.message)), flush=True)
        return 0
    except (ValueError, OSError, ImportError, KeyError, TypeError, configparser.Error, zipfile.BadZipFile) as error:
        # A failed writer lock must not overwrite the active writer's progress/view.
        return failure(error)


if __name__ == '__main__':
    sys.exit(main())
