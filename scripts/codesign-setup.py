#!/usr/bin/env python3
"""Authenticode-sign a CI-built Windows setup and the launcher inside it (FFNY Online releases).

Windows only. The certificate's key stays in the signing service (Certum SimplySign); signtool
reaches it through the certificate in CurrentUser\\My, so this script never sees a key.

  probe    Sign a tiny scratch file. Fails (exit 3) when the SimplySign session has expired.
  sign     Take the setup and provenance CI built (windows-setup-assets) and write the signed
           setup, its .sha256 and a provenance that names both the CI hashes and the signed ones.
  compare  Show that a signed setup differs from the CI setup only by signing: anyone can run it.

What `sign` changes, and nothing else:
  1. DefJamLauncher.exe in the payload gets an Authenticode signature. Its Authenticode image hash
     (the PE hash a signature covers) is checked to be the CI file's: only the signature was added.
  2. The payload's manifest.json lists the launcher's new SHA-256 (setup/engine.py verify_payload
     checks every file against it).
  3. The setup's RCDATA 101 resource holds that payload (every other entry byte for byte as CI
     zipped it, same order and timestamps), and its compiled-in PAYLOAD_SHA256 string names it
     (setup/main.cpp unpack). Every PE section other than .rsrc is CI's, apart from those 64
     characters; every other resource is CI's.
  4. The setup itself is signed last.
Every signature is checked with `signtool verify /pa` and Get-AuthenticodeSignature (Valid, the
expected thumbprint). Python, its DLLs and the wheels are third-party and keep their own signatures;
the game executable is compiled on each player's PC from their own dump and is never shipped.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

if sys.platform == 'win32':
    from ctypes import wintypes

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_THUMBPRINT = '16E6B5882628B2F023AD98D546FA63AADA92D9D7'
DEFAULT_TIMESTAMP = 'http://time.certum.pl'
SIGNTOOL_GUESS = r'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
SIGN_IN_PAYLOAD = ('DefJamLauncher.exe',)   # ours; everything else in python/ is python.org's or a wheel's
PAYLOAD_RESOURCE = 101                      # setup/payload.rc.in
RT_RCDATA = 10
LOGIN_MESSAGE = 'Log in to SimplySign Desktop (tray) and run again'


class SigningUnavailable(RuntimeError):
    """signtool could not sign: almost always an expired SimplySign session."""


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


# ---- PE ------------------------------------------------------------------------------------------

def pe_layout(data):
    """Offsets Authenticode needs, and the section table."""
    if data[:2] != b'MZ':
        raise ValueError('not a PE file')
    e = struct.unpack_from('<I', data, 0x3C)[0]
    if data[e:e + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    coff = e + 4
    count, optional_size = struct.unpack_from('<H', data, coff + 2)[0], struct.unpack_from('<H', data, coff + 16)[0]
    optional = coff + 20
    magic = struct.unpack_from('<H', data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    security = directories + 4 * 8
    cert_offset, cert_size = struct.unpack_from('<II', data, security)
    sections = []
    for i in range(count):
        at = optional + optional_size + 40 * i
        name = data[at:at + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsize, va, raw_size, raw_ptr = struct.unpack_from('<IIII', data, at + 8)
        sections.append({'name': name, 'vsize': vsize, 'va': va, 'raw_size': raw_size, 'raw_ptr': raw_ptr})
    return {'checksum': optional + 64, 'security': security, 'cert_offset': cert_offset, 'cert_size': cert_size,
            'sections': sections}


def authenticode_hash(data):
    """SHA-256 over the file minus the checksum, the security directory entry and the certificate
    table: the digest a signature signs, so it is the same before and after signing. signtool pads
    an unsigned file to 8 bytes before appending the table; the padding is hashed, so pad here too."""
    layout = pe_layout(data)
    end = layout['cert_offset'] if layout['cert_size'] else len(data)
    h = hashlib.sha256()
    h.update(data[:layout['checksum']])
    h.update(data[layout['checksum'] + 4:layout['security']])
    h.update(data[layout['security'] + 8:end])
    if not layout['cert_size'] and end % 8:
        h.update(b'\0' * (8 - end % 8))
    return h.hexdigest()


def is_signed(data):
    return pe_layout(data)['cert_size'] != 0


def section_bytes(data, section):
    return data[section['raw_ptr']:section['raw_ptr'] + min(section['vsize'] or section['raw_size'], section['raw_size'])]


# ---- resources (kernel32) ------------------------------------------------------------------------

def _kernel():
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    kernel.LoadLibraryExW.restype = wintypes.HMODULE
    kernel.FreeLibrary.argtypes = [wintypes.HMODULE]
    kernel.FindResourceExW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD]
    kernel.FindResourceExW.restype = ctypes.c_void_p
    kernel.LoadResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
    kernel.LoadResource.restype = ctypes.c_void_p
    kernel.LockResource.argtypes = [ctypes.c_void_p]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.SizeofResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
    kernel.SizeofResource.restype = wintypes.DWORD
    kernel.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    kernel.BeginUpdateResourceW.restype = wintypes.HANDLE
    kernel.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD,
                                       ctypes.c_void_p, wintypes.DWORD]
    kernel.UpdateResourceW.restype = wintypes.BOOL
    kernel.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    kernel.EndUpdateResourceW.restype = wintypes.BOOL
    kernel.EnumResourceTypesW.argtypes = [wintypes.HMODULE, ENUM_TYPES, ctypes.c_void_p]
    kernel.EnumResourceNamesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ENUM_NAMES, ctypes.c_void_p]
    kernel.EnumResourceLanguagesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, ENUM_LANGS,
                                              ctypes.c_void_p]
    return kernel


if sys.platform == 'win32':   # the PE and ZIP helpers above and below also import (and are tested) elsewhere
    ENUM_TYPES = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p)
    ENUM_NAMES = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
    ENUM_LANGS = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD,
                                    ctypes.c_void_p)


def _resource_id(value):
    """An integer id stays an int; a name pointer becomes its string (valid only inside the callback)."""
    value = value or 0
    return value if value < 0x10000 else ctypes.wstring_at(value)


def _resource_arg(value):
    """An int id, or a buffer holding the name, for a c_void_p argument (kept alive by the caller)."""
    return value if isinstance(value, int) else ctypes.create_unicode_buffer(value)


def read_resources(path):
    """{(type, name, language): bytes} for every resource in the file, loaded as data only."""
    kernel = _kernel()
    handle = kernel.LoadLibraryExW(str(Path(path).resolve()), None, 0x2)  # LOAD_LIBRARY_AS_DATAFILE
    if not handle:
        raise ValueError(f'{path} is not a readable Windows executable')
    found = {}
    try:
        types = []
        on_type = ENUM_TYPES(lambda h, t, p: types.append(_resource_id(t)) or True)
        kernel.EnumResourceTypesW(handle, on_type, None)
        for kind in types:
            names, kind_arg = [], _resource_arg(kind)
            on_name = ENUM_NAMES(lambda h, t, n, p: names.append(_resource_id(n)) or True)
            kernel.EnumResourceNamesW(handle, kind_arg, on_name, None)
            for name in names:
                langs, name_arg = [], _resource_arg(name)
                on_lang = ENUM_LANGS(lambda h, t, n, lang, p: langs.append(lang) or True)
                kernel.EnumResourceLanguagesW(handle, kind_arg, name_arg, on_lang, None)
                for lang in langs:
                    resource = kernel.FindResourceExW(handle, kind_arg, name_arg, lang)
                    if not resource:
                        raise ValueError(f'resource {kind}/{name}/{lang} cannot be read')
                    size = kernel.SizeofResource(handle, resource)
                    pointer = kernel.LockResource(kernel.LoadResource(handle, resource))
                    found[(kind, name, lang)] = ctypes.string_at(pointer, size) if size else b''
    finally:
        kernel.FreeLibrary(handle)
    return found


def payload_resource(resources):
    hits = [(key, value) for key, value in resources.items() if key[0] == RT_RCDATA and key[1] == PAYLOAD_RESOURCE]
    if len(hits) != 1:
        raise ValueError(f'expected one RCDATA {PAYLOAD_RESOURCE} payload resource, found {len(hits)}')
    return hits[0]


def replace_payload_resource(path, language, payload):
    kernel = _kernel()
    handle = kernel.BeginUpdateResourceW(str(path), False)
    if not handle:
        raise OSError(ctypes.get_last_error(), 'BeginUpdateResourceW failed')
    buffer = ctypes.create_string_buffer(payload, len(payload))
    if not kernel.UpdateResourceW(handle, RT_RCDATA, PAYLOAD_RESOURCE, language, buffer, len(payload)):
        error = ctypes.get_last_error()
        kernel.EndUpdateResourceW(handle, True)
        raise OSError(error, 'UpdateResourceW failed')
    if not kernel.EndUpdateResourceW(handle, False):
        raise OSError(ctypes.get_last_error(), 'EndUpdateResourceW failed')


# ---- signtool ------------------------------------------------------------------------------------

def find_signtool(given=None):
    if given and Path(given).is_file():
        return Path(given)
    if Path(SIGNTOOL_GUESS).is_file():
        return Path(SIGNTOOL_GUESS)
    kits = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Windows Kits/10/bin'
    found = sorted(kits.glob('10.*/x64/signtool.exe'))
    if not found:
        raise FileNotFoundError('signtool.exe not found; install the Windows 10/11 SDK signing tools')
    return found[-1]


def signtool_sign(signtool, path, thumbprint, timestamp, timeout=600):
    command = [str(signtool), 'sign', '/sha1', thumbprint, '/fd', 'sha256', '/tr', timestamp, '/td', 'sha256',
               str(path)]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise SigningUnavailable(f'signtool did not finish signing {Path(path).name} in {timeout} s')
    if result.returncode:
        raise SigningUnavailable(f'signtool could not sign {Path(path).name}: '
                                 + (result.stderr.strip() or result.stdout.strip()))


def signtool_verify(signtool, path):
    result = subprocess.run([str(signtool), 'verify', '/pa', '/tw', str(path)], capture_output=True, text=True,
                            timeout=300)
    if result.returncode:
        raise ValueError(f'signtool verify /pa failed for {path}: {(result.stderr or result.stdout).strip()}')


def signer(path):
    """Get-AuthenticodeSignature's view: status, signer and timestamp certificate."""
    script = ("$s = Get-AuthenticodeSignature -LiteralPath $env:FFNY_CODESIGN_FILE; "
              "[pscustomobject]@{ status = [string]$s.Status; thumbprint = $s.SignerCertificate.Thumbprint; "
              "subject = $s.SignerCertificate.Subject; issuer = $s.SignerCertificate.Issuer; "
              "not_after = $s.SignerCertificate.NotAfter.ToString('yyyy-MM-dd'); "
              "timestamped = [bool]$s.TimeStamperCertificate } | ConvertTo-Json -Compress")
    result = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', script], capture_output=True,
                            text=True, timeout=120, env={**os.environ, 'FFNY_CODESIGN_FILE': str(path)})
    if result.returncode:
        raise ValueError('Get-AuthenticodeSignature failed: ' + result.stderr.strip())
    return json.loads(result.stdout)


