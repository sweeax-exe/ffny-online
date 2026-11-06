"""Shared map replay policy and opt-in foreground tags against synthetic metadata."""
from pathlib import Path
import os
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_story_map_replay_policy(tmp_path):
    compiler = shutil.which('cl' if os.name == 'nt' else 'cc')
    if not compiler:
        pytest.skip('C compiler unavailable')
    (tmp_path / 'texture_pack.h').write_text('''
typedef int (*RecompWideReplayFilter)(const char *, int, void *);
void texture_pack_set_wide_replay_filter(RecompWideReplayFilter, void *);
''')
    (tmp_path / 'driver.c').write_text(r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "story_map.h"
#include "texture_pack.h"
static int story;
static RecompWideReplayFilter filter;
static void *filter_user;
static float extend_tag;
int cheats_in_story(void) { return story; }
void texture_pack_set_wide_replay_filter(RecompWideReplayFilter f, void *u)
{ filter=f; filter_user=u; }
void nv2a_ws_set_extend_tag(float tag) { extend_tag=tag; }
int main(void)
{
    const char *ids[]={
        "7b4754dfd82c09030171a6198634968b504062529df77cbc943be5235ca19927",
        "a1f9480b6262480bbc67ea60b4461949304fc8c474fd99ca5df7880319dfcc8a"};
    const char *other="b34f9e94d17d89c1ca766c3fcf434b7ff0527eee133137bee9e428a167135396";
    int mode, i;
    ws_story_map_init();
    assert(filter && !filter_user);
    assert(extend_tag == WS_STORY_MAP_TEXT_TAG);
    /* Change context while retaining the same source identities and callback. */
    for(mode=0;mode<4;mode++) {
        story=mode%2;
        for(i=0;i<2;i++) {
            assert(filter(ids[i],0,filter_user)==story);
            assert(!filter(ids[i],-512,filter_user));
            assert(!filter(ids[i],512,filter_user));
        }
        assert(!filter(other,0,filter_user));
        assert(!filter("",0,filter_user));
        assert(!filter(NULL,0,filter_user));
    }
    /* Synthetic movie metadata: no game instructions/artwork. A known text
     * placement is tagged once; corrupt/unknown layouts stay byte-identical. */
    {
        unsigned char apt[0xB24]={0}, saved[sizeof apt];
        float matrix[]={1,0,0,1,60,432.9f}, y;
        memcpy(apt,"Apt Data",8);
        memcpy(apt+0x3CC,matrix,sizeof matrix);
        memcpy(apt+0xAD4,"shopText",9);
        memcpy(saved,apt,sizeof apt);
        assert(ws_story_map_tag_text(NULL,sizeof apt)==-1);
        assert(ws_story_map_tag_text(apt,0xADC)==-1);
        apt[0]=0;
        assert(ws_story_map_tag_text(apt,sizeof apt)==-1);
        assert(apt[0]==0 && !memcmp(apt+1,saved+1,sizeof apt-1));
        memcpy(apt,saved,sizeof apt);
        apt[0xAD4]='x';
        assert(ws_story_map_tag_text(apt,sizeof apt)==-1);
        apt[0xAD4]='s';
        y=61; memcpy(apt+0x3DC,&y,sizeof y);
        assert(ws_story_map_tag_text(apt,sizeof apt)==-1);
        assert(!memcmp(apt+0x3E0,saved+0x3E0,4));
        memcpy(apt,saved,sizeof apt);
        y=400; memcpy(apt+0x3E0,&y,sizeof y);
        assert(ws_story_map_tag_text(apt,sizeof apt)==-1);
        memcpy(apt,saved,sizeof apt);
        assert(ws_story_map_tag_text(apt,sizeof apt)==1);
        memcpy(&y,apt+0x3E0,4);
        assert(y==432.9f+WS_STORY_MAP_TEXT_TAG);
        assert(!memcmp(apt,saved,0x3E0));
        assert(!memcmp(apt+0x3E4,saved+0x3E4,sizeof apt-0x3E4));
        memcpy(saved,apt,sizeof apt);
        assert(ws_story_map_tag_text(apt,sizeof apt)==0);
        assert(!memcmp(apt,saved,sizeof apt));
    }
    /* All copies of the pin widget share these four visual children. Keep
     * parent navigation coordinates and every other movie byte untouched. */
    {
        unsigned char apt[0x10457]={0}, saved[sizeof apt], expected[sizeof apt];
        const size_t at[]={0x75DC,0x761C,0x765C,0x769C};
        const size_t name_at[]={0xF600,0xF60C,0xF614,0xF61C};
        const char *names[]={"blackPaper","greyPin","paper","pin"};
        float y;
        size_t j;
        memcpy(apt,"Apt Data",8);
        for(i=0;i<4;i++) {
            uint32_t header[]={3,0x26,1+2*(uint32_t)i,(uint32_t)(i==1?130:131)};
            float matrix[]={1,0,0,1,i%2?0.0f:-30.0f,i%2?0.0f:-13.0f};
            memcpy(apt+at[i],header,sizeof header);
            memcpy(apt+at[i]+16,matrix,sizeof matrix);
            memcpy(apt+name_at[i],names[i],strlen(names[i])+1);
        }
        memcpy(saved,apt,sizeof apt);
        memcpy(expected,apt,sizeof apt);
        for(i=0;i<4;i++) {
            y=(i%2?0.0f:-13.0f)+WS_STORY_MAP_TEXT_TAG;
            memcpy(expected+at[i]+36,&y,4);
        }
        assert(ws_map_tag_pins(NULL,sizeof apt)==-1);
        assert(ws_map_tag_pins(apt,0xF61F)==-1);
        assert(!memcmp(apt,saved,sizeof apt));
        /* An unknown last child must not leave earlier children half tagged. */
        for(j=0;j<4;j++) {
            const size_t bad[]={0,0x769C+4,0x769C+16,0xF61C};
            apt[bad[j]]^=1;
            memcpy(saved,apt,sizeof apt);
            assert(ws_map_tag_pins(apt,sizeof apt)==-1);
            assert(!memcmp(apt,saved,sizeof apt));
            apt[bad[j]]^=1;
        }
        y=99;memcpy(apt+at[3]+36,&y,4);
        memcpy(saved,apt,sizeof apt);
        assert(ws_map_tag_pins(apt,sizeof apt)==-1);
        assert(!memcmp(apt,saved,sizeof apt));
        y=0;memcpy(apt+at[3]+36,&y,4);
        assert(ws_map_tag_pins(apt,sizeof apt)==4);
        assert(!memcmp(apt,expected,sizeof apt));
        assert(ws_map_tag_pins(apt,sizeof apt)==0);
        assert(!memcmp(apt,expected,sizeof apt));
        /* A validated partial tag can be completed, with no doubled offset. */
        y=-13;memcpy(apt+at[0]+36,&y,4);
        assert(ws_map_tag_pins(apt,sizeof apt)==1);
        assert(!memcmp(apt,expected,sizeof apt));
    }
    return 0;
}
''')
    settings = ROOT / 'tools/xboxrecomp/src/settings'
    if not (settings / 'recomp_settings.h').exists():
        pytest.skip('initialized toolkit submodule unavailable')
    exe = tmp_path / ('story-map-test.exe' if os.name == 'nt' else 'story-map-test')
    includes = [tmp_path, ROOT / 'src/patches', ROOT / 'src', settings]
    sources = [tmp_path / 'driver.c', ROOT / 'src/patches/story_map.c']
    command = ([compiler, '/nologo', '/W4', '/WX', '/TC',
                *['/I' + str(p) for p in includes], *map(str, sources), '/Fe:' + str(exe)]
               if os.name == 'nt' else
               [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                *['-I' + str(p) for p in includes], *map(str, sources), '-o', str(exe)])
    built = subprocess.run(command, cwd=tmp_path, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    subprocess.run([str(exe)], check=True)
