"""defjam-mcp v2 (tools/defjam-mcp/djlib): the lock, data roots and log parsing, on synthetic data.

No game, no game data: the logs here are written by hand in the formats the game prints.
"""
import json
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "defjam-mcp"))
from djlib import logparse, machine, roots, scenarios  # noqa: E402


# ---- the lock ------------------------------------------------------------------------------------

def test_parse_lock_by_hand_and_by_tool():
    assert machine.parse_lock("agent-x (hotfix) 2026-10-27T22:00:15 online8\n") == {
        "who": "agent-x (hotfix) 2026-10-27T22:00:15 online8", "pid": None, "started": None}
    info = machine.parse_lock("agent-y 2026 note\npid=123 started=456 token=ab host=pc\n")
    assert info["pid"] == 123 and info["started"] == 456


def test_stale_only_with_a_dead_or_reused_pid():
    assert not machine.lock_is_stale({"pid": None})                      # by hand: never stale
    assert machine.lock_is_stale({"pid": 5, "started": 9}, started_of=lambda p: None)     # gone
    assert machine.lock_is_stale({"pid": 5, "started": 9}, started_of=lambda p: 10)       # pid reused
    assert not machine.lock_is_stale({"pid": 5, "started": 9}, started_of=lambda p: 9)    # alive


def test_priority_file_lets_only_its_agent_through():
    text = "agent-addebee (build-32 release checklist) has priority since 2026-10-27T22:12:07; others wait\n"
    assert machine.priority_holder(text) == "agent-addebee"
    assert not machine.priority_blocks(None, "agent-me")
    assert machine.priority_blocks(text, "agent-me")
    assert not machine.priority_blocks(text, "agent-addebee")


@pytest.fixture
def no_games(monkeypatch):
    monkeypatch.setattr(machine, "games", lambda exclude=(): [])


def test_lock_take_release_and_wait(tmp_path, no_games):
    path = str(tmp_path / "test-lock.txt")
    a = machine.MachineLock(path=path, agent="agent-a", log=lambda m: None)
    assert a.acquire(timeout=0)
    text = open(path).read()
    assert text.startswith("agent-a ") and "pid=%d" % os.getpid() in text
    b = machine.MachineLock(path=path, agent="agent-b", log=lambda m: None)
    assert not b.acquire(timeout=0, poll=0.01)          # held by a live process
    a.release()
    assert not os.path.exists(path)
    assert b.acquire(timeout=0)
    b.release()


def test_lock_removes_a_stale_lock(tmp_path, no_games, monkeypatch):
    path = tmp_path / "test-lock.txt"
    path.write_text("agent-gone 2026-10-27 x\npid=999999 started=1 token=zz host=pc\n")
    real = machine.process_started
    monkeypatch.setattr(machine, "process_started", lambda pid: None if pid == 999999 else real(pid))
    lock = machine.MachineLock(path=str(path), agent="agent-a", log=lambda m: None)
    assert lock.acquire(timeout=0)
    assert "agent-a" in path.read_text()
    lock.release()


def test_lock_waits_for_a_hand_written_lock_and_for_games(tmp_path, monkeypatch):
    path = tmp_path / "test-lock.txt"
    path.write_text("agent-other (by hand) 2026-10-27T22:00\n")
    monkeypatch.setattr(machine, "games", lambda exclude=(): [])
    assert not machine.MachineLock(path=str(path), agent="a", log=lambda m: None).acquire(timeout=0, poll=0.01)
    path.unlink()
    monkeypatch.setattr(machine, "games", lambda exclude=(): [(4242, r"C:\Users\x\Desktop\DefJam\app\defjam_recomp.exe")])
    msgs = []
    assert not machine.MachineLock(path=str(path), agent="a", log=msgs.append).acquire(timeout=0, poll=0.01)
    assert "another game copy runs" in msgs[0] and not path.exists()


def test_lock_honours_the_priority_file(tmp_path, no_games):
    lock_path = tmp_path / "test-lock.txt"
    (tmp_path / "test-priority.txt").write_text("agent-hotfix (release) has priority\n")
    msgs = []
    other = machine.MachineLock(path=str(lock_path), agent="agent-me", log=msgs.append)
    assert not other.acquire(timeout=0, poll=0.01)
    assert "priority to agent-hotfix" in msgs[0]
    mine = machine.MachineLock(path=str(lock_path), agent="agent-hotfix", log=msgs.append)
    assert mine.acquire(timeout=0)
    mine.release()


