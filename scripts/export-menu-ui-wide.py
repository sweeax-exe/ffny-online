#!/usr/bin/env python3
"""Export widescreen side slabs ([wide] texture-pack entries) from the approved wide menu masters.

Each layer in config/menu-ui-wide-layers.json names an anchor texture (a background tile the game
draws), the approved wide master that extends it, and the anchor's runtime scale (`per`: title
pixels per source texel, measured with RECOMP_WS_WIDE_TRACE=1). Masters are 3 master pixels per
title pixel (1920 = the 640-wide 4:3 picture). `x0` is the anchor's settled left edge in title
pixels; `sides` keeps slabs left of, right of, or on both sides of the 4:3 picture. The anchor's
position in the master is found by matching its approved 4x tile (only the `match_u` texel columns:
those the game samples and nothing in front of it covers in the master) against the master and must agree to `min_score`. A slab is the
master region at the anchor's own geometry shifted dt source texels, at the anchor replacement's
4x size; the renderer clips slabs to the side areas, so only the art beyond 4:3 shows. Areas past
the master's edges are transparent. `u_range` is the texel columns the game samples (the replayed
quad covers only those, so slabs step by its width); `inner` makes the dt 0 slab also replace the
anchor in widescreen, for art whose 4:3 edges were made for the wide view (the Rewards wall).
`stretch` lists full-screen overlays (vignettes) to draw across the whole widescreen picture.

python scripts/export-menu-ui-wide.py --art-root /private/pilot-v1 --mods <data>/mods \
    --out <data>/mods/menu-ui-wide-test-v1
"""
import argparse
import hashlib
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
MASTER_PX = 3.0          # master pixels per 640-wide title pixel (1440 high masters, 1536 for 512 rows)
SCALE = 4                # slab = 4x the anchor source, like the restored replacements


