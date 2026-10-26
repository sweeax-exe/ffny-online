# v0.12.1: right-side health and Blazin meters

The Discord report is reproduced with a copied VY3 profile on Windows D3D11,
16:9, render scale 3, and no texture packs. Before the correction, the colored
portions of the right-side meters occupy the left side of their frames. After
regenerating the game with toolkit commit `0423521`, both fills anchor at the
right edge. This is separate from the pause-menu anchoring issue #95.

## Cause and correction

The original integer branch in HUD scalar update routine `sub_000B0DE0` consumes
ZF from arithmetic before a control-flow join. A later MOV replaces ECX with
the object pointer without changing EFLAGS. The generated branch instead tests
that live ECX, so a valid object always takes the branch and skips the original
fall-through UI setter.

The lifter already snapshots arithmetic results for delayed flag consumers.
Its common-destination zero-flag merge was the remaining path that read the
live register. The correction makes merged JE/JNE conditions use the result
snapshot written by whichever predecessor executed. The merge stays limited
to the same destination register and width; other flag conditions are unchanged.
No game function replacement or per-side renderer workaround is needed.

The fork commit changes `tools/recomp/lifter.py`, documents the merge in
`translator.py`, and adds synthetic regressions. The game gitlink must point
to the published fork commit before this is integrated. Commit `0423521` is
published on `vyanhursky/xboxrecomp`, branch `defjam/ui-preview-eyes-meters`.

## Evidence

- Compiled synthetic 8/16/32-bit cases fail before the correction and pass
  afterwards. The CFG regression covers two different setters meeting at a
  join, a later destination overwrite, and both JE and JNE consumers.
- Full lifter suite: 679 passed, 20 skipped, 133 subtests passed with MSVC.
- The exact CI MSVC runner (lifter plus disassembler) passes 832 tests,
  with 20 skipped and 133 subtests. Its helper fingerprint is updated only
  after reviewing the compiler selector, unchanged builder and new join cases;
  the fail-closed fingerprint check remains enabled.
- Native game unit suite with MSVC: 639 passed, 27 skipped, one slow test
  deselected (`logs/v0121-ui/unit-meters-msvc.txt`).
- Analysis, regeneration of 18,446 functions, and Windows Release build pass.
  The generated HUD condition now reads the result snapshot after the MOV.
- Native before: `logs/v0121-ui/selection-fixed2.json`,
  `logs/run-20261025-160308.log.err`, runtime `bf9c975` (includes #108 fix).
- Native after: `logs/v0121-ui/hud-lift-fixed.json`,
  `logs/run-20261025-163525.log.err`; partial fills in the 20.2/35.2-second
  fight captures anchor to the right edge. Median 120 presents per two seconds,
  no crashes, watchdogs, truncated batches or unresolved calls. Disposable saves
  restore identically and owner saves remain byte-identical.
- Quick checks pass: unit checks, three startup goldens and the two-player fight
  (120 seconds, median 120 presents per two seconds). Four-player FFA also
  passes (150 seconds, median 120, minimum 106); its 4:3 captures show the
  bottom-right meter fills anchored correctly. Crib and Learn Moves routes pass.
- The combined regression is **8/9**, not all green:
  `logs/regress-20261025-170143.txt`. Story image comparison is **3/4** because
  the random music banner shows a different song/artist/album in the 9-second
  capture. All pixel differences above 12 levels lie in that banner's bounding
  box `(0,367)..(297,453)`; outside the banner area, maximum difference is 3
  and mean absolute error is 0.0305. Other Story images pass. The approved
  baseline and comparison thresholds are unchanged. Evidence is
  `logs/scenarios/regress-visual-20261025-165709/report.json` and `diff-0.png`.

Captures and game data remain local under ignored logs. The loading-card fix
is PR #110; the created-fighter selection portrait is still absent in the
scripted after test, which uses profile/fighter shortcuts. The normal profile
route is being checked independently in issue #113. Eye color is investigated in
issue #111. No performance fix for issue #109 is claimed here.

## Integration

Use the v0.12.1 release train. Other-platform checks and owner acceptance belong
to the release candidate. Required PR regression checks are recorded in the PR
description. Do not close #112 until integration.
