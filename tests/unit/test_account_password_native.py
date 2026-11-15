"""Sign-in without Discord on the game's side (src/hooks/account_pw.c), compiled natively: the JSON bodies the
account service's password routes read (server relay/accounts/password.go pwBody), the local checks that mirror
its rules, the flag reader for has_discord / has_password, and the PC fingerprint's hex form (the same string the
match reports carry, so the service's keyed PC hash matches). Needs MSVC (`cl` on PATH, e.g. after
scripts/env.ps1); skips without it."""
import json
import shutil
import subprocess

import pytest

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "account_pw.h"

static void body(const char *tag, const char *u, const char *p, const char *c, const char *pc)
{
    char out[512];
    int n = account_pw_json(out, sizeof(out), u, p, c, pc);
    printf("%s\t%d\t%s\n", tag, n, n >= 0 ? out : "");
}

static void check(const char *tag, const char *u, const char *p, const char *r)
{
    char why[200];
    int bad = account_pw_check_new(u, p, r, why, sizeof(why));
    printf("%s\t%d\t%s\n", tag, bad, why);
}

int main(void)
{
    char small[16], hex[33], out[256];
    unsigned char id[16];
    int i;
    body("plain", "Neo_Fighter", "correct horse", NULL, "00112233445566778899aabbccddeeff");
    body("escape", "a\"b", "p\\w\"d\n\x01", "AB12-CD34", "");
    body("reset", "user", "new pass", "K7QX-PM2D-9WRT", NULL);
    printf("small\t%d\n", account_pw_json(small, sizeof(small), "user", "long password", NULL, NULL));
    printf("change\t%d\t", account_pw_change_json(out, sizeof(out), "old\"one", "new one"));
    printf("%s\n", out);
    check("ok", "Neo_Fighter", "correct horse", "correct horse");
    check("short-user", "ab", "correct horse", "correct horse");
    check("bad-char", "bad name", "correct horse", "correct horse");
    check("one-letter", "a__", "correct horse", "correct horse");
    check("short-pass", "Neo_Fighter", "short", "short");
    check("mismatch", "Neo_Fighter", "correct horse", "correct horsf");
    check("has-name", "Neo_Fighter", "my neo_fighter pw", "my neo_fighter pw");
    check("signin-ok", "anything", "x", NULL);
    check("signin-empty", "", "x", NULL);
    printf("flag\t%d%d%d%d\n", account_pw_flag("{\"has_discord\":true,\"x\":1}", "has_discord"),
           account_pw_flag("{\"has_discord\": false}", "has_discord"), account_pw_flag("{}", "has_discord"),
           account_pw_flag("{\"enabled\" : true}", "enabled"));
    for (i = 0; i < 16; i++) id[i] = (unsigned char)(i * 17 + 1);
    account_pw_hex16(id, hex);
    printf("hex\t%s\n", hex);
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('accountpw')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'accountpw.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/D_CRT_SECURE_NO_WARNINGS',
                            '/I' + str(ROOT / 'src/hooks'), str(folder / 'driver.c'),
                            str(ROOT / 'src/hooks/account_pw.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    return {line.split('\t')[0]: line.split('\t')[1:] for line in out.splitlines()}


def test_bodies_are_the_json_the_service_reads(rows):
    n, text = rows['plain']
    assert int(n) == len(text)
    assert json.loads(text) == {'username': 'Neo_Fighter', 'password': 'correct horse',
                                'pc': '00112233445566778899aabbccddeeff'}
    # quotes, backslashes and control characters survive as JSON escapes
    assert json.loads(rows['escape'][1]) == {'username': 'a"b', 'password': 'p\\w"d\n\x01', 'code': 'AB12-CD34'}
    assert json.loads(rows['reset'][1]) == {'username': 'user', 'password': 'new pass', 'code': 'K7QX-PM2D-9WRT'}
    assert rows['small'] == ['-1']
    assert json.loads(rows['change'][1]) == {'old': 'old"one', 'password': 'new one'}


def test_local_checks_mirror_the_service(rows):
    assert rows['ok'] == ['0', '']
    for tag in ('short-user', 'bad-char', 'one-letter', 'short-pass', 'mismatch', 'has-name'):
        assert rows[tag][0] == '1' and rows[tag][1], tag
    assert 'not the same' in rows['mismatch'][1]
    assert 'username' in rows['has-name'][1]
    assert rows['signin-ok'] == ['0', '']
    assert rows['signin-empty'][0] == '1'


def test_flags_and_pc_hex(rows):
    assert rows['flag'] == ['1001']
    # the PC fingerprint as anticheat.c's make_hwid writes it: 32 lowercase hex digits
    assert rows['hex'] == [''.join('%02x' % ((i * 17 + 1) & 0xFF) for i in range(16))]


def test_server_rules_match():
    """The username and password limits are the service's (relay/accounts/password.go), when the server repo is
    beside this checkout."""
    header = (ROOT / 'src/hooks/account_pw.h').read_text()
    for name, value in (('ACCOUNT_PW_LOGIN_MIN', 3), ('ACCOUNT_PW_LOGIN_MAX', 16), ('ACCOUNT_PW_MIN', 8),
                        ('ACCOUNT_PW_MAX', 128)):
        assert '#define %s %d' % (name, value) in header
    server = ROOT / 'server/relay/accounts/password.go'
    if not server.is_file():
        pytest.skip('the server repo is not checked out beside the game')
    go = server.read_text()
    for line in ('PasswordMinLen = 8', 'PasswordMaxLen = 128', 'LoginMinLen    = 3', 'LoginMaxLen    = 16'):
        assert line in go
