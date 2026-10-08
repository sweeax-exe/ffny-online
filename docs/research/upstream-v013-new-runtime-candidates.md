# Narrow runtime candidates discovered during v0.13 integration

Read-only review, 2026-10-07. Sources: pristine toolkit tag
`b3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4`, candidate toolkit HEAD
`bec01fa2e8407afcdf61165403f16ed488aca76f` plus its local integration edits,
and `toolkit-v0.13.0-execution.md`. No code edits, tests, builds, game runs,
Git mutations or remote-status checks were performed. The parent agent owns
latest upstream/PR status and any submission decision.

## Recommendation

Prioritize **header-backed event support in KeWaitForMultipleObjects** as a
small upstream follow-up. It reproduces on pristine v0.13 without the fork's
lazy resolver, GPU preemption, affinity policy or Def Jam hooks. Include the
shadow-entry ownership reset only with its explicit lifecycle control and
documented header precondition. Treat live memory statistics as a separate,
larger existing contribution candidate: the migration's two accounting
adjustments are not both standalone fixes to functions present upstream.

| Priority | Candidate | Independent pristine-release defect? | Extraction decision |
|---|---|---|---|
| 1 | Prepare/reconcile header events in multiple wait; consume successful synchronization headers | Yes. Multiple wait bypasses the existing opt-in event model entirely. | Small upstream PR after pristine compiled controls. |
| 2 | Reset `in_use` ownership when replacing a shadow entry | Yes, a stale marker exists upstream; behavioral exposure requires a subsequently plausible inline header. | Small lifecycle hunk, with a focused control; combine with event PR if clearly described. |
| Conditional | KeInitializeEvent guest header Size convention | Source proves initializer/helper/fixture disagreement; no independent ABI oracle or initializer-layout test inspected. | Separate layout candidate after confirming the contract; not part of the validated migration correction. |
| Later | Guest-aware MmQueryStatistics with locked live heap and contiguous accounting | Upstream reports host-derived/fallback pages, but lacks the fork statistics helper/consumer. | Extract the existing statistics topic as a deliberate behavior change, with broader tests. |
| Dependency | Separate contiguous live usage from address high-water | New API can be added atop release allocator; no upstream statistics consumer currently uses live allocations. | Include with guest-aware statistics or establish a maintainer-requested consumer first. |
| Keep local | Lock added around `xbox_HeapLiveBytes` alone | No: pristine release has no such function. | Required integration correctness; not a standalone upstream race fix. |

## Events: precise source triggers and minimal extraction

Pristine `src/kernel/kernel_bridge.c` already has `ke_guest_event`: opt-in
`RECOMP_TITLE_KEVENTS=1`, Type 0/1, Size 4, address validation, and shadow
entries marked `in_use=2`. Single wait reconciles the Win32 event with guest
SignalState and clears a synchronization header on success. Set/reset/pulse
already use this helper.

Its multiple wait instead calls only `bridge_resolve_handle` for each array
element. An untagged guest object VA becomes a raw host HANDLE; the helper
does not consult either header events or `ke_shadow_lookup`. Therefore a
valid, signalled title-built event fails a zero-timeout multiple wait even
with the opt-in enabled. Explicitly initialized shadow objects also lack
multiple-wait resolution on pristine release.

Candidate `kernel_bridge.c:1606` extracts the existing single-wait preparation
into `ke_guest_event_prepare`; its multiple wait at `:5230` uses preparation
and records guest VAs/types. After successful WaitAny it clears only the
selected synchronization header; after successful WaitAll it clears every
synchronization header. Notification headers, timeout/failure paths and
unselected members remain unchanged.

A clean upstream extraction needs the preparation helper, the single-wait
refactor, per-member header metadata and successful-consumption block. For
non-header members use **existing shadow lookup, then existing handle-token
resolution**, rather than importing `ke_object_resolve`. That also resolves
explicitly initialized events through the already-existing upstream table.
Preserve current tagged-handle behavior. Keep default lazy event/semaphore
creation, tracing, preemption and dispatcher changes out of the PR.

