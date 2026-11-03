"""The app's own updates (setup/releases.py newest_app and select_app, engine --check-app-update and
--stage-app): which release offers a newer app, and that a downloaded app is used only after its
GitHub digest, checksum file, provenance and the setup payload inside it all agree. Game-free."""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location('app_update_' + name, ROOT / 'setup' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


releases, engine = load('releases'), load('engine')
PLATFORM = 'macos-arm64'
MANIFEST = {'version': '0.13.1', 'platform': PLATFORM, 'source_commit': 'a' * 40, 'toolkit_commit': 'b' * 40,
            'files': {'source/x.c': 'c' * 64}, 'updater': releases.CONTRACT}


def asset(tag, name, data, idx):
    return dict(name=name, id=idx, state='uploaded', size=len(data), digest='sha256:' + hashlib.sha256(data).hexdigest(),
                browser_download_url=f'https://github.com/{releases.REPO}/releases/download/{tag}/{name}')


def bundle_zip(extra=None, program=True):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w') as z:
        z.writestr(releases.APP_BUNDLE + '/Contents/Info.plist', '<plist/>')
        if program:
            item = zipfile.ZipInfo(releases.APP_BUNDLE + '/Contents/MacOS/' + releases.APP_PROGRAM)
            item.external_attr = 0o100755 << 16
            z.writestr(item, 'synthetic program')
        z.writestr(releases.APP_BUNDLE + '/Contents/Resources/payload/manifest.json', json.dumps(MANIFEST))
        for name in extra or ():
            z.writestr(name, 'extra')
    return out.getvalue()


def release(v, archive=None, provenance=None, app=True, setup=True):
    """A GitHub release row and the bytes behind each of its download URLs."""
    tag = 'v' + v
    files, assets = {}, []
    if setup:
        stem = f'DefJamSetup-{v}-{PLATFORM}'
        for i, ext in enumerate(('.dmg', '.sha256', '.provenance.json')):
            assets.append(asset(tag, stem + ext, b'setup', i))
    if app:
        archive = bundle_zip() if archive is None else archive
        digest = hashlib.sha256(archive).hexdigest()
        stem = f'DefJamRecompiled-{v}-{PLATFORM}'
        prov = dict(MANIFEST, version=v, kind='app', app_sha256=digest) if provenance is None else provenance
        for i, (name, data) in enumerate(((stem + '.zip', archive),
                                          (stem + '.sha256', f'{digest}  {stem}.zip\n'.encode()),
                                          (stem + '.provenance.json', json.dumps(prov).encode())), start=10):
            a = asset(tag, name, data, i)
            assets.append(a)
            files[a['browser_download_url']] = data
    return dict(tag_name=tag, body='notes', assets=assets), files


def serve(monkeypatch, rows):
    files = {}
    for _, f in rows:
        files.update(f)
    monkeypatch.setattr(releases, 'read_url', lambda *a: json.dumps([r for r, _ in rows]).encode())

    class Response(io.BytesIO):
        def __init__(self, url):
            super().__init__(files[url])
            self.url = url

        def __enter__(self):
            return self

        def __exit__(self, *exc):
            return False

    class Opener:
        def open(self, request, *a, **k):
            return Response(request.full_url)
    monkeypatch.setattr(releases.urllib.request, 'build_opener', lambda *a: Opener())


def test_catalog_finds_the_app_beside_setup_and_each_independently(tmp_path, monkeypatch):
    serve(monkeypatch, [release('0.13.1'), release('0.13.0', app=False), release('0.12.9', setup=False)])
    rows = {r['version']: r for r in releases.catalog(PLATFORM, tmp_path)}
    assert rows['0.13.1']['available'] and rows['0.13.1']['app_available']
    assert [a['name'] for a in rows['0.13.1']['app_assets']] == [
        'DefJamRecompiled-0.13.1-macos-arm64.zip', 'DefJamRecompiled-0.13.1-macos-arm64.sha256',
        'DefJamRecompiled-0.13.1-macos-arm64.provenance.json']
    assert rows['0.13.0']['available'] and not rows['0.13.0']['app_available'] and rows['0.13.0']['app_assets'] == []
    assert not rows['0.12.9']['available'] and rows['0.12.9']['app_available']


def test_newest_app_is_the_newest_newer_release_with_an_app(tmp_path, monkeypatch):
    serve(monkeypatch, [release('0.13.2', app=False), release('0.13.1'), release('0.12.0')])
    assert releases.newest_app(PLATFORM, tmp_path, '0.13.0')['tag'] == 'v0.13.1'
    assert releases.newest_app(PLATFORM, tmp_path, '0.13.1') is None


def test_select_app_verifies_and_returns_the_bundle(tmp_path, monkeypatch):
    serve(monkeypatch, [release('0.13.1')])
    seen = []

    def verify(payload):
        seen.append(payload)
        return json.loads((payload / 'manifest.json').read_text())
    scratch, bundle = releases.select_app('v0.13.1', PLATFORM, tmp_path, verify, lambda: None, lambda text: None)
    assert bundle == scratch / releases.APP_BUNDLE and seen == [bundle / 'Contents/Resources/payload']
    program = bundle / 'Contents/MacOS' / releases.APP_PROGRAM
    assert program.read_text() == 'synthetic program'
    assert os.name == 'nt' or program.stat().st_mode & 0o111      # Windows has no execute bit


@pytest.mark.parametrize('case', ['development', 'not-an-app', 'old-contract', 'digest', 'extra-root',
                                  'no-program', 'payload-mismatch', 'other-platform'])
def test_select_app_refuses_anything_that_does_not_verify(tmp_path, monkeypatch, case):
    archive = bundle_zip(extra=['stray.txt'] if case == 'extra-root' else None, program=case != 'no-program')
    digest = hashlib.sha256(archive).hexdigest()
    prov = dict(MANIFEST, kind='app', app_sha256=digest)
    if case == 'development':
        prov['development'] = True
    if case == 'not-an-app':
        prov['kind'] = 'setup'
    if case == 'old-contract':
        prov['updater'] = {'protocol': 0, 'data_schema': 1}
    if case == 'digest':
        prov['app_sha256'] = 'd' * 64
    if case == 'payload-mismatch':
        prov['source_commit'] = 'e' * 40
    serve(monkeypatch, [release('0.13.1', archive=archive, provenance=prov)])
    verify = lambda payload: json.loads((payload / 'manifest.json').read_text())
    platform = 'linux-x64' if case == 'other-platform' else PLATFORM
    with pytest.raises(ValueError):
        releases.select_app('v0.13.1', platform, tmp_path, verify, lambda: None, lambda text: None)
    assert not list(tmp_path.glob('app-*'))                 # nothing unverified is left behind


def payload(tmp_path):
    folder = tmp_path / 'payload'
    (folder / 'source').mkdir(parents=True)
    (folder / 'source/marker.c').write_text('/* synthetic */')
    files = {p.relative_to(folder).as_posix(): engine.digest(p) for p in folder.rglob('*') if p.is_file()}
    engine.write_json(folder / 'manifest.json', dict(schema=1, platform=engine.HOST_PLATFORM, version='0.13.0',
                                                     source_commit='a' * 40, toolkit_commit='b' * 40, files=files))
    return folder


def test_engine_check_app_update_writes_what_the_app_reads(tmp_path, monkeypatch):
    calls = []
    monkeypatch.setattr(engine.releases, 'newest_app',
                        lambda platform, cache, current, cancelled: calls.append(current) or {'tag': 'v0.13.1', 'version': '0.13.1'})
    out = tmp_path / 'check.txt'
    assert engine.main(['--payload', str(payload(tmp_path)), '--check-app-update', str(out),
                        '--download-cache', str(tmp_path / 'cache')]) == 0
    assert calls == ['0.13.0']
    assert out.read_text().splitlines() == ['current=0.13.0', 'tag=v0.13.1', 'version=0.13.1',
                                            f'url=https://github.com/{engine.releases.REPO}/releases/tag/v0.13.1', 'cached=0']
    monkeypatch.setattr(engine.releases, 'newest_app', lambda *a: None)
    assert engine.main(['--payload', str(payload(tmp_path / 'b')), '--check-app-update', str(out),
                        '--download-cache', str(tmp_path / 'cache')]) == 0
    assert out.read_text() == 'current=0.13.0\n'


def test_engine_stage_app_moves_only_a_verified_bundle_and_never_over_a_folder(tmp_path, monkeypatch):
    scratch = tmp_path / 'scratch'
    (scratch / releases.APP_BUNDLE / 'Contents').mkdir(parents=True)
    monkeypatch.setattr(engine.releases, 'select_app', lambda *a: (scratch, scratch / releases.APP_BUNDLE))
    stage = tmp_path / '.Def Jam Recompiled.app.update'
    args = ['--payload', str(payload(tmp_path)), '--stage-app', 'v0.13.1', '--stage-dir', str(stage),
            '--download-cache', str(tmp_path / 'cache')]
    assert engine.main(args) == 0
    assert (stage / 'Contents').is_dir() and not scratch.exists()
    assert engine.main(args) == 2                           # an existing folder is never replaced


# ---- Windows: one program with the setup payload ZIP inside it as a resource ------------------------

WINDOWS = 'windows-x64'


def synthetic_pe(resources):
    """A minimal PE32+ image whose .rsrc section holds `resources` ({(type, id): bytes}, one language
    each): enough for releases.pe_resource, which reads only the headers and the resource tree."""
    va, raw = 0x1000, 0x400
    types = sorted({t for t, _ in resources})
    body = bytearray()

    def directory(entries):                         # entries: [(id, offset | subdirectory flag)]
        out = bytearray(16)
        out[14:16] = len(entries).to_bytes(2, 'little')
        for ident, target in entries:
            out += ident.to_bytes(4, 'little') + target.to_bytes(4, 'little')
        return out
    # Lay out: root, then per type a directory, per id a directory, per id a data entry, then data.
    root_size = 16 + 8 * len(types)
    layout, at = {}, root_size
    for t in types:
        ids = sorted(i for tt, i in resources if tt == t)
        layout[t] = at
        at += 16 + 8 * len(ids)
        for i in ids:
            layout[(t, i)] = at
            at += 16 + 8
    for key in resources:
        layout[('entry',) + key] = at
        at += 16
    for key in resources:
        layout[('data',) + key] = at
        at += (len(resources[key]) + 7) & ~7
    body = bytearray(at)
    body[0:root_size] = directory([(t, 0x80000000 | layout[t]) for t in types])
    for t in types:
        ids = sorted(i for tt, i in resources if tt == t)
        d = directory([(i, 0x80000000 | layout[(t, i)]) for i in ids])
        body[layout[t]:layout[t] + len(d)] = d
        for i in ids:
            d = directory([(0x409, layout[('entry', t, i)])])
            body[layout[(t, i)]:layout[(t, i)] + len(d)] = d
    for key, data in resources.items():
        e = layout[('entry',) + key]
        body[e:e + 8] = (va + layout[('data',) + key]).to_bytes(4, 'little') + len(data).to_bytes(4, 'little')
        body[layout[('data',) + key]:layout[('data',) + key] + len(data)] = data
    pe = 0x80
    head = bytearray(raw)
    head[0:2] = b'MZ'
    head[0x3C:0x40] = pe.to_bytes(4, 'little')
    head[pe:pe + 4] = b'PE\0\0'
    coff = pe + 4
    head[coff:coff + 2] = (0x8664).to_bytes(2, 'little')
    head[coff + 2:coff + 4] = (1).to_bytes(2, 'little')             # one section
    head[coff + 16:coff + 18] = (240).to_bytes(2, 'little')         # optional header size
    opt = coff + 20
    head[opt:opt + 2] = (0x20B).to_bytes(2, 'little')
    head[opt + 108:opt + 112] = (16).to_bytes(4, 'little')          # data directories
    head[opt + 112 + 16:opt + 112 + 24] = va.to_bytes(4, 'little') + len(body).to_bytes(4, 'little')
    sec = opt + 240
    head[sec:sec + 8] = b'.rsrc\0\0\0'
    head[sec + 8:sec + 24] = b''.join(x.to_bytes(4, 'little') for x in (len(body), va, len(body), raw))
    return bytes(head + body)


def payload_zip(manifest):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w') as z:
        z.writestr('manifest.json', json.dumps(manifest))
        z.writestr('source/x.c', '/* synthetic */')
    return out.getvalue()


def test_pe_resource_reads_a_resource_from_the_tables_and_refuses_the_rest():
    image = synthetic_pe({(10, 101): b'payload bytes', (16, 1): b'version'})
    assert releases.pe_resource(image, 10, 101) == b'payload bytes'
    assert releases.pe_resource(image, 16, 1) == b'version'
    for bad in (lambda: releases.pe_resource(image, 10, 102), lambda: releases.pe_resource(image[:600], 10, 101),
                lambda: releases.pe_resource(b'not a program', 10, 101)):
        with pytest.raises(ValueError):
            bad()


def windows_release(v, program, provenance):
    tag = 'v' + v
    stem = f'DefJamRecompiled-{v}-{WINDOWS}'
    digest = hashlib.sha256(program).hexdigest()
    files, assets = {}, []
    for i, (name, data) in enumerate(((stem + '.exe', program), (stem + '.sha256', f'{digest}  {stem}.exe\n'.encode()),
                                      (stem + '.provenance.json', json.dumps(provenance).encode())), start=10):
        a = asset(tag, name, data, i)
        assets.append(a)
        files[a['browser_download_url']] = data
    return dict(tag_name=tag, body='notes', assets=assets), files


@pytest.mark.parametrize('case', ['good', 'payload-mismatch', 'no-payload', 'manifest-mismatch'])
def test_select_app_on_windows_reads_the_payload_out_of_the_program(tmp_path, monkeypatch, case):
    manifest = dict(MANIFEST, platform=WINDOWS)
    archive = payload_zip(manifest)
    program = synthetic_pe({} if case == 'no-payload' else {releases.PAYLOAD_RESOURCE: archive})
    prov = dict(manifest, kind='app', app_sha256=hashlib.sha256(program).hexdigest(),
                payload_sha256=hashlib.sha256(archive).hexdigest())
    if case == 'payload-mismatch':
        prov['payload_sha256'] = 'f' * 64
    if case == 'manifest-mismatch':
        prov['toolkit_commit'] = 'e' * 40
    serve(monkeypatch, [windows_release('0.13.1', program, prov)])
    verify = lambda payload: json.loads((payload / 'manifest.json').read_text())
    if case != 'good':
        with pytest.raises(ValueError):
            releases.select_app('v0.13.1', WINDOWS, tmp_path, verify, lambda: None, lambda text: None)
        assert not list(tmp_path.glob('app-*'))
        return
    scratch, bundle = releases.select_app('v0.13.1', WINDOWS, tmp_path, verify, lambda: None, lambda text: None)
    assert sorted(p.name for p in bundle.iterdir()) == [releases.APP_PROGRAM_WINDOWS, 'payload', 'payload.sha256']
    assert (bundle / 'payload.sha256').read_text().strip() == prov['payload_sha256']
    assert (bundle / releases.APP_PROGRAM_WINDOWS).read_bytes() == program
    assert json.loads((bundle / 'payload/manifest.json').read_text()) == manifest