def sign_and_check(signtool, path, thumbprint, timestamp):
    """Sign one PE file and prove the signature: valid chain, expected signer, timestamped, and the
    Authenticode image hash unchanged (so signing added only the signature)."""
    before = Path(path).read_bytes()
    if is_signed(before):
        raise ValueError(f'{path} is already signed; refusing to sign it twice')
    signtool_sign(signtool, path, thumbprint, timestamp)
    after = Path(path).read_bytes()
    signtool_verify(signtool, path)
    info = signer(path)
    if info['status'] != 'Valid' or (info['thumbprint'] or '').upper() != thumbprint.upper() or not info['timestamped']:
        raise ValueError(f'unexpected signature on {path}: {info}')
    if authenticode_hash(before) != authenticode_hash(after):
        raise ValueError(f'signing changed more than the signature of {path}')
    return {'sha256_before': sha256_bytes(before), 'size_before': len(before),
            'sha256_after': sha256_bytes(after), 'size_after': len(after),
            'authenticode_sha256': authenticode_hash(after)}, info


def probe(signtool, thumbprint, timestamp):
    """Sign a two-line PowerShell script in a scratch folder: proves the SimplySign session is live."""
    with tempfile.TemporaryDirectory(prefix='ffny-codesign-probe-') as folder:
        path = Path(folder) / 'probe.ps1'
        path.write_text('# FFNY Online code-signing session probe\r\n', encoding='ascii')
        signtool_sign(signtool, path, thumbprint, timestamp, timeout=180)
        signtool_verify(signtool, path)