def sha256(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def locate(master, tile, s, hint=None):
    """Best (x, y) of tile (RGB, already at master scale) inside master, by normalised correlation."""
    import numpy as np
    from PIL import Image
    def gray(a):
        return a[..., :3].astype('float32').mean(2)
    m, t = gray(master), gray(tile)
    # Coarse search at 1/8, then refine at full resolution around the best match.
    f = 8
    small = lambda a: np.asarray(Image.fromarray(a).resize((max(1, a.shape[1] // f), max(1, a.shape[0] // f)), Image.BOX))
    ms, ts = small(m), small(t)
    th, tw = ts.shape
    best = (-2, 0, 0)
    ys = range(-th // 2, ms.shape[0] - th // 2 + 1)
    xs = range(-tw // 2, ms.shape[1] - tw // 2 + 1)
    def score(M, T, x, y):
        h, w = T.shape
        x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, M.shape[1]), min(y + h, M.shape[0])
        if (x1 - x0) * (y1 - y0) < 0.4 * w * h:
            return -2
        a = M[y0:y1, x0:x1]; b = T[y0 - y:y1 - y, x0 - x:x1 - x]
        a = a - a.mean(); b = b - b.mean()
        d = np.sqrt((a * a).sum() * (b * b).sum())
        return float((a * b).sum() / d) if d > 0 else -2
    if hint is not None:
        ys = range(int(hint[1] / f) - 3, int(hint[1] / f) + 4)
        xs = range(int(hint[0] / f) - 3, int(hint[0] / f) + 4)
    for y in ys:
        for x in xs:
            sc = score(ms, ts, x, y)
            if sc > best[0]:
                best = (sc, x, y)
    _, bx, by = best
    best = (-2, 0, 0)
    for y in range(by * f - f, by * f + f + 1):
        for x in range(bx * f - f, bx * f + f + 1):
            sc = score(m, t, x, y)
            if sc > best[0]:
                best = (sc, x, y)
    return best


def slab_shifts(lo, hi, step):
    """Runtime manifest offsets are integer source texels, including fractional authored UV spans."""
    return sorted({round(i * step) for i in range(lo, hi + 1)})


def registered_inputs(catalogue, registrations):
    """Keep engineering derivatives separate from owner approvals and require an approved parent."""
    pins = {f['path']: f['sha256'] for r in catalogue['approval_records'] for f in r['files']}
    approved = dict(pins)
    sizes = {s['source_id']: (s['aliases'][0]['width'], s['aliases'][0]['height'])
             for s in catalogue['reviewed_sources']}
    for item in registrations.get('files', []):
        if item['derived_from'] not in approved or approved[item['derived_from']] != item['derived_from_sha256']:
            raise ValueError('Registration requires an unchanged approved parent: ' + item['path'])
        if item['path'] in pins:
            raise ValueError('Registration cannot replace an approval: ' + item['path'])
        pins[item['path']] = item['sha256']
    for source in registrations.get('sources', []):
        sid = source['source_id']
        if sid in sizes:
            raise ValueError('Duplicate registered source: ' + sid)
        alias = source['aliases'][0]
        sizes[sid] = (alias['width'], alias['height'])
    return pins, sizes


def main():
    import numpy as np
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--art-root', type=Path, required=True)
    ap.add_argument('--mods', type=Path, required=True, help='mods folder holding the 4x anchor replacements')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--layers', type=Path, default=REPO/'config/menu-ui-wide-layers.json')
    ap.add_argument('--catalogue', type=Path, default=REPO/'config/menu-ui-approved-artwork.json')
    ap.add_argument('--registrations', type=Path, default=REPO/'config/menu-ui-runtime-registrations.json')
    ap.add_argument('--only', help='comma-separated layer labels')
    args = ap.parse_args()
    art = args.art_root.resolve()
    cfg = json.loads(args.layers.read_text())
    catalogue = json.loads(args.catalogue.read_text())
    registrations = json.loads(args.registrations.read_text()) if args.registrations.exists() else {}
    pins, sizes = registered_inputs(catalogue, registrations)
    replacements = {}
    for comp in cfg['anchor_packs']:
        for line in (args.mods/comp/'manifest.ini').read_text().splitlines():
            k, _, v = line.partition('=')
            if len(k.strip()) == 64:
                replacements.setdefault(k.strip(), args.mods/comp/v.strip())   # first listed pack wins
    if args.out.exists():
        raise SystemExit(f'{args.out} exists; packs are immutable, export a new version')
    (args.out/'slabs').mkdir(parents=True)
    lines = ['[pack]', 'schema=1', 'name=' + cfg['name'], '[wide]']
    receipt = {'schema': 1, 'component': args.out.name, 'status': cfg['status'], 'layers': []}
    only = set(args.only.split(',')) if args.only else None
    masters = {}
    for layer in cfg['layers']:
        if only and layer['label'] not in only:
            continue
        sid = layer['anchor']
        w, h = sizes[sid]
        rel = layer['master']
        if pins.get(rel) is None:
            raise SystemExit(f'{rel}: not an approved pin')
        if rel not in masters:
            if sha256(art/rel) != pins[rel]:
                raise SystemExit(f'{rel}: SHA-256 does not match its approval pin')
            masters[rel] = np.asarray(Image.open(art/rel).convert('RGBA'))
        master = masters[rel]
        s = MASTER_PX * layer['per']                       # master pixels per anchor texel
        tile = Image.open(replacements[sid]).convert('RGBA')
        # Match only texel columns the game samples and nothing in front of covers in the master.
        ua, ub = layer.get('match_u', [0, w])
        tile = tile.crop((round(ua * tile.width / w), 0, round(ub * tile.width / w), tile.height))
        tile_s = np.asarray(tile.resize((round((ub - ua) * s), round(h * s)), Image.LANCZOS))
        score, mx0, my0 = locate(master, tile_s, s, layer.get('hint'))
        mx0 -= round(ua * s)
        if score < layer.get('min_score', 0.9):
            raise SystemExit(f'{layer["label"]}: anchor match {score:.3f} below {layer.get("min_score", 0.9)}')
        if 'offset' in layer:                              # explicit placement overrides the match
            mx0, my0 = layer['offset']
        # The replayed quad covers only the texels the game samples (u_range), so slabs step by that.
        ua, ub = layer.get('u_range', [0, w])
        step = ub - ua
        lo = int(np.floor((-mx0 / s - ub) / step))
        hi = int(np.ceil((master.shape[1] - mx0) / s / step))
        # Pad the master with transparency so regions past its edges come out clear.
        pad = int(s * 2 * (w + step)) + 16
        src = Image.new('RGBA', (master.shape[1] + 2 * pad, master.shape[0] + 2 * pad))
        src.paste(Image.fromarray(master), (pad, pad))
        slabs = []
        for dt in slab_shifts(lo, hi, step):
            # Settled screen extent of this slab; keep only slabs reaching the requested side area.
            start = layer['x0'] + dt * layer['per']
            end = start + step * layer['per']
            want = {'left': start < 0, 'right': end > 640, 'both': start < 0 or end > 640, 'none': False}[layer.get('sides', 'both')]
            inner = layer.get('inner') and dt == 0
            if not (want or inner):
                continue
            # Master region for the anchor's texels [dt, dt + w) x [0, h), sampled at 4x.
            out_w, out_h = SCALE * w, SCALE * h
            x0, y0 = mx0 + dt * s, my0
            box = (x0 + pad, y0 + pad, x0 + pad + w * s, y0 + pad + h * s)
            slab = src.resize((out_w, out_h), Image.LANCZOS, box=box)
            if not np.asarray(slab)[..., 3].any():
                continue
            name = f'slabs/{sid[:16]}_{"inner" if inner else ("m" if dt < 0 else "p") + str(abs(dt))}.png'  # [A-Za-z0-9_.-/]
            slab.save(args.out/name)
            slabs.append({'dt': 'inner' if inner else dt, 'path': name, 'sha256': sha256(args.out/name)})
        if len(slabs) > 16:
            raise SystemExit(f'{layer["label"]}: {len(slabs)} slabs, the loader takes 16')
        lines.append(f'{sid}=' + ';'.join(f'{x["dt"]}:{x["path"]}' for x in slabs))
        receipt['layers'].append({'label': layer['label'], 'anchor': sid, 'master': rel, 'master_sha256': pins[rel],
                                  'per': layer['per'], 'anchor_in_master': [mx0, my0], 'match_score': round(score, 4),
                                  'slabs': slabs})
        print(f'{layer["label"]}: anchor at master ({mx0}, {my0}) score {score:.3f}, {len(slabs)} slabs')
    for item in cfg.get('stretch', []):
        lines.append(f'{item["id"]}=stretch')
        receipt.setdefault('stretch', []).append(item)
    (args.out/'manifest.ini').write_text('\n'.join(lines) + '\n')
    (args.out/'receipt.json').write_text(json.dumps(receipt, indent=1) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
