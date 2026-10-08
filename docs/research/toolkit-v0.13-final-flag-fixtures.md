# v0.13 final flag fixture review

Read-only review on 2026-10-07 of the prepared candidate at `C:/Users/Vlad/code/defjam-upstream013/tools/xboxrecomp` (base `b3700e1`, replay tip `bec01fa`, uncommitted integration fixes). Reviewed `test_flag_union.py`, `test_dynamic_flags.py` and corresponding `lifter.py` / `translator.py` changes. No code edits, builds, tests or game runs performed. Line references describe the reviewed working copy and may move during subsequent edits.

The prepared compiled fixtures use independent expected values: Python integer wrapping, signed ranges and low-byte `bit_count()` parity; Python ordered/unordered floating comparisons; and explicit architectural AH bit masks. They do not derive expected answers by calling the lifter's condition generator. Sharing the generated-code prelude and extracting runtime x87 macros supplies the implementation being exercised, not the expected-value oracle. Float-publication and double-lane mutation controls additionally require deliberate broken generated code to fail. No obvious remaining compile scaffold omission was found after the supplied SET_HI8 fix. This is source review, not confirmation that compilation or execution succeeds; require the queued serial compiled gate to execute without unexpected skips.

## Focused findings

1. **Mixed-join SETP result is overwritten before assertion.** `test_flag_union.py:33` emits `0f9ac1` (SETP CL), immediately followed by `b901000000` (MOV ECX,1). Checks at lines 49-50 assert SETB DL, SETE DH, CMOVP EAX and LAHF AH, but never the SETP result. A broken mixed-join SETP could pass this fixture. Preserve its result in an independently checked register or memory location before overwriting ECX. The separate nonzero-shift SETP fixture does test SETP directly, but does not close this mixed-join case.

2. **SAHF's OF preservation is not checked.** Lines 62 and 71-72 sweep all AH inputs and intentionally pin only supported SF/ZF/PF/CF and bit 1. This independently tests AH decoding and rules out the legacy g_fp_cmp path, but no producer seeds OF before SAHF and LAHF cannot report OF. Add seeded OF=0/1 cases with SETO/SETNO after SAHF if claiming the emitter's `sahf preserves OF` contract (`lifter.py:1603`) is validated. This is a coverage gap, not an observed emitter bug.

3. **The x87 preservation fixture does not prove the second FCOM executed.** Lines 108 and 115-117 exercise FCOMIP; FCOM; LAHF and check first-compare AH plus one pop. These checks can detect accidental EFLAGS overwriting by the second comparison, but an omitted/no-op FCOM also passes. Seed x87 status with a sentinel and assert the second comparison updated it. For the existing inputs, after the pop FCOM compares b against -123: non-NaN b should give g_fp_cmp=1 and g_fp_cc=0; NaN b should give g_fp_cmp=2 and g_fp_cc=0x4500. Expectations can remain independent explicit constants.

## Coverage that looks appropriate

- Mixed integer/SSE incoming edges include MOV operand clobbers, ordered comparisons, NaN, multiple consumers and LAHF. The desired outputs are computed before generated code runs.
- Double-register UCOMISD reads `.d[0]`; values such as 1.0/2.0 have equal low float words, so the deliberate `.f[0]` mutation is discriminating.
- Nonzero SHL/SHR/SAR parity sweeps include counts 1,2,7,31 and values producing both parities. Counts 0 and 32 preserve parity seeded independently by CMP. Existing dynamic fixtures also check zero-count destination/CF/SF/ZF/OF/PF/validity preservation across 8/16/32-bit operands.
- Extending CONDITION masks and the REP zero-count expected mask to include JP/JNP is coherent; expected parity uses Python low-byte population count rather than RECOMP_PARITY8.
- The x87 fixture's independent AH oracle and TOP assertion do cover FCOMIP compare order/unordered behavior and one pop. Runtime fp_top/fp_pop macros are emitted by the translator; the fixture provides the referenced stack/status globals and comparison macros.

These findings do not request a broader lifter redesign. Address the small fixture holes and run the compiled toolkit gate serially after the baseline, as already queued.
