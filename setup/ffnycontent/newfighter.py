# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""A new fighter as an extra roster entry: a copy of a template's Character row with its own front-end ID, name,
display name and model (``docs/research/new-fighters.md``).

Battle mode, the select screen and ``Game.StartGame`` take a fighter from the **Character table of
``assets/djv2.sod``** (table hash ``0x4CE5B1B2``, 26-word rows), not from ``fighter.xml``. The table has:

* two class blocks: class ``0x0AD29D29`` keyed by the row's word 13 (1 = a fighter with its own select cell,
  0 = an alternate outfit: D-Mob 2/3, Sticky 2, Blaze 2/3, E3 Hero), and class ``0x4CE5B1B2`` keyed by the
  row-name hash; both sorted. A row's item ID (the fighter unlock set, ``table << 24 | row``) is its position,
  so new rows go last in both: key 1 in the first, a name whose hash is above every other in the second;
* one plain row list keyed by the front-end ID (field ``0xB03E16FC`` = word 8), sorted by it.

Row words (field = schema key XOR the table hash, ``scripts/sod-characters.py``): 0 name, 2 Charisma, 4 Cost,
5 display name, 6 Entity, 7 Ethnicity, 8 feID, 9 FightingStyle, 10 coreID, 11 Grapple, 12 Health, 13 main
cell (1) or outfit (0), 14 Model, 15 Order, 16 and 17 two more stats, 18 style bits, 19 Sex, 20 Shadow,
21 speechID, 22 Speed, 23 Thumb, 24 tier bits, 25 Toughness.

