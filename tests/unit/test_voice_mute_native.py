"""The session's mutes for voice and text (src/hooks/voice_mute.c), compiled natively.

A player report: "the mute button never muted anyone". The VOICE CHAT page's per-player volume was kept by relay
slot and forgotten with the room, and the chat window's MUTE IN ROOM (by name, text only) never reached the voice,
so someone muted in the chat was still heard. Both now write one list keyed by the player (account when the
relay named it, else name) that voice and text both read. These tests pin that list's rules: a mute follows the
player, never the slot; it learns the account from a first chat line; two players with the same name but different
accounts are two players; it holds until cleared.
Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1) on Windows or `cc` elsewhere; skips without it.
"""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hooks/voice_mute.h"

/* one command per line: set NAME ACCT PCT | mute NAME ACCT | unmute NAME ACCT | learn NAME ACCT | pct NAME ACCT | clear | list
 * (NAME "-" is none) */
int main(void)
{
    char line[256], cmd[32], name[64], tmp[64];
    unsigned acct;
    int pct, n;
    while (fgets(line, sizeof line, stdin)) {
        n = sscanf(line, "%31s %63s %u %d", cmd, name, &acct, &pct);
        if (n >= 2 && !strcmp(name, "-")) name[0] = 0;
        if (!strcmp(cmd, "clear")) vmute_clear();
        else if (!strcmp(cmd, "set") && n == 4) vmute_set(name, acct, pct);
        else if (!strcmp(cmd, "mute") && n >= 3) vmute_mute(name, acct, 1);
        else if (!strcmp(cmd, "unmute") && n >= 3) vmute_mute(name, acct, 0);
        else if (!strcmp(cmd, "learn") && n >= 3) vmute_learn(name, acct);
        else if (!strcmp(cmd, "pct") && n >= 3) printf("%d\n", vmute_percent(name, acct));
        else if (!strcmp(cmd, "muted") && n >= 3) printf("%d\n", vmute_is_muted(name, acct));
        else if (!strcmp(cmd, "list")) {
            int i, p;
            unsigned a;
            for (i = 0; vmute_entry(i, tmp, sizeof tmp, &a, &p); i++) printf("%s,%u,%d\n", tmp, a, p);
            printf("end\n");
        }
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def exe(tmp_path_factory):
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('the native fixture needs a C compiler (MSVC on Windows)')
    folder = tmp_path_factory.mktemp('voicemute')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    src = str(ROOT / 'src/hooks/voice_mute.c')
    if os.name == 'nt':
        out = folder / 'voicemute.exe'
        cmd = [compiler, '/nologo', '/O2', '/TC', '/I' + str(ROOT / 'src'), str(folder / 'driver.c'), src, '/Fe:' + str(out)]
    else:
        out = folder / 'voicemute'
        cmd = [compiler, '-O2', '-std=c11', '-pthread', '-I' + str(ROOT / 'src'), str(folder / 'driver.c'), src, '-o', str(out)]
    built = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def run(exe, *lines):
    out = subprocess.run([str(exe)], input='\n'.join(lines) + '\n', capture_output=True, text=True, check=True).stdout
    return out.split()


def test_nothing_set_plays_at_full_volume(exe):
    assert run(exe, 'pct Bob 0', 'muted Bob 0') == ['100', '0']


def test_mute_by_name_holds_and_unmute_lifts_it(exe):
    assert run(exe, 'mute Bob 0', 'muted Bob 0', 'pct Bob 0', 'unmute Bob 0', 'muted Bob 0') == ['1', '0', '0']


def test_names_compare_without_case(exe):
    assert run(exe, 'mute Bob 0', 'muted BOB 0') == ['1']


def test_a_mute_follows_the_account_when_the_name_is_not_known_yet(exe):
    # muted by account (the chat line named it), then heard in a room list under another name
    assert run(exe, 'mute - 77', 'muted Whoever 77', 'muted Whoever 0', 'muted Whoever 5') == ['1', '0', '0']


def test_a_name_mute_learns_the_account_from_the_first_line(exe):
    # muted by name before they spoke; their first chat line (name + account) ties the two, and the account then
    # matches even when the room list shows another name
    assert run(exe, 'mute Bob 0', 'learn Bob 77', 'muted Robert 77', 'muted Bob 0', 'list') == ['1', '1', 'Bob,77,0', 'end']


def test_two_players_with_one_name_and_different_accounts_are_two_players(exe):
    # the slot (and a name) can be handed to a different player: only the muted account stays muted
    assert run(exe, 'mute Bob 77', 'muted Bob 77', 'muted Bob 88', 'muted Bob 0') == ['1', '0', '1']


def test_a_changed_name_follows_the_account(exe):
    assert run(exe, 'mute Bob 77', 'learn Bobby 77', 'muted Bobby 77', 'muted Bobby 0', 'list') == ['1', '1', 'Bobby,77,0', 'end']


def test_the_volume_is_kept_per_player_and_clamped(exe):
    assert run(exe, 'set Bob 5 50', 'pct Bob 5', 'set Bob 5 900', 'pct Bob 5', 'set Bob 5 -4', 'pct Bob 5',
               'pct Carl 6') == ['50', '200', '0', '100']


def test_a_hundred_percent_forgets_the_player(exe):
    assert run(exe, 'mute Bob 5', 'unmute Bob 5', 'list') == ['end']


def test_a_slot_given_to_the_next_player_is_not_muted(exe):
    # the old slot-keyed volume stayed on the slot: player A muted in slot 2 left, player B took slot 2 and was silent;
    # A came back in slot 3 and was heard. Keyed by the player, it is the other way round.
    assert run(exe, 'mute Alice 11', 'muted Bob 22', 'muted Alice 11') == ['0', '1']


def test_the_list_holds_until_cleared(exe):
    assert run(exe, 'mute A 1', 'mute B 2', 'list', 'clear', 'list') == ['A,1,0', 'B,2,0', 'end', 'end']


def test_an_entry_with_nothing_to_go_by_is_not_made(exe):
    assert run(exe, 'mute - 0', 'list') == ['end']
