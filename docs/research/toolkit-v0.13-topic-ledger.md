# xboxrecomp v0.13.0 topic ledger

Accounting review on 2026-10-07 while the main agent runs the serial game matrix. Read-only Git/source inspection; no builds, tests, game runs, code changes or Git mutations were performed for this ledger. This document is a source mapping, not a fresh behavioral acceptance verdict.

## Exact inputs and counting

- Original private game parent: `e39cefba902823d4fc40ae29acb8d4e8e7f2cbd0`.
- Original toolkit pin: `2ac8e705c453854079f1da7cd9af3250d37b0259`.
- Original common upstream base: `1409a7d7801d3e931fb1074be6104209ddd9a33e`.
- Candidate release base: exact upstream v0.13.0 `b3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4`.
- Candidate toolkit checkout: `C:/Users/Vlad/code/defjam-upstream013/tools/xboxrecomp`, branch `defjam/upstream-v0.13`.
- Observed committed replay tip: `bec01fa2e8407afcdf61165403f16ed488aca76f`, followed by uncommitted compatibility changes.

The original base-to-pin history contains exactly 26 topic commits. Candidate release-to-HEAD history contains exactly 22 replay commits. Unique commit subjects establish a one-to-one accounting match for those 22; the four unmatched original subjects are the production-equivalent PRs #167-170 described in the reviewed migration plan. #171 is only part of the broad retained kernel topic and therefore is not a fifth omitted commit. All 11 M6 settings/presentation/input/overlay commits are accounted for (rows 16-26).

## Complete topic mapping

The destination column names the replay commit or upstream implementation commit, never the release's merge wrapper. Full SHAs make this mapping independent of abbreviated ref ambiguity. "Retained" records source disposition; it does not mean the replay commit alone is the final tested candidate.

