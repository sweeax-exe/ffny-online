# Stale-URB completion at boot: `[ICALL] Failed to resolve VA 0x0002A167` from 0x0027EC6E

Read-only investigation (no edits, builds or runs). Evidence log: `logs/run-20261020-182644.log.err` line ~1185.
Tags: [V] verified in code (file:line or guest address), [I] inferred, [U] unknown.
Line numbers of `kernel_bridge.c` are in `tools/xboxrecomp/src/kernel/kernel_bridge.c`; guest functions are in `src/recomp/gen/recomp_0017.c` unless noted.

## Short answer

The URB is the second control transfer of `sub_0027DE9D`, the XInputGetCapabilities-shaped function. The URB and a
`KEVENT` that the title built inline both live on that function's stack. The wait for the second transfer returns at
once because the bridge keeps a host event per guest VA, and that event is still signalled from the first transfer.
`KeSetEvent` never writes the guest `SignalState`, the wait never reads it, and nothing resets the host event. So
`sub_0027DE9D` returns while the URB is still queued in the OHCI model. The completion arrives a few ms later and
reads `+8` of a stack area that other functions have reused.

## Q1. Who issues the URB, where it lives, how the caller waits

- [V] The XInput capability code is `sub_0027DE9D` (0x0027DE9D-0x0027E075, `recomp_0017.c` ~57950-58110). Its only callers are
  `sub_001EBBE0` (per-frame pad poll, call at 0x001EBC64) and `sub_001EBE10` (4-port init loop, call at 0x001EBE76).
  The caps buffer it fills is static: `esi-26` with `esi` stepping by 0x90 from 0x3C9340 (`recomp_0013.c` ~21287, ~21570). It is not on any stack.
- [V] It does two vendor control transfers, both with a stack URB. Setup is `0xC1, 0x01`: GET_CAPABILITIES. The first uses wValue 0x0200 (`0x0027DF69`) and the second wValue 0x0100 (`0x0027E016`).
  Each is submitted with `sub_002800F5(urb)`.
- [V] Stack layout, with `ebp` = entry esp - 8 (`sub esp,0x48`):

  | Where | What |
  |---|---|
  | URB base B = ebp-0x48 | +0 = 0x30, +1 = 0x40 (control type) |
  | B+4 (ebp-68) | status |
  | B+8 (ebp-64) | completion routine = 0x0027F189 |
  | B+0xC (ebp-60) | completion context = &event (ebp-24) |
  | B+0x10 | endpoint pointer, filled in by the submit path |
  | B+0x14 | length |
  | B+0x18 | buffer pointer, into the static caps struct |
  | B+0x28..0x2F (ebp-32..-26) | setup packet |
  | KEVENT at ebp-24 (= B+0x30) | Type byte = 0, Size byte = 4 (ebp-22), SignalState dword (ebp-20) = 0, WaitListHead (ebp-16/-12) self-linked |

  The Type byte is 0, so it is a notification (manual-reset) event.
- [V] The event is **not** initialised with `KeInitializeEvent`. The XBE import table (parsed from `game/default.xbe`,
  thunks at 0x285240..) contains no ordinal 108, so the title writes the KEVENT header with plain stores. The same
  table has 145 KeSetEvent at 0x28527C, 159 KeWaitForSingleObject at 0x28524C, 129 KeRaiseIrqlToDpcLevel at 0x28525C,
  161 KfLowerIrql at 0x285258 and 119 KeInsertQueueDpc at 0x285278. It also has no KeResetEvent (138).
  `kernel_bridge.c:7071-7078` says the same: FFNY imports KeSetEvent and both KeWaitFor* but not KeInitializeEvent.
