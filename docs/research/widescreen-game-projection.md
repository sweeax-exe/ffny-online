# Where the game builds its 3D projection, and what a Hor+ widescreen patch must touch

Read-only static analysis of the lifted image (`src/recomp/gen/recomp_00NN.c`, line numbers below are in
that tree) cross-checked against the disassembly (`analysis/output/asm/text.asm`, `functions.json`,
`xrefs.json`). Nothing was built or run. Guest addresses are image VAs (image base 0x10000).

Legend: **[V]** verified in code, **[I]** inferred (reasoned from code, not proven), **[?]** not examined.

## 0. One-paragraph answer

All 3D projection goes through **one function, `sub_00103E10`** (`View::SetPerspective(fovDeg, aspect, near, far)`),
the only caller of the D3DX-style `PerspectiveFovRH` builder (`sub_001F384B`). The FOV is a **horizontal angle in
degrees**; the vertical FOV is derived by dividing by the aspect argument. The scene view's aspect is a
**hard-coded 4/3 literal (0x3FAAAAAB) inside `sub_0001A4E0`**, with a dormant "16:9 letterbox" mode 1 (literal 0x3FE38E39)
and a "viewport width/height" mode 2 beside it. The frustum-culling planes are recomputed inside `sub_00103E10` from
the same FOV and aspect, so widening the projection widens culling automatically. The game never feeds the Xbox
widescreen video flag into anything the renderer reads. The resolution (640x480) is a pair of float literals in
`sub_00011E40`. The best patch point is **`sub_0001A4E0`** (not `sub_00103E10`).

## 1. Where the perspective projection is built

### 1.1 The builder: `sub_00103E10` (recomp_0007.c:86918) [V]

Signature (stdcall, `this` in ecx = EAGL "View" object, 4 float args, `ret 0x10`):
`SetPerspective(fovDeg, aspect, nearZ, farZ)`.

```
86960  xmm0 = [esp+0x18] * MEMF(0x28B104)          ; fov * (pi/180)   -> radians
86965  PUSH32(esp, edi)  ...  sub_001F384B          ; PerspectiveFovRH(out, fovY_rad, aspect=1.0, near, far)
86968  fp_top() = fp_top() * MEMF(0x2B69BC)         ; fov * (pi/360)  -> half angle
86972  tan(...)                                      ; used for the culling planes (see 3)
86977  MEMF(edi) = [esi+0x140] * m00                 ; m00 = cot(fov/2) * subrectScaleX
86981  xmm0 = [esi+0x144] * aspect * MEMF(esi+0x54)  ; m11 = cot(fov/2) * aspect * subrectScaleY
86986  MEMF(esi+0x130) = fov ... 0x134 = aspect, 0x138 = near, 0x13C = far   ; stored for the getters
```

- `sub_001F384B` (asm 0x1F384B) is D3DX `MatrixPerspectiveFovRH` (writes -1.0 at m[2][3], m00 = cot/aspect, m11 = cot).
  The game always passes aspect = 1.0 to it and then applies the real aspect itself on m11 (see excerpt).
- Net result: `m00 = cot(fov/2)`, `m11 = cot(fov/2) * aspect`. So **fov is the HORIZONTAL full angle, in DEGREES**,
  and `tan(vfov/2) = tan(hfov/2) / aspect`. At the design aspect 4/3 the vertical FOV is therefore
  `2*atan(0.75*tan(fov/2))`.
- View object fields (this = View): +0x40 projection matrix, +0x130 fov, +0x134 aspect, +0x138 near, +0x13C far
  (getter `sub_00106450` returns fov/aspect, `sub_00106468` near/far); +0x140/+0x144 sub-rectangle scale
  (1.0 when the viewport covers the whole render target), +0x160..0x17C frustum plane tan/cos (see 3), +0x2C projection type
  (0 = perspective, 1 = orthographic).
