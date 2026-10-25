# Toolkit migration ledger: v0.13.0 → v0.16.0

D147, 2026-10-21. Target: upstream v0.16.0 `f0f8a46` (2026-10-20). Source: the fork commits the port pinned or
had in open PRs: `e818066` + `8589f88` (issue #72), `72bec86` (texture-pack list length, PR #73) and `20e7ba7`
(menu widescreen extensions, draft PR #62), i.e. upstream v0.13.0 `b3700e1` plus 113 commits (5 merges and
several duplicate cherry-picks among them). Fork branch: `defjam/upstream-v0.16`.

The plan, its five area reports and the decisions behind it are in `docs/research/toolkit-v016/` (local
research; `PLAN.md` first). The fork's commits were replayed as one commit per area, as in the v0.13 rebase
(`toolkit-rebase-ledger.md`), with each conflict resolved by hand; every commit message says what was taken from
which side and why. Upstream's new machinery that is off by default stays off (#192's DISPATCH gate, paced
vblank, flip pacing, guest CPUs and guest lock; the #162 DMA walk; the #215 D3D11 presenter; the #191 SDL2
window; PVIDEO; `--entry-stubs`; `--back-edge-yield`; `--skip-library-immediates`), and the port never sets
them: the gate, the paced vblank and the flip hold read and write NV2A registers as plain memory on pages the
port traps.

## Topic commits on v0.16.0

