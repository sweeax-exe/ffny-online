"""The Linux app's AppImage (design §6): the squashfs reader the updater and the asset checker use
(setup/releases.py read_appimage), the AppImage scripts/package-appimage.py assembles, the update
that stages it (select_app on linux-x64) and the release check (check-setup-assets.py
inspect_app_linux). Images are made with mksquashfs; a test is skipped where it is missing. Game-free."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
MKSQUASHFS = shutil.which('mksquashfs')
needs_mksquashfs = pytest.mark.skipif(not MKSQUASHFS, reason='mksquashfs (squashfs-tools) is not installed')


def load(name, path):
    spec = importlib.util.spec_from_file_location('test_appimage_' + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


releases = load('releases', ROOT / 'setup/releases.py')
package = load('package', ROOT / 'scripts/package-appimage.py')
assets = load('assets', ROOT / 'scripts/check-setup-assets.py')
updates = load('updates', ROOT / 'tests/unit/test_app_updates.py')
PLATFORM = 'linux-x64'
MANIFEST = {'schema': 1, 'version': '0.13.1', 'platform': PLATFORM, 'source_commit': 'a' * 40,
            'toolkit_commit': 'b' * 40, 'updater': releases.CONTRACT}


def fake_runtime():
    """The smallest program appimage_offset accepts: an ELF header marked as a type 2 AppImage with
    one empty section header after it, so the image starts at byte 128."""
    header = bytearray(64)
    header[:4], header[4], header[5], header[8:11] = b'\x7fELF', 2, 1, b'AI\x02'
    struct.pack_into('<Q', header, 0x28, 64)            # e_shoff
    struct.pack_into('<HH', header, 0x3A, 64, 1)        # e_shentsize, e_shnum
    return bytes(header) + bytes(64)


def squash(tree, out, compressor='gzip'):
    subprocess.run([MKSQUASHFS, str(tree), str(out), '-comp', compressor, '-root-owned', '-noappend', '-no-xattrs',
                    '-quiet', '-no-progress'], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out.read_bytes()


def payload_tree(folder, version='0.13.1'):
    """A small payload: a manifest whose files list matches, one executable and enough files for a
    directory index."""
    files = {'python/bin/python3': b'#!/bin/sh\n', 'engine/engine.py': b'print(1)\n',
             'source/big.bin': os.urandom(200000) + bytes(300000)}
    for i in range(300):
        files['source/many/f%03d.txt' % i] = ('x' * i).encode()
    for name, data in files.items():
        path = folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (folder / 'python/bin/python3').chmod(0o755)
    manifest = dict(MANIFEST, version=version, files={n: hashlib.sha256(d).hexdigest() for n, d in files.items()})
    (folder / 'manifest.json').write_text(json.dumps(manifest))
    return manifest


def app_tree(folder, version='0.13.1'):
    folder.mkdir()
    (folder / 'defjam_app').write_bytes(b'\x7fELF synthetic app')
    (folder / 'defjam_app').chmod(0o755)
    os.symlink('defjam_app', folder / 'AppRun')
    (folder / 'DefJamRecompiled.desktop').write_text('[Desktop Entry]\n')
    (folder / 'DefJamRecompiled.png').write_bytes(b'png')
    os.symlink('DefJamRecompiled.png', folder / '.DirIcon')
    return payload_tree(folder / 'payload', version)


def appimage(tmp_path, change=None, version='0.13.1'):
    tree = tmp_path / ('tree-' + version)
    manifest = app_tree(tree, version)
    if change:
        change(tree)
    return fake_runtime() + squash(tree, tmp_path / ('image-' + version)), manifest, tree


@needs_mksquashfs
def test_reader_matches_every_file_and_mode(tmp_path):
    data, _, tree = appimage(tmp_path)
    image, entries = releases.read_appimage(data)
    files = [p for p in tree.rglob('*') if p.is_file() and not p.is_symlink()]
    assert len([e for e in entries.values() if e[0] == 'file']) == len(files)
    for path in files:
        kind, mode, details = entries[path.relative_to(tree).as_posix()]
        assert image.read(details) == path.read_bytes()
        assert mode & 0o777 == path.stat().st_mode & 0o777
    assert entries['AppRun'] == ('link', entries['AppRun'][1], 'defjam_app')


@needs_mksquashfs
def test_payload_unpacks_with_execute_bits(tmp_path):
    data, _, tree = appimage(tmp_path)
    releases.appimage_payload(data, tmp_path / 'out')
    assert (tmp_path / 'out/source/big.bin').read_bytes() == (tree / 'payload/source/big.bin').read_bytes()
    assert os.access(tmp_path / 'out/python/bin/python3', os.X_OK)
    assert not os.access(tmp_path / 'out/engine/engine.py', os.X_OK)


@needs_mksquashfs
@pytest.mark.parametrize('change, message', [
    (lambda t: (t / 'extra.txt').write_text('x'), 'Unexpected file'),
    (lambda t: os.symlink('/etc/passwd', t / 'payload/source/link'), 'link in its setup files'),
    (lambda t: ((t / 'AppRun').unlink(), os.symlink('/bin/sh', t / 'AppRun')), 'Unexpected link'),
    (lambda t: (t / 'DefJamRecompiled.desktop').unlink(), 'incomplete'),
])
def test_reader_refuses_an_image_of_another_shape(tmp_path, change, message):
    data, _, _ = appimage(tmp_path, change)
    with pytest.raises(ValueError, match=message):
        releases.read_appimage(data)


@needs_mksquashfs
def test_reader_refuses_other_compression_and_plain_programs(tmp_path):
    tree = tmp_path / 'tree'
    app_tree(tree)
    try:
        data = fake_runtime() + squash(tree, tmp_path / 'zstd', 'zstd')
    except subprocess.CalledProcessError:
        pytest.skip('this mksquashfs has no zstd')
    with pytest.raises(ValueError, match='gzip'):
        releases.read_appimage(data)
    plain = bytearray(fake_runtime())
    plain[8:11] = b'\0\0\0'
    with pytest.raises(ValueError, match='type 2 AppImage'):
        releases.appimage_offset(bytes(plain))


@needs_mksquashfs
def test_package_appimage_assembles_the_approved_shape(tmp_path, monkeypatch):
    payload = tmp_path / 'payload'
    payload_tree(payload)
    (tmp_path / 'runtime').write_bytes(fake_runtime())
    monkeypatch.setattr(package, 'runtime', lambda cache: tmp_path / 'runtime')
    monkeypatch.setattr(package, 'check_program', lambda program: None)
    program = tmp_path / 'defjam_app'
    program.write_bytes(b'\x7fELF app')
    out = package.package(program, payload, tmp_path / 'DefJamRecompiled.AppImage', tmp_path)
    assert os.access(out, os.X_OK) and out.read_bytes().startswith(fake_runtime())
    image, entries = releases.read_appimage(out.read_bytes())
    assert image.read(entries['defjam_app'][2]) == b'\x7fELF app'
    assert 'X-AppImage-Version=0.13.1' in image.read(entries['DefJamRecompiled.desktop'][2]).decode()
    assert image.read(entries['payload/source/big.bin'][2]) == (payload / 'source/big.bin').read_bytes()


@pytest.mark.skipif(not shutil.which('readelf'), reason='readelf is not installed')
def test_check_program_refuses_lifted_code_and_libraries_it_does_not_carry(tmp_path):
    true = Path(shutil.which('true')).resolve()
    if any(name not in package.SYSTEM_LIBRARIES for name in package.needed(true)):
        pytest.skip('this system\'s true loads more than the C library')
    package.check_program(true)
    lifted = tmp_path / 'lifted'
    lifted.write_bytes(true.read_bytes() + b'sub_0001A4E0')
    with pytest.raises(ValueError, match='lifted game code'):
        package.check_program(lifted)
    python = Path(os.path.realpath(shutil.which('python3')))
    if any(name not in package.SYSTEM_LIBRARIES for name in package.needed(python)):
        with pytest.raises(ValueError, match='does not carry'):
            package.check_program(python)


def release(v, data, provenance):
    """A GitHub release row with the Linux app's three assets, and the bytes behind each URL."""
    tag = 'v' + v
    digest = hashlib.sha256(data).hexdigest()
    stem = f'DefJamRecompiled-{v}-x86_64'
    files, rows = {}, []
    for i, (name, body) in enumerate(((stem + '.AppImage', data), (stem + '.sha256', f'{digest}  {stem}.AppImage\n'.encode()),
                                      (stem + '.provenance.json', json.dumps(provenance).encode())), start=10):
        a = updates.asset(tag, name, body, i)
        rows.append(a)
        files[a['browser_download_url']] = body
    return dict(tag_name=tag, body='notes', assets=rows), files


