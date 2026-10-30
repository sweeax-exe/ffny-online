"""scripts/save-import.py: re-signing Xbox profiles for the port, on synthetic data only."""
import importlib.util
import re
import struct
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('save_import', ROOT / 'scripts' / 'save-import.py')
si = importlib.util.module_from_spec(spec)
spec.loader.exec_module(si)

CONSOLE_KEY = bytes(range(1, 17))      # stands in for the key of the console that made a save


def meta(name):
    return b'\xff\xfe' + ('Name=%s\r\n' % name).encode('utf-16le')


def profile_file(key, payload_fill=7, size=2000):
    """A synthetic 20CM profile file signed with key."""
    blob = bytearray(size)
    blob[:4] = si.MAGIC
    blob[4:16] = struct.pack('<III', size, 0x40, size - si.IDENTITY_END)
    blob[si.HEADER:si.IDENTITY_END] = b'DJFFNY::User 17 -1'.ljust(64, b'\0')
    blob[si.IDENTITY_END:] = bytes([payload_fill]) * (size - si.IDENTITY_END)
    return si.resign(bytes(blob), key)


def make_zip(path, folder='11540F4914CA', name='JAYFNG', extra=True):
    with zipfile.ZipFile(path, 'w') as zf:
        base = 'UDATA/45410049/'
        zf.writestr(base + 'TitleMeta.xbx', b'TITLE')
        zf.writestr(base + 'TitleImage.xbx', b'IMAGE')
        zf.writestr(base + folder + '/SaveMeta.xbx', meta(name))
        zf.writestr(base + folder + '/' + name, profile_file(CONSOLE_KEY))
        zf.writestr(base + folder + '/saveimage.xbx', b'SAVEIMG')
        if extra:
            zf.writestr(base + 'datelinfo.xbx', b'Savename=x')
            zf.writestr('../escape/SaveMeta.xbx', meta('EVIL'))
            zf.writestr('/abs/SaveMeta.xbx', meta('EVIL'))


def run(*argv):
    return si.main([str(a) for a in argv])


def test_folder_names_match_the_consoles():
    # directory names real console saves use for these profile names
    assert si.folder_name('JAYFNG') == '11540F4914CA'
    assert si.folder_name('PLAYER1') == '11C90F515484'
    assert si.folder_name('Options') == '1A3F1B2E4ADD'


def test_header_checks():
    good = profile_file(CONSOLE_KEY)
    assert si.header_fault(good) is None
    assert 'not a 20CM' in si.header_fault(b'XXXX' + good[4:])
    assert 'size field' in si.header_fault(good[:-1])
    assert si.header_fault(good[:10]) == 'too short'


def test_signatures_only_verify_under_their_key():
    blob = profile_file(CONSOLE_KEY)
    assert all(si.signatures_ok(blob, CONSOLE_KEY))
    assert not any(si.signatures_ok(blob, si.PORT_KEY))
    assert all(si.signatures_ok(si.resign(blob, si.PORT_KEY), si.PORT_KEY))


def test_resign_changes_only_the_three_signature_fields():
    blob = profile_file(CONSOLE_KEY)
    out = si.resign(blob, si.PORT_KEY)
    assert out[:0x10] == blob[:0x10] and out[si.HEADER:] == blob[si.HEADER:]
    assert out[0x10:si.HEADER] != blob[0x10:si.HEADER]


def test_import_zip_copies_and_resigns(tmp_path):
    z = tmp_path / 'saves.zip'
    make_zip(z)
    save = tmp_path / 'save'
    assert run(z, '--save-dir', save) == 0
    title = save / 'UserData' / '45410049'
    out = (title / '11540F4914CA' / 'JAYFNG').read_bytes()
    assert all(si.signatures_ok(out, si.PORT_KEY))
    assert (title / '11540F4914CA' / 'SaveMeta.xbx').read_bytes() == meta('JAYFNG')
    assert (title / '11540F4914CA' / 'saveimage.xbx').read_bytes() == b'SAVEIMG'
    assert (title / 'TitleMeta.xbx').read_bytes() == b'TITLE'
    assert not (title / 'datelinfo.xbx').exists()
    assert {p.name for p in title.iterdir()} == {'11540F4914CA', 'TitleMeta.xbx', 'TitleImage.xbx'}
    assert not (tmp_path / 'escape').exists() and not list(tmp_path.rglob('EVIL*'))


