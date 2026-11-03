# v0.13.0 Windows HD memory investigation — 2026-10-31

Status: the initial two-mod controls below passed, but the later full-Lanczos
regression and its targeted confirmation **fail the unchanged 64 MiB gate**.
The post-playtest section records both failures and the longer menu observations.
No runtime or limit change has been made; the Windows candidate is not all-green.

## What the gate measured

Candidate native sources are unchanged from tested head `7be9133`, toolkit
`d569ca1`. Windows 11, RTX 4070 Ti SUPER; 16:9, 2048 MiB texture-pack cache,
HD Widescreen Menus 1.4.0 and HD Fighters 1.0.0. The save fixture is held constant
and every game run is serial and save-guarded.

`scripts/scenario_suite.py` calls `memory_checks` starting 20 seconds after
`Game.StartGame`. `scripts/test_evidence.py:memory_checks` takes the maximum of
the first three samples as baseline and compares the **last sample of the whole
run**, including result/menu transitions, against it. Samples are roughly six
seconds apart. The 64 MiB threshold is a growth guard, not an absolute memory or
texture-cache budget. The full report is not an all-green result.

| Both-pack run | Baseline MiB | Last fight MiB | Final MiB | Existing gate growth MiB | Fight trend MiB/min |
|---|---:|---:|---:|---:|---:|
| Full regression | 2398.02 | 2383.03 | 2663.71 | +265.70 (FAIL) | -0.423 |
| Exact targeted rerun | 2396.08 | 2380.29 | 2404.15 | +8.07 (PASS) | +4.490 |

Original: `logs/scenarios/regress-combat-20261031-112549/report.json`,
full table `logs/regress-20261031-115227.txt`. Targeted rerun:
`logs/scenarios/regress-combat-20261031-141119/report.json`, table
`logs/regress-20261031-141559.txt` (27/27 assertions). Both retain all gameplay
assertions and do not update any golden. The random seed was not controlled in
these first two existing regression runs.

In the first run the fight starts at 80.25 s, the result request is observed at
278.98 s and the menu request at 282.99 s. The last sample at 285.0 s is only
about two seconds into that menu load. Private memory remains around
2383–2405 MiB during the measured fight and then increases by 280.69 MiB across
the result/menu transition. The repeat has a similar steady fight footprint but
only 23.86 MiB transition growth. This establishes timing-sensitive endpoint
growth, not an accumulating combat leak or a specific allocating subsystem.

## Cache accounting is narrower than process memory

Read-only source audit, verified against the pinned toolkit:

- `src/nv2a/texture_pack.c` budgets resident replacement textures. PNG accounting
  estimates eight bytes per mip texel (GPU plus system copies); DDS accounting
  uses twice the compressed payload. Entries are released by LRU eviction unless
  currently bound.
- WIC decode buffers, padded rows and prepared mip chains are outside that
  resident counter. Up to 16 asynchronous jobs and three workers may hold work;
  unconsumed completed images are reaped after four seconds, and committed
  prepared images are freed after upload.
- The D3D11 title-texture cache is separate (up to 256 entries, with its own byte
  counter and eviction). Driver and allocator retention are not measured by the
  pack-resident counter.

Thus a stable pack-resident total does not imply stable process private bytes.
The original log has about 2034 MiB pack residency during play and about 2020 MiB
after the result/menu load, despite the higher process sample. Decode/staging,
driver work and allocator retention are possible explanations, not measured
attribution. `RECOMP_TEXTURE_SYNC=1` is available for a diagnostic comparison.

## Controlled experiment

Local runner: `logs/hd-memory-audit.py`; plans and reports under
`logs/hd-memory-audit/packs-20261031`. Same candidate executable, same hashed save
fixture, same 16:9/render settings, same 2048 MiB budget, seed `20261031`, original
combat route and six-second result tail. Explicit pack selections are recorded
in each report. Comparisons: no packs, Menus only, Fighters only, both.

The runner separately calculates steady-fight growth/trend and the transition
increase. These are diagnostic measurements; it retains the original gate and
all scenario assertions without rewriting failure results.

All runs request the same roster (player fighter 56, CPU fighter 17), venue and
fixture hash `e6635fa30e3b67d650851aea384e58456756df10475f95e2a3930a51a76707b2`.
This is one created/fixed-fighter matchup, not a worst-case sweep of the entire
Fighters pack. A fixed seed does not make the host-timed input route deterministic:
the measured match durations differ, so phase-aligned endpoints are compared,
not equal wall-clock timestamps. These are attribution controls, not statistical
latency benchmarks or proof of no slow leak over an entire campaign.

