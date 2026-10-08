# v0.13 migration: accepted-baseline presentation stall

Read-only investigation on 2026-10-07 of the original accepted build, parent
`e39cefb`, toolkit `2ac8e705`. No code edits, builds, game runs or live process
reads were performed by this reviewer. Only this report was written. The main
agent reports the baseline full suite finished 10/14; its soak stopped after
two passing boots and one presentation stall. Those are baseline results,
not candidate results.

## Evidence and what it establishes

- `logs/run-20261007-160639.log.err`: presentation stops at 1,379 flips. Repeated
  diagnostics report user-channel DMA PUT `0x02A2B8DC`, GET `0x02A298F4`.
  This is a stationary pair in that logged interval, separated by `0x1FE8`
  bytes. It establishes queued GPU work during that interval, not the reason
  its executor stopped advancing.
- `logs/hang-20261007-160822/native.txt`: an actual native stack walk for thread
  11628 has `sub_0021EC50+0x939`, then `sub_00219AA0+0x2FF` and
  `sub_0010BBE0+0x424`. These are compiled host symbol displacements. Adding
  `0x939` to guest address `0x0021EC50` would be incorrect. The stack locates
  the rendering thread in the fence-wait function and establishes its actual
  callers at that sample; it does not identify an exact guest basic block.
- The same native snapshot shows the acknowledgement/executor thread sleeping
  in `nv2a_ack_thread+0xC89`, the kernel timer thread in a host single-object
  wait, and another guest thread inside `kernel_gpu_preempt`. These snapshots
  do not prove any thread remains at that position indefinitely. The executor
  normally sleeps between passes, and timer waits can be expected operation.
- `guest.txt` says it could not read the memory offset or TLS slot. It provides
  no recovered guest register values or verified guest call chain. A watchdog
  stack-word scan, if encountered elsewhere, must not replace the native walk:
  it can contain stale return addresses.
- `sample.txt` contains a 120-second timeout rather than repeated instruction
  pointer samples. Thus there is no repeated-PC confirmation in this dump.

The main agent independently verified the stalled process identity:
PID 15704, start time 16:06:41. `state.txt` records that same PID. The following
opt-in process was PID 50504 and did not start until 16:12:36, after termination
of the third soak boot. Therefore the late state samples belong to the same
baseline process; they are not evidence accidentally collected from the next
test. The diagnostic files were still being completed while this review began.

## The later state changes materially limit a frozen-GPU claim

Across the four samples in `state.txt`, the issued fence advances
`0x17B1 -> 0x1885 -> 0x1957 -> 0x1A2B`; its completion semaphore at guest
`0x82A0D000` advances `0x17A7 -> 0x187B -> 0x194D -> 0x1A21`.
The difference is ten in each sample. The CPU push pointer and pending-record
index change, and the wrap generation moves from 13 to 14.

These are positive evidence of continued fence/ring activity later in this
same process. They contradict a claim that the engine remained completely
frozen throughout collection. They are consistent with progress or recovery
after the earlier native/log sample, but do not establish resumed presentation.
The requested fence of the particular sampled call was not recorded: an
issued-minus-completed distance of ten cannot by itself show whether that
call's fence is satisfied.

The tick counters at `0x3C9684/0x3C9688` stay `0x1F` across these later samples.
The tick object words at `0x3C9614` are also unchanged. This establishes a
stationary guest clock in that sampled interval; it does not distinguish
intentional inactivity, a missed notification, a stopped clock worker, or
another cause. Preserve this observation separately from the fence activity.

`hang-peek.py` reads PFIFO CACHE1 PUT/GET and reports both zero. The log's
active user-channel DMA PUT/GET are a different register pair. Zero CACHE1
values do not show that the active command stream is empty. PGRAPH INTR is
unreadable in the saved peek: this is missing data, not interrupt status zero.

## What the source establishes about the wait

The original generated `sub_0021EC50` in `src/recomp/gen/recomp_0015.c` spans
guest `0x0021EC50..0x0021EDBA`. As documented in
`docs/research/d3d-pushbuffer-wrap.md`, it is a fence wait with several paths,
including callbacks and an indirect wait fallback. The named native function
alone cannot select between these paths.

