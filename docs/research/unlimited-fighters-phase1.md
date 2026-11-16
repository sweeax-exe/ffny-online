# Community fighters beyond the places: phase 1 (offline banks)

Date 2026-11-16. Branch `feat/unlimited-phase1` from ffny main 46d34601 (build 85). Design:
`community/unlimited-fighters-study.md` (owner-approved phase 1, 2026-11-15). Offline only. Tags: **[V]** measured
in a run or checked in code, **[U]** open.

## What it is

* **Data** (`setup/ffnycontent/unlimited.py`, composer 9): a new-fighter pack whose catalogue ID is 1000 or above is
  a *logical fighter*. It gets no Character row. The composer adds four **bank rows** (feIDs 124..127, the last four
  places; copies of Doc's row named `FFNYBank0..3`, display `BANK 1..4`) and, per logical fighter, one **unindexed
  26-word record** (the template's row with its own row name, display name, model, ID and gameplay words; its
  strings in the database's string table; in no class block and no row list). The side file
  `assets/ffny_fighters.bin` (FFLF v1) lists them: the database's size and FNV-1a, the banks' offsets, per fighter
  its ID, record offset, template, coreID, name, model. Models, camera banks, intro entry, entity and pictures go in
  as for a place fighter, under the logical ID. The game's Character table only ever holds stock + places + 4 banks.
* **Binding** (`src/hooks/fighter_banks.{h,c}`, `fighter_bank_hooks.c`): a `Controller.ControllerSetup(slot, pad,
  id, outfit)` with `id >= 1000` reaches the game as `ControllerSetup(slot, pad, 124 + slot, outfit)` (the FuncCall
  hook and host calls; the original command string is not written), and bank `slot` is bound to the fighter. The
  database's record lookup `sub_000BE760` (wrapped in `recomp_manual.c`, as Vlad's participant window does) answers a
  bound bank's row with the fighter's record. One lookup per bank at `Game.StartGame` (caller 0x2063F) is enough:
  name, model, coreID, stats and the gameplay words reach the slot [V]. Bank *s* is slot *s*'s only, so the same
  fighter in two slots uses two banks (mirror). The binding is each slot's last set-up (a pure function of the
  lineup); a stock pick releases the slot's bank.
* **Screens**: the game's answers keep the bank's ID. With a fighter's own ID (1000+) the screens stopped: the
  result screen right after `Game.GetMatchSummary(n)` (0 fps, `[HANG]`, logs/dj/20261116-110206-offline; with the
  bank's ID 20261116-110907-offline passes) and the loading screen after `Game.GetMatchInfo()` (oldMLoad uses the
  ID as an index, 20261116-115258-unl-sel7) [V]. Instead the composer gives the four banks Doc's pictures as
  placeholders, and the APT movie loader (`sub_001810B0` entry) loads any picture named after a bound bank under
  its fighter's ID: `l_ML_124_2p -> l_ML_1043_2p`, `p_ML_125_2p -> p_ML_1044_2p` (loading screen), `t_MS_124_2p ->
  t_MS_1043_2p`, `l_MS_125_2p -> l_MS_1029_2p` (results) [V, 20261116-120452-unl-sel7]. The fight HUD shows the
  record's display name, the summary its `fighter=` name; intro and win cameras follow the record's model [V].
* **Select grid**: the bank rows are hidden; after the place fighters a **shelf** of up to 30 logical fighters
  (picked this session first, then by ID) and a **MORE** cell (ID 999999) that opens the **community window**
  (`community_ui.inc`: 12 per page, d-pad, LB/RB pages, A takes, B keeps the default) for the slot that picked it.
  Each cell is unlocked (`LOCK=1`), one outfit, its template's string with its own bars.
* **Grid pictures by URL** (`scripts/apt-patch.py` version 6): an ID of 1000+ is past `thumbAll`'s 300 frames, so
  `gotoAndStop` did nothing and the cell kept feID 0's picture [V]. The patch copies the screens' main frame action
  with a call after each of the grid's three `gotoAndStop`s (redraw, deselect: `off`; select: on) to its own
  `nfCommunityThumb(clip, id, suffix)`, which for such an ID empties the cell (frame 279) and loads
  `thumbs/thumb<ID>[off].swf` by URL. chooseF2/3/4 verified on the dump (308 bytes inserted each).
* **Offline only**: while `ffny_online_locked()` a logical ID is refused (the slot plays the fighter's template,
  logged) and nothing is redirected. The relay never sends an ID above 255, the hub picker lists no logical fighter.

## Limits found

* The select screen's own rule "one player per character" (`showCharTaken` compares `getID()`) also holds for
  shelf cells: a mirror match cannot be picked on the select screen, as for stock fighters. Mirror works by
  automatch (and later by room/rematch paths) [V].
* A bank plays its fighter's template voice (the voice follows the coreID). An own voice would need a voice number
  per bank whose files come from the bound fighter (an archive-member redirect, phase 3B) [U].
* Recently played is per session; nothing is saved (as for place fighters).

## Tests

`tests/unit/test_unlimited_fighters.py` (composer, side file, C pure parts compiled natively, apt-patch v6);
`scripts/unlimited-synth.py --count 120` for game runs (synthetic fighters from the stock templates with own model
names). Run results: the branch's report.
