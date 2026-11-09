# Launcher UX test candidate — 2026-11-03

Based on release/v1.0.0 PR177, `d79ca62ca360ecf5f1406594b173272288cff65c`.
This is a feature candidate for owner testing, not a qualified release. No renderer, lifted game code, toolkit pin, gameplay timing or save format changes. Existing graphics choices are preserved until the player selects an experience. Full Windows/macOS/Deck qualification is delegated to the owner's other agents by explicit request.

## Product decisions (D-new)

- Desktop and controller/TV use have equal priority. Automatic layout respects OS scaling; the window fits the monitor work area. Keep Play, update discovery and setup actions reachable.
- Modern is recommended on fresh setup; HD remains optional. **Upscaled Original Textures** are generated locally from the player's game copy; downloadable texture packs are labelled separately. Internal pack IDs and saved selections are unchanged. Downloadable catalogue content is offered in Mods after setup; setup never silently starts optional downloads.
- Home offers Settings, Controls, Mods and Help; two quick graphics controls and experience/device summaries. Settings categories are Graphics, Controls, Mods, Language & access, Cheats, App preferences and Help. Developer diagnostics are collapsed inside Help.
- Explicit **Check for updates** bypasses the 12-hour automatic cache. Checking, current, failed and cached states remain visible. The online target takes precedence over an intermediate local build. Update confirmation explains both app replacement and local game rebuild; after the app restarts with `--updated`, the approved game build continues. Cancel keeps the previous game usable.
- Newly created Steam shortcuts respect the saved launcher preference, rather than forcing `--play`. Existing shortcuts with an earlier Add's `--play` are updated once (owner's choice, 2026-11-03, superseding "not silently rewritten"): by itself while Steam is closed, otherwise through UPDATE STEAM on Home or Maintenance's Update the shortcut. Hold Shift or both shoulder buttons (LB + RB) while starting the app, or use `--launcher`, to override direct play. Physical controller startup detection still needs platform testing.
- Full game graphics/performance retesting is intentionally deferred. The preset values below are candidate choices for owner approval, not claims of pixel-exact Xbox emulation or guaranteed performance.

## Experience contract

`pc_experience_apply` / `pc_experience_current` in the shared settings catalogue own the same graphics fields in both directions. Custom is derived from effective values. Environment overrides prevent partial preset application. Switching experiences leaves personal bindings, display mode, window size, language, saves, installed files, cheats and update preferences untouched.

| Setting | Original | Modern |
|---|---|---|
| Aspect | 4:3 | Widescreen Auto (16:9 / 16:10) |
| Render resolution | 1x / 480p | 2x / 960p |
| Output filter | Smooth | High-quality smooth |
| MSAA | 2x | 2x |
| Mipmaps | On | On |
| Anisotropy | Off | 16x |
| VSync / game brightness curve | On | On |
| Brightness, vignette strength, colour tint | 100% | 100% |
| Vignette / scratches | On | On |
| HUD / cutscenes / side-fill / FOV cap | Auto / authored / blur / 150 | Same |
| Texture replacements | Off, files retained | Preserve selected optional content |

First setup applies the chosen experience only after a successful fresh install has loaded its own settings file. Adopt, repair and update do not apply a preset. Changing quality/other owned settings changes the experience label to Custom. Applying a preset previews its scope and asks before replacing custom graphics choices.

## Setup and recovery

Three steps: USA Xbox game copy → experience → optional HD and location/space review. Default locations are collapsed behind Change locations. Space is calculated asynchronously per destination volume, using the engine's 12 GiB build allowance plus copy size + 1 GiB and optional 15 GiB HD allowance. The engine remains authoritative for full game hashes/toolchain/path validation. The setup body scrolls independently of its primary actions. Technical progress is available under details. Failures retain choices and offer Try again; cancellation no longer promises checkpoint resume.

Maintenance's HD checkbox now applies to Update as well as Repair/Rebuild. Mods has a direct Generate upscaled textures route into that explicit operation. Setup still prepares optional downloads through Mods after completing the base install; it does not implement a background multi-mod onboarding queue.

Uninstall checks source inspection, verifies copied settings contents and save-file contents before deleting data, and reports only actual successful backups. Linux now removes the app-owned Steam entry too. Nothing here is permission to bypass the app's Steam restart consent or data-deletion confirmation.

## Mods and settings

Installed mods and Browse mods are native tab views with a selected colour and gold overline. Browse requests the catalogue on entry and has retry/status feedback. Install and enable activates only a successfully installed, valid pack; texture mods go at highest priority, language mods replace the active language selection. Ordinary Update preserves activation/order. Conflict/load-order tools remain available; no automatic dependency graph installer is added.

Installed-but-off differs from missing or globally disabled. The global texture switch and manual paths/cache limits are in Advanced mods. The app uses its own payload helper/Python, with the installed game's version for catalogue compatibility. Existing override locks remain respected.

Skip startup movies is in App preferences; Story cutscenes still play. The footer says “Navigate with mouse, keyboard, or controller,” without assuming Xbox button labels.

Graphics and control workarounds sit in collapsed sections. Category resets describe their scope. Unlock-all is under Cheats with a permanent-profile warning/confirmation rather than an ordinary Diagnostics toggle. Save failures are surfaced with Retry save; text fields persist on completed editing. Unchecked controls have a visible border. Focus help remains accessible at narrow widths.

## Scaling and legacy front ends

