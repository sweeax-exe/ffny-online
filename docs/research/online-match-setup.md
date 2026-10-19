# Starting a match automatically for online play: how the front end hands over the match, and how to replay it

Read-only research. Nothing was built or run. Sources: the lifted C in `src/recomp/gen/recomp_*.c`
(read with `grep`/`sed`), `src/recomp_manual.c`, `src/hooks/{postcmd,netplay}.c`, the logs in
`logs/run-*.log.err`, the dump's `fighter.xml`, `venue.xml`, `screens/feflow.xml`, and the XBE image
`<data>\extracted\default.xbe` read offline (string and pointer lookups only, the way
`scripts/string-va.py` does). No game bytes are reproduced beyond IDs and names.

Confidence tags: **[V]** verified by reading the exact code or log line, **[I]** inferred from
code or log patterns (reasoning stated), **[U]** unknown, needs a run.

Conventions: `GS` = the game-flow singleton component `MEM32(0x2FB148 + 4*0x18)` (see section 2.3).
Guest addresses are hex. "Hook thread" = the guest thread that calls `FuncCall` (the one that runs
`funccall_hook`).

---

## 0. Findings in one page

1. The front end (Flash screens in `screens.viv`) talks to the game only through `FuncCall("Module.Method(args)")`,
   which the game parses and dispatches by name. A match is configured by about eight such calls
   (`Game.StartBattleMode`, `Game.SetMatchType(t)`, `Game.SetMatchRules(r)`, `Controller.ControllerBind(n)`,
   `Controller.ControllerSetup(slot,pad,fighter,slot)` per fighter, `Game.ChooseVenue(feID)`) and started by
   `Game.StartGame()`. There is no `ChooseFighter` call in any log: fighters travel in `ControllerSetup`. [V]
2. The native side is one registry of `{name hash -> handler}` tables per module. The `Game` handlers are
   registered in `sub_000475F0`; the dispatcher is `sub_000457D0` behind the `FuncCall` entry
   `sub_00068BA0` (already wrapped by `funccall_hook`). Calling `sub_00068BA0` with a string is
   byte-for-byte what a menu does, so a hook can replay a whole menu path without touching Flash. [V]
3. Handlers only write into `GS` (a 5-byte entry per slot at `GS+0x1AA9+5*slot`, match type at `GS+0xB8`,
   rules at `GS+0xBC`, venue in the `Game` module object at `+0xCA4`). `Game.StartGame` then looks
   the fighters and the venue up in the XML databases and builds the match-setup block that the
   fight copies into its globals. Fighter argument = `fighter.xml <ID>` (proved from the intro
   speech file the game opens, section 3.2); venue argument = `venue.xml <feID>`. [V]/[I]
4. `RECOMP_POST_CMD` is a bring-up diagnostic that writes a 32-byte command into a title-loop pointer
   from a side thread. It cannot start a match and cannot call handlers (wrong thread, wrong
   subsystem). It is not the facility to build on. [V]
5. Proposal: a small `automatch` module that, on the hook thread, replays the exact FE call sequence
   (section 5.2) as strings through `sub_00068BA0`, driven by one config message from the host
   that both PCs execute at the same barrier. Fallback: the existing pad-script route. Five
   experiments (section 6) take it from "plausible" to "proved" with no code risk to netplay.

---

## 1. What the front end sends (the call sequence)

### 1.1 Where it comes from

`funccall_hook` (`src/recomp_manual.c`, around line 556, installed for guest `0x00068BA0` by
`recomp_lookup_manual`) prints every call as `[FUNCCALL] <text>`. Note the cap `printed++ < 4000`:
long sessions stop logging after 4000 lines (the harness runs have about 230). [V]

### 1.2 Full sequence, a two-player Versus run (log `run-20261019-210923.log.err`)

Noise removed: `Audio.*`, `Game.GetLanguage`, `Controller.GetControllersEnabled`,
`Game.GetNextFighter` (about 75 per visit), `Game.GetNextVenue` (about 25), `Game.GetNextMatchType`.
Left column is the log line number. [V]

