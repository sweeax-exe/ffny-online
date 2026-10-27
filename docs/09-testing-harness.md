# Gameplay test harness

How to run the scripted gameplay checks and read what they prove. The roadmap
behind this is [08-testing-roadmap.md](08-testing-roadmap.md); what is still
missing is listed at the end.

Everything here needs a local build made from your own dump. Hosted CI has no
game data and runs only the unit tests. Captures, audio and reports are the
game's artwork and stay under `logs/`, which git ignores.

## Three levels

| Command | Time | What it is for |
|---|---|---|
| `python scripts/regress.py --quick` | about 10 min | after every change: unit tests, three golden frames, one fight |
| `python scripts/regress.py` | about an hour | before a commit that touches the runtime, renderer or generated code |
| `python scripts/scenario_suite.py run <route> ...` | one launch | one scenario with all its evidence and a browsable report |

Never run two of these at once: they share the save area, the window and the
frame-rate measurements.

## Quick game tests for agents (defjam-mcp v2)

`tools/defjam-mcp/dj.py` (Bash) and the `defjam` MCP server run whole tests with one compact
JSON result and condense any log, under the machine lock (`D:\DefJam\test-lock.txt`,
`test-priority.txt`, no other game copy running) and on disposable data roots, with the safe
environment set (local addresses only, one guest CPU per copy). Details and the full option
list: [tools/defjam-mcp/README.md](../tools/defjam-mcp/README.md).

```
python tools/defjam-mcp/dj.py scenario online --matches 3                 # 2 copies, 3 room matches, KO at step 600
python tools/defjam-mcp/dj.py scenario online --matches 5 --mode koth --bot p1=aggressive,p2=counter
python tools/defjam-mcp/dj.py scenario online --matches 3 --rollback on --fast-ko 0 --bot
python tools/defjam-mcp/dj.py scenario offline --type 0 --venue 6 --f0 58 --f1 40   # one fight, CPU opponent
python tools/defjam-mcp/dj.py scenario offline --fixture logs/fixtures/created-fighter
python tools/defjam-mcp/dj.py scenario boot --goto battle
python tools/defjam-mcp/dj.py scenario gallery
python tools/defjam-mcp/dj.py digest logs/bug-reports/<id>                # a player's F9 report in <= 60 lines
python tools/defjam-mcp/dj.py analyze logs/run-A.log.err logs/run-B.log.err   # the online result of an older run
python tools/defjam-mcp/dj.py lock run -- python scripts/regress.py --quick   # any other test under the lock
```

