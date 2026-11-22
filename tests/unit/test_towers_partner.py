"""The Team Tower's player 2 sign-in (build 108, src/hooks/towers_partner.c; server relay/accounts/pair.go).

The native part runs the module against a scripted account service (account_call stubbed): the code and page from
/auth/pair/start, the poll's waiting and done answers, player 2's confirm, "not me", a token the service no longer
takes, a failed start, and player 1 signed out. The source checks: the partner token never reaches a log or a file,
the team start sends it, the start card and the sign-in card are wired. Needs MSVC (`cl` on PATH)."""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

TOKEN = 'ab' * 32

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "account.h"
#include "towers_partner.h"

static int g_in = ACCOUNT_IN, g_polls, g_ends, g_start_status = 200, g_done_after = 2;
static char g_last_end[200];

enum AccountState account_state(void) { return (enum AccountState)g_in; }
int account_pair_secret(char secret[65], char login[65])
{
    memset(secret, 'c', 64); secret[64] = 0;
    memset(login, 'd', 64); login[64] = 0;
    return 1;
}
int account_call(const char *method, const char *path, const char *json, char *buf, int size, int receive_ms,
                 void *volatile *live)
{
    (void)method; (void)receive_ms; (void)live;
    if (!strncmp(path, "/auth/pair/start", 16)) {
        if (g_start_status != 200) { snprintf(buf, size, "ok=0\nerror=Too many tries. Wait 5 minutes.\n"); return g_start_status; }
        snprintf(buf, size, "ok=1\ncode=K7Q-2MX\nurl=https://ffny.example/pair\nexpires=600\n");
        return 200;
    }
    if (!strncmp(path, "/auth/pair/poll", 15)) {
        if (++g_polls < g_done_after) { snprintf(buf, size, "ok=1\nstatus=waiting\n"); return 202; }
        snprintf(buf, size, "ok=1\nstatus=done\ntoken=%s\naccount=7\nname=Masa\nhours=12\n", "TOKEN");
        return 200;
    }
    if (!strncmp(path, "/auth/pair/end", 14)) { g_ends++; snprintf(g_last_end, sizeof g_last_end, "%s", json); snprintf(buf, size, "ok=1\n"); return 200; }
    return 404;
}

static void wait_state(int want, int ms)
{
    PartnerView v;
    int t;
    for (t = 0; t < ms; t += 50) { towers_partner_view(&v); if (v.state == want) return; Sleep(50); }
}

static void show(const char *step)
{
    PartnerView v;
    char tok[100], name[64];
    int has;
    towers_partner_view(&v);
    has = towers_partner_token(tok, sizeof tok, name, sizeof name);
    printf("%s|%d|%s|%s|%s|%s|%d|%s|%s|%d|%d\n", step, v.state, v.code, v.url, v.name, v.why, has, has ? tok : "", name,
           g_polls, g_ends);
}

