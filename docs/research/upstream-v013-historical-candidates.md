# Historical upstream candidates after v0.13.0

Read-only review, 2026-10-07. This report revisits the October 4 contribution notes against the current local candidate toolkit. No source changes, builds, tests, game runs, Git mutations, PRs, comments or author contact were performed. The parent agent owns the current GitHub status audit and recommendations for newly discovered migration fixes.

## Recommendation

From the historical programme, prepare **four focused candidates** next: canonical vertex-program state/version accessors, writable save/title-storage routing, luminance/alpha texture sampling, and recorded physical-DMA translation. None is an already prepared isolated upstream branch; the first and fourth have useful existing synthetic evidence, while the second and third need new dedicated reproducers. Submit only after the production slice, baseline-fail/fix-pass cases, current-upstream tests and review packet are concrete.

The five initial contributions (#167-171) are present in v0.13.0 and should not be resubmitted. Their submission record's OPEN status and the October 4 audit's v0.12/current-main statements are historical. The remaining historical master list is **14 candidates, IDs 6-19**. The runtime/lifter/render catalogues describe additional boundaries in the same code; their counts are not additive quotas. New M6 input/settings/overlay and newly found migration defects are outside that original 19-candidate list.

## Suggested next wave and exact boundaries

| Order | Historical ID | Concrete contribution | Existing evidence | Preparation still required |
|---|---|---|---|---|
| 1 | 11; render R2 | Canonical vertex-program/constant views and mutation versions, so CPU interpretation and future GPU caches observe the same uploads. | `tests/nv2a_vsh/adapter_test.c:97-120` checks changed input masks/program hashes, upload revision, constant component version and gated context writes. Source accessors/revisions are in `src/kernel/nv2a_vsh_interp.c:42-124`. | Extract state ownership/accessors and portable state tests from the D3D11/WARP fixture; cover every setter, start/load/context path and version invalidation. Exclude HLSL emission, GPU execution and rendered-output transport. This is the strongest small foundation with existing evidence. |
| 2 | 7; runtime R3 | Route writable UDATA/TDATA aliases to save storage and preserve existing data. | Current candidate retains 211 added lines in `src/kernel/kernel_path.c` versus the release; integrated Story/profile tests support the combined fork. The recent malformed-fixture diagnosis confirms expected UDATA layout, but is not a standalone path-regression fixture. | Add temporary synthetic disc/save trees, direct/relative/count-limited aliases, no writes to disc, existing-file preservation and failed/partial metadata-copy cases. Compose with the release's absolute-root initialization/path hook. Split first-run copying from routing if independently specified. |
| 3 | 10; render R1 | Correct A8/luminance/luminance-alpha sampling in the existing D3D11 fixed/combiner shader paths. | Small resource channel-swizzle boundary in `src/d3d/d3d8_resources.c`, consumed by `d3d8_shaders.c` and `d3d8_combiners.c`. Candidate still differs from release here. Historical renderer review identifies it as independent and low/medium risk. | Build known-pixel A8, L8/L16 and LA cases through both consumers, plus ordinary colour-format controls. Pixel readback matters; shader compilation alone cannot establish correct channels. Exclude unrelated hardware-combiner mapping/backend changes. |
| 4 | 6; runtime R1 | Remember the physical address-to-guest-window translation returned by the kernel, and consume it consistently for USB/APU DMA. | `tests/kernel_physical_address/test_main.c:26-53` checks thunk/stack behavior, 1,025 mappings, offsets, unchanged generation and alias replacement. `kernel_bridge` and `usb_transfer` provide additional integrated boundary evidence. | Add USB/APU consumer tests for both windows, alias replacement/cache invalidation, concurrency and valid physical page zero. Keep fallback policy explicit. Source spans `kernel_memory.c` plus APU cache and OHCI consumers; extracting only the map producer is not complete device correctness. This is worthwhile but less narrow than the first three. |

These orders describe historical extraction work, not which current migration bug should be submitted first. A newly discovered, independently reproduced one-function semantic repair may be a better immediate submission than any historical architecture feature; the parent reviews those separately.

## Disposition of all fourteen remaining master candidates

| Historical ID | Candidate | Readiness / recommended handling |
|---|---|---|
| 6 | Recorded physical DMA translation | Fourth in next wave; existing physical-map tests, missing consumer/concurrency/zero-address proof. |
| 7 | Writable save/title-storage routing | Second in next wave; bounded source scope, requires dedicated synthetic filesystem fixture. |
| 8 | Coherent guest boot clock | Defer. No dedicated fake-clock fixture in the historical notes; shared units/initialization/wrap must be proved. Candidate still contains `XBOX_GUEST_UPTIME_AT_START_MS 10000ull` (`kernel_hal.c:434`), which needs removal or a justified configurable policy. |
| 9 | Controller S hub topology | Defer behind addressed-device/control-transfer work. Existing `usb_transfer` coverage is useful but not complete guest hub enumeration or alternative root topology. Preserve direct/four-pad layout and make the Def Jam root-port choice optional. |
| 10 | Brightness/alpha texture channels | Third in next wave; small independent fix, missing known-pixel tests. |
| 11 | Canonical vertex-program state/versions | First in next wave; extract portable evidence from existing adapter fixture, no HLSL prerequisite. |
| 12 | HLSL vertex-program translation | After 11; broaden numeric MAC/mask/paired/output/address-register tests and audit provenance. Existing 60 ILU cases do not establish complete shader parity. |
| 13 | Minimal D3D11 graphics-batch backend | After maintainer/backend-interface agreement, then synthetic no-game host and one-draw/flip/primitive/default-null tests. Do not submit another competing command walker or backend registration interface. |
| 14 | Forwarded texture cache lifetime/refresh | After 13 and agreed decoder contract. Add eviction across slots, palette/index mutation, row pitch, single-unswizzle and failure cleanup. Cache capacity/refresh interval are policy, not hardware facts. |
| 15 | Forwarded depth/stencil/blend/clip | After 13; known-pixel state tests. May split into smaller depth/stencil, blending and clipping contributions if that produces independently correct units. |
| 16 | Hardware register combiners | After 10/13-15; decode-word and software-reference pixel parity. Existing vertex tests do not cover pixel combiners. |
| 17 | GPU execution of forwarded vertex programs | Highest prerequisite burden, after 11-16. Fix and test dropped first-use failures/same-batch fallback before extracting; preserve upstream batch/index limits or agree a bounded representation. Keep initially optional. |
| 18 | Point sprites | After 17. Historical 1/2-point minimum-count and point-GS failure gaps require fixes and synthetic draw/failure tests. |
| 19 | Reusable GPU upload buffers | After 17. Independent VB/IB wrap, map failure, cleanup/device recreation and measured allocation/timing evidence. Establish ownership instead of retaining unexplained function-static rings. |

## Other held boundaries: useful follow-ups, not ready blanket PRs

- **Flags and switch policy:** #159 and backward-table work are now release inputs. Offer focused missing cases/follow-ups against that design, not the two old flags commits as a competing broad architecture. Switch recovery remains a policy change: a deliberately rewritten valid code target can be redirected to an original arm. Require opt-in or stronger invalid-target proof and an intentional-retarget regression. The parent handles new migration flag/C2/parity repairs separately.
- **IN/OUT:** potential later small lifter/HAL scope. Existing `test_io_ports.py` covers 12 encodings, narrow register preservation and flags, but real HAL helpers must link/test independently. Returning zero on every read and dropping every write is an unsupported-port policy, not full chipset emulation; agree that policy before proposing it. This was held outside the original 19 master candidates.
- **Indirect-tail diagnostics:** existing multiple-unit TLS fixture is useful. Need a real toolkit reader and documented nested-call/nonlocal-exit policy; clearing to zero does not restore an outer site's value. Optional developer feature, not necessary correctness work.
- **Memory/events:** compose with v0.13 allocator and event implementations. Live-stat and single/multiple-wait migration corrections belong to the parent's newer-fix review. Avoid advertising fixed kernel allowance or sequential event tests as complete memory/object/concurrency semantics.
- **Thread references/priorities:** redesign fixed unreclaimed stand-ins, handle reuse/refcounts/locking/type checks; no ready generic object-lifetime PR.
- **IRQL/timer/IRQ/pusher:** needs deterministic concurrency and register/ack/stall-resume fixtures and agreed executor/page-hook contracts. Current timeout bypass of exclusion, guest fence-table offsets and prefetch/backlog heuristics are not generic hardware proofs.
- **Audio output/resampling/interrupt worker and broad OHCI queues:** meaningful integrated behavior but historical overlap with the X-Men umbrella/split work. Add synthetic sample-order/rate/frame-boundary/seek/queue/ISR/race evidence and coordinate the chosen interfaces rather than claiming unowned discoveries. `apu_mixdown` does not prove host output or resampler correctness.
- **Indexed/anchored input:** extend existing scripts/four pads and the release's path hooks. Need grammar/old-format/invalid-input/occurrence/concurrent-registration tests. Do not silently impose the retained 120-ms host input hold (`usb_gamepad.c:687,751`) as the upstream control default.
- **Downmix, render scale, pacing, probes:** optional later features. Preserve upstream defaults and demonstrate enabled paths; fork default scale 2, vsync policy and six-speaker assumptions are not cross-title defaults.

Keep logical-CPU-2 affinity, fixed backlog/prefetch/fence-layout catch-up, DSP scratch `+0x810` auto-ack and title-specific voice selections local or behind explicit title profiles. A working Def Jam integration does not establish portable defaults.

## Submission evidence and limits

The existing complete candidate's native/source/game tests are supporting integration evidence; they do not prove independently extracted branches. Each proposed PR needs its own current-upstream baseline failure, focused fix pass, full tools/conformance/runtime gates, compiler/optimization/skips, provenance and source-only diff. Clearly identify unavailable cross-title/platform checks instead of inferring them from Def Jam. Runtime and shader notices require particular provenance review; comments mentioning xemu/Cxbx are not sufficient to assign a licence.

The parent agent's fresh GitHub API audit reports current upstream main `193e2995ffaec871bb3be43a43bd66aa5fccfeb1` at v0.13.1. Future PR extraction should start there (then refresh before opening), although this game's approved migration target remains v0.13.0. The historical #128/#133-135/#157-162/#165 tables describe October 4. Release inclusion of #133-135/#157-161 makes those accepted prerequisites rather than open competing proposals.

That live audit also reports merged [#173 inline-table arm lifting](https://github.com/sp00nznet/xboxrecomp/pull/173), [#174 gap aliases](https://github.com/sp00nznet/xboxrecomp/pull/174) and [#175 wrapper routing](https://github.com/sp00nznet/xboxrecomp/pull/175), with only [#176 callback recovery](https://github.com/sp00nznet/xboxrecomp/pull/176) and [#162 backend](https://github.com/sp00nznet/xboxrecomp/pull/162) open. Do not duplicate discovery, alias or wrapper fixes. Reassess any switch/tail/callback diagnostic scope against these accepted/current changes before extraction. The four recommended historical foundations above still require that latest-main source check; local candidate-source inspection here uses the v0.13 migration tree, not an independent audit of all v0.13.1 code.

## Sources reviewed

- [Master candidate/risk plan](upstream-pr-review-and-plan.md), especially IDs 6-19 at lines 160-173 and held topics at 175-205.
- [Lifter boundaries and policy](upstream-lifter-pr-plan.md): flags, port I/O, switch recovery, TLS and evidence gaps.
- [Runtime candidate boundaries](upstream-runtime-pr-plan.md): R1-R18, dependencies and fixture limits.
- [Renderer candidate boundaries](upstream-render-pr-plan.md): R1/R2 first-wave foundations, R3-R13 prerequisites/failure gaps.
- [First-five submissions](first-five-pr-submissions.md) and [historical overlap refresh](first-five-upstream-refresh.md): independent branch evidence/provenance and historical status.
- Current local candidate source at `C:/Users/Vlad/code/defjam-upstream013/tools/xboxrecomp`, plus focused release-relative read-only diffs and fixture source inspection. No new execution result is claimed.
