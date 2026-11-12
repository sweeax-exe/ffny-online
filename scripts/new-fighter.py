#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""A test override folder with a new fighter ("Doc 2"): a Character row copied from a template, for dj.py --override.

    python scripts/new-fighter.py --out <folder> [--template 55] [--fe-id 100] [--name "Doc 2"]
           [--display "Doc 2"] [--model V2IP_109A | --new-model V2IP_901A] [--set WORD=VALUE ...]
           [--data <DEFJAM_DATA>] [--list]

Writes ``<folder>/assets/djv2.sod`` (the player's own database plus the row), with ``--fighter-xml`` a
``<folder>/fighter.xml`` record, and with ``--new-model`` the template model's files copied under the new name into
``<folder>/assets/v2ip.viv`` and an ``assets/entity.xml`` entity. The files are the player's own data
re-arranged; they stay on this machine (never commit them). ``--list`` prints the Character rows.
"""
import argparse
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "setup"))

from ffnycontent import newfighter as nf  # noqa: E402
from ffnycontent import viv  # noqa: E402


def data_root(arg):
    if arg:
        return Path(arg)
    if os.environ.get("DEFJAM_DATA"):
        return Path(os.environ["DEFJAM_DATA"])
    return HERE.parent / "game" / ".."


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out")
    ap.add_argument("--data", help="<DEFJAM_DATA> (default: env DEFJAM_DATA, else the repo's game junction)")
    ap.add_argument("--template", type=int, default=55)
    ap.add_argument("--fe-id", type=int, action="append", help="repeat for several copies")
    ap.add_argument("--name", default="Doc 2")
    ap.add_argument("--display", default=None)
    ap.add_argument("--model", default=None, help="an existing model name for the row (default: the template's)")
    ap.add_argument("--new-model", default=None, help="copy the template's model files under this name")
    ap.add_argument("--model-source", default=None, help="with --new-model: copy this model's files (default the "
                    "template's; a test of the new model's own look)")
    ap.add_argument("--no-entity", action="store_true", help="with --new-model: no entity.xml entity (a test)")
    ap.add_argument("--keep-symbols", action="store_true", help="with --new-model: leave the model's own symbol "
                    "names (V2IP_109A_alpha...) in the copied .o (a test)")
    ap.add_argument("--no-taunt", action="store_true", help="with --new-model: no assets/taunt.xml entry (a test)")
    ap.add_argument("--no-banks", action="store_true", help="with --new-model: no CE_/CI_ camera banks (a test)")
    ap.add_argument("--no-face", action="store_true", help="with --new-model: no _F.o (a test)")
    ap.add_argument("--pictures", choices=("none", "copy", "renamed"), default="none",
                    help="menu pictures under the new ID from the template's: outer copies only, or with the inner "
                    "entries renamed (screens/screens.viv, composed over <data>/override's when it has one)")
    ap.add_argument("--fighter-xml", action="store_true", help="also a fighter.xml record (not needed: Battle mode and "
                    "the hub read the database; the game reads FIGHTER.XML for created-fighter archetypes 86..141, so a "
                    "record with the new ID duplicates one of those)")
    ap.add_argument("--set", action="append", default=[], help="WORD=VALUE: a row word (stats)")
    ap.add_argument("--gameplay", help="a gameplay block (JSON, as a pack's new-fighter op holds it; newfighter.py "
                                        "gameplay_words) for every new row: its own stats, styles and Blazins")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    root = data_root(a.data)
    game = root / "extracted" if (root / "extracted").is_dir() else Path(HERE.parent / "game")
    sod = (game / "assets" / "djv2.sod").read_bytes()
    if a.list:
        for r in nf.characters(sod):
            print("%2d feID %3d core %3d main %d %-20s %-18s %s" % (r["index"], r["fe_id"], r["core_id"], r["main"],
                                                                 r["name"], r["display"], r["model"]))
        return 0
    if not a.out:
        ap.error("--out is required")
    out = Path(a.out)
    fe_ids = a.fe_id or [100]
    edits = {}
    if a.gameplay:
        import json
        edits.update(nf.gameplay_words(json.loads(a.gameplay)))
    for s in a.set:
        k, v = s.split("=", 1)
        edits[int(k)] = int(v)
    rows = nf.characters(sod)
    tpl = next(r for r in rows if r["fe_id"] == a.template)
    model = a.new_model or a.model
    fighter_xml = (game / "fighter.xml").read_bytes()
    for n, fe in enumerate(fe_ids):
        name = a.name if len(fe_ids) == 1 else "%s %d" % (a.name, n + 1)
        sod, row_name, item = nf.append_character(sod, a.template, fe, name, a.display or name, model, edits)
        fighter_xml = nf.add_fighter(fighter_xml, a.template, fe, a.display or name, model)
        print("feID %d: row %r, item ID %08X, model %s" % (fe, row_name, item, model or tpl["model"]))
    (out / "assets").mkdir(parents=True, exist_ok=True)
    (out / "assets" / "djv2.sod").write_bytes(sod)
    if a.fighter_xml:
        (out / "fighter.xml").write_bytes(fighter_xml)
    if a.new_model:
        src = str(game / "assets" / "v2ip.viv")
        arc = viv.Archive(src)
        names = {x.upper(): x for x in arc.names()}
        add = []
        source = a.model_source or tpl["model"]
        for suffix in (".o", ".xsh") + (() if a.no_face else ("_F.o",)):
            key = (source + suffix).upper()
            if key in names:
                blob = arc.read(names[key])
                if not a.keep_symbols:
                    blob = nf.rename_model_symbols(blob, source, a.new_model)
                add.append((a.new_model + suffix, viv.refpack_store(blob)))
        viv.compose_archive(src, str(out / "assets" / "v2ip.viv"), add=add)
        if not a.no_entity:
            ent = nf.add_entity((game / "assets" / "entity.xml").read_bytes(), tpl["model"], a.new_model)
            (out / "assets" / "entity.xml").write_bytes(ent)
        print("v2ip.viv: + %s (from %s)" % (", ".join(n for n, _ in add), source))
        if not a.no_taunt:
            # the fight reads the intro/extro list from assets/IntroExtro.xml in nis.viv (the loose
            # assets/taunt.xml is the same list and is not read)
            nis = str(game / "assets" / "nis.viv")
            narc = viv.Archive(nis)
            entry = next(n for n in narc.names() if n.lower() == "assets/introextro.xml")
            taunt = nf.add_taunt(narc.read(entry), tpl["model"], a.new_model)
            viv.compose_archive(nis, str(out / "assets" / "nis.viv"), replace={entry: viv.refpack_store(taunt)})
            print("nis.viv: %s with %s" % (entry, a.new_model))
        if not a.no_banks:
            v2ta = str(game / "assets" / "v2ta.viv")
            banks = nf.model_banks(viv.Archive(v2ta), tpl["model"], a.new_model)
            viv.compose_archive(v2ta, str(out / "assets" / "v2ta.viv"),
                                add=[(n, viv.refpack_store(b)) for n, b in banks])
            print("v2ta.viv: + %s" % ", ".join(n for n, _ in banks))
    if a.pictures != "none":
        base = root / "override" / "screens" / "screens.viv"
        if not base.is_file():
            base = game / "screens" / "screens.viv"
        arc = viv.Archive(str(base))
        add = []
        for fe in fe_ids:
            add += [(n, viv.refpack_store(d))
                    for n, d in nf.picture_copies(arc, a.template, fe, rename_inner=a.pictures == "renamed")]
        (out / "screens").mkdir(parents=True, exist_ok=True)
        viv.compose_archive(str(base), str(out / "screens" / "screens.viv"), add=add)
        print("screens.viv (%s): + %d pictures" % (base, len(add)))
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
