"""Synthetic-only preservation checks for the private artwork verifier."""
import hashlib
import importlib.util
from pathlib import Path
import pytest

spec = importlib.util.spec_from_file_location('menu_artwork', Path(__file__).resolve().parents[2]/'scripts/verify-menu-ui-artwork.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def catalogue(name, data=b'synthetic'):
    return {'schema': 1, 'approval_records': [{'files': [{'path': name, 'sha256': hashlib.sha256(data).hexdigest()}]}]}


def test_integrity_and_tampering(tmp_path):
    path = tmp_path/'synthetic.txt'
    path.write_bytes(b'synthetic')
    assert mod.verify(catalogue(path.name), tmp_path) == (1, [])
    path.write_bytes(b'changed')
    count, errors = mod.verify(catalogue(path.name), tmp_path)
    assert count == 1 and errors == ['synthetic.txt: SHA-256 mismatch']
    path.unlink()
    assert len(mod.verify(catalogue(path.name), tmp_path)[1]) == 1


@pytest.mark.parametrize('name', ['../outside.txt', '/absolute.txt'])
def test_paths_cannot_escape_root(tmp_path, name):
    with pytest.raises(ValueError, match='escapes'):
        mod.verify(catalogue(name), tmp_path)


def test_conflicting_pins_are_rejected(tmp_path):
    data = catalogue('same.txt')
    data['approval_records'] += catalogue('same.txt', b'different')['approval_records']
    with pytest.raises(ValueError, match='Conflicting'):
        mod.verify(data, tmp_path)
