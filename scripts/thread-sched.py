#!/usr/bin/env python3
"""How the scheduler sees every thread of the running port: state, priority, CPU mask, progress.

    python scripts/thread-sched.py [--pid N] [--preset win-x64-release] [--interval 1.0]

One line per thread: its id, the function it was started at (named from the
build's linker map), its state and wait reason, its current and base priority,
the logical CPUs it may run on, and how much CPU time and how many context
switches it had over the interval. Sorted by CPU time.

It never suspends a thread and never reads a thread context, so it cannot get
stuck on a thread that is not being scheduled. scripts/native-stacks.py and
sample-threads.py suspend threads to read them, and Windows only completes a
suspension when the thread next runs: on 2026-10-20 a starved interrupt thread
held native-stacks.py in an unkillable wait. Run this first on a hang.

A thread that is Ready and made no progress over the interval is "starved":
something of equal or higher priority holds every CPU in its mask.

Reads only. Windows only (NtQuerySystemInformation, NtQueryInformationThread).
"""
import argparse
import bisect
import ctypes
import ctypes.wintypes as wt
import os
import struct
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

STATES = ["Initialized", "Ready", "Running", "Standby", "Terminated", "Wait",
          "Transition", "DeferredReady", "GateWait"]
WAIT_REASONS = ["Executive", "FreePage", "PageIn", "PoolAllocation", "DelayExecution",
                "Suspended", "UserRequest", "WrExecutive", "WrFreePage", "WrPageIn",
                "WrPoolAllocation", "WrDelayExecution", "WrSuspended", "WrUserRequest",
                "WrEventPair", "WrQueue", "WrLpcReceive", "WrLpcReply", "WrVirtualMemory",
                "WrPageOut", "WrRendezvous", "WrKeyedEvent", "WrTerminated", "WrProcessInSwap",
                "WrCpuRateControl", "WrCalloutStack", "WrKernel", "WrResource", "WrPushLock",
                "WrMutex", "WrQuantumEnd", "WrDispatchInt", "WrPreempted", "WrYieldExecution",
                "WrFastMutex", "WrGuardedMutex", "WrRundown", "WrAlertByThreadId", "WrDeferredPreempt"]

# x64 layouts. SYSTEM_PROCESS_INFORMATION is 0x100 bytes and its threads follow it,
# each SYSTEM_THREAD_INFORMATION 0x50 bytes.
PROC_NEXT, PROC_NTHREADS, PROC_PID, PROC_SIZE = 0x00, 0x04, 0x50, 0x100
THREAD_SIZE = 0x50


def parse_threads(buf, pid):
    """The SYSTEM_THREAD_INFORMATION records of one process, from a
    SystemProcessInformation buffer. Pure, for the tests.

    Returns {tid: dict(cpu_100ns, switches, state, wait, priority, base, start)}."""
    off = 0
    while True:
        nxt, nthreads = struct.unpack_from("<II", buf, off + PROC_NEXT)
        (p,) = struct.unpack_from("<Q", buf, off + PROC_PID)
        if p == pid:
            out = {}
            for i in range(nthreads):
                t = off + PROC_SIZE + i * THREAD_SIZE
                kernel, user = struct.unpack_from("<qq", buf, t)
                start, = struct.unpack_from("<Q", buf, t + 0x20)
                _proc, tid = struct.unpack_from("<QQ", buf, t + 0x28)
                prio, base, switches, state, wait = struct.unpack_from("<iiIII", buf, t + 0x38)
                out[tid] = dict(cpu_100ns=kernel + user, switches=switches, state=state,
                                wait=wait, priority=prio, base=base, start=start)
            return out
        if not nxt:
            return {}
        off += nxt


def starved(before, after):
    """Ready at both looks and not one context switch or tick of CPU between."""
    return (before["state"] == 1 and after["state"] == 1
            and after["switches"] == before["switches"]
            and after["cpu_100ns"] == before["cpu_100ns"])


def plan_walk(before, after):
    """In what order a tool that suspends threads may take them. Pure, for the tests.

    Windows completes a suspension, and answers GetThreadContext, only when
    the thread next runs. Threads that are using CPU go first, and the tool
    keeps them suspended until it is done: that frees the CPUs they held, so
    the rest -- waiting threads pinned to the same CPU included -- can run
    the few instructions their suspension needs. A thread starved before the
    tool started is skipped: if something outside the process holds its CPU,
    a suspension would never complete and the tool would hang with it.

    Returns (first, rest, skipped) as lists of thread ids."""
    first, rest, skipped = [], [], []
    for tid, t in after.items():
        b = before.get(tid)
        if b is None:
            rest.append(tid)
        elif starved(b, t):
            skipped.append(tid)
        elif t["state"] == 2 or t["cpu_100ns"] > b["cpu_100ns"]:
            first.append(tid)
        else:
            rest.append(tid)
    return first, rest, skipped


