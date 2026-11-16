# Run the Streets (open-world mode, closed test from build 86)

A free-for-all on a whole night-time New York block: up to four fighters, a knocked-out fighter comes back on a street
corner after three seconds, the last fighter who hit them scores the knockout, and the first to five knockouts (or the
leader when five minutes are up) wins. The rules are the Big Arena of `src/hooks/openworld.c`; the door, the map and
the access are `src/hooks/rts.c`; the social layer (chat bubbles, proximity voice, the docked chat) is
`src/hooks/rts_social*.c`. Server side: the private server repo, `docs/run-the-streets.md` there.

## How a player gets in

- **Access.** Only accounts the account service grants `run_the_streets` see anything (`GET /api/features`, by
  Discord role: `-rts-role` "FFNY Team" or `-rts-extra-role` "Run the Streets Tester"; the server owner and pack
  admins always). Signed out or without access the mode is completely hidden. The game asks in the background, keeps
  a yes 5 minutes and a no 1 minute, and asks again when the account signs in or out.
- **The door.** ONLINE (main menu) > **RUN THE STREETS** (after RANKED MATCH; only with access):
  - **VS CPU**: you and 2 or 3 CPU fighters, offline; left/right sets the count, A picks the fighter from the hub's
    fighter grid, then the fight starts. Four fighters play Free for All, three play 3-Way (a Free for All of two or
    three crashed at set-up on a new venue, `sub_001BBC10+0x80`; a One on One against one CPU ran 5 minutes without
    a knockout, so it is not offered). The rules are `[openworld]` (score_limit 5, respawn_seconds 3, time_limit
    300). Fights stay Run the Streets through rematches until the main menu shows again.
  - **QUICK MATCH** / **CREATE A ROOM** (up to four, free places filled by CPU fighters) / **FIND A ROOM**: online,
    a room of mode `0x43` on this relay. The relay checks the access itself and refuses with ERROR 8 and its own
    sentence, which the game shows.
