"""The game's audio options after a restart (src/hooks/options_audio.c), built natively against a
synthetic guest memory. The stand-in front end below does what the game's Option.Set*Volume
handlers do: store the byte, mark the global data changed and set the sound system's volume.
Needs MSVC (`cl` on PATH) or a C compiler; skips without one."""
import shutil
import subprocess

import pytest

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

STUB_TYPES = r'''#include <stdint.h>
extern unsigned char memory[];
#define MEM32(a) (*(uint32_t *)(memory + (a)))
#define MEM8(a)  (*(uint8_t *)(memory + (a)))
'''

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "options_audio.h"
unsigned char memory[0x400000];
#define DATA    0x00300000u     /* component 0x18 */
#define OPTIONS 0x00310000u     /* MEM32(DATA + 4) */
static int live[3] = { 86, 86, 86 };    /* the sound system */
static int calls;
unsigned int defjam_fe_call(const char *command)
{
    static const char *const names[3] = { "Option.SetMusicVolume(", "Option.SetSFXVolume(",
                                           "Option.SetAnnouncerVolume(" };
    int i;
    calls++;
    printf("call %s\n", command);
    for (i = 0; i < 3; i++)
        if (!strncmp(command, names[i], strlen(names[i]))) {
            int v = atoi(command + strlen(names[i]));
            MEM8(OPTIONS + 0x57 + i) = (unsigned char)v;
            MEM8(DATA + 0x1AC3) = 1;
            live[i] = v;
        }
    return 0x1000;
}
/* the front end calling the game: the hook around the call, as src/recomp_manual.c does */
static void fe(const char *text)
{
    options_audio_before_funccall(text);
    if (!strncmp(text, "Option.Set", 10)) {
        int saved = calls;
        defjam_fe_call(text);           /* the player moves a slider: the game's own handler */
        calls = saved;
    }
    options_audio_after_funccall(text);
}
static void stored(int m, int s, int a)
{
    MEM8(OPTIONS + 0x57) = (unsigned char)m;
    MEM8(OPTIONS + 0x58) = (unsigned char)s;
    MEM8(OPTIONS + 0x59) = (unsigned char)a;
}
static void report(const char *label)
{
    printf("%s live %d %d %d dirty %d calls %d\n", label, live[0], live[1], live[2],
           MEM8(DATA + 0x1AC3), calls);
    calls = 0;
}
int main(int argc, char **argv)
{
    const char *scenario = argc > 1 ? argv[1] : "";
    MEM32(0x002FB148u + 4 * 0x18) = DATA;
    MEM32(DATA + 4) = OPTIONS;
    stored(86, 86, 86);
    if (!strcmp(scenario, "boot")) {
        fe("Control.GetScreenInfo(intMain/legal1.swf)");
        fe("MemCard.BootupLoad( intMain/preStart.swf )");
        stored(40, 60, 80);                         /* the save is read in the background */
        fe("MemCard.AutosaveWarningCB(  )");
        report("loaded");
        fe("Control.GetScreenInfo(intMain/mainMenu.swf)");
        report("again");
    } else if (!strcmp(scenario, "sync-load")) {     /* a load that completes inside the call */
        fe("Control.GetScreenInfo(intMain/preStart.swf)");
        options_audio_before_funccall("MemCard.BootupLoad( intMain/preStart.swf )");
        stored(10, 20, 30);
        options_audio_after_funccall("MemCard.BootupLoad( intMain/preStart.swf )");
        report("loaded");
    } else if (!strcmp(scenario, "before-load")) {   /* data set up before any load is not a load */
        fe("Control.GetScreenInfo(intMain/legal1.swf)");
        stored(0, 0, 0);
        fe("Control.GetScreenInfo(intMain/bootLoad.swf)");
        report("early");
    } else if (!strcmp(scenario, "slider")) {        /* the player's own change: already live */
        fe("MemCard.BootupLoad( intMain/preStart.swf )");
        MEM8(DATA + 0x1AC3) = 0;
        fe("Option.SetMusicVolume(50)");
        fe("Control.GetScreenInfo(intMain/mainMenu.swf)");
        report("slider");
    } else if (!strcmp(scenario, "garbage")) {
        fe("MemCard.BootupLoad( intMain/preStart.swf )");
        stored(200, 60, 80);
        fe("Control.GetScreenInfo(intMain/mainMenu.swf)");
        report("garbage");
    } else if (!strcmp(scenario, "no-data")) {
        MEM32(0x002FB148u + 4 * 0x18) = 0;
        fe("MemCard.BootupLoad( intMain/preStart.swf )");
        fe("Control.GetScreenInfo(intMain/mainMenu.swf)");
        report("none");
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    folder = tmp_path_factory.mktemp('options-audio-native')
    include = folder / 'recomp/gen'
    include.mkdir(parents=True)
    (include / 'recomp_types.h').write_text(STUB_TYPES)
    driver = folder / 'driver.c'
    driver.write_text(DRIVER, encoding='utf-8')
    source = ROOT / 'src/hooks/options_audio.c'
    exe = folder / 'options_audio.exe'
    compiler = shutil.which('cl')
    if compiler:
        command = [compiler, '/nologo', '/O2', '/TC', '/I' + str(folder), '/I' + str(ROOT / 'src/hooks'),
                   str(driver), str(source), '/Fe:' + str(exe)]
    else:
        compiler = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not compiler:
            pytest.skip('native C compiler unavailable')
        command = [compiler, '-O2', '-I' + str(folder), '-I' + str(ROOT / 'src/hooks'), str(driver), str(source),
                   '-o', str(exe)]
    result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def run(tool, scenario):
    result = subprocess.run([str(tool), scenario], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return result.stdout.splitlines()


def test_loaded_volumes_reach_the_sound_system_and_leave_the_save_flag(tool):
    out = run(tool, 'boot')
    assert 'call Option.SetMusicVolume(40)' in out
    assert 'call Option.SetSFXVolume(60)' in out
    assert 'call Option.SetAnnouncerVolume(80)' in out
    assert 'loaded live 40 60 80 dirty 0 calls 3' in out
    assert 'again live 40 60 80 dirty 0 calls 0' in out      # once, not on every call


def test_a_load_that_completes_inside_the_call_is_seen(tool):
    assert 'loaded live 10 20 30 dirty 0 calls 3' in run(tool, 'sync-load')


def test_nothing_is_applied_before_a_load_was_asked_for(tool):
    assert 'early live 86 86 86 dirty 0 calls 0' in run(tool, 'before-load')


def test_the_players_own_slider_is_not_applied_again(tool):
    out = run(tool, 'slider')
    assert 'slider live 50 86 86 dirty 1 calls 0' in out      # the game's own save flag stays set


def test_an_impossible_volume_is_left_alone(tool):
    out = run(tool, 'garbage')
    assert 'call Option.SetMusicVolume(200)' not in out
    assert 'garbage live 86 60 80 dirty 0 calls 2' in out


def test_no_global_data_no_access(tool):
    assert 'none live 86 86 86 dirty 0 calls 0' in run(tool, 'no-data')
