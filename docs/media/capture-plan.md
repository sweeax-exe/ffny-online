# Current-release media capture plan

The [gallery](README.md) now contains reviewed Mac gameplay, original/HD and
4:3/16:9 comparisons, the v0.10.6 Home/Advanced launcher (October 20), and a
labelled historical v0.9.0 F1 overlay. The README shows the new Home screen and uses
one owner-selected face-off 4:3/16:9 comparison and one compact 20-fps GIF
(about 4.53 MB together); videos are
linked downloads. Gameplay came from the PR #12 candidate and is labelled as
such. Widescreen beta is now released on all three platforms in v0.9.0.

## Remaining captures

- **Current setup wizard:** none in the supplied new set. Replace the historical
  v0.6.0 Windows example with neutral paths and current install/update controls;
  add Mac and Deck examples where their workflow differs.
- **Steam Deck Gaming Mode:** a clean 16:10 game view and optionally its Steam
  library entry. Mac captures cannot establish this platform's appearance.
- **More varied gameplay:** a bright second venue and a short human-controlled
  sequence would complement the dark Basement clip. An audio demonstration
  needs a new recording; all supplied published clips are silent.
- **Story and four-fighter examples:** current review contact sheets have debug
  labels/empty panels or obscure the action. Capture one polished creator/crib
  view and a clean four-fighter frame. Four fighters alone do not prove four
  human inputs; verify those before calling it local multiplayer footage.
- **Exact release gameplay:** recapture from the tagged v0.9.0 installer when
  convenient; keep the existing candidate provenance until then.

## Pick up on another machine

1. Check out the README documentation PR's `docs/readme-intro-refresh` branch
   with GitHub CLI (`gh pr checkout` followed by its PR number) or Git.
2. Build/install the selected release from your own supported dump using the
   [setup guide](../setup-installer.md) or [source build guide](../build-and-play.md).
   Initialize the pinned toolkit submodule for a source build. Record the exact
   tag and commit used; launcher captures target v0.10.6, while the gameplay
   capture plan was originally reviewed against v0.7.3.
3. Use disposable saves for scripted captures. Keep raw PNGs and recordings
   local, under the ignored `logs/` folder or outside the repository.
4. Capture the priority-one items below, select exports and review the actual
   visuals. Add only approved exports to `docs/media/`, update their provenance
   and the exact SHA-256 allowlist in `scripts/check-source-tree.py` together.
5. Replace README images/captions, link any additional examples from the media
   index and run documentation/source checks before pushing the media commit.

## Shot list

| Priority | Visual | Demonstrates | Placement |
|---|---|---|---|
| 1 | Current-release fight still | Recognizable fighters, representative venue, readable HUD and sharp rendering; choose a clear action pose. | README Gameplay hero |
| 1 | One short gameplay loop | Movement, a readable strike/grapple and recovery, without loading or menu time. | README Gameplay |
| 1 | Original versus faithful HD pair | Same camera/frame, rendering scale and brightness, changing only the texture pack. Show venue and fighter/clothing detail. | Customization or full gallery |
| 1 | F1 overlay | Actual game behind useful display, input or texture controls. Caption that the overlay does not pause. | Customization or gallery |
| 2 | Steam Deck Gaming Mode capture | Native gameplay and readable HUD; add a library shot if illustrating Steam integration. | Platform guide/gallery |
| 2 | Current launcher and setup | Current display/texture/remap controls and setup with neutral example paths. | Settings/install guides |
| 2 | Four-fighter/local-player match | Local multiplayer in a distinctive venue; verify multiple-player input before claiming it in a caption. | Gallery |
| 2 | Story creator or crib/gym | Story customization beyond versus combat, without spoilers. | Gallery |
| 1 (covered) | Widescreen comparison | Matching original/wider framing and correctly placed HUD. | Gallery; released widescreen remains beta |
| Later | Mod manager and working mod | Implemented functionality rather than a mockup of a future feature. | Add when shipped |

Static settings suit screenshots; reserve animation for movement or interaction.
Saves, rumble and hot-plug do not each need a README image. Use a focused guide
or demonstration when a still cannot provide evidence of the feature.

## Capture and export

- Use actual game pixels from the release being shown. Record platform,
  hardware, tag/commit, date and settings. Keep experimental-branch media separate.
- Use 4:3 for original/HD comparisons and matched 4:3 versus 16:9 or 16:10
  for widescreen. Record the selected picture shape; a wide display alone
  does not establish a wider 3D view. Widescreen is beta in v0.9.0.
- For desktop gameplay stills, use render scale 2 or 4 and keep the native PNG
  master at 1280×960 or 2560×1920. A roughly 960-pixel-wide README export is a
  useful starting point; link a full-resolution view when useful.
- Use borderless play or a game-frame readback for the hero. Move the cursor
  away before capture. Keep desktop strips, paths and notifications out.
- Capture UI at native size with readable text. Crop to its window/content
  boundary while preserving actual settings and labels.
- Record at the actual gameplay cadence, preferably 60 fps where the scene
  sustains it. Retain the original local recording. A silent 60-fps video
  preserves motion better than a GIF; link a reviewed hosted clip if appropriate.
- For an inline GIF, audition 3–5 seconds, 640 px wide and 20–24 fps, aiming for
  about 3 MB or less. Check text, gradients and outlines before choosing an
  export. These are starting targets, not requirements to sacrifice quality.
- Use one inline animation and stills/linked clips elsewhere. Aim for an initial
  README media budget around 5 MB; the existing GIF alone is about 6.6 MB.
- Match rendering scale, brightness, camera and subject in HD comparisons.
  Use a repeatable test state: opening the overlay does not pause the game.
  Label comparison sides and retain uncropped originals locally.
- Do not AI-enhance a capture or enlarge old footage and present it as higher
  native rendering resolution. Do not use encoded GIF cadence as a benchmark.
  Measure performance separately, with capture overhead excluded.

The harness supports native rendered pixels with `RECOMP_TRANS_SHOT_NATIVE=1`;
its normal regression captures remain 640×480. See the
[HD capture workflow](../hd-textures.md#local-automated-workflow) and
[gameplay harness guide](../09-testing-harness.md). Run one game capture/test
at a time and use disposable saves.

## Provenance and publication

For each published visual record filename, tag/commit, platform, date, scene,
render scale, aspect, texture pack/version, export dimensions/cadence, source
master location, SHA-256, owner approval and the demonstrated feature. A small
table in the [media index](README.md) is sufficient; a new generator is unnecessary.

Use stable descriptive filenames such as `gameplay-hero.png`,
`gameplay-preview.gif`, `textures-comparison.png` and `overlay.png`. Put exact
build provenance in the index and platform/HD context in visible captions.

Only concrete, owner-reviewed exports may be published. The existing
[agent instructions](../../AGENTS.md), media index and source-tree checker
require exact approved fingerprints for new or replaced media. This is not a
general permission to publish raw game assets or recordings. The game's artwork
retains its original ownership and is not covered by the project's MIT license.
