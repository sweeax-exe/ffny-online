"""Draft mode's pick and ban on the game's side (src/hooks/netplay_draft_rules.c, ranked phase 2b),
compiled natively: DRAFT as the relay's DraftMsg.appendBody writes it (netplay/relay/draft.go; the
example is the one draft_test.go pins), DRAFTACT's bytes, and the rules the Draft pages and the
tests' automatic choice use (turns, ban owners A B B A A B, tile and venue states, banners). Needs
MSVC (`cl` on PATH, e.g. after scripts/env.ps1); skips without it."""
import shutil
import subprocess

import pytest

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "netplay_draft_rules.h"

/* a DRAFT body: step, turn, secs, game, venue, first, bans, struck, venue_bans, picks, locked,
   cursors, options, locks0, locks1, afk */
static int body(unsigned char *b, int step, int turn, int game, int first, const int *bans, int nb,
                const int *struck, int ns, const int *opts, int no)
{
    int n = 0, i;
    b[n++] = (unsigned char)step; b[n++] = (unsigned char)turn; b[n++] = 20; b[n++] = (unsigned char)game;
    b[n++] = 0; b[n++] = (unsigned char)first;
    b[n++] = (unsigned char)nb; for (i = 0; i < nb; i++) b[n++] = (unsigned char)bans[i];
    b[n++] = (unsigned char)ns; for (i = 0; i < ns; i++) b[n++] = (unsigned char)struck[i];
    b[n++] = 0;
    b[n++] = 0xFF; b[n++] = 0xFF; b[n++] = 0; b[n++] = 0xFF; b[n++] = 0xFF;
    b[n++] = (unsigned char)no; for (i = 0; i < no; i++) b[n++] = (unsigned char)opts[i];
    b[n++] = 0; b[n++] = 0; b[n++] = 0xFF;
    return n;
}

