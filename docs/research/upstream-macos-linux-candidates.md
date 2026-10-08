# Upstream candidates from the macOS and Linux port

Written 2026-10-08, after the port branch was rebased onto the v0.13 base.
Sources: toolkit fork branch `defjam/macos-linux` (13 commits on
`defjam/upstream-v0.13` `c979c09`), the game branch `feat/macos-linux-port`,
and the measurements in `PROGRESS.md` (D80, D81) and `docs/worklog/2026-10.md`.

Nothing here has been submitted. Every upstream pull request needs Vlad's
separate approval, and no candidate has been compared against upstream `main`
as it stands today: each one has to be re-cut against it, one topic per PR,
before it is offered. Commit names are the fork branch's and will change if
that branch is rebased again.

## What in the toolkit is host-specific

The question behind this note was whether xboxrecomp is "all generic
recompiler". It is two things.

- **Generic.** The analysis tools and the lifter under `tools/` are Python and
  emit plain C. The generated header's x87 and flag helpers are portable: the
  same lifted code compiled for arm64 here reproduces the Windows fight-replay
  golden hash.
- **Host-specific.** The runtime under `src/` is written against Win32.
  Upstream already carries a POSIX stand-in for threads, events, heaps and
  memory mapping (`src/platform/win32_compat.c`) and POSIX halves of the path
  and file layers, so Linux and macOS are intended targets, but before this
  port the layer did not build as a whole off Windows and several parts had
  no POSIX form at all:

  | Area | On Windows | Off Windows before the port |
  |---|---|---|
  | Device-register traps | vectored exception handler, x86-64 decoder | none |
  | Renderer | Direct3D 11 | partial OpenGL fallback |
  | Audio output | XAudio2 (waveOut fallback) | inert stubs |
  | Pads | XInput, SDL3 since v0.13 | SDL2 path only |
  | One guest CPU | affinity mask | affinity on Linux, nothing on macOS |

The port filled those gaps. Almost none of it is specific to Def Jam.

## Candidates

In the order I would offer them.

| # | Candidate | Fork commit(s) | Size | Independent defect or new capability? | Risk to upstream |
|---|---|---|---|---|---|
| 1 | APU: wait out a stopped front end instead of playing silence | `ba27afa` (the `apu_core.c` half) | ~70 lines | Defect on every host | Low, but changes shared Windows audio |
| 2 | POSIX trap layer with an A64 decoder, 4 KB device pages in larger host pages | `b431430`, `26da697` | ~780 lines, new files | New capability | Low: new files, Windows untouched |
| 3 | Guest-CPU token for hosts without thread affinity | `dc00f70`, rename in `e5e59f6` | ~530 lines | New capability; covers a case upstream's lock documents as unsolved | Medium: overlaps their design |
| 4 | POSIX file layer: listings in name order, search dropped on close, partition images | `962eb36`, part of `7fc5f12` | ~200 lines | Defects off Windows | Low |
| 5 | Audio output through SDL off Windows; SDL3 input off Windows | `26edf72`, `10ab24c`, `dccbb31`, the `apu_xaudio2.c` half of `ba27afa` | ~400 lines | New capability | Low to medium |
| 6 | The runtime libraries build and link off Windows | `747ae66`, rest of `7fc5f12` | ~900 lines across 20 files | Prerequisite for 2-5 as a whole | Medium: wide, mechanical |
| 7 | Vulkan device for the push-buffer translator | `3e80eae`, `a87ab22` | ~2,700 lines | New capability | High: large, one title has exercised it |

### 1. APU front-end stops played as silence

`mcpx_apu_frame_thread` runs a period's eight 32-sample slices back to back
and then sleeps. When the front end traps or halts at slice k (a voice ended
and the title asked to be told), the remaining slices take the path that
renders no voices, so the rest of the period is silent.

- Measured: 232 runs of 32 to 224 zero samples cut into loud audio in a 298 s
  capture of the `fight` route; 0 after the change, with 632 stops bridged in
  413 ms altogether and no dropped or dry buffers (`run-20261007-150231`).
- The change waits up to 20 ms for the title to service the interrupt, lets
  `throttle()` make up to four periods of lateness good, and stops waiting for
  a front end that stays stopped until it has run again.
- It also compares the whole `FEMETHMODE` field; `TRAPPED` is 0xE0 and
  `HALTED` 0x80 of it, so testing either as a bit matches both.
- **Before offering:** it has only been measured on arm64 macOS. Run the same
  capture on Windows before and after (`--env RECOMP_APU_LEVEL=1 --env
  RECOMP_APU_PCM=<file>` on `fight`; the check is a run of zeros between loud
  samples). If Windows shows no holes before, say so in the PR. v0.2.3's D70
  left "the audio chip model's trap handling" as a to-do; read that item
  first, this may be the same thing.
