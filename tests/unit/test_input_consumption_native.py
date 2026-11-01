"""Actual guest adapter: API success alone must not lose a fight action."""
from pathlib import Path
import os
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_consumption.h"
#include "input_latency.h"
#define input_consumption_xdk_end(h,s,status) (input_latency_xdk_begin(h),input_consumption_xdk_end(h,s,status))
unsigned char memory[0x400000];
static unsigned calls, last_port, last_packet, threshold;
static int enabled;
static uint64_t generation=1;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
#define ACTOR(n) (0x3B92E0u+(n)*0x12E8u)
#define PAD(n) (0x3B8F48u+(n)*0x20u)
static void put(uint32_t a,uint32_t v) { memcpy(memory+a,&v,4); }
int input_latency_on(void) { return 0; }
void xbox_InputTraceEmit(const char *s,int p,const void *d,unsigned n,uint64_t a,uint64_t b) {}
void xbox_HostInputSetEdgeThreshold(unsigned n) { threshold=n; }
void xbox_HostInputEnableConsumedEdges(int on) { enabled=on; }
int xbox_HostInputConsumedEdges(void) { return enabled; }
void xbox_HostInputConsumePacket(int port,uint32_t packet,const void *b,unsigned n)
{ CHECK(n==18);calls++;last_port=port;last_packet=packet; }
uint64_t xbox_HostInputEdgeGeneration(int port) { return generation; }
void xbox_HostInputConsumeGeneration(int port,uint32_t packet,const void *b,unsigned n,uint64_t gen)
{ if(gen==generation) xbox_HostInputConsumePacket(port,packet,b,n); }
static void read_pad(unsigned port,unsigned packet,unsigned button)
{
    memset(memory+0x2004,0,18);memory[0x2004+4]=(unsigned char)button;
    put(0x2000,packet);
    input_consumption_xdk_end(0x1000+port*0x200,0x2000,0);
}
int main(void)
{
    unsigned i;
    input_consumption_start();CHECK(enabled && threshold==1);
    for(i=0;i<4;i++) {
        unsigned h=0x1000+i*0x200,c=0x3000+i*0x20;
        put(0x3C9340+i*0x90,h);put(h+0xA3,c);put(c+8,0x3100);memory[0x3100]=18;
        put(ACTOR(i)+0x28,0xffffffffu);
    }
    put(ACTOR(0)+0x28,2);put(0x3B9098+2*4,1);put(0x3B8F30,1);
    read_pad(1,10,255);CHECK(!calls); /* API read during a zero-step frame */
    read_pad(1,10,255);CHECK(!calls);
    put(PAD(1),0x40);input_consumption_fighter(ACTOR(0));
    CHECK(calls==1 && last_port==1 && last_packet==10);
    input_consumption_fighter(ACTOR(0));CHECK(calls==1);
    read_pad(1,11,0);input_consumption_fighter(ACTOR(0));CHECK(calls==1); /* release not read yet */
    put(PAD(1),0);input_consumption_fighter(ACTOR(0));CHECK(calls==2);
    read_pad(1,12,255);put(PAD(1),0x40);memory[ACTOR(0)+0x937]=1;
    input_consumption_fighter(ACTOR(0));CHECK(calls==2); /* CPU actor cannot ack a player */
    memory[ACTOR(0)+0x937]=0;input_consumption_fighter(ACTOR(0));CHECK(calls==3);
    put(0x3B8F30,0);read_pad(1,13,0);CHECK(calls==4); /* frontend uses API ack */
    input_consumption_xdk_end(0x1200,0x2000,1);CHECK(calls==4);
    memory[0x3100]=16;read_pad(1,14,255);CHECK(calls==4); /* foreign layout */
    memory[0x3100]=18;input_consumption_xdk_end(0x1800,0x2000,0);CHECK(calls==4);
    put(0x3B8F30,1);
    for(i=0;i<12;i++) {
        const unsigned masks[12]={0x20,0x10,0x40,0x80,0x200,0x100,0x1000,0x2000,1,2,4,8};
        unsigned before=calls;
        memset(memory+0x2004,0,18);
        if(i<8) memory[0x2004+2+i]=255;else memory[0x2004]=(unsigned char)(1u<<(i-8));
        put(0x2000,20+i*2);input_consumption_xdk_end(0x1200,0x2000,0);CHECK(calls==before);
        put(PAD(1),masks[i]);input_consumption_fighter(ACTOR(0));CHECK(calls==before+1);
        memset(memory+0x2004,0,18);put(0x2000,21+i*2);input_consumption_xdk_end(0x1200,0x2000,0);
        input_consumption_fighter(ACTOR(0));CHECK(calls==before+1);
        put(PAD(1),0);input_consumption_fighter(ACTOR(0));CHECK(calls==before+2);
    }
    { unsigned before=calls;
      read_pad(1,60,255);generation++;put(PAD(1),0x40);
      input_consumption_fighter(ACTOR(0));CHECK(calls==before); /* reset invalidates pending ACK */
      read_pad(1,62,255);read_pad(1,61,0); /* old completion cannot replace pending new press */
      input_consumption_fighter(ACTOR(0));CHECK(calls==before+1 && last_packet==62);
    }
    puts("guest input consumption checks passed");return 0;
}
'''

def test_guest_ack_waits_for_mapped_human_fighter(tmp_path):
    cl=shutil.which('cl')
    if not cl: pytest.skip('native MSVC fixture')
    inc=tmp_path/'recomp/gen';inc.mkdir(parents=True)
    (tmp_path/'host.h').write_text('#include <windows.h>\n#include <stdint.h>\n')
    (inc/'recomp_types.h').write_text('#include <stdint.h>\n#define RECOMP_TLS __declspec(thread)\nextern unsigned char memory[];\n'
        '#define MEM32(a) (*(uint32_t *)(memory+(a)))\n#define MEM8(a) memory[a]\n#define XBOX_PTR(a) (memory+(a))\n')
    driver=tmp_path/'driver.c';driver.write_text(DRIVER)
    exe=tmp_path/'guest.exe'
    result=subprocess.run([cl,'/nologo','/O2','/TC','/I'+str(tmp_path),'/I'+str(ROOT/'src/hooks'),
        '/I'+str(ROOT/'tools/xboxrecomp/src/input'),'/I'+str(ROOT/'tools/xboxrecomp/src'),
        str(driver),str(ROOT/'src/hooks/input_latency_guest.c'),'/Fe:'+str(exe)],cwd=tmp_path,capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    result=subprocess.run([str(exe)],env=dict(os.environ,RECOMP_INPUT_EDGES='1'),capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