- **Online rules are fixed** (first to 5, 3 s respawn, 300 s; no PC's own settings), the fights stay in lockstep
  (`every_player_rolls_back` returns 0 for them; `RECOMP_ROLLBACK_RTS=1` on every copy lifts it for tests), and the
  Big Arena state plus a hash of the map's spawn points are in every rollback save and the checksum.

## Wire

| What | Value |
|---|---|
| Room mode | `NETPLAY_MODE_RTS` 0x43 (ROOM/CREATE/SETTINGS); START carries match type 3 (Free for All) |
| CAPS bit | `NETPLAY_CAP_RTS` = 1 << 24, both ways (bit 23 is the standard preference's `CAPS_ORIGINAL`) |
| Rollback class | `NETPLAY_RB_FFA`; the client keeps RTS fights in lockstep regardless |

## The map (interface with feat/official-maps)

The map is official content from the signed catalogue (kind `official-map`, `rts_map: true`), composed by the
launcher like a community venue and always on. The game reads `<data>/mod-manager/content.ini`:

```
[pack.official-knox-and-jam]
kind=content
type=venue
official=1
rts_map=1
composed=1
venue_fe_id=28
venue_code=28A
```

- The Run the Streets map is the section with `official=1` and `rts_map=1` that is composed and applied
  (`content-files/files.ini`), the lowest `order` first. Its venue is `V2BG_<venue_code>`, front-end ID
  `venue_fe_id`. None: the page says "NO MAP YET" and nothing starts.
- **Spawns:** `<data>/mods/<id>/bigarena.json` (`{"version":1,"mode":"bigarena","size":[w,d],"spawns":[[x,z],...],
  "venue":"V2BG_28A"}`), read defensively (only when it names the venue; up to 16 points). Else a served
  `assets/bigarena_<code>.json` / `assets/bigarena.json`, else Knox and Jam 0.1's sixteen street spawns (a 0.1 pack
  only), else the Big Arena ring scaled to the walls.
- **Hidden from venue lists:** the hub's venue lists and votes leave the map out (`online_data.c`). The game's own
  offline Battle venue select still lists it (it is in the composed venue.xml); hiding it there is the composer's job.
- **Night light on the fighters:** the new-venue op's optional `"fighter_light": "01B"` (`setup/ffnycontent/
  newvenue.py`) gives the venue the `.irr` irradiance settings and character env map of another retail venue
  (01B = Hunt's Point after hours, night); the shadow proxy stays the template's. Knox and Jam is built on The Power
  Plant (03A) and otherwise keeps its daylight on the fighters. The official map's install.json should carry it.
- **Camera:** the follow camera always looks north (-Z) in Run the Streets: the map's north faces are left undrawn for
  it. With that heading the stick is already camera-relative (up = away from the camera). The "behind" camera and its
  turned stick (`ow_pad_read`, `ow_rules_stick_turn`) stay for other Big Arena fights.

## Free roam (walking without a target)

In the stock game a fighter always faces its target (record `+0x00`). Each step `sub_001A4A30` caches the target's
angle and distance (`+0x838`/`+0x834`, from the pair tables `0x3C04A8`/`0x3C04E8` that `sub_001B3EA0` fills) right
before the state handlers. The walker `sub_001CAC40` (state 1, `sub_001CB130`) turns the facing `+0x54` toward
`+0x838` (`sub_001B3E10`) and moves in the stick's *world* direction (nibble -> `sub_001BAB90` -> table `0x2EF510`:
up 180, right 90, down 0, left -90), picking forward, strafe or back by the difference to the latched angle `+0x3EC`.
The target changes only in two places, both from states 1 and 0x55: `sub_001C9610` (the target is down, `+0x932`
0x400: the nearest standing fighter) and `sub_001C99B0` (White/Black newly pressed: the next slot down/up). A
right-stick flick (`sub_001C9DF0`) enters state 0x55, a turn toward the target.

Free roam (`ow_roam_input`, at `sub_001BA800`'s entry, after the cache and before the handlers) rewrites the cache
of a **free** human fighter: the angle becomes the stick's heading (or its own facing when the stick rests) and the
distance far, so it turns to where it walks and walks forward. Its `+0x00` stays on the nearest standing fighter so
the game's retarget never turns it. Modes (per slot, in the Big Arena state, so saved, loaded and checksummed):

| From | To | When |
|---|---|---|
| free | engaged | a fight button (`+0x9F0` & 0x30F0) with a fighter within ~6 m (it becomes the target), or a hit given or taken (the other one) |
| free, engaged | locked | White/Black or a right-stick flick: the fighter best in front within ~26 m, else the nearest within ~12 m; the press goes on as a flick, so the game turns to it. Nobody: the press is dropped |
| locked | locked | White/Black: the game's own switch to the next fighter |
| engaged | free | `roam_seconds` (3) without a fight button or a hit |
| locked | free | White+Black together, or the target farther than ~40 m |
| locked | engaged | the target is knocked out |

CPU fighters keep the game's targeting. While engaged or locked a chevron hangs over the target (red: locked,
white outline: engaged; `rts_social_ui.inc`). `[openworld] free_roam` (on), `roam_seconds` (3), `roam_speed`
(100 %; `sub_001CAC40`'s speed argument while free); online always on, 3 s, 100 %. Log: `[OWROAM]`.

## Test switches

| Env | Effect |
|---|---|
| `RECOMP_RTS_ACCESS=1` / `0` | grant / refuse the access without asking the service (harness runs) |
| `RECOMP_RTS_VENUE=<feID>[,<code>]` | the map is this venue (no content.ini needed) |
| `RECOMP_RTS_TEST_FIGHT=1` | every offline fight on the map plays by Run the Streets rules (with an automatch) |
| `RECOMP_ROLLBACK_RTS=1` | rollback in RTS fights (every copy) |
| `RECOMP_RTS_VOICE_LOG=1`, `RECOMP_RTS_SOCIAL_TEST=1`, `RECOMP_RTS_TEST_*` | the social layer's logs and samples |
| `RECOMP_OW_*` | the Big Arena rules and probes (openworld.c) |

`tools/openworld/rts_content_root.py <out> <map.ffnypack> [--fighter-light 01B] [--bigarena <json>]` makes a data
folder with the map installed as an official RTS map; pass it to `dj.py` as `--content-root` (online: one per copy,
comma-separated). Online runs against the branch relay: `FFNY_SERVER_DIR=<server worktree> DJ_RELAY_ARGS=-rts-open
dj.py scenario online --quick 67,2 --mode ffa --players 2 ...`. Gallery pages: `hub-rts`, `rts`, `rts-online` and
the social layer's `rts-dock*`, `rts-bubbles`.
