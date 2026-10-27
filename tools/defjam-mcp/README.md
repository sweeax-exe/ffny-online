# defjam-mcp v2

Game tests for agents, as an MCP server (`server.py`, registered in the repository's `.mcp.json`)
and as a CLI with the same features for Bash (`dj.py`; subagents often have no MCP tools). Both
use `djlib/`. The point is to spend minutes and a few hundred tokens on a game test instead of hours
of wrapper scripts and log reading: every scenario answers with one compact JSON result, every log
with a digest of at most about 60 lines.

## Safety, built in

- **Machine lock.** Every launch waits until `D:\DefJam\test-lock.txt` is absent, until
  `D:\DefJam\test-priority.txt` is absent or names this agent (first word), and until no
  `defjam_recomp.exe` runs (the owner's own game included). It then writes the lock (agent, time,
  pid, process start time) and removes it on close, at exit, and when a later waiter finds its
  process gone (a lock written by hand, without a `pid=` line, is never removed by the tool). A job
  object ends the copies and relays with the tool, however it ends. The agent name is
  `DEFJAM_AGENT`, else the checkout's folder (a worktree's `agent-...`).
- **No dialogs.** A copy is not started unless its exe, the checkout's `game\default.xbe` and its
  data root's dump are there, so the game's "game files were not found" box never appears.
- **Safe environment.** No inherited `RECOMP_` switches; the account service and both relays on
  `127.0.0.1:9`, no update check, the intro movies skipped, a background window, one guest CPU per
  copy (2, 4, ...), telemetry on. `RECOMP_GUEST_CORES=all` and any non-local server address are
  refused.
- **Disposable data roots** (`D:\DefJam\data-dj-<agent>-<name>`): junctions to `extracted`,
  `online-art` and `override` (read only), the small save files copied (never the cache partition
  images unless asked), optionally a save fixture copied over them. They are deleted at the end:
  junctions first with `cmd /c rmdir`, then a check that the dump is intact, then the rest. Nothing
  is ever written into `<data>` itself.

Logs and pictures go to `logs/dj/<stamp>-<scenario>/` (git-ignored).

## CLI (`python tools/defjam-mcp/dj.py ...`)

| Command | |
|---|---|
| `scenario online --matches 3 [--mode 1v1\|ffa\|team\|koth] [--players 2] [--fighters 55,22] [--rollback on\|off] [--fast-ko 600] [--bot [p1=aggressive,p2=counter]]` | a local relay and 2..4 copies in one quick-match room, N room matches |
| `scenario offline [--type 0] [--venue 1] [--f0 55] [--f1 22] [--human] [--bot] [--fast-ko 600] [--fixture DIR]` | one match from the main menu by automatch, fought to the summary |
| `scenario boot [--goto battle]` | to the main menu (boot time, faults, a picture); optionally on to a menu entry by name |
| `scenario gallery [--page-ms 1500]` | the online screens' sample pages, one PNG each |
| `digest <log or logs/bug-reports/<id>> [--max 60]` | the key events of any game log, also a player's F9 report |
| `analyze <copy0 log> <copy1 log> [--matches N]` | the online result of logs from another run (e.g. `netplay-hub-test.ps1`) |
| `lock status` / `lock run -- <command>` | who holds the machine / run any other test under the lock |
| `roots list\|create NAME\|destroy PATH\|clean`, `cleanup` | managed data roots; stray copies of this checkout |

Exit code 1 on any failure. Other options: `--timeout`, `--lock-timeout`, `--keep-going` (online:
past a desync), `--require-hits`, `--keep-roots`, `--env NAME=VALUE` (checked), `--turbo`.

### Examples

```
$ python tools/defjam-mcp/dj.py scenario online --matches 3
{
  "scenario": "online",
  "ok": true,
  "params": {"matches":3,"mode":"1v1","players":2,"fighters":[55,22],"rollback":"off","fast_ko":600,"bot":null},
  "elapsed_s": ...,
  "matches": [
    {"n":1,"start":"lockstep d2","venue":12,"load_s":[25.3,25.0],"fe":["menu","menu"],"hits":[3,3],"winners":[0],"ko":true,"checks":10,"summary":["cid 0,1","cid 0,1"]},
    {"n":2,"start":"lockstep d2","venue":18,"load_s":[0.2,0.8],"fe":["ok","ok"],"hits":[3,4],"winners":[1],"ko":true,"checks":20,"summary":["cid 0,1","cid 0,1"]},
    ...
  ],
  "problems": [],
  "logs": [...], "shots": [...]
}
```

Per match: `start` (lockstep or rollback, input delay), `load_s` per copy, `fe` the front-end order
per copy (`menu`: from the main menu; `ok`: on the match-type screen after its own
`Game.ResetMatchData()`; `BAD: ...` otherwise), `hits` landed per slot (telemetry: an attack and
damage to its target within 40 steps), `winners`, `ko` (the test knockout fired), `checks`
(checksums compared and equal), `summary` (the controller ids `cid` of `GetMatchSummary`, and
`NOT accepted` without `UpdateMatchRewardsPoints`), `bot` (what the bot tried, below). Problems
name the copy and match: a desync with its detail line, gave-ups, crashes (`[CRASH]` line), a
match that never started, a summary not accepted, controller ids of -99, disagreeing winners.

