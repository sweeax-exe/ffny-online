# Match-end state in Def Jam: Fight for NY (read-only RE notes)

Source: the lifted C in `defjam-recomp/src/recomp/gen/recomp_0012.c` (plus `recomp_0004.c` for the setup copy).
All addresses are guest addresses. Nothing here was run; every claim is from reading code.
"Verified" = read directly in the lifted code. "Inferred" = my interpretation of what was read.
Fighter record = `0x003B92E0 + slot*0x12E8`. Fighter flag words quoted below are record offsets.

## Short answer

The whole match-end logic lives in a small cluster around `sub_001A4900` (per-frame game update, argument is the frame delta).
Poll these, in order of usefulness:

| What | Address | Notes |
|---|---|---|
| Match-over (sticky) | u32 `0x3B9054`, bit `0x08` | set by 5 writers, never cleared until next match init |
| Winner slot | u32 `0x3B8F40` (twin `0x3B8F3C`) | slot index 0..3 of the first surviving fighter, only written on a decisive result |
| Result record | history ring at `0x3BE130`, stride `0x10`, count u32 `0x3BE624` | last record = `0x3BE130 + (count-1)*0x10`; u16 at +0xE is the result code, u16 at +0xC participant bits |
| Elapsed clock | u32 `0x3B9030` (minutes), `0x3B9034` (seconds) | counts up; float tick source `0x3B903C` |
| Phase | u32 `0x3B8F30` | 0 start, 1 fighting, 2 result decided, 3 finished (see caveat) |

## Q1. Where does the game decide the match is over?

Verified.
- `sub_001A4900` is the per-frame update. It runs a 4-state phase machine on `0x3B8F30`:
  phase 0 calls the intro routine `sub_001AACA0`, clears flag bit 1 and sets bit 2 of `0x3B9054`, then moves to phase 1.
  Phase 1 first calls `sub_001A7530` (time limit check); if it returns 1 the phase advances. Otherwise it calls `sub_001A7140`
  (win-condition check); a nonzero return advances the phase. Phase 2 copies `0x3B8F3C` into `0x3B8F40` and sets phase 3.
  Phase 3 and above do nothing. The machine is skipped while the pause bits (`0x20`/`0x40`, without `0x80`) of `0x3B9054` are set.
- `sub_001A7140` is the win-condition check. Two branches, chosen by the match-type word at `0x3B9084`:
  - when it equals 4: it scans the four fighters (slot != -1, `+0x932` bit `0x200` not set). Fighters whose `+0x932` bit `0x08` is set are
    cleared of that bit and put in list A ("eliminated"); the rest go in list B ("still in"). All eliminated: draw result.
    Exactly one still in (or, with rule bit 2 of byte `0x3B907E` set, all survivors on one side, judged by `+0x937` bit 4): that fighter's slot is
    written to `0x3B8F3C` and `0x3B8F40`, result recorded, `0x3B9054 |= 8`, function returns 1. Otherwise a non-final "elimination" result is
    recorded and it returns 0.
  - otherwise: a second branch scans fighters for defeat flags in `+0x932` (table below). For a flagged fighter it asks `sub_001A6750`
    whether the result is decisive; if so it logs the string "GameMain.game_end_winner_wrestler=P%d" (P number = slot+1), writes the slot to
    `0x3B8F3C` and `0x3B8F40`, records the result and returns 1.
- `sub_001A6A80(winners_list, losers_list, code)` is the result recorder (lists are slot indices terminated by -1). It appends a history record,
  sets fighter bits (`+0x933` bit 0 for the first list, bit 1 for the second), and raises one-frame event bits in `0x3B9054`. See Q2.
- `sub_001A7530` is the time limit: adds `frame_delta * 1.2` ticks to the clocks, splits to minutes/seconds, and compares with a table at `0x2EC0C8`
  indexed by the selector at `0x3B9080` (bytes: minutes 60,5,10,15,30,60; seconds all 0; selector 0 = no limit). On equality it sets bit 2 of
  byte `0x3B9050`, calls `sub_001A6E40(0x40)` and returns 1.
