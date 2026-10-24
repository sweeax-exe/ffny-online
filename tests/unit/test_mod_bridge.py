"""Native process/argv/OS lock checks without renderer or game data."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import time
import pytest

ROOT=Path(__file__).resolve().parents[2]


def test_helper_status_with_isolated_python(tmp_path):
    # -I reproduces embedded Windows Python omitting the script directory.
    data=tmp_path/'isolated data'
    result=subprocess.run([sys.executable,'-I',str(ROOT/'setup/mods.py'),'status',
                           '--data-dir',str(data),'--runtime','0.10.8'],
                          cwd=tmp_path,capture_output=True,text=True,timeout=20)
    assert result.returncode==0,result.stdout+result.stderr
    assert (data/'mod-manager/view.ini').is_file()


DRIVER=r'''
#include "pc_mods.h"
#include "pc_ui_actions.h"
#include <iostream>
#include <thread>
#include <chrono>
std::string data;
namespace pcui { std::string folder_path(Folder which) { return data; } }
int main(int argc,char **argv) {
 data=argv[1];
 if(std::string(argv[2])=="hold") {
   if(!pc_mods_game_lock())return 2;
   std::cout<<"locked"<<std::endl;
   std::string input;std::getline(std::cin,input);return 0;
 }
 if(!pcui::mod_start("status"))return 3;
 for(int i=0;i<1000 && pcui::mod_busy();i++) {pcui::mod_poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 std::cout<<pcui::mod_status()<<"\n"<<pcui::mod_view().catalogue<<std::endl;
 return pcui::mod_busy()?4:0;
}
'''

@pytest.fixture(scope='module')
def bridge(tmp_path_factory):
    folder=tmp_path_factory.mktemp('mod bridge with spaces')
    source=folder/'main.cpp';source.write_text(DRIVER)
    compiler=shutil.which('cl') or shutil.which('clang++') or shutil.which('g++')
    if not compiler:pytest.skip('C++ compiler required')
    exe=folder/('bridge.exe' if os.name=='nt' else 'bridge')
    if Path(compiler).stem.lower()=='cl':
        command=[compiler,'/nologo','/std:c++17','/EHsc','/I'+str(ROOT/'src/hooks'),
                 '/DDEFJAM_MOD_SOURCE_DIR="'+str(ROOT)+'"',str(source),str(ROOT/'src/hooks/pc_mods.cpp'),'/Fe:'+str(exe)]
    else:
        command=[compiler,'-std=c++17','-pthread','-I'+str(ROOT/'src/hooks'),
                 '-DDEFJAM_MOD_SOURCE_DIR="'+str(ROOT)+'"',str(source),str(ROOT/'src/hooks/pc_mods.cpp'),'-o',str(exe)]
    subprocess.run(command,cwd=folder,check=True,capture_output=True)
    return exe


def test_async_status_and_space_paths(bridge,tmp_path):
    data=tmp_path/'data with spaces'
    env={**os.environ,'DEFJAM_MOD_PYTHON':sys.executable}
    # Developer override is deliberately unavailable for broken installed Python.
    env.pop('DEFJAM_INSTALL_DIR',None)
    result=subprocess.run([str(bridge),str(data),'status'],env=env,capture_output=True,text=True,timeout=20)
    assert result.returncode==0,result.stdout+result.stderr
    assert 'inventory refreshed' in result.stdout
    assert (data/'mod-manager/view.ini').exists()


def test_polling_does_not_block_atomic_progress_updates(bridge,tmp_path):
    # The real download replaces progress.ini once per MiB while the launcher
    # reads it. Brief Windows sharing/scanner locks must not abort the download.
    source=tmp_path/'helper source';(source/'setup').mkdir(parents=True)
    (source/'setup/mods.py').write_text(f'''
import os, sys, tempfile
from pathlib import Path
sys.path.insert(0,{str(ROOT/'setup')!r})
import mods
root=Path(sys.argv[sys.argv.index('--data-dir')+1])/'mod-manager'
sys.stderr=open(root/'helper-error.log','w')
for i in range(500):
    mods.atomic_bytes(root/'progress.ini',('[progress]\\nmessage=Downloading package\\ndone='+str(i)+'\\ntotal=5000\\n').encode())
''')
    env={**os.environ,'DEFJAM_MOD_PYTHON':sys.executable}
    env.pop('DEFJAM_INSTALL_DIR',None)
    result=subprocess.run([str(bridge),str(tmp_path/'data'),'status'],cwd=source,
                          env=env,capture_output=True,text=True,timeout=20)
    assert result.returncode==0,result.stdout+result.stderr
    error=(tmp_path/'data/mod-manager/helper-error.log').read_text()
    assert 'inventory refreshed' in result.stdout,result.stdout+result.stderr+error


@pytest.mark.skipif(os.name!='nt',reason='Windows readers deny replacing an open file')
def test_progress_replacement_waits_for_a_windows_reader(tmp_path):
    import threading
    sys.path.insert(0,str(ROOT/'setup'))
    try:
        import mods
    finally:
        sys.path.pop(0)
    target=tmp_path/'progress.ini'
    target.write_bytes(b'previous snapshot')
    reader=target.open('rb')
    release=threading.Timer(0.1,reader.close)
    release.start()
    try:
        mods.atomic_bytes(target,b'new snapshot')
    finally:
        release.join()
        reader.close()
    assert target.read_bytes()==b'new snapshot'


def test_runtime_lock_interoperates_with_python_helper(bridge,tmp_path):
    sys.path.insert(0,str(ROOT/'setup'))
    try:
        import mods
    finally:sys.path.pop(0)
    data=tmp_path/'data'
    process=subprocess.Popen([str(bridge),str(data),'hold'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    try:
        assert process.stdout.readline().strip()=='locked'
        manager=mods.Manager(data,keys=[])
        with pytest.raises(ValueError,match='game is running'):
            with mods.file_lock(manager.root/'runtime.lock'):pass
    finally:
        process.communicate('done\n',timeout=10)
    assert (manager.root/'runtime.lock').stat().st_size == 0
    with mods.file_lock(manager.root/'runtime.lock'):pass
    assert (manager.root/'runtime.lock').stat().st_size == 0


def test_incomplete_transaction_blocks_game_even_without_launcher(bridge,tmp_path):
    root=tmp_path/'mod-manager';root.mkdir();(root/'journal.json').write_text('{}')
    result=subprocess.run([str(bridge),str(tmp_path),'hold'],capture_output=True,text=True,timeout=10)
    assert result.returncode==2
