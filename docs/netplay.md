# Online play

Two players, each with their own copy of the game and the same version of this port, fight each
other over the internet. A small relay server pairs them; it never sees game data.

## What you need

- A relay server everyone can reach. One small Linux VPS is plenty (for example a Hetzner CX22):
  the relay uses a few MB of memory and a few kB/s per match. Setup steps:
  [netplay/relay/README.md](../netplay/relay/README.md). The default port is **27777/udp**.
- Both players: this port installed from their own dump, the same version.

## Playing

1. Both open the launcher and go to **Online**. Enter the server (`your.server` or `1.2.3.4`, or
   `host:port` for another port) and your name.
2. One player selects **Host a match** and reads out the six-letter room code. The other enters it
   and selects **Join**. When the status turns green ("Connected to ..."), both press **PLAY**.
3. The host is player 1 and the guest player 2 on both PCs. In the menus each of you sees the
   other's controller, so go to **Battle - One on One** together: both press START, each picks a
   fighter with their own pad, the host picks the venue.
4. The fight runs in lockstep: both PCs simulate the same fight from the same inputs.

**Input delay** (Online page, the host's value is used): each press takes effect this many steps
(1/60 s each) later on both screens. 3 (50 ms) suits players in the same country; raise it if the
fight stutters (the game waits whenever the other player's input is late).

## How it works

- **Menus:** each PC streams its player's controller to the other about 100 times a second; the
  other PC shows it as the second controller.
- **Fights:** every simulation step uses, for both fighters, the controller entry their player's PC
  captured `delay` steps earlier. Every random generator is seeded from the session instead of the
  clock. Every 60 steps the PCs compare a checksum of both fighters (state, position, facing,
  health); a mismatch is logged as `[NETPLAY] desync` and shown on the Online page.
- **Known limit:** the crowd is animated during loading on the host's own clock, and spectators
  can push fighters, so a long fight can still drift apart. The checksum reports it when it happens.

## Testing without a second PC

`RECOMP_NETPLAY=host|join`, `RECOMP_NETPLAY_SERVER=127.0.0.1` and `RECOMP_NETPLAY_CODE_FILE=<file>`
start a session without the launcher: the host writes its room code to the file and the guest
reads it. Run a local relay (`go run ./cmd/relay` in `netplay/relay`) and two copies of the game.
