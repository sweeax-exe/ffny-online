# macOS validation for PR #12

Widescreen is tested on Apple M1 Pro, macOS 26.5.2 arm64, using Vulkan/MoltenVK.
The intended release is **v0.9.0**; published v0.8.0 contains the setup updater.
Tests and media use a disposable extraction and complete-save-root guards.
The owner's original installation and saves are untouched. Raw test failures
remain in local logs; they are not silently removed from the results.

## Installer and source identity

The original candidate is PR head `a7dc1b75ce03f8faa02d5f884fda88d66f1d2336`,
toolkit `872fbc3d18b994f72266b1ff6b532abc98a7a60b`.
The exact macOS setup from CI run `38322574448` was installed with Homebrew and
`/usr/local` unavailable. All 108 local game-build steps completed using the
bundled dependencies. Installer SHA-256:

```
a35ed4331191a33c69846e961d49b08ac402b96cd4b3afb0e7294c4389bbb10b
```

The payload was produced from CI merge `d319a802ecc64c48c59b3c6f84cb65c1f7122b25`;
its runtime sources match the PR head. The installer and build receipt are under
local `logs/widescreen-macos/{artifact,install}`. Game executables, generated guest
source and captures are never release assets.

## Mac-specific correction

On a notched Mac, the desktop mode includes the camera/menu strip while the
fullscreen game's drawable does not. `widescreen (auto)` therefore chose
1.5398 from an 1800×1169-point desktop although its fullscreen drawable was
3600×2260. The result was a narrower field of view and unnecessary side bars.

The Mac host now queries the primary AppKit screen frame minus its safe-area
insets on SDL's main thread, retaining the desktop-mode fallback. This excludes
the camera strip without subtracting the Dock. Existing aspect snapping chooses
16:10 on this machine. Explicit picture shapes and other platforms keep their
existing behavior. Build certification now includes Objective-C sources, with a
fixture that rejects a previously certified executable after the helper changes.

The corrected source builds with the same bundled dependencies (109 steps).
The complete source suite passes **352 tests**, with 30 skipped.
The fixed-build full regression completed **11/16** in 56 minutes
(local `fix-full.txt`, `logs/regress-20261013-210713.txt`). Passed checks include
title/menu goldens, launcher/overlay, one-on-one and four-fighter fights,
a four-minute Terrordome fight, the 80-record replay, Story intro/creator,
gym, and the boot soak (3/3).

Failed checks are retained: the bundled Python has no pytest (the independent
source-suite run above passes), the loading-screen run flags the known USB
callback, crib flags the historical stream callback, and the combat/versus
audio underrun gates fail. Full combat is 26/27 and versus 25/26: every gameplay
and PCM assertion passes; each logs three dry events and zero dropped buffers.
The targeted unit/loading repeats pass (352 source tests; loading frame 100
matches), and the focused crib repeat passes cleanly with saves restored
(`fix-crib-repeat.txt`, `logs/regress-20261013-211510.txt`). Together the repeats
resolve the unit/loading/crib checks. The two existing audio underrun gates
remain failed; the original 11/16 raw result is retained.

The corrected automatic-fullscreen HD run passes with clean diagnostics and
restored saves: aspect **1.6000**, field of view **35.65°**, swap chain
**3600×2260**. It reaches combat; F1 opens and closes the actual Vulkan overlay.
Two native fight captures and a 3600×2260 overlay capture are retained. The
corrected launcher also renders and exits cleanly. Local evidence:
`fix-auto.json`, `fix-auto.txt`, `fix-last.txt`.

## Widescreen and HD captures

| Variant | Native still size | Gameplay replay | Runtime diagnostics |
|---|---|---|---|
| 4:3, original textures | 1280×960 | Matches golden | Clean |
| 4:3, faithful HD | 1280×960 | Matches golden | Clean |
| 16:9, original textures | 1708×960 | Matches golden | Clean |
| 16:9, faithful HD | 1708×960 | Matches golden | Clean |
| 16:10, faithful HD | 1536×960 | Matches golden | One stream callback diagnostic; retained |

Each variant has six native stills at simulation steps 600, 900, 1200, 1500,
1800 and 2100. All 80 gameplay state/event records match the Windows replay
golden at seed `20261006`:

```
66a0593bf098a5431cab169eeaddde85fdfdc2da12f20cdadb63987ee05227ec
```

Capture events are excluded from that comparison: a picture is the first
presented frame at or after its requested step, and one was presented one step
late. Background crowd animation can vary even when gameplay matches. A further
explicit 16:10 replay has clean diagnostics, restored saves and the same golden.

Additional clean runs cover all four HUD corners in a 16:9 HD free-for-all,
Story intro/creator at 16:9 and 16:10, and 16:9 HD crib/gym previews. The authored
Story picture fills 16:9; at 16:10 it occupies 864 of 960 rows with thin bars.
Capture-free fight samples are generally near 120 presents per two seconds.
These observations are specific to the tested routes and hardware.

Local media exports include a fight hero, original/HD and 4:3/16:9 comparison
stills, two silent 60-fps comparison clips and a short gameplay GIF. Clips follow
the same seeded input but their recording starts are not pixel-synchronised.
Video timestamps are resampled to 60-fps CFR for export. GIF cadence is not a
performance measurement. Exact hashes, settings, pack recipe/manifest hashes
and limitations are in local `logs/widescreen-macos/media/exports.json`.
Publication of new game artwork still requires owner review and exact pins.

## Existing diagnostics and limits

The original widescreen-off full regression passed 13 of 16 checks: title/menu
and loading signatures on repeat, launcher/overlay, fights including Terrordome,
replay, Story routes and three boot-soak runs. The initial loading-screen check
flagged the already documented USB callback `0x80003344`; its dedicated repeat
passed. Full-root save restoration passed throughout.

Combat passed all seven gameplay assertions; versus passed all six. Their strict
audio checks failed: the first combat run logged 23 dropped buffers and 11 dry
queue events, and versus logged three dry events. A frozen-fixture accepted-build
comparison (parent `6cc1abb9`, toolkit `7fe8871`, recorded in local
`baseline/identity.json`) passed 26/27 assertions, failing
only the underrun gate with five dry events and no dropped buffers. The focused
candidate repeat passed 24/25, failing only the same gate with two dry events
and no dropped buffers. That focused run is a shorter seeded fight and does not
evaluate match-result assertions; the baseline uses the full result route.
These measurements establish that underruns predate widescreen, rather than
a controlled difference in underrun rate. PCM content and the evaluated
gameplay checks passed. Audio sources
are unchanged by widescreen; these measurements reproduce the existing audio
issue [#11](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/11).
The isolated baseline is a source/executable copy without its own Git metadata;
its report can discover the surrounding candidate worktree. Use the explicit
baseline identity and certified source/executable hashes, not that inherited
Git-head field, when comparing builds.

One HD 16:10 capture run flagged stream callback `0x001E3C14` from caller
`0x001E3A68`. The same symptom was recorded before widescreen on 2026-10-08;
relevant generated functions are byte-identical to the accepted baseline.
A fixed-build crib run also reproduced this exact target/caller during early
loading; it reached the crib and restored saves but its strict diagnostics
remain failed. The focused repeat passes cleanly. The cause is not established and
no speculative callback suppression was added.
The raw failure remains, even though its gameplay replay and subsequent clean
16:10 repeat match the golden.

The full matrix is not a venue sweep. Pause/popups and ultrawide arenas still
need hand play-testing. Vulkan uses black sides beside centred 4:3 pictures;
the blurred fill currently works on Windows only.
