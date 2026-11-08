# Gameplay test harness

How to run scripted gameplay checks and read what they prove. Current policy is
in [the test plan](02-test-plan.md); future coverage belongs in GitHub Issues.

Everything here needs a local build made from your own dump. Hosted CI has no
game data; it runs unit/native fixtures, source compilation and game-free Setup checks. Captures, audio and reports are the
game's artwork and stay under `logs/`, which git ignores.

## Three levels

| Command | Time | What it is for |
|---|---|---|
| `python scripts/regress.py --quick` | about 10 min | after every change: unit tests, three golden frames, one fight |
| `python scripts/regress.py` | about an hour | once per platform on each release candidate ([releasing](releasing.md)); a pull request runs `--only` checks for its area |
| `python scripts/scenario_suite.py run <route> ...` | one launch | one scenario with all its evidence and a browsable report |

Never run two of these at once: they share the save area, the window and the
frame-rate measurements.

Leave `RECOMP_HEADLESS` unset for the full suite: its presence suppresses the
launcher, so the launcher/checkbox checks cannot run in that mode.

Harness and scenario runs skip the startup legal screen and EA/THX/intro movies
by default. Both quick and full regression keep one complete normal startup in
`m3`: it requires legal, all three movies and Press Start in order, then compares
the title image. Other checks use the shorter boot. Story scenes and move previews
remain enabled. Input and timed captures start at the actual Press Start screen
request, so either boot path works without changing route timings.

For an individual normal boot, pass `--env RECOMP_SKIP_INTRO=0` to the harness.
An empty override (`--env RECOMP_SKIP_INTRO=`) removes the test default and uses
the specified INI or the game's default-off setting. This test default does not
change a player's settings file or normal game launches.

The named Terrordome stress routes and generated Terrordome scenarios also select retail venue FE ID6 through
ChooseVenue before loading; the stress checks require its final StartGame observation. Unlocking every venue changes map traversal;
the old one-step-right recipe selected Hunt's Point (FE ID14) with the test defaults.
The positional recipe in the scenario catalog remains dependent on the exact save
and unlock state; the early command makes Terrordome selection independent of that map order.

The full regression is the quick one plus a four-fighter Free For All, the same
at the Terrordome for four minutes, the **combat check**, a **two-pad match**, the
**replay check**, the Story intro, crib, gym and a boot soak. `--only a,b` picks
checks; `fight-terrordome`, `ffa-result`, `two-matches`, `fight-cycles`, `repeat` and `visual`
are selectable that way only.

The harness explicitly sets `RECOMP_ASPECT=4:3` for the reviewed goldens and replays; new player
defaults can use Widescreen (Auto) without changing those baselines. An explicit per-run
`RECOMP_ASPECT` overrides this test choice; an empty value lets the settings file/default apply.