```
28140 Control.GetScreenInfo(battle/cmtype.swf)      <- Battle chosen on the main menu
28143 Game.StartBattleMode()
28271 Game.ResetMatchTypeList()
28286 Game.ResetMatchData()
29406 Game.SetMatchType(0)                           <- One on One highlighted/selected
30511 Control.GetScreenInfo(options/battleID.swf)
30513 Game.SetMatchRules(0)
30579 Game.GetMatchType()
30580 Controller.ControllerBind(-1)                  <- clear all joins
30598 MemCard.ChooseMCDevice()
31961 Controller.ControllerBind(0)                   <- pad 0 pressed START
32794 Controller.ControllerBind(1)                   <- pad 1 pressed START
33651 MemCard.SetBMCurrentUserIndex(0,-1)
34487 MemCard.SetBMCurrentUserIndex(1,-1)
35298 Battle.LoadCreatePlayerData(2)
35299 MemCard.QuerySave( options/battleID.swf )  /  35300 MemCard.Save( options/battleID.swf )
35686 Game.GetMatchType()  x2
35688 Control.GetScreenInfo(battle/chooseF2.swf)
35791 Battle.SetCPViewportCam(2,0)
35792 Controller.ControllerSetup(-1,0,0,0)           <- reset / no-op (arg0 = -1)
35793 Controller.ControllerReturn(0)  /  35794 Controller.ControllerReturn(1)
35795 CreatePlayer.GetAttributesBM(0)  /  35796 CreatePlayer.GetAttributesBM(1)
35797 Game.ResetFighterList()
 ...  (Game.GetNextFighter x ~75: the roster grid is read)
37653 Controller.ControllerSetup(0,0,55,0)           <- slot 0, pad 0, fighter 55
39317 Controller.ControllerSetup(1,1,22,1)           <- slot 1, pad 1, fighter 22
39318 Game.GetMatchType()
39319 Control.GetScreenInfo(battle/chsVenue.swf)
39384 Game.GetGameMode()  /  39385 Game.GetMatchType()
39386 Game.ResetVenueList(0)   (Game.GetNextVenue x ~25)
39412 Crib.EnterMap()
41381 Game.ChooseVenue(1)                            <- venue 1 = The Foundation
41382 Control.GetScreenInfo(battle/oldMLoad.swf)     <- the "loading" screen
41401 Audio.StopMusic()
41403 Game.GetMatchInfo()
41405 Battle.SetCPViewportCam(2,1)
41441 Controller.GetControllersEnabled()
41442 Controller.GetCustomControlMap()
41782 Audio.FadeMusic()
41783 Game.StartGame()                               <- ~400 log lines (about 10 frames) after oldMLoad appears
```

One on One against the CPU (logs `run-20261019-180729`, `-190709`) differs only in: one `ControllerBind`
(n=0 or n=1, the pad that pressed START), and the second `ControllerSetup` is
`Controller.ControllerSetup(1,-1,55,1)` (pad -1 = no pad = CPU). In `190709` the human is on pad 1 and
slot 0: `Controller.ControllerSetup(0,1,56,0)`, i.e. the slot is not tied to the pad. [V]

Free For All is driven by the harness route `ffa` (`scripts/harness.py`, `FFA_SETUP`): third entry of
the match-type list, then screen `battle/chooseF4` and four `ControllerSetup` stages. No available log
contains it (grep for `choosef4` finds nothing), so the exact `SetMatchType` id for FFA and the CPU
`ControllerSetup` arguments for slots 2 and 3 are [U]; the pattern `(slot,-1,fighter,slot)` is [I].

### 1.3 Argument meanings

| Call | Arguments | Evidence |
|---|---|---|
| `Game.StartBattleMode()` | none. Makes the battle-mode run module and sets `GS+0x344 = 1`. `StartGame` does nothing unless `GS+0x344 == 1`. | [V] handler `0x46410`, `StartGame` test at `0x46E70` |
| `Game.ResetMatchTypeList()` | none. Rebuilds the unlock bit mask of match types (`this+0xDAC`) and the cursor (`this+0xDB0`). | [V] `0x47520` |
| `Game.ResetMatchData()` | none. `GS+0xB8 = 0xD` (no type), `GS+0xBC = 4` (rules default), `GS+0x334 = 0`, the 4 fighter entries get 0x9D (empty), controller bindings cleared. | [V] `0x46BB0` -> `sub_0001EC50`, `sub_0001E660` |
| `Game.SetMatchType(t)` | `t` = index in the front-end match-type list (`GetNextMatchType` returns `NAME=%s&STATE=Unlocked/Locked&ID=%d`, `ID` = list index 0..12). 0 = One on One (log). Stored in the `Game` object `+0xC7C` and in `GS+0xB8`. | [V] handlers `0x46790`, `0x46BE0` |
| `Game.SetMatchRules(r)` | rules index, `GS+0xBC` (and `Game+0xC80`). Always 0 in the logs. Meaning of other values [U]. | [V] `0x46850` |
| `Controller.ControllerBind(n)` | `n >= 0`: join pad `n` as player `n` (what START does on the battle-ID screen); `n = -1`: clear all joins. | [I] handler `0x556B0` (writes controller-manager table and `GS` entry byte `+3`) |
| `Controller.ControllerSetup(a0,a1,a2,a3)` | `a0` = slot or -1 (-1 = ignore), `a1` = pad / controller port (-1 = CPU, no pad), `a2` = **fighter ID** (`fighter.xml <ID>`), `a3` = index of the 5-byte entry written (equal to the slot in every log). Stores entry `GS+0x1AA9+5*a3`. | [I] decoded from the lifted handler `0x558E0` by stack-slot tracking; fighter meaning [V] by speech files (3.2) |
| `Game.ChooseVenue(v)` | `v` = `venue.xml <feID>` (the id of the venue-map location). 1 = The Foundation, 6 = Terror Dome (harness), 19 = The Pit. Stored in `Game+0xCA4`; no database lookup yet. | [V] `0x45FD0`; names [V] from `venue.xml` |
| `Game.StartGame()` | none. Reads everything above, resolves venue and fighters in the databases and starts the level load. | [V] `0x46E40` |
| `Game.GetMatchInfo()` | none. Reports the setup back to Flash (`matchtype=`, `venue=` strings); reads `GS+0x3B4`. Cosmetic for the loading screen. | [V] `0x46D20` |
| `Controller.GetCustomControlMap()` | none. Presumably copies the player's custom button map into the per-slot binding table `0x3B90C8+slot*0x18` the fight uses. | [I] handler `0x55C30` not read; table role [V] in `fighter-record-fields.md` |

