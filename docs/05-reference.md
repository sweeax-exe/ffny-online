# Reference: how it works, where things are, how to run it

Moved out of `PROGRESS.md` §7 on 2026-10-02 so that file stays short. This is reference, not status:
the current state and the next steps are in `PROGRESS.md`.

## How the rendering works, in one paragraph
The title's Direct3D 8 is statically linked into the XBE, so it was recompiled along with the game and never
calls the runtime's Direct3D. It builds NV2A command streams instead. `src/hooks/d3d11_translator.c` creates
a window and a 640x480 device through the runtime's own factory so there is something to draw into; patch
0003 makes the executor in `src/kernel/nv2a_pb_exec.c` forward every decoded method, fetch vertices from the
title's attribute arrays, resolve indexed elements and hand over textures; patch 0004 makes
`nv2a_pgraph_d3d11.c` accept prepared vertices and upload DXT textures. No Direct3D symbol had to be
identified anywhere, because the command stream is the interface.

## Object addresses and tools

**How to find the objects involved.** The title keeps its subsystems in a service locator: a plain array at
guest `0x002FB148` indexed by id, so `MEM32(0x002FB148 + id*4)` is the object. Addresses are stable across
runs:

| id | object | address | field of interest |
|---|---|---|---|
| 0x17 | top-level state machine | `0x800250C0` | `+0x18` current, `+0x1C` next, `+0x24` selector, `+0x28` the task manager |
| 0x18 | task manager | `0x801A81E0` | `+0x38C` pending, `+0x390` active |
| 0x27 | frame timer | `0x802469A0` | `+0x17CC` frames owed, `+0x1A80` period, `+0x1A98` frequency |

