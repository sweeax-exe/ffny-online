# The freeze when the next match loads: a screen swap with nothing loaded (2026-10-24)

Symptom (players and two-copy tests): after one or more room matches, the load of the next match never
finishes on one PC; the game stops presenting frames and the other PC says the player stopped answering.
The log shows `[ICALL] target 0x00000000 is not code` skipped millions of times from guest `0x00077758`.
Reproduced twice on 2026-10-24 (`logs/run-20261024-173210.log.err` at the fifth match,
`logs/run-20261024-175740.log.err` at the second). Section 10 of `crash-001B2480.md` read it as a worker
holding a freed object; that was wrong. V = verified by reading the lifted code or the logs, I = inferred.

## What it is

* `sub_00011CD0` is the game's main loop (thread `sub_00011500`), not a worker. It reads registry slot 0x17
  (`0x2FB148 + 0x17*4`) once: the screen manager (constructor `sub_00076FE0`, 0x4C bytes). [V]
* Screen manager fields: `+0x0C` an auto-reset event, `+0x18` the current screen, `+0x1C` the pending
  screen, `+0x24` the state 0..3, `+0x28` registry slot 0x18. [V]
* `sub_000773A0(kind)` makes one of 17 screen classes, stores it in `+0x1C` and sets state 1, with no check
  of the state. The main loop's `sub_00077040` (state 1) sets state 2 and signals the event; the loader
  thread `sub_000771E0` loads the pending screen (`[4]`) and sets state 3; `sub_00077040` (state 3) then
  swaps: `edi = +0x1C; +0x1C = 0; +0x24 = 0; edi->[8](); old->[0](1) (delete); +0x18 = edi`, with no null
  check. `sub_00077740` calls `+0x18 -> [0x10]` every frame. [V]
* In both logs the first null call comes from `0x000770A5` (the swap's `edi->[8]` with `edi = 0`), once;
  every later one from `0x00077758` with the current screen 0. The swap ran with state 3 and nothing
  pending, deleted the live screen and stored 0: nothing drew, no front-end call came any more, and the
  automatch sequence (`[AUTOMATCH] armed`) never ran. [V]
* The other four match ends in the same log made the same front-end calls without it: a race. The likely
  trigger is a second request (or a second signal) while the loader is busy: the loader finishes the first
  load and sets 3, wakes again at once, loads the next request, and sets 3 a second time after the main
  loop already swapped. With all guest threads on one host core the timing differs from the console. [I]

## The fix

`sub_00077040_enter` (`src/recomp_manual.c`): a state of 3 with nothing pending goes back to 0, as after a
finished swap, logged as `[SCREEN] a swap with no screen loaded was skipped`. `sub_000773A0_enter` logs a
request that arrives while another screen is still loading (`[SCREEN] screen N requested from ... while
state ...`), which names the second requester.

Left open: a variant where a half-loaded second screen is swapped in while a third request is pending; the
root fix would be the loader thread setting 3 only for the request it loaded (replace `sub_000771E0`,
reached only as a thread start, through `recomp_lookup_manual`).

## Tested (2026-10-24)

`scripts/netplay-hub-test.ps1 -Route online5` with the fix (`logs/run-20261024-185339.log.err`,
`logs/run-20261024-185345.log.err`): four room matches on each copy, no freeze, no desync. The skip fired
once on one copy and twice on the other, each time right after `screen 9 requested from 000C214C while
state 2` (a second request for screen 9 from `sub_000C2xxx` while the first was still loading): without it,
three freezes in one run.
