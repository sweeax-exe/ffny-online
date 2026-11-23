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
#include <string.h>
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
    netlobby_search_line_cap(line, sizeof(line), 0, 50, "every server", 300);    /* build 109: the ceiling */
    printf("search|cap-late|%s\n", line);
    netlobby_search_line_cap(line, sizeof(line), 0, 5, "every server", 300);
    printf("search|cap-early|%s\n", line);
    netlobby_search_line_cap(line, sizeof(line), 0, 50, "every server", 0);
    printf("search|cap-off|%s\n", line);
    {   /* build 109: the ceiling in the games' pool (netservers_pair_relay), a pair of 350 ms on relay 0 */
        static const int fl[][4] = { { 0, 0, 300, 300 }, { QUICK_SEAT_BOUND, 0, 300, 0 }, { QUICK_SEAT_FAR, QUICK_SEAT_FAR, 300, 300 },
                                     { QUICK_SEAT_BOUND, QUICK_SEAT_BOUND, 0, 0 }, { QUICK_SEAT_BOUND, 0, 400, 360 },
                                     { 0, QUICK_SEAT_BOUND, 400, 300 } };
        NetQuickSeat a, c;
        int k, s;
        printf("ceiling");
        for (k = 0; k < 6; k++) {
            memset(&a, 0, sizeof(a));
            memset(&c, 0, sizeof(c));
            a.pings[0] = 150; c.pings[0] = 200;
            a.flags = fl[k][0]; c.flags = fl[k][1]; a.cap_ms = fl[k][2]; c.cap_ms = fl[k][3];
            printf("|%d", netservers_pair_relay(&a, &c, 0, &s, NULL));
        }
        printf("\n");
    }
    netlobby_afk_line(line, sizeof(line), 42);
    printf("afk|%s\n", line);
    printf("kickmin|%d|%d|%d|%d\n", netlobby_kick_minutes_left(0), netlobby_kick_minutes_left(61000),
           netlobby_kick_minutes_left(299000), netlobby_kick_minutes_left(400000));
    printf("stages|%d|%d|%d|%d|%d\n", QUICK_STAGE0_MS, QUICK_STAGE1_S, QUICK_STAGE1_MS, QUICK_STAGE2_S,
           netservers_pool_limit(QUICK_STAGE2_S));
    {   /* the VENUES presets: the community venues, the list's signature, the step over empty presets */
        static const int stock[] = { 1, 2, 4, 6, 11 }, more[] = { 1, 2, 28, 4, 6, 63, 11, 64, 27 };
        int out[16], k, m;
        m = netlobby_community_venues(stock, 5, out, 16);
        printf("community|stock|%d\n", m);
        m = netlobby_community_venues(more, 9, out, 16);
        printf("community|more|%d", m);
        for (k = 0; k < m; k++) printf("|%d", out[k]);
        printf("\n");
        printf("sig|%d|%d|%d\n", netlobby_venue_sig(stock, 5) == netlobby_venue_sig(stock, 5),
               netlobby_venue_sig(stock, 5) != netlobby_venue_sig(more, 9), netlobby_venue_sig(stock, 4) != netlobby_venue_sig(stock, 5));
    }
    {
        static const int all[6] = { 1, 1, 1, 1, 1, 1 }, no_last[6] = { 1, 1, 1, 1, 1, 0 }, one[6] = { 0, 0, 1, 0, 0, 0 };
        printf("step|%d|%d|%d|%d|%d|%d|%d\n", netlobby_preset_step(4, 1, 6, all), netlobby_preset_step(4, 1, 6, no_last),
               netlobby_preset_step(0, -1, 6, no_last), netlobby_preset_step(-1, 1, 6, all), netlobby_preset_step(-1, -1, 6, no_last),
               netlobby_preset_step(2, 1, 6, one), netlobby_preset_step(5, 1, 6, all));
    }
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
    # build 109: the ceiling from 45 s, tighter stages before it, none when far opponents are allowed
    assert lines['cap-late'] == 'Looking for a close player (under 300 ms) on every server...'
    assert lines['cap-early'] == 'Looking for a close player (under 120 ms) on every server...'
    assert lines['cap-off'] == 'Waiting for another player on every server...'
    # the pool: unbound pairs meet at 350 ms, a bound one not (the lower listed ceiling holds), no ceiling: they meet
    assert by(rows, 'ceiling') == [['0', '-1', '0', '0', '0', '-1']]
    assert by(rows, 'afk') == [['Press any button: you leave the room in 42 s.']]
    assert by(rows, 'kickmin') == [['5', '4', '1', '1']]
    assert by(rows, 'stages') == [['120', '20', '180', '45', '0']]


def test_venue_presets(rows):
    """Build 97: "Community maps" is the installed community venues (feIDs 28..63) and exists only with one; the
    presets follow a changed venue list (signature); VENUES steps over a preset with nothing for the room."""
    assert by(rows, 'community') == [['stock', '0'], ['more', '2', '28', '63']]
    assert by(rows, 'sig') == [['1', '1', '1']]
    # 4 -> 5; 4 -> 0 when 5 (Community maps) is empty for the room; 0 back -> 4; custom -> 0 / the last usable;
    # only 2 usable: stays 2; 5 -> 0 round
    assert by(rows, 'step') == [['5', '0', '4', '0', '4', '2', '0']]
    ui = _src('src/hooks/online_screen.cpp')
    assert 'list.push_back({ "Community maps"' in ui and 'netlobby_venue_sig(ids.data()' in ui
    # build 110: the presets, then Custom (the remembered checklist)
    assert 'int to = netlobby_preset_step(cur, step, custom + 1, usable.data());' in ui
    assert '"create-community-maps"' in ui
    assert 'RECOMP_TEST_EXTRA_VENUES' in _src('src/hooks/online_data.c')


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
    assert ui.count('settings_group("online", { "quick_max_ping", "quick_far" });') == 2     # the launcher's and the overlay's pages


@pytest.mark.skipif(RELAY is None or not (RELAY / 'protocol_kick.go').is_file(),
                    reason=SKIP + ' (or it predates build 97: server branch feat/lobby-97)')
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
    # build 109: the optional flags byte after them (quick_cap.go), the game's NETLOBBY_ACTIVE_FAR
    assert 'm.WithFlags, m.Flags = true, c.u8()' in afk
    cap = (RELAY / 'quick_cap.go').read_text(encoding='utf-8')
    assert 'QuickFlagFar = 1' in cap and '#define NETLOBBY_ACTIVE_FAR      1' in rules
    assert 'CapQuickCap uint32 = 1 << 31' in cap and '#define CAPS_QUICKCAP 2147483648u' in _src('src/hooks/netplay.c')
    assert 'DefaultQuickPingCap = 300' in cap and re.search(r'#define QUICK_CAP_MS\s+300', h)
    assert 'QuickSeatBound = 1' in cap and 'QuickSeatFar   = 2' in cap
    assert re.search(r'#define QUICK_SEAT_BOUND\s+1', h) and re.search(r'#define QUICK_SEAT_FAR\s+2', h)
    # ROOMLIST's seats: max ping in 10 ms units and the wait, three bytes a room after the More pings
    assert 'b = append(b, byte(min(q.MaxPing/10, 255)))' in (RELAY / 'protocol_roomlist.go').read_text(encoding='utf-8')
    assert 'seat_max[k] = b[p] * 10;' in _src('src/hooks/netplay_servers.c')
