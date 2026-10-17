# Launching a scenario by choice: fixtures, generated routes, the crowd, cutscenes

2026-10-14. Asked by Vlad: as bug-fixing moves to short-lived faults at particular
venues, fighters and Story points, can a test start *there*, repeatably, and can the
crowd's randomness be controlled and any in-engine cutscene be launched by choice.
Written on a machine with no game dump: everything below that needs the game is
marked **to run**, with the exact command.

## What exists now

| Need | Tool | State |
|---|---|---|
| Start at a Story point | `scripts/scenarios.py fixture add NAME --story ID` copies the saved profiles into a library in the data folder; any scenario runs on a throw-away copy | built, unit-tested; library to fill **to run** |
| A fight at a venue with chosen fighters | `scripts/scenarios.py generate --mode --venue --p1 --cpu` composes a route from `config/scenario-catalog.json`; `--expect-character SLOT:INDEX` checks the result from telemetry | built; the six calibrated combinations reproduce the harness routes exactly (tested); new venues and fighter moves **to calibrate** |
| A fight that repeats | `--seed N` (pinned seeds, step-timed input) | existed (`docs/09-testing-harness.md`) |
| A crowd that repeats | `RECOMP_TEST_CROWD=hold,reseed`, `RECOMP_WORKERS=inline`; `scripts/crowd-lab.py run` tests them | built, unit-tested against a fake guest; effect on the game **to run** |
| Fight baselines despite the crowd | `scripts/crowd-lab.py mask` learns the cells that move between runs | built, unit-tested on synthetic pictures |
| A cutscene on demand | `RECOMP_FE_INJECT` sends the front end's own command channel any command; `scripts/funccall-catalog.py` lists the commands; `scripts/scenarios.py cutscene` | mechanism built and tested against a fake guest; the scene command **to find** |
| Evidence from any of these | `--record` (`docs/playtest-recorder.md`) | built |

## Fixtures: a library of Story points

There is no savestate in a recompilation of this kind: the game's state is spread
over guest memory, host threads, the GPU translator and the audio model. The game's
own saves are the practical substitute, and the harness already runs every scenario
on a hashed throw-away copy of one (`scenario_suite.py`, fixtures).

What was missing was a *library*: named fixtures at known points, findable by name.

```
python scripts/scenarios.py fixture add crib-day1 --story crib --note "after the first fight, 3 unread messages"
python scripts/scenarios.py fixture list
python scripts/scenarios.py fixture verify
```

`add` copies `<data>/save/UserData` (or `--from`) into `<data>/fixtures/NAME/` with a
hash of every file, and records the name, the Story point and the note in
`<data>/fixtures/library.json`. Nothing goes into the repository: the saves are the
player's. The catalog lists the Story points worth having (`story_points`); add an id
for each new one. Two cautions from the harness: a route is calibrated for a profile
list (how many profiles, which has a Story save), and unread messages change the
crib's menu. Save each fixture with one profile where possible.

**To run:** play to each point worth testing and `fixture add` it: at least
*new-profile* (no save), *crib* (just after the prologue), and one fixture before
each Story fight that matters.

## Generated fight routes

`scripts/scenarios.py generate` composes the harness's own menu blocks:
`MAIN_MENU_BATTLE`, the profile choice, the match type (`modes` in the catalog), the
player's fighter (`--p1`, the `controllersetup(0,0=` stage), the CPU's (`--cpu`),
and the venue (`venues`: moves on the venue map, then A). For the combinations the
harness already had (One on One and Free For All, at the Foundation and the
Terrordome, short and to the result) it produces the harness's routes exactly, which
the unit tests check field by field; so a generated route is as good as a
hand-written one where it overlaps, and new venues and fighters are a catalog edit.

Fighter and venue moves are not yet mapped. Calibrating one is a run:

```
python scripts/scenarios.py generate --mode one-on-one --venue foundation --fixture crib-day1 \
    --p1 "right@4;right@5;a@7;a@12;a@16;a@20;a@24;a@28" --expect-character 0:5 --record
```

