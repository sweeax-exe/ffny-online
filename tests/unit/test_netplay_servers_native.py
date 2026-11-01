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
        'west': (CH_WEST, 'west', WEST), 'West': (CH_WEST, 'west', WEST), 'us_west': (CH_WEST, 'west', WEST),
        'west.defjam.online': (CH_WEST, 'west', WEST), 'west.defjam.online:27777': (CH_WEST, 'west', WEST),
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
    assert relay == {EU: (CH_EU, -1), NA: (CH_NA, WEST), WEST: (CH_WEST, NA)}


def scope_of(rows, choice, up, cur, secs):
    for r in by(rows, 'scope'):
        if (int(r[0]), int(r[1]), int(r[2]), int(r[3])) == (choice, up, cur, secs):
            return int(r[4])
    raise KeyError((choice, up, cur, secs))


def bits(*relays):
    return sum(1 << r for r in relays)


def test_search_widens_on_time(rows):
    for choice in (AUTO, ALL):
        # US East: alone 10 s, then with US West, every relay from 30 s
        assert [scope_of(rows, choice, 7, NA, s) for s in (0, 9, 10, 29, 30, 300)] == \
            [bits(NA)] * 2 + [bits(NA, WEST)] * 2 + [bits(EU, NA, WEST)] * 2
        assert [scope_of(rows, choice, 7, WEST, s) for s in (0, 9, 10, 29, 30)] == \
            [bits(WEST)] * 2 + [bits(NA, WEST)] * 2 + [bits(EU, NA, WEST)]
        # Europe has no partner: alone until 30 s
        assert [scope_of(rows, choice, 7, EU, s) for s in (0, 10, 29, 30)] == [bits(EU)] * 3 + [bits(EU, NA, WEST)]
        # a partner that does not answer is not searched; at 30 s every relay that answers
        assert scope_of(rows, choice, 3, NA, 10) == bits(NA)
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
