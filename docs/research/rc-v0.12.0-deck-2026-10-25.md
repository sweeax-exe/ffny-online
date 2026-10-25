# v0.12.0 release candidate on the Steam Deck (2026-10-25)

Head tested: `af59672` (`release/v0.12.0`, PR #106; only records and docs after the
`1e4d5db` the gate names). Toolkit `8fb21d2`. Built from source in a worktree: `analyze`,
`recomp` (host venv), `build` in `localhost/defjam-dev` (Arch 2025/07/27), preset `posix-release`.
Run as `gamescope --backend headless -W 1280 -H 800 -r 90 -- python -B scripts/regress.py --preset posix-release`.
The owner's save and `settings.ini` were backed up first and were byte-identical afterwards.

| Gate | Result |
|---|---|
| `check-source-tree.py` | 0 failures |
| Full `regress.py` (`logs/regress-20261025-112432.txt`, 39 min) | **17 of 18** |
| `--only replay`, rerun | FAIL again, same numbers |
| `--only repeat` | FAIL (step 541) |

Passed: unit (554 passed, 112 skipped), m2, m3, m4a, launcher, overlay, unlockopt, picture,
fight (median 120 per 2 s, minimum 119), ffa, ffa-terrordome (242 s, median 120, minimum 119),
combat 27/27, versus 26/26, intro, crib, gym, soak 3/3.

## Failures

- `replay`: 79 records against the golden's 80 (hash `ec136f78acf5…`), twice. The same check
  failed on the Deck before this release (migrated: 87, 84 and 79 records; `77714da`: 84), so
  it is the Mac golden not being Deck-valid, not a new change
  (`docs/research/toolkit-v016-ledger.md`, PR #85 Deck pass).
- `repeat` (issue #98): the second identical scripted fight differs from the first at step 541
  (clock, cpu, event, fighters). This is the open Deck finding from the PR #85 pass (also step
  541). Windows and Mac pass it; the pre-migration `77714da` passes it on the Deck. **Not traced;
  not bisected across the four queued PRs in this run.**

## Not covered

Game Mode at 60 Hz (#104), pad/rumble, `audio-clicks.py` at the device, the owner's play-test,
Setup install and Clean Update (Windows).
