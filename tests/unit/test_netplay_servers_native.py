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
    "sa", "SA", "southamerica", "South America", "brazil", "Brasil", "sao paulo", "sa.defjam.online",
    "sa.defjam.online:27777", "SA.DEFJAM.ONLINE:27777", "na2.defjam.online", "sg2.defjam.online:27777", "Miami",
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
            "sg.defjam.online:27777", "5.223.88.150", "sa.defjam.online", "216.238.122.210:27777",
            "127.0.0.1:27900", "relay.example.org", "play.defjam.online:27900", "na.defjam.online.evil.org", "",
        };
        for (i = 0; i < (int)(sizeof(shown) / sizeof(shown[0])); i++) {
            char out[160];
            int ours = netservers_display(shown[i], out, sizeof(out));
            printf("shown|%s|%d|%s\n", shown[i], ours, out);
        }
    }
    printf("parse-null|%d\n", (int)netservers_parse_choice(NULL));
    /* build 91: the table */
    for (i = 0; i < NETSERVER_COUNT; i++) {
        const NetRelayDef *d = netservers_def(i);
        printf("def|%d|%s|%s|%s|%s|%s|%s|%s|%d|%d|%s\n", i, d->id, d->name, d->short_name, d->address, d->fallback_ip,
               d->env, d->social, netservers_index_of_id(d->id), netservers_index_of_social(d->social),
               netservers_known_ip("SA.defjam.online") ? netservers_known_ip("SA.defjam.online") : "-");
    }
    printf("def-out|%d|%d|%d|%d\n", netservers_def(-1) == NULL, netservers_def(NETSERVER_COUNT) == NULL,
           netservers_index_of_id("west"), netservers_known_ip("west.defjam.online") == NULL);
    printf("choice-out|%d|%d\n", netservers_choice_relay(NETCHOICE_END), netservers_choice_relay((enum NetServerChoice)99));
    {   /* the pings trailer: write four relays' pings, read them back for two rooms */
        unsigned char out[1 + 2 * (NETSERVER_MAX - NETSERVER_LEGACY)], two[1 + 2 * 2 * 3];
        int p4[NETSERVER_COUNT] = { 30, 120, 250, 70000 }, n, got[2 * NETSERVER_COUNT] = { 0 }, used;
        p4[NETSERVER_SA] = 186;
        n = netservers_put_more_pings(out, p4, NETSERVER_COUNT);
        printf("more|%d|%d|%d|%d\n", n, out[0], out[1], out[2]);
        p4[NETSERVER_SA] = 70000;
        n = netservers_put_more_pings(out, p4, NETSERVER_COUNT);
        printf("more-cap|%d|%d\n", n, out[1] | out[2] << 8);
        p4[NETSERVER_SA] = -5;
        n = netservers_put_more_pings(out, p4, NETSERVER_COUNT);
        printf("more-neg|%d|%d\n", n, out[1] | out[2] << 8);
        n = netservers_put_more_pings(out, p4, 3);
        printf("more-three|%d|%d\n", n, out[0]);
        /* a relay that knows three more relays than this build: E 3, two rooms (room 0: 186, 1, 2; room 1: 77, 0, 9) */
        two[0] = 3;
        two[1] = 186; two[2] = 0; two[3] = 1; two[4] = 0; two[5] = 2; two[6] = 0;
        two[7] = 77; two[8] = 0; two[9] = 0; two[10] = 0; two[11] = 9; two[12] = 0;
        used = netservers_get_more_pings(two, sizeof(two), 2, got);
        printf("more-read|%d|%d|%d|%d|%d\n", used, got[NETSERVER_SA], got[NETSERVER_COUNT + NETSERVER_SA], got[0],
               got[NETSERVER_COUNT]);
        printf("more-short|%d|%d|%d\n", netservers_get_more_pings(two, 6, 2, got), netservers_get_more_pings(two, 0, 2, got),
               netservers_get_more_pings(NULL, 5, 2, got));
    }
    for (i = 0; i < NETSERVER_COUNT; i++)
        printf("relay|%d|%d|%d\n", i, (int)netservers_relay_choice(i), netservers_partner(i));
    /* the scope: every choice, every relay, the seconds around each step, all up and US West down */
    {
        static const enum NetServerChoice cs[] = { NETCHOICE_AUTO, NETCHOICE_EU, NETCHOICE_NA, NETCHOICE_WEST,
                                                   NETCHOICE_ALL, NETCHOICE_CUSTOM, NETCHOICE_SA };
        static const int secs[] = { 0, 9, 10, 29, 30, 300 };
        unsigned ups[] = { 7u, 3u, 15u };
        int c, u;
        for (c = 0; c < 7; c++)
            for (u = 0; u < 3; u++)
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
        {   /* build 91: me on sa (3), them on eu: sums; a build-90 player there (no sa ping); me without a ping to eu */
            static const int sa_cases[][2][NETSERVER_COUNT] = {
                { { 200, 150, 300, 20 }, { 30, 110, 250, 190 } },  /* eu 230 > sa 210: they come */
                { { 120, 150, 300, 20 }, { 30, 110, 250, 220 } },  /* eu 150 < sa 240: I go */
                { { 120, 150, 300, 20 }, { 30, 110, 250, 0 } },    /* they know no sa (build 90): I go */
                { { 0, 150, 300, 20 }, { 30, 110, 250, 190 } },    /* I have no eu ping: I stay, they come */
                { { 0, 150, 300, 20 }, { 30, 110, 250, 0 } },      /* neither can reach the other: the ages */
            };
            for (c = 0; c < (int)(sizeof(sa_cases) / sizeof(sa_cases[0])); c++)
                printf("sumsa|%d|%d|%d\n", c, netservers_sum_move(NETSERVER_SA, 5, sa_cases[c][0], 0, 5, sa_cases[c][1]),
                       netservers_sum_move(0, 5, sa_cases[c][1], NETSERVER_SA, 5, sa_cases[c][0]));
        }
        printf("sum-null|%d\n", netservers_sum_move(0, 0, NULL, 1, 0, cases[0][1]));
    }
    return 0;
}
'''

EU, NA, WEST, SA = 0, 1, 2, 3
AUTO, CH_EU, CH_NA, CUSTOM, ALL, CH_WEST, CH_SA = 0, 1, 2, 3, 4, 5, 6


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
        # build 91: South America by its id, its words and its host name
        'sa': (CH_SA, 'sa', SA), 'SA': (CH_SA, 'sa', SA), 'southamerica': (CH_SA, 'sa', SA),
        'South America': (CH_SA, 'sa', SA), 'brazil': (CH_SA, 'sa', SA), 'Brasil': (CH_SA, 'sa', SA),
        'sao paulo': (CH_SA, 'sa', SA), 'sa.defjam.online': (CH_SA, 'sa', SA),
        'sa.defjam.online:27777': (CH_SA, 'sa', SA), 'SA.DEFJAM.ONLINE:27777': (CH_SA, 'sa', SA),
        # build 91: North America (Miami) and Asia on their new machines; the old host names still name them
        'na2.defjam.online': (CH_NA, 'na', NA), 'sg2.defjam.online:27777': (CH_WEST, 'sg', WEST),
        'Miami': (CH_NA, 'na', NA),
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
    assert relay == {EU: (CH_EU, -1), NA: (CH_NA, -1), WEST: (CH_WEST, -1), SA: (CH_SA, -1)}


def test_the_relay_table(rows):
    """Build 91: one table names every relay, in the wire's order; ids, chat regions and host names find them."""
    defs = {int(r[0]): r[1:] for r in by(rows, 'def')}
    assert [defs[i][0] for i in sorted(defs)] == ['eu', 'na', 'sg', 'sa']
    assert defs[SA][:7] == ['sa', 'South America', 'S. AMERICA', 'sa.defjam.online:27777', '216.238.122.210',
                            'RECOMP_NETPLAY_SERVER_SA', 'sa']
    assert defs[WEST][6] == 'sg' and defs[NA][6] == 'us-east' and defs[EU][3] == 'play.defjam.online:27777'
    # build 91: the new machines; their fallback addresses are placeholders until they are rented
    assert defs[NA][1:5] == ['North America (Miami)', 'N. AMERICA', 'na2.defjam.online:27777', '']
    assert defs[WEST][3:5] == ['sg2.defjam.online:27777', '']
    for i, d in defs.items():
        assert (int(d[7]), int(d[8])) == (i, i)     # by id, by chat region
        assert d[9] == '216.238.122.210'           # the fallback address of a host name, any case
    assert by(rows, 'def-out') == [['1', '1', '-1', '1']]
    assert by(rows, 'choice-out') == [['-1', '-1']]


def test_pings_trailer(rows):
    """Build 91 (CAPS bit 28): the pings beyond the first three relays as a count and u16 each."""
    assert by(rows, 'more') == [['3', '1', '186', '0']]
    assert by(rows, 'more-cap') == [['3', '65535']] and by(rows, 'more-neg') == [['3', '0']]
    assert by(rows, 'more-three') == [['1', '0']]
    # a relay that knows more relays than this build: the known one is read, the rest skipped
    assert by(rows, 'more-read') == [['13', '186', '77', '0', '0']]
    assert by(rows, 'more-short') == [['-1', '0', '0']]


def test_south_america_moves(rows):
    """Build 91: a quick match between South America and Europe: the smaller sum, and a player who cannot reach
    the other's relay (a build 90 game knows no South America and never sees its rooms) stays while the other moves."""
    got = {int(r[0]): (int(r[1]), int(r[2])) for r in by(rows, 'sumsa')}
    assert got == {0: (0, 1), 1: (1, 0), 2: (1, 0), 3: (0, 1), 4: (-1, -1)}


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
        # build 91: South America answering joins every search from 30 s, its own player's too
        assert scope_of(rows, choice, 15, EU, 30) == bits(EU, NA, WEST, SA)
        assert [scope_of(rows, choice, 15, SA, s) for s in (0, 29, 30)] == [bits(SA)] * 2 + [bits(EU, NA, WEST, SA)]
    # a fixed choice stays on its relay for 30 s, then every relay
    for choice, relay in ((CH_EU, EU), (CH_NA, NA), (CH_WEST, WEST), (CH_SA, SA)):
        assert [scope_of(rows, choice, 7, relay, s) for s in (0, 10, 29)] == [bits(relay)] * 3
        assert scope_of(rows, choice, 7, relay, 30) == bits(EU, NA, WEST) | bits(relay)
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
        'sa.defjam.online': 'the South America server', '216.238.122.210:27777': 'the South America server',
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
