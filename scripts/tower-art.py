#!/usr/bin/env python3
"""Renders the three Towers buildings (the port's own art) to PNG textures.

    python scripts/tower-art.py [--data <DEFJAM_DATA>] [--out DIR] [--check]

Writes <data>/online-art/towers/tower<N>-512.png (512 x 1024, transparent around the building), tower<N>-128.png
(128 x 256) and towers.json (version, and where each picture's floor column is). N = 1 UPTOWN (a brownstone
tenement: brick, fire escape, a water tank on the roof), 2 FIVE BOROUGHS (a skyline stack in setbacks with a spire
and its neighbours), 3 THE WEEKLY (a night warehouse tower in iron with a neon WEEKLY sign down its side).

The repository holds no image: everything is drawn here with Pillow, on the player's computer, from a fixed seed (the
same pictures every time). scripts/online-art.py calls this at setup; without the pictures the game draws its own
simple buildings (towers_ui.inc), so a failure is reported, not fatal. The floor marks, the glow, the plates and the
text are drawn live by the game over the marked floor column (COLUMN below; towers_ui.inc TOWER_ART_COLUMN holds the
same numbers).
"""
import argparse
import json
import os
from pathlib import Path
import random
import sys

VERSION = 1
W, H = 512, 1024
SS = 2                      # drawn at twice the size, then scaled down (smooth edges)
# each tower's floor column: x centre, top, bottom, width, as fractions of the picture (towers_ui.inc TOWER_ART_COLUMN)
COLUMN = {1: (0.50, 0.20, 0.90, 0.13), 2: (0.50, 0.16, 0.96, 0.11), 3: (0.43, 0.15, 0.88, 0.12)}
NAMES = {1: "UPTOWN", 2: "FIVE BOROUGHS", 3: "THE WEEKLY"}


def default_data():
    return os.environ.get("DEFJAM_DATA") or str(Path(__file__).resolve().parents[2] / "data")


class Pen:
    """Drawing in the picture's own units (0..512 x 0..1024), at SS times the size."""

    def __init__(self, img):
        from PIL import ImageDraw
        self.img = img
        self.d = ImageDraw.Draw(img, "RGBA")

    def _blend(self, colours, draw):
        """Pillow's draw replaces the pixels of an RGBA picture: a see-through shape goes on a layer of its own,
        composited over the picture (opaque ones are drawn straight)."""
        if all(c is None or len(c) < 4 or c[3] >= 255 for c in colours):
            draw(self.d, 0, 0)
            return
        from PIL import Image, ImageDraw
        layer = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
        draw(ImageDraw.Draw(layer, "RGBA"), 0, 0)
        box = layer.getbbox()
        if box:
            self.img.alpha_composite(layer.crop(box), (box[0], box[1]))

    def rect(self, x0, y0, x1, y1, fill, outline=None, width=1):
        self._blend((fill, outline), lambda d, ox, oy: d.rectangle([x0 * SS, y0 * SS, x1 * SS - 1, y1 * SS - 1], fill=fill,
                                                                   outline=outline, width=width * SS))

    def poly(self, pts, fill):
        self._blend((fill,), lambda d, ox, oy: d.polygon([(x * SS, y * SS) for x, y in pts], fill=fill))

    def line(self, pts, fill, width=1):
        self._blend((fill,), lambda d, ox, oy: d.line([(x * SS, y * SS) for x, y in pts], fill=fill, width=max(1, int(width * SS))))

    def ellipse(self, x0, y0, x1, y1, fill):
        self._blend((fill,), lambda d, ox, oy: d.ellipse([x0 * SS, y0 * SS, x1 * SS, y1 * SS], fill=fill))


def shade(c, k, a=None):
    r, g, b = (max(0, min(255, int(v * k))) for v in c[:3])
    return (r, g, b, c[3] if a is None and len(c) > 3 else 255 if a is None else a)


def windows(p, rnd, x0, y0, cols, rows, w, h, gx, gy, lit_col, dark_col, frame, skip=None, lit=0.38):
    for r in range(rows):
        for c in range(cols):
            if skip and skip(c, r):
                continue
            x, y = x0 + c * (w + gx), y0 + r * (h + gy)
            on = rnd.random() < lit
            col = lit_col if on else dark_col
            if on:   # a warm glow around a lit window
                p.rect(x - 2, y - 2, x + w + 2, y + h + 2, shade(lit_col, 1.0, 40))
            p.rect(x, y, x + w, y + h, col)
            if on and rnd.random() < 0.5:   # a shade half down, a figure's shadow
                p.rect(x, y, x + w, y + h * rnd.uniform(0.2, 0.5), shade(lit_col, 0.72))
            p.rect(x - 1, y + h, x + w + 1, y + h + 3, frame)          # the sill