- [V] The wait is `sub_0027F19A(ecx=device, edx=urb, [arg]=event)` (0x0027F19A-0x0027F1E7), called at 0x0027DF7B and 0x0027E028:
  1. `KeWaitForSingleObject(event, 0, 0, 0, &timeout)` with timeout = 0xFFFFFFFFFFF85EE0 = -500000 x 100 ns = **50 ms**.
  2. If the result is 0x102 (timeout), it cancels the URB with `sub_0027FCA3` -> `sub_0028275A`, which completes the URB through `sub_0027EC5D` at 0x002827D8.
  3. It then waits again with no timeout.

  On a real kernel the URB therefore cannot outlive the function. Before the wait, `sub_0027DE9D` raises IRQL
  (ordinal 129), submits, then lowers it (161).
- [V] The completion routine 0x0027F189 (`sub_0027F189`) is `KeSetEvent(urb->+0xC, 0, 0)`.
- [I] The URB that goes stale is the second transfer (wValue 0x0100). The first wait blocks properly, because its host event is created
  non-signalled. It could also be the first transfer if an earlier signalled shadow already sat at the same VA.
- [U] I could not tell from static code whether the boot-time instance came from `sub_001EBE10` (init loop) or `sub_001EBBE0`.
  Confirm with `RECOMP_WATCH_EXEC=0x0027DE9D,0x0027F19A` and `RECOMP_KE_TRACE=<ebp-24>` (`kernel_bridge.c:1623-1633`).
- [V] Other synchronous users of the same pattern, for scope:
  - `sub_002800F5` (0x002800F5) builds its own stack KEVENT at ebp-20 when the URB has flag 0x40 in `+1` but no completion in `+8`, then waits at 0x002801CA.
  - `sub_0027F895` (XInputOpen) puts a type-1 (synchronisation) KEVENT at ebp-40. Its URB is inside the heap handle (ebx+0x52), not on the stack.
  - Only `sub_0027DE9D` has the URB on the stack. That is why the stale `+8` shows up here.

## Q2. Why the wait returns early with RECOMP_TITLE_KEVENTS unset

It is neither a wait that fails at once nor a guest `SignalState` left at 1. It is a **stale host event keyed by guest VA**.
- [V] `ke_guest_event` returns NULL unless `RECOMP_TITLE_KEVENTS=1` (`kernel_bridge.c:7194-7198`).
  So `bridge_KeWaitForSingleObject` (`:1683`) skips its first block (`:1691-1699`), and `bridge_KeSetEvent` (`:1635`) skips its block (`:1645-1652`).
- [V] Both then call `ke_object_resolve` (`:7079`). It looks up the VA in the 4096-slot shadow table (`ke_shadow_lookup` `:7050`; table `:6989`).
  On a miss it reads the guest header once and creates a host event (`:7110-7113`). Type 0 becomes a **manual-reset** event, with the initial state taken from guest `SignalState` at that moment.
  The shadow is then inserted with `in_use = 1` and reused for the same VA from then on.
- [V] `bridge_KeSetEvent` does `SetEvent(h)` (`:1655-1657`). It does not write guest `SignalState` and does not return the previous state.
  `bridge_KeWaitForSingleObject` waits on the host handle (`:1711`, then `kernel_sync.c:250-262`) and never reads the guest header (the read at `:1706-1707` is trace-only).
  Nothing resets the shadow. `ke_shadow_remove` (`:7145`) has no caller in the file, and the title has no KeResetEvent import.
- [V] Transfer 2 rebuilds the KEVENT header at the same VA (ebp-24) and writes SignalState = 0 (`MEM32(ebp-20)=0`, 0x0027DFF5-0x0027E00E).
  The host event is still signalled from transfer 1, so the 50 ms wait returns WAIT_OBJECT_0 at once.
  `sub_0027F19A` skips the cancel and returns, and `sub_0027DE9D` returns while URB 2 is queued.
