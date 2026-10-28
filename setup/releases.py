"""Public GitHub setup discovery and verified, release-specific engine handoff.

Only the approved exe/dmg/tar.gz assets are consumed. No GitHub credentials,
dump contents, save contents or personal paths are sent to the service.
"""
import contextlib
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

REPO = 'vyanhursky/DefJamFFNY-recomp'
API = 'https://api.github.com/repos/' + REPO + '/releases'
EXTENSIONS = {'windows-x64': '.exe', 'macos-arm64': '.dmg', 'linux-x64': '.tar.gz'}
CONTRACT = {'protocol': 1, 'data_schema': 1}


def default_cache():
    if os.name == 'nt':
        return Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'DefJamSetup/releases'
    if sys.platform == 'darwin':
        return Path.home() / 'Library/Caches/DefJamSetup/releases'
    return Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache')) / 'DefJamSetup/releases'


def version(value):
    if not isinstance(value, str) or not re.fullmatch(r'v?\d+\.\d+\.\d+', value):
        raise ValueError('Unsupported release version')
    return tuple(map(int, value.removeprefix('v').split('.')))


def capable(value):
    return isinstance(value, dict) and value == CONTRACT and all(type(value.get(key)) is int for key in CONTRACT)


def policy(operation, target, previous):
    if operation == 'auto':
        operation = 'install' if previous is None else 'repair' if version(target['version']) == version(previous['version']) else 'update'
    if operation == 'install':
        if previous:
            raise ValueError('This location is already installed; choose Repair, Clean Update or Downgrade')
        return operation
    if not previous:
        raise ValueError('Choose an existing installation for this operation')
    old, new = version(previous['version']), version(target['version'])
    if operation in ('repair', 'rebuild') and old != new:
        raise ValueError('Repair and Clean Rebuild require the installed version')
    if operation == 'update' and new <= old:
        raise ValueError('Clean Update requires a newer version; use Clean Rebuild for the same version')
    if operation == 'downgrade' and new >= old:
        raise ValueError('Downgrade requires an older compatible version')
    if new < old:
        if operation != 'downgrade' or not capable(target.get('updater')) or not capable(previous.get('updater')):
            raise ValueError('Downgrade to historical or incompatible releases is not supported')
    if operation in ('update', 'downgrade', 'rebuild') and not capable(target.get('updater')):
        raise ValueError('This release does not support clean updates; select an updater-capable release')
    if previous.get('updater') and not capable(previous['updater']):
        raise ValueError('Installed data needs a compatible setup protocol; no data was changed')
    return operation


class HTTPSRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        validate_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def validate_url(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != 'https' or parsed.username or parsed.password or parsed.port not in (None, 443):
        raise ValueError('Unsafe setup download URL')
    if parsed.hostname not in ('github.com', 'api.github.com', 'release-assets.githubusercontent.com', 'objects.githubusercontent.com'):
        raise ValueError('Setup download must come from GitHub release assets')


def read_url(url, limit, cancelled=lambda: None):
    validate_url(url)
    request = urllib.request.Request(url, headers={
        'Accept': 'application/vnd.github+json', 'User-Agent': 'DefJamSetup/1',
        'X-GitHub-Api-Version': '2022-11-28'})
    opener = urllib.request.build_opener(HTTPSRedirect())
    started = time.monotonic()
    for attempt in range(3):
        cancelled()
        try:
            with opener.open(request, timeout=15) as response:
                validate_url(response.url)
                data = bytearray()
                while chunk := response.read(65536):
                    cancelled()
                    if time.monotonic() - started > 60:
                        raise TimeoutError('Release metadata request exceeded one minute')
                    data.extend(chunk)
                    if len(data) > limit:
                        raise ValueError('Setup response exceeds its size limit')
                return bytes(data)
        except urllib.error.HTTPError as ex:
            if ex.code in (403, 429):
                raise ValueError('GitHub rate limit or access restriction; use the bundled release or retry later') from ex
            if ex.code not in (500, 502, 503, 504) or attempt == 2:
                raise
        except (TimeoutError, urllib.error.URLError):
            if attempt == 2:
                raise
        for _ in range(5 * (attempt + 1)):
            cancelled()
            time.sleep(.1)


def latest_tag(cancelled=lambda: None):
    result = json.loads(read_url(API + '/latest', 2 * 1024**2, cancelled))
    tag = result.get('tag_name') if isinstance(result, dict) else None
    if not tag or not re.fullmatch(r'v\d+\.\d+\.\d+', tag):
        raise ValueError('GitHub did not return a supported latest stable release')
    return tag


def catalog(platform, cache, cancelled=lambda: None, allow_cached=False):
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    releases = []
    for page in range(1, 21):
        try:
            rows = json.loads(read_url(API + '?per_page=100&page=' + str(page), 8 * 1024**2, cancelled))
        except (urllib.error.URLError, TimeoutError, OSError):
            snapshot = cache / 'releases.json'
            if not allow_cached or not snapshot.is_file() or snapshot.is_symlink() or snapshot.stat().st_size > 8 * 1024**2:
                raise
            saved = json.loads(snapshot.read_text())
            if not isinstance(saved, list) or any(not isinstance(r, dict) or not re.fullmatch(r'v\d+\.\d+\.\d+', r.get('tag', '')) for r in saved):
                raise ValueError('Invalid cached release list')
            return [dict(row, cached=True) for row in saved]
        if not isinstance(rows, list):
            raise ValueError('Invalid GitHub release response')
        releases.extend(rows)
        if len(rows) < 100:
            break
    else:
        raise ValueError('Release list exceeds the supported page limit')
    result, seen = [], set()
    for release in releases:
        tag = release.get('tag_name', '')
        if release.get('draft') or release.get('prerelease') or not re.fullmatch(r'v\d+\.\d+\.\d+', tag) or tag in seen:
            continue
        seen.add(tag)
        stem = 'DefJamSetup-' + tag[1:] + '-' + platform
        expected = (stem + EXTENSIONS[platform], stem + '.sha256', stem + '.provenance.json')
        assets = release.get('assets', [])
        selected = [next((a for a in assets if a.get('name') == name), None) for name in expected]
        available = all(a and a.get('state') == 'uploaded' and sum(x.get('name') == a['name'] for x in assets) == 1 for a in selected)
        result.append({'tag': tag, 'version': tag[1:], 'available': bool(available),
                       'reason': '' if available else 'No complete setup for this platform',
                       'notes': str(release.get('body') or '')[:20000], 'assets': selected if available else []})
    result.sort(key=lambda row: version(row['version']), reverse=True)
    # Display cache is explicit offline metadata; downloads still verify pinned asset bytes.
    fd, temporary = tempfile.mkstemp(prefix='releases-', suffix='.pending', dir=cache)
    pending = Path(temporary)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(result, stream)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            os.replace(pending, cache / 'releases.json')
        except PermissionError:
            # Another Windows discovery reader can temporarily deny replacement.
            # Online results remain valid; an older display cache can be refreshed later.
            if not (cache / 'releases.json').is_file() or (cache / 'releases.json').is_symlink():
                raise
    finally:
        pending.unlink(missing_ok=True)
    return result


def updater_status(release, cache, cancelled=lambda: None):
    """True when the release's setup can update an existing installation, False when it cannot, None when unknown."""
    try:
        path = download(release['assets'][2], cache, cancelled)
        if path.stat().st_size > 4 * 1024**2:
            return None
        provenance = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError, KeyError, IndexError, urllib.error.URLError, TimeoutError):
        return None
    return capable(provenance.get('updater')) if isinstance(provenance, dict) else False


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def download(asset, cache, cancelled=lambda: None, progress=lambda text: None, expected=None):
    url = asset['browser_download_url']
    prefix = 'https://github.com/' + REPO + '/releases/download/'
    if not url.startswith(prefix):
        raise ValueError('Unexpected release download identity')
    validate_url(url)
    digest = asset.get('digest') or ''
    checksum = digest[7:] if re.fullmatch(r'sha256:[0-9a-f]{64}', digest) else expected
    if not checksum or not re.fullmatch(r'[0-9a-f]{64}', checksum):
        raise ValueError('Release asset has no verifiable SHA-256')
    size = asset.get('size')
    if not isinstance(size, int) or size < 1 or size > 1024**3:
        raise ValueError('Invalid setup download size')
    path = Path(cache) / (checksum + '-' + str(asset['id']))
    if path.is_file() and not path.is_symlink() and path.stat().st_size == size and sha(path) == checksum:
        return path
    if path.is_symlink():
        raise ValueError('Unsafe download cache link')
    fd, temporary = tempfile.mkstemp(prefix='download-', suffix='.partial', dir=cache)
    os.close(fd)
    pending = Path(temporary)
    try:
        request = urllib.request.Request(url, headers={'User-Agent': 'DefJamSetup/1'})
        with urllib.request.build_opener(HTTPSRedirect()).open(request, timeout=15) as response, pending.open('wb') as stream:
            validate_url(response.url)
            count = 0
            started = time.monotonic()
            while chunk := response.read(65536):
                cancelled()
                if time.monotonic() - started > 1800:
                    raise TimeoutError('Setup download exceeded thirty minutes')
                count += len(chunk)
                if count > size:
                    raise ValueError('Setup download is larger than expected')
                stream.write(chunk)
                progress('Downloading setup: ' + str(count // 1024**2) + ' / ' + str(size // 1024**2) + ' MiB')
            stream.flush()
            os.fsync(stream.fileno())
        if count != size or sha(pending) != checksum:
            raise ValueError('Setup download size or SHA-256 mismatch')
        # Another setup may have promoted these same bytes while this one downloaded.
        # Replacing an open final file is denied by Windows; validated cache bytes suffice.
        def valid_final():
            return path.is_file() and not path.is_symlink() and path.stat().st_size == size and sha(path) == checksum
        if valid_final():
            return path
        # Publish with a hard link where the filesystem allows it: linking to an existing
        # name fails without replacing it, so a concurrent reader never sees the file
        # delete-pending (Windows denies that open with PermissionError).
        try:
            os.link(pending, path)
        except FileExistsError:
            if not valid_final():
                os.replace(pending, path)
        except OSError:
            try:
                os.replace(pending, path)
            except PermissionError:
                if not valid_final():
                    raise
        return path
    finally:
        pending.unlink(missing_ok=True)


def safe_zip(data, destination):
    total, seen = 0, set()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for member in archive.infolist():
            name = member.orig_filename.rstrip('/')
            path = PurePosixPath(name)
            folded = name.casefold()
            mode = member.external_attr >> 16
            if (not name or path.is_absolute() or any(p in ('', '.', '..') or p.rstrip(' .') != p for p in name.split('/'))
                    or any(re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?', p, re.I) for p in path.parts)
                    or '\\' in name or ':' in name or folded in seen
                    or mode & 0o170000 not in (0, 0o100000, 0o040000)):
                raise ValueError('Unsafe setup ZIP entry')
            seen.add(folded)
            total += member.file_size
            if total > 2 * 1024**3 or len(seen) > 50000:
                raise ValueError('Setup archive expands beyond its limit')
            target = Path(destination).joinpath(*path.parts)
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(member) as source, target.open('wb') as out:
                    shutil.copyfileobj(source, out)
                if mode & 0o777:
                    target.chmod(mode & 0o777)


def unpack_setup(installer, platform, destination):
    if platform == 'macos-arm64':
        mount = Path(destination).parent / 'mounted'
        mount.mkdir()
        subprocess.run(['/usr/bin/hdiutil', 'attach', '-readonly', '-nobrowse', '-noautoopen', '-mountpoint', mount, installer], check=True, capture_output=True)
        try:
            data = (mount / 'Def Jam Setup.app/Contents/Resources/payload.zip').read_bytes()
        finally:
            subprocess.run(['/usr/bin/hdiutil', 'detach', mount], check=True, capture_output=True)
        safe_zip(data, destination)
    elif platform == 'windows-x64':
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
        kernel.LoadLibraryExW.restype = ctypes.c_void_p
        kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        kernel.FindResourceW.restype = ctypes.c_void_p
        kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        kernel.SizeofResource.restype = ctypes.c_uint32
        kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        kernel.LoadResource.restype = ctypes.c_void_p
        kernel.LockResource.argtypes = [ctypes.c_void_p]
        kernel.LockResource.restype = ctypes.c_void_p
        kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
        handle = kernel.LoadLibraryExW(str(installer), None, 2)
        if not handle:
            raise ValueError('Cannot inspect the selected Windows setup')
        try:
            resource = kernel.FindResourceW(handle, ctypes.c_void_p(101), ctypes.c_void_p(10))
            size = kernel.SizeofResource(handle, resource)
            pointer = kernel.LockResource(kernel.LoadResource(handle, resource))
            if not pointer or not size or size > 1024**3:
                raise ValueError('Invalid setup payload resource')
            safe_zip(ctypes.string_at(pointer, size), destination)
        finally:
            kernel.FreeLibrary(handle)
    else:
        # Linux setup carries an unpacked payload; reject every archive link/device.
        with tarfile.open(installer) as archive:
            members = archive.getmembers()
            total, seen = 0, set()
            for member in members:
                name = member.name.rstrip('/')
                parts = name.split('/')
                if (not name or name.startswith('/') or any(p in ('', '.', '..') or p.rstrip(' .') != p for p in parts)
                        or '\\' in name or ':' in name or name.casefold() in seen or not (member.isdir() or member.isfile())):
                    raise ValueError('Unsafe setup tar entry')
                seen.add(name.casefold())
                total += member.size
                if total > 2 * 1024**3 or len(seen) > 50000:
                    raise ValueError('Setup archive expands beyond its limit')
            roots = {m.name.split('/')[0] for m in members}
            if len(roots) != 1:
                raise ValueError('Unexpected setup archive roots')
            prefix = next(iter(roots)) + '/payload/'
            for member in members:
                if not member.isfile() or not member.name.startswith(prefix):
                    continue
                target = Path(destination).joinpath(*member.name[len(prefix):].split('/'))
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, target.open('wb') as out:
                    shutil.copyfileobj(source, out)
                target.chmod(member.mode & 0o777)



def verify_checksum(path, name, expected):
    if Path(path).stat().st_size > 1024:
        raise ValueError('Setup checksum file is too large')
    match = re.fullmatch(r'([0-9a-f]{64})\s+\*?([^\r\n]+)', Path(path).read_text(encoding='ascii').strip())
    if not match or match[1] != expected or match[2] != name:
        raise ValueError('Setup checksum file does not match release provenance')

def select_payload(tag, platform, cache, verify, previous, operation, cancelled, progress):
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    releases = catalog(platform, cache, cancelled, allow_cached=True)
    target = next((r for r in releases if r['tag'] == tag), None)
    if not target or not target['available']:
        raise ValueError('Selected release has no compatible setup')
    if previous and version(target['version']) < version(previous['version']) and not previous.get('updater'):
        raise ValueError('Historical installations do not support downgrade')
    installer_asset, checksum_asset, provenance_asset = target['assets']
    if type(provenance_asset.get('size')) is not int or provenance_asset['size'] > 4 * 1024**2:
        raise ValueError('Setup provenance too large')
    provenance_path = download(provenance_asset, cache, cancelled, progress)
    if provenance_path.stat().st_size > 4 * 1024**2:
        raise ValueError('Setup provenance too large')
    provenance = json.loads(provenance_path.read_text())
    if provenance.get('development') or provenance.get('platform') != platform or provenance.get('version') != target['version']:
        raise ValueError('Release provenance identity mismatch')
    if previous and not capable(provenance.get('updater')):
        raise ValueError('Historical setup cannot safely repair or update an existing install; use an updater-capable release')
    policy(operation, provenance, previous)
    if type(checksum_asset.get('size')) is not int or checksum_asset['size'] > 1024:
        raise ValueError('Setup checksum file is too large')
    checksum = download(checksum_asset, cache, cancelled, progress)
    verify_checksum(checksum, installer_asset['name'], provenance['installer_sha256'])
    installer = download(installer_asset, cache, cancelled, progress, provenance['installer_sha256'])
    if sha(installer) != provenance['installer_sha256']:
        raise ValueError('Setup provenance checksum mismatch')
    scratch = Path(tempfile.mkdtemp(prefix='selected-', dir=cache))
    try:
        payload = scratch / 'payload'
        payload.mkdir()
        progress('Verifying the selected setup payload')
        cancelled()
        unpack_setup(installer, platform, payload)
        manifest = verify(payload)
        for key in ('version', 'platform', 'source_commit', 'toolkit_commit', 'files', 'updater'):
            if manifest.get(key) != provenance.get(key):
                raise ValueError('Selected payload does not match release provenance: ' + key)
        return scratch, payload
    except BaseException:
        shutil.rmtree(scratch)
        raise


def child_arguments(args, payload, capable=True):
    result = ['-B', str(payload / 'engine/engine.py'), '--payload', str(payload)]
    if capable:
        result += ['--bundled-only', '--handoff-parent', str(os.getpid())]
    for name in ('install_dir', 'data_dir', 'dump', 'operation', 'toolchain', 'log', 'status_file', 'cancel_file'):
        value = getattr(args, name, None)
        if name == 'operation' and not capable:
            continue
        if value:
            result += ['--' + name.replace('_', '-'), str(value)]
    for name in ('silent', 'no_shortcuts', 'no_desktop_shortcut', 'install_prerequisites', 'hd_textures', 'steam_shortcut'):
        if getattr(args, name, False):
            result.append('--' + name.replace('_', '-'))
    return result



class WindowsJob:
    """Own the selected engine's process tree; closing/killing the controller reaps it."""
    def __init__(self, process):
        from ctypes import wintypes as w
        size = ctypes.c_size_t
        class Basic(ctypes.Structure):
            _fields_ = [('process_time', ctypes.c_int64), ('job_time', ctypes.c_int64),
                        ('flags', w.DWORD), ('min_working', size), ('max_working', size),
                        ('active', w.DWORD), ('affinity', size), ('priority', w.DWORD), ('scheduling', w.DWORD)]
        class IO(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint64) for name in ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]
        class Extended(ctypes.Structure):
            _fields_ = [('basic', Basic), ('io', IO), ('process_memory', size), ('job_memory', size),
                        ('peak_process_memory', size), ('peak_job_memory', size)]
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.CreateJobObjectW.argtypes = [ctypes.c_void_p, w.LPCWSTR]
        self.api.CreateJobObjectW.restype = w.HANDLE
        self.api.SetInformationJobObject.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
        self.api.SetInformationJobObject.restype = w.BOOL
        self.api.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
        self.api.AssignProcessToJobObject.restype = w.BOOL
        self.api.CloseHandle.argtypes = [w.HANDLE]
        self.api.CloseHandle.restype = w.BOOL
        self.handle = self.api.CreateJobObjectW(None, None)
        limits = Extended(); limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        if not self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)) or not self.api.AssignProcessToJobObject(self.handle, int(process._handle)):
            error = ctypes.WinError(ctypes.get_last_error()); self.close(); raise error
    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle); self.handle = None


