# Online play

Two to four players, each with their own copy of the game and the same version of this port, fight
each other over the internet. A small relay server runs the lobby and forwards the inputs; it never
sees game data.

## What you need

- A relay server everyone can reach. One small Linux VPS is plenty (for example a Hetzner CX22):
  the relay uses a few MB of memory and a few kB/s per match. Setup steps:
  [netplay/relay/README.md](../netplay/relay/README.md). The default port is **27777/udp**.
- Both players: this port installed from their own dump, the same version.

## Playing: the online hub

1. Open the launcher (or the overlay in the game) and go to **Online**. The server is filled in;
   enter your name and select **GO ONLINE**.
2. Then either
   - **Quick match**: pick the mode and select **FIND A MATCH**. You are put straight into an open
     room of that mode, or a new one is opened that the next player is put into; or
   - **Open rooms**: the list of listed rooms with their mode and players; **Join** one; or
   - **Create a room**: name, listed or code-only, mode, players, and the venues to vote on. Friends
     can also join by its six-letter code.
3. In the room pick **your fighter** and select **READY**. The host can change the settings while
   nobody is voting (that clears everyone's ready).
4. When everyone (at least two) is ready, three venues from the room's list go to the **vote**:
   click one within 15 seconds. The most votes wins; a tie is decided at random.
5. The match then starts by itself on every PC from the main menu (press **PLAY** in the launcher
   if you are not in the game yet). Modes with more fighters than players fill the free places with
   CPU fighters. After the summary press A: you are back in the room for the next match.

Modes offered online: One on One, 3-Way, Free for All (more follow once tested online).

## Playing one friend by code

Under **Play one friend by code** one player selects **Host a match** and reads out the room code;
the other enters it and selects **Join**. When the status turns green both press **PLAY**. The host
is player 1 and the guest player 2 on both PCs; each sees the other's controller in the menus, so
go to **Battle - One on One** together and pick there.

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
  health); a mismatch is logged as `[NETPLAY] desync` and shown on the Online page.
- **Known limit:** the crowd is animated during loading on the host's own clock, and spectators
  can push fighters, so a long fight can still drift apart. The checksum reports it when it happens.

## Testing without a second PC

`RECOMP_NETPLAY=host|join`, `RECOMP_NETPLAY_SERVER=127.0.0.1` and `RECOMP_NETPLAY_CODE_FILE=<file>`
start a session without the launcher: the host writes its room code to the file and the guest
reads it. `RECOMP_NETPLAY=quick` (with `RECOMP_NETPLAY_FIGHTER=<id>`) enters the hub, quick-matches,
readies and votes by itself. Run a local relay (`go run ./cmd/relay` in `netplay/relay`) and two
copies of the game.
