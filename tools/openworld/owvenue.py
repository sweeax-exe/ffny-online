# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Open-world phase 0 test venue (28A "OWTEST", feID 28) from The Foundation (02A export), never released.

usage: python owvenue.py OUT [--floor HALF] [--tris T --draws D --place front|behind|spread]
                             [--planes N] [--spread HALF] [--cli-floor HALF] [--box-visuals]
  --floor HALF     a visible checker floor of +-HALF units (16x16 quads), 0 = none
  --tris/--draws   T extra triangles in D separate mesh nodes (flat grids), placed in view of the stock camera
                   (front: z -60..-900), behind it (behind: z +450..+1300) or over the whole map (spread)
  --planes N       N extra collision planes: boxes (4 barrier walls + a roof floor) on a grid over +-spread,
                   outside the retail arena; they come FIRST in the plane list, so the arena's own 38 planes
                   (its slam walls) get the indices N..N+37
  --cli-floor HALF the arena's big floor plane (plane 35) stretched to +-HALF (default 30000)
Runs Studio's CLI from $STUDIO (default D:/DefJam/DefJamFFNY-recomp/studio).
"""
import argparse, math, os, shutil, struct, subprocess, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
EXPORT = os.environ.get("OW_EXPORT", os.path.join(HERE, "export"))  # Studio export-venue of 02A (glTF); never in git
STUDIO = os.environ.get("STUDIO", r"D:/DefJam/DefJamFFNY-recomp/studio")
sys.path.insert(0, STUDIO)
from ffnystudio import venueimport, viv, collision  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--floor", type=float, default=1500.0)
ap.add_argument("--tris", type=int, default=0)
ap.add_argument("--draws", type=int, default=1)
ap.add_argument("--place", default="front")
ap.add_argument("--planes", type=int, default=0)
ap.add_argument("--spread", type=float, default=1200.0)
ap.add_argument("--cli-floor", type=float, default=30000.0)
ap.add_argument("--box-visuals", action="store_true")
ap.add_argument("--arena-first", action="store_true", help="the arena's own planes first (indices 0..37), the extra planes after")
ap.add_argument("--keep-walls", action="store_true", help="keep the arena's enclosing walls (default: kept)")
a = ap.parse_args()

out = os.path.abspath(a.out)
scene = os.path.join(out, "scene")
if os.path.isdir(out):
    shutil.rmtree(out)
shutil.copytree(EXPORT, scene)
gltf = os.path.join(scene, "V2BG_02A.gltf")
WHITE = (128, 128, 128, 255)
GREY = (90, 90, 100, 255)


def grid_patch(x0, z0, w, d, n, y=0.4):
    """a flat n x n quad grid (2 n^2 triangles), slightly wavy so no face is coplanar with the next"""
    pos, uvs, tris = [], [], []
    for r in range(n + 1):
        for c in range(n + 1):
            pos.append((x0 + c * w / n, y + 0.3 * ((r + c) % 2), z0 + r * d / n))
            uvs.append((c / n, r / n))
    for r in range(n):
        for c in range(n):
            v = r * (n + 1) + c
            tris += [(v, v + 1, v + n + 2), (v, v + n + 2, v + n + 1)]
    return pos, uvs, tris


# ---- visible checker floor
if a.floor > 0:
    cb = ((np.indices((256, 256)) // 32).sum(axis=0) % 2).astype(np.uint8)
    img = Image.fromarray(np.where(cb[..., None] == 1, np.array([200, 200, 200], np.uint8), np.array([60, 70, 90], np.uint8)))
    png = os.path.join(scene, "owfloor.png")
    img.save(png)
    k, H = 16, a.floor
    pos, uvs, tris = [], [], []
    for r in range(k):
        for c in range(k):
            x0, z0, s = -H + c * 2 * H / k, -H + r * 2 * H / k, 2 * H / k
            b = len(pos)
            pos += [(x0, -0.2, z0), (x0 + s, -0.2, z0), (x0 + s, -0.2, z0 + s), (x0, -0.2, z0 + s)]
            uvs += [(0, 0), (1, 0), (1, 1), (0, 1)]
            tris += [(b, b + 2, b + 1), (b, b + 3, b + 2)]
    venueimport.append_mesh_node(gltf, "owfloor", pos, uvs, [WHITE] * len(pos), tris, image=png)

# ---- extra triangles in D nodes
if a.tris > 0:
    per = max(2, a.tris // max(1, a.draws))
    n = max(1, int(math.sqrt(per / 2)))
    cols = int(math.ceil(math.sqrt(a.draws)))
    for i in range(a.draws):
        r, c = divmod(i, cols)
        if a.place == "front":
            area = (-500.0, -900.0, 1000.0, 840.0)        # x0, z0, width, depth
        elif a.place == "behind":
            area = (-500.0, 450.0, 1000.0, 850.0)
        else:
            area = (-a.spread, -a.spread, 2 * a.spread, 2 * a.spread)
        w, d = area[2] / cols, area[3] / cols
        pos, uvs, tris = grid_patch(area[0] + c * w, area[1] + r * d, w * 0.9, d * 0.9, n)
        venueimport.append_mesh_node(gltf, "tri_%04d" % i, pos, uvs, [WHITE] * len(pos), tris, material="0001")

# ---- collision boxes (computed here; visuals optional)
boxes = []
if a.planes > 0:
    nb = int(math.ceil(a.planes / 5))
    side = int(math.ceil(math.sqrt(nb * 1.15))) + 1
    step = 2 * a.spread / side
    size = min(40.0, step * 0.55)
    for r in range(side):
        for c in range(side):
            x = -a.spread + (c + 0.5) * step
            z = -a.spread + (r + 0.5) * step
            if abs(x) < 110 and abs(z) < 110:
                continue                           # keep the retail arena free
            boxes.append((x, z, size))
            if len(boxes) >= nb:
                break
        if len(boxes) >= nb:
            break
if boxes and a.box_visuals:
    chunk = 5000
    for k in range(0, len(boxes), chunk):
        pos, uvs, tris = [], [], []
        for (x, z, s) in boxes[k:k + chunk]:
            h, hs = 30.0, s / 2
            c = [(x - hs, z - hs), (x + hs, z - hs), (x + hs, z + hs), (x - hs, z + hs)]
            for i in range(4):
                (x1, z1), (x2, z2) = c[i], c[(i + 1) % 4]
                b = len(pos)
                pos += [(x1, 0, z1), (x2, 0, z2), (x2, h, z2), (x1, h, z1)]
                uvs += [(0, 1), (1, 1), (1, 0), (0, 0)]
                tris += [(b, b + 1, b + 2), (b, b + 2, b + 3), (b, b + 2, b + 1), (b, b + 3, b + 2)]
            b = len(pos)
            pos += [(x - hs, h, z - hs), (x + hs, h, z - hs), (x + hs, h, z + hs), (x - hs, h, z + hs)]
            uvs += [(0, 0), (1, 0), (1, 1), (0, 1)]
            tris += [(b, b + 2, b + 1), (b, b + 3, b + 2)]
        venueimport.append_mesh_node(gltf, "boxes_%02d" % (k // chunk), pos, uvs, [GREY] * len(pos), tris, material="0019")

cmd = [sys.executable, "-m", "ffnystudio", "import-venue", "02A", gltf, os.path.join(out, "override"),
       "--new-venue", "28A", "--fe-id", "28", "--title", "OWTEST", "--fresh"]
r = subprocess.run(cmd, cwd=STUDIO, capture_output=True, text=True)
print(r.stdout[-1500:])
print(r.stderr[-1500:])
if r.returncode:
    sys.exit(r.returncode)

# ---- collision: extra planes first, then the arena's own, the big floor stretched
arch_path = os.path.join(out, "override", "assets", "v2bg.viv")
arch = viv.Archive(arch_path)
coll = collision.parse(arch.read("V2BG_28A.cli"))
orig = coll.planes
for p in orig:
    if p.horizontal and abs(p.area_field) > 1000:
        F = a.cli_floor
        p.corners = [(-F, 0.0, -F), (-F, 0.0, F), (F, 0.0, F), (F, 0.0, -F)]
        p.area_field = (2 * F) ** 2 / 5
        print("big floor plane %d stretched to +-%.0f" % (p.index, F))
BARRIER = next(m.index for m in coll.materials if m.flags == 0)
FLOORMAT = next(p.material for p in orig if p.horizontal)
print('materials', [(m.index, hex(m.flags)) for m in coll.materials], 'barrier', BARRIER, 'floor', FLOORMAT)
extra = []


def plane(corners, material, outward=None):
    nrm, area = collision.plane_geometry(corners)
    if outward is not None and nrm[0] * outward[0] + nrm[2] * outward[1] < 0:
        corners = [corners[0], corners[3], corners[2], corners[1]]
        nrm, area = collision.plane_geometry(corners)
    return collision.Plane(0, 0, corners, nrm, area, material)


for (x, z, s) in boxes:
    h, hs = 30.0, s / 2
    c = [(x - hs, z - hs), (x + hs, z - hs), (x + hs, z + hs), (x - hs, z + hs)]
    for i in range(4):
        (x1, z1), (x2, z2) = c[i], c[(i + 1) % 4]
        mx, mz = (x1 + x2) / 2 - x, (z1 + z2) / 2 - z
        extra.append(plane([(x1, 0.0, z1), (x2, 0.0, z2), (x2, h, z2), (x1, h, z1)], BARRIER, outward=(mx, mz)))  # material 0: the fresh import's barrier (flags 0)
    roof = [(x - hs, h, z - hs), (x - hs, h, z + hs), (x + hs, h, z + hs), (x + hs, h, z - hs)]
    nrm, area = collision.plane_geometry(roof)
    extra.append(collision.Plane(0, 0, roof, (0.0, 1.0, 0.0), area, FLOORMAT))
extra = extra[:a.planes]
planes = (orig + extra) if a.arena_first else (extra + orig)
for i, p in enumerate(planes):
    p.index = i
    tri = p.flags & 0xFF000000 if p in orig else 0
    kindbits = (p.flags >> 16) & 0xFF if p in orig else 0x08
    p.flags = tri | (kindbits << 16) | ((p.material * 2) & 0xFF) << 8 | (i & 0xFF)
coll.planes = planes
blob = collision.to_bytes(coll)
tmp = arch_path + ".new"
viv.compose_archive(arch_path, tmp, replace={"V2BG_28A.cli": viv.refpack_store(blob)})
os.replace(tmp, arch_path)
print("collision: %d planes (%d extra, %d boxes), %d bytes" % (len(planes), len(extra), len(boxes), len(blob)))
shutil.rmtree(scene)
