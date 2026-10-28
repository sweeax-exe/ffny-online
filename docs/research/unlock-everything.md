# Unlocking everything for playtests and regression runs

2026-10-15, branch `feat/unlock-all-dev-switches`. A dated record: the source is authoritative.
Everything below was read from the user's own USA dump (`default.xbe`, `gconfig.xml`) and
confirmed in live runs of the macOS build against a throw-away copy of the save area.

## Short answer

Yes. The retail game has no working unlock cheat, but it still contains the developers'
unlock-everything routine; nothing the player can press reaches it. The port can call it.
`RECOMP_UNLOCK_ALL=1` makes every fighter and venue available on **any** profile, a fresh one
included, so a regression run does not depend on what the save area holds.

| | Without | With `RECOMP_UNLOCK_ALL=1` |
|---|---|---|
| Unlock Rewards, Unlock Fighters | `UNLOCKED 20 / 74`, 15 points, locked silhouettes | `UNLOCKED 74 / 74`, "You've unlocked all the fighters" |
| Battle, Choose Venue map | about 4 venue pins | about 10 pins |
| Crashes, watchdogs, save-area restore | none, identical | none, identical |

With the switches off the build behaves as before: `regress.py --quick` passed 5 of 5 (unit tests,
the three golden frames, a 120 s fight). The runs above used the retail `gconfig.xml`
(`buildtype` 1) on a copy of the Mac profiles under a throw-away data root; the owner's own save
and data folders were not used.

It is runtime-only. In the flows tried (a played One on One, browsing Unlock Rewards, Story into
the crib) the game never wrote the unlocked state into a profile file, so it leaves no trace in a
save and cannot be carried in one. See "Why not a save".

## What is built in

All addresses are guest virtual addresses in the USA dump.

### 1. The Extras cheat-code screen (retail codes)

`SubmitCheatCode` is a script method of the Extras object, handler `sub_00049D00`. It asks the
classifier `sub_000493D0` what kind of code was typed, then switches on the answer through the
nine-entry table at `0x00049F18`.

| Type | Meaning | Codes |
|---|---|---|
| 1 | a song, once | MILITAIN BIGBOI CHOPPER KIRKJONES CHOCOCITY RESPECT PLATINUMB GONBETRUBL ULTRAMAG PUMP LOYALTY GHOSTSHELL POWER SIEZE CARTAGENA AKIRA |
| 2 | reward points, once per amount | THESOURCE 25, GETSTUFF 50, DUCKETS 100, CROOKLYN 250, NEWJACK 500 |

That is 925 points in all, not "100 each" as some guides say, and the points cannot buy 54 fighters.
The classifier only ever returns 1 or 2 (or 0 for no match). No code unlocks fighters or venues.

### 2. Dead developer branches in the same handler

Cases 5 to 8 of the switch are still there and do real work; the classifier never selects them, so
they cannot be reached from the screen.

| Case | What it does | Reaches |
|---|---|---|
| 5 | on the strings `BIGBANG`, `MSGS`, `LQA` | `BIGBANG`: **`sub_000158F0`** (unlock everything, below) and `sub_00013590(1)`, reply "Bling"; `MSGS`: `sub_00013590(2)`; `LQA`: byte `[game-data object + 0x1AD5] = '1'`, which the front end exposes to its Flash screens as `g_LocalCheat` (`sub_0006B760`) |
| 6 | "Debug" | `sub_000B4CE0(1)`, which stores the byte at `0x0039C1DA` |
| 8 | "Holla" | byte `[game-data object + 0x1AD4] = 1` |

The strings for cases 6 and 8 are not in the image, so their code words are unknown.

### 3. `sub_000158F0`: unlock everything

Fetches the game-data object (slot `0x18` of the manager at `0x002FB148`), which holds the profile record at `+4` and the data tables at `+8`, then:

1. walks the character table (hash key `0x4CE5B1B2`) and inserts each id into the profile's
   unlocked-character set (`sub_00017000` tests membership, `sub_00016810` inserts);
2. does the same for the venue table (`0xD7DCA628`) and the venue set;
3. sets the reward points (record + `0x50`) to `0x2710` (10,000) and notifies the profile's owner. The points are read from the code; the shop was not looked at after the call.

`BuyCharacter` (`sub_00054080`) and `BuyVenue` (`sub_000543C0`) are the same operation one id at a
time: price from `sub_00053C50` / `sub_00054150`, subtract it from the points, insert. Its only
caller is case 5 above.

