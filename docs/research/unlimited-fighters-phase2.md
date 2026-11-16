# Community fighters beyond the places: phase 2 (online)

Date 2026-11-16. Branch `feat/unlimited-phase2` from `feat/unlimited-phase1` (086069c8); server repo branch
`feat/unlimited-phase2` (from main 4a76c6e, its `docs/unlimited-fighters-online.md` has the relay's and the account
service's side). Design: `community/unlimited-fighters-study.md` §4.2, §5 (owner GO for phase 2 on 2026-11-16).
Phase 1 (offline banks): `unlimited-fighters-phase1.md`. Tags: **[V]** measured in a run or checked in code, **[U]** open.

## Wire (CAPS bit 25, `CAPS_UNLIMITED`)

The **fighter number** is a u32: 0..255 the old front-end byte, 1000 and up a community fighter beyond the places (the
pack's `fighter_fe_id` in the signed catalogue; 999999 is the MORE cell, never a fighter). Bits 22 (skins), 23
(standard preference; Run the Streets also uses 23 on its branch and is meant to move to 24) and 24 stay clear.

* **READY**: ready, the fighter's **template** byte, skin 0, the number u32 LE (7 bytes), only to a relay that
  answered bit 25; without it the pick plays its template (2 or 3 bytes, logged).
* **ROOM**: after the rules trailer, count + (slot u8, number u32 LE) for each player whose number the relay keeps.
* **START / WATCH / WATCHLATE**: after the skins trailer (whose count may then be 0), count >= 1 + (slot, number).
  Each replaces its player's template byte, so the match's lineup (`AutoMatch.fighter`) holds the number.
* A bank row (124..127) named as a fighter in START plays Doc on every PC that has banks.

The relay keeps a number only when its switch is on, the number is in the catalogue, the room takes new fighters
(content print, not ranked / tournament set / crew, not STANDARD ONLY or no created fighters; "Original fighters only"
has a marked hook), every member (spectators and the hill line too) announced bit 25 with the room's print, and the
template survives the room's FIGHTER RULES. An older client (no bit 25) is refused with ERROR 10 (update needed)
from a room where a player holds a number; QUICK never puts it there.

## Game

* **Binding** stays a pure function of the START lineup: automatch tells the banks before the set-ups whether the
  lineup names numbers (`fighter_banks_online_lineup`). Only then, online, do the set-ups bind bank *s* to slot *s*'s
  fighter and do the record lookup and the movie loader redirect; until the next set-up, so the results and a Rematch
  keep it. Spectators and hill watchers set up the same START. MORE and the select screen's window stay offline.
* **Print**: `assets/ffny_fighters.bin` joins the fight-file print where there is one (a PC without community
  fighters prints as before). Phase 2 keeps "every player has the whole catalogue": the composed database is the same
  for the same packs and composer, the content print keeps the rest apart.
* **Hub**: CHOOSE FIGHTER shows a shelf of up to 30 community fighters (played this session first, then by number)
  and a MORE card that opens the community window (12 a page, LB/RB) where the relay takes numbers and the room allows
  them; RANDOM stays with the roster; the skin window is unchanged for stock fighters. Names and portraits by number
  (the composer writes `online-art/fighters/<number>.png`; without one the template's portrait), on the grid, the
  room card and the versus screen. Ranked queues a saved community pick as its template (RANKED stays a byte).
* **Launcher**: `index-v5.json` (numbered fighters; v4 and older leave them out, so build 85 launchers never refuse
  the index), falling back to v4 then v3 on 404; `mod_catalogue` takes `fighter_fe_id` 1000..0x7FFFFFFF but 999999.

## Tests

`tests/unit/test_unlimited_online.py`, `test_content_sync.py` (index chain); server `relay/unlimited_test.go`,
`relay/accounts/fighter_numbers_test.go`. Game runs: `dj scenario online` with `--override` a synthetic set
(`scripts/unlimited-synth.py --count 120`), `--env RECOMP_TEST_CONTENT_PRINT=<hex>` (rooms need a content print),
`FFNY_SERVER_DIR=<server worktree>` and `DJ_RELAY_EXTRA="-unlimited -unlimited-numbers @numbers.txt -client-check
off"`. `RECOMP_TEST_NO_UNLIMITED=1` makes a copy an older game (no bit 25). Results: the branch's report.
