#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Synthetic community fighters beyond the places, for tests: an override folder with N of them.

    python scripts/unlimited-synth.py --count 120 --out <folder> [--game <dump>] [--first 1000] [--stats-every 7]

Each is a new-fighter op as a Studio pack has it, composed by the launcher's own code (``content_compose``,
``newfighter.apply_op(logical=True)``, ``unlimited.add_records``): template = the stock fighters with a cell of their
own in turn, an own model ``V2IP_7xxA`` (the template's model files with the symbols renamed, as Studio writes
them), its camera banks, intro entry, entity and pictures under its logical ID (1000 and up), name ``Syn <ID>``;
every ``--stats-every``-th one has gameplay of its own (upper and lower body 15, speed 12), so a test can tell
whose stats a bank holds. The folder holds only the player's own files re-arranged; it stays local
(``DJ_OVERRIDE`` points a test copy at it)."""

import argparse
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "setup"))

import content_compose as cc  # noqa: E402
from ffnycontent import newfighter as nf, unlimited  # noqa: E402

ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
OWN_STATS = {"version": 1, "stats": {"upper": 15, "lower": 15, "speed": 12, "toughness": 6, "health": 6,
                                     "grapple": 0, "charisma": 6}, "blazins": [5, 6, 7, 8]}


def model_name(i):
    return "V2IP_7%s%sA" % (ALPHABET[(i // 36) % 36], ALPHABET[i % 36])


def templates(sod):
    """The stock fighters with a cell of their own (no outfit row, no place, no bank)."""
    return [r for r in nf.characters(sod) if r["main"] == 1 and r["fe_id"] < nf.FIRST_NEW_FE_ID
            and r["fe_id"] not in nf.OUTFIT_FE_IDS and not nf.is_place(r["fe_id"])]


def build(game, out, count, first=1000, stats_every=7, log=print):
    with tempfile.TemporaryDirectory() as empty:
        bases = cc.Bases(empty, game)
        work = cc.Work(bases)
        stock = templates(work.current(nf.SOD_PATH))
        models = bases.archive(nf.MODEL_ARCHIVE)
        for i in range(count):
            tpl = stock[i % len(stock)]
            model = model_name(i + 1)
            gameplay = OWN_STATS if stats_every and (i + 1) % stats_every == 0 else None
            op = nf.make_op(tpl["fe_id"], tpl["model"], model, "Syn %d" % (first + i), gameplay=gameplay)
            originals = {s: models.read(tpl["model"] + s) for s in nf.MODEL_SUFFIXES if models.has(tpl["model"] + s)}
            for entry, _, _, data in nf.model_entries(tpl["model"], model, originals):
                work.add(nf.MODEL_ARCHIVE, entry, data)
            cc.apply_new_fighter(work, op, first + i)
        applied, skipped = [], []
        cc.finish_logical(work, applied, skipped, log)
        if skipped:
            raise SystemExit("not composed: %s" % skipped)
        written = cc.write_files(Path(out), work)
    log("%d fighters %d..%d into %s: %s" % (count, first, first + count - 1, out, ", ".join(written)))
    return written


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--count", type=int, default=120)
    ap.add_argument("--first", type=int, default=1000)
    ap.add_argument("--stats-every", type=int, default=7)
    ap.add_argument("--out", required=True)
    ap.add_argument("--game", default=None, help="the extracted dump (default: game/ or $DEFJAM_DATA/extracted)")
    a = ap.parse_args()
    game = a.game or (ROOT / "game" if (ROOT / "game" / "default.xbe").is_file()
                      else Path(os.environ.get("DEFJAM_DATA", "")) / "extracted")
    build(game, a.out, a.count, a.first, a.stats_every)


if __name__ == "__main__":
    main()