### 4. The debug flag at `0x0039C1DA`

`sub_000B5AD0` runs every frame. When the byte is non-zero and the game state `[0x00340374]` is 2
(a fight), it prints a debug label under each fighter's health bar (seen: `N4007 A`, `N06M2 B`;
it looks like the current animation node). Normal play shows nothing there. Holding the byte at 1
with `RECOMP_POKE=0x39C1DA:1` works with no code change (it writes four bytes, so the three after
the flag are zeroed; nothing visible depended on them in the runs tried).

### 5. Things that looked promising and are not

- `gconfig.xml` has `<buildtype>` with a comment listing 0 "Debug FE" to 5 "Online FE", and
  `<StoryModeCheatFile>cheats/smoke.smc</StoryModeCheatFile>`. The second string is not in the image at all (ASCII or UTF-16),
  so nothing reads it. `buildtype` is read into the game-data object at `+0x33C` (the function that registers the object as slot `0x18`, near `0x000126B5`).
  With it set to 0 the boot reaches the same main menu with the same front-end call sequence. Its
  effect past the main menu was not explored.
- `sub_0002CA20` (`DEBUG MENU -- %s`) and `sub_0002CF80` ("JUMP STARTING THE DAMN GAME!!!"): dead
  code with no way in. See "The debug menu and the jump start" below.

## What was added

| File | Change |
|---|---|
| `src/patches/dev_unlock.c` | `RECOMP_UNLOCK_ALL=1`: from the front end's call stream, once `Game.StartMainMenu` has been seen, run `sub_000158F0` on every `Control.GetScreenInfo(...)`. `RECOMP_DEV_DEBUG=1`: hold the debug flag at 1. Both off by default. |
| `src/hooks/pc_ui_settings.c`, `pc_settings.c`, `pc_ui.cpp` | `[debug] unlock_all` (off by default, restart): a checkbox "Debug - Unlock all Fighters and Venues (Requires Restart)" on the General page of the launcher and the overlay, exported to `RECOMP_UNLOCK_ALL` at start-up. |
| `scripts/harness.py` | every run it starts has `RECOMP_UNLOCK_ALL=1` (so `regress.py` and the scenario suite do too); `=0` turns it off, an empty value leaves it to the settings file. |
| `scripts/regress.py` | check `unlockopt`: the launcher checkbox saves the setting, and the game started from that file alone runs the unlock (and does not without it). |
| `src/recomp_manual.c` | one call to `defjam_unlock_all_on_funccall(text)` in the existing `funccall_hook`. |
| `scripts/harness.py` | `DEFJAM_HARNESS_SNAPSHOT_SAVE=<dir>` keeps the save area as the run left it, before the guard restores the original. |
| `tests/unit/test_harness.py` | covers the snapshot. |

The profile can change between screens (the user-ID screen loads one), so running the routine on
every screen change keeps the unlock in step with whichever profile is current. It only inserts
ids that are missing; what it repeats is setting the points back to 10,000.

Using it, from the repository root (use a disposable data root; the harness guard restores the save area):

```bash
DEFJAM_DATA=/path/to/data-root RECOMP_HEADLESS=1 python scripts/harness.py run unlock --env RECOMP_UNLOCK_ALL=1
python scripts/harness.py run fight --env RECOMP_UNLOCK_ALL=1 --env RECOMP_DEV_DEBUG=1
```

For a build you start by hand the variables work the same:
`RECOMP_UNLOCK_ALL=1 ./build/posix-release/defjam_recomp`.

## The debug menu and the jump start

2026-10-15. Short answer: **no condition enables them, because nothing in the retail image can reach
them.** There is no vtable. It is dead code that the linker kept, like the unlock routine. Direct
starts of a match on a chosen fighter and venue are possible, but through the retail `Game.StartGame`
(next section), not through the jump start.

What the code is, with guest addresses:

- The class is `CRunModuleAki` (strings `AKIDEBUG` 0x0029D8E8, `CRunModuleAki::Init.m_pMode`
  0x0029D8F4, in `sub_00077C40`). Its methods: `sub_00077C40` init, `sub_00077A40` update,
  `sub_00077B20` draw, `sub_00077DD0` shutdown. It owns a `CModeSubsystem` at `+0x14`.
