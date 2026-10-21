# Two boot intermittents (2026-10-20): the boot that never draws, and the call to 0x0002A167

Research date: 2026-10-20. Sources: the logs named below, live processes measured with
`scripts/thread-sched.py` (new) and `scripts/native-stacks.py`, the lifted C, and
`tools/xboxrecomp/src/kernel/kernel_bridge.c`. Tags: [V] verified (measured, or read in code
at the cited place), [I] inferred, [U] unknown.

## Short answer

1. **Boot hang.** It has nothing to do with the save guard or disk I/O. Direct3D's main thread spins on the GPU's
   fence word, on the one guest CPU, at a priority above the interrupt thread's. The GPU waits for an
   interrupt only that thread can deliver, so neither side moves again. It is in the runtime, so
   **players can hit it**. A second copy of the game on the same PC makes it much more likely and spreads it to the
   other copy. Fixed with a starvation watchdog in the toolkit (`kernel_bridge.c`, `kernel_timer_watchdog`).
2. **`[ICALL] Failed to resolve VA 0x0002A167` from 0x0027EC6E.** The pad driver completes a USB request after its owner
   function has returned: XInput's capability query, with its URB and event on the stack. The completion
   routine pointer read from the reused stack is a stale return address. The cause is the runtime's handling of events the
   title builds itself; it is the Windows face of the stale-stack call D95 recorded on Linux.
   It can reach players. The skipped call itself is harmless, but the late completion also writes four
   words into whatever now owns that stack. Not fixed here; see the follow-up below. Details in
   [stale-urb-completion.md](stale-urb-completion.md).
3. **The freeze after a good start (2026-10-21, §6).** The same deadlock, mid-boot, after the watchdog switched
   itself off: in boots where the main thread stays at priority 16, each fence spin needs a rescue, and the
   rescue's re-pin closed its window too late to be watched again. Fixed in the toolkit (window closed before the
   pin, a lapsed window counts as closed). What holds the main thread at 16 is still unknown.

## 1. The hang

### What the logs show

- [V] Hung boots (`run-20261020-153433`, `-170225`, `-171233`, `-183451`, `-183758`; `-141425` got its
  first interrupts some 10 s late and still never presented) and good ones (`-164727`, `-171453`) are line-for-line identical up to
  `KeConnectInterrupt: vector 3` (the GPU's interrupt, Direct3D's service routine `0x00222FD0`). The last `[PATH]`
  line, `...TDATA\45410049\$u\contentmeta.xbx`, is the last file the boot opens before Direct3D
  starts. It fails the same way in good boots and is not where the boots part.
- [V] Where they part: in a good boot the USB deferred routine queued at the same moment runs at once
  (`[DPC] running routine 0x00284476`), the NV2A interrupt is delivered
  (`[NV2A] interrupt 0x00001000 -> ISR claimed it`), and Direct3D's deferred routine `0x002239A0` follows.
  In a hung boot none of the three ever appears, and the OHCI model reports
  `interrupts masked for 2 s: the USB deferred routine has not run`. All three are the work of one
  thread, `kernel_timer_thread`, so that thread stopped running.
- [V] `[NV2A] DMA_PUT ... DMA_GET ... (GPU behind)` repeats with fixed values: the executor stopped.
  `0 flips`: the boot never presents.

### The save-guard hypothesis is refuted

- [V] `SaveGuard.__enter__` copies the 4.9 GB save root in 1.2 s and `__exit__` restores and compares it in 5.0 s
  (measured on `D:\DefJam\data`, NTFS). The copy lands in the write cache before the game starts.
- [V] A hung boot stays hung for the rest of its run (2 to 15 minutes), and a disk or virus-scan stall cannot do
  that. The live hung processes measured below were waiting on CPU time, not I/O.
- [I] The guard needs no change for this. Its byte-identical restore was kept as it is.

### What a hung process looks like (live, `scripts/thread-sched.py`)

Measured on three hung processes. Each line is a thread: state, current/base priority, CPU mask,
CPU time in a 1 s interval, context switches in that interval.

```
pid 44784 (soak boot, alone)            main thread  Running  16/8  mask 0x4  ~1000 ms  0 switches
                                        timer thread Ready    15/15 mask 0x4     0 ms  0 switches  STARVED
pid 42560 (second copy beside a soak)   main thread  Running  20/8  mask 0x4  1000 ms   0 switches
                                        timer thread Ready    15/15 mask 0x4     0 ms              STARVED
                                        a worker     Ready    15/8  mask 0x4     0 ms              STARVED
pid 28696 (the soak boot beside it)     main thread  Ready    15/8  mask 0x4     0 ms              STARVED
```

