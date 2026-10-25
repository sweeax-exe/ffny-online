# Rollback: the sound crashes (2026-10-24)

Builds 19 and 20 shipped rollback on; players' games crashed in the sound engine within minutes
(`sub_0012BE20`, `sub_00126150`: engine objects with junk vtables in the audio arena) and build 21 turned it
off again. This note records what two-copy stress runs with simulated lag found, what was fixed, and what is
still open. Test scripts: the scratch `rb_online.py` (two copies, `RECOMP_NETPLAY_LAG_MS=70,70`, random
presses) and `rb_matches.py` (two copies, fast KO, matches one after another).

## Fixed

1. **The arena merged free blocks across gaps** (`rollback.c` `arena_free_now`). The audio arena is pieces of
   contiguous memory (3 MiB, then 1 MiB steps) with other owners between them. A free merged with its list
   neighbours whether or not they touched, so a free block came to span the gap between two pieces. In the
   runs that gap held DirectSound's memory (`GPSADDR 0x834F4810`, an object at `0x83518134`, between the
   pieces `0x831B0000..0x834B0000` and `0x83519000..`); the engine's next object was placed over it, the APU's
   voice list read a junk link (`0xEFFD`) and the game crashed in DirectSound (`sub_0025F509`). Every run crashed
   at the match setup; with the fix the 240 s run made 933 rollbacks without a crash or desync.
2. **The APU indexed host arrays with guest voice handles unchecked** (toolkit `apu_vp.c`, cdf495f). The
   handle checks were `assert()` only, gone in release builds; a junk link wrote past `g_dbg.vp.v[]` and the
   filter arrays into the program's own data. That is also the likely cause of two players' build-22 crashes
   that jumped to `0x1E` and `0x111` on a non-game thread (rollback off). `voice_ok()` now ends a voice list at
   such a handle and makes the methods ignore it; the guest's voice memory is left as before.
3. **Audio globals in the saved .data.** The AEMS layer's globals `0x331C20..0x3323C0` (written only by its code,
   `0x1C000..0x1E000`) and the audio subsystem's 32 bus objects `0x3454A0..0x345968` (reached only through the
   audio code; the slow-motion effect `sub_000388B0` links a record into `0x345600`) were put back by a load
   while the mixer ran. Found by scanning the lifted code for `.data` constants used only by audio code; a
   fast-KO run had crashed in `sub_0011E530` following a junk pointer.

## Open

* **Heap pages the mixer writes.** The mixer thread writes heap pages that also hold the game's objects;
  `mixer_end` leaves such a page out of the save from then on, so the game's objects on it stop being restored.
  `RECOMP_ROLLBACK_ALLOCLOG=1` lists the blocks on each such page with the code that allocated them and the
  bytes the mixer changed. Seen: the music stream's file buffer (`sub_001E3F20`, 6.8 KB at `0x80254CF0`), and in
  longer runs small blocks from the generic helper `sub_00028950` (many callers) at `0x80029000..0x80030000`,
  `0x800A0000`, `0x800B7000`, `0x8024B000`. Routing audio allocations by the direct caller misses those made
  through helpers.
