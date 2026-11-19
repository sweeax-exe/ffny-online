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
#include <string.h>
#include "netplay_servers_rules.h"

static const char *const k_texts[] = {
    "", "auto", "AUTO", " Automatic ", "play.defjam.online", "play.defjam.online:27777",
    "eu", "Europe", "na", "NA", "north_america", "us", "na.defjam.online", "na.defjam.online:27777", "US East",
    "sg", "SG", "singapore", "sg.defjam.online", "sg.defjam.online:27777",
    "west", "West", "us_west", "west.defjam.online", "west.defjam.online:27777",
    "sa", "SA", "southamerica", "South America", "brazil", "Brasil", "sao paulo", "sa.defjam.online",
    "sa.defjam.online:27777", "SA.DEFJAM.ONLINE:27777", "na2.defjam.online", "sg2.defjam.online:27777", "Miami", "mia", "nj.defjam.online",
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
                                                   NETCHOICE_ALL, NETCHOICE_CUSTOM, NETCHOICE_SA, NETCHOICE_MIA };
        static const int secs[] = { 0, 9, 10, 29, 30, 300 };
        unsigned ups[] = { 7u, 3u, 15u };
        int c, u;
        for (c = 0; c < 8; c++)
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
    /* build 91: the global pool. Six players on six relays, all joining at second 0; pings by relay
     * (eu, na, sg, sa, mia, dfw, lax). Every second the unpaired ones are pooled; a pair leaves the pool. */
    {
        static const int home[6] = { 0, 1, 2, 3, 4, 5 };
        static const int pings[6][NETSERVER_COUNT] = {
            { 20, 95, 170, 210, 120, 130, 160 },    /* Germany */
            { 90, 10, 230, 130, 30, 40, 70 },       /* New York */
            { 170, 230, 8, 330, 240, 190, 170 },    /* Singapore */
            { 200, 120, 330, 15, 110, 140, 170 },   /* Brazil */
            { 110, 30, 240, 110, 8, 30, 60 },       /* Miami */
            { 130, 40, 190, 140, 30, 8, 30 },       /* Texas */
        };
        int done[6] = { 0 }, t, i2, j2;
        for (t = 0; t <= 60; t++) {
            NetQuickSeat seats[6];
            int idx[6], n = 0, partner[6], where[6];
            for (i2 = 0; i2 < 6; i2++) {
                if (done[i2]) continue;
                memset(&seats[n], 0, sizeof(seats[n]));
                seats[n].relay = home[i2];
                seats[n].age = t;
                memcpy(seats[n].pings, pings[i2], sizeof(seats[n].pings));
                snprintf(seats[n].code, sizeof(seats[n].code), "ROOM%d", i2);
                idx[n++] = i2;
            }
            netservers_pool(seats, n, partner, where);
            for (j2 = 0; j2 < n; j2++)
                if (partner[j2] >= 0 && idx[j2] < idx[partner[j2]]) {
                    printf("sim|%d|%d|%d|%d\n", t, idx[j2], idx[partner[j2]], where[j2]);
                    done[idx[j2]] = done[idx[partner[j2]]] = 1;
                }
        }
    }
    {   /* two players in Texas, one on Los Angeles and one on Miami: they play on Dallas; one of them with a
         * Brazilian: Miami, the smallest sum within the limit; the order of the seats
         * does not matter (both games build the list in their own order) */
        NetQuickSeat sa, sb, sc, list1[3], list2[3];
        int partner[3], where[3], sum = 0, worse = 0, r;
        memset(&sa, 0, sizeof(sa)); memset(&sb, 0, sizeof(sb)); memset(&sc, 0, sizeof(sc));
        sa.relay = NETSERVER_LAX; sb.relay = NETSERVER_MIA; sc.relay = NETSERVER_SA;
        {
            int pa[NETSERVER_COUNT] = { 130, 45, 190, 140, 32, 9, 31 }, pb[NETSERVER_COUNT] = { 128, 41, 195, 139, 29, 7, 35 };
            int pc[NETSERVER_COUNT] = { 200, 120, 330, 15, 110, 140, 170 };
            memcpy(sa.pings, pa, sizeof(pa)); memcpy(sb.pings, pb, sizeof(pb)); memcpy(sc.pings, pc, sizeof(pc));
        }
        snprintf(sa.code, sizeof(sa.code), "AAAAAA"); snprintf(sb.code, sizeof(sb.code), "BBBBBB");
        snprintf(sc.code, sizeof(sc.code), "CCCCCC");
        r = netservers_pair_relay(&sa, &sb, 120, &sum, &worse);
        printf("near|%d|%d|%d\n", r, sum, worse);
        printf("far|%d|%d\n", netservers_pair_relay(&sa, &sc, 120, NULL, NULL), netservers_pair_relay(&sa, &sc, 250, NULL, NULL));
        printf("limit|%d|%d|%d|%d|%d\n", netservers_pool_limit(0), netservers_pool_limit(19), netservers_pool_limit(20),
               netservers_pool_limit(44), netservers_pool_limit(45));
        {   /* build 97: a seat's own max ping never widens: 140 ms apart, one at 100 ms, the other at 150 */
            NetQuickSeat x = sa, y = sb;
            int r100, r150;
            memset(x.pings, 0, sizeof(x.pings)); memset(y.pings, 0, sizeof(y.pings));
            x.pings[NETSERVER_DFW] = 70; y.pings[NETSERVER_DFW] = 70;
            x.max_ping = 100;
            r100 = netservers_pair_relay(&x, &y, 0, NULL, NULL);
            x.max_ping = 150;
            r150 = netservers_pair_relay(&x, &y, 0, NULL, NULL);
            y.max_ping = 120;
            printf("own|%d|%d|%d\n", r100, r150, netservers_pair_relay(&x, &y, 0, NULL, NULL));
        }
        list1[0] = sa; list1[1] = sb; list1[2] = sc;
        list2[0] = sc; list2[1] = sb; list2[2] = sa;
        netservers_pool(list1, 3, partner, where);
        printf("order1|%d|%d|%d\n", partner[0], partner[1], where[0]);
        netservers_pool(list2, 3, partner, where);
        printf("order2|%d|%d|%d\n", partner[2], partner[1], where[2]);
        /* two players on one relay are never paired by the pool (the relay joins them) */
        sb.relay = NETSERVER_LAX;
        list1[1] = sb;
        netservers_pool(list1, 2, partner, where);
        printf("same|%d|%d\n", partner[0], partner[1]);
    }
    /* build 94: two games decide, each from its own view. Each reads its own room's age exactly and the other's a
     * little off (-2..+2 s: the lists come from two relays at different moments); B's room may have waited a while
     * when A's search starts. Europe player A, US East player B: within 180 ms only US East works (A moves), within
     * 250 Europe has the smaller sum (B moves): A arriving when B's room is about 20 s old is the step case. */
    {
        static const int pa[NETSERVER_COUNT] = { 20, 170 }, pb[NETSERVER_COUNT] = { 200, 70 };
        static const int eq_a[NETSERVER_COUNT] = { 30, 90, 0, 0, 60 }, eq_b[NETSERVER_COUNT] = { 90, 30, 0, 0, 60 };
        int scen;
        for (scen = 0; scen < 3; scen++) {
            /* 0: the step case above; 1: equal pings both ways (simultaneous search, equal ages), a third relay
             * (Miami) has the smallest sum; 2: both on one relay (the relay joins them, the pool never) */
            int old_swap = 0, new_bad = 0, latest = -1, never = 0, ea, eb, t, d;
            for (d = 0; d <= 30; d++)       /* B's room waited d s when A's search starts */
            for (ea = -2; ea <= 2; ea++)
                for (eb = -2; eb <= 2; eb++) {
                    int decided = -1;
                    for (t = 0; t <= 60 && decided < 0; t++) {
                        NetQuickSeat va[2], vb[2];
                        int partner[2], where[2], k, oa, ob, na, nb, mate, ra, rb;
                        for (k = 0; k < 2; k++) { memset(&va[k], 0, sizeof(va[k])); memset(&vb[k], 0, sizeof(vb[k])); }
                        ra = 0; rb = scen == 2 ? 0 : 1;
                        va[0].relay = vb[0].relay = ra; va[1].relay = vb[1].relay = rb;
                        memcpy(va[0].pings, scen == 0 ? pa : eq_a, sizeof(pa));
                        memcpy(va[1].pings, scen == 0 ? pb : eq_b, sizeof(pb));
                        memcpy(vb[0].pings, va[0].pings, sizeof(pa)); memcpy(vb[1].pings, va[1].pings, sizeof(pb));
                        snprintf(va[0].code, sizeof(va[0].code), "AAAAAA"); snprintf(va[1].code, sizeof(va[1].code), "BBBBBB");
                        memcpy(vb[0].code, va[0].code, sizeof(va[0].code)); memcpy(vb[1].code, va[1].code, sizeof(va[1].code));
                        va[0].age = t; va[1].age = t + d + ea < 0 ? 0 : t + d + ea;     /* A's view */
                        vb[1].age = t + d; vb[0].age = t + eb < 0 ? 0 : t + eb;         /* B's view */
                        netservers_pool(va, 2, partner, where);
                        oa = partner[0] >= 0 ? where[0] : -1;
                        netservers_pool(vb, 2, partner, where);
                        ob = partner[1] >= 0 ? where[1] : -1;
                        old_swap += oa == rb && ob == ra && ra != rb;
                        na = netservers_pool_steady(va, 2, 0, &mate);
                        nb = netservers_pool_steady(vb, 2, 1, &mate);
                        if (na >= 0 && na != ra && nb >= 0 && nb != rb && na != nb) new_bad++;   /* both go, apart */
                        if (na == rb && nb == ra && ra != rb) new_bad++;
                        if ((na >= 0 && na != ra) || (nb >= 0 && nb != rb)) decided = t;
                    }
                    if (decided < 0) never++;
                    else if (decided > latest) latest = decided;
                }
            printf("views|%d|%d|%d|%d|%d\n", scen, old_swap, new_bad, latest, never);
        }
    }
    return 0;
}
'''

EU, NA, WEST, SA, MIA, DFW, LAX = 0, 1, 2, 3, 4, 5, 6
AUTO, CH_EU, CH_NA, CUSTOM, ALL, CH_WEST, CH_SA, CH_MIA, CH_DFW, CH_LAX = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9


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
        # US West (Hillsboro) closed in build 89; build 91's Los Angeles takes its words
        'west': (CH_LAX, 'lax', LAX), 'West': (CH_LAX, 'lax', LAX), 'us_west': (CH_LAX, 'lax', LAX),
        'west.defjam.online': (CH_LAX, 'lax', LAX), 'west.defjam.online:27777': (CH_LAX, 'lax', LAX),
        # build 91: South America by its id, its words and its host name
        'sa': (CH_SA, 'sa', SA), 'SA': (CH_SA, 'sa', SA), 'southamerica': (CH_SA, 'sa', SA),
        'South America': (CH_SA, 'sa', SA), 'brazil': (CH_SA, 'sa', SA), 'Brasil': (CH_SA, 'sa', SA),
        'sao paulo': (CH_SA, 'sa', SA), 'sa.defjam.online': (CH_SA, 'sa', SA),
        'sa.defjam.online:27777': (CH_SA, 'sa', SA), 'SA.DEFJAM.ONLINE:27777': (CH_SA, 'sa', SA),
        # build 91: North America (Miami) and Asia on their new machines; the old host names still name them
        'na2.defjam.online': (CH_MIA, 'mia', MIA), 'sg2.defjam.online:27777': (CH_WEST, 'sg', WEST),
        'Miami': (CH_MIA, 'mia', MIA), 'mia': (CH_MIA, 'mia', MIA), 'nj.defjam.online': (CH_NA, 'na', NA),
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
    assert relay == {EU: (CH_EU, -1), NA: (CH_NA, -1), WEST: (CH_WEST, -1), SA: (CH_SA, -1), MIA: (CH_MIA, -1),
                     DFW: (CH_DFW, -1), LAX: (CH_LAX, -1)}


def test_the_relay_table(rows):
    """Build 91: one table names every relay, in the wire's order; ids, chat regions and host names find them."""
    defs = {int(r[0]): r[1:] for r in by(rows, 'def')}
    assert [defs[i][0] for i in sorted(defs)] == ['eu', 'na', 'sg', 'sa', 'mia', 'dfw', 'lax']
    assert defs[DFW][2:5] == ['DALLAS', 'dfw.defjam.online:27777', '45.76.237.224']
    assert defs[LAX][2:5] == ['LA', 'lax.defjam.online:27777', '45.32.95.226']
    assert defs[SA][:7] == ['sa', 'South America', 'S.AMERICA', 'sa.defjam.online:27777', '216.238.122.210',
                            'RECOMP_NETPLAY_SERVER_SA', 'sa']
    assert defs[WEST][6] == 'sg' and defs[NA][6] == 'us-east' and defs[EU][3] == 'play.defjam.online:27777'
    # build 91: the new machines and their fallback addresses
    assert defs[NA][1:5] == ['US East', 'US EAST', 'nj.defjam.online:27777', '208.167.239.167']
    assert defs[MIA][:7] == ['mia', 'US South (Miami)', 'MIAMI', 'na2.defjam.online:27777', '140.82.31.133',
                             'RECOMP_NETPLAY_SERVER_MIA', 'mia']
    assert defs[WEST][3:5] == ['sg2.defjam.online:27777', '139.180.129.88']
    for i, d in defs.items():
        assert (int(d[7]), int(d[8])) == (i, i)     # by id, by chat region
        assert d[9] == '216.238.122.210'           # the fallback address of a host name, any case
    assert by(rows, 'def-out') == [['1', '1', '-1', '1']]
    assert by(rows, 'choice-out') == [['-1', '-1']]


def test_pings_trailer(rows):
    """Build 91 (CAPS bit 28): the pings beyond the first three relays as a count and u16 each."""
    assert by(rows, 'more') == [['9', '4', '186', '0']]
    assert by(rows, 'more-cap') == [['9', '65535']] and by(rows, 'more-neg') == [['9', '0']]
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


def test_search_is_global_from_the_first_second(rows):
    """Build 91 (owner): the relay is where a match runs, never whom one meets: every answering relay from 0 s,
    whatever the choice."""
    for choice in (AUTO, ALL, CH_EU, CH_NA, CH_WEST, CH_SA, CH_MIA):
        for cur in (EU, NA, WEST, SA):
            for s in (0, 9, 10, 29, 30, 300):
                assert scope_of(rows, choice, 7, cur, s) == bits(EU, NA, WEST) | bits(cur)
                assert scope_of(rows, choice, 15, cur, s) == bits(EU, NA, WEST, SA)
    # a custom address, or no relay: no search beyond the room
    for s in (0, 30, 300):
        assert scope_of(rows, CUSTOM, 7, NA, s) == 0
        assert scope_of(rows, AUTO, 7, -1, s) == 0


def test_pool_matches_everyone_spread_over_six_relays(rows):
    """Six players on six different relays (Germany, New York, Singapore, Brazil, Miami, Texas), all searching from
    0 s: everyone is paired within the widening window (by 45 s any ping is allowed), nobody twice, and never
    with a ping sum above the limit of the moment (build 97: 120 ms, from 20 s 180 ms)."""
    sim = [tuple(map(int, r)) for r in by(rows, 'sim')]
    paired = [p for _, a, b, _ in sim for p in (a, b)]
    assert sorted(paired) == list(range(6)), sim
    assert all(t <= 45 for t, _, _, _ in sim), sim
    pings = [(20, 95, 170, 210, 120, 130, 160), (90, 10, 230, 130, 30, 40, 70), (170, 230, 8, 330, 240, 190, 170),
             (200, 120, 330, 15, 110, 140, 170), (110, 30, 240, 110, 8, 30, 60), (130, 40, 190, 140, 30, 8, 30)]
    for t, a, b, r in sim:
        limit = 120 if t < 20 else 180 if t < 45 else 10 ** 6
        assert pings[a][r] + pings[b][r] <= limit, (t, a, b, r)
    # the Americas pair at once near home; New York and Miami on a US relay, not in Europe
    first = {(a, b): (t, r) for t, a, b, r in sim}
    assert any(t == 0 for t, _, _, _ in sim)
    for (a, b), (t, r) in first.items():
        assert r in (EU, NA, WEST, SA, MIA, DFW, LAX)


def test_pool_near_relay_and_same_answer_in_any_order(rows):
    """Two players in Texas, one on Los Angeles and one on Miami, play on Dallas (a third relay, the smallest
    ping sum); with a Brazilian, Miami; both games, listing the seats in their own order, agree."""
    assert by(rows, 'near') == [[str(DFW), '16', '9']]
    # Texas and Brazil: Miami (the smallest sum, 142 ms): not within 120 ms (build 97: the sum is the pair's ping)
    assert by(rows, 'far') == [['-1', str(MIA)]]
    assert by(rows, 'limit') == [['120', '120', '180', '180', '0']]     # build 97: 120, from 20 s 180, from 45 s any
    # build 97: a player's own max ping holds at any wait (140 ms: over 100 and over 120, under 150)
    assert by(rows, 'own') == [['-1', str(DFW), '-1']]
    assert by(rows, 'order1') == [['1', '0', str(DFW)]]
    assert by(rows, 'order2') == [['1', '2', str(DFW)]]
    assert by(rows, 'same') == [['-1', '-1']]


def test_two_games_with_skewed_views_never_swap(rows):
    """Build 94: each game reads the other's room age from another relay's list, up to 2 s off. Near a limit step
    the pool alone could send each player to the other's relay at once (a swap: two new rooms, the waits back to
    0); netservers_pool_steady waits inside the slack, so the two never move apart, and they still meet."""
    views = {int(r[0]): tuple(map(int, r[1:])) for r in by(rows, 'views')}
    old_swap, new_bad, latest, never = views[0]
    # build 97: the pair's relay is the smallest sum whatever the stage (the stage only says whether they meet), so
    # the plain pool no longer swaps here either; the steady answer still never sends the two apart
    assert old_swap == 0
    assert new_bad == 0 and never == 0
    assert latest <= 45 + 2 + 3   # 220 ms apart: the last step (any ping), the skew and the slack
    # equal pings both ways (a simultaneous search: equal ages): both go to Miami, the smallest sum, at once
    old_swap, new_bad, latest, never = views[1]
    assert (old_swap, new_bad, latest, never) == (0, 0, 0, 0)
    # both on one relay: the pool never pairs them (the relay's QUICK puts them in one room)
    old_swap, new_bad, latest, never = views[2]
    assert (old_swap, new_bad, latest, never) == (0, 0, -1, 31 * 25)


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