@needs_mksquashfs
def test_select_app_on_linux_verifies_the_payload_inside_and_stages_the_appimage(tmp_path, monkeypatch):
    data, manifest, _ = appimage(tmp_path)
    prov = dict(manifest, kind='app', app_sha256=hashlib.sha256(data).hexdigest())
    monkeypatch.setattr(updates, 'releases', releases)       # serve() patches this module's copy
    updates.serve(monkeypatch, [release('0.13.1', data, prov)])
    seen = []

    def verify(payload):
        seen.append(payload)
        return json.loads((payload / 'manifest.json').read_text())
    scratch, bundle = releases.select_app('v0.13.1', PLATFORM, tmp_path / 'cache', verify, lambda: None, lambda t: None)
    program = bundle / releases.APP_PROGRAM_LINUX
    assert sorted(p.name for p in bundle.iterdir()) == [releases.APP_PROGRAM_LINUX]
    assert program.read_bytes() == data and os.access(program, os.X_OK)
    assert seen == [scratch / 'payload'] and not (scratch / 'payload').exists()


@needs_mksquashfs
def test_select_app_on_linux_refuses_a_payload_from_another_build(tmp_path, monkeypatch):
    data, manifest, _ = appimage(tmp_path)
    prov = dict(manifest, kind='app', app_sha256=hashlib.sha256(data).hexdigest(), source_commit='c' * 40)
    monkeypatch.setattr(updates, 'releases', releases)       # serve() patches this module's copy
    updates.serve(monkeypatch, [release('0.13.1', data, prov)])
    cache = tmp_path / 'cache'
    with pytest.raises(ValueError, match='source_commit'):
        releases.select_app('v0.13.1', PLATFORM, cache, lambda p: json.loads((p / 'manifest.json').read_text()),
                            lambda: None, lambda t: None)
    assert not [p for p in cache.iterdir() if p.name.startswith('app-')]


