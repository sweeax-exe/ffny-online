# Why fight pictures do not repeat: the two crowds

2026-10-06. Measured on Release, the Foundation, One on One, fixture `vy2-hour-v1`,
with step-timed input and `--rng-seed 12345`. Addresses are of the user's own dump.
Confidence is stated per claim; nothing here is fixed yet.

## Short answer

A pinned fight's fighters, HUD, arena and camera repeat pixel for pixel; the crowd
does not, and the crowd is two separate systems. Both start animating while the
match loads, and both come out of that window in a slightly different state on
every run although every generator they use is seeded identically. The foreground
spectators also push fighters, so the difference reaches the fight itself.

## How it was measured

`RECOMP_TEST_DRAWS=1` counts every draw from a random generator by generator and
calling site (`[TEST-DRAWS]`, reported at match setup and at fight steps 1, 300 and
900), with the game's own count of screen updates (`0x340538`) and the number of
background-crowd updates. Two runs of the same pinned fight are compared.

## What two runs agree on (measured)

- The C runtime's `rand` is drawn twice before match setup in both runs, so its
  state at setup is the same.
- Every generator seeding logged by `[TEST-SEED]` is identical, including the one at
  `0x375F68` (seeded at setup with the constants the match generator starts from).
- 60 draws of `rand` from `sub_000DB2D0` (return address `0xDB592`) in both: the
  background crowd's members are set up the same way.
- From match setup to the first fight step the game makes **906 screen updates in
  both runs**, and from then on exactly one per fight step. The loading window is
  the same length in game time.

## What differs (measured)

- **Background crowd.** `sub_000DB870` (gated by the byte at `0x2E3488`) calls
  `sub_000DB730`, which for each member (`this+0x11C` four-byte entries at
  `this+0x124`) calls `sub_000DB660`: advance a frame number and jump at random with
  a percentage chance from `rand`. It was called 902 times in the window in one
  run and 903 in the other, then once per step in both. `rand` draws from it by the
  first step: 231 against 221 in one pair.
  (Correction, 2026-10-27, measured on a Steam Deck: the update runs once per outer
  loop iteration, which is per render, not once per screen update. An iteration that
  runs two steps skips it and one that runs none still runs it: 862 or 863 calls over
  873 updates. Separately, the foreground spectators do reach the fight: the flag
  set by `sub_001B8760` for a fighter close to a spectator slot changes what the CPU
  fighter plans.)
- **Foreground spectators** (3D figures near the camera). `sub_0007D2F0` picks the
  next animation by name with a random variant, drawing from the generator at
  `0x375F68` through `sub_000AA340` and `sub_000AA300`. Draws by the first step:
  313 and 18 against 312 and 13 at its three sites.
- **The fight.** In three pinned pairs the state stream parted at step 871, and in a
  fourth near step 2,100. The first field to differ was the CPU fighter's position.

## Readings (inferred)

- One screen update more or fewer of the background crowd in the window is enough:
  every later draw is shifted. Why the count varies by one is not known; the update
  may be tied to a presented frame in a window where presents and updates are not
  one to one.
- The spectators' differences in a window of fixed length in game time point at
  something host-timed inside it, most likely animation data arriving from disc at
  a different update. Not tested.
- Spectators shove fighters who come close. A crowd in a different state therefore
  moves a fighter differently sooner or later; this is the likely cause of the
  divergence at step 3,121 recorded for v0.2.3. Likely, not shown: no run has
  isolated a shove.
- The `replay` check's thirty seconds can therefore fail by chance. It has passed
  every time it has run; step 871 is inside its window.

## What was tried

| Attempt | Result |
|---|---|
| Clear `0x2E3488` during the window, restore at the first step | Crash in the graphics library at the first step: the gated call also sets up what the crowd's drawing needs. |
| Copy the background crowd's entries and the `rand` state at setup, put them back at the first step | No effect on the pictures: the spectators still differed, so this was not a test of the background crowd alone. |
| Put them back at every update (hold the background crowd's pose) | Pictures still about 10 percent different: the spectators. |

None of these is in the code. Only the counting probe was kept.

## What a fix needs

The spectators cannot be copied and restored: they are full actors. The window has
to be made repeatable instead: whatever arrives from disc during it must arrive at
the same screen update on every run (for example by making those loads complete
before the next update in test mode), and the background crowd's update must run
the same number of times. Then compare `[TEST-DRAWS]` at step 1: equal counts at
every site are the test that it worked.

Until then: compare the game-state stream rather than pictures, keep stream
comparisons short, and expect an occasional `replay` failure with the CPU fighter
near the crowd.