| # | Original toolkit commit | Original topic | Candidate destination | Disposition / residual |
|---|---|---|---|---|
| 1 | `193db87c18662076fca08ee81d5099238e8dc014` | fix(recomp): preserve manual entry hooks through boundary repair | `7f1b2636d9e671c94212c5a8864368f878b52c29` | Production equivalent upstream #168; no replay. |
| 2 | `af45945a56e70dc2aefadfc143f3d8656f92cd96` | fix(recomp): carry PUSHAD and POPAD through flag and continuation tracking | `1ba591e85d6f5f182ae262218699a62d26b3b9ad` | Production equivalent upstream #167; helper residual retained through upstream support plus ed816139. |
| 3 | `e53263f6147e97caabf9c185600af283e2631da3` | fix(recomp): classify caller cleanup before emitting guarded calls | `d88cbeaf4644751f9a06a50ca7b23a042fcb816b` | Production equivalent upstream #169; no replay. |
| 4 | `0352cc7c1a7e4b4994299aa9e91ab1ecd9addc3a` | fix(recomp): preserve flags across mixed CFG joins and string comparisons | `4a0eb5f55534b38222eb0d9e165a22e2e40d8403` | Retained; mixed/float joins require additional uncommitted integration. |
| 5 | `032c9f2aecaafda28b96b846d28175e54036404b` | fix(recomp): preserve volatile REP copies across MMIO ranges | `5e1d2ed60defb6e0bf48616a62d96a50e93b1b47` | Production equivalent upstream #170; no replay. |
| 6 | `8e5c73b02e1077d7a196ed59a37055127d3ba283` | fix(recomp): consume published flags and complete partial flag writers | `c1b3d26a9843604b7cc7873990c646e07f7b9db1` | Retained; parity/partial writers/float and SAHF/LAHF require additional uncommitted integration. |
| 7 | `59f3d7aedc729550b08bf298c482fbee0b8678d0` | fix(recomp): emit width-correct IN and OUT runtime calls | `c220abab2ec3665f6eacf3b429a36e4b47312fab` | Retained replay topic. |
| 8 | `7f7eef97092468d87fde8552d03a95ef49fe4653` | fix(recomp): rescue proven switch slots and report indirect tail sites | `ed81613991701ab70fafecb2d8feda723b02b76d` | Retained; includes multiple-TU/MSVC shared fixture residual; backward census amended locally. |
| 9 | `c6140ae105fb1f921a1e48f00892c4bed8fe312c` | fix(recomp): retain proven switch slots across foreign arm ownership | `48268f55c01e22740b36e63091723f5c91e18e0c` | Retained; foreign-arm backward switch coverage amended locally. |
| 10 | `799c19b2aa53a19503be1c960716b2c97ca0448d` | fix(kernel): retain guest object, IRQL, DMA and file contracts on upstream | `99a6efa5c9aafa567cca8bfab857ce5cd6ca9bf1` | Retained kernel residual; #171 directory cleanup deduplicated; event/live-stat integration remains uncommitted. |
| 11 | `f555959ed6c2bd899ce6ae4cc2981849814f3d4b` | fix(runtime): preserve ordered pusher progress and guest interrupt ownership | `7bf9ad59b5a5e4172bdf6acdb744fbede7e54d4a` | Retained replay topic. |
| 12 | `710867cfdec687ac13b6025d2c7ba22e8c554cea` | fix(apu): unify physical translation, DSP acknowledgement and audio modes | `682f51988831dfc05f2621b02a71478be6387f4f` | Retained replay topic. |
| 13 | `faa28f8748868e59ba9e4f8e74a3cd6d77ae171e` | fix(usb): combine Controller S hub transfers with indexed pads and scripts | `89c3b1da40c69ee53609c709e498746d740f7af6` | Retained with upstream OHCI short-packet behavior. |
| 14 | `700d2b644b1a67ef1fbd62871ca0ee5fd742620c` | feat(nv2a): share canonical vertex programs between interpreter and HLSL | `e5557ed1b4193d1c094403243edb424342b5323d` | Retained replay topic. |
| 15 | `aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc` | feat(d3d11): preserve hardware pushbuffer forwarding and rendering state | `05db81c5f1483c846cf0bd740f5dd6e62b531d3a` | Retained replay topic. |
| 16 | `98e9791cb074b3076bddc5580ac0348c473997cc` | feat(settings): add a typed settings table backed by one INI file | `e2bbfffafa6433dd59c24efb59bb2fb25236d162` | Retained replay topic. |
| 17 | `424532558753487254322ac13b92b935cf74d074` | feat(d3d11): add opt-in scaled presentation that follows the window | `26f27477473b26624967aaf8b3776fa23d4156f7` | Retained replay topic. |
| 18 | `c7059bf26856660e669c7991559a649fd508a17a` | feat(d3d11): flip-model swap chain and display-paced vsync for scaled presentation | `c4e5af1069dddc272f0ea23749d72e347d6caeae` | Retained replay topic. |
| 19 | `f11f4462b231e645c152175757380419e279020d` | feat(settings): add a free-text setting type | `3ba2153397c5da98baf3f52f60b55c6152a69ece` | Retained replay topic. |
| 20 | `feeb50bd4ee9e5d34b36aadb755eafeae1c78456` | feat(input): binding, deadzone and pad-remap rules as plain C | `ada3278565e097f534f64f5bad58e0ac2874f9db` | Retained replay topic. |
| 21 | `ac3c64c5ff5dd8dda291ccad3a42be07375d68ac` | feat(input): host input layer - SDL3 and XInput pads, keyboard player, focus, rumble | `f1dc85b2b7f5aa2e4415714239e4d00ac94271d5` | Retained replay topic. |
| 22 | `ee465abb08cfce72d65887e67eba0951e7081dc0` | feat(usb): pass rumble to the host pad; route polling through the host input layer | `4c495ba7b7a04733a324ef9e06e564b792bca3ab` | Retained replay topic. |
| 23 | `8e6e8ef5d5eecd3f63bed26afc3e6d06c4203367` | feat(input): test hooks - no pads, ignore focus, scripted pad as keys | `4a0cb7ea3e737d159edf967e9084f05192f7c222` | Retained replay topic. |
| 24 | `73cb28c475e2b106247afcfd6d6d9fd4a95116a2` | feat(usb,input): plug a controller into the hub when a pad arrives mid-game; rumble diagnostics | `e705a1911e0633dbd20016872edf34bd3f6ddda1` | Retained replay topic. |
| 25 | `b1f002dee5be08b1f96d2247e3435c7536192cf7` | feat(input): make the title's faint, brief rumble felt; buzz a pad when it connects | `6d9d23e21572733a7f7d4ac4eeb4ec89ea601a36` | Retained replay topic. |
| 26 | `2ac8e705c453854079f1da7cd9af3250d37b0259` | feat(settings,d3d,input): reset to default, an overlay hook in the present step, input hooks for a host menu | `bec01fa2e8407afcdf61165403f16ed488aca76f` | Retained replay topic. |

## Equivalence and residual checks

The first five submitted PR implementation commits all exist in the release ancestry: #167 `1ba591e85d6f5f182ae262218699a62d26b3b9ad`, #168 `7f1b2636d9e671c94212c5a8864368f878b52c29`, #169 `d88cbeaf4644751f9a06a50ca7b23a042fcb816b`, #170 `5e1d2ed60defb6e0bf48616a62d96a50e93b1b47`, #171 `a4e0f361866a4795f22e8cebc77c6d0d506277eb`. The production-equivalence decision comes from the prior detailed source review in [migration plan](toolkit-v0.13.0-migration-plan.md); patch IDs do not identify the four as equivalent because the original topic scopes include adapted code/tests.

For the PUSHAD/POPAD residual, the original `af45945` also touched the shared compiled fixture. The later retained switch replay `ed81613991701ab70fafecb2d8feda723b02b76d` explicitly adds extra translation-unit support to `tools/recomp/test_lifter_result_clobber.py`. The normalized UTF-8 source hash is identical in old and candidate checkouts: `c33ecc8c573b4690df4b454ace87c99c57f8910323bb0860a8ee6871a6697a1b`, matching `scripts/test-toolkit-msvc.py`'s approved fingerprint. The separate new `tools/recomp/c_fixture.py` is an additional untracked compiler-dialect helper, not that historical shared helper.