# ---- the setup -----------------------------------------------------------------------------------

def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rewrite_payload(old_zip, replacements):
    """The payload ZIP with some entries' bytes replaced; every entry keeps its order, name,
    timestamp and compression. Returns the new ZIP's bytes."""
    import io
    out = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(old_zip)) as old, zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as new:
        names = [info.filename for info in old.infolist()]
        missing = set(replacements) - set(names)
        if missing:
            raise ValueError('not in the payload: ' + ', '.join(sorted(missing)))
        for info in old.infolist():
            data = replacements.get(info.filename)
            if data is None:
                data = old.read(info)
            fresh = zipfile.ZipInfo(info.filename, info.date_time)
            fresh.compress_type, fresh.external_attr, fresh.create_system = info.compress_type, info.external_attr, info.create_system
            new.writestr(fresh, data)
    return out.getvalue()


def patch_hash_string(data, old_hex, new_hex):
    """setup/CMakeLists.txt compiles PAYLOAD_SHA256 in as L"<64 hex>": replace its one copy."""
    old, new = old_hex.encode('utf-16-le'), new_hex.encode('utf-16-le')
    if data.count(old) != 1:
        raise ValueError(f'expected the payload hash string once in the setup, found it {data.count(old)} times')
    at = data.index(old)
    return data[:at] + new + data[at + len(new):], at


