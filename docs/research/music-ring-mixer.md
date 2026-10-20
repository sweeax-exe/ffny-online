# Music ring mixer: who refills the 6-channel looping ring, and how it can be overrun

Scope: read-only study of the lifted C (`src/recomp/gen/recomp_0000/0008/0009/0011/0012/0013/0016/0017.c`) plus the XBE
image (for vtable and constant words) and a skim of the toolkit APU. Nothing was built or run. Addresses are guest addresses.
"verified" = read in the lifted code or XBE; "inferred" = reasoned from shapes, the toolkit, or the run-time facts supplied with the task.

## 0. Headline (read this first)

1. **The thread whose stack was quoted (`sub_000D1340 -> sub_0019AB60 -> sub_000D1270 -> sub_001ED1A0 -> sub_001F5EE5`) is not the
   ring refiller by the code.** It is a generic command-mailbox dispatcher (verified, section 1). The code that reads the DirectSound
   cursor and refills the 6-channel 2400-frame ring is **`sub_001242D0` (thread) -> `sub_001241A0` (one refill)**, a TIME_CRITICAL
   thread on a drift-free 100 Hz schedule (verified). Its sleep goes `KeDelayExecutionThread <- sub_001F5EE5 <- sub_001F6083 <- sub_001242D0`,
   so a sleeping refiller would show `sub_001F6083`, not `sub_001ED1A0/sub_000D1270`. The 100 Hz / 9-10 ms / TIME_CRITICAL measurements
   match `sub_001242D0` exactly and do not match a `Sleep(5)` poller. Please re-check which thread the stack sample was taken from
   (`scripts/native-stacks.py`, or `RECOMP_WATCH_EXEC=0x1241A0` and log the host thread id).
2. **The game asks DirectSound for the WRITE cursor only** (play-cursor pointer is NULL), then fills up to `write cursor + 960 frames`
   (20 ms) rounded down to 16 frames. That is "cursor read -> fill to cursor + lead", not a fixed chunk and not a budget.
3. **The reader overtakes the writer when the interval between two cursor reads (plus the time for fresh data to land at the old
   write position) exceeds about 16 ms in the worst burst phase** (about 21 ms in the best), versus about 19.7 ms on real hardware.
   The emulated APU's 256-sample bursts plus 64-sample resampler look-ahead eat roughly 4.5 ms of a margin the game believes it has.
4. The cheapest fixes need no game code change: smooth the APU (32-sample frames paced), and/or report a write cursor that includes the
   emulator's real commitment window, and/or poke the one lead word `[0x3A9660]` from 960 to ~1344 after init (section 5).

## 1. Q1: which function is the mixer loop?

### 1a. The quoted stack: command dispatcher, not the mixer (verified)

| Function | What it is |
|---|---|
| `sub_001ED080` | thread start trampoline, passed to `sub_001F68D2` (CreateThread) by `sub_001ED4B0` (`PUSH32(esp, 0x1ED080)`) |
| `sub_000D1340(this)` | thread entry of a small `MyThread` wrapper class (vtable `0x2B04A8`, class name string at `0x2B04CC`). Loops `Sleep(1)` (`sub_001ED1A0(1)`) until byte `[this+0x24]` is set, then calls the stored callback `MEM32(this+8)` with `this`. |
| `sub_000D12F0` | vtable[4] "start(fn)": stores `fn` at `this+8`, calls `sub_001ED4B0(this+0x14, 0xD1340, this, ..., prio=[this+0x20])` |
| `sub_000D1270` | vtable[6] "Sleep(ms)": `sub_001ED1A0(ms)`, which is `sub_001F5EE5(ms, 1)` = SleepEx(ms, alertable) |
| `sub_0019B0C0` | creates the object, calls vtable+0x1C (`sub_000D1280`) with **-2**, then vtable+0x10 with callback **`0x19AB60`** (`PUSH32(esp, 0x19AB60)` at 0x19B17B) |
| `sub_0019AB60` | the callback (below) |

`sub_001ED2D0` maps an abstract priority `p` to a host priority with index `p+3`: 6 -> 15 (TIME_CRITICAL), 5 -> +2, 4 -> +1, 3 -> normal,
2 -> -1, 1 -> -2, 0 -> -15. The 19AB60 thread asks for `p = -2` (index 1), i.e. host LOWEST (verified; whether the host layer remaps it is not checked).

