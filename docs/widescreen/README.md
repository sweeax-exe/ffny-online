# Widescreen reference

Widescreen beta shipped in v0.9.0 on Windows, Apple Silicon macOS and Linux/Deck.
The launcher/settings guide describes player controls. This folder preserves
implemented design, findings and dated validation; open defects and additional
coverage belong in GitHub Issues, not a work-package roadmap.

| Reference | Purpose |
|---|---|
| [Findings](01-findings.md) | Measured/file-audit evidence, confidence levels and corrections |
| [Design](02-design.md) | Renderer-side2D placement,3D Hor+, actual cinema/backend behavior and coordinate maths |
| [Element catalog](03-element-catalog.md) | Content/placement taxonomy and source layer maps |
| [Cameras and venues](05-cameras-and-venues.md) | Fight/special/cinema camera behavior and venue test scope |
| [Testing](07-testing-and-playtest.md) | Aspect matrix, manual acceptance and dated results |
| [Offline FMV trial](08-fmv-ai-pipeline.md) | Optional bounded local experiment and its measured limits; no shipped AI playback |
| [Original Mac validation](09-macos-validation.md) | Historical platform acceptance and retained failures |

## Implemented behavior

Full-screen3D widens horizontally while preserving vertical framing.2D placement
is classified in the toolkit renderer; the rejected game-emitter proposal is
explained in the design. Menus/pictures preserve their shape, fills cover the
canvas, and fight HUD elements can anchor to edges or a centered16:9safe area.
Compatible menu packs add separately registered side artwork.

The game's cinema is authored16:9inside its original4:3frame. At16:9 it fills
height with the authored view. Experimental ultrawide currently uses full-height
Hor+, including in authored mode; the proposed centered16:9cinema box is not
implemented. At16:10 authored mode keeps thin top/bottom bars. D3D11 can blur
picture sides; the pinned Vulkan side-fill request is a no-op/black sides.

Auto is the current default for fresh settings; existing saved choices are
preserved.16:9/16:10 are the primary shapes;21:9/32:9 remain experimental.
A passed route does not establish every venue, overlay, late-Story scene or
physical display configuration. Keep stock4:3checks separate from pack-on/wide
acceptance and preserve failed historical results.

For source maps see [projection](../research/widescreen-game-projection.md),
[cameras](../research/widescreen-game-cameras.md), [front end](../research/widescreen-frontend-2d.md),
[renderer](../research/widescreen-renderer.md) and [prior art](../research/widescreen-prior-art.md).
The optional offline AI trial does not change runtime support or authorize
publication of generated game artwork. General authoring lives in Modding Tools.
