# Team (2 vs 2) and the special match types: what the game does, and what online play needs

Read-only study (2026-10-25). Nothing was built or run. Sources: the front-end screen scripts
(`scripts/apt-dump.py screens/screens.viv screens/battle/<name>.big --actions`, offline against the user's
own dump), the lifted C in `src/recomp/gen/recomp_00NN.c` read with `grep`, a few instruction-level reads of the
XBE image (capstone, to settle flag questions the lifter marks "unsupported"), `game/assets/gamecore.xml`,
`game/venue.xml`, the run logs in `logs/` (telemetry `match_type`, `[AUTOMATCH]`, `[FUNCCALL]`, `[FE-PROBE]`),
and `src/hooks/{automatch,netplay,online_data,online_screen}`, `netplay/relay/`. No game bytes are reproduced.

Tags: **[V]** verified by reading the exact code, script or log line; **[I]** inferred (reasoning given);
**[U]** unknown, needs a run. Fighter record = `0x003B92E0 + slot * 0x12E8`.

There is **no log of anyone playing a Team or a special match offline** (`grep SetMatchType` in `logs/` shows only 0, 1
and 2 as menu cursor moves; the full sequences are all type 0/2/3 from our own automatch). The front-end sequences
below are therefore read from the screens' bytecode and the native handlers, not from a recorded run.

---

## 0. Findings in one page

1. The match-type list has 13 entries (ids 0..12). Online offers 0, 2, 3. **Team is id 1 ("Tag").** The special
   matches are ids 6..11. Ids 4 and 5 (1vs2, 1vs3) exist in the native name table but no screen offers them. [V]
2. **The front-end match type reaches the fight only as a "class" 0..3**: `Game.SetMatchType(t)` maps 0 to 0, 1 to 1,
   2 to 2, 3 to 3 and every other id (1vs2, 1vs3, Demolition, Inferno, Subway, Window, Cage, Ringout, Tutorial) to
   class 0. The setup block then holds `1 << class`, which becomes the byte `0x3B907E`: 1 = one on one,
   **2 = team**, 4 = 3-way, 8 = 4-way. A special match therefore **fights exactly as One on One**. [V]
3. **What makes a special match special is the venue**, not the type. The special-match screen entries fix a venue
   and two players: Demolition = Gun Hill Garage (feID 17), Inferno = Red Hook Tire Co. (24), Subway = 125th Street
   Station (11), Window = Crow's Office (26), Cage = Club Murder / Crow's Cage (10), Ringout = The Dragon House (8).
   The hazards (`<infernomatch>`, `<subwaymatch>`, `<windowmatch>`, `<demolitionmatch>`, `<ringoutposition>`,
   club light, sandbag) are objects in each venue's level data. Online One on One at those venues already runs the
   same fight. [V for the screen table and the class mapping, I for "the hazards are venue data"]
4. The fight's own "match type" word `0x3B9084` is **not** the front-end id; it is an arena kind taken from the venue
   (telemetry: venue feID 1 gives 2, 11 gives 4, 19 gives 15, 6 gives 21, for One on One and for 3-Way alike). [V]
5. **Team = four fighters in the ring at once**, slots 0 and 1 against slots 2 and 3 (partner of slot s is s^1,
   primary opponent s^3). There is no team table: the team is the slot number. The fight marks slots 2 and 3 with
   `+0x937` value 4 (the "side marker"). No partner swapping was found. [V]
6. Team win rule: a side loses when every fighter on it has the "ended" flag; the result recorder lists only the
   **last defeated fighter and his primary opponent**, so the partner is in neither list. The side that won is
   `winner_slot >> 1`. The result record's `+0xC` word also carries the side (0x100: slots 2/3 won, 0x200: slots 0/1). [V]
7. For online Team the work is mostly **netplay**, not automatch: the automatch sequence already handles four slots
   with CPU fill (`mode->fighters`). The blocking design point is that netplay assumes **humans occupy slots
   0..players-1**, so with fewer than four humans they all land on one team (section 6). [V]