int main(void)
{
    /* relay draft_test.go's example */
    static const unsigned char ex[] = { 0x06, 0x01, 0x15, 0x02, 0x05, 0x01, 0x03, 0x03, 0xFF, 0x07, 0x01, 0x01, 0x01,
                                        0x02, 0xFF, 0x16, 0x02, 0x37, 0x16, 0x03, 0x01, 0x02, 0x05, 0x01, 0x09, 0x00, 0xFF };
    static const int grid[] = { 3, 9, 22, 30, 99 };
    unsigned char b[128], act[3];
    char line[64];
    int s[3], n, a, id;
    NetDraft d;
    printf("ex|%d\n", netdraft_decode(ex, (int)sizeof(ex), &d));
    printf("exf|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d\n", d.step, d.turn, d.secs, d.game, d.venue,
           d.first, d.ban_count, d.bans[1], d.struck[0], d.venue_bans[0], d.picks[0], d.picks[1], d.locked[1],
           d.cursors[0], d.option_count, d.lock_count[0], d.locks[0][0], d.afk);
    printf("turn|%d|%d|%d\n", netdraft_my_turn(&d, 0), netdraft_my_turn(&d, 1), netdraft_action(&d));
    netdraft_banner(&d, 0, line, sizeof(line)); printf("banner0|%s\n", line);
    netdraft_banner(&d, 1, line, sizeof(line)); printf("banner1|%s\n", line);
    printf("tiles|%d|%d|%d|%d|%d\n", netdraft_fighter_state(&d, 0, 3), netdraft_fighter_state(&d, 0, 9),
           netdraft_fighter_state(&d, 0, 22), netdraft_fighter_state(&d, 1, 22), netdraft_fighter_state(&d, 1, 9));
    printf("legal|%d|%d|%d|%d\n", netdraft_fighter_legal(&d, 1, 9), netdraft_fighter_legal(&d, 0, 9),
           netdraft_fighter_legal(&d, 0, 99), netdraft_fighter_legal(&d, 0, 3));
    n = netdraft_bans_of(&d, 1, s); printf("bans1|%d|%d|%d|%d\n", n, s[0], s[1], s[2]);
    n = netdraft_bans_of(&d, 0, s); printf("bans0|%d|%d|%d|%d\n", n, s[0], s[1], s[2]);
    printf("venues|%d|%d|%d|%d\n", netdraft_venue_state(&d, 2), netdraft_venue_state(&d, 5),
           netdraft_venue_state(&d, 1), netdraft_venue_legal(&d, 1));
    a = id = -1;
    printf("auto0|%d\n", netdraft_auto(&d, 0, grid, 5, 0, &a, &id));
    printf("auto1|%d|%d|%d\n", netdraft_auto(&d, 1, grid, 5, 0, &a, &id), a, id);
    netdraft_auto(&d, 1, grid, 5, 1, &a, &id); printf("auto1s|%d\n", id);
    netdraft_auto(&d, 1, grid, 5, 2, &a, &id); printf("auto1t|%d\n", id);
    printf("short|%d|%d\n", netdraft_decode(ex, (int)sizeof(ex) - 1, &d), d.valid);
    memcpy(b, ex, sizeof(ex)); b[sizeof(ex)] = 0;
    printf("long|%d\n", netdraft_decode(b, (int)sizeof(ex) + 1, &d));
    n = netdraft_encode_act(act, NETDRAFT_BAN, NETDRAFT_ACT_BAN, 55);
    printf("actlen|%d|%d|%d|%d\n", n, act[0], act[1], act[2]);
    {   /* bans: first = 1, so slot 1 bans first, then 0, 0, 1, 1, 0 */
        const int bans[] = { 5, 6 };
        netdraft_decode(b, body(b, NETDRAFT_BAN, 1, 1, 1, bans, 0, NULL, 0, NULL, 0), &d);
        netdraft_banner(&d, 1, line, sizeof(line)); printf("ban-a|%s\n", line);
        netdraft_banner(&d, 0, line, sizeof(line)); printf("ban-b|%s\n", line);
        netdraft_decode(b, body(b, NETDRAFT_BAN, 0, 1, 1, bans, 2, NULL, 0, NULL, 0), &d);
        netdraft_banner(&d, 0, line, sizeof(line)); printf("ban-c|%s\n", line);
        printf("ban-auto|%d|%d|%d\n", netdraft_auto(&d, 0, grid, 5, 0, &a, &id), a, id);
        printf("owners|%d|%d|%d|%d|%d|%d\n", netdraft_ban_owner(&d, 0), netdraft_ban_owner(&d, 1),
               netdraft_ban_owner(&d, 2), netdraft_ban_owner(&d, 3), netdraft_ban_owner(&d, 4), netdraft_ban_owner(&d, 5));
    }
    {   /* strikes: first = 0, one struck, B's turn */
        const int struck[] = { 1 }, opts[] = { 1, 5, 6, 11, 19 };
        netdraft_decode(b, body(b, NETDRAFT_STRIKE, 1, 1, 0, NULL, 0, struck, 1, opts, 5), &d);
        netdraft_banner(&d, 1, line, sizeof(line)); printf("strike|%s|%d|%d\n", line, netdraft_venue_state(&d, 1),
                                                         netdraft_venue_legal(&d, 1));
        printf("strike-auto|%d|%d|%d\n", netdraft_auto(&d, 1, grid, 5, 0, &a, &id), a, id);
    }
    {   /* blind: both pick; the AFK banner after a timeout */
        netdraft_decode(b, body(b, NETDRAFT_BLIND, 0, 1, 0, NULL, 0, NULL, 0, NULL, 0), &d);
        printf("blind|%d|%d\n", netdraft_my_turn(&d, 0), netdraft_my_turn(&d, 1));
        n = body(b, NETDRAFT_DONE, 0, 1, 0, NULL, 0, NULL, 0, NULL, 0);
        b[n - 1] = 1;
        netdraft_decode(b, n, &d);
        netdraft_banner(&d, 0, line, sizeof(line)); printf("afk0|%s\n", line);
        netdraft_banner(&d, 1, line, sizeof(line)); printf("afk1|%s\n", line);
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('draft')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'draft.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/netplay_draft_rules.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    return [line.split('|') for line in out.splitlines()]


def by(rows, kind):
    return [r[1:] for r in rows if r[0] == kind]


def test_relay_example(rows):
    assert by(rows, 'ex') == [['1']]
    # winner's pick, slot 1's turn, 21 s, game 2, venue 5, A = slot 1, three bans (one skipped), ...
    assert by(rows, 'exf') == [['6', '1', '21', '2', '5', '1', '3', '255', '1', '2', '255', '22', '1', '55', '3', '1',
                                '9', '255']]
    assert by(rows, 'short') == [['0', '0']]
    assert by(rows, 'long') == [['0']]
    assert by(rows, 'actlen') == [['3', '1', '1', '55']]


def test_turns_and_banners(rows):
    assert by(rows, 'turn') == [['0', '1', '3']]
    assert by(rows, 'banner0') == [['OPPONENT PICKING']]
    assert by(rows, 'banner1') == [['PICK YOUR FIGHTER']]
    assert by(rows, 'ban-a') == [['YOUR BAN 1/3']]
    assert by(rows, 'ban-b') == [['OPPONENT BANNING 1/3']]
    assert by(rows, 'ban-c') == [['YOUR BAN 2/3']]
    assert by(rows, 'owners') == [['1', '0', '0', '1', '1', '0']]
    assert by(rows, 'strike') == [['STRIKE A VENUE 1/2', '1', '0']]
    assert by(rows, 'blind') == [['1', '1']]
    assert by(rows, 'afk0') == [['OPPONENT AFK']]
    assert by(rows, 'afk1') == [['YOU WERE AFK']]


def test_tiles_and_venues(rows):
    # banned 3, padlock 9 (slot 0 won with it), the opponent's 22, slot 1's own 22, 9 open for slot 1
    assert by(rows, 'tiles') == [['1', '2', '4', '3', '0']]
    assert by(rows, 'legal') == [['1', '0', '0', '0']]
    assert by(rows, 'bans1') == [['1', '3', '255', '255']]
    assert by(rows, 'bans0') == [['2', '255', '7', '255']]
    assert by(rows, 'venues') == [['2', '3', '0', '0']]


def test_auto_choice(rows):
    assert by(rows, 'auto0') == [['0']]
    assert by(rows, 'auto1') == [['1', '3', '9']]
    assert by(rows, 'auto1s') == [['22']]
    assert by(rows, 'auto1t') == [['30']]      # 99 (a created fighter) never
    assert by(rows, 'ban-auto') == [['1', '1', '3']]   # 5 and 6 are banned already
    assert by(rows, 'strike-auto') == [['1', '2', '5']]
