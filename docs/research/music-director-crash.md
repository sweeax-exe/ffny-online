# The music director crash in sub_001317C0 (read-only study, 2026-10-25)

Scope: lifted C and the XBE disassembly (capstone over `game/default.xbe`, guest VAs), the crash logs
`logs/run-20261024-212529`, `-221020`, `run-20261025-124810`, `-130645`, `-142137`, `-145206` (`.log.err`),
`src/hooks/rollback.c`, and the files of the tracks involved (`music/<song>/tune.mpf`, header fields only; no game
bytes are stored anywhere). Nothing was built or run. `[V]` = read in code, log or file, `[I]` = inferred.
Addresses are guest addresses. Earlier notes: `stream-table-crash.md`, `rollback-sound-crashes.md`.

## 0. Headline

1. **The crash is a stale sound-bank instance, and the cause is that a rollback load rewinds the overlay handle
   `[0x2DBC40]`.** `[0x2DBC40]` (with `[0x2DBC44]` and the name buffer `0x2DBC48`) is the only per-track sound state
   that lives in the *restored* `.data` (0x2D54E0..0x331C20). It holds the handle of the music's overlay player
   (`0x11000004`) or -1. Everything else the director and the engine use is outside the restore. [V by an address scan
   of the sound code, section 4.1]
2. A load that goes back to a save taken before the overlay was opened puts `-1` there while the engine still has
   the overlay player open (engine memory is not rolled back, and the director's state machine `[0x3407B4]` is
   already at 0x7F, so nothing reopens it). At the next track change `sub_0001D5B0` (close music) skips the overlay
   (`if [0x2DBC40] != -1`), closes only the main player, and `sub_00131620` does not free the bank instance because the
   overlay player is still in it (`sub_00133830` counts it). The instance stays registered with pointers derived
   from the **old** `tune.mpf`, while its data pointer `[inst+0x34]` is the shared 0x9000-byte "Fixed MPF Buffer", which
   the next `tune.mpf` read overwrites. [V code, I that this is the exact sequence]
3. The next `sub_0001CBE0` builds the handle `0x11000001`; `sub_001317C0` selects the *first* instance with id
   `0x11000000`, which is the stale one, and reads its offset table at the old song's table position inside the new
   song's file. The two dwords it takes as "start" and "end" are junk, and the loop (bounded only by `end`) walks off
   the end of guest memory. [V]
4. **Replay proof (offline, from the files):** taking the old song's table offset and reading the new song's
   `tune.mpf` there reproduces the crash registers exactly in all six logs (section 3): `ecx` at entry (`start`),
   `edi` (`end - 8`), and which of the two loops faults.
5. Rollback is not needed to *read* the stale instance, only to leave it behind. Without a rewind the close path is
   correct. A one-line, hook-free fix is a byte-level hole in `k_data` for `0x2DBC40..` (section 5.1); a cheap guard
   that also turns the crash into a no-op is in section 5.2.

## 1. What `sub_001317C0` is (Q1)

`int sub_001317C0(u32 handle, int ms)` (cdecl, 2 args; `ms` is always 3000 = `0xBB8`). Direct callers
`sub_0001CBE0` (main music) and `sub_0001CD90` (a variant reached only indirectly). Lifted in `recomp_0009.c:18410`;
real code at 0x1317C0..0x131A1D. [V]

```
if (!MEM8(0x3AD934) || MEM32(0x3AD938)) return -18;          // engine not initialised, or the API is "busy"
if (!sub_00133920(-1)) return -18;                           // sets [3AD938] = -1 (busy sentinel)
inst = sub_00133980(handle & 0xFF000000);                    // find instance by id; selects it:
if (!inst) return 0;                                         //   [3AD928] = inst, [3AD938] = inst id   (see 3.1)
idx = lowest set bit of (handle & 0x00FFFFFF)  (0..23, 24 if none)      // the player slot bit
base = [inst+0x34];  N = MEM8(base+0xD);  if (idx > N) return 0;
P    = [inst+0x4C];                                          // table of dword offsets, relative to base
start = base + 4*P[idx];   end = base + 4*P[idx+1];          // an array of 8-byte records {u32 a, u32 b}
for (r = start; r < end - 8; r += 8)                         // 4x unrolled (0x20 bytes, signed compare) + tail loop
        ratio = (float)(((r[2]-r[0]) << 7) / r[1]);          // fild / fadd 2^32 for "unsigned" / fdivp / fstp float
[3AD938] = 0; [3AD928] = 0;                                  // deselect (only on the fall-through exit!)
return (int)(ms * last_ratio + 0.5);                         // x87 _ftol (0x200BCC); the running max is popped and unused
```

