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
* the mods of a pack of several mods (``bundle``, its title ``bundle_name``) apply all or none: when one of them
  is skipped, the packs are applied again without any mod of that pack, so the files hold nothing of it (its other
  mods are skipped with "another mod of the pack <name> could not be applied"); single packs are as above;
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

import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct

from ffnycontent import clothing, cpparts, delta, fighterart, loc, newfighter, newvenue, unlimited, viv
from ffnycontent import sod as sodmod

SCHEMA = 1
COMPOSER = 10                   # bump when the output for the same inputs changes (10: build 86 = banks, official maps, fighter_light)
NEW_DAYS = 14                   # a new fighter's select thumbs carry NEW for this long after its review
HUB_ART = ('online-art', 'fighters')    # <data>/online-art/fighters/<feID>.png: the online hub's portraits
SKIN_ART = ('online-art', 'skins')      # <data>/online-art/skins/<pack id>.png: a skin's picture in the skin window
TYPES = ('fighter-body', 'clothing', 'venue', 'new-fighter')
ALWAYS_ON = ('content', 'official-map')     # kinds the player cannot switch off
OUT = 'content-files'
LANG_DIR = 'lang'               # content-files/lang/<language pack>/<P>: the packs on top of that translation
LANG_MAX = 8                    # language packs composed with (each costs a copy of the files it covers)
LANG_NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}')
STATE = 'content-compose.json'
MAX_RESULT = 64 * 1024**2
MAX_DELTA = 40 * 1024**2     # one delta file: the account service's PackMaxFile (40 MiB from build 82; a venue with 32 MB of
#                               textures packs its texture file at 33-35 MiB)
CLOTHING_OPS = ('colour', 'item')
VENUE_OPS = ('new-venue',)
FIGHTER_OPS = ('new-fighter',)
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
    allowed = {'clothing': CLOTHING_OPS, 'venue': VENUE_OPS, 'new-fighter': FIGHTER_OPS}.get(info['type'], ())
    for op in ops:
        if not isinstance(op, dict) or op.get('op') not in allowed:
            raise ComposeError('op %r is not allowed in a %s pack' % (op.get('op') if isinstance(op, dict) else op,
                                                                      info['type']))
    if info['type'] == 'new-fighter':
        if len(ops) != 1:
            raise ComposeError('a new-fighter pack holds exactly one new-fighter op')
        problems = newfighter.op_problems(ops[0], allow_fe_id=True)
        if problems:
            raise ComposeError('new fighter: ' + problems[0])
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
        self.logical = []           # records of the fighters beyond the places (unlimited.add_records), in order

    def snapshot(self):
        return ({k: list(v) for k, v in self.loose.items()},
                {k: [v[0], dict(v[1]), list(v[2])] for k, v in self.archives.items()}, list(self.logical))

    def restore(self, snap):
        self.loose, self.archives, self.logical = snap

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


class FighterFiles:
    """The player's files as the packs so far left them, as ``newfighter.apply_op`` reads them."""

    def __init__(self, work):
        self.work = work

    def current(self, rel, entry=''):
        return self.work.current(rel, entry)

    def names(self, rel):
        names = list(self.work.bases.archive(rel).names()) if self.work.bases.exists(rel) else []
        a = self.work.archives.get(rel.lower())
        return names + [n for n, _ in (a[2] if a else [])]


def fighter_place(op, fe_id):
    """A new fighter's front-end ID: the signed catalogue's (``fighter_fe_id``, the account service's place for
    the pack: the same for every player), else the op's own (a pack installed by hand or tested from Studio)."""
    place = fe_id if fe_id else op.get('fe_id')
    if not isinstance(place, int) or isinstance(place, bool) or place == newfighter.MORE_ID:
        raise ComposeError('this new fighter has no place in the roster yet (the catalogue gives none)')
    return place


MAX_VOICE_FILE = 1024 * 1024
ART_FILES = (fighterart.BODY, fighterart.HEAD, fighterart.NAME)


