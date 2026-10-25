# The 0x3A9680 table and the sub_001E8340 crash on the mixer thread (read-only study, 2026-10-25)

Scope: lifted C only (`src/recomp/gen/recomp_0000/0001/0008/0013.c`), the five crash reports in `logs/`
(`rb-online5-filecrash.log.err`, `run-20261024-225329.log.err` (same crash), `run-20261025-131300`, `-132125`,
`-133007`), `src/hooks/rollback.c`, `src/hooks/netplay.c`, and the earlier notes `movie-audio-clock.md` and
`rollback-sound-crashes.md`. Nothing was built or run. `[V]` = read in the code or log, `[I]` = inferred.
Addresses are guest addresses.

## 0. Headline

1. **The table at 0x3A9680 is not a stream table.** It is the sound engine's *deferred-callback queue*:
   `[0x3A9680]` = count, then 8-byte events `{u16 type, u16 queue id, u32 param}` from 0x3A9684
   (`esi` in `sub_00126300` starts at 0x3A9688 = event 0's param). Two writers only. It is filled on the mixer
   thread during a tick and emptied by `sub_00126300` at the end of the same tick. [V]
2. **The crashing event is always a type-1 event ("packet retired", callback `sub_0011EC80`), and the junk is
   not a stale pointer.** In all five crashes `ecx = 0xFE`. `sub_0011EC80` computes
   `chunk = MEM32(param-4); id = MEM32(chunk) & 0xFF; obj = MEM32(0x3A92A0 + id*4)` and hands `MEM32(obj)` to
   `sub_001E8340`. `0xFE` is the low byte of **`0xFFFFFFFE`, the "chunk already released" marker** that
   `sub_001E8340` itself writes into a chunk. So the event's chunk was already released when the event ran.
   `0x3A92A0 + 0xFE*4 = 0x3A9698` = the `param` field of event 2 in the same table. That is why `edx` is "a pointer
   into the arena" (it is event 2's payload pointer P) and why `[edx]` is "audio sample data" (the first PCM
   dword of that payload). Verified numerically in all five dumps (section 3).
3. **So the question is not "why does an entry name reused memory" but "why does a retire event outlive its
   chunk"**: the chunk was released by something else first, or the event was queued twice.