def column_shaft(p, kind):
    cx, top, bottom, cw = COLUMN[kind]
    x0, x1 = (cx - cw / 2) * W - 6, (cx + cw / 2) * W + 6
    p.rect(x0, top * H - 8, x1, bottom * H + 8, (10, 9, 12, 235))
    p.rect(x0, top * H - 8, x0 + 3, bottom * H + 8, (255, 255, 255, 22))


def uptown(p, rnd):
    """A brownstone tenement on a corner: brick, a cornice, a fire escape zigzag, the water tank, the stoop."""
    x0, x1, top, ground = 86, 426, 170, 1000
    brick = (132, 60, 42, 255)
    p.rect(x0, top, x1, ground, brick)
    for y in range(top + 6, ground, 9):     # brick courses, staggered joints, a few darker bricks
        p.line([(x0, y), (x1, y)], (60, 26, 20, 150), 1)
        off = 0 if (y // 9) % 2 else 11
        for x in range(x0 + off, x1, 22):
            p.line([(x, y - 9), (x, y)], (60, 26, 20, 110), 1)
            if rnd.random() < 0.10:
                p.rect(x + 1, y - 8, x + 21, y - 1, shade(brick, rnd.uniform(0.65, 0.85)))
    # soot from the roof down, light from the street up
    for i in range(40):
        p.rect(x0, top + i * 4, x1, top + i * 4 + 4, (0, 0, 0, max(0, 60 - i * 2)))
    # the cornice and its brackets
    p.rect(x0 - 16, top - 30, x1 + 16, top - 4, (78, 44, 34, 255))
    p.rect(x0 - 8, top - 4, x1 + 8, top + 6, (58, 32, 26, 255))
    for x in range(x0 - 6, x1 + 6, 24):
        p.rect(x, top - 4, x + 8, top + 14, (66, 38, 30, 255))
    # the water tank on its legs, a cone roof
    tx = 300
    for lx in (tx + 6, tx + 66):
        p.line([(lx, top - 30), (lx, top - 70)], (54, 40, 32, 255), 4)
    p.line([(tx + 6, top - 50), (tx + 66, top - 50)], (54, 40, 32, 255), 3)
    p.rect(tx - 6, top - 140, tx + 78, top - 70, (110, 78, 52, 255))
    for x in range(tx - 6, tx + 78, 10):
        p.line([(x, top - 140), (x, top - 70)], (80, 54, 36, 200), 1)
    for y in (top - 128, top - 96):
        p.line([(tx - 8, y), (tx + 80, y)], (50, 40, 34, 255), 3)
    p.poly([(tx - 14, top - 140), (tx + 86, top - 140), (tx + 36, top - 178)], (70, 50, 38, 255))
    # windows: two bays left and right of the floor column, lit at random
    def skip(c, r):
        return False
    lit, dark, frame = (255, 198, 112, 255), (22, 20, 26, 255), (176, 150, 128, 255)
    windows(p, rnd, 116, top + 40, 2, 15, 38, 30, 26, 22, lit, dark, frame, skip)
    windows(p, rnd, 334, top + 40, 2, 15, 38, 30, 26, 22, lit, dark, frame, skip)
    # the fire escape over the left bay: landings, rails, a zigzag of ladders
    iron = (34, 34, 38, 255)
    for i in range(7):
        y = top + 84 + i * 104
        p.rect(100, y, 220, y + 5, iron)
        for x in range(100, 221, 12):
            p.line([(x, y), (x, y - 26)], iron, 1.5)
        p.line([(100, y - 26), (220, y - 26)], iron, 2)
        if i < 6:
            a, b = (110, 210) if i % 2 == 0 else (210, 110)
            p.line([(a, y + 5), (b, y + 104)], iron, 3)
            for k in range(1, 9):
                t = k / 9
                rx, ry = a + (b - a) * t, y + 5 + 99 * t
                p.line([(rx - 8, ry), (rx + 8, ry)], iron, 1.5)
    column_shaft(p, 1)
    # the shop at street level: a roll-down gate with a tag, the stoop
    p.rect(x0, ground - 110, x1, ground, (40, 30, 30, 255))
    p.rect(x0 + 14, ground - 96, 200, ground, (88, 88, 94, 255))
    for y in range(ground - 96, ground, 6):
        p.line([(x0 + 14, y), (200, y)], (56, 56, 62, 255), 1)
    for k in range(6):      # a spray tag: loops in two colours
        cx, cy = 120 + k * 13, ground - 50 + rnd.uniform(-8, 8)
        p.ellipse(cx - 9, cy - 12, cx + 9, cy + 12, (255, 120, 30, 200) if k % 2 else (90, 200, 255, 200))
        p.ellipse(cx - 5, cy - 7, cx + 5, cy + 7, (88, 88, 94, 255))
    p.rect(310, ground - 96, x1 - 14, ground - 40, (255, 196, 110, 210))      # a lit bodega window
    p.rect(310, ground - 100, x1 - 14, ground - 96, (200, 40, 40, 255))
    for k in range(4):
        p.rect(256 - 30 - k * 8, ground - 16 + k * 4 - 16, 256 + 30 + k * 8, ground - 12 + k * 4 - 16, (96, 66, 50, 255))


def boroughs(p, rnd):
    """The city in one stack: two neighbours, the tower in setbacks with a spire and a red light."""
    steel, dark = (54, 76, 108, 255), (26, 34, 50, 255)
    p.rect(20, 640, 130, 1010, dark)
    windows(p, rnd, 30, 660, 4, 17, 14, 12, 12, 9, (150, 196, 240, 255), (14, 18, 28, 255), (40, 52, 72, 255), lit=0.3)
    p.rect(388, 560, 500, 1010, shade(dark, 0.9))
    windows(p, rnd, 398, 580, 4, 20, 14, 12, 12, 9, (170, 210, 255, 255), (14, 18, 28, 255), (40, 52, 72, 255), lit=0.3)
    p.rect(440, 760, 510, 1010, shade(dark, 0.7))
    steps = [(330, 330), (270, 220), (200, 170), (130, 110)]     # width, height from the ground up
    y = 1010
    for k, (w, h) in enumerate(steps):
        x0, x1 = 256 - w / 2, 256 + w / 2
        y0 = y - h
        p.rect(x0, y0, x1, y, shade(steel, 1.0 - 0.06 * k))
        p.rect(x0, y0, x1, y0 + 6, (160, 196, 232, 255))           # the setback's lit edge
        for x in range(int(x0) + 10, int(x1) - 10, 16):            # window strips, leaving the column clear
            if abs(x + 4 - 256) < 40:
                continue
            for yy in range(int(y0) + 14, int(y) - 8, 14):
                on = rnd.random() < 0.34
                p.rect(x, yy, x + 8, yy + 8, (172, 214, 255, 255) if on else (16, 22, 34, 255))
        y = y0
    p.poly([(240, y), (272, y), (256, y - 120)], steel)             # the spire
    p.line([(256, y - 120), (256, y - 150)], (180, 200, 220, 255), 2)
    p.ellipse(250, y - 158, 262, y - 146, (255, 40, 40, 255))
    p.ellipse(244, y - 164, 268, y - 140, (255, 40, 40, 70))
    column_shaft(p, 2)
    # the bridge's cables in front, low: Five Boroughs is all of them
    for k in range(9):
        p.line([(0, 940 + k * 2), (512, 1000 - k * 6)], (200, 180, 120, 90), 1)


def weekly(p, rnd, font):
    """A warehouse tower at night: iron panels, rivets, a loading door, the neon WEEKLY sign down its right side."""
    x0, x1, top, ground = 80, 362, 140, 1000
    iron = (36, 32, 44, 255)
    p.rect(x0, top, x1, ground, iron)
    for x in range(x0, x1, 14):         # corrugation
        p.rect(x, top, x + 7, ground, shade(iron, 1.18))
    for y in range(top + 40, ground, 70):   # panel seams with rivets
        p.line([(x0, y), (x1, y)], (84, 76, 98, 255), 2)
        for x in range(x0 + 8, x1, 18):
            p.ellipse(x - 2, y - 6, x + 2, y - 2, (110, 100, 124, 255))
    p.rect(x0 - 10, top - 18, x1 + 10, top + 4, (60, 54, 70, 255))
    # high, small windows: a few lit pink and blue from the party inside
    for r in range(10):
        for c, x in enumerate((104, 290)):
            y = top + 60 + r * 76
            on = rnd.random() < 0.45
            p.rect(x, y, x + 34, y + 20, ((255, 70, 180, 255) if c else (90, 140, 255, 255)) if on else (14, 12, 18, 255))
    column_shaft(p, 3)
    # the loading door and a bouncer's light
    p.rect(160, ground - 120, 280, ground, (24, 22, 28, 255))
    for y in range(ground - 116, ground, 8):
        p.line([(164, y), (276, y)], (54, 50, 62, 255), 2)
    p.ellipse(214, ground - 146, 226, ground - 134, (255, 220, 160, 255))
    p.ellipse(204, ground - 156, 236, ground - 124, (255, 220, 160, 50))
    # the neon sign: a dark board, the letters as glowing tubes
    sx0, sx1, sy0 = 384, 452, 210
    p.rect(sx0, sy0, sx1, sy0 + 560, (22, 8, 20, 255))
    p.rect(sx0, sy0, sx1, sy0 + 560, None, (255, 60, 170, 255), 2)
    for k in range(3):
        p.line([(sx0 - 12, sy0 + 60 + k * 220), (sx0, sy0 + 60 + k * 220)], (90, 84, 100, 255), 4)
    letters = "WEEKLY"
    from PIL import ImageFont, ImageDraw, ImageFilter, Image
    glow = Image.new("RGBA", p.img.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    try:
        f = ImageFont.truetype(str(font), 72 * SS) if font else ImageFont.load_default()
    except OSError:
        f = ImageFont.load_default()
    for k, ch in enumerate(letters):
        cx, cy = (sx0 + sx1) / 2 * SS, (sy0 + 52 + k * 90) * SS
        gd.text((cx, cy), ch, font=f, fill=(255, 60, 170, 255), anchor="mm")
    p.img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(9 * SS)))
    p.img.alpha_composite(glow)
    core = Image.new("RGBA", p.img.size, (0, 0, 0, 0))
    cd = ImageDraw.Draw(core)
    for k, ch in enumerate(letters):
        cx, cy = (sx0 + sx1) / 2 * SS, (sy0 + 52 + k * 90) * SS
        try:
            fc = ImageFont.truetype(str(font), 66 * SS) if font else f
        except OSError:
            fc = f
        cd.text((cx, cy), ch, font=fc, fill=(255, 210, 240, 200), anchor="mm")
    p.img.alpha_composite(core)


