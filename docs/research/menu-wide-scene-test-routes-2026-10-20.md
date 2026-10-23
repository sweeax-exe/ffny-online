# Native routes for approved wide menu backgrounds

Checked 2026-10-20 against `scripts/harness.py`, `scripts/scenarios.py`, the local fixture library, `logs/menu-ui-v1-rc/funccall-catalog.json`, and the extracted front-end flow metadata at `$DEFJAM_DATA/extracted/screens/feflow.xml`. This is a route inventory, not a game run. Existing harness routes are proven to reach their stated stops by their definitions/history; proposed menu navigation below remains unverified until a capture confirms it.

## Proven routes already in the harness

| Background/movie | Existing entry | Evidence and capture point |
|---|---|---|
| `charDec` | `python3 scripts/harness.py run intro --png` | `ROUTES['intro']` starts a new Story ID, watches `getscreeninfo(story/chardec)`, and captures eight seconds after that call. The harness describes the stop as the creator's first screen. This is the only approved target here with a direct Story route already in `ROUTES`. |
| `oldMLoad` | `python3 scripts/harness.py run fight-terrordome --png` or `ffa-terrordome` | Both routes traverse Battle normally and include `@getscreeninfo(battle/oldmload,1,` in `shots`. It is a separate card/loading screen; it is not the dynamic wall background. |
| Dynamic match background | Same two native Battle routes above | Native loading run identifies the active One-on-One background resource as `screens/match/0_bg/paper_1v1.big`; the matching FFA/team variants are `paper_ffa3.big`, `paper_ffa4.big`, and `paper_team.big`. APT geometry, placements, UVs and texture IDs are audited in [`loading-bg-mapping-report.md`](../../logs/menu-ui-v1-rc/loading-bg-mapping-report.md). This is distinct from `battle/matchLoa.big`. |
| `matchLoa` | Same two native Battle routes above | `logs/menu-ui-v1-rc/funccall-catalog.json` identifies `screens/battle/matchLoa.big` separately and lists `Game.StartGame()` there. The dynamic match background is the `screens/match/0_bg/paper_*` resource family, not this APT. |
| `tips` | `python3 scripts/harness.py run fight-result --png` | `fight-result` is a full One-on-One route that waits for `game.getmatchsummary(`. `feflow.xml` has a `battle/tips` screen and its next transition returns to `battle/cmtype`; the catalog associates `Game.GetTips()` and `Game.StoryModeKick(0)` with `screens/battle/tips.big`. The route definition does not currently wait for the tips call or capture it specifically, so the exact screen capture remains to be confirmed from a run log. |
| `cribOptions` | `python3 scripts/harness.py run crib --png` reaches its parent `story/crib` | `feflow.xml` explicitly lists `story/crib -> options/cribOptions`, and the reverse transition back to `story/crib`. The crib route intentionally stops on the crib; it has no calibrated pad stage selecting Options. Thus the parent screen is proven, while the child-screen route is not yet calibrated. |

The repository harness exposes `--stage` on `run`; a new checkpoint can be explored by anchoring to the observed `[FUNCCALL]` screen call, and `--shots` can capture relative to that anchor. The route definitions use `getscreeninfo(...)` anchors to synchronize menu input. `Control.GetScreenInfo` is the game's screen metadata/action channel; do not treat a bare call as a reliable command to load an arbitrary movie.

## No confirmed route yet

| Background/movie | What the metadata establishes | Next bounded route investigation |
|---|---|---|
| `choosGF2`, `choosGF4`, `choosPart` | The command catalog associates all three SWFs with `screens/story/storyChs.big`; its strings include the SWF names. The flow XML does not list these as standalone screens or transitions. That points to sub-movies managed inside `storyChs`, but does not establish the user's actions or how to reach that parent screen. | Trace a normal Story progression from a disposable Story fixture, recording `[FUNCCALL]` lines and screenshots until `storyChs` loads. Then add a calibrated stage to the nearest route or fixture scenario. The current fixture library at `/Users/vlad/Code/defjam-data/fixtures/library.json` contains only `mac-profiles` (Story `crib`), not a `storyChs` checkpoint. |
| `cribOptions` | It is a child of `story/crib` in the flow XML, and its APT catalog contains the ordinary option getters/setters. The XML provides no button mapping. | From a throw-away copy of the existing `mac-profiles` fixture (Story `crib`), observe the crib's visible menu/action mapping and calibrate the input that opens Options. Do not infer a direction from transition ordering. |
| `battle/tips` | Its APT module and function calls are catalogued, and the flow XML gives its exit to `battle/cmtype`. No direct entrance appears in the listed flow transition excerpt. | Capture the full match-result path and inspect the actual front-end call sequence around result/rewards/rematch; add an anchor and screenshot once the tips screen is observed. |
| Standalone `matchLoa` | It is a distinct catalogued APT from `oldMLoad`; `Game.StartGame()` is present in both. Catalog presence alone does not prove runtime visitation. | In a serial native Battle run, record all `[FUNCCALL] Control.GetScreenInfo(...)` entries around the loading handoff. Add only the observed screen anchor to the route. |

## Test environment and fixture boundaries

`scripts/scenarios.py scene <name>` / `RECOMP_SCENE=<name>` launches the game's developer **CINEMA** mode from `assets/cinema/*.viv`. It is useful for approved in-engine cinema scenes, but does not load front-end APT/SWF screens such as `choosGF2` or `cribOptions`.

`RECOMP_FE_INJECT` injects a command through the front-end dispatcher after a matching observed call. The catalog lists commands found in APT strings; it does not establish that any command directly selects or loads these target SWFs. No supported `RECOMP_TEST_*` setting in the inspected scripts/hooks targets these menu movies. Keep captures local under `logs/shots/` and use the harness save guard or a disposable scenario fixture for routes that can alter saves.

## Evidence files

- `scripts/harness.py`: route definitions `intro`, `crib`, `fight-result`, `fight-terrordome`, and `ffa-terrordome`; `--stage` and `--shots` syntax.
- `scripts/scenarios.py`: fixture library rooted at `$DEFJAM_DATA/fixtures`, generated fight routes, and the separate CINEMA scene launcher.
- `/Users/vlad/Code/defjam-data/fixtures/library.json`: only `mac-profiles`, Story point `crib`.
- `/Users/vlad/Code/defjam-data/extracted/screens/feflow.xml`: `intMain/mainMenu -> battle/cmtype` and `options/userID`; `story/crib -> options/cribOptions`; Battle transitions through `battle/oldMLoad`; `battle/tips -> battle/cmtype`.
- `logs/menu-ui-v1-rc/funccall-catalog.json`: Markdown despite the `.json` suffix. The command catalog maps `story/choosGF2.swf`, `story/choosGF4.swf`, and `story/choosPart.swf` to `screens/story/storyChs.big`; `Game.StartGame()` occurs in both `screens/battle/matchLoa.big` and `screens/battle/oldMLoad.big`.
