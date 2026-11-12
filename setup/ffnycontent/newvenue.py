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
"""

import re
from types import SimpleNamespace

from . import sod as sodmod

VENUE_XML = "venue.xml"
ENTITY_XML = "assets/entity.xml"
VENUE_ARCHIVE = "assets/v2bg.viv"
ICROWD_ARCHIVE = "assets/icrowd.viv"
LOCATOR_ENTRY = re.compile(r"icl_\d\d[A-Z]\.o")
LOCATOR_MIN, LOCATOR_MAX = 4 * 1024, 64 * 1024     # retail: 10640 to 20592 bytes


class VenueError(ValueError):
    pass


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


def apply(venue_xml, entity_xml, sod_blob, op, taken_names=()):
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
    if op.get("drop_objects"):
        block = drop_entity_models(block, filename)
    if op.get("crowd_locator") is not None:
        block = set_crowd_locator(block, op["crowd_locator"])
    end = ent.rindex("</entitylist>")
    entity_out = (ent[:end].rstrip("\r\n") + "\r\n\t" + block + "\r\n" + ent[end:]).encode("latin-1")
    sod_out, _ = sodmod.append_venue(sod_blob, int(op["template_fe_id"]), fe_id, filename,
                                     op.get("title") or "New venue")
    return venue_out, entity_out, sod_out
