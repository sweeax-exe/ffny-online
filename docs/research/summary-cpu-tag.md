# The "CPU" tag on the match summary after online matches (2026-10-24)

Players' F9 reports (builds 23, two of them) showed the MATCH SUMMARY after an online match with a
"CPU" tag over both fighters, although both were people. Read from the lifted code; V = verified by
reading, I = inferred.

## The handler

* `Game.GetMatchSummary(n)` is registered twice (strings at `0x292C04` and `0x292C34`, both with handler
  `0x466C0`; `sub_00045610` stores `{djb2(name), handler}`). [V]
* `sub_000466C0` gets the game-setup object GS (component 0x18 of the singleton at `0x2FB148`) and calls
  `sub_00065540(GS+0x3BC, idx)`, which builds `win=..&isAI=..&fighter=..&fid=..&cid=..&total=..`: [V]
  * `isAI` = `MEM32(GS + 0x178 + 4*slot)`;
  * `fid` = byte `GS+0x1AAA+5*slot` (`sub_0001E820`);
  * `cid` = signed byte `GS+0x1AAD+5*slot` (`sub_0001E7F0`), only in battle mode (`GS+0x344 == 1`).
* The screen is `screens/battle/matchSm2.big` (Apt; constants `bAI`, `iConID`, ...). Which of the two
  values draws the tag was not decoded. [I: `iConID < 0`]

## The fields

* `isAI`: written only by StartGame (`sub_00046E40` -> `sub_00020270`) from the slot's setup entry byte 0
  (`0xFF` = CPU, set by `ControllerSetup` with a pad below 0). Online it is 0 for both players: the test
  telemetry's `"cpu":0` reads the same flag (fighter record `+0x937` bit 0, a copy of it). [V]
* `cid` (setup entry byte 4): written only by `Controller.ControllerReturn(n)` (`sub_000557C0`, the n-th
  joined pad's number) and `ControllerSelect`; `ResetMatchData` leaves `0x9D` (-99). [V]
* The game's own versus and against-CPU flows send `ControllerSetup(-1,0,0,0)`, then
  `ControllerReturn(0)`, `ControllerReturn(1)`. Our automatch sequence did not, so `cid` stayed -99 for
  both slots: the only per-slot value that differs from an offline two-pad match. [V]

## The change

`src/hooks/automatch.c` `run_sequence()` sends `Controller.ControllerReturn(i)` for each slot right after
`ControllerSetup(-1,0,0,0)`, as the game's screens do. `ControllerReturn` writes entry byte 4 and a debug
string into a scratch buffer; byte 4 is read only by the summary and `ControllerSelect`, not by StartGame
or the fight, and every PC sends the same call at the same point, so the lockstep is not affected. [V]

`RECOMP_FE_RESULTS=1` prints the summary string (`[FE-RESULT] Game.GetMatchSummary(n) = ...`) to check
it: `cid` should read 0 and 1, not -99.

Do not write `isAI` during a fight: `sub_00035AE0` (an input-event handler) reads it live.

Fallbacks if the tag stays: rewrite the result string in place for online matches (`cid=<slot>`), or
patch the Apt script (`scripts/apt-patch.py` does the main menu).