The shared SDL window clamps initial outer dimensions to usable monitor bounds and rechecks on display/DPI changes. Minimum size also fits the work area. Home removes its decorative poster on short/narrow canvases. App pages keep actions outside scrolling content. This is source implementation plus small-window macOS inspection, **not Windows 300% certification**.

The still-distributed native Windows setup is resizable, per-monitor-DPI-aware, with scaled fonts/rectangles, scrolling content, fixed action buttons and focus-following scroll. Legacy Mac/Linux wizards receive terminology/cancellation corrections; macOS release requests include install context and reject updater-incompatible choices. They remain compatibility/recovery interfaces: the three-step preset flow is in the unified app.

## Verification performed and required handoff

Performed locally: game-free macOS app compile/link; native macOS wrapper and legacy Setup compile; all six changed shared UI/settings translation units compiled with the game/overlay configuration; focused app engine-contract, Steam shortcut and package tests (31 passed, 1 platform skip); source hygiene and diff whitespace checks; isolated Home/Settings and 640x480 first-run navigation screenshots. No Play, game build/install, actual update, actual uninstall, managed mod download or full setting matrix was executed during implementation. A sandboxed GUI attempt had no display; the same isolated app ran with desktop access. This is an environment limitation, not a game failure.

Other agents should test the feature head, not inherit PR177's old-head qualification:

1. Windows native app/setup compilation and 4K 300% scale; 100/150/200%, mixed-DPI moves; enlarged app text; long errors and modals. Check layout, focus and hit targets.
2. Deck Game Mode, controllers/keyboard only, LB+RB direct-play override, file-picker/Desktop Mode handoff and Steam Input/overlay. Existing `--play` shortcuts versus new default shortcuts.
3. Fresh install both experiences, optional HD off/on, low disk and wrong game copy. Compare preflight estimate with actual engine gate. Confirm applied preset values and save location.
4. Modern ↔ Original ↔ Custom, environment locks, settings-write failure, scoped resets; graphics/image/performance qualification at 1x/2x/3x on all target platforms.
5. Install and enable for texture/language mods, update disabled mods, conflicts, global switch override, rollback/cancellation and missing helper/Python. Confirm runtime compatibility on an older installed game.
6. Manual check after cached/failed check, offline response, two-phase self-update/restart/build, cancellation and newest-online-versus-local target.
7. Fault-injected save/settings backup, missing/unreadable data, destination write failure, Linux Steam removal and preservation of unrelated Steam entries/art. Never run destructive tests against the owner's only saves.
8. Upgrade/adopt customised installs; verify bindings/language/mod choices remain intact. Verify legacy wrappers forward to the new app.
9. Full game linking and overlay navigation as well as the standalone app. The six changed shared units compile in the macOS game configuration, but that is not a full game link or platform run.

The owner authorized a source PR and integration into PR177's release candidate on 2026-11-03. Tagging, release publication and merging PR177 to main are not authorized by that integration request. Keep branch/source changes separate from local binary/screenshots and game data.

## Owner feedback revision — 2026-11-03

D-new: Move skip-intro into App preferences, rename Game preferences to Cheats, and use controller-neutral navigation copy. Mods uses native tabs instead of action buttons. Recommend and apply “Upscaled Original Textures” for the local generated option across setup, maintenance, installed groups and load order; “downloadable texture packs” refers to catalogue content. Legacy setup option labels use the same name. No pack IDs, manifests, generation algorithm or setting values change.

Validation for this revision: macOS app compile/link, native Swift Setup compile, shared game/overlay compilation, and existing launcher-model tests (36 passed). Inspected Home, both Mods tabs, return navigation, App preferences and Cheats in isolated UI runs. The earlier broader acceptance limitations still apply; this revision does not qualify Windows or Deck. Logs: `ux-feedback-build.log`, `ux-feedback-swift.log`, `ux-feedback-overlay.log`, `ux-feedback-tests.log`, `ux-feedback-ui.log` in the task's local work folder.

## Texture budget guidance and typography exploration — 2026-11-03

D-new: Explain Texture memory budget both below its Mods control and in focus/hover help. It limits replacement texture residency, including retained upload data; a small budget can evict and reload textures. Recommend trying a larger budget for texture pop-in, slow texture loading or repeated stutters with packs enabled, balanced against graphics/system memory use and requiring a game restart. Defaults and cache behavior are unchanged.

The owner subsequently selected typography option A: keep title case and the existing navigation text size, with a heavier UI font. Windows prefers Segoe UI Semibold; macOS uses Arial Bold and Linux uses DejaVu Sans Bold. Missing font files fall back to the normal UI font. Only the four Home destination buttons use this face; PLAY retains its stronger display font. Validation: native app compile/link and an isolated visual check of the expanded Advanced mods explanation; broad settings/gameplay tests remain deferred as requested.

## Approved publication and testing handoff — 2026-11-03

D-new: Owner selected font mockup A and requested the complete launcher UX change be committed, pushed as a v1.0.0 milestone PR and included in PR177. Use a focused PR targeting `release/v1.0.0`, preserving feature history in a merge commit. Full platform regression and real setup/gameplay acceptance remain with the owner's testing agents; no previous-head pass certifies the new candidate. No game data, binaries or generated mockup images belong in the source PR.

Final feature validation: 67 focused launcher/setup-contract/Steam/package tests passed, one Windows-only check skipped; native macOS app and all six shared game/overlay translation units compile. Option A was visually inspected and all four Home destinations opened and returned successfully using isolated data. Logs: `ux-pr-focused-tests.log`, `ux-font-a-build.log`, `ux-font-a-overlay.log`, `ux-font-a-ui.log`.