* **Remaining crashes and stalls in long runs** (several matches, fast KO): `sub_0011E530`/`sub_00126BE0` in the
  engine, a junk voice handle `0x8225`, an object at `0x8000A640` whose vtable read `0x0D0D0D0D` (called from the
  crowd's update), and a copy that stopped stepping at a KO. Rollback stays off by default until a 10-minute
  multi-match run is clean.

## Tried on 2026-10-24 (not kept)

* **Masks instead of skipped pages, per 16 bytes.** The pieces of a page the mixer changed stayed as they were
  on a load and the rest was restored. Worse: a crash in the first match (`sub_0012E560` writing through junk,
  voice handle `0xFA4C`). The mixer writes single 2-byte fields in small game-heap blocks (`0x4C`, `0xAC`
  bytes: sound objects the game allocates through `operator new`, outer callers `0x11E75`, `0x12723`,
  `0x28A93`); restoring the rest of such an object while the engine moved on breaks it.
* **Masks widened to the whole live block** (a registry of the heap's live blocks fed by `sub_00029500` and
  `sub_0002A180`). No crash in the sound engine, but one long match (6000+ steps, no KO with fast KO) ended
  with a crash in `sub_000ABB70` on another thread's stack (`esp 0x00F7FD88`) and a stall of the other copy.
  Kept as a diff in the session scratch (`rb_blocks_attempt.diff`); the log is `logs/mcp/blocks-crash-copy1.log.err`.
* The allocation log now names the call chain (validated `E8` call sites): the mixer-written `0xAC4` block at
  `0x8024BC60` comes from `sub_00011E40+0x8E3` <- `sub_001FDCAE` <- `sub_0020005D` <- `sub_00203A3A` <-
  `sub_00203A77`; the music stream buffers from `sub_001E3F70` <- `sub_0001C320` (AEMS).

Next: route these allocations into the arena by their call chain (an audio-code site among the first
validated return addresses), so no sound object lives in restored memory; then find out why the match
did not end with fast KO and what `sub_000ABB70` read.

## The blocks the mixer writes, by allocator (diagnostic run, 2026-10-24 evening)

With the call-chain log (`RECOMP_ROLLBACK_ALLOCLOG=1`, logs `logs/mcp/diag-chains-copy0/1.log.err`), the baseline
rollback (no masks) crashed again in the first match (`sub_0012E560`, `sub_0025FA4F`) at 70 ms lag. The blocks
holding the first byte the mixer changed came from:

| allocator (return address) | blocks | notes |
|---|---|---|
| `0x29821` (`sub_00029800`) | 76 | not routed by `rollback_route_alloc` at all, which hooks `sub_00029500` only |
| `0x29696` (wrapper `sub_00029680`) | 28 | outer callers `0x28A93`, `0x28A71`, `0x61ECC`, `0x12723`, `0x12C56`, `0x11E75` |
| `0x296C6` (wrapper `sub_000296B0`) | 6 | outer `0xA4024`, `0xA3189` |
| `0x1E477A`, `0x1E3F20` (file layer) | 6 | stream buffers (0x47B80, 0x1A9C bytes) |
| `0x6A4CC` | 4 | one 3.3 MB block (`0x333AB0`) |

Most chains pass through `0x100000..0x114000` (`0x101F19`, `0x10192D`, `0x102145`, `0x113F42`, `0x10B41C`),
which `audio_code()` does not count as audio (it has `0x11C000..0x142000`). Next: confirm that range is the
sound engine, route `sub_00029800` like `sub_00029500`, and route by the validated call chain (an audio site
among the first few return addresses) instead of the direct caller.

**The 0x100000..0x11C000 range is not audio code.** Its 1012 functions call nothing in the sound engine, AEMS or
the audio subsystem (811 calls stay in the range, 716 go elsewhere) and never use the sound allocator; they
are called from all over the game (0x20000, 0x10000, 0xB0000-0xE0000, 0x170000-0x180000). So the blocks the
mixer writes are the game's own objects (allocated through general code), into which the engine writes back
small fields (two bytes, e.g. a voice handle or a playing flag). Routing by call chain cannot separate them:
they are shared by design. What is left: (a) never restore those fields (the block masks of section "Tried",
which moved the crash elsewhere), (b) roll the sound state back with the game (the arena, the engine's
globals and the APU's voices together, with the mixer held during a load), or (c) find which fields the
engine writes and keep exactly those live. (c) is the smallest; it needs the field offsets per object class.

## The real picture (2026-10-24, late): one CPU per copy

The rollback test runs used `RECOMP_GUEST_CORES=all`. Players' games run every guest thread on one CPU,
and `mixer_end` relies on that: a page that changed between the mixer's lock and unlock while the game
thread's cycle count did not move is the mixer's. With the threads spread over several CPUs the game
thread ran beside the mixer without a context switch, its cycle count stayed, and pages the game wrote
were taken for the mixer's and left out of the save: the game's own objects stopped being restored, and
the runs crashed. A write-guard diagnostic (`RECOMP_ROLLBACK_WRITERS=1`: the saved pages read-only for a
tick, each first write names its thread) showed the mixer writing one block only: the music stream's file
buffer (allocated at return `0x1E3F20` for AEMS).

* `tools/defjam-mcp/server.py` now puts each test copy's guest threads on a CPU of its own
  (`RECOMP_GUEST_CORE=2+2i`); never use `RECOMP_GUEST_CORES=all` for rollback tests.
* With that: 10 minutes, two copies, 70 ms lag, ~1900 rollbacks, no crash, desync or stall.
* `rollback_route_alloc` sends the file layer's buffers for audio streams into the arena (the call chain
  decides). Then: 10 minutes, ~1650 rollbacks, no mixer page at all, no crash, desync or stall.

The sections above that read the mixer pages as the game's own objects were based on the spread runs.

## Five room matches with rollback (2026-10-24, 21:25, 70 ms lag)

`scripts/netplay-hub-test.ps1 -Route online5` with `RECOMP_ROLLBACK=on`: the first match ran with about 1500
and 1850 rollbacks (1.0 ms average), no desync, no mixer page. At its KO, as rollback switched off for the
rest of the match (`42 contiguous block(s) held through the fight given back`), copy 1 crashed in the sound
engine (`sub_001317C0+0x34D`, `ecx 0x8A019D74`: outside guest memory, a junk pointer), so the second match
never started there (`logs/run-20261024-212529.log.err`). Suspect: frees held during the fight and released
at its end. A rollback to a step before a free brings back the game state that still points at the block; if
the re-run does not free it again, releasing the held free at the end hands out memory still in use. Next:
when a load goes back past a held free, drop that free from the list (the re-run frees it again if it should),
and check the contiguous-memory frees the same way.

## Held frees, and a test-setup stall (2026-10-24, 22:00-22:40)

* `forget_frees_since` (on a load, frees held since that save are dropped: the restored state still uses
  those blocks) removed the crash at the first KO.
* Hub tests ran both copies' guest threads on CPU 2: one copy spinning for the other's input starved it
  and both stood still (`logs/mcp/hang-221435.txt`: one game thread "Ready" with 0 ms, the other "Running").
  `scripts/netplay-hub-test.ps1` now gives each copy its own CPU (`RECOMP_GUEST_CORE=2+2i`).
* With that, five-match run: copy 0 crashed mid-fight in the heap (`sub_00029DE0+0x1F3`, junk pointer
  `0xF8F18B56`, step ~5200, 708 rollbacks; `logs/rb-online5-heapcrash.log.err`). The same run logged
  "the mixer writes 80003000 (bytes 800031A4..800031A5)" and "80007000 (80007190..800071B0)": the first heap
  pages, written during mixer ticks and therefore left out of the save from then on. If those hold heap
  bookkeeping or boot-time objects the game also changes, a load restores the game but not them. Next: name
  the blocks on those two pages (allocation log) and who writes them (`RECOMP_ROLLBACK_WRITERS=1`); route
  allocations made on non-game threads to the arena.

## Threads (2026-10-24, 23:00)

A hub run with the write diagnostics showed the mixer thread allocating through the engine allocator's
fallback (`sub_0001DC10` -> `sub_00029710`) and writing the heap's own bookkeeping (`0x80003104`), so that
page left the save. `rollback_route_alloc` now sends to the arena every allocation made during a fight on a
thread other than the game's, and every one made inside a sound command. The next five-match run: the first
match with about 2100 rollbacks, no mixer page, no crash at its KO; early in the second match one copy
crashed in the file layer (`sub_001E8340+0x6D`, `edx 0x832D7C94` in the arena, junk read
`0xF6FE0F27`; `logs/rb-online5-filecrash.log.err`), likely a stream buffer the arena holds across the match
change. Next: what the file layer keeps between matches that now lives in the arena.

## Stream buffers back in the heap: worse (2026-10-25, morning)

With the file layer's stream buffers in the heap again (`RECOMP_ROLLBACK_ROUTE_STREAMS` unset), every
five-match run logged `the mixer writes 80255000 ... left alone from now on`: the music stream's buffer
(`0x80254CF0`, 6.8 KB) shares that page with game objects, which then stopped being restored. Two runs
failed in the first match: one copy stopped drawing at step ~700 (the game thread looping through
`RtlEnterCriticalSection`/`Leave` at esp `0x01181730`, the GPU idle; `logs/run-20261025-103441.log.err`),
and one crashed at step 1322 in `D3DDevice_SetStreamSource` (`sub_0021BB10`, called by EAGL's stream
cache `sub_00119950`) with a vertex buffer pointer of `0x776F7263` ("crow", text where a render
parameter block should be; `logs/run-20261025-111715.log.err`). So the buffers go back to the arena,
and the crash at the second match's start (`sub_001E8340`, a stream handle in the arena reading junk)
is the one to solve. The arena now logs every allocation and free (thread, call chain, save count); a
crash report prints the history of the arena blocks the guest registers point into.

## Frees from other threads wait for the fight's end (2026-10-25, 11:25-11:45)

* Streams in the arena (`RECOMP_ROLLBACK_ROUTE_STREAMS=1`): match 1 ran 2.7 minutes (1800 rollbacks,
  160 equal checksums), then `the mixer writes 80007000 / 80003000` (the heap's free lists, a mixer free
  of a heap block, likely at the music's next track) and that copy stopped drawing at once.
* `late_free`: a heap free on another thread than the game's during a fight (through `rollback_heap_free`
  or the engine's `rollback_sound_free`) is held and run on the game thread when tracking stops. Next run:
  no mixer page at all, the whole first match to its KO (step 11789, 1715 rollbacks, 190 equal checksums);
  200 steps later, right after `50 heap frees from other threads ... given back now`, a crash in the
  heap's allocator (`sub_00029DE0+0x1F3`, free-list link `0xAA53C5AA`, the same shape as the 10-24 22:40
  crash).
* Why: a block allocated during the fight and freed by the mixer can have been handed out again after a
  load went back to before its allocation. Now every heap block allocated during a fight is remembered
  (`fight_alloc_add`, from `sub_00029500`); a late free of one is dropped (a small leak), repeats are
  dropped too, and only blocks from before the fight are given back.

## Mixer words instead of mixer pages (2026-10-25, 11:45)

* With late frees and the fight-allocation set, the next run passed the first rollback stretch, then at
  step ~10000 (again the music's next track, ~2.8 minutes in) `the mixer writes 8000C000 / 8000B000 /
  8000A000` (8 bytes each), and that copy vanished: a division by zero in `sub_00081170+0x246`
  (`0xC0000094`, a ring of entries whose count at `+0x18` read 0). The crash report printed nothing:
  `src/main.c` reported access violations only. It now reports divisions by zero, integer overflow and
  illegal or privileged instructions the same way.
* The other copy hit a game thread looping in the sound engine (`sub_0012BE20`, `sub_0012DBF0`) at step
  1430 in the run before: sound commands that name an engine object the engine already gave back (a
  load brought back game state that still holds its handle) are now silenced
  (`rollback_sound_command_dead`, from `netplay_sound_command`).
* A page the mixer writes is no longer left out of the saves: `s_mixmask` records the words it changed
  (per page, one bit per word), and `load_page` restores the page around them.
* The next run (mixer words kept live) ran 5.6 minutes of match 1 (step ~20100), then copy 0's game thread
  looped in `sub_0013A380` (the audio subsystem's list code, from the screen manager through
  `sub_0003D960` / `sub_000379E0` / `sub_0013A9E0`): the list object at `0x3454F0` in the game's `.data`
  (restored by loads) named arena node `0x8331D388`, which the sound allocator had just handed out again
  as the new node, so the node pointed at itself with a null callback. The heads of these lists
  (`0x3454A0..0x345518`) are game state, their nodes were not. Nodes the container code (`0x139810..
  0x13A430`, six allocation sites) asks for on the game thread outside a sound command now come from the
  heap (`game_list_node`, `t_heap`). Reverted at once: the next run crashed on the mixer thread at step
  ~1090 (`sub_0011D480` from the mixer tick) after 196 frees of those nodes by other threads in 18
  seconds: the mixer uses these lists too, and the bus objects (`0x3454A0..0x345968`) were already left
  out of the `.data` restore (2026-10-24). So the head was live, and the reused node is still unexplained.
  `scripts/guest-regs.py` read the guest registers at the wrong TLS
  offset (the `.tls` start inside its section was ignored), so `guest-stack.py` printed nothing for
  release builds; fixed, and it named this loop.

## Match 1 runs clean; the second match broke XAPI's heap (2026-10-25, 12:10-12:40)

* With the arena fixes, two runs played the whole first match on both copies (4.5 and 5.5 minutes, ~1000
  rollbacks each, both copies with the same result; the arena at the fight's end about 5-6 MB). Both then
  stopped early in the second match (steps ~1800 and ~700) with the same stack: the render path into
  Direct3D's resource creation (`sub_00219E50`) -> `XMemAlloc` (`sub_001F9DC6`) -> `RtlAllocateHeap`
  (`sub_001FE621`) on XAPI's process heap (`[0x3CD1D0]` = `0x00F81000`, outside the save states), looping.
  Direct3D's fence word (`[device+0x30]` = `0x82A0D000`) being two fences behind in the first of these was a
  side effect: the game thread never got to kick the push buffer.
* Why: that heap holds Direct3D's resource headers. A load brings back game state that still holds a
  resource the game had released; the game releases it again and the heap's free lists break. XAPI's heap
  free (`sub_001FBE1E`) is now hand-written in `src/recomp_manual.c`, and frees on the game thread in a fight
  are held (`rollback_hold_xapi_free`): forgotten by a load that goes back before them, given back once no
  save older than them is left, and all at the fight's end (`release_held_xapi`).
* With XAPI frees held, the next run played the first match for 7.2 minutes (2522 rollbacks), then copy 0
  crashed in the sound engine (`sub_001317C0`, a boot-time engine block holding junk) as the second match
  loaded. Likely the change of 11:55 that stopped forgetting the mixer's frees on a load: restored game
  state can still hold a handle to an object whose sound ended after the save, and given back, the block
  went to a new owner. Every held arena free is forgotten by a load again; the blocks kept that way (used
  and freed) are freed when the next fight starts (`release_kept_arena`), after the match change has torn
  the game's sound handles down. The crash report's stack scan stops at the thread's stack base (it
  faulted past it inside the report).
* Freeing every kept block at the next fight (10900 of them, 1.8 MB, after a 3-minute first match) crashed
  the mixer 3800 steps into the second match in `sub_001E8340` (a stream handle in the arena read junk;
  the same shape as the 10-24 file-layer crash): blocks freed on the game thread and kept by a load are
  alive again (the restored state uses them; the music stream's handle lives on into the next match).
  Held frees now remember their thread; a load that forgets a game-thread free clears the block's freed
  mark, so only the mixer's kept frees are freed at the next fight (and only they make a sound command
  "dead"). The crash report also prints the sound bank table `[0x3AD928]` and its arena blocks, for the
  `sub_001317C0` crashes.
