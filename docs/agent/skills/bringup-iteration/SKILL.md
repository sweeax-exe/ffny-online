---
name: bringup-iteration
description: One bring-up iteration for Def Jam recomp - run, classify the stop, pick the fix, rebuild, regress, log, commit. Use when continuing the port, fixing a boot, crash, hang or missing function, or when asked what to do next.
---

# One iteration

The loop is in `AGENTS.md` §4; this is the checklist.

1. Start from `python scripts/progress.py brief` (hand-off, open questions); open `PROGRESS.md` sections or
   decisions by number only when needed (`progress.py section N`, `progress.py decision D<n>`).
2. Kill `defjam_recomp` before building. Run (`scripts/run.ps1 -WatchdogSecs 10`, then `tests/smoke/boot-smoke.ps1`;
   on macOS and Linux `python scripts/harness.py`). Digest the log with skill `triage-run`.
3. Fix in this order: missing function -> seed, analyze, recomp, build; runtime gap -> one commit on the
   toolkit fork, published before the gitlink moves; lifter bug -> `docs/agent/lifter-overrides.md`.
4. `python scripts/regress.py --quick` and the area's `--only` checks; the full run belongs to the release
   candidate (`docs/agent/release.md`), except one run before queueing a toolkit migration.
5. On `main` or a release branch: log it in `PROGRESS.md` §6 with numbers (`python scripts/worklog-add.py
   <file>`), add a `D<n>` for a real choice, rewrite §7 for the next agent. On a feature branch: the same
   content goes in the PR description (`D-new: ...`, `## Hand-off`); do not edit `PROGRESS.md`.
6. One commit per fix; `git add -A --dry-run` first (no game data, ever); message in a file with `git commit -F`.
