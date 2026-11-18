# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Community fighters beyond the 48 places: four per-slot bank rows and unindexed fighter records
(``community/unlimited-fighters-study.md``, phase 1, offline only).

The game's Character table keeps the stock rows, the place fighters and **four bank rows** (front-end IDs 124..127,
one per player slot). Every other community fighter is a **logical fighter**: a front-end ID of 1000 and up that the
game never sees in its fighter byte. Its record is a 26-word Character row image appended to ``assets/djv2.sod``
with its strings, but in no class block and no row list (unindexed): the game's own lookups never reach it, and the
roster list (128 entries), the 80-entry unlock set and the select list stay as they are. Our own side file,
``assets/ffny_fighters.bin``, lists them:

    header  "FFLF", version (1), count, the SOD's size, the SOD's FNV-1a 32
            the four bank front-end IDs, the four bank rows' file offsets
    rows    (count, sorted by ID, 72 bytes each): ID, record offset, template feID, coreID, flags, 0,
            display name (32 bytes, NUL-padded), model (16 bytes)

At a match the game binds slot ``s``'s pick (a logical ID) to bank ``s`` (``src/hooks/fighter_banks.c``): the
ControllerSetup hook hands the game ``124 + s``, and the record lookup returns the logical record for that bank's
row, so the match reads the bound fighter's name, model, coreID and stats. Two slots never share a bank, so the
same fighter twice (a mirror match) works. The binding is a pure function of the lineup.

A logical fighter is a new-fighter pack whose ``fighter_fe_id`` is ``FIRST_LOGICAL`` or above; its model, camera
banks, intro entry and pictures go in exactly as for a place fighter (``newfighter.apply_op``, ``logical=True``),
under its logical ID. Standard library only."""

import struct

from .newfighter import (FIRST_LOGICAL, LAST_LOGICAL, MORE_ID, FighterError, ROW_WORDS, W_CORE_ID, W_DISPLAY,
                         W_FE_ID, W_MAIN, W_MODEL, W_NAME, append_character, characters, is_logical)
from .sodwrite import SodEditor

BANKS = (124, 125, 126, 127)    # bank s is player slot s; the last four places (newfighter.PLACE_ORDER)
BANK_TEMPLATE = 55              # Doc: the banks' own (neutral) look when nothing is bound
MAX_LOGICAL = 16384             # fighters in one side file
SIDE_FILE = "assets/ffny_fighters.bin"
MAGIC = b"FFLF"
VERSION = 1
HEADER = struct.Struct("<4sIIII4I4I")
ROW = struct.Struct("<6I32s16s")
NAME_MAX = 31
MODEL_MAX = 15
ROW_NAME_MAX = 19               # ResetFighterList copies a row name into 32 bytes at +4 with tier/feID at +0x18


# -- own voices (phase 3B) --------------------------------------------------------------------------------------
#
# A voice number names a fighter's eight speech files (``fightervoice.py``), and the speech code addresses at most
# 200 of them (its bank flags, 0x33EA78[voice < 500 ? voice : voice - 400]), so a logical fighter cannot have a
# number of its own. Each bank has one: bank s speaks as voice 064 + s (``fightervoice.voice_number`` of 124 + s,
# the own voice of the place a bank row holds, so no place fighter has it). The speech archive holds
#
#   * the bank voices' eight entries (``bank_voice_placeholders``): short silence, never played as they are;
#   * per logical fighter with an own voice (``logical_voice_files``) its files under its ID: a ``.dat`` per bank,
#     the two streams, and its ``.hdr`` four times, once per bank voice (a header names its voice number), e.g.
#     ``speech\Taunts\Def_Jam_Characters\L1000_065.hdr``, ``...\L1000.dat``, ``speech\streams\intro\L1000.asf``.
#
# At a match's speech set-up the game points a bound bank's eight directory entries (the archive's directory the
# game keeps in memory) at its fighter's files and gives the slot the bank's voice (src/hooks/new_fighters.c,
# fighter_voice.h). Audio only; the result depends only on the packs and the dump (the content print).

LOGICAL_VOICE_PREFIX = "L"


def bank_voice(bank_fe_id):
    """The voice number bank ``bank_fe_id`` (124..127) speaks with when its fighter has an own voice (064..067)."""
    from . import fightervoice
    if bank_fe_id not in BANKS:
        raise FighterError("%r is not a bank" % (bank_fe_id,))
    return fightervoice.voice_number(bank_fe_id)


def logical_entry(entry, fe_id):
    """The archive entry a logical fighter ``fe_id``'s own file has for the bank voice ``entry`` names (one of
    ``fightervoice.voice_entries(bank voice)``): a ``.hdr`` keeps the bank voice in its name, the others do not."""
    head, _, leaf = entry.rpartition("\\")
    stem, _, ext = leaf.partition(".")
    if ext == "hdr":
        return "%s\\%s%d_%s.hdr" % (head, LOGICAL_VOICE_PREFIX, fe_id, stem)
    return "%s\\%s%d.%s" % (head, LOGICAL_VOICE_PREFIX, fe_id, ext)


def logical_entries(fe_id):
    """Every entry of logical fighter ``fe_id``'s own voice: 12 headers, 3 banks, 2 streams (17)."""
    from . import fightervoice
    out = []
    for bank in BANKS:
        for e in fightervoice.voice_entries(bank_voice(bank)):
            name = logical_entry(e, fe_id)
            if name not in out:
                out.append(name)
    return out


