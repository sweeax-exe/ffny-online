# Launcher implementation and playtest — 2026-10-19

Vlad approved the Brick & paper proposal and requested implementation in a worktree, tests and a push.
Branch `codex/launcher-redesign`, based on main `63207ea`; source commit `205582d`.
[Draft PR #58](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/58) is pushed for owner playtest. No release/tag or merge was requested.

Incoming agents: start with the [short launcher handover](../agent/launcher-handoff.md).

## Delivered behavior

- Native Home uses procedural faint brickwork, a torn-paper title and gold Play, common display choices,
  local HD-pack availability and the real controller-slot summary. Narrow/large-text layouts use a
  compact title and a scrollable body, retaining footer actions.
- Advanced has Display, Controls, Mods, Interface, Diagnostics and Maintenance. Controls contains
  expandable mapping sections; the overlay calls the same page functions and setting widgets. A wide
  window has contextual help, constrained layouts a category selector. B/Esc returns Home, shoulders
  switch categories, and returning restores focus to the originating Home action.
- Existing settings/defaults, file round-tripping, binding capture and launcher skip/force paths remain.
  Environment overrides display effective values; explicit RECOMP_TEXTURE_PACKS also locks and reflects
  the HD enable switch. Startup labels distinguish Play from next launch, including transitions into
  or out of widescreen. RECOMP_TEXTURE_ROOT is respected when inventorying packs.
- Mods inventories local manifest headers and preserves missing selections. Earlier/Later edits the
  selected list; later selected packs retain precedence. Lists over the existing 95-byte string payload
  limit are rejected with a message. This adds no mod downloads or gameplay patch loader.
- Utilities resolve data/save/mod/install/settings/play-log paths, expose Copy path and report failures.
  The chosen setup is remembered in launcher-setup-path.txt, a UI tool path rather than a gameplay
  setting: real application paths can exceed the toolkit string setting's 96-byte buffer.
- Update / Repair closes the launcher and opens setup, without starting an operation. POSIX setup
  children close inherited descriptors, including play.lock; Windows uses native shell launching.
  Updated installed wrappers export install/log context, and all three wizards prefill install/data
  fields from that context. The overlay directs the player to close the game first.

The display face is Impact where available on Windows/macOS, condensed DejaVu on Linux, and the readable
UI face as fallback. No font binary or new game artwork is committed. Theme/geometry are pc_ui_theme.*,
Home is pc_launcher_home.*, OS actions are pc_ui_actions.*, and inventory/timing helpers are pc_ui_model.h.
Launcher hosts and game renderer remain the existing D3D11 / SDL3 / Vulkan integrations.

## Validation

The macOS candidate builds with pinned toolkit 32aab4d. Eleven serial native UI cases use a
throwaway data root and the harness save guard: Home, Advanced, Mods, 200% text, system clipboard,
640x480 edits/Back, constrained Setup dialog, real pack selection/reordering, environment overrides,
Setup handoff and the local playtest app. All exited normally and restored their save root. Copy path
was verified against the system pasteboard, then all previous pasteboard formats were restored. A
harmless setup stand-in acquired play.lock after launch through a wrapper deliberately inheriting that
locked descriptor: the setup child does not retain it. A final constrained-tooltip capture also passed.
Evidence stays local in logs/launcher-review/checks.json, handoff.json, tooltip-check.json and PNG captures.

Seven renderer-independent model fixtures cover selected priority/missing packs, newly discovered packs
staying disabled, invalid names, schema headers/BOM/whitespace, duplicate priority, absent Mods folders
and mixed live/startup aspect timing. Six native OS-action fixtures cover Unicode/separate paths,
checkout fallbacks, texture-root overrides, unavailable paths, long/moved setup paths and explicit setup
selection. The complete unit suite with pinned Pillow/NumPy passed: **527 passed, 32 skipped**. Both Swift
wrappers type-check with the setup build's generated payload-hash constant supplied.

The full pre-polish regression completed **16/18 in 42 minutes**, including all launcher/overlay/picture,
fight performance, Story and three-boot soak checks (logs/regress-20261019-123630.txt). Replay matched
all 80 records over 1800 steps, but its runtime gate reported the historical stream target 0x001E3C14
(D108): logs/scenarios/regress-replay-20261019-122511/game.log.err:21232. No diagnostic was suppressed.

An early full regression was deliberately stopped for the texture-override UI correction. SIGINT also
interrupted the harness's save restore; the retained pre-run payload was restored and compared byte for
byte before continuing. Recovery evidence is logs/launcher-review/save-recovery.json; the interrupted
run was logs/run-20261019-115113.log.err (normal presentation up to the stop, no game crash). The final
regression uses a disposable copy of the restored save and shader cache, with the extracted dump linked
read-only by convention, avoiding further changes to the owner's save area.

The full run caught one device-audio underrun in combat: 26/27 assertions passed (all seven
combat assertions passed), with audio.dry_queue reporting one event. Bounded triage found the event
beside a 110 ms host stall, with no refused audio submissions or crash. Evidence is
logs/scenarios/regress-combat-20261019-121629/report.json and game.log.err:88813. This run skips the
launcher. A final-build repeat is recorded below; the first failure is retained rather than reclassified.
The two-player route passed 26/26 assertions.

Final-build checks passed:

- **Quick 5/5 in 7 minutes**: logs/regress-20261019-125121.txt; fight median 120 presents per
  2 seconds, minimum 112.
- **UI 4/4**: logs/regress-20261019-125530.txt; Home routes/Play/skip, F1 overlay,
  unlock-option persistence/default off, brightness 151/vignette off saved and applied.
- **Combat retry 27/27 assertions**, including the audio gates: logs/regress-20261019-125931.txt,
  scenario logs/scenarios/regress-combat-20261019-125530. The first failure above remains recorded.
- **[Source CI 28/28](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/39289203701)** at `205582d`: Windows/MSVC game-side source compile and native
  fixtures, Windows/macOS/Linux setup builds and smoke/update checks, POSIX runtime fixtures,
  Python tools and source hygiene. All run without game data.

The owner's save still byte-matches the verified restored payload after all isolated tests
(logs/launcher-review/final-owner-save-check.json). Source hygiene: 427 files, zero failures.
Local playtest entry is build/Def Jam Launcher Playtest.app in the launcher-redesign worktree. It forces
Home even if the normal skip preference is enabled and uses the owner's normal data by default.
The bundle/build/captures are local only; keep this worktree available during owner playtest.

## Owner playtest

1. Inspect Home at your normal window size; check Play, skip/Shift recovery, Advanced/Back and direct
   Mods/Configure paths.
2. Use the controller alone: visible focus, A select, B back, shoulders switching categories; check
   remapping/cancel and rumble. On the Deck compare 100/150/200% text and a small window.
3. Change a quick setting and find the same value in Advanced; Play and open F1. Brightness/vignette
   should change live; startup-only settings should say next launch.
4. Check actual pack inventory, enablement and precedence, and confirm HD behavior after launch. Missing
   or invalid manifests must be explained. Authoring switches belong in Diagnostics.
5. Use folder/log actions. Choose the real platform setup and inspect its prefilled install/data paths;
   close the wizard without installing if only checking the handoff. Installation retains the existing
   release verification, locks and recovery rules.

Native GUI testing here is macOS. Windows and Steam Deck need the owner's live acceptance; remote CI
compiles source/setup without game data. No built game executable is published.

## Follow-up — 2026-10-20

Vlad continued in the original chat and accepted the Mac appearance, requesting three refinements:

- Slider tracks now use a lighter warm charcoal with distinct hover/active states; the gold handle
  stays. Styling is scoped to sliders in the shared launcher/overlay widget.
- New/reset settings use Windowed, 1920×1080, Widescreen (Auto), render scale 2; existing saved values
  survive. Auto follows the monitor. A fresh native boot resolved 16:10 here and macOS fitted the
  requested window to 1800×1034 points (3600×2068 backing pixels), keeping it within the desktop.
- Render scale help now distinguishes internal height from output/window size: 1×/2×/3×/4× means
  480p/960p/1440p/1920p. For 1080p output, 2× balances cost and clarity, or 3× renders above the
  output height for extra sharpness; 3× matches 1440p. The initial long tooltip went off-screen
  at 200% in 640×480. The shorter help and explicit viewport placement now keep all of it visible.

Native captures cover normal/large/narrow slider tracks, fresh settings creation, preservation of
explicit older fullscreen/size/aspect/scale choices, actual Auto startup, and final Home/Advanced/
640×480 at 200% help. Each run restored its disposable save root. Receipts are local in
logs/launcher-review/slider-20261020/checks.json and display-20261020/{default-checks,final-tooltip-checks}.json;
the latter folder is also under logs/launcher-review. No owner settings/save were changed.

Main `af0c03c` is integrated, including the released macOS setup quarantine and skip-intro audio fixes;
both Swift wrappers type-check. The harness explicitly selects 4:3 for reviewed goldens/replays,
independently of player defaults; no golden data or threshold changed. Prior full-run limitations
above remain recorded. Windows/Deck GUI and physical-pad acceptance still require real hardware.

Final follow-up automation: quick **5/5** (logs/regress-20261020-102601.txt), including **528 unit passes, 32 skips,
one slow test deselected**, unchanged golden frames and 120 s of fight, median 120 presents per 2 s, minimum 119. Picture/overlay **2/2**
(logs/regress-20261020-102746.txt): F1 overlay drawn and brightness/vignette edits saved and applied. Native final tooltip
captures show the complete help at normal size and 200% in 640×480. Source hygiene: 430 files,
zero failures. The local playtest app now reports the integrated 0.10.5 base; its wrapper is unchanged.

### Maximum text scale correction (D143)

An extra check after `7fac946` passed CI 28/28 exposed an existing compact-layout corner:
640×480 at 250% left no body below the large header/navigation/footer. The script could not reach
Render Scale (logs/run-20261020-103627.log.err); bounded triage confirmed a normal prelaunch quit.
The compact header and padding now leave room for controls, the footer Play font fits its reserved
row, and help text fits vertically as well as horizontally. Stored/control text scale is preserved.
The SDL test script's new named hover action reveals controls without editing them.

Final native checks pass at 100%, 200% and 250%, including complete help, visible footer controls
and Back to Home. Receipts: logs/launcher-review/display-20261020/max-layout-checks.json, with
fit100/fit200/fit250 captures. Final quick **5/5** (logs/regress-20261020-105429.txt), picture/overlay **2/2** (logs/regress-20261020-105615.txt);
quick units remain 528 pass/32 skip/one slow deselected. Source hygiene 430/0. No further broad
suite is needed for this UI correction; the earlier full-run limitations remain above.
