# Rollback and save states: where the fight's simulation state lives, and what a snapshot must leave alone

Read-only study. Nothing was edited, built or run. Sources: the lifted C in `src/recomp/gen`, the toolkit in
`tools/xboxrecomp/src/kernel`, the hooks in `src/hooks`, the logs `logs/run-20261022-111120.log.err`,
`logs/run-20261020-220838.log.err`, `logs/mcp-memdiff.txt`, `logs/mcp/copy0.log.err`, and the earlier notes in this directory.
Static reach numbers (functions reachable from a root, written `.data` addresses) come from a small script over `recomp_00*.c`
(direct `RECOMP_ABI_CALL`s and tail jumps, plus the fighter handler table built by `sub_001B54D0`); **indirect calls through
vtables are not followed**, so "not reachable" never proves absence.

Tags: **[V]** read in code, data or a log; **[I]** inferred (reason given); **[U]** not settled (how to settle it is given).
Addresses are guest addresses unless marked "host".

## 0. Short answer

1. **The game's own heap is not in the 64 MB main RAM. It is one ~42 MiB block in the separate 64 MB "contiguous window" at guest
   `0x80000000`.** At start-up `sub_0001A890` asks `MmQueryStatistics` for the available pages, subtracts a fixed 14 MB and takes
   the rest with one `MmAllocateContiguousMemoryEx` (44,081,152 bytes = 0x2A0A000); the game's pool allocator lives inside it
   (section 1.3). Everything the game `new`s during a fight (crowd members, record tables, the world/effects object, the
   `main_all.dat` block, models, animation buffers) is in that block. The static `.data/.bss` (992 KiB) holds the fighter
   records, the match globals, the generators and the arena object, but also the **pointers into the heap**.
2. **A restore of `.data/.bss` alone cannot work.** The existing groundwork (`src/hooks/test_telemetry.c:283-330`) saves
   `0x2D5000..0x3CD000` (or, with `RECOMP_TEST_SNAP=full`, main RAM `0x10000..0x4000000` minus `0x700000..0xF80000`); neither
   touches `0x80000000..`. The one recorded load (`logs/mcp/copy0.log.err`, saved at step 203, loaded about 500 frames later)
   dies in the very next step: a vtable slot read gives `0x002B5850`, then a float-looking garbage pointer `0xBEA2DE1B` is
   dereferenced (section 5). `peek`, `poke` and `memdiff` are capped at `0x04000000` (`src/hooks/test_control.c:230,278,289`), so the
   "state lives almost entirely in .data/.bss" statement was measured with a tool that cannot see the heap.
3. **The heap is self-contained.** The pool allocator keeps all its metadata inside the block and in `.data`
   (pool table `0x3C9158[64]`, hooks `0x3C9138..0x3C914C`); its locks are host shadows keyed by address
   (`kernel_rtl.c:165-260`), so guest lock bytes carry no state. Snapshot the heap block and the pool globals **together**
   and the allocator comes back consistent. That also means the fight step allocates and frees from it (sections 1.3, 6).
4. **Do not use the whole 64 MB, and do not use "everything that is not a known host region" either.** Per step the sensible
   choices are: heap block + the sim part of `.data/.bss` (about 43 MiB, section 4), or, once measured, only the pages that changed.
5. **`GetWriteWatch` cannot be used**: both guest mappings are pagefile-backed file-mapping views (`MapViewOfFileEx`), not
   `VirtualAlloc(MEM_WRITE_WATCH)` memory [I]. Dirty pages need `VirtualProtect` + the existing vectored handler, or hashing.
6. First experiment: section 7.

## 1. What holds the simulation state

### 1.1 Guest address space as the runtime builds it

Two big storage areas plus device apertures. Host base of guest page 0 is `0x10000000` in the logged run
(`xbox_MemoryLayoutInit: mapped 65536 KB at 0x0000000010000000 (offset +268435456 ...)`, `logs/run-20261022-111120.log.err:10`);
`MEM32(a)` is `*(uint32_t*)((uint32_t)a + g_xbox_mem_offset)` (`recomp_types.h:425-430`).

