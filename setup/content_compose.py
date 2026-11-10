# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Compose FFNY Online's fighter-body, clothing and venue packs into patched copies of the player's own files.

A composed pack (``install.json`` in its folder, docs/content-packs.md) never carries the game's bytes: its
entries are deltas against the player's own archive entries and its ops edit the player's own database, parts
list, name tables, venue list and entity list. This module builds the result for the game:

* the base of a game-relative path P is ``<data>/override/P`` when it exists (the port's own patched files),
  else ``<game>/P``;
* the packs apply in catalogue order (``order``, then id); a replacement the player switched off is left out;
* a pack whose delta original or result does not match, whose ``add`` name exists already (in the base or from
  an earlier pack), whose new venue's feID or filename is taken, or whose op fails is skipped whole; the others go
  on. Two ``replace`` of one entry: the later pack wins;
* the output is ``<data>/mod-manager/content-files/<P>`` plus ``files.ini`` (which packs applied, which were
  skipped and why, the files), built in ``content-files.new`` and swapped in. With nothing applied the folder is
  removed, so the game reads the originals again;
* nothing is rebuilt when the packs, the switches, the language packs and the bases (size and mtime) are what
  the last run saw (``content-compose.json`` beside the folder).

A language pack (docs/language-packs.md) rebuilds some of the same files from the dump at game start (string
tables in ``assets/misc.viv``, the menu art in ``screens/screens.viv``), and the game serves its copies ahead
of the composed ones, which would lose a clothing pack's item names and shop pictures while it is on. So for
every installed language pack the same packs are composed once more, on top of its translation: the files it
covers are rebuilt here as the game would (``scripts/langpack.py`` is the reference the game's rebuild is
tested against), the packs' ops and entries go on top (two PCs with the same packs and the same language
build byte-identical files), and the results go to ``content-files/lang/<language pack>/<P>``, listed in
``files.ini`` as ``lang=<id>|<P>``. The game serves those for the chosen language and leaves the rest of the
pack's files to its own rebuild. A language pack whose translation does not take the packs is left out
(``language-skipped``), and the game then rebuilds its files itself, as before.