def compare_setups(ci_path, signed_path, report=print):
    """What differs between the CI setup and a signed one; raises unless it is only signing.
    Returns the summary dictionary."""
    ci, signed = Path(ci_path).read_bytes(), Path(signed_path).read_bytes()
    if is_signed(ci):
        raise ValueError('the CI setup is expected unsigned')
    ci_res, signed_res = read_resources(ci_path), read_resources(signed_path)
    (ci_key, ci_payload), (signed_key, signed_payload) = payload_resource(ci_res), payload_resource(signed_res)
    if ci_key != signed_key or set(ci_res) != set(signed_res):
        raise ValueError('the resource lists differ')
    for key in ci_res:
        if key != ci_key and ci_res[key] != signed_res[key]:
            raise ValueError(f'resource {key} differs')
    ci_hash, new_hash = sha256_bytes(ci_payload), sha256_bytes(signed_payload)
    # Sections: all but .rsrc identical, apart from the payload hash string in one of them.
    ci_layout, signed_layout = pe_layout(ci), pe_layout(signed)
    by_name = {s['name']: s for s in signed_layout['sections']}
    if [s['name'] for s in ci_layout['sections']] != [s['name'] for s in signed_layout['sections']]:
        raise ValueError('the section lists differ')
    old_string, new_string = ci_hash.encode('utf-16-le'), new_hash.encode('utf-16-le')
    patched = []
    for section in ci_layout['sections']:
        if section['name'] == '.rsrc':
            continue
        a, b = section_bytes(ci, section), section_bytes(signed, by_name[section['name']])
        if a == b:
            continue
        if a.count(old_string) == 1 and a.replace(old_string, new_string) == b:
            patched.append(section['name'])
            continue
        raise ValueError(f'section {section["name"]} differs beyond the payload hash string')
    if ci_hash != new_hash and len(patched) != 1:
        raise ValueError('the payload changed but its hash string did not')
    # The payload: same entries, same bytes, except the signed launcher and the manifest's line for it.
    import io
    changed = {}
    with zipfile.ZipFile(io.BytesIO(ci_payload)) as a, zipfile.ZipFile(io.BytesIO(signed_payload)) as b:
        if a.namelist() != b.namelist():
            raise ValueError('the payload entry lists differ')
        for name in a.namelist():
            x, y = a.read(name), b.read(name)
            if x != y:
                changed[name] = (x, y)
    for name, (x, y) in changed.items():
        if name == 'manifest.json':
            mx, my = json.loads(x), json.loads(y)
            files_x, files_y = mx.pop('files'), my.pop('files')
            if mx != my or files_x.keys() != files_y.keys():
                raise ValueError('manifest.json differs beyond file hashes')
            for entry in files_x:
                if files_x[entry] != files_y[entry] and (entry not in changed or sha256_bytes(changed[entry][1]) != files_y[entry]):
                    raise ValueError('manifest.json changes the hash of ' + entry)
        elif name in SIGN_IN_PAYLOAD:
            if is_signed(x) or not is_signed(y) or authenticode_hash(x) != authenticode_hash(y):
                raise ValueError(name + ' differs beyond its signature')
        else:
            raise ValueError('payload file changed: ' + name)
    if changed and 'manifest.json' not in changed:
        raise ValueError('a payload file changed but the manifest did not')
    summary = {'ci_setup_sha256': sha256_bytes(ci), 'signed_setup_sha256': sha256_bytes(signed),
               'setup_signed': is_signed(signed), 'setup_authenticode_sha256': authenticode_hash(signed),
               'ci_payload_sha256': ci_hash, 'signed_payload_sha256': new_hash,
               'payload_files_signed': sorted(n for n in changed if n != 'manifest.json'),
               'sections_with_payload_hash': patched}
    report(f'CI setup     {summary["ci_setup_sha256"]}')
    report(f'signed setup {summary["signed_setup_sha256"]} (Authenticode signed: {summary["setup_signed"]})')
    report(f'payload      {ci_hash} -> {new_hash}')
    report('signed inside the payload (Authenticode image hash unchanged): '
           + (', '.join(summary['payload_files_signed']) or 'nothing'))
    report('manifest.json: only those files\' SHA-256 changed; other resources and sections: identical'
           + (f'; {", ".join(patched)} names the new payload hash' if patched else ''))
    return summary


