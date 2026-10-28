# Created fighters online, step 1: the record and an offline injection

Goal of the feature: a player fights online with the fighter they created in Story mode. Step 1
(this report) confirms which profile bytes Battle mode reads to build a created fighter, and proves
offline that a record copied into a spare profile slot is enough. The relay protocol, the UI and the
cleanup between matches are later steps (section 5).

Tags: **[V]** verified (code read and measured in a run), **[I]** inferred, **[U]** unknown.
`GS` = `MEM32(0x2FB148 + 4*0x18)` (game-flow singleton, see `online-match-setup.md`).
`P(i)` = `MEM32(GS+4) + 0x177C + i*0x5950`, profile `i` (0..15); the current profile index is the
dword right after the sixteenth, `MEM32(GS+4) + 0x5AC7C` (-1 at boot).

Sources: the lifted C, a Story run that made a fighter (`AW5J3555`, preset face, voice 3,
Kickboxing) on a throw-away data root, and the injection run in section 4. Run logs and captures
stay local.

## 1. How Battle builds a created fighter (all [V])

1. `Controller.ControllerSetup(slot,pad,99,slot)`: fighter byte 99 (0x63) at `GS+0x1AAA+5*slot`
   means "the created fighter of this slot's pad". Story mode itself sets its fighter up the same way
   (`ControllerSetup(0,0,99,0)` right after `MemCard.CreateNewUser`).
2. `MemCard.SetBMCurrentUserIndex(pad,idx)` (handler 0x528E0) writes `GS+0x1ABD[pad] = idx`
   (`sub_0001E6E0`) **and**, for 0 <= idx <= 16, the current profile index. idx = -1 (what
   `automatch.c` sends) only writes the pad byte. Note the bound 16 is one past the last profile.
3. `Game.StartGame` -> `sub_00020270`, branch at 0x2096D (battle mode, `GS+0x344 != 0`), for a slot
   whose fighter byte is 0x63: port = `GS+0x1AAB+5*slot`; profile index = `GS+0x1ABD[port]`
   (port < 0 or index < 0 gives profile 0 = a NULL pointer and the reads below crash);
   `GS+0x1E8[slot] = slot+1`; name pointer `P+0xC` into the setup; fighter id replaced by
   `P+0x143` (archetype, fighter.xml 86..141); `sub_0001EE80(slot)` computes the stats; the four
   move keys `P+0x12C..0x13B` are looked up (`sub_000BE760`, a keyed lookup [I]) into the setup's
   move table.
4. Stats, `sub_0001EE80`: for i = 0..5,
   `stat[i] = (int)(floor(P[0xFC+4i] / 5) * 1.275f + P[0x114+4i] * 0.45f + 0.5f)`
   (constants 0x289998 = 1.275, 0x289994 = 0.45, 0x286D2C = 0.5; the base is unsigned and divided
   with the 0xCCCCCCCD multiply, the bonus is signed). They land in the 7-dword block
   `GS+0x2B0+slot*0x1C`: dwords 0..4 = stat 0..4, dword 5 = 0, dword 6 = stat 5; the fight copies
   the block to `0x3B9128+slot*0x1C`.
5. Match load, `sub_0008B730` (0x8B800..0x8B951): for each slot with `GS+0x1E8[slot] != 0` the
   holder is `MEM32(GS+0x1A9C) + port*0x1C` (`sub_0001E410`: **per pad port**, not per slot). If the
   holder has no fighter object, `sub_00062A00` allocates "CP-RecipeData", "CP-OptionData" and
   "CP-Fighter" (0xAD20). If `holder+1` (built) is 0, `sub_00064170(P)` builds the look:
   `sub_00063780` copies 0x98 bytes from `MEM32(P+0x5910)` and 0x26 bytes from `MEM32(P+0x5914)`,
   turns them into parts (`sub_0009B220`, CWPCreatePlayer::SetPart) and `sub_0009AB90` builds;
   then `holder+1 = 1`. A holder already built is not rebuilt in that load. Measured: `holder+1`
   stays 1 through the summary and back on `battle/cmtype`, and is 0 again by the time the
   automatch sequence reaches ControllerSetup (one of ResetMatchData / ControllerBind /
   SetBMCurrentUserIndex / ControllerSetup(-1..) clears it; which one [U]), so the next match
   rebuilds by itself.
6. `P+0x5910/0x5914/0x5918/0x591C/0x5920` are pointers into the profile itself (set once at start-up
   by `sub_000145A0`: `+0x5564`, `+0x55FC`, `+0x5624`, `+0x5698`, `+0x56B8`). Every profile slot has
   its own, so copying a record into slot 15 must not touch them. Measured: slot 0's point at
   `P(0)+0x5564` / `+0x55FC`, slot 15's at its own.
