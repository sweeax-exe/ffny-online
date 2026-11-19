# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""A venue pack's ``new-venue`` op on the player's own files (FFNY Studio ``ffnystudio/newvenue.py``).

The new venue is a copy of a template venue with its own filename (``V2BG_28A``), name (``ARENA_BG28A``), display
name, row ``ID`` (max + 1 at install time) and ``feID``; it keeps the template's ``coreID``. The fight takes the
venue from the Venue table of ``assets/djv2.sod`` (``sod.append_venue``); ``venue.xml`` gets a ``<venue>`` record
and ``assets/entity.xml`` the template's ``<entity>`` renamed. The venue's own archive entries are the pack's
``add`` entries.

Its far crowd (the rows drawn at the bones of a locator file in ``assets/icrowd.viv``, named by the entity's
``<instancecrowdlocator>``) is the template's unless the op names one of its own: ``"crowd_locator": "icl_28A"``,
which the pack adds as ``assets/icrowd.viv|icl_28A.o`` (the template's locator with moved places). Only the pack's
own code, only as an add (a retail locator is never replaced by a new venue), checked by ``check_crowd``. A
launcher before this ignored the field: the venue played with the template's far crowd.

Its breakable objects (FFNY Studio beta.24, ``docs/ffny-studio-app.md`` 11.19) are stage models of its own: the op's
``"models": ["V2BG_28A_Z", ...]``, each the pack's add of ``<model>.o`` to ``v2bg.viv`` made from the retail
breakable ``V2BG_13A_J.o``; the new entity gets a ``<model>`` line for each (the game draws no stage model its
entity does not list). ``check_models`` and ``check_model`` hold the account service's rules
(``packs_check.go`` ``checkVenueModels``). The catalogue gives such a pack the capability ``venue-models-v1``, so a
launcher before build 83 (which ignored the field and would not draw the objects) skips it.
"""

import re
from types import SimpleNamespace

from . import sod as sodmod
from . import viv

VENUE_XML = "venue.xml"
ENTITY_XML = "assets/entity.xml"
VENUE_ARCHIVE = "assets/v2bg.viv"
ICROWD_ARCHIVE = "assets/icrowd.viv"
CAMERA_ARCHIVE = "assets/v2ta.viv"
CAMERA_BANK = re.compile(r"(BT4V|VI_)(\d\d[A-Z])(\d{0,2})\.viv", re.IGNORECASE)
_BANK_SYMBOL = re.compile(rb"__AnimationBank:::(BT4V|VI_)(\d\d[A-Z])(\d{0,2})_")
# every retail camera bank (105 in v2ta.viv): entries at 128-byte offsets, the directory ends in L231 + version 2
BANK_ALIGN = 128
BANK_TRAILER = b"L231\x00\x00\x00\x02"
LOCATOR_ENTRY = re.compile(r"icl_\d\d[A-Z]\.o")
LOCATOR_MIN, LOCATOR_MAX = 4 * 1024, 64 * 1024     # retail: 10640 to 20592 bytes


class VenueError(ValueError):
    pass


MODEL_NAME = re.compile(r"V2BG_(\d\d[A-Z])_([O-Z])")
MODEL_ADD = re.compile(r"V2BG_\d\d[A-Z]_[O-Z]\.o", re.I)      # a v2bg.viv add named like a breakable's model
MODELS_MAX = 12
MODEL_MAX_BYTES = 8 << 20
# the retail breakable models a breakable starts from: exact size and SHA-256 in the expected dump
BREAKABLE_MODELS = {"V2BG_13A_J.o": (35872, "55568ad99697ec364f38c76f0d0406ca87d80f68a8f23750e49a900c5e409faa")}
# the retail venues' own stage models with the letters O..Z: a copy keeps them under its code, a breakable never
# takes their letter
RETAIL_STAGE_LETTERS = {"04A": "OPT", "05A": "OPQSTUVWXZ", "06A": "O", "09A": "R", "09B": "R", "11A": "R",
                        "13A": "OV", "15A": "OPQSTU", "20A": "OPQSTUV", "23A": "R"}


def _rename_text(blob, old, new):
    """Whole-name replacements of ``old`` (V2BG_02A, followed by a digit or the end of the name) in XML bytes;
    names with an object suffix (V2BG_02A_A) are left alone."""
    pattern = re.compile(re.escape(old.encode("latin-1")) + rb"(?=\d?<|\d?\s*<)", re.IGNORECASE)
    return pattern.sub(new.encode("latin-1"), blob)


def venue_record(template_text, template, filename, fe_id, venue_id, dispname):
    """The ``<venue>`` block of the template in venue.xml, copied with the new identity."""
    m = None
    for block in re.finditer(r"<venue>.*?</venue>", template_text, re.S):
        if re.search(r"<filename>\s*%s\s*</filename>" % re.escape(template.filename), block.group(0), re.I):
            m = block
            break
    if m is None:
        raise KeyError("no venue.xml record for %s" % template.filename)
    text = m.group(0)
    code = filename.upper().replace("V2BG_", "")
    for tag, value in (("name", "ARENA_BG%s" % code), ("filename", filename), ("dispname", dispname),
                       ("ID", str(venue_id)), ("feID", str(fe_id))):
        text = re.sub(r"(<%s>)[^<]*(</%s>)" % (tag, tag), lambda mm: mm.group(1) + value + mm.group(2), text,
                      count=1)
    return text


def entity_block(entity_text, template, filename):
    """The template's ``<entity>`` in entity.xml with the main model, its texture, its morphs and the stage XML
    renamed to ``filename``."""
    blocks = [b.group(0) for b in re.finditer(r"<entity>.*?</entity>", entity_text, re.S)]
    own = [b for b in blocks if (re.search(r"<name>\s*([^<]*?)\s*</name>", b) or [None, ""])[1].upper()
           == template.filename.upper()]
    loose = [b for b in blocks if re.search(r"<name>\s*%s\s*</name>" % re.escape(template.filename),
                                            b.split("<lighting>")[0], re.I)]
    for text in own + loose:
        return _rename_text(text.encode("latin-1"), template.filename, filename).decode("latin-1")
    raise KeyError("no entity.xml entity for %s" % template.filename)


def set_fighter_light(block, entity_text, venue):
    """An ``<entity>`` block whose fighters are lit like another retail venue's (``fighter_light``, e.g. ``01B``, the
    night junkyard): that venue's ``<lighting><setting>`` (the ``.irr`` irradiance files; the shadow ``<proxy>`` stays) and its
    ``<characterenvmap>``. Build 86, the Run the Streets map: a night block copied from a daytime template (03A) kept
    the Power Plant's daylight on the fighters. VenueError for an unknown venue or one without lighting."""
    if not isinstance(venue, str) or not re.fullmatch(r"\d\d[A-Z]", venue):
        raise VenueError("invalid fighter_light %r" % (venue,))
    src = None
    for b in re.finditer(r"<entity>.*?</entity>", entity_text, re.S):
        name = re.search(r"<name>\s*([^<]*?)\s*</name>", b.group(0))
        if name and name.group(1).upper() == "V2BG_" + venue.upper():
            src = b.group(0)
            break
    light = re.search(r"<lighting>.*?(<setting>.*?</setting>).*?</lighting>", src or "", re.S)
    if not light:
        raise VenueError("fighter_light: venue %s has no lighting" % venue)
    # only the irradiance settings: the <proxy> (the shadow proxy of the template's geometry) stays the template's
    out, n = re.subn(r"(<lighting>.*?)<setting>.*?</setting>", lambda m: m.group(1) + light.group(1), block, count=1,
                     flags=re.S)
    if not n:
        raise VenueError("the template has no lighting block")
    env = re.search(r"<characterenvmap>[^<]*</characterenvmap>", src)
    if env:
        out = re.sub(r"<characterenvmap>[^<]*</characterenvmap>", lambda m: env.group(0), out, count=1)
    return out


