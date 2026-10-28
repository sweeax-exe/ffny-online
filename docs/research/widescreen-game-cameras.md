# Game cameras and what a wider Hor+ view does to them

> **Errata (2026-10-11, checked against the code and a live run):** the game's FOV is **horizontal**, not vertical (`sub_00103E10` passes aspect 1.0 to `sub_001F384B` and scales `m11` by the aspect), so the half-width tables in sections 1.6 and 3.2 need re-deriving; and aspect mode 1 **is** used: `sub_000712D0` selects it for Story cutscenes (logged live, 640x360 letterbox). The recommendation to pillarbox Story cinema is superseded. See `docs/widescreen/01-findings.md` section 5.

Research date: 2026-10-10. Static analysis only: lifted C in `src/recomp/gen/recomp_00NN.c`, the disassembly in `<data>/analysis/output/asm/text.asm`, and game data under `<data>/extracted/`. Nothing was built or run.

Tags: **[V]** verified directly in code or data, **[I]** inference from verified pieces, **[U]** unknown or not located.
Function names (`sub_000AB550`) are guest addresses. Code quoted from the lifted C is shortened, with flag bookkeeping removed.

## TL;DR

1. **The fight camera never reads the aspect ratio [V].** Zoom is set from the 3-D spread of the fighters and the vertical FOV, and the same code runs for 2, 3 and 4 fighters. A wider Hor+ view only shows more of the arena at the sides. Nothing in the zoom logic has to change.
2. **The engine applies the game's FOV as a vertical FOV [V].** `sub_001F384B` builds `m00 = cot(fov/2) / aspect` and `m11 = cot(fov/2)`. The aspect comes from one function, `sub_0001A4E0`, which has three modes: 4:3, 16:9 letterbox, and "viewport width / height". The game only ever uses mode 0 (4:3) [V for the init call site].
3. **The fight camera places itself using the FOV value it chooses, not the FOV the projection ends up using.** Changing aspect in the projection leaves the camera position untouched. Do not scale the FOV by hooking `sub_00044400` or `sub_000190C0` for Hor+. Changing the aspect (or the x scale of the projection) keeps the vertical framing exactly as authored.
4. **The fight camera never yaws and has no collision.** It looks down -Z from a point above and behind the focus, follows the fighters' bounding-box centre in X without clamping, and has no wall test. Each venue's own `<camera>` block (zoom FOV, pitch, offset, "changepos") is what keeps the 4:3 view clean. Widening exposes extra geometry at the sides that nobody checked, so a per-venue visual sweep is needed.
5. **Special cameras (`camera.xml`) are different.** They use a fixed 45 degree FOV, an orbit radius of 40, 50 or 55 units around a joint, and a ray test (`sub_001AC220`) for line of sight to the subject. Wide shots at the sides are not checked against the frustum, only against the sight line.
6. **Cinematic cameras (story cutscenes, intros and outros) are animation-driven [V]** via `sub_00070DF0`: FOV and eye/target come from authored animation channels (`CAM_EYE`, `CAM_TARGET`). They were composed for 4:3. No engine-drawn letterbox was found.

---

## 1. The fight camera

### 1.1 Objects and entry points

| Item | Address | Notes |
|---|---|---|
| Camera object `this` | passed in `ecx`, owner = match object `+0x2DB44` | Called from `sub_000AC590` (per-tick wrapper), which is called by `sub_0008BB00` (match setup) and `sub_00087700` [V]. |
| Wrapper that copies venue tuning into the camera and dispatches | `sub_000AC590` | Picks one of three updaters (below). |
| Main updater (normal fight camera) | `sub_000AB550` (`recomp_0005.c:9639`) | The part of interest. |
| Special-camera updater | `sub_000AC1F0` (flag bit 0), frame interpreter `sub_000ABB70` | See section 3. |
| Free/debug camera | `sub_000AAE00`, used when `[0x3BE0F4] & 0x40` | Pad-driven. Irrelevant for shipping. |
| Reset | `sub_000AA4C0` | Initial distance 32.0, or 20.0 when rules bit 0 of `0x3B907E` is set. |
| Venue tuning loader | `sub_000AB160` (`recomp_0005.c:8922`) | Parses the venue XML `<camera>` node. Also calls `sub_000AA590`, which parses `motion/camera.xml`. |
| FOV setter (fight path) | `sub_00044400` -> `sub_000190C0` | `sub_190C0(view, fov, 1)` writes `view+0x0C = fov` and sets dirty bit 2 in `view+0x3C`. |

