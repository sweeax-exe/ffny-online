"""The server choice and the staged quick-match search (src/hooks/netplay_servers_rules.c, build 38),
compiled natively: every saved value of builds 30..37 (auto, eu, na, both, the relays' host names, the
old default) and the new ones (west, all) read as the right choice and save back the same; the search
widens on time (its own relay, the other US relay from 10 s for Automatic and All, every answering
relay from 30 s for every choice); and of two players waiting on different relays exactly one moves,
to the older room's relay. Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1); skips without it."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "netplay_servers_rules.h"

static const char *const k_texts[] = {
    "", "auto", "AUTO", " Automatic ", "play.defjam.online", "play.defjam.online:27777",
    "eu", "Europe", "na", "NA", "north_america", "us", "na.defjam.online", "na.defjam.online:27777", "US East",
    "sg", "SG", "singapore", "sg.defjam.online", "sg.defjam.online:27777",
    "west", "West", "us_west", "west.defjam.online", "west.defjam.online:27777",
    "all", "All servers", "both", "Both", "both_servers",
    "127.0.0.1:27900", "relay.example.org", "westeros",
};

int main(void)
{
    int i, s, a, b;
    for (i = 0; i < (int)(sizeof(k_texts) / sizeof(k_texts[0])); i++) {
        enum NetServerChoice c = netservers_parse_choice(k_texts[i]);
        const char *back = netservers_choice_text(c);
        printf("parse|%s|%d|%s|%d\n", k_texts[i], (int)c, back ? back : "-", netservers_choice_relay(c));
    }
    {
        static const char *const shown[] = {
            "play.defjam.online:27777", "PLAY.defjam.online", "49.12.78.32:27777", "49.12.78.32",
            "142.132.178.17", "na.defjam.online:27777", "na.defjam.online", "5.161.206.134:27777",
            "5.161.206.134", "west.defjam.online:27777", "5.78.214.178", "5.78.214.178:27777",
            "sg.defjam.online:27777", "5.223.88.150",
            "127.0.0.1:27900", "relay.example.org", "play.defjam.online:27900", "na.defjam.online.evil.org", "",
        };
        for (i = 0; i < (int)(sizeof(shown) / sizeof(shown[0])); i++) {
            char out[160];
            int ours = netservers_display(shown[i], out, sizeof(out));
            printf("shown|%s|%d|%s\n", shown[i], ours, out);
        }
    }
    printf("parse-null|%d\n", (int)netservers_parse_choice(NULL));
    for (i = 0; i < NETSERVER_COUNT; i++)
        printf("relay|%d|%d|%d\n", i, (int)netservers_relay_choice(i), netservers_partner(i));
    /* the scope: every choice, every relay, the seconds around each step, all up and US West down */
    {
        static const enum NetServerChoice cs[] = { NETCHOICE_AUTO, NETCHOICE_EU, NETCHOICE_NA, NETCHOICE_WEST,
                                                   NETCHOICE_ALL, NETCHOICE_CUSTOM };
        static const int secs[] = { 0, 9, 10, 29, 30, 300 };
        unsigned ups[] = { 7u, 3u };
        int c, u;
        for (c = 0; c < 6; c++)
            for (u = 0; u < 2; u++)
                for (i = -1; i < NETSERVER_COUNT; i++)
                    for (s = 0; s < 6; s++)
                        printf("scope|%d|%u|%d|%d|%u\n", (int)cs[c], ups[u], i, secs[s],
                               netservers_scope(cs[c], i, secs[s], ups[u]));
    }
    /* the move: two waiting players, one on each relay, rooms a and b seconds old */
    for (i = 0; i < NETSERVER_COUNT; i++)
        for (s = 0; s < NETSERVER_COUNT; s++)
            for (a = 0; a <= 60; a += 1)
                for (b = 0; b <= 60; b += 1)
                    printf("move|%d|%d|%d|%d|%d|%d\n", i, a, s, b, netservers_should_move(i, a, s, b),
                           netservers_should_move(s, b, i, a));
    /* build 90: the ping sum (pings by relay index; the other player's view swaps the two) */
    {
        static const int cases[][2][NETSERVER_COUNT] = {
            { { 20, 90, 200 }, { 100, 40, 230 } },     /* eu 120 < na 130 */
            { { 20, 90, 200 }, { 120, 30, 230 } },     /* na 120 < eu 140 */
            { { 10, 60, 0 }, { 110, 60, 0 } },         /* tie 120: na's worse ping 60 < eu's 110 */
            { { 50, 70, 0 }, { 70, 50, 0 } },          /* a full tie */
            { { 0, 90, 0 }, { 100, 40, 0 } },          /* a ping unknown */
            { { 20, 180, 170 }, { 180, 220, 10 } },    /* me on eu, them on sg: sg 180 < eu 200 */
        };
        static const int ages[][2] = { { 0, 0 }, { 31, 5 }, { 41, 11 }, { 42, 12 }, { 12, 42 } };
        int c, g;
        for (c = 0; c < (int)(sizeof(cases) / sizeof(cases[0])); c++)
            for (g = 0; g < (int)(sizeof(ages) / sizeof(ages[0])); g++) {
                int their = c == 5 ? 2 : 1;   /* me on eu (0), them on na (1), or on sg for the last case */
                printf("sum|%d|%d|%d|%d\n", c, g,
                       netservers_sum_move(0, ages[g][0], cases[c][0], their, ages[g][1], cases[c][1]),
                       netservers_sum_move(their, ages[g][1], cases[c][1], 0, ages[g][0], cases[c][0]));
            }
        printf("sum-same|%d\n", netservers_sum_move(1, 0, cases[0][0], 1, 0, cases[0][1]));
        printf("sum-null|%d\n", netservers_sum_move(0, 0, NULL, 1, 0, cases[0][1]));
    }
    return 0;
}
'''

EU, NA, WEST = 0, 1, 2
AUTO, CH_EU, CH_NA, CUSTOM, ALL, CH_WEST = 0, 1, 2, 3, 4, 5


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('servers')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'servers.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/netplay_servers_rules.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    return [line.split('|') for line in out.splitlines()]


def by(rows, kind):
    return [r[1:] for r in rows if r[0] == kind]


def test_old_and_new_setting_values(rows):
    parsed = {r[0]: (int(r[1]), r[2], int(r[3])) for r in by(rows, 'parse')}
    expect = {
        '': (AUTO, 'auto', -1), 'auto': (AUTO, 'auto', -1), 'AUTO': (AUTO, 'auto', -1),
        ' Automatic ': (AUTO, 'auto', -1),
        # builds before 30 saved the first relay's name: now the nearest
        'play.defjam.online': (AUTO, 'auto', -1), 'play.defjam.online:27777': (AUTO, 'auto', -1),
        'eu': (CH_EU, 'eu', EU), 'Europe': (CH_EU, 'eu', EU),
        # "na" (builds 30..37: North America) is US East and stays saved as "na"
        'na': (CH_NA, 'na', NA), 'NA': (CH_NA, 'na', NA), 'north_america': (CH_NA, 'na', NA), 'us': (CH_NA, 'na', NA),
        'na.defjam.online': (CH_NA, 'na', NA), 'na.defjam.online:27777': (CH_NA, 'na', NA), 'US East': (CH_NA, 'na', NA),
        'sg': (CH_WEST, 'sg', WEST), 'SG': (CH_WEST, 'sg', WEST), 'singapore': (CH_WEST, 'sg', WEST),
        'sg.defjam.online': (CH_WEST, 'sg', WEST), 'sg.defjam.online:27777': (CH_WEST, 'sg', WEST),
        # US West closed in build 89: a saved choice falls back to Automatic
        'west': (AUTO, 'auto', -1), 'West': (AUTO, 'auto', -1), 'us_west': (AUTO, 'auto', -1),
        'west.defjam.online': (AUTO, 'auto', -1), 'west.defjam.online:27777': (AUTO, 'auto', -1),
        # "both" (builds 31..37) is every relay now and is saved again as "all"
        'all': (ALL, 'all', -1), 'All servers': (ALL, 'all', -1), 'both': (ALL, 'all', -1), 'Both': (ALL, 'all', -1),
        'both_servers': (ALL, 'all', -1),
        # anything else is a relay's own address
        '127.0.0.1:27900': (CUSTOM, '-', -1), 'relay.example.org': (CUSTOM, '-', -1), 'westeros': (CUSTOM, '-', -1),
    }
    assert parsed == expect
    assert by(rows, 'parse-null') == [[str(AUTO)]]


def test_relays_and_partners(rows):
    relay = {int(r[0]): (int(r[1]), int(r[2])) for r in by(rows, 'relay')}
    assert relay == {EU: (CH_EU, -1), NA: (CH_NA, -1), WEST: (CH_WEST, -1)}


def scope_of(rows, choice, up, cur, secs):
    for r in by(rows, 'scope'):
        if (int(r[0]), int(r[1]), int(r[2]), int(r[3])) == (choice, up, cur, secs):
            return int(r[4])
    raise KeyError((choice, up, cur, secs))


def bits(*relays):
    return sum(1 << r for r in relays)


def test_search_widens_on_time(rows):
    for choice in (AUTO, ALL):
        # no relay has a partner (build 89): alone until 30 s, then every relay
        for cur in (EU, NA, WEST):
            assert [scope_of(rows, choice, 7, cur, s) for s in (0, 10, 29, 30)] == [bits(cur)] * 3 + [bits(EU, NA, WEST)]
        assert scope_of(rows, choice, 3, NA, 30) == bits(EU, NA)
    # a fixed choice stays on its relay for 30 s, then every relay
    for choice, relay in ((CH_EU, EU), (CH_NA, NA), (CH_WEST, WEST)):
        assert [scope_of(rows, choice, 7, relay, s) for s in (0, 10, 29)] == [bits(relay)] * 3
        assert scope_of(rows, choice, 7, relay, 30) == bits(EU, NA, WEST)
    # a custom address, or no relay: no search beyond the room
    for s in (0, 30, 300):
        assert scope_of(rows, CUSTOM, 7, NA, s) == 0
        assert scope_of(rows, AUTO, 7, -1, s) == 0


def test_exactly_one_of_two_waiting_players_moves(rows):
    seen = 0
    for r in by(rows, 'move'):
        mine, my_age, theirs, their_age, i_move, they_move = map(int, r)
        if mine == theirs:
            assert not i_move and not they_move
            continue
        seen += 1
        if their_age >= 40 or my_age >= 40:
            continue    # the last resort: the older side goes too, once the younger room has waited 40 s
        # one moves, never both (no swap) and never neither
        assert i_move + they_move == 1, r
        # the younger room's player moves to the older room's relay
        if their_age > my_age + 2:
            assert i_move, r
        if my_age > their_age + 2:
            assert they_move, r
        # within 2 s, the higher relay index moves (US West to US East or Europe, US East to Europe)
        if abs(my_age - their_age) <= 2:
            assert i_move == (mine > theirs), r
    assert seen


def test_the_older_side_goes_once_the_younger_room_waited_40_s(rows):
    move = {(int(r[0]), int(r[1]), int(r[2]), int(r[3])): int(r[4]) for r in by(rows, 'move')}
    # this PC on Europe, its room 60 s old; a US West room of 39 s: not yet; of 40 s: go
    assert move[(EU, 60, WEST, 39)] == 0
    assert move[(EU, 60, WEST, 40)] == 1


def test_players_see_regions_not_addresses(rows):
    shown = {r[0]: (r[1], r[2]) for r in by(rows, 'shown')}
    eu, east, west = 'the Europe server', 'the US East server', 'the US West server'
    asia = 'the Asia (Singapore) server'
    expect = {
        'play.defjam.online:27777': eu, 'PLAY.defjam.online': eu, '49.12.78.32:27777': eu, '49.12.78.32': eu,
        '142.132.178.17': eu, 'na.defjam.online:27777': east, 'na.defjam.online': east,
        '5.161.206.134:27777': east, '5.161.206.134': east, 'west.defjam.online:27777': west,
        '5.78.214.178': west, '5.78.214.178:27777': west,
        'sg.defjam.online:27777': asia, '5.223.88.150': asia,
    }
    for address, text in expect.items():
        assert shown[address] == ('1', text), address
    # a custom address, another port or a look-alike name stays as typed
    for address in ('127.0.0.1:27900', 'relay.example.org', 'play.defjam.online:27900',
                    'na.defjam.online.evil.org', ''):
        assert shown[address] == ('0', address), address


def test_ping_sum_move(rows):
    """Build 90: the relay with the smaller sum of the two pings; exactly one of the two moves, and the other
    side reads the same numbers; the sum does not decide (-1: the ages do) on an unknown ping, a full tie, the
    same relay, or when the player who should come has not (its room 42 s old, this one listed 12 s)."""
    got = {(int(r[0]), int(r[1])): (int(r[2]), int(r[3])) for r in by(rows, 'sum')}
    # me (eu) / them (na): eu 120 < na 130: I stay, they come; the staying side falls back to the ages only
    # once the other room is 42 s old and its own listed 12 s (ages row 4: me 12, them 42)
    assert [got[(0, g)] for g in range(5)] == [(0, 1), (0, 1), (0, 1), (0, 1), (-1, 1)]
    # na 120 < eu 140: I move; the na player stays, unless I have not come (ages row 3 seen from its side)
    assert [got[(1, g)] for g in range(5)] == [(1, 0), (1, 0), (1, 0), (1, -1), (1, 0)]
    # a tie on the sum: the lower worse ping (na)
    assert got[(2, 0)] == (1, 0)
    # a full tie, an unknown ping: the ages decide
    assert all(got[(3, g)] == (-1, -1) for g in range(5))
    assert all(got[(4, g)] == (-1, -1) for g in range(5))
    # eu 200 vs sg 180: the sg player's relay
    assert got[(5, 0)] == (1, 0)
    assert by(rows, 'sum-same') == [['-1']] and by(rows, 'sum-null') == [['-1']]