def test_env_rules():
    assert machine.check_env({"RECOMP_GUEST_CORES": "all"})
    assert machine.check_env({"RECOMP_ACCOUNT_SERVER": "accounts.defjam.online:443"})
    assert machine.check_env({"RECOMP_NETPLAY_SERVER": "play.defjam.online:27777"})
    assert not machine.check_env({"RECOMP_NETPLAY_SERVER": "127.0.0.1:27900", "RECOMP_ACCOUNT_SERVER": "127.0.0.1:9"})
    e = machine.safe_env(4, r"D:\x", {"RECOMP_WINDOW_BACKGROUND": None})
    assert e["RECOMP_GUEST_CORE"] == "4" and e["RECOMP_ACCOUNT_SERVER"] == "127.0.0.1:9"
    assert "RECOMP_WINDOW_BACKGROUND" not in e and e["RECOMP_NETPLAY_SERVER_EU"].startswith("127.0.0.1")
    with pytest.raises(ValueError):
        machine.safe_env(2, r"D:\x", {"RECOMP_GUEST_CORES": "all"})


def test_preflight_refuses_a_copy_without_its_game_folder(tmp_path):
    bad = machine.preflight(str(tmp_path / "missing.exe"), str(tmp_path / "root"), cwd=str(tmp_path))
    assert len(bad) == 4 and "not built" in bad[0] and "game" in bad[1]


# ---- data roots ----------------------------------------------------------------------------------

@pytest.fixture
def fake_data(tmp_path, monkeypatch):
    data = tmp_path / "data"
    (data / "extracted").mkdir(parents=True)
    (data / "extracted" / "default.xbe").write_bytes(b"XBEH")
    (data / "online-art").mkdir()
    (data / "save" / "UserData").mkdir(parents=True)
    (data / "save" / "UserData" / "Options").write_bytes(b"opts")
    (data / "save" / "Partition1.img").write_bytes(b"\0" * 64)
    (data / "save" / "Partition0.img").write_bytes(b"p0")
    monkeypatch.setattr(machine, "DATA", str(data))
    monkeypatch.setattr(machine, "agent_id", lambda: "agent-test1234")
    return data


def test_root_create_and_destroy_keeps_the_dump(fake_data, tmp_path):
    fixture = tmp_path / "fixture" / "save" / "UserData"
    fixture.mkdir(parents=True)
    (fixture / "Fighter").write_bytes(b"made")
    root = roots.create("c0", fixture=str(tmp_path / "fixture"))
    assert os.path.basename(root) == "data-dj-test1234-c0"
    assert roots.is_link(os.path.join(root, "extracted"))
    assert os.path.isfile(os.path.join(root, "extracted", "default.xbe"))
    assert os.path.isfile(os.path.join(root, "save", "UserData", "Options"))
    assert os.path.isfile(os.path.join(root, "save", "UserData", "Fighter"))         # the fixture
    assert os.path.isfile(os.path.join(root, "save", "Partition0.img"))
    assert not os.path.exists(os.path.join(root, "save", "Partition1.img"))         # no cache images
    assert [p for p, _ in roots.listing()] == [root]
    roots.destroy(root)
    assert not os.path.exists(root)
    assert (fake_data / "extracted" / "default.xbe").read_bytes() == b"XBEH"
    assert (fake_data / "save" / "UserData" / "Options").read_bytes() == b"opts"


def test_destroy_refuses_unmanaged_folders(fake_data, tmp_path):
    with pytest.raises(PermissionError):
        roots.destroy(str(fake_data))
    other = tmp_path / "data-other"
    other.mkdir()
    with pytest.raises(PermissionError):
        roots.destroy(str(other))


# ---- log parsing ---------------------------------------------------------------------------------

CRASH = """[CRASH] Access violation at RIP=0x140D999BB, fault addr=0x10A3BB98C (read)
  Xbox regs: eax=0xFA3BB98C ecx=0x003BB8F4 edx=0xFE800000 esp=0x01485B38
  Xbox VA of fault: 0xFA3BB98C
  in sub_001BBC10+0x8B
  guest stack (scan, not a walk -- includes stale frames):
    [esp+0   ] 0x001B2671
    [esp+4   ] 0x003BB8F4
    [esp+56  ] 0x001B3778
  Native stack (exe base 0x140000000, words pointing into it):
    [rip] 0x140D999BB
    [1] 0x140D8B726
"""

