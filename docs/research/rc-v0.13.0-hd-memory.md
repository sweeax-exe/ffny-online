# v0.13.0 Windows HD memory investigation — 2026-10-31

Status: all four controlled pack comparisons and the extended menu-settling
test passed the existing gate. The original failing result is
retained; neither the 64 MiB limit nor the runtime has been changed.

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