def start_selected(interpreter, arguments):
    if os.name != 'nt':
        return subprocess.Popen([interpreter, *arguments], start_new_session=True), None
    # Start only a standard-library gate until the controller owns the Windows job.
    # Running the engine in this same interpreter then inherits the job for every stage,
    # including historical engines; no child can escape before job assignment.
    gate = ("import sys\nif sys.stdin.buffer.read(1)!=b'1':sys.exit(6)\n"
            "import os,runpy\nsys.argv=sys.argv[1:]\nsys.path.insert(0,os.path.dirname(sys.argv[0]))\n"
            "runpy.run_path(sys.argv[0],run_name='__main__')")
    child = subprocess.Popen([interpreter, '-B', '-c', gate, *arguments[1:]], stdin=subprocess.PIPE)
    job = None
    try:
        job = WindowsJob(child)
        child.stdin.write(b'1'); child.stdin.flush(); child.stdin.close()
        return child, job
    except BaseException:
        child.terminate(); child.wait()
        if job: job.close()
        raise

def run_selected(args, platform, payload, cancelled):
    interpreter = payload / ('python/python.exe' if platform == 'windows-x64' else 'python/bin/python3')
    # The verified target runs its own engine/Python. stdout remains native wizard diagnostics.
    supported = capable(json.loads((payload / 'manifest.json').read_text()).get('updater'))
    if not supported and not args.dump:
        raise ValueError('Historical setup requires the original dump for install/repair')
    def processes():
        if os.name == 'nt':
            return {}
        try:
            rows = subprocess.run(['ps', '-axo', 'pid=,ppid=,pgid=,lstart='], capture_output=True, text=True).stdout.splitlines()
        except OSError:
            # Some sandboxes refuse to start ps. Stage tracking is only a backstop for a crashed
            # engine; losing it must not abort the handoff or skip killing the engine.
            return {}
        result = {}
        for row in rows:
            fields = row.split(None, 3)
            if len(fields) == 4:
                result[int(fields[0])] = (int(fields[1]), int(fields[2]), fields[3])
        return result
    def sample():
        rows = processes()
        # Retain only the same still-live process, including orphaned stage sessions.
        for pid, identity in list(known.items()):
            if pid not in rows or rows[pid][1:] != identity:
                del known[pid]
        found = {child.pid}
        while True:
            more = {pid for pid, row in rows.items() if row[0] in found}
            if more <= found:
                break
            found |= more
        for pid in found - {child.pid}:
            known[pid] = rows[pid][1:]
    def stop_stages(sig):
        rows = processes()
        for pid, identity in known.items():
            # Only session/group leaders that still have the observed start identity.
            if pid in rows and rows[pid][1:] == identity and identity[0] == pid:
                with contextlib.suppress(ProcessLookupError, PermissionError):
                    os.killpg(pid, sig)
    known = {}
    sampled = 0
    child, job = start_selected(interpreter, child_arguments(args, payload, supported))
    try:
        while child.poll() is None:
            if time.monotonic() - sampled > .5:
                sample()
                sampled = time.monotonic()
            cancelled()
            time.sleep(.1)
        if child.returncode and os.name != 'nt':
            # A killed/crashed engine cannot reap stages that started their own sessions.
            stop_stages(signal.SIGKILL)
        return child.returncode
    except BaseException:
        # Let a capable engine consume its cancel file and clean its separately-sessioned stages.
        try:
            if supported:
                child.wait(timeout=12)
        except subprocess.TimeoutExpired:
            pass
        if child.poll() is not None:
            if os.name != 'nt':
                stop_stages(signal.SIGKILL)
            raise
        with contextlib.suppress(Exception):
            sample()  # A failed sample must never skip the kill below and leave the engine running.
        if os.name == 'nt':
            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], capture_output=True)
        else:
            for sig in (signal.SIGTERM, signal.SIGKILL):
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(child.pid, sig)
                stop_stages(sig)
                try:
                    child.wait(timeout=3)
                    break
                except subprocess.TimeoutExpired:
                    pass
        child.wait()
        raise
    finally:
        if job:
            job.close()
