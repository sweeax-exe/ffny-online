# Controller input practices across 15 recomp projects

Source audit: 2026-10-30. Three research agents inspected five distinct game projects each, including their input backends and shared runtime source. No comparison game was built or benchmarked. Full commit pins, immutable code links, call chains, device/focus/remapping details, and evidence gaps are in the three accompanying reports:

- [N64 action/racing/fighting projects](recomp-input-survey-n64.md)
- [Xbox 360 action/racing/shooter projects](recomp-input-survey-xenon.md)
- [Additional N64 projects and DBZ: Burst Limit](recomp-input-survey-other.md)

This report records the initial source audit and recommendations. Subsequent local
implementation and measurements are in the [transition correction](input-transition-fix-2026-10-30.md)
and [optional late refresh experiment](late-input-refresh-2026-10-30.md).

## Findings that matter for Def Jam

More frequent host polling is useful, but cannot by itself overcome stale guest snapshots, a long button hold, an extra game-frame buffer, or queued rendering. None of these 15 source audits establishes a measured hardware-button-to-photon latency ranking. Shared frameworks also mean 15 projects are not 15 independent input designs.

Our current 120 ms minimum press hold is a poor fit for rapid repeats: the independent Def Jam tests and new traces reproduce separate presses merging at that exact stage. The appropriate goal is prompt fresh input plus preservation of ordered press/release edges until the guest has consumed them. This is an engineering recommendation from the observed defect and API semantics, not a claim that all surveyed projects already implement an edge queue.

## The 15 projects