@needs_mksquashfs
def test_asset_check_compares_the_appimage_payload_with_the_inspected_one(tmp_path):
    data, _, tree = appimage(tmp_path)
    app = tmp_path / 'app.AppImage'
    app.write_bytes(data)
    archive = tmp_path / 'payload.zip'

    def write(skip=None, exec_bits=True):
        with zipfile.ZipFile(archive, 'w') as z:
            for path in sorted((tree / 'payload').rglob('*')):
                name = path.relative_to(tree / 'payload').as_posix()
                if path.is_file() and name != skip:
                    item = zipfile.ZipInfo(name)
                    item.external_attr = (path.stat().st_mode if exec_bits else 0o100644) << 16
                    z.writestr(item, path.read_bytes())
    write()
    assets.inspect_app_linux(app, archive, binary_checks=False)
    write(skip='engine/engine.py')
    with pytest.raises(ValueError, match='does not match'):
        assets.inspect_app_linux(app, archive, binary_checks=False)
    write()
    with zipfile.ZipFile(archive, 'a') as z:
        z.writestr('engine/extra.py', 'x')
    with pytest.raises(ValueError, match='does not match'):
        assets.inspect_app_linux(app, archive, binary_checks=False)


def test_linux_app_asset_names():
    # Since 1.0.0 a Linux release attaches only the AppImage, its checksum and its provenance.
    names = ['DefJamRecompiled-0.14.0-linux-x86_64.AppImage', 'DefJamRecompiled-0.14.0-linux-x86_64.sha256',
             'DefJamRecompiled-0.14.0-linux-x86_64.provenance.json']
    setup = ['DefJamSetup-0.14.0-linux-x64.tar.gz', 'DefJamSetup-0.14.0-linux-x64.sha256',
             'DefJamSetup-0.14.0-linux-x64.provenance.json']
    assets.validate_asset_names('0.14.0', names, PLATFORM, app=True)
    with pytest.raises(ValueError):
        assets.validate_asset_names('0.14.0', names + setup, PLATFORM, app=True)
    with pytest.raises(ValueError):
        assets.validate_asset_names('0.14.0', names + ['DefJamRecompiled-0.14.0-x86_64.zip'], PLATFORM, app=True)
