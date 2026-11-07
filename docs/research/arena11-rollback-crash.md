# Rollback crashes in venue 11 (the subway): the train's pitch effect in re-runs

Builds 45-58, rollback on, venue 11 (feID 11, 125th Street Station, arena kind 4) only: six
`sub_000C5070+0x6D` faults (F9 291b96, b85e35, eb260f, 6f0dc9, e2ea01, b50322; steps 2060-2832, one at
9211) and one `recomp_icall_not_code` (F9 6c6e15). The build-60 reports first named for this (69e6b6,
f84384, 957a1e, 5f37e4) are 20-30 s hangs, not rollback crashes (f84384's venue-11 matches were lockstep).

## Cause [V for the icall, I for sub_000C5070]

The subway's train (`sub_001A94B0`, every update when `[0x3B9084] == 4`) calls `sub_0008F990(value)`,
which stores the value at `[0x37AB6C]` and tail-jumps to the audio subsystem's pitch / slow-motion effect
`sub_000388B0`. The effect keeps its record (`sub_00029680`, caller 0x388E3, so a block of rollback's
audio arena) at `[0x3407DC]` and the last value at `[0x340344]`. Both are game sound globals
(0x340318..0x340D58): saves leave them out, and `rollback_set_resim` puts them back as they were when a
re-run ends.

* While waiting, the train passes 1.5 every update; at its start -4.0, rising to 1.5. A value below 0
  with no record makes one (linked into the bus at 0x345600 by `sub_0013A9E0`); a value >= 1.5 equal to
  the last one unlinks (`sub_0013A430`) and frees it and clears `[0x3407DC]`.
* A re-run that starts two or more steps before the train's start runs the waiting steps with the
  record and last value of "now": the second 1.5 frees the record (arena event `H ... from 001A9647`, the
  waiting branch's call), and the start step makes a new one. When the re-run ends, the globals are put
  back: `[0x3407DC]` names the freed block again.
* If no later load goes back over that free, it is given back `ROLLBACK_HOLD_SAVES` (240) saves later.
  The normal updates of the train's run-in still write into the block (`[rec+8]` = attenuation
  -1000..1000, `[rec+4]` = 0..0xFFFF) and call `sub_0013A480([rec], rec+4)`, which walks the engine
  effect's listeners (freed by the re-run's unlink) and calls each one's `[node+8]`. 6c6e15: record
  allocated at save 2629 and freed at 2631, both from 0x1A9647; given back at 2872, taken by the engine at
  2888 (from 0x13AA02); the crash is the call at 0x13A4A5 on that block.
* `sub_000C5070+0x6D` (five reports) is the emitter-group walk (`sub_000C52B0`, the manager in registry
  slot 0x21) reading a group pointer of -0x1F8..-0x24. Not reproduced here. Those values are in the
  attenuation's range, and a listener callback reached through the stale effect writes the effect's
  values into its target, so it is likely the same stale record; the one emitter group in venue 11
  (0x81002Fxx, made at the load) was intact in every run below.

## Fix

`sub_000388B0` does nothing in a re-run on the game thread (`src/recomp_manual.c`, the
`audio_rerun_skip` rule the audio update and the stream calls follow). It changes only sound state: the
sound globals it writes are put back after a re-run anyway, and the fight's own time scale `[0x37AB6C]`
is written by `sub_0008F990` before the jump.

## Test switches

* `RECOMP_ROLLBACK_TEST_PITCH_RERUN=1`: the effect runs in re-runs as before.
* `RECOMP_ROLLBACK_TEST_BACK=lo,hi,depth,every` (depth and every are new): every `every`-th update in
  `lo..hi` goes back `depth` steps. `1500,7000,6,7` crosses the train's start now and then with no later
  load reaching back over the free; every update (`every` 1) always undoes it again.
* `RECOMP_ROLLBACK_CHECK_EMITTERS=1`: prints the emitter groups when they change, a group or emitter
  pointer that is no block, the record's state at each check, and `a re-run changed the pitch record`.

## Tests (two copies, venue 11, bot fights, 2026-11-07, logs under `logs/dj/`)

* Unfixed (`RECOMP_ROLLBACK_TEST_PITCH_RERUN=1`), `TEST_BACK=1500,4200,4` (every update): three re-runs
  changed the record and put back a held-free block; each next load undid the free (`-162551-online`).
* Unfixed, `TEST_BACK=1500,7000,6,7`, 60+10 ms (`-163954-online`): one re-run each; the record went to
  "arena, free" 240 saves later on copy 0; copy 1 got `[ICALL] target 0x00000000` from 0x13A4A8
  (0x38A8C <- 0x1A9510, the train's run-in), its game thread stopped and the match was given up at step
  3039. This is the 6c6e15 path and the null call seen at venue 11 before (`-052352-online`, F9 291b96).
* Fixed, same switches, three matches (`-165415-online`): no re-run changed the record, no null call, no
  crash (588-1764 rollbacks a match). Matches 1 and 2 went out of sync at steps 3480 and 4740 on fighter
  positions only (the open position desync; a build-60 run with `TEST_BACK=1700,4400` every update
  desynced the same way at step 4320, `-153620-online`); match 3 ended at step 3652 with both fighters hit
  by the train.
* Fixed, natural rollbacks (100+20 ms, ping limit 400), three matches (`-172620-online`): 212-602
  rollbacks a match, deepest 9; KOs at step 5401, 5401 and the train at 3652; checksums equal, no crash.
* `regress.py --quick`: 5 of 5 (`logs/regress-20261107-173945.txt`).