def own_entity_models(block, template_filename, filename, added=()):
    """The entity block with each template stage model renamed to the new venue's own (``V2BG_15A_H`` ->
    ``V2BG_29A_H``) when the pack adds that model (``added``: v2bg.viv entry names the pack adds). FFNY Studio
    gives an edited template object its own copy under the new code and the copy's object XML names it; an
    entity that kept the template's name drew none of the venue's objects (Crow's Office Street came out black,
    2026-11-19)."""
    added = {a.lower() for a in added}
    pattern = re.compile(r"(<name>\s*)(%s)(_\w+)(\s*</name>)" % re.escape(template_filename), re.IGNORECASE)

    def swap(m):
        new = filename + m.group(3)
        return m.group(1) + new + m.group(4) if (new + ".o").lower() in added else m.group(0)
    return pattern.sub(swap, block)


def drop_entity_models(block, main):
    """An ``entity.xml`` block with only the main model's ``<model>`` left."""
    def keep(m):
        name = re.search(r"<name>\s*([^<\s]+)\s*</name>", m.group(0))
        return m.group(0) if name and name.group(1).upper() == main.upper() else ""
    return re.sub(r"[ \t]*<model>.*?</model>[ \t]*\r?\n?", keep, block, flags=re.S)


def set_crowd_locator(block, locator):
    """An ``<entity>`` block whose ``<instancecrowdlocator>`` names ``locator``; VenueError without one."""
    text, n = re.subn(r"(<instancecrowdlocator>)[^<]*(</instancecrowdlocator>)",
                      lambda m: m.group(1) + locator + m.group(2), block, count=1)
    if not n:
        raise VenueError("the template %s has no far crowd" % locator)
    return text


