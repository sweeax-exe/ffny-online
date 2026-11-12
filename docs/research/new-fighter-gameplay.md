# New fighters with their own gameplay (stats, styles, Blazins)

Date 2026-11-12. Branch `feat/fighter-gameplay` (from `release/79`). Follows the static study
`community/new-fighter-gameplay-study.md` (option (b), "the created-fighter way") and `new-fighters.md` (phase 1:
a new fighter is a Character row of `assets/djv2.sod` with its own feID 100..127, name and model on a stock
fighter's coreID). The voice stays the template's (a separate task; the pack format reserves `voice`).
Tags: **[V]** measured in a run or verified in code, **[I]** inferred, **[U]** open.

## 1. Measurements

Runs with `RECOMP_NF_STATS_DUMP=1` log every fighter record's decoded parameters at the Blazins' registration
(`sub_001A8180_enter`, right after the records spawn): styles (`+0xAE4..`), the seven stats (`+0xB34..+0xB4C`),
the four Blazins (`+0xBB8..`) and the max health (`+0x354`).

| Fighter (feID, character index) | Styles | Stats U L S T H 6th C | Sum | Blazins | Max health | Run |
|---|---|---|---|---|---|---|
| Doc (55, 9) | 4 | 9 6 10 5 10 0 10 | 50 | 4 4 4 4 | 304.0 | `20261112-153657-offline` |
| Blaze (56, 4) | 3 | 10 9 5 10 7 0 9 | 50 | 2 2 2 2 | 265.6 | same |
| Crow (58, 0) | 4 2 | 7 9 10 9 9 0 10 | 54 | 0 0 0 0 | 291.2 | `20261112-171413-offline` |
| Suspect (60, 80) | 0 | 10 10 10 10 10 0 10 | 60 | 56 56 56 56 | 304.0 | same |

* **Max health = 176 + 12.8 x health stat** [V: Doc 10 -> 304, Blaze 7 -> 265.6, Crow 9 -> 291.2]. The study read the constant as 16
  (it multiplies 16.0 by 0.8 somewhere on the way); the run is what counts. A health stat of 15 gives 368.
* **Stock stats are 0..10**, the 6th dword is **0** for both (and for every created fighter, `sub_0001EE80`
  writes 0 there). Its effect is not measured; the pack calls it `grapple` (the Character row's word 11 field name)
  and allows 0..15, the editor keeps it at 0.
* A stock fighter has its **one own Blazin in all four keys** (Doc: Blazin 4 `Blazing_Doc`, Blaze: 2). So four
  Blazins need not differ; the rule is "four ids 0..81".
* **The select-screen bars are about 5 x the stat** (Doc 9 6 10 5 10 10 -> bars 43 30 50 27 50 50; Blaze
  10 9 5 10 7 9 -> 50 43 27 50 33 47): `round(bar / 5)` gives back all four measured fighters' stats exactly, so
  the editor starts a template's values from its bars and its own Blazin from the Blazin table's owner word
  (`newfighter.template_gameplay`; Suspect's Blazin 56, "Shadowy Figure", has no owner row and is taken from the
  run). A unit test checks the four against the runs.
* Styles: the WPAR enum 4 = Martial arts (Doc, word 18 bit 2), 3 = Wrestling (Blaze, bit 3); none = -1 here
  (the study's 7 was a guess).
* The core table `0x3B864C` holds a WPAR only for the cores of the match being loaded (all 141 empty at
  `sub_001A4D90_enter`), so a one-run dump of every stock fighter is not possible this way; the budget comes from
  the bars (section 3).

## 2. Game hooks (`src/hooks/new_fighters.c`, `fighter_gameplay.c`)

The new fighter's gameplay is two Character row words the game never reads (the study: no field-hash immediate
for words 3 and 11 anywhere; stock rows hold 0 and 50):

```
word 11  bits 0..27  stat k (k = 0..6) in bits 4k..4k+3: Upper, Lower, Speed, Toughness, Health (1..15),
                     6th "grapple" (0..15), Charisma (1..15)
         bits 28..29 version (1)       bits 30..31 moveset bits 4..5
word 3   bits 0..27  Blazin key k (k = 0..3) in bits 7k..7k+6, each 0..81
         bits 28..31 moveset bits 0..3
moveset  0 = the template's own moves; 1..55 = created-fighter archetype 85 + moveset (fighter.xml 86..140)
```

`fighter_gameplay_decode` refuses a block as a whole (the fighter then fights as its template) when the version
is not 1, a stat is out of range, the seven add up to more than `FGP_STAT_BUDGET`, a Blazin is above 81 or the
moveset above 55. It is a pure function of the two words of the database both PCs load (the composed `djv2.sod`
is in the fight-file print), the same clamp on every PC, like `cp_record_sanitize`.

Three hooks, each a write the game's own created-fighter branch makes, all before the first fight step:

1. **After `Game.StartGame`** (`start_game_wrapper`, `dev_unlock.c`): for a slot whose fighter byte
   (`GS+0x1AAA+5s`, unsigned) is a new fighter with gameplay and whose `GS+0x168+4s` holds the row's coreID: the
   seven stats into `GS+0x2B0+0x1C*s`, `GS+0x2AC+s = 1`, the Blazin keys 0..3 into `GS+0xE0/+0xD8/+0xE4/+0xDC+16s`
   and the copies 0x40 above (`GS+0x120/+0x118/+0x124/+0x11C`) [V code: `sub_00020270` 0x208DB..0x2090C, loop
   strides 5 / 4 / 0x10 at 0x20B6D].
2. **`sub_001A4D90_enter`** (the fight's set-up, before `sub_001B3110` spawns the records): a style combination's
   character index into `0x3B90B8+4s` (85 + moveset), checked to hold the template's index first. Styles, moves
   (WWAZ), the `wres` animation bank and the >= 86 Blazin rule follow it. `GS+0x168` keeps the template's coreID
   (speech row, intro clip).
3. **`sub_001A8180_enter`** (new; a re-lift): a template-moveset fighter (index < 86) gets its four Blazins into
   its record (`+0xBB8.. = keys 1, 3, 0, 2`, the game's order for created fighters) before the body registers the
   motions (`0x3BE628`) from the records [V code: `sub_001A8180`, records `0x3B92E0 + 0x12E8*n`, `+0x28` slot,
   `+0x2C` index].

**Lockstep and rollback.** All three write before step 0 into the set-up (`GS`), the globals `sub_000871F0`
rebuilds from it, and the fighter records the rollback snapshots cover; nothing changes during the fight and no
host state is read later, so a re-run step sees the same values. The model is a normal `V2IP` model (no
runtime-built look) and `setup+0xA8` stays below 87 (no random created-fighter intro). Measured in section 4.

**CPU.** The AI file picker (`sub_000D6EE0`, the cores 0x57..0x8D -> `char0001` test at 0xD6F5A) reads the
set-up's coreID copy at `0x3B9278+4s`, which hook 2 leaves at the template's core. So a style-combination
new fighter as the CPU keeps its template's AI file (`char0010` for a Doc copy; the log line `set-up core for
the AI 10`) driving the archetype's moves. Measured [V]: CPU "Doc S" (Wrestling + Street, character index 120)
against a bot-driven Magic attacked and landed 4 hits by update 882 and led 5000 to 500 points
(`20261112-171521-offline`, the fight then stopped at a weapon tutorial card the bot does not dismiss), against
4 hits by update 863 (8 by 1334) for stock CPU Doc (`20261112-172221-offline`; that run too ended at the
harness's time limit without a result). It fights; pointing the AI at
`char0001` (the CP cores' file) was not needed.

**Ranked.** The relay plays a new fighter only in a room with a non-zero content print and never in ranked,
tournament sets, crew battles or under FIGHTER RULES stock (`newFighterRoom`, server `relay/protocol_lobby.go`);
unchanged.

## 3. Budget and ranges

The stock bars sum to 215..266 for every stock fighter except Suspect and the Story hero (300 each), i.e. stat
sums of 43..54 and 60; measured: Doc and Blaze 50, Crow 54, Suspect 60. **Budget = 60** (Suspect, the largest
stock sum; the Story hero is not a template), the same constant in `fighter_gameplay.h` (`FGP_STAT_BUDGET`),
`newfighter.py` (`STAT_BUDGET`) and the account service (`GameplayStatBudget`). Each stat 1..15 (grapple 0..15),
so a new fighter can go past a stock fighter in one stat (health 15 = 368 HP) only by giving up others.

## 4. Tests

Override folder from `newfighter.append_character` + `gameplay_words` (as the installer writes it): **Doc T**
(feID 100, Doc's moves, stats 12 9 4 6 15 0 4, Blazins 74 44 16 65 = Snowman, Skull, Trejo, Trick) and **Doc S**
(feID 101, Wrestling + Street = `CP WB_`, character index 120, stats 10 10 8 8 6 0 8, Blazins 2 0 9 4).

| Run (logs/dj/...) | What | Result |
|---|---|---|
| `20261112-165849-offline` | Doc T vs CPU Doc S | Doc T: index 9, stats as set, **max health 368** (176 + 12.8 x 15), Blazins in the record 44 65 74 16 (keys 1 3 0 2, set by hook 3). Doc S: **index 120, styles 3 0** (W, B), max health 252.8, the set-up's Blazins 0 4 2 9 (keys 1 3 0 2) copied by the game. HUD `DOC T` / `DOC S`, KO |
| `20261112-171521-offline` | bot Magic vs CPU Doc S | the CPU fights on the archetype's moves with Doc's AI (section 2) |
| `20261112-173435-online` | two copies, lockstep, 3 matches, Doc T vs Doc S (`--override`, `RECOMP_TEST_CONTENT_PRINT`) | both copies log the same set-up and records; **50 checksums compared and equal, desync 0**, three KOs |
| `20261112-173655-online` | two copies, **rollback** (`--rollback on`), 3 matches, Doc S vs Doc T | the same records on both; **40 checksums equal, desync 0**; match 3 ran **49 rollbacks** (venue 24) |

Unit tests: `tests/unit/test_fighter_gameplay.py` (the C decoder compiled natively against the Python encoder on 300
random blocks, the archetype order against the dump's `fighter.xml`, the four measured stock fighters against
`template_gameplay`, the budget), `test_new_fighter_pack.py` (a pack's block reaches the composed row; an
out-of-range one is skipped), the server's `packs_fighter_gameplay_test.go`, Studio's
`test_ffny_studio_fighter_gameplay.py`.

## 5. Open

* The 6th stat (`grapple`): 0 in every measured fighter; its effect is unknown, the editor keeps it 0.
* Own voice (study section 4): the pack's `voice` key is reserved (`{"mode": "template"}` only).
* A style combination keeps the template's AI file (`char0010` for Doc); it fights, but whether `char0001` (the
  CP cores') plays the archetype's moves better was not compared.
* The select screen's style letters for a style combination come from word 18 (written by the installer); not
  looked at on the select screen yet. A new fighter's stat bars there are 5 x stat (50 at most).
* Not run: a KOTH room, the Studio "Test in a fight" button end to end with the installed game (needs a build
  with these hooks), a rematch with a changed fighter.