```
$ python tools/defjam-mcp/dj.py scenario offline --type 0 --venue 1 --f0 55 --f1 22
{
  "scenario": "offline",
  "ok": true,
  "params": {"type":0,"venue":1,"f0":55,"f1":22,"cpu":true,"bot":null,"fast_ko":600,"turbo":false},
  "times_s": {"menu":28.5,"start_game":28.5,"fight":39.1,"result":51.9,"summary_done":62.0},
  "fight": {"characters":[9,10],"hits":{"0":2},"result":{"winners":[0],"code":33,"step":781,"time_up":false},"fight_s":13.0,"ko":true,"summary_cids":{"0":0,"1":-2},"rewards":true,"bot":[]},
  "problems": [],
  "log": "logs\\dj\\20261028-002325-offline\\copy0.log.err",
  "shots": ["logs\\dj\\20261028-002325-offline\\fight.png", "logs\\dj\\20261028-002325-offline\\end.png"],
  "elapsed_s": 64.2, "lock_wait_s": ..., "dir": "logs\\dj\\20261028-002325-offline"
}
```

`characters` are the telemetry's character numbers (not fighter.xml ids); a CPU's summary `cid` is -2.

```
$ python tools/defjam-mcp/dj.py scenario boot --goto battle      # menu_s, faults, goto {"ok":true,"screen":"battle/cmtype.swf"}
$ python tools/defjam-mcp/dj.py scenario gallery                 # pages: N, dir: logs/dj/.../gallery
$ python tools/defjam-mcp/dj.py digest logs/bug-reports/20261027-033506.997-45ea37
report: id: 45ea37; kind: crash; build: 31; ...
14333 lines; 17 online matches; crash 1, watchdog 0, desync 0; tags: ...
screens (last 14 of 40): ...
  match: START type 0 venue 13 2p me P2, 877E980B lockstep d5 load 0.3s, pings 31/94/-1/-1 ms, 50 checks equal, winners 0x1, code 0x1
[NETPLAY] gave up at step 10451 waiting for the entries up to 10451 ...
[CRASH] Access violation at RIP=0x140D999BB, fault addr=0x10A3BB98C (read)
  in sub_001BBC10+0x8B | eax=... 
  callers (filtered, innermost first): sub_001B2480->sub_001BBC10 | sub_001B3490->sub_001B2480 | ...
```

The digest's callers are the crash block's stack scan filtered against the lifted C's call sites
(`scripts/guest-stack.py`'s cache; from the main checkout when a worktree has no lifted C).

## Faster tests

- **Where the time goes.** An offline scenario skips the intro movies, starts the match by
  automatch without the menus and knocks the loser out at fight step 600 (`--fast-ko`, ten
  seconds in): boot to the menu 28 s, load 10 s, fight 13 s, summary 10 s -- about 62 s from
  launch to the accepted summary. The fight runs on the guest's own timer (PTIMER, real time), so
  `--turbo` (no flip pacing, no vsync wait: `RECOMP_FLIP_HZ=0`, `RECOMP_PRESENT_VSYNC=0`) gains
  nothing measurable (61.1 s against 62.0 s, one run each) and is off by default; a faster-than-real
  fight would need a time scale on the toolkit's PTIMER. Online scenarios keep real pacing.
- **Bot behaviours** (`--bot p1=aggressive,p2=counter`, or one name for all; `RECOMP_TEST_BOT_PLAN`):
  balanced, aggressive, defensive, block, grapple, counter (blocks from the first step of the
  opponent's strike wind-up), mash, idle; all but idle mash strikes while held. Each match reports
  per driven slot what was tried against what came of it, e.g. `s0 counter: 3 strikes, 0 grabs,
  64 block steps, counters 9/11 wind-ups, 6 clean, held 1 broke 0`, next to the landed `hits` -- the
  numbers for comparing input delay online with offline. Not yet: Blazin moves and real reversals
  (their inputs are not mapped yet).

## MCP tools

`scenario` {kind, args} and `digest`, `analyze`, `lock_status` as above, plus live driving:

| Tool | |
|---|---|
| `launch` | 1..4 copies under the lock on disposable roots; `online` adds a local relay and a quick-match room (`quick` = "mode,players", `rollback`); `pads`; `ko_step`/`ko_winner`, `fast_ko`, `bot`; `fixture`; extra `env` (checked) |
| `state` | screen, last front-end calls, match (phase, clock, step, fighters), online and lockstep state |
| `press` | press / hold / release controls on a virtual pad |
| `to_menu`, `goto` | to the main menu; to a menu entry by name (story, battle, rewards, online, slotN) through the cursor the menu reports |
| `match` | start any match from the main menu (`type=0,venue=6,f0=58,p0=0,f1=40,p1=-1`) |
| `fe` | run a front-end command on the game thread |
| `inject` | pad-table bits for a fighter on exact fight steps (offline) |
| `save_state` / `load_state` | snapshot the fight at the next step and go back to it (offline; `[SNAP]` lines) |
| `peek` / `poke` / `memdiff` | guest memory |
| `screenshot` | the copy's own window as a PNG (never the desktop) |
| `log`, `wait_for`, `wait_state` | the copy's log, filtered; wait for a line or a state field |
| `close` | stop everything this server started, delete its roots, give the lock back |

Build the game first (`scripts/build.ps1 -Preset win-x64-release`) and the local relay
(`go build -o dist/relay.exe ./cmd/relay` in `netplay/relay`). Tests: `tests/unit/test_defjam_mcp_v2.py`.