---

## 2. How the native side implements it

### 2.1 Dispatch chain (all [V])

```
Flash ActionScript FuncCall("Game.StartGame()")
  -> GetURL -> guest 0x00068BA0 (cdecl, one stack arg = char* command)        [hooked: funccall_hook]
       copies the string into a 512-byte stack buffer, calls
     sub_000457D0(buf)   : split "Module" "." "Method" "(" args ")"
         sub_00045510  tokenizer (the "." at 0x2928E4 and "(" at 0x2928E8)
         sub_000455E0  module lookup: count MEM32(0x341534), module objects MEM32(0x3412B0 + 4*i)
         sub_00045670  method lookup in the module's table (count at module+0xC08, entries of 0x30
                       bytes starting at module+8, name hash at +8, handler at +0xC) and
                       handler(this=module, char **argv, int argc) via an indirect call
     result: handler returns eax = 0 (or a char* result such as 0x341330, the shared return buffer);
     sub_00068BA0 copies a non-null result into a new string and returns it; a null result yields
     the string "NO_RETURN" at 0x299A9C / "NO_FOUND" at 0x292998.
```

Name hash = djb2 (`h = h*33 + c`, start 5381) in `sub_000CAA30`. Handlers are `thiscall`,
`ret 8`, arguments are decimal strings parsed with `sub_00200D22` (an `atoi`/`strtol` tail jump).
Registration is `sub_00045610(module, nameString, handler)`; `sub_000475F0` registers the `Game`
module, `sub_00055E00` the `Controller` module, `sub_0006AC10` `Battle`, `sub_00053700` `MemCard`,
`sub_0006ADE0` `Control`, `sub_000619E0` `Crib`.

### 2.2 Handler addresses (extracted by reading the registration calls in the lifted code)

`Game` (module registered in `sub_000475F0`): [V]

| Method | Handler | | Method | Handler |
|---|---|---|---|---|
| ChooseCharacter | 0x46020 | | StartBattleMode | 0x46410 |
| ChoosePlayer | 0x46090 | | StartStoryMode | 0x46450 |
| ChooseVenue | 0x45FD0 | | StartMainMenu | 0x46610 |
| ChooseFightingStyle | 0x46170 | | StartGame | 0x46E40 |
| InitGame | 0x45F50 | | Rematch | 0x46380 |
| FEDone | 0x45F20 | | Quit | 0x462C0 |
| SelectMatchType | 0x45FA0 | | FEScreenLoaded | 0x463F0 |
| SetMatchType | 0x46790 | | EnterFEScreen | 0x465B0 |
| GetMatchType | 0x46810 | | LoadNextRunModule | 0x46200 |
| SetMatchRules | 0x46850 | | GetMatchSummary | 0x466C0 |
| GetMatchInfo | 0x46D20 | | GetNumPlayers | 0x46690 |
| ResetMatchData | 0x46BB0 | | GetGameMode | 0x46E10 |
| ResetFighterList / GetNextFighter | 0x46AC0 / 0x46AE0 | | ResetVenueList / GetNextVenue | 0x46B10 / 0x46B50 |
| ResetMatchTypeList / GetNextMatchType | 0x47520 / 0x46BE0 | | GetLanguage | 0x46CB0 |

`Controller` (`sub_00055E00`): ControllerBootup 0x55640, **ControllerSetup 0x558E0**, ControllerSelect 0x55A20,
**ControllerBind 0x556B0**, ControllerRemove 0x55760, ControllerReturn 0x557C0, GetCustomControlMap 0x55C30,
GetControllersEnabled 0x55DA0. `Battle`: LoadCreatePlayerData 0x4BFC0, SetCPViewportCam 0x4BFD0,
StartUserMainMenuCtrl 0x4C150. `MemCard`: SetBMCurrentUserIndex 0x528E0, Save 0x52B60, QuerySave 0x52990.
`Control`: GetScreenInfo 0x55490, GetPrevScreenInfo 0x55570.

`ChooseCharacter`/`ChoosePlayer` exist and write `Game+0xC84/0xC94` but are never called by the front
end in any log: fighters do not travel that way. Both only emit the debug line
`GAME: PLAYER %d --> CHARACTER %d %s` (format at 0x292EE0). [V]

### 2.3 The state the handlers write

`sub_00012CF0(ecx=0x2FB148, idx)` is simply `MEM32(0x2FB148 + 4*idx)`: the singleton is an array of
component pointers. Components used by the match path: 0x18 = `GS`, 0x19 = controller manager,
0x0D and 0x0C = front-end/flow objects, 0x10 = error sink used by `sub_00068BA0`. [V] (indices),
names are my labels.

`GS` layout touched by the match path:

| Offset | Meaning | Written by | Tag |
|---|---|---|---|
| `+0x344` | 1 = battle mode (required by StartGame) | StartBattleMode | V |
| `+0x38C` | current run module object (vtable +8 = start/advance, +0xC = other) | StartBattleMode/StartMainMenu/Quit/Rematch | V |
| `+0xB8` | match type, FE list index; 0xD after reset | SetMatchType / ResetMatchData | V |
| `+0xBC` | rules; 4 after reset | SetMatchRules / ResetMatchData | V |
| `+0x1AA9 + 5*i` | entry i, byte 0: device id of the bound pad (from the controller manager) or 0x9D when empty | ControllerSetup (`sub_0001E690`) | I |
| `+0x1AAA + 5*i` | entry i, byte 1: **fighter ID** (`sub_0001E820(i)` reads it in StartGame; 0 is replaced by 0x62) | ControllerSetup (`sub_0001E700`) | V (reader), I (writer arg) |
| `+0x1AAB + 5*i` | entry i, byte 2: raw `a1` (pad/port argument) | ControllerSetup | I |
| `+0x1AAC + 5*i` | entry i, byte 3: bound marker (0x9D = none) | ControllerBind (`sub_0001E6C0`), ControllerRemove | I |
| `+0x1ABD + i` | per-slot byte (`sub_0001E6E0` / `sub_0001E810`), -1 after reset | ControllerRemove | I |
| `+0x1AD0` | venue side index from the 14-entry table at `0x2DD978` (pairs feID -> 0..13; 0 for feIDs not in it, e.g. 6 and 19) | StartGame via `sub_000461C0` | V |
| `+0x3B4` | venue feID re-read from the database record | StartGame -> `sub_00020270` | I |
| `+0xC0 ...` | the **match-setup block** (`GS+0x1E280()` returns `GS+0xC0`). Its `+0xA0` = `GS+0x160` is the match type the fight copies to `0x3B9084`. | StartGame -> `sub_00020270` | I |

`ControllerSetup` also mirrors four 5-byte entries into the global `0x2DDC08` after each call. [V]

The `Game` module object (found through the registry, section 2.1) holds: `+0xC7C` match type,
`+0xC80` rules, `+0xCA4` **venue feID** (`ChooseVenue`), `+0xC84[4]`/`+0xC94[4]` (ChooseCharacter/
ChoosePlayer, unused), `+0xC40` fighter list, `+0xC0C` venue list, `+0xDAC` match-type unlock mask,
`+0xDB0` match-type cursor. [V]

### 2.4 What `StartGame` does (all [V] from the lifted code at `0x46E40`, paraphrased)

1. Fetches `GS` and component 0x0D; if `GS+0x344 != 1` it writes 0 to the component's first word and returns (a no-op).
2. `sub_000461C0(Game+0xCA4, GS)`: the venue side index (`GS+0x1AD0`).
3. Looks up the venue in the venue database by the attribute whose hash is `0xB03E16FC` equal to the feID
   (this is the attribute `GS+0x3B4` and `GetMatchInfo` read back), through `sub_00012ED0`/`sub_000BEAD0`/`sub_000BE560`.
4. For each of the 4 slots: `fighter = sub_0001E820(slot)` (0 becomes 0x62), looks the record up by the attribute hashed
   `0x4CE5B1B2`, counts active slots (entry byte 0 nonzero) and builds a bitmask of empty ones.
5. Reads the match type from `Game+0xC7C` and calls `sub_00020270(GS, ...)` which fills the setup block at `GS+0xC0`
   (match type record -> `GS+0x160`, venue attributes -> `GS+0x1EC..0x204`, `GS+0x3B4`), and sets the first word of
   component 0x0D to 1 ("start accepted"). The main loop then loads the level; `sub_0002CF80` (prints
   "JUMP STARTING THE DAMN GAME!!!") builds the world.
6. The fight itself copies the block into globals at match start (`sub_000871F0`, called from `sub_0008BB00`; documented in
   `match-end-state.md` and `fighter-record-fields.md`): match type -> `0x3B9084`, rules -> `0x3B9078..0x3B907E`,
   per-slot character index `setup[+0xA8+4s]-1` -> `0x3B90B8[s]` -> record `+0x2C`, CPU flag `setup[+0xB8+4s]` ->
   `0x3B9088[s]` -> record `+0x937` bit 0.

Unresolved here: the exact stores that put the per-slot fighter and CPU flag into the setup block (`setup+0xA8`,
`+0xB8` = `GS+0x168`, `GS+0x178` by the +0xC0 base). They are inside `sub_00020270`/`sub_0002CF80`, 1000+ lines, and
were not traced. [U]

---

## 3. Where venue and fighter IDs are, and what is valid

### 3.1 Match types (FE list index)

`0x2DBCC8` is a table of 13 name pointers: 0 Single, 1 Tag, 2 3Way, 3 4Way, 4 1vs2, 5 1vs3, 6 Demolition,
7 Inferno, 8 Subway, 9 Window, 10 Cage, 11 Ringout, 12 Tutorial [V]. `GetNextMatchType` formats
`NAME=<table name>&STATE=Unlocked|Locked&ID=<index>` [V], so `SetMatchType`'s argument is this index.
Verified in logs: `0` = One on One. The harness takes the third visible entry for Free For All (4 fighters,
screen `chooseF4`), which would be `SetMatchType(2)`, but the display order or ID of FFA was never logged [U].
`feflow.xml` shows the follow-up screens: `chooseF2`, `chooseF3`, `chooseF4`, `chsTeam4` then `chsVenue` then `oldMLoad`.
The "unlocked" mask is `Game+0xDAC` (built from save data, `sub_00047520`); StartGame does not re-check it. [I]
The fight's own `0x3B9084` is a different number (a live One on One reports 2 in
`tests/unit/test_scenario_evidence.py`; it comes from the database record, not the list index). [I]