4. All five crashes follow a **music stream (re)open** by 140 to 2,000 log lines: `music/<song>/tune.mpf`,
   `overlay0.mus`, `loading0.mus` were opened (the 256 KB block is the music stream's buffer, section 4).
   Two are at the second match's step 0, two mid-fight at steps 3,820 and 5,950 (the in-fight track change).
5. Best guess for the rollback link: **the audio subsystem's per-step update `sub_0003D960` -> music director
   `sub_0003D060` is not silenced in re-run steps** (section 6). It is a call-counted state machine in
   non-restored globals, fed by restored game state. The proof needs one logging run (section 6.3). A
   cheap guard in `sub_0011EC80` (section 6.4) turns this crash into a no-op, because `sub_001E8340` is already
   idempotent on a released chunk.

## 1. The table (Q1, Q2)

### 1.1 Layout [V]

| Address | Meaning |
|---|---|
| `0x3A9680` | event count (no bounds check anywhere) |
| `0x3A9684 + 8*i` | event i: `u16 type` (+0), `u16 queue id` (+2), `u32 param` (+4) |
| `0x3A9984 + 4*id` | queue object pointer table (`S`, `[0x3A9984+h*4]` in `movie-audio-clock.md`) |

Capacity before the queue table is hit: (0x3A9984 - 0x3A9684) / 8 = **96 events**. Neither writer checks the
count, and `sub_00126300` reads `[0x3A9680]` fresh on every iteration. [V] (No bound seen in `recomp_0008.c`
69984-69991 and 70224-70230; I did not look for a way to queue 96 events in a tick, there is none in sight.)

### 1.2 Writers: exactly two [V]

`grep` for `0x3A9680/84/88/8C` over `src/recomp/gen`: only `recomp_0008.c:69984-69991` and `:70224-70230`, plus
the reader (`:70255-70330`).

* **`sub_00126150(queue, reader, &size, &flag)`** (queue pop), block 0x126199-0x1261B5:
  `ev = 0x3A9684 + 8*count; ev.type = 1; ev.id = queue; ev.param = MEM32(slot + 8); count++`, only if
  `S+0x54` (callback A) is non-zero. `slot = S + 0x7C + (S[0x44] << 5)` is the **oldest** packet slot, taken when
  *all* readers have popped past it (first loop of the function: `reader counter [S+0x10+4i] > S[8]` for every
  reader). Then `S[0x44]++` (mod `S[0x40]`) and `S[8]++`. So a type-1 event means "the last reader is done with
  the tail packet", and it is **generated lazily, on the next pop call**, not at the moment of the last pop.
  `slot[+8]` is channel 0's data pointer of the packet (`sub_00125FA0` copies `desc[3+ch]` to `slot+8+4*ch`);
  `sub_0011F300` builds that descriptor (the pointers lie inside the SCDl chunk).
* **`sub_001262B0(queue, ch, n)`** ("n samples copied to the hardware ring"), block 0x1262D9-0x1262F0:
  `ev.type = 0; ev.id = queue; ev.param = n`, only if `ch == S+0x42` and `S+0x58 != 0`.

### 1.3 Who calls the writers, and when [V]

* `sub_00126150` / `sub_001262B0` callers: `sub_001293F0` (hardware-stream path, via `sub_00124F30` <-
  `sub_0011FAF0` <- `sub_001242D0`) and the three indirect codec pullers `sub_0012D5D0`, `sub_0012DBF0`,
  `sub_0012E290` (software mix, `sub_001241A0 -> sub_001291D0`). All run on the mixer thread
  (`sub_001242D0`) inside its tick.
* The mixer tick (`sub_001242D0`, loop at 0x1242E0): lock `ICALL [0x2E61C0]` -> user hook -> `sub_0011FAF0`
  (voices: events are created here) -> `sub_0011FAB0` -> `sub_001241A0` (ring refill, more type-0 events) ->
  **`sub_00126300` (flush, call at 0x124344)** -> hook -> unlock `ICALL [0x2E61C4]` (at 0x12435A). The table
  is empty again before the lock is dropped. [V]

### 1.4 The reader `sub_00126300` [V]

Behaviour: it reads the count at `0x3A9680` (again on every pass), walks the 8-byte events from `0x3A9688`, looks up the queue object of each event's queue id in the table at `0x3A9984`, and calls that object's callback at `+0x58` (type 0, three arguments: `0x11ECB0`) or `+0x54` (type 1, two arguments: `0x11EC80`) with the event's param and the context at `+0x5C`; then it sets the count to 0.
The callbacks are set once in `sub_00125B20` (queue create, called from `sub_0011F6E0`):
`+0x54 = 0x11EC80`, `+0x58 = 0x11ECB0`, `+0x5C = ctx = the channel object C` (the call at 0x11F7DF pushes
`0x11EC80, 0x11ECB0, esi`). [V]

* **Removal**: only by `sub_00126300` setting the count to 0 (end of each mixer tick, and from `sub_00126370`
  = stream stop, which calls it before it resets the queue; `sub_00126370` callers `sub_0011F0D0` (stop) and
  `sub_0011F4A0` (source switch), both under the engine lock `sub_0011FCE0`). Closing a queue
  (`sub_00126120`: `[0x3A9984+h*4] = 0`) does **not** touch the table.
  A type-0 event for a queue closed after it was queued would read `S = NULL` (`MEM32(0x58)`/`MEM32(0x54)`);
  the "jumped to 0x1E / 0x111 on a non-game thread" crashes of build 22 (rollback off, `rollback-sound-crashes.md`
  Fixed 2) have that shape. [I, not proven]

## 2. sub_001E8340 and the STRM object (Q1)

### 2.1 sub_001E8340(handle, chunk) [V] (cdecl, 2 args, `recomp_0013.c:9345`)

Behaviour: it returns at once for a null handle, for a handle cell whose object lacks the `'STRM'` magic (`0x4D525453`), for a chunk outside the ring (`+0x40` start, `+0x44` end), and for a chunk already marked released (`0xFFFFFFFE`). Otherwise it takes the size from the chunk's second word, marks the chunk released, lowers the buffered byte count (`+0x5C`; below the low-water mark `+0x54` it asks the file layer for more through `sub_001E56A0`), moves the state at `+0x48` from 2 back to 1 under the object's lock, and reclaims the ring through `sub_001E8080` (advance the tail over released chunks, wrap at `0xFFFFFFFF`, issue the next read).
It reads: handle cell, `S` magic, ring bounds, the chunk tag. The first dereference is `esi = MEM32(handle)`; with
`handle` = junk that is where the host fault lands (the "+0x6D" in the reports is the host offset of that load).

### 2.2 STRM ring protocol [V unless marked]

* `sub_001E7900(nBuf, nHandles, ?, mem, size)` builds a `STRM` object inside a caller-provided block:
  `+0x00 'STRM'`, `+0x04` critical section, `+0x24/0x28` request-node array (0x124 bytes each), `+0x2C` handle
  filter table (12-byte entries: mask, value, state), `+0x34` handle cells (16 bytes: STRM*, index+1, ..),
  `+0x3C/+0x40/+0x60/+0x64/+0x68` ring start/read/tail/head/write pointers (128-aligned), `+0x44` ring end.
  Returns the handle-cell array.
* Chunks in the ring are EA `SCxl` blocks: `chunk[0]` = tag (`'SCDl'` data = 0x6C444353, `'SCHl'` header =
  0x6C484353), `chunk[1]` = size | handle bits. Special `chunk[0]` values: `0xFFFFFFFF` wrap marker (written by
  `sub_001E8080`), **`0xFFFFFFFE` released**.
* `sub_001E7560` (parse new bytes, reached from `sub_001E7FB0`, which `sub_001E8080` calls) writes `0xFFFFFFFE` into
  chunks no handle wants. `sub_001E8600` (STRM flush, from the stream stop `sub_0011F0D0`) walks the ring from tail
  to head and writes `0xFFFFFFFE` into **every** chunk, then resets the ring (`+0x64 = +0x40 = +0x3C`).
* `sub_0011F300` (the pump submitting a data chunk, game thread) does `MEM32(P-4) = chunk` and
  `chunk[0] = node->id` (`node = C[0x134]`; low byte = channel index into `0x3A92A0`), then `sub_00125FA0`
  pushes the packet. This is the back-pointer `sub_0011EC80` follows. [V that the stores exist, I that
  `eax` there is `desc[3]` = P0]
* `sub_0011EC80(param, ctx)` = [V]
  Behaviour: the chunk is the word before the payload; the channel is the low byte of the chunk's first word, used as an index into `0x3A92A0`; it tail-jumps to `sub_001E8340` with that channel object's stream handle and the chunk.
  No check of the tag, of `id < [0x3A9386]`, or of `obj != 0`.

## 3. The five crashes decoded [V]

`edi` is `sub_00126300`'s loop index, `esi` its cursor (0x3A9688 + 8*edi). Stack: `ret 0x12634C` = type-1 path.
The table's own words give `0x3A9698`.

| log | step / rollbacks | edi | esi | ecx | edx | eax = `[edx]` | `[0x3A9698]` |
|---|---|---|---|---|---|---|---|
| filecrash / 225329 | match 2 step 0, 2130 | 0 | 3A9688 | FE | 832D7C94 | F6FE0F27 | 832D7C94 (dump +0x10) |
| 131300 | step 3820, 2592, predicting | 2 | 3A9698 | FE | 832DFA34 | E98BE9D4 | 832DFA34 (= esi's word) |
| 132125 | step 5950, 2753, predicting | 5 | 3A96B0 | FE | 83299BF0 | E1F2CD17 | not in dump |
| 133007 | match 2 step 0, 1027 | 2 | 3A9698 | FE | 832E88B4 | FCD0FCA0 | 832E88B4 (= esi's word) |

* In every case `ecx = 0xFE` and `edx` equals the word at `0x3A9698` where visible. 0x3A92A0 + 0xFE*4 = 0x3A9698.
* `eax` is the first dword of the payload that event 2's `param` points to: PCM pairs (`FCD0 FCA0`, ...). It was
  never a pointer, hence the fault at `MEM32(eax)` in `sub_001E8340`.
* Event params seen: 832E92E4, 832D7C94, 832DFA34, 832E88B4, 832F0CCC, 83299BF0. The 256 KB block is
  `832DA500..8331A500` (133007 log): 832E92E4, 832DFA34, 832E88B4, 832F0CCC lie **inside** it (ring offsets
  0xE3B4, 0x5534, ...); 832D7C94 and 83299BF0 are in other arena blocks (not identified; other streams' rings).
* The dumps show other events around: type-0 events (`hdr 0x00050000`, i.e. queue 5, n = 0x180, 0x108, 0xB4, 0x24,
  ...) and queues 0, 1, 2, 5 alive at once.
* Timeline (133007): `music/mamasaid/tune.mpf`, `loading0.mus` opened (line 136750), match 2 set up, intro
  speech `intro/025.asf`, `002.asf` opened, crash at line 138715. 131300: `nuffrespect/tune.mpf` and
  `overlay0.mus` 730 lines before the crash. 132125: `cnnyessir/tune.mpf` and `overlay0.mus` 140 lines before.
  filecrash: `mamasaid/loading0.mus` then the same intro speech.

## 4. The 256 KB block (Q3) [V unless marked]

* Allocation: `sub_0001D880` (AEMS init, called from `sub_0003C6E0` at 0x3C8A2, itself from `sub_00011E40` at boot),
  at 0x1DB24: `sub_000296B0(0x40000, tag 0x287E78, 0)`, result in **`[0x331D1C]`**. Tag string at 0x287E78:
  **`AUDIO::PathStreamBuffer`**. The matching 0x2800 block (`[0x331DA4]`) is `AUDIO::PathOverlayBuffer`.
  (Other AEMS tags: `AudioHeap` 0x400, `AUDIO::INIT::Aems Stream Buffer`, `SPCHSTRM` 0x19000+, `GENRSTRM` x4,
  `PATH_OV_STRM`.)
* Use: `sub_0001CBE0` (called by the music loader `sub_0003BE50` and the music director `sub_0003D060`) opens
  `music/%s/main.mus` (0x2902CC; `loading0.mus`, `tune.mpf`, `overlay0.mus` are the siblings) with
  `sub_00131310(flags, name, 1000, 0, 2, 100, [0x331D1C], size)`. That function zeroes a 0x110-byte sound-bank slot
  header at the block start, registers it in the player table `[0x3AD928]+0x58`, and passes the *rest of the block* to
  `sub_000BF750` (player slot in `0x3CD040[0..23]`) -> `sub_0011F9E0` -> `sub_0011F6E0`, which in turn lays out, in
  that memory: the channel object C (0x210+ bytes, `0x3A92A0[id]`), the queue S (`sub_00125B20`, `0x3A9984[h]`), and
  the STRM object + data ring (`sub_001E7900`).
* So it is **not a pool and not a cache: it is the music stream's whole object graph and its ring buffer.**
  It is never freed; the objects inside are **rebuilt in place at every track change**
  (`sub_0011F6E0` re-zeroes C with `sub_00126970`, `sub_001E7900` re-initialises STRM).
* Writers of the ring: the sample data comes from asynchronous file-layer reads into the ring write pointer
  (`sub_001E7790`/`sub_001E7FB0`/`sub_001E56A0`, completed on the file worker thread) [I for the exact copy path;
  `sub_001ED010` is just memcpy and is used for the wrap pad]. Chunk headers are written by `sub_001E7560` (parse),
  `sub_0011F300` (game thread: `chunk[0]`, `P-4`), `sub_001E8340` / `sub_001E8600` (release marks), `sub_001E8080`
  (wrap marker). The mixer writes the ring only through `sub_001E8340` -> `sub_001E8080` -> `sub_001E7FB0` ->
  `sub_001E7560` [V call chain].

## 5. Game-thread side (Q4) [V]

* **Track change**: the audio subsystem update `sub_0003D960` (called first in every screen update
  `sub_00079B60`, so once per step) runs the music director `sub_0003D060` (call at 0x3D973), which calls
  `sub_0003BE50(name, ..)`. `sub_0003BE50` =
  `sub_0001D5B0` (close music: `sub_001329A0(0xFFFFFF)` stops every player's stream through `sub_000C0480` ->
  `sub_0011F0D0` -> `sub_00126370` + `sub_001E8600`; then `sub_00131620([0x2DBC40])` (overlay) and
  `sub_00131620([0x331D20])` (main) -> `sub_000BF8F0` -> `sub_0011F5F0` (close: `sub_0011F0D0`, table cells
  `0x3A92A0[id] = 0`, `sub_00126120` queue destroy, `sub_001E87B0` STRM close after the unlock)) then
  `sub_001E41B0` (read `tune.mpf`), `sub_0001CBB0` (open), `sub_0001CBE0` (main/loading) and `sub_0001CC70`
  (overlay) as above. Each of these takes the engine lock separately; the file opens between them block the game
  thread for tens of ms while the mixer keeps ticking.
* **Other stream control** on the game thread: `sub_0001D7C0` (restart a GENRSTRM slot in its own block: close +
  `sub_0011F9E0` again, from `sub_000369A0/0003A570/0003B900`, i.e. mode changes in the audio subsystem), `sub_0001D670`
  (open a slot at boot, `sub_0003C6E0`), `sub_0001C720` (SPCHSTRM, speech), the pump `sub_0011F4A0` (below).
* **The pump** `sub_0011F4A0` (submits chunks, switches sources, `sub_00126370` + `sub_0011E9C0` -> `sub_00125BD0`
  queue reset) is registered in the callback table `0x3A94D8` and run from `sub_0011BBB0` <- `sub_00127550` <-
  the game's tick scheduler `sub_001ECE20` (main/game thread, wall-clock paced, `movie-audio-clock.md` section 2). It
  holds the engine lock for its whole run.
* **Free/reuse while an event still names a buffer?** Memory-wise no: the block is never freed, so no event ever
  names freed memory. What *is* reused is the memory *inside* the block: C, S and the ring are rebuilt at each
  `sub_0001CBE0`. Releases: a chunk is released (a) by the type-1 event, (b) by `sub_0011F4A0`/`sub_0011F440`
  for chunks it will not submit (`sub_001E8340` direct), (c) in bulk by `sub_001E8600` on every stop, (d) by
  `sub_001E7560` for unwanted chunks. Within one lock hold (a), (c) cannot interleave with an event of the same
  chunk in the original design: the table is flushed by `sub_00126370` before `sub_001E8600`, and the readers are
  freed (`sub_00122ED0`) before the queue is closed or reset. [V for the order, I for "cannot"]
* Nothing in this path is a *sound command* (`sub_00137010`) or one of the ten silenced bus handlers (list in
  `netplay.c`: 34A60, 38440, 35250, 35670, 35890, 35970, 35A20, 35AE0, 38620, 38760). [V by grep]

## 6. Best hypothesis for the rollback link (Q5)

### 6.1 What the logs rule out

* A load does not touch any of this memory: `0x3A9000..0x3B1000` is skipped (`k_data_skip`), the AEMS globals
  `0x331C20..0x3323C0` and the game's sound globals `0x33E778..0x33ECA0`, `0x340318..0x340D58` are not in `k_data`,
  the ring/C/S/STRM live in the arena, the file layer's request block is `skip_heap`'d. [V] So there is no
  "restored half of the structure". Stale heap restore as the *direct* cause is not supported.
* The block does not move: one arena event (alloc) in 133007. The junk is not a freed-and-reused buffer.
* The rollback hooks that could change *behaviour* (not just memory) around this are only the lock wrappers
  (`rollback_mixer_lock_*`, which call the real lock; `mixer_begin`/`mixer_end` copy and compare every tracked
  heap page, MBs per tick, **inside** the engine lock, so ticks run long and late) and the allocator routing.

### 6.2 The hypothesis [I]

All five crashes are within about a second of a music (re)open, and the re-run steps of a rollback run the
audio subsystem's update again:

1. `netplay.c` re-run loop (`:2953-2965`) calls `sub_00077040` (screen manager) for every re-run step; that calls
   `sub_00079B60` -> **`sub_0003D960` -> `sub_0003D060`** (music director). Only sound commands and the 10 bus
   handlers are silenced; `sub_0003D960` is not (grep over `src/` for 3D960/3D060: no hook). [V]
2. The director is a state machine in *non-restored* globals (`0x340538` counter "+1 per call", `0x340540`,
   `0x3407B4/B8`, `0x33E9DC`, `0x340D53`, ...), driven by game state that a load *does* rewind. Every rollback of
   k steps therefore gives it k extra calls and may replay a decision whose side effect (stream open/close,
   `sub_0003BE50`) already happened. The in-fight track changes at steps 3,820 and 5,950 (earlier than the ~2.7 min,
   ~9,700 steps, of the 2026-10-25 note) fit a call-counted timer running ahead by the re-run steps. [I]
3. A second `sub_0003BE50` / `sub_0001D5B0` / `sub_0001D7C0` sequence on a stream that is still being torn
   down or filled (or two in a row) releases/flushes chunks that queue slots still reference: `sub_001E8600` marks
   *all* ring chunks `0xFFFFFFFE` while the mixer's queue S (rebuilt or not yet destroyed) still holds the old
   slots and the lazily generated last-slot retire event. The event then reads a released tag. The guarding
   assumption of the original (stop is serialized with the mixer and happens once per source) is what a replayed
   director breaks. [I]
4. The fact that it needs rollback to happen but no state is restored points at *behaviour*, not memory: replayed
   audio-subsystem logic, or timing (long locked ticks, a starved/bursty pump on one core per copy) widening a race
   that the original never loses. The two cannot be told apart statically.

### 6.3 One logging run that settles it

Log, with thread id, `N.resim`, tick and caller: (a) every type-1 event creation in `sub_00126150` (queue id, P,
chunk, `chunk[0]`), (b) every `sub_001E8340` that actually releases (`chunk[0]` was not `0xFFFFFFFE`) and every
early return for that reason, (c) `sub_001E8600` and `sub_00125BD0` entries (queue id, ring bounds), (d) entries of
`sub_0003BE50`, `sub_0001D5B0`, `sub_0001D7C0`, `sub_0011F5F0` with `N.resim`. The first line that
names the crashing chunk twice (or a (c)/(d) between (a) and its event) is the cause. If (d) shows `N.resim == 1`,
silencing the director in re-run steps is the real fix.

### 6.4 Robust mitigation, independent of the cause

`sub_0011EC80` has no direct callers (only `sub_00126300` calls it indirectly), so it can be overridden in
`recomp_lookup_manual` like `0x137010`. A guarded version is semantically what `sub_001E8340` already does for a
released chunk:

Proposed guard, in words: return when the chunk word before the payload is not guest memory, when the chunk is already released (`0xFFFFFFFE`), when its channel byte is not below the channel count at `0x3A9386`, or when the channel's slot in `0x3A92A0` is empty; otherwise go on to `sub_001E8340` as before.
`sub_0011ECB0` (type 0) can get a similar check (`ctx == 0`, `MEM32(ctx+0x11C) == 0`). The guard stops the
crash; whether the stream then plays on correctly depends on why the chunk was released early (probably it does,
since the ring bookkeeping was already updated by the first release).

### 6.5 What would have to be consistent to prevent it by construction

* One retire per queue slot, before the chunk is released any other way: the deferred table, the queue objects
  (`0x3A9984`), the channel objects (`0x3A92A0`), the STRM objects and the ring tags are one unit; nothing may
  stop/flush/reopen a stream while the table or the queue still holds events/slots for it.
* The game's idea of the music state (restored heap) and the director's state (non-restored `.data`) must stay
  in step: either skip `sub_0003D960` in re-run steps (like the bus handlers), or make the director's
  decisions depend only on engine state.
* No code may hold the engine lock for MBs of page work (`mixer_begin/end`), or the pump's pacing changes.

## 7. Address index

| Item | Address |
|---|---|
| deferred table | count 0x3A9680, events 0x3A9684.. (8 B), 96 max |
| flush / writers | `sub_00126300`; `sub_00126150` @0x126199; `sub_001262B0` @0x1262D9 |
| stop / reset | `sub_00126370` (stop+flush), `sub_00125BD0` (start, resets counters), `sub_00126120` (destroy) |
| callbacks | `sub_0011EC80` (type 1), `sub_0011ECB0` (type 0), set in `sub_00125B20` at +0x54/+0x58/+0x5C |
| tables | channel objects `0x3A92A0[id]` (count byte `0x3A9386`), queues `0x3A9984[h]` |
| STRM | `sub_001E7900` init, `sub_001E8340` release, `sub_001E8080` reclaim, `sub_001E8600` flush, `sub_001E7560` parse, `sub_001E7D80` peek, `sub_001E7FB0` read-done |
| block | `[0x331D1C]` `AUDIO::PathStreamBuffer` 0x40000 (alloc @0x1DB24 in `sub_0001D880`); overlay `[0x331DA4]` 0x2800 |
| music | `sub_0003D960` -> `sub_0003D060` -> `sub_0003BE50` -> `sub_0001D5B0` / `sub_0001CBE0` / `sub_0001CC70`; main handle `[0x331D20]`, overlay `[0x2DBC40]` |
| pump / mixer | `sub_0011F4A0` (game thread), `sub_001242D0` (mixer thread) |
| engine lock | `ICALL [0x2E61C0]` / `[0x2E61C4]`, wrappers `rollback_mixer_lock_*` |

## 8. Open

* Which of the other two arena blocks hold the rings behind 832D7C94 and 83299BF0 (SPCHSTRM `[0x331EE0]`, a GENRSTRM
  `[0x331ECC+4i]`, or a file-layer 0x47B80 buffer).
* Whether `sub_0003D060` really re-enters `sub_0003BE50` in re-run steps (section 6.3 (d)).
* Whether the pump's lazily released last slot of a source is released at all in the original on a source switch
  (the reset in `sub_00125BD0` forgets it); not needed for this crash but relevant to ring clogging.
