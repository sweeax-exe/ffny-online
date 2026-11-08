"""The app's release asset (scripts/check-setup-assets.py --app, D215): the exact names, and an
archive that holds the approved bundle files and, byte for byte, the inspected setup payload,
with a program that carries no lifted game code and loads only what it carries. Game-free."""
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('app_package_assets', ROOT / 'scripts/check-setup-assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)
BUNDLE = assets.releases.APP_BUNDLE


@pytest.mark.parametrize('platform', assets.APP_PLATFORMS)
def test_releases_require_only_the_app_checksum_and_provenance(platform):
    version = '1.1.0'
    setup_stem = f'DefJamSetup-{version}-{platform}'
    setup = [setup_stem + ext for ext in (assets.INSTALLER_EXTENSION[platform], '.sha256', '.provenance.json')]
    app_stem = assets.app_stem(version, platform)
    app = [assets.releases.APP_ASSETS[platform].format(v=version), app_stem + '.sha256', app_stem + '.provenance.json']
    assets.validate_asset_names(version, app, platform, app=True)
    assets.validate_asset_names(version, setup, platform)  # local standalone packaging remains available
    for names in ([], setup, app[:2], setup + app, app + ['defjam_recomp']):
        with pytest.raises(ValueError):
            assets.validate_asset_names(version, names, platform, app=True)
    with pytest.raises(ValueError):
        assets.validate_asset_names(version, setup + app, platform)


PAYLOAD = {'manifest.json': b'{}', 'source/a.c': b'/* synthetic */', 'python/bin/python3': b'synthetic'}


def payload_zip(tmp_path, files=PAYLOAD):
    path = tmp_path / 'setup-payload.zip'
    with zipfile.ZipFile(path, 'w') as z:
        for name, data in files.items():
            z.writestr(name, data)
    return path


def app_zip(tmp_path, payload=PAYLOAD, extra=None, program=True, root=BUNDLE):
    path = tmp_path / 'app.zip'
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr(root + '/Contents/Info.plist', b'<plist/>')
        if program:
            item = zipfile.ZipInfo(root + '/Contents/MacOS/defjam_app')
            item.external_attr = 0o100755 << 16
            z.writestr(item, b'synthetic program')
        for name, data in payload.items():
            z.writestr(root + '/Contents/Resources/payload/' + name, data)
        for name in extra or ():
            z.writestr(name, b'extra')
    return path


def test_an_app_holding_exactly_the_payload_passes(tmp_path):
    assets.inspect_app(app_zip(tmp_path), payload_zip(tmp_path), binary_checks=False)


@pytest.mark.parametrize('case', ['changed-payload-file', 'missing-payload-file', 'extra-payload-file',
                                  'extra-bundle-file', 'second-root', 'no-program', 'other-name'])
def test_anything_else_in_the_app_is_refused(tmp_path, case):
    payload = dict(PAYLOAD)
    extra = None
    if case == 'changed-payload-file':
        payload['source/a.c'] = b'/* changed */'
    if case == 'missing-payload-file':
        del payload['source/a.c']
    if case == 'extra-payload-file':
        payload['source/gen/recomp_0000.c'] = b'lifted'
    if case == 'extra-bundle-file':
        extra = [BUNDLE + '/Contents/MacOS/defjam_recomp']
    if case == 'second-root':
        extra = ['README.txt']
    archive = app_zip(tmp_path, payload, extra, program=case != 'no-program',
                      root='Other.app' if case == 'other-name' else BUNDLE)
    with pytest.raises(ValueError):
        assets.inspect_app(archive, payload_zip(tmp_path), binary_checks=False)


@pytest.mark.skipif(sys.platform != 'darwin' or not shutil.which('clang'), reason='Mach-O checks need macOS')
@pytest.mark.parametrize('source,ok', [('int main(void) { return 0; }', True),
                                       ('void sub_00012345(void) {} int main(void) { sub_00012345(); return 0; }', False)])
def test_the_program_must_carry_no_lifted_code(tmp_path, source, ok):
    (tmp_path / 'p.c').write_text(source)
    program = tmp_path / 'defjam_app'
    subprocess.run(['clang', '-o', str(program), str(tmp_path / 'p.c')], check=True)
    out = io.BytesIO()
    with zipfile.ZipFile(tmp_path / 'app.zip', 'w') as z:
        z.writestr(BUNDLE + '/Contents/Info.plist', b'<plist/>')
        item = zipfile.ZipInfo(BUNDLE + '/Contents/MacOS/defjam_app')
        item.external_attr = 0o100755 << 16
        z.writestr(item, program.read_bytes())
        for name, data in PAYLOAD.items():
            z.writestr(BUNDLE + '/Contents/Resources/payload/' + name, data)
    if ok:
        assets.inspect_app(tmp_path / 'app.zip', payload_zip(tmp_path), binary_checks=True)
    else:
        with pytest.raises(ValueError, match='lifted'):
            assets.inspect_app(tmp_path / 'app.zip', payload_zip(tmp_path), binary_checks=True)


# ---- Windows: the program with the payload built in (resource 101) ---------------------------------

@pytest.mark.parametrize('platform,setup_check,app_check', [
    ('windows-x64', 'verify_embedded_payload', 'inspect_app_windows'),
    ('macos-arm64', 'verify_embedded_payload_macos', 'inspect_app'),
    ('linux-x64', 'verify_embedded_payload_linux', 'inspect_app_linux'),
])
def test_packaging_inspects_both_payloads_but_emits_only_app_assets(tmp_path, monkeypatch,
                                                                 platform, setup_check, app_check):
    import hashlib
    import json

    payload = tmp_path / 'payload.zip'
    payload.write_bytes(b'synthetic payload')
    installer = tmp_path / ('setup' + assets.INSTALLER_EXTENSION[platform])
    installer.write_bytes(b'synthetic legacy setup')
    app = tmp_path / 'app'
    app.write_bytes(b'synthetic game-free app')
    output = tmp_path / 'assets'
    monkeypatch.setattr(assets, 'inspect_payload', lambda p: {'version': '1.1.0', 'platform': platform})
    inspected = []
    monkeypatch.setattr(assets, setup_check, lambda p, archive: inspected.append(('setup', p, archive)))
    monkeypatch.setattr(assets, app_check, lambda p, archive: inspected.append(('app', p, archive)))

    assert assets.main(['--payload', str(payload), '--installer', str(installer), '--app', str(app),
                        '--output-dir', str(output)]) == 0
    assert inspected == [('setup', installer, payload), ('app', app, payload)]
    name = assets.releases.APP_ASSETS[platform].format(v='1.1.0')
    stem = assets.app_stem('1.1.0', platform)
    assert sorted(p.name for p in output.iterdir()) == sorted([name, stem + '.sha256', stem + '.provenance.json'])
    assert (output / name).read_bytes() == app.read_bytes()
    digest = hashlib.sha256(app.read_bytes()).hexdigest()
    assert (output / (stem + '.sha256')).read_text().split() == [digest, name]
    provenance = json.loads((output / (stem + '.provenance.json')).read_text())
    assert provenance['kind'] == 'app' and provenance['app_sha256'] == digest
    assert provenance['payload_sha256'] == hashlib.sha256(payload.read_bytes()).hexdigest()


def windows_program(tmp_path, payload, extra=b''):
    from test_app_updates import synthetic_pe
    program = tmp_path / 'DefJamRecompiled.exe'
    program.write_bytes(synthetic_pe({assets.releases.PAYLOAD_RESOURCE: payload}) + extra)
    return program


def test_the_windows_app_carries_exactly_the_inspected_payload(tmp_path):
    archive = payload_zip(tmp_path)
    assets.inspect_app_windows(windows_program(tmp_path, archive.read_bytes()), archive)
    with pytest.raises(ValueError):                                     # another payload
        assets.inspect_app_windows(windows_program(tmp_path, archive.read_bytes() + b'x'), archive)
    with pytest.raises(ValueError):                                     # lifted code
        assets.inspect_app_windows(windows_program(tmp_path, archive.read_bytes(), b'sub_0001A4E0'), archive)


@pytest.mark.skipif(sys.platform != 'win32', reason='reads a Windows system program')
def test_pe_imports_lists_a_real_programs_libraries():
    names = assets.pe_imports((Path(os.environ['SystemRoot']) / 'System32/cmd.exe').read_bytes())
    assert 'kernel32.dll' in names or any(n.startswith('api-ms-win-') for n in names)