- Conclusion on units, with the camera code: `highfov`/`lowfov` (0x2A8334/0x2A833C, parsed at asm 0xAB2C7..0xAB324 into camera
  config fields +0x40/+0x3C) are degrees of horizontal FOV at 4:3 [I for the link between those config fields and the
  call below; V for the units of the projection input].
- Near/far: scene viewport = 1.0 / 65535.0 (see 1.2). Render-to-texture view = 1.0 / 50000.0 (0x47435000). View ctor
  default (`sub_001063C0`/`sub_00104260`) = fov 60.0 (0x296DDC), aspect 4/3 (0x2B69C0), far 65535.0 (0x2B4CC4); these are
  overwritten before use [V].

### 1.2 The four ways `sub_00103E10` is reached [V]

| Entry | Function | What it is |
|---|---|---|
| main scene viewport create | `sub_0001A610` (recomp_0000.c:28118) | PerspectiveViewport ctor, called once from `sub_000195C0` (CRenderSubsystem::Init) |
| main scene viewport refresh | `sub_0001A710` (recomp_0000.c:28236) | applies dirty flags (bit0 = rect, bit1 = projection) via `sub_001042D0` / `sub_00103E10` |
| render-to-texture view | `sub_000177E0` (recomp_0000.c:20302) | builds its own View, fov/aspect read from a descriptor ([desc+0x20], [desc+0x24]), size [desc+0x28]/[desc+0x2C] |
| "restore after 2D overlay" | `sub_0005AEB0`, `sub_0007B9C0` | read fov/aspect/near/far back with `sub_00106450`, call `sub_000CC2C0` (flash overlay draw, switches the view to ortho), then re-apply the same four values |

`sub_001042D0` (viewport rect) also tail-calls `sub_00103E10`/`sub_00103970`/`sub_001038A0` to re-apply the projection when the
rect changes.

Every 3D scene goes through the first two (fights, in-engine cutscenes, crib/shop, anything using the CRenderSubsystem
viewport). The RTT path (`sub_000177E0`, reached from `sub_00017F10` <- `sub_00078630`/`sub_00078C90`) supplies its own aspect and does **not**
pass through `sub_0001A4E0`.

### 1.3 CRenderSubsystem / PerspectiveViewport layout [V unless noted]

`sub_000195C0` (recomp_0000.c:26031) is `CRenderSubsystem::Init`; `this` = the 0xAC-byte subsystem object
(registered as subsystem index 4: `sub_00012CF0(0x2FB148, 4)` returns it everywhere).

CRenderSubsystem: +0x30 device interface (ctor `sub_0010E400`), +0x34 PerspectiveViewport, +0x38 square view (h x h, unit
ortho), +0x3C screen-space ortho view, +0x40 unit-space ortho view (full w x h), +0x1C / +0x20 = device width/height in floats
(from device vtable slot 2 = `sub_0010C060`, which converts the integer back buffer size at device +0x134/+0x138 to float),
+0x88 / +0x8C requested width/height (floats, 640.0 / 480.0, see 5), +0x94/+0x98 two more floats.

PerspectiveViewport (`sub_0001A610`, object name string 0x286AC8): +0x00 owner, +0x04 device iface, +0x08 the View,
+0x0C **fov (deg)**, +0x10 **aspect mode**, +0x14/+0x18 x,y offsets, +0x1C/+0x20 viewport width/height, +0x24 near,
+0x28 far, +0x34 scale (inert), +0x3C dirty flags.

The create call in `sub_000195C0` (recomp_0000.c:26258-26268):
```
PUSH32(esp, 0);  PUSH32(esp, 0x477FFF00);  PUSH32(esp, 0x3F800000);  PUSH32(esp, 0);  PUSH32(esp, 0x41F00000); ...
; => far = 65535.0, near = 1.0, aspectMode = 0, default fov = 30.0 deg, x = y = 0, w/h = device size
```

The per-frame/per-scene API is a set of tiny CRenderSubsystem wrappers that edit the viewport and set dirty bits;
the application of the change happens in `sub_0001A710`:

