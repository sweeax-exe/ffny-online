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
