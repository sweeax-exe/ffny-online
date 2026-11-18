"""Community fighters beyond the places online (phase 2, build 86): the fighter number on the wire behind CAPS bit 25,
the per-match lineup that lets the banks bind online, the launcher's index-v5.json and its catalogue check, the hub
portraits under a number. The wire layout is the relay's (server repo docs/unlimited-fighters-online.md,
relay/protocol_unlimited.go and unlimited_test.go): these checks keep the game's reader and writer on it."""
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))

import content_compose as cc  # noqa: E402
import mod_catalogue  # noqa: E402

HOOKS = ROOT / "src" / "hooks"
NETPLAY = (HOOKS / "netplay.c").read_text(encoding="utf-8")


def _body(source, head):
    """The text of the C function whose definition starts with ``head`` up to its closing brace at column 0."""
    m = re.search(re.escape(head) + r"[^;{]*\)\n\{", source)     # the definition, not a prototype or a stub
    assert m, head
    return source[m.start():source.index("\n}\n", m.start())]


def test_caps_bit_is_25_and_announced():
    m = re.search(r"#define CAPS_UNLIMITED (\d+)u", NETPLAY)
    assert m and int(m.group(1)) == 1 << 25
    # 22 skins, 23 standard preference (and Run the Streets on its branch), 24 Run the Streets: none of them
    for other in (1 << 22, 1 << 23, 1 << 24):
        assert int(m.group(1)) != other
    caps = NETPLAY[NETPLAY.index("put32(cb + n, CAPS_KOTH"):]
    assert "CAPS_UNLIMITED" in caps[:caps.index("n += 4;")]


def test_ready_long_form():
    """READY: ready, the template byte, skin 0, then the number u32 LE (7 bytes), only to a relay that answered the
    bit; without it the template plays (2 or 3 bytes)."""
    body = _body(NETPLAY, "void netplay_lobby_ready_skin(")
    assert "uint8_t b[9]" in body                       # build 94: the wide skin (u16) makes it 8 bytes with a number
    assert "b[1] = (uint8_t)unl_template(number);" in body and "skin = 0;" in body
    assert re.search(r"number && relay_has\(CAPS_UNLIMITED\)\) \{\s+len = wide \? 8 : 7;\s+put32\(b \+ \(wide \? 4 : 3\), \(uint32_t\)number\);", body)
    assert "fighter = b[1];" in body            # a relay without the bit: the template, as the room will show it


def _reads_in_order(body, marks):
    at = 0
    for m in marks:
        nxt = body.find(m, at)
        assert nxt >= 0, m
        at = nxt + len(m)


def test_room_reads_numbers_after_the_rules():
    body = _body(NETPLAY, "static void on_room(")
    _reads_in_order(body, ["v.blocked_count = rd8(&r);", "rdbytes(&r, v.blocked, v.blocked_count);",
                           "build 86: the players' community fighter numbers", "int nn = rd8(&r)",
                           "int s = rd8(&r)", "num |= (uint32_t)rd8(&r) << (8 * j)",
                           "v.players[j].fighter = (int)num;"])


def test_start_reads_numbers_after_the_skins():
    body = _body(NETPLAY, "static void on_start_as(")
    _reads_in_order(body, ["build 85: the players' skins", "int ns = rd8(&r)",
                           "build 86: the community fighters' numbers", "int nn = rd8(&r)",
                           "num |= (uint32_t)rd8(&r) << (8 * j)", "fighters[j] = (int)num;",
                           # bank rows never play online as fighters of their own
                           "fighter_banks_index(fighters[i]) >= 0", "fighters[i] = 55;",
                           # positions follow the slots: the numbers are sorted with them
                           "positions follow the room slots in order", "m.fighter[i] = fighters[i];"])


def test_banks_bind_online_only_for_a_numbered_lineup():
    hooks = (HOOKS / "fighter_bank_hooks.c").read_text(encoding="utf-8")
    assert "static int locked(void) { return ffny_online_locked() && !g_online_lineup; }" in hooks
    record = _body(hooks, "uint32_t fighter_banks_record(")
    assert "if (locked() || g_listing) return record;" in record
    assert "locked()" in _body(hooks, "void fighter_banks_movie_load(")
    before = _body(hooks, "int fighter_banks_before_call(")
    assert "} else if (locked()) {" in before
    # MORE and the select screen's community window stay offline only
    assert "!ffny_online_locked()" in before and "ffny_online_locked()" in _body(hooks, "int fighter_banks_bind(")
    automatch = (HOOKS / "automatch.c").read_text(encoding="utf-8")
    run = automatch[automatch.index('call("Game.ResetFighterList()");'):]
    assert run.index("fighter_banks_online_lineup(unl)") < run.index("Controller.ControllerSetup(%d,%d,%d,%d)")


def test_side_file_in_the_fight_file_print():
    body = _body(NETPLAY, "static void make_data_print(")
    assert "file_print(FB_SIDE_FILE, &overridden)" in body and "if (f) {" in body


def test_names_and_portraits_by_number():
    data = (HOOKS / "online_data.c").read_text(encoding="utf-8")
    assert "fighter_banks_find(id)" in _body(data, "const char *online_fighter_name(")
    screen = (HOOKS / "online_screen.cpp").read_text(encoding="utf-8")
    port = screen[screen.index("bool portrait(ImDrawList"):]
    port = port[:port.index("\n}\n")]
    assert "fighter_tex(e->template_fe_id)" in port
    assert "bool community_numbers_here(const NetRoom &r)" in screen
    assert "community_window(dl, f," in screen and "more.id = FB_MORE_ID;" in screen


@pytest.mark.parametrize("value,ok", [(1000, True), (1043, True), (0x7FFFFFFF, True), (999999, False), (999, False),
                                      (127, False), (True, False), ("1000", False), (0x80000000, False)])
def test_catalogue_takes_fighter_numbers(value, ok):
    assert mod_catalogue.fighter_number(value) is ok


def _mod(fe):
    return {"id": "studio-syn", "name": "Syn", "description": "d", "authors": ["a"], "class": "original",
            "licence": "CC-BY-4.0", "homepage": "https://play.defjam.online/", "kind": "content", "type": "new-fighter",
            "fighter_fe_id": fe, "versions": [{"version": "1.0.0"}]}


def test_catalogue_row_with_a_number():
    mod_catalogue.ffny_mod(_mod(1043))
    mod_catalogue.ffny_mod(_mod(101))
    for bad in (999999, 500, 128):
        with pytest.raises(Exception):
            mod_catalogue.ffny_mod(_mod(bad))


def test_hub_portraits_drop_a_gone_number(tmp_path):
    folder = tmp_path.joinpath(*cc.HUB_ART)
    folder.mkdir(parents=True)
    for name in ("55.png", "1043.png", "101.png", "999999.png"):
        (folder / name).write_bytes(b"x")
    assert cc.hub_portraits(tmp_path, [], lambda entry: None) == []
    left = sorted(p.name for p in folder.iterdir())
    assert left == ["55.png", "999999.png"]     # stock kept, the place and the number gone, MORE is no number
