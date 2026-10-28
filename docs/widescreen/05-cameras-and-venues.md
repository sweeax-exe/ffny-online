# 05 — Cameras, fights and venues

Sources: the [cameras report](../research/widescreen-game-cameras.md) (static reading; its "vertical FOV" premise is
wrong, see [01 §5](01-findings.md#5-errata-to-the-research-reports)) and the 16:9 / 21:9 runs in [01 §3](01-findings.md#3-the-experiments).

## 1. The fight camera (1v1, tag, 3-way, 4-way free-for-all)

What it does **[V]** (`sub_000AB550`, called per tick through `sub_000AC590`; the free camera `sub_000AAE00` and the special
camera `sub_000AC1F0` are the other two branches):

1. Builds an axis-aligned box around every active fighter (four slots from `0x340D70`, stride `0xC8`; a slot counts when
   bit 3 of `slot+0xC0` is clear). The same code serves 2, 3 and 4 fighters: more fighters only make the box bigger.
2. `radius` = half the 3-D diagonal of the box, clamped to 20..80 (15..80 when rules bit `0x3B907E & 1` is set).
3. Frame size `F` chases `1.95·radius` at 20% a tick (dead zone 0.25).
4. Distance `Hd = F / tan(fov/2) + offset − (F − minR)²/180`, with `fov`, `offset` and pitch blended between the venue's
   "low" and "high" tuning by the fighters' depth (`changepos`, ±5 units).
5. Eye = target + rotation by pitch of `(0, 0, Hd)`, eye height capped at 140; target follows the box centre (rates
   0.1125–0.25 a tick in X/Z), **no clamp to the arena, no yaw, no collision**.
6. The FOV goes to the view through `sub_00044400` → `sub_000190C0`.

Because the game's FOV is the **horizontal** angle at 4:3, step 4 fits the fighters' spread into the 4:3 width. Hor+
keeps that fit and the vertical view, and adds picture at the sides only. Measured: 1v1 at 16:9 and a 4-fighter FFA at
21:9 frame the fighters exactly as at 4:3 **[M]**.

**Rule:** do not touch `sub_000AB550`, `sub_00044400` or the venue FOV values for widescreen. Widen only in the view
(`sub_0001A4E0`, WS-2). Changing the FOV upstream would move the camera (the distance is computed from it).

### What the player gains

Visible half-width at the focus plane = `Hd·tan(fov'/2)` = (4:3 half-width) × `A/A0`:

| Aspect | Extra width per side vs 4:3 |
|---|---|
| 16:10 | +20% of the 4:3 half-width |
| 16:9 | +33% |
| 21:9 | +75% |
| 32:9 | +167% |

At the closest zoom (radius 20, `F = 39`) the 4:3 half-width is roughly `F` minus the offset term, about 30–40 world units
depending on venue tuning **[computed, A]**; the venue sweep (WS-9) measures it per venue.

### Free-for-all specifics

- Four HUD blocks occupy the four corners (P1 top-left, P2 top-right, P3 bottom-left, P4 bottom-right, timer top-centre)
  **[M]**. In `edge` mode they move to the screen corners; at 21:9+ (`auto`) they sit at the corners of a centred 16:9 box,
  which keeps them near the action.
- The FFA camera hits the radius clamp (80) more often, so more of the arena is visible already; Hor+ adds to that. The
  21:9 FFA run showed crowd and walls to the edges at Foundation with nothing missing **[M]**.
- Fairness is not a concern (one shared camera, local play).

### Special match types

Match types (`0x2DBCC8` table): Single, Tag, 3Way, 4Way, 1vs2, 1vs3, Demolition, Inferno, Subway, Window, Cage, Ringout,
Tutorial. All use the same camera; Inferno (07A), Subway (04A), Window, Cage and Ringout venues have set pieces near the
edges (fire, train, window, cage walls) and are in the high-priority half of the sweep.

## 2. Special cameras (finishers, environment attacks, Blazin' moves)

`motion/camera.xml`: 559 timetables matched by move name; orbit cameras around a joint at radius 40/50/55, fixed 45° FOV
(narrowing by a per-tick rate), pitch presets, line-of-sight ray test only (`sub_001AC220`) **[V]**. Hor+ widens them like
any full-screen view (they are mode 0). Close shots: the subject stays centred; the sides show more set or crowd. Expect a
few shots to reveal set edges; WS-9 spot-checks five common ones (`C5` midpoint aims, `S`/`M` radii) in a small and a large
venue. Mitigation if needed: a special-camera tag (camera flag bit 0, `sub_000AC1F0` entry) that caps the 3D aspect to 16:9
for the shot.

## 3. Fight intros and outros

`sub_000A2EA0` (IntroExtro: entrances, victory) samples authored camera animations (`CAM_EYE`/`CAM_TARGET`, FOV channel)
through `sub_00070DF0`/`sub_00070FC0` **[V]**. They are full-screen mode 0 views, so they widen. Risk: fighters walking in
from off-screen become visible earlier; extras staged at the 4:3 edge may show. Check three venues' intro and outro shots in
WS-9; mitigation: tag the IntroExtro update (entry/exit hooks on `sub_000A2EA0`) and apply the cutscene policy (authored
framing in a 16:9 box) to it if any shot looks wrong.

## 4. Story cameras

| Camera | Mode | Treatment |
|---|---|---|
| CINEMA cutscenes (`sub_00078A40` → `sub_0005BE50` → `sub_00070DF0`) | 1 (16:9 letterbox) | Cutscene policy (WS-3). Verified at 16:9. |
| Crib, gym, shop hub cameras (`assets/spycam/*.spy`, `@default.scam`, `SetAnchorCam`) | 0, full screen | Hor+ (verified in the crib and gym). |
| Shop cinema cam (`SHOP::Cin Cam`, `v%d_eyecam`) and tattoo camera | 0 | Hor+; panels centred. Check one shop of each kind. |
| Create-a-player and menu previews (`SetCPViewportCam`, `SetSingleCPViewportCam`) | 0/2 sub-views | No Hor+; rectangle squeezed (WS-6). |
| Tutorial camera (`/tutorialcamera` block) | fight camera | As fights. |

## 5. Venue sweep plan (WS-9)

28 playable venues (`V2BG_xxA.xml`; FOV 25–46°, pitch 6–36°, offset −15 to −50). Priority by inference from tuning and
arena size: **07A Inferno, 17A Parkade, 15A Crow's Office, 05A Dragon House** first; then indoor clubs with crowd cut-outs
(09A/B, 14A, 19A, 22A/B, 23A); then the rest. For each: 1v1 with both fighters pushed to the left wall, then the right;
a 4-way FFA; captures at 16:9, 21:9 and 32:9 at closest and widest zoom. Verdicts: OK / cosmetic / cap. A "cap" venue gets
a per-venue maximum 3D aspect (the 3D rendered in a centred box of that aspect, the HUD unaffected), applied in the
`sub_0001A4E0` replacement when the venue id (from the venue tuning loader `sub_000AB160`) is in the table.
