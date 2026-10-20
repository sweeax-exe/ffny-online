"""scripts/thread-sched.py: the thread records it parses and the order it lets tools suspend in.

No process is inspected here; the records are built by hand.
"""
import importlib.util
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("thread_sched", os.path.join(ROOT, "scripts", "thread-sched.py"))
ts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ts)


def process_record(pid, threads, last=False):
    """One SYSTEM_PROCESS_INFORMATION with its SYSTEM_THREAD_INFORMATION array (x64)."""
    size = ts.PROC_SIZE + ts.THREAD_SIZE * len(threads)
    buf = bytearray(size)
    struct.pack_into("<II", buf, ts.PROC_NEXT, 0 if last else size, len(threads))
    struct.pack_into("<Q", buf, ts.PROC_PID, pid)
    for i, (tid, cpu, switches, state, wait, prio, base, start) in enumerate(threads):
        t = ts.PROC_SIZE + i * ts.THREAD_SIZE
        struct.pack_into("<qq", buf, t, cpu // 2, cpu - cpu // 2)        # kernel + user
        struct.pack_into("<Q", buf, t + 0x20, start)
        struct.pack_into("<QQ", buf, t + 0x28, pid, tid)
        struct.pack_into("<iiIII", buf, t + 0x38, prio, base, switches, state, wait)
    return bytes(buf)


def test_parse_finds_the_process_and_reads_each_thread():
    buf = (process_record(4, [(8, 100, 1, 5, 6, 8, 8, 0x1000)])
           + process_record(77, [(10, 5000, 40, 2, 0, 16, 8, 0x2000),
                                 (11, 0, 9, 1, 0, 15, 15, 0x3000)], last=True))
    got = ts.parse_threads(buf, 77)
    assert set(got) == {10, 11}
    assert got[10] == dict(cpu_100ns=5000, switches=40, state=2, wait=0, priority=16, base=8,
                           start=0x2000)
    assert got[11]["base"] == 15 and got[11]["state"] == 1
    assert ts.parse_threads(buf, 999) == {}


def t(cpu, switches, state):
    return dict(cpu_100ns=cpu, switches=switches, state=state, wait=0, priority=8, base=8, start=0)


def test_walk_plan_takes_busy_threads_first_and_never_a_starved_one():
    before = {1: t(100, 5, 2),     # running
              2: t(0, 3, 1),       # ready, never scheduled in between: starved
              3: t(50, 7, 5),      # waiting, idle
              4: t(10, 1, 5)}      # waiting at both looks but ran in between
    after = {1: t(900, 6, 2), 2: t(0, 3, 1), 3: t(50, 7, 5), 4: t(20, 2, 5), 5: t(0, 0, 5)}
    first, rest, skipped = ts.plan_walk(before, after)
    assert sorted(first) == [1, 4]
    assert skipped == [2]
    assert sorted(rest) == [3, 5]          # 5 is new since the first look


def test_a_ready_thread_that_got_one_switch_is_not_starved():
    assert ts.starved(t(0, 3, 1), t(0, 3, 1))
    assert not ts.starved(t(0, 3, 1), t(0, 4, 1))
    assert not ts.starved(t(0, 3, 5), t(0, 3, 1))
