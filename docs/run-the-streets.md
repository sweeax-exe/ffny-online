# Run the Streets (open-world mode, closed test from build 86)

A free-for-all on a whole night-time New York block: up to four fighters, a knocked-out fighter comes back on a street
corner after three seconds, the last fighter who hit them scores the knockout, and the first to the KO target (or the
leader when the time limit is up) wins. Build 87: the time limit (5, 10, 15 minutes or none) and the KO target (5, 10,
20) are the room host's choice, VS CPU's on its set-up; the defaults are 10 minutes and first to 10. The rules are the Big Arena of `src/hooks/openworld.c`; the door, the map and
the access are `src/hooks/rts.c`; the social layer (chat bubbles, proximity voice, the docked chat) is
`src/hooks/rts_social*.c`. Server side: the private server repo, `docs/run-the-streets.md` there.

## How a player gets in

- **Access.** Only accounts the account service grants `run_the_streets` see anything (`GET /api/features`, by
  Discord role: `-rts-role` "FFNY Team" or `-rts-extra-role` "Run the Streets Tester"; the server owner and pack
  admins always). Signed out or without access the mode is completely hidden. The game asks in the background, keeps
  a yes 5 minutes and a no 1 minute, and asks again when the account signs in or out.
- **The door.** ONLINE (main menu) > **RUN THE STREETS** (after RANKED MATCH; only with access):
  - **VS CPU**: you and 2 or 3 CPU fighters, offline; A opens its set-up (CPU FIGHTERS, TIME LIMIT, KO TARGET,
    left/right on each, kept in `[openworld] rts_time_limit` / `rts_ko_target`), then CHOOSE FIGHTER picks the
    fighter from the hub's fighter grid and the fight starts. Four fighters play Free for All, three play 3-Way (a Free for All of two or
    three crashed at set-up on a new venue, `sub_001BBC10+0x80`; a One on One against one CPU ran 5 minutes without
    a knockout, so it is not offered). Before build 87 the rules were `[openworld]` (score_limit 5, respawn_seconds 3, time_limit 300);
    those settings now apply only to the other Big Arena fights. Fights stay Run the Streets through rematches until the main menu shows again.
  - **QUICK MATCH** / **CREATE A ROOM** (up to four, free places filled by CPU fighters) / **FIND A ROOM**: online,
    a room of mode `0x43` on this relay. The relay checks the access itself and refuses with ERROR 8 and its own
    sentence, which the game shows.
- **Online rules are the room's** (build 87): CREATE A ROOM opens the room settings with TIME LIMIT and KO TARGET
  (the host may change them later with Y, ROOM SETTINGS); the room card shows them (`10 MIN - FIRST TO 10`). QUICK
  MATCH plays the defaults. 3 s respawn always; no PC's own settings. The fights stay in lockstep
  (`every_player_rolls_back` returns 0 for them; `RECOMP_ROLLBACK_RTS=1` on every copy lifts it for tests), and the
  Big Arena state plus a hash of the map's spawn points are in every rollback save and the checksum.

## Wire

