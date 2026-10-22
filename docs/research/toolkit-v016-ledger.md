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
