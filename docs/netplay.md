# Online play

Two to four players, each with their own copy of the game and the same version of this port, fight
each other over the internet. A small relay server runs the lobby and forwards the inputs; it never
sees game data.

## What you need

- A relay server everyone can reach. One small Linux VPS is plenty (for example a Hetzner CX22):
  the relay uses a few MB of memory and a few kB/s per match. Setup steps:
  [netplay/relay/README.md](../netplay/relay/README.md). The default port is **27777/udp**.
- Both players: this port installed from their own dump, the same version.

## Playing: ONLINE in the main menu

The game's main menu has an **ONLINE** entry in its bottom-left slot (made by the setup from your own
copy of the game, `scripts/apt-patch.py`; the art and fonts of the online screens come from it too,
`scripts/online-art.py`). Choose it with A:

1. The online screens open over the menu and connect to the **server**: Europe (`play.defjam.online`)
   or North America (`na.defjam.online`). **Automatic** (the default) asks both for their ping when the
   screens open and takes the lower one, once per session; the chip at the bottom right shows the server,
   its ping and who is online, and **X** opens the picker (Automatic, Europe, North America, each with its
   ping and players; a server that does not answer shows OFFLINE). The launcher's settings (Basics,
   Online) have the same choice. Players meet only players on the same server; accounts, statistics,
   the leaderboard and updates are the same on both (they stay on the Europe server).
   **Both servers** (build 31, `[online] server = both`) lets the player decide whom to play: the hub
   connects like Automatic (the chip says BOTH SERVERS and the server it is on), FIND A ROOM lists the
   rooms (King of the Hill too) of Europe and North America, the lower ping first, each with its
   server and this PC's ping there, and joining a room on the other server moves the hub there by
   itself (leave, connect, join by code). QUICK MATCH searches on the lower-ping server; alone there
   for 20 seconds while the other server has a player waiting in a quick room of the same mode, it
   moves over with a notice (the player chose Both, so a higher ping is fine). Of two such players
   the one whose room is younger moves (within 2 s, the one on North America); the other moves only
   once that room has waited 35 s, so a player there who is not on Both is still met and two players
   on Both never swap. The other choices keep the hint below and never move by themselves.
2. **QUICK MATCH** (left/right picks the mode) puts you straight into an open room of that mode, or
   opens one that the next players are put into. **FIND A ROOM** lists the open rooms; **CREATE A
   ROOM** sets the mode, the players, the venues to vote on and whether the room is listed.
3. In the room press X to **choose your fighter** from the roster (the game's portraits) and A to
   get **READY**; the host can change the room settings with Y while nobody is ready.
4. When everyone (at least two) is ready, three venues go to the **vote**: left/right and A, within
   15 seconds. The most votes win; a tie is decided at random.
5. The match then starts by itself on every PC. Modes with more fighters than players fill the free
   places with CPU fighters. After the summary press A: the room readies for the next one.

In a room every player's card shows their ping to the server (players of build 30 and later report it),
and a red line warns when a match would have a high ping (above 150 ms between the two slowest players:
inputs will feel delayed). Alone in a quick-match room for 30 seconds while the other server has players,
the room offers to search there (LB, or a click); it never switches by itself. Ranked play runs on the
Europe server only: on North America the RANKED MATCH entry says so and switches over with A.

Modes offered online: One on One, 3-Way, Free for All (more follow once tested online). The
fight's first-time help cards (BLAZIN', weapon, KO) are skipped in online matches: they depend on
each player's save and would pause only one of the games.

## Playing a friend

Create a room that is not listed ("By code only") and tell your friend the code shown in the room;
the friend joins it from FIND A ROOM by code, or from the room list if you leave it listed.

**Input delay**: each press takes effect this many steps (1/60 s each) later on every screen. In a
hub match it is set by itself: after `START` every PC sends the others its ping to the server
(worst of the last five), and all derive the same delay from the two slowest players, half of
each round trip plus one step, between 2 and 10. A code match uses the host's setting.

## How it works

- **Menus:** each PC streams its player's controller to the other about 100 times a second; the
  other PC shows it as the second controller.
- **Hub matches:** the server's `START` names the mode, venue, players and fighters and a random
  seed; each PC replays the front end's own match-setup calls with them (`src/hooks/automatch.c`).
- **Fights:** every simulation step uses, for every human fighter, the controller entry their player's PC
  captured `delay` steps earlier. Every random generator is seeded from the session instead of the
  clock. Every 60 steps the PCs compare a checksum of both fighters (state, position, facing,
  health); a mismatch is logged as `[NETPLAY] desync` and shown on the Online page. Since build 34
  the checksum message also carries the raw fields per fighter, the two pre-fight CLogic generators'
  hashes and the crowd hash (`src/hooks/netplay_check.h`); they do not decide anything, but
  `[NETPLAY] desync fields:` then names each word that differs, here and there.
- **Known limit:** the crowd is animated during loading on the host's own clock, and spectators
  can push fighters, so a long fight can still drift apart. The checksum reports it when it happens.

## Testing without a second PC

`RECOMP_NETPLAY=host|join`, `RECOMP_NETPLAY_SERVER=127.0.0.1` and `RECOMP_NETPLAY_CODE_FILE=<file>`
start a session without the launcher: the host writes its room code to the file and the guest
reads it. `RECOMP_NETPLAY=quick` (with `RECOMP_NETPLAY_FIGHTER=<id>`) enters the hub, quick-matches,
readies and votes by itself. Run a local relay (`go run ./cmd/relay` in `netplay/relay`) and two
copies of the game.

The server choice: `RECOMP_NETPLAY_SERVER=auto|eu|na` with `RECOMP_NETPLAY_SERVER_EU` and
`RECOMP_NETPLAY_SERVER_NA` naming two local relays (one started with `-test-lag 150ms` answers like a far
one); `RECOMP_TEST_SERVER_SWITCH=<ms>,<eu|na|auto>` switches the server in the lobby the way the picker
does before quick-matching. `scripts/netplay-hub-test.ps1 -NaRelayPort 27858 -EuLagMs 150 -Server auto`
runs it end to end; `relayprobe -mode status` asks one relay for its `STATUS`.

Both servers: `RECOMP_NETPLAY_SERVER=both`; `RECOMP_TEST_JOIN_LISTED=<s>` makes a `quick` copy join the
first open room its room list shows instead (moving to the other relay when the room is there).
`scripts/netplay-hub-test.ps1 -NaRelayPort 27858 -NaLagMs 150 -Servers na,both -JoinListed 1` has a copy
on North America open a room that a copy on Both finds and joins; `-Servers na,both` alone has the Both
copy quick-match on Europe and move to the North America copy after 20 s; `-Servers na,eu -JoinListed 1`
shows that a copy on Europe never lists North America's room. `relayprobe -mode roomlist` asks one relay
for its `ROOMLIST`.