def check_crowd(op, entries):
    """A new-venue pack's far crowd: the op's ``crowd_locator`` is ``icl_<code>``, and the pack's only
    ``icrowd.viv`` entry is the add of exactly that file from a locator of the same kind; or neither. Raises
    VenueError."""
    code = op.get("code")
    locator = op.get("crowd_locator")
    mine = [e for e in entries if e.get("archive") == ICROWD_ARCHIVE or e.get("base_archive") == ICROWD_ARCHIVE]
    if locator is None:
        if mine:
            raise VenueError("the new venue changes icrowd.viv without naming its own far crowd")
        return None
    if not isinstance(code, str) or locator != "icl_" + code:
        raise VenueError("invalid far crowd %r" % (locator,))
    want = locator + ".o"
    if len(mine) != 1:
        raise VenueError("the new venue's far crowd needs exactly one icrowd.viv entry, %s" % want)
    e = mine[0]
    if (e.get("mode") != "add" or e.get("entry") != want or e.get("base_archive") != ICROWD_ARCHIVE
            or not LOCATOR_ENTRY.fullmatch(str(e.get("base_entry")))):
        raise VenueError("a new venue only adds its own far crowd %s to icrowd.viv" % want)
    if not LOCATOR_MIN <= e.get("result_size", 0) <= LOCATOR_MAX:
        raise VenueError("%s has an unlikely size" % want)
    return want


def check_models(op, entries):
    """A new-venue pack's breakable models: the op's ``models`` (1..12 ``V2BG_<code>_<O..Z>``, no letter of the
    template's own stage models), each with exactly one ``v2bg.viv`` add ``<model>.o`` from a retail breakable
    model, at most 8 MiB; every ``v2bg.viv`` add named like one (any case) is listed, except the template's own
    stage model copied under the new code. Returns {entry name: model name}; raises VenueError."""
    code, template = op.get("code"), op.get("template_code")
    listed = []
    if "models" in op:
        models = op["models"]
        if not isinstance(models, list) or not 1 <= len(models) <= MODELS_MAX:
            raise VenueError("the new venue's models are not a list of 1 to %d" % MODELS_MAX)
        for name in models:
            m = MODEL_NAME.fullmatch(name) if isinstance(name, str) else None
            if not m or m.group(1) != code:
                raise VenueError("%r is not a stage model of the new venue" % (name,))
            if name in listed:
                raise VenueError("%s is named twice" % name)
            if m.group(2) in RETAIL_STAGE_LETTERS.get(template, ""):
                raise VenueError("%s is the name of a stage model copied from the template" % name)
            listed.append(name)
    found = {}
    for e in entries:
        if e.get("archive") != VENUE_ARCHIVE or e.get("mode") != "add":
            continue
        name = str(e.get("entry"))
        if not MODEL_ADD.fullmatch(name):
            continue
        stem = name[:-2]
        if stem not in listed and isinstance(code, str) and isinstance(template, str) \
                and stem[5:8].upper() == code.upper() \
                and str(e.get("base_entry")).upper() == ("V2BG_%s%s.o" % (template, stem[8:])).upper():
            continue                # the template's own stage model, copied under the new code
        if stem not in listed:
            raise VenueError("%s is a breakable object's model the new venue does not name" % name)
        retail = BREAKABLE_MODELS.get(e.get("base_entry"))
        if retail is None or e.get("base_archive") != VENUE_ARCHIVE or e.get("original_sha256") != retail[1]:
            raise VenueError("%s must start from the game's own breakable model" % name)
        if not 0 < e.get("result_size", 0) <= MODEL_MAX_BYTES:
            raise VenueError("%s has an unlikely size" % name)
        found[name] = stem
    missing = sorted(set(listed) - set(found.values()))
    if missing:
        raise VenueError("the pack does not add %s.o" % missing[0])
    return found