ONLINE = """[SETTINGS] defaults only (RECOMP_SETTINGS=none)
D3D8: graphics card Test GPU (vendor 10DE, device 1, 8000 MB), feature level B000
[INPUT] Virtual test pad is player 1
[FUNCCALL] Control.GetScreenInfo(intMain/mainMenu.swf)
[NETPLAY] START: seed AAAA0001, type 0, rules 0, venue 7, 2 players, this PC is player 1
[NETPLAY] input delay 2 steps (pings 0/0/-1/-1 ms)
[NETPLAY] rollback off for this match: King of the Hill fights play in lockstep
[AUTOMATCH] Game.StartBattleMode() -> ok
[AUTOMATCH] Game.StartGame() -> ok
[AUTOMATCH] sequence done
[NETPLAY] match 11111111 starts in lockstep (2 players, delay 2); loaded in 26.0 s since the START
[TEST-EVENT] {"event":"match_start","match":1,"step":1,"host_ms":1000,"fighters":[{"slot":0,"character":55},{"slot":1,"character":22}]}
[TEST-EVENT] {"event":"attack","match":1,"step":90,"slot":0,"target_slot":1}
[TEST-EVENT] {"event":"damage","match":1,"step":91,"slot":1,"health_before":300,"health_after":290}
[TEST-EVENT] {"event":"attack","match":1,"step":120,"slot":1,"target_slot":0}
[TEST-EVENT] {"event":"damage","match":1,"step":121,"slot":0,"health_before":300,"health_after":280}
[NETPLAY] in step: 10 checksums compared and equal (match 11111111, step 600)
[TEST-KO] match 1 step 600: slot 1 knocked out
[TEST-EVENT] {"event":"result","match":1,"step":601,"host_ms":11000,"winners":[0],"code":33}
[NETPLAY] match result: winners 0x1, code 0x1; reporting it when signed in
[FE-RESULT] Game.GetMatchSummary(0) = win=1&isAI=0&fighter=Doc&fid=55&cid=0&total=1
[FE-RESULT] Game.GetMatchSummary(1) = win=0&isAI=0&fighter=Magic&fid=22&cid=1&total=1
[FUNCCALL] Game.UpdateMatchRewardsPoints()
[FUNCCALL] Control.GetScreenInfo(battle/cmtype.swf)
[FUNCCALL] Audio.PlayMusic()
[FUNCCALL] Game.ResetMatchTypeList()
[FUNCCALL] Game.ResetMatchData()
[NETPLAY] START: seed AAAA0002, type 0, rules 0, venue 4, 2 players, this PC is player 1
[AUTOMATCH] Game.StartBattleMode() -> ok
[AUTOMATCH] Game.StartGame() -> ok
[AUTOMATCH] sequence done
[NETPLAY] match 22222222 starts in lockstep (2 players, delay 2); loaded in 1.1 s since the START
[TEST-EVENT] {"event":"match_start","match":2,"step":1,"host_ms":20000}
[NETPLAY] in step: 30 checksums compared and equal (match 22222222, step 1200)
[NETPLAY] desync in match 22222222 at step 1234 (local 0000AAAA, remote 0000BBBB)
[NETPLAY] desync detail: fighter 1 health differs; the CPU fighters' generators agree; the shared generator and time scale agree
[NETPLAY] gave up at step 1300 waiting for the entries up to 1300 (final 0, phase 1)
[FE-RESULT] Game.GetMatchSummary(0) = win=1&cid=-99&total=1
""" + CRASH


def test_parse_matches():
    ms = logparse.parse_matches(ONLINE.splitlines())
    assert [m["id"] for m in ms] == ["11111111", "22222222"]
    assert ms[0]["mode"] == "lockstep" and ms[0]["delay"] == 2 and ms[0]["load_s"] == 26.0
    assert ms[0]["checks"] == 10 and ms[1]["checks"] == 20
    assert ms[1]["desync"]["step"] == 1234 and "fighter 1 health" in ms[1]["desync_detail"]
    assert ms[1]["gave_up"] and ms[0]["result"] == "winners 0x1, code 0x1"


def test_rematch_without_a_start_line_is_its_own_match():
    lines = ["[NETPLAY] START: seed A, type 0, rules 0, venue 1, 2 players, this PC is player 2",
             "[NETPLAY] match 0000000A starts in lockstep (2 players, delay 5); loaded in 0.3 s since the START",
             "[NETPLAY] match 0000000B starts in lockstep (2 players, delay 5); loaded in 95.7 s since the START"]
    ms = logparse.parse_matches(lines)
    assert len(ms) == 2 and ms[1]["start"] is None and ms[1]["id"] == "0000000B"