def pack_reader(folder):
    """``read(path)`` of a pack's own files for its ops: a new fighter's voice clips (``voice/<name>``) and its
    menu art (``art/body.png``, ``art/head.png``, ``art/name.png``; each optional: FileNotFoundError for one the
    pack does not have, and the pictures it would fill stay the template's)."""
    def read(rel):
        if rel in ART_FILES:
            path = Path(folder).joinpath(*PurePosixPath(rel).parts)
            if not path.is_file():
                raise FileNotFoundError(rel)
            if path.is_symlink() or path.stat().st_size > fighterart.ART_MAX_BYTES:
                raise ComposeError('%s of the pack is too large' % rel)
            return path.read_bytes()
        parts = PurePosixPath(rel).parts
        if (not isinstance(rel, str) or len(parts) != 2 or parts[0] != 'voice' or '..' in parts
                or not re.fullmatch(r'[A-Za-z0-9_.\-]{1,64}', parts[1])):
            raise ComposeError('%r is not a voice file of the pack' % (rel,))
        path = Path(folder).joinpath(*parts)
        if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_VOICE_FILE:
            raise ComposeError('%s is missing from the pack' % rel)
        return path.read_bytes()
    return read


def apply_new_fighter(work, op, fe_id, folder=None, new=False):
    pack = pack_reader(folder) if folder is not None else None
    place = fighter_place(op, fe_id)
    logical = newfighter.is_logical(place)
    if logical and any(r['id'] == place for r in work.logical):
        raise ComposeError('the community fighter %d is there already' % place)
    for where, rel, entry, data in newfighter.apply_op(FighterFiles(work), op, place, pack=pack, new=new,
                                                       logical=logical,
                                                       taken_models=[r['model'] for r in work.logical]):
        if where == 'logical':
            work.logical.append(data)
        elif where == 'loose':
            work.set_loose(rel, data)
        elif where == 'replace':
            work.replace(rel, entry, data)
        else:
            if work.exists(rel, entry):
                raise ComposeError('%s|%s exists already' % (rel, entry))
            work.add(rel, entry, data)


def is_skin(p):
    """A new-fighter pack the team accepted as a skin (the catalogue's ``fighter_mode``): an outfit of its template."""
    return p.get('fighter_mode') == 'skin'


def apply_skin(work, op, skin_of=None):
    """A skin: a new-fighter pack the team accepted as an extra outfit of its template (same gameplay, new look).
    It gets the model's own files (the pack's add entries), its entity, IntroExtro entry, camera banks and
    animation tags, as a new fighter does, but no Character row, no place and no menu pictures, and none of the
    op's gameplay or voice: the game swaps the template's model for this one at match start for a player who
    picked the skin (src/hooks/fighter_skins.c). ``skin_of``: the template the catalogue names (it must be the
    op's). Returns (template feID, model, name)."""
    problems = newfighter.op_problems(op, allow_fe_id=True)
    if problems:
        raise newfighter.FighterError(problems[0])
    template_fe_id, template, model = op['template_fe_id'], op['template_model'], op['model']
    if skin_of is not None and skin_of != template_fe_id:
        raise ComposeError('the catalogue names fighter %s as this skin\'s template, the pack %d' % (skin_of,
                                                                                                 template_fe_id))
    files = FighterFiles(work)
    rows = {r['fe_id']: r for r in newfighter.characters(files.current(newfighter.SOD_PATH))}
    if template_fe_id not in rows or rows[template_fe_id]['model'].upper() != template.upper():
        raise newfighter.FighterError('%s is not fighter %d of your game' % (template, template_fe_id))
    if any(r['model'].upper() == model.upper() for r in rows.values()):
        raise newfighter.FighterError('the model %s is another fighter\'s already' % model)
    work.set_loose(newfighter.ENTITY_XML, newfighter.add_entity(files.current(newfighter.ENTITY_XML), template, model))
    nis = next((n for n in files.names(newfighter.NIS_ARCHIVE) if n.lower() == newfighter.INTRO_EXTRO.lower()), None)
    if nis is None:
        raise newfighter.FighterError('%s has no %s' % (newfighter.NIS_ARCHIVE, newfighter.INTRO_EXTRO))
    work.replace(newfighter.NIS_ARCHIVE, nis, newfighter.add_taunt(files.current(newfighter.NIS_ARCHIVE, nis),
                                                                    template, model))
    banks = newfighter.model_banks(newfighter._View(files, newfighter.BANK_ARCHIVE), template, model)
    if len(banks) != 2:
        raise newfighter.FighterError('the camera banks of %s are missing, or %s has its own already' % (template,
                                                                                                         model))
    for name, data in banks:
        if work.exists(newfighter.BANK_ARCHIVE, name):
            raise ComposeError('%s|%s exists already' % (newfighter.BANK_ARCHIVE, name))
        work.add(newfighter.BANK_ARCHIVE, name, data)
    try:
        tags = files.current(newfighter.ANIM_TAGS)
    except (KeyError, OSError):
        tags = None
    if tags is not None:
        work.set_loose(newfighter.ANIM_TAGS, newfighter.add_anim_tags(tags, template, model))
    return template_fe_id, model, op['name']


