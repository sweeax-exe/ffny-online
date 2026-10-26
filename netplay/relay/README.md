# Netplay relay

A small UDP relay for online play of the Def Jam: Fight for NY port. It puts two to
four players into a room and forwards their packets to each other. It never runs the
game and never looks inside the packets: inputs and checksums are opaque payloads.

The wire protocol is the contract in [`docs/netplay-protocol.md`](../../docs/netplay-protocol.md)
(protocol version 1 with the lobby extension). This server implements it exactly; the
choices it makes where the specification leaves room are listed under
[Spec interpretations](#spec-interpretations) and [Lobby interpretations](#lobby-interpretations).

Two ways in:

- **Rooms by code (v1).** `HELLO` mode 0 creates a two-player room, mode 1 joins it by
  its code; both players get `PEER` about each other. Unchanged from the first version.
- **Lobby (extension).** `HELLO` mode 2 registers without a room. From there a client
  `LIST`s public rooms, `CREATE`s one (public or code-only, 2..4 players, a venue pool),
  `JOIN`s by code or `QUICK`-matches. In the room players `READY` with a fighter; when
  all (at least two) are ready the server draws up to three venues from the pool, the
  players `VOTE` for 15 s, and the server sends `START` (seed, mode, rounds, venue,
  fighters) to everyone. `DATA` then goes to every other member; when all have sent
  `OVER` (or after 30 minutes) the room is back in its lobby. `ROOM` carries the whole
  room state and is sent on every change and once a second while voting.

```
 player A (host, slot 0) --UDP--> relay --UDP--> player B (guest, slot 1)
```

- One static binary, standard library only, no state on disk, no configuration file.
- Default port **UDP 27777**.
- Logs one line per room event (created, joined, peer left, expired, closed) with the
  room code and an anonymised client id (salted hash of the address, new salt every
  start), plus a summary line every minute. Names, tokens and addresses are never logged.

## Layout

| Path | What |
|---|---|
| `protocol.go` | Message encoding/decoding (no state). |
| `protocol_status.go`, `status.go` | `STATUS`: version, load and quick-match players for a client choosing between relays (no token). |
| `protocol_lobby.go` | The same for the lobby messages (0x10..0x1B) and the `Settings` block. |
| `relay.go` | Room/client state machine. No sockets; the current time is a parameter, so it is tested with a fake clock. |
| `lobby.go` | The lobby part of the state machine: rooms of up to four, phases, voting, the lobby requests. |
| `server.go` | UDP loop: one goroutine reads the socket, one runs the 1 s timer; a mutex guards the state. |
| `cmd/relay` | The server binary. |
| `cmd/relayprobe` | A test client for checking a deployed server from a PC. |
| `deploy/defjam-relay.service` | Hardened systemd unit. |

## Build

Requires Go 1.22 or newer (developed with 1.27). No third-party modules.

Run the tests:

```sh
go test ./...
```

(`go test -race ./...` needs cgo and a C compiler; on a Windows machine without gcc,
run the tests without `-race`.)

Fuzz the decoders for a while (any target in `protocol_test.go` / `protocol_lobby_test.go`):

```sh
go test -run '^$' -fuzz FuzzDecodeLobby -fuzztime 15s .
go test -run '^$' -fuzz '^FuzzDecodeClient$' -fuzztime 15s .
```

Cross-compile the Linux server from any machine:

```sh
# bash / Git Bash / Linux / macOS
GOOS=linux GOARCH=amd64 CGO_ENABLED=0 go build -trimpath -ldflags "-s -w" -o dist/relay ./cmd/relay
```

```powershell
# PowerShell
$env:GOOS = "linux"; $env:GOARCH = "amd64"; $env:CGO_ENABLED = "0"
go build -trimpath -ldflags "-s -w" -o dist/relay ./cmd/relay
Remove-Item Env:GOOS, Env:GOARCH, Env:CGO_ENABLED
```

If `go` is not on `PATH`, call it by its full path, e.g. `& "C:\Program Files\Go\bin\go.exe" build ...`.
`dist/` is git-ignored.

Server flags:

| Flag | Default | Meaning |
|---|---|---|
| `-listen` | `:27777` | UDP address to listen on (IPv4 and IPv6). |
| `-max-rooms` | `512` | Open rooms; beyond that `HELLO` mode 0, `CREATE` and a creating `QUICK` get `ERROR 5`. |
| `-max-clients` | `1024` | Connected clients; beyond that `HELLO` gets `ERROR 5`. |
| `-log-interval` | `60s` | Summary line period; `0` disables it. |
| `-test-lag` | `0` | Tests only: hold every datagram the relay sends this long, so a local relay answers like one far away (the game's server choice tests). |

## Deploy on a fresh Hetzner Ubuntu 24.04 server

The smallest cloud server (e.g. CX22, 2 vCPU, 4 GB) is far more than enough. Pick a
location close to the players. Below, `SERVER` is the server's public IP.

1. **Update the system** (as root over SSH):

   ```sh
   ssh root@SERVER
   apt update && apt upgrade -y
   ```

2. **Create a system user** for the service (no home, no login shell):

   ```sh
   useradd --system --no-create-home --shell /usr/sbin/nologin defjam-relay
   ```

3. **Copy the binary and the unit** from your PC (Windows 10/11 ships `scp`), in
   `netplay/relay`:

   ```sh
   scp dist/relay deploy/defjam-relay.service root@SERVER:/tmp/
   ```

4. **Install them** on the server:

   ```sh
   install -d -m 0755 /opt/defjam-relay && install -m 0755 -o root -g root /tmp/relay /opt/defjam-relay/relay
   install -m 0644 -o root -g root /tmp/defjam-relay.service /etc/systemd/system/defjam-relay.service
   rm /tmp/relay /tmp/defjam-relay.service
   systemctl daemon-reload
   systemctl enable --now defjam-relay
   systemctl status defjam-relay          # should be "active (running)"
   journalctl -u defjam-relay -f          # live log; Ctrl+C to stop following
   ```

5. **Open the firewall.** Allow SSH *before* enabling ufw, or you lock yourself out:

   ```sh
   ufw allow OpenSSH
   ufw allow 27777/udp
   ufw enable
   ufw status
   ```

   If the server also has a **Hetzner Cloud Firewall** attached (console → Firewalls),
   add an inbound rule there too: UDP, port 27777, any source.

6. **Check it from a PC** with the probe (in `netplay/relay` on the PC):

   ```sh
   go run ./cmd/relayprobe -server SERVER:27777
   ```

   Pair mode (the default) opens two clients in one process and checks create, join,
   PEER, DATA both ways, PING/PONG, BYE, peer-left and ERROR 7 for a dead token. It ends
   with `PASS: relay at ... works` and exit code 0, or lists the failed checks and exits 1.
   `no WELCOME (server unreachable, or UDP blocked by a firewall?)` means nothing came
   back: check steps 4 and 5.

   To test between two machines:

   ```sh
   relayprobe -server SERVER:27777 -mode create            # prints a room code, waits for a peer
   relayprobe -server SERVER:27777 -mode join -room ABC234 # on the other machine
   ```

   Both sides print the PING round-trip time every second and every DATA that
   arrives. Other flags: `-n` (DATA packets to send, default 10), `-interval`,
   `-name`, `-build` (8-byte hex fingerprint), `-wait`, `-timeout`.

   To check the lobby extension:

   ```sh
   relayprobe -server SERVER:27777 -mode lobby
   ```

   Two clients enter the lobby (`HELLO` mode 2); A creates a public room of two, B
   finds it with `LIST` and joins it by code, both `READY`, both `VOTE` for the first
   candidate, and both must get the same `START` (same seed, the voted venue); then
   both send `OVER` and the room must be back in phase 0. Every request is retried
   every 500 ms like a real client. It prints every `ROOMS`/`ROOM`/`START`, one `ok`/`FAIL`
   line per check, and ends with `PASS: lobby at ... works` (exit 0) or `FAIL: ...`
   (exit 1). Against a relay without the extension it fails at the first step: such a
   relay drops `HELLO` mode 2 silently.

   To check `STATUS` (what the game asks every relay to pick the nearest; no HELLO, nothing
   registered), with the round trip and the player counts:

   ```sh
   relayprobe -server SERVER:27777 -mode status
   ```

   An older relay without `STATUS` never answers (`FAIL: ... did not answer STATUS`).

Optional check of the sandbox: `systemd-analyze security defjam-relay`.

## Update

Build a new `dist/relay` on the PC, then:

```sh
scp dist/relay root@SERVER:/tmp/relay
ssh root@SERVER 'install -m 0755 /tmp/relay /opt/defjam-relay/relay && rm /tmp/relay && systemctl restart defjam-relay'
```

All state is in memory: a restart ends every room. Clients get `ERROR 7` on their next
message and start over with `HELLO`. Restart when nobody is playing, or accept that.
Shutdown on `SIGTERM`/`SIGINT` (what `systemctl stop` sends) is clean.

## Resource needs

- **Memory:** a few MB (a few hundred bytes per client, room and tracked source IP; the
  per-IP table is capped at 100 000 entries, about 40 MB in the worst case, reached only
  under a spoofed flood). The unit caps memory at 128 MB.
- **CPU:** negligible. One core handles far more than 1024 clients at 120 messages/s.
- **Bandwidth:** each client may send up to 400 messages/s; a 60 Hz game with
  redundancy sends about 60-120. At 120 messages/s with ~80-byte payloads, one room
  costs about 30 KB/s in and 30 KB/s out (≈0.5 Mbit/s, ≈200 MB per hour of play).
  512 full rooms would be about 250 Mbit/s. Check the plan's included traffic (20 TB a
  month in Hetzner's EU locations at the time of writing, roughly 90 000 room-hours).
- **Disk:** the binary (≈2.5 MB) and the journal.

## Spec interpretations

The specification is the contract; where it left room, the server takes the simplest
reading below. None of these change the wire format.

1. **Joining a v1 room uses slot 1 only.** In a room made by `HELLO` mode 0, a join (by
   `HELLO` mode 1 or by `JOIN`) takes slot 1 when slot 1 is free.
   If the guest left, a new guest can join and the host gets a new `PEER`. If the
   *host* left, the remaining guest keeps slot 1 and the room answers `ERROR 2` (room
   full) to new joiners; the guest should `BYE` and create a new room. (Rooms made by
   `CREATE`/`QUICK` use the lowest free slot instead.)
2. **Repeated HELLO.** Any `HELLO` (mode 0, 1 or 2) from an address and port that
   already has a token is answered with the same `WELCOME` it got first, whatever its
   contents and wherever the client is now; if it is in a full room of two the `PEER`
   about the other player is sent again too (it may have been lost with the `WELCOME`).
   Such repeats do not count against the HELLO rate limit and refresh the idle timer.
   To change rooms a v1 client sends `BYE` first (a lobby client sends `LEAVE`).
3. **Check order for a new HELLO:** rate limit (`6`), name (`4`), server full (`5`),
   then for a join: room not found (`1`), room full (`2`), build mismatch (`3`).
4. **Names** must be 1..24 bytes of valid UTF-8; anything else is `ERROR 4`. A name
   length byte that disagrees with the datagram size is a malformed packet (dropped).
   Content is not otherwise filtered.
5. **Room codes.** The `room` field of a create is ignored (it need not be zeros). A join
   code is compared byte for byte; the server does not upper-case, so a code outside
   the alphabet is simply `ERROR 1`.
6. **Exact lengths.** `HELLO` is 22 + name bytes, `DATA` 15..1164, `PING` exactly 18,
   `BYE` exactly 14. Extra or missing bytes, `mode` other than 0/1/2, server-to-client
   types sent to the server, and datagrams over 1200 bytes are dropped silently. (The
   lobby messages' lengths and ranges are under [Lobby interpretations](#lobby-interpretations).)
7. **No reply larger than the request.** `ERROR` texts are informational and at most
   14 bytes, so `ERROR 1`-`6` always fit in the smallest `HELLO`. When the text would
   make the reply larger than the request, it is sent **empty**: `ERROR 7` in reply to
   `PING` (18 bytes) or `BYE` (14 bytes) always has an empty text. Clients must act on
   the code only.
8. **Per-IP limits.** "IP" is the IPv4 address, or the /64 prefix for IPv6 (an IPv6 host
   normally owns a whole /64); IPv4-mapped IPv6 counts as IPv4. HELLO: at most 10
   *new* HELLOs (including ones answered with `ERROR 1`-`5`) in any sliding 60 s. The
   400 messages/s limit is a fixed one-second window and counts every datagram,
   malformed ones included.
9. **Error rate limits.** `ERROR 7` is sent at most once per second per source IP (not
   per address and port). `ERROR 6` is likewise sent at most once per second per source
   IP; further limited HELLOs in that second are dropped silently.
10. **BYE** has no reply. A `BYE` with a wrong token is treated like any message with a
    wrong token (`ERROR 7`, rate-limited).
11. **Idle timeout.** Any message with a valid token (and a repeated `HELLO`) resets the
    15 s timer; messages with a bad token and malformed datagrams do not. Timers are
    checked every second, so removal happens 15-16 s after the last message. The
    peer-left `PEER` is sent once and not retried.
12. **10-minute expiry** counts from room creation and applies only to v1 rooms
    (`HELLO` mode 0) that never had a guest. The host is removed without a message (it
    gets `ERROR 7` on its next `PING`). A room whose guest joined and left does not
    expire while the host stays. Rooms made by `CREATE`/`QUICK` never expire: a public
    room may wait for quick-matchers as long as its host stays (idle timeout still applies).
13. **DATA while alone** in a room, or from the lobby, is dropped silently.
14. **On join** the server sends `WELCOME` to the guest, then `PEER` to the guest, then
    `PEER` to the host. UDP may reorder them.
15. **Source table cap.** The server tracks at most 100 000 source IPs (entries expire
    after about a minute of silence). If the table is full, datagrams from new IPs are
    dropped; this only matters under a spoofed flood.

## Lobby interpretations

Choices for the lobby extension where the specification leaves room. None change the
wire format; a C client written to the spec works with all of them.

1. **Who gets ROOM and START.** A client that entered with `HELLO` mode 2, or that has
   sent any lobby request (0x10..0x1B, including `LIST`), gets `ROOM` and `START`. A
   plain v1 client (mode 0/1, never a lobby request) never sees a message type it does
   not know, so v1 behaves exactly as before.
2. **PEER in rooms of two.** Every room whose `max` is 2 (v1 rooms and `CREATE`/`QUICK`
   rooms of two) sends `PEER` as v1 does: both ways when the second player joins, and
   `PEER` with an empty name to the remaining player when one leaves. Rooms of 3 or 4
   never send `PEER`. Lobby clients can ignore `PEER` and use `ROOM`.
3. **Requests are idempotent** (clients retry every 500 ms):
   - `CREATE`, `QUICK`, or `JOIN` of its own room code, from a client already in a room,
     changes nothing and is answered with a fresh `ROOM` of that room. Only `JOIN` of a
     *different* code while in a room gets `ERROR 9`.
   - Every accepted `CREATE`/`JOIN`/`QUICK`/`SETTINGS`/`READY`/`VOTE`/`OVER` is answered
     with at least a `ROOM` to the sender: a `ROOM` to every member if something changed,
     otherwise a `ROOM` to the sender alone (identical `SETTINGS` do not clear ready).
   - `LIST` always gets `ROOMS`, even with count 0.
   - `LEAVE` has no reply (the others get `ROOM`, and `PEER` in a room of two); a `LEAVE`
     from the lobby is ignored silently.
   - `VOTE` while playing (phase 2) re-sends the room's `START` and a `ROOM` to the
     sender, so a client whose `START` was lost recovers by voting again once `ms_left`
     reaches 0. `START` may therefore arrive more than once; the same seed means the same
     match.
   - `OVER` outside phase 2 (late or repeated) changes nothing and gets a `ROOM`.
4. **Errors.** `ERROR 9`: `READY`/`VOTE`/`SETTINGS`/`OVER` from the lobby, or `JOIN` of
   another room while in one. `ERROR 8`: `SETTINGS` from a non-host, outside phase 0, in a
   v1 room, or with a `max` below an occupied slot; `READY` outside phase 0; `VOTE` in
   phase 0 or for a candidate index that does not exist; `JOIN`/`HELLO` mode 1 into a
   room that is voting or playing. `ERROR 4`: a `Settings.name` that is not 1..24 bytes of
   UTF-8. `ERROR 5`: `CREATE`/`QUICK` when 512 rooms are open. Check order for `JOIN`:
   `9`, `1` not found, `2` full, `3` build mismatch, `8` wrong phase. As for v1, an
   `ERROR` text is dropped when it would make the reply larger than the request.
5. **Malformed lobby messages are dropped silently** (before any token check): wrong
   length (`LIST` 15 bytes, `JOIN` 20, `QUICK` 16, `READY` 16, `VOTE` 15, `LEAVE`/`OVER`
   14, `CREATE`/`SETTINGS` 14 + Settings), a `Settings` with `public` not 0/1, `mode`
   0xFF, `max` outside 2..4 or a pool of 0 or more than 32 entries; `QUICK` with `mode`
   0xFF or `max` outside 2..4; `READY` with `ready` not 0/1; `VOTE` with `choice` above 2.
6. **Build fingerprints.** A lobby client's build comes from its `HELLO`. `LIST` shows and
   `QUICK` matches only rooms of the same build; `JOIN` of another build is `ERROR 3`.
7. **LIST** shows public rooms in every phase (with their phase), full ones included,
   newest first, at most 30 (30 always fit in 1200 bytes). v1 rooms are not public.
8. **QUICK** only considers public phase-0 rooms of the same mode, `max` and build with
   a free slot; most players first, then the oldest. When it creates a room, `rounds` is 0
   and the pool is 1, 6, 19.
9. **Phase 0.** A player joins in the lowest free slot with ready cleared. Its `fighter`
   is 0xFF until its first `READY` and is kept afterwards (also across rooms); every
   `READY` sets it, whether `ready` is 0 or 1. The vote starts as soon as at least two
   players are present and all are ready: after a `READY`, but also after an unready
   player leaves. A host-only `SETTINGS` change clears every ready flag.
10. **Voting.** The candidates are up to 3 *distinct* values of the pool (duplicates in
    the pool count once), in random order; with fewer than 3 distinct venues there are
    fewer candidates. `votes` has one entry per candidate. `ROOM` goes to every member on
    each 1 s timer tick while voting (`ms_left` counts down from 15000), and on every
    vote. The vote ends when all present players have voted or 15 s have passed; the
    winner is the most-voted candidate, ties (including no votes at all) at random among
    the tied. `START` lists every member by slot with its fighter; then a `ROOM` in phase
    2 follows (UDP may reorder them). A player leaving or timing out during the vote
    returns the room to phase 0 with ready and votes cleared.
11. **Playing.** `DATA` is forwarded to every other member in every phase, not only phase
    2 (that keeps v1 rooms, which never leave phase 0, working). Clients should ignore
    `DATA` outside a match. Joining is refused in phases 1 and 2. When the last member
    that had not sent `OVER` leaves, the match also ends. Phase 2 lasts at most 30 min.
    After a match `candidates`, `votes` and every ready flag are cleared.
12. **Host** is the lowest slot present, recomputed on every change; nothing else moves.
13. **v1 rooms** (`HELLO` mode 0) have the settings: name = the host's name, public 0,
    mode 0, max 2, rounds 0, pool {1}. Lobby clients may `JOIN` them by code; their
    settings cannot be changed. `HELLO` mode 1 may join a `CREATE`/`QUICK` room by code
    (lowest free slot; the `WELCOME` carries that slot).
14. **Randomness.** Room codes, tokens, seeds, the candidate draw and tie breaks use
    `crypto/rand`. `Config.Intn` and `Config.Rand` can be replaced in tests.
15. **Detecting the extension.** An old relay drops `HELLO` mode 2 silently, so a client
    that gets no `WELCOME` to mode 2 within about 2 s should fall back to rooms by code
    (mode 0/1). A client that is already registered can probe with `LIST` as the spec
    says.
