# New fighters as extra roster entries (phase 1)

Date 2026-11-11. Branch `feat/new-fighters` from `remaster` 5014e839 (build 76). Follows the static study
`community/roster-slots-study.md` (option (b)). Measured offline with the user's own dump on disposable data roots
(`tools/defjam-mcp/dj.py` machine lock, override folders from `scripts/new-fighter.py`). Tags: **[V]** measured in
a run or verified in code, **[I]** inferred, **[U]** open.

Goal: "Doc 2" next to Doc, its own front-end ID, name and model, selectable and fighting; as many such fighters as
possible; later every one editable (stats, styles, moves, model, textures, name).

## 1. What a new fighter is

A **Character row appended to `assets/djv2.sod`**, copied from a stock fighter (the template) with the same
**coreID** and its own front-end ID (feID), row name, display name and optionally model
(`setup/ffnycontent/newfighter.py`, `append_character`). Nothing stock moves:

* The Character table (hash `0x4CE5B1B2`, 75 rows of 26 words) has two class blocks, both sorted: class
  `0x0AD29D29` keyed by **word 13** (1 = a fighter with its own select cell, 0 = an alternate outfit: D-Mob 2/3,
  Sticky 2, Blaze 2/3, E3 Hero) and class `0x4CE5B1B2` keyed by the row-name hash; plus one plain row list keyed by
  feID (field `0xB03E16FC`), sorted by feID. A new row goes last in both class blocks (key 1; a row name whose hash
  is above every other, e.g. `Doc 2_mfaaaaa`) and in its feID place in the list. Every existing row keeps its
  position, so every item ID (`7 << 24 | row`, the fighter unlock set, saved recipes) stays [V, unit test].
* Row words (field hash = schema key XOR table hash, as `scripts/sod-characters.py` reads them): 0 row name,
  1 bodysize (enum), 2 Charisma, 3 (0), 4 Cost, 5 display name, 6 Entity (`DefaultEntity`), 7 Ethnicity, 8 feID,
  9 FightingStyle (enum), 10 coreID, 11 (stat, not shown), 12 Health, 13 own cell / outfit, 14 Model, 15 Order,
  16 lower-body power, 17 upper-body power, 18 style bits (0 Kickboxing, 1 Street fighting, 2 Martial arts,
  3 Wrestling, 4 Submission), 19 Sex, 20 Shadow, 21 speechID, 22 Speed, 23 Thumb (`SomeThumb`), 24 tier bits,
  25 Toughness.

## 2. The probe: answers to the study's unknowns

