#!/usr/bin/env python3
"""Map the approved title logo and menu emblems back onto their native texture tiles; never installs.

The approved logo/emblem artwork (transparency-v2/*-repaired.png, D137/UI) lives on the
generated canvases, not on the game's atlases. transparency-v2 kept, for each one, the guide
placement of the native artwork on that canvas (guide-receipt.json: source alpha bbox -> raw
material bbox) and a dense registration field (u, v) with mapped_source(x + w(x)) ~ canvas(x).
This script pushes every approved canvas pixel through that chain onto the 4x native grid
(forward splatting, so a locally folded field needs no inversion), and
keeps the native (Faithful 4x) alpha exactly, so holes, partial alpha and silhouettes are the
original ones. Texels outside each tile's sampled UV rectangle (stored padding) and texels the
approved artwork does not cover stay Faithful HD. Output: a schema-1 pack component with a
receipt, plus side-by-side review sheets.

python scripts/register-menu-branding.py --art-root /private/pilot-v1 \
    --faithful <mods>/faithful-hd-...-opacity --out <mods>/menu-ui-branding-registered-test-v1 \
    --review <folder for review sheets>
"""
import argparse
import hashlib
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
LABELS = ['title-logo'] + ['emblem-' + s for s in
                           ('story', 'battle', 'options', 'jukebox', 'extras', 'high-scores', 'rewards', 'credits')]
LOGO_ID = 'c194328377ad8dd75b4c3fadfd8886b5bc2af468a92e362519651a7942adea39'  # prestart #19, 512x128