Reusable runner: `scripts/memory-pack-audit.py`, with explicit `--fixture`,
`--texture-root`, `--output` and optional `--preset`. It verifies both installed
packs first, records their archive identities and preserves all scenario gate
failures. `--result-tail 60 --hold-menu` adds a stable returned-menu observation.
Windows uses private bytes; macOS/Linux use RSS through the existing harness.
Absolute values between those metrics must not be compared directly.

| Pack selection | Baseline MiB | Last fight MiB | Final MiB | Existing gate growth MiB | Result |
|---|---:|---:|---:|---:|---|
| None | 303.70 | 303.37 | 305.12 | +1.42 | PASS, 27/27 |
| Menus 1.4.0 | 2390.04 | 2374.50 | 2444.82 | +54.79 | PASS, 27/27 |
| Fighters 1.0.0 | 394.19 | 393.23 | 398.38 | +4.19 | PASS, 27/27 |
| Both | 2390.05 | 2389.39 | 2435.44 | +45.39 | PASS, 27/27 |

These four reports use the same executable SHA-256
`8649e815237232b23296579e8521e0b715e6f83715f8e399d52ae92e3d96be11`.
The runtime records 1744 entries with Menus, 218 with Fighters and 1962 with both;
the no-pack run has no active pack. All fixture/restoration, audio, combat and
pacing assertions passed alongside the unchanged memory gate.

With this 2048 MiB test budget, the menu pack dominates steady private memory in
this matchup: roughly 2390 MiB (2.33 GiB) versus 304 MiB without packs. Fighters alone add
about 90.5 MiB to the baseline. Adding Fighters to Menus does not add those bytes
linearly because both share the same bounded replacement cache and evict entries.
Those are measured within-host footprints, not a guaranteed budget for other
rosters, renderers or operating systems.

## Extended observation and conclusion

`logs/hd-memory-audit/both-menu-settle-20261031/both/report.json` uses both packs,
the same executable/fixture/seed and a 60-second result tail. The script stops
input at the second `battle/cmtype` request. The log confirms that anchor and
exactly one match start, so the menu is held rather than launching another match.
All 27 scenario assertions pass.

- Fight baseline: **2398.07 MiB**; last fight sample: **2397.14 MiB**.
- Highest sampled transition memory: **2423.41 MiB**.
- Final memory: **2379.55 MiB**, **18.52 MiB below** the fight baseline.
- Eight settled-menu samples, starting ten seconds after menu return:
  **0.00 MiB growth** and a fitted trend of **-0.057 MiB/min**.

The HD packs materially increase memory footprint, mainly Menus in this matchup.
Their scene-dependent allocations also make a short cross-scene endpoint test
more variable: a menu-only transition adds 70.33 MiB over its last fight sample,
whereas the no-pack transition adds 1.75 MiB. The original 265.7 MiB failure
occurred after the match, with a flat fight; an exact rerun, all four controlled
pack selections and the extended observation pass without changing the limit.

The evidence is consistent with transient HD load/cache allocation at the sampled
endpoint, not sustained growth in the tested fight or settled menu. It does **not**
identify the exact allocating subsystem, reproduce the original 265.7 MiB spike
under a debugger, or prove the absence of a slower leak across a whole campaign.
Individual allocation attribution would need a heap/driver trace.

**Decision:** retain the **64 MiB** gate and preserve the original failure next to
the passing confirmations. No runtime/cache-budget fix is justified by these
measurements. A future refinement should compare like-for-like settled scenes
and report transition peaks separately, rather than increasing the limit to
cover arbitrary loading spikes. The reusable audit runner already provides those
separate measurements and keeps every original assertion.

Validation for the new diagnostic helper and existing evidence/profile logic:
**73 focused tests pass**, including synthetic transition-spike and settling
cases. Game runtime, toolkit pin and goldens are unchanged.

## Post-playtest full Lanczos configuration

The owner approved the Windows playtest with full Lanczos4x, Menus1.4.0 and
Fighters1.0.0. The requested automated retest then ran on source/test head
`5771350dadb027eb04b4db9f030370cfc23f6693`, toolkit `d569ca1`, with the same
native executable SHA-256 `8649e815237232b23296579e8521e0b715e6f83715f8e399d52ae92e3d96be11`.
The host remains Windows11/RTX4070TiSUPER/driver32.0.16.1088. All gameplay lanes
use 16:9, render scale2 and a 2048 MiB replacement-texture budget. This budget
is not a total-process memory cap.

Lanczos recipe `81ddfce230cae3c81100c324d2b0f552bd6668a8b534e84c57677b717c48a0e3`
contains 18,971 generated images and 13 runtime aliases. Its opacity/channels
folders load before Menus and Fighters. Runtime logs verify **20,946** entries
with all packs, **18,984** with Lanczos alone, and **1,962** with the two managed
mods. The signed mod receipts and all generated image hashes were checked.

### Full suite and targeted confirmation