* The result is a byte count: `sub_0001CBE0` adds it to `sub_00131780(2,100)` (the player's own structures) and passes
  the sum as the buffer size to `sub_00131310`. It is an estimate (3000 times the data rate of the last record),
  not something the engine depends on being exact. [V that it is added; I for the meaning of the ratio]
* Tables it reads: `[0x3AD928]` (the selected instance), `[inst+0x34]` (data pointer), `byte [data+0xD]` (count N),
  `[inst+0x4C]` (offset table), then the record array. The instance layout is in section 3.2.
* **The loop is bounded only by `end`** (`edi = base + 4*P[idx+1] - 8`), never by the buffer size. With a valid bank
  `end - start` is about 0x100..0x600 bytes inside the 0x9000-byte buffer. Fed junk, it runs through memory as long as
  `ecx < edi`:
  * Unrolled loop, `+0x34D` (host): the loop test `cmp ecx, ebp; jl` is **signed**, so it only continues while
    both are on the same side of 0x80000000. A start in the 0x8xxxxxxx window with a huge `end` walks the window
    to its end (`0x84130000`, the first unmapped page). `esi = 0` in that crash = it was reading zeroed memory. [V]
  * Tail loop, `+0xFCD` (host): `cmp ecx, edi; jb` unsigned. Entered when the signed test fails first (start positive
    below 0x80000000, `end` above), it walks from the junk start up to `0x6C000000` (fault address `0x7C000000` host)
    with `esi = 0x11000001` still the handle. [V]
* Latent bug, not our crash: the two early exits that return 0 (`id not found`, `idx > N`, code at 0x131817) leave
  `[0x3AD938]` set (-1 or the id). Every later SND API call (`sub_1317C0`, `sub_131310`, `sub_131620`) then
  fails on the "busy" test. A guard that returns early must clear both `[0x3AD938]` and `[0x3AD928]` itself.

## 2. sub_0001CBE0, the director and the load (Q2)

### 2.1 What `sub_0001CBE0(name, slot, class)` passes [V]

```
handle = 0x10000000 | (0x1000000 << class) | (1 << slot)          // the director calls (name, 0, 0): 0x11000001
[0x331D08] = handle
extra = sub_001317C0(handle, 3000)
size  = extra + sub_00131780(2, 100)
r = sub_00131310(handle, name, 1000, 0, 2, 100, [0x331D1C] /*AUDIO::PathStreamBuffer, 256 KB*/, size)
[0x331D20] = (r >= 0) ? handle : -1;   return r >= 0
```
The overlay variant `sub_0001CC70(name, buf, slot=2, class=0)` makes handle `0x11000004`, opens it on the 0x2800-byte
`[0x331DA4]` block and stores it in `[0x2DBC40]`, with the name copied to `0x2DBC48`.

The `0x11000000` part is the instance id the caller *expects*: `0x10000000 << n | 0x1000000 << class` where n is the
number of already-registered instances of that class (see 3.2). It is hard-coded to n = 0.

### 2.2 The director state machine [V] (`sub_0003D060` called every update; `sub_0003BE50` starts a load)

| Variable | Meaning |
|---|---|
| `[0x3407B4]` | state: 0 idle, 1 `tune.mpf` read pending, 2 overlay/loading0 read pending, 0x7F done, -1 failed |
| `[0x3407B8]` | file request handle of the pending async read |
| `[0x3407B0]` / `[0x3407AC]` | mode (0 main only, 2 main + overlay, 1/3 synchronous modes) / argument (0x16) |
| `0x340768` | song name; `[0x3407A8]` overlay buffer |
| `[0x33EC2C]` | "Fixed MPF Buffer", 0x9000 bytes |
| `[0x33E9E4]` | AudioManagedRAM, 0x96000 bytes (overlay file lands here) |

Flow for modes 0 and 2 (the async ones, `sub_0003BE50+0x1B0..`):
1. `sub_0003BE50` calls **`sub_0001D5B0` (close music)** first, then `sub_0001ADD0("music/%s/tune.mpf", [0x33EC2C], 0x9000)`:
   an **asynchronous** read through the file layer (`sub_001E6750`; AEMS request table at `0x3319A0`, 32 slots of
   0x14 bytes; completion callback `sub_0001AA10` sets the done byte at +0x11). State = 1.
2. Each update `sub_0003D060` polls `sub_0001AB20([0x3407B8], 0, 0)`. Only when it returns true does it call
   `sub_0001CBB0([0x33EC2C])` (-> `sub_00131080`, registers the bank), then `sub_0001CBE0` (this crash), then, for mode 2,
   reads `overlay0.mus` (0x96000 bytes, same async mechanism, state 2) and finally `sub_0001CC70`.
3. Modes 1 and 3 (`dirty` at the start, `loading0.mus`, `walkwithme`) use the synchronous `sub_001E41B0` instead.

**The director does not use the data before the read completed.** In the 145206 crash the log shows the whole
`whatshappening/tune.mpf` read (8192 + 8192 + 3808 = 20192 bytes = the file's size) 47 lines before the crash, magic
`78 44 46 50` ("xDFP") first. `makeithurt` (19584 = 2*8192 + 3200) in the same log matches too. The buffer holds a
valid new file at the time of the crash. [V]

Minor risk (not this crash): `sub_0001ADD0` returns 0 when no AEMS slot is free or the open fails; the director then
polls `sub_0001AB20(0, ..)`, which matches a free slot (handle 0) and returns that slot's stale done byte.

### 2.3 Where the data goes [V]

`[0x33EC2C]` is allocated once at boot in `sub_0003C6E0+0x50` (`sub_000296B0(0x9000, "Fixed MPF Buffer")`), second
after AudioManagedRAM (`831B0080`, 0x96000). In every rollback log this puts it at **`0x83246100`** (arena: user pointer
= block start + 0x80; AudioManagedRAM block ends at 0x83246090). It is never freed and never moves; each track change
overwrites it in place. That address is also what makes the replay in section 3 come out exact. The 256 KB stream
buffer and the objects `stream-table-crash.md` studied are not involved.

## 3. The bank instances (Q3)

### 3.1 Selection

`[0x3AD928]` is not a table; it is the **currently selected instance pointer**, a plain global with no lock, set and
cleared inside each SND API call:
* `sub_00133920(idx)`: idx -1 sets `[0x3AD938] = -1` if it was 0 (marks the API busy); 0..3 with an active instance sets
  `[0x3AD928] = 0x3AB488 + idx*0x928` and `[0x3AD938] = [inst+0x30]` (the id) and returns it.
* `sub_00133980(mask)`: scans the 4 instances for `[inst+0x34] != 0 && [inst+0x30] == mask`; **first match wins**; tail-calls
  `sub_00133920(idx)`.
* Every API function (`sub_00131310`, `sub_00131620`, `sub_001317C0`, ...) selects, works, and writes 0 to both globals
  at the end. Callers of `sub_00133920`: 21 functions (listed by `scripts/callers.py`).

### 3.2 Instance layout (4 x 0x928 bytes at 0x3AB488; `0x3AD928` is the first byte after the array) [V from sub_00131080]

| Offset | Content |
|---|---|
| +0x30 | id = `0x10000000 << n | 0x1000000 << class` (n = number of other registered instances whose data header has the same class byte) |
| +0x34 | data pointer (the `tune.mpf` buffer); nonzero = instance in use |
| +0x38..+0x54 | absolute pointers into that data, **computed once at registration** (+0x4C = offset table, +0x50, +0x54) |
| +0x58..+0xB8 | 24 player pointers, indexed by the slot bit of the handle |
| +0xC/10/14/18 | 0x32, 0xA, 0x3E8, 1 |

Header of the data (`tune.mpf`): magic `0x50464478`, byte +4 = 4 (version), +0xC class (0 in all 33 music files), +0xD N (4 in
all), +0xF, +0x10, u16 +0x12. The +0x4C table is a dword-offset table (N + 1 entries) followed by the records.

### 3.3 Registration and release

* **Built:** `sub_00131080(data)` (via `sub_0001CBB0`) takes the first free slot, zeroes it, counts same-class instances,
  derives +0x30..+0x54. It reads the class of *existing* instances from `byte [inst+0x34 + 0xC]`, i.e. from whatever the
  shared buffer holds now. Boot also registers a bank through `sub_0001DB80` (`[0x331EC8]`, a file-layer block in the arena).
* **Freed:** `sub_00131620(handle)` (player close): clears the player slot, `sub_000BF8F0(player)`, then
  `if (sub_00133830(0x1000000 << instance_index) == 0) { [inst+0x34] = 0; [inst+0xB8] = 0; sub_001339C0(); }`.
  `sub_00133830` counts **all 24 player slots** of the instance. The instance therefore dies only with its **last**
  player. [V]
* **Close music** `sub_0001D5B0`: `sub_001329A0(0xFFFFFF)` (stop all players), then
  `if ([0x2DBC40] != -1) { sub_00120270([0x331EAC]); sub_00131620([0x2DBC40]); [0x2DBC40] = -1 }`, then
  `if ([0x331D20] != -1) { sub_00131620([0x331D20]); [0x331D20] = -1 }`. Other callers: `sub_0003A570`, `sub_0003B900`,
  `sub_0003BE50`, `sub_0003E170`.

### 3.4 Can `[0x3AD928]` point at freed, reused or unfilled memory?

The pointer is always inside the static array. What can be wrong is the **instance contents**:
* The data buffer is never freed, but it is **reused in place**: a registered instance whose players were not all
  closed keeps its derived pointers (+0x38..+0x54) while the buffer receives the next song. That is this crash.
* "Not yet filled": no. The registration happens only after `sub_0001AB20` reported the read done (2.2).
* Freed memory: only the boot bank's block (`[0x331EC8]`, freed by `sub_0001D620`) would qualify; it is not involved.

## 4. What produces it under rollback (Q4)

### 4.1 The restore and the sound code

An address scan (every absolute `.data` operand in the AEMS code 0x1C000..0x1E000, the audio subsystem
0x30000..0x3E200, the SND control layer 0x130000..0x135000, the player layer 0xBE000..0xC1000, the sound engine
0x11C000..0x142000 and the file layer 0x1E3000..0x1E9000) against `k_data`/`k_data_skip` in `rollback.c` gives:

* Not restored, and so in step with the engine: `0x331Cxx` (AEMS: `[0x331D20]`, `[0x331EE4]`, async table `0x3319A0`),
  `0x33E778..0x33ECA0` (`[0x33EC2C]`, `[0x33E9E4]`), `0x340318..0x340D58` (director state, mode `[0x340374]`),
  the instance array `0x3AB488..0x3AD938` and the engine globals (pages 0x3A9000..0x3B1000), `0x3CD040`.
* **Restored: `0x2DBC40` (overlay handle, 17 references), `0x2DBC44` (overlay buffer pointer, 4), `0x2DBC48` (overlay name).**
  The other hits are constants or one-time init tables (`0x2FB148` the game singleton, `0x33AB30` a loop bound, `0x39E858` a
  text scratch, `0x39EA58..` and `0x39E844..` callback/slot tables written once at init, `0x3A71A8..` init-once hooks).

So the director/engine side is self-consistent across a load, with one exception: the overlay handle.

### 4.2 The sequence

1. Mode-2 track starts (`tune.mpf` + `overlay0.mus`). The overlay opens at step T (`[0x2DBC40] = 0x11000004`). The
   rollback tracking is on, so saves are taken every step.
2. A rollback within about 9 steps after T loads a save from before T: `[0x2DBC40]` (and `[0x2DBC44]`) go back to -1
   / 0. The engine keeps the overlay player in slot 2 of instance 0. The director is at state 0x7F, so it does not open
   it again. (About one step in ten starts a rollback here, with a depth of up to 9, so this happens in a large share
   of overlay opens.)
3. The track ends (KO, or the in-fight change). `sub_0001D5B0`: overlay skipped, main closed, instance **not freed**.
4. The next song: `sub_0003BE50` reads its `tune.mpf` into the same buffer; `sub_00131080` finds slot 0 taken, counts
   the stale instance as same-class (it reads class from the *new* header in the shared buffer), puts the new
   instance in slot 1 with id **`0x21000000`**.
5. `sub_0001CBE0` uses handle `0x11000001`; `sub_00133980(0x11000000)` returns the stale slot 0. `sub_001317C0` reads
   `P = [stale+0x4C]` (the old file's table position) in the new file: `start`/`end` are junk. Crash.
   When the junk happens to give a small range, no crash, but the new main player is then created in the stale
   instance with wrong derived tables (silent wrong state; see 4.4).

### 4.3 Replay against the six logs (the evidence)

For each log, take the previous in-fight (overlay) track as the stale one, parse its `tune.mpf` header the way
`sub_00131080` does (+0x4C), read two dwords at that position in the new song's file, and compute
`start = 0x83246100 + 4*off0`, `edi = 0x83246100 + 4*off1 - 8`. Observed values are from the `[CRASH]` register dump.

| log (run-...) | new song | stale song (overlay track) | predicted start (= initial ecx) / edi | observed |
|---|---|---|---|---|
| 20261024-212529 | mamasaid | getintoit | 8A019D74 / 8A424234 | ecx 8A019D74, edi 8A424234 |
| 20261024-221020 | getintoit | pump | 9364A100 / D92461F8 | ecx 9364A100, edi D92461F8 |
| 20261025-124810 | mamasaid | whatshappening (overlay0 at line 152375) | 02646200 / 832460F8 | edi 832460F8, ecx 6BFFFFF8 (tail loop, +0xFCD) |
| 20261025-130645 | whatshappening | letsgo | 83A46398 / 9364A0F8 | edi 9364A0F8, ecx 8412FFF8 (unrolled, +0x34D) |
| 20261025-142137 | hitem | getintoit | 47246108 / 832460F8 | edi 832460F8, ecx 6BFFFFF8 (+0xFCD) |
| 20261025-145206 | whatshappening | letsgo (loaded 116k log lines earlier; makeithurt, also an overlay track, came after it) | 83A46398 / 9364A0F8 | edi 9364A0F8, ecx 8412FFF8 (+0x34D) |

Every one of the six has an in-fight overlay track earlier in the same run, and the same stale song gives the same
registers whatever else happened (so "identical registers in three of them" is the same pair of songs, not chance).
`0x84130000` is where the 0x8xxxxxxx window ends; `0x6C000000` is where the low mapping ends. Which loop faults
follows from the sign of `start` (section 1). [V numerically; the identification of the stale song per log is the
most plausible reading of each log's music lines.]

### 4.4 Ranked hypotheses

| # | Hypothesis | Verdict |
|---|---|---|
| 1 | Restored `[0x2DBC40]` rewound to -1 -> overlay player leaked -> stale instance with old derived pointers over the reused shared buffer | **Matches all six logs to the byte (4.3). Near certain.** |
| 2 | Async read finishing into reused memory / director using the file before the read completed | Ruled out: full reads logged, sizes equal the file sizes, buffer holds the new header; registration waits on `sub_0001AB20`. |
| 3 | A stream buffer routed to the arena, freed or reused | Ruled out for this crash: the only buffer in the path is the boot-time block at `0x83246100`, never freed; the file-layer buffers are not read here. |
| 4 | A load-in-progress flag in restored memory | None exists: the state machine, request table and instance array are all outside the restore (4.1). |
| 5 | Director replays its decisions in re-run steps (`stream-table-crash.md` 6.2) | Not needed to explain this crash: state 0x7F is sticky, so a re-run does not reopen the overlay. (It may still matter for other crashes.) |
| 6 | A second consequence of the same rewind, the opposite direction (overlay closed after the save, handle H restored) | Plausible and harmless-looking: `sub_0001CC70` then refuses to open the next overlay (`[0x2DBC40] != -1`), and `sub_0001D5B0` calls `sub_00131620(H)` with no such player, whose fall-through frees the *selected* instance. Not seen in the logs; worth the same fix. |

**The one line that tells them apart (for the doubters):** log, at every rollback load, the old and new value of
`[0x2DBC40]` when they differ (`[ROLLBACK] load rewound [0x2DBC40] 11000004 -> FFFFFFFF`), and at `sub_0001D5B0` entry
`[0x2DBC40]`, `[0x331D20]`, `sub_00131760(0x11000004)` (returns -8 = 0xFFFFFFF8 when no player has that handle) and the
ids/data pointers of the four instances. `[0x2DBC40] == -1` together with a non-(-8) `sub_00131760(0x11000004)` is
hypothesis 1 caught in the act. Without a rebuild, `RECOMP_WATCH_WRITE=0x2DBC40` shows the load as the writer.

## 5. Fix and guard (Q5)

### 5.1 The cause: keep the overlay words out of the restore

The words are written only by the AEMS overlay code on the game thread and describe engine objects that are not
rolled back, so they must not be rolled back either. `rollback.c` has a byte-level mechanism for exactly this:
`k_data_hole` (the page around goes in, the hole stays live). Add

```
{ 0x002DBC40u, 0x002DBC48u + <length of the name buffer> }     // overlay handle, overlay buffer, overlay name
```
The name buffer is the destination of the string copy at 0x1CCDB/0x1CEDE (`music/%s/overlay0.mus` is under 40 bytes);
before choosing the end, check what else lives after `0x2DBC48` (only AEMS references were found, none beyond +0).
`k_data_skip` is page-granular (4 KB) and would take other game variables out of the restore; do not use it here.

### 5.2 A guard that rejects the bad call

The invariant the original keeps: **exactly one registered instance is backed by `[0x33EC2C]`, and it is the one with
id `handle & 0xFF000000`.** In the crash two are (the stale one in slot 0 and the new one in slot 1 with id `0x21000000`).

* **Repair, at registration** (hook `sub_0001CBB0_enter`, direct callers `sub_0003BE50` and `sub_0003D060`; this is the
  point that fixes the state, not just the symptom). Before the new bank is registered:
  ```
  for i in 0..3: inst = 0x3AB488 + i*0x928
      if (MEM32(inst+0x34) == MEM32(0x33EC2C)) {                       // still backed by the shared buffer
          for slot in 0..23: if (MEM32(inst+0x58+4*slot)) sub_00131620(MEM32(inst+0x30) | (1 << slot));
          if (MEM32(inst+0x34) == MEM32(0x33EC2C)) { MEM32(inst+0x34) = 0; MEM32(inst+0xB8) = 0; }   // last resort
      }
  ```
  `sub_00131620` is the engine's own close (it frees the instance with the last player), so nothing leaks; it needs
  `[0x3AD938] == 0`, which holds here (the game thread, no SND call in progress). It is a no-op in normal play (the
  buffer backs no instance then). Because the new instance does not exist yet, it also gets the right id
  (`0x11000000`).
* **Safety net, at the call** (in a hand-written `sub_0001CBE0`, or as a check before the call to `sub_001317C0`):
  `inst = instance with id (handle & 0xFF000000)`; `base = [inst+0x34]`, `P = [inst+0x4C]`, `idx` as the function
  computes it. Reject (skip the call, treat the size as 0, and let the caller's failure path run, or fail the whole
  open: `[0x331D20] = -1; return 0`) unless
  `base == [0x33EC2C]` (music) and `base <= P && P + 4*(idx+2) <= base + 0x9000` and
  `off0 <= off1 && 4*off1 <= 0x9000` with `off0 = P[idx]`, `off1 = P[idx+1]`.
  The real music files fit by a wide margin (largest file 33,288 bytes; records span roughly 0x100..0x600 bytes). This
  alone only turns the crash into silence, and a stale instance whose junk happens to be in range still gets through;
  hence the repair above is the guard to ship, this one a backstop.
* **Do not** make `sub_001317C0` itself bail out with a bare early `return`: both its fall-through exit and the
  early-0 exits behave differently with respect to `[0x3AD938]`/`[0x3AD928]` (section 1); an early exit must clear
  both, or the SND API stays "busy" for the rest of the session.
* A hand-written `sub_001317C0` is possible but pointless: the float result is only a buffer-size estimate, and
  reproducing the x87 sequence exactly buys nothing over guarding its input.

## 6. Not examined / open

* The mode value `[0x340374]` and how `Audio.PlayMusic()` (script) sets it; only its use in the director was read.
* `sub_0001CD90` (the indirect variant of `sub_0001CBE0`, calls `sub_001317C0` too): same stale-instance exposure by the
  same invariant, not traced.
* Whether the opposite rewind (hypothesis 6) or the same pattern exists for other per-track AEMS state outside the
  scanned code ranges, for example in the game's own cue wrappers (their words were not enumerated; the scan covered
  the audio, AEMS, SND, player, engine and file-layer code only).
* Which of the candidates for the 124810 stale song (whatshappening, holler, poppalarge all give the same registers);
  the log's music lines make whatshappening the only one played.
* The analysis scripts (disassembly of the XBE with capstone; the `tune.mpf` header model) are in the session scratch
  directory, not in `scripts/`. They read the files in memory and print header numbers only.