def apply_pack(work, folder, info, fe_id=None, new=False, skin=False, skin_of=None, rts=False):
    """One pack's ops (first, on the files as the earlier packs left them) and entries; raises on any failure.
    ``fe_id``: a new-fighter pack's place from the signed catalogue; ``new``: its select thumbs carry NEW;
    ``skin``: the team accepted the new fighter as a skin of its template (``skin_of``, ``apply_skin``);
    ``rts``: an official Run the Streets map (build 86): its venue is left out of Battle mode's venue select."""
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
    crowd = None
    models = {}
    for op in info.get('ops', []):
        if op['op'] != 'new-venue':
            continue
        crowd = newvenue.check_crowd(op, info.get('entries', []))
        models = newvenue.check_models(op, info.get('entries', []))
        if crowd and work.exists(newvenue.ICROWD_ARCHIVE, crowd):
            raise ComposeError('%s|%s exists already' % (newvenue.ICROWD_ARCHIVE, crowd))
        taken = set()
        if work.bases.exists(newvenue.VENUE_ARCHIVE):
            taken = {n.lower() for n in work.bases.archive(newvenue.VENUE_ARCHIVE).names()}
        a = work.archives.get(newvenue.VENUE_ARCHIVE)
        taken |= {n.lower() for n, _ in (a[2] if a else [])}
        venue_xml, entity_xml, sod_blob = newvenue.apply(work.current(newvenue.VENUE_XML),
                                                         work.current(newvenue.ENTITY_XML),
                                                         work.current(clothing.SOD_PATH), op, taken)
        if rts:
            sod_blob = sodmod.set_venue_modes(sod_blob, int(op['fe_id']), 0)
        work.set_loose(newvenue.VENUE_XML, venue_xml)
        work.set_loose(newvenue.ENTITY_XML, entity_xml)
        work.set_loose(clothing.SOD_PATH, sod_blob)
    for op in info.get('ops', []):
        if op['op'] == 'new-fighter' and skin:
            apply_skin(work, op, skin_of)
        elif op['op'] == 'new-fighter':
            apply_new_fighter(work, op, fe_id, folder, new)
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
        if crowd and rel == newvenue.ICROWD_ARCHIVE:
            newvenue.check_locator(base, result)
        if rel == newvenue.VENUE_ARCHIVE and e['mode'] == 'add' and entry in models:
            newvenue.check_model(result, models[entry])
        if rel == newvenue.CAMERA_ARCHIVE and e['mode'] == 'add':
            # a new venue's camera bank from Studio <= beta.24 names the template inside (BLAZIN' heap crash)
            for op in info.get('ops', []):
                if op['op'] == 'new-venue':
                    result = newvenue.repair_camera_bank(entry, result, op.get('code'))
            # any added animation bank (Studio <= beta.24: alignment 1, no L231 trailer): the retail layout
            result = newvenue.retail_bank_layout(result)
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


