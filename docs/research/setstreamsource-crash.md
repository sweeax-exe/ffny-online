# The SetStreamSource crash in rollback fights: the background crowd's frame number runs off its model (read-only study, 2026-10-25)

Scope: lifted C (`src/recomp/gen/recomp_0000/0003/0004/0006/0008/0015/0016.c`), the disassembly under `tools/xboxrecomp/tools/disasm/output/asm/text.asm`,
`src/hooks/rollback.c`, `src/hooks/netplay.c`, three crash reports and the user's own dump read with `tools/ffny-studio` (metadata and byte
offsets only; nothing copied out). Nothing was built or run. `[V]` = read in code, data or a log, `[I]` = inferred, `[U]` = not settled.
Addresses are guest addresses unless marked.

The three crashes (all build 26, all rollback on, `sub_0021BB10+0x9F` or `+0x8C`, same guest stack):

| | log | step / rollbacks | `edx` (the parameter block) | `edi` (the "vertex buffer") |
|---|---|---|---|---|
| A | `logs/bug-reports/20261025-161459.684-917ddc` | 701 / 298, last back to 697 from 700 | `0x8073A6F0` | `0x776F4C00` = bytes `00 4C 6F 77` |
| B | `logs/run-20261025-111715.log.err` (two-copy test) | 1322 / 239, last back to 1313 from 1320 | `0x807C46D0` | `0x776F7263` = "crow" |
| C | `logs/bug-reports/20261025-164936.988-0f9bbe` | 685 / 122, last back to 677 from 684 | `0x80743DE0` | `0x776F7263` = "crow" |

## 0. Short answer

1. **The block is a vertex-buffer descriptor inside the background crowd's model image, and the draw chose a descriptor that does not exist.** [V]
   Everything that reaches `sub_0021BB10` through this stack (`sub_00118030` stream opcode, `sub_00114FE0` interpreter, `sub_00111040`
   instance renderer, `sub_00115510`, the render list `sub_0001BF60`) is the **`CInstanceCrowd`** (the arena's background crowd, "InstanceCrowd"
   render method of `assets/icrowd.viv|icrowd01.o`). It is the only user of `sub_00111040`: its only item constructor, `sub_000DB2D0`, is the only
   caller of `sub_001153B0`, `sub_00115400` and `sub_00115550`. [V by `scripts/callers.py`]
2. **The descriptor is picked by a per-spectator 16-bit frame number.** The stream opcode computes `block = P[1] + C[1] * [0x3A5758] - 20`;
   for this model `C[1]` is `0x230` and `[0x3A5758]` is the frame number the instance renderer reads from the instance-state table
   (`item+8`, 4-byte entries, frame in the upper half). The model holds 383 stream-0 descriptors, 0x230 bytes apart (frames 0..382); the game
   keeps frames in 0..379 (idle 240 + excited 140, from `icrowd01.xml`). [V]
3. **All three crashes are that number running past the end of the image.** Crash A: frame 385 lands exactly on `\0Low_RezCrowd`, the only
   `\0Low` in the file, 16-byte aligned and on the 0x230 grid (chance coincidence about 1 in 560). Crash C: with the same image base as A
   (the same account, same allocation order), `0x80743DE0` is **exactly frame 454**, 0x2610 bytes past the end of the 245,712-byte image, into the
   next heap block, which starts with "crow". Crash B has the same value "crow" as C, so most likely a frame past the image end too (its image
   base is unknown, other machine). [V arithmetic, I for the base]
4. **A precursor appears in every crash:** `[HEAP] free #n va=0x80000000` (a `MmFreeContiguousMemory` of a resource whose data pointer is 0),
   the first ever in the process, 2 to 8+ times in the 1 to 50 frames before the crash (section 4). These are D3D releasing the "vertex buffer"
   that frames 383 and up pointed at: `sub_0021BB10` adds `0x80000` to the word at `block[0]`'s target, a later bind subtracts it, and a word that
   was zero reaches "no references" and is destroyed (data free of `0|0x80000000`, then a free of the header through the held XAPI free).
   So the bad frames write into the model's tail (and the neighbour) for several frames before one dereference of text kills the process. [I, matches all three logs]
