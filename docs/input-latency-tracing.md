# Input latency tracing

The optional input trace follows controller state through the host, USB emulation and fighter input reader on one monotonic clock. Tracing is disabled by default and does not itself change input handling. The local investigation candidate also contains the separately enabled transition-retention correction described below. The implementation was built and exercised on Windows; macOS/Linux validation remains outstanding.

## Capture

From the project directory, before launching the diagnostic build:

```powershell
$env:RECOMP_INPUT_TRACE = '1'
$env:RECOMP_INPUT_TRACE_LIMIT = '1000000' # optional; default attempted-event budget
.\scripts\run.ps1
```

`1` creates `logs/input-trace-<pid>.csv`. Alternatively set an explicit CSV path whose parent already exists. Each launch overwrites that path, so use a unique name when retaining captures. Unset the variable or set it to `0` to disable tracing. This requires both the game tracing hooks and the matching toolkit hooks; the released v0.12.3 build does not contain them.

Retain the CSV, its adjacent `.csv.json` metadata, the normal run log, settings/build identity and any recorder frame data. Note which controller, connection and player slot were used. A useful physical-pad capture includes isolated taps, repeated taps and ordinary fighting, with focus changes or overlay use identified. A manual play-through is not required to reproduce the already established minimum-hold merging defect.

Producers copy records into an 8,192-cell MPSC queue. A separate writer formats and flushes them; producers perform no file I/O. Queue-full, budget, malformed-payload and I/O counters are explicit. This reduces interference but cannot promise zero measurement overhead. The one-million-event default covers only a bounded session; increase it for longer captures. Limits are clamped to 2,147,475,455 to keep queue generations within range. After the budget is exhausted, 64-bit attempt/budget counters continue and the analyzer treats the trace as lossy. Initial metadata is written before producers start, preventing an immediate stop from reusing an earlier sidecar.

## Events and clock

CSV schema 1 is `t_ns,seq,tid,stage,slot,aux0,aux1,n,payload`. `t_ns` is QPC time relative to trace startup, expressed as nanoseconds; the metadata preserves QPC origin/frequency. Nanosecond units do not imply nanosecond hardware accuracy. `seq` identifies an attempted event; concurrent producers can publish in a different order, so analyze by timestamp then sequence. Payloads are copied bytes encoded as hex, at most 96 bytes.

| Stage | Observation |
|---|---|
| `device_add`, `device_remove`, `focus` | Slot/backend lifecycle and focus policy context. Device names are in add payloads. |
| `host_poll_begin`, `host_poll_end` | Actual host-worker cadence, including hotplug/update/mapping work. |
| `host_thread_cpu` | Windows input-worker kernel/user CPU counters in 100 ns ticks, approximately once per second while tracing. The analyzer reports percentage of one CPU core over the measured interval. |
| `sdl_clock`, `sdl_button` | SDL clock anchor and received button events; button payload is button/down bytes, aux values are SDL timestamp and instance ID. Presently Windows SDL event path only. |
| `host_raw` | Changed raw pad buttons/axes before mapping/focus filtering; aux values identify backend and focus/UI/live flags. Keyboard state is represented in mapped output. |
| `host_mapped` | Baseline/changed 18-byte Xbox gamepad state before host publication; packet number and backend in aux fields. |
| `script_virtual` | Changed scripted virtual controls immediately before SDL setters; absent in normal physical play. |
| `sampler_tick`, `usb_host_read`, `usb_sample` | Sampler cadence, changed host packet read, changed raw 20-byte USB report. Sampling can also occur during report construction. |
| `usb_latched`, `usb_built` | Every report after press latching and after minimum hold, respectively. |
| `usb_complete` | Report accepted by a successful OHCI transfer, with transfer-descriptor address. |
| `usb_control_snapshot` | Control GET_REPORT observes current physical/mapped state, without consuming a retained transition. |
| `input_edge_delivered`, `input_edge_consumed` | Retained token accepted by USB, then acknowledged by guest consumption. A transfer attempt alone does not acknowledge an input. |
| `input_edge_loss`, `input_edge_cancel` | Cumulative overflow/expiry counters and explicit queue cancellation. These describe input retention, separately from trace-record loss. |
| `xdk_read_begin`, `xdk_read`, `xdk_read_error` | Both sides of guest XInputGetState. A successful normal controller result is a packet DWORD plus an 18-byte Xbox gamepad body. |
| `pad_cache_before`, `pad_cache_after` | Raw 32-byte table rows around the three equivalent human input producers. `aux0` is the table base, `aux1` the producer function VA; use observed writer IDs to identify the active game-mode path. |
| `fighter_input_ack` | The human fighter read the corresponding raw gameplay-button state. Native gameplay filters still decide whether an action can execute. |
| `game_step`, `guest_pad_read`, `fighter_after` | Simulation boundary and both sides of fighter routine `0x001BAA00`. |
| `present_begin`, `present_end` | Actual graphics Present call boundaries and frame/step counters. |

