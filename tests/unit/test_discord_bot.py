"""The FFNY Online Discord bot's Discord-free part (community/discord-bot/ffny_core.py)."""
import importlib.util
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PATH = os.path.join(HERE, "..", "..", "community", "discord-bot", "ffny_core.py")
spec = importlib.util.spec_from_file_location("ffny_core", PATH)
core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core)


def test_layout_names_are_valid_discord_names():
    seen = set()
    for category, channels in core.LAYOUT:
        assert 0 < len(category) <= 100
        for name, kind in channels:
            assert kind in {"read", "chat", "polls", "team", "voice", "stat"}
            if kind in {"read", "chat", "polls", "team"}:
                assert re.fullmatch(r"[a-z0-9-]{1,100}", name), name
            assert name not in seen
            seen.add(name)
    for needed in (core.TICKET_LOG, core.ANNOUNCEMENTS, core.HELP_CHANNEL):
        assert needed in seen
    assert core.TICKET_CATEGORY in [c for c, _ in core.LAYOUT]


def test_ticket_forms_fit_discord_limits():
    for kind, info in core.TICKET_TYPES.items():
        assert 1 <= len(info["fields"]) <= 5
        assert len(info["label"]) <= 45
        keys = [f[0] for f in info["fields"]]
        assert len(keys) == len(set(keys))
        for key, label, style, required, placeholder in info["fields"]:
            assert len(label) <= 45 and len(placeholder) <= 100
            assert style in {"short", "paragraph"}


def test_texts_fit_a_message():
    for text in (core.WELCOME, core.RULES, core.HELP_PANEL, core.ALERTS_TEXT):
        assert len(text) <= 2000
    assert len(core.FAQ) <= 25
    for q, a in core.FAQ:
        assert len(q) <= 256 and len(a) <= 1024


def test_ticket_channel_name():
    assert core.ticket_channel_name("bug", 7, "Masa_Fan 99") == "bug-0007-masa-fan-99"
    assert core.ticket_channel_name("other", 12, "!!!") == "other-0012-player"
    assert len(core.ticket_channel_name("online", 1, "x" * 80)) <= 100


def test_relay_summary_takes_the_newest_line():
    text = ("2026/10/20 21:18:57 summary rooms=0 clients=1 in=1.0pkt/s\n"
            "2026/10/20 21:19:57 summary rooms=2 clients=5 in=1.0pkt/s\n")
    assert core.parse_relay_summary(text) == (2, 5)
    assert core.parse_relay_summary("no summary yet") is None
    assert core.status_name((1, 3)) == "🟢 3 online · 1 room"
    assert core.status_name((2, 5)) == "🟢 5 online · 2 rooms"
    assert core.status_name(None) == "🔴 Server offline"


def test_build_announcements():
    m = core.parse_manifest(json.dumps({"build": 11, "version": "0.7.2-remaster.11", "notes": "x"}))
    assert m == {"build": 11, "version": "0.7.2-remaster.11", "notes": "x"}
    assert core.parse_manifest("<html>") is None
    assert core.parse_manifest('{"version": "1"}') is None
    assert not core.is_new_build(None, m)          # first look only records the build
    assert not core.is_new_build(11, m)
    assert core.is_new_build(10, m)
    assert not core.is_new_build(10, None)


def test_transcript():
    text = core.transcript("bug-0001-a", [("2026-10-20 21:00", "a", "it crashed", ["https://x/y.png"])])
    assert text.startswith("Transcript of #bug-0001-a\n")
    assert "[2026-10-20 21:00] a: it crashed" in text and "attachment: https://x/y.png" in text


def test_onboarding_meets_discords_rules():
    kinds = {name: kind for _, channels in core.LAYOUT for name, kind in channels}
    defaults = core.ONBOARDING_DEFAULTS
    assert all(name in kinds for name in defaults)
    assert len(defaults) >= 7
    assert sum(kinds[n] == "chat" for n in defaults) >= 5      # ones @everyone can write in
    roles = {core.TEAM_ROLE, core.ALERT_ROLE, core.MATCH_ROLE}
    for key, title, options in core.ONBOARDING_PROMPTS:
        assert len(title) <= 100 and 1 <= len(options) <= 50
        for okey, emoji, otitle, desc, opt_roles, chans in options:
            assert len(otitle) <= 50 and len(desc) <= 100
            assert opt_roles or chans
            assert set(opt_roles) <= roles
            assert all(c in kinds and c not in defaults for c in chans)
    assert len(core.WELCOME_CHANNELS) <= 5
    for name, emoji, desc in core.WELCOME_CHANNELS:
        assert name in kinds and len(desc) <= 42
    assert len(core.WELCOME_SCREEN) <= 140 and len(core.MATCH_TEXT) <= 2000


def test_profile_and_leaderboard_lines():
    st = {"wins": 12, "losses": 5, "draws": 1, "win_rate": 71,
          "recent": [{"result": "win", "fighter": "A", "opponents": ["DMX"], "venue": "Club"},
                     {"result": "loss", "fighter": "A", "opponents": [], "venue": ""}]}
    assert core.record_line(st) == "12 W · 5 L · 1 D · 71%"
    assert core.record_line({"wins": 0, "losses": 0}) == "0 W · 0 L"
    assert core.recent_lines(st) == ["🟢 W  A vs DMX  · Club", "🔴 L  A vs ?"]
    lines = core.leaderboard_lines([{"rank": 1, "name": "Masa", "wins": 3, "losses": 0, "win_rate": 100},
                                    {"rank": 4, "name": "X", "wins": 1, "losses": 2, "win_rate": 33}])
    assert lines[0].startswith("🥇 **Masa**") and lines[1].startswith("` 4.` **X**")


def test_report_texts():
    crash = {"id": "a1b2c3", "kind": "crash", "build": "18", "player": "not signed in",
             "crash": "[CRASH] Access violation | in sub_0010A7D0+0x1E8"}
    assert core.report_title(crash).startswith("💥 Crash report a1b2c3")
    fields = dict(core.report_fields(crash))
    assert fields["Build"] == "18" and "a1b2c3" in fields["Get it"] and "sub_0010A7D0" in fields["Crash"]
    f9 = {"id": "d4e5f6", "kind": "picture", "build": "18", "player": "account 1, Masa (@masa)"}
    assert core.report_title(f9) == "🐞 F9 report d4e5f6"
    assert "Crash" not in dict(core.report_fields(f9))
    assert all(len(v) <= 1024 for _, v in core.report_fields(dict(crash, crash="x" * 5000)))


def test_polls_channel_keeps_polls_and_team_posts_only():
    assert core.keep_in_polls(True, False, False)
    assert core.keep_in_polls(False, True, False)
    assert core.keep_in_polls(False, False, True)
    assert not core.keep_in_polls(False, False, False)
    assert any(name == core.POLLS_CHANNEL and kind == "polls" for _, chans in core.LAYOUT for name, kind in chans)


def test_poll_form_parsing():
    ok, err = core.parse_poll("  Next  mode? ", "Team\n- Cage\n\nteam\nInferno")
    assert err is None and ok == ("Next mode?", ["Team", "Cage", "Inferno"], core.POLL_HOURS)
    assert core.parse_poll("Q", "Only one")[0] is None
    assert core.parse_poll("Add tournaments", "")[0][1] == list(core.POLL_IDEA_ANSWERS)
    assert core.parse_poll("", "a\nb")[0] is None
    assert core.parse_poll("Q", "\n".join(str(i) for i in range(11)))[0] is None
    assert len(core.parse_poll("Q", "x" * 80 + "\nb")[0][1][0]) == core.POLL_ANSWER_LEN
