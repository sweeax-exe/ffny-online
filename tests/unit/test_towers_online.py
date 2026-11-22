# SPDX-License-Identifier: GPL-3.0-or-later
"""Team Tower Online: the game's wire constants match the relay's (server relay/protocol_tower.go), and the TOWER
layout towers_online.c reads is the one the relay writes."""
import os
import pathlib
import re

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
HOOKS = REPO / "src" / "hooks"


def _server():
    for d in [os.environ.get("FFNY_SERVER_DIR"), REPO / "server", REPO.parents[2] / "server" if len(REPO.parents) > 2 else None]:
        if d and (pathlib.Path(d) / "relay" / "protocol_tower.go").is_file():
            return pathlib.Path(d) / "relay"
    return None


def _c(name, text):
    m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)" % name, text)
    assert m, name
    return int(m.group(1), 0)


def test_game_constants():
    net_h = (HOOKS / "netplay.h").read_text(encoding="utf-8")
    net_c = (HOOKS / "netplay.c").read_text(encoding="utf-8")
    tow = (HOOKS / "towers_online.h").read_text(encoding="utf-8")
    assert _c("NETPLAY_MODE_TOWER", net_h) == 0x44
    assert _c("T_TOWER", net_c) == 0x3D and _c("T_TCHOICE", net_c) == 0x3E
    assert _c("TOWER_TRAILER", net_c) == 0x54 and _c("TOWER_RESULT", net_c) == 0xF7
    assert _c("TOWERNET_WINNER_VOID", tow) == 0xFE


@pytest.mark.skipif(_server() is None, reason="no server checkout (FFNY_SERVER_DIR)")
def test_relay_matches():
    go = (_server() / "protocol_tower.go").read_text(encoding="utf-8")
    want = {"ModeTower": 0x44, "TypeTower": 0x3D, "TypeTChoice": 0x3E, "TowerTrailerTag": 0x54, "TowerResultTag": 0xF7,
            "WinnerVoid": 0xFE}
    for name, v in want.items():
        m = re.search(r"\b%s\s+(?:\w+\s+)?=\s+(0x[0-9A-Fa-f]+)" % name, go)
        assert m and int(m.group(1), 16) == v, name
    # the TOWER body: state, flags, tower, floor u16, floors u16, beaten u16, cpu u16 x2, venue, secs u16,
    # saved u16, last u16, how, voids, then two strings (towers_online.c reads 22 bytes before them)
    body = go.split("func (m Tower) appendBody", 1)[1].split("\n}\n", 1)[0]
    fields = re.findall(r"AppendUint16|append\(b, ([^)]*)\)", body)
    assert body.count("AppendUint16") == 8
    src = (HOOKS / "towers_online.c").read_text(encoding="utf-8")
    assert "i = 22;" in src and "rd16(b + 18)" in src
    assert fields