- [V] The spinning thread is the title's main thread. Its native stack is
  `sub_0021EC50+0x91A <- sub_0021EF60 <- sub_00218F80 <- ... <- bridge_PsCreateSystemThreadEx <- WinMain`,
  the same in both spinning processes.
- [V] `sub_0021EC50` is Direct3D's wait for push-buffer space. Its loop at `0x0021ED87` reads the GPU's fence
  word (`MEM32(MEM32(device + 0x30))`) until the GPU has passed the wanted fence. It makes no call and enters no kernel
  routine, so the runtime's existing holds (`kernel_gpu_preempt` on kernel entry, `sub_0021EB90_enter`)
  never run while it spins.
- [V] The GPU executor is stopped at a software method (`[FENCE]`/`[PGRAPH]` lines; `DMA_GET` frozen). Only the
  interrupt, raised and serviced on `kernel_timer_thread`, releases it.
- [V] All guest threads, the timer thread among them, are pinned to logical CPU 2 (`xbox_PinToGuestCore`,
  D28; `src/main.c:848` for the main thread). The timer thread runs at TIME_CRITICAL (15) so that it
  preempts the title as an interrupt does. The comment beside it already names this exact case.
- [V] The spinning thread ran at 16 and at 20 with a base of 8, which is the realtime band, without a single
  context switch. A Ready thread at 15 on the same and only CPU can never preempt it, so this is a deadlock and not a
  slowdown.
- [U] What raises the main thread above 15. It is not one of the runtime's critical sections: all were free in pid
  42560, read from memory through the linker map. Nothing in the source calls MMCSS
  (`AvSetMmThreadCharacteristics`). The only realtime thread in the process is XAudio2's (22).
  A lock-ownership boost (Windows AutoBoost) or a boost from a driver call earlier in the frame is the likeliest
  reading. It needs a kernel trace (xperf/WPR, ReadyThread and priority events) to settle.

### Why some days are worse: a second copy on the same CPU

- [V] Every copy of the game pins its guest threads to CPU 2 unless `RECOMP_GUEST_CORE` says otherwise
  (`kernel_bridge.c:500`). Online tests start two copies a few seconds apart (`run-20261020-135717` with
  `-135724`, `-141425` with `-141431`: both hung in both pairs). Twice this session a harness boot hung
  while the owner's installed 0.7.2 was running.
- [V] With a copy spinning at 20 on CPU 2, the other copy's threads on CPU 2 starve too, and so does the
  exit of a killed copy: Windows runs termination on each thread, and pid 28696 kept one thread alive for
  minutes after `TerminateProcess`. It went the moment the spinning copy was stopped.
- [V] A plain busy loop on CPU 2 at normal priority did not cause a hang in 10 boots. It only slowed a killed
  copy's exit past 15 s. Contention alone is not the trigger; the realtime-band spinner is.
- [V] Frequency, old build: 0 of 10 boots alone (19:05-19:13), then 1 of 1 alone (`-192044`), 1 of 1 beside a
  second copy (`-191543`), 2 of 2 beside the installed game (`-183451`, `-183758`). Across the day's harness logs,
  10 of the 48 runs between 13:32 and 18:38 never presented.
- [I] The PC-wide freeze at about 18:55 came while a hung copy spun at 20 on CPU 2 and `native-stacks.py`
  sat in an unkillable wait on that process (next section). A thread in the realtime band that never yields
  starves whatever Windows itself needs to run on that CPU. This is the likeliest cause, unproven.

### Players

- [V] The hang is in the runtime and the title, not in the harness, so a player's boot can stop on a black
  screen the same way. The installed 0.7.2 has the same pinning and the same timer thread.
- [I] It is likelier on a PC where something else keeps CPU 2 busy at high priority, and when two copies run
  (a local online test without `RECOMP_GUEST_CORE`).
- [I] The same wait runs whenever Direct3D runs out of push-buffer space, not only at boot. A mid-game freeze with the
  same signature is possible; none was seen in today's logs.

### The fix: a starvation watchdog for the timer thread (toolkit)