def check_model(result, name):
    """A breakable's stage model made from the retail one: an ELF object under its own name, none of the retail
    model's names left."""
    if result[:4] != b"\x7fELF" or name.encode("latin-1") not in result \
            or any(n[:-2].encode("latin-1") in result for n in BREAKABLE_MODELS):
        raise VenueError("%s.o is not a breakable model renamed for the new venue" % name)


def add_entity_models(block, names):
    """An entity.xml ``<entity>`` block with a ``<model>`` for each of ``names`` before its end (as FFNY Studio's
    ``breakables.add_entity_models``: the composed entity.xml is the same on every PC)."""
    if not names:
        return block
    have = {n.upper() for n in re.findall(r"<model>\s*<name>\s*([^<\s]+)\s*</name>", block)}
    for n in names:
        if n.upper() in have:
            raise VenueError("the new venue lists %s already" % n)
    end = block.rfind("</entity>")
    add = "".join("\t\t<model>\r\n\t\t\t<name>%s</name>\r\n\t\t</model>\r\n" % n for n in names)
    return block[:end] + add + "\t" + block[end:]


def repair_camera_bank(entry, data, code):
    """A new venue's camera bank (``assets/v2ta.viv|BT4V28A.viv``, ``VI_28A11.viv``) with its objects' symbols
    naming the venue's own ``code``. FFNY Studio up to 0.1.0-beta.24 copied the template's bank with only the
    directory renamed, so the objects still said ``__AnimationBank:::BT4V03A_EYE``; a BLAZIN' on such a venue
    corrupted the game's heap and the game crashed after the fight (``sub_00029DE0``). The fix is the same-length
    rename the new Studio makes (the layout is ``retail_bank_layout``'s); ``data`` is returned unchanged when it is
    not such a bank or is right already."""
    m = CAMERA_BANK.fullmatch(entry or "")
    if not m or m.group(2).upper() != (code or "").upper():
        return data
    prefix, own, suffix = m.group(1).upper().encode(), m.group(2).upper().encode(), m.group(3).encode()
    wrapped = viv.is_refpack(data)
    blob = viv.unwrap(data) if wrapped else data
    if blob[:4] != b"BIGF":
        return data

    def own_name(s):
        if s.group(1).upper() == prefix and s.group(3) == suffix and s.group(2) != own:
            return b"__AnimationBank:::" + s.group(1) + own + s.group(3) + b"_"
        return s.group(0)
    fixed = _BANK_SYMBOL.sub(own_name, blob)
    if fixed == blob:
        return data
    return viv.refpack_store(fixed) if wrapped else fixed