| What | Value |
|---|---|
| Room mode | `NETPLAY_MODE_RTS` 0x43 (ROOM/CREATE/SETTINGS); START carries match type 3 (Free for All) |
| CAPS bit | `NETPLAY_CAP_RTS` = 1 << 24, both ways (bit 23 is the standard preference's `CAPS_ORIGINAL`) |
| Rollback class | `NETPLAY_RB_FFA`; the client keeps RTS fights in lockstep regardless |

## Round rules, the clock and the end (build 87)

The owner's first real test (build 86) ended after 5 minutes with everyone else going down at once: the time limit,
but it read as a bug. So:

- **One byte.** `openworld.h` `ow_rules_rts_*`: 0 = the defaults (10 minutes, first to 10), else
  `0x80 | time | ko << 2` (time 0..3 = 5, 10, 15 minutes, none; ko 0..2 = 5, 10, 20); anything else reads as the
  defaults. Online it is the room settings' rounds byte (CREATE / SETTINGS / ROOM), which the relay checks and sends
  unchanged in START; `netplay.c` takes START's byte as the fight's rules (`netplay_rts_rules`) and gives the game's own
  match rules 0 as before. Rooms are per build, so a build-86 game never shares a fight with one that reads it.
- **Lockstep safety.** The rules byte, the KO target, the time limit, how the fight ended and when are in the Big Arena
  state (`BA`), which every rollback save carries and the online checksum folds in, next to the spawn hash.
- **No time limit.** The KO target ends the fight; a fight nobody scores in still ends after 30 minutes
  (`OW_RTS_SAFETY_SECONDS`) like a time limit, so a room is never stuck.
- **The plate** (fight_hud_ui.inc): the round clock is its own block (TIME over m:ss, orange in the last minute, red
  and beating in the last 30 s; NO LIMIT without a time limit until the safety end's last minute). At 0:30 "30 SECONDS
  LEFT" slams in for 3 s.
- **TIME!** When the clock ends the fight, the leader is the winner (ties: fewer times knocked out, then the lower
  slot) and every fighter is held at rest and unhurt for 3 s (`ow_pad_hold`, `ow_health_change`) while TIME! shows
  (1.5 s) and then the winner; only then do the losers go down, last placed first, and the game's results follow.
  The KO target's end keeps its half second before the losers go down.
- **One clock.** The game's own fight clock (top centre, counting up; `sub_000B54E0`, the HUD's `text_time`) is left
  out in a Run the Streets fight, since the plate's clock counts down to the room's limit and two clocks running
  opposite ways confused the testers (`recomp_manual.c`; `RECOMP_RTS_GAME_CLOCK=1` keeps it). Every other fight draws
  it as before.
- **Coming back** (fixed in build 87). A knocked-out fighter is put at a spawn point after 3 s. Up to build 86 it
  stood up where its body lay instead: the record also keeps the place the running animation has carried the body
  to (`+0xC4` x, `+0xCC` z, `+0xD4` yaw, `sub_001A76A0`), and the first animation after the revive starts from there
  (`sub_001A85A0` flags 4 and 8). `ba_revive` now sets both, at the height the body lay at.

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
right-stick flick (`sub_001C9DF0`) enters state 0x55, a gesture at the target (it does not turn). Standing, the
walker turns the fighter toward its target by the target's joint position when it is more than 80 degrees off
(`+0x910` bits 0/1, `+0x81C`; cool-down bit 1 for 10 steps). Walking at 20 units or more from the target
(`+0x924` bit 4) the stock walker already faces the move direction (backward first when more than 120 degrees
off, for 24 steps); closer it strafes around the target.

Free roam (`ow_roam_input`, at `sub_001BA800`'s entry, after the cache and before the handlers) rewrites the cache
of a **free** human fighter: the angle becomes the stick's heading (or its own facing when the stick rests) and the
distance far, so it turns to where it walks and walks forward; standing, the game's idle turn toward the target (`+0x910`/`+0x81C`, by the target's joint) is held by its own cool-down (`+0x910` bit 1). Its `+0x00` stays on the nearest standing fighter so
the game's retarget never turns it. Modes (per slot, in the Big Arena state, so saved, loaded and checksummed):

| From | To | When |
|---|---|---|
| free | engaged | a fight button (`+0x9F0` & 0x30F0) with a fighter within ~6 m (it becomes the target), or a hit given or taken (the other one) |
| free, engaged | locked | White/Black or a right-stick flick: the fighter best in front within ~26 m, else the nearest within ~12 m; the press goes on as a flick, so the fighter gestures at it (state 0x55) while free roam turns it to face it (12 degrees a step). Nobody: the press is dropped |
| locked | locked | White/Black: the game's own switch to the next fighter |
| engaged | free | `roam_seconds` (3) without a fight button, a hit, or a standing fighter within ~4 m |
| locked | free | White+Black together, or the target farther than ~40 m |
| locked | engaged | the target is knocked out |

CPU fighters keep the game's targeting. While engaged or locked a chevron hangs over the target (red: locked,
white outline: engaged; `rts_social_ui.inc`). `[openworld] free_roam` (on), `roam_seconds` (3), `roam_speed`
(100 %; `sub_001CAC40`'s speed argument while free); online always on, 3 s, 100 %. Log: `[OWROAM]`.

## CPU fighters that stop fighting (build 88)

On the big map two CPUs that target each other could stand face to face, idle or blocking (states 1/2), for the rest
of a round; only a hit from someone else woke them (the game takes the attacker as the target on a hit). The game
changes a target only when it is down (`sub_001C9610`) or on the target buttons (`sub_001C99B0`). `cpu_idle_step`
(openworld.c): a CPU that stays within 6 units of one place in states 1/2 for 240 steps (4 s) takes the nearest
other standing fighter as its target (`+0x00`). The count is in the Big Arena state (`cpu_ax/az/still`), so it is
saved, loaded and checksummed. Log: `[BIGARENA] step N: CPU slot S stood still 4 s ...: target A -> B`.

## Nameplates and the minimap (build 88)

Overlay only (`rts_social_ui.inc`, from the view `rts_social.c` copies each frame; nothing reaches the fight):

- **Nameplates:** the room name over every other fighter's head (CPUs: "CPU"), small, fading out from 30 to 40 m
  from the camera's target; not over a fighter with a chat bubble; your own only with `nameplate_self`.
- **Minimap:** a rounded square at the picture's right, below the HUD's top right bar and score; centred on your
  fighter, up = the camera's forward (`rts_minimap_point`). The map's walls come from the collision planes (every
  upright plane's two farthest corners, copied once per fight: `rts_social_walls`); humans are dots in their slot's
  colour with their name (8 letters), CPUs small grey dots, fighters beyond the range small arrows on the rim, the
  speaker by a player who talks; you are an arrow along your facing.
- `[openworld] nameplates` (on), `nameplate_self` (off), `minimap` (on), `minimap_range` (70 m, 30..150).
- **Combat pings (build 91):** a fighter who lands or takes a hit (the damage hook `sub_001A5DD0`, both sides), or is in
  a grab (state 0x1E, 0x34, 0x35), gets a red dot with a pulsing ring on the minimap for 3 s: full for 1.2 s, then
  fading out (`rts_ping_alpha`, unit-tested); every new hit refreshes it. Beyond `minimap_range` it is a larger red
  arrow on the rim, pointing the way (the same clamp as the grey arrows). This PC's own fighter is not pinged. Only a
  time per slot in host memory (`rts_social_combat`), read by the overlay: no network data, nothing the fight reads,
  so lockstep and rollback do not see it. `[openworld] combat_pings` (on). Crew colours: not done (the mode does not
  carry crews yet).
- **A real map image later:** the pack would carry a top-down picture (e.g. `minimap.png`, north up, 1024 px) and
  its world rectangle (`"minimap": {"image": "minimap.png", "x0": .., "z0": .., "x1": .., "z1": ..}` in
  bigarena.json), rendered by the map generator / Studio from the same scene; the minimap would draw it under the
  dots, turned and scaled with the same `rts_minimap_point`.

## Test switches

| Env | Effect |
|---|---|
| `RECOMP_RTS_ACCESS=1` / `0` | grant / refuse the access without asking the service (harness runs) |
| `RECOMP_RTS_VENUE=<feID>[,<code>]` | the map is this venue (no content.ini needed) |
| `RECOMP_RTS_TEST_FIGHT=1` | every offline fight on the map plays by Run the Streets rules (with an automatch) |
| `RECOMP_RTS_TIME_LIMIT=<min>`, `RECOMP_RTS_KO_TARGET=<n>` | VS CPU's round rules (the `[openworld]` settings) |
| `RECOMP_RTS_TEST_SECONDS=<s>` | offline tests: the clock ends a Run the Streets fight after s seconds (never online) |
| `RECOMP_TEST_RTS_RULES=<min>,<kos>` | online tests: the host sets its Run the Streets room's round rules |
| `RECOMP_RTS_GAME_CLOCK=1` | keeps the game's own fight clock (top centre) in Run the Streets fights |
| `RECOMP_ROLLBACK_RTS=1` | rollback in RTS fights (every copy) |
| `RECOMP_RTS_VOICE_LOG=1`, `RECOMP_RTS_SOCIAL_TEST=1`, `RECOMP_RTS_TEST_*` | the social layer's logs and samples |
| `RECOMP_OW_*` | the Big Arena rules and probes (openworld.c) |

`tools/openworld/rts_content_root.py <out> <map.ffnypack> [--fighter-light 01B] [--bigarena <json>]` makes a data
folder with the map installed as an official RTS map; pass it to `dj.py` as `--content-root` (online: one per copy,
comma-separated). Online runs against the branch relay: `FFNY_SERVER_DIR=<server worktree> DJ_RELAY_ARGS=-rts-open
dj.py scenario online --quick 67,2 --mode ffa --players 2 ...`. Gallery pages: `hub-rts`, `rts`, `rts-online` and
the social layer's `rts-dock*`, `rts-bubbles`.
