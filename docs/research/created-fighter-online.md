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
RECOMP_TEST_CP="slot=1,from=0[,file=<profile dump or user ID save>][,mod=<n>:<i>:<hex>]"
```

For `slot`: make a record from profile `from` (or from `file`, as a received record would arrive),
check it as every PC does, and inject it as in section 5 (spare profile slot, `GS+0x1ABD[pad]`
written directly, **not** with SetBMCurrentUserIndex, which would also move the current profile
index; `holder+1` cleared; fighter 99). `mod` changes one recipe dword from the n-th match on. The
slot needs a pad: the profile and the holder are found through the port, so a created fighter
cannot sit in a CPU slot (port -1 would index `GS+0x1ABC` and `holder - 0x1C`). The step-1 version
of this hook also took `to=`, `keep=` and `nounlink=`; section 4 used those.

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

## 5. Steps 2-4 as built (build 37 candidate)

**Protocol** (`docs/netplay-protocol.md`, "Created fighters"; `netplay/relay/protocol_created.go`,
`created.go`): the 300-byte record (version, the 0x127-byte payload of section 2, CRC-32) goes up
with `FIGHTER` (0x26), comes back as `RECORD` slot 0xFE (the relay's copy), and reaches the other
room members as `RECORD` (0x27) on upload, on joining and right before `START`; `RECGET` (0x28)
asks again. `START` gets a trailer naming, per created fighter, the slot and the CRC of the
record, so every PC injects the same bytes. `READY` 99 plays the record only with the switch on
and a kept record (otherwise Doc, as for any fighter without a database record); King of the Hill
and ranked stay roster-only.

**Server switch**: `CAPS` bit 2. The relay answers it while the file `created-fighters-allowed`
exists in its updates folder (read at every request; `-created` forces it on for tests), like
`rollback-allowed`. Off: no upload is kept, 99 plays Doc, the clients offer no created fighter.

**Validation and clamp** (`cp_record_sanitize`, `src/hooks/created_fighter.c`, identical on every
PC, the sender included): sealed (version, CRC); name 1..8 characters, non-printable ones become
`?`; archetype 86..141, created flag 1, first style 0..4 (else the record is refused: Doc); other
styles 0..4 or 0xFF; voice 1..6 (else 1); **base stats at most 50** [V: the gym's cost table at
0x2DBB70 has ten tiers of five, `sub_0004EB60`], **bonus stats 0..38** [I: with base 50 that is
the Battle attribute screen's 100 %, which shows `stat * 3.333`, `sub_0004A3D0`]; move keys and
recipe parts must be keys the game's database has (`(table<<24)|(colour<<16)|row`: a table of
djv2.sod, a row below its record count, colour below 16 [I]), else the fresh-fighter move
0x06000033 / the default part (-1); the twelve body/face values at most 0xFF. The relay checks only
the shape (size, version, CRC, name, archetype, first style, flag).

**The player's own record** (`cp_local_record`): the newest user ID save in
`<data>/save/UserData/45410049/*/` (0x5990 bytes: a 0x80-byte header, then the profile) whose
created flag is set; read at every hub session, so a fighter made meanwhile shows up.
`RECOMP_CP_RECORD=<profile dump or save>` replaces it for tests.

**Injection** (`created_fighter_inject`, from the automatch sequence for each slot whose START
fighter is 99): the record into the highest profile slot without a loaded user ID, the pad linked
to it, the pad's holder marked unbuilt, ControllerSetup with 99. The pads are unlinked before
`Game.UpdateMatchRewardsPoints`, `MemCard.Save/QuerySave`, `Game.Quit`, `Game.StartBattleMode`
(the reward save of section 4.3) and linked again on `Game.Rematch()`. A CPU fill never plays 99
(it draws from the roster, which has no 99).

**UI** (`online_screen.cpp`): the fighter grid shows "your fighter" first (a silhouette card with
an orange YOURS ribbon; the large card names the fighter and its styles from the archetype's
fighter.xml letters: B streetfighting, K kickboxing, S submissions, W wrestling, M martial arts
[I]) while the relay keeps the record; room cards show a created fighter's name and styles; gallery
pages `grid-created` and `room-created`. `netplay_slot_created(slot, ...)` gives the versus
screen the same name and styles.

**Two more things the tests found** (both fixed):

- *A crash after the knockout with two created fighters* (`sub_000BE3D0` from `sub_00014C20`, via
  `sub_00064ED0` / `sub_00065FD0` / `sub_0001E1B0` / `sub_00079190`, the screen loader's worker):
  the game's post-fight reward pass walks every slot's profile (Story rewards; it is also what
  raised the sixth base stat from 6 to 50 in section 4.3) and does database lookups with the
  profile's other fields. So the pads are unlinked once the match is finished (phase word
  `0x3B8F30` = 3, checked each step by `created_fighter_on_step`; simulated state, so every PC
  unlinks at the same step), and before the reward step's front-end calls. `Game.Rematch()` links
  them again; the rematch's fight and summary ran clean (offline check, below). A first version
  unlinked as soon as the looks were built; that did not change the desync below.
- *Rollback*: a rollback at step 4 of a created-fighter match crashed in `sub_0008BE60` (the
  runtime-built looks are outside what a rollback restores). A match whose START names a record now
  plays in lockstep on every PC (`match_has_created`, decided from the START, so the input delay and
  the decision agree); the log says "rollback off for this match: a created fighter plays".
  Rollback for created fighters is future work.

**Tests** (first on build 35 base c470302, then rebased onto e1866fe; `tools/defjam-mcp/dj.py scenario online` with
`--created --cp-records`; two copies, local relay, knockout at step 600; the save area of each
copy's root hashed before and after, cache partitions left out):

| Run | Matches | Result |
|---|---|---|
| created (AW5J3555) vs stock (Doc), lockstep | 2 | all played to the summary, checksums equal (10/20 compared), the remote PC shows the same look |
| created vs created (AW5J3555 vs CPTWO: other name, base stat, jacket colour) | 5 | 5/5, checksums equal, both PCs render the same two looks; (before the early unlink: a crash after the first knockout) |
| created vs created, both copies with rollback on | 3 | 3/3 in lockstep (created-fighter rule), checksums equal; see the open desync below |
| stock vs stock, rollback on (baseline) | 2 | match 1 fine; match 2 never started (room flow; not investigated here) |
| stock vs stock, lockstep (regression) | 3 | 3/3, checksums equal |
| offline Rematch (RECOMP_TEST_CP, one PC) | 2 | Rematch relinks, the created fighter fights the rematch, both summaries reached, no crash |

Save area in every run: only `UserData/45410049/1A3F1B2E4ADD/Options` changed on each copy, as in
the stock baseline (the reward step saves the options); no user ID save was written or created.
The scripted online rematch of `dj.py --rematch` did not reach the Rematch choice (the fight's
button mashing chose Quit), so the online rematch is covered by the offline check only.

**Open: a lockstep desync that only created fighters cause (build 36 base e1866fe).** Over about
35 two-copy matches with a created fighter on at least one side, 8 desynced (several runs of 3
matches each; a desync ends the run): created vs created and created vs stock, at drawn venues and
at Crow's Office (26), first seen at checks from step 60 to step 600. Stock vs stock under the same
harness: 0 desyncs in 26 matches (21 of them at venue 26). Every report: the two PCs' fighter
positions (`+0x44`, `+0x4C`, `+0x54` of the fight record) differ by 1 to 150 float ulps, both
fighters, with the generators, the crowd and the time scale equal. Ruled out: the pads' link to
the profiles during the fight (unlinked early or kept to the end: same rate), audio timing (both
copies muted: 3 of 4 runs desynced), rollback (these matches play in lockstep), the HD pack (with
and without). Not yet looked at: something in the created fighter's runtime-built model or its
per-frame update that the simulation reads (collision or hit volumes from the composed skeleton),
an x87 control-word change on the game thread by the created-player code (the lifter's
`g_fp_control_word` is per thread, but a render-frame path that sets it between steps would make
the step's rounding depend on frame timing). Until this is fixed the server switch stays off.

**KOTH note** (not about created fighters): `dj.py scenario online --mode koth --players 3
--matches 4` stops on this base and on e1866fe alone (checked by building e1866fe without this
branch): the king's next fight never starts on its PC ("Match found ... starts by itself" on
`battle/cmtype`), the third copy gives up at step 3. Also, a stock match with rollback on at venue
26 crashed (`RIP 0x1408203D2`) once; rollback is off by default.

Screenshots (local only): `logs/dj/20261028-143000-online/fight-m1-copy1.png` (AW5J3555 on the
other PC), `logs/dj/20261028-143601-online/fight-m1-copy0/1.png` (created vs created, same on both),
the gallery pages `grid-created`, `room-created`, `versus-created`.

## 6. Plan for steps 2-4 (as written after step 1)

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