| Wrapper | Effect | Notes |
|---|---|---|
| `sub_000190C0(fov, apply)` | `vp.fov = fov`, dirty bit 1 | 9 callers (`sub_00044400`, `sub_0005D810`, `sub_0005E830`, `sub_00062170`, `sub_00070DF0`, `sub_00070FC0`, ...). `sub_0005E8F9` resets it to 30.0. Does nothing if `apply` is 0. |
| `sub_000190F0(near)` / `sub_00019120` | `vp.near`, reset to 1.0 | callers `sub_00070730`, `sub_00070845`, `sub_00089E00` / `sub_000709B0`, `sub_000759F0`, `sub_00089F20` |
| `sub_00018C80(mode)` | `vp.aspectMode`, dirty 3 | mode 0 from `sub_000689B0`, `sub_00068A03`, `sub_00075A7C`, `sub_00078E4B`, `sub_00079701`; mode 1 from `sub_000712D0`; mode 2 from `sub_00062170` |
| `sub_0001A300` / `sub_0001A250` | set viewport rect / apply | `sub_00062170`, `sub_00078C90`, `sub_000796A0`, ... |

The viewport's own setters (`sub_0001A320`, `sub_0001A340`, `sub_0001A360`, `sub_0001A390`) have no callers and no vtable
reference [V: no direct xref, no `.rdata` pointer found]; they are dead.

### 1.4 The aspect source: `sub_0001A4E0` (recomp_0000.c:27891) [V]

`GetViewParams(this = viewport, mode, &fov, &aspect, &y, &height)`, `ret 0x14`. Only two callers: `sub_0001A610`, `sub_0001A710`.

```
mode 0 (default, used by the game):  fov = vp.fov;  aspect = 0x3FAAAAAB (=4/3 literal, 27968);  y = vp.y;  h = vp.height
mode 1 (dormant 16:9 letterbox):     fov = vp.fov;  aspect = 0x3FE38E39 (=16/9 literal, 27949);
                                     y = vp.y + vp.height*0.125;  h = vp.height*0.75     ; 0x286D2C=0.5, 0x286D30=0.25
mode 2 (rect aspect):                fov = vp.fov;  aspect = vp.width / vp.height ;  y = vp.y;  h = vp.height
```

So: **the aspect is a hard-coded 4/3 constant, not derived from the viewport**, except in mode 2.
`sub_0001A710` then calls `sub_001042D0(x, y, w, h, ...)` for the rect and `sub_00103E10(fov, aspect, near, far)` for the projection
(recomp_0000.c:28236+, asm 0x1A743..0x1A78D).

Mode 1 is genuinely a "16:9 picture letterboxed inside a 4:3 frame" mode (viewport 0.75 of the height, centred), i.e. the
letterbox option of the Xbox video settings. It is used by `sub_000712D0` (called from `sub_00076C60`, `sub_0007B290`; the same function
sets device mode 2 via `sub_00018C60` and clears to black via `sub_00019230`) [I: most likely full-motion video / cinematic
presentation, not confirmed]. No code path selects mode 1 based on a setting.

## 2. Does the game read the video flags / any aspect variable?

### 2.1 XGetVideoFlags is called, but its widescreen bit goes nowhere [V]

- Kernel import `ExQueryNonVolatileSetting` (ordinal 24, thunk slot 0x2852FC) is reached only through the stub `sub_00200456`.
  Direct callers: XDK library functions `sub_001F538E` (XGetLanguage, index 7), `sub_001F53BB` (XGetAudioFlags, index 9),
  **`sub_001F53FD` (XGetVideoFlags, index 8; result `>>16 & 0x5F`; AV-pack fixups through `sub_001FAD2B`/`sub_001FAD33`, index 0x103)**, `sub_001FA262`
  (generic wrapper), `sub_001FAD33`, `sub_001FCEA1` (index 0xA, parental control).
- `AvGetSavedDataAddress` (ordinal 1, thunk 0x285250): the only reference to the thunk is its import stub `sub_00200366`, which
  has **no callers**. Unused.
