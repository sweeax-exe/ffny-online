"""The trace is bounded, copies payloads, counts loss, and drains on clean exit."""
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "input_latency.h"
#include "input_trace.h"
static XboxInputTraceSink sink;
void xbox_InputTraceSetSink(XboxInputTraceSink f) { sink = f; }
int xbox_InputTraceActive(void) { return sink != 0; }
void xbox_InputTraceEmit(const char *s,int p,const void *d,unsigned n,uint64_t a,uint64_t b)
{ if (sink) sink(s,p,d,n,a,b); }
static DWORD WINAPI producer(void *p) {
    uint32_t value = 0x1234abcd;
    int i;
    for (i=0;i<2000;i++) xbox_InputTraceEmit("test",(int)(intptr_t)p,&value,4,i,0);
    return 0;
}
int main(int argc,char **argv) {
    int i, count = argc > 1 ? atoi(argv[1]) : 1;
    HANDLE th[4];
    uint32_t value = 0x1234abcd;
    input_latency_start();
    if (count == -3) { TerminateProcess(GetCurrentProcess(), 0); return 0; }
    if (count < 0) {
        for(i=0;i<4;i++) th[i]=CreateThread(0,0,producer,(void*)(intptr_t)i,0,0);
        if (count == -2) { Sleep(1); return 0; }
        WaitForMultipleObjects(4,th,TRUE,INFINITE);
        for(i=0;i<4;i++) CloseHandle(th[i]);
    } else for(i=0;i<count;i++) xbox_InputTraceEmit("test",0,&value,4,i,0);
    value=0; /* Output must contain copies, not borrowed pointers. */
    return 0;
}
'''

@pytest.fixture(scope='module')
def trace_tool(tmp_path_factory):
    cl = shutil.which('cl')
    if not cl:
        pytest.skip('native tracing fixture needs MSVC on Windows')
    folder = tmp_path_factory.mktemp('input-trace')
    driver = folder/'driver.c'
    driver.write_text(DRIVER)
    exe = folder/'trace.exe'
    result = subprocess.run([cl,'/nologo','/O2','/TC', '/I'+str(ROOT/'src'),
        '/I'+str(ROOT/'src/hooks'), '/I'+str(ROOT/'tools/xboxrecomp/src/input'),
        str(driver),str(ROOT/'src/hooks/input_latency.c'),'/Fe:'+str(exe)],cwd=folder,capture_output=True,text=True)
    assert result.returncode == 0, result.stdout+result.stderr
    return exe

def run_trace(exe,tmp_path,count,limit='1000000'):
    path=tmp_path/'input.csv'
    env=dict(os.environ,RECOMP_INPUT_TRACE=str(path),RECOMP_INPUT_TRACE_LIMIT=limit)
    subprocess.run([str(exe),str(count)],env=env,check=True,capture_output=True)
    meta=json.loads(Path(str(path)+'.json').read_text())
    with path.open() as f:
        rows=list(csv.DictReader(f))
    return rows,meta

def test_payload_snapshot_and_high_resolution_clock(trace_tool,tmp_path):
    rows,meta=run_trace(trace_tool,tmp_path,100)
    assert meta['closed']==1 and meta['io_errors']==0
    assert len(rows)==meta['accepted']==meta['written']==100
    assert all(r['payload']=='cdab3412' and r['n']=='4' for r in rows)
    assert len({r['seq'] for r in rows})==100
    assert all(int(r['t_ns'])>=0 for r in rows)
    assert any(int(r['t_ns'])%1000000 for r in rows)

def test_budget_is_bounded_and_loss_is_explicit(trace_tool,tmp_path):
    rows,meta=run_trace(trace_tool,tmp_path,20000,'32')
    assert meta['attempted']==20000 and meta['drop_budget']==19968
    assert meta['accepted']<=32 and len(rows)==meta['accepted']==meta['written']
    assert meta['attempted']==meta['accepted']+sum(meta[k] for k in ('drop_lock','drop_full','drop_budget','bad_payload'))

def test_concurrent_producers_are_accounted_and_drained(trace_tool,tmp_path):
    rows,meta=run_trace(trace_tool,tmp_path,-1)
    assert meta['attempted']==8000 and meta['closed']==1 and meta['io_errors']==0
    assert meta['accepted']==8000 and meta['drop_lock']==0 and meta['drop_full']==0
    assert len(rows)==meta['accepted']==meta['written']
    assert meta['attempted']==meta['accepted']+meta['drop_lock']+meta['drop_full']
    assert len({r['seq'] for r in rows})==len(rows)
    assert all(r['payload']=='cdab3412' for r in rows)

def test_disabled_does_not_create_a_trace(trace_tool,tmp_path):
    subprocess.run([str(trace_tool),'100'],cwd=tmp_path,env=dict(os.environ,RECOMP_INPUT_TRACE='0'),check=True)
    assert not list(tmp_path.iterdir())

def test_shutdown_drains_producers_that_are_still_active(trace_tool,tmp_path):
    rows,meta=run_trace(trace_tool,tmp_path,-2)
    assert meta['closed']==1 and meta['io_errors']==0
    assert len(rows)==meta['accepted']==meta['written']
    assert meta['attempted']==meta['accepted']+meta['drop_lock']+meta['drop_full']

@pytest.mark.parametrize('limit', ['2147483647', '4294967295', '18446744073709551615'])
def test_oversized_budget_cannot_wrap_queue_generations(trace_tool,tmp_path,limit):
    rows,meta=run_trace(trace_tool,tmp_path,3,limit)
    assert meta['limit']==2147483647-8192
    assert meta['attempted']==meta['accepted']==meta['written']==len(rows)==3
    assert meta['drop_budget']==0

def test_immediate_termination_has_fresh_unfinished_metadata(trace_tool,tmp_path):
    (tmp_path/'input.csv.json').write_text('{"schema":999,"closed":1}')
    rows,meta=run_trace(trace_tool,tmp_path,-3)
    assert meta['schema']==1 and meta['closed']==0 and meta['frequency']>0
    assert meta['attempted']==0 and meta['written']==0 and not rows
