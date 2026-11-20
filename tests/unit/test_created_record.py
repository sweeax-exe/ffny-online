"""The created-fighter record has one layout in three places: the game (src/hooks/created_fighter.h),
the relay (server/relay/protocol_created.go) and the protocol document. A size or a message type
that drifts in one of them makes every record refused, so they are compared here."""
import re
from pathlib import Path
import pytest

from server_src import RELAY as RELAY_SRC, SKIP

if RELAY_SRC is None:
    pytest.skip(SKIP, allow_module_level=True)

ROOT = Path(__file__).resolve().parents[2]


def _int(text, pattern):
    m = re.search(pattern, text)
    assert m, pattern
    return int(m.group(1), 0)


def test_record_size_and_types_agree():
    header = (ROOT / "src/hooks/created_fighter.h").read_text(encoding="utf-8")
    go = (RELAY_SRC / "protocol_created.go").read_text(encoding="utf-8")
    netplay = (ROOT / "src/hooks/netplay.c").read_text(encoding="utf-8")
    doc = (ROOT / "docs/netplay-protocol.md").read_text(encoding="utf-8")

    payload_c = _int(header, r"#define CP_PAYLOAD\s+(0x[0-9A-Fa-f]+)")
    payload_go = _int(go, r"RecordPayload = (0x[0-9A-Fa-f]+)")
    assert payload_c == payload_go == 0x127
    total = 1 + payload_c + 4
    assert "`len` u16 (0 or %d)" % total in doc

    # the name: every PC cuts it to the length the relay accepts (online names, build 39: 16)
    assert _int(header, r"#define CP_NAME_MAX\s+(\d+)") == _int(go, r"MaxRecordName = (\d+)") == 16
    assert _int(header, r"#define CP_NAME_BYTES\s+(0x[0-9A-Fa-f]+)") == _int(go, r"recNameLen\s+= (0x[0-9A-Fa-f]+)")
    assert _int(header, r"#define CP_FIGHTER_ID\s+(\d+)") == _int(go, r"CreatedFighter uint8 = (\d+)")
    assert _int(header, r"#define CP_REC_VERSION\s+(\d+)") == _int(go, r"RecordVersion uint8 = (\d+)")
    for name, go_name in (("T_FIGHTER", "TypeFighter"), ("T_RECORD", "TypeRecord"), ("T_RECGET", "TypeRecGet")):
        assert _int(netplay, r"#define %s\s+(0x[0-9A-Fa-f]+)" % name) == _int(go, r"%s\s+MsgType = (0x[0-9A-Fa-f]+)" % go_name)
    assert _int(netplay, r"#define CAPS_CREATED\s+(\d+)u") == 8
    # no clash with another extension's message type or CAPS bit (build 36 took 0x26 YIELD and bit 2)
    types = [int(t, 16) for t in re.findall(r"MsgType = (0x[0-9A-Fa-f]+)", "".join(
        p.read_text(encoding="utf-8") for p in RELAY_SRC.glob("protocol*.go")))]
    assert len(types) == len(set(types)), sorted(types)
    bits = re.findall(r"Cap\w+ uint32 = 1 << (\d+)", "".join(
        p.read_text(encoding="utf-8") for p in RELAY_SRC.glob("*.go")))
    assert len(bits) == len(set(bits)), bits
    assert "CapCreated uint32 = 1 << 3" in go


def test_switch_file_named_in_relay_and_doc():
    main = (RELAY_SRC / "cmd/relay/main.go").read_text(encoding="utf-8")
    doc = (ROOT / "docs/netplay-protocol.md").read_text(encoding="utf-8")
    assert '"created-fighters-allowed"' in main
    assert "`created-fighters-allowed`" in doc


