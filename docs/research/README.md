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

## Front end and movies

- [The loader's own script](m3-feloader-script.md)
- [How a loaded movie becomes the displayed one](m3-movie-display.md)
- [Screen transitions](m4a-fe-transitions.md)
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
- [Why a scripted fight does not replay identically](fight-determinism.md)
- [Why fight pictures do not repeat](crowd-nondeterminism.md)

## HD textures

- [Feature notes](hd-textures-notes.md)
- [Representative audition](hd-representative-review.md)
- [In-game Lanczos pilot](hd-lanczos-game-pilot.md)
- [Full Lanczos corpus](hd-full-lanczos-corpus.md)
- [Generation in Windows Setup](hd-setup-integration.md)
- [Acceptance, storage and release packaging](hd-texture-release-packaging.md)
- [v0.6.0 validation](hd-v060-release-validation.md)

## Toolkit and platforms

- [Toolkit rebase acceptance](toolkit-rebase-acceptance.md)
- [Toolkit rebase ledger](toolkit-rebase-ledger.md): maps the 108 retired patch numbers to fork commits
- [macOS and Linux port survey](macos-linux-port-survey.md)