Guest fighter payloads contain twelve little-endian DWORDs: the eight-word pad-table entry, fighter held buttons, pressed buttons, state and flags. Flags bit 0 identifies CPU control; bits 8 and above identify the mapped pad index. Pad table is `0x003B8F48`, stride `0x20`; actors start at `0x003B92E0`, stride `0x12E8`. The actor's controller index is resolved through `0x003B9098`, rather than assuming its actor ordinal is its pad row. Human and CPU fighters must be separated. The pre-read probe precedes any `RECOMP_TEST_INPUT` override, so latency investigations should leave that override unset and use the real controller path.

`guest_pad_read` is the game's cached pad table being consumed by a fighter, **not** guest XDK `XInputGetState`. The gap from USB completion to this read includes driver and game cache/update scheduling. `fighter_after` records input fields, not an attack starting. The next Present boundary does not identify a responding pixel or display scanout. SDL event timestamps similarly do not identify physical switch closure.

## Analyze

```powershell
python scripts/input-latency.py logs/input-trace-123.csv --button x --slot 0 --guest-bit 0x40
```

The command writes `<trace>.analysis.json` with stage edge counts, cadence distributions, matched-edge delay distributions, worker CPU cost, retention counters, quality counters and exclusions. Slot numbers are zero-based. `--xdk-slot` selects the guest handle-array ordinal when that differs from the host/fighter slot; automatic selection only accepts a unique port with the selected button rising in an isolated script. The guest X bit `0x40` was calibrated in isolated X tests on this build. Other controls need their own verified guest mask. Automatic inference is allowed only when an isolated scripted phase varies one action bit; it removes constant modifiers but cannot calibrate arbitrary mixed physical play reliably.

## Local input correction

The game candidate enables `RECOMP_INPUT_EDGES=1` by default. The generic toolkit leaves this mode off unless an application integrates successful guest acknowledgements. In this mode the game defaults to a 1 ms input-worker deadline; `RECOMP_INPUT_POLL_MS=2` selects the comparison cadence. The generic toolkit otherwise retains its 2 ms default. A deadline is a scheduling target, not a guaranteed interval or a way to increase a controller's hardware report rate.

Mapped button presses and releases enter a per-controller, 64-entry FIFO. Independent changes can form a chord, while repeated transitions of the same button remain separate. The first press is exposed immediately. The queue advances after successful USB delivery and guest acknowledgement: during a live fight, gameplay buttons wait for the mapped human fighter's raw pad read; Start/Back, frontend input and controls without a mapped gameplay bit use the successful XDK copy. This preserves a captured tap across a frame without a simulation step. It does not make the simulation run at 1,000 updates per second or force attacks during recovery, hitstun or other native restrictions.

