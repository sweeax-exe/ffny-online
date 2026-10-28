---
name: drive-game
description: Run Def Jam recomp unattended with scripted pad input, captures, scenarios and the regression suite. Use when asked to reach a screen, run a fight, check a route, soak-test boots, or run regress.py.
---

# Drive the game unattended

Full reference: `docs/agent/harness.md` and `docs/09-testing-harness.md`.

- `python scripts/harness.py routes` lists the routes; `python scripts/harness.py run <route> [--png]` runs one.
- `python scripts/regress.py --quick` (unit tests, three golden frames, a fight) after every fix, plus
  `--only <checks>` for the area (table in `docs/agent/release.md`). The full `python scripts/regress.py`
  (about an hour) runs once per platform on a release candidate, not on each PR; a toolkit migration gets one
  full run before it is queued. Run the full one in the background and read its table, not the logs. A failing row prints a
  `python scripts/triage.py <log>` line: use it (skill `triage-run`).
- Game runs are serial. Never start two at once: timing floors, audio, input and the save area are shared.
- Every run goes through the complete-save-root guard and must restore it byte-identical. Captures stay
  under `logs/shots/` and are never committed.
- A new screen to reach: run the nearest route with extra `--stage`s and `--shots`, look at the captures
  (`harness.py montage`), then add the stage to `ROUTES`.