def retail_bank_layout(data):
    """An animation bank added to ``assets/v2ta.viv`` laid out as all 313 retail ones are: entries at 128-byte
    offsets and the directory's ``L231`` + version 2 trailer (``viv.build_archive`` gives every retail bank back
    byte for byte). FFNY Studio up to 0.1.0-beta.24 rebuilt the banks it copies (a new venue's ``BT4V``/``VI_``
    cameras, a new fighter's ``CE_``/``CI_`` cinema banks) at alignment 1 or 4 without the trailer, and a BLAZIN'
    on a venue with such a camera bank corrupted the game's heap even with the symbols renamed (crash after the
    fight in ``sub_00029DE0``, 2026-11-15). Not a BIG archive, or laid out so already: ``data`` unchanged."""
    wrapped = viv.is_refpack(data)
    blob = viv.unwrap(data) if wrapped else data
    if blob[:4] != b"BIGF":
        return data
    inner = viv.Archive(blob)
    trailer = blob[inner.header_size - 8:inner.header_size]
    if not inner.entries or (viv.entry_alignment(inner.entries) >= BANK_ALIGN and trailer[:4] == b"L231"):
        return data
    files = [(e.name, inner.raw(e)) for e in inner.entries]
    fixed = viv.build_archive(files, magic=inner.magic, align=BANK_ALIGN, size_le=inner.size_le is not False,
                              trailer=BANK_TRAILER)
    return viv.refpack_store(fixed) if wrapped else fixed


def check_locator(base, result):
    """A far-crowd locator made from ``base`` (a retail one): an ELF object of the same size (only places move)."""
    if result[:4] != b"\x7fELF" or len(result) != len(base):
        raise VenueError("the far crowd file is not a locator like the template's")


def _numbers(text, tag):
    out = []
    for block in re.finditer(r"<venue>.*?</venue>", text, re.S):
        m = re.search(r"<%s>\s*([^<]*?)\s*</%s>" % (tag, tag), block.group(0))
        if m:
            try:
                out.append(int(m.group(1), 0))
            except ValueError:
                pass
    return out


def apply(venue_xml, entity_xml, sod_blob, op, taken_names=(), added=()):
    """(venue.xml, entity.xml, djv2.sod) bytes with the op's venue added. ``taken_names`` are the v2bg.viv entry
    names that exist already (lower case): the new filename must be free there, in venue.xml and in the table."""
    filename, fe_id = op["filename"], int(op["fe_id"])
    if not re.fullmatch(r"V2BG_\d\d[A-Z]", filename) or filename != "V2BG_" + op["code"]:
        raise VenueError("invalid venue filename %r" % filename)
    text = venue_xml.decode("latin-1")
    if fe_id in _numbers(text, "feID"):
        raise VenueError("feID %d is taken" % fe_id)
    if any(f.upper() == filename.upper()
           for f in re.findall(r"<filename>\s*([^<]*?)\s*</filename>", text)) \
            or (filename + ".o").lower() in taken_names:
        raise VenueError("venue %s exists already" % filename)
    if any(v["fe_id"] == fe_id or (v["filename"] or "").upper() == filename.upper()
           for _, v in sodmod.venues(sod_blob)):
        raise VenueError("feID %d or %s is in the Venue table already" % (fe_id, filename))
    template = SimpleNamespace(filename=op["template"])
    ids = _numbers(text, "ID")
    record = venue_record(text, template, filename, fe_id, (max(ids) + 1) if ids else 0, op.get("dispname") or
                          "%s %s" % (op["code"], op.get("title") or "New venue"))
    end = text.rindex("</venuelist>")
    venue_out = (text[:end].rstrip("\r\n") + "\r\n" + "\t" + record + "\r\n\r\n" + text[end:]).encode("latin-1")
    ent = entity_xml.decode("latin-1")
    block = entity_block(ent, template, filename)
    block = own_entity_models(block, op["template"], filename, added)
    if op.get("drop_objects"):
        block = drop_entity_models(block, filename)
    if op.get("models"):
        block = add_entity_models(block, op["models"])
    if op.get("crowd_locator") is not None:
        block = set_crowd_locator(block, op["crowd_locator"])
    if op.get("fighter_light") is not None:
        block = set_fighter_light(block, ent, op["fighter_light"])
    end = ent.rindex("</entitylist>")
    entity_out = (ent[:end].rstrip("\r\n") + "\r\n\t" + block + "\r\n" + ent[end:]).encode("latin-1")
    sod_out, _ = sodmod.append_venue(sod_blob, int(op["template_fe_id"]), fe_id, filename,
                                     op.get("title") or "New venue")
    return venue_out, entity_out, sod_out
