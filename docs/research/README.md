# Research notes

Selected investigations from the port's development, kept because they explain
how the game or the runtime behaves. Each is a dated record: later work may have
superseded its conclusions, and the current source is authoritative. Some notes
cite other development notes, logs and captures that are not published.

## The disc and the executable

- [Disc layout](disc-layout.md)
- [Static XDK library symbols](d3d8-symbols.md)
- [Toolkit bring-up notes](toolkit-bringup-notes.md): runtime diagnostics and environment variables

## Rendering

- [v0.13.0 HD memory investigation](rc-v0.13.0-hd-memory.md) — pack-by-pack Windows controls, steady-fight versus result/menu growth, and the unchanged 64 MiB gate.
- [v0.12.1 Story portraits and Power Plant](v0121-portrait-viewport-read.md) — logical versus AA surface clipping, verified portrait correction, and VY3 FFA performance measurements.
- [v0.12.1 created-fighter thumbnails](v0121-created-fighter-thumbnail.md) — small guest capture surfaces and normal save/reload recovery for empty profile images.
- [v0.12.1 right-side meters](v0121-right-meters-fix.md) — a merged zero-flag snapshot correction and native health/Blazin fill validation.
- [v0.12.1 eye-color previews](v0121-eye-preview-cache.md) — small-texture cache signature collisions and independently verified palette refresh.
- [v0.12.1 widescreen HUD while paused](v0121-pause-hud-widescreen.md) — the fight HUD heartbeat carried through the pause menu (#95), measured call counts and pause-panel extents.
- [v0.12.1 widescreen song notification](v0121-menu-song-banner.md) — feloader's `chyron` clip tagged in-band and anchored left by the renderer (#118).
- [v0.12.1 shop LOADING flash](v0121-shop-loading-flash.md) — the part loader's LOADING frame skipped during item refreshes, with recorder timings for the remaining load hitch (#119).

- [Render path: push-buffer executor and the D3D8 intercept](m2-render-path.md)
- [Vertical-blank interrupt hang](m2-vblank-isr.md)
- [Push-buffer space reservation and ring wrap](d3d-pushbuffer-wrap.md)
- [Vertex programs on the GPU](m4e-gpu-vertex-programs.md)
- [Picture options: brightness and the venue vignette](picture-options.md): the post-effect variances, the tint draw and how the options change it

## Front end and movies

- [Mod system design](mod-system-design.md): signed catalogue architecture and future override types (PR17).
- [Integrated launcher/menu RC](integrated-menu-launcher-rc-2026-10-23.md): combined candidate, hosted delivery and acceptance evidence.
- [CachyOS setup compile failure, issue #83](issue83-cachyos-notes.md): C++20 UTF-8 paths and native regression checks

- [Launcher UI proposal, 2026-10-19](launcher-ui-proposal-2026-10-19.md): Home/Advanced organization and source review
- [Launcher implementation and playtest, 2026-10-19](launcher-ui-implementation-2026-10-19.md): native UI, setup handoff and acceptance evidence
- [Separate HD widescreen menu RC2](menu-ui-separate-release-candidate-2026-10-20.md): sparse pack, measured size, actual loader registration, native evidence and remaining acceptance.
- [Menu pack hosting and PR17](menu-modpack-hosting-pr17-2026-10-20.md): independent catalogue/downloads and current provider sizing.
- [Launcher mod-manager implementation](launcher-mod-manager-implementation-2026-10-21.md): agreed local/catalogue/shared-order UI, JSON/R2 architecture, protected publishing and source-only catalogue foundation.
- [Native menu route coverage](menu-wide-scene-test-routes-2026-10-20.md) and [transition black-tile controls](menu-transition-native-black-rectangle-2026-10-20.md).

- [The loader's own script](m3-feloader-script.md)
- [How a loaded movie becomes the displayed one](m3-movie-display.md)
- [Screen transitions](m4a-fe-transitions.md)
- [Skipping the startup legal screen and movies](startup-skip-flow.md): the first-screen selector, controller initialization and native play tests; [settings and test design](startup-skip-setting.md)
  and [PR25 integration](startup-skip-pr25-integration.md), including the combined toolkit and faster regression policy.
  The [venue audit](startup-skip-venue-audit.md) records the stress-route calibration issue found during the full run.
- [Movie audio clock](movie-audio-clock.md)

## Controllers and sound

- [v0.12.3 controller latency baseline](input-latency-2026-10-30.md) and [tracing follow-up](input-latency-tracing-2026-10-30.md): reproduced rapid-tap merging, measured input boundaries and regression evidence; [capture instructions](../input-latency-tracing.md).
- [Controller input across 15 recomp projects](recomp-controller-input-practices-2026-10-30.md): pinned source audits and fighting-game design targets.
- [Controller transition correction](input-transition-fix-2026-10-30.md): guest-consumption retention, measured 1 ms acquisition, controller calibration and regression evidence.
- [Optional late controller refresh](late-input-refresh-2026-10-30.md): refresh at the existing manager update, queue-generation acknowledgements, A/B timing and validation.
- [USB enumeration state machine](m4-usb-enumeration.md)
- [Controller ports and slots](m4b-controller-ports.md)
- [DirectSound voice-list crash](dsound-voice-list-crash.md)
- [Code-segment corruption](codeseg-corruption.md)

## Gameplay state and test determinism

- [Simulation tick](simulation-tick.md)
- [Fighter record fields](fighter-record-fields.md)
- [Match-end state](match-end-state.md)
- [Combat telemetry](combat-telemetry.md)
- [Unlocking everything for playtests](unlock-everything.md): the unlock routine the retail image still carries, and the switch that calls it
- [Why a scripted fight does not replay identically](fight-determinism.md)
- [Why fight pictures do not repeat](crowd-nondeterminism.md)
- [Launching a scenario by choice: fixtures, generated routes, the crowd, cutscenes](scenario-launching.md)

## HD textures


- [Press Start fire-escape transition mapping](menu-fire-escape-transition-mapping-2026-10-20.md): ten-tile continuous scene, pan measurement and widescreen registration still required

- [Menu/UI HD + widescreen playtest pack](menu-ui-playtest-pack-2026-10-19.md): sparse 4x export, logo/emblem registration, `[wide]` side slabs and stretch overlays, Mac checks and gaps
- [UI widescreen runtime audit](ui-widescreen-audit-2026-10-17.md): exact-pin placement, loader restrictions and integration requirements
- [Feature notes](hd-textures-notes.md)
- [In-game Lanczos pilot](hd-lanczos-game-pilot.md)
- [Generation in Windows Setup](hd-setup-integration.md)
- [Acceptance, storage and release packaging](hd-texture-release-packaging.md)
- [v0.6.0 validation](hd-v060-release-validation.md)

## Widescreen

The plan built from these is in [docs/widescreen](../widescreen/README.md), which also lists their errata.

- [Prior art in ports, recomps and patches](widescreen-prior-art.md)
- [Def Jam community patches and platform support](widescreen-defjam-community.md)
- [Generative AI for widescreen extension](widescreen-genai.md)
- [The 3D projection and culling](widescreen-game-projection.md)
- [Fight, special and cinematic cameras](widescreen-game-cameras.md)
- [Front end, HUD and movies](widescreen-frontend-2d.md)
- [Renderer: canvas, viewport and classification](widescreen-renderer.md)

## Toolkit and platforms

- [Windows startup memory layout, issue #72](issue72-windows-startup-memory-layout.md): root cause, regression history, checked placement and Windows test candidate

- [Toolkit rebase acceptance](toolkit-rebase-acceptance.md)
- [Toolkit rebase ledger](toolkit-rebase-ledger.md): maps the 108 retired patch numbers to fork commits
- [macOS and Linux port survey](macos-linux-port-survey.md)

## Performance

- [Story scene performance on macOS, 2026-10-18](scene-perf-macos-2026-10-18.md): all 38 scenes at 16:9 and render scale 3, random game-side stalls, load freezes, shader compiles, and the baseline for the Deck run
- [Story scene performance on the Steam Deck, 2026-10-18](scene-perf-deck-2026-10-18.md): the same tour on a Deck OLED with and without the shader cache, and a retest by render scale and refresh rate

## Menu/UI artwork restoration and modpack handover

- [Integrated release readiness](menu-mods-release-readiness-2026-10-24.md) — owner-approved Mods layout, final wording/author edits and remaining CI, packaging, catalogue and rendering gates.

- [Owner feedback and Mods tables](mods-owner-feedback-2026-10-23.md) — modpack1.1 backlog, pause-HUD issue95, Recomp HD grouping, descending priority table and pending native visual acceptance.

- [Windows integrated menu/mods RC verification](windows-integrated-menu-mods-rc-2026-10-23.md) — packaged helper fixes, offline install/rebuild,600unit passes, native gameplay/menu results and the remaining transition coverage gap.



- [Start here: approved-art modpack implementation](../menu-ui-modpack-handover.md)

- [GameBanana, Thunderstore and Quiver with the curated catalogue](mod-storefront-integration-2026-10-21.md) — host roles, wrapper/path work and GameBanana AI-art constraint.

- [Menu RC2 R2 upload and Windows playtest](menu-ui-r2-windows-playtest-2026-10-23.md) — authorized test upload, integrity receipt and compatible-runtime prerequisite.




## Authoring records and released mods

Reusable texture authoring procedures live in [Modding Tools](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/menu-text-restoration.md). Production drafts, exact prompts, approvals and local checkpoint records are retained in the private authoring workspace. [Released menu versions](https://github.com/vyanhursky/DefJamFFNY-mods/blob/main/docs/menu-ui-hd-wide.md) are documented with the catalogue. This tree retains runtime investigations and compatibility results.