The new row keeps the template's coreID (moves, styles, AI and animation banks are per coreID), so no
per-character data is needed. Standard library only; reads and writes the player's own files.
"""

import re
import struct

from .sod import Sod, name_hash
from .sodwrite import name_above

CHARACTER = 0x4CE5B1B2
FE_ID_FIELD = 0xB03E16FC
ROW_WORDS = 26
W_NAME, W_CHARISMA, W_COST, W_DISPLAY, W_ENTITY, W_FE_ID, W_CORE_ID, W_HEALTH, W_MAIN, W_MODEL = \
    0, 2, 4, 5, 6, 8, 10, 12, 13, 14
W_SPEED, W_TOUGHNESS, W_SPEECH = 22, 25, 21
FIRST_NEW_FE_ID = 100
LAST_NEW_FE_ID = 127        # the match setup reads the fighter byte signed: 128 crashes the load
STOCK_ROWS = 75


class FighterError(ValueError):
    pass


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _block(b, off):
    tag, count, stride = struct.unpack_from("<4sII", b, off)
    keys = list(struct.unpack_from("<%dI" % count, b, off + 12))
    vals = list(struct.unpack_from("<%dI" % (count * stride), b, off + 12 + 4 * count)) if stride else \
        list(struct.unpack_from("<%dI" % count, b, off + 12))
    return tag, count, stride, keys, vals


def characters(blob):
    """[{index, offset, words, name, display, model, fe_id, core_id, main}] of the Character table, in ID
    order (the first class block)."""
    s = Sod(blob)
    t = s.table_hashes.index(CHARACTER)
    off = s.table_offsets[t]
    classes = _u32(s.blob, off + 0x10)
    first = _u32(s.blob, off + 0x18 + 4 * classes)
    _, count, stride, _, vals = _block(s.blob, first)
    out = []
    for i, p in enumerate(vals[::stride]):
        w = list(struct.unpack_from("<%dI" % ROW_WORDS, s.blob, p))
        out.append({"index": i, "offset": p, "words": w, "name": s.string(w[W_NAME]),
                    "display": s.string(w[W_DISPLAY]), "model": s.string(w[W_MODEL]), "fe_id": w[W_FE_ID],
                    "core_id": w[W_CORE_ID], "main": w[W_MAIN]})
    return out


def append_character(blob, template_fe_id, fe_id, name, display, model=None, edits=None):
    """``djv2.sod`` with a new Character row copied from the row of ``template_fe_id``: its own ``fe_id``
    (100..127), row name (``name``, made to hash above every other row name), display name and model
    (default: the template's). ``edits`` maps row word indices to new values (stats). Returns
    (bytes, row name, item ID).

    Everything new goes after the last byte (every pointer is an absolute file offset): the row, the two class
    blocks and the feID list each one entry longer, and a new string table. The table header's row count and
    the three block pointers and the file header's string-table offset are updated in place; no existing
    row moves or changes its ID."""
    if not FIRST_NEW_FE_ID <= fe_id <= LAST_NEW_FE_ID:
        raise FighterError("a new fighter's feID is %d..%d" % (FIRST_NEW_FE_ID, LAST_NEW_FE_ID))
    s = Sod(blob)
    t = s.table_hashes.index(CHARACTER)
    off = s.table_offsets[t]
    rows = characters(blob)
    by_fe = {r["fe_id"]: r for r in rows}
    if fe_id in by_fe:
        raise FighterError("feID %d is in the Character table already" % fe_id)
    if template_fe_id not in by_fe:
        raise FighterError("no Character row with feID %d" % template_fe_id)
    tpl = by_fe[template_fe_id]
    out = bytearray(s.blob)

    def put(o, v):
        struct.pack_into("<I", out, o, v)

    def append(data):
        while len(out) % 16:
            out.append(0)
        at = len(out)
        out.extend(data)
        return at

    classes = _u32(out, off + 0x10)
    name_block_at = None
    blocks = []
    for c in range(classes):
        at = off + 0x18 + 4 * classes + 4 * c
        blocks.append((c, at, _u32(out, off + 0x18 + 4 * c)))
    # the name-keyed class is the one whose class hash is the table's own
    for c, at, class_hash in blocks:
        if class_hash == CHARACTER:
            name_block_at = at
    if name_block_at is None:
        raise FighterError("no name-keyed class block in the Character table")
    _, _, _, name_keys, _ = _block(out, _u32(out, name_block_at))
    row_name = name_above(name, max(name_keys))
    # strings
    texts = [row_name, display] + ([model] if model else [])
    pool = []
    for text in texts:
        pool.append(append(text.encode("latin-1") + b"\0"))
    first = len(s.strings)
    strt = append(b"STRT" + struct.pack("<I", first + len(texts))
                  + s.blob[s.strings_offset + 8:s.strings_offset + 8 + 8 * first]
                  + b"".join(struct.pack("<II", name_hash(t_), p) for t_, p in zip(texts, pool)))
    put(20, strt)
    # the row
    words = list(tpl["words"])
    words[W_NAME] = first
    words[W_DISPLAY] = first + 1
    if model:
        words[W_MODEL] = first + 2
    words[W_FE_ID] = fe_id
    words[W_MAIN] = 1
    for k, v in (edits or {}).items():
        if k in (W_NAME, W_DISPLAY, W_MODEL, W_FE_ID, W_CORE_ID, W_MAIN):
            raise FighterError("word %d is not editable here" % k)
        words[k] = v
    row = append(struct.pack("<%dI" % ROW_WORDS, *words))
    count = _u32(out, off + 0xC)
    # class blocks: new entry last (key 1 in the 0/1 class, the row name's hash in the name class)
    for c, at, class_hash in blocks:
        tag, n, stride, keys, vals = _block(out, _u32(out, at))
        key = name_hash(row_name) if class_hash == CHARACTER else words[W_MAIN]
        if keys and key < max(keys):
            raise FighterError("class block %d: the new key %08x does not sort last" % (c, key))
        keys.append(key)
        vals += [row] + [0] * (stride - 1)
        put(at, append(tag + struct.pack("<II", n + 1, stride) + struct.pack("<%dI" % len(keys), *keys)
                       + struct.pack("<%dI" % len(vals), *vals)))
    # plain row lists (stride 0): the feID list, kept sorted by feID
    lists = _u32(out, off + 8)
    m = _u32(out, lists)
    for i in range(m):
        at = lists + 8 + 4 * m + 4 * i
        list_hash = _u32(out, lists + 8 + 4 * i)
        tag, n, stride, ptrs, _ = _block(out, _u32(out, at))
        if stride != 0:
            continue
        if list_hash ^ CHARACTER == FE_ID_FIELD:
            fes = [_u32(out, p + 4 * W_FE_ID) for p in ptrs]
            pos = len([f for f in fes if f < fe_id])
            ptrs.insert(pos, row)
        else:
            ptrs.append(row)
        put(at, append(tag + struct.pack("<II", n + 1, 0) + struct.pack("<%dI" % len(ptrs), *ptrs)))
    put(off + 0xC, count + 1)
    return bytes(out), row_name, (t << 24) | count


# -- entity.xml / fighter.xml -------------------------------------------------------------------------------------

def entity_blocks(entity_text, template_model, model):
    """The template model's ``<entity>`` (named after the model, ``V2IP_109A``) copied for ``model``."""
    for b in re.finditer(r"<entity>.*?</entity>", entity_text, re.S):
        text = b.group(0)
        first = re.search(r"<name>\s*([^<]*?)\s*</name>", text)
        if first and first.group(1).upper() == template_model.upper():
            return re.sub(re.escape(template_model), model, text, flags=re.I)
    raise FighterError("no entity.xml entity for %s" % template_model)


def add_entity(entity_bytes, template_model, model):
    text = entity_bytes.decode("latin-1")
    if re.search(r"<name>\s*%s\s*</name>" % re.escape(model), text, re.I):
        return entity_bytes
    block = entity_blocks(text, template_model, model)
    end = text.rindex("</entitylist>")
    return (text[:end].rstrip("\r\n") + "\r\n\t" + block + "\r\n" + text[end:]).encode("latin-1")


def fighter_record(fighter_text, template_fe_id, fe_id, hub_name, model=None):
    """The template's ``<fighter>`` in fighter.xml (the one whose <ID> is ``template_fe_id``) copied with the
    new ID and hub name (``Doc 2 (MAR)``: the template's style letters kept)."""
    for b in re.finditer(r"<fighter>.*?</fighter>", fighter_text, re.S):
        text = b.group(0)
        if re.search(r"<ID>\s*%d\s*</ID>" % template_fe_id, text):
            disp = re.search(r"<dispname>([^<]*)</dispname>", text).group(1)
            style = re.search(r"\(([A-Z]{3})\)\s*$", disp)
            new_disp = "%s (%s)" % (hub_name, style.group(1)) if style else hub_name
            text = re.sub(r"(<dispname>)[^<]*(</dispname>)", lambda m: m.group(1) + new_disp + m.group(2), text)
            text = re.sub(r"(<ID>)[^<]*(</ID>)", lambda m: m.group(1) + str(fe_id) + m.group(2), text)
            if model:
                text = re.sub(r"(<model>)[^<]*(</model>)", lambda m: m.group(1) + model + m.group(2), text)
            return text
    raise FighterError("no fighter.xml record with ID %d" % template_fe_id)


def add_fighter(fighter_bytes, template_fe_id, fe_id, hub_name, model=None):
    text = fighter_bytes.decode("latin-1")
    record = fighter_record(text, template_fe_id, fe_id, hub_name, model)
    end = text.rindex("</fighterlist>") if "</fighterlist>" in text else text.rindex("</")
    return (text[:end].rstrip("\r\n") + "\r\n\t" + record + "\r\n" + text[end:]).encode("latin-1")


# -- menu pictures --------------------------------------------------------------------------------------------------

# The per-fighter art in screens/screens.viv, loaded by path (select screen, loading and summary cards). A family
# whose name starts with p_ (and the bare portrait/<n>_<k>p) is numbered feID + 1, the others feID.
PICTURE_FAMILIES = (
    ("screens/portrait/p_CF/p_cf_%d_%dp.big", 1, (2, 3, 4)),
    ("screens/portrait/l_CF/l_CF_%d_%dp.big", 0, (2, 3, 4)),
    ("screens/match/3_fighters/p_ML_%d_%dp.big", 1, (2, 3, 4)),
    ("screens/match/3_fighters/l_ML_%d_%dp.big", 0, (2, 3, 4)),
    ("screens/portrait/I_ML_%d_%dp.big", 0, (2, 3, 4)),
    ("screens/portrait/l_MS_%d_%dp.big", 0, (2, 3, 4)),
    ("screens/portrait/t_MS/t_MS_%d_%dp.big", 0, (2, 3, 4)),
    ("screens/portrait/%d_%dp.big", 1, (2, 3)),
    ("screens/portrait/p_CT/p_CT_%d.big", 1, None),
    ("screens/portrait/t_CT/t_CT_%d.big", 0, None),
    ("screens/portrait/t_CO/t_CO_%d.big", 0, None),
    ("screens/thumbs/thumb%d.big", 0, None),
)


def picture_copies(archive, template_fe_id, fe_id, rename_inner=True):
    """[(new entry name, stored bytes)]: the template's menu pictures under the new ID (placeholders). Each is a
    small BIG whose entries are named after it (``p_cf_56_2p.apt/.const/.o/.xsh``); with ``rename_inner`` the
    copy's entries take the new name too. ``archive`` is a ``viv.Archive`` of screens.viv."""
    from . import viv
    names = {n.lower(): n for n in archive.names()}
    out = []
    for pattern, offset, players in PICTURE_FAMILIES:
        for k in (players or (None,)):
            args_old = (template_fe_id + offset,) + ((k,) if k else ())
            args_new = (fe_id + offset,) + ((k,) if k else ())
            old, new = pattern % args_old, pattern % args_new
            if old.lower() not in names or new.lower() in names:
                continue
            raw = archive.raw(names[old.lower()])
            if rename_inner:
                inner = viv.Archive(viv.unwrap(raw))
                stem_old = old.rsplit("/", 1)[1][:-4]
                stem_new = new.rsplit("/", 1)[1][:-4]
                files = []
                for e in inner.entries:
                    n = e.name
                    if n.lower().startswith(stem_old.lower()):
                        n = stem_new + n[len(stem_old):]
                    files.append((n, inner.raw(e)))
                raw = viv.refpack_store(viv.build_archive(files, magic=inner.magic, size_le=bool(inner.size_le)))
            out.append((new, raw))
    return out


def model_banks(v2ta, template_model, model):
    """[(entry, bytes)]: the template model's camera banks in ``assets/v2ta.viv`` (``CE_109A.viv``, the entrance,
    and ``CI_109A.viv``, the intro; loaded by the model's code) copied for ``model`` with their inner entries
    renamed (``CE_109A_EYE.o`` -> ``CE_901A_EYE.o``) and the symbols in them. Without them a fight with the new model stalls in the
    intro with no fighters (measured 2026-11-11). ``v2ta`` is a ``viv.Archive``; the bytes are plain archives."""
    from . import viv
    old = template_model.upper().replace("V2IP_", "")
    new = model.upper().replace("V2IP_", "")
    names = {n.lower(): n for n in v2ta.names()}
    out = []
    for prefix in ("CE_", "CI_"):
        src = names.get(("%s%s.viv" % (prefix, old)).lower())
        if src is None or ("%s%s.viv" % (prefix, new)).lower() in names:
            continue
        inner = viv.Archive(v2ta.read(src))
        # the entries' own symbols carry the code too (CI_109A_s, CE_109A_EYE...): renamed, same length
        if len(old) != len(new):
            raise FighterError("a model copy keeps the code length (%s -> %s)" % (old, new))
        files = [(re.sub(re.escape(old), new, e.name, flags=re.I),
                  viv.unwrap(inner.raw(e)).replace(b"_" + old.encode("latin-1"), b"_" + new.encode("latin-1")))
                 for e in inner.entries]
        out.append(("%s%s.viv" % (prefix, new), viv.build_archive(files, magic=inner.magic,
                                                                  size_le=bool(inner.size_le))))
    return out


def rename_model_symbols(blob, old_model, new_model):
    """A fighter model's ``.o`` (an EAGL ELF) names its parts after the model (``V2IP_109A_alpha``,
    ``__GP_RM_V2IP_109A_alpha_0``...), and the game finds them by the model name it loads: a copy under a new
    name needs them renamed. Same-length names only (the string table keeps its offsets)."""
    old, new = old_model.encode("latin-1"), new_model.encode("latin-1")
    if len(old) != len(new):
        raise FighterError("a model copy keeps the name length (%s -> %s)" % (old_model, new_model))
    return blob.replace(old, new)


def add_taunt(taunt_bytes, template_model, model):
    """``assets/taunt.xml`` with a ``<taunt>`` for ``model``: the template's, its intro and extro camera banks
    (``CI_109A``/``CE_109A``) renamed after the model's code. Without one a fight with the new model stalls in the
    intro (measured 2026-11-11)."""
    text = taunt_bytes.decode("latin-1")
    if re.search(r"<name>\s*%s\s*</name>" % re.escape(model), text, re.I):
        return taunt_bytes
    old = template_model.upper().replace("V2IP_", "")
    new = model.upper().replace("V2IP_", "")
    for b in re.finditer(r"<taunt>.*?</taunt>", text, re.S):
        if re.search(r"<name>\s*%s\s*</name>" % re.escape(template_model), b.group(0), re.I):
            block = re.sub(re.escape(old), new, b.group(0), flags=re.I)
            at = b.end()        # right after the template's, inside the same list
            return (text[:at] + "\r\n\r\n\t" + block + text[at:]).encode("latin-1")
    raise FighterError("no taunt.xml entry for %s" % template_model)