### 3.2 Fighters: `ControllerSetup` argument 2 is `fighter.xml <ID>`

Proof from the logs. After `Game.StartGame` the game opens the fighters' intro speech streams
`speech\streams\intro\<speechID>.asf`. In `fighter.xml` the `speechID` of ID 55 (DOC) is 25, of ID 22 (MAGIC) is 2,
of ID 56 (BLAZE) is 22 ([V], dump data):

| Run | `ControllerSetup` fighter args | Intro streams opened after StartGame |
|---|---|---|
| 210923 | 55, 22 | `025.asf`, `002.asf` = DOC, MAGIC |
| 180729 / 190709 | 56, 55 | `022.asf`, `025.asf` = BLAZE, DOC |

The same numbers read as `coreID` would be NYNE/SCARFACE/BO, whose speech IDs are 0/26/0: they do not match.
So the argument is `<ID>`; the defaults of the front end are Blaze (56) for player 1 and Doc (55) for the CPU. [V]
(The earlier note in `fighter-record-fields.md` and the `scripts/test_evidence.py` regex name the fourth argument
"difficulty"; the code decode in 1.3 says it is the entry index. Treat the fourth argument as "= slot" until
the CPU level control is found. [I])

Valid values: `<ID>` 0..85 are stock characters (list below); 86..141 are the Create-a-Player archetype placeholders
(`CP B__`, `CP K__`, ..., `CP_HERO`) and need create-a-player data: do not use them online. Names, from `fighter.xml`:

```
0=HOUSE 1=BO 2=NYNE 3=TRICK 4=CRUZ 5=CHECHO 6=SKULL 7=TREJO 8=ICE-T 9=SOLO 10=WIDOWMAKER 11=ELEPHANTMAN
12=SEAN_PAUL 13=CHIANG 14=MASA 15=TDB 16=WC 17=CRACK 18=BAXTER 19=DAN_G 20=CRAZY_LEGS 21=FLAVA_FLAV
22=MAGIC 23=BUBBA_SPARXX 24=LIL_FLIP 25=BANNER 26=BLESS 27=ROME 28=HAVOC 29=TBD_SOUTH 30=FAM_LAY
31=BONECRUSHER 32=KURUPT 33=BABY_CHRIS 34=MACK_10 35=XZIBIT 36=WarrenG 37=CROW2 38/39/41/42/85=unnamed (---nn)
40=BULLETPROOF 43=BEEK 44=FREEWAY 45=GHOSTFACE 46=COMP 47=CAPONE 48=NORE 49=TBD 50=JOE_BUDDEN 51=SCARFACE
52=LL_COOL_J 53=DMX 54=LUDACRIS 55=DOC 56=BLAZE 57=STICKY 58=CROW 59=D-MOB 60=SHADOWYFIGURE 61=JERVIS
62=STARKS 63=BIGPAUL 64=LIL_KIM 65=SHAWNNA 66=KELIS 67=CINDY 68=SHANIQUA 69=CARMEN_ELECTRA 70=LAUREN
71=SNOWMAN 72=STINGRAY 73=JACOB 74=MANNY 75=ROLINS 76=POCKETS 77=PRODIGY 78=S-WORD 79=LOCKED
80=STICKY2 81=BLAZE2 82=BLAZE3 83=D-MOB2 84=D-MOB3
```

Which of them are selectable in the real roster (unlocks, placeholders such as `TBD`, `LOCKED`, `---nn`,
story-only duplicates) is not decided by the databases alone; build the offered list from the front end's own
`Game.ResetFighterList()` / `Game.GetNextFighter()` output (string `NAME=..&STATE=Unlocked|Locked&ID=..`, ID semantics
to be confirmed, experiment E0). Do not hard-code the 85-name list as "playable".

### 3.3 Venues: `venue.xml <feID>`

`ChooseVenue(feID)` [V]. feID -> venue (venue `<ID>`), for the entries a front end shows (`CUT` rows omitted):

```
1  The Foundation (2)          2  The Limit (22)          4  Club 357 (9)        5  Club 357 Intense (10)
6  The Terror Dome (21)        7  Syn Energy Power Plant (3)  8  The Dragon House (5)  10 Club Murder (17)
11 125th Street Station (4)    12 Babylon (24)            13 Chopshop (14 and 15 share it)  14 Hunt's Point Scrapyard (0)
15 Scrapyard After Hours (1)   16 7th Heaven (25)         17 Gun Hill Garage (20)  18 The Heights (6)
19 The Pit (16)                21 Gauntlet (11)           22 Gauntlet Round Two (12)  24 Red Hook Tire Co. (7)
25 Crow's Alley (19)           26 Crow's Office (18)      9  Red Room (26) and 7th Heaven Intense (27) share it
0  Blaze's Gym (13) and many story interiors (do not use)
```

Duplicated feIDs (9, 13, 20) resolve to whichever record the database query returns first [U].
Defaults/logged: 1 (Foundation), 6 (Terror Dome, harness), 19 (The Pit, manual run). The venue's real files are
`V2BG_xx` in `assets\v2bg.viv`. Venue availability on the map is a save-data property; `StartGame` itself does not
check it. [I]

