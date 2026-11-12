# Deterministic math for cross-platform lockstep

2026-11-12, branch `feat/det-math` (phase 1). Goal: the same fight computes the same bits on the
Windows build (x86-64, MSVC), the planned Linux / Steam Deck / macOS builds and contatg's Android
port (arm64, clang/NDK), so lockstep and rollback hashes agree across platforms. Results and the
owner report: `D:/DefJam/community/det-math-phase1.md` (not published).

## 1. Inventory: where floating point in the simulation can differ between hosts

The lifted game models the x87 stack as `double` (`g_fp_stack`), SSE registers as `float`/`double`
lanes and MMX as integers (`tools/xboxrecomp/templates/runtime/recomp_types.h`, copied into
`src/recomp/gen`). Every value is computed with C operations on the host. `tests/detmath/inventory.py`
counts the calls in the lifted chunks (build 78 lift):

| Class | Calls | What |
|---|---|---|
| **A. exact on every IEEE host** | 674 | `recomp_cvt_si32` 206, `sqrt` 157 (fsqrt), `recomp_fp_trig_in_range` 116, `recomp_fist` 79, `fabs` 44, `recomp_f80_load` 26, `recomp_fscale` 15, `recomp_frndint` 12, `isfinite` 10, `recomp_f80_store` 5, `recomp_fxam` 3, `recomp_fprem` 1; plus every inline `+ - * /`, int/float conversion, SSE lane op (`XMM_ADD/MUL/SUB/DIV/MIN/MAX/CMP`) and MMX integer op |
| **B. platform libm** (now routed) | 213 | `atan2` 69 (fpatan), `recomp_x87_sin` 52, `recomp_x87_cos` 40, `log2` 21 (fyl2x), `recomp_x87_sincos` 14, `recomp_x87_tan` 10, `expm1` 7 (f2xm1). The `recomp_x87_*` helpers call `sin`/`cos`/`tan` (and, past 2^-26 x 2^70, `sin` of the 66-bit-pi correction). |
| **C. x86-specific instructions** | 0 | `rcpss`/`rsqrtss` (`recomp_rcpss`/`recomp_rsqrtss`: the hardware's approximation on x86, which differs between Intel and AMD, and `1/x` elsewhere). This game's lift has none; the inventory fails if a re-lift adds one. |

Why class A is safe: IEEE 754 defines `+ - * / sqrt` and conversions as one correctly rounded
operation, and C defines `fabs floor ceil trunc fmod remquo frexp ldexp scalbn copysign` exactly; the
toolkit applies the guest's rounding modes itself (`recomp_frndint`, `recomp_cvt_si32` with MXCSR RC,
`recomp_fist`) and returns the integer indefinite explicitly for NaN and out-of-range values, so no
C cast's undefined behaviour (x86 gives 0x80000000, arm64 saturates) is reached. That holds only
under four conditions, each checked:

- **No fused multiply-add.** A contracted `a*b+c` rounds once instead of twice. arm64 compilers
  contract by default. The non-MSVC game target already builds with `-ffp-contract=off`
  (`CMakeLists.txt`, the comment names exactly this), as does the Android port. MSVC (the shipped
  compiler is VS 18 BuildTools, cl 14.51) defaults to `/fp:precise`, contracts nothing without
  `/fp:contract`, and the x64 target has no `/arch:AVX2`, so no FMA instruction exists to fuse into.
  `src/detmath/detmath.c` also says `#pragma STDC FP_CONTRACT OFF` / `#pragma fp_contract(off)`.
- **FLT_EVAL_METHOD 0.** x86-64 SSE2 and arm64 evaluate `float` in float and `double` in double. A
  32-bit x87 build would not; `detmath.c` refuses to build there.
- **Round to nearest, no flush-to-zero.** The runtime never changes the host's MXCSR (the guest's is
  modelled in `g_mxcsr`), but a driver, overlay or audio library can change a thread's MXCSR/FPCR.
  `recomp_detmath_fenv_guard()` now resets it (x86: MXCSR 0x1F80; arm64: FPCR RMode/FZ/DN clear) at
  the start of every fight step (`step_body`, called from `netplay_on_step`) and logs the first five repairs.
- **No host `long double`.** Neither the runtime header nor `src/` uses it (on MSVC it is 64-bit, on
  arm64 Linux/Android 128-bit). The x87's 80-bit registers are **not** emulated: the stack is
  `double` on every host, `fld tbyte` rounds to 53 bits, and precision control 24 is
  `recomp_fp_round24` (frexp/ldexp/float casts, all exact). That is a fidelity question against the
  real Xbox, not a cross-platform one: every host computes the same `double` model.

