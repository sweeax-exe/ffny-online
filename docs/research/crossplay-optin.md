# Cross-play with Android: the PC player's opt-in

2026-11-12. Design (owner rule of the same day): cross-play between the PC game and contatg's
Android port is **off by default for every PC player**. A PC player meets Android players only
when **both** hold:

1. the player turned it on ("Play with Android players"), and
2. the relay's cross-play switch is on for that build (`updates/android-crossplay`, already in
   `server/relay/platform.go`).

Android players chose the port, so they need no opt-in of their own; the relay's switches
(`android-allowed`, `android-crossplay`) and the Android fingerprint salt (`ANDR` while cross-play
is off for the build) stay as they are. Ranked never mixes platforms (`ranked.go`, unchanged).

Nothing below is implemented yet: the client bit is only safe once the relay masks it in its CAPS
consistency check (step 1 of the rollout), and that is a change to the private server repository.
The deterministic math layer (`deterministic-math.md`) is the precondition for turning the switch
on for any build.

## Client (game repository)

**Setting.** `[online] crossplay_android`, `RECOMP_SETTING_BOOL`, default `0`, in the table in
`src/hooks/pc_settings.c` next to `rollback`:

```c
{ "online", "crossplay_android", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, 0,
  "Let quick match, rooms and King of the Hill pair you with players of the community Android port "
  "(by contatg) in builds that FFNY Online has tested for cross-play. Off: you only meet PC players. "
  "Ranked never mixes platforms. Takes effect the next time you go online." },
```

No restart flag: the value is read when a lobby session starts (the CAPS word is built then).

**Launcher.** `src/hooks/pc_ui.cpp`: label row `{"online", "crossplay_android", "Play with Android players"}`
in the label table (line ~255) and the key in the Online group (`settings_group("online", { "rollback",
"crossplay_android" })`, line ~2781). Help text under the toggle:

> Off by default. When on, you can be matched with players of the Android port once a build is
> cleared for cross-play. Your fights stay lockstep- and rollback-checked as always.

The in-game Online menu shows the same toggle (pc_ui's overlay reads the same catalogue row).
Optional, later: the hub's player list tags Android players `[A]` (the relay would add the platform
byte to PLAYERS/ROOM entries; old clients ignore trailing bytes there only if the length rules
allow it, so this needs its own CAPS bit and is not part of the opt-in).

**Wire.** One CAPS bit, sent only when the setting is on:

```c
#define CAPS_XPLAY_ANDROID 2097152u  /* 1 << 21: this PC player meets Android players where the relay allows it */
```

added to the `T_CAPS` word in `src/hooks/netplay.c` (the block at "King of the Hill: CAPS until the
relay answers"): `... | CAPS_HILL_CREATED | (setting_on ? CAPS_XPLAY_ANDROID : 0)`. Bits 0-20 are taken
(the highest is the relay's `CapRollbackFrames`, 1 << 20). CAPS is sent at the start of every lobby
session; a session without CAPS (no lobby, or an older client) counts as **not** opted in.

Why CAPS and not HELLO: HELLO's layout is length-coded (`buildNo`, content print, Android's platform
byte); a PC flag byte there would change the PC HELLO length, which the relay reads as "a platform
byte follows". CAPS is a free-form u32 that every lobby session already sends, and the relay already
keeps it per client (`c.caps`).

## Relay (private server repository, `server/relay`)

1. **Constant.** `platform.go`: `CapXplayAndroid uint32 = 1 << 21`.
2. **CAPS consistency check.** `client_check.go` compares a session's CAPS with its build's
   `OfficialCaps` row. The opt-in bit varies per player, so it is masked before the comparison:
   `expected == caps &^ CapXplayAndroid`. Without this, an opted-in player of an enforced build would
   be refused as an unofficial client. **This ships first.**
3. **Pairing.** `meets(a, b uint8, n uint32)` becomes a per-client rule:

   ```go
   // meetsClients: x and y may share a room, a quick match, a hill, a crew battle, a tournament
   // match or a spectator seat as far as platforms go.
   func (r *Relay) meetsClients(x, y *client) bool {
       if x.platform == y.platform {
           return true
       }
       if !platformKnown(x.platform) || !platformKnown(y.platform) || !r.crossplay(buildOf(x, y)) {
           return false
       }
       return optedIn(x) && optedIn(y)
   }
   // optedIn: Android needs no opt-in; a PC client must have sent CapXplayAndroid in its CAPS.
   func optedIn(c *client) bool { return c.platform != PlatformPC || c.caps&CapXplayAndroid != 0 }
   ```

   `poolOK(rm, c)` checks `c` against the room's opener (not only `rm.platform`), every member and
   every spectator with `meetsClients`. The quick-match / automatch queue, the hill queue, crew
   battles and tournament sets already go through `poolOK` (`platform.go` comment); any pairing that
   compares only fingerprints must call it too (audit: `lobby.go` queue matching, `koth.go`,
   `crew_server.go`, `tset.go`).
4. **Late changes.** A room's mix is fixed when players join; a PC player who is in a room with an
   Android player cannot have "not opted in" afterwards (CAPS is per session). If the cross-play
   switch is turned off while mixed rooms exist, running matches finish; new joins follow the switch.
5. **Answer.** Optional: `RelayCapsMsg` sets `CapXplayAndroid` when cross-play is on for the
   client's build, so the client can show "Android players can join (this build)" next to the toggle
   and grey it out otherwise.
6. **Logs and reports.** "lobby entered by" already logs the platform; add `xplay=on|off` for PC.
   Match reports carry both platforms (the Android integration plan, §5e).
7. **Tests.** `platform_test.go`: PC without opt-in never meets Android (room join, quick match,
   spectate, hill); PC with opt-in meets Android only while `android-crossplay` lists the build; two
   PC players meet whatever their opt-in; `client_check_test.go`: an enforced build's CAPS with and
   without the bit both pass, any other extra bit still fails.

## Rollout

1. Relay: mask the bit in the CAPS check, `meetsClients`, tests. Deploy (cross-play stays off: no
   `android-crossplay` file).
2. Game build N: the setting, the launcher toggle, the CAPS bit, the deterministic math layer
   (`LOCKSTEP_VERSION` 5).
3. Android build N from our tag `build-N` (same lifter, runtime, `recomp_manual.c`, patches, the
   deterministic layer, `-ffp-contract=off`), salted fingerprint, platform byte 1.
4. Determinism test for build N (`deterministic-math.md`, "Proving a build"): the vector check on
   his toolchain and device, then PC-vs-Android lockstep matches with zero `P_HASH` mismatches.
5. `echo N > updates/android-crossplay`: from now on Android build N sends the unsalted fingerprint,
   and opted-in PC players of build N can meet Android players.