RAW_ARCHIVES = ('audio/',)     # the sound engine reads its archives' entries as they are (no RefPack)


def stored(data, rel=''):
    """An added entry as the archives keep them: literal RefPack (raw when too large for its 3-byte size, and in
    the audio archives, whose stock entries are plain SCHl/BNK data the speech and sound code read in place)."""
    if rel.lower().replace(chr(92), '/').startswith(RAW_ARCHIVES):
        return data
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
        viv.compose_archive(str(src), str(target), replace=rep, add=[(n, stored(d, rel)) for n, d in add])
        files.append(rel)
    return files


def skins_of(packs):
    """The applied skins in catalogue order: [(pack id, template feID, model, name)]. The game numbers the skins of
    each template from 1 in this order (0 is the template's own look), the same on every PC with the same packs
    (the content print), so the online START names a skin by that number."""
    out = []
    for p in packs:
        if not is_skin(p):
            continue
        try:
            ops = [op for op in read_install(p['folder']).get('ops', []) if op['op'] == 'new-fighter']
        except PACK_ERRORS:
            continue
        if ops:
            out.append((p['id'], ops[0]['template_fe_id'], ops[0]['model'], ops[0]['name']))
    return out


def write_files_ini(new, applied, skipped, off, files, lang_files, lang_skipped):
    lines = ['[content-files]', 'schema=%d' % SCHEMA, 'set=' + set_hash(applied, off),
             'applied=' + ','.join(p['id'] for p in applied),
             'skipped=' + '; '.join('%s: %s' % (i, clean(r)) for i, r in skipped)]
    # skins (pc_content.c, fighter_skins.c): skin=<pack id>|<template feID>|<model>|<name in the game>
    lines += ['skin=%s|%d|%s|%s' % (i, t, m, clean(n).replace('|', ' ')) for i, t, m, n in skins_of(applied)]
    lines += ['file=' + rel for rel in sorted(files, key=str.lower)]
    lines += ['lang=%s|%s' % (pack_id, rel) for pack_id, rel in sorted(lang_files, key=lambda x: (x[0], x[1].lower()))]
    if lang_skipped:
        lines.append('language-skipped=' + '; '.join('%s: %s' % (i, clean(r)) for i, r in lang_skipped))
    with (new / 'files.ini').open('w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def new_fighter_op(folder):
    """The new-fighter op of a pack folder's install.json, or None (none, or an unreadable file)."""
    try:
        return next((op for op in read_install(folder).get('ops', []) if op.get('op') == 'new-fighter'), None)
    except PACK_ERRORS + (ComposeError,):
        return None


def settle_places(packs, reserved=(), log=print):
    """One feID per new fighter, before composing. A community pack keeps its catalogue place (the account service
    gives each its own; a second one on a place already given is left out). A player's own pack (``local``, FFNY
    Studio's "Use in my game") keeps the place Studio chose while no community pack, no place of the signed
    catalogue (``reserved``: a pack that is live but not synced yet) and no earlier own pack has it; else it moves
    to the first free place in ``newfighter.PLACE_ORDER`` (100..123, the low places, 124..127), and with none
    free it is left out. Changes ``packs`` in place: a moved pack's ``fighter_fe_id``, ``place_problem`` (the
    sentence for the Content page) on one left out. Returns [(pack id, old place, new place)] of the moves."""
    order = list(newfighter.PLACE_ORDER)
    valid = set(order)
    fighters = []
    beyond = False
    for p in packs:
        op = new_fighter_op(p['folder']) if p.get('folder') else None
        if op is not None:
            place = p.get('fighter_fe_id') or op.get('fe_id')
            if newfighter.is_logical(place):
                beyond = True       # a community fighter beyond the places: no place, it needs the banks
                continue
            fighters.append((p, place if isinstance(place, int) and not isinstance(place, bool) else None))
    taken = {r for r in reserved if r in valid}
    if beyond:
        taken |= set(unlimited.BANKS)   # an own pack does not move onto a bank (a community pack there: finish_logical)
    owner = {}
    for p, place in fighters:
        if p.get('local'):
            continue
        if place in owner:
            p['place_problem'] = "its place %d is the pack %s's already" % (place, owner[place])
            log('[compose] %s left out: %s' % (p['id'], p['place_problem']))
        elif place is not None:
            owner[place] = p['id']
            taken.add(place)
    moves = []
    for p, place in fighters:
        if not p.get('local'):
            continue
        if place in valid and place not in taken:
            taken.add(place)
            continue
        free = next((f for f in order if f not in taken), None)
        if free is None:
            p['place_problem'] = ('every place for a new fighter is taken by the community fighters and your other '
                                  'packs; switch one of your own packs off to make room')
            log('[compose] %s left out: %s' % (p['id'], p['place_problem']))
            continue
        why = ('the community pack %s has %s now' % (owner[place], place) if place in owner else
               'place %s is taken' % place if place is not None else 'it had no place')
        log('[compose] %s: new fighter moved from place %s to %d (%s)' % (p['id'], place, free, why))
        p['fighter_fe_id'] = free
        taken.add(free)
        moves.append((p['id'], place, free))
    return moves


def run_packs(bases, chosen, log, new=()):
    """Apply ``chosen`` on ``bases`` in order: (work, applied, skipped). ``new``: the IDs of the new-fighter packs
    whose thumbs carry NEW (``new_packs``)."""
    work = Work(bases)
    applied, skipped = [], []
    for p in chosen:
        snap = work.snapshot()
        try:
            if p.get('place_problem'):
                raise ComposeError(p['place_problem'])
            info = read_install(p['folder'])
            apply_pack(work, p['folder'], info, p.get('fighter_fe_id'), p['id'] in new, is_skin(p), p.get('skin_of'),
                       bool(p.get('rts_map')))
            applied.append(p)
        except PACK_ERRORS + (cpparts.PartsError, loc.LocError, newfighter.FighterError) as error:
            work.restore(snap)
            reason = str(error) if not isinstance(error, KeyError) else 'not found: %s' % error.args[0]
            skipped.append((p['id'], reason))
            log('[compose] %s skipped: %s' % (p['id'], reason))
    if work.logical:
        finish_logical(work, applied, skipped, log)
    return work, applied, skipped


def finish_logical(work, applied, skipped, log):
    """The community fighters beyond the places into the database: the four bank rows, their unindexed records
    and the side file (``unlimited.py``). When the banks cannot go in (a place fighter at 124..127) every such
    fighter is left out, with the reason."""
    try:
        sod, side = unlimited.add_records(work.current(newfighter.SOD_PATH), work.logical)
    except newfighter.FighterError as error:
        ids = {r['id'] for r in work.logical}
        for p in list(applied):
            op = new_fighter_op(p['folder']) if p.get('folder') else None
            if op is not None and fighter_place(op, p.get('fighter_fe_id')) in ids:
                applied.remove(p)
                skipped.append((p['id'], str(error)))
                log('[compose] %s skipped: %s' % (p['id'], error))
        work.logical = []
        return
    work.set_loose(newfighter.SOD_PATH, sod)
    work.set_loose(unlimited.SIDE_FILE, side)
    # the banks' own pictures (Doc's, as placeholders): the screens open a bank's pictures by its ID, and the game
    # loads the bound fighter's in their place (src/hooks/fighter_bank_hooks.c, the movie loader)
    files = FighterFiles(work)
    there = {n.lower() for n in files.names(newfighter.SCREENS_ARCHIVE)}
    for fe in unlimited.BANKS:
        for name, data in newfighter.picture_copies(newfighter._View(files, newfighter.SCREENS_ARCHIVE),
                                                    unlimited.BANK_TEMPLATE, fe):
            if name.lower() not in there:
                work.add(newfighter.SCREENS_ARCHIVE, name, data)
                there.add(name.lower())
    log('[compose] %d community fighters beyond the places (banks 124..127)' % len(work.logical))


def run_whole(bases, chosen, log, new=()):
    """``run_packs``, with the mods of a pack of several mods (``bundle``) applied all or none: a pack with a
    skipped mod is left out whole and the rest applied again from the bases, until no such pack is left (each
    round leaves out at least one more). Returns (work, applied, skipped, used): ``used`` the packs applied on."""
    dropped, reasons = set(), {}
    while True:
        used = [p for p in chosen if p.get('bundle') not in dropped]
        work, applied, skipped = run_packs(bases, used, log, new)
        failed = dict(skipped)
        broken = {p['bundle'] for p in used if p.get('bundle') and p['id'] in failed}
        if not broken:
            break
        reasons.update((p['id'], failed[p['id']]) for p in used if p.get('bundle') in broken and p['id'] in failed)
        dropped |= broken
    if not dropped:
        return work, applied, skipped, used
    for p in chosen:
        if p.get('bundle') in dropped and p['id'] not in reasons:
            reasons[p['id']] = 'another mod of the pack %s could not be applied' % (p.get('bundle_name') or p['bundle'])
            log('[compose] %s skipped: %s' % (p['id'], reasons[p['id']]))
    reasons.update(skipped)
    return work, applied, [(p['id'], reasons[p['id']]) for p in chosen if p['id'] in reasons], used


def compose_language(lang, chosen, applied, touched, data, game, bases, new, tmp, fresh=()):
    """The same packs once more on top of the language pack's rebuilt files: the paths it covers among
    ``touched`` (those the packs changed) go to <new>/lang/<id>/. Returns the paths written ([]: no overlap)."""
    overlap = sorted(touched & lang['covered'])
    if not overlap:
        return []
    built = {key: build_language_file(lang, key, bases, tmp / lang['id']) for key in overlap}
    work, again, _ = run_packs(Bases(data, game, built), chosen, lambda text: None, fresh)
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


def inputs_key(game, packs, off, languages=(), new=()):
    raw = json.dumps([COMPOSER, str(Path(game).resolve()),
                      [[p['id'], p['version'], p['content_sha256'], p.get('fighter_fe_id', 0)]
                       + ([p['bundle']] if p.get('bundle') else []) + (['left out'] if p.get('place_problem') else [])
                       + (['skin', p.get('skin_of')] if is_skin(p) else []) + (['rts'] if p.get('rts_map') else [])
                       for p in packs],
                      sorted(off),
                      [[lang['id'], lang['stamp']] for lang in languages],
                      sorted(new)])
    return hashlib.sha256(raw.encode('utf-8')).hexdigest()


def new_packs(packs, today=None):
    """The IDs of the new-fighter packs (those with a place from the catalogue) whose version the team accepted
    less than ``NEW_DAYS`` days ago (``reviewed``, the catalogue's date; ``today``: a UTC date, now when None).
    Their select thumbs carry NEW; the set is part of the inputs key, so the files are composed again on the day
    it changes. A date up to a day ahead still counts (the review's day against the player's clock)."""
    today = today or datetime.datetime.now(datetime.timezone.utc).date()
    out = set()
    for p in packs:
        try:
            days = (today - datetime.date.fromisoformat(str(p.get('reviewed') or ''))).days
        except ValueError:
            continue
        if p.get('fighter_fe_id') and -1 <= days < NEW_DAYS:
            out.add(p['id'])
    return out


def hub_portraits(data, packs, template, known=()):
    """<data>/online-art/fighters/<feID>.png (the online hub's portrait, like the stock ones ``scripts/
    online-art.py`` writes) for each applied new fighter whose pack has body art; a new fighter's place's portrait
    of no
    applied pack is removed. ``template(entry)``: an entry of screens.viv (the template's team portrait gives the
    tone; KeyError and the like: the stock cool tone). ``known``: the [feID, pack id] pairs written before for
    the same inputs, not drawn again while their file is there. Returns the pairs there now."""
    folder = Path(data).joinpath(*HUB_ART)
    known = {(int(fe), str(pack_id)) for fe, pack_id in known}
    there = []
    for p in packs:
        if is_skin(p):
            continue                        # a skin has no place (skin_portraits)
        try:
            ops = [op for op in read_install(p['folder']).get('ops', []) if op['op'] == 'new-fighter']
            if not ops:
                continue
            place = fighter_place(ops[0], p.get('fighter_fe_id'))
            # build 86: a community fighter beyond the places has one too, under its number (the hub's picker,
            # the room card and the versus screen show it online)
            if not (newfighter.is_place(place) or newfighter.is_logical(place)) or place in dict(there):
                continue
            path = folder / ('%d.png' % place)
            if (place, p['id']) not in known or not path.is_file():
                body = pack_reader(p['folder'])(fighterart.BODY)
                try:
                    team = template('screens/portrait/p_CT/p_CT_%d.big' % (ops[0]['template_fe_id'] + 1))
                except PACK_ERRORS:
                    team = None
                picture = fighterart.hub_portrait(body, team)
                folder.mkdir(parents=True, exist_ok=True)
                tmp = path.with_name(path.name + '.tmp')
                tmp.write_bytes(picture)
                os.replace(tmp, path)
            there.append([place, p['id']])
        except PACK_ERRORS:
            continue                        # no body art, or art the composer refused: no portrait
    keep = {place for place, _ in there}
    stale = [folder / ('%d.png' % place) for place in newfighter.PLACES if place not in keep]
    if folder.is_dir():     # a number's portrait whose pack is gone (stock portraits are below 1000)
        for path in folder.glob('*.png'):
            if path.stem.isdigit() and newfighter.is_logical(int(path.stem)) and int(path.stem) not in keep:
                stale.append(path)
    for path in stale:
        if path.is_file():
            try:
                path.unlink()
            except OSError:
                pass
    return there


def skin_portraits(data, packs, template, missing_only=False):
    """<data>/online-art/skins/<pack id>.png, the picture of each applied skin whose pack has body art (made like a
    new fighter's hub portrait, ``hub_portraits``), for the skin window of the select screens and the online hub; a
    picture of no applied skin is removed. ``template(entry)`` as for ``hub_portraits``. ``missing_only``: draw only the pictures that are not
    there (the inputs did not change). Returns the pack ids with a picture."""
    folder = Path(data).joinpath(*SKIN_ART)
    there = []
    for p in packs:
        if not is_skin(p):
            continue
        try:
            ops = [op for op in read_install(p['folder']).get('ops', []) if op['op'] == 'new-fighter']
            if not ops:
                continue
            path = folder / ('%s.png' % p['id'])
            if missing_only and path.is_file():
                there.append(p['id'])
                continue
            body = pack_reader(p['folder'])(fighterart.BODY)
            try:
                team = template('screens/portrait/p_CT/p_CT_%d.big' % (ops[0]['template_fe_id'] + 1))
            except PACK_ERRORS:
                team = None
            picture = fighterart.hub_portrait(body, team)
            if not path.is_file() or path.read_bytes() != picture:
                folder.mkdir(parents=True, exist_ok=True)
                tmp = path.with_name(path.name + '.tmp')
                tmp.write_bytes(picture)
                os.replace(tmp, path)
            there.append(p['id'])
        except PACK_ERRORS:
            continue                        # no body art, or art the composer refused: the window shows the name
    if folder.is_dir():
        for path in folder.glob('*.png'):
            if path.stem not in there:
                try:
                    path.unlink()
                except OSError:
                    pass
    return there


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


def compose(data, game, packs, off=None, root=None, log=print, languages=None, today=None, reserved=()):
    """Compose ``packs`` ([{id, version, content_sha256, kind, order, folder, fighter_fe_id, reviewed, bundle,
    bundle_name}], every synced pack with an install.json; ``fighter_fe_id`` the catalogue's place of a new
    fighter, ``reviewed`` the catalogue's review date of the version (``new_packs``; ``today``: the UTC date it
    is measured from, now when None), ``bundle`` the pack of several mods it belongs to, applied all or none) for the player, and once more with each installed language pack (``languages``: the list
    ``language_packs`` makes, found when None). Returns {applied, skipped, files, lang_files, lang_skipped,
    unchanged}. ``reserved``: the signed catalogue's new-fighter places (settle_places); a player's own new
    fighter whose place another one has moves (its pack dict's ``fighter_fe_id`` changes: the caller keeps it)."""
    data = Path(data)
    root = Path(root) if root else data / 'mod-manager'
    root.mkdir(parents=True, exist_ok=True)
    off = read_off(data) if off is None else set(off)
    recover(root)
    packs = sorted(packs, key=lambda p: (p.get('order', 1 << 30), p['id']))
    # content packs and the FFNY team's official maps (build 86) are always on; only replacements switch off
    switched = sorted(p['id'] for p in packs if p.get('kind') not in ALWAYS_ON and p['id'] in off)
    chosen = [p for p in packs if p['id'] not in switched]
    settle_places(chosen, reserved, log)
    languages = language_packs(data, off, log) if languages is None else list(languages)
    fresh = new_packs(chosen, today)
    key = inputs_key(game, chosen, switched, languages, fresh)
    state_path = root / STATE
    try:
        state = json.loads(state_path.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        state = None
    if unchanged(root, state, key, data, game):
        ids = set(state['applied'])
        screens = Bases(data, game)
        hub_portraits(data, [p for p in chosen if p['id'] in ids],
                      lambda entry: screens.read(newfighter.SCREENS_ARCHIVE, entry), state.get('hub', []))
        skin_portraits(data, [p for p in chosen if p['id'] in ids],
                       lambda entry: screens.read(newfighter.SCREENS_ARCHIVE, entry), missing_only=True)
        return {'applied': state['applied'], 'skipped': [tuple(s) for s in state.get('skipped', [])],
                'files': state.get('files', []), 'lang_files': [tuple(x) for x in state.get('lang_files', [])],
                'lang_skipped': [tuple(s) for s in state.get('lang_skipped', [])], 'unchanged': True}
    bases = Bases(data, game)
    work, applied, skipped, used = run_whole(bases, chosen, log, fresh)
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
                written = compose_language(lang, used, applied, touched, data, game, bases, new, tmp, fresh)
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
    hub = hub_portraits(data, applied, lambda entry: work.current(newfighter.SCREENS_ARCHIVE, entry))
    skin_portraits(data, applied, lambda entry: work.current(newfighter.SCREENS_ARCHIVE, entry))
    state = {'schema': SCHEMA, 'key': key, 'bases': bases.seen, 'applied': [p['id'] for p in applied],
             'skipped': skipped, 'files': files, 'lang_files': lang_files, 'lang_skipped': lang_skipped, 'hub': hub,
             'set': set_hash(applied, switched) if applied else ''}
    tmp = root / (STATE + '.tmp')
    tmp.write_text(json.dumps(state, sort_keys=True), encoding='utf-8')
    os.replace(tmp, state_path)
    return {'applied': state['applied'], 'skipped': skipped, 'files': files, 'lang_files': lang_files,
            'lang_skipped': lang_skipped, 'unchanged': False}