This mode bypasses the legacy 120 ms minimum hold and OR latch. Sticks remain current-valued. Analog-button transitions use the game's `>1` classification, while pressure changes within the same pressed/released classification remain current. System buttons are excluded from the fighter-verification mask; a combined snapshot can still wait while a gameplay-button state needs verification. Focus loss (unless explicitly ignored), host UI, unplug, input remapping and USB reset cancel pending transitions. Overflow or a backlog older than 100 ms resynchronizes to current input and is counted; old commands are not played indefinitely after a stall.

Set `RECOMP_INPUT_EDGES=0` for the legacy comparison path; its `RECOMP_PAD_MIN_HOLD_MS` option remains available. Use disposable test data for automated tests:

```powershell
python scripts/input-latency-fight.py --name controlled-poll1 --pairs 40 --poll-ms 1
python scripts/input-latency.py logs/input-audit/controlled-poll1.csv --guest-bit 0x40
```

The fixture uses two SDL virtual pads and two human-controlled fighters. It verifies the roster, absence of a match result, save restoration, run faults and frame pacing. In the default transition-retention pulse test, it also requires exactly the expected press count in both phases at every delivery/consumption stage, with no reported trace loss or retention discards. A healthy frame rate alone cannot pass that registration check. Legacy, calibration and mixed-combat runs need separate interpretation of their edge counts. The optional `--calibrate-buttons` sweep covers the 12 non-system digital/analog controls. It is software-path validation; physical controller transport and display response need separate measurement.

Scripted phases are separated by gaps over three seconds. Without scripted markers the entire retained trace is one window. Matching uses same-button rising edges within 250 ms and excludes ambiguous matches; missing/merged/late edges are counted instead of shifting all subsequent matches. Inspect counts before interpreting percentiles. Twenty matched presses cannot characterize rare intermittent latency or a population p99.

The writer checkpoints metadata roughly once a second and drains when C exit handlers run. Harness termination and Windows window-close paths that use `ExitProcess` can leave `closed:0`; the analyzer excludes the uncheckpointed tail and distinguishes earlier windows with no reported loss from a complete capture. A missing sidecar, malformed row or nonzero loss/error counter prevents a clean attribution. Exit status is 0 for checkpointed windows without reported loss, 2 otherwise; inspect `trace_complete` separately. Whole-trace completion is required for claims about the whole session.

See the [measured investigation](research/input-latency-tracing-2026-10-30.md), [transition correction](research/input-transition-fix-2026-10-30.md) and [15-project source survey](research/recomp-controller-input-practices-2026-10-30.md).

## Optional late report refresh

`RECOMP_INPUT_LATE_REFRESH=1` refreshes a connected controller's report through
the guest XDK API at the manager's existing update, before its button history and
filters run. It uses the original guest report converter and private scratch;
it does not repeat the device poller's lifecycle or one-poll edge-latch update.
The option is off by default. `shadow` only compares the small raw conversion
against the original poller. See the [experiment record](research/late-input-refresh-2026-10-30.md).

`late_report` contains the applied 22-byte guest packet and body; `late_shadow`
has a byte-difference bitmask in `aux0` (zero means all relevant bytes match).
`late_skip_busy`, `late_skip_dispatch` and `late_read_error` retain the original
report for that update. `late_refresh_begin/end` include synchronization and
report conversion, so a quick inner API read cannot hide a surrounding wait.
The analyzer reports these counts separately and distinguishes the earliest XDK
read from the later applied `late_report` timing boundary. Add
`--phase-step-ms 7` to the fight fixture to move each test pair through different
positions within a frame while keeping the same A/B input pattern.
Delayed fighter acknowledgements carry a queue generation so resets cannot
acknowledge a new same-button transition with an older pending read.

In the configured local test checkout, `scripts/playtest-input.ps1` launches the
candidate with physical controllers, 1 ms acquisition, late refresh enabled and
input tracing. It keeps its save/settings copy in `logs/input-playtest-data`.
Use `-LateRefresh 0` for the same build and transition retention without the late
refresh. The script restores the calling shell's environment when the game exits.
