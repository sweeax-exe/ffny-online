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
