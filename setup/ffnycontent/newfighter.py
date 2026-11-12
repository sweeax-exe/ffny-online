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

One implementation for both sides: FFNY Studio (``ffnystudio/newfighter.py``) and the player's install
(``setup/ffnycontent/newfighter.py`` of the game repository) hold the same text below their first comment lines
(Studio's tests compare the two when both are there).
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
    """[(new entry name, bytes)]: the template's menu pictures under the new ID (placeholders), as plain (not
    RefPack-stored) archives. Each is a small BIG whose entries are named after it (``p_cf_56_2p.apt/.const/.o/
    .xsh``); with ``rename_inner`` the copy's entries take the new name too (without it the picture stays blank,
    measured 2026-11-11). ``archive``: anything with ``names()`` and ``read(name)`` (the unwrapped bytes) of
    screens.viv, e.g. a ``viv.Archive``."""
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
            raw = archive.read(names[old.lower()])
            if rename_inner:
                inner = viv.Archive(raw)
                stem_old = old.rsplit("/", 1)[1][:-4]
                stem_new = new.rsplit("/", 1)[1][:-4]
                files = []
                for e in inner.entries:
                    n = e.name
                    if n.lower().startswith(stem_old.lower()):
                        n = stem_new + n[len(stem_old):]
                    files.append((n, inner.raw(e)))
                raw = viv.build_archive(files, magic=inner.magic, size_le=bool(inner.size_le))
            out.append((new, raw))
    return out


def model_banks(v2ta, template_model, model):
    """[(entry, bytes)]: the template model's camera banks in ``assets/v2ta.viv`` (``CE_109A.viv``, the entrance,
    and ``CI_109A.viv``, the intro; loaded by the model's code) copied for ``model`` with their inner entries
    renamed (``CE_109A_EYE.o`` -> ``CE_7K3A_EYE.o``) and the symbols in them. Without them a fight with the new
    model stalls in the intro with no fighters (measured 2026-11-11). ``v2ta``: anything with ``names()`` and
    ``read(name)`` (a ``viv.Archive``); the bytes are plain archives."""
    from . import viv
    old = template_model.upper().replace("V2IP_", "")
    new = model.upper().replace("V2IP_", "")
    if len(old) != len(new):
        raise FighterError("a model copy keeps the code length (%s -> %s)" % (old, new))
    names = {n.lower(): n for n in v2ta.names()}
    out = []
    for prefix in ("CE_", "CI_"):
        src = names.get(("%s%s.viv" % (prefix, old)).lower())
        if src is None or ("%s%s.viv" % (prefix, new)).lower() in names:
            continue
        inner = viv.Archive(v2ta.read(src))
        # the entries' own symbols carry the code too (CI_109A_s, CE_109A_EYE...): renamed, same length
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
    """``assets/IntroExtro.xml`` (in nis.viv; the loose ``assets/taunt.xml`` is the same list and is never read)
    with a ``<taunt>`` for ``model``: the template's, its intro and extro camera banks (``CI_109A``/``CE_109A``)
    renamed after the model's code. Without one a fight with the new model stalls in the intro (measured
    2026-11-11)."""
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


# -- the new-fighter pack (FFNY Studio "Add as new fighter"; the launcher's install) --------------------------------
#
# A new-fighter pack's install.json (type "new-fighter", kind content) has
#   * entries: the new model's own files in assets/v2ip.viv, mode "add": <model>.o, <model>.xsh and <model>_F.o,
#     each a delta against the template's entry of the same suffix (the model's own symbols renamed, so the
#     game finds them under the new name);
#   * one op {"op": "new-fighter", "template_fe_id": 55, "template_model": "V2IP_109A", "model": "V2IP_7K3A",
#     "name": "Doc 2"}: everything else is derived from the player's own files when the pack is installed
#     (``apply_op``): the Character row in djv2.sod, the entity, the IntroExtro entry, the camera banks and
#     placeholder menu pictures.
#
# The front-end ID (100..127) is not in the pack: the account service gives every accepted new fighter its place
# and the signed catalogue carries it (``fighter_fe_id``), so every player with the same packs gets the same IDs
# whatever else is installed. A pack installed by hand or tested from Studio says it in the op (``fe_id``).

SOD_PATH = "assets/djv2.sod"
ENTITY_XML = "assets/entity.xml"
MODEL_ARCHIVE = "assets/v2ip.viv"
NIS_ARCHIVE = "assets/nis.viv"
INTRO_EXTRO = "assets/IntroExtro.xml"
BANK_ARCHIVE = "assets/v2ta.viv"
SCREENS_ARCHIVE = "screens/screens.viv"
OP = "new-fighter"
OP_KEYS = ("op", "template_fe_id", "template_model", "model", "name")
OP_OPTIONAL = ("gameplay",)     # the fighter's own stats, styles and Blazins (``gameplay_problems``)
MODEL_SUFFIXES = (".o", ".xsh", "_F.o")
TEMPLATE_MODEL = re.compile(r"^V2IP_[0-9]{3}[A-Z]$")
NEW_MODEL = re.compile(r"^V2IP_7[0-9A-Z]{2}A$")
NAME_MAX = 11               # the row name gets an 8-character suffix and must stay under 20 bytes
NAME_CHARS = re.compile(r"^[A-Za-z0-9'.!-]+( [A-Za-z0-9'.!-]+)*$")
# fighters with their own select cell: the templates a new fighter may copy (not the alternate outfits 80..84, 98)
OUTFIT_FE_IDS = (80, 81, 82, 83, 84, 98)
# the stock fighters' display names (djv2.sod word 5, fighter.xml): a new fighter takes none of them
STOCK_NAMES = (
    "Trejo", "Ghostface Killah", "Capone", "Ludacris", "Baxter", "Kimora", "Shawnna", "Trick", "Joe Budden", "Meca",
    "Bo", "Baby Chris", "Xzibit", "Teck", "Fam-Lay", "Santos", "Starks", "Freeway", "Skull", "Solo", "Sticky Fingaz",
    "Elephant Man", "Cruz", "Shaniqua", "Jacob", "House", "Manny", "Havoc", "Lil' Kim", "Jervis", "Memphis Bleek",
    "Doc", "Carmen Electra", "Bonecrusher", "WC", "Comp", "Bubba Sparxxx", "Scarface", "Sean Paul", "Masa", "Bless",
    "Rome", "Erick Sermon", "Nyne", "Ice-T", "Rollins", "Stingray", "Mack 10", "O.E.", "Crazy Legs", "Flava Flav",
    "Prodigy", "Magic", "Lil' Flip", "Crow", "Slick Rick", "Warren G", "NORE", "D-Mob", "Dan G", "Pockets", "Suspect",
    "Blaze", "Crack", "Banner", "Lauren", "Snowman", "Chiang", "Cindy J", "Hero")


def name_problems(name):
    """Why ``name`` cannot be a new fighter's name in the game ([] when it can), in words for the mod creator.
    The name is the HUD's (upper-cased by the game) and the select screen's, and the base of the row name."""
    name = name if isinstance(name, str) else ""
    if not name.strip():
        return ["give your fighter a name for the game"]
    out = []
    if len(name.encode("latin-1", "replace")) > NAME_MAX:
        out.append("%d characters is %d too many: the game takes at most %d (it adds its own ending to the name)"
                   % (len(name), len(name) - NAME_MAX, NAME_MAX))
    if not NAME_CHARS.match(name):
        out.append("letters A-Z, digits, single spaces and ' . ! - only, no space at the start or end")
    if name.strip().lower() in {n.lower() for n in STOCK_NAMES}:
        out.append("%s is a fighter of the game already; add something (e.g. \"%s 2\")" % (name.strip(),
                                                                                        name.strip()[:9]))
    return out


def model_for(pack_id):
    """The new model's name for a pack: ``V2IP_7`` + two letters or digits from the pack id's hash + ``A``. Same
    length as the game's (``V2IP_109A``), so the symbol rename keeps every offset; no stock model starts with 7.
    The account service refuses a model another pack holds (its entries are claimed)."""
    import hashlib
    alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    n = int.from_bytes(hashlib.sha256(pack_id.encode("utf-8")).digest()[:4], "big")
    return "V2IP_7%s%sA" % (alphabet[n % 36], alphabet[(n // 36) % 36])


def make_op(template_fe_id, template_model, model, name, fe_id=None, gameplay=None):
    op = {"op": OP, "template_fe_id": int(template_fe_id), "template_model": template_model, "model": model,
          "name": name}
    if fe_id is not None:
        op["fe_id"] = int(fe_id)
    if gameplay is not None:
        op["gameplay"] = gameplay
    return op


def op_problems(op, allow_fe_id=False):
    """What is wrong with a new-fighter op ([] when nothing): the account service's rules; ``allow_fe_id`` for a
    pack installed by hand or tested from Studio (the server gives a submitted pack its place)."""
    if not isinstance(op, dict) or op.get("op") != OP:
        return ["not a new-fighter op"]
    keys = set(OP_KEYS) | set(OP_OPTIONAL) | ({"fe_id"} if allow_fe_id else set())
    out = []
    if "fe_id" in op and not allow_fe_id:
        out.append("leave fe_id out; the server gives a new fighter its place (100 to 127)")
    extra = sorted(set(op) - keys - {"fe_id"})
    if extra or any(k not in op for k in OP_KEYS):
        out.append("a new-fighter op holds %s (and optionally %s)" % (", ".join(OP_KEYS), ", ".join(OP_OPTIONAL)))
    t = op.get("template_fe_id")
    if not (isinstance(t, int) and not isinstance(t, bool) and 0 <= t < FIRST_NEW_FE_ID and t not in OUTFIT_FE_IDS):
        out.append("template_fe_id must be a stock fighter with its own select cell")
    if not TEMPLATE_MODEL.match(str(op.get("template_model", ""))):
        out.append("template_model must be a stock model (V2IP_109A)")
    if not NEW_MODEL.match(str(op.get("model", ""))):
        out.append("model must be V2IP_7xxA")
    if "fe_id" in op and allow_fe_id:
        f = op["fe_id"]
        if not (isinstance(f, int) and not isinstance(f, bool) and FIRST_NEW_FE_ID <= f <= LAST_NEW_FE_ID):
            out.append("fe_id must be %d..%d" % (FIRST_NEW_FE_ID, LAST_NEW_FE_ID))
    out += ["name: " + p for p in name_problems(op.get("name"))]
    if "gameplay" in op:
        out += ["gameplay: " + p for p in gameplay_problems(op["gameplay"])]
    return out


def texture_name(model):
    """The main texture's name in a fighter model's files: the outfit letter and the number (``V2IP_109A`` ->
    ``A109``; the .xsh holds it, the .o names it twice, as the shape and in ``SHAPENAME=A109``)."""
    code = model.upper().replace("V2IP_", "")
    return code[-1] + code[:-1]


def model_entries(template_model, model, originals, results=None):
    """The new model's entries: [(entry, base entry, base bytes, result bytes)] for each suffix the template has
    (``originals`` {suffix: the template's bytes}); ``results`` {suffix: bytes} replaces a file (an imported body,
    textures painted into the .xsh); the rest are the template's. Every result gets the model's own symbols
    renamed (``rename_model_symbols``) and its main texture too (``A109`` -> ``A7K3``), so it never shares a name
    with the template's while both are in a fight."""
    out = []
    tex_old, tex_new = texture_name(template_model).encode("latin-1"), texture_name(model).encode("latin-1")
    for suffix in MODEL_SUFFIXES:
        if suffix not in originals:
            continue
        base = originals[suffix]
        result = (results or {}).get(suffix)
        result = rename_model_symbols(base if result is None else result, template_model, model)
        out.append((model + suffix, template_model + suffix, base, result.replace(tex_old, tex_new)))
    return out


class _View:
    """One archive of ``files`` as ``picture_copies`` and ``model_banks`` read it."""

    def __init__(self, files, rel):
        self.files, self.rel = files, rel

    def names(self):
        return self.files.names(self.rel)

    def read(self, name):
        return self.files.current(self.rel, name)


def apply_op(files, op, fe_id, pictures=True):
    """The edits of one new-fighter op on the player's files as the packs so far left them: [(where, path,
    entry, bytes)], ``where`` "loose" (a whole file, entry None), "replace" or "add" (an archive entry; plain bytes,
    the caller stores them). ``files`` gives ``current(path, entry="")`` (bytes; a missing file or entry raises
    KeyError or OSError) and ``names(path)`` (an archive's entry names, the added ones too). ``fe_id`` is the
    place (100..127). Raises FighterError when the op does not fit (the ID or the model is taken, no such
    template)."""
    problems = op_problems(op, allow_fe_id=True)
    if problems:
        raise FighterError(problems[0])
    if not (isinstance(fe_id, int) and FIRST_NEW_FE_ID <= fe_id <= LAST_NEW_FE_ID):
        raise FighterError("a new fighter needs its place (front-end ID %d..%d)" % (FIRST_NEW_FE_ID, LAST_NEW_FE_ID))
    template_fe_id, template, model, name = op["template_fe_id"], op["template_model"], op["model"], op["name"]
    sod_blob = files.current(SOD_PATH)
    rows = {r["fe_id"]: r for r in characters(sod_blob)}
    if template_fe_id not in rows or rows[template_fe_id]["model"].upper() != template.upper():
        raise FighterError("%s is not fighter %d of your game" % (template, template_fe_id))
    if any(r["model"].upper() == model.upper() for r in rows.values()):
        raise FighterError("the model %s is another fighter's already" % model)
    edits = []
    words = gameplay_words(op["gameplay"]) if "gameplay" in op else None
    sod_out, _, _ = append_character(sod_blob, template_fe_id, fe_id, name, name, model, edits=words)
    edits.append(("loose", SOD_PATH, None, sod_out))
    edits.append(("loose", ENTITY_XML, None, add_entity(files.current(ENTITY_XML), template, model)))
    nis = next((n for n in files.names(NIS_ARCHIVE) if n.lower() == INTRO_EXTRO.lower()), None)
    if nis is None:
        raise FighterError("%s has no %s" % (NIS_ARCHIVE, INTRO_EXTRO))
    edits.append(("replace", NIS_ARCHIVE, nis, add_taunt(files.current(NIS_ARCHIVE, nis), template, model)))
    banks = model_banks(_View(files, BANK_ARCHIVE), template, model)
    if len(banks) != 2:
        raise FighterError("the camera banks of %s are missing, or %s has its own already" % (template, model))
    edits += [("add", BANK_ARCHIVE, n, data) for n, data in banks]
    if pictures:
        edits += [("add", SCREENS_ARCHIVE, n, data)
                  for n, data in picture_copies(_View(files, SCREENS_ARCHIVE), template_fe_id, fe_id)]
    return edits


# -- the fighter's own gameplay (stats, fighting styles, Blazins) ---------------------------------------------------
#
# A new fighter fights on its template's gameplay unless its op has a ``gameplay`` block
# (``docs/research/new-fighter-gameplay.md`` in the game repository):
#
#   "gameplay": {"version": 1,
#                "stats": {"upper": 9, "lower": 7, "speed": 10, "toughness": 6, "health": 9, "grapple": 7,
#                          "charisma": 8},                  # each 1..15 (grapple 0..15), the sum at most STAT_BUDGET
#                "styles": ["K", "S", "M"],                 # optional: 1..3 styles, the first the main one;
#                                                           # left out: the template's own moves and styles
#                "blazins": [34, 7, 51, 12],                # four Blazin ids 0..81 (keys 0..3; a stock fighter
#                                                           # has its own one in all four)
#                "bars": {"upper": 30, ...},                # optional: the select screen's bars 0..50
#                                                           # (U L S T H C); left out: made from the stats
#                "voice": {"mode": "template"}}             # optional, reserved: the template's voice for now
#
# The game applies it at match set-up the way it applies a created fighter's (the per-slot stat block, the
# style-combination core 86..140 and the four Blazin keys), the same on every PC: the values live in two words
# of the fighter's Character row the game never reads (word 11: the stats, word 3: the Blazins; the moveset
# split over both; ``src/hooks/fighter_gameplay.h``), so they are in the signed database and the fight-file print.

STAT_KEYS = ("upper", "lower", "speed", "toughness", "health", "grapple", "charisma")
STAT_NAMES = ("Upper body", "Lower body", "Speed", "Toughness", "Health", "Grapple", "Charisma")
STAT_MIN, STAT_MAX = 1, 15
GRAPPLE_MIN = 0             # the sixth stat: 0 for every stock and created fighter (its effect is not measured)
STAT_BUDGET = 60            # the largest stock stat sum (all seven), measured; the game and the server use the same
BAR_KEYS = ("upper", "lower", "speed", "toughness", "health", "charisma")
BAR_WORDS = (17, 16, 22, 25, 12, 2)     # the select screen's U L S T H C bars, 0..50
BAR_MAX = 50
STYLES = ("B", "K", "S", "W", "M")      # the created fighters' archetype letters, in the game's order
STYLE_NAMES = {"B": "Street Fighting", "K": "Kickboxing", "S": "Submissions", "W": "Wrestling",
               "M": "Martial Arts"}
STYLE_BITS = {"K": 0, "B": 1, "M": 2, "W": 3, "S": 4}    # Character row word 18 (the select screen's styles)
BLAZIN_COUNT, BLAZIN_MAX = 4, 81
GAMEPLAY_VERSION = 1
GAMEPLAY_KEYS = ("version", "stats", "styles", "blazins", "bars", "voice")
W_BLAZINS, W_GAMEPLAY, W_STYLE_BITS = 3, 11, 18
ARCHETYPE_FIRST = 86        # fighter.xml "CP B__"; the 55 archetypes run to 140 "CP MW_"
BLAZIN_TABLE = 0x47D305FD   # djv2.sod table 6: Blazing_<owner> rows, word 4 the Blazin id


def archetypes():
    """[(fighter.xml id, styles)] of the created fighters' 55 style combinations, in the game's order: per main
    style (B K S W M) the main alone, then with each other one in that order, each followed by its pairs."""
    out = []
    for main in STYLES:
        others = [x for x in STYLES if x != main]
        combos = [()]
        for i, a in enumerate(others):
            combos.append((a,))
            combos += [(a, b) for b in others[i + 1:]]
        out += [(main,) + c for c in combos]
    return [(ARCHETYPE_FIRST + i, c) for i, c in enumerate(out)]


def archetype_name(styles):
    """``CP KSM`` (fighter.xml's name of the archetype) for a style list."""
    styles = list(styles)
    return "CP " + "".join([styles[0]] + sorted(styles[1:], key=STYLES.index)).ljust(3, "_")


def moveset_of(styles):
    """The moveset number (1..55) of a style list (main first, then up to two others in any order); 0 for None
    (the template's own moves)."""
    if styles is None:
        return 0
    styles = list(styles)
    key = tuple([styles[0]] + sorted(styles[1:], key=STYLES.index))
    for k, (_, combo) in enumerate(archetypes()):
        if combo == key:
            return k + 1
    raise FighterError("no style combination %s" % "".join(styles))


def styles_of(moveset):
    """The style list of a moveset number (None for 0, the template's moves)."""
    return None if not moveset else list(archetypes()[moveset - 1][1])


def stat_bar(value):
    """The select screen's bar (0..50) for a stat: a stock fighter's bars are about five times its stats (Doc
    9 6 10 5 10 10 -> 43 30 50 27 50 50), and no stock stat passes 10."""
    return max(0, min(BAR_MAX, int(value) * 5))


def stat_min(key):
    return GRAPPLE_MIN if key == "grapple" else STAT_MIN


def _int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def gameplay_problems(g):
    """What is wrong with a gameplay block ([] when nothing), in words for the mod creator; the account service
    checks the same."""
    if not isinstance(g, dict):
        return ["gameplay must be an object"]
    out = []
    extra = sorted(set(g) - set(GAMEPLAY_KEYS))
    if extra:
        out.append("unknown key%s %s" % ("s" if len(extra) > 1 else "", ", ".join(extra)))
    if g.get("version") != GAMEPLAY_VERSION:
        out.append("version must be %d" % GAMEPLAY_VERSION)
    stats = g.get("stats")
    if not isinstance(stats, dict) or set(stats) != set(STAT_KEYS):
        out.append("stats must name all seven: %s" % ", ".join(STAT_KEYS))
    else:
        bad = [k for k in STAT_KEYS if not (_int(stats[k]) and stat_min(k) <= stats[k] <= STAT_MAX)]
        if bad:
            out.append("each stat is %d to %d, grapple 0 to %d (%s)" % (STAT_MIN, STAT_MAX, STAT_MAX, ", ".join(bad)))
        else:
            total = sum(stats.values())
            if total > STAT_BUDGET:
                out.append("the stats add up to %d, %d over the budget of %d points" % (total, total - STAT_BUDGET,
                                                                                        STAT_BUDGET))
    if g.get("styles") is not None:
        st = g["styles"]
        if not (isinstance(st, list) and 1 <= len(st) <= 3 and all(isinstance(x, str) and x in STYLES for x in st)
                and len(set(st)) == len(st)):
            out.append("styles: one main style and up to two more, all different, of %s" % " ".join(STYLES))
    bl = g.get("blazins")
    if not (isinstance(bl, list) and len(bl) == BLAZIN_COUNT and all(_int(x) and 0 <= x <= BLAZIN_MAX for x in bl)):
        out.append("blazins: four Blazin ids, 0 to %d" % BLAZIN_MAX)
    if "bars" in g:
        bars = g["bars"]
        if not (isinstance(bars, dict) and set(bars) == set(BAR_KEYS)
                and all(_int(bars[k]) and 0 <= bars[k] <= BAR_MAX for k in BAR_KEYS)):
            out.append("bars: %s, each 0 to %d" % (", ".join(BAR_KEYS), BAR_MAX))
    if "voice" in g and g["voice"] != {"mode": "template"}:
        out.append('voice: only {"mode": "template"} for now (own voices come later)')
    return out


def gameplay_words(g):
    """{row word: value} for a valid gameplay block: words 11 and 3 (what the game applies, the encoding of the
    game's ``src/hooks/fighter_gameplay.h``), the select screen's bars and, for a style combination, its style
    bits (word 18)."""
    problems = gameplay_problems(g)
    if problems:
        raise FighterError("gameplay: " + problems[0])
    moveset = moveset_of(g.get("styles"))
    w11 = GAMEPLAY_VERSION << 28 | ((moveset >> 4) & 3) << 30
    for k, key in enumerate(STAT_KEYS):
        w11 |= g["stats"][key] << (4 * k)
    w3 = (moveset & 0xF) << 28
    for k, v in enumerate(g["blazins"]):
        w3 |= v << (7 * k)
    out = {W_GAMEPLAY: w11, W_BLAZINS: w3}
    bars = g.get("bars") or {k: stat_bar(g["stats"][k]) for k in BAR_KEYS}
    for k, word in zip(BAR_KEYS, BAR_WORDS):
        out[word] = bars[k]
    if g.get("styles"):
        out[W_STYLE_BITS] = sum(1 << STYLE_BITS[x] for x in g["styles"])
    return out


def gameplay_from_words(words):
    """The gameplay block a Character row holds (words 3 and 11), or None (its template's): the reverse of
    ``gameplay_words``, with the game's own checks."""
    w3, w11 = words[W_BLAZINS], words[W_GAMEPLAY]
    if (w11 >> 28) & 3 != GAMEPLAY_VERSION:
        return None
    moveset = (w3 >> 28) & 0xF | ((w11 >> 30) & 3) << 4
    if moveset > len(archetypes()):
        return None
    g = {"version": GAMEPLAY_VERSION, "stats": {k: (w11 >> (4 * i)) & 0xF for i, k in enumerate(STAT_KEYS)},
         "blazins": [(w3 >> (7 * k)) & 0x7F for k in range(BLAZIN_COUNT)]}
    if moveset:
        g["styles"] = styles_of(moveset)
    return None if gameplay_problems(g) else g


def blazin_names(sod_blob):
    """{Blazin id: name} from the game's database (table 6: ``Blazing_<owner>`` rows, word 4 the id), the names
    the Studio's Blazin pickers show (``Blazing_Doc`` -> ``Doc``, ``Blazing_CP WBS FRONT`` -> ``Created WBS
    FRONT``)."""
    s = Sod(sod_blob)
    if BLAZIN_TABLE not in s.table_hashes:
        return {}
    off = s.table_offsets[s.table_hashes.index(BLAZIN_TABLE)]
    classes = _u32(s.blob, off + 0x10)
    _, count, stride, _, vals = _block(s.blob, _u32(s.blob, off + 0x18 + 4 * classes))
    out = {}
    for p in vals[::stride]:
        bid = _u32(s.blob, p + 16)
        if bid > BLAZIN_MAX:
            continue
        label = re.sub(r"^blazing_", "", s.string(_u32(s.blob, p)) or "", flags=re.I).strip()
        out[bid] = re.sub(r"^CP ", "Created ", label) or "Blazin %d" % bid
    return out


def _item_index(sod_blob, row_offset):
    """A Character row's item index (its place in the name-keyed class block; item ID = 7 << 24 | index)."""
    s = Sod(sod_blob)
    off = s.table_offsets[s.table_hashes.index(CHARACTER)]
    classes = _u32(s.blob, off + 0x10)
    for c in range(classes):
        if _u32(s.blob, off + 0x18 + 4 * c) == CHARACTER:
            _, _, stride, _, vals = _block(s.blob, _u32(s.blob, off + 0x18 + 4 * classes + 4 * c))
            ptrs = list(vals[::stride])
            return ptrs.index(row_offset) if row_offset in ptrs else None
    return None


def own_blazin(sod_blob, fe_id):
    """The Blazin id of a stock fighter's own Blazin (the Blazin row whose owner, word 1, is the fighter's item
    ID), or None (Suspect has none)."""
    s = Sod(sod_blob)
    rows = {r["fe_id"]: r for r in characters(sod_blob)}
    if fe_id not in rows or BLAZIN_TABLE not in s.table_hashes:
        return None
    index = _item_index(sod_blob, rows[fe_id]["offset"])
    item = (s.table_hashes.index(CHARACTER) << 24) | (index if index is not None else 0xFFFFFF)
    off = s.table_offsets[s.table_hashes.index(BLAZIN_TABLE)]
    classes = _u32(s.blob, off + 0x10)
    _, _, stride, _, vals = _block(s.blob, _u32(s.blob, off + 0x18 + 4 * classes))
    for p in vals[::stride]:
        if _u32(s.blob, p + 4) == item:
            return _u32(s.blob, p + 16)
    return None


def template_gameplay(sod_blob, fe_id):
    """A stock fighter's own gameplay as a gameplay block (its moves: no ``styles``), for the editor's start and
    "Reset": the stats from its select-screen bars (a fifth of each; measured exact for Doc and Blaze, whose
    stats the game decodes as 9 6 10 5 10 0 10 and 10 9 5 10 7 0 9), Grapple 0 and its own Blazin in all four
    keys, as the game gives a stock fighter (``None`` in the four when it has none of its own). None when there
    is no such fighter."""
    rows = {r["fe_id"]: r for r in characters(sod_blob)}
    if fe_id not in rows:
        return None
    w = rows[fe_id]["words"]
    stats = {k: max(STAT_MIN, min(STAT_MAX, int(w[word] / 5.0 + 0.5))) for k, word in zip(BAR_KEYS, BAR_WORDS)}
    stats["grapple"] = GRAPPLE_MIN
    own = own_blazin(sod_blob, fe_id)
    return {"version": GAMEPLAY_VERSION, "stats": {k: stats[k] for k in STAT_KEYS}, "blazins": [own] * BLAZIN_COUNT}


def template_styles(sod_blob, fe_id):
    """The style letters a stock fighter's select-screen entry shows (Character row word 18)."""
    rows = {r["fe_id"]: r for r in characters(sod_blob)}
    bits = rows[fe_id]["words"][W_STYLE_BITS] if fe_id in rows else 0
    return [x for x in STYLES if bits >> STYLE_BITS[x] & 1]
