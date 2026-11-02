# Late-input refresh source audit

Scope: static disassembly of the FFNY recomp only; no runtime/build validation. Addresses below are guest addresses from `C:/Users/Vlad/code/defjam/analysis/tools_disasm_output/output/asm/text.asm`.

## Finding

The game-facing controller accessors read a cached controller object; they do not query the XDK handle table. `sub_DD2B0(device, selector)` reads held/previous/repeat words at `device+0x84/+0x88/+0x8C`; selector 1 computes pressed edges as `(held ^ previous) & held`. `sub_DD3C0(device, axis, out)` copies the two floats at `device+0xA8+axis*8` and `+0xAC+axis*8`. Consequently, changing only the raw XDK result table will not refresh values returned by these accessors until the controller-manager update path copies/transforms a new report into this object.

The worker path is explicit: `sub_D7700` invokes `sub_D7AE0` in its loop. `sub_D7AE0` calls `sub_EBBE0` (the four-handle XInput poller), then for each connected port invokes a virtual method at `this+0x0C` with that port's local report block. It then enters the critical section at `this+0x1A34`, advances a 10-entry ring queue, copies the report block via `sub_D7960`, advances queue indices, and leaves the critical section. This is a dedicated worker-produced/queued update, not a leaf safe to rerun from the main/game thread.

`sub_D7BF0` is a report conversion routine: it calls `sub_EB980(port)` and writes cached words at object offsets `+0x80/+0x84/+0x88/+0x8C`, axis floats at `+0x04/+0x0C/+0x14/+0x1C/+0x24/+0x2C/+0x34/+0x3C`, and button-derived values at offsets through `+0x7C`. This is evidence of a conversion leaf but not proof it is the active manager callback or that its destination is the exact `DD2B0` object; the vtable target/manager mapping remains unresolved in this bounded pass.

## Recommendation and limits

Do not call the full `sub_EBBE0` / `sub_D7AE0` worker pipeline from the game thread: that repeats handle polling, connection handling, callback dispatch, and shared queue mutation while the worker owns the same lifecycle. Do not directly overwrite `+0x84/+0x88` from a late refresh either: `DD2B0` selector 1 derives the pressed edge from those fields, so copying both from a newly-polled snapshot can erase a one-frame press or create duplicate edges depending on what the normal update has already consumed.

A plausible safe seam would be a narrowly-scoped report-to-cached-object refresh called under the existing controller-manager synchronization, with one authoritative edge transition per sample and no handle open/close/rumble work. The disassembly reviewed here does not establish a safe lock/API boundary for that seam or prove `sub_D7BF0` is reachable from the active Battle manager. Treat this as a candidate only; runtime instrumentation must first establish the worker callback target and shared object/lock ownership. The active Battle pad-table producer is separately identified as `sub_79280`; the source here does not prove an 8 ms refresh is safe or that it will reduce end-to-end latency.

## Evidence addresses

- `sub_DD2B0`: `0xDD2B0–0xDD2FD`; selectors read held, edge, repeat, and alternate edge from cached object fields.
- `sub_DD3C0`: `0xDD3C0–0xDD3DD`; copies cached axis floats.
- `sub_D7AE0`: `0xD7AE0–0xD7BBB`; calls poller, dispatches per-port vtable callback, updates ring under `ECF00/ECF10` critical-section wrappers.
- `sub_D7BF0`: `0xD7BF0–0xD7D42`; transforms `EB980(port)` report into cached button/axis fields.
- `sub_D7700`: `0xD7700` worker loop calls `D7AE0`.
- `sub_ECF00` / `sub_ECF10`: enter/leave the critical section through the Xbox runtime imports.

## Follow-up: queue-consumer and manager-update boundary

The worker queue is consumed by `sub_D75E0`, called from `sub_11CD0` (which is in a service/setup loop called by `sub_11500`). `D75E0` checks the queue count at `this+0x17CC`; if nonzero, it enters the same critical section `this+0x1A34`, copies one 0x260-byte entry from the ring at `this+4 + readIndex*0x260` via `D7960` into `this+4`, advances the read index and decrements the count, then leaves the lock. This is a real queue-consume operation, but the destination is the worker/service object’s staging buffer, not the `DCBA0` per-port controller object returned at `manager+0x84+port*0xD4`. Its identified caller is the service loop, not the Battle input tick.

`DCBA0(manager, port)` selects a different per-port object when `manager+0x64+port*4` is non-null (ports 0–7); otherwise it returns fallback `manager+0x7F8` (special index 8 returns `manager+0x724`). The DD2B0/DD3C0 getters operate on that selected object's cached fields. The nearby `DC2E0` path can call `DD400` to update per-object floating values from manager arrays, but it only passes selectors 0 and 1 and does not consume the worker report queue or write the held/previous/axis fields consumed by DD2B0/DD3C0.

Therefore the expected main-thread queue-to-controller-object handoff was not found in the examined `D7xxx` ring routines or `DCBxx/DCCxx` accessors. The visible queue-consume leaf `D75E0` is lock-protected but is not a safe demonstrated candidate for late Battle refresh: it advances a shared ring and stages one report; no path from its staged report into the Battle `DCBA0` object was established. The active producer/consumer link and any suitable per-frame queue-advance hook remain unresolved; don't relocate/call `D75E0` from a fighting tick on this evidence.

## Follow-up: manager cache update found

The `DDBxx` reference is inside `sub_DD930` (`0xDD930–0xDDD3D`), called by `sub_11CD0`. This is the concrete cache update path: it loops 8 manager port slots at `esi+0x64` / controller objects `esi+0x84 + port*0xD4`. For each connected slot, it snapshots current held bits `[device+0x84]` into previous `[device+0x88]`, clears held/repeat `[device+0x84]/[device+0x8C]`, asks the slot object's vtable method at `+4` to fill a local 0x94 report, then calls `sub_DCE00(device, report)`. `DCE00` transforms report entries into held bits at `device+0x84`, sets axes at `+0xA8/+0xAC` and related axis values; subsequent `sub_DCFB0` smooths/filters those axis values. This directly populates the values returned by `DD2B0`/`DD3C0`.

The update does not invoke `sub_EBBE0`/`XInputGetState`. It reads/samples state via the device vtable callback and uses controller-manager source state reached through `[esi+0x8CC]`: `DD930` compares the values at `[ [esi+0x8CC] + 0x17D0 ]`, `+0x17D4`, `+0x17D8`, and `+0x17DC` with cached prior values at manager offsets `+0x8E0`, `+0x930`, `+0x980`, `+0x9D0`. Thus it consumes a manager-owned staged/current snapshot field, not a direct poller call. Whether that `+17D0..+17DC` object is specifically the `D7xxx` worker's queue staging object is not established by this trace; the object is reached indirectly through manager `+8CC`.

This is the best candidate for a once-per-frame late refresh because it already centralizes previous/current edge transition, conversion, per-device update, and axis smoothing. However, its known caller is `sub_11CD0` service/setup loop, and this static trace does not prove it runs in Battle or is safe to call twice per frame. A second call would overwrite `+0x88` from current `+0x84` before recomputing `+0x84`, potentially suppressing edge transitions; it also advances manager timing/repeat state and axis filters. Only move/schedule the existing authoritative call later after proving its active Battle call order, rather than duplicating it.

Evidence: `sub_DD930` address range above; direct caller `sub_11CD0`; updater writes around `0xDDB79–0xDDBB8`; staging comparisons at `0xDD961–0xDDA67`; getter implementations `DD2B0/DD3C0` as documented above.
