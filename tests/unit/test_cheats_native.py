"""The cheats (src/patches/cheats.c) against synthetic guest memory, with the real settings registry.

Each fight cheat is checked at the guest fields and calls the live runs confirmed (PR #97,
docs/research/cheat-menu.md): who it applies to, the health routine's delta, the Blazin' gate and the
momentum-gain call, the model scale nodes; then the Story slot, the one-shot requests, the style
encoding and the Story hero's match setup at Game.StartGame. The launcher's fighter list is checked too.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "recomp/gen/recomp_types.h"
#include "recomp_settings.h"
#include "patches/cheats.h"

unsigned char memory[0x500000];
uint32_t g_eax, g_ecx = 0x55, g_edx, g_ebx, g_esp = 0x1000, g_ebp, g_esi, g_edi;
int g_fp_top;
static int ready_calls, ready_slot = -1, gain_calls, gain_fills = 1, styles_calls;
static uint32_t gain_record, gain_amount, styles_this;

#define R(slot) (0x3B92E0u + (uint32_t)(slot) * 0x12E8u)
static float getf(uint32_t va) { float f; uint32_t b = MEM32(va); memcpy(&f, &b, 4); return f; }
static void setf(uint32_t va, float f) { uint32_t b; memcpy(&b, &f, 4); MEM32(va) = b; }

/* The guest routines cheats.c calls: cdecl or thiscall, the callee pops its return address. */
void sub_000B0130(void) { ready_calls++; ready_slot = (int)MEM32(g_esp + 4); assert(MEM32(g_esp + 8) == 1); g_esp += 4; }
void sub_001A5190(void)
{
    gain_calls++;
    gain_record = MEM32(g_esp + 4);
    gain_amount = MEM32(g_esp + 8);
    if (gain_fills) { MEM32(gain_record + 0x364) = 0x42C60000u; MEM8(gain_record + 0x93E) |= 1; }
    g_esp += 4;
}
void sub_00014C20(void) { styles_calls++; styles_this = g_ecx; g_esp += 4; }

/* sub_001A50B0(record, delta, flags, extra) as its entry hook sees it. */
static float health_call(int slot, float delta, uint32_t ret)
{
    g_esp = 0x2000;
    MEM32(g_esp) = ret;
    MEM32(g_esp + 4) = R(slot);
    setf(g_esp + 8, delta);
    cheats_health_change();
    return getf(0x2000 + 8);
}

static void fighter(int slot, int cpu)
{
    MEM32(R(slot) + 0x28) = (uint32_t)slot;
    MEM8(R(slot) + 0x937) = (uint8_t)cpu;
    setf(R(slot) + 0x354, 200.0f);
    setf(R(slot) + 0x358, 150.0f);
    setf(R(slot) + 0x350, 100.0f);
    setf(R(slot) + 0x364, 20.0f);
    MEM8(R(slot) + 0x93E) = 0;
}

int main(int argc, char **argv)
{
    RecompSetting table[8];
    size_t n = cheats_settings(table, 8);
    int slot, i;
    assert(n == 5);
    recomp_settings_register(table, n);
    recomp_settings_load(argv[1]);
    cheats_apply_settings();
    for (slot = 0; slot < 4; slot++)
        MEM32(R(slot) + 0x28) = 0xFFFFFFFFu;
    fighter(0, 1);               /* a computer fighter in slot 0: player 1 is the first human */
    fighter(1, 0);
    fighter(2, 0);

    if (!strcmp(argv[2], "health")) {
        printf("cpu %g p1 %g p2 %g\n", health_call(0, -10, 0x1A605B), health_call(1, -10, 0x1A605B),
               health_call(2, -10, 0x1A605B));
        printf("gain %g\n", health_call(1, 5, 0x1A5807));
        MEM32(0x3B8F30) = 1;
        cheats_fight_step();
        printf("step %g %g %g\n", getf(R(0) + 0x350), getf(R(1) + 0x350), getf(R(1) + 0x358));
        setf(R(1) + 0x350, 0.0f);            /* a fighter already knocked out stays down */
        cheats_fight_step();
        printf("ko %g\n", getf(R(1) + 0x350));
    } else if (!strcmp(argv[2], "regen")) {
        printf("regen %g other %g loss %g\n", health_call(1, 5, 0x1A5807), health_call(1, 5, 0x1A605B),
               health_call(1, -5, 0x1A5807));
    } else if (!strcmp(argv[2], "blazin")) {
        MEM32(0x3B8F30) = 0;
        for (i = 0; i < 400; i++)
            cheats_fight_step();
        printf("walk-in gains %d\n", gain_calls);
        MEM32(0x3B8F30) = 1;
        for (i = 0; i < 299; i++)
            cheats_fight_step();
        printf("early gains %d\n", gain_calls);
        cheats_fight_step();
        printf("gains %d record %d amount %08X state %u %u %u\n", gain_calls, gain_record == R(1),
               gain_amount, MEM8(R(0) + 0x93E), MEM8(R(1) + 0x93E), MEM8(R(2) + 0x93E));
        cheats_fight_step();
        printf("held gains %d\n", gain_calls);
        MEM8(R(1) + 0x93E) = 0;               /* the Blazin' ran out: ready again on the next step */
        gain_fills = 0;                       /* and a gain that falls short is completed by hand */
        cheats_fight_step();
        printf("fallback gains %d ready %d slot %d meter %g state %u timer %g\n", gain_calls, ready_calls,
               ready_slot, getf(R(1) + 0x364), MEM8(R(1) + 0x93E), getf(R(1) + 0x368));
        assert(g_ecx == 0x55 && g_esp == 0x1000);
    } else if (!strcmp(argv[2], "size")) {
        uint32_t plain = 0x200000, parts = 0x210000, node = 0x220000;
        MEM32(0x375120 + 4) = plain;
        MEM32(0x375120 + 8) = parts;
        MEM32(parts + 0xABB4) = 1;
        MEM32(parts + 0x9D0) = node;
        cheats_fight_step();
        printf("plain %g %g %g parts %g node %g\n", getf(plain + 0x310), getf(plain + 0x314), getf(plain + 0x318),
               getf(parts + 0x310), getf(node + 0x2E0));
        recomp_settings_set("cheats", "fighter_size", 0);
        cheats_apply_settings();
        cheats_fight_step();
        printf("normal %g %g\n", getf(plain + 0x310), getf(node + 0x2E0));
    } else if (!strcmp(argv[2], "story")) {
        CheatStory s;
        uint32_t gd = 0x80001000u, p = 0x80010000u, slot_va = p + 0x177C + 0x5950;
        MEM32(0x2FB148 + 0x18 * 4) = gd;
        MEM32(gd + 4) = p;
        MEM32(p + 0x5AC7C) = 1;
        MEM32(slot_va + 0x20) = 5800;
        MEM32(slot_va + 0x28) = 465;
        MEM8(slot_va + 0x140) = 1; MEM8(slot_va + 0x141) = 0xFF; MEM8(slot_va + 0x142) = 0xFF;
        for (i = 0; i < 6 * 6 * 6; i++)
            MEM8(0x2EBF6F + i - 43) = (uint8_t)(i + 50);   /* any table: the id must come from it */
        printf("outside %d\n", cheats_story_info(&s));
        assert(!cheats_in_story());
        cheats_story_request(CHEAT_STORY_CASH, 0, 0, 0);
        cheats_game_tick();
        printf("status %s\n", cheats_story_status());
        cheats_on_funccall("Game.StartStoryMode( 0,0 )");
        assert(cheats_in_story());
        cheats_on_funccall("Control.GetScreenInfo(battle/chsVenue.swf)");   /* the Story map */
        assert(cheats_in_story());
        printf("inside %d cash %u devpts %u styles %d %d %d\n", cheats_story_info(&s), s.cash, s.devpts,
               s.style[0], s.style[1], s.style[2]);
        cheats_story_request(CHEAT_STORY_CASH, 0, 0, 0);
        cheats_game_tick();
        cheats_story_request(CHEAT_STORY_DEVPTS, 0, 0, 0);
        cheats_on_funccall("Crib.GetNextSE()");
        printf("cash %u devpts %u status %s\n", MEM32(slot_va + 0x20), MEM32(slot_va + 0x28), cheats_story_status());
        cheats_story_request(CHEAT_STORY_STYLES, 2, 2, CHEAT_STYLE_NONE);
        cheats_game_tick();
        printf("same style: %s calls %d\n", cheats_story_status(), styles_calls);
        cheats_story_request(CHEAT_STORY_STYLES, 2, CHEAT_STYLE_NONE, 4);
        cheats_game_tick();
        printf("gap: calls %d\n", styles_calls);
        cheats_story_request(CHEAT_STORY_STYLES, 2, 0, CHEAT_STYLE_NONE);
        cheats_game_tick();
        printf("styles %u %u %u id %u expect %u calls %d this %d\n", MEM8(slot_va + 0x140), MEM8(slot_va + 0x141),
               MEM8(slot_va + 0x142), MEM8(slot_va + 0x143),
               MEM8(0x2EBF6F + cheats_style_table_index(2, 0, -1)) + 1u, styles_calls, styles_this == slot_va);
        printf("index %u\n", cheats_style_table_index(1, -1, -1));

        /* The Story hero: the match setup's slot marked as the created hero (gd+0x1E8+slot = slot + 1) is
         * rewritten for the chosen fighter from game/fighter.xml, its names found in the loaded copy. */
        {
            uint32_t pool = 0x80060000u;
            const char *names = "V2IP_009A\0V2IP_012A\0V2SD_2\0";
            memcpy(memory + GUEST(pool + 1), names, 28);
            MEM8(gd + 0x1E8) = 1;
            MEM8(gd + 0x1E9) = 0;
            MEM32(gd + 0x168) = 98;
            MEM32(gd + 0x188) = 98;
            MEM32(gd + 0x208) = 0x80070000u;              /* the hero's own (template) name */
            MEM32(gd + 0x20C) = pool + 1;                 /* the opponent's model name */
            MEM32(gd + 0x228) = 0x80070010u;
            MEM32(gd + 0x218) = 0x12345678u;              /* the hero's display name: kept */
            cheats_start_game(gd);
            printf("hero created %u core %d models %d %d shadow %d outfit %d name %d\n", MEM8(gd + 0x1E8),
                   (int)MEM32(gd + 0x168), MEM32(gd + 0x208) == pool + 11, MEM32(gd + 0x20C) == pool + 1,
                   MEM32(gd + 0x228) == pool + 21, (int)MEM32(gd + 0x188), MEM32(gd + 0x218) == 0x12345678u);
            MEM8(gd + 0x1E8) = 1;
            MEM32(gd + 0x168) = 98;
            cheats_on_funccall("Control.GetScreenInfo(intMain/mainMenu.swf)");
            assert(!cheats_in_story());
            cheats_on_funccall("Game.StartStoryMode( 0,0 )");
            assert(cheats_in_story());
            cheats_on_funccall("Game.StartMainMenu( 0 )");
            assert(!cheats_in_story());
            printf("left %d\n", cheats_story_info(&s));
            cheats_start_game(gd);
            printf("battle created %u core %d\n", MEM8(gd + 0x1E8), (int)MEM32(gd + 0x168));
        }
    }
    assert(g_esp == 0x1000 || g_esp == 0x2000);
    return 0;
}
'''

ROSTER = r'''
#include "pc_ui_model.h"
#include <iostream>
int main()
{
    std::string xml = "<fighterlist>"
        "<fighter><name>HOUSE</name><dispname>House (STF)</dispname><ID>0</ID><coreID>63</coreID></fighter>"
        "<fighter><name>BO</name><ID>1</ID></fighter>"
        "<fighter><name>CP K__</name><dispname>CP K__</dispname><ID>97</ID></fighter>"
        "<fighter><name>BROKEN</name><ID>x1</ID></fighter>"
        "<fighter><name>ICE-T</name><dispname>Ice-T (SUB)</dispname><ID>8</ID></fighter>"
        "</fighterlist>";
    for (const auto &f : pcui::roster(xml))
        std::cout << f.id << "=" << f.name << "\n";
    std::cout << "empty " << pcui::roster("").size() << "\n";
}
'''


@pytest.fixture(scope='module')
def cheats_tool(tmp_path_factory):
    if not (ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c').is_file():
        pytest.skip('native cheats fixture needs the initialized toolkit submodule')
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('native cheats fixture needs a C compiler')
    folder = tmp_path_factory.mktemp('cheats-native')
    inc = folder / 'recomp/gen'
    inc.mkdir(parents=True)
    # Guest memory is one array; the contiguous heap at 0x80000000 is folded in above 4 MiB.
    (inc / 'recomp_types.h').write_text(
        '#include <stdint.h>\nextern unsigned char memory[];\n'
        'extern uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi;\nextern int g_fp_top;\n'
        'typedef void (*recomp_func_t)(void);\n'
        '#define GUEST(a) ((uint32_t)(a) >= 0x80000000u ? (uint32_t)(a) - 0x80000000u + 0x400000u : (uint32_t)(a))\n'
        '#define MEM32(a) (*(uint32_t *)(memory + GUEST(a)))\n'
        '#define MEM8(a) (*(uint8_t *)(memory + GUEST(a)))\n')
    (folder / 'host.h').write_text('/* No host APIs needed by this fixture. */\n')
    driver = folder / 'driver.c'
    driver.write_text(DRIVER)
    exe = folder / ('cheats.exe' if os.name == 'nt' else 'cheats')
    includes = [folder, ROOT / 'src', ROOT / 'src/patches', ROOT / 'tools/xboxrecomp/src/settings']
    sources = [driver, ROOT / 'src/patches/cheats.c', ROOT / 'src/patches/cheats_settings.c', ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c']
    if os.name == 'nt':
        cmd = [compiler, '/nologo', '/O2', '/TC', *['/I' + str(p) for p in includes],
               *map(str, sources), '/Fe:' + str(exe)]
    else:
        cmd = [compiler, '-O2', *['-I' + str(p) for p in includes], *map(str, sources), '-o', str(exe), '-pthread', '-lm']
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


FIGHTER_XML = """<fighterlist>
<fighter><name>BO</name><model>V2IP_012A</model><shadow>V2SD_2</shadow><ID>1</ID><coreID>56</coreID></fighter>
<fighter><name>ICE-T</name><model>V2IP_009A</model><shadow>V2SD_2</shadow><ID>8</ID><coreID>15</coreID></fighter>
<fighter><name>CP KB_</name><model></model><ID>99</ID><coreID>100</coreID></fighter>
</fighterlist>
"""


def run(tool, tmp_path, case, ini='', **env_values):
    path = tmp_path / 'settings.ini'
    path.write_text(ini)
    (tmp_path / 'game').mkdir(exist_ok=True)
    (tmp_path / 'game/fighter.xml').write_text(FIGHTER_XML)   # the game's own file, read from game/
    env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_CHEAT')}
    env.update(env_values)
    result = subprocess.run([str(tool), str(path), case], env=env, capture_output=True, text=True, timeout=10,
                            cwd=tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.splitlines()


@pytest.mark.parametrize('who,cpu,p1,p2', [
    ('off', -10, -10, -10),
    ('player 1', -10, 0, -10),
    ('every player', -10, 0, 0),
    ('everyone', 0, 0, 0),
])
def test_infinite_health_targets_and_refills(cheats_tool, tmp_path, who, cpu, p1, p2):
    out = run(cheats_tool, tmp_path, 'health', '[cheats]\ninfinite_health = %s\n' % who)
    assert out[0] == 'cpu %g p1 %g p2 %g' % (cpu, p1, p2)
    assert out[1] == 'gain 5'                     # health given back is never touched by this cheat
    refilled = {'off': (), 'player 1': (1,), 'every player': (1, 2), 'everyone': (0, 1, 2)}[who]
    assert out[2] == 'step %g %g %g' % (200 if 0 in refilled else 100, 200 if 1 in refilled else 100,
                                        200 if 1 in refilled else 150)
    assert out[3] == 'ko 0'


def test_no_regen_only_stops_regeneration(cheats_tool, tmp_path):
    assert run(cheats_tool, tmp_path, 'regen', RECOMP_CHEAT_NO_REGEN='1') == ['regen 0 other 5 loss -5']
    assert run(cheats_tool, tmp_path, 'regen') == ['regen 5 other 5 loss -5']


def test_max_blazin_waits_for_the_fight_and_uses_the_game_gain(cheats_tool, tmp_path):
    out = run(cheats_tool, tmp_path, 'blazin', RECOMP_CHEAT_MAX_BLAZIN='player 1')
    assert out == [
        'walk-in gains 0',                        # never during the walk-in (phase 0)
        'early gains 0',                          # nor in the first five seconds of fighting
        'gains 1 record 1 amount 447A0000 state 0 1 0',
        'held gains 1',                           # ready already: nothing more
        'fallback gains 2 ready 1 slot 1 meter 99 state 1 timer 1800',
    ]


def test_fighter_size_scales_both_kinds_of_model(cheats_tool, tmp_path):
    out = run(cheats_tool, tmp_path, 'size', '[cheats]\nfighter_size = giant\n')
    assert out == ['plain 1.4 1.4 1.4 parts 0 node 1.4', 'normal 1 1']
    out = run(cheats_tool, tmp_path, 'size', RECOMP_CHEAT_FIGHTER_SIZE='tiny')
    assert out[0] == 'plain 0.6 0.6 0.6 parts 0 node 0.6'


def test_story_one_shots_styles_and_hero(cheats_tool, tmp_path):
    out = run(cheats_tool, tmp_path, 'story', RECOMP_CHEAT_STORY_HERO='1')
    assert out[0] == 'outside 0'
    assert out[1] == 'status Not applied: no Story profile is loaded.'
    assert out[2] == 'inside 1 cash 5800 devpts 465 styles 1 255 255'
    assert out[3] == 'cash 999999 devpts 99999 status Development points set to 99999.'
    assert out[4].startswith('same style: Not applied') and out[4].endswith('calls 0')
    assert out[5] == 'gap: calls 0'
    styles = out[6].split()
    assert styles[1:4] == ['2', '0', '255'] and styles[5] == styles[7] and styles[-3:] == ['1', 'this', '1']
    assert out[7] == 'index 29'                   # main 1, no hybrids: what the live profile showed (id 98 = 97 + 1)
    assert out[8] == 'hero created 0 core 56 models 1 1 shadow 1 outfit 56 name 1'
    assert out[9] == 'left 0'
    assert out[10] == 'battle created 1 core 98'   # outside Story nothing is touched


@pytest.mark.parametrize('hero', ['99', '5'])
def test_story_hero_needs_a_real_fighter(cheats_tool, tmp_path, hero):
    # 99 is the style template Story uses for the created hero; 5 is not in this fighter.xml.
    out = run(cheats_tool, tmp_path, 'story', RECOMP_CHEAT_STORY_HERO=hero)
    assert out[8] == 'hero created 1 core 98 models 0 1 shadow 0 outfit 98 name 1'


def test_settings_file_round_trip(cheats_tool, tmp_path):
    run(cheats_tool, tmp_path, 'regen', '[cheats]\nno_regen = yes\nmax_blazin = everyone\n')
    # An unreadable value falls back to off rather than to some other cheat.
    out = run(cheats_tool, tmp_path, 'health', '[cheats]\ninfinite_health = 7\n')
    assert out[0] == 'cpu -10 p1 -10 p2 -10'


def test_fighter_roster_from_fighter_xml(tmp_path):
    compiler = shutil.which('cl') or shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        pytest.skip('C++ compiler required')
    source = tmp_path / 'roster.cpp'
    source.write_text(ROSTER)
    exe = tmp_path / ('roster.exe' if Path(compiler).stem.lower() == 'cl' else 'roster')
    if Path(compiler).stem.lower() == 'cl':
        cmd = [compiler, '/nologo', '/std:c++17', '/EHsc', '/I' + str(ROOT / 'src/hooks'), str(source), '/Fe:' + str(exe)]
    else:
        cmd = [compiler, '-std=c++17', '-I' + str(ROOT / 'src/hooks'), str(source), '-o', str(exe)]
    result = subprocess.run(cmd, cwd=tmp_path, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
    assert out == ['1=BO', '0=House (STF)', '8=Ice-T (SUB)', 'empty 0']
