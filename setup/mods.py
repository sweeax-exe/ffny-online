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
import socket
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
import content_compose

# FFNY Online's own catalogue: the account service signs it with its content key when the team accepts a
# pack (docs/content-packs.md); the launcher syncs it at every start ("sync"). index-v4.json lists everything,
# also new fighters at the 20 low places (build 85, newfighter.LOW_PLACES; their versions need the capability
# fighter-places-v2); index-v3.json is the same catalogue without those, for launchers of builds 80..84, whose
# check refuses a place outside 100..127; index-v3.json and up list the mods of a pack of several mods (with
# "bundle" and "bundle_name"); index-v2.json is without pack items, for launchers of build 79; index.json also
# leaves new fighters out, for launchers up to build 78. All share their revisions. Build 85: index-v4.json also
# lists the skins (new fighters the team accepted as an extra outfit of a stock fighter: "fighter_mode": "skin",
# "skin_of", no place). Build 86: index-v5.json is everything plus the FFNY team's official maps (kind official-map,
# rts_map, venue_fe_id; their versions need official-map-v1); index-v4.json is the same without them.
CATALOGUE_URL = 'https://play.defjam.online/updates/content/index-v5.json'
CATALOGUE_URL_V4 = 'https://play.defjam.online/updates/content/index-v4.json'
CATALOGUE_URL_V3 = 'https://play.defjam.online/updates/content/index-v3.json'
CATALOGUE_URL_V2 = 'https://play.defjam.online/updates/content/index-v2.json'
CATALOGUE_URL_V1 = 'https://play.defjam.online/updates/content/index.json'
KEYS = Path(__file__).resolve().parents[1] / 'config/mods/catalogue-keys.json'


def local_test_url(url):
    """Tests only: FFNY_CONTENT_URL may name a plain-HTTP catalogue on this machine (a local account
    service with -content-serve; its packs' homepage is the bare -public-url, http://127.0.0.1:<port>); its key
    comes from FFNY_CONTENT_TEST_KEYS. Never another host."""
    return isinstance(url, str) and re.match(r'http://(127\.0\.0\.1|localhost):[0-9]{1,5}(/|$)', url) is not None


def local_catalogue_url():
    url = os.environ.get('FFNY_CONTENT_URL', '')
    return url if local_test_url(url) else ''


if local_catalogue_url():
    _https = contract.https

    def _https_or_local(value):
        if local_test_url(value):
            return
        _https(value)
    contract.https = _https_or_local
MAX_INDEX = 8 * 1024**2
MAX_FILE = 512 * 1024**2
MAX_ARCHIVE = 8 * 1024**3
MAX_FILES = 65536
ALLOWED = {'.ini', '.png', '.dds', '.json', '.txt', '.md', '.djd', '.ffdelta', '.sch', '.asf'}  # language and FFNY Studio deltas; .sch/.asf: a new fighter's own voice clips (EA SCHl)


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


LANGUAGE_TARGET = re.compile(r'[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*')
LANGUAGE_DELTA = re.compile(r'[A-Za-z0-9_-]+(/[A-Za-z0-9_.-]+)*\.djd')


def validate_language(folder):
    """A language mod (docs/language-packs.md): language.ini and its deltas. The game checks each delta
    against the player's dump when it builds the pack; here only the shape and the files' presence."""
    ini = folder / 'language.ini'
    check(ini.is_file() and not linked(ini) and ini.stat().st_size <= 1024**2, 'Missing/oversized language.ini')
    raw = ini.read_text(encoding='utf-8-sig')
    check(all(len(line.encode('utf-8')) < 1900 for line in raw.splitlines()), 'language.ini line too long')
    parser = configparser.ConfigParser(interpolation=None, comment_prefixes=(';', '#'),
                                       inline_comment_prefixes=None, strict=True)
    parser.optionxform = str
    parser.read_string(raw)
    check(not parser.defaults() and set(parser.sections()) <= {'language', 'files'}, 'Unsupported language.ini section')
    check(parser.get('language', 'schema', fallback='') == '1', 'Unsupported language.ini schema')
    check(parser.get('language', 'name', fallback='').strip() and parser.get('language', 'language', fallback='').strip(),
          'language.ini needs name and language')
    check(parser.has_section('files') and 0 < len(parser['files']) <= 256, 'Missing language file entries')
    targets = set()
    for target, delta in parser['files'].items():
        parts = [part.strip() for part in target.split('|')]
        check(all(LANGUAGE_TARGET.fullmatch(part) and '..' not in part.split('/') for part in parts),
              'Invalid language target')
        check('|'.join(parts).casefold() not in targets, 'Language target listed twice')
        targets.add('|'.join(parts).casefold())
        delta = delta.strip()
        check(LANGUAGE_DELTA.fullmatch(delta), 'Invalid language delta path')
        safe_relative(delta)
        path = folder.joinpath(*PurePosixPath(delta).parts)
        check(path.is_file() and not linked(path), 'Missing delta: ' + delta)
        with path.open('rb') as stream:
            check(stream.read(8) == b'DJDELTA1', 'Not a language delta: ' + delta)
    return {'capabilities': ['language-v1'], 'texture_ids': [], 'wide_ids': []}


def validate_install(folder):
    """A composed pack (fighter-body, clothing, venue; docs/content-packs.md): install.json and its deltas. The
    deltas are checked against the player's files when the launcher composes them (content_compose.py)."""
    path = folder / 'install.json'
    check(path.is_file() and not linked(path) and path.stat().st_size <= 4 * 1024**2, 'Missing/oversized install.json')
    try:
        info = json.loads(path.read_bytes(), object_pairs_hook=contract.unique_object)
        listed = content_compose.check_install(info)
    except ValueError as error:
        raise ModError('Invalid install.json: %s' % error) from error
    for relative in sorted(listed):
        safe_relative(relative)
        delta = folder.joinpath(*PurePosixPath(relative).parts)
        check(delta.is_file() and not linked(delta), 'Missing delta: ' + relative)
        with delta.open('rb') as stream:
            head = stream.read(8)
        check(len(head) == 8 and head[:4] == b'FFDL' and struct.unpack('<I', head[4:8])[0] == 1,
              'Not an FFNY delta: ' + relative)
    present = {p.relative_to(folder).as_posix() for p in folder.rglob('*.ffdelta')}
    check(present <= listed, 'Delta not listed in install.json')
    capabilities = ['content-v1']
    if any(isinstance(op, dict) and op.get('op') == 'new-venue' and 'models' in op for op in info.get('ops', [])):
        capabilities.append('venue-models-v1')      # a new venue's breakable objects (launchers from build 83)
    return {'capabilities': capabilities, 'texture_ids': [], 'wide_ids': []}