The game (src/hooks/pc_content.c) serves the listed files through the game-file redirect.
"""

import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct

from ffnycontent import clothing, cpparts, delta, loc, newvenue, viv

SCHEMA = 1
COMPOSER = 2                    # bump when the output for the same inputs changes
TYPES = ('fighter-body', 'clothing', 'venue')
OUT = 'content-files'
LANG_DIR = 'lang'               # content-files/lang/<language pack>/<P>: the packs on top of that translation
LANG_MAX = 8                    # language packs composed with (each costs a copy of the files it covers)
LANG_NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}')
STATE = 'content-compose.json'
MAX_RESULT = 64 * 1024**2
MAX_DELTA = 32 * 1024**2
CLOTHING_OPS = ('colour', 'item')
VENUE_OPS = ('new-venue',)
PATH = re.compile(r'[A-Za-z0-9_][A-Za-z0-9_.-]*(/[A-Za-z0-9_][A-Za-z0-9_.-]*)*')
ENTRY = re.compile(r'[A-Za-z0-9_][A-Za-z0-9_./ -]{0,199}')
HEX = re.compile(r'[0-9a-f]{64}')
ENTRY_KEYS = ('archive', 'entry', 'mode', 'base_archive', 'base_entry', 'delta', 'original_sha256',
              'result_sha256', 'result_size')
# a failing pack is skipped with one of these; anything else is a bug and stops the composition
PACK_ERRORS = (ValueError, KeyError, IndexError, TypeError, OSError, struct.error, UnicodeError)


class ComposeError(ValueError):
    pass


def game_path(value):
    if not (isinstance(value, str) and len(value) < 300 and PATH.fullmatch(value) and '..' not in value.split('/')):
        raise ComposeError('invalid game path %r' % (value,))
    return value


def entry_name(value):
    if value == '':
        return value
    if not (isinstance(value, str) and ENTRY.fullmatch(value) and '..' not in value.split('/')):
        raise ComposeError('invalid entry name %r' % (value,))
    return value


def check_install(info):
    """The install.json shape (contract section 2); raises ComposeError."""
    if not isinstance(info, dict) or info.get('schema') != 1 or info.get('type') not in TYPES:
        raise ComposeError('unsupported install.json')
    entries, ops = info.get('entries', []), info.get('ops', [])
    if not isinstance(entries, list) or not isinstance(ops, list) or len(entries) > 4096 or len(ops) > 256:
        raise ComposeError('install.json entries/ops are not lists')
    deltas = set()
    for e in entries:
        if not isinstance(e, dict) or any(k not in e for k in ENTRY_KEYS):
            raise ComposeError('install.json entry lacks a field')
        game_path(e['archive'])
        game_path(e['base_archive'])
        entry_name(e['entry'])
        entry_name(e['base_entry'])
        if e['mode'] not in ('replace', 'add'):
            raise ComposeError('unknown entry mode %r' % (e['mode'],))
        d = e['delta']
        if not (isinstance(d, str) and d.startswith('deltas/') and d.endswith('.ffdelta')):
            raise ComposeError('invalid delta path %r' % (d,))
        game_path(d)
        if d in deltas:
            raise ComposeError('delta listed twice: ' + d)
        deltas.add(d)
        if not (isinstance(e['original_sha256'], str) and HEX.fullmatch(e['original_sha256'])
                and isinstance(e['result_sha256'], str) and HEX.fullmatch(e['result_sha256'])):
            raise ComposeError('invalid hash in install.json')
        if not isinstance(e['result_size'], int) or not 0 <= e['result_size'] <= MAX_RESULT:
            raise ComposeError('invalid result size')
    allowed = CLOTHING_OPS if info['type'] == 'clothing' else VENUE_OPS if info['type'] == 'venue' else ()
    for op in ops:
        if not isinstance(op, dict) or op.get('op') not in allowed:
            raise ComposeError('op %r is not allowed in a %s pack' % (op.get('op') if isinstance(op, dict) else op,
                                                                      info['type']))
    return deltas


def read_install(folder):
    path = Path(folder) / 'install.json'
    if path.stat().st_size > 4 * 1024**2:
        raise ComposeError('install.json is too large')
    info = json.loads(path.read_bytes())
    check_install(info)
    return info


def read_off(data):
    """The player's switches: [content] off in $RECOMP_SETTINGS (``none``: no file) or <data>/settings.ini."""
    env = os.environ.get('RECOMP_SETTINGS', '')
    if env.lower() == 'none':
        return set()
    path = Path(env) if env else Path(data) / 'settings.ini'
    try:
        text = path.read_text(encoding='utf-8-sig', errors='replace')
    except OSError:
        return set()
    section, off = '', set()
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1].strip().lower()
        elif section == 'content' and '=' in line:
            key, value = line.split('=', 1)
            if key.strip().lower() == 'off':
                off = {p.strip() for p in value.split(',') if p.strip()}
    return off


_langpack_module = None


