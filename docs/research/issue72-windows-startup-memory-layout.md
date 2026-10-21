# Issue #72: Windows startup address-space collision

Investigation on 2026-10-20 of [issue #72](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/72),
including the reporter's follow-up and attached
[`run-20261020-193903.error.log`](https://github.com/user-attachments/files/33730789/run-20261020-193903.error.log).
The original attachment is retained locally under `logs/issue72/`; no game bytes
or personal paths are copied into this report.

## Root cause

The Windows memory-layout allocator accepts a location for the 64 MB Xbox RAM
view without checking whether the rest of the guest address space fits at the
same translation offset. The later fixed-address physical-memory and device
allocations fail, but initialization reports success. Startup then reads an
unmapped AC97 register page to initialize its register shadow and crashes in
the host C runtime's copy routine, before guest execution or any rendered frame.

The report is v0.10.5, Windows 11, Ryzen 7800X3D and RTX 5080. Event Viewer gives
an access violation (`0xC0000005`) in `VCRUNTIME140.dll`, offset `0x1C177`.
The game log establishes the earlier cause:

| Attachment lines | Evidence | Meaning |
|---|---|---|
| 6–7 | Launcher opens, Play closes it | Setup built an executable and the launcher runs |
| 12 | RAM mapped at `0xBFFF0000`, offset `3221159936` | Only the base RAM placement was accepted |
| 32–36 | Contiguous, NV2A, MCPX, flash and kernel allocations fail | Multiple required translated windows are absent; Windows error 487 |
| 38–40 | 28/28 RAM mirrors map, tiled mapping fails, init says complete | RAM/mirror success does not establish a usable device layout |
| 48 | OHCI cannot trap `0xFED00000` | Another consequence of the missing MCPX window |
| 49–53 | Read fault at host `0x1BEBF0000`, guest `0xFEC00000` | Exactly the AC97 page copied during device setup |

The address calculation matches without truncation:

```text
guest AC97 0xFEC00000 + memory offset 0xBFFF0000 = host 0x1BEBF0000
```

The physical window would occupy host `0x13FFF0000..0x143FF0000`, which
includes the usual Windows x64 executable image base `0x140000000`. Values
`0x14003F9DF` and `0x141B7FC58` in the native-stack block are consistent with
that overlap, but that block is a raw scan filtered to `0x140000000..0x150000000`,
not an unwind or a host mapping inventory (`src/main.c`, crash handler).
The log does not establish which allocation occupies any rejected range.

`src/hooks/ac97_bm.c::ac97_bm_init()` unconditionally copies 4096 bytes from
`AC97_BASE + mem_offset` before protecting that page. `src/main.c` reaches this
after `xbox_MemoryLayoutInit()` returns true. Its registers are still at their
startup values in the attachment. The `_NLG_Return2+0x547` label is the nearest
available C-runtime symbol; it does not establish an exception-unwinding bug.

Triage commands used:

```sh
python3 scripts/triage.py logs/issue72/run-20261020-193903.error.log
python3 scripts/triage.py logs/issue72/run-20261020-193903.error.log --lines 1:80
```

## When it appeared

This is an older, machine-dependent startup bug. It was reported on v0.10.5,
but the same crash path was already shipped in **v0.1.0 on 2026-10-04**, the
first public release. The reporter also says older builds fail on their PC.
History does not establish when that PC first encountered the bad placement.

| History evidence | What it establishes |
|---|---|
| Toolkit `7a71ed2`, 2026-03-10 | The initial memory-layout file already selects RAM independently and translates guest addresses with an offset |
| Toolkit `75119d5`, 2026-09-16, “Add test cases” | Fixes iteration of the base-hint array so its NULL/OS-chosen fallback is actually tried; an arbitrary base such as `0xBFFF0000` becomes reachable when fixed hints fail |
| Toolkit `9547d39`, 2026-09-27, “Stop leaking the RAM mirror span reservation on Windows” | Removes an ineffective POSIX-style partial-reservation attempt from Windows; its message says Windows already fell through to the fixed-base loop. It does not introduce the fallback |
| Public tag `v0.1.0`, toolkit pin `aa1a1b91` | Already includes the fallback changes, required apertures that only warn on failure, the unguarded AC97 copy, and the call to it after layout initialization |
| Public tag `v0.4.0`, 2026-10-07 | Adds the pre-game launcher before guest-memory initialization, which can change host address-space occupancy; an exposure hypothesis, not a proven trigger on this PC |
| `v0.10.4` → `v0.10.5` | No change to the toolkit pin, `src/main.c` or `src/hooks/ac97_bm.c`; the startup memory-placement code was not changed by this release |

Primary source links: [toolkit fallback change](https://github.com/vyanhursky/xboxrecomp/commit/75119d5),
[Windows reservation cleanup](https://github.com/vyanhursky/xboxrecomp/commit/9547d39),
[AC97 initialization in v0.1.0](https://github.com/vyanhursky/DefJamFFNY-recomp/blob/v0.1.0/src/hooks/ac97_bm.c#L134-L151).

In plain language: the port found a place for Xbox RAM but did not check that
the other memory areas needed by the emulated devices could fit around it.
Windows refused those other allocations, and the port carried on into audio
initialization anyway. Reading that missing memory caused the C-runtime crash.
The chosen addresses can differ between PCs and runs, so the age of the code
does not tell us the date it first failed on a particular computer.

## Fix

Toolkit branch `defjam/issue72-memory-layout`, isolated worktree
`logs/issue72/toolkit`, published commit `e81806613baf5a8c20e9b2a09e61cb103f7160fd`,
based on the released pin `32aab4d`:

- Select room for the full guest address span, at least 4 GB of virtual address
  space, rather than just the 64 MB RAM view. This is an address-space probe,
  with no physical-memory commitment for the full span.
- Release that probe in full, then place RAM, physical/tiled views, device
  apertures and RAM mirrors as one transaction, before copying the XBE or
  starting register-polling threads. Ordinary fixed-address Win32 calls refuse
  collisions and never overwrite a foreign allocation. If placement races with
  another allocation, release every owned mapping and retry a newly selected
  span, at most eight times. No partial Win32 reservation releases, placeholder
  APIs or newer Windows SDK requirement.
- Return a clear initialization failure if no complete layout can be placed.
  Device initialization cannot then reach the invalid AC97 copy.
- Keep hardware windows ahead of overlapping generic RAM mirrors for larger
  RAM/map configurations.
- Start the NV2A polling worker after layout completion. On teardown, stop the
  workers before releasing windows, do not independently free the kernel-page
  pointer inside physical memory, unmap file views with `UnmapViewOfFile`, and
  close both backing handles. These ownership rules also make retries/re-init
  reliable.

The ordinary macOS/Linux placement path is retained. The shared change moves
the NV2A worker start to the end of layout initialization and corrects kernel
page/backing-handle ownership on shutdown.

## Validation and publication

The game-free `tests/windows_guest_layout` fixture exercises the actual Windows
placement helper through real memory mappings: native Win32 on Windows, the
runtime's Win32 compatibility primitives on macOS/Linux. On the M1 Pro it passes
a real MCPX collision while RAM is free, each of 34 placement failures, retries
and exhaustion, span-reservation failure, alias contents, AC97 accessibility,
teardown/re-init, and a 128 MB configuration with hardware-overlapping mirrors.

The Mac game was rebuilt using the isolated toolkit worktree and its existing,
verified analysis/lift inputs. Final results on the M1 Pro:

| Check | Result |
|---|---|
| Windows allocation algorithm, through the POSIX primitives | 1/1 fixture passes; collision, 34 failure points, all 28 retail mirrors, retries/exhaustion, aliases, re-init and 128 MB layout |
| Existing native POSIX memory-layout fixture | 1/1 passes, including teardown and second initialization |
| Project unit tests | 478 passed, 44 skipped, 1 deselected (`not slow`) |
| Loading/title/menu goldens, launcher/overlay, unlock and picture settings | All seven pass |
| One-on-one, FFA, Terrordome FFA | All three pass; median 120 presents per 2 s; minima 120, 119 and 118 respectively |
| Combat to result / two-controller versus | 27/27 and 26/26 assertions, including audio health and save/fixture integrity |
| Deterministic replay | All 80 state records over 1800 steps match the golden hash |
| Story intro, crib, Learn Moves | All three pass, including cutscenes and preview-movie request |
| Boot soak | 3/3 reach the main menu, zero hangs/failures |
| Build-state verification | Passes against the isolated toolkit; no game left running |

All 17 game checks in the full suite were covered across two complementary
runs. The first invocation used plain Python without pytest and the harness's
empty fallback data root: ten game checks passed, but combat/versus/replay had
no profile fixture. It was stopped through the save guard, whose restoration
of the original tree/absence was verified. The seven remaining checks then
passed with the established data root, verified `mac-profiles` fixture and the
documented `uv` Python environment. Unit tests were also rerun successfully
with that environment. This was not a single clean 18/18 command invocation.

Local evidence: `logs/issue72/full-regression.txt`,
`logs/issue72/remaining-regression.txt`, `logs/regress-20261020-180044.txt`,
`logs/issue72/unit-tests.txt`, `logs/issue72/windows-layout-fixture.txt`,
`logs/issue72/memory-posix-fixture.txt`; scenario reports
`logs/scenarios/regress-combat-20261020-174242`,
`regress-versus-20261020-174615`, `regress-replay-20261020-174948`.

## Windows candidate published

Vlad restored GitHub authentication and requested publication of the test installer
and a reply on #72 (D144). Toolkit `e818066` is published on
`defjam/issue72-memory-layout`; parent source/pin commit is
`9e4f805877d36e8fad34a3fae8d7d0c49ba998d2` on `fix/issue72-startup`.
The candidate retains the v0.10.5 baseline plus this memory fix; it does not
include unrelated launcher-beta changes or replace the stable release.

[Windows candidate run 39519139830](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/39519139830) passed:

- Native MSVC allocator fixture: 1/1; real collision, all 34 rollback points,
  retries/exhaustion, reservation failure, aliases, re-init and 128 MB layout.
- Existing native memory regressions: 4/4; runtime (including
  `xbox_memory_layout.c`) and game-side source compilation succeeded.
- Installer tests: 177 passed / 9 skipped; production payload inventory passed.
- Packaged UI, preparation/responsiveness and silent invalid-input smoke tests passed.

[Download the Windows test installer ZIP](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/39519139830/artifacts/12310426740)
(GitHub sign-in required; expires **2026-11-20 00:11 UTC**). It contains exactly
the game-free `DefJamSetup-0.10.5-windows-x64.exe`, SHA-256 and provenance.
Downloaded installer: **54,650,880 bytes**, SHA-256
`87d84cd4e373206987c96498d1295196f6bf16d7c69ccd75e326fe07a6f590ed`.
Independent local verification matched the checksum and provenance, checked
`development=false`, source/pin above, and inclusion of `windows_guest_layout.h`.
No game executable or translated code is distributed.

[Issue reply posted and read back](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/72#issuecomment-6304712397) explains the cause and history,
links the artifact, and asks @klwn911 to use the bundled build with **Clean
Rebuild**, then test the main menu and a fight. The installed game itself has
not been run on Windows by this agent; **reporter acceptance remains pending**.
The version label remains 0.10.5; the artifact name and provenance distinguish
this candidate from the regular release.

Local evidence: `logs/issue72/windows-job-current.log`,
`logs/issue72/windows-test-reports/`, `logs/issue72/windows-candidate/`,
and sent body `logs/issue72/issue-comment-published.md` (all ignored).
A later integration target may include the separately published texture-list
fix `72bec86`; apply this memory topic on that fork tip to preserve the other fix.

## Pull requests and release hold

At Vlad's request, submitted [parent PR #76](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/76)
and [fork toolkit PR #3](https://github.com/vyanhursky/xboxrecomp/pull/3).
Both are open and mergeable. Parent #76 starts from current main `7bb4bdf`, retains
its launcher/beta work, pins the same tested `e818066`, and registers the new
fixture in Windows and POSIX CI. Its diff contains only CI, the pin and issue72
research/state. Parent integration CI is running; the existing reporter artifact
and its v0.10.5 baseline remain unchanged.

**Leave both PRs unmerged.** Vlad will request the release after the reporter
confirms the Windows fix. No auto-merge, tag or release was enabled. Fork PR #3
targets the project's `defjam/audio-fix`, not the upstream repository. Separate
texture-list PR #73 must be preserved when the two toolkit topics are integrated.

## 2026-10-21 reporter follow-up: setup version mismatch

[Reporter reply](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/72#issuecomment-6311677698)
contains setup logs, not a new game-crash log. The screenshot and current five-line
log show the correct candidate (`source=9e4f805`, `toolkit=e818066`) selected
**Bundled release (offline)** and **Clean Rebuild (same version)**, then stopped:
`Repair and Clean Rebuild require the installed version`.

The 1,309-line log is the earlier successful v0.10.1 installation (`0985f40`,
toolkit5445b73) in the default AppData app/data locations; it ends with setup
complete. The original v0.10.5 game crash came from a different C:\Games installation.
The current installed receipt was not attached, but the refusal establishes that
its version differs from the candidate; the supplied earlier log identifies
v0.10.1 at the selected location. This is consistent with selecting the older
installation rather than the original v0.10.5 one.

`setup/releases.py::policy` intentionally refuses same-version rebuild across
versions, before source preparation/build/activation. For the selected v0.10.1
location, choose **Clean Update** to install the bundled patched v0.10.5. Keep
its app/data pair; leave the game-copy field blank to reuse verified extraction.
Alternatively choose the original v0.10.5 app/data pair and use Clean Rebuild.
Play is initially disabled and is enabled after successful setup; Install/Repair
starts the selected operation. This attempt never exercised the patched runtime,
so it neither confirms nor disproves the memory fix. No further runtime fix or
new Windows artifact is justified by these logs.

[Clarifying test instructions posted](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/72#issuecomment-6316443011).
Existing setup policy/recovery tests pass **72/1 skip**; direct checks reproduce
the exact rebuild refusal and confirm update/auto accept 0.10.1 → 0.10.5, and
rebuild accepts 0.10.5 → 0.10.5. No installer policy was relaxed or changed.

Separately, PR #76 integration CI passed all Windows/macOS jobs but failed Ubuntu
linking the newly standalone fixture: undefined `feclearexcept` in win32_compat.
Published test-only toolkit **8589f8834e3c370e2def3c6757b0d4f6e3e89d70** adds Linux
`libm` linkage to that fixture, without runtime code changes. Parent PR pin now
includes it; integration CI must rerun. Local fixture passes on Mac (the first
sandboxed run could not create shared memory; permitted rerun passed). No full
game regression is repeated for a test-build-only change.

Evidence (ignored): `logs/issue72/followup-20261021/candidate-rebuild.log` is
attachment51061AB7 (5 lines), `old-install-0.10.1.log` is attachment2778FFCD
(1309 lines); `setup.png`, `error-message.png`, `pr-ci-failure.log`, and
`issue-reply.md`. No reporter paths or log contents are committed.