An online result has one line per match: `{"n":2,"start":"lockstep d2","venue":18,"load_s":[0.2,0.8],
"fe":["ok","ok"],"hits":[3,4],"winners":[1],"ko":true,"checks":20,"summary":["cid 0,1","cid 0,1"]}`
-- the start mode and input delay, the front-end order (the match-type screen's own
`ResetMatchData` before automatch's `StartBattleMode`), hits landed per slot, the winners, checksums
compared and equal, the summary's controller ids and whether it was accepted
(`UpdateMatchRewardsPoints`). Problems (desync with its detail, gave-ups, crashes with their `sub_`,
summaries not accepted) are listed under `problems`, and the exit code is 1.

The full regression is the quick one plus a four-fighter Free For All, the same
at the Terrordome for four minutes, the **combat check**, a **two-pad match**, the
**replay check**, the Story intro, crib, gym and a boot soak. `--only a,b` picks
checks; `fight-terrordome`, `ffa-result`, `two-matches`, `repeat` and `visual` are
selectable that way only.

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
`two-matches` (a One on One and a Free For All in one launch), `fight-terrordome`,
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
- **Memory.** The game's private bytes, sampled every five seconds, must not have
  grown more than 64 MB from the start of play to the end of the run. This catches
  a fast leak; one fight is too short for a slow one.

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

## Matches that end by themselves

In this game zero health is not a knockout: the fighter has to be finished with a hit
or a grab that carries the finishing flag, and blind pad scripts rarely manage it, so
scripted matches lasted four to eleven minutes or never ended. Two test switches fix
that; both turn the telemetry on by themselves and are off unless set.

**`RECOMP_TEST_KO_STEP=<step>[,<winner slot>|alt]`**: from that fight step of every
match (60 a second, counted from the match start) the loser is knocked out the game's
own way. Every fighter but the winner gets zero health and the knockout state 0x18
through the routine the game's knockout check uses (`sub_001B5420`), as soon as both
stand free (state 1, so no grapple or throw holds the loser), and at the latest 180
steps later. The game then records its usual decisive result (code 0x21), plays the
slow motion and goes through the results and the summary. The winner is slot 0 by
default; `alt` alternates it per match (slot 0 wins the first). It runs at the start
of a step, before the step's checksum, and depends only on the step number, the match
ordinal and the fighters' records, so both PCs of a room apply it at the same step;
it keeps no state of its own, so a step run again by rollback does the same again.
Lines `[TEST-KO]` say when it fired. (`RECOMP_TEST_FAST_KO=<percent>` is older: it caps
health, but the fighter at zero still has to be finished.)

**`RECOMP_TEST_BOT=1[,<aggression 0..100>]`**: a state-reading player
(`src/hooks/test_bot.c`). Each fight step it reads its own and the opponent's position,
health, stun and state, and presses pad buttons: it walks up, throws strings of
strikes, grabs, blocks (more often when the opponent winds up a strike, and more
with a low aggression), and once the opponent is under the knockout threshold or
stunned it goes for grabs and heavy hits until the game decides the knockout. It
writes the pad-table entry the fighter reads, as `RECOMP_TEST_INPUT` does. Online each
copy drives only its own player, and netplay exchanges that input like a human's, so
the lockstep is not at risk; offline it drives every human slot, or those in
`RECOMP_TEST_BOT_SLOTS=0,1`. Its choices follow from the step, the slot, the fighters'
records and the match seed (`RECOMP_TEST_BOT_SEED` adds to it); it learns what each pad
direction moves its fighter by, since the camera turns. Pad bits it uses (found by a
step-input probe): `0x80` and `0x40` strikes, `0x20` grab, `0x2000` block (held),
`0x1000` held with strikes as the modifier (unconfirmed), `0x1`/`0x2`/`0x4`/`0x8` up, down, left, right.

```powershell
# offline: one One on One to the summary, knocked out at step 600 (or fought by the bot)
python scripts/harness.py run fight-result --env RECOMP_TEST_KO_STEP=600
python scripts/harness.py run fight-result --env RECOMP_TEST_BOT=1
# online: five room matches in a row, two copies, a knockout at step 600 alternating the winner
.\scripts\netplay-hub-test.ps1 -Route online5 -FastKo 600
.\scripts\netplay-hub-test.ps1 -Route online5 -Bot
# beside another test: its own relay port, CPUs (16 and 18) and data roots (<prefix>0, <prefix>1)
.\scripts\netplay-hub-test.ps1 -Route online5 -FastKo 600 -RelayPort 27807 -BaseCore 16 -DataPrefix D:\DefJam\data-bot
```

The MCP test server's `launch` takes the same as `ko_step`, `ko_winner`, `bot`,
`relay_port` and `base_core`; `dj.py scenario ... --fast-ko N --bot` does it in one command.
`RECOMP_TEST_BOT_PLAN="p1=aggressive,p2=counter"` gives each slot a behaviour (balanced,
aggressive, defensive, block, grapple, counter, mash, idle; see `src/hooks/test_bot.c`), and a
`[TEST-BOT] summary` line per slot and fight says what the bot tried (strikes, grabs, block steps,
counters on the opponent's wind-ups and how many took no damage, holds broken). The hub test leaves the save area's cache partition
images out of each copy's data root (the runtime makes sparse ones): copied, they are
5 GB a root and again in each of the harness's save backups.

## HD pack validation

For HD-pack checks, `scenario_suite.py run` accepts explicit `--texture-packs`
and `--texture-root` arguments. The root points at your local mods directory;
the scenario still copies fixture saves to a disposable data root, suppresses
inherited diagnostics and records the explicit pack inputs in its report.
Ordinary original-texture scenarios omit both flags. Pack-on captures have
separate review evidence; they do not replace the original-texture goldens.

## Not done


- Menus are still driven on the host's clock; only the fight's input is step-timed.
- Repeatability is not guaranteed even for thirty seconds: the spectators push
  fighters, the spectators do not repeat, and pinned pairs have parted at step 871.
  The `replay` check has passed every time so far and can fail by chance.
- Whole-frame fight baselines: the crowds are not repeatable (see above).
- Sounds are not tied to events (hit effects, the announcer).
- A human win, time-up, grapples and throws as such, save and load, and cutscene
  skipping are not covered.
- The live-input session recipes in `tests/scenarios/` are uncalibrated examples;
  the `two-matches` route uses the ordinary pad script instead.
- Long sessions are not measured. Five online matches in a row (two copies, 2026-10-25): 4.9 min
  with `-FastKo 600` (110 checksums equal), 4.2 min with it and `RECOMP_ROLLBACK=on`,
  `RECOMP_NETPLAY_LAG_MS=70,70`, and 15.9 min with `-Bot` (four knockouts and a venue double
  knockout, 3,052 to 15,254 steps a match, 730 checksums equal); scripted routes needed 40 minutes or
  more. The bot is not a time bound: bot against bot a match can run past ten minutes, so add
  `-FastKo` (for example 18000, five minutes) when a test needs one. Offline the CPU usually beats it.