def langpack():
    """scripts/langpack.py, the language packs' reference implementation (beside setup/ in every install)."""
    global _langpack_module
    if _langpack_module is None:
        path = Path(__file__).resolve().parent.parent / 'scripts' / 'langpack.py'
        spec = importlib.util.spec_from_file_location('content_langpack', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        _langpack_module = module
    return _langpack_module


def language_packs(data, off, log=print):
    """The installed language packs (<data>/mods/<id>/language.ini) the packs are composed with: those not switched
    off, by id, at most LANG_MAX. Each: {id, folder, stamp (over language.ini and the deltas), covered (the game
    paths it rebuilds, lower-case; default.xbe is patched in memory, not a file), edits {path key: (path, [(parts
    inside the file, delta bytes)])}}."""
    lp = langpack()
    out = []
    mods = Path(data) / 'mods'
    try:
        folders = sorted(p for p in mods.iterdir() if p.is_dir() and not p.is_symlink())
    except OSError:
        return out
    for folder in folders:
        pack_id = folder.name
        if not LANG_NAME.fullmatch(pack_id) or pack_id in off or not (folder / 'language.ini').is_file():
            continue
        try:
            _, files = lp.read_pack(str(folder))
            digest = hashlib.sha256((folder / 'language.ini').read_bytes())
            edits = {}
            for parts, patch in files:
                blob = folder.joinpath(*PurePosixPath(patch).parts).read_bytes()
                digest.update(blob)
                if parts[0].lower() == 'default.xbe':
                    continue
                edits.setdefault(parts[0].lower(), (parts[0], []))[1].append((parts[1:], blob))
        except (lp.PackError, OSError, UnicodeError, ValueError) as error:
            log('[compose] language pack %s not composed with: %s' % (pack_id, error))
            continue
        if len(out) >= LANG_MAX:
            log('[compose] language pack %s not composed with: more than %d language packs' % (pack_id, LANG_MAX))
            continue
        out.append({'id': pack_id, 'folder': folder, 'stamp': digest.hexdigest(), 'covered': set(edits),
                    'edits': edits})
    return out


def build_language_file(lang, key, bases, folder):
    """The file ``key`` as the game rebuilds it for the language pack: its deltas on the dump's copy (never the
    override's: the pack carries the port's own changes itself, docs/language-packs.md), written under ``folder``."""
    lp = langpack()
    rel, edits = lang['edits'][key]
    source = Path(lp.find_file(str(bases.game), rel))
    bases.seen['game:' + key] = [rel] + (stat_of('game', source) or [None])
    data = lp.rebuild(source.read_bytes(), edits)
    target = folder.joinpath(*PurePosixPath(rel).parts)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    return target


class Bases:
    """The player's files: <data>/override/P, else <game>/P. Remembers what it resolved (the unchanged check).
    With ``lang`` ({path key: file}), those paths are read from the language pack's rebuilt copies instead (a
    variant's bases); the deltas' originals stay the player's own files (``read_original``)."""

    def __init__(self, data, game, lang=None):
        self.override, self.game = Path(data) / 'override', Path(game)
        self.lang = dict(lang or {})
        self.seen = {}
        self._archives = {}
        self._originals = {}

    def plain_path(self, rel):
        key = rel.lower()
        over = self.override.joinpath(*PurePosixPath(rel).parts)
        where, path = ('override', over) if over.is_file() else ('game', self.game.joinpath(*PurePosixPath(rel).parts))
        if key not in self.seen:
            self.seen[key] = [rel] + (stat_of(where, path) or [None])
        return path

    def path(self, rel):
        plain = self.plain_path(rel)
        return self.lang.get(rel.lower(), plain)

    def exists(self, rel):
        return self.path(rel).is_file()

    def archive(self, rel):
        key = rel.lower()
        if key not in self._archives:
            path = self.path(rel)
            if not path.is_file():
                raise ComposeError('%s is not in your game folder' % rel)
            self._archives[key] = viv.Archive(path)
        return self._archives[key]

    def read(self, rel, entry=''):
        if entry == '':
            path = self.path(rel)
            if not path.is_file():
                raise ComposeError('%s is not in your game folder' % rel)
            return path.read_bytes()
        return self.archive(rel).read(entry)

    def read_original(self, rel, entry=''):
        """The player's own file or entry, whatever language the variant is built on."""
        if rel.lower() not in self.lang:
            return self.read(rel, entry)
        path = self.plain_path(rel)
        if not path.is_file():
            raise ComposeError('%s is not in your game folder' % rel)
        if entry == '':
            return path.read_bytes()
        key = rel.lower()
        if key not in self._originals:
            self._originals[key] = viv.Archive(path)
        return self._originals[key].read(entry)


def stat_of(where, path):
    try:
        st = path.stat()
        return [where, st.st_size, st.st_mtime_ns]
    except OSError:
        return None


class Work:
    """What the applied packs changed so far: loose files and archive entries (replaced and added)."""

    def __init__(self, bases):
        self.bases = bases
        self.loose = {}             # path key -> [path, bytes]
        self.archives = {}          # path key -> [path, {entry key: [name, bytes]}, [[name, bytes]]]

    def snapshot(self):
        return ({k: list(v) for k, v in self.loose.items()},
                {k: [v[0], dict(v[1]), list(v[2])] for k, v in self.archives.items()})

    def restore(self, snap):
        self.loose, self.archives = snap

    def _archive(self, rel):
        key = rel.lower()
        if key in self.loose:
            raise ComposeError('%s is changed as a whole and as an archive' % rel)
        if key not in self.archives:
            self.archives[key] = [rel, {}, []]
        return self.archives[key]

    def added(self, rel, entry):
        a = self.archives.get(rel.lower())
        return a is not None and any(n.lower() == entry.lower() for n, _ in a[2])

    def exists(self, rel, entry=''):
        if entry == '':
            return rel.lower() in self.loose or self.bases.exists(rel)
        return self.added(rel, entry) or (self.bases.exists(rel) and self.bases.archive(rel).has(entry))

    def current(self, rel, entry=''):
        """The content as the packs so far left it (else the player's)."""
        if entry == '':
            if rel.lower() in self.loose:
                return self.loose[rel.lower()][1]
            return self.bases.read(rel)
        a = self.archives.get(rel.lower())
        if a is not None:
            for n, data in a[2]:
                if n.lower() == entry.lower():
                    return data
            if entry.lower() in a[1]:
                return a[1][entry.lower()][1]
        return self.bases.read(rel, entry)

    def set_loose(self, rel, data):
        if rel.lower() in self.archives:
            raise ComposeError('%s is changed as a whole and as an archive' % rel)
        self.loose[rel.lower()] = [self.loose.get(rel.lower(), [rel])[0], bytes(data)]

    def replace(self, rel, entry, data):
        a = self._archive(rel)
        if self.added(rel, entry):
            for item in a[2]:
                if item[0].lower() == entry.lower():
                    item[1] = bytes(data)
            return
        name = self.bases.archive(rel).find(entry).name
        a[1][entry.lower()] = [name, bytes(data)]

    def add(self, rel, entry, data):
        self._archive(rel)[2].append([entry, bytes(data)])


def apply_pack(work, folder, info):
    """One pack's ops (first, on the files as the earlier packs left them) and entries; raises on any failure."""
    clothing_ops = [op for op in info.get('ops', []) if op['op'] in CLOTHING_OPS]
    if clothing_ops:
        tables = {}
        for name in clothing.NAME_TABLES:
            if work.exists(clothing.NAMES_ARCHIVE, name):
                tables[name] = work.current(clothing.NAMES_ARCHIVE, name)
        parts = work.current(clothing.PARTS_ARCHIVE, clothing.PARTS_XML).decode('latin-1')
        sod_blob, parts, tables = clothing.apply_ops(work.current(clothing.SOD_PATH), parts, tables, clothing_ops)
        work.set_loose(clothing.SOD_PATH, sod_blob)
        work.replace(clothing.PARTS_ARCHIVE, clothing.PARTS_XML, parts.encode('latin-1'))
        for name, blob in tables.items():
            work.replace(clothing.NAMES_ARCHIVE, name, blob)
    for op in info.get('ops', []):
        if op['op'] != 'new-venue':
            continue
        taken = set()
        if work.bases.exists(newvenue.VENUE_ARCHIVE):
            taken = {n.lower() for n in work.bases.archive(newvenue.VENUE_ARCHIVE).names()}
        a = work.archives.get(newvenue.VENUE_ARCHIVE)
        taken |= {n.lower() for n, _ in (a[2] if a else [])}
        venue_xml, entity_xml, sod_blob = newvenue.apply(work.current(newvenue.VENUE_XML),
                                                         work.current(newvenue.ENTITY_XML),
                                                         work.current(clothing.SOD_PATH), op, taken)
        work.set_loose(newvenue.VENUE_XML, venue_xml)
        work.set_loose(newvenue.ENTITY_XML, entity_xml)
        work.set_loose(clothing.SOD_PATH, sod_blob)
    for e in info.get('entries', []):
        rel, entry = e['archive'], e['entry']
        if entry and not work.bases.exists(rel):
            raise ComposeError('%s is not in your game folder' % rel)
        path = Path(folder).joinpath(*PurePosixPath(e['delta']).parts)
        if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_DELTA:
            raise ComposeError('delta %s is missing' % e['delta'])
        base = work.bases.read_original(e['base_archive'], e['base_entry'])
        if hashlib.sha256(base).hexdigest() != e['original_sha256']:
            raise ComposeError('%s in your game is not the version this pack was made for'
                               % (e['base_entry'] or e['base_archive']))
        result = delta.apply(base, path.read_bytes())
        if len(result) != e['result_size'] or hashlib.sha256(result).hexdigest() != e['result_sha256']:
            raise ComposeError('the result of %s does not match install.json' % e['delta'])
        if e['mode'] == 'add':
            if work.exists(rel, entry):
                raise ComposeError('%s exists already' % ('%s|%s' % (rel, entry) if entry else rel))
        elif not (work.bases.exists(rel) and (entry == '' or work.bases.archive(rel).has(entry))):
            raise ComposeError('%s is not in your game folder' % ('%s|%s' % (rel, entry) if entry else rel))
        if entry == '':
            work.set_loose(rel, result)
        elif e['mode'] == 'add':
            work.add(rel, entry, result)
        else:
            work.replace(rel, entry, result)


def set_hash(applied, off):
    digest = hashlib.sha256()
    for p in applied:
        digest.update(('%s\0%s\0%s\n' % (p['id'], p['version'], p['content_sha256'])).encode('utf-8'))
    digest.update(('off:' + ','.join(sorted(off))).encode('utf-8'))
    return digest.hexdigest()


def clean(text):
    return re.sub(r'[\r\n;]+', ' ', str(text))[:300]


def stored(data):
    """An added entry as the archives keep them: literal RefPack (raw when too large for its 3-byte size)."""
    return viv.refpack_store(data) if len(data) < 1 << 24 else data


def write_files(folder, work, only=None):
    """The composed files under ``folder`` (``only``: these path keys alone). Returns the paths written."""
    files = []
    for rel, data in sorted(work.loose.values()):
        if only is not None and rel.lower() not in only:
            continue
        target = folder.joinpath(*PurePosixPath(rel).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        files.append(rel)
    for rel, replace, add in sorted(work.archives.values(), key=lambda a: a[0]):
        if only is not None and rel.lower() not in only:
            continue
        src = work.bases.path(rel)
        source = work.bases.archive(rel)
        rep = {}
        for name, data in replace.values():
            raw = source.raw(name)
            rep[name] = (viv.refpack_store(data) if len(data) < 1 << 24 else data) if viv.is_refpack(raw) else data
        target = folder.joinpath(*PurePosixPath(rel).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        viv.compose_archive(str(src), str(target), replace=rep, add=[(n, stored(d)) for n, d in add])
        files.append(rel)
    return files


def write_files_ini(new, applied, skipped, off, files, lang_files, lang_skipped):
    lines = ['[content-files]', 'schema=%d' % SCHEMA, 'set=' + set_hash(applied, off),
             'applied=' + ','.join(p['id'] for p in applied),
             'skipped=' + '; '.join('%s: %s' % (i, clean(r)) for i, r in skipped)]
    lines += ['file=' + rel for rel in sorted(files, key=str.lower)]
    lines += ['lang=%s|%s' % (pack_id, rel) for pack_id, rel in sorted(lang_files, key=lambda x: (x[0], x[1].lower()))]
    if lang_skipped:
        lines.append('language-skipped=' + '; '.join('%s: %s' % (i, clean(r)) for i, r in lang_skipped))
    with (new / 'files.ini').open('w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def run_packs(bases, chosen, log):
    """Apply ``chosen`` on ``bases`` in order: (work, applied, skipped)."""
    work = Work(bases)
    applied, skipped = [], []
    for p in chosen:
        snap = work.snapshot()
        try:
            info = read_install(p['folder'])
            apply_pack(work, p['folder'], info)
            applied.append(p)
        except PACK_ERRORS + (cpparts.PartsError, loc.LocError) as error:
            work.restore(snap)
            reason = str(error) if not isinstance(error, KeyError) else 'not found: %s' % error.args[0]
            skipped.append((p['id'], reason))
            log('[compose] %s skipped: %s' % (p['id'], reason))
    return work, applied, skipped


def compose_language(lang, chosen, applied, touched, data, game, bases, new, tmp):
    """The same packs once more on top of the language pack's rebuilt files: the paths it covers among
    ``touched`` (those the packs changed) go to <new>/lang/<id>/. Returns the paths written ([]: no overlap)."""
    overlap = sorted(touched & lang['covered'])
    if not overlap:
        return []
    built = {key: build_language_file(lang, key, bases, tmp / lang['id']) for key in overlap}
    work, again, _ = run_packs(Bases(data, game, built), chosen, lambda text: None)
    if [p['id'] for p in again] != [p['id'] for p in applied]:
        raise ComposeError('the packs do not apply the same way on its files')
    return write_files(new / LANG_DIR / lang['id'], work, only=set(built))


def swap(root, new):
    """content-files.new -> content-files; the old folder goes aside first and is removed after."""
    out, old = root / OUT, root / (OUT + '.old')
    if old.exists():
        shutil.rmtree(old)
    if out.exists():
        os.replace(out, old)
    os.replace(new, out)
    if old.exists():
        shutil.rmtree(old)


def recover(root):
    """A run that stopped mid-swap: keep the complete folder, drop the partial one."""
    out, old, new = root / OUT, root / (OUT + '.old'), root / (OUT + '.new')
    if old.exists() and not out.exists():
        os.replace(old, out)
    for leftover in (old, new, root / (OUT + '.lang-tmp')):
        if leftover.exists():
            shutil.rmtree(leftover)


def remove_output(root):
    out = root / OUT
    if out.exists():
        shutil.rmtree(out)


def read_files_ini(root):
    path = root / OUT / 'files.ini'
    result = {'files': [], 'lang': []}
    try:
        text = path.read_text(encoding='utf-8')
    except OSError:
        return None
    for line in text.splitlines():
        key, sep, value = line.partition('=')
        if not sep:
            continue
        if key == 'file':
            result['files'].append(value)
        elif key == 'lang':
            pack_id, bar, rel = value.partition('|')
            if bar:
                result['lang'].append((pack_id, rel))
        else:
            result[key] = value
    return result


def inputs_key(game, packs, off, languages=()):
    raw = json.dumps([COMPOSER, str(Path(game).resolve()),
                      [[p['id'], p['version'], p['content_sha256']] for p in packs], sorted(off),
                      [[lang['id'], lang['stamp']] for lang in languages]])
    return hashlib.sha256(raw.encode('utf-8')).hexdigest()


def unchanged(root, state, key, data, game):
    if not state or state.get('schema') != SCHEMA or state.get('key') != key:
        return False
    bases = Bases(data, game)
    for seen in state.get('bases', {}).values():
        rel, where = seen[0], seen[1]
        base = bases.override if where == 'override' else bases.game
        if [rel] + (stat_of(where, base.joinpath(*PurePosixPath(rel).parts)) or [None]) != seen:
            return False
    if not state.get('applied'):
        return not (root / OUT).exists()
    files = read_files_ini(root)
    if not files or files.get('set') != state.get('set'):
        return False
    if files['lang'] != [tuple(x) for x in state.get('lang_files', [])]:
        return False
    out = root / OUT
    return (all(out.joinpath(*PurePosixPath(f).parts).is_file() for f in files['files'])
            and all((out / LANG_DIR / i).joinpath(*PurePosixPath(f).parts).is_file() for i, f in files['lang']))


def compose(data, game, packs, off=None, root=None, log=print, languages=None):
    """Compose ``packs`` ([{id, version, content_sha256, kind, order, folder}], every synced pack with an
    install.json) for the player, and once more with each installed language pack (``languages``: the list
    ``language_packs`` makes, found when None). Returns {applied, skipped, files, lang_files, lang_skipped,
    unchanged}."""
    data = Path(data)
    root = Path(root) if root else data / 'mod-manager'
    root.mkdir(parents=True, exist_ok=True)
    off = read_off(data) if off is None else set(off)
    recover(root)
    packs = sorted(packs, key=lambda p: (p.get('order', 1 << 30), p['id']))
    switched = sorted(p['id'] for p in packs if p.get('kind') != 'content' and p['id'] in off)
    chosen = [p for p in packs if p['id'] not in switched]
    languages = language_packs(data, off, log) if languages is None else list(languages)
    key = inputs_key(game, chosen, switched, languages)
    state_path = root / STATE
    try:
        state = json.loads(state_path.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        state = None
    if unchanged(root, state, key, data, game):
        return {'applied': state['applied'], 'skipped': [tuple(s) for s in state.get('skipped', [])],
                'files': state.get('files', []), 'lang_files': [tuple(x) for x in state.get('lang_files', [])],
                'lang_skipped': [tuple(s) for s in state.get('lang_skipped', [])], 'unchanged': True}
    bases = Bases(data, game)
    work, applied, skipped = run_packs(bases, chosen, log)
    files, lang_files, lang_skipped = [], [], []
    if applied:
        new, tmp = root / (OUT + '.new'), root / (OUT + '.lang-tmp')
        for folder in (new, tmp):
            if folder.exists():
                shutil.rmtree(folder)
        new.mkdir(parents=True)
        files = write_files(new, work)
        touched = set(work.loose) | set(work.archives)
        for lang in languages:
            try:
                written = compose_language(lang, chosen, applied, touched, data, game, bases, new, tmp)
                lang_files += [(lang['id'], rel) for rel in written]
            except PACK_ERRORS + (ComposeError, cpparts.PartsError, loc.LocError, langpack().PackError) as error:
                lang_skipped.append((lang['id'], str(error)))
                log('[compose] language pack %s not composed with: %s' % (lang['id'], error))
                if (new / LANG_DIR / lang['id']).exists():
                    shutil.rmtree(new / LANG_DIR / lang['id'])
        if tmp.exists():
            shutil.rmtree(tmp)
        write_files_ini(new, applied, skipped, switched, files, lang_files, lang_skipped)
        swap(root, new)
    else:
        remove_output(root)
    state = {'schema': SCHEMA, 'key': key, 'bases': bases.seen, 'applied': [p['id'] for p in applied],
             'skipped': skipped, 'files': files, 'lang_files': lang_files, 'lang_skipped': lang_skipped,
             'set': set_hash(applied, switched) if applied else ''}
    tmp = root / (STATE + '.tmp')
    tmp.write_text(json.dumps(state, sort_keys=True), encoding='utf-8')
    os.replace(tmp, state_path)
    return {'applied': state['applied'], 'skipped': skipped, 'files': files, 'lang_files': lang_files,
            'lang_skipped': lang_skipped, 'unchanged': False}
