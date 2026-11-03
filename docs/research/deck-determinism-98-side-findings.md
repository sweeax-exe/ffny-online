# Side findings from the #98 investigation (Steam Deck, 2026-10-27)

See `deck-determinism-98.md` for the root cause and the fix.

Things found while chasing the Deck `repeat`/`replay` failures that are worth doing whatever happens to #98.
Nothing here is fixed by this change; items marked (measured) were seen directly, (inferred) were not.

## Bugs / likely bugs
1. `RECOMP_WORKERS=inline` hangs before the fight on Linux (measured; the fight never starts). Separate from #98.
2. `RECOMP_GUEST_LOCK=1` on the Deck stalls ~37 s early in startup (vblank gap 37318 ms near vblank 3000) and the
   scripted menu route then misses `StartGame` (measured). Cause: item 16.
3. `docs/research/crowd-nondeterminism.md` says the background crowd update is "called once a screen update". It is
   called once per outer loop iteration (per render): iterations that run 2+ steps skip it, iterations with 0 steps
   run it. Measured: 862-863 calls per 873 updates on the Deck (measured).
4. `RECOMP_TEST_CROWD=hold` default N=880 never engages on the Deck (only ~860 crowd updates in the window), so the
   default hold is a silent no-op there (measured). Either derive N from the observed window or warn when it does not engage.
5. `docs/known-issues.md` / the #98 text imply `77714da` passed on the Deck. It does not reliably: 6 of 6 pairs
   diverged on a Deck build of `77714da` (measured). Worth correcting so nobody bisects for a regression that is not there.

## Tooling / harness improvements
6. A per-update memory-hash trace (hash 256-byte blocks of 0x2E0000-0x3D0000 each screen update, diff two runs with
   a small script) found the first differing memory in minutes. Worth keeping as a determinism-debug feature
   (it was a local patch with two env knobs, not in the tree).
7. `scenario_suite.py --test-env` only allows `RECOMP_TEST_*`; useful host knobs (`RECOMP_SHADER_CACHE`,
   `RECOMP_GUEST_CORES`, `RECOMP_GUEST_LOCK`, `RECOMP_GUEST_QUANTUM`, `RECOMP_GUEST_CPU_TRACE`) needed a wider allow-list.
8. `scenario_suite` `default_fixture()` creates a new fixture folder on every call; callers that want to reuse one
   have to name it with `--fixture`. Easy to leak folders.
9. The game's stderr only lands in `game.log.err` when the run ends, so a hung or slow run shows nothing live.
   A live tail (or periodic flush) would make hangs much easier to diagnose.
10. The `localhost/defjam-dev` image lacks `cryptography` (unit tests fail to collect) and `capstone` (re-lift fails
    with `ModuleNotFoundError`, after a misleading "previous generated sources preserved"). The lift step also
    exits quickly with a bare "failed" and no reason; surfacing the child's stderr would save a round trip.
11. `pipeline.py recomp` and `build` take `--toolkit` before the subcommand; passing it after gives a bare argparse
    error. Minor.
12. Editing `src/recomp_manual.c` (adding an `_enter` hook) forces a ~2m20s re-lift on the Deck. A way to add a
    probe hook without re-lifting (e.g. a runtime hook table) would speed up debugging a lot.

## Observations that may matter for performance / correctness
13. Guest heap addresses differ run to run before the match starts (spectator table pointer at 0x3494E4 differs by
    0x980) (measured). If anything in the game orders by pointer, results depend on allocation order.
14. The audio writer (`sub_001242D0`) keeps a float clock at 0x2E6224 that differs run to run; harmless but it
    shows up in any whole-memory comparison, so determinism diffs should mask 0x2E6000-0x2E8000.
15. Match-load window on the Deck: ~15.5 s wall for 873 game updates (one update per 60 Hz tick), the same
    count every run (measured). That is the floor for any load-time work on the Deck.

## Added later
16. `RECOMP_GUEST_LOCK=1` needs the generated code lifted with `--back-edge-yield` (`pipeline.py recomp -- --back-edge-yield`);
    the project's normal lift does not use it. Without it the lock cannot preempt a guest loop that makes no kernel call:
    on the Deck the longest turn was 37.7 s (another thread waiting 37.2 s), preempts 0, and the scripted menu route missed
    `StartGame`. With a back-edge lift the longest turn is 400 ms, preempts ~2400 per window, and the fight runs to the end
    (measured). So lock mode was never usable on a normal lift. Either document the requirement, make `pipeline.py recomp`
    pass the flag when the lock is wanted, or have the runtime warn at startup when `RECOMP_GUEST_LOCK=1` meets code with no
    back-edge checks (a startup message when the lock is on and `preempts` stays 0 while turns run long would do).
17. `scenario_suite.py run ... --output <dir>` with an existing `<dir>` dies with `FileExistsError`, but a wrapper that
    keeps a separate `--stream-out <label>.stream` leaves the OLD stream in place. Comparing "new" runs then silently
    compares stale files (it cost me two wrong conclusions in this investigation). The harness should refuse an existing
    `--stream-out` file, or write it only after the output folder is created.
18. The `replay` check's record count depends on wall time (the stream ends when the 30 s window ends): the Deck gets
    78-79 records against the golden's 80 even with a stable fight, so `replay` cannot pass on a slower host regardless
    of determinism. Bounding the stream by step (first 1800 steps) rather than by wall time would fix that.
19. `RECOMP_TEST_STATE_PERIOD` does not exist on main (it was a local debug patch of mine); the sample period is a
    compile-time 30. Making it a knob would help diagnosing determinism, since a divergence between samples is invisible.
20. `RECOMP_TEST_CROWD=hold` / `crowd_control.c` and its docs assume 902/903 background-crowd updates per window; the
    Deck sees ~860 and the update is per render iteration, not per game update. With `--isolate-crowd` available the
    hold machinery may be redundant for repeatability; worth deciding whether to keep it (it only affects pictures now).
21. The match logic stores the raw time-stamp counter at CLogic+0x2D878 (sub_0008BB00) and passes it into a 64-bit
    record at CLogic+0x2BDF0-ish; if a future replay/recorder feature wants byte-identical match records across hosts,
    that word needs the same treatment as the two generators.
22. `scripts/regress.py` `repeat` compares against the plain golden as well as against itself, so on any host where a
    deliberate change moves the stream, `repeat` fails for the golden reason even though the determinism part passed
    ("79 records identical over 1800 steps; but the fight no longer plays as recorded"). Splitting it into
    "repeatable" and "matches golden" rows would make that readable at a glance.