int main(void)
{
    char tok[100];
    show("idle");
    towers_partner_begin();
    wait_state(PARTNER_WAITING, 3000);
    show("waiting");
    wait_state(PARTNER_CONFIRM, 9000);
    show("confirm");
    printf("confirm-ok|%d\n", towers_partner_confirm());
    show("signed");
    printf("token-size|%d\n", towers_partner_token(tok, 10, NULL, 0));
    towers_partner_started(1, "Masa");
    show("started");
    towers_partner_started(1, "");
    {
        int d1 = towers_partner_dropped(), d2 = towers_partner_dropped();     /* once, then no more */
        printf("dropped|%d|%d\n", d1, d2);
    }
    show("after-drop");
    /* "not me" */
    g_polls = 0;
    towers_partner_begin();
    wait_state(PARTNER_CONFIRM, 9000);
    towers_partner_reject();
    Sleep(300);
    show("rejected");
    printf("end-body|%s\n", g_last_end);
    /* B while waiting: back to the guest */
    g_polls = 0; g_done_after = 1000;
    towers_partner_begin();
    wait_state(PARTNER_WAITING, 3000);
    towers_partner_cancel();
    show("cancelled");
    /* a refused start */
    g_start_status = 429;
    towers_partner_begin();
    wait_state(PARTNER_FAILED, 3000);
    show("failed");
    /* player 1 signed out */
    g_in = ACCOUNT_OUT;
    printf("signed-out|%d\n", towers_partner_begin());
    return 0;
}
'''.replace('"TOKEN"', '"' + TOKEN + '"')


@pytest.fixture(scope='module')
def rows():
    if not shutil.which('cl'):
        pytest.skip('MSVC cl not on PATH')
    with tempfile.TemporaryDirectory() as d:
        drv = Path(d) / 'drv.c'
        drv.write_text(DRIVER)
        exe = Path(d) / 'drv.exe'
        subprocess.run(['cl', '/nologo', '/W3', '/I', str(HOOKS), '/I', str(ROOT / 'src'), str(drv),
                        str(HOOKS / 'towers_partner.c'), '/Fe' + str(exe), '/Fo' + d + '\\'], check=True,
                       capture_output=True, cwd=d)
        run = subprocess.run([str(exe)], check=True, capture_output=True, text=True, encoding='utf-8', timeout=120)
    out = {}
    for line in run.stdout.splitlines():
        p = line.split('|')
        out[p[0]] = p[1:]
    out['_stderr'] = run.stderr
    return out


def test_the_sign_in_flow(rows):
    NONE, STARTING, WAITING, CONFIRM, SIGNED, FAILED = range(6)
    assert rows['idle'][0] == str(NONE) and rows['idle'][5] == '0'
    w = rows['waiting']
    assert w[0] == str(WAITING) and w[1] == 'K7Q-2MX' and w[2] == 'https://ffny.example/pair' and w[5] == '0'
    c = rows['confirm']
    assert c[0] == str(CONFIRM) and c[3] == 'Masa' and c[5] == '0'      # no token before player 2's confirm
    assert rows['confirm-ok'] == ['1']
    s = rows['signed']
    assert s[0] == str(SIGNED) and s[5] == '1' and s[6] == TOKEN and s[7] == 'Masa'
    assert rows['token-size'] == ['0']                                   # never cut short
    assert rows['started'][0] == str(SIGNED)                             # the service took it
    assert rows['dropped'] == ['1', '0'] and rows['after-drop'][0] == str(NONE) and rows['after-drop'][5] == '0'


def test_not_me_cancel_failure_signed_out(rows):
    r = rows['rejected']
    assert r[0] == '0' and r[5] == '0' and int(r[9]) >= 1                # the token ends on the service
    assert rows['end-body'] == ['{"token":"%s"}' % TOKEN]
    assert rows['cancelled'][0] == '0'
    f = rows['failed']
    assert f[0] == '5' and 'Wait 5 minutes' in f[4]
    assert rows['signed-out'] == ['0']


def test_the_token_never_reaches_a_log(rows):
    assert TOKEN not in rows['_stderr']
    assert 'code K7Q-2MX' in rows['_stderr'] and 'player 2 is Masa' in rows['_stderr']
    src = (HOOKS / 'towers_partner.c').read_text(encoding='utf-8')
    for line in re.findall(r'fprintf\(stderr,[^;]*;', src, re.S):
        assert 'g_token' not in line and 'g_pending' not in line and 'secret' not in line.split(',', 1)[1], line
    assert 'fopen' not in src and 'recomp_settings_set' not in src       # memory only


def test_the_team_start_and_the_cards_are_wired():
    towers = (HOOKS / 'towers.c').read_text(encoding='utf-8')
    assert 'towers_partner_token(partner, (int)sizeof partner, partner_name, (int)sizeof partner_name)' in towers
    assert 'towers_partner_started(partner[0] != 0, rec);' in towers
    assert '"[TOWERS] new rewards for player 2: %s\\n"' in towers
    ui = (HOOKS / 'towers_ui.inc').read_text(encoding='utf-8')
    assert 'hint(dl, f, hx, 336, "y", "PLAYER 2 SIGN IN")' in ui
    assert 'towers_pair_card(dl, f, tower, pad);' in ui
    # player 2's confirm is player 2's own A (the second controller), "not me" is B on either
    assert 'bool p2a = !gallery_on() && tw_p2_edge(INPUT_SRC_SOUTH)' in ui
    assert 'if (p2a && towers_partner_confirm())' in ui and 'towers_partner_reject();' in ui
    gal = (HOOKS / 'online_screen.cpp').read_text(encoding='utf-8')
    for page in ('"towers-pair"', '"towers-pair-confirm"', '"towers-start-team"'):
        assert page in gal and page in ui