`sub_0019AB60` one iteration (verified):

```
while (!byte[0x3B808C]) {                       // quit flag
    if (MEM32[0x3B8090]==0 && MEM32[0x2EB73C]!=0) {   // pending message
        ((vt(0x3B8088))->+0xC)();                       // lock / dequeue
        memset(0x2EBE90, 0, 0x23*4);                    // 35-dword command block
        switch (MEM32[0x2EB73C]->+8 - 1) { 31 cases -> sub_001A0640, sub_0019FC70, sub_0019FD00, ... }
        if (MEM32[0x2EBE94]) MEM32[0x3B8090] = 0x2EBE90;
        ((vt(0x3B8088))->+0x10)();                      // unlock
    }
    ((vt(0x3B8084))->+0x18)(5);                         // PUSH32(esp,5): Sleep(5) via sub_000D1270
}
```

A BFS over **direct** calls from `sub_0019AB60` reaches 263 functions and **none** of the DirectSound wrappers (`sub_00259423` GetCurrentPosition,
`sub_0025BB3D` SetBufferData, `sub_00259323` Play) or `sub_001293F0/sub_001241A0`. Indirect calls (vtables) are not covered by that search, so this is
strong evidence, not proof (inferred). It looks like a script/event or file-service thread.

### 1b. The actual ring refiller (verified)

* Ring creation, `sub_00123F60` (called from `sub_00124610` at 0x124797, which then starts the thread): one DirectSound buffer, 6 channels,
  16-bit, looping: `SetBufferData(buf, mem, ring*12)`, `SetCurrentPosition(buf, 0)`, zero fill, `Play(buf, 0, 0, 1)` (flag 1 = looping), calls at
  0x1240F7 / 0x124103 / 0x124133. Globals: buffer `[0x3A962C]`, ring memory `[0x3A9644]`, ring frames `[0x3A965C]`, lead `[0x3A9660]`, last write `[0x3A9664]`.
* Thread start (0x12479C-0x1247DB and `sub_001243A0`): `CreateThread(0, 0x8000, 0x1242D0, 0, 0, &id)` via `sub_001F68D2`, then
  `sub_001F6433(handle, 0xF)` = `SetThreadPriority(15)` = **TIME_CRITICAL**; `[0x3A95DD]=1` run flag; `[0x3A95D8] = KeTickCount` (first deadline).
* `sub_001242D0`, one iteration (verified):

```
ICALL [0x2E61C0]            // engine lock (see 1c)
ICALL [0x3A9594] if set     // user hook
sub_0011FAF0                // per-tick voice/service update: sub_00124F30 -> voice groups -> sub_001293F0 (hardware streams),
                            //   per-type hooks [0x3A94C0+i*4]
float time accumulate [0x3A95E0] -> [0x3A9324]; sub_0011FAB0
sub_001241A0                // <<< the ring refill (GetCurrentPosition + mix)
sub_00126300                // dispatch queued position events
ICALL [0x3A9598] if set; ICALL [0x2E61C4]   // unlock
sleep (section 4)
```

* `sub_001241A0` (one refill) calls `sub_001291D0(&dest, n)` for up to two segments (before and after the wrap). `sub_001291D0` walks **512-frame chunks**
  (`cmp ebx, 0x200`): per chunk `sub_00128F30(ptrs, n)` (mix active voices into planar buffers; inferred from shape) then `sub_0012BFE0`
  (convert and interleave to the ring using a 6-entry channel map `{4,1,0,5,3,2}` at 0x12927D, verified), advancing the destination by `ch*n` samples.
  This path is taken when byte `[0x3A9A36] != 0`; when it is 0 the code at 0x1292D9 produces planar output without interleaving (inferred: interleaved
  ring is the `!= 0` case, matching the run-time observation).

### 1c. Engine lock (relevant to late refills; verified code, inferred impact)

`sub_00121CA0`, called once at game init (`sub_0001D880`, call at 0x1DA54), sets the hookable pair `[0x2E61C0]/[0x2E61C4]` to
`0x121C80/0x121C90` -> `sub_00139A20/30` = `WaitForSingleObject(MEM32[0x3AE10C], INFINITE)` / `ReleaseMutex`. The default pair is
`0x11FCC0/0x11FCD0` -> mutex `[0x3A9A04]`. So the refiller holds a **game-wide mutex for its whole tick**, and takes it **before** it reads the cursor.
`sub_00139A20` has 46 direct callers (0x34BC0..0x43AA0, 0x11C3B0, 0x11C490, ...) and `sub_0011FCE0` (the public sound-API lock) 59 callers,
including the movie clock `sub_0013B9A0` and the stream feeder `sub_0011F4A0`. A long hold on another thread delays the cursor read although the
sleep returned on time.