def validate_pack(folder):
    if (folder / 'install.json').exists():
        result = validate_install(folder)
        if (folder / 'manifest.ini').exists():
            # A venue's texture edits beside it, in the texture-pack format; the catalogue lists such a pack
            # with content-v1 (every launcher with content-v1 loads texture packs).
            images = validate_textures(folder)
            result = dict(images, capabilities=result['capabilities'])
        return result
    if (folder / 'language.ini').exists():
        result = validate_language(folder)
        if (folder / 'manifest.ini').exists():
            # The language mod's own images, in the texture-pack format (docs/language-packs.md).
            images = validate_textures(folder)
            result = dict(images, capabilities=result['capabilities'] + images['capabilities'])
        return result
    return validate_textures(folder)


def validate_textures(folder):
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


# Build 83: Let's Encrypt's own roots (ISRG Root X1 and X2, public certificates), shipped with the setup. Python's
# ssl reads the Windows root store as it is and never asks Windows Update for a missing root the way WinHTTP
# (the game's sign-in and updater) does, so on a PC whose store lacks ISRG Root X1 every catalogue and pack
# download failed with CERTIFICATE_VERIFY_FAILED ("certificate has expired": the old DST Root CA X3 path) and
# the PC silently kept its old packs. After such a failure the call is made again trusting only these roots, and
# every later call of this run goes straight to them. Nothing else changes: the hostname is checked as before and
# the catalogue and packs are still verified against our Ed25519 keys. FFNY Studio does the same (packsapi.py).
ROOTS_PEM = Path(__file__).resolve().parents[1] / 'config/mods/isrg-roots.pem'
_roots_only = False


def cert_failure(error):
    """A TLS certificate verification failure (urllib wraps it in URLError)."""
    import ssl
    reason = getattr(error, 'reason', error)
    return isinstance(reason, ssl.SSLCertVerificationError) or 'CERTIFICATE_VERIFY_FAILED' in str(reason)


def _opener(roots=False):
    import ssl
    handlers = [SafeRedirect()]
    if roots:
        handlers.append(urllib.request.HTTPSHandler(context=ssl.create_default_context(cafile=str(ROOTS_PEM))))
    return urllib.request.build_opener(*handlers)


def open_url(url, headers=None):
    global _roots_only
    contract.https(url)
    request = urllib.request.Request(url, headers={'User-Agent': 'DefJamMods/1', 'Accept-Encoding': 'identity',
                                                   **(headers or {})})
    if _roots_only and ROOTS_PEM.is_file():
        return _opener(roots=True).open(request, timeout=20)
    try:
        return _opener().open(request, timeout=20)
    except urllib.error.HTTPError:
        raise
    except (urllib.error.URLError, OSError) as error:
        if not cert_failure(error) or not ROOTS_PEM.is_file():
            raise
        response = _opener(roots=True).open(request, timeout=20)
        _roots_only = True
        print(json.dumps(dict(event='log', message='TLS: the Windows root store lacks the server\'s root; '
                              'using the shipped ISRG roots')), file=sys.stderr, flush=True)
        return response


# Build 83: the server's clock (its HTTP Date) from the last catalogue fetch, so a sync refused for "issued in the
# future" or "expired" can say how far off the PC's clock is (None: not known).
LAST_SERVER_DATE = None


def fetch_small(url, limit):
    global LAST_SERVER_DATE
    with open_url(url) as response:
        check(response.status == 200, 'Unexpected catalogue response')
        try:
            import email.utils
            LAST_SERVER_DATE = email.utils.parsedate_to_datetime(response.headers.get('Date'))
        except (TypeError, ValueError, IndexError):
            LAST_SERVER_DATE = None
        data = response.read(limit + 1)
        check(len(data) <= limit, 'Catalogue response too large')
        return data


FALLBACK = {CATALOGUE_URL: CATALOGUE_URL_V4, CATALOGUE_URL_V4: CATALOGUE_URL_V3}


def fetch_index(url):
    """(raw, signature, url) of a catalogue index. An index the server does not have yet (404) falls back one step:
    index-v5.json (an account service from before build 86) to index-v4.json, the same catalogue without official
    maps; index-v4.json (before build 85) to index-v3.json, without fighters at low places. Any other failure, and
    any other index (a local test catalogue), stays a failure."""
    while True:
        try:
            return fetch_small(url, MAX_INDEX), fetch_small(url + '.sig', 64), url
        except urllib.error.HTTPError as error:
            if error.code != 404 or url not in FALLBACK:
                raise
            url = FALLBACK[url]


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


def clock_skew(now=None, server=None):
    """Seconds the PC's clock is ahead of the server's (negative: behind), None when the server's is not known."""
    server = LAST_SERVER_DATE if server is None else server
    if server is None:
        return None
    if server.tzinfo is None:
        server = server.replace(tzinfo=dt.timezone.utc)
    now = now or dt.datetime.now(dt.timezone.utc)
    return (now - server).total_seconds()


def plain_reason(problems, skew=None):
    """Build 83: (kind, text) for a failed sync or check, in words a player can act on. kind: 'clock' (the PC's
    clock is off: the catalogue looks issued in the future or expired), 'network' (no answer from the server),
    'disk' (no room or no access on this PC), 'catalogue' (another catalogue fault), 'pack' (one pack failed)."""
    text = '; '.join(str(p) for p in problems)
    low = text.lower()
    if 'issued in the future' in low or 'catalogue expired' in low:
        if skew is not None and abs(skew) >= 300:
            hours = abs(skew) / 3600
            amount = '%.0f hours' % hours if hours >= 2 else '%.0f minutes' % (abs(skew) / 60)
            return 'clock', 'the PC clock is %s %s' % (amount, 'ahead' if skew > 0 else 'behind')
        if 'issued in the future' in low:
            return 'clock', 'the PC clock is behind (the catalogue looks issued in the future)'
        return 'clock', 'the catalogue looks expired: the PC clock may be ahead'
    if 'certificate_verify_failed' in low or 'certificate verify failed' in low:
        # still refused with the shipped ISRG roots (open_url): most often the PC's clock, else a program that
        # inspects HTTPS (antivirus, VPN, a company proxy)
        return 'clock', ('the secure connection to the content server was refused (certificate %s): check that the '
                         'PC date and time are right, or an antivirus/VPN scanning HTTPS' %
                         ('expired' if 'expired' in low else 'not trusted'))
    if any(w in low for w in ('no space', 'disk full', 'errno 28', 'permission denied', 'access is denied',
                              'winerror 5', 'errno 13', 'winerror 112')):
        return 'disk', text[:300]
    if any(w in low for w in ('urlopen error', 'timed out', 'getaddrinfo', 'connection', 'unreachable',
                              'name or service', 'http error', 'unexpected catalogue response', 'ssl')):
        inner = re.sub(r'^(Catalogue:\s*)+', '', text)
        inner = re.sub(r'<urlopen error (.*)>', r'\1', inner)
        inner = re.sub(r'\[(WinError|Errno) -?\d+\]\s*', '', inner).strip()
        return 'network', 'no connection to the content server (%s)' % inner[:160]
    if text.startswith('Catalogue') or 'catalogue' in low.split(':', 1)[0]:
        return 'catalogue', text[:300]
    return 'pack', text[:300]


