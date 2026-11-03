# Toolkit rebase acceptance, 2026-10-03

The 108 historical patches have been mapped to 15 logical commits on upstream
`1409a7d7801d3e931fb1074be6104209ddd9a33e`. Vlad explicitly approved publication;
the public fork branch is published at
[`aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc`](https://github.com/vyanhursky/xboxrecomp/tree/aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc).
The actual submodule is clean at that commit. Vlad accepted the Release play-test
on 2026-10-04: it plays perfectly to him, audio is much better than before,
frames are smooth and he observed no graphical glitches. Private PR
#1 (private development record) merged into `main` at
`c23b44a51c11333017b94f47c7276aba8bb11b33`. This closes the rebase acceptance gate.
Merged-main CI (private development record)
passes all three jobs: hygiene, scripts/unit tests and Windows save/runtime build.

## Source and workflow

The working D3D11/HLSL forwarding path remains the default. Upstream's vertex
program/constant storage now owns state shared by our adapter; its CPU/software
paths remain available. Kernel, audio and indexed four-pad USB behavior combine
the previous working contracts with upstream changes. The ledger names a
replacement commit and bounded evidence for every archived patch.

| Topic | Commit |
|---|---|
| Entry hooks before function coalescence | `193db87` |
| PUSHAD/POPAD saved ESP | `af45945` |
| Caller-cleanup indirect calls | `e53263f` |
| Canonical flags and dynamic validity | `0352cc7` |
| MMIO-safe REP MOVS | `032c9f2` |
| Flag consumers and partial writers | `8e5c73b` |
| Width-correct port I/O | `59f3d7a` |
| Proven switch-slot fallback | `7f7eef9` |
| Foreign switch arms and tail-site ownership | `c6140ae` |
| Kernel contracts | `799c19b` |
| Ordered memory/pusher/interrupt integration | `f555959` |
| APU integration | `710867c` |
| Indexed USB and anchored scripts | `faa28f8` |
| Canonical vertex storage and adapter | `700d2b6` |
| D3D11 forwarding | `aa1a1b9` |

The parent uses the published fork URL and exact gitlink. Patch replay scripts
and the CI replay step are retired; the 108 patches remain an annotated archive.
AGENTS.md and CLAUDE.md describe the same fork workflow. Windows CI adds the
native legacy-save fixture, and CTest failures propagate. A CTest invocation
without registered targets does not provide the dedicated fixture coverage below.

Analysis/lift/build record input and output hashes, reject stale or incomplete
stages and stage generated output before replacement. All harness routes and
soaks restore the complete disposable save/cache root and share a failure policy.

## Fresh lift and focused checks

| Check | Result | Local evidence under `logs/rebase-work/` |
|---|---|---|
| Analysis | 17,876 functions; unchanged seeds | `actual-checkout-analysis.txt` |
| Lift | 17,886 translated, 4 manual, 111 same unresolved stubs; 20 C files / 87 MB | `actual-checkout-lift.txt` |
| Local switch audit | 409 tables, zero missing arms | `actual-checkout-jump-audit.txt` |
| Coverage | 87.95%; +9 code bytes | `actual-checkout-coverage-audit.txt` |
| Definition/extent census | 16 additions explained; one explicit manual replacement; both entry hooks preserved | `actual-checkout-generated-audit.txt`, `generated-census.json` |
| Project tests, MSVC | 67 passed | `actual-checkout-project-tests.txt` |
| Toolkit CPU tests | 464 passed, 26 existing compiler/platform skips, 57 compiled subtests | `actual-checkout-cpu-tests.txt` |
| Kernel/audio fixtures | 9/9 | `runtime-fixtures-results.txt` |
| Four-pad USB/OHCI fixture | 1/1 | `usb-test-results.txt` |
| VSH WARP parity/state fixtures | 2/2, including 60 numeric cases | `vsh-test-results.txt` |
| Actual-checkout Debug and Release builds | Both certified; QPC/QPF type mismatch removed | `actual-checkout-debug-build.txt`, `actual-checkout-release-build.txt` |

Full analysis/lift reports and images contain game-derived content and remain
local ignored artifacts. The tracked reports describe counts, addresses and
findings without distributing game bytes.

## Fresh checkout CI

Private draft PR #1 (private development record) has a
successful CI run at parent `7068110` (private development record).
All three jobs passed: game-data hygiene, Ubuntu Python tooling (67 tests), and
Windows MSVC runtime build with the native save-compatibility fixture (19 tests).
The Windows checkout log confirms submodule `aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc`.
Root CTest registers no targets in this configuration; the separate native runtime
fixtures above were run locally. CI uses no game data and does not run the game.

## Game acceptance

All agent runs use the preserved disposable `runtime-data` save root. Analysis
reads the original hash-validated extracted dump, which is separate from saves.

| Run | Result | Evidence |
|---|---|---|
| Pre-rebase Release | 9/9, fight median/minimum 120/112 presents per two seconds | `logs/regress-20261003-123017.txt` |
| Candidate Release after fixes | 9/9 | `logs/regress-20261003-180239.txt` |
| Candidate Debug | 9/9 | `logs/regress-20261003-183307.txt` |
| Final actual-checkout Release | 9/9, goldens and Story routes pass, fight 120/112, 3/3 boots | `logs/regress-20261003-191107.txt` |
| Final actual-checkout Debug plus 20 boots | 9/9; 20/20 boots, zero hangs/failures; fight 120/66 | `logs/regress-20261003-202026.txt` |
| Extra Release intro/fight/crib/gym/unlock | 5/5, 13 captures; all save restores equal and fault lists empty | `logs/rebase-work/extra-acceptance-results.json` |

The initial candidate's 4/9 failure remains documented in
`logs/regress-20261003-165713.txt`. Two concrete fixes closed it: preserving
value-first/index-second fallback when upstream splits the 15 switch arms at
001D5270, and read-only compatibility for old save-directory hashes. The latter
keeps upstream's correct zero-count SHRD behavior; the game-specific helper
prefers canonical paths and only accepts the precise legacy spelling with
matching UTF-16 SaveMeta Name. It never renames or merges folders. Optimized
baseline/candidate hash fixtures and 19 native compatibility cases cover the cause
and replacement ABI, path encoding, collisions and bounds.

Final Release has the same measured fight cadence as the pre-rebase run. The
full regression takes about 28 minutes versus 25 minutes before the rebase;
route/startup times are modestly longer. Final Debug fight median/minimum is
120/66. The intervals below 100 are the first two after game start; after the
first 16 seconds its minimum is 107. Pre-rebase Debug logs have full-window
minima 52/64/54 and later minima 119/116/117. These observations retain every
interval and do not assert exact Debug timing parity. The historical M4e summary
minimum 108 used a different window. Detailed comparison:
`logs/rebase-work/debug-cadence-window-review.json`.

## Captures, audio and saves

All 13 capture pairs were inspected. Nine PNGs are byte-identical: intro-8s and
intro-16.5s, crib-9s, both gym captures and all four Unlock Fighters positions.
The latter preserve highlighted cells, the Method Man preview, prices and point
totals, supplying direct game evidence for the retained FRNDINT behavior.

The remaining two intro and two fight captures differ in animation pose, scene
phase or camera/gameplay state. **Inference from the samples:** these differences
are consistent with timing and gameplay variation; no obvious missing geometry,
texture or broken layout was found. At the 110-second fight capture both HUDs
read 1:53; at the 60-second capture baseline/candidate read 0:53/0:59. This is
bounded visual evidence, not frame-synchronized or game-clock equivalence.
Evidence: `logs/rebase-work/final-comparison-{intro,fight,crib,gym,unlock}.png` and
`final-artifact-review.json`. Images remain local.

The extra fight used LEVEL and PCM observation over its normal 120-second fight
window. Its PCM is 56,815,616 bytes / 14,203,904 complete stereo frames, or 295.915
generated seconds at 48 kHz across the whole process. No full-scale sample occurs
in either channel. Whole-run peak left/right is 24,001/23,811; RMS is
3,053.46/3,024.81 (-20.61/-20.70 dBFS). The last 60 generated seconds have RMS
2,606.34/2,718.24 (-21.99/-21.62 dBFS), with zero-sample proportions 1.657%/1.653%.

There are 296 level reports in the whole run: zero dropped buffers, ten dry-queue
events across reports 1, 2, 4 and 6, before game start. After game start, 121 reports
contain zero drops and zero dry-queue events; 120 have nonzero peaks and one is
silent. The startup events are retained for owner listening review. The baseline
ordinary log supplies initialization/handshake evidence but no equivalent PCM or
queue metrics; numerical baseline parity is not claimed. Pre-submit PCM can
include rejected submissions, and its tail is not sample-aligned to the log.
Vlad's 2026-10-04 play-test reports much better audio and accepts the experience.
This is owner listening evidence; no numerical baseline comparison was recorded.
Evidence: `run-20261003-202450.log.err`, `candidate-fight.pcm` and
`final-artifact-review.json`, all ignored local artifacts.

After all five routes, the original save rehash is identical to the preserved
baseline: 18 files / 5,243,503,676 bytes. Every automated run used the disposable
copy and restored its complete save/cache tree. Final evidence:
`logs/rebase-work/original-save-verified.json`. Both executable certificates still
verify after all runs, and the published submodule remains clean.

## Owner acceptance and remaining limits

Vlad's Release play-test is accepted and the private integration is merged.
Each proposed upstream PR requires separate approval. M6 PC features
follow the rebase, then M5 Proton and M9 macOS.

For the review, launch the certified Release with the preserved disposable
profiles from a PowerShell terminal in this repository:

As of 2026-10-04, `run.ps1` supplies the graphics, vblank and USB settings that
the harness has always supplied. The original review command omitted these
settings and could start a windowless process in a fresh shell. The corrected
launcher was verified with Windows PowerShell 5.1: a real game window opened,
D3D11 initialized and frames rendered (`logs/run-20261004-094353.log.err`).

```powershell
$previousDefJamData = $env:DEFJAM_DATA
$snapshot = (Get-Content logs/rebase-work/baseline-path.txt -Raw).Trim()
$env:DEFJAM_DATA = Join-Path $snapshot 'runtime-data'
try { .\scripts\run.ps1 -Preset win-x64-release }
finally { $env:DEFJAM_DATA = $previousDefJamData }
```

Check the existing profiles, Story/cutscenes/creator, a fight, menus and Unlock
Fighters selection. Listen for music, speech, effects, pitch, balance and dropouts.
Record any issue with the resulting `logs/run-*.log.err` filename. The original
save root stays separate from this review copy.

The tests do not establish optional CPU vertex fallback performance, arbitrary
hardware-contract equivalence, two-pad gameplay or perceptual audio fidelity.
The ledger keeps the CPU decoded-op cache change explicitly partial (0073).
Existing rare silent boot/crash, long-session memory and visual backlog items
remain outside this rebase. Rollback preserves `ours-on-pin` = `dc32f30` and the
pre-rebase parent/source/build/save/capture snapshot.


## Historical M4e acceptance scope

M4e's 2026-09-29 design separated GPU vertex execution from the preceding audio/crowd/R3 work. Its original requested gates were Debug fight presentation at 60 fps with the design-rate clock (1.2 ticks per step), three fights without a new crash, unchanged M2/M3/M4a goldens, bounded visual perspective checks, and retained CPU diagnostics/fallback. That proposal alone did not establish those outcomes. This rebase's measured WARP parity/state fixtures, unchanged goldens and game runs above are the retained migration evidence; its Debug cadence comparison explicitly uses different windows from the older M4e minimum 108. CPU fallback performance, constant-writing programs, unobserved point sprites and arbitrary hardware-contract equivalence were not newly certified by the rebase.
