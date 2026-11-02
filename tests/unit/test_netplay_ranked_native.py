"""Ranked play's wire forms and ladder on the game's side (src/hooks/netplay_ranked.c, ranked phase 2a),
compiled natively: the long RANKED the relay's decodeRankedExt reads, the long QUEUE and RANKINFO as the
relay's appendExt / RankInfo.appendBody write them (server/relay/protocol_ranked.go, draft.go), the stats
text's ranked_<mode>= lines (old ten fields and the thirteen of phase 2a), and the divisions of
accounts/ranks.go. Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1); skips without it."""
import shutil
import subprocess

import pytest

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "netplay_ranked.h"

static void hex(const char *tag, const unsigned char *b, int n)
{
    int i;
    printf("%s|", tag);
    for (i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

int main(void)
{
    unsigned char out[32];
    int pings[3] = { 30, 110, 70000 }, n, d, r;
    static const int ratings[] = { 900, 1099, 1100, 1149, 1150, 1199, 1200, 1349, 1350, 1500, 1549, 1550, 1799,
                                   1800, 1850, 1899, 1949, 1950, 2099, 2100, 2600 };
    NetQueueExt q;
    NetRankInfo ri;
    NetModeRank m;
    char name[48];
    /* RANKED: modes Open|Classic, primary Classic, rollback on, a code */
    n = netranked_encode_ext(out, 3, NETRANK_CLASSIC, 1, pings, "XMOVE1");
    hex("ext", out, n);
    n = netranked_encode_ext(out, 0, NETRANK_OPEN, 0, NULL, NULL);
    hex("ext-min", out, n);
    {   /* QUEUE 5 (accept) and 6 (move), as relay Queue.appendExt writes them */
        unsigned char acc[] = { 1, 0, 3, 0, 0, 0, 1, 15, 0x08, 0x06, 'M', 'A', 'T', 'C', 'H', '1', 0 };
        unsigned char mv[] = { 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 'X', 'M', 'O', 'V', 'E', '1', 16,
                               'n', 'a', '.', 'e', 'x', 'a', 'm', 'p', 'l', 'e', ':', '2', '7', '7', '7', '7' };
        netranked_decode_queue_ext(acc, (int)sizeof(acc), &q);
        printf("qacc|%d|%d|%d|%d|%d|%d|%d|%s|%s\n", q.valid, q.counts[0], q.counts[1], q.counts[2], q.mode,
               q.accept_secs, q.opp_rating, q.code, q.addr);
        netranked_decode_queue_ext(mv, (int)sizeof(mv), &q);
        printf("qmove|%d|%d|%s|%s\n", q.valid, q.mode, q.code, q.addr);
        printf("qshort|%d\n", netranked_decode_queue_ext(mv, 10, &q));
        printf("qbadlen|%d\n", netranked_decode_queue_ext(mv, (int)sizeof(mv) - 1, &q));
    }
    {   /* RANKINFO: relay draft_test's example, with and without the set */
        unsigned char b[] = { 1, 2, 0, 15, 0, 0x14, 0x07, 1, 0, 1, 0xFF, 3, 0xDC, 0x05, 0, 1, 2, 1, 0xFF };
        netranked_decode_rankinfo(b, (int)sizeof(b), &ri);
        printf("ri|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d\n", ri.valid, ri.mode, ri.count, ri.p[0].division,
               ri.p[0].rating, ri.p[0].set_wins, ri.p[1].division, ri.p[1].placements, ri.p[1].provisional,
               ri.has_set, ri.game, ri.picker, ri.winner, netranked_player(&ri, 1) == &ri.p[1]);
        netranked_decode_rankinfo(b, (int)sizeof(b) - 3, &ri);
        printf("ri-noset|%d|%d|%d\n", ri.valid, ri.has_set, ri.picker);
        printf("ri-bad|%d\n", netranked_decode_rankinfo(b, (int)sizeof(b) - 1, &ri));
        netranked_decode_rankinfo(b, (int)sizeof(b), &ri);
        netranked_player_label(&ri.p[0], 5, name, sizeof(name));
        printf("label0|%s\n", name);
        netranked_player_label(&ri.p[1], 5, name, sizeof(name));
        printf("label1|%s\n", name);
    }
    /* stats lines */
    netranked_parse_line("1712|98|13|0|5|0|38|7|3|Enforcer II", &m);
    printf("line10|%d|%d|%d|%d|%d|%d|%s|%d|%s|%d\n", m.valid, m.rating, m.division, m.to_next, m.set_wins, m.set_losses,
           m.label, m.peak, m.peak_rank, m.hint);
    netranked_parse_line("1880|120|16|0|5|1|20|12|4|Kingpin II ?|1902|Kingpin III|promotion", &m);
    printf("line13|%d|%d|%d|%s|%d|%s|%d\n", m.valid, m.provisional, m.peak, m.label, m.hint, m.peak_rank,
           m.deviation);
    netranked_parse_line("1500|350|9|3|5|1|0|2|0|PLACEMENT 2/5|1520|Contender III|", &m);
    printf("lineplace|%d|%d|%d|%s|%d\n", m.valid, m.placements_left, m.placements_of, m.label, m.hint);
    printf("lineshort|%d\n", netranked_parse_line("1500|350|9", &m));
    printf("keys|%d|%d|%d|%d|%d\n", netranked_mode_of_key("ranked_open"), netranked_mode_of_key("ranked_classic"),
           netranked_mode_of_key("ranked_draft"), netranked_mode_of_key("ranked"), netranked_mode_of_key("ranked_x"));
    for (d = 0; d < NETRANK_DIVISIONS; d++) {
        netranked_division_name(d, name, sizeof(name));
        printf("div|%d|%d|%s\n", d, netranked_division_min(d), name);
    }
    for (r = 0; r < (int)(sizeof(ratings) / sizeof(ratings[0])); r++) {
        d = netranked_division_of(ratings[r]);
        printf("of|%d|%d|%d\n", ratings[r], d, netranked_division_percent(ratings[r], d));
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('ranked')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'ranked.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/netplay_ranked.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    return [line.split('|') for line in out.splitlines()]


def by(rows, kind):
    return [r[1:] for r in rows if r[0] == kind]


def test_ranked_long_form(rows):
    # modes 3, flags rollback | primary 1 << 4, pings LE (70000 capped), the code
    assert by(rows, 'ext') == [['03' + '11' + '1e00' + '6e00' + 'ffff' + 'XMOVE1'.encode().hex()]]
    # the primary is always among the modes; no pings or code: zeros
    assert by(rows, 'ext-min') == [['01' + '00' + '0' * 12 + '0' * 12]]


def test_queue_long_form(rows):
    assert by(rows, 'qacc') == [['1', '1', '3', '0', '1', '15', '1544', 'MATCH1', '']]
    assert by(rows, 'qmove') == [['1', '1', 'XMOVE1', 'na.example:27777']]
    assert by(rows, 'qshort') == [['0']]
    assert by(rows, 'qbadlen') == [['0']]


def test_rankinfo(rows):
    assert by(rows, 'ri') == [['1', '1', '2', '15', '1812', '1', '255', '3', '1', '1', '2', '1', '255', '1']]
    assert by(rows, 'ri-noset') == [['1', '0', '-1']]
    assert by(rows, 'ri-bad') == [['0']]
    assert by(rows, 'label0') == [['Kingpin III']]
    assert by(rows, 'label1') == [['PLACEMENT 2/5']]


def test_stats_lines(rows):
    # the ten fields of phase 1: peak falls back to the rating, no hint
    assert by(rows, 'line10') == [['1', '1712', '13', '38', '7', '3', 'Enforcer II', '1712', '', '0']]
    assert by(rows, 'line13') == [['1', '1', '1902', 'Kingpin II ?', '1', 'Kingpin III', '120']]
    assert by(rows, 'lineplace') == [['1', '3', '5', 'PLACEMENT 2/5', '0']]
    assert by(rows, 'lineshort') == [['0']]
    assert by(rows, 'keys') == [['0', '1', '2', '-1', '-1']]


def test_ladder_matches_the_account_service(rows):
    divs = {int(r[0]): (int(r[1]), r[2]) for r in by(rows, 'div')}
    # accounts/ranks.go: Street Rookie II from 1100, I from 1150; 50 points a division; Legend one division
    assert divs[1] == (1100, 'Street Rookie II') and divs[2] == (1150, 'Street Rookie I')
    assert divs[3] == (1200, 'Hustler III') and divs[9] == (1500, 'Contender III') and divs[15] == (1800, 'Kingpin III')
    assert divs[16] == (1850, 'Kingpin II') and divs[20] == (2050, 'Don of the Bronx I')
    assert divs[21] == (2100, 'Def Jam Legend')
    assert divs[0][1] == 'Street Rookie III'
    of = {int(r[0]): (int(r[1]), int(r[2])) for r in by(rows, 'of')}
    assert of[900] == (0, 0) and of[1099] == (0, 98) and of[1100] == (1, 0) and of[1199] == (2, 98)
    assert of[1200] == (3, 0) and of[1549] == (9, 98) and of[1550] == (10, 0)
    assert of[1800] == (15, 0) and of[1899] == (16, 98) and of[2099] == (20, 98)
    assert of[2100] == (21, 100) and of[2600] == (21, 100)