`kernel_bridge.c`, `kernel_timer_watchdog` (Windows). A small thread outside the guest CPU checks the timer
thread every 10 ms. It rescues only when all three hold:
- the heartbeat has not come round for 50 ms;
- the thread has spent no CPU cycles in that time (`QueryThreadCycleTime`);
- the scheduler reports it Ready, meaning runnable but not running (one `SystemProcessInformation` read, taken
  only once the first two hold).

A rescue widens the thread's affinity to every CPU the process may use, and the thread pins itself back to the
guest CPU 250 ms later. `RECOMP_TIMER_WATCHDOG=0` turns it off. Each rescue is logged (`[KERNEL] timer thread
starved ...`), and so is a quiet thread that turned out to be waiting (`... idle ... but not starved`).

- [V] The first version rescued on the heartbeat alone. That was wrong. In `regress.py` runs (`m2`
  `run-20261020-194554`, `m4a` `-194724` and `-200120`) it fired at the same two points of the boot every time:
  the start of Direct3D and a moment during loading. Each run then stopped at frame 21, and one exited with
  -1. The timer thread was waiting there, not starved, and letting it off the guest CPU let its interrupt work
  run beside title code. This is what D28 exists to prevent, and it broke the boot. The cycle check alone did not
  stop it (`-200120`); the Ready check does.

- [V] While the guest CPU is free nothing changes: the thread stays pinned and preempts the title (D28 and
  the DirectSound analysis in `dsound-voice-list-crash.md` depend on that).
- [I] During a rescue an interrupt can run beside title code. The title thread on the guest CPU is then the
  one spinning for that interrupt, or something outside the process holds the CPU and the title is not
  running at all, so the window D28 closed stays closed in practice.
- [I] Unpinning the timer thread for good was rejected for that reason. Making the wait yield (a hook in
  `sub_0021EC50`) was rejected because the loop has no call to hook without hand-writing the function, and it would
  cover one wait in one title.
- Results: see §4.

### Harness and tooling changes (the hang was invisible to them)

- [V] `harness.py soak` only called a boot hung once it had presented and then stopped. A boot that never presents
  ran out its whole time. New: `start_secs` (60 s in the soak), `stalled()`, unit-tested.
- [V] The hang dump ran its tools without a pid, so they attached to the first `defjam_recomp.exe` tasklist listed.
  At 18:35 that was the owner's installed copy. All five tools now take `--pid`, and the harness passes the hung
  child's pid and preset.
- [V] `native-stacks.py` and `sample-threads.py` suspend threads to read them. Windows completes a suspension
  only when the thread next runs, so on a starved thread `GetThreadContext` waited for ever. A kill could not
  end that wait, and the soak stopped with the hung game still spinning. Now:
  - `scripts/thread-sched.py` (new) reads state, priorities, CPU mask and progress without suspending anything,
    and runs first.
  - Both suspending tools walk the threads that use CPU first and keep them suspended until the end. That frees their CPUs.
  - Both skip any thread found starved (`plan_walk`, unit-tested).
  - The harness runs each tool with output to a file and abandons one after 90 s (`run_diagnostic`, unit-tested).
- [V] A killed copy can take more than 15 s to exit when its CPU is busy. The harness now waits 60 s
  (`EXIT_WAIT_SECS`).

## 2. `[ICALL] Failed to resolve VA 0x0002A167` (`run-20261020-182644.log.err:1185`)

- [V] 0x0027EC6E is the return address of the indirect call in `sub_0027EC5D(urb)`, which calls
  `urb->+8(urb, urb->+0xC)` when `+8` is non-zero, which is a URB completion. Its twelve callers are all in the pad
  driver (0x0027FE53..0x00284AB8; OHCI service routine 0x00283D6E, deferred routine 0x00284476).
- [V] 0x0002A167 is not a function. It is the return address after `call sub_001ECF10` (leave critical section)
  inside `sub_0002A140`, which is a stack word left behind by an unrelated call.
- [V] The URB belongs to `sub_0027DE9D`, the XInputGetCapabilities shape. It does two control transfers, each with
  the URB at `ebp-0x48` and a notification KEVENT that the title builds itself at `ebp-24`. Def Jam does not import
  `KeInitializeEvent`. It waits in `sub_0027F19A`: 50 ms, then cancel, then wait forever. On a console the URB
  cannot outlive the function.
