# v0.13 migration baseline: missing Story captures

Read-only investigation, 2026-10-07. Baseline parent `e39cefba902823d4fc40ae29acb8d4e8e7f2cbd0`, toolkit `2ac8e705c453854079f1da7cd9af3250d37b0259`. No build, game run, test or source edit was performed for this report.

## Finding

The baseline visual failure is a malformed disposable save fixture, not an observed pixel mismatch. The frozen fixture at `logs/upstream-013-work/fixture/save` contains `45410049` directly. It needs `UserData/45410049` beneath the save root. Its manifest SHA-256 is `76eb2b23f4c9f499686d9c285873412393436ea993d79ec938b210e399f6a887`; the first manifest paths start `45410049/`, confirming the missing `UserData/` level.

`scripts/scenario_suite.py:254-256` copies `fixture/save` to `disposable/save` without inserting another directory. `tools/xboxrecomp/src/kernel/kernel_path.c:61-62` maps guest UDATA to `<save>/UserData`. The default fixture creator explicitly inserts that level (`scripts/scenario_suite.py:82-94`). Consequently, the frozen fixture's profiles are outside the runtime's lookup path.

At runtime, `kernel_path.c:332-365` creates an empty UserData directory and copies the disc's title metadata into it. This explains why save-directory opens succeed despite missing actual profiles. The observed log confirms that behavior: `logs/scenarios/regress-visual-20261007-165202/game.log.err:28027-28033` enumerates only `TitleImage.xbx`, `TitleMeta.xbx`, then `0x80000006` (no more files). There are no saved-profile subdirectories in that enumeration.

## Observed route and failure

Evidence: `logs/scenarios/regress-visual-20261007-165202/report.json`, its `game.log.err` and empty `captures` directory.

- Main menu was reached around 122.42 seconds; the script's A at +6 was sampled (`game.log.err:27720`).
- User-ID screen was reached around 128.44 seconds (`27732-27733`). The script's down at +4, down at +6 and A at +9 were all sampled (`28411`, `28653`, `28994`). This is not evidence of dropped scripted input.
- No Story crib or gym anchor appears. The last FUNCCALLs are `Control.GetPrevScreenInfo()` and `Audio.Play(1)` around 138.47 seconds (`29002-29003`). Presentation continued until the 300-second route timeout.
- `story-tour` inherits gym navigation: choose the third ID, then wait for crib before further inputs (`scripts/harness.py:300-325,352-353`). With no profiles visible, those downstream stages never activate.
- Captures are anchored to crib and gym, so none fire. Four `visual.capture.*` assertions fail as "required checkpoint capture missing"; no approved-baseline pixel comparison was performed (`scripts/scenario_suite.py:201-217`). Route assertions and gym preview request also fail. Save restoration and fixture immutability pass.

The successful save guard proves restoration of the supplied tree; it does not prove the tree has the shape expected by the runtime. Similarly, a hash-valid fixture can still be structurally wrong.

## Narrow next diagnostic

Keep the malformed fixture/report untouched as failed evidence. Create a new local staging save root with `UserData` under it, copy the frozen fixture's `save/45410049` into `staging/UserData/45410049`, and create a new fixture using `scenario_suite.py fixture --source <staging-save-root> --output <new-fixture> --profile <explicit-description>`. Verify that its manifest paths start `UserData/45410049/` and preserve each profile file's original hash.

Then run only baseline `story-tour` with the corrected fixture and the existing approved visual manifest, serially. Confirm profile-directory enumeration, crib/gym anchors and captures before interpreting pixel metrics. If navigation still fails, capture the user-ID screen at +3/+8/+12 seconds relative to its anchor to calibrate the actual profile order; the third-ID choice remains a separate assumption to verify. Use the same corrected fixture for candidate comparisons.

Do not update visual baselines or game-state goldens to compensate for this prerequisite failure. Other scenario runs using this malformed fixture should be labeled accordingly and repeated where save/profile state matters. The separately observed WEAPON tutorial pause in repeat is outside this report; no equivalence between that pause and this Story failure has been established here.