| Topic commit | Fork commits carried | Notes |
|---|---|---|
| `54a8479` feat(settings) | e2bbfff, 3ba2153, 72bec86, f1dc85b (cmake/xbox_sdl3) | unchanged |
| `6419765` feat(platform) | b431430, 7fc5f12, 26da697, dc00f70, cfa38f6/466930f, fb153f1/0117356, a5b2ccf/9eb2194, 8742fa9, 32aab4d, 8474609, e5e59f6, 8f08c4f | mmio_decode.h: upstream's decoder, typed on the fork's mmio_ctx_t; the fork's POSIX branch defers to the trap layer |
| `0d0c6a8` fix(kernel) paths and saves | 99a6efa (paths), 7fc5f12 (first-run UDATA, anchor), 85f46e3, 086c68c, 962eb36 (sorted listing) | `Partition1\UDATA`/`TDATA` keep `UserData`/`TitleData`; upstream's partition images replace the fork's |
| `599d76e` fix(memory) | e818066, 8589f88 (issue #72), 8474609, 7bf9ad5 (runner state), 99a6efa (physical record), ca5e480 | upstream's contiguous page allocator replaces the block list; 837e2c7/e995cc6 obsolete |
| `8ab2ec8` fix(kernel) interrupts, DPCs, timers | 99a6efa, 7bf9ad5, 447d980, fa0da9e/7642acd, feab10b/9051be9, ca5e480 | fork's NV2A level model and 1 ms timer loop on upstream's drain/fire; DISPATCH lock stands aside under the gate |
| `9fbce3f` feat(nv2a) | 7bf9ad5, e5557ed, 05db81c, 4bff257, dff3985, 91577b1, 0fa2051, 48edfee, 2992bd8, 660f953, 7fe8871, 3686899, 49a1798, 6a71315, f14821c, f41f243, 20e7ba7 | both walkers kept; the fork's runner drives the executor |
| `97a7307` fix(apu) | 682f519, 8ae5cb7/964ad13, f0b38f8/55dc406, 26edf72, dccbb31, d7b656c, 4a6c88e/8507a75 | device-paced output and resampler from upstream; slice pacing re-seated; apu_sdl3.c |
| `e4849e8` feat(usb,input) | 89c3b1d, f1dc85b, 4c495ba, 4a0cb7e, e705a19, 6d9d23e, bec01fa, 8c72bf1, 3552057/81f2cf1 | mapping rules are upstream #203; wheel VKs and the round keyboard stick are fork deltas |
| `652a48f` build(sdl3) | (new) | XBOXRECOMP_SDL3 at the root; fb_present_stub.c |
| `06a9cf3` feat(d3d) | 05db81c, 26f2747, c4e5af1, bec01fa, 3e80eae, 10ab24c, a87ab22, dc132fa, c4b0bf9, e4e653a, 0e191bf, eb2de31, f3e7c60, b4464f8, 085d2b2, 16edd57, 769d3ca, c527129, 7afcd20, bac3653, 48702e6, 6eb0abc, eb4b0f1, adb67f7, 2edf96f | unchanged |
| `aa00125` feat(disasm) alias seeds | (new) | a seed entry marked `"alias": true` becomes an alias body, never a start |
| `0988577`, `f30a713`, `81810c8` fix(recomp) switch slots | ed81613, 48268f5, c3e95e7 | cherry-picked; two upstream tests adjusted for the fallback's second path |
| `2b73d81` perf(disasm) decode cache | (new) | fork copy of upstream PR #255 (issue #254): the callback probes share one decoder that decodes each instruction once; disasm 990 s -> 167 s here, output byte-identical. Drop at the next rebase once upstream has it |
| `3e63e7b` fix(apu) named heap bytes | (new) | a page MmGetPhysicalAddress named from the heap is RAM only for the bytes of the live block that was named; the mix buffer's last page (shared with a named heap block) had read RAM and clicked once a lap. Upstream candidate (upstream's page map, #193) |
| `957c1ba` fix(apu) sequencer restart | (new) | a frame that reads SECTL while DirectSound's SSL service routine has the sequencer stopped (150-400 us every 43 ms) waits instead of playing a silent slice; one that stays off past 5 ms gets lightweight frames as before (`mcpx_apu_frame_kind`, D201) |

## Dropped

| Fork commit | Why |
|---|---|
| 10fe9d9 MSVC C fixtures | upstream #179 (ours) |
| b8754f8 FPREM C2 | upstream #177 (ours) and #183's `recomp_fprem` |
| 447d980 (wait code) | upstream #181 (ours); the fork's wait body is kept in `8ab2ec8` |
| 962eb36 (drop on close) | upstream #201 (ours) / #190 |
| ada3278 mapping rules | upstream #203 (ours, rebuilt on #191's key table) |
| c220aba width-correct IN/OUT | upstream #194 `xbox_PortIn/Out`; the fork's `xbox_Io*` names stay as wrappers for older lifts |
| 4a0eb5f, c1b3d26, aeed97c dynamic flags | upstream's `_fa`/`g_eflags` model (#183, #186, #198). A stock v0.16 lift of the game leaves 5 flag reads unassigned, all at function entries the fork's model also read as unknown; the replay hash and goldens are the acceptance check |
| b81fb57 A64 ALU forms, b64a677 clock overflow | upstream #191 |
| 7fc5f12 (partition images) | upstream #190, same geometry |
| 3eea9a8/3251811 look-ahead 8 | upstream #193's exact-consumption resampler |
| ba27afa front-end wait | upstream #193's trap handling |
| fork DSP doorbell derivation | upstream `RECOMP_APU_DSP_ACK=auto` |
| 837e2c7/e995cc6 contiguous high-water log | allocator replaced |

## Discovery parity (config/seed_functions.json)

v0.16's discovery drops 340 functions of the v0.13 analysis and adds 530. Several dropped ones are reached
at runtime through function pointers and switch tables: the first migrated run drew no loading screen
(`0x001180B0` and `0x001186C0`, two `wbinvd` routines, unresolved 9,000 times). All 340 are seeded back,
noted "Toolkit v0.16 migration (D147)":

- 136 that lie in no v0.16 function, as ordinary seeds;
- 204 that lie inside a v0.16 function, as `"alias": true` seeds (`aa00125`). As ordinary seeds they cut
  their function short: `sub_0019FFC0` ended at `0x001A0003`, its switch arms went unresolved 61,000 times,
  and the first fight never started.

With both, the analysis lists 18,442 functions; 5 of v0.13's are still absent (no recorded callers), and the
613 functions whose extent shrank match a stock v0.16 lift (trailing padding no longer counted). The 42
addresses `src/recomp_manual.c` names are all function starts (`scripts/check-manual-addresses.py`, run by
`pipeline.py recomp`). Pruning the restored seeds is later work, one at a time against the regressions.

## Windows results (2026-10-22)

Windows 11, RTX 4070 Ti SUPER, VS 2019 Build Tools (MSVC 14.29), branch at `ada3323`, pin `32aa980`; the
comparison build is v0.10.7 (`009473a`, toolkit `8589f88`) on the same PC.

| Check | Result |
|---|---|
| Toolkit tests, MSVC (`scripts/test-toolkit-msvc.py`) | 826 passed, 20 skipped. CI's Windows job: 844/2. The 18 extra skips are tests that call clang or gcc, which this PC does not have |
| Native fixtures (all 19 CI runs, Release, Ninja) | 19/19, including `windows_guest_layout`, `memory_regressions`, `kernel_regressions` (slow tests on), `input_host` |
| Analyze, lift, build `win-x64-release` | Pass. Analyze 16.6 min against 1 min for v0.10.7 (below). 18,440 of 18,446 functions lifted, 0 failed; MSVC build clean |
| `check-manual-addresses.py` | 42 checked, 0 missing. Run by hand: `recomp.ps1` does not call it, only `pipeline.py recomp` does |
| Full regression (`logs/regress-20261022-152519.txt`) | 14/14; launcher, overlay, unlockopt and picture skip on Windows by design. Replay hash unchanged (80 records, 1,800 steps); Terrordome 242 s at median 120; combat 27/27; versus 26/26; soak 3/3 |
| Port unit tests in the MSVC environment | 539 passed, 25 skipped (v0.10.7: 537/25) |
| Saves | crib and gym load the existing profile; `save/` has no `UDATA`/`TDATA`; `UserData` identical to a copy taken before the run |
| Missing functions | No `[ICALL] Failed to resolve` and no `[CRASH]` in 21 run logs |
| HRTF | `RECOMP_APU_HRTF` unset in the shell, user and machine environment; nothing in the launcher, scripts or `settings.ini` sets it |
| Setup (CI run 39846827103, SHA-256 checked) | Silent install into a new folder: exit 0 in 30.5 min. The installed game reaches the main menu at median 60 with no crash (the issue #72 path) |
| **Audio clicks** | **Fail.** Below |
| Play in the Windows launcher | Not run: it needs a person (the scripted launcher check is SDL-only) |

**Audio.** Opening scene, 330 s, four runs per build, alternating, holes from `scripts/audio-holes.py`:

| Build | F8 | FA |
|---|---|---|
| migrated | 5 / 367 / 421 / 187 | 0 / 125 / 45 / 44 |
| v0.10.7 | 9 / 182 / 5 / 5 | 1 / 0 / 0 / 0 |

Three migrated runs have holes through the whole scene (0.6-1.4 a second, per-minute counts flat), and FA
has them too. v0.10.7's one bad run has its holes in the last minute only. Frame pacing is the same in all
runs (median 120 presents per 2 s). This is a Windows regression of the migration. It is not fixed here: the
fix is in the toolkit. On Windows the APU still writes through waveOut with period pacing (slice pacing is
off on Windows, `apu_core.c`). The suspect is `97a7307`, which took upstream's device-paced output and
#193's resampler in place of look-ahead 8. The Mac's one high run (147) may be the same effect, less often.

**Analysis time.** The disasm step takes 990 s here (965 s inside Setup) against 30-32 s for the v0.10.6 and
v0.10.7 Setups. A profile shows 46,514 `probes_as_callback_body` calls, each a full `disassemble_cfg`, 25
million capstone decodes in all. That is upstream v0.16 code; the fork's only disasm change after
`f0f8a46` is the alias seeds (`aa00125`). Players' Setup time triples (30.5 min).

**Re-test on pin `2b73d81` (2026-10-23).** Branch `7350962`: the v0.16 migration, the disasm decode
cache and main after the v0.10.7 publication. Toolkit tests with MSVC 829 passed / 20 skipped (the 3
new ones added). Analyze 2 min 44 s (disasm 157 s); the analysis output and all 23 lifted C files are
byte-identical to `32aa980`'s; manual addresses 42/0. Full regression 14/14 again
(`logs/regress-20261023-151711.txt`): replay hash unchanged, Terrordome 242 s at median 120, combat
27/27, versus 26/26, soak 3/3; no ICALL failure or crash in 18 logs; saves unchanged. CI run
40028710042 green; its Setup (provenance toolkit `2b73d81`) installs from the dump into a new folder
in 16.5 min (30.5 on `32aa980`, disasm 155 s against 965), and the installed game reaches the main
menu at median 60.


## Windows audio in borderless fullscreen (2026-10-24, D199)

The owner's play-test (pin `2b73d81`) crackled all through the opening scene. Every scripted run before
it was windowed with default settings (`RECOMP_SETTINGS=none`, 4:3); his settings play borderless
fullscreen, widescreen, render scale 3. Opening scene, 330 s, holes in the mix buffer
(`scripts/audio-holes.py`, F8 / FA), no XAudio2 underruns in any run:

| Build and settings | F8 | FA |
|---|---|---|
| migrated, owner's settings (fullscreen) | 381, 400 | 122, 96 |
| v0.10.7, owner's settings | 6, 9 | 0, 2 |
| migrated, owner's settings, render scale 2 | 219 | 48 |
| migrated, owner's settings, 4:3 | 69 | 21 |
| migrated, owner's settings, **windowed** | **5** | **0** |
| migrated, fullscreen, XAudio2 3 x 1024 (64 ms) | 146 | 52 |
| migrated, fullscreen, 6 x 512 (64 ms) | 5, 5 | 0, 0 |
| migrated, fullscreen, 8 x 512 (85 ms) | 5, 4 | 0, 0 |
| migrated, fullscreen, 4 x 512, upstream's in-thread APU interrupt (local experiment) | 270, 281 | 76, 73 |
| **migrated with the D199 default (6 x 512), owner's settings, no overrides** | **5, 6** | **0, 0** |

Cause: upstream #193 (v0.14.0, `a16163f`) moved XAudio2 from a fixed 1024 x 3 (64 ms) to 512 x 4 (43 ms)
and paces the APU frame thread by the device queue, rendering ahead in a burst whenever the queue
falls to its low mark. In borderless fullscreen each `Present` waits for the display; that jitter
reaches the low mark of a 43 ms queue, and the burst moves the play cursor past the title's own
stream writes. The fork's APU interrupt path is not the cause: upstream's in-thread delivery also
crackles. Fix (D199): the port defaults `RECOMP_AUDIO_BUF_COUNT=6` on Windows (`src/main.c`), the
smallest clean depth and v0.10.x's latency. Upstream issue https://github.com/sp00nznet/xboxrecomp/issues/259.
Issue #93 (power throttling) is likely the same queue starvation by another route.


## Windows audio: the second cause (2026-10-24)

The owner's re-test with D199 still crackled. What the device is given (`RECOMP_APU_PCM`) had step clicks at
32-sample slice edges in the opening scene's first three minutes, in every display mode: 386-422 per 330 s,
v0.10.7 2. They are already in voices 0xF8/0xFA's fetched samples, and sit at lap positions 2,371-2,399 of the
2,400-frame six-channel buffer: its last page. The buffer is a window allocation (physical pages 0x01309-0x01310);
its last 344 bytes share page 0x01310 with a heap block that starts after them (0x01310790) and that the title
named through MmGetPhysicalAddress. Upstream's page map (#193) then sent the whole page to RAM, so those 28
frames played what RAM held there. v0.10.7's allocator never put a window buffer next to a named heap block.

Ruled out first, by local experiments since reverted: stream underruns, dropped resampler source, the
render-ahead, v0.10.7's resampler in place of #193's, and the queue size.

Fix `3e63e7b`: heap blocks remember being named and `xbox_PhysRamNamedAt` answers per byte; `apu_phys` gains the
shared-page case (it fails without the change). Opening scene, device clicks per 330 s:

| Build | windowed, defaults | owner settings, fullscreen |
|---|---|---|
| pin `2b73d81` + D199 | 386-422 | 386 |
| with `3e63e7b` | 1 | 1, 1 |
| v0.10.7 | | 2 |

Full regression 14/14 with it (`logs/regress-20261024-112504.txt`); fixtures `apu_phys`, `memory_regressions`,
`windows_guest_layout`, `kernel_regressions`, `kernel_physical_address` pass with MSVC.

## Linux / Steam Deck results (2026-10-23)

Steam Deck (LCD, 90 Hz panel, SteamOS 3.8, RADV VANGOGH), branch `7350962`, pin `2b73d81`, built in the pinned
Arch container (`localhost/defjam-dev`). The regression ran on SteamOS under `gamescope -W 1280 -H 800 -r 90`.
The comparison build is `77714da` (toolkit `5445b73`, built 2026-10-18), run the same way. Raw logs are
not committed.

| Check | Result |
|---|---|
| Toolkit tests (`djpy`, container) | 848 passed, 1 skipped (`test_fpdiff`: needs 32-bit MSVC). `tools/fusion/test_lifter_diff.py` does not collect (no `pefile`), as in CI's scope |
| Port unit tests (`-m "not slow"`, `--init`) | 502 passed, 38 skipped. Without `podman run --init`, `test_cancellation_stops_the_whole_process_tree` fails |
| Native fixtures (11) | 11/11. `memory_layout_posix`, `kernel_memory_posix`, `kernel_path_posix` and `kernel_bridge` need `-DXBOXRECOMP_SDL3=ON` in the Deck image (no SDL2 there; CI's Ubuntu job has it). The handover command omits it |
| Analyze / recomp / build `posix-release` | Analyze **4 min 10 s** (`--no-archive`), recomp 2 min 18 s, build 1 min 50 s. No SDL2 symbol, `ldd -r` clean on SteamOS |
| `check-manual-addresses.py` | 42 checked, 0 missing |
| Regression, full table in one `gamescope --backend headless` (40 min, no abort) | 16 of 17 **ok**: unit, m2, m3, m4a, launcher, overlay, unlockopt, picture, fight, ffa, ffa-terrordome, combat 27/27, versus 26/26, intro, crib, gym, soak 3/3. `replay` fails the Mac golden (below). With `--only`: skipintro ok, fight-terrordome ok, two-matches 35/35 ok; `ffa-result` and `repeat`: below; `visual` blocked (no baselines on this Deck) |
| Saves | crib and gym load the existing profile; `save/` has no `UDATA`/`TDATA`; the fresh Setup install creates `UserData`/`TitleData` |
| Missing functions | No `[ICALL] Failed to resolve` in 35 run logs |
| HRTF | `RECOMP_APU_HRTF` unset in the shell; no HRTF line in any log |
| Setup (CI run 40035905816, SHA-256 checked, provenance toolkit `2b73d81`) | Silent install into a new folder (`--no-shortcuts --toolchain container`): exit 0 in **7 min 56 s**. The installed game runs 3,177 frames with no crash. Not run: Game Mode launch, pad and rumble, launcher rebinding (need a person) |

**Harness problems on the Deck, not regressions of the migration:**
- Nested `gamescope` aborts (SIGABRT in one of its own threads) on a second game launch and kills the harness; the game then dies in `exit()` while `nv2a_ack_thread` still runs. `gamescope --backend headless` does not abort: `skipintro`, `unlockopt` (three launches) and the whole table pass in it.
- `unlockopt` and `picture` failed on the migrated and the old build alike for another reason: the Deck's larger launcher font makes the layout "stacked", which draws an on/off setting as an Off/On list where the Mac draws a checkbox, so the click only opened the list. Fixed in the harness: the list items are test targets (`<section>.<key>.on`/`.off`) and the checks take an optional second click (`uiopt:`).
- A run killed that way left the save changed, and the next guard copied the changed save as the original (this happened once here; the save was put back from a copy taken before the pass). `SaveGuard` now refuses to start behind an unresolved guard, ignores kill signals while restoring, and `harness.py save-recover` settles it.
- `replay` fails the Mac golden on both builds: migrated 87, 84 and 79 records in three runs, `77714da` 84 (golden 80). It is not a Deck-valid golden.

**Regression candidate: run-to-run determinism** (issue #98). `repeat` (two identical scripted fights, the second compared with the first) fails on the migrated build in 2 of 2 runs: the states agree until record 24, where the first run has a hit on fighter 0 at step 532 and the second at step 541 (clock, cpu, event, fighters then differ). On `77714da` the same check shows the two runs identical (79 records over 1,800 steps) and fails only against the Mac golden. The fight outcome therefore depends on something that varies between runs of the migrated build on the Deck. It is not traced. It fits the next item. The Mac replay hash is unchanged, so this may be specific to Linux or to the Deck's timing.

**`ffa-result`** (4-fighter free for all). Migrated: 1 pass, 2 fail on `combat.eliminations` in 3 runs (fighter 1 put out, two fighters alive at the end, codes 32, 32, 33 instead of 32, 32, 9). `77714da`, 4 runs: all 4 fail `audio.dry_queue` (issue #11), one of them also `audio.dropped_buffers`, one also `combat.eliminations`. So the elimination failure is not new, but it is 2 of 3 against 1 of 4, and the migrated build does not show the audio-queue failure at all.

**Audio (opening scene, 330 s, `scripts/audio-holes.py`):**

| Build | F8 | FA |
|---|---|---|
| migrated | 20 / 24 (0.06-0.07/s) | 0 / 0 |
| `77714da` | 43 / 57 (0.13-0.17/s) | 0 / 0 |

The Deck does not show the Windows crackle: the migrated build has fewer holes than the old one, and both are far below
Windows' bad runs (F8 187-421). Run with nothing else playing audio; I did not log the minutes of the holes.

**After the Deck pass (2026-10-23, branch `bf284cc`).** Rebuilt for the `pc_ui.cpp` change; port unit tests
under MSVC 562 passed / 26 skipped (the six new `SaveGuard` tests included). Against v0.10.7 (`009473a`), the
PR's own base, alternating:

| Check | `bf284cc` | v0.10.7 |
|---|---|---|
| `repeat` (issue #98) | 2/2 ok: 80 records identical over 1,800 steps, matching the golden hash | 2/2 ok, the same |
| `ffa-result` | 2/2 ok, 28/28 | 1 ok, 1 fails `combat.eliminations` |

So Windows does not show #98, and the `combat.eliminations` failure exists before the migration. A guarded
`crib` run on the real save went through the new guard's whole cycle (marker written, restored identical,
marker removed, `.ok` written; `save-recover` finds nothing unresolved).

**Two alias seeds have no effect.** `0x001F707B` and `0x001FF9B7` are in v0.10.7's dispatch table and
missing from this lift although `config/seed_functions.json` seeds them. Each sits right after the end of a
real function, in bytes only tail-jump alias bodies cover (`0x001F705D`, `0x001FF995`), and
`_pass_seed_aliases` needs a real enclosing body. Both are SEH handler entries. The runtime has no guest SEH
dispatch, so they are unreachable today. The third missing address, `0x001D5295`, is expected: the lifter now
extends `0x001D5270` over its inline jump tables.

## macOS re-check of the final pin (2026-10-24)

M1 Pro, macOS 26.5.2, branch `4fd3293` (main v0.11.0 merged), pin `3e63e7b` from a fresh
`git submodule update --init --recursive`. Python from the `djpy` alias plus `numpy`, `pillow` and
`cryptography` (`docs/build-macos-linux.md`). Raw logs are not committed.

| Check | Result |
|---|---|
| Analyze / recomp / build `posix-release` | Analyze **1 min 53 s** (`--no-archive`), recomp 1 min 2 s, build 33 s. 18,446 functions, as on Windows. `check-manual-addresses.py` 42 checked, 0 missing. Links SDL3 only |
| Full regression | **18 of 18** in 37 min (`logs/regress-20261024-175105.txt`): unit 631 passed / 33 skipped (482 / 57 on 10-22; main's mod tests), m2, m3, m4a, launcher, overlay, unlockopt, picture, fight (median 120, minimum 120), ffa, Terrordome 242 s (median 120, minimum 120; 107 on 10-22), combat 27/27, versus 26/26, **replay 80 records over 1,800 steps, golden hash unchanged**, intro, crib, gym, soak 3/3 |
| `--only repeat` (issue #98), twice | 2/2 ok: 80 records identical over 1,800 steps, matching the golden hash (`logs/regress-20261024-175753.txt`, `-180440.txt`). The Mac's first `repeat` run. Like Windows, it does not show the Deck's divergence |

The first full pass was 12/18, for two reasons in the shell, neither in the branch: `DEFJAM_DATA` was not set, so
`harness.py` used `../defjam`, which has no save (combat, versus, replay: no fixture; crib, gym: no Story
profile), and the documented `djpy` alias has no `cryptography`, which main's `test_mod_manager.py` imports
(unit: one collection error). With both set, 18/18 above.

**Audio at the device** (opening scene, `harness.py run intro --secs 330 --to-end`, `RECOMP_APU_PCM`,
`scripts/audio-clicks.py`). Same harness and route; the old build is the main checkout's v0.10.5 (toolkit
v0.13.0 + fork `e818066`, built 2026-10-20). Old and new alternated:

| Build | Clicks (on slice edges) | Dropouts |
|---|---|---|
| v0.10.5, toolkit `e818066` (2 runs) | 1 (0), 5 (3) | 1, 1 |
| pin `2b73d81` (runtime rebuilt, same lift) | **401 (399)** | 1,495 |
| pin `3e63e7b` (2 runs) | 7 (4), 9 (5) | 1,344, 1,307 |

- The Mac had the Windows click defect too: 401 per 330 s on `2b73d81`, nearly all on slice edges and in the
  first three minutes, as on Windows. The 10-22 Mac pass did not catch it: it counted mix-buffer holes. `3e63e7b`
  fixes it here as well; what is left is close to the old build.
- **One-slice zero gaps, found and fixed (`957c1ba`, D201).** The migrated build handed the device about 1,300
  gaps per 330 s that the old build does not (1): each one 32-sample slice of exact zero (0.67 ms; a few are 64),
  starting on a slice edge, a median 8,300 frames (0.17 s) apart through the whole scene, mostly in quiet passages
  (peak about 700 in the ms either side; the step into each gap is about 300, under the click threshold), on all
  eight slices of the period. They are already there on `2b73d81`, so they came with the migration, not with
  `3e63e7b`.

  Cause: DirectSound's sample-limit service routine (each SSL, 2048 samples, every 43 ms) stops the sequencer
  and starts it again, SECTL `0x07` then `0x0F` 150-400 us later. The frame thread reads SECTL once per frame;
  a frame that read it inside that gap took the lightweight branch (software mixer and test tone only), which
  leaves the VP's slice silent. A temporary trace put all 60 logged gaps on that branch, none in `se_frame`,
  and showed the SECTL pattern. The old toolkit has the same gate; its timing never caught the gap.

  Fix: `mcpx_apu_frame_kind` makes the frame decision. A sequencer that was rendering and reads off is held
  like a trapped front end (no frame; the title's next SECTL write ends the wait) for up to 5 ms; one that
  stays off longer gets lightweight frames as before, and is not held again until the VP has rendered.
  `apu_monitor` gains the case (ten simulated seconds of SSL stops: 0 frames played silent).

  | Build, opening scene 330 s | Clicks (on slice edges) | Dropouts |
  |---|---|---|
  | pin `3e63e7b` (2 runs, above) | 7 (4), 9 (5) | 1,344, 1,307 |
  | pin `957c1ba` (2 runs) | 1 (0), 3 (2) | **1, 1** |
  | v0.10.5 (2 runs, above) | 1 (0), 5 (3) | 1, 1 |

  With `957c1ba`: full regression 18/18 (`logs/regress-20261024-204851.txt`; replay hash unchanged,
  Terrordome 242 s at median 120, combat 27/27, versus 26/26, soak 3/3), `repeat` 2/2
  (`logs/regress-20261024-205538.txt`, `-210226.txt`); fixtures `apu_monitor`, `apu_irq`, `apu_lifecycle`,
  `apu_phys`, `apu_mixdown`, `apu_dsp_ack`, `apu_wav` pass on macOS. The fight in `combat` lasted 265 s (116 s
  earlier the same day, 193-194 s on 10-22); its length varies with how the fight goes, and all 27 assertions
  pass. The code is shared: Windows and the Deck have not run it yet.

Not run: the Setup from the newest green CI run and the Story intro with a pad (§7 step 5; needs a person).