- `sub_001F53FD` (XGetVideoFlags) has exactly two game callers:
  1. **`sub_000DB040`** (recomp_0006.c:41678), a system-info object constructor (vtable 0x2B4274). It stores `[this+0x28] = 1` if
     `flags & 1` (widescreen), else `(flags>>3)&2` (=2 for letterbox, 0 for normal): an aspect-mode field. The only caller is
     `sub_00011E40`, which builds it as a **stack temporary** (`lea ecx,[esp+0x14C]`), passes it to the date/language
     helpers `sub_000DB170`/`sub_000DB1A0`, and never reads `[+0x28]` (no `[esp+0x174]` access in the function).
     **So the widescreen/letterbox answer is computed and then discarded.** [V for the discard within `sub_00011E40`; the
     object does not escape, only its address is passed to the two helpers, which only read the date fields.]
  2. `sub_0010DB60` (vtable method, no direct callers): tests bit 0x40 (PAL-60) and sets device byte +0xDC. Not aspect related.
- Therefore the toolkit answering "widescreen + HDTV" has **no visible effect**, as observed.

### 2.2 A dormant device-level widescreen path [V that it is dormant]

- The device interface object (`sub_0010E400`, vtable 0x2B7F08) has byte +0xA4 "widescreen" and +0xA5 "10x11 pixel aspect".
  `sub_0010C280` (D3DPRESENT_PARAMETERS fill) turns +0xA4 into `D3DPRESENTFLAG_WIDESCREEN` (0x10) and +0xA5 into
  `D3DPRESENTFLAG_10X11PIXELASPECTRATIO` (0x100, only when width == 640); it also sets 0x50 / 0x30 / 0xB0 for 1280x720 / 1920x1080.
- The setters `sub_0010D310` (SetWidescreen, forces it on for widths 0x500/0x780) and `sub_0010D3B0` (Set10x11) and the getters
  `sub_0010D390`/`sub_0010D410` have **no callers and no vtable references**. The ctor zeroes the bytes (`sub_0010E400`). So the
  Xbox "anamorphic widescreen" present flag is never set by the game. [V]
- Practical consequence: the real console never got a widescreen signal from this build either; the build ships 4:3 only.

### 2.3 Other aspect variables [V]

The only 4/3 and 16/9 literals in code are the two in `sub_0001A4E0` (asm 0x1A52E, 0x1A567); the only 4/3 data constants are
0x2B69C0 (read once, in the View default init `sub_00104260`). No global "aspect" variable exists. The `gWidescreenMode` flag
mentioned for the Flash front end is unrelated to this path.

## 3. Frustum culling

- The culling frustum is **derived from the same FOV and aspect** inside `sub_00103E10` [V]. After building the matrix it computes
  `T = tan(fov/2)` and stores into the View: +0x160/+0x164 (left plane: `tan`, `cos(pi/2 - a)`), +0x168/+0x16C (right),
  +0x170/+0x174 (bottom or top), +0x178/+0x17C (the other), where the half-angles are `atan(T * k)` with `k` from +0x180..+0x18C
  (1.0 for a viewport that covers the render target; otherwise derived from the sub-rectangle by `sub_001042D0`, asm 0x104510..0x1045F9)
  and, for the vertical planes, divided by `aspect` ([esp+0x10]). (recomp_0007.c:86995-87050, five `fpatan`, four `fptan`.)
- The consumer is **`sub_00103B10`** (recomp_0007.c:86516), `View::IsSphereVisible(center*, radius)`: transforms the centre by the
  view matrix, rejects on near/far (`[+0x138]`, `[+0x13C]`), then on the four side planes using +0x160..0x17C. Its callers are
  `sub_000849C0`, `sub_00111040`, `sub_00111111`, `sub_00112610` and, at level 2, `sub_00023810`, `sub_000241A0`, `sub_00026C40`, `sub_00026D30`,
  `sub_00084B40`, `sub_000CAB80`, `sub_001154E0..`, `sub_00181640`, `sub_00182080` [V for the list; I that these are the scene/object/particle cull paths].