def sign_setup(args):
    signtool = find_signtool(args.signtool)
    installer, provenance_path = Path(args.installer), Path(args.provenance)
    provenance = json.loads(provenance_path.read_text(encoding='utf-8'))
    ci_bytes = installer.read_bytes()
    if sha256_bytes(ci_bytes) != provenance['installer_sha256']:
        raise ValueError('the setup is not the one its provenance describes')
    if is_signed(ci_bytes):
        raise ValueError('the setup is already signed')
    if provenance.get('development'):
        raise ValueError('a development payload is not signed')
    (key, ci_payload) = payload_resource(read_resources(installer))
    if sha256_bytes(ci_payload) != provenance['payload_sha256']:
        raise ValueError('the setup\'s payload is not the one its provenance describes')
    out = Path(args.output_dir)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix='work-', dir=out.parent))
    engine = load_module('codesign_setup_engine', ROOT / 'setup/engine.py')
    assets = load_module('codesign_setup_assets', ROOT / 'scripts/check-setup-assets.py')
    try:
        # 1. The CI payload, checked file by file against its manifest.
        package = load_module('codesign_setup_package', ROOT / 'scripts/package-setup.py')
        (work / 'payload.zip').write_bytes(ci_payload)
        package.safe_unpack(work / 'payload.zip', work / 'payload')
        manifest = engine.verify_payload(work / 'payload', provenance['platform'])
        if {k: v for k, v in manifest.items()} != {k: v for k, v in provenance.items()
                                                    if k not in ('installer_sha256', 'payload_sha256')}:
            raise ValueError('the payload manifest is not the provenance\'s')
        # 2. Our executables inside it.
        signed_files, replacements, signature = [], {}, None
        for name in SIGN_IN_PAYLOAD:
            path = work / 'payload' / name
            record, signature = sign_and_check(signtool, path, args.thumbprint, args.timestamp)
            if record['sha256_before'] != manifest['files'][name]:
                raise ValueError(name + ' is not the manifest\'s')
            manifest['files'][name] = record['sha256_after']
            replacements[name] = path.read_bytes()
            signed_files.append({'path': 'payload/' + name, **record})
            print(f'Signed payload/{name}: {record["sha256_before"]} -> {record["sha256_after"]}')
        # The same formatting package-setup.py writes.
        replacements['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode('utf-8')
        (work / 'payload' / 'manifest.json').write_bytes(replacements['manifest.json'])
        engine.verify_payload(work / 'payload', provenance['platform'])
        new_payload = rewrite_payload(ci_payload, replacements)
        new_payload_path = work / 'payload-signed.zip'
        new_payload_path.write_bytes(new_payload)
        new_payload_hash = sha256_bytes(new_payload)
        # 3. The CI setup with that payload and its hash.
        unsigned = work / installer.name
        patched, at = patch_hash_string(ci_bytes, provenance['payload_sha256'], new_payload_hash)
        unsigned.write_bytes(patched)
        replace_payload_resource(unsigned, key[2], new_payload)
        assets.inspect_payload(new_payload_path)               # inventory, allowed files, embedded Python runs
        assets.verify_embedded_payload(unsigned, new_payload_path)
        # 4. The setup itself.
        final = out / installer.name
        shutil.copyfile(unsigned, final)
        record, signature = sign_and_check(signtool, final, args.thumbprint, args.timestamp)
        signed_files.append({'path': installer.name, **record,
                             'ci_sha256': provenance['installer_sha256'], 'ci_size': len(ci_bytes)})
        assets.verify_embedded_payload(final, new_payload_path)
        summary = compare_setups(installer, final, report=lambda line: print('  ' + line))
        if summary['payload_files_signed'] != sorted(SIGN_IN_PAYLOAD):
            raise ValueError('the signed setup does not carry the signed launcher')
        final_hash = sha256_file(final)
        (out / (installer.stem + '.sha256')).write_text(f'{final_hash}  {final.name}\n', encoding='utf-8')
        signed_provenance = {'installer_sha256': final_hash, 'payload_sha256': new_payload_hash, **manifest,
                             'codesign': {
                                 'signer': {k: signature[k] for k in ('subject', 'issuer', 'thumbprint', 'not_after')},
                                 'timestamp_url': args.timestamp,
                                 'before_signing': {'built_by': args.built_by, 'run': args.run_url,
                                                    'installer_sha256': provenance['installer_sha256'],
                                                    'installer_size': len(ci_bytes),
                                                    'payload_sha256': provenance['payload_sha256'],
                                                    'provenance_sha256': sha256_file(provenance_path)},
                                 'after_signing': {'installer_sha256': final_hash, 'installer_size': final.stat().st_size,
                                                   'payload_sha256': new_payload_hash},
                                 'signed_files': signed_files,
                                 'changes': 'Only Authenticode signatures: DefJamLauncher.exe and the setup are signed '
                                            '(their Authenticode image hashes are unchanged), manifest.json lists the '
                                            'signed launcher\'s SHA-256, and the setup carries that payload and its hash. '
                                            'Every other file, section and resource is the build named in before_signing. '
                                            'Check: python scripts/codesign-setup.py compare --ci <that setup> --signed <this setup>',
                             }}
        (out / (installer.stem + '.provenance.json')).write_text(json.dumps(signed_provenance, indent=2) + '\n',
                                                                 encoding='utf-8')
        print(f'Signed {final.name}: CI {provenance["installer_sha256"]} -> {final_hash} ({final.stat().st_size} bytes)')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('probe', 'sign'):
        p = sub.add_parser(name)
        p.add_argument('--thumbprint', default=DEFAULT_THUMBPRINT)
        p.add_argument('--timestamp', default=DEFAULT_TIMESTAMP)
        p.add_argument('--signtool')
    s = sub.choices['sign']
    s.add_argument('--installer', required=True, type=Path, help='the unsigned DefJamSetup-<version>-windows-x64.exe')
    s.add_argument('--provenance', required=True, type=Path, help='its .provenance.json')
    s.add_argument('--output-dir', required=True, type=Path, help='emptied, then holds the signed exe, .sha256, provenance')
    s.add_argument('--built-by', default='ci', choices=['ci', 'local'])
    s.add_argument('--run-url', default=None, help='the CI run that built the setup')
    c = sub.add_parser('compare')
    c.add_argument('--ci', required=True, type=Path)
    c.add_argument('--signed', required=True, type=Path)
    args = parser.parse_args(argv)
    if sys.platform != 'win32':
        parser.error('Windows only')
    try:
        if args.command == 'probe':
            probe(find_signtool(args.signtool), args.thumbprint, args.timestamp)
            print('Code signing works (SimplySign session live).')
            return 0
        if args.command == 'compare':
            compare_setups(args.ci, args.signed)
            return 0
        return sign_setup(args)
    except SigningUnavailable as error:
        print(f'{LOGIN_MESSAGE}. ({error})', file=sys.stderr)
        return 3


if __name__ == '__main__':
    sys.exit(main())