**Remaining arch trap (not fixed in phase 1): NaN bit patterns.** An invalid operation in plain lifted
C (`0/0`, `inf-inf`, `sqrt(-1)`) gives the x86 default NaN `0xFFF8000000000000` on x86 and
`0x7FF8000000000000` on arm64. Comparisons cannot tell them apart; only code that reads the bits can
(`fxam`'s sign, an integer move of the float into memory that the step hash then covers). It needs a
NaN in fight state, which would already be a bug; phase 2 counts NaN results in fights before deciding
whether stores need canonicalising.

**Class B is the real problem**, measured (`tests/detmath/libm_compare.py`, 200,000 game-range inputs,
|x| < 1000): Windows UCRT and Linux glibc give different bits for 3.2 % of `sin`, 3.1 % of `cos`,
4.5 % of `tan`, 0.13 % of `atan2`, 0.03 % of `log2` and 4.9 % of `expm1` results (1 ulp, `expm1` 2).
Even PC against PC is not safe today: the UCRT dispatches its transcendentals to an FMA3 code path on
CPUs that have FMA3 and to an SSE2 one on older CPUs, and the two disagree (`dm_vectors --host` vs
`--host --no-fma3`: different difference counts against the same reference on `sin`, `cos`, `tan`).

**Outside the simulation** (checked, no change): `src/patches/widescreen.c` (`atan`/`tan` for the
field of view; a per-PC setting that never desyncs PCs today, so not fight state), `picture.c` (`pow`,
the gamma table), `test_bot.c` / `test_telemetry.c` (tests, logs), the toolkit's NV2A, APU, D3D and
audio code (rendering and sound), `input_map.c` (`sqrt`, exact, before the input is sent).

## 2. Design

**One fixed implementation, used on every platform.** `src/detmath/detmath.c`: `dm_sin`, `dm_cos`,
`dm_tan`, `dm_atan`, `dm_atan2`, `dm_log`, `dm_log2`, `dm_log1p`, `dm_expm1`, built from IEEE
`+ - * /`, int/double conversions and integer bit operations only; no C library call, no errno, no
flags, no `long double`, no FMA. The algorithms and coefficients are fdlibm's (Sun, 1993/2004;
FreeBSD msun revisions), whose notice permits use, modification and distribution with the notice
kept; it is in the file, which is MIT-licensed so it can move into the toolkit and the Android port
(D-new: the layer is MIT, not on the GPL list in `NOTICE`). Nothing was downloaded: the code is
written from the published algorithms, and `tools/detmath/gen_constants.py` recomputes every constant
that derives from pi, ln 2 or 1/ln 2 with Python integers (Machin's formula) and checks every
coefficient's decimal spelling against its bit pattern (73 constants), and generates the 2/pi bits
(`detmath_tables.h`) for the large-argument reduction. Changes from fdlibm: the large reduction is a
plain integer Payne-Hanek over a 192-bit window of 2/pi; the medium reduction always runs the
cancellation check (no `npio2_hw` table); `log1p` is `log(1+x)*x/((1+x)-1)`; an invalid operation
returns the x86 default NaN on every host and a NaN operand comes back quieted.

Accuracy against the UCRT (`dm_vectors --host`, 1,000,000 inputs per function and set, six sets from
any bit pattern to near multiples of pi/2): `sin`, `cos`, `tan`, `atan`, `atan2`, `log`, `log2` at
most 1 ulp apart everywhere (both are < 1 ulp from the true value), `expm1` at most 2, `log1p` at most 3
(`log1p` is not used by this game). Speed (MSVC, /O2): `sin` 9.9 ns against the UCRT's 4.4, `atan2`
7.7 / 7.9, `log2` 7.4 / 5.1, `expm1` 5.7 / 3.5; the game makes a few thousand such calls per frame.

**Routing without touching the toolkit or the lifted code.** `src/detmath/detmath_redirect.h` is
force-included (`/FI` / `-include`, `CMakeLists.txt`) ahead of every lifted chunk and
`src/recomp_manual.c`. It includes the headers `recomp_types.h` uses and then defines `sin`, `cos`,
`tan`, `atan2`, `log2`, `log1p`, `expm1` as function-like macros naming `recomp_libm_*`, so both the
lifted calls and the `recomp_x87_*` helpers inside `recomp_types.h` reach the switch. Exact functions
are not renamed. `llvm-nm` on the built chunks: 57 references to `recomp_libm_*`, none to the host
`sin`/`cos`/`tan`/`atan2`/`log2`/`expm1`. Later (phase 2), the same rename can move into the toolkit
(`recomp_types.h` calling `recomp_libm_*`) as one fork commit, so any port that uses our runtime gets
it without the forced include.

**The switch** (`src/detmath/detmath_select.c`), read once from the environment:

| `RECOMP_DETMATH` | |
|---|---|
| unset / `1` | the deterministic functions (shipping) |
| `0` | the host libm, as before (comparison runs). Such a copy salts its build fingerprint, so it only meets other `0` copies online; `RECOMP_TEST_DETMATH_SAME_FP=1` lets a test pair it with `1` copies on purpose. |
| `2` | deterministic, and each call also computes the host's answer and counts differences (`[DETMATH] sin: N calls, M differ ...` lines every 4M calls and at exit) |

`RECOMP_DETMATH_TRACE=<file>` writes every call's operands (24-byte records, up to
`RECOMP_DETMATH_TRACE_MAX`, default 4M), which `tests/detmath` replays on other architectures.