For native launcher help checks, `RECOMP_LAUNCHER_SCRIPT` accepts `<ms>:ui:<id>` to click a named
control and `<ms>:hover:<id>` to reveal/hover it without editing its value (for example,
`600:ui:Advanced;1200:hover:display.render_scale`). `<ms>:uiopt:<id>` clicks it if it is there and says
nothing if not, for a control that exists in one layout only: a narrow or large-text launcher (the Steam Deck's)
draws an on/off setting as an Off/On list, whose items are `<section>.<key>.on` and `.off`, where a wide one
draws a checkbox (`2000:ui:display.vignette;2600:uiopt:display.vignette.off`). Captures and quit use the
existing script actions.

## The combat check

`python scripts/regress.py --only combat`, or directly:

```powershell
python scripts/scenario_suite.py run fight-result --fixture logs/fixtures/NAME --require-combat
```

It plays a One on One until the game asks for the match summary and asserts, from
the game's own state rather than from the picture:

| Assertion | Proves |
|---|---|
| `combat.setup` | the match started with two fighter records, one human-controlled |
| `combat.movement` | the player's fighter walked two units while a direction was held |
| `combat.attack` | a hit by the player's fighter resolved within a second of an action press |
| `combat.damage` | that hit's target lost health within a few simulation steps |
| `combat.result` | the game recorded exactly one decisive result with a winner and a loser, and its "decided" phase named the same winner |
| `combat.results_screen` | the front end requested the match summary after that |

Two more assertions appear where they apply:

| Assertion | Route | Proves |
|---|---|---|
| `combat.eliminations` | `ffa-result` | every fighter but the winner was recorded as put out, each once, and ended at zero health |
| `combat.two_players` | `versus` | both fighters are pad-controlled, each walked, and each landed a hit that did damage |

`versus` plugs in a second emulated controller: both pads press START to join, pick
their fighters and fight, so the second pad's whole path through the USB model is
exercised. `two-matches` plays a One on One to its result, goes back through the
summary and the menus, and plays a Free For All to its result; each match is judged
on its own events (`combat.result.match1`, `combat.eliminations.match2`, ...).

A fight that renders but ignores the pad, or never causes damage, or never ends,
fails. With no telemetry in the log the check is `blocked`, which is a failure,
never a skip. It does not prove which move was performed or why the fight ended;
[combat telemetry](research/combat-telemetry.md) says exactly what was confirmed
against live fights.

The same run also asserts audio health, frame pacing and memory growth (below).

## Scenario reports

Each `scenario_suite.py run` writes `logs/scenarios/<route>-<stamp>/` with
`report.json`, `junit.xml`, `index.html`, the game log, captures, an audio preview
and any image differences. The report records the source and build identity, the
machine and GPU driver, the fixture's hashes, the settings, the input schedule and
which gates were selected. A gate that could not be evaluated is an error.

Routes: `fight`, `fight-result`, `ffa`, `ffa-result`, `versus` (two pads),
`two-matches` (a One on One and a Free For All in one launch), `fight-cycles` (five One on
Ones in one launch, then a held menu), `fight-terrordome`,
`ffa-terrordome`, `story-tour` (crib and gym in one launch), `intro`, `crib`, `gym`,
`boot`, `unlock`.

Options:

- `--require-combat` the combat assertions above (the result part only on
  `fight-result`, which plays to the end).
- `--observe-combat` record the telemetry without requiring it.
- `--no-audio` / `--audio` fight routes check audio by default.
- `--baselines <manifest>` compare captures with locally approved images.
- `--step-input`, `--rng-seed`, `--stream-out`, `--stream-expect` repeatable fights (below).
- `--step-shots N,N` capture the frame at those fight steps.
- `--settings file.json` render scale, gamma and vsync for the run.
- `--session-plan file.json` experimental: live input driven by screen events.
- `--record` turn the [play-test recorder](playtest-recorder.md) on: the run's frame rows, events and
  pictures are kept, analysed with `scripts/bug-bundle.py` and linked from the report; `--record-hitch-ms N`
  also saves a bundle for every frame of N ms or more.

## Repeatable fights

By default the scripted fighter's input is timed on the host's clock and the CPU
fighters seed their random numbers from the time-stamp counter, so the same script
plays a different fight every run. Two options change that:

- `--step-input default` (or a file) drives the player's pad from a table indexed
  by the game's simulation step for the whole match. The host-timed script still
  works the menus.
- `--rng-seed N` pins the seed of the match's random number generator and of the
  CPU fighters' own generators.

With both, the game's state is the same from run to run: three replays reproduced a
recorded thirty-second stream exactly. `--stream-out FILE` records that stream
(positions, health, buttons, events for the first `--stream-steps` steps, 1,800 by
default) and `--stream-expect FILE` requires a run to reproduce it, naming the step
and the fields where it first differs.

The stream came out identical on the Release and Debug builds and on two different
save fixtures, so its hash is kept in `tests/golden/fight-stream.json` as a record
of how the fight plays. The full regression's `replay` check plays the pinned fight
once and compares with that hash: a change to the translator or the runtime that
alters the simulation fails it even if the picture still looks right.

On a slow host the same pinned fight can part from itself (the Steam Deck, #98): the spectators' positions
follow the host's timing, two match-logic generators are seeded from the time-stamp counter, and a
general-purpose generator is drawn once per presented frame. `--isolate-crowd` (on `scenario_suite.py run` and
on `regress.py`) removes those, changes how the fight plays and so has its own golden,
`tests/golden/fight-stream-isolated.json`; `docs/research/deck-determinism-98.md` has the evidence.

```powershell
python scripts/regress.py --only replay            # one fight against the golden hash
python scripts/regress.py --only repeat            # two fights against each other, then the hash
python scripts/regress.py --only repeat --update-golden
```

`repeat` tells a change in play from a disturbed run; use `--update-golden` when a
change is meant to alter play. Longer stretches usually repeat too, but
one pair of fights parted after 52 seconds for a reason not yet found, so the
default comparison stops at thirty. A recorded stream is tied to the build and the
fixture; record a new one after a change that is meant to alter play.

Both options write to the game's memory (the pad table and two seed arguments).
They are test controls, off unless asked for.

## Save fixtures

A scenario never runs on your save folder. It runs on a throw-away copy of a
*fixture*: a small folder holding profiles and options with a hash of every file.

```powershell
python scripts/scenario_suite.py fixture --source C:/LOCAL/prepared-save --output logs/fixtures/NAME --profile "who and how far"
```

The regression's combat check makes one automatically from `save/UserData` in the
data folder (a few hundred kilobytes; the gigabytes of cache are left out and the
source is only read). Menu navigation depends on the profiles: how many there are,
their progression and unread messages. A route calibrated for one profile list is
not guaranteed on another.

The other regression checks still use the whole save folder with the copy-aside
guard, which is why they are slower. Do not stop one mid-run: the guard restores
the saves when the run ends.

### Every fighter and venue, on any profile

The harness (and so `regress.py` and the scenario suite) turns `RECOMP_UNLOCK_ALL` on for every
run it starts; `--env RECOMP_UNLOCK_ALL=0` turns it off for one (the scenario suite drops inherited `RECOMP_*`
variables, so it takes `--no-unlock`). The `replay` and `repeat` checks use `--no-unlock`: with the unlock
on, the pinned fight plays differently (95 records against the golden 80; the cause is not known), and
the golden was recorded without it. For a game you start yourself the
switch is the `[debug] unlock_all` setting (the Diagnostics page of the launcher and overlay), or the
variable. `RECOMP_UNLOCK_ALL=1` runs the developers' own unlock routine (guest `0x000158F0`, still in the
retail image) on each screen change once the main menu has started: all 74 fighters and every
venue are available, whatever the profile holds, a new one included. It is runtime-only and off
unless asked for:

```bash
python scripts/harness.py run unlock --env RECOMP_UNLOCK_ALL=1     # Unlock Fighters: 74 / 74
python scripts/harness.py run fight --env RECOMP_UNLOCK_ALL=1 --env RECOMP_DEV_DEBUG=1
```

`RECOMP_DEV_DEBUG=1` adds the in-fight debug overlay. To keep the save area as a run left it
(the guard still restores the original afterwards) set `DEFJAM_HARNESS_SNAPSHOT_SAVE=<dir>`.
It does not unlock Story progress, songs or items. How it was found, and what was checked:
[unlocking everything](research/unlock-everything.md).

The unlock changes how the pinned fight plays (95 state records against the golden 80, every time; the
cause is not known), so `replay` and `repeat` run without it: `scripts/scenario_suite.py run ... --no-unlock`.

### Choosing the fighters and the venue

A fight route plays "defaults all the way"; these switches replace the choice at the moment the match is
made, so any fighter on any venue can be tested without scripting the menus:

| Variable | Meaning |
|---|---|
| `RECOMP_START_FIGHTERS=a,b[,c,d]` | The fighter in each slot, by `<ID>` in `fighter.xml` (the default One on One is `56,55`, Blaze and Doc). Fewer ids than slots leave the rest as chosen. |
| `RECOMP_START_VENUE=n` | The venue, as the `<feID>` number in `venue.xml` that `Game.ChooseVenue(n)` carries (1 is The Foundation, the default; 17 is Gun Hill Garage). StartGame also maps it through a compact 13-entry table (0-5, 8, 10-12, 17, 24, 26), with a zero fallback, while separately loading venue data by the original FE ID. That compact field alone does not identify the rendered arena; verify the loading caption and fight. See `docs/research/startup-skip-venue-audit.md`. |
| `RECOMP_START_LOG=1` | Print what `Game.StartGame` is about to be given (`[START]` in the log). Setting either variable above prints it too. |

```bash
python scripts/harness.py run fight --env RECOMP_START_FIGHTERS=3,20 --env RECOMP_START_VENUE=17
```

The menus still run up to Fight, and only the choices are replaced. The ids need no unlock, because
nothing on the way to the match checks them. Only the two slots a One on One fills were tried (a
four-fighter mode takes four ids but was not run). The combat telemetry reports the engine's index for a
fighter, not the `<ID>`, so check the names on the HUD. The addresses and the runs are in
[unlocking everything](research/unlock-everything.md#starting-a-match-on-a-chosen-fighter-and-venue).

## Audio, pacing and memory

- **Audio.** The generated PCM must be long enough, not clipped and not silent,
  and the device queue must not run dry. Dropped buffers are allowed at one per
  five minutes, plus one when a result was recorded: both fights played to a
  knockout dropped exactly one buffer in the slow motion after it. PCM is captured
  before it reaches the device and is aligned to events only to within the log
  polling interval, so it cannot check that a particular sound played.
- **Frame pacing.** From the runtime's two-second reports during the fight: the
  median present interval within 1 ms of 16.67, and at most 2 percent of presents
  later than 1.5 times their window's median.
- **Memory.** Samples every 5 seconds. Windows samples private bytes; POSIX samples
  RSS despite the JSON field name `private_bytes`. These OS metrics are not directly
  comparable, and a positive delta is not an attributed leak.
  - **Baseline:** the highest of the first three samples from 20 seconds after
    `Game.StartGame`.
  - **`performance.memory_growth`** (limit 64 MiB): compares the baseline with the
    highest of the last three samples in the last match's settled fight window, which
    runs from 20 seconds after its `StartGame` to its first `GetMatchSummary`. Its
    metrics give the same figure for every match (`cycle_growth_mb`).
  - **`performance.memory_transition`** (limit 256 MiB): reports scene changes
    separately. It bounds the highest sample of play against the baseline, and also
    reports where the run ended. A sample taken in a result or menu load holds decode
    buffers in flight and, on Linux, free memory in the decode workers' glibc arenas
    (issue #166), so it is not mixed into the growth figure.
  - **Endpoint fallback:** without a timeline, or when the first or last fight window
    has fewer than three samples, growth falls back to the old last-sample reading
    (`method: endpoint`) and no transition row is written.
  - **Repeated cycles:** `--only fight-cycles` plays five One on Ones in one launch.
    This is the most the native pad script holds (4095 characters, 32 distinct
    anchors).
  - Preserve earlier threshold failures as they were recorded; see the
    [v0.13 investigation](research/rc-v0.13.0-hd-memory.md) and issue #166.

## Visual baselines

Images are compared against baselines you have looked at and approved yourself.
They are the game's artwork, so they live in the data folder and are never
committed; each machine makes its own.

```powershell
python scripts/scenario_suite.py baseline-candidates --report logs/scenarios/RUN/report.json --output C:/DATA/test-baselines/NAME
```

copies a run's captures and a manifest with `approved: false`. Look at each image,
set `approved` only for the ones that are right, add `masks` (rectangles to ignore)
for what legitimately varies, and pass the manifest with `--baselines`. Approval is
locked to the image's hash; nothing is approved automatically.

**Still screens work now.** `python scripts/regress.py --only visual` compares the
crib menu, the Learn Moves list and the move preview playing against
`<data folder>/test-baselines/story-tour-v1/manifest.json`, and is `blocked` when
that does not exist. What two healthy runs of those screens differed by:

- the crib's "now playing" strip (the track is chosen at random): masked;
- the Learn Moves list: 0.4 percent of pixels;
- the preview movie's frame, which depends on timing: masked for a strict
  comparison of everything else, plus a loose unmasked comparison that a blank
  preview screen fails.

**Fights are a different matter.** `--step-shots` captures the frame at a fight
step, and in a repeatable fight (above) the fighters, the health bars, the clock,
the arena and the camera came out pixel-identical between two runs. The crowd did
not, and it covers 10 to 20 percent of the picture. There are two crowds, a
background one and the 3D spectators near the camera; both start animating while
the match loads and leave that window in a slightly different state every run,
with identical seeds (`docs/research/crowd-nondeterminism.md`). So a whole-frame
fight baseline is not usable yet; mask the crowd by hand for a particular camera
position or compare the game-state stream instead.

`--test-env RECOMP_TEST_DRAWS=1` adds `[TEST-DRAWS]` lines: how many values each
random generator had given out, by calling site, at match setup and at fight steps
1, 300 and 900. Two runs with different counts at the same point differ there.

## Game-state telemetry

`RECOMP_TEST_OBSERVATIONS=1` (set by `--require-combat` and `--observe-combat`)
turns on read-only probes that write `[TEST-EVENT]` and `[TEST-STATE]` lines:
match start, fighter positions, health, buttons, hits, damage, the result. `step`
in those lines counts the game's simulation steps, 60 a second in a fight. Normal
play leaves the probes off. Adding or removing a probe point needs a re-lift.

## HD pack validation

For HD-pack checks, `scenario_suite.py run` accepts explicit `--texture-packs`
and `--texture-root` arguments. The root points at your local mods directory;
the scenario still copies fixture saves to a disposable data root, suppresses
inherited diagnostics and records the explicit pack inputs in its report.
Ordinary original-texture scenarios omit both flags. Pack-on captures have
separate review evidence; they do not replace the original-texture goldens.

## Scenarios on demand

`scripts/scenarios.py` starts a run where a bug is ([details](research/scenario-launching.md)):

```powershell
python scripts/scenarios.py fixture add crib-day1 --story crib --note "after the first fight"   # the current saves, into a library
python scripts/scenarios.py list                                                                 # modes, venues, Story points, fixtures
python scripts/scenarios.py generate --mode free-for-all --venue terrordome --fixture crib-day1 --seed 12345 --record
python scripts/scenarios.py generate --mode one-on-one --p1 "right@4;right@5;a@7;a@12;a@16;a@20;a@24;a@28" --fixture crib-day1 --expect-character 0:5
python scripts/scenarios.py cutscene --fixture crib-day1 --from crib --inject "getscreeninfo(story/crib#1=>Some.Command(x))"
```

The fixture library lives in the data folder (`<data>/fixtures/`, with `library.json`), never in the
repository. Generated routes come from `config/scenario-catalog.json`; the combinations the harness
already had generate exactly its routes. `--expect-character SLOT:INDEX` fails a run whose menus picked
another fighter (from the `match_start` telemetry), which is how a new fighter or venue move list is
calibrated. `cutscene` uses `RECOMP_FE_INJECT` to send the front end a command once a Story point is
reached; which command starts a chosen scene is still to be found (`scripts/funccall-catalog.py`).

`scripts/crowd-lab.py run --fixture NAME` plays a pinned fight twice under each crowd control
(`RECOMP_TEST_CROWD=hold`, `reseed`, both, and inline workers) and tabulates whether the crowds came out
the same; `crowd-lab.py mask` learns which cells of a fight step's capture move between runs and writes
them as baseline `masks`, refusing a mask that would hide a fighter.

## Not done


- Menus are still driven on the host's clock; only the fight's input is step-timed.
- Repeatability is not guaranteed even for thirty seconds: the spectators push
  fighters, the spectators do not repeat, and pinned pairs have parted at step 871.
  The `replay` check has passed every time so far and can fail by chance.
- Whole-frame fight baselines: the crowds are not repeatable (see above). The crowd controls and learned
  masks (`scripts/crowd-lab.py`) exist but have not been measured on the game.
- Sounds are not tied to events (hit effects, the announcer).
- A human win, time-up, grapples and throws as such, save and load, and cutscene
  skipping are not covered.
- The live-input session recipes in `tests/scenarios/` are uncalibrated examples;
  the `two-matches` route uses the ordinary pad script instead.
- Long sessions and many repeated matches are not measured.

## Coverage follow-ups

Future coverage is tracked in GitHub, not a parallel roadmap: [Story fixture identity](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/172), [gameplay/lifecycle assertions](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/173), [audiovisual fidelity](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/174), [report provenance](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/175) and [cross-title compatibility adapters](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/176). These describe missing test evidence, not newly established gameplay bugs.


## Fast-start precedence and measurement limits

`startup_default()` runs after the caller's environment merge: explicit `0` remains normal, an explicit empty value removes the variable, and only absence receives automation default1. This shared `run_game()` boundary also serves scenario-suite instances that sanitize inherited RECOMP variables; changing only the regression module's environment would miss them. Read the final child environment in run identities before claiming a route used fast startup. Sanitize inherited pad scripts/RECOMP options for the one normal `m3` boot and keep the existing title golden: require ordered actual FUNCCALL/PATH evidence for legal1, bootLoad, Game.StartMainMenu(0), EA/THX/intro opens, then preStart. Movie opens and normal progression are positive sequence evidence, not per-frame decoder telemetry.

The `skipintro` matrix removes the injected default for real default-off and INI-only cases; environment-off overrides INI-on, environment-on overrides INI-off, and the combined unlock case retains its independent unlock setting. Off cases may stop at positive legal1 because m3 supplies the complete presentation once. UI checkbox tests also clear the override so the widget is editable. The startup-fault parser ends at the first main-menu request; a long idle-title run can intentionally open attract `intro.mad` before that, so test attract separately rather than classify its whole log with that parser.

Initial M2/resource-crest and launcher-only captures may end before any front-end selector event; do not require preStart in those deliberately short captures. Skipped soak boots measure accelerated-boot stability; they do not establish repeated full-movie reliability. Preserve original fight durations, replay hashes and strict audio gates. Use disposable INIs: the complete-save-root guard restores saves/caches, not arbitrary settings files beside them. Recorder output is independent of that guard; record-on performance evidence must be labeled separately.


## Front-end background route boundaries (inventory, 2026-10-20)

This was a source/flow/fixture inventory, not a native capture pass. A route reaching a parent screen or loading a catalogue entry does not prove a child movie appeared.

| Consumer | Existing route/anchor | Proven boundary and remaining calibration |
|---|---|---|
| `story/charDec` | `harness.py run intro --png`; `getscreeninfo(story/chardec)` | Route captures eight seconds after creator entry; fixture/profile identity still matters. |
| `battle/oldMLoad` | `fight-terrordome` / `ffa-terrordome`; `getscreeninfo(battle/oldmload` | Normal Battle traverses this loading card. Its dynamic wall is `screens/match/0_bg/paper_1v1.big`, with `paper_ffa3`, `paper_ffa4`, `paper_team` variants; it is distinct from `battle/matchLoa.big`. |
| `battle/matchLoa` | Catalogue lists `Game.StartGame()` | Separate APT from `oldMLoad`; catalogue presence alone does not establish runtime visitation. Observe the loading handoff before adding an anchor. |
| `battle/tips` | `fight-result` waits for `game.getmatchsummary(` | Flow exits tips to `battle/cmtype`, but this route does not wait for/capture tips. Observe result/rewards/rematch calls before claiming coverage. |
| `options/cribOptions` | `crib` reaches `story/crib`; flow explicitly links parent/child both ways | Existing route stops at the parent. Calibrate visible Options input from a disposable fixture; transition ordering is not a button map. |
| `story/choosGF2`, `choosGF4`, `choosPart` | Catalogue associates SWFs with `screens/story/storyChs.big` | Likely parent-managed submovies; no standalone flow transition or calibrated route was established. The inspected fixture library had only `mac-profiles` at Crib, not a `storyChs` checkpoint. |

Use observed `[FUNCCALL]` anchors with `--stage`/`--shots` and the complete-save guard or a disposable scenario fixture. `Control.GetScreenInfo` is a metadata/action channel, not a reliable arbitrary-movie loader. `RECOMP_FE_INJECT` dispatches after an observed call; an APT string catalogue does not prove any listed command loads a target SWF. `scenarios.py scene` / `RECOMP_SCENE` selects developer CINEMA content in `assets/cinema/*.viv`, not front-end APT/SWF screens. Evidence: route definitions, `<data>/fixtures/library.json`, `<data>/extracted/screens/feflow.xml`, and local `logs/menu-ui-v1-rc/funccall-catalog.json` (Markdown despite its suffix). Current missing-fixture/test evidence belongs to [issues172–176](https://github.com/vyanhursky/DefJamFFNY-recomp/issues?q=is%3Aissue+172+OR+173+OR+174+OR+175+OR+176).

## Experimental recipe examples

`tests/scenarios/fight-return.experimental.json` and
`story-session.experimental.json` illustrate `--session-plan` with host-second
live input. They explicitly declare `validated_live:false`; calibrate them against
fresh ordered screen events before using them for acceptance. `session_driver.py`
is dynamically loaded by the suite and remains required. The visual-manifest
example shows the local approved-baseline schema; its placeholder hash and
`approved:false` are not a real baseline. Keep generated reports/media local.

Manual native launcher coverage is available in `scripts/test-mod-launcher.py`;
its synthetic catalogue/key/data stay disposable. The former hosted RC2 helper
was retired because it hardcoded version1.0.0-rc2. Its dated acceptance result
remains in the integration evidence; it is not a current live-catalogue smoke.