### 3.4 Where to read it at run time

| What | Address | When |
|---|---|---|
| `GS` pointer | `MEM32(0x2FB1A8)` (= `0x2FB148 + 4*0x18`) | any time after boot |
| Match type / rules | `GS+0xB8`, `GS+0xBC` | after SetMatchType / SetMatchRules |
| Fighter table | `GS+0x1AA9`, 5 bytes x 4 (also mirrored at `0x2DDC08`) | after ControllerSetup |
| Venue feID | `Game` module object `+0xCA4`, `GS+0x3B4` after StartGame | after ChooseVenue / StartGame |
| Setup block | `GS+0xC0` (+0xA0 type, +0xA8.. fighters, +0xB8.. CPU flags) [I] | after StartGame |
| Fight globals | `0x3B9078..0x3B927C` (type `0x3B9084`, CPU flag `0x3B9088[s]`, char `0x3B90B8[s]`), records `0x3B92E0 + s*0x12E8` (`+0x2C` character) | at match start (`sub_001A4D90`) |

---

## 4. `RECOMP_POST_CMD` and what it can and cannot do

`src/hooks/postcmd.c` (started from `src/main.c:698` only when the variable is set) is a thread that sleeps
`RECOMP_POST_CMD_MS` (default 25 s) and writes a 32-byte structure (command id at `+8`, rest zero) at
guest `0x83F00000`, then stores its address in `MEM32(0x002EB73C)`. That pointer is read by the title front-end loop at
`0x0019AB60`, which dispatches a 31-entry jump table on the id. It was written to separate "the machinery is broken"
from "nothing posts commands" during early bring-up; its own header says nothing in the image ever writes that pointer.
[V]

What that means for this task:

* It cannot start a match: it never calls a handler, it never uses the `FuncCall` registry, and the `0x2EB73C`
  loop is the title loader, not the battle path. Nobody has documented any command id that maps to
  `StartGame` (no log contains a `[POST]` line). [V]
* It runs on its own host thread. Guest state (registers are `RECOMP_TLS`, the stack, the allocator) is per
  guest thread, so a guest function must not be called from there. [V] for TLS registers (`recomp_manual.c`
  includes `recomp_types.h` for exactly this reason).
* Do not reuse the "write a pointer and hope a loop picks it up" idea: the working hook point is `FuncCall` itself.

---

## 5. Proposal

### 5.1 Hook point and calling convention

Run everything on the hook thread inside `funccall_hook` (`src/recomp_manual.c`). In the menus the front end
calls `FuncCall` every frame or two (`Controller.GetControllersEnabled` 406 times, `Game.GetLanguage` 375 times
over the logs), so it doubles as a game-thread tick that exists exactly when the front end is alive. Calling the
lifted `sub_00068BA0` directly (not through `recomp_lookup`) skips the `[FUNCCALL]` log line, so the helper should
print its own.

```c
/* sketch: replay one front-end command; hook thread only. cdecl, pops only the return address. */
static uint32_t fe_call(const char *cmd)
{
    uint32_t sp0 = g_esp, s, n = (uint32_t)strlen(cmd) + 1;
    s = (sp0 - 0x40 - n) & ~3u;            /* the string sits above the callee's 0x200-byte frame */
    for (uint32_t i = 0; i < n; i++) MEM8(s + i) = (uint8_t)cmd[i];
    g_esp = s;
    PUSH32(g_esp, s);                      /* argument */
    PUSH32(g_esp, 0x00000000u);            /* dummy return address, popped by the callee */
    sub_00068BA0();
    g_esp = sp0;
    return g_eax;                          /* guest address of the result string, or 0 */
}
```
This follows the existing manual-override convention (`PUSH32(g_esp, ...)`, call, `g_esp += n`, see the
`sub_0025F530` and timer overrides). [I] for the exact stack margin; test with E1.

### 5.2 The sequence to replay (from the Versus log, section 1.2)

Parameters: `T` match type, `R` rules, `F0..F3` fighter IDs, `P0..P3` pad per slot (-1 = CPU), `V` venue feID.

```
Game.StartBattleMode()
Game.ResetMatchTypeList()
Game.ResetMatchData()
Game.SetMatchType(T)
Game.SetMatchRules(R)
Controller.ControllerBind(-1)
Controller.ControllerBind(0)                      # once per human pad (host pad = 0, guest pad = 1)
Controller.ControllerBind(1)
MemCard.SetBMCurrentUserIndex(0,-1)               # once per human, as the FE does
MemCard.SetBMCurrentUserIndex(1,-1)
Battle.LoadCreatePlayerData(2)                    # number of humans, as logged ("2" with two pads)
Controller.ControllerSetup(-1,0,0,0)
Game.ResetFighterList()
Controller.ControllerSetup(0,P0,F0,0)
Controller.ControllerSetup(1,P1,F1,1)             # 2-player; for FFA add (2,P2,F2,2) and (3,P3,F3,3) [I]
Game.ResetVenueList(0)
Game.ChooseVenue(V)
Controller.GetCustomControlMap()
Game.GetMatchInfo()
Game.StartGame()
```

