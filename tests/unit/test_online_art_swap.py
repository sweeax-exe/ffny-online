# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Build 110: the online screens' art folder survives an update another program disturbs (scripts/online-art.py
swap / merge / repair, setup/mods.py repair_online_art). Before, a file held open left half of the art or none."""
import errno
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
WINDOWS = os.name == 'nt'


@pytest.fixture
def art(monkeypatch):
    spec = importlib.util.spec_from_file_location('online_art', ROOT / 'scripts/online-art.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    monkeypatch.setattr(module, 'RETRIES', (0.01, 0.01))
    return module


def png(art, text):
    return art.PNG_HEAD + text.encode() + art.PNG_END


def fill(art, folder, text, version=None, extra=()):
    """A finished art folder (its pictures' bytes say which one it is)."""
    manifest = {'version': art.VERSION if version is None else version, 'fighters': [1, 55], 'venues': [2],
                'ui': ['online_poster'], 'modes': ['ffa'], 'fonts': ['TradeBld.ttf'], 'factions': ['crow'],
                'missing': [], 'ranks': 1, 'towers': 1}
    names = ['fighters/1.png', 'fighters/55.png', 'venues/2.png', 'ui/online_poster.png', 'modes/ffa.png',
             'factions/crow.png', 'ranks/em1-256.png', 'ranks/em1-64.png', 'towers/tower1-512.png',
             'towers/tower1-128.png'] + list(extra)
    for name in names:
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(png(art, text))
    (folder / 'fonts').mkdir(exist_ok=True)
    (folder / 'fonts/TradeBld.ttf').write_bytes(text.encode())
    (folder / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
    return folder


def files(folder):
    return {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()}


def folders(tmp_path):
    return sorted(p.name for p in tmp_path.iterdir() if p.name.startswith('online-art') and p.is_dir())


def is_new(folder):
    return b'new' in (folder / 'fighters/55.png').read_bytes()


def blocked(monkeypatch, art, refuse):
    """os.rename fails as Windows does (WinError 5 / 32) where refuse(source, target) says so."""
    real = os.rename

    def rename(a, b):
        if refuse(Path(a), Path(b)):
            raise PermissionError(errno.EACCES, 'held by another program', str(a))
        return real(a, b)
    monkeypatch.setattr(art.os, 'rename', rename)


def test_swap(art, tmp_path):
    out, tmp = fill(art, tmp_path / 'online-art', 'old'), fill(art, tmp_path / 'online-art.tmp', 'new')
    art.swap(tmp, out)
    assert is_new(out) and art.faults(out) == [] and folders(tmp_path) == ['online-art']


def test_first_install(art, tmp_path):
    out = tmp_path / 'online-art'
    art.swap(fill(art, tmp_path / 'online-art.tmp', 'new'), out)
    assert is_new(out) and folders(tmp_path) == ['online-art']


def test_held_old_folder_is_updated_file_by_file(art, tmp_path, monkeypatch):
    out, tmp = fill(art, tmp_path / 'online-art', 'old'), fill(art, tmp_path / 'online-art.tmp', 'new')
    blocked(monkeypatch, art, lambda a, b: a == out)
    art.swap(tmp, out)
    assert is_new(out) and art.faults(out) == [] and folders(tmp_path) == ['online-art']


@pytest.mark.skipif(not WINDOWS, reason='an open file blocks a rename on Windows only')
@pytest.mark.parametrize('held', ['fighters/55.png', 'venues/2.png', 'manifest.json'])
def test_really_held_old_file(art, tmp_path, held):
    out, tmp = fill(art, tmp_path / 'online-art', 'old', version=7), fill(art, tmp_path / 'online-art.tmp', 'new')
    before = files(out)
    with open(out / held, 'rb'):
        with pytest.raises(art.ArtError, match=held):
            art.swap(tmp, out)
        assert files(out) >= before                     # not one file fewer, all of them readable
        assert (out / held).read_bytes()
        assert art.faults(tmp) == []                    # the new art waits
    assert art.repair(tmp_path, out)[0] == 'repaired'   # the next start
    assert is_new(out) and art.faults(out) == [] and folders(tmp_path) == ['online-art']


@pytest.mark.skipif(not WINDOWS, reason='an open file blocks a rename on Windows only')
def test_really_held_new_file(art, tmp_path):
    """A virus scanner reads a file of the new art when it is put in place (WinError 5): no art at all before."""
    out, tmp = fill(art, tmp_path / 'online-art', 'old'), fill(art, tmp_path / 'online-art.tmp', 'new')
    with open(tmp / 'manifest.json', 'rb'):
        art.swap(tmp, out)
        assert is_new(out) and art.faults(out) == []
    assert art.repair(tmp_path, out)[0] == 'current'    # also clears what could not be removed
    assert folders(tmp_path) == ['online-art']


def test_new_folder_blocked_old_comes_back_first(art, tmp_path, monkeypatch):
    out, tmp = fill(art, tmp_path / 'online-art', 'old'), fill(art, tmp_path / 'online-art.tmp', 'new')
    blocked(monkeypatch, art, lambda a, b: a == tmp)

    def no_copy(*args):
        raise PermissionError(errno.EACCES, 'held', 'x')
    monkeypatch.setattr(art.shutil, 'copyfile', no_copy)
    with pytest.raises(art.ArtError):
        art.swap(tmp, out)
    assert art.faults(out) == [] and not is_new(out)    # the old art, whole
    assert art.faults(tmp) == []


def test_rollback_blocked_too(art, tmp_path, monkeypatch):
    out, tmp = fill(art, tmp_path / 'online-art', 'old'), fill(art, tmp_path / 'online-art.tmp', 'new')
    blocked(monkeypatch, art, lambda a, b: b == out)    # nothing can take the folder's name any more
    art.swap(tmp, out)
    assert is_new(out) and art.faults(out) == []        # copied


@pytest.mark.parametrize('left', [
    ('online-art.tmp',), ('online-art.tmp', 'online-art.old'), ('online-art.old',)])
def test_crash_between_the_renames(art, tmp_path, left):
    """The PC goes off after the old art moved aside: the next start has art again, without a dump too."""
    for name in left:
        fill(art, tmp_path / name, 'new' if 'tmp' in name else 'old')
    out = tmp_path / 'online-art'
    assert art.repair(tmp_path, out)[0] == 'repaired'
    assert art.faults(out) == [] and is_new(out) == ('online-art.tmp' in left)
    assert art.repair(tmp_path, out)[0] == 'current'
    assert folders(tmp_path) == ['online-art']


def test_the_lost_art_of_build_109_heals(art, tmp_path):
    """Build 109's stranded online-art.tmp, and an online-art the composer made again with one community portrait."""
    tmp = fill(art, tmp_path / 'online-art.tmp', 'new')
    out = tmp_path / 'online-art'
    (out / 'fighters').mkdir(parents=True)
    (out / 'fighters/1000.png').write_bytes(png(art, 'community'))
    assert art.repair(tmp_path, out)[0] == 'repaired'
    assert art.faults(out) == [] and (out / 'fighters/1000.png').is_file() and not tmp.exists()


@pytest.mark.parametrize('damage', ['manifest', 'missing', 'empty', 'zeros', 'cut', 'version'])
def test_a_damaged_tmp_is_not_taken(art, tmp_path, damage):
    tmp, out = fill(art, tmp_path / 'online-art.tmp', 'new'), tmp_path / 'online-art'
    picture = tmp / 'ui/online_poster.png'
    if damage == 'manifest':
        (tmp / 'manifest.json').unlink()
    elif damage == 'missing':
        picture.unlink()
    elif damage == 'empty':
        picture.write_bytes(b'')
    elif damage == 'zeros':
        picture.write_bytes(b'\0' * 400)
    elif damage == 'cut':
        picture.write_bytes(picture.read_bytes()[:-4])
    else:
        fill(art, tmp, 'new', version=art.VERSION - 1)
    assert art.faults(tmp)
    assert art.repair(tmp_path, out)[0] == 'nothing'    # no dump to make it from here
    assert not out.exists()


def test_incomplete_art_is_made_again(art, tmp_path, monkeypatch):
    out = fill(art, tmp_path / 'online-art', 'old')
    (out / 'fonts/TradeBld.ttf').unlink()
    for rel in ('screens/screens.viv', 'fonts/fonts.viv'):
        (tmp_path / 'extracted' / rel).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / 'extracted' / rel).write_bytes(b'x')
    monkeypatch.setattr(art, 'build', lambda data, o: art.swap(fill(art, tmp_path / 'online-art.tmp', 'new'), o))
    assert art.repair(tmp_path, out)[0] == 'repaired' and is_new(out)


def test_a_failed_rebuild_never_leaves_fewer_files(art, tmp_path, monkeypatch):
    """Whatever single step of the swap fails, and whatever fails with it: every file there before is there after."""
    steps = ['rename-out', 'rename-tmp', 'rename-back', 'copy', 'replace']
    for mask in range(1, 1 << len(steps)):
        bad = {s for i, s in enumerate(steps) if mask >> i & 1}
        case = tmp_path / str(mask)
        out, tmp = fill(art, case / 'online-art', 'old', extra=['skins/a.png']), case / 'online-art.tmp'
        fill(art, tmp, 'new', extra=['skins/a.png'])
        before = files(out)
        with monkeypatch.context() as m:
            def refuse(a, b):
                return ('rename-out' in bad and a == out or 'rename-tmp' in bad and a == tmp
                        or 'rename-back' in bad and b == out and a != tmp)
            blocked(m, art, refuse)
            real_copy, real_replace = art.shutil.copyfile, os.replace

            def copyfile(a, b):
                if 'copy' in bad:
                    raise OSError(errno.EACCES, 'held', str(a))
                return real_copy(a, b)

            def replace(a, b):
                if 'replace' in bad:
                    raise OSError(errno.EACCES, 'held', str(b))
                return real_replace(a, b)
            m.setattr(art.shutil, 'copyfile', copyfile)
            m.setattr(art.os, 'replace', replace)
            try:
                art.swap(tmp, out)
            except OSError:
                pass                                    # rename-back with nothing to copy: the repair below
            except art.ArtError:
                pass
            kept = files(out) if out.exists() else set()
            if not kept >= before:                      # nothing may touch the folder any more: aside, whole
                assert {'rename-tmp', 'rename-back'} <= bad and bad & {'copy', 'replace'}, bad
                assert any(files(p) >= before for p in art.sibling(out, 'old')), bad
        art.repair(case, out)                           # the next start
        assert files(out) >= before and art.faults(out) == [], bad


def test_a_linked_folder_is_left_alone(art, tmp_path):
    real = fill(art, tmp_path / 'real', 'old')
    (real / 'ui/online_poster.png').unlink()
    out = tmp_path / 'online-art'
    if WINDOWS:
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(out), str(real)], check=True, capture_output=True)
    else:
        out.symlink_to(real, target_is_directory=True)
    before = files(real)
    assert art.repair(tmp_path, out)[0] == 'nothing' and files(real) == before
    if WINDOWS:
        os.rmdir(out)