`--expect-character` reads the `match_start` telemetry (each slot's character index)
and fails when the menus picked another fighter, so a calibration that drifts is
caught. Record what each move list picks in the catalog. A venue: add
`"<name>": {"steps": "right@6;down@7", "calibrated": false}` and look at the captures
(the venue's name is on the map screen), then set `calibrated`.

## The crowd

`docs/research/crowd-nondeterminism.md` measured why a pinned fight's pictures do not
repeat: the two crowds start animating in the match's loading window and leave it in
a slightly different state every run. The background crowd (`sub_000DB870`, gated
by the byte at `0x2E3488`) is updated 902 times in one run's window and 903 in
another's, each update drawing from the C runtime's `rand`; the foreground
spectators draw their animation variants from the generator at `0x375F68` a
different number of times. Three controls are now available, all test-only:

1. **hold** (`RECOMP_TEST_CROWD=hold[:N]`, `src/hooks/crowd_control.c`): let the
   background crowd update N times after match set-up (default 880, below every
   count measured), then clear `0x2E3488` until the first fight step and restore
   it. Every run reaches the fight with exactly N updates, and so with the same
   number of crowd draws. The earlier attempt cleared the byte for the whole window
   and crashed at the first step because the first updates also set up the crowd's
   drawing; holding after N updates keeps that set-up.
2. **reseed** (`RECOMP_TEST_CROWD=reseed`, with `RECOMP_TEST_RNG_SEED`): at the first
   fight step, re-seed every object-form generator that was seeded during set-up
   (`0x375F68`, `0x376FDC`, ...) through the game's own seeding routine
   `sub_000AA0D0`. Whatever was drawn in the window, the draws from the first step
   on are the same every run. It changes the fight's state stream (one of these
   generators feeds the fight; the earlier note recorded that), so a golden stream
   has to be recorded with it on.
3. **workers inline** (`RECOMP_WORKERS=inline`, the toolkit's): runs the title's
   worker threads on their caller's thread. The window's length in screen updates
   was the same in both runs (906) while the crowd's update count differed, which
   points at something host-timed inside the window; the worker threads are the
   likeliest. Inline workers have been used as a diagnostic before
   (`docs/03-workflows.md`); whether the title runs a whole match that way is
   unknown.

**To run:**

```
python scripts/crowd-lab.py run --fixture crib-day1 --variants none,hold,reseed,both,workers,workers-both
```

Two pinned fights per variant; the report (`logs/crowd-lab/<time>/report.md`) has,
per variant: whether the `[TEST-DRAWS]` counts agree at step 1 (the criterion the
research note set), the crowd update counts, where the state streams part, and the
share of each step capture that differs. Expected: `none` disagrees at step 1 and
differs 10-20 percent; `hold` makes the background crowd's counts equal; `both`
should leave only what the spectators' in-flight animations carry over. A variant
that gives equal counts and near-zero picture differences becomes the default for
visual fight baselines (and needs a new `tests/golden/fight-stream.json`, recorded
with it on).

**Whatever the lab finds, masks work now.** `crowd-lab.py mask` compares captures of
the same fight step from several runs and writes the 16-pixel cells that differ as
rectangles in the baseline manifest's `masks` format. Fighters and HUD repeat, so
in a healthy pair only the crowd differs. If more than 30 percent of the picture
moves, the runs parted for another reason (a spectator shoved a fighter: the state
streams will say where) and it refuses to write a mask that would hide the fight.

## Cutscenes

What is known (`docs/research/widescreen-game-cameras.md` section 4,
`docs/widescreen/06-cutscenes-movies-and-ai.md`):

- Story cutscenes are in-engine scenes run by the `CINEMA` subsystem
  (`CINEMA::CCinemaSubsystem::LoadScene`, assert strings from guest `0x29C715`),
  loaded from `assets/cinema/*.viv` (`sc00`..`sc21`, and a `gf*` set). The cinema
  update path is `sub_00078A40` -> `sub_0005BE50` -> the camera sampler
  `sub_00070DF0`. FMV movies (`movies/*.mad`) are separate.
- A cutscene's view is mode 1 (`sub_0001A4E0`, replaced in `src/recomp_manual.c`
  for widescreen). The recorder now logs that as a `view` event with
  `cutscene: true`, so every session says when a scene starts and ends.
- The front end reaches the game only through FuncCall strings, dispatched by
  `sub_00068BA0` (`Game.StartStoryMode()`, `Control.GetScreenInfo(...)`). The intro
  route shows Story mode starting from one such command.

What was built: **RECOMP_FE_INJECT** (`src/hooks/fe_inject.c`). After the Nth front-end
call containing an anchor, it hands `sub_00068BA0` a command of the test's choosing,
on the game's thread, with every register restored, exactly as a menu would. Any
command a screen can send, a test can now send at any moment a screen is up.

What is not known: **which command, if any, plays a chosen scene.** Three ways to
find out, cheapest first. **To run:**

1. `python scripts/funccall-catalog.py --grep cinema --grep scene` lists every
   command string in every front-end movie (grouped by module, with the screens
   that hold them), keyword strings in the movies and in `default.xbe` with their
   guest addresses, and the scene files. A Story-module command taking a scene or
   chapter (for example something like `Story.PlayCinema(` or a debug or replay
   screen's command) is the lead. Try it:

   ```
   python scripts/scenarios.py cutscene --fixture crib-day1 --from crib \
       --inject "getscreeninfo(story/crib#1=>Story.Candidate(sc05)"
   ```

   It reaches the crib, sends the command once, captures every three seconds for 90
   seconds and records the session; `[FE-INJECT]` lines and the recorder's `view`
   events say whether a cutscene view followed. When one works, put the command in
   the catalog's `cutscenes.launch_command` as a format with `%s` for the scene, and
   `scenarios.py cutscene --scene sc05` uses it from then on.

2. If no command takes a scene, find what does: `python scripts/string-va.py
   "LoadScene"` (and the other `CCinemaSubsystem` assert strings) gives their guest
   addresses; grep the lifted C (`src/recomp/gen`) for those constants to find
   `LoadScene` itself, then `scripts/callers.py <va> --depth 3` up to the Story code
   that picks the scene. With `RECOMP_WATCH_EXEC=<LoadScene>` and
   `RECOMP_WATCH_EXEC_STR=1` during the intro route, the log shows the scene name it
   is handed. Then a `sub_XXXXXXXX_enter` hook (re-lift) can replace that name with
   one from an environment variable (`RECOMP_CINEMA_SCENE`): the game then plays the
   chosen scene wherever it would have played its own. That is the robust launcher,
   and the same hook can log every scene by name for the recorder.

3. Failing both, a fixture just before each scene plus the route that triggers it
   (`--from` with extra `--stage`s) reaches every scene the long way, once each
   fixture exists.

### Superseded, 2026-10-17

Scenes are now played by the game's own developer cinema mode, in its developer run module
rebuilt from the host: [dev-module-revival.md](dev-module-revival.md), user guide
[../scene-selector.md](../scene-selector.md). `RECOMP_TEST_CINEMA_SCENE` and the fixture
route below were removed; the loader hook still logs every scene (`[SCENE] loading`).

### Found on macOS, 2026-10-14

- **No front-end command names a scene.** `funccall-catalog.py --grep cinema --grep scene`
  lists 444 commands in 113 modules; the scene-like ones are FMV movies
  (`Extra.PlayMovie(`, `Game.ShowFightingMovie(`), voicemail (`Crib.PlayVoiceMail(`) and
  Story flow (`Game.LoadNextRunModule()`, `Game.StoryModeKick(`, `Crib.FightMatch(`).
  `Game.LoadNextRunModule()` was tried once; its anchor (the crib) was not reached, see below.
- **The loader.** `assets/cinema/%s.viv` (guest `0x29C488`) and `assets/nis_%s.bin`
  (`0x29C474`) are formatted in `sub_00073FD0` (thiscall, the scene's name as its first
  argument; called from `sub_00076C60` and `sub_0007B150`, above them `sub_0002D4F0` and
  `sub_0002DBE0`). `RECOMP_WATCH_EXEC` cannot watch it on Apple Silicon (x86 debug
  registers), so it now has an `_enter` hook (`src/hooks/fe_inject.c`): every scene is
  logged as `[CINEMA] scene scNN` and as a recorder `cinema` event.
- **The launcher.** `RECOMP_TEST_CINEMA_SCENE=scNN` plays that scene in place of the first
  one the game loads. A profile with no Story progress loads the prologue's `sc00` right
  after `Game.StartStoryMode`; with the `mac-profiles` fixture the `crib` route's profile is
  one, so `scenarios.py cutscene --fixture mac-profiles --from crib --scene sc05` played an
  in-engine scene for about 65 s (only `sc05`'s files opened), then the character screen.
  The catalog's `cutscenes.launch_env` holds this. The name is rewritten in the game's own
  buffer, so a longer name is refused (every `scNN` fits).
- **The view signal is weak.** View mode 1 was set once during `sc00` (at 193 s of 144-213 s
  of scene) and never during `sc05`. The `cinema` event is the signal to use.
- **The intro route** (a new user ID) plays only movies (`thx`, `eagames`, `intro.mad`)
  before the character creator; no cinema scene in its window.

## Evidence limits

- Nothing above ran against the game. The C controls were built against the real
  generated-code header and run against a fake guest; the scripts were unit-tested
  on synthetic data; the generated routes are proven only where they equal the
  harness's.
- `hold` assumes `0x2E3488` is tested before each update and that nonzero means
  "update"; the earlier experiment (clearing it stopped the updates) supports both.
- `reseed` assumes `sub_000AA0D0` is `thiscall` with two stack arguments, as the
  existing probe reads it (`defjam_test_generator_seed`); the helper restores the
  stack either way.
- Injecting a command in the middle of the front end's own call is what a screen
  does when it makes two calls in a row; a command that changes screen may still
  meet state the menus never produce. Treat a crash there as a finding about the
  command, not the game.