| Guest range | What | Storage | Evidence |
|---|---|---|---|
| `0x00004000` | fake TIB (main thread) | main RAM | `xbox_memory_layout.h` `XBOX_TIB_MAIN`; log line 14 [V] |
| `0x00010000..0x004017B8` | XBE image: 12 sections (table below) | main RAM, file-mapping view | log lines 12-24 [V] |
| `0x00700000` / `0x00740000` / `0x00760000` / `0x00770000` | RW data, kernel data exports (**KeTickCount at `0x740020`**), TLS, TLS block | main RAM | log line 15; `xbox_memory_layout.h` `KDATA_TICK_COUNT` [V] |
| `0x00780000..0x00F7FFF0` | main thread stack (8 MiB; worker slices of 256 KiB carved from the low end: `0x7BFFF0` is slice 0) | main RAM | `xbox_memory_layout.h` `XBOX_STACK_*`, `XBOX_WORKER_STACK_*`; memdiff shows pages `0x7BF000`, `0xF7F000` [V] |
| `0x00F80000..0x04000000` | **host bump heap** (`xbox_HeapAlloc`): spawned-thread stacks (512 KB each, `0x01082000`, `0x01103000`, ...), TIBs, kernel-object stand-ins, DPC/USB objects | main RAM | `xbox_memory_layout.c:3582,3995-4070`; log lines 531-551 [V] |
| `0x80000000..0x84000000` | **contiguous window**, 64 MB, *separate* file mapping (not an alias of main RAM) | own mapping, host `0x90000000` | `xbox_memory_layout.c:3017-3033` (comment: "Deliberately NOT a view of the 64 MB RAM mapping") [V] |
| `0xF0000000..` | tiled aperture, alias of the contiguous window | second view of the same mapping | `xbox_memory_layout.c:3370-3400` [V] |
| `0xFD000000` NV2A (16 MB), `0xFE800000` MCPX/APU/AC97/USB (8 MB), `0xFF000000` flash | device models | `VirtualAlloc` / trapped | `xbox_memory_layout.c:3062,3107,3198` [V] |
| `0x83F00000` | scrap used by `postcmd.c` | contiguous window | `src/hooks/postcmd.c:36` [V] |

XBE sections (log lines 12-24; sizes are `vsize`) [V]:

| Section | VA range | Size | Role |
|---|---|---|---|
| `.text` | `0x011000..0x210DF4` | 2,096,628 | code (constant) |
| XGRPH | `0x210E00..0x213991` | 11,153 | XDK graphics lib (code + data) |
| D3D | `0x2139A0..0x236F60` | 144,832 | Direct3D: code, **device struct at `0x234770`, device pointer `0x234768`** (`d3d-pushbuffer-wrap.md:4-7`) |
| D3DX | `0x236F60..0x2569F8` | 129,688 | D3DX |
| DSOUND | `0x256A00..0x27CD84` | 156,548 | DirectSound |
| XPP | `0x27CDA0..0x285238` | 33,944 | USB / XInput |
| `.rdata` | `0x285240..0x2D54C8` | 328,328 | constants, kernel thunk table at `0x285240` (host-filled) |
| **`.data` (+bss)** | **`0x2D54E0..0x3CD584`** | **1,015,972 (992 KiB)**; 154,376 bytes initialised, rest BSS | the game's globals, the sim state |
| DOLBY | `0x3CD5A0..0x3D4720` | 29,056 | audio codec |
| `$$XTIMAGE`, DISCERROR_E/F | `0x3D4720..0x401798` | ~182 KB | constants |

`.data` and `.rdata` share a page (`0x2D5000`), which is why `SNAP_LO` is `0x2D5000`.

### 1.2 The sim state that is in `.data/.bss` [V unless noted]

| Range | What | Evidence |
|---|---|---|
| `0x3B92E0 + slot*0x12E8` (4 slots, ends `0x3BDE80`) | fighter records | `fighter-record-fields.md`; `crash-001B2480.md:41` |
| `0x3B8F28..0x3B9074` | match globals (phase `0x3B8F30`, scale `0x3B8F2C`, flags `0x3B9054`, clocks `0x3B9030/34`) | `simulation-tick.md` Q2/Q3 |
| `0x3B8F48 + slot*0x20` | pad table the step reads (netplay injects here) | `netplay.c:134,1925-1951` |
| `0x3B85D8..0x3B8F18` | parsed `main_all.dat` tables: **pointers into the heap block** | `crash-001B2480.md:195` |
| `0x3BF990 + slot*0xB8`, `0x3BFC70 + slot*0x88` | AI brains, per-fighter AI generators | `fight-determinism.md` Q1/Q2 |
| `0x3C1400..0x3C1488` | match generator | `fight-determinism.md` Q1.A |
| `0x39C0E0..0x39C168`, `0x376FDC`, `0x37705C` | utility generator, CLogic generator | `fight-determinism.md` Q1.B |
| `0x349760..>=0x3797A4` | the arena/CLogic object "A" (state word `+0x2D820`, crowd manager `+0x2B9F0` = `0x375150`, crowd generator `+0xE18` = `0x375F68`, restart counter `+0x30040`) | `crowd-lockstep-reset.md` section 1; `crash-001B2480.md:256` |
| `0x3494E0` / `0x3494E4` | crowd record count / **pointer to the record table (heap)** | `netplay.c:44`; `crowd-lockstep-reset.md` |
| `0x340D38..0x341018` | the game's own per-step state snapshot buffer (`sub_001A3D50(0x340D58)`) | `simulation-tick.md` Q1.7 |
| `0x33DB54` | small-block allocator object; `0x33DB5C` global generator (6 words) | `crash-001B2480.md:283`; `crowd-lockstep-reset.md` section 4 |
| `0x3C9138..0x3C9258` | pool allocator globals and the 64-entry pool table | `recomp_0013.c:14715-14719,14740,14835` |