Its tight loop at guest label `0x0021ED87` polls the completion word addressed
by the device's `+0x30` field. It compares modular unsigned distances from an
issued-reference snapshot: continue while `(issued - requested) <
(issued - completed)`. The generated memory accessor is volatile, so the
source does not permit the compiler simply to hoist this completion read out
of the loop. There is no kernel call inside this particular loop.

The caller `sub_00219AA0` includes a fence wait using a fence from alternating
device slots at `+0x1974/+0x1978`, with second argument 1; other call sites
within that caller must still be distinguished by the compiled return PC.
This is consistent with a rendering synchronization path, not proof of a
push-buffer-space reservation wait. `sub_0021EDC0`, the ring reservation
routine, is not on this native call chain.

Device `+0x44` is the bytes consumed in the previous lap, not a fixed ring
size. Later values `0x7F9E8/0x7FE10` agree with that documented dynamic meaning.
Guest contiguous addresses with `0x80000000` set and the lower physical DMA
offsets are different address forms and require the runtime's canonical
mapping before comparing pointers.

## Missing state needed to resolve the stop

For a recurrence, collect one PID-pinned, timestamped bundle rather than
inferring causality across sequential diagnostics:

1. Retain the matching executable/PDB/map and generated-source hashes. Resolve
   host `sub_0021EC50+0x939` and caller return PCs to compiled instructions or
   source lines, then identify the guest label. Record repeated RIP samples
   and guest register/TLS state for that same thread.
2. Capture the actual requested reference, issued-reference snapshot and
   completion address/value used by that call. At the tight loop, the issued
   and requested-distance values have already been placed in registers;
   current device `+0x2C` alone is not necessarily that snapshot.
3. Read the active user-channel register block (device `+0x1C20`, offsets
   `+0x40/+0x44`) alongside ring base/end, CPU pointer/limit, wrap generation,
   previous-lap size and relevant fence records. Do not substitute CACHE1
   zeros for those values.
4. Capture executor cursor, waiting/subroutine state, pending releases,
   software-method queue head/tail, PGRAPH status/enables, FIFO-access state,
   interrupt servicing and backlog. These distinguish a pending software
   interrupt or withheld release from command damage/mapping issues; none is
   established by the present snapshot.
5. Capture tick worker actual stack, its event shadow/guest SignalState,
   pulse/set/wait ordering and PTIMER alarm/status at the same time. Measure
   presentation, fence progress and clock progress together over a short
   interval. The current data demonstrate that these can diverge.

## Relationship to upstream v0.13.0

Comparing common base `1409a7d` with release `b3700e1`, the only incremental
`src/nv2a` change is compiler portability in `qemu_shim.h`. There is no direct
new NV2A executor, renderer or fence-release implementation in that delta.
Much of the broad release graphics changelog was already in the common base.

Upstream's title-built event option, final KePulseEvent correction and guest
CPU-lock dispatcher changes plausibly interact with event/interrupt/clock
scheduling around this path. They do not demonstrate a fix here. The optional
guest lock operates around kernel transitions and cannot itself interrupt the
guest-only fence spin described above. The candidate initially keeps these
new options disabled and must retain the fork's event resolver, guest affinity
and GPU-preemption behavior during conflict resolution.

The flag/join migration can alter regenerated control flow, so compare this
function's emitted branches after regeneration. Its tight-loop branch is an
ordinary integer CMP/JB already supported by the accepted fork; the native
stack supplies no evidence of a mixed-float join or damaged switch causing
this baseline stall. Opt-in memory reclamation/VMA changes can affect layout
and DMA lifetime when enabled, but were not evidence of this baseline stop.

The bounded conclusion is a baseline presentation-stall failure with a real
fence-wait stack sample, an earlier stationary active DMA pair, later advancing
fences/ring state, and a later stationary guest clock. A fixed guest-PC hang,
specific missed event, corrupted command, or upstream repair remains unproven.