8. Three venues have only a one-on-one start table in `gamecore.xml` (Inferno warehouse, Crow's Office, Parkade =
   feIDs 24, 26, 17); Team, 3-Way and Free for All must not be offered there. One 3-Way at feID 17 desynced and crashed
   in build 20 (F9 report eef32e, rollback build, cause not proven). [V for the table, U for the cause]
9. Existing issue found on the way: `netplay_on_step` takes the winner from `0x3B8F40` without reading the result
   code, so a draw (everyone eliminated in the subway arena, or a time-up tie) reads as "slot 0 won". `koth_check_result`
   already does it right (reads the record). [V]

---

## 1. The match types (Q1)

### 1.1 The name table and the front-end list

The native table at `0x2DBCC8` lists 13 names; `Game.GetNextMatchType()` returns `NAME=<name>&STATE=..&ID=<n>` with
`n` = the table index (probe log `logs/run-20261020-135221.log.err`). [V]

The list the screen actually uses is built in `screens/battle/cmtype.big` (bytecode `aMODES`, 12 entries, looked up by
`nID`). Per entry: id, internal name, whether the rules overlay is shown, the fixed venue (`sVenue`, `CHOOSE` = the
player picks), the number of fighters. The select handler sends `Game.ChooseVenue(sVenue - 1)`: the stored number is
the venue feID plus one. [V]