For directory search, candidate replay `99a6efa5c9aafa567cca8bfab857ce5cd6ca9bf1` changes `kernel_file.c` by renaming the upstream public implementation to private `query_directory` and adding its budgeted logging wrapper. It does not replay another copy of the upstream search-drop implementation. In the Windows backend the current file has one public wrapper at line 749 and the private implementation at line 640; the second public definition at line 1190 is in the separate POSIX conditional branch. Broader guest-object, IRQL, DMA/file/path contracts remain in this replay; omitting the entire old kernel topic would lose them.

## Pending local compatibility layer

At inspection the candidate has 30 tracked modified toolkit paths plus two untracked files. These edits are outside the 22-commit replay mapping and must receive reviewed source commits before the candidate can be published or pinned. The [execution record](toolkit-v0.13.0-execution.md) is the source of main-agent test results; the following inventory does not independently rerun those tests.

| Additional integration | Uncommitted locations | Relationship to ledger |
|---|---|---|
| Explicit flag-join consumer plans, parity snapshots, scalar float/double EFLAGS compares, SAHF/LAHF, FPREM/FPREM1 completion C2 | `tools/recomp/lifter.py`, `translator.py`, flag/FPU/x87 tests; untracked `test_flag_union.py` | Reconciles upstream flags with rows 4/6 and adds discovered correctness repairs; not contained solely in their replay SHAs. |
| Backward displacement-slot census and compiled damaged-slot/foreign-arm coverage | `tools/recomp/lifter.py`, `test_switch_slot_fallback.py` | Follow-up to rows 8/9 and new release table behavior. |
| Shared preparation/consumption of header-backed events for single/multiple waits | `src/kernel/kernel_bridge.c`, `tests/kernel_events/README.md`, `test_main.c` | Residual integration for row 10. Sequential fixtures do not prove concurrent event/pulse races solved. New opt-in event mode stays disabled during default game acceptance. |
| Separate live contiguous allocation accounting from high-water physical range | `src/kernel/kernel_memory.c`, `xbox_memory_layout.c/.h`, `tests/memory_regressions/test_main.c` | Residual integration for row 10/new upstream reclaim mode; preserves default high-water behavior when reclaim is absent. |
| Compiler-dialect coverage and indirect-tail TLS scaffold | Modified compiled probes under `tools/recomp`, three `tools/conformance` harnesses and `cases.py`; untracked `tools/recomp/c_fixture.py` | Acceptance infrastructure beyond historical helper retention; must be staged explicitly. |

The exact 30-path modified list is available from `git status --short`; it includes both production files and the existing fixture modifications. Untracked `c_fixture.py` and `test_flag_union.py` are necessary to preserve the reviewed working candidate, so committing tracked changes alone is incomplete. No staged or committed semantic-fix SHA exists at this checkpoint.

A separate candidate-parent AC97 publication/protection startup-window repair and its deterministic native regression belong to the game checkout, not these 26 toolkit topics. Record those exact source commits and tests in the parent migration history; do not hide them inside the toolkit-equivalence claim.

## Publication and acceptance still pending

The candidate release base plus `bec01fa` alone is not the reviewed build input: final compatibility changes remain uncommitted. Preserve exact final source/tree/build identity after those changes are committed. Publish the reviewed source-only toolkit branch/commit before updating a parent gitlink. The old published `defjam/m6` history and accepted pin remain rollback references; no rewrite of them is implied by this ledger.

At the reviewed execution checkpoint, source/native/x86 and fresh Release/Debug build gates are recorded as passing, but the complete serial candidate game matrix, host-input/overlay checks, owner physical-device/listening test, fresh-clone reproducibility, CI and publication/pin update remain pending. The root agent will update execution results as gates finish. Do not convert historical passing baseline results or this accounting table into candidate acceptance.

Default game acceptance leaves `RECOMP_HEAP_RECLAIM` and `RECOMP_EXT_VMA` absent (presence enables them even if set to 0), and uses `RECOMP_TITLE_KEVENTS=0`, `RECOMP_GUEST_LOCK=0`. Optional new feature experiments remain distinct from consuming the release.

## Review limits and reproducibility

This bounded review verifies history accounting, exact destinations, helper retention and directory deduplication. It does not re-establish every production equivalence or re-derive tested semantics. The migration plan records those decisions and the execution record records test evidence. Repository state can change while the main agent continues; refresh this ledger's compatibility/publication checkpoint before final acceptance.

Read-only history collection used per-command `git -c safe.directory=<exact-toolkit> -C <exact-toolkit>` with `log --reverse --format=%H|%s <base>..<tip>`, plus focused `show`/working-tree `diff`/status. No Git mutation or archive-patch replay occurred.
