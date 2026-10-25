#!/usr/bin/env python3
"""Build the installable FFNY Studio Blender add-on zip.

    python tools/ffny-studio/build_addon.py [--out PATH]

The zip holds one folder, ``ffny_studio/``: the add-on (tools/ffny-studio/blender/ffny_studio, with its
``blender_manifest.toml`` for Blender 4.2+ extensions and ``bl_info`` for the legacy add-on installer), the
``ffnystudio`` library copied in as ``ffny_studio/ffnystudio/``, and scripts/hd_assets.py (the RefPack and SHPX
reference decoders) as ``ffny_studio/ffnystudio/_hd_assets.py``. Only ``.py`` and ``.toml`` source files go in:
the add-on carries no game data. The default output is build/ffny-studio/ffny_studio-<version>.zip (ignored by
git). Entries have a fixed timestamp, so the same sources give the same zip.
"""

import argparse
import re
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
ADDON = HERE / "blender" / "ffny_studio"
LIBRARY = HERE / "ffnystudio"
HD_ASSETS = REPO / "scripts" / "hd_assets.py"
ALLOWED = {".py", ".toml"}
STAMP = (2026, 1, 1, 0, 0, 0)


def manifest_version(text):
    match = re.search(r'^version\s*=\s*"([^"]+)"', text, re.MULTILINE)
    if not match:
        raise SystemExit("blender_manifest.toml has no version")
    return match.group(1)


def bl_info_version(text):
    match = re.search(r'"version":\s*\((\d+),\s*(\d+),\s*(\d+)\)', text)
    if not match:
        raise SystemExit("__init__.py has no bl_info version")
    return ".".join(match.groups())


def sources(folder):
    for path in sorted(folder.rglob("*")):
        if "__pycache__" in path.parts or not path.is_file():
            continue
        if path.suffix not in ALLOWED:
            raise SystemExit("refusing to bundle %s: only .py and .toml sources go in the add-on" % path)
        yield path


def entries():
    """[(archive name, source path)] of the add-on zip."""
    out = [("ffny_studio/" + p.relative_to(ADDON).as_posix(), p) for p in sources(ADDON)]
    out += [("ffny_studio/ffnystudio/" + p.relative_to(LIBRARY).as_posix(), p) for p in sources(LIBRARY)
            if p.name != "__main__.py"]
    out.append(("ffny_studio/ffnystudio/_hd_assets.py", HD_ASSETS))
    return out


def build(out=None):
    version = manifest_version((ADDON / "blender_manifest.toml").read_text(encoding="utf-8"))
    if bl_info_version((ADDON / "__init__.py").read_text(encoding="utf-8")) != version:
        raise SystemExit("bl_info version and blender_manifest.toml version differ")
    out = Path(out) if out else REPO / "build" / "ffny-studio" / ("ffny_studio-%s.zip" % version)
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, path in entries():
            info = zipfile.ZipInfo(name, STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, path.read_bytes())
    return out, version


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", help="zip to write (default build/ffny-studio/ffny_studio-<version>.zip)")
    args = parser.parse_args(argv)
    out, version = build(args.out)
    print("FFNY Studio %s add-on: %s (%d files)" % (version, out, len(entries())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