| id | name | logo (online-art) | rules overlay | venue (feID sent) | fighters |
|---|---|---|---|---|---|
| 0 | Single (One on One) | oneOnOne | yes | player picks | 2 |
| 1 | Tag (Team, 2 vs 2) | team | yes | player picks | 4 |
| 2 | 3Way | ffa | yes | player picks | 3 |
| 3 | 4Way | ffa | yes | player picks | 4 |
| 4, 5 | 1vs2, 1vs3 | none | not reachable | not reachable | not defined |
| 6 | Demolition | demolition | no | 17 (Gun Hill Garage, "Parkade") | 2 |
| 7 | Inferno | inferno | no | 24 (Red Hook Tire Co.) | 2 |
| 8 | Subway | subway | no | 11 (125th Street Station) | 2 |
| 9 | Window | window | no | 26 (Crow's Office) | 2 |
| 10 | Cage | cage | no | 10 (Club Murder, "Crow's Cage") | 2 |
| 11 | Ringout | ringOut | no | 8 (The Dragon House) | 2 |
| 12 | Tutorial / Challenges | none | no | player picks | 1 (story style) |

A duplicate entry `oFFA_MATCH` (id 2, 3 fighters) sits in the array but the lookup returns the first match, the 3Way
entry. On `battle/battleID` a 3Way/4Way choice is made by pressing the "3 players" or "4 players" button, which re-sends
`Game.SetMatchType` with 2 or 3. [V]

The venue descriptions confirm the pairing: feID 8 "ring out rules are always in effect", feID 10 "Crow's cage
club", feID 11 "dark subway platform", feID 24 "combustible warehouse", feID 26 "Headquarters ... overlooking the streets
below", feID 17 "underground parkade". [V] (`[FE-PROBE] venue[type N]` lines.)

The list is the same for every type: the game does not filter venues by type (online-match-setup.md section 6). Specials
are shown "Locked" on a fresh save (unlocked through the story); `StartGame` does not check the lock. [V/I]

### 1.2 Per-type options

* **Rules** (only types 0..3 show the overlay, `screens/battle/rules.big`, three buttons): index 0 Standard, 1 Crowd
  Votes, 2 No Submission, sent as `Game.SetMatchRules(i)`; the specials always send `Game.SetMatchRules(0)`. In
  `StartGame`'s setup builder: rules 0 and 1 give the flag word 7, rules 2 gives 5 (this becomes `0x3B907C`); rules 1
  also sets the time-limit selector `0x3B9080` to 1 (a 5-minute limit per the table in match-end-state.md; the
  default is no limit). [V, read from the instructions]
* **Rounds**: none; single round. Online, the START byte called `rules` in `on_start` is `Settings.Rounds` in the relay.
* **Time**: only through rules index 1. Online always sends 0.
* **Difficulty**: not per type; automatch forces Hard (`ONLINE_DIFFICULTY`).

---

## 2. How the type reaches the fight

| step | what | evidence |
|---|---|---|
| `Game.SetMatchType(t)` (handler `0x46790`) | stores `t` in `GS+0xB8` (`GS` = component 0x18 of the singleton at `0x2FB148`); calls `GS.SetMatchType` (`sub_0001E120`) which returns the class (0 for 0, 6..12, 4, 5; 1 for 1; 2 for 2; 3 for 3); the class goes into the `Game` module object at `+0xC7C` | disassembly of `0x46790`, `0x1E120` [V] |
| `Game.GetMatchType()` | answers from `GS+0xB8`, the front-end id (the screens test `== 1` for Tag and `> 4` for the specials) | handler `0x46810` [V] |
| `Game.StartGame()` (`0x46E40`) | passes `Game+0xC7C` (the class) to the setup builder `sub_00020270` | call site `0x472C5` [V] |
| setup builder, battle mode (`GS+0x344 == 1`) | `GS+0xC0 = 0`, **`GS+0xC4 = 1 << class`**; also `GS+0xC8` = the rules word above | disassembly `0x20541..0x205B9` [V] |
| fight start `sub_000871F0` | copies setup `+0` to `0x3B9078`, setup `+8` (16 bit) to `0x3B907C`, setup `+4` (8 bit) to **`0x3B907E`**; `0x3B9080 = 1` when the rules index is 1; setup `+0xA0` to **`0x3B9084`** | disassembly [V] |

`0x3B907E` therefore holds 1, 2, 4 or 8 only. (match-end-state.md calls the team flag "bit 2"; the mask value is 2.)
Bit 0x40 of that byte, which would randomise a fighter's health in `sub_001B3110`, is never set from the menus. [V]

`0x3B9084` comes from the venue record (setup `+0xA0`, from a database lookup inside the setup builder, not decoded).
Telemetry (`"event":"match_start"`, field `match_type`, joined to the `[AUTOMATCH] armed:` line before it):

| armed (type, venue feID, fighters) | `match_type` |
|---|---|
| 0, 1, 2 (The Foundation) | 2 |
| 0, 11, 2 (125th Street Station) | 4 |
| 2, 11, 3 (3-Way, same venue) | 4 |
| 0, 19, 2 (The Pit) | 15 |
| 0, 6, 2 (Terror Dome) | 21 |

[V] Fits "venue.xml `<ID>`, minus one for IDs above the cut venue 08A": Foundation ID 2 gives 2, Subway ID 4 gives 4,
The Pit ID 16 gives 15, Old School Club ID 22 gives 21. Extended, that would make Dragon House 5, Red Hook (Inferno)
7, Club Murder 16, Crow's Office 17, Parkade 19. [I] This agrees with the code: the fight compares the word with 4
in the subway elimination logic, with 5 in four ring-out/wall functions, with 7 in the "killed by the environment"
handler, with 0x11 and 0x13 in the window/car code (match-end-state.md, wall-stagger.md). A probe run over every venue
would turn the [I] into [V] (section 8).

---

## 3. Team (2 vs 2) (Q2)

### 3.1 The front-end sequence (what the menus do)

1. `cmtype`: `Game.SetMatchType(1)`, rules overlay, `Game.SetMatchRules(r)`, `options/battleID.swf`.
2. `battleID`: pads join (`Controller.ControllerBind(n)`), `Battle.LoadCreatePlayerData(numPlayers)` with
   `numPlayers` = the mode's fighter count (the offline vs-CPU One on One logs show 2 even with one human; our
   automatch sends the number of humans), then **`battle/chooseF4.swf`** (the screen is chosen as `chooseF` plus the
   mode's fighter count).
3. `chooseF4`: for each player, `Controller.ControllerSetup(slot, pad, fighter, slot)` (the usual form; the third
   argument is the `fighter.xml` ID, alternate outfits are separate IDs). With all four ready the screen asks
   `Game.GetMatchType()`; for Tag it goes to `battle/chsTeam4.swf`, for the specials straight to the loading screen, for
   the others to the venue screen.
4. `chsTeam4`: a header "choose team", the camera call `Battle.SetCPViewportCam(4, ...)` (front end only), then for
   each player `Controller.ControllerSelect(thumb, pad, fighterID + 1, position)`, `position` 0..3 being the portrait
   the player placed. `ControllerSelect(-1, ...)` undoes the last one, `(-2, ...)` clears all. Then
   `battle/chsVenue.swf`, `Game.ChooseVenue(feID)`, `battle/oldMLoad.swf`, `Game.StartGame()`.

`Controller.ControllerSelect` (handler `0x55A20`, in the `Controller` module): collects the four picks in a table of
5-byte entries indexed by position (module `+0xC0E`, count at `+0xC0C`); when the fourth arrives it **rewrites the
four `GS` setup entries so that entry i = the pick at position i** (pad, fighter, controller number), and copies the
per-position outfit byte. After that, slot = position. [V]

### 3.2 Where the team of a slot is stored

* **Nowhere as data: the team is the slot number.** Slots 0 and 1 are one team, 2 and 3 the other. [V]
  * The fighter-record initialiser `sub_001B3110(record, slot, character)` reads `0x3B907E`:
    * value 1 (one on one): primary opponent pointer `+0` = record of slot^1.
    * **value 2 (team): `+0` = record of slot^3 (the diagonal opponent), `+4` = record of slot^1 (the partner),
      `+8` = null; `+0x937` |= 2; and for slots 2 and 3 (`cmp slot, 2; jl` skips) `+0x937` |= 4, the side marker**; `+0x91E` |= 4.
    * value 4 (3-way): `+0` = record of (slot+1) mod 3. Value 8 (4-way): `+0` = record of slot^3.
  * `gamecore.xml` start positions, per venue: "2 vs 2 TAG" gives 1P and 2P on the left (x about -30, -20) and 3P and
    4P on the right (x about +20, +30). [V]
* Fighter record `+0x937`: bit 0 (value 1) CPU, value 2 "has an opponent list" (set in every mode), **value 4 = side
  marker (slots 2 and 3)**. [V]
* Setup entries (`GS+0x1AA9 + 5*slot`): pad/device, fighter, raw pad argument, bound marker, controller number. No team field. [V]

### 3.3 All four at once, or tag-team?

All four fighters are in the ring from the start (four start positions, four records, four pad slots); the match
logic below treats the partner as an independent fighter who fights on after his partner goes down. No code path
that swaps a partner in was found (not an exhaustive search; the Create-a-player "tag" strings are in the story
side). The game has an AI "partner" command path (a debug string `partner com_main_data->cmd`, not referenced from the
lifted code) for CPU partners. [V for four-at-once, U for absence of any swap]

### 3.4 Who wins (the decision)

All of this is in `sub_001A7140` (called each frame in phase 1, and directly by the hazard-kill handlers) and
`sub_001A6750`. [V]

* **Generic branch** (every venue except the subway arena type 4): a fighter whose defeat word (`+0x932`) gets a
  defeat bit is first marked "ended" (`+0x932 |= 0x200`), then `sub_001A6750` asks whether the match is decided:
  value 1 (one on one) always; **value 2 (team): decided when the number of not-ended fighters with the side marker
  is zero, or the number without it is zero**; 3-way/4-way: when at most one fighter is left. If decided, the winner
  slot is the **defeated fighter's primary opponent** (slot^3 in team), written to `0x3B8F3C` and `0x3B8F40`, and the
  recorder gets winners = {that slot}, losers = {the defeated fighter}.
* **Subway arena (type 4) branch**: scans the four fighters for the elimination bit; survivors are counted by side
  marker. With value 2: decided when all survivors are on one side; the winner slot is the first survivor and the
  recorder gets winners = all survivors, losers = the eliminated. Everyone eliminated: draw, code 0x105 (plus
  0x1000, 0x2000 or 0x4000 for 2, 3 or 4 fighters).
* **Time up** (only with the Crowd Votes rule): `sub_001A6E40` sums scores per side (team) and calls the recorder with
  full side lists, code 0x41 or 0x45 (tie).

### 3.5 What the result record holds for a team

History ring at `0x3BE130`, 16-byte records, count at `0x3BE624` (match-end-state.md). Team-specific, read from the
recorder `sub_001A6A80(winners_list, losers_list, code)` (lists are slot numbers ended by -1): [V]

* `+0xE` code as before (bit 0 decisive, bit 2 draw, 0x40 time up).
* `+0xC` participant bits: winners get 1, 2, 4, 8 by position in the list, losers 0x10..0x80. **With value 2 in
  `0x3B907E` and both lists non-empty the word starts at 0x100 when the first winner has the side marker (slots 2
  and 3 won) and 0x200 otherwise (slots 0 and 1 won)**, then the participant bits are OR-ed in.
* The recorder also sets `+0x933` bit 0 on winners and bit 1 on losers, and clears bit 0x100 of `+0x932` in all four.
* So **the partner of the last defeated fighter is in no list** in the generic branch. A report that wants "both
  team members won" has to expand the winner slot to its side: slots (w & ~1) and (w | 1). In the subway branch the
  winners list is already the surviving fighters only (a team whose partner died early has one entry).

---

## 4. The special matches (Q3)

For all six: front-end type 6..11, class 0, `0x3B907E` = 1, rules word 7, `Game.SetMatchRules(0)`, two fighters, a fixed
venue, no venue screen (`chooseF2` goes straight to `oldMLoad`). Everything else comes from the venue. [V]

| type | venue | venue.xml row | arena kind `0x3B9084` | start tables in gamecore.xml | level-data object / notes |
|---|---|---|---|---|---|
| Demolition | feID 17 | `17A` Gun Hill Garage (Parkade), ID 20 | 19 (0x13) [I] | **one on one only** | `<demolitionmatch>` (`CStageEnvironment::InitDemolition`, `CDemolitionCar`) |
| Inferno | feID 24 | `07A` Red Hook Tire Co. (Inferno), ID 7 | 7 [I] | **one on one only** | `<infernomatch>`; `sub_001D1140` handles environment kills for type 7 |
| Subway | feID 11 | `04A` 125th Street Station, ID 4 | 4 [V] | all | `<subwaymatch>`; elimination logic (section 3.4); train arrives after a random 30..60 s window (gamecore "SUBWAY Train come in time") |
| Window | feID 26 | `15A` Crow's Office, ID 18 | 17 (0x11) [I] | **one on one only** | `<windowmatch>` |
| Cage | feID 10 | `14A` Crow's Cage / Club Murder, ID 17 | 16 (0x10) [I] | all | no controller object found; wall/cage in the collision data |
| Ringout | feID 8 | `05A` The Dragon House, ID 5 | 5 [I] | all | `<ringoutposition>` list in the arena XML (`CArena::_LoadXML.m_pRingOutPosition`); ring-out compares the arena kind with 5 in four functions |

* **1v1 only?** The *mode* is 1v1 only (two fighters, class 0). The *venues* of Cage, Subway and Ringout also have
  Team/3-way/4-way start tables and can be played in those modes by choosing the venue; Demolition, Inferno and
  Window venues cannot (no start table). [V]
* The level components the arena loader recognises (`sub_00091010` sets one bit per component in the arena's flag
  word): inferno, subway, demolition, club light, window, sandbag. Cage and ringout have no component of their own. [V]
* Venues with a special "Blazin KO" cinematic camera in `assets/nis.viv`: 04A, 05A, 07A, 15A (Subway, Ringout, Inferno,
  Window). [V]
* **Lockstep relevance:** the subway train timer and the demolition cars / inferno fire / window objects are
  per-venue simulation objects. The subway venue is in the relay's `QuickPool` and ran online many times; Inferno
  (24), Window (26), Demolition (17) and Cage (10) appeared in a few logged online matches (feIDs 10, 26, 17 are in
  the `[AUTOMATCH] armed` lines). No dedicated soak exists for them. [I]
* **Result handling:** hazard kills go through `sub_001D1140` (type-dependent defeat bit) or `sub_001DA400` (type
  0x11) and then the same decision function; the winner is the surviving fighter's slot. Subway double eliminations
  are draws. [V]

So "adding the special matches online" is in substance **a venue pool of one, a locked player count of 2, and the
type id and logo for the room** (and for the account history). The fight setup could keep sending type 0.

---

## 5. What `automatch` needs (Q4)

The current sequence (`run_sequence` in `src/hooks/automatch.c`): `Option.SetDifficulty(2)`, `Game.StartBattleMode()`,
`Game.ResetMatchTypeList()`, `Game.ResetMatchData()`, `Game.SetMatchType(t)`, `Game.SetMatchRules(r)`,
`Controller.ControllerBind(-1)` and one bind per human pad, `MemCard.SetBMCurrentUserIndex(pad,-1)` per human,
`Battle.LoadCreatePlayerData(humans)`, `Controller.ControllerSetup(-1,0,0,0)`, `Controller.ControllerReturn(i)` per
slot, `Game.ResetFighterList()`, `Controller.ControllerSetup(i, pad_i, fighter_i, i)` per slot, `Game.ResetVenueList(0)`,
`Game.ChooseVenue(v)`, `Controller.GetCustomControlMap()`, `Game.GetMatchInfo()`, `Game.StartGame()`.

### 5.1 Team

No new calls are required: it is the 4-fighter sequence of Free for All with type 1.

```
Game.SetMatchType(1)
Game.SetMatchRules(0)
Controller.ControllerBind(-1) ; Controller.ControllerBind(p) for each human pad
Battle.LoadCreatePlayerData(4)            (the front end sends the mode's count, 4; automatch sends the humans)
Controller.ControllerSetup(-1,0,0,0) ; Controller.ControllerReturn(0..3)
Game.ResetFighterList()
Controller.ControllerSetup(slot, pad or -1, fighter, slot)    for slots 0..3, team A = 0,1 and team B = 2,3
Game.ResetVenueList(0) ; Game.ChooseVenue(v) ; GetCustomControlMap ; GetMatchInfo ; StartGame
```

* The `ControllerSelect` calls of `chsTeam4` only reorder the same four entries; setting the entries in final slot
  order with `ControllerSetup` gives the same `GS` table. The only extra thing `ControllerSelect` does is the
  per-position outfit byte, which `ControllerSetup` leaves alone (as it does for every mode today). [V/I]
* Test form: `RECOMP_AUTOMATCH="type=1,venue=1,f0=..,p0=0,f1=..,p1=1,f2=..,p2=-1,f3=..,p3=-1"` (the parser already
  accepts four fighters).
* `LoadCreatePlayerData`: our number of humans differs from the front end's number of fighters; only Create-a-player
  fighters (IDs 86 and up, not offered) read that data. Keep as is, but mirror the front end if a Team test shows
  anything odd. [I]
* Venue: choose only venues with a "2 vs 2 TAG" start table (everything except feIDs 24, 26, 17). `UnsafeVenues`
  (The Pit, 19) stays out.
* Rules: 0. CPU fill: `on_start` already fills missing places up to `mode->fighters` with seeded roster picks, the same
  on every PC; add `{ 1, "Team", 4 }` to `k_modes`.

### 5.2 Special matches

```
Game.SetMatchType(6..11)       (keeps GS+0xB8 = the front-end id: the summary and logos read it; the fight sees class 0)
Game.SetMatchRules(0)
... as One on One, two fighters ...
Game.ChooseVenue(17 | 24 | 11 | 26 | 10 | 8)     Demolition | Inferno | Subway | Window | Cage | Ringout
```

* The front end calls `Game.ChooseVenue` right after `SetMatchType` (before the pads join); automatch calls it after
  the fighters. `StartGame` reads the stored venue only, so the order should not matter. [I]
* Alternatively keep sending type 0 and only fix the venue; the fight is identical. Sending the real id changes only
  what `Game.GetMatchType()` and the match summary report.
* Add the six types to `k_modes` with fighters = 2 and a table `type -> fixed venue`; `on_start` takes the venue from the
  relay's START, so the relay's room needs a pool of exactly that venue (the START carries the voted venue; a pool of
  one is valid: `len(Pool) >= 1`).

### 5.3 Free for All / 3-Way (unchanged)

Types 2 and 3 are already live; the 3-Way and 4-Way entries come from `battleID`'s "3 or 4 players" button, so id 2 =
3 fighters and id 3 = 4 fighters. [V] (this also settles the [U] in online-match-setup.md section 1.2.)

---

## 6. Netplay, relay, accounts and UI changes

### 6.1 The slot assumption (the one real design issue)

START carries `type, rules, venue, count` and, per player, `(room slot, fighter)`. `on_start` sorts by room slot and
makes position i the game slot i with pad i; the free places (slots `count..mode->fighters-1`) become CPUs with pad -1.
Throughout `netplay.c` a human is `slot < N.players` (`netplay_on_pad_read`, `remote_slot_state`, `assign_input`'s
mask, the hash parts, `N.start_slots[w]`). [V]

Consequence for Team: position 0,1 form team A and 2,3 team B, so with 2 humans they are **partners against two
CPUs**; with 3 humans team B is one human and one CPU. A human-vs-human Team with CPU partners (humans in slots 0
and 2) needs a position-to-slot map in all of those places. Options:

1. **v1: Team rooms of 4 humans only** (room `max` 4 and START only with four ready), or accept "2 or 3 humans =
   team A first". No netplay change; the team is the room join order (position >> 1). A per-player "swap side" in the
   room would be an order change in the relay's member list.
2. v2: add `slot_of_pos[]` (and its inverse) and thread it through the pad injection, the lockstep entries, the hashes
   and the result mapping; the START would carry the team per player (a bit in the fighter byte or a new field).

The relay needs no change for either: `Mode` is any byte except 0xFF, `Max` 2..4 for every mode
(`maxPlayers`), pool 1..32, START already carries `Mode, Rounds, Venue, Players[]`. The client clamps `Max`
(`room_max`, `online_modes`); a Team mode would force 4 (or at least offer 2..4 with the v1 rule).

### 6.2 Results (team)

* Read the result like `koth_check_result` does: the ring at `0x3BE130` with its count at `0x3BE624`, code at `+0xE`: decisive
  (bit 0), draw (bit 2). Winner slot: `0x3B8F40` (phase 3) or `0x3B8F3C` (phase 2), as the King-of-the-Hill code does.
* Team: expand to the winner's side (`w >> 1`), so the `winners` array of the account report holds both members
  (the account side already accepts several winners: `winners` is a list, `valid()` only requires them to be
  slots of the match, and wins/losses are counted per account in the winner set). CPU partners appear as players with
  `cpu: true`. [V]
* Draw: must be taken from the code, not from the winner word (section 0, item 9).
* "Left" reports keep working per slot; a leaver's partner is not a winner automatically: the existing `report_left`
  marks no winners, so no change.
* Ranked stays One on One only; Team and specials are unranked.

### 6.3 Hub UI and accounts

* `online_modes` / `mode_cycle` / `mode_logo` already know the logo names for 0..3 and 6..11. For Team add the mode;
  for a special either add the six types (fighters 2, `max` fixed 2, pool fixed to the one venue, the venue picker
  hidden) or present them as venue "themes" of One on One.
* `report_event` writes `mode` and `mode_name` (`online_mode(type)->name`, at most 40 characters) and `venue`; both
  are free text for the account server; lineup/outcome agreement between the PCs uses `Mode`, `Venue`, players.
* The venue vote for Team and for 3-Way/4-Way must exclude 24, 26, 17.

---

## 7. Risks (Q5)

| risk | detail | tag |
|---|---|---|
| humans must be slots 0..n-1 | section 6.1; also the reason a 1+1 Team cannot be offered in v1 | V |
| venues without start tables | 07A (24), 15A (26), 17A (17) have only a one-on-one table; the game's behaviour with 3-4 fighters there is unknown; the one 3-Way logged at feID 17 desynced at step 300 and crashed (build 20, rollback on) | V / U |
| CPU partners | the partner pointer `+4` and the AI command path are new to our online runs (CPU fill so far was 3-Way/4-Way with `+4` null); AI output is part of the simulation and should be deterministic given the seeds, but untested | I |
| randomness | the subway train window (30..60 s), crowd, AI and hazards use the game's RNG, which `netplay_on_seed` hooks; Team adds no new random call from the menus (`0x3B907E` bit 0x40 is never set). A soak with four copies is still needed | I |
| camera / viewport modes | `Battle.SetCPViewportCam(4, ...)` is front-end only; the fight camera frames 4 fighters as in 4-way | I |
| controller ports beyond 4 | none: Xbox pads 0..3 only; automatch binds pads 0..3 | V |
| per-machine state | the match summary and results screens read the setup entries (`ControllerReturn` number, CPU flag) and the team art (`paper_team.big`); the summary shows a win only for the recorded winners (one per KO in the generic branch), a cosmetic mismatch for the partner | I |
| winner/draw reading | draws read as slot 0 (existing, section 0) | V |
| unlock state | `StartGame` does not check it, so locked specials start; the screens' lock only gates the menu | V/I |
| rematch | `Game.Rematch()` re-runs with the same setup; team positions are kept in the `GS` entries. Not tested | U |

---

## 8. Experiments that turn the [I] into [V]

1. **Venue to arena kind table:** run `RECOMP_AUTOMATCH="type=0,venue=N,..."` for every feID in the venue list (the harness
   already prints `match_type` in the telemetry) and write the table to this document. Confirms the "ID minus one"
   reading and which kind each special venue has.
2. **Offline Team, two pads:** `type=1` with four fighters, pads 0 and 1 human and the rest CPU, at feID 1 and 11:
   check `0x3B907E` = 2, `0x3B9C17`-style side markers (value 4 on slots 2 and 3), start positions, the partner pointer
   at record `+4`, and the result record's `+0xC` after a KO in each of the four ways (generic KO, subway elimination).
3. **Faithful capture:** play one Team match and one Cage match offline through the real menus with `[FUNCCALL]` logging
   on (`grep "FUNCCALL\] \(Game\|Controller\)\."`), and compare with section 5; in particular whether the front end
   sends `ControllerSetup` after `ChsTeam4`'s `ControllerSelect` calls.
4. **Special venues with `type=6..11` versus `type=0`:** the telemetry state of the first 300 steps should be identical
   (same venue); then four-copy soaks for 24, 26, 17, 10 and 8, which have almost no online hours.
5. **Draw check:** two players walking into the subway train together, and a time-up tie with Crowd Votes, to confirm the
   0x105 / 0x45 records and fix the report path.

## 9. Corrections to earlier notes

* match-end-state.md "`0x3B907E` bit 2 = team": the team flag is the mask value 2 (bit index 1); value 4 is the 3-way
  mode and `+0x937` value 4 is the team side marker.
* online-match-setup.md: the fight's `0x3B9084` being "a different number" than the front-end id is explained here (it is
  an arena kind from the venue); the free-for-all ids are 2 (3 fighters) and 3 (4 fighters).
* fighter-record-fields.md `+0x937` bit 2 "team or partner flag": it is the side marker for slots 2 and 3 in team mode.