- [I] It is a polled-flag-free, state-mismatch bug: guest says 0, host says signalled, and the bridge only consults the host. Every GetCapabilities after the first at a given stack VA is affected, including the 4-port loop in `sub_001EBE10`.
- [I] Any other stack-resident notification event reused at the same VA is affected too, as is a type-1 event whose signal was never consumed. The shadow is created once, with the type seen the first time, and never updated.
- [V] Latency of the real completion: the OHCI controller thread ticks every 4 ms (`ohci.c:173`).
  The ISR runs on that thread on a worker stack slice (`ohci.c:1081-1137`) and is held off while the guest IRQL is raised (`ohci.c:1445-1460`).
  The DPC is queued and drained on another thread (`kernel_bridge.c` ~2130-2260).
  The slice tops are 0x780000 + n*0x40000 - 0x10 (`xbox_memory_layout.h:407-409`), so slice 0 is 0x7BFFF0. The failing esp 0x7BFF84 is on the interrupt/DPC thread, not the title's.
  So the completion is at least several ms after the early return. During that time the issuing thread's stack gets reused.
- [I] This explains the rarity on Windows (1 of 112 logs). `RECOMP_ICALL_IS_CODE` (`recomp_types.h:126`) silently skips a non-code `+8`, logging it only rate-limited.
  With `+8` still holding 0x27F189 the call is a harmless KeSetEvent. A stale word that is in the code range but is not a function entry is the only case that prints "Failed to resolve".
  0x2A167 is exactly that: the return address left by `call sub_001ECF10` in `sub_0002A140`.
- [V] ICALL history: `recomp_icall_fail_log` (`src/recomp_manual.c:792-830`) prints oldest first, so [15] is the failing target. Just before it come raise (0xFE00001C), MmLockUnlockBufferPages (175), MmGetPhysicalAddress (173) and lower (0xFE000018).
  The 0xFE0000xx VAs are thunk slot index x 4 (`kernel_bridge.c:19`). That is consistent with a completion/done-list drain, and `sub_00283F6F` calls ordinal 175 just before `sub_0027EC5D`. [I] the exact drain function.
- [V] The `[KERNEL] lazy shadow` log line is capped at 8 prints (`s_logged++ < 8`, ~`:7125`). Its absence for the XInput VA proves nothing.
  Lines 534, 536, 609, 1100 and 1101 are other events.

## Q3. What the late completion writes into the URB area

[V] Completion paths on the OHCI done queue before `+8` is called:
- `sub_00284111` (0x00284111, TD to URB at TD+0x18): `URB+0x14 += bytes`, then `URB+4 &= 0` (status OK).
- `sub_0028401D`: `URB+0x14 += n` and `URB+4 = 0`, or `URB+4 = (TD.cc | 0xC0000000)` or 0xC000000F on error.
- `sub_00283F6F` (0x00283F6F) then does:
  - reads `URB+0x18` and `URB+0x20` for the ordinal-175 call;
  - walks the pending-URB list at `ctrl+0x42C` linked through `URB+0x24`. It clears `URB+0x24` and writes the predecessor's `+0x24` (another URB, possibly also a stale stack one);
  - does `URB+0x22 |= 8`;
  - calls `sub_0027EC5D`.
- `sub_00284315` (list `ctrl+0x430`): reads `URB+0x24` and `+0x10`, may write `URB+0x14` (re-link), and writes `URB+4 = 0`.
- `sub_0028439C` (list `ctrl+0x434`) writes `URB+0x14` in its defer branch.
- Writes to the endpoint object (`URB+0x10` target: `+0x20`, `+0x27`, `+0x10`, `+1`) land in the driver's static pool at 0x3CD0A0, not on the stack.

So in the stale stack window B..B+0x30 the damage is: **B+4 (dword zeroed or set to an error), B+0x14 (dword += n), B+0x22 (byte |= 8), B+0x24 (dword zeroed), plus the predecessor URB's `+0x24`**, and the call through `+8`.
[V] The TD chain, ED and HCCA writes by `ohci.c` go to the driver pool, and the control data stage goes to the static caps struct (B+0x18). Neither touches the stack.