def test_wrongly_named_folder_gets_the_games_name(tmp_path):
    z = tmp_path / 'saves.zip'
    make_zip(z, folder='SOMETHING', extra=False)
    save = tmp_path / 'save'
    assert run(z, '--save-dir', save) == 0
    assert (save / 'UserData/45410049/11540F4914CA/JAYFNG').is_file()


def test_existing_profile_kept_unless_forced_and_then_backed_up(tmp_path):
    z = tmp_path / 'saves.zip'
    make_zip(z, extra=False)
    save = tmp_path / 'save'
    existing = save / 'UserData/45410049/11540F4914CA'
    existing.mkdir(parents=True)
    (existing / 'JAYFNG').write_bytes(b'mine')
    assert run(z, '--save-dir', save) == 1
    assert (existing / 'JAYFNG').read_bytes() == b'mine'
    assert run(z, '--save-dir', save, '--force') == 0
    assert all(si.signatures_ok((existing / 'JAYFNG').read_bytes(), si.PORT_KEY))
    backups = list(save.glob('import-backup-*/11540F4914CA/JAYFNG'))
    assert len(backups) == 1 and backups[0].read_bytes() == b'mine'


def test_dry_run_and_check_write_nothing(tmp_path):
    z = tmp_path / 'saves.zip'
    make_zip(z, extra=False)
    save = tmp_path / 'save'
    assert run(z, '--save-dir', save, '--dry-run') == 0
    assert run(z, '--save-dir', save, '--check') == 0
    assert not save.exists()


def test_folder_source_and_bad_profile_skipped(tmp_path):
    src = tmp_path / 'UDATA' / '45410049'
    good = src / '11540F4914CA'
    bad = src / '0000000000AA'
    good.mkdir(parents=True)
    bad.mkdir(parents=True)
    (good / 'SaveMeta.xbx').write_bytes(meta('JAYFNG'))
    (good / 'JAYFNG').write_bytes(profile_file(CONSOLE_KEY))
    (bad / 'SaveMeta.xbx').write_bytes(meta('BROKEN'))
    (bad / 'BROKEN').write_bytes(b'not a profile')
    save = tmp_path / 'save'
    assert run(src, '--save-dir', save) == 0
    assert [p.name for p in (save / 'UserData/45410049').iterdir() if p.is_dir()] == ['11540F4914CA']


def test_save_area_from_environment(tmp_path, monkeypatch):
    z = tmp_path / 'saves.zip'
    make_zip(z, extra=False)
    monkeypatch.setenv('DEFJAM_DATA', str(tmp_path / 'data'))
    assert run(z) == 0
    assert (tmp_path / 'data/save/UserData/45410049/11540F4914CA/JAYFNG').is_file()


def test_custom_key(tmp_path):
    z = tmp_path / 'saves.zip'
    make_zip(z, extra=False)
    key = bytes(range(16))
    assert run(z, '--save-dir', tmp_path / 's', '--key', key.hex()) == 0
    out = (tmp_path / 's/UserData/45410049/11540F4914CA/JAYFNG').read_bytes()
    assert all(si.signatures_ok(out, key))
    with pytest.raises(SystemExit):
        run(z, '--save-dir', tmp_path / 's2', '--key', 'zz')


def test_port_key_is_the_kernels_zero_key():
    """The tool signs with zeros because the kernel exports XboxSignatureKey as zeros; if that
    changes, saves imported by this tool stop verifying, so change PORT_KEY with it."""
    kernel = ROOT / 'tools/xboxrecomp/src/kernel/kernel_xbox.c'
    if not kernel.is_file():
        pytest.skip('toolkit submodule not checked out')
    text = kernel.read_text(encoding='utf-8')
    assert re.search(r'xbox_SignatureKey\[16\]\s*=\s*\{0\}', text)
    assert si.PORT_KEY == bytes(16)