## 2. Q2: how it learns the play position

`sub_001241A0` (verified):

```
PUSH32(esp, eax /* &local */); PUSH32(esp, 0); PUSH32(esp, ecx /* [0x3A962C] */);
RECOMP_ABI_CALL(0x00259423u, sub_00259423);    // IDirectSoundBuffer::GetCurrentPosition(this, pPlay=NULL, pWrite=&local)
```

`sub_00259423(this, pPlay, pWrite)` (stdcall, `ret 12`; `this-0x1C` adjust) -> `sub_00257F0E` -> `sub_0026415B(pPlay, pWrite)`. **pPlay is NULL, pWrite is the only output.**
(`sub_001293F0`, the hardware stream path at 0x129428, is the opposite: pPlay set, pWrite NULL, 36-byte units; `sub_00125870` also asks for play only.)

`sub_0026415B` (verified): if `(this+0x12 & 3) == 3` (playing):

```
play  = sub_0026302E(this)                         // CBO of the voice, see below
write = play + min( sub_002605CD(this, 0x20) /* 32 frames in bytes = 32 * [fmt+0x14] */, [[this+0x80]+0x14] )
write %= [[this+0x80]+0xC0]                        // buffer length in bytes (non-looping-region branch)
```

so the **write cursor is the play position plus 32 frames (0.67 ms)** (the second min operand `[[this+0x80]+0x14]` is not resolved; if it is below
384 bytes the margin is smaller; inferred that it is not). Not playing: both outputs = `[this+0x148]`.

`sub_0026302E(this)` (verified in `docs/research/movie-audio-clock.md` section 3, re-checked here only at the call level): reads the voice array `[0x27CCB0]`
(stride 0x80) entry of the voice selected by byte `this+0x64`, spins on `MMIO 0xFE820010`, locks via `0xFE8202F8/0xFE8202FC`, returns
`voice[+0x58] & 0xFFFFFF` (**CBO**, converted to bytes by `sub_002605CD`). Returns 0 while `voice[+0x54]` bit 20 (NEW_VOICE) is set. **Only one of the three voices
(0xF8/0xF9/0xFA) is read; the game assumes the other two sit at the same frame** (inferred from the single-voice read; the three are 32-sample frames apart at most).

Game-side math in `sub_001241A0` (verified):

```
x      = write cursor in bytes
frames = (x * 0xAAAAAAAB >> 35) & 0xFFFFF0         // x/12, i.e. 6 ch * 2 bytes, rounded DOWN to 16 frames
target = (frames + [0x3A9660]) & 0xFFFF0           // lead added, rounded to 16
if (target >= [0x3A965C]) target -= [0x3A965C]     // ring wrap, once
```

Constants set in `sub_00123F60` (rate `R = MEM16(0x3A936A)`, default `0xBB80` = 48000, stored by `sub_00124410` at its `MEM16(0x3A936A) = 0xBB80`):

| Word | Value | Source |
|---|---|---|
| `[0x3A9660]` lead | `R*20/1000` = **960 frames (20 ms)** | `5R<<2`, `*0x10624DD3 >> 38` (=/1000), stored at 0x124026 |
| `[0x3A965C]` ring | `(R*50/1000) & 0xFFFF0` = **2400 frames (50 ms)** = `0x960` | `*0x32`, `/1000`, `& 0xFFFF0` |
| bytes per frame | 12 (6 x 16-bit) | `/12` above, buffer size `esi = ring*12` = **28800 bytes** (not 14400) |
| `[0x3A9664]` last write | 0 at start | `MEM32(0x3A9664) = edi` |
| alignment | 16 frames | masks `0xFFFFF0`, `0xFFFF0` |

So: yes, a "safety margin" exists: the game fills **20 ms beyond the DirectSound write cursor**, and the write cursor is itself play + 32 frames.

## 3. Q3: how much per iteration

Exactly the gap between its own last write position and `target` (verified, `sub_001241A0`):

