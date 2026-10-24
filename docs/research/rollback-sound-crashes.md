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
