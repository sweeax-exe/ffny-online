"""Community skins at scale (build 94): up to 2048 skins in all, up to 255 for one fighter, the wide skin on the wire
behind CAPS bit 29 (the relay's side: server relay/protocol_skins.go), and no skin array on the stack."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / "src" / "hooks"
NETPLAY = (HOOKS / "netplay.c").read_text(encoding="utf-8")
SKINS_C = (HOOKS / "fighter_skins.c").read_text(encoding="utf-8")
SKINS_H = (HOOKS / "fighter_skins.h").read_text(encoding="utf-8")
CONTENT_H = (HOOKS / "pc_content.h").read_text(encoding="utf-8")
CONTENT_C = (HOOKS / "pc_content.c").read_text(encoding="utf-8")


def _define(source, name):
    m = re.search(r"#define %s (\d+)" % name, source)
    assert m, name
    return int(m.group(1))


def test_limits():
    assert _define(CONTENT_H, "PC_SKIN_MAX") >= 2048
    assert _define(CONTENT_H, "PC_CONTENT_MAX") >= _define(CONTENT_H, "PC_SKIN_MAX") + 256   # skins are packs
    assert _define(SKINS_H, "FIGHTER_SKINS_PER") == 255
    assert _define(SKINS_H, "FIGHTER_SKINS_WIRE_OLD") == 15


def test_no_skin_array_on_the_stack():
    refresh = SKINS_C[SKINS_C.index("void fighter_skins_refresh(void)"):]
    refresh = refresh[:refresh.index("\n}\n")]
    assert "malloc(" in refresh and "FighterSkin got[" not in refresh and "static PcContentSkin raw[" not in refresh


def test_skins_are_a_lookup_in_catalogue_order():
    # numbered per template in the order pc_content_skins lists them; one beyond 255 is left out on every PC alike
    assert "index = by_count[raw[i].template_fe_id] + 1;" in SKINS_C
    assert "if (index > FIGHTER_SKINS_PER) { over++; continue; }" in SKINS_C


def test_files_ini_lines_are_not_cut_at_1200_bytes():
    skins = CONTENT_C[CONTENT_C.index("int pc_content_skins("):]
    skins = skins[:skins.index("\n}\n")]
    assert "read_long_line(" in skins and "line[1200]" not in skins
    assert "static char applied[APPLIED_MAX]" in CONTENT_C and "#define APPLIED_MAX (320 * 1024)" in CONTENT_C


def test_wide_skin_bit_29_on_the_wire():
    assert _define(NETPLAY, "CAPS_SKINSWIDE") == 1 << 29
    for other in range(0, 29):
        assert (1 << other) != 1 << 29
    caps = NETPLAY[NETPLAY.index("put32(cb + n, CAPS_KOTH"):]
    assert "CAPS_SKINSWIDE" in caps[:caps.index("n += 4;")]
    # READY: u16 only with both bits; without it a skin above 15 goes as Default
    ready = NETPLAY[NETPLAY.index("void netplay_lobby_ready_skin(int ready, int fighter, int skin)"):]
    ready = ready[:ready.index("\n}\n")]
    assert "wide = relay_has(CAPS_SKINS) && relay_has(CAPS_SKINSWIDE);" in ready
    assert "if (!wide && skin > FIGHTER_SKINS_WIRE_OLD) skin = 0;" in ready
    # START: the count's top bit says the skins are u16; an old (byte) trailer above 15 is refused
    start = NETPLAY[NETPLAY.index("static void on_start_as("):]
    start = start[:start.index("build 86: the community fighters' numbers")]
    assert "wide = (ns & 0x80) != 0" in start and "if (wide) v |= rd8(&r) << 8;" in start
    assert "(wide || v <= FIGHTER_SKINS_WIRE_OLD)" in start


def test_skin_window_grid_for_many_skins():
    ui = (HOOKS / "skins_ui.inc").read_text(encoding="utf-8")
    assert "int skin_window_grid(" in ui and "n + 1 > 5 ? skin_window_grid" in ui
    assert "fighter_name_matches(" in ui and "ImGuiKey_Tab" in ui      # the name search
    assert "pad.lb" in ui and "pad.rb" in ui and "MouseWheel" in ui    # pages: pad, keyboard, mouse
