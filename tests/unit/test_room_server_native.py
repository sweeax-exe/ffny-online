"""CREATE A ROOM's SERVER row (build 111, src/hooks/netplay_servers_rules.c netservers_room_*), compiled natively: the
saved text reads back as Automatic or one relay of the table (built from the table, so the relays added later need
no change here), the row steps through Automatic and every relay and over the ones that did not answer, and
Automatic's choice by the fresh pings keeps the relay the hub is on unless another is ROOMSERVER_MARGIN_MS faster.
Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1); skips without it."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

# commands on stdin, one per line; one answer line each
DRIVER = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "netplay_servers_rules.h"

int main(void)
{
    char line[512];
    while (fgets(line, sizeof(line), stdin)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (!strcmp(line, "table")) {
            int i;
            printf("%d|%d|%d", NETSERVER_COUNT, ROOMSERVER_AUTO, ROOMSERVER_MARGIN_MS);
            for (i = 0; i < NETSERVER_COUNT; i++) printf("|%s|%s", netservers_def(i)->id, netservers_def(i)->name);
            printf("\n");
        } else if (!strcmp(line, "parse-null")) {
            printf("%d\n", netservers_room_parse(NULL));
        } else if (!strncmp(line, "parse ", 6) || !strcmp(line, "parse")) {
            const char *t = line[5] ? line + 6 : "";
            int v = netservers_room_parse(t);
            printf("%d|%s|%d\n", v, netservers_room_text(v), netservers_room_relay(v));
        } else if (!strncmp(line, "text ", 5)) {
            int v = atoi(line + 5);
            printf("%s|%d\n", netservers_room_text(v), netservers_room_relay(v));
        } else if (!strncmp(line, "step ", 5)) {
            int v, s;
            unsigned skip;
            sscanf(line + 5, "%d %d %u", &v, &s, &skip);
            printf("%d\n", netservers_room_step(v, s, skip));
        } else if (!strncmp(line, "target ", 7)) {
            int v, cur, n, k, pings[64], used = 0, at = 7;
            sscanf(line + at, "%d %d %d%n", &v, &cur, &n, &used);
            at += used;
            for (k = 0; k < n && k < 64; k++) { sscanf(line + at, "%d%n", &pings[k], &used); at += used; }
            printf("%d\n", netservers_room_target(v, cur, n ? pings : NULL, n));
        } else {
            printf("?\n");
        }
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def run(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('room_server')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'room_server.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/netplay_servers_rules.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr

    def ask(*commands):
        out = subprocess.run([str(exe)], input='\n'.join(commands) + '\n', capture_output=True, text=True,
                             check=True).stdout.splitlines()
        assert len(out) == len(commands), out
        return out
    return ask


@pytest.fixture(scope='module')
def table(run):
    row = run('table')[0].split('|')
    count, auto, margin = int(row[0]), int(row[1]), int(row[2])
    ids = row[3::2]
    assert len(ids) == count
    return {'count': count, 'auto': auto, 'margin': margin, 'ids': ids, 'names': row[4::2]}


def test_table_shape(table):
    assert table['auto'] == 0
    assert table['count'] >= 7           # build 91's seven; build 111 adds more at the end
    assert table['ids'][:3] == ['eu', 'na', 'sg']
    assert 0 < table['margin'] <= 20


def test_saved_text(run, table):
    # Automatic for anything that is not one relay: the default, "all", a custom address, an unknown id
    for text in ['', 'auto', 'AUTO', ' Automatic ', 'all', 'both', '127.0.0.1:27900', 'relay.example.org', 'westeros']:
        v, back, relay = run('parse ' + text)[0].split('|')
        assert (int(v), back, int(relay)) == (0, 'auto', -1), text
    assert run('parse-null') == ['0']
    # every relay of the table by its id, upper case too, and back to the same id
    for i, rid in enumerate(table['ids']):
        for text in (rid, rid.upper()):
            v, back, relay = run('parse ' + text)[0].split('|')
            assert (int(v), back, int(relay)) == (1 + i, rid, i), text
    # a relay's other words name it as the old server setting did
    assert run('parse Europe')[0].split('|')[0] == '1'
    assert run('parse US East')[0].split('|')[0] == '2'
    assert run('parse singapore')[0].split('|')[0] == '3'
    # out of range is Automatic
    n = table['count']
    assert run('text -1', 'text %d' % (n + 1), 'text 99', 'text 0') == ['auto|-1'] * 4
    assert run('text %d' % n) == ['%s|%d' % (table['ids'][-1], n - 1)]


def test_step_goes_through_every_relay(run, table):
    n = table['count']
    # right from Automatic: every relay in table order, then Automatic again
    seen, v = [], 0
    for _ in range(n + 1):
        v = int(run('step %d 1 0' % v)[0])
        seen.append(v)
    assert seen == list(range(1, n + 1)) + [0]
    # left from Automatic: the last relay first
    seen, v = [], 0
    for _ in range(n + 1):
        v = int(run('step %d -1 0' % v)[0])
        seen.append(v)
    assert seen == list(range(n, 0, -1)) + [0]
    # no step, a value out of range
    assert run('step 3 0 0', 'step 99 0 0', 'step -4 1 0') == ['3', '0', '1']


def test_step_skips_relays_without_answer(run, table):
    n = table['count']
    skip = (1 << 1) | (1 << 2)          # US East and Asia did not answer
    assert run('step 1 1 %d' % skip) == ['4']        # Europe -> past US East and Asia
    assert run('step 4 -1 %d' % skip) == ['1']
    every = (1 << n) - 1                # nothing answers: only Automatic is left
    assert run('step 0 1 %d' % every, 'step 0 -1 %d' % every, 'step 2 1 %d' % every) == ['0', '0', '0']
    # the last relay without answer: right from the one before it wraps to Automatic
    assert run('step %d 1 %d' % (n - 1, 1 << (n - 1))) == ['0']


def target(run, value, cur, pings):
    return int(run('target %d %d %d %s' % (value, cur, len(pings), ' '.join(str(p) for p in pings)))[0])


def test_chosen_relay_whatever_the_pings(run, table):
    n = table['count']
    pings = [20] + [200] * (n - 1)
    for i in range(n):
        assert target(run, 1 + i, 0, pings) == i
        assert target(run, 1 + i, -1, [-1] * n) == i      # even without answers (the page refuses that itself)


def test_automatic_lowest_ping(run, table):
    n = table['count']
    m = table['margin']
    base = [-1] * n
    # the England report: the hub sat on US East (78 ms) because Europe answered slowly at Online start
    p = list(base); p[0] = 22; p[1] = 78
    assert target(run, 0, 1, p) == 0
    # the hub is on the fastest: it stays
    assert target(run, 0, 0, p) == 0
    # within the margin the hub's relay stays (no move for noise); one more ms and it moves
    p = list(base); p[0] = 30; p[1] = 30 + m
    assert target(run, 0, 1, p) == 1
    p[1] = 30 + m + 1
    assert target(run, 0, 1, p) == 0
    # a tie between two other relays: the lower index
    p = list(base); p[2] = 50; p[3] = 50; p[1] = 200
    assert target(run, 0, 1, p) == 2
    # the hub's relay did not answer the fresh round: the lowest ping
    p = list(base); p[1] = -1; p[n - 1] = 40; p[0] = 90
    assert target(run, 0, 1, p) == n - 1
    # nothing answered: where the hub is, or -1 with no hub
    assert target(run, 0, 3, base) == 3
    assert target(run, 0, -1, base) == -1
    # no hub on the table (a custom address): the lowest ping
    p = list(base); p[1] = 60; p[0] = 70
    assert target(run, 0, -2, p) == 1
    assert target(run, 0, n + 3, p) == 1
    # a new relay at the end of the table is chosen like any other
    p = [150] * n; p[n - 1] = 12
    assert target(run, 0, 0, p) == n - 1