5. **What I could not find is why a frame number gets past 379.** The advance code (`sub_000DB660`) keeps the number in range for small steps;
   it overshoots only if one call advances by about 280 frames or more (4.7 s of accumulated crowd time at 60 frames a second) or the
   state table is damaged. Section 5 lists the candidates in order and the one log line that tells them apart. It is not a freed model: the image
   is allocated at arena load, before the rollback reference, and freed only at match end.
6. **Fix, in order:** (a) a guard in the crowd's render entry that clamps the accumulated time and every frame number before they are used (new
   code in the existing `sub_000DB870_enter` hook, a few hundred 16-bit reads a frame; section 6.1); (b) one diagnostic run to name the cause
   (6.2); (c) invalidating EAGL's stream cache at a load, which is right for a different, milder inconsistency (6.3).
7. **Delay 9 with rollback on:** not an ordering bug. The delay is computed after every ping and every rollback flag has arrived; the other
   PC reported rollback off (section 7).

## 1. Who calls `sub_00119950` and what the block is (Q1)

### 1.1 The path [V]

| Level | Function | What it is |
|---|---|---|
| 1 | `sub_00077740` -> `sub_000796A0` | screen render; calls the render subsystem's list walk |
| 2 | `sub_0001BF60` (return `0x1BFA7`, `0x7973A`) | for each layer, follows the items' `+0x104` next links and calls vtable slot 1 of each item |
| 3 | `sub_000DB870` (the `CInstanceCrowd` slot 1) | if the byte `[0x2E3488]` (always 1, never written) is set: runs the crowd's frame update `sub_000DB730`, then tail-jumps to `sub_00115510` |
| 4 | `sub_00115510` (return `0x115525`) | clears the item's culled counter, calls `sub_00111040` with the model, the item and its instance count |
| 5 | `sub_00111040` / `sub_00111111` (return `0x11132D`) | per instance: culls, sets `[0x3A5758]` from the instance-state table, copies the per-instance constants into the model's constant blocks, then runs each draw through `sub_00114FE0` |
| 6 | `sub_00114FE0` (return `0x115022`) | the render-method interpreter: walks the command words of the draw's render method, calls the handler `[0x2E56F0 + opcode*4]` for each; the cursor is `[0x3A597C]`, the draw's parameter pairs (count, pointer) start at `[0x3A5978]` |
| 7 | `sub_00118030` (return `0x11805B`) | the stream opcode: `block = C[1]*[0x3A5758] + P[1] - 20`, stream `C[2]`; calls `sub_00119950(stream, block)` |
| 8 | `sub_00119950` (return `0x119990`) | EAGL's per-stream cache: reads `vb = [block]` and the stride `[block+8]`; if the cache (`0x3A6E20[stream]`, stride `0x3A6E60[stream]`) differs, stores them and calls `sub_0021BB10` |
| 9 | `sub_0021BB10` | `D3DDevice_SetStreamSource`: `[vb] += 0x80000` (internal reference), and releases the old buffer in the device's stream table (`0x236C18 + stream*12`) |

At the crash `edx` still holds the block (the callee has not touched it) and `edi` is `[block]`, so the first dereference of the new buffer's header faults.
The EAGL cache plays no part in the fault.

### 1.2 What the block is, from the model file [V]