Which lines are essential is not proved. By code reading, the necessary effects are StartBattleMode (flag the
StartGame test needs), ResetMatchData (clears stale slots), SetMatchType/Rules, ControllerBind (joins the pads),
ControllerSetup (the fighters), ChooseVenue and StartGame; the `MemCard`, `Battle.LoadCreatePlayerData`,
`GetCustomControlMap`, `GetMatchInfo` and `ResetVenueList` lines reproduce the front end's other side effects
(profile index for the create-a-player data, the button map). Keep them in the first version; remove one at a time
in experiments. [I]

### 5.3 Making it identical on both PCs at the same moment

1. The host (player 1) holds the settings (extend the Online page in `pc_ui.cpp`, the host picks T, venue, two
   fighters; later FFA slots). It sends one data packet (new payload type `P_MATCHCFG` beside `P_GAMEHELLO`,
   `P_STEPS`, `P_HASH` in `netplay.c`): config id, the parameters above, and a 32-bit hash of the parameters.
   Both sides log the config.
2. The guest checks the build id (already part of the session), the dump identity and the hash, replies `P_MATCHACK`.
3. Both PCs are "armed". Each executes the sequence at the first `funccall_hook` after: the front end reached
   `intMain/mainMenu` and settled (`Audio.PlayMusic` seen, no `MemCard.*` call in the last second), and the peer's
   ACK/READY has arrived. Because both run the same strings from the same front-end state, no clock agreement is
   needed; the existing lockstep already waits up to 90 s for the first remote step (`START_LIMIT_MS`), covering
   unequal load times.
4. Immediately before `Game.StartGame()` call a new `netplay_reset_for_match()` that zeroes the per-source seed
   counters (`N.seeds[*].count`) on both PCs: today they count seeding calls since the session began, including
   any made while sitting in menus, and those need not be equal on both PCs. [I] (see risks)
5. Rematch: use `Game.Rematch()` (handler `0x46380`, front-end screen `battle/rematch` exists) or re-run the sequence
   from the front end the game returns to (`Game.FEScreenLoaded` then `GetScreenInfo(battle/cmtype.swf)` in the log).

### 5.4 Preconditions (what the game needs)

* The front end is alive and idle: `Control.GetScreenInfo(intMain/mainMenu.swf)` seen (it is the earliest screen
  where `Game.StartBattleMode` is a normal request; `preStart` and the boot screens are earlier and have the
  MemCard boot sequence in flight, `MemCard.BootupLoad` etc.). [I]
* Pads: `ControllerBind(n)` needs the controller manager (component 0x19) to know pad `n`, which comes from the
  USB hub built at start with `RECOMP_USB_PADS` pads (`pc_input_start`); online play needs two on both PCs
  (`xbox_HostInputSetNetplay(1, N.slot, 1 - N.slot, remote_state)` makes the remote player the other pad). Make sure
  the launcher starts with at least 2 hub pads when netplay is selected. [I]
* Profiles: the logs always use `SetBMCurrentUserIndex(i,-1)` (no profile), so no profile is required. The harness
  needs a user-ID dance only "with more than one user ID in the save area" (`harness.py` comment). [I]
* No MemCard/autosave popup in flight (`MemCard.AutosaveWarningCB` appears on the title/main-menu path). [I]

### 5.5 Alternatives, in order of preference

A. **Replay FuncCall strings from the main menu** (above). Smallest code, deterministic, no timing.
   Risk: it skips the Flash screens, so the Flash side still shows the main menu while the game starts; the
   normal flow shows `oldMLoad` for about 10 frames. Unproven that this is harmless. (E1, E2)
B. **Hybrid**: navigate the front end to `oldMLoad` with a deterministic pad script (the `harness.py` machinery:
   `@getscreeninfo(...)` anchors) and replay only the data calls (`ControllerSetup`, `ChooseVenue`,...) at
   `battle/chooseF2`/`chsVenue` time, letting the Flash call `StartGame` itself. Robust for the screen state, but
   scripts depend on cursor positions and unlocks, and the pad script must also run on the guest.
C. **Pure pad script on both PCs** (what the harness does for `fight`/`versus`). Works for One on One only,
   fragile (timing, unlock-dependent grid positions), and needs the exact roster position of every fighter.
D. Direct handler calls with a built `argv`: no advantage over strings and loses the dispatcher's own argument parsing.
   Not recommended.

---

## 6. Experiments to run (none done here)

| # | Change | Question answered |
|---|---|---|
| E0 | Log the result string (`g_eax`) after `sub_00068BA0` for `Game.GetNextFighter/GetNextVenue/GetNextMatchType/GetMatchInfo` in `funccall_hook` | Real roster/venue lists with their `STATE=` and `ID=` fields; is `ID` the same as `fighter.xml <ID>`; what FFA's match-type id is |
| E1 | `RECOMP_AUTOMATCH` env on one PC, one pad, CPU opponent: replay 5.2 from `mainMenu` | Does the match start, does the fight use the chosen fighters/venue (check `0x3B90B8[s]`, record `+0x2C`, intro `.asf` names, `[TEST]` telemetry)? |
| E2 | Same with `RECOMP_USB_PADS=2`, two humans | Pad binding and the slot/pad mapping |
| E3 | `scripts/netplay-local-test.ps1` (two windows) with automatch on both | Lockstep from auto-start, desync checksum clean over 60+ s |
| E4 | Remove optional calls one by one (MemCard, LoadCreatePlayerData, GetCustomControlMap) | Minimal sequence |
| E5 | FFA: log a real `ffa` harness run once and read `SetMatchType`, CPU `ControllerSetup` args | Confirms FFA parameters |