| # | Question | Answer |
|---|---|---|
| 1 | Doc copy as feID 100, data only: in Versus select? thumbnail? locked? | **Shown as a cell of its own, locked** (`LOCK=0`, it is not in the profile's fighter set) among the locked cells [V]. `LOCK=1` means unlocked (the 12 unlocked cells of a fresh profile are exactly the `LOCK=1` strings). The exact `GetNextFighter` strings: `CLOTH=1&FE_ID=55&LOCK=1&U=43&L=30&S=50&T=27&H=50&C=50&K=2` (Doc), `CLOTH=3&FE_ID=56/81/82&...` (Blaze's three outfits, one string each), `CLOTH=1&FE_ID=100&LOCK=0&U=43&L=30&S=50&T=27&H=50&C=50&K=2` (the copy) [V, `RECOMP_FE_PROBE=1`]. Bars: U = word 17, L = word 16, S = word 22, T = word 25, H = word 12, C = word 2; then the styles (`K=`, `S=`, `M=`, `W=` are the 1st..4th style, values 0..4) [V]. |
| 2 | Match with feID 100: loads? HUD name? intro speech? CPU? | **Loads and fights** (select screen and automatch) [V, screenshots]. **HUD name = word 5** (display name, upper-cased: `DOC 2`) [V]. Character index 9 = Doc's (coreID 10 - 1), max health 304 as Doc [V, peek]. **Intro speech and taunts are Doc's** (`speech/streams/intro/025.asf`, `Taunts/.../025.DAT`) and stay Doc's when the row's speechID (word 21) is changed: the voice follows the coreID or the setup, not that word [V]. CPU Blaze fought Doc 2 normally; Doc 2 as the CPU not run [U]. |
| 3 | A new model name (`V2IP_901A`)? | **Needs four things**, each measured by its absence (fight stalls in the intro, no fighters, no HUD) [V]: the model files `V2IP_901A.o/.xsh/_F.o` in `v2ip.viv` **with the model's own symbols renamed** (the `.o` is an EAGL ELF whose parts are named `V2IP_109A_alpha...`; same-length rename), the entrance/intro camera banks `CE_901A.viv`/`CI_901A.viv` in `v2ta.viv` (copies with entry names and symbols renamed), and a `<taunt>` entry `V2IP_901A -> CI_901A/CE_901A` in **`assets/IntroExtro.xml` inside `nis.viv`** (the loose `assets/taunt.xml` is the same list and is never opened). Plus an `entity.xml` entity named after the model (added; not tested without). Result of the full set: see section 4. |
| 4 | Block 0's key; does appending change an item ID? | Word 13: 1 own cell, 0 outfit (the six alternates). Appending changes no ID [V]. |
| 5 | IDs 100..141 vs the created-fighter archetypes | The game **reads `FIGHTER.XML`** (archetypes 86..141 of created fighters live there, e.g. ID 100 = `CP KBW`). So a new fighter gets **no fighter.xml record** (it would duplicate an archetype ID); Battle and the hub read the database. In `Game.StartGame` the created-fighter branch (`sub_00020270` at 0x2096D) writes the archetype into the slot after the Character scan, which matches the fighter byte 99, so the scan never sees an archetype ID [I, static]. A Battle match of a created fighter with archetype 100..141 next to a new row of that feID is not run yet [U]. |
| 6 | Name fields | `ResetFighterList` copies the row's name string into a 32-byte list record at +4 with no bound (+0x18 and +0x1C follow: tier, feID): row names must stay **under 20 bytes** [V, code]; `name_above` adds 8 characters, so a base name of at most 11. The slot's model/name/shadow copies at +0x148/+0x158/+0x168 are 16 bytes apart: keep model and row names under 16 [I]. The HUD shows the display name (word 5) [V]. |
| 7 | Copied portraits: inner names? | The select screen's big portrait of an outfit is `portrait/p_CF/p_cf_<feID+1>_<n>p` (blank for feID 100 without it [V, screenshot]); the name logo stays the cell's. Each picture is a small BIG whose entries are named after it (`p_cf_56_2p.apt/.const/.o/.xsh`). Families and ID offsets: `p_cf`, `p_ML`, `p_CT`, `portrait/<n>_<k>p` use feID + 1; `l_CF`, `l_ML`, `I_ML`, `l_MS`, `t_MS`, `t_CT`, `t_CO`, `thumbs/thumb` use feID [V, dump listing]. `newfighter.picture_copies` makes the copies (outer only, or inner entries renamed); which one the movie needs is section 4. |
| 8 | Story; the unlock tool with 76 rows | Not run [U]. The select hooks only answer the select screens; Story reads its own lists. |
| 9 | Online | Not run (no relay change deployed). See section 6. |

## 3. Game hooks (branch `feat/new-fighters`)

`src/hooks/new_fighters.c`:

* **Read the database the game reads** (`netplay_open_fight_file`: the override/composed copy the print hashes)
  and list every row with feID 100..254, its template = the stock fighter with an own cell and the same coreID.
* **`Game.ResetFighterList` wrapper** (`sub_00047BC0`, reached through the command vtable, so
  `recomp_lookup_manual` can wrap it; no re-lift): the game keeps its list in a global array of **128 entries**
  (0x3A07B0, no bound check), 74 stock. While it runs, each new row says feID 98 (the E3 Hero, which the list
  skips) in the loaded database and gets its own back right after, so the game's array never holds a new row.
  The loaded database is found once in guest memory by the file's first 32 bytes and checked by the rows' feIDs.
* **After `Game.ResetFighterList`**: read the game's whole list once (its own strings, its order), then answer the
  screen's `Game.GetNextFighter` calls from a copy (the **`GetNextFighter` wrapper**, `sub_000480E0`: the game's
  call runs, then its answer buffer at `this+0x10` gets the copy's string): every new fighter is one more outfit
  behind its template's (`CLOTH` + 1 in every string of the cell, `LOCK=1`), with the template's string as its
  base and its own feID and stat bars (row words 17, 16, 22, 25, 12, 2). **Always unlocked, nothing saved**: the
  profile's fighter set (80 entries) is not touched.
* Result [V]: Doc's cell shows outfits **1 2**; outfit 2 is Doc 2 (`Controller.ControllerSetup(0,0,100,0)`),
  the fight runs with `DOC 2` on the HUD.