- [V] Here the wait can return at once. The bridge creates a host event for a title-built event once, keyed by
  its guest address, and keeps it (`ke_object_resolve`, `kernel_bridge.c` ~7099-7114, manual reset). The lazy
  path of `KeSetEvent` sets the host event and never writes the guest `SignalState` (~1655), the wait never reads
  it, and nothing resets the host event. The second transfer's event sits at the same stack address as the
  first's, so it finds the host event still set from the first transfer and returns before the transfer
  completes. The completion arrives later, on the timer thread (`esp 0x007BFF84` is a worker stack),
  and reads `+8` from a stack that has moved on.
- [V] It is the same mechanism as D95's Linux boot call through 0x80003344. `RECOMP_TITLE_KEVENTS=1` removes it but
  hung FFA fighter select, so it stays off.
- [V] The ICALL safe path skips the call and restores `esp`. Besides the call, the late completion writes the
  URB's status, length and flag words (`+4`, `+0x14`, `+0x22`, `+0x24`) and the predecessor URB's `+0x24`.
- [I] Those writes land on whatever frame now uses that stack. [U] Whether that has ever corrupted a live frame:
  no crash has been tied to it. Only a stale `+8` that looks like code but is not a function entry gets printed;
  a stale `+8` that is not code is skipped silently. So the logged count (1 in about 110 logs today) understates
  how often the late completion happens.
- [I] Players: yes, at the same low rate, on every platform. The capability query also runs from the per-frame
  pad poll (`sub_001EBBE0`), not only at boot.
- [V] Not a race at boot in the sense of an early interrupt. `KeConnectInterrupt: vector 6` just before it is the
  audio codec's, and unrelated. The timing only decides whether the stale word looks like code.
- Not fixed here. A narrow fix needs two toolkit changes plus a hook: `KeSetEvent` must write `SignalState`
  on the lazy path as the kernel does, and the wait in `sub_0027F19A` must resync the host event from the guest's
  `SignalState` first, using `ke_guest_event_prepare`'s reset-then-recheck. Done globally, this is
  what `RECOMP_TITLE_KEVENTS` does, and that hung fighter select. It needs that hang understood first or the
  narrow scope; see `stale-urb-completion.md` §4. Proposed as a separate session.

## 3. Logs and dumps

- Hung boots: `logs/run-20261020-141425`, `-153433`, `-170225`, `-171233`, `-183451`, `-183758`,
  `-191543` (dump `logs/hang-20261020-191644`), `-192044`. Good: `-164727`, `-171453`.
- `logs/hang-20261020-183552` is a dump of the wrong process (the installed copy), kept as the example of the pid bug.
- ICALL: `logs/run-20261020-182644.log.err` line 1185.
- §6: frozen boot `logs/run-20261021-003451` (dump `logs/hang-20261021-003552`); stuck-16 boot that passed on
  the fix build `logs/run-20261021-005427`; the recovered twin of the owner's boot `logs/run-20261019-173225`.

## 4. Results with the watchdog

- First version (heartbeat only):
  - [V] 15 boots alone (`run-20261020-192427` .. `-193510`): 0 hung, 1 rescue (`-193340`).
  - [V] 10 boots beside a second copy (`-193635` .. `-194351`): 0 hung, 0 rescues.
  - [V] It broke `m2`/`m4a` (above).
- Final version (Ready check), on build 9:
  - [V] 4 of 4 `m4a` runs pass (`-200256` .. `-200622`), with no rescue and no "not starved" line.
  - Quick regression and boot soak: see PROGRESS.md §6.
- [I] Rescue is the right action when the thread really is Ready and starved. But this session caught no hang
  live under the final version, so it is not yet proven to rescue one. The full `regress.py` soak and a few days
  of normal runs are the real test: count `timer thread starved` and `but not starved` lines.
- [V] The stale URB of §2 fired in 5 of the 10 paired boots (all `0x0002A167` from `0x0027EC6E`) and in 1 of 15
  alone (`0x80003344`, the Linux value). Before this, 3 of the day's 75 logs. A busy CPU delays the completion, so
  the late write is common under load. It is not caused by the watchdog: it appears in old-build logs (`-182644`, `-191325`).

## 5. A later freeze with the watchdog in place (owner's boot, build 11 code, 2026-10-20)

Log: `logs/run-hung-boot-account-test.log.err`, the owner's own launcher boot. An extra host thread was making a WinHTTP request at start-up; it has since been moved out of start-up.

