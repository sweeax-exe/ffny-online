# Venue ID audit: Terror Dome route

2026-10-17. Read-only audit of the user's own retail image and checked-in route/source metadata. No image bytes or lifted-code excerpts are included here.

## Finding

`Game.ChooseVenue(14)` does **not** name The Terror Dome in this image. In `game/venue.xml`, FE ID 14 is **Hunt's Point Scrapyard** (`ARENA_BG01A`, display name `01A Hunt's Point Scrapyard (Junkyard R)`). FE ID 6 is **The Terror Dome** (`ARENA_BG19A`, display name `19A The Terror Dome (OldSchoolClub)`). Thus the answer at the XML/script-ID layer is 6 for Terror Dome and 14 for Hunt's Point.

There is a second mapping in guest code. `Game.StartGame` reads the script venue value saved at Game object `+0xCA4` and calls `sub_000461C0`. The real loop starts at index 0, increments, and repeats only while signed `eax < 0xD`; it therefore searches **13 entries**, indices 0 through 12, at guest address `0x002DD978`. The effective script ID → stored game-data slot pairs are `0→0, 1→1, 2→2, 3→3, 4→4, 5→5, 17→6, 24→7, 11→8, 26→9, 10→10, 8→11, 12→12`. (The apparent 14th pair, `13→13`, is not searched.) Any input absent from those 13 keys writes stored venue `0` at the output object's `+0x1AD0`. Consequently, **both 6 and 14 fall through to mapped value 0**. The script FE ID and the stored venue slot are different name domains; this compact mapping alone cannot distinguish the two venues.

## What the bounded call trace establishes

In `sub_00046E40` (`Game.StartGame`), the `+0xCA4` value is passed as the mapper's first argument and the mapped output object receives the write at `+0x1AD0`. StartGame then continues to use the original `+0xCA4` script value in a separate loop that retrieves venue-table data using key `0xD7DCA628` and compares candidate record values against that original script value. So the raw script ID and compact mapped value both participate in StartGame's setup path; one is not simply a display alias for the other. The bounded source trace does **not** establish that `+0x1AD0` is persisted Story progress, nor that it alone selects the rendered fight scene.

Generated guest sources also contain later reads of offset `+0x1AD0` in `sub_000647F0` (including a comparison with 8) and `sub_00065AA0` (the value is compared with several small constants). These reads establish that the compact field is consumed by further runtime logic. Their object identity and behavior are not enough, in this bounded audit, to label the field as a Story-save representation or as the final arena selector. The safe conclusion for IDs 6 and 14 is only that the mapper writes 0; **do not infer the rendered arena from that value alone**.

## Route consequence

The existing `ffa-terrordome` harness route is not calibrated as named. Its `right@6` stage is recorded in `config/scenario-catalog.json` as venue index 6, but its current plain run reports `Game.ChooseVenue(14)`. The report name cannot be justified by that telemetry: 14 names Hunt's Point at the XML FE-ID layer, and then maps to compact value 0. The catalog's FE ID6 agrees with the XML; its positional input recipe is the stale part when every venue is unlocked. Treat the route as selecting a front-end menu item whose script request is 14; mapper output is 0. The trace alone does not identify the rendered arena, so a match-scene confirmation is still required before calling it a Terror Dome fight.

This audit establishes the named IDs from the image XML and mapper behavior from the generated guest function. It does not independently identify the scene rendered after the mapper writes 0, nor explain the front-end map's traversal order. The recipe `right@6` is one right press six seconds after the anchor, not six presses. Those require an actual scene/capture or a further route calibration; no inference from the route label is evidence of the arena.

## Evidence trail

- `game/venue.xml`: FE ID 14 entry at the beginning of the table is `ARENA_BG01A`, Hunt's Point Scrapyard; FE ID 6 entry is `ARENA_BG19A`, The Terror Dome.
- `src/patches/dev_unlock.c`: `start_game_wrapper` documents Game object `+0xCA4` as the `Game.ChooseVenue` script ID and logs it at `Game.StartGame` entry.
- `src/recomp/gen/recomp_0002.c`, `sub_000461C0`: loop condition `eax < 0xD` means 13 searched entries (indices 0–12); a match reads the paired value at `+4`, otherwise writes zero to the output object's field at `+0x1AD0`.
- `src/recomp/gen/recomp_0002.c`, `sub_00046E40`: the mapper call passes `[Game object +0xCA4]`; StartGame also uses that original script value in a venue-table-key lookup and record comparison.
- `src/recomp/gen/recomp_0003.c`, `sub_000647F0` and `sub_00065AA0`: later consumers read offset `+0x1AD0`; their presence alone does not identify its higher-level meaning or the rendered venue.
- `docs/research/unlock-everything.md` § “Starting a match on a chosen fighter and venue” records a 14-pair interpretation, but the real loop bound confirms only the first 13 entries are searched. XML FE ID 6 is Terror Dome, but the guest mapper does not accept it.
- `config/scenario-catalog.json` and `scripts/harness.py` show the route/catalog discrepancy (`right@6`, catalog says index 6; harness stage is named `TERRORDOME`).


## Native verification and correction

The coordinator checked the primary game UI: the old positional recipe produced
Hunt’s Point Scrapyard on both the map and loading screen
(`run-20261017-162539.log.err`, local captures in
`logs/startup-skip/venue-menu-montage.png`). This confirms the catalog’s FE ID6
and refutes the old route’s Terrordome coverage with the current unlock state.

The corrected named routes and generated Terrordome scenarios inject
`Game.ChooseVenue(6)` after the menu’s first choice, through PR25’s existing
normal front-end dispatcher, before the loading screen begins. They retain the
menu navigation, fighter selections and combat input. The stress check now
requires both that early injection and the final `[START] venue(script)=6`.

`run-20261017-162943.log.err` positively records ChooseVenue14, the immediate
ChooseVenue6 injection, then StartGame venue6. Its loading capture explicitly
reads **The Terrordome**; the 15-second capture shows the four-player fight.
These local images are `logs/startup-skip/terrordome-{loading,fight}-final.png`;
no capture or game bytes are published. This actual UI and match evidence,
rather than the compact mapper’s fallback value, identifies the arena.

The corrected early-injection stress passes: **240seconds of four-player
combat, median120 presents per2seconds, minimum110**, no detected runtime
fault or save mismatch (`logs/regress-20261017-163533.txt`). The same table’s
unit row retains two route/composer capture-parity failures from before the
composer was synchronized; those are not waived. The corrected composer and
request-evidence parser have134focused unit passes, with the complete unit
rerun passing464cases (34skip,1slow deselected), recorded in
`logs/regress-20261017-164056.txt`. The earlier late
StartGame override run (`regress-20261017-162523.txt`) is superseded because
it did not verify the loading caption.