Static reach from the fight step (`sub_00087700` plus the handler table): 824 functions; from the whole arena frame
(`sub_0008BE60`, `sub_00087700`, the crowd update `sub_00081170`): 2,135. Their literal-address writes into `.data` fall in about 70
clusters between `0x2DBC40` and `0x3CD580`, thickest at `0x3B8598..0x3B9318`, `0x3BE624..0x3BE7AC`, `0x3C03E0..0x3C053C`,
`0x3494E8..0x3495F4`. Fighter records, the arena object and the generators are reached through base+offset, so they do not appear
as literal addresses; the list above is by documentation, not by this scan.

### 1.3 The game heap: one block in the contiguous window [V chain, I for the base]

1. Start-up function `sub_00011E40` calls `sub_0001A890` (`recomp_0000.c:5254`, body `:28456-28548`). It computes
   `size = AvailPhys - 14680064` (`fild`, `fadd 2^32` for the unsigned fix, `fsub 14 MB`; constants `0x286384` = 4294967296.0 and
   `0x287368` = 14680064.0, read from `game/default.xbe`), calls `sub_001F4F8B(size, -1, 0, 4)` and hands the result to
   `sub_000298F0(base, size)`, which calls `sub_001EA1F0` = create the root pool over `[base, base+size)`
   (`recomp_0000.c:28524-28530`; `recomp_0001.c:10459`; `recomp_0013.c:16103-16128`).
2. `sub_001F4F8B` is a thin wrapper that calls the kernel thunk at `0x285260` (`recomp_0013.c:48543-48580`). The XBE import word at that slot is
   `0x800000A6` = ordinal 166 = `MmAllocateContiguousMemoryEx(size, low, high, align, prot)` (read from the XBE; `kernel_bridge.c:9583,9970`). With
   `phys = -1` it passes low 0, high 0xFFFFFFFF, align 0.
3. `bridge_MmAllocateContiguousMemoryEx` (`kernel_bridge.c:867-915`): `low == 0` so it is not the pinned-address case; it calls
   `xbox_ContiguousAlloc` (`xbox_memory_layout.c:3837`), a bump allocator that starts at `0x80001000` (`:3807`).