```
if (target < lastwrite) { seg0 = ring - lastwrite; seg1 = target; }    // wrapped
else                    { seg0 = target - lastwrite; seg1 = 0; }
for each seg > 0: sub_001291D0(&(ringbase + lastwrite*12), seg); lastwrite = 0 after seg0;
[0x3A9664] = target
```

* Not a fixed chunk (the 480-frame figure in the question is only the nominal 10 ms average of this gap) and no decode budget. The only internal chunking is
  512 frames inside `sub_001291D0`. With the sleep at 10 ms the normal fill is 480 frames (+/- 16); after a 16 ms interval 768.
* If `target == lastwrite` nothing is written. If the reader stalls the writer by more than a lap the compare `target < lastwrite` cannot tell a full lap from
  zero advance, so a stall >= ring (50 ms) is silently under-filled (inferred from the code shape, not observed).
* Data is written from the old write position upward, so the part the reader needs first lands first; but it lands only after the lock wait, `sub_0011FAF0`,
  `sub_0011FAB0` and the mix of the first 512-frame chunk (inferred ordering from the code above).

## 4. Q4: the sleep interval (verified)

After the tick (0x12435A-0x124381):

The pacing step, in words: it reads the kernel tick count (`KeTickCount`, import ordinal 156, read
through the import slot at 0x2852C8), takes the deadline stored at 0x3A95D8, computes the signed
wait as deadline minus now, advances the stored deadline by 10 ticks, clamps a negative wait to 1,
and sleeps that long (`SleepEx(wait, FALSE)`, which reaches `KeDelayExecutionThread`).

* A fixed 100 Hz schedule from the first `KeTickCount`, no drift, catch-up by 1 ms sleeps after a stall. The request is `deadline - now` after the work of the tick, so
  work time is subtracted and any late wake shortens the next request. The requested value is therefore 1..10 ms, **mean exactly 10 ms**; intervals above 10 ms in your
  measurement come from late wakes (host Sleep granularity or scheduler), and the following request is correspondingly short (the "3" end of 3..16).
* Host side: `KeTickCount` is rewritten by a host thread from the performance counter in 1 ms resolution (`xbox_memory_layout.c:1772`, `kernel_hal.c:453`), so the
  game sees a clean 1 ms clock.
* Important: the interval that matters is between **cursor reads**, not between wake-ups: `Delta_read = Delta_wake + a_k - a_(k-1)`, where `a_k` is the time from wake to the
  `GetCurrentPosition` call (mutex wait, `sub_0011FAF0` voice service/decode, `sub_0011FAB0`). That term is not in the "wakes on time" measurement (inferred).

## 5. Q5: when the reader overtakes the writer, and what to change

### 5a. What the game assumes (verified code, inferred reading)

* It uses the **write-cursor** meaning of GetCurrentPosition. It never reads the play cursor of this ring. It treats `write cursor` as "the first position the hardware is already
  committed to; nothing before it can be changed", and fills `20 ms` past it. On Xbox hardware the APU processes one 32-sample frame every 0.667 ms, so the committed window beyond the
  reported play position is about 32 frames, matching the 32-frame margin in `sub_0026415B`. The game's unspoken contract: **between two of its cursor reads the hardware reads at most
  `lead + margin - small` frames past the first read's cursor.**
* Slack right after a fill: `W = floor16(floor(x/12)) + 960`, with `x` including the 32-frame margin: `W - CBO` in **977..992** frames.

### 5b. Hardware vs the emulated APU

| | Real APU | Emulated APU (facts from the task) |
|---|---|---|
| step | 32 frames every 0.667 ms | 8 x 32 = 256 frames back to back every 5.333 ms |
| furthest read past published CBO | ~32-64 | 256 (next burst) + 64 (resampler) = **320** |
| tolerated time from cursor read to new data landing | (977 - 64) / 48 = **~19 ms** | worst phase **16.0 ms** (3 bursts), best phase 21.3 ms (4 bursts) |

Derivation (emulated): read just before a burst, furthest read after burst j is `CBO + 256*(j+1) + 64`; stale when that exceeds `CBO + 977`, i.e. j = 3 at 16.0 ms; read
just after a burst gives j = 4 at 21.3 ms. The stale stretch is `(256*(j+1)+64) - 977` = up to ~110 frames (~2 ms), one audible tick. Measured: wake intervals reach 16 ms and the cursor
read adds `a_k - a_(k-1)` on top, so the 16 ms line is crossed in the worst phase about once a second. (Inferred; the arithmetic is from the verified constants.)