7. The blocks at `+0x5624`, `+0x5698`, `+0x56B8` are only reached through `sub_00013880`,
   `sub_000138D0`, `sub_0004A070/0x4A0B0/0x4A870` (CreatePlayer screens), `sub_000606E0` and
   `sub_000C0BF0`, none of them on the StartGame / match-load path [I: static callers; nothing in
   the injection run needed them].

## 2. The record (what a client has to send)

Offsets from the profile start. Values from `AW5J3555` as measured.

| Offset | Size | Field | Example | Tag |
|---|---|---|---|---|
| `+0x00C` | name, NUL-terminated | user ID; the entry screen takes at most **8** characters (it stopped at 8 when more were typed); bytes up to `+0x40` were zero | `AW5J3555` | V |
| `+0x0FC` | 6 x u32 | base stats | 0,5,5,0,0,6 | V |
| `+0x114` | 6 x s32 | bonus stats | 0,2,1,0,0,0 | V |
| `+0x12C` | 4 x u32 | move keys (what they key [I]: entries of the move table) | 0x06000033 x4 (fresh fighter) | V |
| `+0x13C` | u32 | voice (1..6, `CreatePlayer.SetVoice`) | 3 | V |
| `+0x140` | 3 bytes | style bytes (first = chosen style 1, others 0xFF until learned) | 01 FF FF | V |
| `+0x143` | u8 | archetype = fighter.xml id the fight uses (Kickboxing gave 98) | 98 | V |
| `+0x4D8A` | u8 | created-fighter flag (set when the style is chosen; 0 before) | 1 | V |
| `+0x5564` | 38 x u32 | recipe: 12 body/face values, then parts `(table<<24)|(colour<<16)|row`, -1 = default | see below | V |
| `+0x55FC` | 38 bytes | options | `00 x12, 01, 00 x16, FF x9` | V |

Recipe of `AW5J3555`: `2 6 3 0x4A 3 4 4 8 1 4 2 3`, `0x10020099 0x1000000B 0x10000023`, five -1,
`0x0E00001A`, seventeen -1.

The subset is sufficient [V]: the injection below wrote only these fields into an empty profile
slot and the fighter appeared with the right name, look, archetype and stats. That the rest of the
0x5950 bytes (Story progress, money, the bit field at `+0x4DC0`, shop state) is never read by a
Battle match is [I] (static reading plus the run); sending it would also leak Story state for
nothing. Wire size of the subset: 0x20 (name, padded) + 0x48 (`+0xFC..+0x143`) + 1 + 0x98 + 0x26 =
**0x127 bytes** (295), before framing.

The profiles are not loaded at boot: all 16 slots are empty until a user ID is chosen or created
(measured on a save area that had `AW5J3555` on disk). The save file is `UDATA/45410049/<id>/<name>`,
0x5990 bytes = a 0x80-byte header and `P+0 .. P+0x5910` (the name sits at file offset 0x8C).

## 3. The test hook

`src/hooks/created_fighter.c`, off unless `RECOMP_TEST_CP` is set; called from the automatch
sequence (`automatch.c`) for each slot after the pads are bound, and before every front-end call.

```
RECOMP_TEST_CP="slot=1,from=0,to=15[,file=<0x5950-byte profile dump>][,mod=<n>:<i>:<hex>][,nounlink=1]"
```

For `slot`: write the record subset (from profile `from`, or from `file` as a received record would
arrive) into profile `to`, `GS+0x1ABD[pad] = to` (directly, **not** SetBMCurrentUserIndex, which
would also move the current profile index), clear `holder+1`, and set the slot up with fighter 99.
`mod` changes one recipe dword from the n-th match on. The slot needs a pad: the profile and the
holder are found through the port, so a created fighter cannot sit in a CPU slot (port -1 would
index `GS+0x1ABC` and `holder - 0x1C`). Before `Game.UpdateMatchRewardsPoints`, any `MemCard.*`,
`Game.Quit` or `Game.StartBattleMode` it puts `GS+0x1ABD[pad]` back to -1 (section 4.3).

## 4. The offline experiment (one PC, automatch, no relay)

Setup: build of `remaster` e9828a3 + the hook; own data root; Story run to make `AW5J3555`
(new user ID, preset face 2, voice 3 "clean cut", Kickboxing), quit the tutorial fight, dump the
profile from memory (`profile0-full.bin`, local only). Injection runs: fresh boot, `RECOMP_AUTOMATCH=
type=0,venue=1,f0=55,p0=0,f1=22,p1=1`, `RECOMP_TEST_CP=slot=1,from=0,to=15,file=...,mod=2:12:10000099`,
`RECOMP_TEST_KO_STEP=1500,0`; P1 = fighter 55 (Doc) on pad 0, P2 = the injected fighter on pad 1.

### 4.1 Look, name, stats [V]

- Match 1 (both runs): P2 is `AW5J3555` in the HUD and on the summary, the preset face with the
  black hoodie of the recipe; fight record character 97, health 176 (Doc 304).