- `CModeSubsystem` (`RunModuleAKI.log` 0x0028CBF4 is in its init): `sub_0002C790` init (state
  `[this+0x2A0] = 0`, `[+0x2A4] = 8`, `[+0x29C]` = initialised), `sub_0002C970` shutdown,
  `sub_0002C9E0` per-frame update, `sub_0002DB00` the per-frame switch on `[this+0x2A0]`.
- The switch is a jump table at 0x0002DBB8: state 0 is the **debug menu**, reached by the tail jump
  `jmp 0x2CA20` at 0x0002DB6D. States 1, 3, 4, 5, 6, 7 pass to the objects at 0x00349760 (match),
  0x003459A0, 0x003797F0, 0x0037ACD0, 0x00385F50, 0x003473F0 (`sub_00087040`, `sub_00076C90`,
  `sub_0008D970`, `sub_00092490`, `sub_00092AF0`, `sub_0007C940`); state 2 does nothing. A fresh
  `CModeSubsystem` starts in state 0, so the debug menu is what the module showed first.
- `sub_0002CF80` is a method (`ret 0x14`, five stack arguments). It logs the jump-start string, builds
  an array of the four name strings (`dirty`, `nuffrespect`, `rockandroll`, `manup`, 0x0028CCB0..)
  for `sub_0003CFF0`, sets state 1 and `[this+0x21] = 1`, fills four slots from record lookups
  (`sub_000C7D00`), and ends by starting the match object at 0x00349760 (`sub_0008BB00`). It starts
  the AKI module's own match, not the retail one. What the four names are was not worked out.

Why nothing reaches it (all checked by scanning every byte of the image, code and data, not only the
lifted C; `scripts/callers.py` only sees direct calls and needs `src/recomp/gen` flat):

- No dword in the XBE equals 0x0002CF80, 0x00077A40, 0x00077B20, 0x00077C40 or 0x00077DD0. No call or
  jump targets 0x0002CF80, 0x00077A40, 0x00077B20, 0x00077C40 or 0x00077DD0 either.
- The only code that enters the chain is itself in it: `sub_00077B20` calls `sub_0002DB00` (0x77BAD) and
  `sub_0002C9E0` (0x77C22); `sub_00077A40` calls `sub_0002DFF0` (0x77ABD); `sub_00077C40` calls
  `sub_0002C790` (0x77CA6); `sub_00077DD0` calls `sub_0002C970` (0x77DE3). `sub_0002CA20` has one entry,
  the jump above, and `sub_0002CF80` has none.
- The run-module factory `sub_000773A0` (switch on ids 0 to 0x10) builds the module objects, and the
  vtables written by their constructors are 0x0029D5C4 to 0x0029D7A4. None of them holds an AKI
  method. There is no `CRunModuleAki` vtable in the image at all.
- So `buildtype`, a command line, the profile and the debug byte 0x0039C1DA are not what enables it. The
  byte 0x0039C1DA is a different feature (the fight label, above). Only the live parts of
  `CModeSubsystem` run: `sub_0002C5B0` is called from four live run-module methods (0x78C90, 0x796A0,
  0x79E40, 0x7B3B0).

Not tested at run time: `RECOMP_WATCH_EXEC` needs x86 debug registers and the macOS host has none,
and `RECOMP_TRACE_ICALL` sees only indirect calls, so the "never entered" part rests on the scan
above. Running it for the player would mean building a `CRunModuleAki` by hand (an object of 0x44 or
more bytes with fields at +0x04 to +0x40, and a vtable to go with it), which is not small.

## Starting a match on a chosen fighter and venue

The retail front end already does this, so a regression needs no debug code. A match is made by the
script call `Game.StartGame()` (handler `sub_00046E40`, registered by name at 0x000476F5, so it is
reached through a function pointer and `recomp_lookup_manual()` can wrap it). It reads:

- the venue: `[Game object + 0xCA4]`, the number `Game.ChooseVenue(n)` stored. `sub_000461C0`
  translates it through the 14-pair table at 0x002DD978 into `[game-data + 0x1AD0]`. The pairs, script
  id to stored value: 0:0, 1:1, 2:2, 3:3, 4:4, 5:5, 17:6, 24:7, 11:8, 26:9, 10:10, 8:11, 12:12, 13:13;
  an id that is not in the table stores 0. The script ids are the `<feID>` numbers in `venue.xml`
  (1 is The Foundation, the default One on One venue; 17 is Gun Hill Garage). The harness calls its
  `TERRORDOME` stage "venue 6" and the Terror Dome has `<feID>` 6, which this table does not list:
  what that route selects was not re-checked here.