- Therefore: if `sub_00103E10` receives the widened (fov, aspect), culling widens with it. No separate hard-coded cull constants
  were found on this path. [V]
- Not examined [?]: other "is on screen" style tests that read the view/projection matrices directly (the globals
  `EAGL::ViewPort::gpProjectionMatrix` = 0x3A51E0, `gpViewMatrix` = 0x3A5120, `gpViewProjectionMatrix` = 0x3A5260, copied in
  `sub_00104000`, asm 0x1040F0-0x104115). Candidates to check for 640-width or 4:3 assumptions: crowd, particles, shadows, name tags,
  lens flares. Suggested grep: functions referencing 0x3A51E0 / 0x3A5260 / 0x3A5120 and the callers of `sub_00103840` (viewport getter: x,y,w,h as floats + z range).
  One concrete consumer found: `sub_00115CB0` (asm 0x115CB0, a render pass with its own 3-deep read-back buffers) reads the current view's fov/aspect/near/far through the getters
  (`sub_00106450`, `sub_00103840`), builds its own projection with `fptan`, so it follows the patched values; it uses -0.5 (0x28D1C4) and 0.5 constants in clip space. Verify
  visually; it may be a post effect (depth of field / glow) [?].

## 4. Orthographic / 2D side

`sub_000195C0` creates, besides the PerspectiveViewport, three more Views (all from `sub_00106570` = View factory) [V]:

| Field | Viewport rect (`sub_001042D0`) | Projection | Notes |
|---|---|---|---|
| CRS+0x38 | (0,0,h,h), z 0..1 | `sub_001038A0(1.0, 1.0, 200.0)` unit-space ortho | square viewport, height by height [I: thumbnails / portrait style content] |
| CRS+0x3C | (0,0,w,h), z 0..1 | `sub_00103970(1.0, 10.0)`: pixel ortho | the **2D screen view**; `sub_00103970` reads the device size through the device vtable (+8) and builds an off-center ortho 0..w x 0..h (`sub_001F3B97`) |
| CRS+0x40 | (0,0,w,h), z 0..1 | `sub_001038A0(1.0, 1.0, 200.0)` | unit-space ortho for full-screen quads [I] |

(`w`/`h` = `[CRS+0x1C]`/`[CRS+0x20]`, floats from the device.)

- The pixel ortho therefore matches the **actual backbuffer size**, not a fixed 640x480. Anything that lays out in 640x480 virtual
  units is positioned by a different layer. 2D UI rectangles use `sub_00028950(x, y, 640.0, 480.0, ...)` (a rect/stage constructor; asm sites with
  `push 0x44200000 / push 0x43F00000`: 0xB369F, 0xCCED9, 0xCD54A, 0xD4C28, 0xD693A, 0xD8993, 0xD8A04, 0xD8A67, 0xD8B30, 0xD9A12, 0xD9A77) [V that the literals exist;
  I that they are Flash/UI stage rectangles]. `sub_0005AF80`/`sub_0005E6F0` use 480.0 or 460.0 (0x43E60000) selected by a field of the object returned by `sub_00045160` (`[+0x3C] == 3`, likely a
  video standard / PAL switch) [I].
- Systems that draw through the 2D views: Flash UI overlays (`sub_000CC2C0` switches the current view to ortho via `sub_00103970`, runs a vtable draw, and restores) [V for the
  switch, I for "Flash"], HUD, fades, flashes. Full-screen colour filters are not identified here [?]. If they are drawn through the CRS+0x40 unit-space view they cover any aspect automatically [I].
- Render-to-texture: `sub_000177E0` (descriptor size [+0x28]x[+0x2C], own fov/aspect) must keep its size and aspect. The `sub_0001A4E0` patch does not touch it. [V]
- The `sub_00103840` getter returns the viewport as ints->floats from View +0x118..0x124, so any code that reads the viewport rect is relative to the real target size [V].