| Game / project | Source-backed acquisition design | Relevance / limit |
|---|---|---|
| Majora's Mask — [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) | N64ModernRuntime calls the host poll callback on guest controller-read request; SDL2 levels feed the later state read. | Freshness tied to guest reads; actual physical poll rate and latency unmeasured. |
| Star Fox 64 — [Starfox64Recomp](https://github.com/sonicdcer/Starfox64Recomp) | Same callback family at a separately pinned runtime version. | Fast action example, largely shared input design. |
| Mario Kart 64 — [MarioKart64Recomp](https://github.com/sonicdcer/MarioKart64Recomp) | Same SDL2 level-query / guest-read callback architecture. | This is the static recomp, separate from the SpaghettiKart decomp port. |
| Diddy Kong Racing — [DKR-R](https://github.com/ThatGuyMcd/DKR-R) | Explicit SDL2 update before per-player reads; guest callbacks, separate event processing; SDL3 snapshot support. | Useful focus/background-input, remapping, deadzone and response-curve handling. Netplay commit cadence is not physical polling rate. |
| Super Smash Bros. — [smash64r](https://github.com/zestydevy/smash64r) | Zelda-derived SDL2 / N64ModernRuntime read-request path. | Relevant fighting game, but old snapshot and shared architecture; no competitive-latency benchmark. |
| Sonic Unleashed — [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) | Waits for swap-chain availability, then pumps SDL, then calls the guest update; XAM reads event-updated cached state. | Strong example of pacing **before** acquiring the next frame's input. |
| Skate 3 — [skate3recomp](https://github.com/mchughalex/skate3recomp) | ReXGlue: synchronous XInput reads on Windows; SDL path schedules UI event refresh and drains controller updates on state read. | Main-thread/UI ownership and focus policy matter. Its separate render-debug pad probe is not gameplay polling. |
| Call of Duty: Modern Warfare 2 — [mw2-recompiled](https://github.com/PaulCombal/mw2-recompiled) | Guest input import is called once per game frame; connected-pad state updated/read per poll; absent-slot enumeration backed off. | Avoid expensive disconnected-device discovery in the active-pad path. |
| GoldenEye 007 XBLA — [GoldenEye-Recomp](https://github.com/SunJaycy/GoldenEye-Recomp) | Guest controller-poll hook follows XamInputGetState through its ReXGlue backend; separate raw mouse path. | Rendering/vsync options and mouse polling do not prove pad acquisition latency. SDK not pinned by the project. |
| Sonic Free Riders — [Free-Riders-Recompiled](https://github.com/YuutaTsubasa/Free-Riders-Recompiled) | Guest frame-driven reads; Windows XInput queried on read; Linux SDL snapshot refreshed through the presentation event pump. | Platform-specific freshness can differ even in one project; motion-input handling is separate. |
| Banjo-Kazooie — [BanjoRecomp](https://github.com/BanjoRecomp/BanjoRecomp) | SDL2 event processing in runtime loop; guest `osContStartReadData` / `osContGetReadData` callbacks sample current input. | A roughly 1 ms outer event loop does not prove 1 kHz guest input. |
| Banjo-Tooie — [Tooie-Recompiled](https://github.com/Frostie77/Tooie-Recompiled) | Bundled SDL2/N64Recomp runtime and guest-request current-state reads. | Audited release is Android-specific; not a PC latency benchmark. |
| Kirby 64 — [Kirby64Recomp](https://github.com/Kirby64Ret/Kirby64Recomp) | Own host adapter with the N64ModernRuntime callback family. | Work in progress; current-level state queries do not guarantee preserving sub-poll taps. |
| Mischief Makers — [Trouble Makers](https://github.com/ThiagoLira/trouble-makers-pc-recomp) | SDL2 update / current button-axis reads on guest controller request. | Its optional 240 Hz visual interpolation does not establish 240 Hz simulation/input consumption. |
| Dragon Ball Z: Burst Limit — [DBZ-Burst-Limit-Recompiled](https://github.com/iExplosiveRage/DBZ-Burst-Limit-Recompiled) | ReXGlue SDL3 default; optional poll-on-read throttled to at least 1 ms. Separate patch changes the game's intentional offline three-frame input buffer. | Most relevant fighting-game comparator: distinguish backend freshness, game buffering and online delay. Source settings are not measured latency results. |

## Concrete source examples

Unleashed calls `Video::WaitOnSwapChain()` before `SDL_PumpEvents()` and the original game update. Waiting for presentation after input has already been sampled would age that input while waiting; this placement addresses that part of the pipeline. It does not establish a hardware latency number. [Pinned update hook](https://github.com/hedge-dev/UnleashedRecomp/blob/cf829a9eca8fb680fba4b0409ddeb6ca92f22e3c/UnleashedRecomp/app.cpp#L47-L95).

DBZ's SDK can refresh controller state on guest reads, with a nominal 1 ms throttle, rather than rely only on an older UI-thread snapshot. Separately, the game consumes commands from a frame-indexed buffer: its reduced offline-delay option selects the current tick's command instead of one three frames old. The online path is separate. We should not assume Def Jam has that same game-level delay or transplant that patch. [Pinned DBZ frame-consumption code](https://github.com/iExplosiveRage/DBZ-Burst-Limit-Recompiled/blob/8faff03f1cc316e123339111c3f7364876e63ab8/src/burstlimit_netinput.cpp#L605-L630); SDK call sites and settings are linked in the detailed report.

The N64 callback family samples when guest code requests controller reads. That reduces unnecessary separation between request and host query, but a current-state query alone can miss a press which begins and ends between requests. The audited paths generally did not implement an explicit host press/release queue or our 120 ms hold. Their absence of a hold is not proof that they preserve every rapid tap. [Pinned runtime controller-read path](https://github.com/N64Recomp/N64ModernRuntime/blob/df7e820d8c55e4fcb4616c210cbb2c01b25cd48c/ultramodern/src/input.cpp#L106-L145).

## Target design for this fighting game

1. **Preserve transitions without a fixed long hold.** Keep current analog/held state and bounded, timestamped button edges. A press must survive until a guest read can observe it, while a release between repeated presses must remain visible. Integrate acknowledgement at the guest input boundary and prevent stale input from accumulating. Clear pending input on focus loss, overlay capture and device removal according to explicit policy. Validate this through USB and the guest, rather than bypassing the game's input code.
2. **Keep acquisition independent of slow rendering, then refresh near consumption.** A measured 1 ms polling option is a reasonable candidate to evaluate, not a hardware guarantee or a fix for lost edges. Record actual interval tails, CPU cost and device report availability. Avoid running mapping/discovery/rumble work unnecessarily on every acquisition or read.
3. **Place pacing waits before the next input-dependent update and keep presentation queueing bounded.** Measure Def Jam's driver/game snapshot age before choosing a change. Preserve the native simulation rate and combat rules unless a separate, explicitly tested option changes them.
4. **Respect SDL's threading rules.** `SDL_PollEvent` and `SDL_PumpEvents` are documented for the main thread; `SDL_UpdateGamepads` is documented as safe on any thread. Def Jam's Windows worker currently polls SDL events. The official docs do not state a special exception for a native Win32 window with SDL used only for controllers, so this deserves correction in the input redesign; it is not established as the cause of the measured merging. [SDL_PollEvent](https://wiki.libsdl.org/SDL3/SDL_PollEvent), [SDL_UpdateGamepads](https://wiki.libsdl.org/SDL3/SDL_UpdateGamepads).
5. **Use one fine-grained clock and measure each boundary.** SDL3 button events include instance ID, down/up state and a nanosecond timestamp, but that timestamp is not a physical-switch timestamp. Record first host observation, mapped publication, sample/latch/hold output, successful USB completion, guest read, fighter state and presentation. Reject incomplete traces; distinguish software boundary timing from actual button-to-photon measurements. [SDL3 event structure](https://wiki.libsdl.org/SDL3/SDL_GamepadButtonEvent).

These are proposed engineering targets. This turn adds observational tracing and tests; it does not yet change normal polling cadence, the hold policy, controller mappings or combat buffering. No surveyed project establishes a universal “competitive” preset to copy unchanged.

For Def Jam's acquisition thread specifically, measure an absolute-deadline schedule with a suitable cancellable high-resolution waiter, rather than assuming a 1 ms timeout produces a 1 ms interval. Its current loop waits `POLL_MS` and then performs update/mapping work; the trace measures the resulting interval. The toolkit already has monotonic clock and per-thread high-resolution sleep support in [host_time.h](../../tools/xboxrecomp/src/platform/host_time.h) and [host_time.c](../../tools/xboxrecomp/src/platform/host_time.c). Reuse the appropriate abstraction after checking cancellation and platform behavior; the separately named kernel timer wake facility has a single-sleeper contract and must not be shared with a new input sleeper. This is a proposed implementation experiment, not a measured improvement.

## Acceptance tests for a later input redesign

- Preserve the demonstrated 20 ms taps at 70 ms separation through the guest and fighter reader, and test shorter pulses at many offsets relative to guest ticks. Include sustained holds, releases, simultaneous buttons, alternating attacks, analog triggers, four slots and remapping. A level-only guest API cannot express arbitrarily many transitions in one read: define a bounded consumption policy instead of allowing a queue of old commands to grow.
- Fix and validate neutral axes in the existing SDL virtual fixture before treating a successful attack animation as evidence. Test event acquisition separately from deliberate combat buffering and action eligibility.
- Compare current polling with a 1 ms candidate on the same hardware, separately for wired and Bluetooth connections. Use hundreds or thousands of presses, record lost edges, median/tail delays, poll gaps, CPU cost and focus/hotplug behavior. Exercise both smooth fights and rendering/load stalls. An app poll interval cannot increase the controller's own firmware/transport report rate.
- Split USB completion → guest XDK query → game pad cache → fighter consumption before changing game timing. Keep simulation/combat semantics stable while reducing stale snapshots and render queueing; replay/combat regression must still pass.
- Finish with an external button-to-photon measurement on the owner's actual device/display. Software timestamps can localize delays inside the program but cannot certify the full physical experience.
