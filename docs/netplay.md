# Online play

Two to four players, each with their own copy of the game and the same version of this port, fight
each other over the internet. A small relay server runs the lobby and forwards the inputs; it never
sees game data.

## What you need

- A relay server everyone can reach. One small Linux VPS is plenty (for example a Hetzner CX22):
  the relay uses a few MB of memory and a few kB/s per match. Setup steps:
  the relay's README in the private server repository (sweeax-exe/ffny-server). The default port is **27777/udp**.
- Both players: this port installed from their own dump, the same version.

## Playing: ONLINE in the main menu

The game's main menu has an **ONLINE** entry in its bottom-left slot (made by the setup from your own
copy of the game, `scripts/apt-patch.py`; the art and fonts of the online screens come from it too,
`scripts/online-art.py`). Choose it with A:

1. The online screens open over the menu and connect to the **server**: Europe (`play.defjam.online`),
   US East (`na.defjam.online`, Ashburn; "North America" until build 37) or US West
   (`west.defjam.online`, Hillsboro; build 38). **Automatic** (the default) asks every server for its
   ping when the screens open and takes the lowest, once per session; the chip at the bottom right shows
   the server, its ping and who is online, and **X** opens the picker (Automatic, Europe, US East, US
   West, each with its ping and players; a server that does not answer shows OFFLINE). The launcher's
   settings (Basics, Online) have the same choice; `[online] server` is `auto`, `eu`, `na` (US East),
   `west`, `all` or a relay's address (the `both` of builds 31..37 reads as `all`). Accounts, statistics,
   the leaderboard and updates are the same everywhere (they stay on the Europe server).
   **All servers** (build 31 "Both servers"; `[online] server = all`) lets the player decide whom to play:
   the hub connects like Automatic (the chip says ALL SERVERS and the server it is on), FIND A ROOM lists
   the rooms (King of the Hill too) of all three servers, the lowest ping first, each with its server and
   this PC's ping there, and joining a room on another server moves the hub there by itself (leave,
   connect, join by code).
   **QUICK MATCH searches in stages** (build 38, for every choice but a relay's own address; not King of
   the Hill): it starts on the home server (the chosen one; with Automatic and All the lowest ping) and
   waits there alone for 10 seconds; then, with Automatic and All, the other US server is searched too
   (US East and US West are about 65 ms apart; Europe has no such neighbour); from 30 seconds on every
   server is searched, a fixed choice too, so nobody waits forever. The room says where it searches
   ("Waiting for another player on US East and US West...") and a notice says when the search widens
   ("Looking on US West too...", "Looking on every server..."). A player waiting in a quick room of the
   same mode on a searched server is met there: of two such players the one whose room is younger moves
   to the older room's server (within 2 s of each other, the one on US West moves to US East or Europe,
   the one on US East to Europe), with a notice and the ping there; the older one moves only once the
   younger room has waited 40 s, so two players never move at once and swap. At most three moves per
   QUICK MATCH; the search time is kept across a move, and the next QUICK MATCH starts on the home
   server again.
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
inputs will feel delayed). Ranked play runs on the Europe server only: on US East and US West the RANKED
MATCH entry says so and switches over with A.

Modes offered online: One on One, 3-Way, Free for All (more follow once tested online). The
fight's first-time help cards (BLAZIN', weapon, KO) are skipped in online matches: they depend on
each player's save and would pause only one of the games.

## Watching a match (spectator mode)