Add temporary reads of `GS` (`MEM32(0x2FB1A8)`) at `+0xB8,+0xBC,+0x344,+0x1AA9..+0x1ABC,+0x3B4,+0xC0..+0x1F0`
before and after `StartGame` to settle the [I] items in 2.3; `scripts/peek-guest.py` and `scripts/mem-find.py`
exist for offline checks.

---

## 7. Risks: what must be identical on both PCs

Already covered by netplay: session-derived seeds for the match generators (`sub_001BC690`, `sub_001AD220`,
`sub_000AA0D0`), per-step input, and a checksum of fighters 0 and 1 every 60 steps. Not covered, in priority order:

1. **Per-slot button remap.** `sub_001BAA00` remaps six action bits through the per-slot binding table at
   `0x3B90C8 + slot*0x18` after the netplay hook has replaced the raw pad entry. That table comes from the
   player's custom control map (`Controller.GetCustomControlMap` immediately before `StartGame`). If one
   player changed their buttons, the same raw input means a different action on each PC: certain desync. Force
   the default map (or the host's) into `0x3B90C8..` on both PCs after `StartGame`. [I]
2. **Identical setup tuple**: match type, rules, venue feID (and which `venue.xml` record a duplicated feID
   resolves to), fighter IDs per slot, which slots are human/CPU and which pad is which slot (host = slot 0 on
   both PCs). Hash it in `P_MATCHCFG` and re-check the globals (`0x3B9078..0x3B927C`, the 20-byte table at `0x2DDC08`)
   at `netplay_on_match_start`; compare across the wire. Note `fighter_hash` only covers slots 0 and 1. [V]
3. **CPU settings** if any CPU fights (FFA with 2 humans + 2 CPUs): game difficulty (`Option.SetDifficulty`, saved
   per user) feeds the AI; both PCs must use the same value, and the hash must cover all active slots. The AI seeds
   are already session-derived, but whether CPU fighters are deterministic across PCs is unproven. [I]
   Today `netplay_on_pad_read` ignores slots above 1: a third/fourth human is not supported.
4. **Create-a-player / save data.** Stock IDs 0..85 read only the databases, but any selection of an ID that pulls
   save data (create-a-player archetypes 86..141, alternate outfits or unlocked costumes) would differ per profile.
   Restrict the offered list to stock IDs and verify E1 shows no CAP data load. [I]
5. **Seed counters vs. menu history.** `netplay_on_seed` mixes in a per-call-site counter that has been counting
   since the session began, so two PCs whose menus called a seeding function a different number of times diverge
   on the first draw. Reset counters at the barrier (5.3 step 4) and/or start from a freshly booted front end on
   both. [I]
6. **Front-end residue.** The state before the replay (list objects, MemCard status, controller manager
   bindings) should be the same on both PCs; the cheapest guarantee is auto-starting only from a fresh boot to
   the main menu and for rematches using `Game.Rematch`. [I]
7. **Venue-specific nondeterminism.** `docs/netplay.md` names the crowd and spectators as a known drift source and
   `docs/releases/v0.2.1.md` records a Terror Dome fault with an unknown cause. Start with the Foundation (feID 1),
   the venue all automated tests ran. [V] for the docs, [I] for the advice.
8. **Build/data identity.** Same executable version (already compared via the session's build id) and the same
   disc dump contents (`fighter.xml`, `venue.xml`, `gconfig.xml` feed the match); add the three XML hashes to the
   handshake. [I]
9. **Logging cap.** `funccall_hook` stops printing after 4000 lines, so a long online session will have no
   `[FUNCCALL]` evidence for later matches. Print the automatch calls separately (`[AUTOMATCH]`). [V]

---

## 8. Status of every claim

| Claim | Tag |
|---|---|
| FE call sequence of 1.2, argument values and screens | V (logs) |
| `FuncCall` chain `0x68BA0 -> 0x457D0 -> 0x45510/0x455E0/0x45670`, registry layout, handler convention | V (code) |
| Handler addresses in 2.2 | V (registration calls) |
| `StartGame` requires `GS+0x344 == 1` | V |
| `ChooseVenue` argument is the feID; `StartGame` finds the venue by that value | V (arg), I (hash attribute = feID: `GetMatchInfo` restores `Game+0xCA4` from `GS+0x3B4`) |
| `ControllerSetup` arg 2 = fighter `<ID>` | V (speech files) |
| `ControllerSetup` arg 0/1/3 meanings (slot / pad / entry index) | I |
| Entry byte layout of `GS+0x1AA9` | I (byte 1 = fighter: V as read by StartGame) |
| Setup block = `GS+0xC0` and per-slot offsets inside it | I |
| Match type list indexes and FFA id | V (0), U (FFA) |
| `RECOMP_POST_CMD` cannot start a match | V |
| Replay-from-main-menu works without the Flash screens | U (E1) |
| Button-map desync risk | I |
