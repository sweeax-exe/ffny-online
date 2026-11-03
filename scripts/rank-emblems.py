#!/usr/bin/env python3
"""Renders the ranked emblems (scripts/rank_emblem_art.py, the port's own SVG art) to PNG textures.

    python scripts/rank-emblems.py [--data <DEFJAM_DATA>] [--out DIR] [--check]

Writes <data>/online-art/ranks/em<N>-256.png and em<N>-64.png (N = 1..8, Street Rookie .. Def Jam
Legend) and ranks.json. The repository holds no image: the SVG is drawn here, on the player's computer,
by a Chromium browser in headless mode (Microsoft Edge, which every Windows 10/11 has, else Chrome or
Chromium), offline, from a page written to a temporary folder. Pillow cuts the one screenshot into the
sixteen textures. scripts/online-art.py calls this at setup; without a browser the game draws a simple
emblem of its own (ranked_ui.inc), so a failure is reported, not fatal.

The division chevrons and the Legend plate are not in the textures: the game draws them on top.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rank_emblem_art as art  # noqa: E402

VERSION = 1
BIG, SMALL = 256, 64
PAD = 16            # room around each emblem in the sheet (the art may glow past its box)


def default_data():
    return os.environ.get("DEFJAM_DATA") or str(Path(__file__).resolve().parents[2] / "data")


def find_browser():
    """A Chromium-based browser that can take a headless screenshot, or None."""
    env = os.environ.get("RECOMP_EMBLEM_BROWSER")
    if env and Path(env).is_file():
        return env
    candidates = []
    for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles"), os.environ.get("LOCALAPPDATA")):
        if base:
            candidates += [Path(base) / "Microsoft/Edge/Application/msedge.exe",
                           Path(base) / "Google/Chrome/Application/chrome.exe",
                           Path(base) / "Chromium/Application/chrome.exe"]
    candidates += [Path("/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"),
                   Path("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"),
                   Path("/Applications/Chromium.app/Contents/MacOS/Chromium")]
    for c in candidates:
        if c.is_file():
            return str(c)
    for name in ("microsoft-edge", "google-chrome", "google-chrome-stable", "chromium", "chromium-browser"):
        found = shutil.which(name)
        if found:
            return found
    return None


def sheet_html(font_file=None):
    """One page: the eight 256 px emblems in a row, the eight 64 px ones below, on a clear background."""
    cell = BIG + 2 * PAD
    face = ""
    if font_file:
        face = "@font-face{font-family:'FFNY Cond';src:url('%s');}" % Path(font_file).resolve().as_uri()
    css = (face + "html,body{margin:0;padding:0;background:transparent;overflow:hidden}"
           "text{font-family:Anton,'FFNY Cond',Impact,'Arial Narrow',sans-serif}"
           "svg.e{position:absolute}")
    parts = []
    for i in range(1, 9):
        x = (i - 1) * cell + PAD
        parts.append(art.big(i, BIG, plate=False).replace("<svg ", '<svg class="e" ', 1)
                     .replace('style="overflow:visible;', 'style="overflow:visible;left:%dpx;top:%dpx;' % (x, PAD), 1))
        sx = (i - 1) * (SMALL + 2 * PAD) + PAD
        parts.append(art.small(i, SMALL).replace("<svg ", '<svg class="e" ', 1)
                     .replace('style="overflow:visible;', 'style="overflow:visible;left:%dpx;top:%dpx;' % (sx, cell + PAD), 1))
    # the animations hold still: the first frame of each is what the texture keeps
    return ("<!doctype html><html><head><meta charset='utf-8'><style>%s</style></head><body>%s%s</body></html>"
            % (css, art.defs_svg(), "".join(parts)))


def sheet_size():
    cell = BIG + 2 * PAD
    return 8 * cell, cell + SMALL + 2 * PAD


def render(out, font_file=None, browser=None):
    """Writes the textures to `out`; returns the browser used. Raises RuntimeError when it cannot."""
    from PIL import Image
    browser = browser or find_browser()
    if not browser:
        raise RuntimeError("no Edge, Chrome or Chromium for the emblems")
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    w, h = sheet_size()
    with tempfile.TemporaryDirectory(prefix="ffny-emblems-") as tmp:
        page = Path(tmp) / "sheet.html"
        shot = Path(tmp) / "sheet.png"
        page.write_text(sheet_html(font_file), encoding="utf-8")
        cmd = [browser, "--headless", "--disable-gpu", "--hide-scrollbars", "--no-first-run",
               "--no-default-browser-check", "--disable-extensions", "--user-data-dir=" + str(Path(tmp) / "profile"),
               "--force-device-scale-factor=1", "--default-background-color=00000000",
               "--window-size=%d,%d" % (w, h), "--virtual-time-budget=1500", "--screenshot=" + str(shot), page.as_uri()]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120, check=False)
        if not shot.is_file():
            raise RuntimeError("the browser made no screenshot")
        sheet = Image.open(shot).convert("RGBA")
        if sheet.getpixel((1, 1))[3] != 0:
            raise RuntimeError("the browser drew no clear background")
        cell = BIG + 2 * PAD
        for i in range(1, 9):
            x = (i - 1) * cell + PAD
            sheet.crop((x, PAD, x + BIG, PAD + BIG)).save(out / ("em%d-256.png" % i))
            sx = (i - 1) * (SMALL + 2 * PAD) + PAD
            sheet.crop((sx, cell + PAD, sx + SMALL, cell + PAD + SMALL)).save(out / ("em%d-64.png" % i))
    (out / "ranks.json").write_text(json.dumps({"version": VERSION, "tiers": 8, "sizes": [BIG, SMALL]}), encoding="utf-8")
    return browser


def current(out):
    try:
        m = json.loads((Path(out) / "ranks.json").read_text(encoding="utf-8"))
        return m.get("version") == VERSION and all((Path(out) / ("em%d-%d.png" % (i, s))).is_file()
                                                 for i in range(1, 9) for s in (BIG, SMALL))
    except (OSError, ValueError):
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--data", default=default_data())
    ap.add_argument("--out", help="the folder to write (default <data>/online-art/ranks)")
    ap.add_argument("--check", action="store_true", help="only say whether the textures are there and current")
    args = ap.parse_args()
    out = Path(args.out) if args.out else Path(args.data) / "online-art" / "ranks"
    if args.check:
        ok = current(out)
        print("rank emblems: %s" % ("current" if ok else "missing or old"))
        return 0 if ok else 1
    font = Path(args.data) / "online-art" / "fonts" / "TradeCon.ttf"
    try:
        used = render(out, font if font.is_file() else None)
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print("rank emblems: not made (%s); the game draws its own" % error)
        return 1
    print("rank emblems: 8 x 256 px and 8 x 64 px -> %s (%s)" % (out, Path(used).name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