## 5. Viewport / resolution setup with hard-coded 640 / 480

- **Requested resolution**: `sub_00011E40` (recomp_0000.c:5233) stores `0x44200000` (640.0) and `0x43F00000` (480.0) at lines 5297/5301
  into `[CRS+0x88]`/`[CRS+0x8C]`, plus three 32.0 values (`[+0x90..0x98]`, probably margins) [V]. `sub_000195C0` (asm 0x19627-0x1963A) passes them to
  `sub_0010C040(w, h)` (recomp_0007.c:103105) which truncates to ints and writes the device interface fields +0x40/+0x44 (D3DPRESENT_PARAMETERS back buffer width/height) [V].
- The device ctor `sub_0010E400` (recomp_0007.c:108873; lines 108929/108932) also pre-loads +0x40/+0x134 = 0x280 and +0x44/+0x138 = 0x1E0 (and +0x48 = 0x20) as defaults [V]. The
  cached +0x134/+0x138 are what the engine later reads back (`sub_0010C060`) as device width/height; they are filled from +0x40/+0x44 when the D3D device is created in
  `sub_0010C280`'s present-parameters path [I for the exact copy; the function also special-cases 1280x720, 1920x1080 and the 0x280 width, asm 0x10C25B-0x10C2D9].
- **D3D viewport**: `sub_00104000` (View::Activate, recomp_0007.c:87068) issues the real SetViewport (`sub_0021D940`/`sub_00214DE0`/`sub_0021E040`) from View +0x118..0x12C (x,y,w,h ints, z min/max)
  computed by `sub_001042D0` from float arguments, clamped to the device size obtained through `[View+0x28]` vtable slot 2. No literal 640/480 is involved in the viewport itself [V].
  No scissor-rect call was identified [?].
- Other literals: `sub_00011E40` also passes 128.0 / 176.0 / 256.0 to `sub_00028950` (asm 0x12085..0x120A6); purpose unknown [?].

## 6. Recommended patch

### 6.1 What to wrap: `sub_0001A4E0` (replace it, hand-written in `src/recomp_manual.c`)

Reason: it is the single place that supplies (fov, aspect) to the scene viewport, it has only two callers, and it sits **upstream** of
`sub_00103E10`'s stored copy. If instead `sub_00103E10` were patched, the restore sites `sub_0005AEB0` / `sub_0007B9C0` (which read the stored
values back through `sub_00106450` and re-apply them) would convert twice, and the RTT views (`sub_000177E0`) would be widened wrongly.
With the `sub_0001A4E0` approach the viewport's own `fov` field always holds the design value (the camera code keeps writing 4:3 degrees through `sub_000190C0`), the conversion
is re-done from it at each refresh (no accumulation), and `sub_00103E10` receives and stores the final value for the getters.

Convention to follow (from the lifted function, recomp_0000.c:27891-27990): `this` in ecx, `MEM32(esp+4)` = mode, `MEM32(esp+8/0xC/0x10/0x14)` = pointers to
the four float outputs (fov, aspect, y, height), return with `esp += 24`.

Sketch (Hor+ with constant vertical FOV; A = target aspect = `[vp+0x1C]/[vp+0x20]` once the backbuffer is the real window size, or a configured value):

```
design   = 4.0f/3.0f
fov_out  = vp.fov                         (degrees, horizontal at 4:3)
if (A > design)  fov_out = 2*atan( tan(radians(vp.fov)/2) * (A/design) ) in degrees   // Hor+: vertical FOV unchanged
else             fov_out = vp.fov                                                      // narrower than 4:3: keep horizontal (Vert-)
fov_out  = min(fov_out, ~170)
aspect   = A
y, height = vp.y, vp.height   (mode 0 and 2 unchanged)
```

