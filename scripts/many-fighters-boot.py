#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Boot regression for many new fighters at once (the build-83 main-menu crash, 2026-11-15).

    python scripts/many-fighters-boot.py [--count 8] [--out logs/many-fighters] [--fight]

Makes ``--count`` new-fighter packs from the player's own dump, the way FFNY Studio writes them (a stock
template's model under a new name, the op), installs them as synced packs in a content root and composes them
with ``setup/mods.py compose``, the launcher's own step. Then, through ``tools/defjam-mcp/dj.py`` (it takes the
machine lock), with ``RECOMP_UNLOCK_ALL=1`` (the settings item "Unlock all fighters and venues", which ran the
fighter set over in build 83):

1. boot to Battle (main menu music, the unlock routine on every screen change), the save kept;
2. boot again on the save the first boot left (a save that ran over in build 83 crashed every later start);
3. with ``--fight``, an offline fight with the last new fighter.

Each step must reach its screen without a crash; the game's ``[SETS]`` lines are printed. The packs and the
composed files are the player's own data re-arranged and stay under ``--out`` (never commit them).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "setup"))
sys.path.insert(0, str(ROOT / "tools" / "defjam-mcp"))

from ffnycontent import delta, newfighter as nf, viv  # noqa: E402
from djlib import roots  # noqa: E402

DATA = Path(os.environ.get("DEFJAM_DATA") or r"D:\DefJam\data")


def templates(game, count):
    """Stock fighters with a cell of their own and distinct models: [(fe_id, model)]."""
    rows = nf.characters((game / nf.SOD_PATH).read_bytes())
    arc = viv.Archive(str(game / nf.MODEL_ARCHIVE))
    out, seen = [], set()
    for r in rows:
        m = r["model"].upper()
        if (r["main"] == 1 and r["fe_id"] < nf.FIRST_NEW_FE_ID and r["fe_id"] not in nf.OUTFIT_FE_IDS
                and nf.TEMPLATE_MODEL.match(m) and m not in seen and arc.has(m + ".o")):
            seen.add(m)
            out.append((r["fe_id"], m))
    return [out[i % len(out)] for i in range(count)]


def make_pack(folder, game, template_fe, template_model, model, name):
    (folder / "deltas").mkdir(parents=True)
    arc = viv.Archive(str(game / nf.MODEL_ARCHIVE))
    originals = {s: arc.read(template_model + s) for s in nf.MODEL_SUFFIXES if arc.has(template_model + s)}
    entries = []
    for entry, base_entry, base, result in nf.model_entries(template_model, model, originals):
        rel = "deltas/%s.ffdelta" % entry
        (folder / rel).write_bytes(delta.encode(base, result))
        entries.append({"archive": nf.MODEL_ARCHIVE, "entry": entry, "mode": "add", "base_archive": nf.MODEL_ARCHIVE,
                        "base_entry": base_entry, "delta": rel, "original_sha256": hashlib.sha256(base).hexdigest(),
                        "result_sha256": hashlib.sha256(result).hexdigest(), "result_size": len(result)})
    info = {"schema": 1, "type": "new-fighter", "entries": entries,
            "ops": [nf.make_op(template_fe, template_model, model, name)]}
    (folder / "install.json").write_text(json.dumps(info), encoding="utf-8")
    return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(folder.rglob("*")) if p.is_file()}


def content_root(out, game, count):
    """<out>/content: mods/<id>, mod-manager/state.json (synced receipts with their places), composed."""
    root = out / "content"
    if root.exists():
        link = root / "override"
        if link.exists():
            subprocess.run(["cmd", "/c", "rmdir", str(link)], capture_output=True)
        shutil.rmtree(root)
    (root / "mods").mkdir(parents=True)
    (root / "mod-manager").mkdir()
    if (DATA / "override").is_dir():     # the bases the game copies are given (dj roots link the same folder)
        subprocess.run(["cmd", "/c", "mklink", "/J", str(root / "override"), str(DATA / "override")], check=True,
                       capture_output=True)
    installed, places = {}, []
    for i, (fe, model_of) in enumerate(templates(game, count)):
        pid = "many-fighter-%d" % i
        model = "V2IP_7%s%sA" % ("ABCDEFGHJKLMNPQRSTUVWXYZ"[i % 24], "0123456789"[i // 24])
        files = make_pack(root / "mods" / pid, game, fe, model_of, model, "Copy %d" % i)
        place = nf.FIRST_NEW_FE_ID + i
        places.append(place)
        installed[pid] = dict(id=pid, folder=pid, version="0.1.0", name=pid, synced=True, kind="content",
                              type="new-fighter", order=i + 1, fighter_fe_id=place, files=files,
                              content_sha256=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(),
                              capabilities=["content-v1"])
    (root / "mod-manager" / "state.json").write_text(json.dumps({"schema": 1, "installed": installed}))
    r = subprocess.run([sys.executable, str(ROOT / "setup" / "mods.py"), "compose", "--data-dir", str(root),
                        "--game-dir", str(game)], capture_output=True, text=True)
    last = (r.stdout.strip().splitlines() or ["{}"])[-1]
    print("compose:", last)
    if r.returncode or "%d pack(s) applied" % count not in last:
        raise SystemExit("compose failed: %s %s" % (r.stdout[-800:], r.stderr[-800:]))
    return root, places


def dj(args, label):
    cmd = [sys.executable, str(ROOT / "tools" / "defjam-mcp" / "dj.py"), "scenario"] + args
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    text = r.stdout[r.stdout.find("{"):] if "{" in r.stdout else "{}"
    try:
        result = json.loads(text)
    except ValueError:
        result = {"ok": False, "raw": r.stdout[-1500:] + r.stderr[-1500:]}
    log = ROOT / result["log"] if result.get("log") else None
    lines = []
    if log and log.is_file():
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith(("[SETS]", "[CRASH]", "[HANG]", "[UNLOCK] sub_")) or "  in sub_" in line:
                lines.append(line)
    crashed = any(x.startswith(("[CRASH]", "[HANG]")) for x in lines)
    ok = bool(result.get("ok")) and not crashed and not result.get("faults")
    print(json.dumps({"step": label, "ok": ok, "log": result.get("log"), "faults": result.get("faults"),
                      "game": lines[:12]}))
    return ok, result


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--count", type=int, default=8, help="new fighters (6 or more ran the build-83 set over)")
    ap.add_argument("--out", default=str(ROOT / "logs" / "many-fighters"))
    ap.add_argument("--fight", action="store_true", help="also an offline fight with the last new fighter")
    a = ap.parse_args()
    game = DATA / "extracted" if (DATA / "extracted" / "default.xbe").is_file() else ROOT / "game"
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    content, places = content_root(out, game, a.count)
    unlock = ["--env", "RECOMP_UNLOCK_ALL=1", "--content-root", str(content)]
    ok1, _ = dj(["boot", "--goto", "battle", "--keep-roots"] + unlock, "boot")
    saved = out / "save-after-boot"
    if saved.exists():
        shutil.rmtree(saved)
    src = Path(roots.root_path("boot0")) / "save" / "UserData"
    if src.is_dir():
        shutil.copytree(src, saved / "UserData")
    for path, _ in roots.listing():
        roots.destroy(path)
    ok2, _ = dj(["boot", "--goto", "battle", "--fixture", str(saved)] + unlock, "boot-again")
    ok3 = True
    if a.fight:
        ok3, _ = dj(["offline", "--f0", str(places[-1]), "--f1", "22", "--fixture", str(saved)] + unlock, "fight")
    ok = ok1 and ok2 and ok3
    print(json.dumps({"many_fighters": a.count, "ok": ok}))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
