# v0.13 migration: baseline AC97 startup read fault

Read-only source/log review, 2026-10-07. No code edits, builds, tests, game
runs or live process inspection were performed. The original accepted
toolkit pin is `2ac8e705c453854079f1da7cd9af3250d37b0259`.

## Finding and evidence

The corrected original combat run
`logs/scenarios/regress-combat-20261007-172831/` exits with native access
violation `3221225477` before reaching StartGame. Its stderr identifies a
**read** at host address `0x10EC0010B`, guest address `0xFEC0010B`, in
`nv2a_ack_thread+0xC48`. All printed guest registers are zero. Memory layout,
APU and OHCI initialization have completed; the game AC97/NV2A initialization
messages appear interleaved after the crash message. This is a host startup
fault in the original build, not a demonstrated v0.13 regression or guest
DirectSound reset-loop failure.

The strongest source explanation is an unowned-page publication window:

1. Toolkit `src/kernel/xbox_memory_layout.c:3181` starts the acknowledgement
   thread during memory-layout initialization, before title device setup.
2. That thread repeatedly reads AC97 control bytes, beginning with guest
   `0xFEC0010B` (`:1742-1752`).
3. Title `src/hooks/ac97_bm.c:146-154` copies the page shadow, calls
   `VirtualProtect(..., PAGE_NOACCESS, ...)`, **then** writes its page base
   and increments `s_page_count`.
4. A reader between protection and publication faults. The toolkit AC97 VEH
   handles write AVs only (`xbox_memory_layout.c:448-460`), so this read reaches
   title VEH `src/main.c:384-387`. Its ownership check uses `page_for`, which
   only searches published entries (`ac97_bm.c:57-64`), and can return false.
   The title then prints the fatal crash.

This matches the precise address, fault direction, thread and initialization
phase. No `[AC97] undecoded access` is logged, which favors a failed ownership
check over an unsupported instruction after successful ownership. The decoder
supports normal byte MOV and MOVZX forms (`src/platform/mmio_decode.h:132`
and `:190`); the actual native opcode was not captured, so decoder failure is
not conclusively excluded.

There is an unusually close precedent: title `src/hooks/nv2a_regs.c:356-361`
already publishes ownership before protecting a page, with a comment naming
the occasional startup `nv2a_ack_thread` crash. AC97 retains the opposite
order. Main-thread progress can continue while the faulting thread logs its
crash, explaining why the successful AC97 message appears afterwards.

## Candidate and additional ownership hazard

Original and candidate `src/hooks/ac97_bm.c` are identical. The toolkit diff
from `2ac8e705` to candidate HEAD changes memory allocation/extended mapping,
but contains no AC97 trap, polling or initialization fix; the current
uncommitted memory-layout changes likewise do not fix this path. Reviewing
upstream `1409a7d..b3700e1` found no change in these AC97 sections. Thus the
same publication hazard remains in the migration candidate.

Two models also control the first AC97 page. The toolkit installs its write
VEH at the front of the chain during layout initialization. The title later
changes that page to NOACCESS and expects shadow-based reads/writes. A write
still reaches the toolkit handler first: it opens the real page READWRITE,
single-steps, masks RR and restores READONLY. That can bypass title shadow
updates and remove the title read trap. This is a separate existing ownership
problem; it does not explain the observed initial **read** fault as directly
as publication order. The second title page at `0xFEC02000` is outside the
toolkit's one-page trap.

## Safe focused diagnosis and repair gate

Keep the original fixture, audio thresholds and regression gates. For a
bounded diagnostic, capture at the failing read: native thread ID, RIP opcode,
fault direction/address, page protection, published page count/bases and
whether title ownership/MMIO handling succeeded. A debugger breakpoint on
the AC97 protection/publication sequence or a bounded native fixture can
force a reader between those two operations, avoiding repeated full-game
startup attempts and timing-sensitive verbose logging.

The minimal candidate repair to validate is publishing a fully initialized
page before protection, with defined cross-thread publication ordering and
rollback if VirtualProtect fails. A meaningful fixture should force that
window with a polling byte reader and verify handled reads rather than merely
checking source order. Separately establish one owner for the overlapping
toolkit/title trap; test reset writes, run-bit preservation, both pages and
coherent subsequent reads. Neither suppressing the read AV nor disabling
audio/reset semantics is acceptance. After any authorized repair, rerun
startup boots and the affected combat route serially before resuming the full
migration gates.

## Candidate fix and fixture review

Subsequent read-only review of candidate `src/hooks/ac97_bm.c` and
`tests/ac97_startup/{test_main.c,CMakeLists.txt,README.md}` found no blocking
source or fixture defect. This assessment precedes compilation/test execution.

- The initialized shadow/base are published through InterlockedIncrement
  before protection. `page_for` takes one InterlockedCompareExchange count
  snapshot, providing defined publication ordering under the existing
  single-initializing-thread contract.
- Failed first protection rolls the count back to zero, so the successful
  second page occupies slot zero. Failed second protection leaves only the
  successful first page published. Repeated initialization preserves either
  one-page partial result or the two-page result, matching existing semantics.
- The fixture forces ownership/read checks inside the replacement protection
  call. Its `8A 08` oracle is MOV CL from `[RAX]`; seeded `0x51` changes RCX
  from `0x123400` to `0x123451`, preserving the other bits, and advances RIP
  by two bytes. The old protect-before-publish source should fail ownership
  deterministically; the separate old-source build is the required negative
  control.
- CMake's implementation and platform-header paths resolve from the fixture
  source directory. A forward-slash absolute AC97_FIXTURE_SOURCE supports the
  old-source control. Windows x64 is assumed, consistent with the target
  machine and use of CONTEXT.Rip/Rax.

The fixture verifies call-boundary ordering and actual decoder/shadow read
behavior; it does not perform real page protection, dispatch through the VEH
chain or schedule a concurrent reader. Compiled negative/positive controls
and subsequent boot gates remain necessary. Distinct seed bytes for the two
pages would additionally detect a wrong-page shadow read, although the current
rollback indexing is correct. The separate overlapping-trap issue is unchanged
by this minimal publication fix.