def run(*args):
    return subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/online-art.py'), *map(str, args)],
                          capture_output=True, text=True)


def test_command_line_and_status(art, tmp_path):
    assert run('--data', tmp_path, '--repair').returncode == 0          # nothing there: nothing to do, no file
    assert not (tmp_path / art.STATUS).exists()
    fill(art, tmp_path / 'online-art.tmp', 'new')
    assert run('--data', tmp_path, '--repair').returncode == art.REPAIRED
    assert 'result=ok\nstep=repair' in (tmp_path / art.STATUS).read_text(encoding='utf-8')
    assert run('--data', tmp_path, '--repair').returncode == 0 and run('--data', tmp_path, '--check').returncode == 0
    (tmp_path / 'online-art/fonts/TradeBld.ttf').unlink()
    assert run('--data', tmp_path, '--check').returncode == 1
    done = run('--data', tmp_path, '--repair')
    assert done.returncode == 1 and 'fonts/TradeBld.ttf' in done.stdout
    text = (tmp_path / art.STATUS).read_text(encoding='utf-8')
    assert 'result=failed\nstep=repair' in text and 'fonts/TradeBld.ttf' in text
    assert run('--data', tmp_path).returncode == 1                      # setup's call: no dump
    assert 'result=failed\nstep=setup' in (tmp_path / art.STATUS).read_text(encoding='utf-8')


def test_the_launchers_start_repairs(art, tmp_path, monkeypatch):
    sys.path.insert(0, str(ROOT / 'setup'))
    import mods
    manager = mods.Manager(tmp_path, '0.13.0', ['textures-v1'])
    assert manager.repair_online_art() == ''
    fill(art, tmp_path / 'online-art.tmp', 'new')
    assert 'have been repaired' in manager.repair_online_art()
    assert manager.repair_online_art() == ''
    (tmp_path / 'online-art/ui/online_poster.png').unlink()
    assert 'could not be repaired' in manager.repair_online_art()
    with mods.file_lock(manager.root / 'runtime.lock'):                 # the game runs: not now
        assert manager.repair_online_art() == ''
    source = (ROOT / 'setup/mods.py').read_text(encoding='utf-8')
    assert source.index('art = manager.repair_online_art()') < source.index('manager.sync()')


def test_the_game_logs_the_status_and_tells_the_player():
    source = (ROOT / 'src/hooks/online_screen.cpp').read_text(encoding='utf-8')
    assert 'online-art-status.ini' in source and '[ONLINE] art status:' in source
    assert 'MENU PICTURES ARE MISSING' in source