def render(out, font_file=None):
    """Writes the textures and towers.json to `out`. Raises on a missing Pillow."""
    from PIL import Image
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for kind in (1, 2, 3):
        img = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
        p = Pen(img)
        rnd = random.Random(0xF0F0 + kind)
        if kind == 1:
            uptown(p, rnd)
        elif kind == 2:
            boroughs(p, rnd)
        else:
            weekly(p, rnd, font_file)
        big = img.resize((W, H), Image.LANCZOS)
        big.save(out / ("tower%d-512.png" % kind))
        big.resize((W // 4, H // 4), Image.LANCZOS).save(out / ("tower%d-128.png" % kind))
    (out / "towers.json").write_text(json.dumps({"version": VERSION, "size": [W, H], "names": NAMES,
                                                 "column": {str(k): v for k, v in COLUMN.items()}}), encoding="utf-8")


def current(out):
    try:
        m = json.loads((Path(out) / "towers.json").read_text(encoding="utf-8"))
        return m.get("version") == VERSION and all((Path(out) / ("tower%d-%d.png" % (k, s))).is_file()
                                                 for k in (1, 2, 3) for s in (512, 128))
    except (OSError, ValueError):
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--data", default=default_data())
    ap.add_argument("--out", help="the folder to write (default <data>/online-art/towers)")
    ap.add_argument("--check", action="store_true", help="only say whether the textures are there and current")
    args = ap.parse_args()
    out = Path(args.out) if args.out else Path(args.data) / "online-art" / "towers"
    if args.check:
        ok = current(out)
        print("tower art: %s" % ("current" if ok else "missing or old"))
        return 0 if ok else 1
    font = Path(args.data) / "online-art" / "fonts" / "TradeCon.ttf"
    try:
        render(out, font if font.is_file() else None)
    except (ImportError, OSError) as error:
        print("tower art: not made (%s); the game draws its own" % error)
        return 1
    print("tower art: 3 x 512 x 1024 and 3 x 128 x 256 -> %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