The added call to `ke_guest_event` inside fork `ke_object_resolve` is purely
the ownership adaptation between two models: it prevents fork lazy creation
from winning before upstream header recognition. Pristine has no such
resolver, so that hunk is not an independent upstream fix. Similarly the
fixture's new default lazy-shadow expectations and inline semaphore checks
depend on retained fork behavior and must not replace pristine default tests.
The sequential pulse assertion protects behavior already present in the tag;
it is coverage, not a new pulse implementation fix.

### Ownership replacement

Pristine `ke_shadow_insert` replaces an existing HANDLE but leaves `in_use`
unchanged. A previously header-backed entry remains marker 2 after explicit
initialization. Candidate resets the marker to 1 on replacement, enforcing
the same ownership invariant as a newly inserted explicit object.

The existing candidate test correctly restores a plausible Type 1/Size 4
header after KeInitializeEvent, sets SignalState to 1, and verifies the
explicitly initialized, initially unsignalled host object still times out.
This precondition matters: pristine KeInitializeEvent writes Size **16**, so
an ordinary immediate wait already fails header recognition and masks the
stale-marker bug. Do not claim that an unmodified initialize-then-wait alone
reproduces it. A fresh header-shaped object at a reused VA or inline header
rewrite exposes the stale ownership. Do not broaden this PR into a dispatcher
header-size correction or claim general object-lifetime safety.

### Separate initializer Size candidate

The release fixture explicitly describes Size in **dwords** and builds a
16-byte KEVENT with Size 4. The helper uses that same Size 4 shape, while
KeInitializeEvent emits Size 16. This is strong source evidence for a separate
guest-header layout inconsistency. However, the other bridge initializers also
emit byte-size-looking values (timer 40, mutant/semaphore 16). The inspected
source contains no authoritative independent guest ABI definition or focused
initializer-layout oracle that settles whether those emitted headers are
deliberately approximate stubs. Current migration fixtures do not test the
initializer's Size; the reinitialization check explicitly rewrites it.

Classify this as a **conditional independent layout candidate**, not a proven
newly measured regression and not a helper-recognition correction. Excluding
explicitly initialized objects from the header-backed path is intentional:
the marker 1/2 distinction already expresses that policy, regardless of Size.
Accepting Size 16 in the helper would weaken inline-event validation instead
of establishing correct guest-visible headers.

Before proposing an initializer change, confirm the Xbox dispatcher-header
contract from an independent source/native guest initialization specimen and
add exact header-byte assertions for both event types. Test normal event
initialization, native auto-reset behavior and inline-header-to-explicit-object
reinitialization. A Size 4 change must include or follow the marker reset:
otherwise it makes the stale marker 2 observable in an ordinary reinit path
without the fixture's current manual header rewrite. Do not silently broaden
the correction to timer/mutant/semaphore layout without their own ABI evidence
and controls. No Size production change is included in the reviewed migration.

### Required independent event controls

Use the upstream synthetic conformance XBE and existing event fixture, retaining
the pristine default mode. Add opt-in-only cases for first access through
WaitAny at nonzero index, selected/unselected signalled members, direct header
reset before multiwait, failed WaitAll retaining signalled headers, successful
WaitAll consuming synchronization members, notification persistence and mixed
explicit/header objects. Preserve thunk stack-cleanup assertions.

1. Compile/run the extended fixture against **pristine b3700e1**: the new
   multiple-wait cases must fail while existing single-wait cases pass.
2. A preparation/resolution-only variant must still fail consumption/repeated
   wait checks; this prevents crediting mere successful HANDLE translation as
   the full fix.
3. A variant retaining the old `in_use=2` marker must fail the separately
   described plausible-header reinitialization check.
4. The clean extracted fix must pass both unchanged default and extended
   opt-in modes; include tagged-handle/explicit-shadow resolution and error
   cases with untouched headers.

Risk is moderate despite the small change. Header reconciliation and
post-wait guest writes retain upstream concurrent setter/waiter races. Closing
replaced handles while waits are in flight and table initialization/lifetime
are existing separate issues. Sequential tests establish the stated contract,
not concurrent delivery, pulse reliability or general semaphore semantics.

## Statistics: separate fork integration from release defects

