#!/usr/bin/env python3
"""Serial native launcher checks using synthetic signed packs and disposable data.

Run from any cwd with the development venv after a certified posix-release build:
    python scripts/test-mod-launcher.py
Requires pytest, Pillow and cryptography; reuses the unit-test pack builders.
Creates a unique ignored logs directory. Do not run alongside another game.
Does not cover physical gamepad interaction or hosted download providers.

The ephemeral public key applies only to a copied helper in this fixture's cwd.
The real signing key, ordinary install, review artwork and saves are untouched.
"""
from pathlib import Path
import argparse,datetime,json,os,shutil,subprocess,sys,configparser
repo=Path(__file__).resolve().parents[1];sys.path[:0]=[str(repo/'tests/unit'),str(repo/'setup'),str(repo/'scripts')]
import harness as h
import test_mod_manager as t
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from PIL import Image
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',type=Path,help='new output directory (must not exist)')
args=parser.parse_args()
root=(args.out or repo/'logs'/('mod-manager-tour-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))).resolve()
root.mkdir(parents=True,exist_ok=False)
fixture=root/'fixture-source';fixture.mkdir()
(fixture/'setup').mkdir(exist_ok=True);(fixture/'config/mods').mkdir(parents=True,exist_ok=True)
for name in ('mods.py','mod_catalogue.py'):shutil.copy2(repo/'setup'/name,fixture/'setup'/name)
if not (fixture/'game').exists():(fixture/'game').symlink_to(repo/'game',target_is_directory=True)
key=Ed25519PrivateKey.generate();public=key.public_key().public_bytes_raw().hex()
(fixture/'config/mods/catalogue-keys.json').write_text(json.dumps({'schema':1,'keys':[public]}))
data=root/'ui-data';data.mkdir(exist_ok=True)
manager=t.mods.Manager(data,'0.10.6',keys=[public])
first,row=t.package(root,name='menu-ui-test',version='1.0.0')
second,newer=t.package(root,name='menu-ui-test',version='1.1.0',color='blue')
testing,preview=t.package(root,name='menu-ui-test',version='2.0.0-rc1',color='yellow')
preview['versions'][0]['channel']='prerelease'
row['versions']+=newer['versions']+preview['versions'];t.signed(manager,key,[row]);manager.install('menu-ui-test',requested='1.0.0',archive_path=first)
shutil.copy2(second,manager.cache/(newer['versions'][0]['archive']['sha256']+'.zip'))
shutil.copy2(testing,manager.cache/(preview['versions'][0]['archive']['sha256']+'.zip'))
base=manager.mods/'faithful-test';(base/'textures').mkdir(parents=True,exist_ok=True)
(base/'manifest.ini').write_text('[pack]\nschema=1\nname=Local test\n[textures]\n'+t.ID+'=textures/paper.png\n')
Image.new('RGBA',(8,8),'green').save(base/'textures/paper.png');manager.view()
exe=repo/'build/posix-release/defjam_recomp';receipts=[]
for case,scale,width,height in [('normal',100,1100,750),('large',250,820,560),('recovery',100,1100,750),('testing',100,1100,750)]:
 settings=data/'settings.ini'
 settings.write_text(f'[textures]\nenabled=1\npacks=faithful-test;menu-ui-test\n[launcher]\nskip={int(case=="recovery")}\n[ui]\nscale={scale}\n[display]\nwindow_width={width}\nwindow_height={height}\n[debug]\nskip_intro=1\n')
 if case=='recovery':
  old=manager.state()['installed']['menu-ui-test'];t.mods.atomic_json(manager.root/'journal.json',dict(id='menu-ui-test',old=old,new=None))
 before=root/(case+'-mods.bmp');order=root/(case+'-order.bmp');after=root/(case+'-updated.bmp')
 script=f'100:resize:{width}:{height};900:ui:Mods;2200:shot:{before};2600:ui:mods.order;3400:shot:{order};'
 if case=='normal':script+=f'3800:ui:pack.earlier.menu-ui-test;4400:ui:mods.order-done;4900:ui:mod.install.menu-ui-test;6800:shot:{after};7500:quit'
 elif case=='testing':script+=f'3800:ui:mods.order-done;4300:ui:mods.testing-versions;5400:ui:mod.install.menu-ui-test;7400:shot:{after};8200:quit'
 else:script+='4000:ui:mods.order-done;4700:quit'
 env={k:v for k,v in os.environ.items() if not k.startswith('RECOMP_')}
 env.update(DEFJAM_DATA=str(data),DEFJAM_MOD_PYTHON=sys.executable,RECOMP_SETTINGS=str(settings),
   RECOMP_LAUNCHER_SCRIPT=script,RECOMP_INPUT_IGNORE_FOCUS='1')
 env.pop('DEFJAM_INSTALL_DIR',None)
 output=root/(case+'.log')
 with h.SaveGuard(str(data)) as guard,output.open('w') as log:
  process=subprocess.Popen([str(exe)],cwd=fixture,env=env,stdout=log,stderr=log)
  h._OWNED_PROCESSES.add(process)
  try:code=process.wait(timeout=35)
  finally:
   if process.poll() is None:process.kill();process.wait()
 text=output.read_text(errors='replace')
 assert code==0,(case,code)
 assert '[UI] script target missing:' not in text,(case,'missing target')
 assert guard.restored_equal,(case,'save guard')
 assert before.exists() and order.exists(),case
 if case=='normal':
  assert manager.state()['installed']['menu-ui-test']['version']=='1.1.0','native update failed'
  assert 'packs=menu-ui-test;faithful-test' in settings.read_text().replace(' ',''),'load order failed'
 if case=='testing':
  assert manager.state()['installed']['menu-ui-test']['version']=='2.0.0-rc1','testing channel install failed'
 if case=='recovery':
  assert not (manager.root/'journal.json').exists(),'recovery failed'
  parsed=configparser.ConfigParser();parsed.read(settings);assert parsed.getboolean('launcher','skip'),'skip preference changed'
 for image in (before,order,after):
  if image.exists():
   with Image.open(image) as picture:picture.save(image.with_suffix('.png'))
 receipts.append(dict(case=case,exit=code,save_ok=guard.restored_equal,log=str(output),shots=[str(before.with_suffix('.png')),str(order.with_suffix('.png'))]))
 print(json.dumps(receipts[-1]),flush=True)
(root/'launcher-receipts.json').write_text(json.dumps(receipts,indent=2)+'\n')
