"""scripts/codesign-setup.py: the parts that need no signtool (PE hashing, payload rewrite, hash patch)."""
import importlib.util
import io
import json
from pathlib import Path
import struct
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('codesign_setup', ROOT / 'scripts/codesign-setup.py')
codesign = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codesign)


def tiny_pe(body=b'\x90' * 0x200, cert=b''):
    """A minimal PE32+ layout: DOS header, PE signature, COFF, optional header with 16 directories,
    one section. Enough for the offsets Authenticode hashing uses."""
    e = 0x40
    optional_size = 112 + 16 * 8
    header = bytearray(0x200)
    header[:2] = b'MZ'
    struct.pack_into('<I', header, 0x3C, e)
    header[e:e + 4] = b'PE\0\0'
    struct.pack_into('<HH', header, e + 4, 0x8664, 1)
    struct.pack_into('<H', header, e + 4 + 16, optional_size)
    optional = e + 24
    struct.pack_into('<H', header, optional, 0x20B)
    section = optional + optional_size
    header[section:section + 8] = b'.text\0\0\0'
    struct.pack_into('<IIII', header, section + 8, len(body), 0x1000, len(body), 0x200)
    data = bytes(header) + body
    if cert:
        security = optional + 112 + 4 * 8
        padded = data + b'\0' * (-len(data) % 8)
        data = bytearray(padded + cert)
        struct.pack_into('<II', data, security, len(padded), len(cert))
        struct.pack_into('<I', data, optional + 64, 0x1234)      # signtool also refreshes the checksum
        data = bytes(data)
    return data


def test_authenticode_hash_ignores_only_the_signature():
    unsigned, signed = tiny_pe(), tiny_pe(cert=b'CERT' * 10)
    assert not codesign.is_signed(unsigned) and codesign.is_signed(signed)
    assert codesign.authenticode_hash(unsigned) == codesign.authenticode_hash(signed)
    changed = bytearray(signed)
    changed[0x200] ^= 1                                           # one code byte
    assert codesign.authenticode_hash(bytes(changed)) != codesign.authenticode_hash(unsigned)


def test_authenticode_hash_pads_like_signtool():
    odd = tiny_pe(body=b'\x90' * 0x1FD)
    signed = tiny_pe(body=b'\x90' * 0x1FD, cert=b'X' * 16)
    assert codesign.authenticode_hash(odd) == codesign.authenticode_hash(signed)


def zip_bytes(entries):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as package:
        for name, data in entries:
            info = zipfile.ZipInfo(name, (2026, 11, 22, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            package.writestr(info, data)
    return out.getvalue()


def test_rewrite_payload_keeps_order_and_timestamps():
    old = zip_bytes([('manifest.json', b'{}'), ('DefJamLauncher.exe', b'old'), ('python/python.exe', b'py')])
    new = codesign.rewrite_payload(old, {'DefJamLauncher.exe': b'signed', 'manifest.json': b'{"x": 1}'})
    with zipfile.ZipFile(io.BytesIO(old)) as a, zipfile.ZipFile(io.BytesIO(new)) as b:
        assert a.namelist() == b.namelist()
        assert [i.date_time for i in a.infolist()] == [i.date_time for i in b.infolist()]
        assert b.read('DefJamLauncher.exe') == b'signed' and b.read('python/python.exe') == b'py'
        assert json.loads(b.read('manifest.json')) == {'x': 1}
    with pytest.raises(ValueError):
        codesign.rewrite_payload(old, {'missing.exe': b''})


def test_patch_hash_string_replaces_exactly_one_copy():
    old, new = 'a' * 64, 'b' * 64
    data = b'head' + old.encode('utf-16-le') + b'tail'
    patched, at = codesign.patch_hash_string(data, old, new)
    assert patched == b'head' + new.encode('utf-16-le') + b'tail' and at == 4
    with pytest.raises(ValueError):
        codesign.patch_hash_string(data + old.encode('utf-16-le'), old, new)
    with pytest.raises(ValueError):
        codesign.patch_hash_string(b'nothing', old, new)
