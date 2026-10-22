# Rollback netcode: how it is built (build 17)

Companion to `rollback-state.md` (where the state lives) and `rollback-step-structure.md` (what a step
does). This file records what was built, why each part is there, and how it was tested.

## 1. The loop

* **Save point.** `sub_0002C530_enter` (the main loop's dt read, the first call of every game update,
  `src/recomp_manual.c`) calls `netplay_on_update()`. Nothing of the update is on the stack there, so
  a load can be followed by running whole updates again.
* **Prediction.** In a fight (`phase == 1`), once every other PC's first entries have arrived,
  `netplay_on_pad_read` uses a missing remote entry's last known value and records what it used
  (`N.used`, `N.used_pred`). A PC runs at most `RB_AHEAD` (8) steps past the oldest entry it lacks.
* **Rollback.** At each update start the steps since `N.final_step` are checked; the first step that
  ran on a prediction that differs from the entry that has since arrived is the target. The save
  taken before that step is loaded and the updates since are run again from the hook the way the
  main loop runs them (`sub_000DD930`, `sub_00077040`, then `sub_000BD860`, `sub_000BDF40`,
  `sub_0001A240`), with saves for each of them. `N.resim` is set meanwhile: no sends, no waits, no
  local polling, no result report, and the sound side is silent (section 3).
* **Checksums.** The fighter hash of each step is kept per step and sent once the steps before it
  are final (`send_hashes`), so it is the same value a PC without rollback sends.
* **Delay.** Hub matches: every PC says in `P_RTT` byte 7 whether it rolls back; when all do, the
  agreed delay is `RB_LESS` (4) steps shorter (minimum 2). Mixed rooms keep the old delay; prediction
  itself is local and safe with any peer.
* **End.** Once the fight is decided and every step is final, rollback stops for the match (write
  tracking off, no saves on the results screen). `RECOMP_ROLLBACK=off` turns it off entirely (on by default since build 19).

## 2. Save states (`src/hooks/rollback.c`)

* `.data/.bss` 0x2D54E0..0x3C92C8 less the other threads' clusters, the sound engine's pages
  (0x3A9000..0x3B1000, 0x2E6000..0x2E8000), the game's own sound globals (0x33E778..0x33ECA0,
  0x340318..0x340D58) and the producer/loader words (0x3C8000..0x3CA000). Kept on purpose:
  0x3A4000..0x3A7000 (the small-block pool's free lists; restoring the pool's block without them
  crashed in the next allocation).
* The heap block 0x80003000..0x82A0D000 page by page. **Write tracking**: after a reference copy at
  the first step, every heap page kept is made read-only; the first write raises an access violation
  that `track_fault` turns into "changes from now on" (game thread, outside a sound command) or
  "another thread's" (left alone), and the write goes on. A save copies the changed pages (about
  4 MB); a load restores them and gives each page first written after that save its reference
  content, which it provably still had. Host file reads write guest memory from kernel mode, where no
  exception comes; the toolkit calls `xbox_guest_write_notify` before each `ReadFile`.
* Learning which pages change by sampling (an earlier version) missed pages written for the first
  time between a save and the load, and crashed in the allocator within seconds of the first
  rollback; tracking made the 2..10-step soak exact.

## 3. Sound is not rolled back

Sound plays in real time; corrected steps are silent. Everything the sound side owns is kept out of
the save states, so a load never puts back half of it:

* the sound engine's allocator (`sub_0001DC10` / `sub_0001DC20`, vtable 0x289480) and every block the
  game's audio code asks `sub_00029500` for (callers in 0x1C000..0x1E000 AEMS, 0x30000..0x3E200 audio
  subsystem, 0x11C000..0x142000 engine) come from an arena in contiguous memory outside the heap
  (`sub_00029500` is hand-written in `recomp_manual.c` with that test in front; frees go through the
  heap object's `sub_0002A180`, intercepted; arena blocks carry pool index 0x3F at -14 so the game's
  direct pool free leaves them alone);
* the engine's command call (`[0x3ADB7C]` = `sub_00137010`) and the audio subsystem's ten bus handlers
  do nothing while steps run again;
* pages the mixer thread writes during its tick (lock/unlock through 0x2E61C0/0x2E61C4, counted only
  when the game thread did not run meanwhile, `QueryThreadCycleTime`) are left alone.

## 4. Tests

* `RECOMP_ROLLBACK_SOAK=<depth>` (offline, one copy): every 5 updates the game goes back 2..depth
  steps and compares the per-step fingerprints of the second run with the first
  (`scratchpad soak.py` pattern: two injected pads, random presses).
* `RECOMP_NETPLAY_LAG_MS=<ms>[,<jitter>]`: arriving packets are held, so two copies on one PC play
  as if far apart (`rb_online.py`: 60 ms each way + 10 ms jitter).
* `RECOMP_ROLLBACK_FULL=1`: every page in every save (4 ms a save); the reference for "is the
  tracking the problem".
