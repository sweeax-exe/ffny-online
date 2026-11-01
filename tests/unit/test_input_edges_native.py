"""Exercise the actual transition FIFO: short taps, chords, commit/ack and bounds."""
from pathlib import Path
import shutil
import subprocess
import pytest

TOOLKIT = Path(__file__).resolve().parents[2] / 'tools/xboxrecomp'

@pytest.fixture(scope='module')
def edge_tool(tmp_path_factory):
    cl=shutil.which('cl')
    if not cl: pytest.skip('native MSVC fixture')
    folder=tmp_path_factory.mktemp('input-edges');exe=folder/'edges.exe'
    result=subprocess.run([cl,'/nologo','/O2','/TC','/I'+str(TOOLKIT/'src/input'),
        str(TOOLKIT/'src/input/input_edges.c'),str(TOOLKIT/'tests/input_edges/test_main.c'),
        '/Fe:'+str(exe)],cwd=folder,capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    return exe

@pytest.mark.parametrize('case',['rapid','chord','commit','age','overflow','reset','analog','threshold','wrap','ring'])
def test_consumption_retains_ordered_edges(edge_tool,case):
    result=subprocess.run([str(edge_tool),case],capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