- `sub_001A6E40(0x40)` is the time-up judge: sums per-fighter scores (int at record `+0x480`), picks the leader (per side in team mode) and
  calls the recorder with code `0x41`, or `0x45` for a tie.
- Other writers of the match-over bit: `sub_001D1140` ("fighter killed by environment/hazard": zeroes health, notification code 5, sets a defeat
  flag in `+0x932` by match type, calls `sub_001A7140`, then sets the bit if the new record's low bit is set); `sub_001DA400` (match type 0x11
  with a train/environment flag: sets `+0x932` bit 0x20, calls `sub_001A7140`, sets the bit unconditionally); `sub_001CB130` (the idle-type
  state handler, table index 1: once the end-of-match slow-motion is on and the winner is idle, sets the bit); `sub_001A4900` itself (end-of-match
  slow-motion, bit `0x04`, accumulates `0x3B9064`, bit set when it reaches 300.0); `sub_001A46F0` (bare "set the bit" thunk, only reached
  indirectly from a flag-clearing helper `sub_000AAAE0`, probably a debug path).

The front end does NOT carry the result: `Game.GetMatchInfo` (handler `sub_00046D20`) and `GetMatchSummary` (`sub_000466C0`) only format setup data
(hero side, match type, fighter ids). I did not find the code that feeds the `battle/reward` screens; next read would be callers that read `0x3B8F40`
or the `0x3BE130` ring after the match (only `sub_001A3D50`, a state snapshot, reads `0x3B8F40` directly).

Confidence: high that `sub_001A7140` + `sub_001A6A80` decide and record the end; high for the phase machine; medium for "bit 0x08 means match over".

## Q2. Globals

Verified unless marked.
- `0x3B9054` (u32 game flags, zeroed to 1 at match init by `sub_001A4D90`). `sub_001A4900` clears
  bits `0x80` and `0x200..0x10000` every frame and keeps the rest, so bits `0x01..0x40` and `0x100` persist. Bit 1 = pre-fight, 2 = fight running (set on first update), `0x04` = end-of-match
  slow-motion active (set by the recorder, also needs `sub_000442E0(6, 60.0)` side effect), `0x08` = match over, `0x20`/`0x40` pause-like, `0x80` per-frame.
  One-frame event bits set by the recorder: `0x400` (a result was recorded), `0x800` (code bit 0x08), `0x1000/0x2000/0x4000/0x8000/0x10000/0x20000`
  (other code bits; exact mapping not worked out). `sub_001D87B0` sets `0x200`.
- `0x3B8F30` phase, see Q1. Reset to 0 by `sub_001A4D90`. Inferred caveat: when `sub_001D1140`/`sub_001DA400` consume the event by calling
  `sub_001A7140` directly, the phase machine's own later call finds nothing and may never advance to 2/3. Use bit `0x08`, not the phase, as the primary signal.
- `0x3B8F3C` / `0x3B8F40` winner slot. Both are zeroed at init, so slot 0 is the default: only trust them when the match-over bit is set AND the last record's code has bit 0 set AND is not a draw (bit 2 clear).
- History record layout (16 bytes, base `0x3BE130`, count at `0x3BE624`, cleared at init). +0 and +4: minutes and seconds from `0x3B9030/34` at the
  moment of recording (verified). +8: character-id style value of the first fighter in the second list, or -1 (inferred meaning). +0xC (u16): participant
  bits, first list gets 1,2,4,8 by position, second list 0x10,0x20,0x40,0x80 (verified for bit values). +0xE (u16): result code.
- Result code (u16, inferred bit meanings from the callers):
  `0x0001` decisive (match over), `0x0004` draw/tie (seen as `0x105`, `0x45`), `0x0040` time up (`0x41`, `0x45`), `0x0100` an elimination in the
  type-4 branch (`0x100` non-final, `0x101` final with a winner, `0x105` everybody out), `0x1000/0x2000/0x4000` added to `0x105` when 2/3/4
  fighters are involved. Defeat-flag branch maps `+0x932` bits to reasons: bit 1 -> `0x21`, bit 2 -> `0x09`, bit 4 -> `0x81`, bit `0x10` -> `0x101`,
  bit `0x20` -> `0x201`, bit `0x40` -> `0x401`, bit `0x80` -> `0x801` (result is that value, low bit = decisive).
