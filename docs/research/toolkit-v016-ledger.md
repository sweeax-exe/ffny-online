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
| Regression, per check (see below) | unit, m2, m3, m4a, launcher, overlay, fight, ffa, fight-terrordome, ffa-terrordome, combat 27/27, versus 26/26, two-matches 35/35, intro, crib, gym, soak 3/3: **ok**. `ffa-result`, `replay`: below. `unlockopt`, `picture`, `skipintro`, `repeat`: gamescope, below. `visual`: blocked (no baselines on this Deck) |
| Saves | crib and gym load the existing profile; `save/` has no `UDATA`/`TDATA`; the fresh Setup install creates `UserData`/`TitleData` |
| Missing functions | No `[ICALL] Failed to resolve` in 35 run logs |
| HRTF | `RECOMP_APU_HRTF` unset in the shell; no HRTF line in any log |
| Setup (CI run 40035905816, SHA-256 checked, provenance toolkit `2b73d81`) | Silent install into a new folder (`--no-shortcuts --toolchain container`): exit 0 in **7 min 56 s**. The installed game runs 3,177 frames with no crash. Not run: Game Mode launch, pad and rumble, launcher rebinding (need a person) |

**Not a regression of the migration (the baseline does the same on the Deck):**
- `unlockopt` / `picture`: the launcher click on a checkbox does not reach `settings.ini` under gamescope. `unlockopt` gives the same result on `77714da`.
- `skipintro` / `repeat`: gamescope itself aborts (SIGABRT, "X connection broken") on the second game launch, on both builds. The game then dies in `exit()` while `nv2a_ack_thread` still runs (a shutdown race that follows the X IO error). `repeat` is therefore not answered here, and neither is whether the Deck's replay record count is stable.
- `replay`: fails the Mac golden on both builds: migrated 87 records, `77714da` 84 (golden 80). Replay is not a Deck-valid golden; compare within the Deck only.

**Needs a decision:** `ffa-result` (4-fighter free for all). Migrated: 1 pass, 2 fail on `combat.eliminations` in 3 runs (fighter 1 put out, two fighters alive at the end, codes 32, 32, 33 instead of 32, 32, 9). `77714da`: 1 run, failed on `audio.dry_queue` only (issue #11). The failing assertion differs from the baseline's, but 1 run of the baseline says little about the rate. Not run: more baseline repeats.

**Audio (opening scene, 330 s, `scripts/audio-holes.py`):**

| Build | F8 | FA |
|---|---|---|
| migrated | 20 / 24 (0.06-0.07/s) | 0 / 0 |
| `77714da` | 43 / 57 (0.13-0.17/s) | 0 / 0 |

The Deck does not show the Windows crackle: the migrated build has fewer holes than the old one, and both are far below
Windows' bad runs (F8 187-421). Run with nothing else playing audio; I did not log the minutes of the holes.

**Two alias seeds have no effect.** `0x001F707B` and `0x001FF9B7` are in v0.10.7's dispatch table and
missing from this lift although `config/seed_functions.json` seeds them. Each sits right after the end of a
real function, in bytes only tail-jump alias bodies cover (`0x001F705D`, `0x001FF995`), and
`_pass_seed_aliases` needs a real enclosing body. Both are SEH handler entries. The runtime has no guest SEH
dispatch, so they are unreachable today. The third missing address, `0x001D5295`, is expected: the lifter now
extends `0x001D5270` over its inline jump tables.