def test_stat_limits_agree():
    """The game's own clamp (cp_record_sanitize) is the relay's stat check (build 46,
    relay/record_stats.go): a record the game sends is never refused with ERROR 12."""
    game = (ROOT / "src/hooks/created_fighter.c").read_text(encoding="utf-8")
    go = (RELAY_SRC / "record_stats.go").read_text(encoding="utf-8")
    assert _int(game, r"#define MAX_BASE\s+(\d+)") == _int(go, r"MaxBaseStat\s+= (\d+)")
    assert _int(game, r"#define MAX_BONUS\s+(\d+)") == _int(go, r"MaxBonusStat\s+= (\d+)")
    assert _int(game, r"#define MAX_BONUS_CHARM\s+(\d+)") == _int(go, r"MaxBonusCharm\s+= (\d+)")
    assert _int(game, r"#define MAX_BONUS_TOTAL\s+(\d+)") == _int(go, r"MaxBonusTotal\s+= (\d+)")


def test_part_limits_agree():
    """The relay's part check (build 74, RecordPartsRefused) takes every record the game's clamp makes:
    the same Blazin table and rows, the same body-value limit, the same table and colour bounds, the
    same offsets; and neither side caps the base stats' sum (a Story run can buy 5 x 50)."""
    game = (ROOT / "src/hooks/created_fighter.c").read_text(encoding="utf-8")
    go = (RELAY_SRC / "record_stats.go").read_text(encoding="utf-8")
    assert _int(game, r"#define BLAZIN_TABLE\s+(\d+)u") == _int(go, r"BlazinTable = (\d+)") == 6
    assert _int(game, r"#define BLAZIN_ROWS\s+(\d+)u") == _int(go, r"BlazinRows\s+= (\d+)") == 82
    assert _int(game, r"#define SOD_TABLES (\d+)") == _int(go, r"SodTables\s+= (\d+)")
    assert "colour < 16" in game and _int(go, r"SodColours = (\d+)") == 16
    assert "v > 0xFF" in game and _int(go, r"MaxRecipeBody = (0x[0-9A-Fa-f]+)") == 0xFF
    assert "if (i < 12)" in game and _int(go, r"RecipeBody\s+= (\d+)") == 12
    assert _int(game, r"#define R_RECIPE\s+(0x[0-9A-Fa-f]+)") == _int(go, r"recRecipe = (0x[0-9A-Fa-f]+)")
    assert _int(game, r"#define R_MOVES\s+\(R_PROFILE \+ (0x[0-9A-Fa-f]+)\)") == _int(go, r"recMoves\s+= recProfile \+ (0x[0-9A-Fa-f]+)")
    assert "MAX_TOTAL_BASE" not in game and "MaxTotalBase" not in go


def test_a_changed_created_fighter_is_read_again_between_matches():
    # Lukio, build 99: the list was read once per session, so edited moves stayed old online until a restart
    netplay = (ROOT / "src/hooks/netplay.c").read_text(encoding="utf-8")
    assert "MemCard.Save(" in netplay and "InterlockedExchange(&g_cp_dirty, 1)" in netplay
    body = netplay.split("static void cp_tick_refresh(void)", 1)[1].split("\n}\n", 1)[0]
    assert "N.match_live" in body and "memcard_save_busy()" in body
    assert "created_fighter_reload();" in body and "cp_mine_set(" in body
    assert netplay.count("cp_tick_refresh();") == 1


def test_a_join_by_code_asks_the_other_relays():
    # ticket #200: a private room is on one relay only; "no room" on this relay sends the hub to the next one
    netplay = (ROOT / "src/hooks/netplay.c").read_text(encoding="utf-8")
    servers = (ROOT / "src/hooks/netplay_servers.c").read_text(encoding="utf-8")
    ui = (ROOT / "src/hooks/online_screen.cpp").read_text(encoding="utf-8")
    assert "netservers_code_miss((const char *)N.pend + 14)" in netplay and "Looking on other servers..." in netplay
    assert "No server has a room with that code." in netplay
    assert "if (CS_.miss) code_search_step();" in servers and "move_to(kind == 4 ? 4 : 1, best" in servers
    assert ui.count("netservers_code_begin(") == 2