def logical_voice_files(current, names, template_fe_id, template_speech, fe_id, lines, read):
    """The files of logical fighter ``fe_id``'s own voice: [(entry, bytes)] to add to the speech archive (see
    above). Built as a place fighter's own voice would be at the first bank's number (``fightervoice.voice_files``),
    then put under the fighter's ID; the headers once per bank voice. Raises ``fightervoice.VoiceError``."""
    from . import fightervoice
    if not is_logical(fe_id):
        raise fightervoice.VoiceError("%r is not a community fighter beyond the places" % (fe_id,))
    have = {n.replace("/", "\\").lower() for n in names()}
    taken = [e for e in logical_entries(fe_id) if e.lower() in have]
    if taken:
        raise fightervoice.VoiceError("%s is in the speech archive already" % taken[0])
    first = bank_voice(BANKS[0])
    # the bank voices' own entries are not looked at here: a place fighter cannot have them, and the placeholders
    # go in after every pack (bank_voice_placeholders)
    files = fightervoice.voice_files(current, lambda: [n for n in names() if n.lower() not in
                                                       {e.lower() for e in fightervoice.voice_entries(first)}],
                                     template_fe_id, template_speech, BANKS[0], lines, read)
    out = []
    for entry, data in files:
        if entry.endswith(".hdr"):
            for bank in BANKS:
                v = bank_voice(bank)
                h = bytearray(data)
                struct.pack_into("<H", h, 2, v)
                out.append((logical_entry(entry.replace("%03d.hdr" % first, "%03d.hdr" % v), fe_id), bytes(h)))
        else:
            out.append((logical_entry(entry, fe_id), data))
    return out


def has_logical_voice(names, fe_id):
    """Whether a speech archive with entries ``names`` holds every own-voice file of logical fighter ``fe_id``."""
    have = {n.replace("/", "\\").lower() for n in names}
    return all(e.lower() in have for e in logical_entries(fe_id))


def bank_voice_placeholders(current, names):
    """The bank voices' entries the speech archive lacks: [(entry, bytes)], silence in the layout of voice
    ``fightervoice.LAYOUT_VOICE`` (the game points them at a fighter's files before it reads them)."""
    from . import fightervoice
    have = {n.replace("/", "\\").lower() for n in names()}
    out = []
    for fe in BANKS:
        v = bank_voice(fe)
        for bank in fightervoice.BANKS:
            hname = fightervoice.bank_entry(bank, v, "hdr")
            dname = fightervoice.bank_entry(bank, v, "dat")
            if hname.lower() in have and dname.lower() in have:
                continue
            hdr = current(fightervoice.bank_entry(bank, fightervoice.LAYOUT_VOICE, "hdr"))
            count = fightervoice.parse_hdr(hdr)["count"]
            h, d = fightervoice.build_bank(hdr, bank, v, [fightervoice.silence_clip()] * count)
            out += [(n, b) for n, b in ((hname, h), (dname, d)) if n.lower() not in have]
        for kind in ("intro", "extro"):
            name = fightervoice.stream_entry(kind, v)
            if name.lower() not in have:
                out.append((name, fightervoice.silence_clip(stream=True)))
    return out


def fnv1a(blob):
    """FNV-1a 32 (the game checks the side file against the database it reads with it)."""
    h = 0x811C9DC5
    for b in memoryview(blob):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def bank_rows(blob):
    """{bank feID: row offset} of the bank rows a database has (empty when it has none)."""
    return {r["fe_id"]: r["offset"] for r in characters(blob) if r["fe_id"] in BANKS}


def add_banks(blob):
    """``djv2.sod`` with the four bank rows (copies of Doc's row with their own row names, "BANK 1".."BANK 4"),
    unless it has them already. A bank place held by a place fighter refuses: logical fighters need all four."""
    rows = {r["fe_id"]: r for r in characters(blob)}
    have = [fe for fe in BANKS if fe in rows]
    for fe in have:
        if not rows[fe]["name"].startswith("FFNYBank"):
            raise FighterError("front-end ID %d is a new fighter's place (%s), not a bank: community fighters beyond "
                               "the places need 124..127 free" % (fe, rows[fe]["display"]))
    for i, fe in enumerate(BANKS):
        if fe not in rows:
            blob, _, _ = append_character(blob, BANK_TEMPLATE, fe, "FFNYBank%d" % i, "BANK %d" % (i + 1))
    return blob