def sha256(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def register(label, art, guide):
    """Return (rgb, coverage) of the approved art resampled onto the 4x faithful assembled canvas."""
    import numpy as np
    from PIL import Image
    faithful = np.asarray(Image.open(art/'branding-v1'/f'{label}-faithful.png').convert('RGBA')).astype('float32')
    approved = np.asarray(Image.open(art/'transparency-v2'/f'{label}-repaired.png').convert('RGBA')).astype('float32')
    field = np.load(art/'transparency-v2'/f'{label}-registration.npz')
    u, v = field['u'].astype('float32'), field['v'].astype('float32')
    sx0, sy0, sx1, sy1 = guide['source_alpha_bbox']
    dx0, dy0, dx1, dy1 = guide['raw_material_bbox']
    h, w = faithful.shape[:2]
    # Forward splat: every canvas pixel x lands at guide position m = x + w(x), i.e. faithful
    # position f(m) (inverse of the crop resize in prepare-guides.py). The canvas is at least as
    # dense as 4x native, so splatting fills the native grid without inverting a folded field.
    ch, cw = approved.shape[:2]
    cy, cx = np.indices((ch, cw)).astype('float32')
    mx, my = cx + u, cy + v
    fx = sx0 + (mx + 0.5 - dx0) * (sx1 - sx0) / (dx1 - dx0) - 0.5
    fy = sy0 + (my + 0.5 - dy0) * (sy1 - sy0) / (dy1 - dy0) - 0.5
    pre = approved.reshape(-1, 4).copy()
    pre[:, :3] *= pre[:, 3:4] / 255
    acc = np.zeros((h * w, 4), 'float64')
    wsum = np.zeros(h * w, 'float64')
    fx, fy = fx.ravel(), fy.ravel()
    x0, y0 = np.floor(fx).astype(int), np.floor(fy).astype(int)
    for ox in (0, 1):
        for oy in (0, 1):
            xi, yi = x0 + ox, y0 + oy
            wt = (1 - np.abs(fx - xi)) * (1 - np.abs(fy - yi))
            ok = (xi >= 0) & (xi < w) & (yi >= 0) & (yi < h) & (wt > 0)
            idx = yi[ok] * w + xi[ok]
            np.add.at(wsum, idx, wt[ok])
            for c in range(4):
                np.add.at(acc[:, c], idx, wt[ok] * pre[ok, c])
    hit = wsum > 0.25
    acc[hit] /= wsum[hit, None]
    acc = acc.reshape(h, w, 4).astype('float32')
    hit = hit.reshape(h, w)
    a = acc[..., 3] * hit
    rgb = np.where(a[..., None] > 1e-3, acc[..., :3] * 255 / np.maximum(a[..., None], 1e-3), faithful[..., :3])
    fyi, fxi = np.indices((h, w))
    inside = (fxi >= sx0) & (fxi < sx1) & (fyi >= sy0) & (fyi < sy1)
    residual = 1.0 - hit.astype('float32')        # share of the native grid the splat did not reach
    # Weight: approved coverage relative to native coverage; full where the approved art is at
    # least as opaque as the original, fading to Faithful where it has no material.
    native_a = faithful[..., 3]
    weight = np.clip(a / np.maximum(native_a, 1.0), 0, 1) * inside * hit
    out = faithful.copy()
    out[..., :3] = rgb * weight[..., None] + faithful[..., :3] * (1 - weight[..., None])
    return out, faithful, weight, float(residual[inside & (native_a > 0)].mean())


def main():
    import numpy as np
    from PIL import Image
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--art-root', type=Path, required=True)
    ap.add_argument('--faithful', type=Path, required=True, help='Faithful HD opacity component (for native tiles)')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--review', type=Path)
    ap.add_argument('--catalogue', type=Path, default=REPO/'config/menu-ui-approved-artwork.json')
    args = ap.parse_args()
    art = args.art_root.resolve()
    catalogue = json.loads(args.catalogue.read_text())
    pins = {f['path']: f['sha256'] for r in catalogue['approval_records'] for f in r['files']}
    guides = {g['label']: g for g in json.loads((art/'transparency-v2/guide-receipt.json').read_text())}
    assembled = {r['label']: r for r in json.loads((art/'branding-v1/assembled-references.json').read_text())}
    faithful_map = {}
    for line in (args.faithful/'manifest.ini').read_text().splitlines():
        k, _, val = line.partition('=')
        if len(k.strip()) == 64:
            faithful_map[k.strip()] = args.faithful/val.strip()
    if args.out.exists():
        raise SystemExit(f'{args.out} exists; packs are immutable, export a new version')
    (args.out/'textures').mkdir(parents=True)
    if args.review:
        args.review.mkdir(parents=True, exist_ok=True)
    lines = ['[pack]', 'schema=1', 'name=Menu UI branding registered test v1', 'png_mips=opacity', '[textures]']
    receipt = {'schema': 1, 'component': args.out.name,
               'status': 'derivative of owner-approved D137/UI artwork registered to native atlases; needs owner visual review',
               'labels': [], 'entries': []}
    for label in LABELS:
        rel = f'transparency-v2/{label}-repaired.png'
        if pins.get(rel) != sha256(art/rel):
            raise SystemExit(f'{rel}: not the approved pin')
        out, faithful, weight, resid = register(label, art, guides[label])
        receipt['labels'].append({'label': label, 'art': rel, 'art_sha256': pins[rel],
                                  'registration_sha256': sha256(art/'transparency-v2'/f'{label}-registration.npz'),
                                  'approved_coverage': float(weight[faithful[..., 3] > 0].mean()),
                                  'unreached_native_share': resid})
        if label == 'title-logo':
            parts = [{'source_id': LOGO_ID, 'assembled_origin': [0, 0], 'stored_size': [512, 128],
                      'nominal_sample_uv': [0, 0, 512, 128], 'image': 19}]
        else:
            parts = assembled[label]['parts']
        for p in parts:
            sid = p['source_id']
            tile = np.asarray(Image.open(faithful_map[sid]).convert('RGBA')).astype('float32')
            sw, sh = p['stored_size']
            if tile.shape[1] != 4 * sw or tile.shape[0] != 4 * sh:
                raise SystemExit(f'{sid}: faithful tile is not 4x {sw}x{sh}')
            ox, oy = (int(round(c * 4)) for c in p['assembled_origin'])
            u0, v0, u1, v1 = p['nominal_sample_uv']
            res = tile.copy()
            # Only the sampled UV rectangle takes registered pixels; stored padding stays Faithful.
            x1, y1 = min(4 * u1, out.shape[1] - ox), min(4 * v1, out.shape[0] - oy)
            region = out[oy + 4 * v0:oy + y1, ox + 4 * u0:ox + x1, :3]
            res[4 * v0:4 * v0 + region.shape[0], 4 * u0:4 * u0 + region.shape[1], :3] = region
            res[..., 3] = tile[..., 3]                    # native alpha, exactly
            path = args.out/'textures'/f'{sid}.png'
            Image.fromarray(np.clip(np.rint(res), 0, 255).astype('uint8'), 'RGBA').save(path)
            lines.append(f'{sid}=textures/{sid}.png')
            receipt['entries'].append({'source_id': sid, 'label': f'{label} image {p["image"]}',
                                       'faithful_parent_sha256': sha256(faithful_map[sid]),
                                       'output': f'textures/{sid}.png', 'output_sha256': sha256(path)})
        if args.review:
            def on(bg, img):
                a = img[..., 3:4] / 255
                return img[..., :3] * a + np.array(bg, 'float32') * (1 - a)
            row = [np.concatenate([on(c, faithful), on(c, out)], 1) for c in ((40, 40, 40), (200, 200, 200))]
            Image.fromarray(np.clip(np.concatenate(row, 0), 0, 255).astype('uint8')).save(args.review/f'{label}-faithful-vs-registered.png')
        print(f'{label}: {len(parts)} tiles, approved coverage {receipt["labels"][-1]["approved_coverage"]:.3f}, '
              f'native texels not reached {resid:.3f}')
    (args.out/'manifest.ini').write_text('\n'.join(lines) + '\n')
    (args.out/'receipt.json').write_text(json.dumps(receipt, indent=1) + '\n')
    print(f'{len(receipt["entries"])} textures -> {args.out}')


if __name__ == '__main__':
    raise SystemExit(main())
