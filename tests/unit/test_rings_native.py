"""F0, the Fight Complex and King of the Ring v1 (community/fight-complex-f0-protocol.md): the game's RINGS wire
(src/hooks/rings_rules.c, compiled natively: the request's layout and padding, the answer's cards, a cut answer, the
ring types' ids and names) and the wiring around it (every relay is asked, the report's `ring` field, the hub entry,
the page's empty state, the gallery pages). Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1) for the native
part; skips without it."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'
SERVERS_C = (HOOKS / 'netplay_servers.c').read_text(encoding='utf-8')
NETPLAY_C = (HOOKS / 'netplay.c').read_text(encoding='utf-8')
SCREEN = (HOOKS / 'online_screen.cpp').read_text(encoding='utf-8')
RINGS_UI = (HOOKS / 'rings_ui.inc').read_text(encoding='utf-8')
RULES_H = (HOOKS / 'rings_rules.h').read_text(encoding='utf-8')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "rings_rules.h"

static int put_str(unsigned char *b, int at, const char *s)
{
    int n = (int)strlen(s);
    b[at++] = (unsigned char)n;
    memcpy(b + at, s, (size_t)n);
    return at + n;
}

/* one card as the protocol doc's example lays it out */
static int put_card(unsigned char *b, int at, int ring, int type, int ch, const char *code, const char *name, int flags,
                    int members, int line, int watching, int phase, int streak, const char *king, int fights,
                    const unsigned char *venues, int nv, int rounds)
{
    int v;
    b[at++] = (unsigned char)ring; b[at++] = (unsigned char)type; b[at++] = (unsigned char)ch;
    memcpy(b + at, code, 6); at += 6;
    at = put_str(b, at, name);
    b[at++] = (unsigned char)flags; b[at++] = (unsigned char)members; b[at++] = (unsigned char)line;
    b[at++] = (unsigned char)watching; b[at++] = (unsigned char)phase;
    b[at++] = (unsigned char)streak; b[at++] = (unsigned char)(streak >> 8);
    at = put_str(b, at, king);
    b[at++] = (unsigned char)fights; b[at++] = (unsigned char)(fights >> 8);
    b[at++] = (unsigned char)nv;
    for (v = 0; v < nv; v++) b[at++] = venues[v];
    b[at++] = (unsigned char)rounds;
    return at;
}

int main(void)
{
    unsigned char req[1500], b[1200];
    unsigned char print[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    unsigned char cage[1] = { 10 }, boxing[2] = { 4, 5 };
    NetRingCard cards[4];
    unsigned nonce = 0;
    int n, at, k, i, zeros = 1;
    n = rings_build_request(req, (int)sizeof(req), 0xA1B2C3D4u, print, 0x5FDFFFF9u);
    for (i = 22; i < n; i++) if (req[i]) zeros = 0;
    printf("req|%d|%.4s|%d|%d|%02X%02X%02X%02X|%d%d|%02X%02X%02X%02X|%d\n", n, (const char *)req, req[4], req[5], req[6], req[7],
           req[8], req[9], req[10], req[17], req[18], req[19], req[20], req[21], zeros);
    printf("small|%d|%d\n", rings_build_request(req, 21, 1, print, 0), rings_build_request(req, 40, 1, print, 0));
    memset(b, 0, sizeof(b));
    memcpy(b, "DJNP", 4); b[4] = 1; b[5] = RINGS_TYPE;
    b[6] = 0x78; b[7] = 0x56; b[8] = 0x34; b[9] = 0x12;
    b[10] = 2;
    at = put_card(b, 11, 1, 1, 1, "K7Q2ZD", "The Cage", 0, 4, 3, 1, 3, 3, "Blaze", 12, cage, 1, 0);
    at = put_card(b, at, 2, 2, 1, "PX4M2A", "The Ring", 1, 0, 0, 0, 0, 0, "", 0, boxing, 2, 3);
    n = rings_parse_answer(b, (int)sizeof(b), &nonce, cards, 4);   /* padded to 1200: the zeros after the cards */
    printf("ans|%d|%08X\n", n, nonce);
    for (k = 0; k < n; k++)
        printf("card|%d|%d|%d|%s|%s|%d|%d|%d|%d|%d|%d|%s|%d|%d|%d|%d|%d\n", cards[k].ring, cards[k].type, cards[k].channel,
               cards[k].code, cards[k].name, cards[k].flags, cards[k].members, cards[k].line, cards[k].watching, cards[k].phase,
               cards[k].king_streak, cards[k].king, cards[k].fights, cards[k].venue_count, cards[k].venues[0],
               cards[k].venue_count > 1 ? cards[k].venues[1] : -1, cards[k].rounds);
    printf("exact|%d\n", rings_parse_answer(b, at, NULL, cards, 4));
    printf("cut|%d\n", rings_parse_answer(b, at - 1, NULL, cards, 4));
    printf("max1|%d|%s\n", rings_parse_answer(b, at, NULL, cards, 1), cards[0].code);
    b[10] = 0;
    printf("none|%d\n", rings_parse_answer(b, 11, NULL, cards, 4));
    b[5] = 0x25;
    printf("other|%d\n", rings_parse_answer(b, 11, NULL, cards, 4));
    for (k = 0; k <= 5; k++)
        printf("type|%d|%s|%s|%s|%d\n", k, ring_type_id(k) ? ring_type_id(k) : "-", ring_type_title(k), ring_type_king(k),
               ring_type_of_id(ring_type_id(k)));
    cards[0].flags = 0;
    k = ring_room_mode(&cards[0]);
    cards[0].flags = RING_FLAG_VOTE;
    printf("mode|%d|%d\n", k, ring_room_mode(&cards[0]));
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('rings')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'rings.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(HOOKS),
                            str(folder / 'driver.c'), str(HOOKS / 'rings_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return [line.split('|') for line in out.splitlines()]


def row(rows, tag):
    return [r for r in rows if r[0] == tag]


def test_request_layout_and_padding(rows):
    (r,) = row(rows, 'req')
    # padded to a full 1200-byte datagram: the relay never answers with more bytes than it was sent
    assert r[1:] == ['1200', 'DJNP', '1', str(0x46), 'D4C3B2A1', '18', 'F9FFDF5F', '1']
    assert row(rows, 'small') == [['small', '0', '40']]


def test_answer_cards(rows):
    assert row(rows, 'ans') == [['ans', '2', '12345678']]
    cage, boxing = row(rows, 'card')
    assert cage[1:] == ['1', '1', '1', 'K7Q2ZD', 'The Cage', '0', '4', '3', '1', '3', '3', 'Blaze', '12', '1', '10', '-1', '0']
    assert boxing[1:] == ['2', '2', '1', 'PX4M2A', 'The Ring', '1', '0', '0', '0', '0', '0', '', '0', '2', '4', '5', '3']


def test_cut_and_other_answers(rows):
    assert row(rows, 'exact') == [['exact', '2']]
    assert row(rows, 'cut') == [['cut', '-1']]
    assert row(rows, 'max1') == [['max1', '1', 'K7Q2ZD']]
    assert row(rows, 'none') == [['none', '0']]          # a relay with F0 but no ring for this build
    assert row(rows, 'other') == [['other', '-1']]


def test_type_ids_match_the_report_and_the_board(rows):
    types = {int(r[1]): r[2:] for r in row(rows, 'type')}
    assert types[1][0] == 'cage' and types[2][0] == 'boxing' and types[3][0] == 'pit' and types[4][0] == 'ringout'
    assert types[0][0] == '-' and types[5][0] == '-'
    for k in range(1, 5):
        assert int(types[k][3]) == k
    assert types[1][1] == 'King of the Cage' and types[1][2] == 'Cage King'
    assert row(rows, 'mode') == [['mode', str(0x41), str(0x40)]]


def test_header_constants_match_the_doc():
    assert re.search(r'#define RINGS_TYPE\s+0x46\b', RULES_H)
    assert re.search(r'#define RINGS_REQ_LEN\s+1200\b', RULES_H)
    assert re.search(r'#define RING_FLAG_FREE\s+1\b', RULES_H) and re.search(r'#define RING_FLAG_FULL\s+2\b', RULES_H)
    assert re.search(r'#define RING_FLAG_VOTE\s+4\b', RULES_H)


def test_every_relay_is_asked_and_the_list_thread_runs_it():
    assert 'rings_round();' in SERVERS_C.split('static DWORD WINAPI list_thread')[1].split('\n}\n')[0]
    fetch = SERVERS_C.split('void netservers_fetch_rings(int every_ms)')[1].split('\n}\n')[0]
    assert 'NETSERVER_COUNT' in fetch and 'NETSERVER_OFFLINE' in fetch
    # RINGS carries the same CAPS word as T_CAPS
    assert 'netplay_client_caps()' in SERVERS_C and 'put32(cb + n, netplay_client_caps())' in NETPLAY_C


def test_report_carries_the_ring():
    report = NETPLAY_C.split('static int report_json(')[1].split('\n}\n')[0]
    assert '\\"ring\\":\\"%s\\"' in report and 'ring_type_id(ring)' in report
    # taken at START from the RINGS answers, before g_cs (netplay_servers.c has a lock of its own)
    start = NETPLAY_C.split('static void on_start_as(const uint8_t *b, int len, int watching)\n{')[1]
    assert start.index('netservers_ring_of(') < start.index('EnterCriticalSection(&g_cs);\n    memset(N.rematch_want')


def test_hub_entry_page_and_empty_state():
    assert '"FIGHT COMPLEX"' in SCREEN and 'HUB_COMPLEX' in SCREEN and 'open_complex(HUB, 0)' in SCREEN
    assert '#include "rings_ui.inc"' in SCREEN
    assert 'NO RINGS RIGHT NOW' in RINGS_UI and 'LOOKING FOR RINGS...' in RINGS_UI
    assert '/api/rings/board?format=text' in RINGS_UI and 'season=all' in RINGS_UI
    for page in ('hub-complex', 'complex', 'complex-empty', 'kotr', 'kotr-alltime', 'hill-ring', 'watch-ring', 'hill-hud-ring'):
        assert '"%s"' % page in SCREEN, page
