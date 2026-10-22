# defjam-mcp

An MCP server that drives test copies of the game live, for Claude Code (registered in the
repository's `.mcp.json`). Copies run on their own data folders (`<data>-mcp<N>`, the saves copied
fresh), never report to the live account service, and are reached through the game's test-only
control channel on 127.0.0.1 (`RECOMP_CONTROL_PORT`, `src/hooks/test_control.c`).

| Tool | |
|---|---|
| `launch` | 1..4 copies; `online` adds a local relay and a quick-match room (`quick` = "mode,players"); `pads`; `fast_ko` (health percent at the start of the fight); extra `env` |
| `state` | screen, last front-end calls, match (phase, clock, type, fighters' health), online and lockstep state |
| `press` | press / hold / release controls on a virtual pad |
| `match` | start any match from the main menu (`type=0,venue=6,f0=58,p0=0,f1=40,p1=-1`) |
| `fe` | run a front-end command on the game thread |
| `peek` / `poke` | read / write guest memory |
| `screenshot` | the copy's window as a PNG |
| `log`, `wait_for` | the copy's log, filtered; wait for a line |
| `close` | stop everything this server started |

Build the game first (`scripts/build.ps1 -Preset win-x64-release`) and the local relay
(`go build -o netplay/relay/dist/relay.exe ./cmd/relay` in `netplay/relay`).
