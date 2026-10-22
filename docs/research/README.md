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

- [Render path: push-buffer executor and the D3D8 intercept](m2-render-path.md)
- [Vertical-blank interrupt hang](m2-vblank-isr.md)
- [Push-buffer space reservation and ring wrap](d3d-pushbuffer-wrap.md)
- [Vertex programs on the GPU](m4e-gpu-vertex-programs.md)
- [Picture options: brightness and the venue vignette](picture-options.md): the post-effect variances, the tint draw and how the options change it

## Front end and movies

- [Launcher UI proposal, 2026-10-19](launcher-ui-proposal-2026-10-19.md): Home/Advanced organization and source review
- [Launcher implementation and playtest, 2026-10-19](launcher-ui-implementation-2026-10-19.md): native UI, setup handoff and acceptance evidence

- [The loader's own script](m3-feloader-script.md)
- [How a loaded movie becomes the displayed one](m3-movie-display.md)
- [Screen transitions](m4a-fe-transitions.md)
- [Skipping the startup legal screen and movies](startup-skip-flow.md): the first-screen selector, controller initialization and native play tests; [settings and test design](startup-skip-setting.md)
  and [PR25 integration](startup-skip-pr25-integration.md), including the combined toolkit and faster regression policy.
  The [venue audit](startup-skip-venue-audit.md) records the stress-route calibration issue found during the full run.
- [Movie audio clock](movie-audio-clock.md)

## Controllers and sound

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

- [Feature notes](hd-textures-notes.md)
- [Representative audition](hd-representative-review.md)
- [In-game Lanczos pilot](hd-lanczos-game-pilot.md)
- [Full Lanczos corpus](hd-full-lanczos-corpus.md)
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