- End reason: the game distinguishes time up (`0x40`), defeat-flag reasons (above) and elimination/last-standing (`0x100`). Which flag bit is
  "knockout" versus "ring out" versus "submission" I could not name from code alone; see Q3 for the two best candidates.
- Match type: u32 `0x3B9084`, copied from the front-end setup struct (+0xA0) by `sub_000871F0`, which also copies rules to `0x3B9078..0x3B907E`
  (`0x3B907E` bit 2 = team rule used above; bit 0 makes `sub_001A6750` always say decisive; bit 6 of `0x3B907C` changes the time-up tie handling)
  and the time selector `0x3B9080`. Values seen in end logic: 4 (elimination branch), 7, 0x11. A name table at `0x2DBCC8` lists 13 match types
  (Single, Tag, 3Way, 4Way, 1vs2, 1vs3, Demolition, Inferno, Subway, Window, Cage, Ringout, Tutorial), but I did not prove that `0x3B9084` indexes it.
- Rounds: no round counter found; it looks like single-round matches. Timer is elapsed-time, see table above.

## Q3. Per-fighter knocked-out / danger indicators

Verified writers and readers; meanings partly inferred.
- `+0x925` bit `0x08`: set by `sub_001A50B0` (health update) when health after the delta is below 0; health is then clamped to exactly 0.
  Bit `0x10`: health / max (`+0x35C`) below 0.1 (constant `0x2972C4`). So a "danger" indicator is health ratio < 0.1, read from `+0x350`/`+0x354`
  or `+0x35C`; I found no separate "danger state" handler index.
- `+0x380`: announcer warning stage (0..2) set in `sub_001A5DD0` when health ratio is below 0.1 (stage 2) or below 0.25 (stage 1); via `sub_00044960(slot, stage)`.
- Lethal-hit gate (inferred from `sub_001A5DD0` tail and `sub_001B6670`): after a hit, if `+0x925` bit 8 (health overshot) and `+0x926` bit `0x1000`
  (attack carries a finishing flag) are both set, `+0x926` bit `0x800` is set. The state handler `sub_001B6670` (table index 113 by my count of the
  stack table in `sub_001B54D0`, index = (slot offset-4)/4) then takes its fatal branch: sets `+0x932` bit `0x04`, forces health to 0, plays a fixed
  down animation and goes to sub-state 3; the non-fatal branch goes to state 0x1A. This matches "zero health alone is not a KO". Best candidate for
  the knockout flag: `+0x932` bit `0x04` (maps to result code `0x81`). Medium-low confidence.
- `+0x932` defeat/result flag word (16-bit), writers found: bit 1 (`sub_001B39F0` idx 24, `sub_001C3890`/`sub_001D3100`/`sub_001D31D0` idx 47),
  bit 4 (`sub_001B6670`, also `sub_001D1140` in match type 7), bit `0x10` (`sub_001D1140`, match type 4), bit `0x20` (`sub_001D1140` type 0x11, `sub_001DA400`),
  bit `0x80` (`sub_001D7BD0`, idx 19), bit `0x400` (acknowledge flag set by idle handler and others). Bit `0x08` and `0x200` have no explicit
  writer that I found (`0x200` is set in `sub_001A7140`; `0x08` is read as "eliminated" by the type-4 branch only). Bits 2 and 0x40 have no writer found either.
- `+0x933` bit 0/1 (word bits `0x100`/`0x200`) mark result participants; `+0x937` bit 2 (value 4) is the side marker used for team results.
- `+0x91E`/`+0x936`: only seen as unrelated animation/state flags in these routines; not part of the end logic.
- Not found: a fixed `+0x30` value for "knocked out". I did not read the grapple states (0x55 is entered from grab code, so it is not KO). Next read:
  entry points of handlers 113 (`sub_001B6670`), 24 (`sub_001B39F0`), 19 (`sub_001D7BD0`), 47 (`sub_001D31D0`); and who calls `SetState (sub_001B3CA0)` with 113.

## Q4. A once-per-match hook