Check of the maths: with `m00 = cot(fov/2)` and `m11 = cot(fov/2)*aspect`, passing `fov_out` and `aspect = A` gives
`m11 = cot(h0/2) * (design/A) * A = cot(h0/2) * design`, identical to the original vertical scale, and `m00` narrower by `design/A`, as wanted.

Mode handling inside the replacement:
- mode 0 (everything the game uses today): as above, aspect = A instead of the 4/3 literal.
- mode 2 (sub-rectangle viewports, `sub_00062170`): leave as is (aspect = w/h of that rectangle); apply the same Hor+ fov conversion only if the rectangle is a full-screen 3D view, otherwise leave.
- mode 1 (letterbox, `sub_000712D0`, likely cinematics): with a wide backbuffer it must not shrink the height by 0.75 any more. Treat as `min(16/9, A)`-style: if A >= 16/9 use full height and aspect = 16/9
  with the rect pillarboxed by the caller, or simply route it to the mode 0 treatment. Needs a look at what is drawn in that mode before deciding [I].

### 6.2 What else must change with it

1. **Backbuffer size**: change the two literals `0x44200000` / `0x43F00000` in `sub_00011E40` (5297/5301; via an `sub_00011E40_enter` hook writing `[CRS+0x88/0x8C]` is not possible because the CRS is created inside the function; use a wrapper around `sub_0010C040` instead, which receives the floats on the guest stack: `MEM32(esp+4)`, `MEM32(esp+8)`), or leave the guest at 640x480 and let the host layer present at the window aspect (anamorphic stretch, the real Xbox widescreen method). In the second option set A to the output aspect in the `sub_0001A4E0` patch but the 2D layer is then stretched too.
   Also patch the cached defaults 0x280/0x1E0 in `sub_0010E400` (108929/108932) if the device layer reads them before `sub_0010C040` runs [I].
2. **2D / UI**: the pixel ortho (`sub_00103970`) and every viewport rect follow the real backbuffer, but all the 640x480 `sub_00028950` rectangles and the 460/480 selections (`sub_0005AF80`/`sub_0005E6F0`)
   are in virtual 640x480 units. With a wider real backbuffer these need a scale + centring (or a pillarboxed 4:3 UI region) in the 2D view setup; this is a separate work item [I].
3. **Culling**: nothing separate; it follows (section 3). Re-verify `sub_00115CB0` and any matrix-reading screen-space code (section 3, not examined list).
4. **RTT / portrait / thumbnail targets**: untouched by this patch; do not scale them.
5. **Video flags**: no need to touch the kernel answer; the game ignores it. If the host layer honours `D3DPRESENTFLAG_WIDESCREEN` (0x10) it is never set here (section 2.2).
6. **Guard rails**: keep a config switch for A (auto from the window, or forced 4:3/16:9/16:10/21:9); clamp the horizontal FOV; skip the conversion when `A` equals 4/3 so the existing determinism tests/baselines (4:3) are bit-identical.

### 6.3 Optional lower-level alternative

Patching the literal bytes `ABAAAA3F` (0x3FAAAAAB) at VA 0x1A569 to a runtime value only fixes the aspect, not the Hor+ widening of the FOV, so the picture would be vertically
cropped incorrectly relative to the camera design; it is not enough on its own.

## 7. Open items / not verified

- Which CRenderSubsystem wrapper each camera mode uses (the other agent is on the fight cameras): verified only that `sub_000190C0(fov, 1)` is the entry and that `sub_0001A710` applies it.
- The exact content drawn with aspect mode 1 and mode 2 callers.
- Screen-space "is visible" logic outside `sub_00103B10` (crowds, particles, shadows, name tags, lens flares).
- The role of `sub_00115CB0` and `sub_0011A7D0` (post-process pass that reads view parameters).
- Copy of +0x40/+0x44 to +0x134/+0x138 at device creation (host D3D layer behaviour with changed sizes).
- No scissor or `SetViewport` literal other than the ones above was searched exhaustively (`0x280`/`0x1E0` immediates and `640.0`/`480.0` float immediates were; the XBE has no other `640.0` data constants besides code immediates).
