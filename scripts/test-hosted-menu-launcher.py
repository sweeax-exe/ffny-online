#!/usr/bin/env python3
"""Exercise the real signed catalogue and R2 menu install through native launcher UI.

Run serially after building posix-release. Uses a new ignored data root, optional
read-only local Faithful pack links, the repository's actual public trust pin and
SaveGuard. Never preloads an archive or writes a synthetic catalogue/signature.
Screenshots/logs/receipts remain local. Network and source pack paths are read-only.

    python scripts/test-hosted-menu-launcher.py --faithful-root /path/to/mods

The public catalogue must be approved/deployed first. This is a manual acceptance
utility, not a CI test and not a replacement for Windows/physical-controller QA.
"""
import argparse
import configparser
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import harness


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--faithful-root', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--download-seconds', type=int, default=120)
    args = parser.parse_args()
    out = (args.out or ROOT/'logs'/('hosted-menu-launcher-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))).resolve()
    if ROOT/'logs' not in out.parents:
        parser.error('--out must be a new directory below this checkout logs/')
    out.mkdir(parents=True, exist_ok=False)
    data = out/'data'; (data/'mods').mkdir(parents=True)
    baseline = []
    if args.faithful_root:
        for pack in sorted(args.faithful_root.resolve().glob('faithful-hd-*')):
            if pack.is_dir() and (pack/'manifest.ini').is_file():
                (data/'mods'/pack.name).symlink_to(pack, target_is_directory=True)
                baseline.append(pack.name)
    settings = data/'settings.ini'
    settings.write_text('[textures]\nenabled=true\npacks='+ ';'.join(baseline) +'\n[launcher]\nskip=false\n[display]\nwindow_width=1100\nwindow_height=800\n[debug]\nskip_intro=true\n')
    exe = ROOT/'build/posix-release/defjam_recomp'
    receipts = []

    def tour(name, actions, seconds, offline=False):
        shot = out/(name+'.bmp')
        env = {k:v for k,v in os.environ.items() if not k.startswith('RECOMP_')}
        env.pop('DEFJAM_INSTALL_DIR', None)
        env.update(DEFJAM_DATA=str(data), DEFJAM_MOD_PYTHON=sys.executable,
                   RECOMP_SETTINGS=str(settings), RECOMP_INPUT_IGNORE_FOCUS='1',
                   RECOMP_LAUNCHER_SCRIPT='900:ui:Mods;'+actions+f';{seconds*1000-1000}:shot:{shot};{seconds*1000}:quit')
        if offline:
            # Refuse outbound helper requests during the cached reopen check.
            env.update(HTTPS_PROXY='http://127.0.0.1:9', HTTP_PROXY='http://127.0.0.1:9', NO_PROXY='')
        log = out/(name+'.log')
        with harness.SaveGuard(str(data)) as guard, log.open('w') as output:
            process = subprocess.Popen([str(exe)],cwd=ROOT,env=env,stdout=output,stderr=output)
            harness._OWNED_PROCESSES.add(process)
            try:
                code = process.wait(timeout=seconds+30)
            finally:
                if process.poll() is None:
                    process.kill(); process.wait()
        text = log.read_text(errors='replace')
        assert code == 0, (name,code)
        assert '[UI] script target missing:' not in text, (name,'missing target')
        assert guard.restored_equal, (name,'save guard')
        assert shot.exists(), (name,'missing screenshot')
        receipts.append(dict(tour=name,exit=code,save_equal=guard.restored_equal,log=str(log),screenshot=str(shot)))
        print(json.dumps(receipts[-1]),flush=True)

    tour('refresh', '2200:ui:mods.catalogue-refresh', 20)
    saved = json.loads((data/'mod-manager/catalogue.json').read_text())
    assert saved['revision'] > 0, 'No verified signed catalogue retained'
    tour('install', '2200:ui:mods.testing-versions;4200:ui:mod.install.menu-ui-hd-wide', args.download_seconds)
    state = json.loads((data/'mod-manager/state.json').read_text())
    receipt = state['installed']['menu-ui-hd-wide']
    assert receipt['version']=='1.0.0-rc2'
    assert (data/'mods/menu-ui-hd-wide/manifest.ini').is_file()
    # New installs start disabled; enabling appends after current local packs.
    tour('enable-order', '2200:ui:pack.menu-ui-hd-wide;3400:ui:mods.order;6500:ui:mods.order-done', 9)
    parsed = configparser.ConfigParser(); parsed.read(settings)
    enabled = parsed.get('textures','packs').split(';')
    assert enabled == baseline+['menu-ui-hd-wide'], enabled
    tour('offline-reopen', '2200:ui:mods.order;6500:ui:mods.order-done', 9, offline=True)
    parsed.read(settings)
    assert parsed.get('textures','packs').split(';')==enabled
    result = dict(catalogue_revision=saved['revision'],installed_version=receipt['version'],
                  data=str(data),load_order=enabled,receipts=receipts)
    (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result),flush=True)


if __name__=='__main__':
    main()