- [V] The start-up starvation happened and was rescued (line 588: `timer thread starved for 78 ms ...`).
  Interrupts and deferred routines then ran normally, and the boot drew 234 frames.
- [V] At frame 234, a game critical section (`0x802493D8`) was contended between two game threads (the `[CS]`
  lines). The timer thread waited 62 ms on a lock (state 5, wait reason 37 = `WrAlertByThreadId`), and the
  watchdog correctly left it alone (`idle ... but not starved`).
- [V] After that the timer thread kept working: the pad was polled 1250 times every 10 s until the window was closed,
  which only happens when its deferred routines run. So this is not the starvation of §1.
- [V] What stopped was the GPU executor: `DMA_GET` frozen at `0x02A2CBC0`, with a backlog of 0x13D4
  (`[GPU] executor not moving ...`).
- [U] What the executor waited on. It might be a software method, a semaphore or a fence. A live look is needed: run
  `scripts/thread-sched.py` and then `scripts/native-stacks.py --tid <executor>` on a frozen process. Also check
  `[PGRAPH] software method held` lines in a log with `RECOMP_PGRAPH_SWM_TRACE=1`.
- [U] Whether the rescue 4 s earlier contributed. A rescue can let interrupt work run beside title code (§1, the
  misfire). Unlike the misfire, this rescue was a Ready-starved one, and the boot ran 234 frames past it. A run
  with `RECOMP_TIMER_WATCHDOG=0` that freezes the same way would settle it.

## 6. Why the GPU executor stops after a good start (2026-10-21)

Sources: the owner's log of §5, every harness log from 2026-10-19 to -21, a frozen process held live
(`logs/run-20261021-003451`, dump `logs/hang-20261021-003552`), a priority poller run beside the boots
(`NtQuerySystemInformation` every 20 ms, reads only), and the runtime's host globals read through the
linker map.

### Short answer

The executor stops because §1's deadlock comes back after the watchdog has switched itself off. In some boots
Windows holds the title's main thread at priority 16 for the whole run, not only at start-up. Every time
Direct3D's fence spin runs (`sub_0021EC50`), it starves the timer thread again, and the watchdog rescues it, several
times a boot. A rescue ends with the timer thread pinning itself back to the guest CPU and *then* closing the
rescue window. The pin puts it behind the spinning main thread at once, so the window is never closed, and the
watchdog does not look at a thread whose window is open. From then on nothing rescues it. Fixed in the toolkit:
the window is closed before the pin, and a window that has run out counts as closed.

### What a frozen boot looks like (live)

- [V] `thread-sched.py`: the main thread Running at 16 (base 8) on CPU 2, a full second of CPU, no context
  switch; its native stack is `sub_0021EC50+0x91A <- sub_00219AA0 <- sub_0010BBE0 <- ...`, §1's spin. The
  timer thread is Ready at 15, CPU mask `0x4`, starved; four more guest threads are Ready and starved.
- [V] Host globals: `g_timer_beat` had not moved for 8.4 s; `g_timer_free_until` was non-zero and long past;
  `g_timer_rescues` 6; one software method queued and not presented (`g_swm_tail` 87, `g_swm_head` 86);
  `g_gpu_servicing` 0; the DPC queue empty; no runtime lock held.
- [V] The log: rescues at frames 0, 5, 23, 43, 59; frames stop at 80; no watchdog line after the last rescue.
- [V] Widening the timer thread's CPU mask from outside, which is what a rescue does, revived the boot: the queued
  method was presented, `g_timer_free_until` went to 0, and the frame count went from 80 to 1181.
- [V] The code (`kernel_timer_heartbeat`, before the fix): `xbox_PinToGuestCore()` then
  `InterlockedExchange64(&g_timer_free_until, 0)`; the watchdog skipped any thread with `g_timer_free_until` set.

### The main thread at 16

- [V] Poller, eight unscripted boots of the old build: in three the main thread was at 16 (base 8) when first
  seen. In two of them it was back at 9 within 100 ms and the boot was clean. In the third it stayed at 16 for
  the whole run, and that boot froze as above.
- [V] On a later boot with the main thread stuck at 16, one `SetThreadPriority(main, THREAD_PRIORITY_NORMAL)`
  from outside, the level it already had, brought it to 8, and it stayed there. So the 16 is a dynamic priority
  that never decays; re-setting the base clears it.
