# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Open-world phase 1 test map: the FFNY Studio "Big Arena" template (studio branch feat/openworld-template)
built as an override folder (venue 28A), without the Studio GUI. Run from the Studio checkout with the dump at
$DEFJAM_DATA/extracted:  python <this> <out>/override   then DJ_OVERRIDE=<out>\override (backslashes) dj.py ...
"""
import sys, os
from pathlib import Path
sys.path.insert(0, os.getcwd())
from ffnystudio import _repo, game as gamemod, mapbasics, venue, venueedit, collision as collmod, viv
out = Path(sys.argv[1]); proj = out.parent / "proj"
g = gamemod.Game(_repo.game_dir())
r = venueedit.Recipe("V2BG_03A")
r.new_venue = {"code": "28A", "fe_id": 28, "title": "Big Arena", "blank": mapbasics.blank_spec("bigarena")}
r.objects = [mapbasics.floor_object(proj, "bigarena")]
print(venueedit.build_override(g.root / venue.VENUE_ARCHIVE, r, out, project_dir=proj))
a = viv.Archive(str(out / "assets" / "v2bg.viv"))
coll = collmod.parse(a.read("V2BG_28A.cli"))
print("planes", len(coll.planes), coll.kinds())
for row in mapbasics.bigarena_budget(len(coll.planes), 5, 5658, 0): print(row)
