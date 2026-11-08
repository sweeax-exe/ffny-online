# Research and validation evidence

These references explain non-obvious game/runtime behavior and preserve measured
acceptance, controls and failed results. Test dates, source/toolkit pins and
original platform limits matter. Historical proposals and negative searches are
not current implementation instructions or guarantees of external-project status.
Use the [current guides](../README.md), source and [GitHub Issues](https://github.com/vyanhursky/DefJamFFNY-recomp/issues)
for present behavior and open work. Logs/captures/save bytes stay local.

Intermediate readiness/proposal/handoff files have been consolidated into the
corresponding topic references below; their original versions remain in Git
history. A later successful retry does not erase a failed aggregate. Research
confidence labels and disproved hypotheses remain distinct from measured facts.

## Current release qualifications

- [v0.13.0 Windows HD memory investigation — 2026-10-31](rc-v0.13.0-hd-memory.md)
- [v0.13.0 RC — macOS observations, 2026-10-31](rc-v0.13.0-macos.md)

## Controller input: boundaries, corrections and source evidence

- [Guest input read and fighter-cache boundaries](input-consumption-boundaries.md)
- [Controller input investigation — 2026-10-30](input-latency-2026-10-30.md)
- [Controller transition preservation — 2026-10-30](input-transition-fix-2026-10-30.md)
- [Optional late controller report experiment — 2026-10-30](late-input-refresh-2026-10-30.md)
- [Controller input source survey (shared runtime families distinguished)](recomp-controller-input-practices-2026-10-30.md)
- [N64 static recomp input source audit](recomp-input-survey-n64.md)
- [Recomp input survey: additional projects](recomp-input-survey-other.md)
- [Xbox 360 recomp input survey: Xenon and ReXGlue projects](recomp-input-survey-xenon.md)

## Rendering and front-end repairs

- [Story map Shops widescreen border and heading: shared map replays and foreground clipping (#162)](story-map-shops-widescreen.md)
- [D3D8 push-buffer: space reservation and ring wrap](d3d-pushbuffer-wrap.md)
- [M2 — render path: NV2A pushbuffer executor vs D3D8 intercept, and why the main thread parks](m2-render-path.md)
- [M2 — vblank ISR hang: why the D3D ISR declines, and what's actually stuck](m2-vblank-isr.md)
- [Picture options: brightness and the venue vignette](picture-options.md)
- [Fire-escape source geometry repair and earlier negative controls](menu-ui-v1.1-2026-10-26.md)
- [Wide-background APT root placements — 2026-10-20](wide-background-root-placement-audit-2026-10-20.md)
- [v0.12.1: created-fighter snapshot thumbnails](v0121-created-fighter-thumbnail.md)
- [v0.12.1: Sketch Artist eye-color preview refresh](v0121-eye-preview-cache.md)
- [v0.12.1: widescreen menu song notification (#118)](v0121-menu-song-banner.md)
- [v0.12.1: widescreen fight HUD while paused (#95)](v0121-pause-hud-widescreen.md)
- [Widescreen combo call-out (#196)](widescreen-combo-callout.md)
- [v0.12.1 Story portrait and Power Plant investigation](v0121-portrait-viewport-read.md)
- [v0.12.1: right-side health and Blazin meters](v0121-right-meters-fix.md)
- [v0.12.1: Story shop LOADING flash on item changes (#119)](v0121-shop-loading-flash.md)

## Front end, movies and launcher/mod integration

- [M3 — feloader's own script: when does it request the next screen?](m3-feloader-script.md)
- [M3 — what has to happen for a loaded movie to become the one that is displayed](m3-movie-display.md)
- [M4a — feloader's screen-transition system: easeInOut, the pan/swap/door functions, and what drives them per frame](m4a-fe-transitions.md)
- [Movie audio clock: what drives the stream counters at 0x3A9984[h]+0x48/+0x4C](movie-audio-clock.md)
- [Skipping the startup legal screen and movies](startup-skip-flow.md)
- [Venue ID audit: Terror Dome route](startup-skip-venue-audit.md)
- [Launcher implementation and playtest — 2026-10-19](launcher-ui-implementation-2026-10-19.md)
- [Integrated hosted-menu launcher RC — 2026-10-23](integrated-menu-launcher-rc-2026-10-23.md)
- [Mod system: design proposal](mod-system-design.md)
- [GameBanana, Thunderstore and Quiver alongside the curated catalogue](mod-storefront-integration-2026-10-21.md)
- [Reviving the developer module (CRunModuleAki): progress](dev-module-revival.md)

## Game state, saves and determinism

- [Simulation tick of Def Jam: Fight for NY (static read of the lifted code)](simulation-tick.md)
- [Fighter record fields for the test harness](fighter-record-fields.md)
- [Match-end state in Def Jam: Fight for NY (read-only RE notes)](match-end-state.md)
- [Combat telemetry: what the probes read and what was confirmed](combat-telemetry.md)
- [Why a scripted fight does not replay identically (static read)](fight-determinism.md)
- [Why fight pictures do not repeat: the two crowds](crowd-nondeterminism.md)
- [Launching a scenario by choice: fixtures, generated routes, the crowd, cutscenes](scenario-launching.md)
- [Unlocking everything for playtests and regression runs](unlock-everything.md)

## Disc, runtime, controller and sound maps

- [Disc layout — Def Jam: Fight for NY (USA), Xbox](disc-layout.md)
- [Static XDK library symbols (D3D8/DSOUND/XGRAPHC/XAPILIB) via XbSymbolDatabase](d3d8-symbols.md)
- [Code-segment corruption: where does the destination pointer come from?](codeseg-corruption.md)
- [DirectSound voice-list crash at 0xFFFFFFBE (sub_002626B5)](dsound-voice-list-crash.md)
- [USB enumeration state machine in the lifted XAPI USB core (M4)](m4-usb-enumeration.md)
- [M4b: how XAPI assigns controller port/slot, and whether our topology matches hardware](m4b-controller-ports.md)

## Widescreen source maps

- [Where the game builds its 3D projection, and what a Hor+ widescreen patch must touch](widescreen-game-projection.md)
- [Game cameras and what a wider Hor+ view does to them](widescreen-game-cameras.md)
- [Widescreen: every 2D element, and what the front end's dormant widescreen mode really does](widescreen-frontend-2d.md)
- [Widescreen: what the renderer does today and how to widen it](widescreen-renderer.md)
- [Widescreen prior art: 4:3 to 16:9 / 16:10 / ultrawide in ports, recomps and emulator patches](widescreen-prior-art.md)

The [implemented design and catalog](../widescreen/README.md) reconcile the original source maps and their errata.

## Toolkit, platform and early HD acceptance

- [Toolkit Bring-Up Notes — Def Jam: Fight for NY (XDK 5849)](toolkit-bringup-notes.md)
- [Toolkit rebase ledger](toolkit-rebase-ledger.md)
- [Toolkit migration ledger: v0.13.0 → v0.16.0](toolkit-v016-ledger.md)
- [Toolkit rebase acceptance, 2026-10-03](toolkit-rebase-acceptance.md)
- [macOS and Linux support in recompilation and decompilation ports: survey and implications](macos-linux-port-survey.md)
- [Issue #72: Windows startup address-space collision](issue72-windows-startup-memory-layout.md)
- [Issue #83: CachyOS installation compile failure](issue83-cachyos-notes.md)
- [v0.6.0 candidate validation](hd-v060-release-validation.md)
- [v0.12.0 release candidate on the Steam Deck (2026-10-25)](rc-v0.12.0-deck-2026-10-25.md)

## Performance comparisons and machine-readable controls

- [Story scene performance on macOS, 2026-10-18](scene-perf-macos-2026-10-18.md)
- [Story scene performance on the Steam Deck, 2026-10-18](scene-perf-deck-2026-10-18.md)
- [Def Jam recomp on the Steam Deck: FFA at the Power Plant, 2026-10-24](deck-ffa-powerplant-2026-10-24.md)

Retain the separate measurement arms, not only summary prose:

- [scene-perf-deck-base-2026-10-18](scene-perf-deck-base-2026-10-18.json)
- [scene-perf-deck-retest-A-stress-90hz-2026-10-18](scene-perf-deck-retest-A-stress-90hz-2026-10-18.json)
- [scene-perf-deck-retest-B-scale2-90hz-2026-10-18](scene-perf-deck-retest-B-scale2-90hz-2026-10-18.json)
- [scene-perf-deck-retest-C-stress-60hz-2026-10-18](scene-perf-deck-retest-C-stress-60hz-2026-10-18.json)
- [scene-perf-deck-retest-D-deck-60hz-2026-10-18](scene-perf-deck-retest-D-deck-60hz-2026-10-18.json)
- [scene-perf-deck-shader-cold-2026-10-18](scene-perf-deck-shader-cold-2026-10-18.json)
- [scene-perf-deck-shader-warm-2026-10-18](scene-perf-deck-shader-warm-2026-10-18.json)
- [scene-perf-macos-2026-10-18](scene-perf-macos-2026-10-18.json)

## Authoring and published mod versions

General authoring procedures live in [Modding Tools](https://github.com/vyanhursky/DefJam-Modding-Tools).
Exact prompts, candidates, approval ledgers and checkpoint/backups stay in the
private authoring workspace. [Mod catalogue](https://github.com/vyanhursky/DefJamFFNY-mods)
metadata/release notes own published pack versions. The game keeps runtime/Setup
compatibility and concise native acceptance; it does not recreate authoring history.

## v1.0.0 candidate additions

- [Cheat menu: research for issue #71](cheat-menu.md)
- [Custom and cut songs: how the game's music works](custom-songs.md)
- [Side findings from the #98 investigation (Steam Deck, 2026-10-27)](deck-determinism-98-side-findings.md)
- [Why a pinned fight does not repeat on the Steam Deck (#98)](deck-determinism-98.md)
- [Graphics enhancements: test plan for Windows and Linux / Steam Deck](graphics-enhancements-test-plan.md)
- [Graphics enhancements: Windows validation, 2026-11-01](graphics-enhancements-windows-2026-11-01.md)
- [Graphics enhancements: texture levels, anisotropic filtering, MSAA, downscaling, post effects](graphics-enhancements.md)
- [SC04 audio/video synchronization: root cause](sc04-av-sync-rca.md)
- [SC04 zero-time guard: scope, implementation and validation](sc04-fix-validation.md)
- [One app for setup, launcher and updates (design, 2026-10-26)](unified-app-design-2026-10-26.md)