`icrowd01.o` (RefPack 145,325 -> 245,712 bytes; `.data` 217,296 = 0x350D0 bytes at file offset 0x40; `.shstrtab` at `.data+0x350D0`, `.strtab`
12,144 bytes from `.data+0x35100`, then `.symtab` and `.rel.data`) holds: one `__Model` ("icrowd01", one material, name `Low_RezCrowd`), one
`__RM` (`icrowd01_0_RM = InstanceCrowd`) and **384 `__VB`** objects. A `__VB` is a descriptor `{ptr trailer, byte length, stride, 1, ptr data}`, data at
descriptor+20, then a sentinel and the trailer, a D3D vertex-buffer header `{Common = 1, Data, ...}` (`docs/research/ffny-studio-eagl.md`).
The render method's two stream entries are `{0x230, stream 0, stride 6}` and `{0, stream 1, stride 8}`: stream 0 steps by **0x230 bytes per
frame**, i.e. the 383 descriptors `__GPvb_8_0_icrowd01`, `_1`..`_382` at `.data+0x480 + 0x230*k` are consecutive position buffers, one per animation frame
(a vertex-animation flip-book); stream 1 is a single buffer (`__GPvb_9_0`, at `.data+0x34A50`, which is where frame 383 would point).
The descriptor's first word is a pointer to its trailer, which is why `[block]` is the "vertex buffer" for `sub_0021BB10`.

Where the other stored blocks sit around the grid (frame k -> first word, `.data` offset `0x480 + 0x230*k` unless noted):

| k | offset | block[0] | meaning |
|---|---|---|---|
| 0..382 | `0x480`..`0x34820` | pointer to the trailer | valid descriptors |
| 383 | `0x34A50` | pointer to the trailer | the stream-1 descriptor: a valid pointer, wrong data |
| 384 | `0x34C80` | `0x3F16A076` | float data: not a guest address |
| 385 | `0x34EB0` | `0x776F4C00` | `\0Low_RezCrowd` (crash A) |
| 386..407 | `0x350E0`..`0x37ED0` | ASCII | `.shstrtab` / `.strtab` text ("`\0.st`", "`1\0__`", "`__VB`", "`icro`", "`rowd`", ...) |
| 408.. | `0x38100`.. | small integers | `.symtab`, `.rel.data` |
| 428.. | past the image | next heap block | crash C is k = 454; text "crow" |

### 1.3 Who allocates and frees it [V]

* The image is read from `assets/icrowd.viv|%s.o` by `sub_000DBBA0` (`sub_001E46E0` = read and decompress into a heap block, `sub_0010B3E0` = dlopen +
  Resolve, which relocates it in place) when the arena's crowd is built at match load. The instance object (`sub_000DB230`: vtable `0x2B4760`,
  fields `+0x110..+0x144`) and its arrays (`sub_000DB2D0`: instance states `N*4` bytes, colours, UV offsets, matrices; item `sub_001153B0`) are
  heap blocks from the game's pool allocator, all zero-filled by `sub_000296B0`.
* They are freed by the destructor `sub_000DBF80` at the arena's destruction (match end), including `sub_0010B490` (unload from the EAGL list `0x3A52A8`).
* **Neither happens in a fight.** So the image is not freed and reused inside a fight, and a rollback never un-creates it (it exists at the
  rollback reference). Text at `block[0]` is the image's own tail or its neighbour, read because the index is wrong, not because the image moved.

### 1.4 The other render blocks

The foreground spectators and everything else are drawn by other model renderers (`sub_00112610`, `sub_00114B40`, `sub_00114C80`, `sub_00111BE0`), which set
`[0x3A5758]` from the model (`+0xC0`) or to 0. They share `sub_00118030`, so the same pointer rule applies, but the logs name `sub_00111040`. Venue models
(`V2BG_*`) also have stream entries with a non-zero first word (colour sets); the same failure would show a different text. Not seen.

## 2. What the frame number is and how it is advanced [V]

