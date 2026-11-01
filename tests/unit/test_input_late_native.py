"""Exercise the real optional adapter: guest context, fallback and lifecycle contention."""
from pathlib import Path
import os
import shutil
import subprocess
import pytest

ROOT=Path(__file__).resolve().parents[2]
TYPES=r'''
#include <stdint.h>
#include <math.h>
#define RECOMP_TLS __declspec(thread)
extern unsigned char memory[];
#define XBOX_PTR(a) (memory+(a))
#define MEM8(a) memory[a]
#define MEM32(a) (*(uint32_t *)(memory+(a)))
#define MEMF(a) (*(float *)(memory+(a)))
#define MEMD(a) (*(double *)(memory+(a)))
#define RECOMP_FP_PC(v) (v)
#define PUSH32(sp,v) do { (sp)-=4;MEM32(sp)=(v); } while(0)
extern uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_eflags;
extern int g_df,g_fp_top,g_fp_cmp;
extern uint16_t g_fp_control_word,g_fp_cc;
extern double g_fp_stack[8];
'''
DRIVER=r'''
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_late.c"
unsigned char memory[0x400000];
uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_eflags;
int g_df,g_fp_top,g_fp_cmp;
uint16_t g_fp_control_word,g_fp_cc;
double g_fp_stack[8];
static unsigned reads,converts,status,irql,skips,dispatch_busy;
static HANDLE locked,release;
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
int input_consumption_on(void) { return 1; }
int xbox_HostInputConsumedEdges(void) { return 1; }
void xbox_InputTraceEmit(const char *s,int p,const void *d,unsigned n,uint64_t a,uint64_t b)
{ if(!strcmp(s,"late_skip_busy")) skips++; }
int xbox_DispatchExclusionTryEnter(void) { if(dispatch_busy) return 0;irql++;return 1; }
void xbox_DispatchExclusionLeave(void) { irql--; }
void sub_0027E075(void)
{
    uint32_t out=MEM32(g_esp+8);reads++;
    memset(memory+out,0,22);MEM32(out)=17;MEM8(out+4)=0x11;MEM8(out+8)=255;
    *(int16_t *)(memory+out+14)=16384;
    g_eax=status;g_ecx=0xfeed;g_edx=0xbeef;g_esp+=12;
}
void sub_000D7BF0_gen(void)
{
    uint32_t out=MEM32(g_esp+4),raw=input_late_raw();
    CHECK(raw && MEM8(raw+0x10)==0x11 && MEM8(raw+0x14)==255);
    CHECK(MEMF(raw)==.5f && MEMF(raw+4)==0);
    converts++;memset(memory+out,0,0x94);MEM32(out)=0x12345678;
    g_eax=99;g_ebx=77;g_esi=66;g_edi=55;g_ebp=44;g_seh_ebp=33;
    g_eflags=22;g_df=1;g_fp_top=2;g_fp_cmp=3;g_fp_cc=4;g_fp_control_word=5;
    g_fp_stack[2]=999;g_esp+=12;
}
static DWORD WINAPI hold_lock(void *unused)
{ input_late_lock();SetEvent(locked);WaitForSingleObject(release,2000);input_late_unlock();return 0; }
int main(void)
{
    guest_context before,after;
    HANDLE thread;
    input_late_start();CHECK(input_late_on()==1);
    MEM32(0x3B8F30)=1;MEM32(0x800+8)=0;MEM32(0x800+12)=2;
    MEM32(0x3C9340)=0x1000;MEM32(0x1000+0xA3)=0x3000;MEM32(0x3008)=0x3100;MEM8(0x3100)=18;
    MEMF(0x286D2C)=0;MEMF(0x2B5D1C)=1.0f/32768;MEMD(0x2D0538)=.25;
    g_eax=1;g_ecx=2;g_edx=3;g_esp=0x20000;g_ebx=4;g_esi=5;g_edi=6;
    g_ebp=7;g_seh_ebp=8;g_eflags=9;g_fp_stack[2]=123;
    before=save_context();memset(memory+0x3C92B8,0xA5,0x26); /* handle lives later in this record */
    input_late_refresh(0x800,0x22000);after=save_context();
    CHECK(!memcmp(&before,&after,sizeof before) && irql==0 && reads==1 && converts==1);
    CHECK(!input_late_raw() && MEM32(0x22000)==0x12345678 && MEM8(0x3C92B8)==0xA5);
    input_late_refresh(0x800,0x22000);CHECK(reads==2 && converts==2); /* repeat keeps global edge latch */
    status=1;MEM32(0x22000)=0xabcdef;input_late_refresh(0x800,0x22000);
    CHECK(MEM32(0x22000)==0xabcdef && reads==3 && converts==2 && irql==0);
    MEM8(0x3100)=16;input_late_refresh(0x800,0x22000);CHECK(reads==3);MEM8(0x3100)=18;
    MEM32(0x800+8)=4;input_late_refresh(0x800,0x22000);CHECK(reads==3);MEM32(0x800+8)=0;
    MEM32(0x3B8F30)=0;input_late_refresh(0x800,0x22000);CHECK(reads==3);MEM32(0x3B8F30)=1;
    locked=CreateEvent(NULL,FALSE,FALSE,NULL);release=CreateEvent(NULL,FALSE,FALSE,NULL);
    thread=CreateThread(NULL,0,hold_lock,NULL,0,NULL);CHECK(WaitForSingleObject(locked,2000)==WAIT_OBJECT_0);
    input_late_refresh(0x800,0x22000);CHECK(skips==1 && reads==3 && irql==0);
    SetEvent(release);CHECK(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    dispatch_busy=1;input_late_refresh(0x800,0x22000);CHECK(reads==3 && irql==0);
    CloseHandle(thread);CloseHandle(locked);CloseHandle(release);
    puts("late input context and fallback checks passed");return 0;
}
'''

def test_late_refresh_preserves_context_and_original_on_failure(tmp_path):
    cl=shutil.which('cl')
    if not cl: pytest.skip('native MSVC fixture')
    inc=tmp_path/'recomp/gen';inc.mkdir(parents=True)
    (tmp_path/'host.h').write_text('#include <windows.h>\n#include <stdint.h>\n')
    (inc/'recomp_types.h').write_text(TYPES)
    driver=tmp_path/'driver.c';driver.write_text(DRIVER)
    exe=tmp_path/'late.exe'
    result=subprocess.run([cl,'/nologo','/O2','/TC','/I'+str(tmp_path),
        '/I'+str(ROOT/'src/hooks'),'/I'+str(ROOT/'tools/xboxrecomp/src/input'),
        '/I'+str(ROOT/'tools/xboxrecomp/src'),str(driver),'/Fe:'+str(exe)],
        cwd=tmp_path,capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    result=subprocess.run([str(exe)],env=dict(os.environ,RECOMP_INPUT_LATE_REFRESH='1'),capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