- The pre-roll in the POSIX half of `apu_xaudio2.c` belongs with candidate 5.

### 2. POSIX trap layer

`src/platform/mmio_trap.c/.h` and `tests/mmio_trap_posix`. `mprotect` plus a
SIGSEGV/SIGBUS handler, an A64 load/store decoder, and an always-open second
view of the same memory so that a 4 KB device page can sit inside a 16 KB host
page without its neighbours faulting for ever. x86-64 hosts reuse
`mmio_decode.h` on a copy of the context.

- Self-contained and fixture-tested (arm64 natively, x86-64 under Rosetta).
- Without it no device model works on Apple Silicon or arm64 Linux.
- Left Windows-only on purpose and worth saying in the PR: the AC97 write trap
  and the `RECOMP_WATCH` page, which single-step with the trace flag.

### 3. Guest-CPU token

`win32_compat.c` (`guest_turn_*`), fixture `tests/guest_cpu_posix`. Decision
D81 has the design and why the first version (preempt anywhere) deadlocked.

- Upstream v0.13 has its own answer to the same rule: `guest_cpu_join` /
  `guest_cpu_part`, opt-in with `RECOMP_GUEST_LOCK=1`, released around kernel
  calls. Its comment says a thread that spins in guest code with no kernel
  call deadlocks there and names a yield point in backward branches as the
  upgrade. That spinning thread is exactly this title's stream reader.
- The token preempts by signal instead, but only while the program counter is
  in the lifted code. The toolkit half is generic; telling it where the lifted
  code is (`guest_turn_set_code`) is the game's job, and on macOS that takes a
  named section for the generated sources (`src/recomp/guest_section.h`).
- **This is a conversation before it is a PR.** Open an issue describing the
  spinning case and the two mechanisms, and ask which they want: the token as
  the POSIX implementation of their lock, or beside it. Do not send it cold.
- Known cost: a woken thread waits 1 to 3 ms as a rule, 9 at worst in a
  four-minute match, 23 seen once.

### 4. POSIX file layer

- Directory listings come back in name order (the host's order is arbitrary;
  a title that takes the first match picks a different file) and a search is
  dropped when its handle closes. One of the two cured "Unable to load" on a
  valid save; which was not isolated, and the PR should say so.
- Partition images (`\Device\Harddisk0\partition0` and friends) on POSIX,
  without which this title exits at boot.
- Upstream merged #171 (release Win32 directory searches on NtClose) since;
  check what is left to offer.

### 5. SDL audio and input off Windows

- Audio: a ring the device callback drains, so "the device ran dry" counts
  what is heard; pre-roll of two submissions; SDL3 through a stream; the dummy
  driver for headless runs through SDL3's hint (a late `setenv` is not seen,
  and unattended runs were playing through the speakers).
- Input: the host input layer off Windows, with pads arriving and leaving
  noted by an event watch because the window's thread reads the same queue.
- Depends on 6.

### 6. Building off Windows

Guards, missing shim functions and CMake branches across the kernel, D3D, APU,
USB and input libraries. Mechanical, wide, and the part most likely to have
drifted from upstream `main`; it should be rebuilt from a fresh checkout
rather than cherry-picked. Offer after 2, since reviewers will ask what it is
for.

### 7. Vulkan device

`src/d3d/d3d8_vk.c`: the Direct3D 8 style interface the push-buffer translator
draws through, with the existing HLSL generators compiled at run time by
shaderc. Matches the three golden frames and holds 60 fps in a fight on an
Apple GPU through MoltenVK.

- Not yet: no run on Linux with the game, no persistent shader cache, no
  vsync pacing, textures converted to RGBA8 on the CPU.
- Hold it until the Steam Deck run. Then ask upstream whether they want a
  second renderer to maintain at all before preparing anything.

## Not candidates

- The game repo's side of the port: `src/host_posix.c`, `src/host.h`, the
  `__TEXT,__guest` section, `scripts/pipeline.py`, the harness changes.
- The Windows-only stand-ins for the v0.4.0 launcher and overlay.
- `e5e59f6`'s rename, except as part of candidate 3.

## Before any of these is prepared

1. Fetch upstream `main` and its open pull requests; the fork base is v0.13.0
   plus Def Jam's own commits, and upstream has moved since (v0.13.1 at least).
2. For each candidate, show the defect or the missing capability on a pristine
   upstream checkout, without the fork's other commits.
3. One topic per PR, with its fixture, in the order above.
4. Vlad approves each one before it is opened.