def plan_for(pid, interval=0.3):
    """plan_walk() over two snapshots of a live process `interval` seconds apart."""
    before = snapshot(pid)
    time.sleep(interval)
    return plan_walk(before, snapshot(pid))


def describe(t):
    state = STATES[t["state"]] if t["state"] < len(STATES) else str(t["state"])
    if t["state"] == 5:
        state += "/" + (WAIT_REASONS[t["wait"]] if t["wait"] < len(WAIT_REASONS) else str(t["wait"]))
    return state


def snapshot(pid):
    ntdll = ctypes.WinDLL("ntdll")
    size = 1 << 20
    while True:
        buf = ctypes.create_string_buffer(size)
        need = wt.ULONG(0)
        status = ntdll.NtQuerySystemInformation(5, buf, size, ctypes.byref(need)) & 0xFFFFFFFF
        if status == 0xC0000004:                     # STATUS_INFO_LENGTH_MISMATCH
            size = max(size * 2, need.value + 65536)
            continue
        if status:
            raise SystemExit("NtQuerySystemInformation failed: %#x" % status)
        return parse_threads(buf.raw, pid)


def thread_info(tid):
    """(AffinityMask or None, Win32 start address or 0) of one thread.

    The mask is THREAD_BASIC_INFORMATION's; the start address is asked for
    separately because SystemProcessInformation reports 0 without admin rights."""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    ntdll = ctypes.WinDLL("ntdll")
    k32.OpenThread.restype = wt.HANDLE
    h = k32.OpenThread(0x0040, False, tid)            # THREAD_QUERY_INFORMATION
    if not h:
        return None, 0
    try:
        mask = None
        tbi = ctypes.create_string_buffer(0x30)
        if not ntdll.NtQueryInformationThread(wt.HANDLE(h), 0, tbi, 0x30, None):
            mask = struct.unpack_from("<Q", tbi.raw, 0x20)[0]
        start = ctypes.c_uint64(0)
        ntdll.NtQueryInformationThread(wt.HANDLE(h), 9, ctypes.byref(start), 8, None)
        return mask, start.value
    finally:
        k32.CloseHandle(wt.HANDLE(h))


def namer(pid, preset):
    """Start address -> name, from the linker map of the build the process runs."""
    spec_path = os.path.join(REPO, "scripts", "sample-threads.py")
    import importlib.util
    spec = importlib.util.spec_from_file_location("sample_threads", spec_path)
    st = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(st)
    base, size = st.module_base(pid)
    mappath = os.path.join(REPO, "build", preset, "defjam_recomp.map")
    if not base or not os.path.exists(mappath):
        return lambda a: "%#x" % a
    pref, addrs, names = st.load_map(mappath)

    def name(a):
        if base <= a < base + size and addrs:
            off = a - base + pref
            i = bisect.bisect_right(addrs, off) - 1
            if i >= 0:
                return "%s+0x%X" % (names[i], off - addrs[i])
        return "%#x" % a
    return name


def find_pid(name="defjam_recomp.exe"):
    import subprocess
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq " + name, "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0].lower() == name:
            return int(parts[1])
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--pid", type=int, help="this process, when more than one copy runs")
    ap.add_argument("--preset", default="win-x64-release")
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--all", action="store_true", help="also idle waiting threads")
    a = ap.parse_args(argv)
    pid = a.pid or find_pid()
    if not pid:
        print("defjam_recomp.exe is not running")
        return 1
    before = snapshot(pid)
    time.sleep(a.interval)
    after = snapshot(pid)
    name = namer(pid, a.preset)
    rows = []
    for tid, t in after.items():
        b = before.get(tid, t)
        cpu_ms = (t["cpu_100ns"] - b["cpu_100ns"]) / 10000.0
        sw = t["switches"] - b["switches"]
        busy = cpu_ms > 0 or sw > 0 or t["state"] != 5
        if not (busy or a.all):
            continue
        mask, start = thread_info(tid)
        rows.append((cpu_ms, tid, t, sw, mask, start or t["start"], starved(b, t)))
    rows.sort(key=lambda r: -r[0])
    print("pid %d, %.1f s interval; %d threads, %d shown (--all for idle waiters)"
          % (pid, a.interval, len(after), len(rows)))
    print("%7s %-22s %4s/%-4s %-18s %8s %7s  %s"
          % ("tid", "state", "prio", "base", "cpu mask", "cpu ms", "switch", "start"))
    for cpu_ms, tid, t, sw, mask, start, st in rows:
        print("%7d %-22s %4d/%-4d %-18s %8.1f %7d  %s%s"
              % (tid, describe(t), t["priority"], t["base"], "?" if mask is None else "%#x" % mask,
                 cpu_ms, sw, name(start), "   <- STARVED" if st else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