Dispatch in `sub_000AC590` [V]:

- debug flag byte `0x3BE0F4` has bit `0x40` set: the free camera `sub_000AAE00`;
- otherwise, the camera object's flags word has bit 0 set: the special camera `sub_000AC1F0`;
- otherwise: the normal fight camera `sub_000AB550`.

### 1.2 Per-venue tuning: where "highfov" and "lowfov" come from

`sub_000AB160` reads the venue's own XML (the file `V2BG_xxA.xml` inside `<data>/extracted/assets/v2bg.viv`, RefPack-compressed). The text is a `<camera>` node (or `/tutorialcamera` when `[0x3B8598] & 1`). Not in `venue.xml`, `gconfig.xml`, `gamedata.xml`, or `motion/camera.xml` [V: grepped].

Recognised tags and where they land (strings at `0x2A8330..0x2A8388`) [V]:

| Tag | Camera field | Global copy | Default |
|---|---|---|---|
| `fov` | `+0x3C` and `+0x40` | | 35.0 |
| `lowfov` | `+0x3C` | `0x39C168` | 35.0 |
| `highfov` | `+0x40` | `0x39C18C` | 35.0 |
| `lowangle` | `+0x44` (stored as a negative angle; a positive value is negated) | `0x39C17C` | -25.0 |
| `highangle` | `+0x48` (same sign rule) | `0x39C180` | -25.0 |
| `changepos` | `+0x54` | `0x39C178` | 0 |
| `offset` | `+0x4C` and `+0x50` | | 0 |
| `lowoffset` | `+0x4C` | `0x39C188` | 0 |
| `highoffset` | `+0x50` | `0x39C184` | 0 |
| `upcamera` | `+0x58` | `0x39C16C` | 6.0 |

Lifted C of the default setup:

```c
MEM32(ebp + 0x40) = 0x420C0000;  MEM32(ebp + 0x3C) = 0x420C0000;   /* 35.0 */
MEM32(ebp + 0x48) = 0xC1C80000;  MEM32(ebp + 0x44) = 0xC1C80000;   /* -25.0 */
MEM32(ebp + 0x58) = 0x40C00000;                                     /* upcamera 6.0 */
```

Example (Subway, `V2BG_04A.xml`, values only): low FOV 31, low angle 30, low offset -35; high FOV 33, high angle 8,
high offset -35; `changepos` -43; `upcamera` 4.0.