`logs/regress-20261031-160305.txt` finished in46 minutes: **16/17 passed**, with
four Windows-inapplicable SDL UI checks skipped. Units745pass/28skip/1slow
deselected; source-image goldens/replay pass unchanged. All gameplay assertions,
audio and frame checks pass. The sole failure is combat memory growth:

| Observation | Baseline MiB | Last fight MiB | Final MiB | Gate growth MiB | Result |
|---|---:|---:|---:|---:|---|
| Full-suite combat | 2475.13 | 2426.72 | 2571.80 | +96.68 | FAIL |
| Targeted combat, same fixture | 2480.36 | 2465.09 | 2855.93 | +375.58 | FAIL |

Reports: `logs/scenarios/regress-combat-20261031-153606/report.json` and
`logs/scenarios/regress-combat-20261031-161905/report.json`; targeted driver
summary `logs/regress-20261031-162353.txt`. Both have27 assertions,26 pass,
all7 combat assertions pass, and save restoration passes. The fixture manifest is
`fc722fe2d427e54dbf901a1979237873402455367dae9016a3951fc464fe80a3`.
RNG is uncontrolled in these normal regression routes; this is a confirmation
of the same gate/configuration/fixture, not a deterministic replay.

Both failed endpoints occur about2 seconds after returning to the match-selection
menu. Their final fight samples are below the initial fight baseline. Triage
finds no crash/watchdog/unresolved indirect call; continued render activity is
visible in the bounded log tails. The original failures remain recorded.

French menu text (MENU PRINCIPAL, CHOISIR, RETOUR) and English sc05 subtitle lines
at16s/24s were visually reviewed and pass. These still captures do not establish
audio/video synchronization in SC04. Both controller-tracing trials pass all40
presses through all9 required stages, no faults, and guarded save restoration.
The largest measured step gap is22.46 ms with refresh off and68.17 ms with refresh
on, below100 ms. Refresh-on conversions/applications pass the runner checks.
Evidence: `logs/input-audit/regress-20261031-155736-late{0,1}*`.

### Serial pack comparison and longer returned menu

The comparison uses the original fixed fixture manifest
`e6635fa30e3b67d650851aea384e58456756df10475f95e2a3930a51a76707b2`, seed20261031,
the same executable/roster/venue and a60 second held menu. Inputs use host timing;
match durations vary. It is not a statistical benchmark or whole-roster limit.

| Pack selection | Fight baseline MiB | Final MiB | Gate growth MiB | Settled-menu growth MiB | Assertions |
|---|---:|---:|---:|---:|---|
| Menus + Fighters | 2387.07 | 2356.18 | -30.89 | -18.26 | 27/27 PASS |
| Lanczos alone | 2291.71 | 2413.98 | +122.27 | -16.62 | 26/27; memory FAIL |
| Lanczos + Menus + Fighters | 2472.30 | 2434.00 | -38.30 | -16.12 | 27/27 PASS |

All three restore saves byte-for-byte. Each reaches exactly one match and holds
the returned menu; each has8 settled samples. Reports and summary are under
`logs/lanczos-memory-postplaytest-20261031/`. Lanczos alone remains above the
fight baseline after60 seconds even though the observed settled-menu trend is
downward. The two managed mods and the all-pack cases pass the existing gate.

One final all-pack observation uses the **post-playtest fixture from the failed
regression**, seed20261031 and a60 second held menu. It passes27/27: baseline
2475.04MiB, final2453.29MiB, growth**-21.74MiB**, highest sample2529.71MiB.
Eight settled samples alternate between2437.26 and2453.29MiB; final growth
against the maximum of their first3samples is**0.00MiB**. The fitted slope is
+13.332 MiB/min because of that oscillation; it is not a monotonically rising
sequence. Report: `logs/lanczos-memory-currentfixture-20261031/all/report.json`.
This run did not reproduce the375.58 MiB spike and cannot prove how long that
particular earlier allocation would have survived.

### Interpretation and remaining gate

Lanczos materially increases footprint. In this fixed-fixture comparison it
alone starts around2292MiB, while all packs start around2472MiB; cache reuse
and overlapping replacements mean pack footprints do not add linearly. Scene
transitions affect the short endpoint gate, and a longer all-pack observation
can pass after the menu settles. The measurements do not identify an allocator
or prove the absence of a slower campaign leak.

**The Windows memory gate remains failed and reproduced.** Longer passing runs
do not waive it, and the Lanczos-only control also fails. Keep the64 MiB limit,
runtime and goldens unchanged. Investigate allocation lifetime or review a
same-scene settled-memory test separately before calling this candidate
all-green. macOS/Linux full regression and setup install/Clean Update are still
separate pending gates; the owner's playtest is approved. CI is green on the
tested5771350head. No merge, tag or release was performed.
