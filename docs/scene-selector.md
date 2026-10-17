# Playing a chosen Story scene

The Story's cutscenes are in-engine scenes, not movies: the game loads one by name
(`sc00` to `sc21`, then a `gf` set) from `assets/cinema/<name>.viv` and plays it with the
fighters, sets and cameras of the engine. A test switch makes the game play the scene
you name in the place of the first scene it loads, so any scene can be watched, recorded
and compared without playing the Story up to it.

It is a test control: nothing changes unless the variable below is set, and the game's
own data is not modified (the game is pointed at another entry of its own scene table).

## Which scenes there are

```bash
python scripts/scenarios.py scenes
```

lists the scenes in your dump (38 in the USA Xbox version: `sc00`-`sc06`, `sc07a`, `sc07b`,
`sc08`-`sc21`, `gf1a`-`gf3c`). Which scene is which is not mapped yet; the tour below
is the way to find out. Record what you learn in `config/scenario-catalog.json`
(`cutscenes`).

## By hand

Start the game with `RECOMP_TEST_CINEMA_SCENE` set to the scene's name, then start Story
Mode with a profile that has **no Story progress yet** (its Story begins with the
prologue scene, `sc00`). Your scene plays in the prologue's place, then the Story goes
on as usual.

macOS and Linux (from the repository folder):

```bash
RECOMP_TEST_CINEMA_SCENE=sc07a ./build/posix-release/defjam_recomp
```

Windows (PowerShell, from the repository folder):

```powershell
$env:RECOMP_TEST_CINEMA_SCENE = "sc07a"; .\build\win-x64-release\defjam_recomp.exe
```

The log says what happened: `[CINEMA] scene sc00` for every scene the game loads, then
`[CINEMA] sc00 replaced by sc07a`. A name the game does not have is refused and the log
lists the ones it has. Only the first scene of the run is replaced; quit and start
again for another one. With `--record` the session's `events.jsonl` also has a
`cinema` event for every scene.

## Many scenes in a row, unattended

```bash
python scripts/scenarios.py scenes sc05 sc07a gf1a --fixture <fixture>
python scripts/scenarios.py scenes all --fixture <fixture>        # every scene, about 5 minutes each
```

Each scene is a separate scripted run: the `crib` route (`--from` picks another) starts
Story with the fixture's profiles, the scene plays in the first scene's place, and a
capture is taken every 6 seconds for 90 seconds after Story starts (`--every`,
`--secs`). The result is a folder `logs/scene-tour/<time>/` with a labelled contact sheet
per scene (`sc07a.png`) and `index.md`, a table of every scene, whether it played and
where its run's report is. Add `--record` to keep a play-test session of each.

The fixture has to be a save whose route reaches Story at its first scene: with the
`crib` route that is the third profile in the list (`down, down` on the user ID screen),
and it must have no Story progress. Make one with
`python scripts/scenarios.py fixture add <name> --story new-story --note "..."` from such
a save. (On the development Mac the `mac-profiles` fixture is one.)

The pictures are the game's own: keep `logs/` on your computer, do not commit or share it.

## How it works

`sub_00073FD0` loads a scene; its first argument points into the image's table of scene
names at guest `0x0028CA28` (one name every 8 bytes). An `_enter` hook
(`src/hooks/fe_inject.c`, `fe_cinema_scene_enter`) logs each name and, when the variable
is set, points the first call's argument at the entry of the chosen scene. The research
behind it is in `docs/research/scenario-launching.md`.