- [U] What raises it. The runtime calls no MMCSS function (SDL's WASAPI backend does, on its own audio thread,
  which the Windows build does not use). The poller first sees it in `Wait/Executive` at start-up. §1's 20
  suggests a value set from outside rather than a boost. A kernel trace (WPR, priority events) would settle it.
- [I] A main thread stuck at 16 also starves every other guest thread on the guest CPU whenever it runs, not only
  the timer thread. That is the slow start in the owner's boot: the loader thread (#8, `0x001F683A` with
  `0x001ED080`) opens its first file two frames after it is created in good boots and 229 frames later there
  (`run-20261019-173225`: 934 frames later, then recovered). It is also behind
  `[IRQL] a thread stayed at DISPATCH_LEVEL for 200 ms`: a title thread at DISPATCH_LEVEL, holding the dispatch
  lock, preempted by the main thread.

### Across the logs

- [V] Every log with a rescue at frame 0, when Direct3D starts, failed later: 9 of 9
  (`run-20261020-194554`, `-194724`, `-200120`, `-203920`, `-204106`, `-224630`, `-224736`,
  `run-20261021-000511` and the owner's). Eight of them had a second rescue at frame 5, after the loader thread
  is created, and stopped at frame 21, 53 or 374 with `DMA_GET` frozen. About 144 boots without a rescue show
  none of it.
- [V] Not a second copy: only `-200120` overlapped another copy of the game (the installed one).

### Does the watchdog contribute?

- [V] It does not cause the freeze. The frozen boot was §1's deadlock, and doing by hand what a rescue does
  ended it. The watchdog carried that boot past five deadlocks and lost the sixth to its own re-pin.
- [I] Without the watchdog these boots hang at their first deadlock, at frame 0, as in §1.
  `RECOMP_TIMER_WATCHDOG=0` runs: see the results below.

### The owner's boot (§5)

- [I] Same precondition: a rescue at frame 0 and the loader thread starved for about 3.9 s.
- [U] The last step. The re-pin race cannot be it: the watchdog wrote `idle for 62 ms but not starved` at frame
  234, which it only does with the window closed, and a later starvation would have been rescued and logged. The
  timer thread was blocked on a host lock for those 62 ms (wait reason 37). Candidates: a lock held by a title
  thread that the main thread at 16 starves, or Direct3D's interrupt enable left cleared. Neither is shown. A
  live look at the next such freeze would settle it.

### The fix (toolkit `kernel_bridge.c`)

- `kernel_timer_heartbeat` closes the window with a compare-exchange before it pins the thread back.
- `kernel_timer_watchdog` treats a window that has run out as closed. A rescue that lands between the
  compare-exchange and the pin therefore costs at most one more window, and its log line says so
  (`it was starved again before it closed the last window`).
- Not changed: the realtime main thread itself. Re-setting a guest thread's priority when it is found above 15
  with a lower base would remove the cause of §1 and of this. That is proposed separately.

### Results

- [V] Old build, unscripted boots shaped like `regress.py`'s m3 (`RECOMP_SETTINGS=none`, no pad script),
  2026-10-21 00:28-00:35: 7 of 8 ok; the 8th is the frozen boot above.
- [V] Fix build (a private build of the same tree plus the fix), 2026-10-21 00:51-00:56: 5 of 5 ok. One of them
  had the main thread stuck at 16, with rescues at frames 0, 5, 156 and 157, and reached the title, but its
  priority was also reset by hand during the boot, so it is not clean evidence.
- [V] Fix build, 14:16-14:34: 20 of 20 ok, no rescue at all. The main thread went to 16 in 9 of the 20 and
  came back within 100 ms every time, so no boot needed the fix.
- [U] The fix has not yet been seen to rescue a stuck-16 boot with nobody intervening. It cannot be forced from
  outside: this account gets the HIGH priority class at most (13-15), and only a thread above 15 makes the
  deadlock permanent. Watch the logs for rescues past frame 0 and for the new
  `starved again before it closed the last window`.
- `RECOMP_TIMER_WATCHDOG=0` was not compared: with no stuck-16 boot in the day's 20, such runs compare boots that
  never need the watchdog. The live by-hand rescue above is the direct answer.
- [V] Two-copy online test runs by another session on a build with the fix (`run-20261021-132658`/`-132704`,
  `-135558`/`-135604`): no rescue, no freeze.
