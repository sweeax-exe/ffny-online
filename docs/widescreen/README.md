# Widescreen for Def Jam: Fight for NY — implementation pack

Branch `feat/widescreen`. Written 2026-10-11 from code reading, seven research reports and live experiments on
the Windows release build. This folder is the hand-off for the agents that implement widescreen; read it in
order. Every claim here is either measured in a run, verified in the lifted code, or marked as an assumption.

| File | What it gives an implementer |
|---|---|
| [01-findings.md](01-findings.md) | What the game already does, what the experiments showed, and the corrections to the research reports |
| [02-design.md](02-design.md) | The architecture, the settings, the coordinate maths and the rules every work package follows |
| [03-element-catalog.md](03-element-catalog.md) | Every on-screen element, how it is drawn, its widescreen treatment and how it will look |
| [04-work-packages.md](04-work-packages.md) | The tasks (WS-0 … WS-13): scope, code sites, acceptance checks, dependencies, order |
| [05-cameras-and-venues.md](05-cameras-and-venues.md) | Fight cameras in 1v1 and free-for-all, special and cinematic cameras, the venue sweep |
| [06-cutscenes-movies-and-ai.md](06-cutscenes-movies-and-ai.md) | Cutscene policy, the FMVs, full-screen backdrops, and where generative AI fits (and where it does not) |
| [07-testing-and-playtest.md](07-testing-and-playtest.md) | Automated checks, the aspect test matrix, the beta release and the play-test script |

Research behind it (all in `docs/research/`): [prior art](../research/widescreen-prior-art.md),
[community patches](../research/widescreen-defjam-community.md), [generative AI](../research/widescreen-genai.md),
[projection](../research/widescreen-game-projection.md), [cameras](../research/widescreen-game-cameras.md),
[front end and 2D](../research/widescreen-frontend-2d.md), [renderer](../research/widescreen-renderer.md).
Where a report and this folder disagree, this folder wins: it was checked against the code and the experiments
(the errata are listed in [01-findings.md](01-findings.md#5-errata-to-the-research-reports)).

## The short version

1. **The 3D is easy and already proven.** All 3D views go through one function that hands the engine a
   hard-coded 4:3 aspect (`sub_0001A4E0`). Replacing it to widen the horizontal field of view (Hor+) gives a
   correct wider picture in fights, free-for-alls, the Story hub and menus. Culling follows automatically.
   A one-on-one at 16:9 and a four-fighter free-for-all at 21:9 ran with no crash, no pop-in at the edges and
   the frame rate unchanged.
2. **Cutscenes were authored in 16:9.** The in-engine Story cutscenes use a dormant "16:9 letterbox" view mode
   (640x360 inside the 4:3 frame). Rendered at full height in a 16:9 window they fill the screen with exactly
   the framing the game's makers chose: no bars, nothing new revealed. No AI, no pillarbox.
3. **The 2D is the real work.** Menus (Flash/APT), the fight HUD, pause, popups, loading screens and movies are
   all laid out for 640x480. They must be centred in a 4:3 box (pillarboxed) or anchored to the screen edges,
   except full-screen tints, fades and dimmers, which must stretch. Because, under Hor+, the original 4:3
   picture is exactly the centre of the wide picture, centring the 2D keeps everything that sits on top of the
   3D (character previews, the gym projector, name tags) aligned.
4. **The game's own "widescreen mode" is a dead end.** `gWidescreenMode` in the Flash globals is a leftover of
   a shared EA framework; setting it to `"1"` changes nothing (tested). The Xbox video-flag answer is read and
   thrown away. The game shipped 4:3 only on every platform.
5. **Generative AI is optional polish, not a requirement.** Only about three minutes of full-screen FMV and a
   handful of full-screen 2D backdrops cannot be widened by rendering. They get a pillarbox with a blurred
   side fill by default; an optional local AI "side art" pack builder is a late, separate work package.

## What it will look like (owner-facing summary)

- **Fights (1v1 and free-for-all):** more of the arena and crowd at the sides; fighters the same size; the
  camera behaves exactly as before. Health bars move out to the screen corners (or stay in a centred 16:9 box
  on ultrawide). The KO/round text and timer stay centred.
- **Story cutscenes:** full-screen 16:9, framed as the game's makers framed them. On 21:9 they are shown in a
  16:9 box; on the Steam Deck (16:10) with two thin bars or, optionally, a slightly taller view.
- **Story hub (crib, gym, shops) and 3D menus:** the 3D room or scene behind the menu fills the screen; the menu
  panels stay centred and the right shape.
- **2D-only menus, loading posters and the intro/attract movies:** centred 4:3 with soft blurred side panels
  (or black, the player's choice). An optional user-built AI pack can replace the side panels with extended art.

## Decisions already taken in this pack (owner may override)

| # | Decision | Why |
|---|---|---|
| W1 | Widescreen is a **built-in setting**, shown in the launcher and overlay, not a separate mod. It is `off` in the first beta and becomes `auto` (match the window) once the play-test passes. | The code lives in the executable (`src/` and the toolkit fork). The mod mechanism (texture packs) is kept for the optional side-art pack only. |
| W2 | 3D uses **Hor+** at every aspect wider than 4:3; narrower windows keep 4:3 with bars. | Fixed vertical framing is what the fight camera and every authored shot were tuned for. |
| W3 | 2D placement happens **in the game code that emits it** (the native 2D quad core, the APT renderer's stage transform, the movie quad, the sub-viewport rectangle setter), not by guessing per draw in the renderer. The renderer only gets a wider canvas. A renderer-side classifier is kept as a debug aid and a fallback. | Prior art: heuristic 2D detection is the main source of widescreen bugs (RT64 #28); explicit tagging from game code is what Unleashed Recompiled and Zelda64Recomp settled on. |
| W4 | HUD: **edge-anchored up to 16:9, centred-16:9 box above** (setting `hud = edge | centered`). | Matches Unleashed/Zelda64Recomp experience; 4-player HUD corners at 32:9 are unreadable at the edges. |
| W5 | Cutscenes: **authored 16:9 framing** (fill at 16:9, 16:9 box when wider, thin bars or a slightly taller view at 16:10). | It is the makers' own framing (finding 2). |
| W6 | Goldens, the replay hash and every existing regression check run with widescreen **off** and stay bit-identical. Widescreen gets its own checks. | Keeps 4:3 a supported, unchanged mode (Unleashed's "Original 4:3" lesson). |

## Open questions for the owner

Asked in chat on 2026-10-11 and copied to `PROGRESS.md` §5 when the implementation starts:

1. First-release scope: 16:9 + 16:10 only, with 21:9/32:9 as "experimental", or all four from the start?
2. HUD default: edge-anchored (W4) or always centred?
3. Side fill for 4:3 content: blurred (proposed default) or black?
4. AI side-art packs: build the optional local tool (WS-12), or leave AI out entirely?
5. Release shape: a public `v0.8.0-beta.1` with widescreen `off` by default for play-testing, then `v0.8.0` with `auto`?
