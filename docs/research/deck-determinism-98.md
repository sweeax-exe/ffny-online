# Why a pinned fight does not repeat on the Steam Deck (#98)

2026-10-27. Measured on a Steam Deck (SteamOS, `posix-release`, headless gamescope 1280x800), fixture
`auto-20261027-093758`, seed `20261006`, the scripted `fight` route, v0.12.2 (`8152382`) and then `main`
(`b94fde6`). Confidence is stated per claim. Every run listed was a full 1,800-step fight.

## Short answer

Two pinned fights part on a slow host because four things the title takes from the host reach the fight, and
the pins from `fight-determinism.md` cover none of them:

1. **Fighters meet spectators.** `sub_001B8760` walks the spectator slot table (count at `0x3494E0`, 0x34-byte
   entries from the pointer at `0x3494E4`) once per fighter per step and sets `0x12` in the flags byte at
   `0x3B8F20` for a fighter close to an entry. The entries' positions follow the spectators' animations, and
   those advance with the host's timing, so the same fight meets them at different steps in different runs.
   The flag changes what the CPU fighter plans: the first field to differ was always the CPU fighter's brain
   (`0x3BFA50..0x3BFA5C`, a target vector and flags), then its position. *Measured.*
2. **Two match-logic generators are seeded from the time-stamp counter.** `CLogic+0x2D87C` (`0x376FDC`, in
   `sub_0008ABF0`) and `CLogic+0x2DC44` (`0x3773A4`, seeded by `sub_0008B730` and `sub_000891A0` through
   `sub_000C8820`). `fight-determinism.md` pinned the AI generators and the match generator but left these two
   as the game seeds them. *Measured: pinning them removed the late splits.*
3. **The general-purpose generator (`0x39C160`) is drawn outside the fight step.** Effects that run once per
   presented frame draw from it, and a host presents a different number of frames between two steps in
   different runs. In two pinned runs whose fights were otherwise identical this word differed in the last
   sample (step 1,771), which is enough to fail `repeat`. *Measured. Whether the fight itself draws from it
   was not shown; restarting it at every step removes the question.*
4. **The sample count of `replay` comes from the wall clock on a slow host.** Not a determinism problem; see
   "What is not fixed".

`RECOMP_TEST_ISOLATE_CROWD=1` (`--isolate-crowd` on the suite and on `regress.py`) removes 1 to 3: the
generators are seeded from the run's seed, the spectator count is cleared before every step, and the
general-purpose generator restarts from the step number at every step. With it the Deck gave one hash in
every run: the golden recorded once, then `repeat` against it twice and `replay` once, 79 records over 1,800
steps each (`6f8b8f32...`, `tests/golden/fight-stream-isolated.json`).

## It is not a regression

`77714da` (toolkit `5445b73`) built on the same Deck is as flaky: all six run pairs differed, one at step 541,
the step the v0.12.2 candidate fails at. The earlier "passes on `77714da`" was two lucky runs.

## How it was found

- **Counters, then hashes.** Produced and consumed 60 Hz records were equal (no tick loss), and the match
  state machine changed state at the same game-update counts every run (state 1 to 3 at +475 updates, 3 to 4
  at +873), so the loading window is fixed in game time. Hashing 256-byte blocks of `0x2E0000..0x3D0000` at
  every screen update and diffing two runs shows what differs at match set-up: heap pointers (the spectator
  table pointer differed by `0x980`, so allocation order differs before the match), animation-name scratch
  strings, and the audio writer's clock at `0x2E6224` (noise: mask `0x2E6000..0x2E8000` in any whole-memory
  comparison).
- **The spectators.** The foreground spectators choose their next animation at different updates every run
  (first choice at +71 to +90 updates; 603 to 652 draws of their generator by step 1), from the game thread
  through the slot manager (`sub_00081170`, `sub_00080A00`, `sub_0007D4B0`, `sub_0007D2F0`). Holding the
  measured per-step time (`0x33DB9C`) at 1/60 did not change that.
- **Skipping the spectator manager** moved the first split from step 541 to step 1,784 in two of three runs;
  the third still split at 328. A per-step dump of the fight regions then showed the first non-noise
  difference, before anything the stream samples: the flag at `0x3B8F20`, set by `sub_001B8760`. Clearing the
  table count made four runs in a row identical, and the manager update itself turned out not to matter.
- **The generators.** `fight-determinism.md` listed `0x376FDC` as time-seeded and unpinned; reading the lifted
  match logic found the second one and that `sub_0008BB00` stores the raw counter at `CLogic+0x2D878`.

## Ruled out

Seeds of the AI, match and crowd generators; shader cache warm or cold; lost 60 Hz records; the measured
per-step time; `RECOMP_TEST_CROWD=hold` (its default of 880 updates never engages on the Deck, which sees
about 860 in the window, and `hold:800` and `hold` with `reseed` fix the crowd's generators but not the fights);
and guest-thread serialisation. `RECOMP_GUEST_LOCK=1` needs code lifted with `--back-edge-yield`: without it
one Deck guest thread held the CPU for 37 s, preempts stayed at 0 and the scripted menus missed `StartGame`.
With a back-edge lift the longest turn is 400 ms and the fight runs, but two runs still part at the same steps,
so the host's scheduling is not what decides this.

## What is not fixed

- **The isolated hash is a Deck hash.** It counts as proven when Windows and macOS record the same value with
  `python scripts/regress.py --only repeat --isolate-crowd --update-golden` on a clean tree (a different value
  there would mean the isolated fight is not platform-independent, which is worth knowing). The plain golden
  (`fight-stream.json`, 80 records) is untouched, and so are the plain `replay` and `repeat` checks.
- **The plain `replay` check still fails on the Deck.** The Deck's plain fight is not the one the Windows and
  macOS golden recorded: 79 records here against 80 there, in every variant tried (seed only; each half of
  the isolation alone; both). The Deck's variants all share the first damage at step 71 and then part, so the
  fight is sensitive to small inputs; what the other platforms' stream contains is not known here, only its
  hash. The sample count can also depend on how many steps fit in the check's window. Nothing in this change
  alters either.
- **The background crowd** (`sub_000DB870`) runs once per outer loop iteration, not once per game update as
  `crowd-nondeterminism.md` says: an iteration that runs two steps skips it and one that runs none still runs
  it (862 or 863 calls over 873 updates on the Deck). It does not reach the fight.
- **Guest heap addresses differ before the match.** Nothing found depends on them.

## Reproducing

```
python scripts/regress.py --only repeat --isolate-crowd --update-golden   # once, on a new platform
python scripts/regress.py --only repeat --isolate-crowd                   # then: two fights, then the hash
python scripts/regress.py --only replay --isolate-crowd
```

The probes that found this (a per-update memory hash, per-step raw dumps, counters for the 60 Hz queue) were
local and are not in the tree; the method above is enough to redo them.
