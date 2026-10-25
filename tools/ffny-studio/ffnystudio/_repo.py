"""Locate the repository's own helpers (scripts/hd_assets.py) and the user's dump."""

import importlib.util
import os
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_cache = {}


def repo_root():
    """The checkout this package lives in (tools/ffny-studio/ffnystudio -> repo), or None."""
    root = _HERE.parents[2]
    return root if (root / "scripts" / "hd_assets.py").is_file() else None


def hd_assets():
    """scripts/hd_assets.py as a module: the bounded RefPack decoder and the SHPX decoder.

    $FFNY_HD_ASSETS wins; then the copy tools/ffny-studio/build_addon.py bundles next to this package
    (``_hd_assets.py``, in the Blender add-on); then the checkout's scripts/hd_assets.py."""
    if "hd" not in _cache:
        override = os.environ.get("FFNY_HD_ASSETS")
        bundled = _HERE / "_hd_assets.py"
        if override:
            path = Path(override)
        elif bundled.is_file():
            path = bundled
        else:
            path = (repo_root() or Path(".")) / "scripts" / "hd_assets.py"
        if not path.is_file():
            raise RuntimeError("scripts/hd_assets.py not found (set FFNY_HD_ASSETS)")
        spec = importlib.util.spec_from_file_location("ffny_hd_assets", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        _cache["hd"] = module
    return _cache["hd"]


def game_dir():
    """The user's own dump: $FFNY_GAME, then <repo>/game, then $DEFJAM_DATA/extracted."""
    candidates = []
    if os.environ.get("FFNY_GAME"):
        candidates.append(Path(os.environ["FFNY_GAME"]))
    if repo_root():
        candidates.append(repo_root() / "game")
        # A worktree under .claude/worktrees/<name> shares the main checkout's junction.
        for parent in repo_root().parents:
            if (parent / "game" / "default.xbe").is_file():
                candidates.append(parent / "game")
                break
    if os.environ.get("DEFJAM_DATA"):
        candidates.append(Path(os.environ["DEFJAM_DATA"]) / "extracted")
    for candidate in candidates:
        if (candidate / "assets").is_dir():
            return candidate
    raise RuntimeError("the game dump was not found; set FFNY_GAME to the extracted folder")