- [V] Skipped wild call (the observed case): `RECOMP_ICALL_SAFE_AT` restores `g_esp` and sets `eax = 0` (`recomp_types.h:1083`), so the DPC thread is unharmed.
- [I] Live-frame corruption is possible. 3 dwords and a byte of the issuing thread's old frame get overwritten several ms after the function returned. Whether that hits a live word depends on what runs at that depth.
- [I] In the observed boot the word at B+8 was a return address pushed by `call sub_001ECF10`. That puts `sub_0002A140`'s callee area at B..B+0xC, so B+4 is likely a pushed argument of `sub_001ECF10`'s indirect call and B+0x14/0x22/0x24 are above it, in `sub_0002A140`'s caller's frame.
  I cannot show a concrete corrupted value without the stack. [U] whether the observed boot was damaged beyond the skipped call; the boot completed.
- [I] With `+8` intact (0x27F189) the call is `KeSetEvent(stale ctx)`. That is harmless unless the VA now holds another live KEVENT, which would be woken spuriously.
- [U] If the stale `+8` were a valid function entry, it would run with (urb, ctx) args. Not observed.

## Q4. Narrow fixes

Rejected as the main fix: setting `RECOMP_TITLE_KEVENTS=1` globally. [V] `PROGRESS.md` D95 and item 3 say it hangs FFA fighter select, and it is unknown which setter bypasses guest `SignalState` [U].

**Recommended: make the one waiter honour the guest SignalState (toolkit helper + game hook).**
1. Toolkit: in `bridge_KeSetEvent`'s `ke_object_resolve` path, write `BRIDGE_MEM32(va+4) = 1` before `SetEvent`. This is truthful and harmless. For auto-reset events, clear it after a successful wait.
2. Toolkit: add an exported `xbox_KeEventResyncFromGuest(va)`. Copy the `ke_guest_event_prepare` pattern (`:1668-1681`): resolve the shadow, and if guest `SignalState` is 0, `ResetEvent`, then re-read and `SetEvent` if it turned 1. The re-read closes the race with a concurrent KeSetEvent.
3. Game: `void sub_0027F19A_enter(void)` in `src/recomp_manual.c`, per the CLAUDE.md §4 step 3 recipe (`sub_0021EB90_enter` is the example, and it needs a re-lift). It calls the helper with the event pointer from the stack. Check the argument offset against the hook's entry esp: the event is the stack argument of `sub_0027F19A`, read as `MEM32(ebp+8)` after its prologue, i.e. `MEM32(esp+4)` at function entry.
   This scopes the change to the events this function waits on (XInputGetCapabilities and XInputOpen).

Risks:
- The helper must run after the title's inline init write and before the wait; the hook does that. [V] The init writes precede the submit.
- A fast completion could already have set the host event before the hook runs. Step 1 makes that visible as guest = 1, so the reset is skipped. Without step 1 the hook would lose that wake-up and the code would hang at the second, infinite wait.
- The hook does not cover `sub_002800F5`'s own inline wait (0x002801CA, stack KEVENT at ebp-20, same bug class) or any other stack event. Put the same check in a hook on `sub_002800F5_enter` that runs on exit, or generalise later.
- A generalisation, "resync every lazily-created shadow on every wait", is the same semantic as KEVENTS and carries the same FFA-hang risk.

Alternatives considered:
- Reset the host shadow whenever the title writes the header: no hook point (inline stores), so rejected.
- Make `ke_shadow_remove` run when the stack frame dies: no event for that.
- Shadow only stack events: needs a stack-range test. Thread stacks come from the heap (e.g. 0x01101FF0), so a range test is unreliable. Not recommended.

Verification plan (not run): `RECOMP_KE_TRACE` on the ebp-24 VA should show two waits with a "returned 0" before the second KeSetEvent today. After the fix the second wait should block until the DPC. A temporary `RECOMP_WATCH_WRITE` on B+8 should show no write after `sub_0027DE9D` returns.
Also re-run the FFA fighter-select route and the boot soak (CLAUDE.md §4b).

## Method notes
- The caller list for `sub_0027EC5D` has 12 direct callers, as stated in the brief. The user-visible one here is the done-queue drain (`sub_00283F6F` / `sub_00284315`).
- The thunk table was parsed from the user's own `game/default.xbe` (read, nothing written).
