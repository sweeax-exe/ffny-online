# v0.13 compiled fixture portability

Read-only review of the candidate toolkit at `C:/Users/Vlad/code/defjam-upstream013/tools/xboxrecomp` on 2026-10-07. Parent reports the completed MSVC-adapted tools gate as 627 passed, 28 skipped, 57 subtests. This review did not run tests, builds or the game, and changed no toolkit files. Source inventory explains the 28 skips exactly when GCC/Clang are absent and the conformance vcvars32 discovery is unavailable: 26 tests behind compiler probes plus two x86 conformance tests. Test-level log results remain the authoritative execution evidence.

## Compiler paths requiring portability

| Module | Probe / build path | Affected test methods or functions | Smallest change |
|---|---|---|---|
| `test_icall_guarded_runtime.py` | `_cc` at 42; `_run` at 131; GCC flags only | `test_guarded_site_matches_generic_dispatch_for_every_target`, `test_runtime_site_dump_round_trips_through_the_merge_parser` (2 skips) | Use shared compiler selection and a dialect-aware build adapter. First harness can use shared `_build_and_run`. Recorder needs two TUs, RECOMP_ICALL_FEEDBACK definition, kernel include directory, execution in the supplied temporary directory and retained dump files for parser assertions. Do not lose this artifact contract by naively calling a helper that cleans its directory before returning. |
| `test_lifter_double_shift.py` | local `_build_and_run` at 107 | `test_matches_x86_at_every_count` and `test_negative_control_the_old_expression_is_caught`, each SHLD/SHRD (4 skips) | Replace local builder with imported shared `_build_and_run`; leave sweeps and negative-control exit/output checks intact. |
| `test_sar_width.py` | inline Clang/GCC probe at 9 and build at 46 | `test_sar_width_and_carry_compiled` (1 skip) | Replace inline build/run with shared helper; add RECOMP_PARITY8 to prelude. |
| `test_result_sign_width.py` | inline probe at 36 and build at 80 | `test_result_sign_uses_operand_width` (1 skip) | Replace inline build/run with shared helper; add RECOMP_PARITY8. |
| `test_incdec_result.py` | inline probe at 12 and build at 54 | `test_saved_incdec_result_compiled` (1 skip) | Replace inline build/run with shared helper; add RECOMP_PARITY8. |
| `test_x87_classification.py` | inline probe at 13 and build at 54 | `test_classification_status_compiled` (1 skip) | Shared helper for Windows; preserve non-Windows libm linking (`-lm`) capability if helper is made common. Keep existing Intel status-pattern checks. |
| `test_lifter_logic_above_below.py` | `_find_cc` at 91; class setUp; `_sweep` at 103 | All four class methods skip, including two text-only checks | Shared builder in `_sweep`; remove compiler-only class-wide setUp gate or allow shared CL selector. Keep pre-fix constant negative control. Source-only checks should run even without a compiler. |
| `test_lifter_sar_width.py` | `_find_cc` at 125; class setUp; `_run` and standalone AL=0x80 build | All five class methods skip, including text-only `test_count_is_masked_to_five_bits` | Route `_run` and standalone AL build through shared helper. Keep old-expression negative control and count masks; move compiler requirement out of text-only check. |
| `test_lifter_sf_overflow.py` | two inline compiler probes at 88 and 139 | `test_the_emitted_expression_is_right_at_O2`, `test_the_old_form_is_undefined_and_the_new_one_is_not` (2 skips) | First test can use shared optimized builder (/O2 on MSVC). Second specifically requires GCC/Clang UBSan signed-integer-overflow instrumentation: retain explicit unsupported-sanitizer skip on MSVC. Do not replace its cause-of-UB negative control with an ordinary execution. Preserve GCC `-fstrict-overflow` and sanitizer flags on capable compilers. |
| `test_reserved_idents.py` | `_find_cc` at 56; `_compile` at 72; class setUp | Four class methods skip, including two source-only checks | Share compiler selection but retain a compile-only result-returning adapter: `/c`, `/Fo:` for MSVC versus `-c`, `-o`; combine stdout/stderr. Negative controls REQUIRE compilation failure, so assertion-based `_build_and_run` cannot replace `_compile` directly. Keep Microsoft CRT detection and onexit case. |
| `test_lifter_frndint.py` | inline probe at 38; build at 88 | `test_each_rounding_control_rounds_its_own_way` (1 skip) | Shared compile/run adapter with runtime include directory and platform math link flag. Preserve original source and all rounding/signbit/FIST checks. Distinguish GNU strict-warning flags from MSVC options. |