**Lockstep version.** `LOCKSTEP_VERSION` 4 -> 5 in `src/hooks/netplay.c` (part of the build
fingerprint): fights computed with the deterministic layer are a different simulation from builds
before it. Every platform's build N switches together; build numbers already keep N and N-1 apart.

## 3. Proving a build: `tests/detmath`

- `dm_vectors.c`: runs every function over six fixed input sets (1M each by default) and prints one
  FNV-1a 64 hash of the result bits per function and set (54 lines), or replays a
  `RECOMP_DETMATH_TRACE` file. Hosted (Windows, any libc; `--host` compares with the host libm) or
  freestanding (`DM_FREESTANDING`: its own `_start` and Linux syscalls, so a static arm64 ELF runs
  under qemu-user with nothing installed).
- `check.py`: builds it with MSVC x64, clang x86-64 (`-O2`, and `-march=haswell` so FMA3 exists), clang
  arm64 (`-O2`, `-O0`) and a negative control (arm64 with `-ffp-contract=fast`), runs them (native,
  WSL, WSL+qemu-aarch64), counts fused multiply-adds in each binary and compares the hashes. Every
  build must match MSVC; the control must not.
- `inventory.py`: the class table above from the current lift; fails on a class-C call.
- `libm_compare.py`: the host-libm disagreement (run on two platforms, then `diff`).

On real Android hardware the same freestanding arm64 ELF runs as is (`adb push`, `adb shell`), or
contatg builds `dm_vectors.c` into his NDK toolchain; either must print the MSVC hashes.

## 4. Measured (build 78 + this branch, 2026-11-12)

- **Regressions.** `regress.py --quick`: golden frames m2/m3/m4a and the fight pass; the unit tests pass (1,139) once the FP guard moved from `netplay_on_step` into `step_body` (`test_fight_hud_native.py::test_wiring` pins the former). `dj.py scenario online --matches 3` (lockstep):
  3 matches, 40 step-hash checks equal. `--rollback on --matches 2`: 2 matches, 30 checks equal.
- **What the game really calls.** `RECOMP_DETMATH_TRACE` over two online matches (menus included): 1,638,400
  calls, of which `sin` 804,031, `cos` 795,881, `atan2` 32,031, `log2` 3,463, `tan` 2,962, `expm1` 32.
- **Gameplay impact.** Against the UCRT, the deterministic results differ in 1,820 `sin`, 25,044 `cos`, 25
  `tan`, 5,941 `atan2` and 7 `expm1` results of that trace (2.0 % overall, each by 1 ulp). Not one of those
  32,837 differences survives rounding to `float`, the precision the game keeps its state in (and the x87
  precision control 24 it runs with rounds every following operation to float). So a 1-ulp change reaches
  fight state only when a result lies within one double ulp of a float rounding boundary, about 2^-29 of
  the time; once it does, the fight diverges. Consistent with that, a lockstep match between a
  `RECOMP_DETMATH=0` copy and a `=1` copy (`RECOMP_TEST_DETMATH_SAME_FP=1`) ran 2 matches with 30 equal
  checks, and again 2 full-length bot fights without the fast knockout (`--fast-ko 0 --bot`, 460 equal
  checks, about 27,600 steps). A `RECOMP_DETMATH=2` run of 2 matches counted 0.3 % (`sin`, `cos`) to 20 %
  (`atan2`) of results differing from the UCRT, all by 1 ulp. Cross-platform libm desyncs are therefore rare per match but certain over enough matches; the
  layer removes the class for a cost of nanoseconds.
- **Cross-architecture proof.** `check.py` (1M inputs x 6 sets x 9 functions): MSVC x64, clang x86-64,
  clang x86-64 `-march=haswell`, clang arm64 `-O2` and `-O0` (qemu-aarch64 11.1.3) give identical hashes;
  0 fused multiply-adds in the clang binaries. The control (arm64, `-ffp-contract=fast`, 113 FMAs) differs in
  50 of 55 lines. `check.py --trace` replays the game's 1.64M recorded calls on all of them: identical (the
  control differs in 5 of 7 lines).