* `sub_000DB620` adds a float (the arena step's time times the game-speed scale, from `sub_00094B80`, call at `0x94DA4`) to the accumulator at `crowd+0x13C`.
* At draw time `sub_000DB870` -> `sub_000DB730` turns the accumulator into whole frames: `frames = round(acc * 60.0)` (constant at `0x296DDC`). For
  each instance the previous state byte is kept, the visibility test `sub_00019300` (distance below 50.0 at `[0x2E348C]`, threshold 25.0 at `[0x2E3490]`)
  updates it, and if it is set and `frames > 0`, `sub_000DB660(instance, frames, previous state)` runs. At the end, if `frames != 0`, the accumulator is zeroed.
* `sub_000DB660` (A = `crowd+0x134` = idle 240, L = `crowd+0x138` = excited 140, hi = A + L = 380, percent = `crowd+0x140`, default 50): if the previous state
  byte was 0 the frame is re-rolled (`rand() % L + A` or `rand() % A`). Otherwise `new = frame + frames`; it is stored as is while it stays in
  idle (below 240) or in excited (below 380); when it reaches a clip end it draws `rand() % 100`: not excited (50% or more) -> `new % 240`; excited -> `new`
  if below 380, else `new - 140`. Every branch is bounded **except the last one when `new >= 520`**, which stores `new - 140` unbounded.
* The initial frame (`sub_000DB2D0`) is `rand() % L + A` or `rand() % A`: in range. The state table is only written by these two functions.
* **Advance happens inside Render, not in the step.** The crowd's frame work (and its `rand()` draws) run once per rendered frame. Re-run steps never
  render, so a rollback neither re-advances nor silences them (they are not in the netplay sound-handler list). The array is heap state, rewound
  by a load like any other page.

Overshoot values for the observed frames: 385 and 454 need `new - 140 = frame`, i.e. `new = 525` and `594`, i.e. an advance of at least
`525 - 379 = 146` and in practice of 280 frames or more from an idle start, which is **4.7 s of accumulated time** (or damaged state). A normal render advances 1 to 3 frames.

## 3. What a rollback load leaves inconsistent (Q2)

| State | Verdict for this crash |
|---|---|
| EAGL's per-stream cache `0x3A6E20..0x3A6EA0` (restored) against the device's stream table `0x236C18` (not restored) | **Real but not this fault.** After a load the cache names the buffers bound at the save, the device holds the buffers of the last render. If the next draw's buffer equals the cache's, the bind is skipped and the GPU uses the device's buffer: a one-frame wrong picture, not a junk pointer. The cache is never read as a pointer on this path. |
| Device table entry naming a trailer in a freed or reused heap block | Possible after a load back past a model's creation or destruction (weapons are the only fight-time models: `sub_000BADB0` -> `sub_00026A80`). Releases write `old+8` and `old+0` there. Not the crowd: its trailers live in a never-freed image. |
| Objects made by a dynamic load (`sub_0010AC20` from `sub_0010B3E0`) | The 50 loads at updates 60 to 72 are `CAnimationBank` objects (4 symbols, fallback `sub_00022720`, pushed only by `sub_00021710`) for the foreground spectators (`CCrowdAnimation::Init`, `sub_000866D0` -> `sub_00021830`), with eight RefPack reads of 2.6 to 5.8 KB. They end up at the head of the EAGL list, which is the "loaded object" the crash report prints. They happen once, before the first save that could be loaded back to, and are not near the crash. Not the cause. |
| The foreground crowd (`crowd_reset`, `RECOMP_ONLINE_CROWD`) | A different object (`CCrowdControl`, 30 members, vtable `0x29EBE8`), drawn by other renderers. Not on this stack. |
| Shadows, particles, HUD text | HUD text allocates in render and is restored consistently (rollback-async-loads.md 3.2 item 5). None is on the stack. |
| Frees held for a load (contiguous, arena, XAPI) | Held until no save can reach them, so a restored model never points at freed D3D data. But note the effect of the bad frames in section 4: they make the XAPI free path free a **game-heap** address. |
| Pages left alone (`s_skip`), mixer-masked words | Six pages in the logs (frame queue, file requests, one held). Nothing about the crowd's arrays is known to be on them: **[U], and the cheapest thing to check** (6.2). |
| The CRT `rand()` state used by the crowd | Draws happen at render time and are not rewound (or silenced), so the two PCs draw a different crowd. Visual only; does not break the range. [I] |

A diagnostic bug found on the way: `rollback_crash_state` (netplay.c 2913) and `watched_loaded` (rollback.c 506) walk the EAGL list through `+4`, but the list is
`next at +0, previous at +4` (`sub_00109F80` insert: new->next = old head, old head->prev = new). So the crash report always prints only the newest
loaded object (one entry in all three logs), and `RECOMP_ROLLBACK_CHECK` only ever finds the head.

## 4. The `free va=0x80000000` precursor [V mechanism, I that it is this event]

* The line is printed by `xbox_HeapFree` for the first 8 frees that no tracked block matches. With `RECOMP_HEAP_RECLAIM` on (default) real D3D
  contiguous frees are tracked and silent; a `MmFreeContiguousMemory(0x80000000)` is not.
* `sub_0021DEE0` (resource destroy) frees `Data | 0x80000000`, then the header with tag `0x24800000` (the held XAPI free). It runs when a bind's release leaves
  `(Common & 0x78FFFF) == 0`. A zeroed word that was bound (`+0x80000`) and released (`-0x80000`) ends at 0 and takes that path with `Data = 0`, hence
  `0x80000000`.
* Logs: A: two frees 12 lines before the crash (the first of the whole session, after 298 rollbacks); B: eight printed frees (cap) from about 50 frames before; C: three just before. None of the
  other 2026-10-25 rollback logs has one. So frames at 383 and up (valid-looking pointers into zeroed data, such as frame 383's trailer or text pointing into the image) are drawn for one to many frames, and each bind/release writes `+0x80000`, `-0x80000` and the fence into
  arbitrary words of the image tail or the next block before the first non-pointer faults.
* Consequence for the fix: **when this event is seen, memory has already been touched.** The XAPI free of a game-heap header (`defjam_xapi_free_now` or the held list,
  `release_held_xapi`) would run at the end of the fight on such an address. The guard in 6.1 prevents all of it; cleaning up afterwards is not enough.

## 5. Best hypotheses for the bad frame number, with evidence

Both observed values (385, 454) are above 380 by 5 and 74; the log shows several frames in a row with a bad index in A and B. The candidates, most likely first:

1. **One huge advance (accumulator).** If the render of the crowd did not run for several seconds while the step kept adding time (or the accumulator
   or the speed scale was restored wrong), the first render after turns `acc*60` into a few hundred frames; every instance that reaches a clip end and rolls "excited" gets
   `new - 140` above 379. That gives exactly this picture: **many instances over, with spread** (385, 454, ...), the first in loop order faulting. What the logs do not
   show is a render gap (the picture continued at 38 to 60 fps) or a speed-scale glitch. Where it could come from in rollback: `crowd+0x13C` is plain heap state restored by a load, but the
   consumer (render) never runs in re-run steps, and the producer (`sub_00094B80` -> AddTime) does; the page of the crowd object may also be one of the masked or left-alone pages. [I]
2. **Damaged state table.** The 1.5 KB `InstanceStates` block shares pages with neighbours. A restore that mixes saved and live words (mixer-mask words, `s_skip` pages, or
   a page first written by the render after the save and then given its reference content) could leave entries from different frames, but all such values are individually
   in range. A value like 454 needs a foreign writer. The writer candidates are the D3D release writes in section 4 *once the first bad frame exists*: that makes this a
   *consequence* (a chain), not an origin. [I]
3. **Latent original bug.** `sub_000DB660` is unbounded for a large advance (section 2) and the third argument of the call is a stack slot the code fills from the previous state byte. A long
   enough pause in the original game would break it too; the original never pauses the crowd's render. The rollback beta makes long gaps possible (steps without renders). [I]

**The log line that decides it:** in `sub_000DB870_enter` (the existing hook in `src/recomp_manual.c:74`, which calls `defjam_test_crowd_update`), once a second and on any frame number outside `[0, A+L)`: the accumulator, `frames`, the item's
`InstanceStates` pointer and its `rollback_page_state`, `N`, A, L, percent, the largest frame number in the table, `N.updates`, `N.resim`, `N.rb_count`. Candidate 1 shows a large accumulator before the first bad frame; candidate 2 shows
a bad entry with a small accumulator and a page state of "left alone"; candidate 3 matches candidate 1 with an explained gap. One `RECOMP_WATCH_WRITE` on `states+2` of one
instance in a 15-minute two-copy soak (the test run crashed at step 1322) adds the writer.

## 6. Fixes (Q3)

### 6.1 Make the crowd unable to run off its model (recommended, low risk)

In `sub_000DB870_enter` (called from every caller, before the body; `g_ecx` is the crowd, `crowd+0x124` the states, `+0x11C` the count, `+0x13C` the accumulator):

* clamp the accumulator to at most 0.1 s (6 frames) and treat NaN as 0;
* for each of the `N` entries read the frame as a signed 16-bit value; if it is outside `[0, A+L)` (and `A+L <= 380`, a sanity check on the crowd's parameters), replace it by `frame mod A`
  (idle) and log it once with the counters of 5. A few hundred 16-bit reads per rendered frame.

This removes the overshoot at the source (a clamped advance cannot pass 379), repairs damaged entries, and so also stops the section 4 writes. It does not touch
rollback; it is a robustness fix for the game's own latent range bug. Risk: a clamped accumulator changes the crowd's pose in a long gap; the crowd is
already different on each PC (host `rand()`), so there is nothing to keep identical.

A second, outer guard: the stream opcode handler `sub_00118030` has no direct callers (it is reached through the opcode table), so `recomp_lookup_manual`
can replace it. Validate `block` and `[block]` (both in the heap, `0x80003000..0x82A0D000`, the vertex buffer's `Common` word with type bits 0) and, when invalid, skip the bind and set a
"skip the draw" flag. The draw would still run with the previous stream bound, so this only converts a crash into garbage for a frame; do it only as a net behind 6.1.

### 6.2 Settle the cause (one run)

The hook in section 5, plus `RECOMP_ROLLBACK_ALLOCLOG=1` so `rollback_page_state` and the block of the states can be named, in the two-copy test (it reproduced at step 1322). A
run that logs and clamps is also the proof for 6.1.

### 6.3 EAGL stream cache after a load (hygiene, not for this crash)

Zero `0x3A6E20..0x3A6EA0` (stream buffers and strides) at the end of `rollback_load` (or in `rollback_to` right after it) so the next draw always calls `sub_0021BB10`. Effect: the
bind is repeated, the device's old entry is released correctly (the device table is self-consistent), the "wrong buffer drawn for a frame" case disappears. Risks: one extra bind per stream per
frame after a load (negligible); the dirty byte `0x3A61F8` is not touched. Do not also zero `0x3A5030/0x3A5050` (texture bindings) without checking the same release rule.

### 6.4 What not to do

* Holding more frees or keeping a structure live does not help: the image is never freed in a fight (1.3).
* Restoring the D3D device or its stream table is excluded for the reasons in rollback-state.md section 2.

## 7. Input delay 9 with rollback on, in the player's matches (Q4) [V]

Order of events in `src/hooks/netplay.c`:

1. START (`~1255`): `N.delay = LOBBY_DELAY (3)`, every `peer_rtt` -1 and every `peer_rb` 0, then **this PC's own `peer_rb[pos]` = `rollback_wanted()`** (the local setting `[online] rollback`, read once at start) and its own ping.
2. `send_rtt` (every 100 ms until the match is seen, line `~1596`) sends u16 ping + u8 `peer_rb[pos]` + u32 file fingerprint.
3. `on_data` P_RTT (`~826`): stores `peer_rtt[from]` and `peer_rb[from] = (len >= 8 && p[7] == 1)` **in the same handler**, only for the current START seed.
4. `netplay_sync_point` (`~2082`): waits (up to 5 s) until every `peer_rtt` is known, **then** `N.delay = agreed_delay()`, `delay_fixed = 1`, one more `send_rtt`. The delay is therefore computed after both
   the pings and the rollback flags arrived; there is no ordering window. The log line `input delay 9 steps (pings 109/125/-1/-1 ms)` has no "some missing", so both had arrived.
5. `agreed_delay`: `((109+125)/2 * 3 + 49) / 50 + 1 = 9`; the 4-step discount (`RB_LESS`) applies only if **every** `peer_rb[i]` is set. 9 means the other PC's flag was 0: its `[online] rollback` is off (the default),
   or its build is older than 17 (7-byte P_RTT).

This is correct behaviour for a mixed match, but two things are worth knowing:

* **Rollback is not "only when every player has it on."** `N.rb_on` is set from the local `rollback_wanted()` alone (netplay.c `2629`, `2670`); `peer_rb` only changes the delay. The comment at
  rollback.c 174-178 says otherwise. A PC with rollback on against one with it off predicts and rolls back with the delay (9) the lockstep side needs, i.e. all the rollback work with none of the latency win. The three reports
  are the same account's matches, so likely the same opponent.
* The crash stays the same either way: the crowd fault does not depend on the delay. A mixed match is not a precondition (test run B was two rollback copies).

## 8. Address and file index

| Item | Where |
|---|---|
| stream opcode / cache / SetStreamSource | `sub_00118030`, `sub_00119950` (cache `0x3A6E20`, strides `0x3A6E60`), `sub_0021BB10` (device stream table `0x236C10/0x236C18`, 12 bytes per stream) |
| render-method interpreter | `sub_00114FE0` (opcode table `0x2E56F0`, state `0x3A5974/78/7C`), instance renderer `sub_00111040`/`sub_00111111`, `[0x3A5758]` set at `0x1111FE` |
| crowd class | vtable `0x2B4760` (slot 0 `sub_000DC190`, slot 1 `sub_000DB870`), ctor `sub_000DB230`, setup `sub_000DB2D0`, advance `sub_000DB660`, frame update `sub_000DB730`, matrices `sub_000DB8C0`, loader `sub_000DBBA0`, destructor `sub_000DBF80`, AddTime `sub_000DB620` (caller `sub_00094B80` at `0x94DA4`) |
| crowd fields | `+0x110` model loader, `+0x114` Model, `+0x118` XML text, `+0x11C` N, `+0x120` item, `+0x124` states, `+0x128` bones, `+0x12C` colours, `+0x130` UV offsets, `+0x134` A (idle), `+0x138` L (excited), `+0x13C` accumulator, `+0x140` percent, `+0x144` locator loader |
| constants | `0x2E3488` crowd enable (1), `0x2E348C` 50.0, `0x2E3490` 25.0, `0x296DDC` 60.0 |
| model | `assets/icrowd.viv` `icrowd01.o` (245,712 bytes, `.data` 0x350D0, stream-0 grid `.data + 0x480 + 0x230*k`, k = 0..382), `icrowd01.xml`: idle 240, excited 140 |
| rollback | `src/hooks/rollback.c` (`rollback_load`, `release_held_xapi`), `src/hooks/netplay.c` (`rollback_to` `~2936`, `rollback_crash_state` `~2893`, `agreed_delay` `~768`, `netplay_sync_point` `~2077`), `src/recomp_manual.c:74` (`sub_000DB870_enter`) |
