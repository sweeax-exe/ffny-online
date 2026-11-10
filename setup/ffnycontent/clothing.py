# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Clothing packs' ops on the player's own files (FFNY Studio ``ffnystudio/clothing.py`` ``apply_ops``).

A clothing pack never ships the database, the parts list or the name tables: its ``colour`` and ``item`` ops make
the same append-only edits on the player's ``assets/djv2.sod``, ``createplayer_parts.xml`` (in
``assets/v2cp_0.viv``) and ``cmusen/cmuken/cmfren.loc`` (in ``assets/misc.viv``), so IDs in saves stay valid. The
new textures and thumbnails are the pack's ``add`` entries (deltas against the template item's own entries).
"""

import re
import struct

from . import cpparts, loc, sod, sodwrite

SOD_PATH = "assets/djv2.sod"
PARTS_ARCHIVE = "assets/v2cp_0.viv"
PARTS_XML = "createplayer_parts.xml"
NAMES_ARCHIVE = "assets/misc.viv"
NAME_TABLES = ("cmusen.loc", "cmuken.loc", "cmfren.loc")


class ClothingError(ValueError):
    pass


def text_hash(kind, text, salt=""):
    """The hash a new name gets in the database and the name tables (Studio's namespace)."""
    return sod.name_hash("FFNYS:%s:%s:%s" % (kind, salt, text))


def row_name_for(hint, floor):
    """The row name an item op gets: the pack's hint when its hash is above every Clothing key today, else
    ``name_above(<the hint's stem>, floor)``, so packs made against the same database combine in order."""
    if sod.name_hash(hint) > floor:
        return hint
    m = re.fullmatch(r"(.+?)_[a-z]{7}", hint)
    return sodwrite.name_above(m.group(1) if m else hint, floor)


def apply_ops(sod_blob, parts_xml, name_tables, ops):
    """The database, parts list and name tables with ``ops`` applied (one pack's, or several in order).
    Returns (sod bytes, parts xml text, {table: bytes})."""
    ed = sodwrite.SodEditor(sod_blob)
    clothing_table = ed.base.table_index(sod.CLOTHING)
    texts = []
    for op in ops:
        db = ed.db()
        rows = {r.name: r for r in db.clothing()}
        if op["op"] == "colour":
            row = rows.get(op["item"])
            if row is None:
                raise ClothingError("no item %r" % op["item"])
            h = text_hash("colour", op["label"], op["textures"][0]["texture"])
            desc = ed.add_string(text_hash=h)
            texts.append((h, op["label"]))
            ed.add_colour(row.offset, int(op["number"]), desc)
            for t in op["textures"]:
                parts_xml = cpparts.append_colour(parts_xml, op["section"], t["model"], t["first_texture"],
                                                  t["texture"])
        elif op["op"] == "item":
            row = rows.get(op["template"])
            if row is None:
                raise ClothingError("no template item %r" % op["template"])
            keys = db.rows(clothing_table)[0]
            row_name = row_name_for(op["row_name"], max(keys) if keys else 0)
            words = list(struct.unpack_from("<9I", db.blob, row.offset))
            h_name = text_hash("item", op["name"], row_name)
            texts.append((h_name, op["name"]))
            colours = []
            for c in op["colours"]:
                h = text_hash("colour", c["label"], c["texture"])
                texts.append((h, c["label"]))
                colours.append((ed.add_string(text_hash=h), int(c["number"])))
            words[0] = ed.add_string(row_name)
            words[2] = ed.string_index(op["group"])
            words[3] = ed.new_colour_list(colours)
            words[4] = int(op["price"])
            words[5] = ed.add_string(text_hash=h_name)
            words[7] = words[7] + 1
            item_id = ed.add_row(clothing_table, words, sod.name_hash(row_name), row.offset)
            if op.get("bundle"):
                ed.add_bundle_item(op["bundle"], item_id)
            for v in op["variants"]:
                parts_xml = cpparts.append_texture(parts_xml, op["section"], v["model"], v["textures"][0],
                                                   v["textures"][1:])
        else:
            raise ClothingError("unknown op %r" % op["op"])
    tables = {}
    for name, blob in name_tables.items():
        table = loc.Loc(blob)
        tables[name] = table.with_texts([(h, t) for h, t in texts if h not in table])
    return ed.finish(), parts_xml, tables