- Effective stats, `GS+0x2B0+0x1C` and the fight copy `0x3B9128+0x1C`: `0,2,2,0,0,(0),1`;
  formula on base `0,5,5,0,0,6` / bonus `0,2,1,0,0,0`: `0,2,2,0,0,1` -> equal.
- The match plays to the end (knockout at step 1500, summary, reward step, back to
  `battle/cmtype`), no crash in either run.

### 4.2 Rebuild with a different recipe [V], and a determinism warning [V/I]

- Match 2 with recipe dword 12 changed from `0x10020099` to `0x10000099` (same part, colour 2 -> 0):
  the look was rebuilt without help. `holder+1` was 1 on `battle/cmtype` and already 0 when the
  hook ran inside the next sequence, so the game itself clears it during the setup calls; clearing
  it in the hook is belt and braces.
- **The same changed recipe gave two different looks in two runs**: a white hoodie in run 2, a
  white tank top in run 3. The original recipe gave the same black hoodie in both runs. So a part
  value the item database does not hold for that colour is resolved to something run-dependent [I:
  colour 0 of that row probably does not exist]. Online this would put two different looks on the
  two PCs (cosmetic, but visible), so step 2 must validate every recipe dword against the item
  database (`djv2.sod`) and replace unknown values with -1 (default) before injecting.
- Recipe dword 12 is the top (hoodie) [V by the change].

### 4.3 The save area: a real problem, found and fixed [V]

- Run 2 (no unlink): after each match the summary's reward step (`Game.UpdateMatchRewardsPoints`,
  then `MemCard.Save(battle/rewardun.swf)`) **wrote profile 15 to disk** as the user ID
  `AW5J3555` (`UDATA/45410049/0C6F8520AAF5/AW5J3555` rewritten twice, now holding the created
  fighter; the reward step had also changed the dword at `+0x110`, the sixth base stat, from 6 to
  50 [V]; why [U], but a linked copy would drift from the sender's record match by match). The save directory is derived from the name, so online a
  remote player's record would create a user ID named after them on the receiving PC, or overwrite a
  local user of the same name.
- Run 3 (hook unlinks the pad before the reward step): the same file is byte-identical before and
  after; only `Options` changes (it changes in stock matches too [I]) and the partition images'
  times. Measured once to the end (match 1); match 2 of run 3 stopped on the summary with the known
  "summary takes no pad" race (fixed on the build-32 hotfix branch, not in this base), so it never
  reached the reward step.
- The summary still shows the name (it reads the setup's name pointer); the reward step finds no
  profile for the pad, as in every automatch today.

### 4.4 What was not measured [U]

- Determinism of the build beyond appearance: the build path reads the global generator 0x33DB5C
  only for jewellery sparkle (study, [I]); not exercised (the fighter had no jewellery). The 87
  indirect calls on the build path were not followed.
- Two copies (lockstep) with a created fighter on one side: step 2.
- Created fighters on both pads at once (two holders, two spare slots): expected to work the same
  per port [I].
- Rematch: `Game.Rematch` keeps the setup; whether `holder+1` is cleared there [U] (same fighters, so
  harmless).

## 5. Plan for steps 2-4

**Step 2, protocol and validation (about 1.5 days).** Room/quick join carries an optional
"created fighter" record (0x127 bytes + version + CRC) from the player's chosen user ID; the relay
stores it with the player and sends it to the room with the match start (same message that carries
fighter ids), size-capped, no interpretation. Each client: validate (name 1..8 printable chars;
archetype in fighter.xml's created range 86..141; style bytes known or 0xFF; voice 1..6; flag 1;
recipe dwords present in `djv2.sod` for that table/row/colour, else -1; options bytes in range),
**clamp stats** (base stats to the Story maximum, bonus to its range [U: maximum values to read
from the Story stat screens / gym code]), then inject into slot 15 (14, 13, 12 for further remote
players) before ControllerSetup with fighter 99, and unlink before the reward step as the hook
does. The record and the slot assignment must be identical on all PCs (lockstep): the stats feed
the simulation, so every PC uses the clamped record, the sender's own PC included. The local
player's own created fighter goes through the same injection path (not their live profile), so
the reward step never writes their Story save either.

**Step 3, UI (about 1 day).** In the room screens of our glass look: "Fighter: Created
(AW5J3555)" as a choice beside the roster when the signed-in player has a user ID with a created
fighter (read from the save, not from the game's profile slots, which are empty until chosen);
show the remote created fighter's name and style on the versus screen (no portrait: the summary
also shows none). Picking the user ID needs a small list of the save's user IDs.

**Step 4, cleanup between matches (half a day).** Unlink before rewards (done in the hook);
clear the spare slots' flag/name after the room ends; clear `holder+1` before each injection;
make sure a CPU fill never gets fighter 99; test two copies with 5 matches in a row, a rematch,
and a mix of created and stock fighters, with the save area compared before and after.