Compiler-probe subtotal: 2+4+1+1+1+1+4+5+2+4+1 = 26. Five of those skips are source-only methods unnecessarily behind class compiler gates (logic:2; SAR:1; reserved names:2). The UBSan check is a separate compiler capability requirement; ordinary CL availability cannot satisfy it.

## Two silent non-executions counted as passes

- `test_dispatch_flat.py:test_generated_dispatch_flat_matches_binary_search` (probe at 102-106) prints SKIP and returns normally when no Clang/GCC exists. This is not a pytest skip. Compile the generated dispatch TU plus harness and stub header with shared CL selection and a dialect-aware multi-source/include adapter. Shared `_build_and_run(..., extra_sources={...})` can hold generated TU and header, provided the source directory is included (or the harness includes the generated TU). Retain binary-search/flat identity checks and output assertion.
- `test_fpu_branch.py:test_idiom_semantics_compiled` (125-132) also prints SKIP and returns normally. Route C_HELPER through shared `_build_and_run`, preserving exit and OK output assertions. Its existing prelude already supplies RECOMP_PARITY8.

A count such as 627 passed therefore does not by itself establish that these two compiled checks executed. Report them as unexecuted until compiler routing is fixed and the gate reruns.

## Named modules already portable

`test_rep_movs_mmio.py:_build_and_run` (70), `test_icall_caller_cleans.py:_build_and_run` (85), `test_lifter_pushal.py:_build_and_run` (61), and `test_entry_hooks.py:test_direct_and_indirect_entries_execute_hook_before_body` (82) already choose CL and emit `/nologo /W0 /O2 /Fe:`. They can be deduplicated onto the shared helper but need no compiler-availability fix. Their reviewed fixture bodies do not require an extra parity macro: REP MOVS uses a non-dynamic Lifter, entry-hook fixture has no flag consumer, and caller-cleanup bodies have no guest conditional/SET/CMOV/LAHF reader. Existing `test_lifter_rep_compare_flags.py:_build_and_run` already delegates to the shared helper and its prelude includes parity.

## Missing dynamic parity scaffold

New translated conditional functions now publish `_pf` and emit `RECOMP_PARITY8` calls even when the particular assertion only reads sign, carry or overflow. The three skipped standalone translated tests (`test_sar_width`, `test_result_sign_width`, `test_incdec_result`) omit the macro and will encounter a compile/link problem when enabled. Add the same implementation-side macro supplied by `test_dynamic_flags.PRELUDE`:

```c
#define RECOMP_PARITY8(v) ((0x9669u >> (((v) ^ ((v) >> 4)) & 15u)) & 1u)
```

Keep their independent answer oracles unchanged. The direct-Lifter double-shift and old SAR suites do not enable dynamic flags and do not need `_pf` declarations or this macro. The x87 classification fixture also uses Lifter defaults rather than dynamic publication.

## Separate x86 conformance skips

`tools/conformance/test_conformance.py:ConformanceTest.test_lifted_code_matches_the_cpu` requires discoverable vcvars32 or an explicitly opted-in linux/386 container; `tools/conformance/test_fuzz.py:test_runner_accepts_flag_preserving_nop_noise` requires vcvars32. These are the remaining two reported skips. They compare real assembled x86 with lifted behavior and must not be routed to a generic x64 C fixture helper. Run the planned real MSVC x86 conformance gate separately and report its evidence separately.

## Suggested minimal implementation order

1. Reuse existing shared `_cc` / `_build_and_run` for ordinary single-source executable fixtures; avoid new independent compiler probes.
2. Add the three parity prelude definitions; retain all original expected answers and negative controls.
3. Handle recorder artifacts, multi-source includes and compile-failure controls through a small shared lower-level compilation adapter or small local dialect adapters. A helper that always asserts successful compilation or destroys output files cannot satisfy these tests unchanged.
4. Make the two print-and-return branches real execution under CL (or real pytest skips if no compiler), and stop skipping source-only methods at class setup.
5. Rerun serial tools pytest with reasons; expect only explicit sanitizer and separately handled x86 capability skips once portable fixtures execute. Do not weaken a failing negative control to obtain a green count.