Pristine v0.13 `kernel_memory.c` calls GlobalMemoryStatusEx, reports 16384 total
pages and derives AvailablePages from Windows free memory; on an ordinary
host the over-total result falls back to 8192 pages. It does not use
`xbox_HeapLiveBytes`, which is absent from pristine `xbox_memory_layout.c/h`.
The candidate's image + fixed kernel allowance + live allocation calculation
is retained fork behavior. Adding the new release allocator lock around that
fork helper fixes a merge/integration race, not an upstream-existing unlocked
statistics walk.

If offered upstream, extract the guest-aware statistics behavior, the live
heap helper with its shared `g_heap_lock`, and a coherent contiguous census
together. Describe the host-dependent/flat-page trigger and the resulting
guest allocation/free accounting. Review the fixed 2 MiB kernel allowance,
XBE image-size validation/caching, saturation and currently omitted memory
categories as explicit model limitations. The locks protect each allocator
census, not one atomic snapshot across allocators or the cached image state.

### Contiguous live accounting and #158

The actual release source, including the merged reclamation work associated
with #158, deliberately keeps `xbox_ContiguousAllocatedBytes` as bump high-water
(`g_contig_next - XBOX_CONTIG_BASE`). Its comment says that range is needed to
recognize physical surface addresses. Older planning text claiming #158
changed this function to live usage is superseded by the released code.

Candidate `xbox_memory_layout.c:3930` adds `xbox_ContiguousLiveBytes`: with
reclamation enabled it takes `g_contig_lock`, sums nonfree tracked blocks and
the reserved first 4 KiB page; without reclamation it returns the existing
high-water amount. Candidate `kernel_memory.c:172` switches only statistics
to this helper. GPU `nv2a_pb_exec.c:117` continues using high-water. This
separation is correct: summing live bytes is not a valid address-range bound
after holes appear through free/reuse. Do not replace the existing range API
with live usage or duplicate allocator changes already present in v0.13.

The helper alone is a new API, not an existing pristine statistics correction.
A baseline failing to compile because the new symbol is absent is not a
behavioral negative control. The useful pristine-release trigger is the
public MmQueryStatistics path: allocate/free a known contiguous block under
reclamation and require AvailablePages to recover those pages. The pristine
host-derived statistic cannot provide that accounting contract. Separately
use a high-water-instead-of-live mutation in the extracted statistics topic
to prove the reclaimed-page assertion detects the specific integration bug.

Keep two independently meaningful checks: a free recovers live/statistics
bytes, while high-water remains unchanged. Add alignment-gap, multi-block
free/coalesce/reuse, reserved-page minimum, saturation and concurrent query
checks before proposing a general accounting contribution. Under reclamation
the census can only describe allocations represented in the release's fixed
block table; capacity/untracked-block handling deserves a boundary control.
Default-mode high-water reads retain existing allocator concurrency limits.

The candidate memory fixture now tests the before/free/after **bridge**
AvailablePages delta as well as helper values and high-water preservation. It
also clears ambient RECOMP_HEAP_RECLAIM/RECOMP_EXT_VMA rather than assigning
`0`, because upstream enables these modes by variable presence. That isolation
fix can be a tiny fixture-only improvement, independent of guest statistics.

## Measured evidence and remaining extraction gap

The execution record reports compiled candidate event and memory fixture
success, all 15 native projects, 656 toolkit tests/107 subtests, and fresh
Release/Debug CRT gates at its recorded checkpoint. Those are evidence for
the combined migration implementation. Game acceptance keeps header events
and heap reclamation disabled, so successful game routes do not independently
validate the new opt-in semantics. The execution record explicitly excludes
concurrent waiter/setter and pulse-delivery guarantees.

No clean pristine-upstream build, extracted runtime PR branch or compiled
negative controls for these new runtime candidates were inspected in this
review. Prepare those controls after the serial game gates, then refresh remote
status before a submission decision. The parent AC97 publication fix belongs
to the Def Jam host and is excluded from these toolkit candidates; no generic
toolkit ownership change is required for the event/statistics extractions.

The parent subsequently reports upstream main `193e2995` (v0.13.1) changes
only lifter/discovery/wrapper/documentation after the release tag, with no
runtime delta. Thus the runtime source findings remain applicable according
to that inventory; this reviewer did not independently query remote status.
