"""Build 97's lobby features (owner-approved 2026-11-19): host kick, the Quick Match ping limit (staged widening and
the player's own max ping) and AFK in rooms. src/hooks/netplay_lobby97_rules.c compiled natively (MSVC; skips
without `cl`), the wiring in netplay.c, the online screens and the settings, and the numbers the relay must share
(server relay/protocol_kick.go, quick_ping.go, afk.go; skipped without the server checkout)."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from server_src import RELAY, SKIP

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src' / 'hooks'

DRIVER = r'''
#include <stdio.h>
#include "netplay_lobby97_rules.h"
#include "netplay_servers_rules.h"

int main(void)
{
    char line[200];
    unsigned char b[NETLOBBY_ACTIVE_LEN];
    int i, n;
    for (i = -1; i <= NETLOBBY_MAX_PING_CHOICES; i++) printf("maxping|%d|%d\n", i, netlobby_max_ping_ms(i));
    n = netlobby_active_body(b, 61, 150, 70000);
    printf("active|%d|%d|%d|%d|%d|%d|%d\n", n, b[0], b[1], b[2], b[3], b[4], b[5]);
    netlobby_search_line(line, sizeof(line), 150, 12, "every server");
    printf("search|own|%s\n", line);
    netlobby_search_line(line, sizeof(line), 200, 25, "every server");
    printf("search|own-wider|%s\n", line);
    netlobby_search_line(line, sizeof(line), 0, 5, "every server");
    printf("search|0|%s\n", line);
    netlobby_search_line(line, sizeof(line), 0, 25, "Europe and US East");
    printf("search|25|%s\n", line);
    netlobby_search_line(line, sizeof(line), 0, 50, NULL);
    printf("search|50|%s\n", line);
    netlobby_search_line(line, sizeof(line), 150, 50, "every server");
    printf("search|own-late|%s\n", line);
    netlobby_afk_line(line, sizeof(line), 42);
    printf("afk|%s\n", line);
    printf("kickmin|%d|%d|%d|%d\n", netlobby_kick_minutes_left(0), netlobby_kick_minutes_left(61000),
           netlobby_kick_minutes_left(299000), netlobby_kick_minutes_left(400000));
    printf("stages|%d|%d|%d|%d|%d\n", QUICK_STAGE0_MS, QUICK_STAGE1_S, QUICK_STAGE1_MS, QUICK_STAGE2_S,
           netservers_pool_limit(QUICK_STAGE2_S));
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('lobby97')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'lobby97.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/utf-8', '/I' + str(HOOKS),
                            str(folder / 'driver.c'), str(HOOKS / 'netplay_lobby97_rules.c'),
                            str(HOOKS / 'netplay_servers_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return [line.split('|') for line in out.splitlines()]


def by(rows, kind):
    return [r[1:] for r in rows if r[0] == kind]


def _src(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_max_ping_choices_and_active_body(rows):
    assert by(rows, 'maxping') == [['-1', '0'], ['0', '0'], ['1', '100'], ['2', '150'], ['3', '200'], ['4', '0']]
    # idle 61 s, max ping 150, a search over 0xFFFF s is 0xFFFF; little-endian u16s
    assert by(rows, 'active') == [['6', '61', '0', '150', '0', '255', '255']]


def test_search_lines(rows):
    lines = dict((r[0], r[1]) for r in by(rows, 'search'))
    assert lines['own'] == 'Waiting for a player under 150 ms on every server...'
    assert lines['own-wider'] == 'Waiting for a player under 200 ms on every server...'
    assert lines['0'] == 'Looking for a close player (under 120 ms) on every server...'
    assert lines['25'] == 'Looking for a close player (under 180 ms) on Europe and US East...'
    assert lines['50'] == 'Waiting for another player on every server...'
    assert lines['own-late'] == 'Waiting for a player under 150 ms on every server...'         # never widened
    assert by(rows, 'afk') == [['Press any button: you leave the room in 42 s.']]
    assert by(rows, 'kickmin') == [['5', '4', '1', '1']]
    assert by(rows, 'stages') == [['120', '20', '180', '45', '0']]


def test_game_wiring():
    net = _src('src/hooks/netplay.c')
    assert 'case NETLOBBY_T_ACTIVE:' in net and 'on_afk(b + 6, len - 6)' in net
    assert 'code == NETLOBBY_ERR_KICKED || code == NETLOBBY_ERR_IDLE' in net
    assert 'active_tick(now);' in net
    # the max ping and the search time reach the relay before every QUICK
    quick = net[net.index('void netplay_lobby_quick(int mode, int max)'):]
    assert quick.index('send_active(GetTickCount64());') < quick.index('request(T_QUICK, b, n);')
    assert 'request(NETLOBBY_T_KICK, b, 1);' in net
    ui = _src('src/hooks/online_screen.cpp')
    assert 'netplay_note_input();' in ui and 'draw_afk_warning(dl, f, afk);' in ui
    assert 'hint(dl, f, hx, hy, "lb", "REMOVE")' in ui and 'netplay_lobby_kick(S.kick_confirm);' in ui
    for page in ('"room-kick"', '"room-kick-prompt"', '"room-afk"', '"room-quick-maxping"', '"room-quick-stage"'):
        assert page in ui


def test_setting():
    row = re.search(r'\{\s*"online",\s*"quick_max_ping",\s*RECOMP_SETTING_ENUM,\s*0,\s*0,\s*0,\s*k_quick_max_ping,',
                    _src('src/hooks/pc_settings.c'))
    assert row, 'no [online] quick_max_ping row (default off)'
    assert 'k_quick_max_ping[] = { "off", "100", "150", "200", NULL }' in _src('src/hooks/pc_settings.c')
    ui = _src('src/hooks/pc_ui.cpp')
    assert '{"online", "quick_max_ping", "Quick Match max ping"}' in ui
    assert ui.count('settings_group("online", { "quick_max_ping" });') == 2     # the launcher's and the overlay's pages


@pytest.mark.skipif(RELAY is None, reason=SKIP)
def test_relay_agrees():
    kick = (RELAY / 'protocol_kick.go').read_text(encoding='utf-8')
    afk = (RELAY / 'afk.go').read_text(encoding='utf-8')
    quick = (RELAY / 'quick_ping.go').read_text(encoding='utf-8')
    rules = _src('src/hooks/netplay_lobby97_rules.h')
    assert 'const TypeKick MsgType = 0x3B' in kick and '#define NETLOBBY_T_KICK          0x3B' in rules
    assert 'const TypeActive MsgType = 0x3C' in afk and '#define NETLOBBY_T_ACTIVE        0x3C' in rules
    assert 'const ErrKicked uint8 = 16' in kick and '#define NETLOBBY_ERR_KICKED      16' in rules
    assert 'const ErrIdle uint8 = 17' in afk and '#define NETLOBBY_ERR_IDLE        17' in rules
    assert 'const KickBlockTime = 5 * time.Minute' in kick and '#define NETLOBBY_KICK_BLOCK_S    300' in rules
    assert 'const Lobby97MinBuild = 97' in kick
    # the staged widening: the relay's QuickPingStages are the game's QUICK_STAGE* numbers
    m = re.search(r'var QuickPingStages = \[\]QuickPingStage\{\{0, (\d+)\}, \{(\d+) \* time\.Second, (\d+)\}, \{(\d+) \* time\.Second, 0\}\}', quick)
    assert m, 'QuickPingStages changed shape'
    h = _src('src/hooks/netplay_servers_rules.h')
    game = [re.search(r'#define %s\s+(\d+)' % k, h).group(1) for k in ('QUICK_STAGE0_MS', 'QUICK_STAGE1_S', 'QUICK_STAGE1_MS', 'QUICK_STAGE2_S')]
    assert list(m.groups()) == game
    # ACTIVE's body: idle, max ping, search (three u16 after the token), as the game writes it
    assert 'Active{Token: tok, Idle: c.u16(), MaxPing: c.u16(), Search: c.u16()}' in afk
    # ROOMLIST's seats: max ping in 10 ms units and the wait, three bytes a room after the More pings
    assert 'b = append(b, byte(min(q.MaxPing/10, 255)))' in (RELAY / 'protocol_roomlist.go').read_text(encoding='utf-8')
    assert 'seat_max[k] = b[p] * 10;' in _src('src/hooks/netplay_servers.c')