class Manager:
    def __init__(self, data, runtime='0.0.0', capabilities=('textures-v1',), keys=None, cancel=None, prerelease=False,
                 game_dir=None):
        self.data = Path(data).resolve()
        # The player's dump: --game-dir, else <data>/extracted, else <cwd>/game.
        if game_dir:
            self.game = Path(game_dir)
        elif (self.data / 'extracted').is_dir():
            self.game = self.data / 'extracted'
        else:
            self.game = Path.cwd() / 'game'
        safe_dir(self.data)
        self.root = safe_dir(self.data / 'mod-manager')
        self.mods = safe_dir(self.data / 'mods')
        self.cache = safe_dir(self.root / 'downloads')
        self.staging = safe_dir(self.root / 'staging')
        self.backups = safe_dir(self.root / 'rollback')
        self.runtime, self.capabilities = runtime, set(capabilities)
        if keys is None and local_catalogue_url() and os.environ.get('FFNY_CONTENT_TEST_KEYS'):
            keys = read_json(os.environ['FFNY_CONTENT_TEST_KEYS'], {}).get('keys', [])
        key_config = read_json(KEYS, {}) if keys is None else {'keys': keys}
        self.keys = key_config.get('keys', [])
        self.cancel_file = Path(cancel) if cancel else None
        self.message = ''
        self.prerelease = prerelease
        self.newer = {}         # build 83: raw sync problems -> plain words (a pack only a newer build takes)
        self.notes = []         # build 84: what a sync did besides installing (an unowned folder adopted or moved aside)

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
        raw, signature, url = fetch_index(url)
        previous = read_json(self.root / 'catalogue.json', {})
        # index.json, index-v2.json and index-v3.json share their revisions but not their bytes: the same revision
        # from another file is no change without a new revision (an older revision is still a rollback)
        same_file = previous.get('url', CATALOGUE_URL_V1) == url
        index = verify_index(raw, signature, self.keys, previous.get('revision', 0),
                             previous.get('sha256') if same_file else None, fresh=True)
        atomic_json(self.root / 'catalogue.json', dict(raw=base64.b64encode(raw).decode(),
                    signature=base64.b64encode(signature).decode(), revision=index['revision'],
                    sha256=hashlib.sha256(raw).hexdigest(), fetched=dt.datetime.now(dt.timezone.utc).isoformat(),
                    url=url))
        self.progress('Catalogue verified and refreshed')

    def choose(self, mod_id, requested=None, prerelease=None, index=None):
        prerelease = self.prerelease if prerelease is None else prerelease
        index = index or self.trusted(fresh=True)
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
                check(not target.exists() and not linked(target),
                      'A local folder already uses this mod ID; it will not be overwritten (%s)' % target)
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

    def install(self, mod_id, requested=None, prerelease=None, archive_path=None, synced=False):
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
            if synced:
                # FFNY Online's catalogue: the signed content hash must be what was unpacked, and the
                # pack's own signature must check out with the catalogue key (the index already covers
                # both; this keeps a pack's identity checkable on its own).
                if 'content_sha256' in version:
                    check(content_hash(receipt['files']) == version['content_sha256'], 'Pack content does not match its signed hash')
                if 'pack_signature' in version:
                    check(pack_signature_ok(self.keys, mod_id, version, mod.get('kind', 'replacement')),
                          'Pack signature is invalid')
                receipt.update(synced=True, kind=mod.get('kind', 'replacement'), type=mod.get('type', ''),
                               order=mod.get('order', 0), language=mod.get('language', ''),
                               authors=', '.join(mod['authors']), description=mod['description'][:500],
                               content_sha256=version.get('content_sha256', content_hash(receipt['files'])),
                               bytes=version['archive']['unpacked_size'])
                if 'fighter_fe_id' in mod:
                    receipt.update(fighter_fe_id=mod['fighter_fe_id'])
                if mod.get('fighter_mode') == 'skin':
                    receipt.update(fighter_mode='skin', skin_of=mod['skin_of'])
                if mod.get('kind') == OFFICIAL_KIND:
                    # build 86: an official map (always on; content.ini official=1, rts_map, venue_fe_id)
                    receipt.update(official=True, rts_map=bool(mod.get('rts_map')), venue_fe_id=mod['venue_fe_id'])
                if 'bundle' in mod:
                    # one mod of a pack of several: the pack is installed and removed as one unit; its mods at
                    # this catalogue revision tell an interrupted sync's half pack (drop_broken_bundles)
                    items = sorted(m['id'] for m in self.trusted()['mods'] if m.get('bundle') == mod['bundle'])
                    receipt.update(bundle=mod['bundle'], bundle_name=mod['bundle_name'], bundle_items=items)
            self.cancelled()
            if synced and self.take_unowned(mod_id, receipt):
                self.progress('Installed ' + mod['name'])
                return
            self.progress('Installing verified package')
            self.commit(mod_id, stage, receipt)
            self.progress(('Installed ' + mod['name']) if synced else
                          'Installed; enable this pack in the Installed mods table, then set its priority in Load Order')
        finally:
            if stage.exists():
                shutil.rmtree(stage)

    def take_unowned(self, mod_id, receipt):
        """Build 84, a catalogue pack's folder <data>/mods/<id> is there but state.json does not own it (a copy by
        hand, or a sync cut short, e.g. by the certificate failures before build 83), which used to block the pack
        for good ("A local folder already uses this mod ID"):
          (a) its files are exactly the signed package's: adopted into state.json as installed (True);
          (c) it has local.json (the player's own pack from FFNY Studio): never touched; refused with its path;
          (b) anything else: moved aside, never deleted, to <data>/mod-manager/replaced/<id>-<UTC time>, and the
              signed package is installed as usual (False). The move is logged and named on the Content page."""
        target = self.mods / mod_id
        if mod_id in self.state()['installed'] or not (target.exists() or linked(target)):
            return False
        check(not linked(target), 'the folder %s is a link, not a pack folder; remove the link to get this pack' % target)
        check(not target.is_file(), 'a file %s is in the way of this pack; move it away to get the pack' % target)
        check(not (target / LOCAL_FILE).exists(),
              'your own pack in %s uses the same id as this community pack; it is kept (give it another id to get both)'
              % target)
        with file_lock(self.root / 'runtime.lock'):
            if self.unchanged(target, receipt):
                state = self.state()
                state['installed'][mod_id] = receipt
                json_bytes(state)
                atomic_json(self.root / 'state.json', state)
                self.note('%s: the folder %s already held this exact pack; taken over as installed' % (mod_id, target))
                return True
            replaced = safe_dir(self.root / 'replaced')
            aside = replaced / ('%s-%s' % (mod_id, dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%SZ')))
            check(not aside.exists(), 'Unsafe replaced folder')
            durable_rename(target, aside)
            self.note('%s: an old folder %s that was not the signed pack was moved to %s' % (mod_id, target, aside))
        return False

    def note(self, text):
        """Build 84: one line of what a sync did besides installing, for the Content page and the launcher log."""
        self.notes.append(text)
        print(json.dumps(dict(event='log', message=text)), file=sys.stderr, flush=True)

    def bundle_members(self, mod_id):
        """The installed synced mods of the pack ``mod_id`` names (the pack's id or one of its mods), else []."""
        installed = self.state()['installed']
        bundle = installed[mod_id].get('bundle') if mod_id in installed else mod_id
        if not bundle:
            return []
        return sorted(i for i, r in installed.items() if r.get('synced') and r.get('bundle') == bundle)

    def remove(self, mod_id):
        """Remove a managed mod; a pack of several mods (its id or one of its mods' ids) goes whole."""
        installed = self.state()['installed']
        members = [] if mod_id in installed and not installed[mod_id].get('synced') else self.bundle_members(mod_id)
        check(mod_id in installed or members, 'Only managed mods can be removed here')
        synced = bool(members) or installed[mod_id].get('synced')
        for item in members or [mod_id]:
            self.commit(item, None, None)
        if synced:
            # a community pack: content.ini and the composed files follow at once (as a sync that drops it),
            # so the game neither lists a missing pack nor serves files built with it
            self.write_content(self.content_revision())
            problems = []
            self.compose_quietly(problems)
            for problem in problems:
                self.progress(problem)
        self.progress('Removed managed mod; local folders were preserved')

    def content_revision(self):
        """The catalogue revision content.ini was last written for (0 when there is none)."""
        c = configparser.ConfigParser(interpolation=None)
        try:
            c.read(self.root / 'content.ini', encoding='utf-8')
            return int(c.get('content', 'revision', fallback='0'))
        except (OSError, ValueError, configparser.Error):
            return 0

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

    def sync(self, url=None):
        """FFNY Online: every pack of the verified catalogue, installed for everyone in catalogue order.
        Only packs whose version or archive changed are downloaded; synced packs the catalogue no longer lists
        (or revoked) are removed. A pack that fails is left as it was and the others go on. Writes
        content.ini for the game and the launcher's Content page, and (build 83) sync-result.ini."""
        url = url or local_catalogue_url() or CATALOGUE_URL
        problems = []
        plain = self.newer = {}     # build 83: the Content page's words for a raw problem (sync-result.ini keeps it raw)
        try:
            self.refresh(url)
        except (ValueError, OSError, urllib.error.URLError) as error:
            problems.append('Catalogue: ' + str(error))
            if 'certificate' in str(error).lower():
                plain[problems[-1]] = plain_reason(problems[-1:], clock_skew())[1]
            elif isinstance(error, OSError):        # urllib.error.URLError too; not a signature or format fault
                plain[problems[-1]] = ('the FFNY server could not be reached (%s); the packs you have stay as they are'
                                       % plain_network_error(error))
        try:
            index = self.trusted(fresh=True)
        except (ValueError, OSError) as error:
            self.write_content()
            self.compose_quietly(problems)      # the player's own packs work without a catalogue too
            reason = '; '.join(problems) or str(error)
            self.write_sync_result('failed', [reason])
            raise ModError('; '.join(plain.get(p, p) for p in problems) or str(error)) from error
        try:
            done = self.sync_packs(index, problems)
        except ModError:
            # PLAY cancelled the downloads: the game still gets files that match the packs on disk (and never
            # half of a pack of several mods)
            self.drop_broken_bundles(problems)
            self.write_content(index['revision'])
            self.compose_quietly(problems)
            self.write_sync_result('cancelled', problems, index['revision'])
            raise
        self.write_content(index['revision'])
        self.compose_quietly(problems)
        self.write_sync_result('failed' if problems else 'ok', problems, index['revision'])
        self.settle_news(index['revision'])
        count = sum(1 for r in self.state()['installed'].values() if r.get('synced'))
        message = 'Community content up to date: %d pack(s)' % count if not done else \
            'Community content synced: %d change(s), %d pack(s) installed' % (done, count)
        if problems:
            message += '. Not synced: ' + '; '.join(plain.get(p, p) for p in problems)
        if self.notes:
            message += '. ' + '; '.join(self.notes)
        self.progress(message)

    def sync_packs(self, index, problems):
        listed, done = set(), 0
        ordered = sorted(index['mods'], key=lambda m: (m.get('order', 1 << 30), m['id']))
        wanted, reasons = {}, {}        # the catalogue's version of each mod; why a pack's mod did not install
        for mod in ordered:
            listed.add(mod['id'])
            installed = self.state()['installed'].get(mod['id'])
            try:
                _, version = self.choose(mod['id'])
                wanted[mod['id']] = version
                if installed and not installed.get('synced'):
                    raise ModError('a pack installed by hand uses this id; it is kept')
                if (installed and installed.get('version') == version['version']
                        and installed.get('archive_sha256') == version['archive']['sha256']
                        and installed.get('fighter_fe_id') == mod.get('fighter_fe_id')
                        and installed.get('fighter_mode') == mod.get('fighter_mode')
                        and installed.get('skin_of') == mod.get('skin_of')
                        and installed.get('bundle') == mod.get('bundle')
                        and installed.get('bundle_name') == mod.get('bundle_name')):
                    continue
                self.progress('Downloading ' + mod['name'])
                self.install(mod['id'], version['version'], synced=True)
                done += 1
            except (ValueError, OSError, KeyError, zipfile.BadZipFile, urllib.error.URLError) as error:
                if isinstance(error, ModError) and 'cancelled' in str(error):
                    raise
                if mod.get('bundle'):
                    reasons[mod['id']] = '%s: %s' % (mod['name'], error)     # reported for its pack below
                else:
                    problems.append('%s: %s' % (mod['id'], error))
                    if self.needs_newer(mod):     # build 83: the Content page names it in plain words
                        self.newer[problems[-1]] = (mod.get('name') or mod['id']) + NEEDS_NEWER
        done += self.sync_bundles(ordered, wanted, reasons, problems)
        for mod_id, receipt in list(self.state()['installed'].items()):
            mod = next((m for m in index['mods'] if m['id'] == mod_id), None)
            if receipt.get('synced') and (mod is None or receipt.get('version') in mod['revoked_versions']):
                try:
                    self.commit(mod_id, None, None)
                    done += 1
                except (ValueError, OSError) as error:
                    problems.append('%s: %s' % (mod_id, error))
        return done

    def needs_newer(self, mod):
        """Whether a catalogue mod has a stable, unrevoked version that only a newer game or launcher could take
        (a higher min_recomp or a capability this launcher does not offer, as voice-v1 before build 83): the
        Content page then says so in plain words instead of 'No compatible, nonrevoked version'."""
        versions = [v for v in mod.get('versions', []) if v.get('version') not in mod.get('revoked_versions', [])
                    and (self.prerelease or v.get('channel') == 'stable')]
        return bool(versions) and not any(semver(v['min_recomp']) <= semver(self.runtime)
                                          and set(v['capabilities']) <= self.capabilities for v in versions)

    def sync_bundles(self, ordered, wanted, reasons, problems):
        """A pack of several mods is one unit: unless every mod of it is installed at the catalogue's version,
        none of them stays. Returns the count of mods removed."""
        bundles, done = {}, 0
        for mod in ordered:
            if mod.get('bundle'):
                bundles.setdefault(mod['bundle'], []).append(mod)
        for bundle, items in bundles.items():
            reason = None
            for mod in items:
                receipt = self.state()['installed'].get(mod['id'])
                version = wanted.get(mod['id'])
                if mod['id'] in reasons or not (receipt and receipt.get('synced') and version
                                                 and receipt.get('version') == version['version']
                                                 and receipt.get('archive_sha256') == version['archive']['sha256']):
                    reason = reasons.get(mod['id'], '%s: not installed' % mod['name'])
                    break
            if reason is None:
                continue
            for item in self.bundle_members(bundle):
                try:
                    self.commit(item, None, None)
                    done += 1
                except (ValueError, OSError) as error:
                    problems.append('%s: %s' % (item, error))
            problems.append('%s: not installed (%s)' % (items[0]['bundle_name'], reason))
        return done

    def drop_broken_bundles(self, problems):
        """A sync stopped half way: remove every pack of several mods whose installed mods are not one whole
        version of it, all its mods (the next sync installs it again)."""
        groups = {}
        for mod_id, r in self.state()['installed'].items():
            if r.get('synced') and r.get('bundle'):
                groups.setdefault(r['bundle'], []).append((mod_id, r))
        for bundle, rows in sorted(groups.items()):
            ids = sorted(i for i, _ in rows)
            if len({r.get('version') for _, r in rows}) == 1 and all(r.get('bundle_items') == ids for _, r in rows):
                continue
            for mod_id in ids:
                try:
                    self.commit(mod_id, None, None)
                except (ValueError, OSError) as error:
                    problems.append('%s: %s' % (mod_id, error))
            problems.append('%s: not installed (the sync stopped before all its mods were in)' %
                            rows[0][1].get('bundle_name', bundle))

    def composed_packs(self):
        """Every synced pack with an install.json, for content_compose."""
        out = []
        reviewed = self.reviewed_dates()
        for mod_id, r in self.state()['installed'].items():
            contract.slug(mod_id)
            folder = self.mods / mod_id
            if r.get('synced') and not linked(folder) and (folder / 'install.json').is_file():
                # reviewed: the day the team accepted this version (a new fighter's thumbs carry NEW for a while)
                pack = dict(id=mod_id, version=r.get('version', ''), content_sha256=r.get('content_sha256', ''),
                            kind=r.get('kind', 'replacement'), order=r.get('order', 0), folder=folder,
                            fighter_fe_id=r.get('fighter_fe_id', 0),
                            fighter_mode=r.get('fighter_mode', ''), skin_of=r.get('skin_of'),
                            reviewed=reviewed.get((mod_id, r.get('version')), ''))
                if r.get('bundle'):
                    # content_compose applies a pack of several mods whole or not at all
                    pack.update(bundle=r['bundle'], bundle_name=r.get('bundle_name', r['bundle']))
                out.append(pack)
        return out

    def local_packs(self):
        """The player's own packs (FFNY Studio's "Use in my game"): every folder <data>/mods/<id> with a
        local.json that the mod manager does not own, by id. They apply after the community packs, can be switched
        off like a replacement (settings.ini [content] off), never count in the online content print, and those
        that are not online-safe (local_online_safe) keep the game offline while they are on."""
        installed = self.state()['installed']
        out = []
        for folder in sorted(self.mods.iterdir(), key=lambda p: p.name):
            if not folder.is_dir() or linked(folder) or folder.name in installed:
                continue
            try:
                contract.slug(folder.name)
                info = read_json(folder / LOCAL_FILE, None, 64 * 1024)
            except (ValueError, OSError):
                continue
            if not isinstance(info, dict):
                continue
            files = {}
            for path in sorted(folder.rglob('*')):
                if path.is_file() and not linked(path) and path.name != LOCAL_FILE:
                    files[path.relative_to(folder).as_posix()] = sha(path)
            text = lambda key, limit=80: str(info.get(key, '')).replace('\n', ' ').replace('\r', ' ')[:limit]
            place = info.get('fighter_fe_id', 0)
            # an official map project tried in the player's own game is a content pack there (offline, local)
            kind = 'content' if text('kind') == OFFICIAL_KIND else text('kind') or 'replacement'
            out.append(dict(id=folder.name, name=text('name') or folder.name, kind=kind,
                            type=text('type'), version=text('version') or '0.0.0', authors=text('author'),
                            description=text('description', 500), content_sha256=content_hash(files),
                            order=LOCAL_ORDER + len(out), folder=folder, local=True,
                            fighter_fe_id=place if isinstance(place, int) and not isinstance(place, bool) else 0,
                            composed=(folder / 'install.json').is_file(), online_ok=local_online_safe(folder)))
        return out

    def catalogue_places(self):
        """The new-fighter places of the cached signed catalogue (a live pack this PC has not synced yet holds its
        place too); empty when there is none or it does not verify."""
        try:
            return {m['fighter_fe_id'] for m in self.trusted()['mods'] if isinstance(m.get('fighter_fe_id'), int)}
        except (ValueError, OSError, KeyError, TypeError):
            return set()

    def compose(self):
        """Compose the fighter-body, clothing and venue packs into <data>/mod-manager/content-files (the game
        serves them). Holds the runtime lock, so never while the game runs. A player's own new fighter whose place
        a community fighter has now moves to a free one (content_compose.settle_places); its local.json keeps the
        new place, so the next compose and the Content page agree."""
        with file_lock(self.root / 'runtime.lock'):
            self.progress('Preparing community content')
            off = content_compose.read_off(self.data)
            packs = self.composed_packs() + [p for p in self.local_packs() if p['composed'] and p['id'] not in off]
            moved = []
            result = content_compose.compose(self.data, self.game, packs, root=self.root, log=moved.append,
                                             reserved=self.catalogue_places())
            if any([self.keep_local_place(p) for p in packs if p.get('local')]):
                self.write_content(self.content_revision())     # the Content page and the game: the new place
            for line in moved:
                if 'moved from place' in line or 'left out' in line:
                    self.progress(line.replace('[compose] ', ''))
        skipped = '; '.join('%s: %s' % s for s in result['skipped'] + result.get('lang_skipped', []))
        languages = sorted({i for i, _ in result.get('lang_files', [])})
        with_lang = ' (also with %s)' % ', '.join(languages) if languages else ''
        if result['unchanged']:
            text = 'Community content ready: %d pack(s) applied%s' % (len(result['applied']), with_lang)
        else:
            text = 'Community content prepared: %d pack(s) applied, %d file(s)%s' % (len(result['applied']),
                                                                                    len(result['files']), with_lang)
        self.progress(text + ('. Not applied: ' + skipped if skipped else ''))
        return result

    def keep_local_place(self, p):
        """Write a player's own pack's (new) place into its local.json when compose moved it; True when it did."""
        path = Path(p['folder']) / LOCAL_FILE
        try:
            info = read_json(path, None, 64 * 1024)
        except (ValueError, OSError):
            return False
        place = p.get('fighter_fe_id') or 0
        if isinstance(info, dict) and place and info.get('fighter_fe_id') != place and not linked(path):
            atomic_json(path, dict(info, fighter_fe_id=place))
            return True
        return False

    def compose_quietly(self, problems):
        try:
            result = self.compose()
            problems.extend('%s: %s' % s for s in result['skipped'])
        except (ValueError, OSError) as error:
            problems.append('Composing: %s' % error)

    def write_content(self, revision=0):
        """<data>/mod-manager/content.ini: the synced packs for the game (pc_content.c) and the Content page."""
        lines = ['; FFNY Online community packs, written by setup/mods.py sync. Do not edit.',
                 '[content]', 'revision=%d' % revision]
        reviewed = self.reviewed_dates()
        rows = [(r.get('order', 0), mod_id, r) for mod_id, r in self.state()['installed'].items() if r.get('synced')]
        for order, mod_id, r in sorted(rows, key=lambda x: (x[0], x[1])):
            contract.slug(mod_id)
            lines.append('[pack.' + mod_id + ']')
            for key in ('name', 'authors', 'kind', 'type', 'version', 'language', 'content_sha256', 'order', 'bytes',
                        'description', 'fighter_fe_id', 'bundle', 'bundle_name'):
                value = str(r.get(key, '')).replace('\n', ' ').replace('\r', ' ')[:500]
                if key == 'kind' and value == OFFICIAL_KIND:
                    value = 'content'
                lines.append(key + '=' + value)
            if r.get('fighter_mode') == 'skin':
                # build 85: a skin, an extra outfit of the stock fighter skin_of (pc_content.c)
                lines += ['fighter_mode=skin', 'skin_of=%d' % r.get('skin_of', -1)]
            if r.get('official'):
                # build 86: an official map; kind= above says content (always on, in the content print), as the
                # game of build 85 reads any other kind as a replacement the player could switch off
                lines += ['official=1', 'rts_map=%d' % bool(r.get('rts_map'))]
            # build 82: the day the team accepted this version (the hub's NEW badge) and a new venue's feID
            lines.append('reviewed=' + reviewed.get((mod_id, r.get('version')), ''))
            if (self.mods / mod_id / 'install.json').is_file():
                lines.append('composed=1')     # pc_content.c counts it online only when content-files applied it
                venue = new_venue_fe_id(self.mods / mod_id / 'install.json')
                if venue:
                    lines.append('venue_fe_id=%d' % venue)
                    code = new_venue_code(self.mods / mod_id / 'install.json')
                    if code and r.get('official'):
                        lines.append('venue_code=' + code)      # build 86: the official map's venue, e.g. 28A
                    if r.get('official') and (self.mods / mod_id / 'bigarena.json').is_file():
                        # its Big Arena layout (size, spawn points; Studio's mapbasics.bigarena_info) in the pack
                        # folder <data>/mods/<id>/bigarena.json, the same bytes on every PC (in the content hash)
                        lines.append('bigarena=1')
                template = new_fighter_template_fe_id(self.mods / mod_id / 'install.json')
                if template:
                    # build 83: the Content page says where the new fighter is ("in D-Mob's spot")
                    lines += ['template_fe_id=%d' % template, 'template_name=' + self.fighter_name(template)]
        for p in self.local_packs():
            # the player's own packs (FFNY Studio, "Use in my game"): local=1, never in the online content print;
            # online_ok=0 keeps the game offline while the pack is on (pc_content.c)
            lines.append('[pack.' + p['id'] + ']')
            for key in ('name', 'authors', 'kind', 'type', 'version', 'content_sha256', 'order', 'description',
                        'fighter_fe_id'):
                lines.append(key + '=' + str(p[key]))
            lines += ['local=1', 'online_ok=%d' % p['online_ok']]
            if p['composed']:
                lines.append('composed=1')
                venue = new_venue_fe_id(p['folder'] / 'install.json')
                if venue:
                    lines.append('venue_fe_id=%d' % venue)
                template = new_fighter_template_fe_id(p['folder'] / 'install.json')
                if template:
                    lines += ['template_fe_id=%d' % template, 'template_name=' + self.fighter_name(template)]
        atomic_bytes(self.root / 'content.ini', ('\n'.join(lines) + '\n').encode('utf-8'))

    def fighter_name(self, fe_id):
        """A stock fighter's name as the select screen shows it (the player's djv2.sod Character rows), '' when
        the dump is not at hand."""
        if not hasattr(self, '_fighter_names'):
            self._fighter_names = {}
            try:
                from ffnycontent import newfighter
                rows = newfighter.characters((self.game / 'assets' / 'djv2.sod').read_bytes())
                self._fighter_names = {r['fe_id']: r['display'] for r in rows}
            except (OSError, ValueError, KeyError, ImportError):
                pass
        return re.sub(r'[\r\n]', ' ', str(self._fighter_names.get(fe_id, '')))[:40]

    def write_sync_result(self, result, problems, revision=0):
        """Build 83: <data>/mod-manager/sync-result.ini, the last sync's outcome for the game (pc_content.c logs it
        at the start, the F9 report carries it, the online hub tells the player when it failed):

            [sync]
            time=<UTC>
            result=ok | failed | cancelled
            kind=<plain_reason's kind, "" when ok>
            reason=<plain words, "" when ok>
            detail=<the raw problems>
            revision=<the catalogue revision content.ini was written for, 0 none>
            packs=<synced packs installed>
            ids=<their ids, comma-separated>
            skipped=<packs this launcher cannot take yet (a newer build or capability), their ids>

        A pack this launcher cannot take (choose's "No compatible" refusal) is skipped=, not a failure: the game's
        news check leaves it out the same way. Never raises: a sync's own outcome must not be hidden by this file."""
        try:
            skipped = [p for p in problems if 'No compatible, nonrevoked version' in str(p)]
            problems = [p for p in problems if p not in skipped]
            if result == 'failed' and not problems:
                result = 'ok'
            kind, reason = plain_reason(problems, clock_skew()) if problems else ('', '')
            ids = sorted(i for i, r in self.state()['installed'].items() if r.get('synced'))
            clean = lambda v: str(v).replace('\n', ' ').replace('\r', ' ')
            atomic_bytes(self.root / 'sync-result.ini', (
                '; the last community content sync, written by setup/mods.py sync. Do not edit.\n[sync]\n'
                'time=%s\nresult=%s\nkind=%s\nreason=%s\ndetail=%s\nskipped=%s\nrevision=%d\npacks=%d\nids=%s\n'
                'notes=%s\n' % (
                    dt.datetime.now(dt.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'), result, kind, clean(reason)[:300],
                    clean('; '.join(str(p) for p in problems))[:1000],
                    clean(', '.join(str(p).split(':', 1)[0] for p in skipped))[:500], revision, len(ids),
                    clean(','.join(ids))[:1000], clean('; '.join(getattr(self, 'notes', [])))[:1000])
            ).encode('utf-8'))
        except (ValueError, OSError, KeyError, TypeError):
            pass

    def reviewed_dates(self):
        """{(mod id, version): the catalogue's review date} from the cached verified catalogue ({} without one)."""
        try:
            index = self.trusted()
        except (ValueError, OSError, KeyError, TypeError):
            return {}
        return {(m['id'], v['version']): v.get('reviewed', '') for m in index['mods'] for v in m['versions']}

    def check_news(self, url=None):
        """FFNY Online, from the running game (the online hub): would a sync now change the community packs? Fetches
        and verifies the catalogue like refresh, but writes only <data>/mod-manager/news.ini (never the cached
        catalogue, the state or the packs):

            [news]
            checked=<UTC time>
            revision=<the catalogue's revision>
            pending=<mods a sync would install, update or remove; 0 when the last sync already saw this revision>
            names=<their names, ' | ' between>

        The last sync's revision (content.ini) counts as handled: a pack that failed there is not news again."""
        url = url or local_catalogue_url() or CATALOGUE_URL
        check(self.keys, 'Catalogue signing key has not been provisioned')
        raw, signature, url = fetch_index(url)
        previous = read_json(self.root / 'catalogue.json', {})
        index = verify_index(raw, signature, self.keys, previous.get('revision', 0), None, fresh=True)
        names = []
        if index['revision'] > self.content_revision():
            installed = self.state()['installed']
            for mod in sorted(index['mods'], key=lambda m: (m.get('order', 1 << 30), m['id'])):
                receipt = installed.get(mod['id'])
                if receipt and not receipt.get('synced'):
                    continue        # a pack installed by hand keeps its id; sync leaves it
                try:
                    _, version = self.choose(mod['id'], index=index)
                except ValueError:
                    continue        # not for this launcher (capabilities, runtime): sync skips it too
                if not receipt or receipt.get('version') != version['version'] or \
                        receipt.get('archive_sha256') != version['archive']['sha256']:
                    names.append(mod['name'])
            for mod_id, receipt in installed.items():
                mod = next((m for m in index['mods'] if m['id'] == mod_id), None)
                if receipt.get('synced') and (mod is None or receipt.get('version') in mod['revoked_versions']):
                    names.append(receipt.get('name', mod_id))
        clean = ' | '.join(n.replace('\n', ' ').replace('\r', ' ')[:80] for n in names)[:1000]
        atomic_bytes(self.root / 'news.ini', ('[news]\nchecked=%s\nrevision=%d\npending=%d\nnames=%s\n' % (
            dt.datetime.now(dt.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'), index['revision'], len(names), clean)
        ).encode('utf-8'))
        self.message = '%d community pack change(s) waiting' % len(names) if names else 'Community content up to date'
        return names

    def settle_news(self, revision):
        """Build 84: a finished sync for catalogue ``revision`` answers the news a check found up to that revision:
        news.ini then says pending=0 (the game also ignores a news.ini not newer than content.ini)."""
        old = configparser.ConfigParser(interpolation=None)
        try:
            old.read(self.root / 'news.ini', encoding='utf-8')
            if not old.has_section('news') or int(old.get('news', 'revision', fallback='0')) > revision:
                return
            atomic_bytes(self.root / 'news.ini', ('[news]\nchecked=%s\nrevision=%d\npending=0\nnames=\n' % (
                old.get('news', 'checked', fallback=''), revision)).encode('utf-8'))
        except (OSError, ValueError, configparser.Error):
            pass

    def write_news_failure(self, error):
        """Build 83: a check that failed says so in news.ini (error= and kind=, plain words), keeping the last good
        check's revision, pending and names, so the hub shows it and the F9 report carries it."""
        old = configparser.ConfigParser(interpolation=None)
        try:
            old.read(self.root / 'news.ini', encoding='utf-8')
        except (OSError, configparser.Error):
            pass
        get = lambda key, default: old.get('news', key, fallback=default) if old.has_section('news') else default
        kind, reason = plain_reason([error], clock_skew())
        clean = lambda v: str(v).replace('\n', ' ').replace('\r', ' ')
        try:
            atomic_bytes(self.root / 'news.ini', (
                '[news]\nchecked=%s\nrevision=%s\npending=%s\nnames=%s\nkind=%s\nerror=%s\n' % (
                    dt.datetime.now(dt.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'), clean(get('revision', '0'))[:12],
                    clean(get('pending', '0'))[:12], clean(get('names', ''))[:1000], kind, clean(reason)[:300])
            ).encode('utf-8'))
        except OSError:
            pass

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
                    load_after=','.join(latest['load_after']) if latest else '',
                    bundle=mod.get('bundle', ''), bundle_name=mod.get('bundle_name', '')))
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
                    version=receipt['version'], rollback=int(bool(receipt.get('previous'))),
                    bundle=receipt.get('bundle', ''), bundle_name=receipt.get('bundle_name', '')))
        atomic_bytes(self.root / 'view.ini', ('\n'.join(lines) + '\n').encode())


OFFICIAL_KIND = 'official-map'      # build 86: the FFNY team's official maps (mod_catalogue.py)


def new_venue_code(install_json):
    """A venue pack's new venue code (its new-venue op's code, e.g. 28A), else ''."""
    try:
        for op in json.loads(Path(install_json).read_text(encoding='utf-8')).get('ops') or []:
            if isinstance(op, dict) and op.get('op') == 'new-venue' and isinstance(op.get('code'), str) \
                    and re.fullmatch(r'[0-9]{2}[A-Z]', op['code']):
                return op['code']
    except (OSError, ValueError, AttributeError):
        pass
    return ''


def new_venue_fe_id(install_json):
    """A venue pack's new venue front-end ID (its new-venue op's fe_id, 28..63), else 0."""
    try:
        ops = json.loads(Path(install_json).read_text(encoding='utf-8')).get('ops') or []
        for op in ops:
            if isinstance(op, dict) and op.get('op') == 'new-venue' and isinstance(op.get('fe_id'), int)                     and 28 <= op['fe_id'] <= 63:
                return op['fe_id']
    except (OSError, ValueError, AttributeError):
        pass
    return 0


NEEDS_NEWER = ' needs a newer FFNY Online - update to get it'     # a pack only a newer build takes


def plain_network_error(error):
    """A few words for a failed download: no connection, a timeout, an HTTP status, else the OS's text."""
    reason = getattr(error, 'reason', error)
    if isinstance(error, urllib.error.HTTPError):
        return 'the server answered %d' % error.code
    if isinstance(reason, (TimeoutError, socket.timeout)) or 'timed out' in str(reason):
        return 'no answer in time'
    if isinstance(reason, (ConnectionRefusedError, ConnectionResetError)) or getattr(reason, 'winerror', 0) in (10061, 10054):
        return 'no connection'
    if isinstance(reason, socket.gaierror):
        return 'no internet connection or no name lookup'
    return 'no connection'


def new_fighter_template_fe_id(install_json):
    """A new fighter pack's template fighter (its new-fighter op's template_fe_id, fighter.xml 0..99): the new
    fighter shows as an outfit in that fighter's spot on the select screen. Else 0."""
    try:
        ops = json.loads(Path(install_json).read_text(encoding='utf-8')).get('ops') or []
        for op in ops:
            if (isinstance(op, dict) and op.get('op') == 'new-fighter' and isinstance(op.get('template_fe_id'), int)
                    and 0 <= op['template_fe_id'] < 100):
                return op['template_fe_id']
    except (OSError, ValueError, AttributeError):
        pass
    return 0


LOCAL_FILE = 'local.json'          # FFNY Studio's "Use in my game" marks the player's own pack folders with it
LOCAL_ORDER = 1 << 20              # the player's own packs apply after every community pack
_TEXTURE_ENTRY = re.compile(r'^[A-Za-z0-9_]+\.xsh$')


def local_online_safe(folder):
    """Whether one of the player's own packs may stay on online: it changes only how things look. A texture or
    language pack (no install.json: textures go through the texture loader, and a language pack that rebuilt a
    file a fight reads changes the fight-files print, which keeps different PCs apart) is; a composed pack only
    when it has no ops and every entry replaces a texture file (``*.xsh``) of the game. Models, collision,
    placements, lights, new items, venues and fighters are offline only."""
    path = Path(folder) / 'install.json'
    if not path.is_file():
        return True
    try:
        info = content_compose.read_install(folder)
    except (ValueError, OSError):
        return False
    return not info.get('ops') and all(
        e['mode'] == 'replace' and e['archive'] == e['base_archive'] and e['entry'] == e['base_entry']
        and e['archive'].lower().endswith('.viv') and _TEXTURE_ENTRY.fullmatch(e['entry'])
        for e in info.get('entries', []))


def content_hash(files):
    """The pack's content hash: SHA-256 over "path\\0sha256\\n" of every file, sorted by path (Studio's
    contentpack and the account service compute the same)."""
    digest = hashlib.sha256()
    for path in sorted(files):
        digest.update(path.encode('utf-8') + b'\0' + files[path].encode('ascii') + b'\n')
    return digest.hexdigest()


def pack_signature_ok(keys, mod_id, version, kind):
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    signed = json.dumps(dict(content_sha256=version.get('content_sha256', ''), id=mod_id, kind=kind,
                             version=version['version']), sort_keys=True, separators=(',', ':')).encode()
    for key in keys:
        try:
            Ed25519PublicKey.from_public_bytes(bytes.fromhex(key)).verify(bytes.fromhex(version['pack_signature']), signed)
            return True
        except (InvalidSignature, ValueError):
            pass
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('status', 'refresh', 'install', 'remove', 'rollback', 'recover', 'sync',
                                            'compose', 'check'))
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--game-dir', type=Path, help="the player's dump (default <data>/extracted, else ./game)")
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
        manager = Manager(args.data_dir, args.runtime, args.capability, cancel=args.cancel_file, prerelease=args.prerelease,
                          game_dir=args.game_dir)
        if args.command == 'check':
            # from the running game: no writer lock (a launcher's sync may hold it), only news.ini is written
            try:
                manager.check_news()
            except (ValueError, OSError, KeyError, TypeError, ImportError, urllib.error.URLError) as error:
                manager.write_news_failure(error)
                return failure(error)
            print(json.dumps(dict(event='complete', message=manager.message)), flush=True)
            return 0
        with file_lock(manager.root / 'writer.lock'):
            try:
                if (manager.root / 'journal.json').exists():
                    with file_lock(manager.root / 'runtime.lock'):
                        manager.recover()
                if args.command == 'refresh':
                    manager.refresh()
                elif args.command == 'sync':
                    manager.sync()
                elif args.command == 'compose':
                    # also the player's own packs (FFNY Studio's "Use in my game", a switch on the Content page):
                    # content.ini lists them for the game and the launcher
                    manager.write_content(manager.content_revision())
                    manager.compose()
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
