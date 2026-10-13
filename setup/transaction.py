"""Durable, owned-path activation rollback; never deletes player data or old builds.

Setup holds installation/data/play locks for the entire transaction. Launchers refuse
to play while this journal exists. Optional shortcuts are reconciled after commit.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil

HOOK = None  # Internal fixture injection only; no environment/CLI switch in shipped setup.
NAME = '.setup-transaction'


def checkpoint(name):
    if HOOK:
        HOOK(name)


def sync_directory(path):
    if os.name != 'nt':
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def sync_tree(path):
    path = Path(path)
    if path.is_dir():
        for directory in sorted((p for p in path.rglob('*') if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
            sync_directory(directory)
        sync_directory(path)
    sync_directory(path.parent)


def atomic_json(path, value):
    path = Path(path)
    pending = path.with_name(path.name + '.pending')
    with pending.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(pending, path)
    sync_directory(path.parent)


def posix_mode(item):
    # Windows reports execute bits for .exe and no others, so a backup copied under
    # another name reads back with different bits; the mode is not comparable there.
    return 0 if os.name == 'nt' else item.stat().st_mode & 0o777


def inventory(path):
    if not path.exists() and not path.is_symlink():
        return None
    if path.is_symlink():
        raise ValueError('Activation target must not be a symlink: ' + str(path))
    result = {}
    items = sorted(path.rglob('*')) if path.is_dir() else [path]
    for item in items:
        name = item.relative_to(path).as_posix() if item != path else '.'
        if item.is_symlink():
            raise ValueError('Activation backup contains a symlink: ' + str(item))
        if item.is_file():
            with item.open('rb') as stream:
                sha = hashlib.file_digest(stream, 'sha256').hexdigest()
            result[name] = [sha, posix_mode(item)]
        elif item.is_dir():
            result[name + '/'] = ['directory', posix_mode(item)]
        else:
            raise ValueError('Unsupported activation file')
    return {'directory': path.is_dir(), 'files': result}



def flush_file(path):
    # Windows _commit/FlushFileBuffers requires a writable descriptor.
    with Path(path).open('r+b' if os.name == 'nt' else 'rb') as stream:
        os.fsync(stream.fileno())

def copy(source, destination):
    if source.is_dir():
        shutil.copytree(source, destination)
    else:
        shutil.copy2(source, destination)
    for item in destination.rglob('*') if destination.is_dir() else [destination]:
        if item.is_file():
            flush_file(item)
    sync_tree(destination)


def remove(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


class Activation:
    def __init__(self, root, data):
        self.root, self.data = Path(root).resolve(), Path(data).resolve()
        self.folder = self.root / NAME
        self.allowed = {str(self.root / name) for name in (
            'installed.json', 'installed.ini', 'DefJamLauncher.exe',
            'Def Jam Recompiled.app', 'Def Jam Recompiled', 'Def Jam Recompiled.png')}
        self.allowed.add(str(self.data / 'settings.ini'))

    def recover(self):
        if not self.folder.exists():
            return
        if self.folder.is_symlink():
            raise ValueError('Unsafe activation journal folder')
        journal_path = self.folder / 'journal.json'
        if not journal_path.is_file() or journal_path.is_symlink():
            # A crash during backup precedes mutations. Only our owner marker permits cleanup.
            owner = self.folder / 'owner.json'
            if not owner.is_file() or owner.is_symlink() or json.loads(owner.read_text()) != {'product': 'DefJamRecompiled', 'schema': 1}:
                raise ValueError('Unrecognized activation recovery folder')
            shutil.rmtree(self.folder)
            sync_directory(self.root)
            return
        journal = json.loads(journal_path.read_text())
        if journal.get('schema') != 1 or journal.get('root') != str(self.root) or journal.get('data') != str(self.data):
            raise ValueError('Unsupported activation journal; keep backups and rerun the matching setup')
        entries = journal.get('entries')
        if not isinstance(entries, list) or len(entries) > len(self.allowed):
            raise ValueError('Invalid activation journal inventory')
        seen = set()
        for i, entry in enumerate(entries):
            path = Path(entry['path'])
            if str(path) not in self.allowed or str(path) in seen or path.is_symlink() or path.parent.resolve() != path.parent:
                raise ValueError('Unsafe activation recovery path')
            seen.add(str(path))
            if entry['before'] is not None and inventory(self.folder / str(i)) != entry['before']:
                raise ValueError('Activation backup changed; recovery stopped without deleting it')
        if journal.get('committed') is not True:
            for i, entry in enumerate(entries):
                path = Path(entry['path'])
                checkpoint('recover:' + path.name)
                pending = path.with_name(path.name + '.restore')
                remove(pending)
                if entry['before'] is not None:
                    copy(self.folder / str(i), pending)
                if entry['before'] is None:
                    remove(path)
                else:
                    if pending.is_dir() or path.is_dir():
                        remove(path)
                    checkpoint('restore:' + path.name)
                    os.replace(pending, path)
                sync_directory(path.parent)
        shutil.rmtree(self.folder)
        sync_directory(self.root)

    def begin(self, paths):
        self.recover()
        self.folder.mkdir()
        atomic_json(self.folder / 'owner.json', {'product': 'DefJamRecompiled', 'schema': 1})
        entries = []
        for i, path in enumerate(paths):
            path = Path(path)
            if str(path) not in self.allowed or path.parent.resolve() != path.parent:
                raise ValueError('Unsafe activation target')
            before = inventory(path)
            if before is not None:
                copy(path, self.folder / str(i))
            entries.append({'path': str(path), 'before': before})
        self.journal = {'schema': 1, 'root': str(self.root), 'data': str(self.data), 'entries': entries, 'committed': False}
        atomic_json(self.folder / 'journal.json', self.journal)
        sync_directory(self.root)
        checkpoint('prepared')

    def commit(self):
        # Persist every promoted file/bundle before publishing the commit marker.
        for entry in self.journal['entries']:
            path = Path(entry['path'])
            if path.exists():
                for item in path.rglob('*') if path.is_dir() else [path]:
                    if item.is_file():
                        flush_file(item)
                sync_tree(path)
        self.journal['committed'] = True
        atomic_json(self.folder / 'journal.json', self.journal)
        checkpoint('committed')
        self.recover()