In **FIND A ROOM**, RB (**WATCH**) on a room, or RB on the code screen, watches that room instead of
joining it (relays that take spectators; not King of the Hill or ranked rooms). The room shows
SPECTATING and the players' cards; the players see "WATCHING: <names>". Every match that starts while
the spectator is there is run on its PC from the players' inputs (no local fighter) and shown with a
"SPECTATING" notice; afterwards its game goes through the summary by itself and is back in the room
for the next match. START during the fight stops watching (then Quit in the pause menu); B leaves the
room. A match already running is watched from the next one. Design and limits:
`docs/research/spectator-mode.md`. Tests: `RECOMP_NETPLAY=spectate` (watch the first listed room, or
`RECOMP_NETPLAY_SPECTATE=<code>`), `RECOMP_TEST_READY_SPECTATORS=<n>` on the players (ready only while
n watch), `dj.py scenario online --spectators 1`.

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
  health); a mismatch is logged as `[NETPLAY] desync` and shown on the Online page. Since build 35
  the checksum message also carries the raw fields per fighter, the two pre-fight CLogic generators'
  hashes, the crowd hash, and the shared generator, the time scale and the match generator apart
  (`src/hooks/netplay_check.h`); they do not decide anything, but `[NETPLAY] desync fields:` then
  names each word that differs, here and there. Since build 35 every PC also seeds the shared
  generator (0x39C0E0) at the start of every online step (`RECOMP_NETPLAY_GEN_PIN=off` disables it
  for comparisons), so draws made outside the step on host timing cannot part the PCs.
- **Languages (build 44, upstream v0.13.0):** the game's own French (`[language] game = french`) and
  language mods change text, subtitles, fonts and menu art, not the fight. Two PCs with different
  languages play together; a French and an English copy stayed in step in the two-copy test. The game
  files' print sent with each START (`make_data_print`) hashes the fight files as the game opens them,
  a language mod's rebuilt copy included, so a mod that rebuilds one of those files shows the "game
  files differ" notice; one that only translates text archives does not. A mod's changes to
  `default.xbe`'s data sections (strings) are not part of the print.
- **Known limit:** the crowd is animated during loading on the host's own clock, and spectators
  can push fighters, so a long fight can still drift apart. The checksum reports it when it happens.

## Testing without a second PC

`RECOMP_NETPLAY=host|join`, `RECOMP_NETPLAY_SERVER=127.0.0.1` and `RECOMP_NETPLAY_CODE_FILE=<file>`
start a session without the launcher: the host writes its room code to the file and the guest
reads it. `RECOMP_NETPLAY=quick` (with `RECOMP_NETPLAY_FIGHTER=<id>`) enters the hub, quick-matches,
readies and votes by itself. Run a local relay (`go run ./cmd/relay` in `server/relay`) and two
copies of the game. Three or four copies also get `RECOMP_TEST_READY_PLAYERS=<n>`: a copy readies (or,
on the hill, answers a call) only while its room holds `n` players. The relay starts a room's vote as
soon as two are in and every one of them is ready, so without it the first two copies started their
match before the third joined, and that one opened a room of its own (`dj.py scenario online --players
3` and `scripts/netplay-hub-test.ps1 -Players 3` set it).

The server choice: `RECOMP_NETPLAY_SERVER=auto|eu|na|west|all` with `RECOMP_NETPLAY_SERVER_EU`,
`RECOMP_NETPLAY_SERVER_NA` and `RECOMP_NETPLAY_SERVER_WEST` naming local relays (one started with
`-test-lag 150ms` answers like a far one; a test sets all three, `127.0.0.1:9` for one it does not run, so
no copy reaches a live relay); `RECOMP_TEST_SERVER_SWITCH=<ms>,<eu|na|west|auto>` switches the server in
the lobby the way the picker does before quick-matching. `scripts/netplay-hub-test.ps1 -NaRelayPort 27858
-EuLagMs 150 -Server auto` runs it end to end (`-WestRelayPort 27859` adds US West); `relayprobe -mode
status` asks one relay for its `STATUS`.

All servers: `RECOMP_NETPLAY_SERVER=all`; `RECOMP_TEST_JOIN_LISTED=<s>` makes a `quick` copy join the
first open room its room list shows instead (moving to another relay when the room is there).
`scripts/netplay-hub-test.ps1 -NaRelayPort 27858 -NaLagMs 150 -Servers na,all -JoinListed 1` has a copy
on US East open a room that a copy on All finds and joins; `-Servers na,eu -JoinListed 1` shows that a
copy on Europe never lists US East's room. The staged quick match: `-NaRelayPort 27858 -WestRelayPort
27859 -Servers na,west` starts the copies on US East and US West, and the younger one moves to the other
after 10 s (`[NETPLAY] tests: quick match searching on ...` and `moving the quick match to ...`);
`-Servers eu,west` meets after 30 s. `relayprobe -mode roomlist` asks one relay for its `ROOMLIST`.