All 28 playable venues were dumped. FOV runs 25 to 46 degrees, pitch 6 to 36 degrees, offset -15 to -50, `changepos` -43 to 66. Four "CUT / cinema only" venues (`08A`, `16A`, `18A`, `20B`) use the legacy form `<fov>45</fov>`, angles -25/-35, offset 0. `11A` (Blaze's Gym) misspells `upcamera` as `upcam`, so it silently gets the 6.0 default [V]. The hub venues `100..117` and the viewer `00A` have no `<camera>` block [V].

`upcamera` ends up at `view_object[4] + 0x80` (end of `sub_000AB550`). Its meaning is not established [U].

### 1.3 What it tracks

`sub_000AB550` loops over four fighter slots. Slot base `0x340D70`, stride `0xC8`, position table at `0x340DB4`, loop ends at `0x3410D4` [V]. A slot counts when `[slot] != 0` and bit 3 of the byte at `slot+0xC0` is clear:

```c
if (MEM32(0x340D70) != 0) { if ((MEM8(0x340E30) & 8) == 0) active[0] = 1; }   /* ... x4 */
```

What bit 3 means (eliminated, spectating, off-camera) is [U]. Each active fighter contributes one point. Which point depends on `[slot+0x1C]` (mode 0, 1 or 2 picks different vector sources, and 0 and 2 add +10.0 to Y) [V]. The result is an axis-aligned bounding box (min init +1000, max init -1000). Nothing else is tracked: no velocity, no facing, no weights [V].

```c
centre = (min + max) * 0.5;                 /* fmul by 0.5 at 0x286D2C */
extent = len(max - centre);  extent2 = len(min - centre);   /* sub_000A9C50 = 3-D length */
radius = max(extent, extent2);
radius = min(radius, 80.0);                 /* 0x2A1528 */
minR   = (MEM8(0x3B907E) & 1) ? 15.0 : 20.0;  /* 0x292384 or 0x28CD54 */
radius = max(radius, minR);
```

So the "radius" is the half 3-D diagonal of the fighters' box (height included), clamped to the range 20 to 80 (15 to 80 if rules bit 0 of `0x3B907E` is set; the setup structure `0x3B9078` is filled by `sub_000871F0`, see `match-end-state.md`).

### 1.4 Distance, FOV and height

Smoothed "frame size" `cam+0x34` chases `1.95 * radius` at 20% per tick with a dead zone of 0.25 [V]:

```c
diff = radius * 1.95f - cam[0x34];          /* 0x2A85A0 = 1.95 */
if (fabs(diff) >= 0.25f)  cam[0x34] += diff * 0.2f;   /* 0x2976CC = 0.2 */
```

Camera distance along the view axis (`recomp_0005.c:10110`, `fptan`):

```c
fov  = lerp(lowfov, highfov, t);                         /* degrees */
Hd   = cam[0x34] / tan(fov * 0.5 * (pi/180))             /* fit the "frame size" into the FOV */
     + lerp(lowoffset, highoffset, t)
     - (cam[0x34] - minR)^2 / 180.0;                     /* 0x2A859C = 1/180 */
```

The blend factor `t` is a ramp on the largest world Z of the fighters, centred on `changepos` with a +/-5 unit (`0x29238C`) band [V]:

```c
if (maxZ <= changepos - 5)      t = 1.0;     /* "low" tuning */
else if (maxZ >= changepos + 5) t = 0.0;     /* "high" tuning */
else                            t = 1.0 - (maxZ - (changepos - 5)) * 0.1;
```

So `changepos` is a depth threshold: when the fighters are nearer one side of the arena, the camera switches pitch, FOV and offset between the two tuning sets. Pitch also eases: `cam+0x38 += (target_pitch - cam+0x38) * 0.03` (`0x2A85A4`) unless the special flags are set [V].

Eye position (`sub_000A9980` builds an X-axis rotation matrix at `0x39C0E0`, `sub_000A9B60` rotates the vector (0, 0, Hd)) [V]:

```
eye = target + ( 0, -sin(pitch) * Hd, cos(pitch) * Hd )     (pitch is negative, so the eye is above and on the +Z side)
eye.y = min(eye.y, 140.0)                                   (0x2A1524)
```

Notes:
- The yaw is never computed for placement. The camera looks along -Z; only pitch, distance and X/Z centring vary [V]. `sub_000AA010` computes yaw/pitch angles for `cam+0x28`, but only as an output.
- Focus follow (not zoom): the target lerps toward the box centre with per-tick rates 0.1125 (X, Z) / 0.045 (Y) when frame size < 20; 0.15 / 0.06 when < 25; 0.25 / 0.10 otherwise (constants `0x2A8598/94`, `0x2A8590/8C`, `0x286D30/0x2972C4`) [V]. There is **no clamp of the target to arena bounds** [V: no compare against anything but the fighters].
- Eye smoothing: if the view direction's Y changed by more than 1e-5 (`0x2A8588`) the eye eases at 0.75 (X, Z) and 0.5 (Y) (`0x2A8584`, `0x286D2C`), otherwise it snaps [V].
- Everything is per-tick, not time-scaled (the `dt` argument only reaches the shake code, `sub_000AACA0`) [V]. Keep a fixed tick when porting.
- Final calls: `sub_00044400(fov)` -> view FOV, and `view_object[4]+0x80 = upcamera` [V].

### 1.5 The 2, 3 and 4 fighter cases

There is no per-count branch [V]. A fourth fighter simply enlarges the box, which enlarges `radius` (up to 80) and so `Hd`. Worst case from the formulas: radius 80 gives frame size 156, `Hd` about 490 for FOV 28 degrees, about 108 to 127 units of vertical half-extent at the focus. The eye height cap of 140 then flattens the view angle.

The rules bit `0x3B907E & 1` only lowers the minimum radius from 20 to 15 (tighter close-ups) [V]. Which match types set it is [U]. The match-type name table at `0x2DBCC8` (Single, Tag, 3Way, 4Way, 1vs2, 1vs3, Demolition, Inferno, Subway, Window, Cage, Ringout, Tutorial) is documented in `match-end-state.md`.

### 1.6 Does it depend on aspect? No

- The only FOV used in the placement formula is the venue's own `lowfov/highfov` number. It is treated as the angle that must cover `frame size`, and the projection later applies that same number as a **vertical** FOV (section 1.7). The 4:3 aspect never appears in `sub_000AB550` [V: no use of 1.333 or the view dimensions].
- Framing consequence [I]: at a 4:3 aspect the horizontal half-width is 1.333 times the vertical half-extent. The fit is isotropic (3-D radius), so a horizontal spread always fits comfortably. At 16:9 or 21:9 (Hor+ with vertical FOV fixed) the same camera simply shows more at the sides.
- Visible half-width at the focus plane for the minimum zoom (radius 20), derived from the formulas above for all 28 playable venues [I, arithmetic on verified formulas]:

| Aspect | Half-width range across venues |
|---|---|
| 4:3 | 33 to 51 (most 36 to 41) |
| 16:9 | 44 to 68 (most 49 to 54) |
| 21:9 | 59 to 91 (most 65 to 72) |

At full zoom-out (radius 80) the vertical half-extent is 100 to 127 units.

**Recommendation:** keep the zoom logic untouched. Do not scale the FOV for Hor+ at the camera. Change the projection aspect only (section 6).

One real caveat: if an implementation adjusts `lowfov/highfov` or hooks `sub_00044400`/`sub_000190C0` to widen the picture, the zoom formula would not compensate (the position is already computed), so the picture would simply zoom the wrong way. That is why the projection side is the right place.

### 1.7 How the FOV turns into a projection (adjacent finding, for the projection research)

- `sub_000190C0(view, fov, 1)` stores the FOV in `view+0x0C` and sets bit 2 of `view+0x3C`.
- `sub_0001A710` (view update) calls `sub_0001A4E0(mode, &fov, &aspect, &vp_y, &vp_h)`, then `sub_00103E10(fov, aspect, near, far)`, which calls `sub_001F384B` (a perspective-from-vertical-FOV builder) [V]:

```c
/* sub_001F384B */
cot = cos(fov*0.5) / sin(fov*0.5);
m[0x00] = cot / aspect;     /* x scale */
m[0x14] = cot;              /* y scale */
m[0x2C] = -1.0;
```

- `sub_0001A4E0` has three modes selected by `view+0x10` [V]:
  - mode 0: aspect `0x3FAAAAAB` (1.3333), viewport untouched.
  - mode 1: aspect `0x3FE38E39` (1.7778), viewport y = `h/8 + y0`, height = `0.75*h` (a 16:9 letterbox inside the 4:3 frame).
  - mode 2: aspect = `view+0x1C / view+0x20` (viewport width / height).
- The view is created in `sub_000195C0` through `sub_0001A610` with arguments including FOV 30.0 (`0x41F00000`), near 1.0, far 65535.0 and **mode 0** [V]. No other writer of `view+0x10` was found [U: not exhaustively searched; the only setters of the viewport rectangle are `sub_0001A7A0` via `sub_0001A300`].
- `sub_0001A300` (SetViewport x, y, w, h) is called by `sub_00062170`, `sub_00062230`, `sub_00078C90` and `sub_000796A0`, i.e. the 3-D preview and UI viewports (section 5) [V]. For mode 2 it re-marks the projection dirty.

---

## 2. Collision and "background hit"

- **Normal fight camera: no collision test of any kind [V].** `sub_000AB550` has no ray, no sphere and no wall clamp. The only guards are the eye height cap (140) and the radius clamps. The designers hand-tuned each venue's block (section 1.2) instead.
- The debug strings `" Front Camera Collision or Background Hit"`, `" Camera Front Set"` and `" Camera Collision or Background Hit %d %f"` (at `0x2A850F`, `0x2A8535`, `0x2A8549`) have **no references** in the lifted C or the disassembly [V]. They are leftover debug text. The same goes for `" SpecialCamera Begin %d %f %s"` (`0x2A84A1`). No code prints them.
- **Special camera: a ray, not a view cone [V].** `sub_000ABB70` tests up to three candidate eye positions with `sub_001AC220(origin, target, &dist, &hit)`. That function loops over the world collision objects (`count = [0x3BF1C0]`, list at `0x3BF1B8`, stride `0x48`), selecting those whose flags contain `0x2000000` and whose pointed structure has bit 1 clear. It ray-tests each with `sub_001BC380`, keeps the nearest hit, and returns whether any was found. If a candidate has clear line of sight (hit farther than 90 units, `0x2A85AC`, or none) it is used. If none is clear a random candidate is chosen (`sub_000AA340` picks a random index below 3). The test is eye-to-subject only. Nothing checks what the frustum contains.
- Venue collision data (`V2BG_xxA.cli`, magic `CLLI`) holds the volumes. Their format was not decoded here [U].

**Would a wider view expose outside-of-venue content?** [I] Likely yes in some places, and this cannot be settled statically.
- The normal camera centres on the fighters with no clamp. A fighter near an arena edge already puts about 33 to 51 units of area beyond the fighters (4:3, section 1.6) in view. Hor+ adds 11 to 17 units (16:9) or 26 to 40 units (21:9) of extra area per side.
- Evidence of arena scale: environment-attack particle coordinates in the venue XML span roughly +/-43 to +/-75 in X (for example `05A` +/-43, `07A` +/-34, `13A` -64..60, `21A` +/-75, `04A` -74..67) [V for numbers, I for interpretation as arena size].
- Rough risk ranking [I, not measured]. Highest: venues where the 4:3 view already covers a big share of the room width at minimum zoom, or where the camera sits far behind with a wide FOV: `07A` Inferno (half-width 46 at 4:3 vs. arena about +/-34 in X), `17A` Parkade (FOV 45 to 46, narrow and long), `15A` Crow's Office (small indoor room, `changepos` 66 puts it at the far Z), `05A` Dragon House (about +/-43). Medium: indoor club venues with crowd cut-outs (`09A/B`, `14A`, `19A`, `22A/B`, `23A`). The crowd presets are `csmall` and `cmed` in `venue.xml`; crowd sprites sit around the ring and their lateral extent is [U].
- The cut/legacy `<fov>45` venues are not playable and so not at risk (`08A`, `16A` "Cinema Only", `18A`, `20B`).

**Verification step to do at runtime:** put fighters at each venue's left and right extremes and capture 21:9 screenshots (a debug camera path exists, `sub_000AAE00`).

---

## 3. Special cameras (`motion/camera.xml`)

### 3.1 File structure [V]

`<data>/extracted/motion/camera.xml` (5147 lines, Shift-JIS comments) holds 559 `<timetable>` entries, each `<name>` plus `<frame>` lines of six comma-separated fields:

Example (timetable `Z101`, paraphrased): at frame 1 an orbit shot (one position code, pitch preset B3, no keyword, aim at the midpoint joint C5, radius class S); at frame 81 a medium `shake`; at frame 120 `back` to the gameplay camera.

Names are matched (string compare) to the name copied from the fighter slot (`slot+0x340DCC`, stored at `cam+0xE0`) in `sub_000AC100`, which loops the timetable array (`cam+0xD4`, stride 0x18) and starts `sub_000ABB70`. Most names look like move/animation ids (`Z101`, `N4722_A`, `N04H61A`) [V for the match, I for "animation name"].

The parser (`sub_000AA590`) reads tags `specialcamera`, `name`, `frame`. The keyword list is `slow`, `finish`, `after`, `back`, `shake` (strings `0x2A83C4..0x2A83E4`). Frame record layout (0x24 bytes) [V from the parser]:

| Field (in the line) | Stored at | Meaning |
|---|---|---|
| 0: frame number | `+0x00` (float) | Time key. Maximum value in the file is 840. |
| 1: position codes, `A` + up to three letters/digits | `+0x04`, `+0x08`, `+0x0C` | Up to three candidate eye azimuths. A digit `1..8` maps to 0..7 and a letter `A..H` to 8..15, each in 22.5 degree steps (`0x2A8578`) relative to the subject facing. The first candidate with line of sight wins. `0` or other: none. Literal `random` is parsed as code 0x10. |
| 2: `B<n>` | `+0x10` | Pitch preset from `sub_000AAB60`: B1 +25, B2 +5, B3 0, B4 -5, B5 -15 degrees. |
| 3: keyword | `+0x1C` | 0 slow, 1 finish, 2 after, 3 back, 4 shake; `0` = none (5). |
| 4: aim code, or shake strength | `+0x20` | `A<n>` = joint n of the acting fighter; `B<n>` = joint n of the opponent (`|0x100`); `C<n>` = midpoint of both (`|0x200`). With `shake`, the field instead holds `S/M/L` (strength 6/7/8). Joint positions come via `sub_000445A0` in `sub_000AA390`. |
| 5: orbit radius class and zoom rate | `+0x14`, `+0x18` | `S/M/L` -> radius 40 / 50 / 55 units (`sub_000AABA0`). A trailing digit (`M2`, `S3`) picks a per-tick FOV change from the table -10, -20, -30, -40, -50 divided by 60 (`0x28C798`); default index 5 = 0. |

Counts over all frames [V]: keyword `shake` 1092, `0` 620, `after` 529, `back` 454, `slow` 97, `finish` 96. Aim code `A#`/`C#`/`B#` and `S/M/L` dominate field 4. `M`, `S` and `0` dominate field 5.

Meaning of the keywords is only partly established. `shake` triggers screen shake (`sub_000AABD0`, strength table 0.5, 0.75, 1.0, 0.07, 0.12, 0.2, 1.0, 0.5) [V]. `back` returns control to the gameplay camera (state 3 sets bit 1 in the flags) [I]. `after` is a follow-through shot [I]. `slow` and `finish` look like slow-motion and finisher markers [U].

### 3.2 Framing and FOV [V]

- Orbit around the aim point: `eye = aim + (sin(az)*r, sin(pitch)*r + y0, cos(az)*r)` with `r` = 40/50/55.
- FOV: `cam+0xA8` is set to 45.0 (`0x42340000`) at start (`ABC38`) and passed to `sub_00044400` each tick. A radius-class digit then narrows it by 0.17 to 0.83 degrees per tick (`cam+0xAC` added to `cam+0xA8` at `AC3F9`).
- Visible vertical half-extent at the subject: `r * tan(22.5)` = 17 to 23 units. At 4:3 that is a half-width of 22 to 30.6. At 16:9 it is 29.5 to 40.8, at 21:9 it is 39 to 53.6.

### 3.3 Would they look wrong when widened? [I]

These are close shots on joints, 17 to 23 units tall, and the subject is always centred. Props, the opponent (for `C` aim codes the midpoint) and background walls are what show up at the sides. Risks: venue walls and prop dressings that were hidden at 4:3, and crowds. The ray test only guarantees the line from eye to subject, not the sides. Expect most shots to be fine and a few to show unfinished edges. A sweep over all 559 timetables is not practical statically, so a runtime spot check of the most used shot types (position codes starting `A` + digits, `C5`, `M`) in a small and a large venue is the efficient test.

---

## 4. Cinematic cameras

### 4.1 Common engine path [V]

All scripted cameras funnel through `sub_00070DF0` (also `sub_00070FC0`), a "sample the camera animation" function. It evaluates an animation (`sub_00021E80`, `sub_00022150`), then does:

```c
MEM32(esp) = MEM32(esi);                       /* ch0 = FOV from the animation */
RECOMP_ABI_CALL(0x000190C0, sub_000190C0);     /* view.fov = anim FOV, dirty bit 2 */
```

Callers: `sub_00070DF0` <- `sub_0005BE50` (from `sub_00078A40`, the cinema subsystem), `sub_000761A0`, and `sub_000A2EA0`. `sub_00070FC0` <- `sub_000A2EA0` only. `sub_000A2EA0` is the intro/outro (fighter entrance and victory) update called from the match flow states `sub_000898E0/89BC0/89F20/8A5B0/8B120` [V for callers, I for the name].

- **Eye and target** are animated nodes (strings `CAM_EYE`, `CAM_TARGET`, `CAM_EYEp1/p2`, `CAM_TARGETp1/p2` at `0x2A6880..`, `0x29C2F4`). The intro/outro config `assets/extro.xml` lists shots `extro_00..02` each with `CAM_EYE` and `CAM_TARGET` [V]. `sub_000A2190/A22A0` look up these nodes by name.
- **FOV** comes from the authored animation, so there is no single constant. It was composed for the 4:3 vertical FOV.

### 4.2 Per subsystem

| Subsystem | Evidence | Notes |
|---|---|---|
| CINEMA (story cutscenes) | `CINEMA::CCinemaSubsystem::LoadScene` assert strings (`0x29C715..`: prop list, camera list, particle list, tex-anim scale list), `CCinemaCamera::Init` (`0x29C2D4`), files `assets/cinema/*.viv` (`sc00..sc21`, `gf*`) | Camera/props/particles are per-shot lists, FOV from the animation. Props are staged for a 4:3 frame [I]. |
| IntroExtro (fight intros/outros) | `assets/IntroExtro.xml`, `assets/extro.xml`, `CIntroExtroSubsystem::Init` | Same animated eye/target. Characters enter from off-screen [I]; with a wider view they may be visible earlier. |
| SHOP cinema cam | `SHOP::Cin Cam` (`0x297244`, `sub_0005B010`), `v%d_targetcam`, `v%d_eyecam` | Per-vignette cameras built from named scene nodes [V]. |
| Tattoo camera | `GetTattCameraPos` (`0x294750`) | A script-callable accessor (handler `sub_00051BF0` registers it); the camera itself is hub/viewport driven [I]. |
| Hub/crib | `assets/spycam/*.spy` (barber, cloth, crib, crplayer, default, gym), `@default.scam`, `SetAnchorCam`, `SetCamSpeed` | Scripted spy/anchor cameras. Formats were not decoded [U]. |
| Tutorial camera | `/tutorialcamera` in the venue XML when `[0x3B8598] & 1` (`sub_000AB160`) | Same block as the fight camera [V]. |
| CP viewport (create-a-player and menu previews) | `SetCPViewportCam`, `SetSingleCPViewportCam` (`sub_0004BFD0/4C020`) -> `sub_0005F060` | Each preview viewport holds its own camera. Default FOV reset is 30.0 (`sub_0005E830`). |

### 4.3 Letterboxing

- **No engine-drawn letterbox or narrowed cinema viewport was found [I].** The view is created once as a full-frame mode-0 view (4:3). The 16:9 letterbox mode in `sub_0001A4E0` (mode 1) is never selected [V for the creation site]. The viewport rectangle is only changed by `sub_0001A300`, called from UI/preview code (section 5). Bars baked into cutscenes would have to come from the video/FMV path (`<data>/extracted/movies/`) or from screen overlays [U].
- Because there is no bar drawn by the engine, a pillarbox or crop decision for cutscenes has to be made by the port, not read from the game.

---

## 5. Split-screen and multi-viewport

- **No split-screen in fights [I].** One view object (index 4 of the table at `0x2FB148`) is used by the fight camera. Four human players share the one camera, which is why the box covers all active fighters [V for the loop].
- **Multiple 3-D viewports exist in menus [V].** `sub_0004BFD0` (`SetCPViewportCam`) and `sub_0004C020` (`SetSingleCPViewportCam`) store a camera into an array of viewport objects (at `+0xC0C` of the screen object). The viewport rectangle comes from `sub_0001A300` (SetViewport) calls at `sub_00062170/62230/78C90/796A0`. These are pixel-space rectangles in the 640x480 frame, so a port that changes the output resolution has to scale them with the UI.
- In mode 0 each such view keeps aspect 4:3 whatever the rectangle is, so the preview shapes are as authored [V].
- Not found: picture-in-picture inside fights. Per-venue post effects exist (`V2BG_xxA.pfx`: glow, screen tint, `ScreenTintVignetteEnable` with `VignetteAlpha`) and are full-screen passes [V for the file keys; the draw code was not examined].

---

## 6. Recommendations

| Camera | Recommendation | Why |
|---|---|---|
| Normal fight camera, 1 vs 1 and FFA | **Keep Hor+** (fix vertical FOV, widen the aspect) | Zoom is aspect-independent and the vertical framing is hand-tuned. FFA benefits most, since the radius clamp (80) is hit more often. |
| Special cameras | **Hor+ by default, with a safety option** | Shots are 17 to 23 units tall, orbit-based. A fixed horizontal crop to about 16:10 is the fallback if edges show. |
| IntroExtro (entrances/victories) | **Hor+ with a per-shot check, or 4:3 pillarbox** | Characters enter from off-screen and the sets are staged for 4:3. |
| CINEMA story scenes | **Pillarbox to 4:3 by default, optional Hor+ toggle** | Props, particles and extras are staged per shot and there is no clip data to check. |
| Shop, hub, crib, tattoo, tutorial | **Keep Hor+** if the set is a full 3-D room, otherwise pillarbox | Spy/anchor camera formats unverified. |
| CP/menu 3-D previews | **Keep their own rectangle** and scale it with the UI | Rectangle is set in pixel space. |

Hooks (all in guest address space; use the project's patch mechanism):

1. **Aspect**: `sub_0001A4E0`. Return `aspect = real_w / real_h` in mode 0, or set `view+0x10 = 2` after view creation (`sub_0001A610`) so that mode 2 (`view+0x1C / view+0x20`) takes the real aspect. `sub_00103E10` is the single consumer that builds the projection (`fov`, `aspect`, near, far). Keep the vertical FOV for Hor+. For a pillarbox, set the viewport rectangle (`sub_0001A7A0` fields `+0x14..+0x20`) to the centred 4:3 sub-rectangle. Because the viewport is in pixel space, also scale UI rectangles.
2. **"This is a cutscene" tag**: set a global flag around `sub_00070DF0` (and `sub_00070FC0`), the only path where an animated FOV is fed to `sub_000190C0`. The fight and special cameras go through `sub_00044400` instead. A cleaner pair: set the tag on entry to `sub_000A2EA0` while its flag bit 0 is set and clear it on exit; for story cinema use `sub_00078A40` -> `sub_0005BE50`. Combine with the view object so `sub_00103E10` can choose between Hor+ and 4:3 per frame.
3. **Special-camera detection**: the camera object flags word (`cam+0`): bit 0 = special camera active (`sub_000AC1F0` runs), bits `0x1C0` = snap modes, state at `cam+0x8C` [V]. A tag at entry to `sub_000AC1F0` is enough if special shots should be pillarboxed.
4. **Do not touch** `sub_000AB550`'s `fov` computation or `sub_00044400` for Hor+.
5. **If a venue shows its back**: the right place for a per-venue guard is the venue tuning (`sub_000AB160` globals `0x39C168..0x39C18C`) rather than the update code, for example by clamping the target X, which the game never does.
6. **Frame timing**: the camera smoothing is per tick. Keep the fixed tick the project already uses.

---

## Verified vs. inferred, at a glance

Verified: all formulas in section 1.3 to 1.5, the `<camera>` tags and defaults, venue values, no aspect use in the fight camera, the projection builder, the three aspect modes, the special-camera parser fields and constants, the ray test, the animation-driven FOV path, and the call graph quoted.

Inferred: the practical meaning of `changepos`, the names of subsystems, venue risk ranking, the pillarbox choices, and that CP viewports are authored for 4:3.

Unknown: what bit 3 of `slot+0xC0` means, which match types set rules bit 0, the meaning of `upcamera`, the `slow` and `finish` keywords, the format of `.cli` (collision), `.spy` and `.scam` files, and the crowd extents.

Related notes: `widescreen-defjam-community.md` (community patches and the lack of native 16:9), `match-end-state.md` (the `0x3B9078` rules block), `fighter-record-fields.md`.
