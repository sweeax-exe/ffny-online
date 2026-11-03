# src/patches — gameplay patches (opt-in)

Reserved for behavioral changes to the game itself, such as a widescreen view.
Each goes behind a runtime setting so the unmodified path stays testable. CMake
picks up every `.c` file here. Fixes that make the game run belong in `src/hooks`
or `src/recomp_manual.c`.

- `dev_unlock.c`: `RECOMP_UNLOCK_ALL=1` (every fighter and venue, through the developers'
  own routine at `0x000158F0`) and `RECOMP_DEV_DEBUG=1` (the in-fight debug overlay). Both off
  by default; [the investigation](../../docs/research/unlock-everything.md).
- `skip_intro.c`: `[debug] skip_intro` / `RECOMP_SKIP_INTRO=1` selects Press Start on the
  first screen load, retaining native controller initialization. Off by default; later
  Story and attract movies are unchanged. [The investigation](../../docs/research/startup-skip-flow.md).
- `subtitles.c`: `[language] story_subtitles` (or a language pack's request) shows Story subtitles in
  every cutscene, not only the opening one: after guest `0x000DA8A0` loads a cutscene's subtitles, its
  enable step is applied whatever the scene. Off by default ([language packs](../../docs/language-packs.md)).
- `game_language.c`: `[language] game = french` starts the game in the French built into the disc (string
  table `cmfren.loc`): the boot call to guest `0x00044FC0` asks for language 3 instead of 1. English by
  default, and never while a language mod is on ([the game's own French](../../docs/language-packs.md#the-games-own-french)).
- `picture.c`: `[display] brightness` (a curve on top of the title's gamma ramp), `vignette` and
  `vignette_strength` (the venue post effect's darkened edges: removed, or blended towards the
  undarkened picture). Defaults leave the picture exactly as before; all three apply as they change.
  [The investigation](../../docs/research/picture-options.md).
