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

- [v0.12.1 Story portraits and Power Plant](v0121-portrait-viewport-read.md) — logical versus AA surface clipping, verified portrait correction, and VY3 FFA performance measurements.
- [v0.12.1 created-fighter thumbnails](v0121-created-fighter-thumbnail.md) — small guest capture surfaces and normal save/reload recovery for empty profile images.
- [v0.12.1 right-side meters](v0121-right-meters-fix.md) — a merged zero-flag snapshot correction and native health/Blazin fill validation.
- [v0.12.1 eye-color previews](v0121-eye-preview-cache.md) — small-texture cache signature collisions and independently verified palette refresh.
- [v0.12.1 widescreen HUD while paused](v0121-pause-hud-widescreen.md) — the fight HUD heartbeat carried through the pause menu (#95), measured call counts and pause-panel extents.
- [v0.12.1 widescreen song notification](v0121-menu-song-banner.md) — feloader's `chyron` clip tagged in-band and anchored left by the renderer (#118).

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

- [Popup/profile/autosave review batch](menu-popup-profile-restoration-2026-10-20.md): fifteen studies over twenty-two sources, original-core wider wall and prior batch still pending
- [Shared popup mapping](menu-shared-popup-mapping-2026-10-20.md): two-tile paper, authored header sampling and live backer clips
- [Profile and autosave mapping](menu-profile-autosave-mapping-2026-10-20.md): 640×480 backdrop crop, paper pairs and exact platform/language structure
- [Larger menu detail review batch](menu-details-restoration-2026-10-20.md): phone approved; sixteen candidates across nineteen venue/title/support source textures
- [Venue card and shadow mapping](menu-venue-frame-mapping-2026-10-20.md): five independent animated layers with live photo content
- [Title sticker/effect and support mapping](menu-title-support-mapping-2026-10-20.md): four proven sticker groups, soft radial effect and independent library icons
- [Story phone restoration draft](menu-story-phone-restoration-2026-10-20.md): source-protected casing, exact partial alpha and centered five-aspect backdrop studies
- [Story message-center native mapping](menu-story-phone-mapping-2026-10-20.md): two visible tiles, fractional UVs, offstage third part and live foreground caveat
- [Fire-escape art studies and Extras approval](menu-fire-escape-artwork-2026-10-20.md): ten-tile source reconstruction, material/framing retries and separate native placement caveat

- [Resumed Extras paper drafts](menu-extras-restoration-2026-10-20.md): nine source tiles, original-color/alpha/graphics protection, six reviews and current PR62/73 baseline
- [Press Start fire-escape transition mapping](menu-fire-escape-transition-mapping-2026-10-20.md): ten-tile continuous scene, pan measurement and widescreen registration still required

- [Menu/UI inventory and restoration pilot](menu-ui-restoration-plan-2026-10-17.md): fresh static census, comparison-tool check, imagegen/widescreen plan and owner questions
- [First menu/UI artwork trials](menu-ui-pilot-results-2026-10-17.md): original-color constraints, live workshop revisions and skyline previews through32:9
- [Skyline cloud-join audit](menu-ui-seam-audit-2026-10-17.md): structural mismatch at original bounds, final-composite prompting and inner/outer seam checks
- [Approved skyline and next background drafts](menu-ui-second-batch-results-2026-10-17.md): pinned art acceptance, reproducible recipe, remaining title/mode layers and five-aspect studies
- [Menu art approvals and next screens](menu-ui-next-scope-2026-10-17.md): approved tower, conditional Mode Select lighting check, inspected Options/Jukebox/Battle recommendations
- [Menu/UI HD + widescreen playtest pack](menu-ui-playtest-pack-2026-10-19.md): sparse 4x export, logo/emblem registration, `[wide]` side slabs and stretch overlays, Mac checks and gaps
- [Title logo and eight emblem trials](menu-logo-emblem-results-2026-10-17.md): city approval, exact source shapes, nine built-in trials and source-protected comparisons
- [Branding transparency source probes](menu-branding-transparency-audit-2026-10-18.md): clear holes, partial ink and opaque distress
- [Branding transparency repair](menu-branding-transparency-results-2026-10-18.md): nine alpha-only revisions, unchanged RGB, registered source coverage and retained retry evidence
- [Logo and Start-sticker geometry](menu-logo-shape-audit-2026-10-17.md): source UVs, real alpha/color, variant joins and vector-source check
- [Eight selected-mode emblem shapes](menu-emblem-shape-audit-2026-10-17.md): 21fragments, active joins, support-icon distinction and alpha
- [Options/Jukebox geometry](menu-options-jukebox-shape-audit-2026-10-17.md): Xbox platform variant, independent scrolling wall, masks and background joins
- [Remaining Menu/UI inventory](menu-ui-remaining-inventory-2026-10-19.md): six paper layouts approved,71 source IDs covered,1,252 registered-flow sources untouched by custom restoration and23 confirmed next priorities
- [Result and rematch paper drafts](menu-result-rematch-panels-2026-10-19.md): High Scores approval, six source-protected panels/18 tiles, alpha and five aspect placement studies
- [High Scores draft and remaining UI queue](menu-hiscores-background-results-2026-10-19.md): Rewardswallapproval, protected score board, five ratios and inspected result/paper/phone candidates
- [Rewards/Unlock wall and Battle approval](menu-rewards-unlock-backgrounds-2026-10-19.md): approved setupv5, shared3screen wall draft, exactsource alpha and explicit wide-vignette policy
- [Battle upper-left lighting v5](menu-battle-left-lighting-v5-2026-10-19.md): corrected side, retained brick detail and exact restoration of pre-v4 right artwork
- [Battle upper-right lighting v4](menu-battle-right-lighting-v4-2026-10-19.md): localized illumination correction with v3 brickwork and native center preserved
- [Battle left material v3](menu-battle-left-material-v3-2026-10-19.md): full-left source-only outpaint, native sampling/edge material calibration and retained failed studies
- [Battle setup refinement and fighter approvals](menu-battle-setup-refinement-2026-10-19.md): approved2/3/4fighter backgrounds, source-brick material repair and ceiling alignment
- [Battle background drafts](menu-battle-background-results-2026-10-18.md): four source-protected restorations, five ratios, both-join repairs and original comparisons
- [Battle background geometry](menu-battle-background-shape-audit-2026-10-17.md): match type and2/3/4fighter backgrounds, distinct IDs, padding and protected materials
- [Remaining title-layer geometry](menu-title-remaining-layers-2026-10-17.md): alpha, active UV heights, opaque padding and settled placement
- [Options softness v4](menu-options-softness-v4-2026-10-18.md): slight right defocus on existing pixels, preserved geometry/colors and versioned review
- [Jukebox approved / Options reference door v3](menu-options-door-v3-2026-10-18.md): narrow reference geometry, worn panel texture, exact Jukebox pins and preserved Options left/center
- [Options door and Jukebox join revision](menu-options-jukebox-joins-2026-10-18.md): complete right architecture, course-guided regeneration, preserved left/centers and versioned comparisons
- [Options and Jukebox background drafts](menu-options-jukebox-results-2026-10-18.md): nine branding approvals, source layers, five aspects, prompts and remaining join/scroll limitations
- [Remaining mode-layer geometry](menu-mode-remaining-layers-2026-10-17.md): lit/dark alignment, shutter/surround UVs, alpha doorway and existing offstage scenery
- [Pilot tile UV/layout proof](menu-pilot-uv-layout-2026-10-17.md): tile order, authored overlap/padding and settled title placement
- [Menu layer/texture mapping](menu-asset-mapping-2026-10-17.md): retail title and all 120 mode-select fragments
- [UI authoring-tool audit](ui-texture-tooling-audit-2026-10-17.md): standalone workshop capabilities and aspect/import boundaries
- [UI widescreen runtime audit](ui-widescreen-audit-2026-10-17.md): exact-pin placement, loader restrictions and integration requirements
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

## Menu/UI artwork restoration and modpack handover

- [Integrated release readiness](menu-mods-release-readiness-2026-10-24.md) — owner-approved Mods layout, final wording/author edits and remaining CI, packaging, catalogue and rendering gates.

- [Owner feedback and Mods tables](mods-owner-feedback-2026-10-23.md) — modpack1.1 backlog, pause-HUD issue95, Recomp HD grouping, descending priority table and pending native visual acceptance.

- [Windows integrated menu/mods RC verification](windows-integrated-menu-mods-rc-2026-10-23.md) — packaged helper fixes, offline install/rebuild,600unit passes, native gameplay/menu results and the remaining transition coverage gap.

- [Full menu image browser and widescreen focus](menu-inventory-browser-2026-10-20.md) — owner stops routine panel generation;1323/1834gallery and sourcecounts.
- [Remaining Menu/UI census](menu-ui-remaining-census-2026-10-20.md) — scoped coverage/pending/untouched totals.
- [Widescreen background gaps](menu-widescreen-background-gaps-2026-10-20.md) — ten unreviewed tiles in five candidate groups, native usage unverified.
- [Story/shop paper mapping](menu-story-shop-mapping-2026-10-20.md) — deferred ten-panel source audit.

- [Gym/controls restoration and linked mouse review](menu-controls-restoration-2026-10-20.md) — fifteen studies,22sources; popup approval and review navigation.
- [Gym and shared popup source mapping](menu-ui-next-batch-mapping-2026-10-20.md) — complete shapes50/60/64 and unknown popup consumers.

- [Start here: approved-art modpack implementation](../menu-ui-modpack-handover.md)

- [GameBanana, Thunderstore and Quiver with the curated catalogue](mod-storefront-integration-2026-10-21.md) — host roles, wrapper/path work and GameBanana AI-art constraint.

- [Menu RC2 R2 upload and Windows playtest](menu-ui-r2-windows-playtest-2026-10-23.md) — authorized test upload, integrity receipt and compatible-runtime prerequisite.