def _row_name(display, fe_id):
    base = "".join(c for c in display if c.isalnum())[:ROW_NAME_MAX - 1 - len(str(fe_id))] or "L"
    return "%s_%d" % (base, fe_id)


def add_records(blob, fighters):
    """``djv2.sod`` with one unindexed record per logical fighter, and the side file. ``fighters``: [{id,
    template_fe_id, name, model (None: the template's), edits ({word: value}: gameplay words)}]. Returns (sod,
    side file). The records are the template's row with its own row name, display name, model, ID and edits;
    nothing indexes them, so no existing row, ID or list changes."""
    fighters = sorted(fighters, key=lambda f: f["id"])
    if len(fighters) > MAX_LOGICAL:
        raise FighterError("at most %d community fighters beyond the places" % MAX_LOGICAL)
    seen = set()
    for f in fighters:
        if not is_logical(f["id"]) or f["id"] in seen:
            raise FighterError("logical fighter ID %r is not a free ID of %d and up" % (f["id"], FIRST_LOGICAL))
        seen.add(f["id"])
        if not (0 < len(f["name"]) <= NAME_MAX) or any(ord(c) < 32 or ord(c) > 126 for c in f["name"]):
            raise FighterError("%r is no fighter name" % f["name"])
        if f.get("model") and len(f["model"]) > MODEL_MAX:
            raise FighterError("the model name %r is too long" % f["model"])
    blob = add_banks(blob)
    rows = {r["fe_id"]: r for r in characters(blob)}
    banks = {fe: rows[fe]["offset"] for fe in BANKS}
    ed = SodEditor(blob)
    known = {}
    for i in range(len(ed.base.strings)):
        t = ed.base.string(i)
        if t is not None:
            known.setdefault(t, i)

    def string(text):
        if text not in known:
            known[text] = ed.add_string(text)
        return known[text]

    placed = []
    for f in fighters:
        tpl = rows.get(f["template_fe_id"])
        if tpl is None or tpl["fe_id"] in BANKS:
            raise FighterError("no Character row with feID %r for %s" % (f["template_fe_id"], f["name"]))
        words = list(tpl["words"])
        words[W_NAME] = ed.add_string(_row_name(f["name"], f["id"]))
        words[W_DISPLAY] = string(f["name"])
        model = f.get("model") or tpl["model"]
        words[W_MODEL] = string(model)
        words[W_FE_ID] = f["id"]
        words[W_MAIN] = 1
        for k, v in (f.get("edits") or {}).items():
            if k in (W_NAME, W_DISPLAY, W_MODEL, W_FE_ID, W_CORE_ID, W_MAIN):
                raise FighterError("word %d is not editable here" % k)
            words[k] = v & 0xFFFFFFFF
        off = ed.append(struct.pack("<%dI" % ROW_WORDS, *words))
        placed.append((f, off, tpl, model))
    sod = ed.finish()
    side = bytearray(HEADER.pack(MAGIC, VERSION, len(placed), len(sod), fnv1a(sod), *BANKS,
                                 *[banks[fe] for fe in BANKS]))
    for f, off, tpl, model in placed:
        side += ROW.pack(f["id"], off, tpl["fe_id"], tpl["core_id"], 0, 0,
                         f["name"].encode("latin-1"), model.encode("latin-1"))
    return sod, bytes(side)


def read_side(side, sod=None):
    """The side file as {banks: {feID: offset}, fighters: [{id, offset, template_fe_id, core_id, name, model}]};
    with ``sod`` (the database it belongs to) it is checked against it as the game checks it. Raises
    FighterError."""
    if len(side) < HEADER.size or side[:4] != MAGIC:
        raise FighterError("not a fighter side file")
    magic, version, count, sod_size, sod_hash, *rest = HEADER.unpack_from(side)
    if version != VERSION or len(side) != HEADER.size + count * ROW.size:
        raise FighterError("a fighter side file of another version or size")
    banks = dict(zip(rest[:4], rest[4:]))
    out = []
    for i in range(count):
        fid, off, tpl, core, flags, _, name, model = ROW.unpack_from(side, HEADER.size + i * ROW.size)
        out.append({"id": fid, "offset": off, "template_fe_id": tpl, "core_id": core, "flags": flags,
                    "name": name.rstrip(b"\0").decode("latin-1"), "model": model.rstrip(b"\0").decode("latin-1")})
    if any(b["id"] <= a["id"] for a, b in zip(out, out[1:])):
        raise FighterError("the fighter side file is not sorted")
    if sod is not None:
        if len(sod) != sod_size or fnv1a(sod) != sod_hash:
            raise FighterError("the fighter side file belongs to another database")
        if bank_rows(sod) != banks:
            raise FighterError("the bank rows moved")
        for f in out:
            w = struct.unpack_from("<%dI" % ROW_WORDS, sod, f["offset"])
            if w[W_FE_ID] != f["id"] or w[W_CORE_ID] != f["core_id"]:
                raise FighterError("record %d does not match the database" % f["id"])
    return {"banks": banks, "fighters": out}