4. `MmQueryStatistics` (`kernel_memory.c:130-190`; the comment at `:146-150` names this title: "takes AvailablePages minus a fixed 14 MB
   reserve as its main arena") returned 14,346 pages at that moment (`run-20261022-111120.log.err:549`: image 4037 KB, heap 2052 KB, contiguous 12 KB).
   So `size = 14346*4096 - 14680064 = 44,081,152 = 0x2A0A000` (42.04 MiB). [V]
5. 12 KB were already allocated in the window, so the block starts at about `0x80003000` and ends at about `0x82A0D000` [I; the
   12 KB figure is `xbox_ContiguousLiveBytes`, the alignment is the allocator's]. The D3D push buffer then starts right behind it: `DMA_PUT` values cycle
   through `0x02A0E000..0x02A8Axxx` (the `[NV2A] DMA_PUT/GET` lines in every long log, e.g. `logs/mcp/copy0.log.err`; ring size `0x80000` per
   `d3d-pushbuffer-wrap.md:28`). [I, consistent]
6. Independent confirmations that heap objects have `0x80xxxxxx` addresses: `simulation-tick.md:96` (world/effects object at `0x805CD4B0`), `codeseg-corruption.md:118`
   ("`ecx = 0x80F2005B`, a normal-looking heap pointer"), `postcmd.c:36-41` ("the title's allocator ... starts at 0x80001000"), `watchpoint.c:205-210`
   (the two readable windows are `0x1000..0x4000000` and `0x80000000..0x84000000`). [V]

What is in the window outside the block (guest `0x82A0D000..0x84000000`, about 21.9 MiB): the push buffer (0x80000 bytes,
`sub_001F9DC6` tag `0xBC800000`, `d3d-pushbuffer-wrap.md:28`), the two display framebuffers (physical `0x02CE8000` and `0x02E14000`
from `PCRTC_START`, i.e. guest `0x82CE8000`, `0x82E14000`), depth/back buffers, the DirectSound arena (`[APU] DSP doorbell derived from GPSADDR: 0x831F8810`,
log line ~1140), the D3D `MmAllocateContiguousMemory` pages the title frees one by one (`[HEAP] free #1 va=0x8321F000`, `...83220000`, run
log 1966-3162), the GPU instance memory carved off the top (`XBOX_GPU_INSTANCE_DEFAULT` 128 KiB, `kernel.h:402`), and the first 12 KB (USB host-controller
structures: `HCCA=00001000` in the OHCI log, i.e. window offset `0x1000`; `xbox_memory_layout.c:3795-3806` comment). [V/I]

Without `RECOMP_HEAP_RECLAIM` (not set anywhere in this repo, grep) contiguous blocks are never reused, so the D3D part of the window only grows
(`xbox_memory_layout.c:3795-3797,3900-3905`) and the bump pointer `g_contig_next` is host state. [V]

### 1.4 Inside the block

* Root pool header at the block base, pools registered in `0x3C9158[tag & 0x3F]` (`recomp_0013.c:14021`); chunk header 16 bytes with a pool tag at
  `user-14` (`sub_001E9F90`, `recomp_0013.c:15690-15700`), magic words `0x424F`/`0x4246`/`0x4253` written by the pool code; free-list unlink at
  `recomp_0013.c:14690-14697`; pool object fields: first chunk `+8`, header size `+0x30`, lock flag `+0x38`, lock `+0x3C` [V shape, I names].
  Sub-heaps are carved from it by `sub_000293B0` (table of 4 at `0x2DBED0`; two 1.5 MiB heaps, names `0x2988FC`, `0x2A4640`:
  `recomp_0003.c:15590-15638`, `recomp_0004.c:58420-58463`). Small requests (<= 0x400) go to the small-block object at `0x33DB54`
  (`recomp_0001.c:9537-9570`). [V]
* The allocator's user-visible entry points are `sub_00029500` (alloc) / `sub_000294D0` (free), set as the file layer's callbacks by `sub_001E5860`
  (`crash-001B2480.md:283`). **The fight step reaches both**: alloc via `sub_001A4900 -> sub_001A94B0 -> sub_0008F990 -> sub_000388B0 -> sub_00029680 -> sub_00029500`,
  free via `sub_001A4A30 -> sub_001AB410 -> sub_000BBAA0 -> sub_000BA920 -> sub_000294D0` [V path, U frequency].
* Per-match objects known to be in it: the `main_all.dat` block (282,904 bytes, `MEM32(0x349760)`, read-only during a fight; `crash-001B2480.md:279`),
  the crowd's members (0x17C each, array at `mgr+0xA90`), the record table (`count * 0x34` at `*(0x3494E4)`), the crowd animation bank, the AI archive buffers
  (`ai.viv`, loaded by the same loader), the world/effects object, fighter models/animation/skeleton buffers [I; sizes U].
* The frame/pad record queue object (registry slot `0x27`, pointer kept at `0x3A017C`, ring of 10 x 0x260 records, head `+0x17C4`, tail `+0x17C8`, count `+0x17CC`,
  step counter `+0x1A5C`, event `+0x1A60`) also lives here and is written by the 60 Hz producer thread (`simulation-tick.md` Q1.3). **It is input, not state.**

### 1.5 Large static assets that never change during a fight

* In main RAM: `.text` (2,096,628), `.rdata` (328,328), the XTIMAGE/DISCERROR sections: ~2.5 MB, never write targets. A snapshot starting at `0x2D5000` already skips them. [V]
* In the heap block: the `main_all.dat` block, models, textures the title registers with D3D, motion data, AI archives. There is **no cheap static way to separate them**:
  the allocator has no "read-only" class. Two ways: (i) dirty-page tracking (section 4), (ii) walk the pools and classify chunks by pool tag + a one-time diff across a
  few fights; the walk is the routine at `sub_001EA4A0` (`recomp_0013.c:16690+`: pool from `0x3C9158[tag]`, chunk list from `pool+8`, next chunk at `chunk+8`) [I].
  Neither has been done; the size of the "really changing" part of the heap is **[U]**.

## 2. State outside guest RAM that affects the simulation

| State | Where | Needed for a restore? | How |
|---|---|---|---|
| **Netplay**: `N.step`, `N.in[][]` ring, `N.latest_local`, local hash ring, `N.match`, seed counters | `src/hooks/netplay.c:1810-1900,1925-1951` | Yes for netplay rollback | rewind `N.step`; keep the ring (inputs are the thing being replayed); the pad table `0x3B8F48+slot*0x20` is rewritten by `netplay_on_pad_read` every read |
| **Crowd's private copy of the global generator** `g_crowd_gen[6]` | `netplay.c:1689-1700` | Yes | save/restore with the snapshot (the real `0x33DB5C` is drawn at host timing by ~60 other callers and must not be restored into the crowd) |
| Test harness: `s_step`, hash ring | `test_telemetry.c:298-330` | Yes | `snapshot_point` already rewinds `s_step` |
| Registers: `g_esp g_ecx g_edi g_ebp ...`, FPU stack `g_fp_stack/g_fp_top/g_fp_control_word`, MMX/XMM TLS | `recomp_types.h`, `netplay.c:1790-1797` | At a function-entry hook (`sub_00087700_enter`, `src/recomp_manual.c:61`) the callee has no live registers except `ecx = 0x349760` and `g_edi` (the crowd's dt, rewritten by the hook); restore **in place** (rewind and continue) needs nothing else | **do not** restore the guest stack: it holds the live frames of the callers [I] |
| Time: `KeTickCount` (`0x740020`), `KeQueryInterruptTime` (ordinal 125, 94,978 calls in the long log), `rdtsc` = QPC scaled to 733.33 MHz | `xbox_memory_layout.c:1772`; `kernel_hal.c:1195-1215,410-480` | The fight step reads none of them (no `rdtsc`, tick, QPC import in the 1,111 functions of `fight-determinism.md` Q4). `rdtsc` seeds exist **only at match start** (`sub_0008BB00` tail, `sub_000C8820 -> sub_001AD220`, `sub_0008ABF0`; `fight-determinism.md` Q1) and end up in guest memory | nothing for a mid-fight restore; do **not** restore across a match start |
| Kernel dispatcher objects: events, semaphores, timers, mutants | host `HANDLE`s keyed by guest VA, created lazily from the guest bytes only the first time (`kernel_bridge.c:7270-7340`, `g_timers[32]` at `:3130`) | No: signalled state is in the host object, the guest bytes are not consulted again. The sim thread does not wait on them in a step | restoring the bytes is harmless and does nothing |
| Critical sections | host `CRITICAL_SECTION` table keyed by guest VA, guest 20 bytes untouched (`kernel_rtl.c:165-260`) | No. The step runs while holding `0x2FB000` (`simulation-tick.md` Q1.4); rewinding in place keeps the host lock state valid | none |
| Thread contexts | real host threads (`PsCreateSystemThreadEx` spawns; log lines 63-66, 531-551), per-thread TLS register set | Cannot be saved | exclude their stacks and everything they write (section 3) |
| APU / audio | host model `[APU] ... 256 voices`, `XAudio2` backend; the music ring refiller (`sub_001242D0` at ~100 Hz, `music-ring-mixer.md`) writes the DSound ring and `.data` `0x3A9324..0x3A9684` | No, and restoring its `.data` would corrupt it. Sounds the sim triggers will play again when steps are replayed (the trigger path is a mailbox, `sub_0019AB60`, not a direct DSOUND call: no direct-call path from `sub_00087700` to any `0x256A00..0x27CD84` function, [V] for direct calls) | mute or skip audio posts during a resim [I]; leave the APU alone |
| GPU: NV2A executor on `nv2a_ack_thread`, translator state, D3D device `0x234770`, push buffer, fence word `*(dev+0x30)` | host thread `xbox_memory_layout.c:1327-1470`; device `d3d-pushbuffer-wrap.md` | **Not read back by gameplay** as far as found: the only GPU-to-guest writes are the fence/semaphore (`nv2a_pb_exec.c:4003-4010`), `GET_REPORT` visibility results (`nv2a_pb_exec.c:4097-4110`, written at the report DMA offset, nothing in this title was found to read them [U]), clears of render targets, and the `dev+0x2C/0x30/frame counter` mirrors (`xbox_memory_layout.c:744-860`). The step does reach D3D code: `sub_001AB410 -> sub_000BBAA0 -> sub_000BA920 -> sub_00019320 -> sub_0010E0C0 -> sub_002142F0 -> sub_0021EB90` (fence insert) [V path, U whether it runs in a normal step] | never restore D3D memory or the push buffer; if the game's own D3D wrapper cache (texture/stream binding arrays at `0x3A5030`, `0x3A5050`, `0x3A6E20`, written by `sub_0010E0C0`) is restored it will disagree with the device [I] |
| Files / streaming | `NtReadFile` (ordinal 219: 10,630 calls in the 86k-frame log), host file handles and positions (`kernel_file.c:373,419`), loader thread `sub_001E4B40` | No, if no load is in flight | restore only in fight states (CLogic state 5 or 8); count `NtReadFile` per fight step to be sure [U] |
| USB/pad model | OHCI model, HCCA in the window's first page; pad reports written by the host | No | exclude |
| Host allocator bookkeeping | `g_heap_blocks[65536]`, `g_heap_next`, `g_contig_next`, `g_contig_blocks[]` (`xbox_memory_layout.c:3582-3600,3807`) | Only if the step allocates from the host: it can reach `MmAllocateContiguousMemoryEx` (`sub_00044960 -> ... -> sub_001010D0 -> sub_001F4F8B` from the hit resolver `sub_001A5DD0`, [V path, U runtime]). Restoring would hand the same memory out twice | do not restore; a leaked contiguous block is harmless |

Writes the *runtime itself* makes into guest RAM while a fight runs (a restore races with all of them): `nv2a_ack_thread` (KeTickCount page `0x740000`, fence mirrors,
frame-counter mirrors, executor output), OHCI periodic completions, DPC routines, `NtReadFile` completions, the APU model's doorbells, and every guest thread.

## 3. Guest memory that is not simulation and must be excluded from a restore

Evidence column: S = static reach from the thread root, M = `logs/mcp-memdiff.txt` (main RAM only), D = documentation or code comment.

| Range | Owner | Evidence |
|---|---|---|
| `0x00004000` TIB; `0x00700000..0x00780000` RW data, kernel data (`0x740020` KeTickCount), TLS; main stack `0x780000..0xF80000` (and its worker slices) | runtime + live frames | D; M (`0x740000`, `0x7BF000`, `0xF7F000` change every step) |
| `0x00F80000..0x04000000` host heap: other threads' stacks, TIBs, kernel-object stand-ins, DPC/USB objects | runtime | D; M (`0x01181000`, `0x00F9E000`). **`RECOMP_TEST_SNAP=full` restores this range and will corrupt running threads** |
| D3D `0x2139A0..0x236F60` (device `0x234770`, fence/ring bookkeeping), XGRPH, D3DX, DSOUND `0x256A00..0x27CD84`, XPP `0x27CDA0..0x285238` (USB), DOLBY `0x3CD5A0..0x3D4720` | driver libraries | M (`0x233000` 16 KB dirty every step, `0x27C000`) |
| In `.data`: front-end command mailbox thread (`sub_0019AB60`): `0x2EB5AC..0x2EB61C`, `0x2EB7AC..0x2EBF1C`, `0x3B8070..0x3B83BC` (includes `FE_RESULT_PTR 0x3B8090`) | thread | S (261 functions, 264+286 writes) |
| In `.data`: music ring refiller (`sub_001242D0`): `0x2E6224`, `0x3A9324..0x3A9330`, `0x3A94BC`, `0x3A95D8..0x3A9684` (lead word `0x3A9660`), `0x3A9B14..0x3A9B34`, `0x3AB418` | thread | S (148 functions); `music-ring-mixer.md` |
| In `.data`: pad/frame producer thread (`sub_000D7700`) and loader thread (`sub_001E4B40`): `0x3C92C8..0x3C92D8`, `0x3CC7FC..0x3CC814`, `0x3CCA98`, `0x3CCC60`, `0x3CD0A8..0x3CD0CC` | threads | S |
| In `.data`: the game's D3D wrapper caches `0x3A4000..0x3A7000` (bindings at `0x3A5030`, `0x3A5050`, `0x3A6E20`, written by `sub_0010E0C0`) | graphics layer | S, M (`0x3A4000`, `0x3A5000` dirty); I that they mirror device state |
| In the heap block: the frame/pad record queue (`*(0x3A017C)`, at least `0x1A80` bytes) | producer thread | D (`simulation-tick.md` Q1.3) |
| In the heap block: audio objects the refiller/mailbox threads read, DirectSound-visible buffers if the title allocates any there | threads | U |
| In the window outside the block: push buffer, framebuffers (`0x82CE8000`, `0x82E14000`), depth/back buffers, DSOUND arena (`0x831F8810` doorbell), HCCA and USB structures (`0x80001000`), GPU instance memory (top 128 KiB) | GPU / APU / USB | D; `xbox_memory_layout.c:3795-3806` |
| NV2A/MCPX apertures `0xFD000000`, `0xFE800000` | device models | D |

The audio and loader threads also **allocate from the heap** (the file layer's callbacks, `crash-001B2480.md:283`). That is the hazard of restoring the allocator: an
allocation another thread made after the save is forgotten by the restored pool, and its owner keeps the pointer (section 6, risk 1).

## 4. Size of a per-step snapshot

Measured, not guessed: `logs/mcp-memdiff.txt` (page-hash diff of main RAM `0..0x4000000`, taken during a fight):

| Window | Steps | Changed pages (main RAM only) |
|---|---|---|
| 0.05 s, fight idle | 1 | 84 KB |
| 0.05 s, fight active | 1 | 112 KB |
| 1 s, fight active | 44 | 216 KB |
| 5 s union, fight active | 236 | 228 KB (33 ranges) |

Of those ~56 pages, the sim ones are the `.data` pages (`0x375000`, `0x39B000`, `0x3B9000`, `0x3BD000`, `0x3B8000`, `0x349000`, `0x354000`, ...); roughly a third are
D3D (`0x233000`), stacks, TLS and KeTickCount pages. **The heap block was not included in that diff.**

| Option | Bytes per step | At 60 steps/s | Ring of 10 |
|---|---|---|---|
| (a1) all main RAM (64 MiB) | 67,108,864 | 4.0 GB/s | 640 MiB. **Wrong answer**: misses the heap |
| (a2) both windows (128 MiB) | 134,217,728 | 8.1 GB/s (a whole frame of memcpy on one core) | 1.25 GiB. Restores threads' stacks, driver state, GPU memory: unusable as is |
| (b) heap block (44,081,152) + `.data/.bss` (1,015,972) | 45,097,124 (43.0 MiB) | 2.7 GB/s, ~4-5 ms of memcpy per step at 8-12 GB/s | 430 MiB. Needs the exclusions of section 3 inside the `.data` part, and the heap's queue object |
| (b') whitelisted sim state only (fighter records 19 KB, arena object ~192 KB, match/AI/RNG/crowd globals ~60 KB, generators) | ~0.3 MB | ~20 MB/s | 3 MB. **Insufficient alone**: pointers in it go to heap objects that change |
| (c) dirty pages only | main RAM: ~60-110 KB measured; heap: **[U]** (expected 0.2-4 MB: pose/skeleton buffers, effects, crowd, allocator headers) | < 0.3 GB/s | 10-50 MB [I] |

### Can the dirty pages be found without copying everything?

* `GetWriteWatch` needs memory from `VirtualAlloc(..., MEM_WRITE_WATCH, ...)`. Both guest areas are `CreateFileMappingA/W(INVALID_HANDLE_VALUE, ..., PAGE_READWRITE, ...)`
  + `MapViewOfFileEx` (`xbox_memory_layout.c:2465-2556` main RAM with 27 mirror views, `:3017-3028` contiguous window, `:3370` tiled alias). File-mapping views are `MEM_MAPPED`; the
  API documents write watch for `VirtualAlloc` regions only, so the call is expected to fail with `ERROR_INVALID_PARAMETER` [I, Win32 documentation, not tried here]. The only
  `VirtualAlloc` of the window is the fallback when the mapping fails (`:3025`), and it passes no `MEM_WRITE_WATCH`. Switching the window to a write-watch `VirtualAlloc` would break the tiled
  alias (`:3370-3400`), which D3D relies on (`:3360-3366` comment).
* **Page protection + the existing vectored handler** works on file-mapping views (`VirtualProtect` up to the section's `PAGE_READWRITE`; `xbox_ProtectMirrorsForDebug`, `:3435-3447`,
  already does it to the mirrors). `veh_handler` (`src/main.c:391-430`) already routes access violations on trapped device pages. Hazards: (i) a Win32 call that writes into a protected
  page (`ReadFile` in `kernel_file.c`) returns `ERROR_NOACCESS` instead of raising; only arm during a fight state, never during a load; (ii) writes through the tiled alias (`0xF0000000`) or a
  mirror view do not trap the base view; (iii) the GPU thread and guest threads fault too (a first write costs a few microseconds each); (iv) re-arming = one `VirtualProtect` over the
  block per step (cost [U], measure).
* **Hashing**: the existing `memdiff` hashes 4 KB pages with a scalar multiply per 8 bytes (about 1 GB/s: tens of ms for 64 MB; fine for a diagnostic). A vector hash (xxh3/CRC32C) over the 43 MiB
  of option (b) is memory-bound, about 4 ms; it needs no traps, finds writes made through any alias, and is the safest first step.
* A software card table is not possible: `MEM8/16/32` are lvalue macros (`recomp_types.h:428-430`) and the runtime's own writers bypass them.

## 5. What the existing groundwork tells us

* `src/hooks/test_telemetry.c:283-340`: `save` copies `[0x2D5000, 0x3CD000)` at the start of a step on the game thread; `load` copies it back at the start of the next step and rewinds `s_step`. `RECOMP_TEST_SNAP=full`
  copies `[0x10000, 0x4000000)` except `[0x700000, 0xF80000)`. The per-step hash covers only the `.data` range (`region_hash`, `:321`), so it also contains the thread-written ranges of section 3 and can never replay equal. The hook is
  `sub_00087700_enter` (`src/recomp_manual.c:61`).
* `logs/mcp/copy0.log.err`: `[SNAP] saved the state of step 203` (line 18379), draws 309,400 -> 359,200 later `[SNAP] loaded the state of step 203` (line 20043), then
  `[ICALL] Failed to resolve VA 0x002B5850 ... called from guest 0x000221FB`, then `[CRASH] Access violation ... fault addr=0x10AC2C98C` with `eax=0xBEA2DE1B ecx=0x00349760 ... in sub_00086F20+0x4F` (the chain includes `sub_00087700`, return `0x00087722`). Which snapshot mode ran is not
  printed [U]. Both modes leave the heap block as it was 500 frames later, while the restored `.data/.bss` points at the heap as it was at step 203: stale vtable (`0x2B5850` is in `.rdata`, not code), stale object fields. This is what section 0 point 2 predicts. [I]
* The `peek`, `poke` and `memdiff` control verbs refuse anything above `0x04000000` (`test_control.c:230-292`) and the MCP tool wrappers inherit that.
* Correction to `crash-001B2480.md:256`: `this + 0x30040` with `this = 0x349760` is `0x3797A0`, not `0x379DA0`. [V arithmetic]

## 6. Risks and open questions

1. **Allocator vs other threads [U].** Restoring the pool state forgets allocations made by the loader/audio/mailbox threads after the save. Check by hooking `sub_00029500_enter` with the host thread id during a fight (count by thread). If only the sim thread allocates during a fight, the risk is nil.
2. **Does the step touch the host allocators [U]?** Hook `sub_001F4F8B` (ordinal 166) and `NtAllocateVirtualMemory`; the static path through the hit resolver exists.
3. **Heap dirty set [U].** Not measured. Extend `memdiff` to `0x80003000..0x82A0D000` first (section 7).
4. **Which heap objects are shared with non-sim code [U]**: audio voices, D3D resource descriptors held by effects (textures registered with D3D), the frame queue. A restored descriptor that points at a texture the host translator already released or replaced shows up as a one-frame glitch at worst; a restored D3D wrapper cache (`0x3A5030..`) could skip a bind [I].
5. **Resimulation re-triggers side effects**: sounds, rumble (`[INPUT] virtual pad 2 rumble` appears right at the crash in `copy0.log.err`), HUD events (`0x3BE130` event ring), telemetry. The crowd update runs *after* `sub_00087700` (`crowd-lockstep-reset.md` section 2) and takes a measured dt, which the netplay hook clamps to 1/60 only for `0.001 < f < 0.25` (`netplay.c:1851-1859`); a resim that calls only `sub_00087700` skips the crowd and the AI hand-off `sub_001AD380`, so the unit to replay is the whole `sub_0008BE60` pass or the hook must reproduce them [I].
6. **GPU async reads**: the executor reads vertex/texture data from the heap on its own thread. A restore during a frame can draw one frame from half-restored data [I]; invisible if the restore is brief.
7. Static reach caveat: vtable calls are not followed (`fight-determinism.md` "What I could not settle").

## 7. Recommended first save/restore experiment

Goal: prove that **heap block + sim part of `.data` + rewound host state** replays a fight bit-exactly, before any netcode.

1. **Measure the heap dirty set** (no restore yet). Add `0x80003000..0x82A0D000` (rounded to the block read from the first `MmAllocateContiguousMemoryEx` of 44,081,152 bytes, logged in `bridge_MmAllocateContiguousMemoryEx`) to `memdiff` and to `peek`. Run the same `begin/end` windows as `logs/mcp-memdiff.txt` (1 step, 12 steps, 5 s union). Output: dirty KB per step, the largest ranges, and by walking `0x3C9158[]` which pool/tag they belong to. This also decides between (b) and (c).
2. **Extend `snapshot_point`** (`test_telemetry.c:330`): (a) save/load the heap block (one 42 MiB `malloc`, ~5 ms) with `memcpy` through `XBOX_PTR` (the window is already addressable; `watchpoint.c:205-210` shows the check); (b) restore `.data` as two or three ranges that skip the section 3 thread-owned clusters (`0x2E5000-0x2E7000`, `0x2EB000-0x2EC000`, `0x3A4000-0x3A7000`, `0x3A9000-0x3AC000`, `0x3B8000-0x3B8500`, `0x3C9200-0x3CD100`; page-granular is enough because the owners write only inside them), and the pool table `0x3C9138..0x3C9258` **with** the heap; (c) skip `*(0x3A017C)`'s object; (d) leave stacks, TLS, kernel data, host heap and D3D alone. Drop `RECOMP_TEST_SNAP=full` for good.
3. **Fingerprint = what the sim owns**, not `region_hash`: the four fighter records' fields already used by `fighter_hash` (`netplay.c:1598`: `+0x30 +0x44 +0x48 +0x4C +0x54 +0x350`) plus the generators `0x3C1404`, `0x3BFCF0/0x3BFD78/0x3BFE00/0x3BFE88`, `0x39C160`, `0x37705C`, and a hash of `0x3B8F28..0x3B9074`. Record it every step.
4. **Run**: fixed `RECOMP_TEST_RNG_SEED`, scripted pads (`RECOMP_TEST_INPUT`), a one-on-one with the crowd restart on. Save at step 203; run 60 steps recording fingerprints; load at the entry of step 263 (rewind in place, `s_step = 203`); run 60 steps again with the same scripted input. **Pass**: the two fingerprint sequences are identical for all 60 steps, no `[ICALL]` failures, no crash for a minute afterwards. **Fail analysis**: the first differing step plus `RECOMP_WATCH_WRITE` on the differing field names the missing region.
5. Only then dirty-page trapping (section 4) and the netplay-side state (section 2).

Reference points for later: heap block 44,081,152 bytes, `.data` 1,015,972 bytes, ring of 10 snapshots at full copy 430 MiB, per-step full copy ~4-5 ms.