- the fighters: five bytes a slot at `game-data + 0x1AA9`; the second byte of a slot is the fighter's
  `<ID>` from `fighter.xml` (`Controller.ControllerSetup` stores it through `sub_0001E690`). The
  default One on One holds 56 and 55 (Blaze1 and Doc). The game-data object is slot 0x18 of the
  manager list at 0x002FB148 (`MEM32(0x002FB148 + 0x18*4)`).

`RECOMP_START_FIGHTERS=a,b[,c,d]` and `RECOMP_START_VENUE=n` (src/patches/dev_unlock.c) write those
values at the entry of `Game.StartGame`; `RECOMP_START_LOG=1` prints what it was given. They do not
build a match from nothing: the menus still run up to Fight (the harness `fight` route, "defaults all
the way", does not depend on what is unlocked), and only the choices are replaced. The ids need no
unlock, because nothing on the way to `StartGame` checks them.

Tested on a throw-away data root with a copy of the `mac-profiles` fixture, the `fight` route with
`RECOMP_TEST_OBSERVATIONS=1`:

| Run | `[START]` line | In the match |
|---|---|---|
| none (`RECOMP_START_LOG=1`) | venue 1, fighters 56 55 | Blaze v Doc at the club; telemetry characters 4 and 9 |
| `FIGHTERS=10,20 VENUE=6` | venue 6, fighters 10 20 | Meca v Crazy Legs; telemetry characters 74 and 34; 6 is not in the table, so the venue stored is 0 (a rubble floor, not identified) |
| `FIGHTERS=3,20 VENUE=17` | venue 17, fighters 3 20 | Trick v Crazy Legs in a parking garage (Gun Hill Garage, `venue.xml` `<feID>` 17) |

No crashes, no watchdogs, save area restored identical in each. Limits: only the two slots the route
fills are covered (a four-fighter mode needs `ffa` and four ids, not tried); the telemetry
"character" is the engine's index, not the `<ID>`, so assert fighters by the names on the HUD or add
the `<ID>`-to-index map before asserting; the venue was judged from a capture, and the table above is read from the code plus the one garage test.

## Why not a save

A profile is a 22,928-byte file (`20CM` magic, a header whose bytes `0x10` to `0x4B` change with any
change in the contents, then fixed-size tables). Two id lists in the owner's profiles have a fixed
number of slots (ten and eight; they look like characters and venues, which is unconfirmed), while the
game shows 74 fighters, so a full unlock probably does not fit the file as it is and the
on-disk list is not obviously the same thing as the in-memory set. A hand-edited file would also need
the header reproduced. With the unlock active, no profile file changed in any flow tried (One on One
to the result, Unlock Rewards, Story into the crib); the Options file was rewritten by the fight, and
whether the unlock touches it was not checked. So there is no game-made save to keep, and the runtime
switch makes one unnecessary for tests. It also means nothing is shared (saves are not published).

## Not covered

- **The pinned fight stream.** With the unlock on, the `replay` fight produces 95 records where the golden
  has 80, repeatably, so the unlock changes how that fight plays (why was not looked into). `replay` and
  `repeat` therefore run without it (`scenario_suite.py --no-unlock`).
- **Two failures that are not this change.** On this Mac, pristine `origin/main` also fails the `combat`
  check's `audio.dry_queue`, and one `replay` run on it logged an indirect call to non-code; the launcher
  check failed once and passed on a re-run.
- **Story progress and cutscenes.** Story chapter state is in the profile and `sub_000158F0` does
  not touch it. The Story routes (`intro`, `crib`, `gym`) still need their profile, as the backlog
  item on golden saves says.
- **Songs** (Extras): codes only, once each. Typing the sixteen codes through the harness would do it.
- **Clothing, tattoos, items, moves**: bought in Story with cash; not looked at.
- **A real save.** The routine tells the profile's owner it changed. On a profile you care about,
  do not use it until it is known which flows save it.

## Next

1. A named harness route that asserts the unlock (reads `unlocked 74/74` from game state rather than a screenshot).
2. ~~Find what enables the debug menu and jump start.~~ Done: nothing does, and a direct start is
   done at `Game.StartGame` instead (below). Left: a route that uses `RECOMP_START_*` and asserts the
   fighters from `[TEST-STATE]`; the venue is checked by eye only.
3. Decide whether Story checkpoints should be produced by a route (backlog item, option 2), now that characters and venues no longer depend on them.