def test_condense_crash_with_and_without_callers():
    block = logparse.crash_blocks(CRASH.splitlines())[0][1]
    plain = logparse.condense_crash(block)
    assert plain[0].startswith("[CRASH] Access violation") and "sub_001BBC10+0x8B" in plain[1]
    assert "001B2671" in plain[2] and "native: 2 words" in plain[3]
    sites = {0x001B2671: ("sub_001B2480", "sub_001BBC10"), 0x001B3778: ("sub_001B3490", "sub_001B2480")}
    named = logparse.condense_crash(block, sites)
    assert "sub_001B2480->sub_001BBC10 | sub_001B3490->sub_001B2480" in named[2]


def test_digest_is_short_and_keeps_what_matters():
    noise = "\n".join("[GPU] flip %d" % i for i in range(5000)) + "\n"
    many = "".join("[NETPLAY] notice: Loading the match... %d\n[MEM] contiguous memory at the match start: %d MB\n" % (i, i)
                   for i in range(200))
    out = logparse.digest(noise + many + ONLINE, max_lines=40)
    text = "\n".join(out)
    assert len(out) <= 41
    assert "[CRASH] Access violation" in text and "sub_001BBC10" in text
    assert "desync in match 22222222" in text and "fighter 1 health differs" in text
    assert "gave up at step 1300" in text
    assert "match: START type 0 venue 7" in text and "11111111 lockstep d2 load 26.0s" in text
    assert "graphics card Test GPU" in text
    assert "screens: mainMenu > cmtype" in text


def test_fe_order_ok_menu_and_bad():
    seqs = logparse.fe_sequences(ONLINE.splitlines())
    assert [s["order"] for s in seqs] == ["menu", "ok"]
    bad = ["[FUNCCALL] Control.GetScreenInfo(battle/cmtype.swf)", "[FUNCCALL] Audio.PlayMusic()",
           "[AUTOMATCH] Game.StartBattleMode() -> ok", "[AUTOMATCH] Game.StartGame() -> ok", "[AUTOMATCH] sequence done",
           "[FUNCCALL] Game.ResetMatchTypeList()", "[FUNCCALL] Game.ResetMatchData()",
           "[NETPLAY] match 33333333 starts in lockstep (2 players, delay 2); loaded in 1.0 s since the START"]
    assert logparse.fe_sequences(bad)[0]["order"].startswith("BAD: the screen's reset came after")


def test_analyze_and_rows():
    a = logparse.analyze_copy(ONLINE)
    m1, m2 = a["started"]
    assert m1["hits"] == {0: 1, 1: 1} and m1["winners"] == [0] and m1["ko"]
    assert m1["summary"] == {0: 0, 1: 1} and m1["rewards"] and m1["fe"] == "menu"
    assert m2["fe"] == "ok" and m2["summary"] == {0: -99} and not m2["rewards"]
    assert m2.get("crash", "").startswith("[CRASH]")
    problems = []
    rows, ok = scenarios.online_rows({0: a, 1: a}, 2, False, problems)
    assert not ok and rows[0]["hits"] == [1, 1] and rows[0]["summary"] == ["cid 0,1", "cid 0,1"]
    joined = "\n".join(problems)
    assert "desync at step 1234" in joined and "gave up" in joined
    assert "summary not accepted" in joined and "controller ids" in joined and "[CRASH]" in joined


def test_offline_summary():
    o = logparse.offline_summary(ONLINE)
    assert o["started"] and o["fighters"] == [55, 22] and o["fight_s"] == 10.0 and o["rewards"]


def test_online_tail_counts_summaries_per_match():
    t2 = scenarios.online_tail(3, 2, False)
    assert "@game.getmatchsummary(,a" in t2 and "@game.getmatchsummary(#3," in t2 and "@game.getmatchsummary(#5," in t2
    assert "@game.startgame(#2," in t2 and "@game.startgame(#3," in t2 and "@game.startgame(#4" not in t2
    t4 = scenarios.online_tail(2, 4, True)
    assert "@game.getmatchsummary(#5," in t4 and "right:" not in t4


def test_compact_json_is_one_line_per_match():
    r = {"scenario": "online", "ok": True, "matches": [{"n": k} for k in range(8)], "problems": []}
    text = scenarios.compact_json(r)
    assert len(text.splitlines()) == 15
    assert json.loads(text) == r