Ruled out along the way, each by measurement: input (`RECOMP_USB=1` presents a device and the routine
claims the interrupt), deadlock (the only sleeping call site in the title is the front end's idle loop),
missing files (one absent from the disc, expected to come from an archive), truncated jump tables (402
audited, zero lost arms), and saved-game storage (TDATA now routes somewhere writable).

Tools, all cheap:

| Tool | What it answers |
|---|---|
| `scripts/guest-stack.py` | A thread's guest call chain, filtered against the return addresses the lifter emitted. |
| `scripts/icall-window.py` | The last sixteen indirect-call targets, sampled out of the running process. |
| `scripts/peek-guest.py` | Guest memory, with `--diff` for what is moving. **Kill any stale `defjam_recomp.exe` first**: it attaches to the first process it finds, and a leftover one gave a wrong answer here for an hour. |
| `RECOMP_WATCH_WRITE=0x...` | A hardware watchpoint on one guest address, host RIP reported; `tools/symbolize.py` names the function. This is what found the PTIMER sequence. Add `RECOMP_WATCH_RW=1` to catch reads as well, which answers "does anything look at this". |
| `RECOMP_TRACE_ICALL=0x..,0x..` | Every call to the listed addresses, with the caller. Indirect calls only. |
| `RECOMP_NV2A_REG_TRACE=1` | Every access to the trapped NV2A pages. |
| `scripts/jump-table-audit.py` | Every indexed jump's arms, checked against the image. |
| `scripts/coverage-audit.py` | What in the image no lifted function covers, classified as padding, data or missed code. |
| `RECOMP_SELFTEST=1` | Checks the lifted CRT's four 64-bit divide helpers against answers computed in the host. |
| `scripts/frame-signature.py` | What a captured frame contains, without storing any of it. |

Build/lift times depend on toolkit and host. The canonical
[lifter override playbook](agent/lifter-overrides.md) explains current wrapper semantics:
since toolkit v0.16 direct callers reach manual wrappers too. Older notes about
indirect-only wrapping describe v0.13, not the current pin.

## Standing items
1. **Run `scripts/jump-table-audit.py` and `scripts/coverage-audit.py` after every re-lift.** The first
   checks every indexed jump's arms against the image; the second accounts for every byte of the
   code-bearing sections and classifies whatever no function covers. A regression in either is silent
   corruption that is close to undebuggable once it is in.
2. **`src/hooks/nv2a_regs.c` is a table.** A future spin on a register that reads back the wrong thing is
   added there, not worked around elsewhere; the sampler will name the function and the lifted body will
   name the register.
3. **Toolkit changes are fork commits.** `tools/xboxrecomp` is pinned to a published commit on
   `vyanhursky/xboxrecomp`, the branch configured in `.gitmodules`. Build that checkout directly. Make generic
   changes in an isolated toolkit worktree, validate and publish, then commit the published gitlink in
   the parent. The 108 numbered patches that source comments still cite are retired and no longer shipped. The ledger in
   `docs/research/toolkit-rebase-ledger.md` maps all 108 old patches. Each upstream PR needs Vlad's approval.
4. **Open structural work belongs in GitHub Issues/milestones**, not another checked-in backlog.
5. **Captures come from the back buffer** (`RECOMP_TRANS_SHOT`, D12, D15) and carry the gamma ramp;
   `scripts/window-shot.ps1` grabs the game's own window when what is presented is the question. Both are
   the game's artwork: under `logs/` only, never committed.
6. **The save area is `$DEFJAM_DATA/save`** (TDATA and UDATA). Every harness run/soak guards the
   complete tree, including caches and the absence of a save root, and fails on unequal restoration.
   `save-guard/` retains three successful payloads and every failed backup; status markers live outside
   payloads. A guard whose run was killed leaves a `state.json` in its stamp, and the next guard refuses to
   start until `harness.py save-recover` settles it (`--restore` or `--accept`). Rebase runs use the preserved disposable save copy. Legacy name-hash compatibility verifies
   exact UTF-16 metadata and prefers canonical folders; it never renames or merges player folders (D52).
7. **Historical bring-up qualifications** remain in the dated research/release evidence and
   PROGRESS decisions. Revalidate old crash/performance hypotheses before treating them as current bugs.

## Running it, and checking it

```powershell
.\scripts\build.ps1 -Preset win-x64-release     # kill defjam_recomp.exe first, or the link fails
python scripts\regress.py --quick                # unit tests, the three golden frames, one fight
python scripts\regress.py                        # everything: adds the Story routes and a boot soak
```

To play: `.\scripts\run.ps1 -Preset win-x64-release` with `DEFJAM_DATA` pointing at the data root.
The launcher supplies absent `RECOMP_VBLANK`, `RECOMP_PB_EXEC`, `RECOMP_PB_D3D11` and `RECOMP_USB`
settings as 1, preserves explicit overrides and restores its additions when the game exits. `RECOMP_VBLANK`
drives the GPU's interrupt (without it the title stalls early), `RECOMP_PB_D3D11` gates the whole
translator path (without it nothing is drawn), `RECOMP_USB` presents the controller. The harness sets all
four as well. Current executable startup supplies the normal runtime defaults; preserve explicit diagnostic overrides.
See the build guide and source rather than applying historical launch recipes blindly.

To drive it unattended, `scripts/harness.py` (its docstring has the stage syntax):

```powershell
python scripts\harness.py routes                          # boot, fight, crib, gym, intro
python scripts\harness.py run fight --png                 # a scripted One on One, with captures
python scripts\harness.py run intro --env RECOMP_PB_BATCH_DUMP=shot
python scripts\harness.py soak --boots 20                 # boots until one stops presenting, then dumps state
```

A healthy run: `[D3D] 2.0s` lines at 120 presents (60 while loading), no `[CRASH]`, no `[WATCHDOG]`, no
`[D3D11-DEBUG]`, no `indices truncated`. `scripts/regress.py` checks exactly these, per route.

The golden frames (`tests/golden/`) store measurements of a capture, not pixels, which is why they can
live in a repository that will be public. They were taken without the gamma ramp (`RECOMP_GAMMA=0`).


## GPU vertex-program path: original constraints

The historical M4e design (2026-09-29) addressed CPU execution of the title's NV2A vertex programs, one vertex at a time, after decode-once/vertex reuse still left Debug fights near 38–42 fps. That is a dated baseline, not current performance. The working GPU path forwards attributes and indexed batches through the translator/D3D8 pixel pipeline while retaining CPU fallback for diagnostics and rejected programs.

The important geometry constraint is clip-space reconstruction. The game's Direct3D epilogue leaves output position in screen pixels with reciprocal clip `w`; reconstruct `w=1/oPos.w`, `ndc.xy=(oPos.xy/ScreenSize)*(2,-2)+(-1,1)`, `z=oPos.z/zmax`, then output `float4(ndc*w,z*w,w)`. The old CPU screen-space path used `rhw=1`, causing affine texture/colour interpolation and screen-space near clipping; real clip `w` enables perspective interpolation and hardware clipping. Keep the D3D8 output layout/pixel pipeline consistent (saturated oD0, scaled linear-texture coordinates, further texture stages/fog as supported).

Decoder semantics must stay shared with the verified CPU interpreter: NV2A fields are in microcode dwords 1..3; R12 maps to oPos, O[3] to oD0 and O[9] to oT0; there are 192 constants and relative `a0` addressing. The older separate HLSL decoder used a different field layout and its FVF input layouts did not describe this title's raw attributes. Constants must reflect per-draw writes, especially bone matrices, with dirty ranges updated by `SET_TRANSFORM_CONSTANT`. Programs that write constants (`o_mask` with `orb` clear) require a safe fallback; point-sprite/oPts support was not observed by that design. `RECOMP_VSH_GPU=0`, batch dumps and `RECOMP_VSH_TRACE` retain the CPU diagnostic path. See [rebase acceptance](research/toolkit-rebase-acceptance.md) for measured parity and limits, rather than treating this original design as fresh certification.
