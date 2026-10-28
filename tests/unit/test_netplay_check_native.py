"""The lockstep checksum message (src/hooks/netplay_check.h, P_HASH in netplay.c), compiled natively:
a full message round-trips, the two older lengths (13 bytes before build 23, 37 bytes for builds 23..33)
are read as far as they go, a newer build's message keeps the old offsets an older build reads, and the
desync report names the differing raw words. Needs MSVC (`cl` on PATH); skips without it."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')

DRIVER = r'''
#include <stdio.h>
#include "netplay_check.h"

static void fill(NetCheck *c, uint32_t base)
{
    int k, f;
    memset(c, 0, sizeof(*c));
    c->match = 0xCF0A906Fu; c->step = 120; c->hash = base;
    for (k = 0; k < NETCHECK_PARTS; k++) c->parts[k] = base + 0x100u + (uint32_t)k;
    for (k = 0; k < NETCHECK_SLOTS; k++)
        for (f = 0; f < NETCHECK_FIELDS; f++) c->fields[k][f] = base + 0x1000u * (uint32_t)k + (uint32_t)f;
    c->gens[0] = base + 0x77u; c->gens[1] = base + 0x88u; c->crowd = base + 0x99u;
    c->has_parts = c->has_extra = 1;
}

int main(void)
{
    uint8_t buf[256];
    NetCheck a, b, c;
    char text[600];
    int n, k, f, same;

    printf("lengths %d %d %d extra %d\n", NETCHECK_LEN_OLD, NETCHECK_LEN_PARTS, NETCHECK_LEN_FULL, NETCHECK_EXTRA);

    /* a full message round-trips */
    fill(&a, 0x11110000u);
    n = netcheck_pack(buf, sizeof(buf), 5, &a);
    netcheck_unpack(buf, n, &b);
    same = b.match == a.match && b.step == a.step && b.hash == a.hash && b.has_parts && b.has_extra &&
           !memcmp(b.parts, a.parts, sizeof(a.parts)) && !memcmp(b.fields, a.fields, sizeof(a.fields)) &&
           !memcmp(b.gens, a.gens, sizeof(a.gens)) && b.crowd == a.crowd;
    printf("full len %d type %d roundtrip %d\n", n, buf[0], same);
    /* the old offsets an older build reads stay where they were */
    printf("old offsets %d\n", netcheck_get32(buf + 9) == a.hash && netcheck_get32(buf + 13) == a.parts[0] &&
                                netcheck_get32(buf + 33) == a.parts[5]);
    printf("too small %d\n", netcheck_pack(buf, NETCHECK_LEN_FULL - 1, 5, &a));

    /* a builds 23..33 message: the parts, no fields */
    netcheck_unpack(buf, NETCHECK_LEN_PARTS, &c);
    printf("len37 parts %d extra %d part5 %d field %08X crowd %08X\n", c.has_parts, c.has_extra,
           c.parts[5] == a.parts[5], c.fields[0][0], c.crowd);
    /* anything between 37 and 145 is read as a 37-byte one */
    netcheck_unpack(buf, NETCHECK_LEN_FULL - 1, &c);
    printf("len144 parts %d extra %d\n", c.has_parts, c.has_extra);
    /* a message from before build 23: the hash only */
    netcheck_unpack(buf, NETCHECK_LEN_OLD, &c);
    printf("len13 ok hash %d parts %d extra %d\n", c.hash == a.hash, c.has_parts, c.has_extra);
    printf("len12 %d\n", netcheck_unpack(buf, 12, &c));
    /* packing what an older peer sent gives its own length back */
    c.has_parts = 1; c.has_extra = 0;
    printf("pack37 %d\n", netcheck_pack(buf, sizeof(buf), 5, &c));
    c.has_parts = 0;
    printf("pack13 %d\n", netcheck_pack(buf, sizeof(buf), 5, &c));

    /* the report: fighter 1's x and fighter 2's action differ, a generator and the crowd too */
    fill(&a, 0x11110000u);
    b = a;
    {
        float x = 12.5f;
        memcpy(&b.fields[0][1], &x, 4);
        x = 3.25f;
        memcpy(&a.fields[0][1], &x, 4);
    }
    b.fields[1][0] ^= 1;
    b.fields[3][5] ^= 1;     /* slot 4: not in a two-fighter match */
    b.parts[0] ^= 1; b.parts[1] ^= 1;
    b.gens[0] ^= 1;
    b.crowd ^= 1;
    netcheck_fields_text(text, sizeof(text), &a, &b, 2);
    printf("fields: %s\n", text);
    netcheck_parts_text(text, sizeof(text), &a, &b, 2, 2);
    printf("parts: %s\n", text);
    /* equal fields, equal generators */
    netcheck_fields_text(text, sizeof(text), &a, &a, 2);
    printf("equal: %s\n", text);
    /* an older peer sent no fields: no fields line */
    b.has_extra = 0;
    printf("old peer %d\n", netcheck_fields_text(text, sizeof(text), &a, &b, 2));
    /* a long report stays inside its buffer */
    b = a;
    for (k = 0; k < NETCHECK_SLOTS; k++) for (f = 0; f < NETCHECK_FIELDS; f++) b.fields[k][f] ^= 0x80000000u;
    n = netcheck_fields_text(text, 64, &a, &b, 4);
    printf("bounded %d %d\n", n, (int)strlen(text));
    printf("fnv %08X\n", netcheck_fnv_words(a.parts, 0));
    return 0;
}
'''


@pytest.fixture(scope='module')
def output(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native checksum test needs MSVC')
    folder = tmp_path_factory.mktemp('netcheck')
    driver = folder / 'driver.c'
    driver.write_text(DRIVER, encoding='utf-8')
    exe = folder / 'netcheck.exe'
    r = subprocess.run([compiler, '/nologo', '/W3', '/TC', '/I' + str(ROOT / 'src/hooks'), str(driver), '/Fe:' + str(exe)],
                       cwd=folder, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    return run.stdout


def line(out, key):
    m = re.search(r'^%s(.*)$' % re.escape(key), out, re.M)
    assert m, key
    return m.group(1).strip()


def test_lengths(output):
    assert line(output, 'lengths') == '13 37 145 extra 27'


def test_full_message_round_trips_and_keeps_the_old_offsets(output):
    assert line(output, 'full len') == '145 type 5 roundtrip 1'
    assert line(output, 'old offsets') == '1'
    assert line(output, 'too small') == '0'


def test_older_lengths_are_read_as_far_as_they_go(output):
    assert line(output, 'len37') == 'parts 1 extra 0 part5 1 field 00000000 crowd 00000000'
    assert line(output, 'len144') == 'parts 1 extra 0'
    assert line(output, 'len13 ok') == 'hash 1 parts 0 extra 0'
    assert line(output, 'len12') == '0'
    assert line(output, 'pack37') == '37'
    assert line(output, 'pack13') == '13'


def test_report_names_the_differing_words(output):
    fields = line(output, 'fields:')
    assert fields.startswith('fighter 1 0x44 here 40500000 there 41480000 (3.25 / 12.5), fighter 2 0x30 here ')
    assert 'fighter 4' not in fields                    # two fighters: slot 4 is not compared
    assert '; pre-fight generators differ (00376FDC here ' in fields
    assert '00375F68' not in fields
    assert '; crowd differ (here ' in fields
    assert line(output, 'parts:') == ("fighter 1 (player 1), fighter 2 (player 2); the CPU fighters' generators equal; "
                                      "the shared generator and time scale equal")
    assert line(output, 'equal:') == 'no fighter field differs; pre-fight generators equal; crowd equal'
    assert line(output, 'old peer') == '0'
    n, length = line(output, 'bounded').split()
    assert n == length and int(n) <= 63
    assert line(output, 'fnv') == '811C9DC5'


def test_netplay_sends_and_reads_the_full_message():
    # the sender packs every part (the whole 145 bytes), the receiver unpacks by length, and only the
    # hash decides a desync: the extra words are diagnostic
    assert 'uint8_t p[NETCHECK_LEN_FULL];' in NETPLAY_C
    assert 'netcheck_pack(p, (int)sizeof(p), P_HASH, &c)' in NETPLAY_C
    assert 'netcheck_unpack(p, len, &in)' in NETPLAY_C
    assert 'if (N.local_hash[i].hash != N.remote_hash[j].hash)' in NETPLAY_C
    assert '[NETPLAY] desync fields: %s' in NETPLAY_C