`src/hooks/online_data.c`: `online_new_fighters()` lists the new fighters (name = display name, style = the
template's) and the hub's fighter picker (`online_screen.cpp`, `roster()`) offers them; `online_fighter_name()`
names them. They stay **out of** `online_fighters()`: CPU places, random picks and drafts stay stock, as the
relay's list is.

`scripts/new-fighter.py` (tests and the future installer op): builds an override folder with the row, optional
stat edits (`--set WORD=VALUE`), a new model (`--new-model`, with the symbol rename, banks and IntroExtro entry),
placeholder pictures (`--pictures copy|renamed`). The files are the player's own data re-arranged and stay local.

## 4. Measured with the hooks

(Filled in from the runs listed in the hand-off; see the report.)

## 5. How many fighters

* **Now: 155** (feID 100..254) [code; 3 at once measured, section 4]. Limits that are gone with the hooks: the grid
  thumbnail sprite (feID + 150 frames; outfits use the template cell's thumbnail), the 80-entry unlock set (not
  used), the 128-entry list array (new rows are kept out of it).
* What still limits it:
  1. **The fighter ID is one byte** everywhere a match is set up: `Controller.ControllerSetup` writes a 5-byte
     entry per slot (byte +1 = fighter), `Game.StartGame` compares the row's feID with it, and online the ID is one
     byte in `READY`, `ROOM`, `START`, `RANKED`, `DRAFTACT`. 255 stays free, 98 and 99 are taken.
  2. Unused feIDs below 100 (5, 15, 29, 37, 39..42, 49, 52, 53, 63, 66, 78, 79, 86..90, 94..97) are about 25 more,
     but several are fighter.xml IDs of stock fighters the hub maps (40, 41, 52, 78) and the probe frames for
     them are emptied: not worth it.
  3. **Outfits per cell**: the clothing step draws one numbered card per outfit (`CLOTH_THUMB_SPACING` apart);
     2 and 3 measured, more than about 6 on one template will run off the paper [I]. Spread new fighters over
     templates, or (b2) give them own cells.
  4. The hub list (`NEW_FIGHTER_MAX` 155) and the hook's answer list (256 strings) are sized for this.
* **Beyond 255**: a 16-bit fighter ID through the front end's setup entries (`sub_000558E0`, `sub_0001E690`,
  the 5-byte entry and the 20-byte copy at 0x2DDC08), `Game.StartGame`'s compare, the summary and rewards that
  read the byte, and the netplay protocol (every message above, the relay's validation, ranked/draft) — a
  protocol version step for every player. Several days plus a coordinated release. Not needed below ~150 packs.

## 6. Online (described, not deployed)

The relay's `ValidFighters`/`safeFighter` turns any other ID into Doc (55). Needed: accept 100..254 in
`READY`/`ROOM`/`START`/hill rooms when the room's content print is non-zero (both PCs then have the same packs:
`djv2.sod` and `v2ip.viv`, `v2ta.viv`, `nis.viv` are fight files in the print), never in Classic/ranked-stock,
never as a CPU fill, draft and random stay stock; the accounts service claims feIDs per pack (like venues
28..63); My Stats names them from the catalogue. The game side already keeps CPU/random/draft stock.

## 7. What a new row can carry by itself (for the Studio fighter editor)

| Field | Where | Per new fighter? |
|---|---|---|
| Name on the HUD | word 5 (display) | **yes** [V] |
| Row name | word 0 | yes (hash must sort last; < 20 bytes) [V] |
| Model and textures | word 14 + `v2ip.viv` (.o/.xsh/_F.o) + CE/CI banks + IntroExtro entry | **yes** (new model name) [V/section 4]; same skeleton as the template |
| Select-screen stat bars | words 17, 16, 22, 25, 12, 2 | yes, **display only**: a row with every bar at 10 fought with Doc's max health (304) and Doc's per-slot stat block [V, peek of record +0x350 and +0xB34] |
| Style list on the select screen | word 18 bits, word 9 | display only [I: the fight's styles and moves come from the character index = coreID - 1] |
| Cost, Order, tier | words 4, 15, 24 | menu (Rewards shop cost, order) [I] |
| Shadow, bodysize, ethnicity, sex | words 20, 1, 7, 19 | read by `Game.StartGame` into the slot [V code]; effect not measured [U] |
| Voice (intro, taunts) | word 21 speechID | **no**: changing it kept Doc's voice [V]; follows the coreID [I] |
| Fight stats, health, fighting styles, special moves, Blazins, AI, animations | per coreID tables (0x3B864C, 0x3B8654, `motion/main_all.dat`, the Blazin table by coreID) | **no**, the template's [V for health/stat block, I for the rest] |

So a new row is a new **look and name** on a template's **gameplay**. Editing gameplay per new fighter needs a
new coreID with its own per-character data (the 74-entry tables and banks indexed by character index), or a
run-time hook that swaps the per-slot data after `Game.StartGame` (the created-fighter path already computes
its own stats into the 7-dword block `GS+0x2B0+slot*0x1C` and takes its four Blazins from Blazin-table keys:
the model for an "edit stats and Blazins" hook, but it needs a lockstep-safe design online).
