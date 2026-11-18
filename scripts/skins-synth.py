#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""A synthetic community-skin catalogue for tests (build 94: up to 2048 skins, up to 255 per fighter).

    python scripts/skins-synth.py --out <folder> [--target 55] [--per-target 40] [--real-at 37] [--fillers 40]

Makes a content root (``<out>/content``, for ``dj.py scenario ... --content-root``) with
  * two REAL skins of the target fighter (``--target``, default Doc 55): the model files composed by the launcher's
    own step (``setup/mods.py compose``), so a match can load them;
  * then synthetic catalogue rows added to content.ini and files.ini: ``--fillers`` templates with 2..5 skins each
    first (so the target's skins sit beyond position 64 of the catalogue), then ``--per-target`` skins of the target
    in which the real skin is number ``--real-at``, a template with exactly 15 skins, one with 20, and one with 260
    (255 listed, 5 left out on every PC alike). The synthetic rows share the real model names (a match with them
    shows a real model); their packs are empty folders.
The numbers (skins in all, per template) are printed as JSON. Nothing here is committed or shipped."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "setup"))
sys.path.insert(0, str(ROOT / "tools" / "defjam-mcp"))

spec = importlib.util.spec_from_file_location("mfb", ROOT / "scripts" / "many-fighters-boot.py")
mfb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mfb)
from ffnycontent import newfighter as nf  # noqa: E402


def real_skins(out, game, target, template_model):
    root = out / "content"
    if root.exists():
        link = root / "override"
        if link.exists():
            subprocess.run(["cmd", "/c", "rmdir", str(link)], capture_output=True)
        shutil.rmtree(root)
    (root / "mods").mkdir(parents=True)
    (root / "mod-manager").mkdir()
    if (mfb.DATA / "override").is_dir():
        subprocess.run(["cmd", "/c", "mklink", "/J", str(root / "override"), str(mfb.DATA / "override")], check=True,
                       capture_output=True)
    installed, models = {}, []
    for i in range(2):
        pid = "real-skin-%d" % i
        model = "V2IP_7S%dA" % i
        files = mfb.make_pack(root / "mods" / pid, game, target, template_model, model, "Real Skin %d" % i)
        installed[pid] = dict(id=pid, folder=pid, version="0.1.0", name=pid, synced=True, kind="content",
                              type="new-fighter", order=i + 1, fighter_mode="skin", skin_of=target, files=files,
                              content_sha256=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(),
                              capabilities=["content-v1"])
        models.append(model)
    (root / "mod-manager" / "state.json").write_text(json.dumps({"schema": 1, "installed": installed}))
    r = subprocess.run([sys.executable, str(ROOT / "setup" / "mods.py"), "compose", "--data-dir", str(root),
                        "--game-dir", str(game)], capture_output=True, text=True)
    last = (r.stdout.strip().splitlines() or ["{}"])[-1]
    if r.returncode or "2 pack(s) applied" not in last:
        raise SystemExit("compose failed: %s %s" % (r.stdout[-800:], r.stderr[-800:]))
    return root, models


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=str(ROOT / "logs" / "skins-synth"))
    ap.add_argument("--target", type=int, default=55, help="the fighter whose skins matter (front-end ID)")
    ap.add_argument("--per-target", type=int, default=40)
    ap.add_argument("--real-at", type=int, default=37, help="the target's skin number that has a real model")
    ap.add_argument("--fillers", type=int, default=40)
    a = ap.parse_args()
    game = mfb.DATA / "extracted" if (mfb.DATA / "extracted" / "default.xbe").is_file() else ROOT / "game"
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    rows = nf.characters((game / nf.SOD_PATH).read_bytes())
    template_model = next(r["model"].upper() for r in rows if r["fe_id"] == a.target)
    root, models = real_skins(out, game, a.target, template_model)
    others = [fe for fe in range(1, 98) if fe != a.target][:a.fillers + 3]
    fillers, specials = others[:a.fillers], others[a.fillers:]
    # the catalogue, in order: (pack id, template, model, name)
    cat, n = [], 0

    def add(template, count, real_at=None):
        nonlocal n
        for k in range(1, count + 1):
            n += 1
            real = models[0] if real_at == k else models[1]
            cat.append(("syn-skin-%04d" % n, template, real, "Syn %d.%d" % (template, k)))

    for j, fe in enumerate(fillers):
        add(fe, 2 + j % 4)
    add(a.target, a.per_target, a.real_at)
    for fe, count in zip(specials, (15, 20, 260)):
        add(fe, count)
    # the two real packs stand in for their synthetic rows: files.ini names the real ids at the real places
    for idx, (pid, t, m, name) in enumerate(cat):
        if t == a.target and name == "Syn %d.%d" % (t, a.real_at):
            cat[idx] = ("real-skin-0", t, models[0], "Real Skin 0")
        if t == a.target and name == "Syn %d.%d" % (t, a.real_at + 1):
            cat[idx] = ("real-skin-1", t, models[1], "Real Skin 1")
    # content.ini: a synced pack section for each synthetic one (the real ones are there from the compose)
    ini = root / "mod-manager" / "content.ini"
    text = ini.read_text(encoding="utf-8")
    extra, order = [], 100
    for pid, t, m, name in cat:
        if pid.startswith("real-"):
            continue
        order += 1
        (root / "mods" / pid).mkdir(exist_ok=True)
        extra += ["[pack.%s]" % pid, "name=%s" % name, "authors=", "kind=content", "type=new-fighter", "version=0.1.0",
                  "content_sha256=%s" % hashlib.sha256(pid.encode()).hexdigest(), "order=%d" % order, "bytes=1000",
                  "description=", "fighter_fe_id=", "bundle=", "bundle_name=", "fighter_mode=skin", "skin_of=%d" % t,
                  "reviewed=", "composed=1"]
    ini.write_text(text.rstrip("\n") + "\n" + "\n".join(extra) + "\n", encoding="utf-8")
    # files.ini: applied= every id, the skin= rows in catalogue order
    fin = root / "mod-manager" / "content-files" / "files.ini"
    lines = fin.read_text(encoding="utf-8").splitlines()
    res = []
    for line in lines:
        if line.startswith("applied="):
            line = "applied=" + ",".join(pid for pid, *_ in cat)
        if line.startswith("skin="):
            continue
        res.append(line)
        if line.startswith("skipped="):
            res += ["skin=%s|%d|%s|%s" % (pid, t, m, name) for pid, t, m, name in cat]
    fin.write_text("\n".join(res) + "\n", encoding="utf-8")
    per = {}
    for _, t, _, _ in cat:
        per[t] = per.get(t, 0) + 1
    print(json.dumps({"content_root": str(root), "skins": len(cat), "templates": len(per), "per_template": per,
                      "target": a.target, "target_real_numbers": [a.real_at, a.real_at + 1],
                      "first_global_position_of_target": next(i + 1 for i, c in enumerate(cat) if c[1] == a.target)}))


if __name__ == "__main__":
    main()