After the miss the writer does not notice: the next `target` is computed from the new cursor, and `[old W, target)` is rewritten (the region already played stale is simply overwritten).

### 5c. Emulator-side changes that need no game edit (ranked)

1. **Smooth APU consumption.** Process one 32-sample frame per 0.667 ms instead of 8 back to back. Commitment window drops from 320 to ~96 frames; tolerance becomes `(977 - 96)/48`
   ~ 18.4 ms in every phase, 2+ ms above the observed 16 ms tail. This restores the hardware contract and also helps the movie/hardware-stream paths that read the true play cursor.
   (Same direction as `audio-quality-survey.md` item 4.)
2. **Report the write cursor honestly.** If bursts must stay, make the value the game derives equal "play + real commitment window": add ~288 frames (256 burst + 64 resampler - 32
   already added by `sub_0026415B`) to what is reported for the **ring voices 0xF8-0xFA only** (CBO word or a hook on `sub_0026415B`'s write output). Worst-case tolerance becomes 21.3 ms.
   **Do not bias the play cursor of other voices**: the hardware-stream path `sub_001293F0` writes `[last_write, play)`, so a play cursor that is ahead of the real one would overwrite
   audio that has not been played yet.
3. **Raise the lead word** `[0x3A9660]` from 960 to ~1344 (28 ms; must stay a multiple of 16 and well under 2400 minus jitter) with a one-word poke after `sub_00123F60` returns. Tolerance +8 ms
   (worst case 24 ms), cost +8 ms output latency for everything on the software mixer. Simple and fully local, but it is a data patch of game state.
4. **Reduce `a_k`.** Keep the lock wait and decode work out of the path: `timeBeginPeriod(1)` and host priority for the refiller thread are secondary; the real lever is not holding the
   game-wide mutex `[0x3AE10C]` on other threads across long operations (needs measurement, section 6).

## 6. What to measure next

* Per tick: time from `KeDelayExecutionThread` return to entry of `sub_001241A0`, and from there to the first store into `[0x3A9644] + lastwrite*12` (the "landing"). Log `Delta_read` and
  the CBO read/value; compare with the stale events. If `a_k` is mostly < 1 ms the cause is the burst model; if it spikes, the mutex or `sub_0011FAF0` decode.
* Watch `RECOMP_WATCH_EXEC=0x1241A0,0x1291D0` hit times and `MEM32(0x3A9664)` (last write), `MEM32(0x3A9660)` (should read 960), `MEM32(0x3A965C)` (2400).
* Confirm by `scripts/native-stacks.py` that the sleeping TIME_CRITICAL thread has `sub_001F6083 <- sub_001242D0` and which thread has `sub_000D1270`.

## 7. Claim ledger

| Claim | Status |
|---|---|
| `sub_000D1340` is a generic thread wrapper; `sub_0019AB60` is a mailbox dispatcher with a `Sleep(5)` per iteration | verified |
| `sub_0019AB60` has no direct-call path to GetCurrentPosition/SetBufferData/Play | verified (direct calls only) |
| The 19AB60 thread requests abstract priority -2 which `sub_001ED2D0` maps to host LOWEST | verified (host remap unchecked) |
| Ring refiller is `sub_001242D0` -> `sub_001241A0`, TIME_CRITICAL (`SetThreadPriority(h, 15)`) | verified |
| GetCurrentPosition is called with pPlay = NULL and pWrite = &local | verified |
| write cursor = play + min(32 frames, `[[this+0x80]+0x14]`) | verified; second operand unresolved |
| lead 960 frames, ring 2400 frames, 12 bytes/frame, alignment 16, `[0x3A9664]` last write | verified (R = 48000 default) |
| fill = `target - lastwrite`, no cap, 512-frame inner chunks | verified |
| sleep = max(1, deadline - KeTickCount), deadline += 10 | verified |
| engine mutex `[0x3AE10C]` held for the whole tick, taken before the cursor read | verified code; impact on refill lateness inferred |
| 16 ms worst-case tolerance with 256+64 commitment window | inferred (arithmetic on verified constants and supplied APU facts) |
| Only one of three voices' CBO is read | inferred (single `this+0x64` selection) |
| `[0x3A9A36] != 0` selects the interleaved ring path | inferred |