Best single places, none perfect:
1. `sub_001A6A80` return when the code argument has bit 0 set: all decisive results (KO flags, elimination, time up) pass through it with bit 0 set exactly once.
   Args (cdecl, stack): arg1 = winners list pointer, arg2 = losers list pointer (each up to 4 slot indices, terminated by `0xFFFFFFFF`), arg3 = u16 code.
   Draw results pass an empty first list. It is also called for non-decisive events (codes without bit 0), so filter on bit 0.
2. `sub_001A7140` return value 1 (called per frame in phase 1, and directly by `sub_001D1140`/`sub_001DA400`). Winner is then in `0x3B8F3C`.
3. Poll `0x3B9054 & 8` and read the last ring record. This is the least invasive and probably the most robust, since the two direct-callers above set the bit
   themselves.
Start-of-match hook: `sub_001A4D90` (called once from the setup copy `sub_000871F0`); it zeroes `0x3B8F28`.. (0x53 dwords), the ring, the clocks and the 4 records.

## What would confirm this at run time

- Watch `0x3B9054`: expect 1 at start, 2 after the first frame, `0x04` then `0x08` (~300 ticks later) after a KO; only `0x08` after a time-up or hazard end.
- Count entries to `sub_001A6A80` and log arg3: expect exactly one with bit 0 set per match; compare winner list[0] with `0x3B8F40`.
- Log `0x3BE624` and the `+0xE` code of the last record after a played KO, a time-up (set `0x3B9080` nonzero) and a hazard KO. That tells which `+0x932` bit
  is the KO (candidates `0x04`/`0x81` and `0x01`/`0x21`).
- Watch `+0x925` of the loser when health hits 0, and `+0x932` when the finishing move lands.

## Addendum 2026-10-25: the knockout path, read and confirmed

Read in the disassembly and confirmed by the test knockout (`RECOMP_TEST_KO_STEP`, `src/hooks/test_telemetry.c`)
in offline and online fights; this settles the Q3 candidates.

- The played knockout is result code `0x21` (decisive + defeat flag `+0x932` bit 1), with the loser in **state 0x18**
  (handler `sub_001B39F0`, slot 24 of the table built in `sub_001B54D0`, index = (stack offset - 4) / 4 after its
  `push esi`). Seen in every logged online knockout, and the loser stays in 0x18 through the result.
- The decision is `sub_001C5FC0(attacker, defender, kind)`, called from the hit and grapple paths (`sub_001C3890`,
  `sub_001C6CD0`, `sub_001D3100`, `sub_001D31D0`, `sub_001D5870` area): it needs the attacker's current move to carry the
  finishing flag (`attacker+0xF68`, set by `sub_001B4930` from bit 0x8000 of the move's table entry), the defender under
  the knockout threshold (`+0x927` bit 0x10, set by `sub_001A5DD0` while the health ratio `+0x35C` is below 0.1) and a
  chance roll (`sub_001A5C30` mode 1, through `sub_001BC730`). It then writes the fall's variant bits to
  `defender+0x943` (1, plus 2/4/8 by the defender's state and side, 0x10 for a special case that skips the state) and,
  for `kind != 0`, puts the defender in state 0x18 through `sub_001B5420` (set-state and restart).
- `sub_001B39F0` sub-state 0 picks the fall animation from `+0x943`, plays it and sets `+0x932` bit 1 (unless `+0x943`
  bit 0x10). The phase machine's win check (`sub_001A7140`) then records the result on the next step.
- So zero health alone is not a knockout because of the finishing flag and the chance roll; health under 10 % only
  makes the fighter knockable. The test knockout does the game's last step itself: health and ratio 0, `+0x943 = 1`,
  `sub_001B5420(loser, 0x18)` at the start of a step; the game takes it from there (result 0x21, slow motion, summary).
- Pad bits seen in a step-input probe (One on One, slot 0): `0x80` strike (state 0x45), `0x40` strike (0x49), `0x20`
  grab (0x1E, then 0x34 holding and 0x35 held), `0x2000` held block (state 2); `0x1000` alone changed no state (read as the modifier, unconfirmed); state 1 is
  standing and walking (about 0.4 world units a step). Not settled: what `0x10` does.
