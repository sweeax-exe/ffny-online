# v0.13.0 RC — macOS observations, 2026-10-31

Publication: the owner requested commit/push and PR comments. Commits `cfcc5e6` (Mac build fix and report) and `da8bcd7` (input confirmation and Windows correction) were pushed to `release/v0.13.0`. [Findings on PR #160](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/160#issuecomment-6553622809). No merge, tag or release publication. Earlier references to local-only state below describe the test-time state.

## Owner-requested input confirmation and Windows correction

The owner confirms the Windows memory gate was already resolved on Windows; it is no longer an open blocker. Older Windows failure measurements remain historical evidence.

The Mac build fix is committed locally as `cfcc5e642c3a76e2e3fbecba55280b2f2f3f4e42`; no push. No additional runtime code changed. The same executable/profile was retested on AC with temporary awake assertions, and no sleep event occurred during the retest.

| Late refresh | Result | Generated / required presses | Delivered to every downstream stage | Maximum game-step gap | Trace loss / retention discards |
|---|---|---|---|---|---|
| off | FAIL | 38 / 40 | 38 / 38 | 92.630 ms | 0 / 0 |
| on | PASS | 40 / 40 | 40 / 40 | 70.486 ms | 0 / 0 |

Off failed only pulse registration: the first scheduled pair (25.00s and25.07s after the fight anchor) never appears at `script_virtual` or in the sampled-press log. All38generated presses reached every required downstream stage. This localizes the observed loss before SDL injection; it does not demonstrate the game discarding received input. The script samples fixed20ms time windows. The exact reason those windows were missed is not yet established; the failing phase's measured sampler/host-poll gaps were only6.206/3.334ms, so a simple long sampler stall is not proven. An injection/anchor/clock investigation remains appropriate before declaring this test reliable.

On passed with5,274applied reports,17,795shadow comparisons,0mismatches and0read errors. Both fixtures were valid and both saves restored. No game or awake helper remains running. Thresholds and goldens are unchanged.

Evidence: `logs/regress-20261031-213153.txt`, `logs/rc-input-owner-confirm.json`, `logs/rc-input-owner-confirm-sampled.txt`, `logs/rc-input-owner-confirm-triage.txt`, `logs/rc-input-owner-confirm-power.txt`, and `logs/input-audit/regress-20261031-212639-late{0,1}*`. The earlier full-suite, visual and memory observations below are retained unchanged.

**The full run finished 16/21 PASS, 5 FAIL, 0 SKIP. The five-check targeted run passed 4/5. An additional awake controller pair still failed (off FAIL/on PASS).** The input gate remains open. Original failures and sleep-affected memory controls are retained. No release acceptance or waiver is implied.

## Exact candidate and build

- Fetched PR #160/release head: `5d22e8b9711a0099d58daec83956b5e9340147cc`.
- Toolkit: `d569ca10811cef67ec4e9fa2173088c2f9fd0580` (recursive submodule initialized; source clean).
- Executable SHA-256: `43b1a7d7808e4053d091e73119e5ab580f8c2c6e1391771ac6e5879f24adaa42`.
- Tested modification: owner-authorized two `(const void *)XBOX_PTR(state)` casts in `src/hooks/input_late.c:45,149`. Patch SHA-256 `d5d6bdfa72b696d4c753d5d5ee0c226dbadf11314e84aeb93ee2635092824628`; patched file SHA-256 `4687d912bef3800746d04d0895e899c6ccab0b52953314dae1db2cf659041f3c`.
- The unchanged RC fails Apple Clang compilation at those two lines, reproduced with a targeted object build. Runtime results certify the identified patched build only.
- Build: posix-release, RelWithDebInfo, arm64, Apple Clang 21.0.0 (clang-2100.1.1.101); cmake version 4.4.4, Ninja 1.13.2, Python 3.12.14.
- Fresh analysis: 18,446 functions. Fresh lift: 21 C files, 65 MiB; 64 manual addresses checked, zero missing. No generated tree or executable was reused.
- Host: macOS 26.5.2 (25F84); MacBook Pro MacBookPro18,3; Apple M1 Pro, 8 CPU/14 GPU cores, 16 GiB RAM. Vulkan device reports Apple M1 Pro / Vulkan 1.1.357.
- Libraries: SDL3 3.4.18, MoltenVK 1.4.2, Vulkan loader 1.4.363.0, shaderc 2026.4, OpenSSL 3.6.3.
- Own-dump verification passed; XBE SHA-256 `31cc0d11f7c656b1a6f337fdf7d9bbaac9df11c324a2fb881f7eb639d4be00c2`. Disposable copied saves; original installed settings/packs/save root preserved.

The first CMake attempt needed network for pinned ImGui; the authorized retry fetched the hash-checked dependency, then exposed the compiler errors. No warning override or unrelated code fix was used.

## Complete regression table

| Check | Full run | Targeted confirmation | Full-run detail |
|---|---|---|
| unit | PASS | — | 719 passed, 54 skipped, 1 deselected in 41.10s |
| m2 | FAIL | PASS | 1 indirect calls to non-code |
| m3 | FAIL | PASS | normal startup missing or out of order: Press Start; no capture (logs/run-20261031-170719.log.err) |
| m4a | PASS | — | OK: matches tests/golden/m4a-main-menu.json |
| launcher | FAIL | PASS | 1 indirect calls to non-code |
| overlay | PASS | — | opened by F1, Display category drawn, 3600x2068 window frame has the overlay |
| unlockopt | PASS | — | the checkbox saves [debug] unlock_all; from the file alone the unlock runs, and without it does not |
| picture | PASS | — | Brightness 151 and Vignette off saved by the launcher and applied when the game started |
| fight | PASS | — | 120 s of fight, median 120 presents per 2 s, minimum 84 |
| ffa | PASS | — | 150 s of fight, median 120 presents per 2 s, minimum 120 |
| ffa-terrordome | PASS | — | 242 s of fight, median 120 presents per 2 s, minimum 108 |
| combat | PASS | — | 27 of 27 assertions (7 combat)  logs/scenarios/regress-combat-20261031-173243 |
| versus | PASS | — | 26 of 26 assertions (6 combat)  logs/scenarios/regress-versus-20261031-173748 |
| replay | PASS | — | 80 records over 1800 steps match the golden hash  logs/scenarios/regress-replay-20261031-174121 |
| intro | PASS | — | cutscenes to the creator, median 120 presents per 2 s, 4 captures |
| crib | PASS | — | crib reached with the first profile |
| gym | PASS | — | Learn Moves, preview movie requested |
| soak | PASS | — | 3 of 3 boots reached the main menu |
| french | PASS | — | French table and main menu; review MENU PRINCIPAL/CHOISIR/RETOUR in logs/shots/boot |
| subtitles-en | FAIL | PASS | missing [SCENE] playing sc05 ; missing [LANG] cutscene subtitles shown (story_subtitles); too few subtitle captures |
| input-trace | FAIL | FAIL; awake pair also FAIL (off FAIL/on PASS) | rapid 20 ms paired presses through SDL/USB/XDK/fighter, refresh off/on: [True, False]; logs/input-audit/regress-20261031-175510-* |

Full report: `logs/regress-20261031-180020.txt` and its JSON/JUnit/HTML folder (53 minutes). Targeted report: `logs/regress-20261031-181550.txt` (14 minutes). Awake pair: `logs/rc-awake-input.stdout.txt` and its final regress report. Unit lane: 719 passed, 54 skipped, 1 slow deselected; these internal unit skips do not change the 0 SKIP full-suite row count. Optional `--only` scenarios outside DEFAULT were not part of the full suite.

## Failure confirmation and power qualifications

- `m2` and launcher: one call to non-code `0x81000344` from guest `0x0027EC6E`; bounded triage read; targeted checks passed. Original intermittent failures retained.
- `m3`: startup never reached Press Start, CoreAudio `AudioQueueStart -66681`; power log shows sleep 17:07:23–17:14:55 during this run. Awake targeted rerun passed all startup order checks and the unchanged title golden.
- English subtitles: initial run stopped before window/render initialization and had no captures. Power log shows sleep at 17:52:14–17:55:07. Targeted rerun passed and produced actual readable English lines.
- Input: original off PASS/on FAIL; original on missed pulses, two retention expirations and maximum step gap164.907ms. First targeted off PASS/on FAIL: on never reached fight, CoreAudio failure, sleep from18:08:37 onward. Awake pair off FAIL/on PASS confirms an intermittent registration gate failure independently of that sleep interruption.
- First memory matrix overlapped repeated sleep/dark-wake cycles with lid closed on battery. Retained raw results are not a valid settled-menu comparison: even its passing both run has a gap from286.5s to611.4s in memory samples. Lanczos timed out without a result; all never reached combat. Subsequent matrix ran on AC with verified temporary display/system/disk idle-sleep assertions. No persistent power settings changed.
- Triage digests/context/tails: `logs/rc-triage-*.txt`. Power timeline: `logs/rc-power-events.txt`. All original failed reports/logs remain local; no goldens or limits changed.

## Verified packs and profile

File-by-file receipt verification and generated corpus/manifest verification passed. Both Lanczos mip-policy folders load first, then HD Widescreen Menus 1.4.0, then HD Fighters 1.0.0. All gameplay/scenario/soak/language/controller lanes retain 16:9 and cache2048MiB; m2/m3/m4a remain stock4:3 original-image checks. Total20,946manifest entries.

| Pack | Identity | Manifest SHA-256 |
|---|---|---|
| faithful-hd-7ed266c7ccf5-opacity | 7ed266c7ccf554627c83d6505d682306526f8659493329ac5931f2cebc68c8dd | `53520744f60d05d49243d686a833e947c3937997d44e34edd129a99a4546396c` |
| faithful-hd-7ed266c7ccf5-channels | 7ed266c7ccf554627c83d6505d682306526f8659493329ac5931f2cebc68c8dd | `2ad894166f8e307f430fc48b7dfa5d14ff1f64743739da09d63750876ca15dbf` |
| menu-ui-hd-wide | 1.4.0 | `045acf19f458ac7f8ca866d5da182d16ef94fe95a547e4ff82d327c7e1b270de` |
| hd-fighters | 1.0.0 | `c45e0cb78cf5dbf1254f4303d57d7636b02a970289a767cb7772201d6ae50e56` |

- menu-ui-hd-wide archive SHA-256: `f3d79f86a024b5786919ef2d6f38b1f599cb49a151d031b4a85023a5f480848d` (1746 receipt files).
- hd-fighters archive SHA-256: `1bea1239abb8e6b86fff85ecb9fbd0d805df2b1567183e6e8a16b6f131725e0f` (221 receipt files).

Lanczos: 18,971 unique local images; 6,212,168,677bytes; 13runtime aliases; one unresolved inventory item. Opacity14,063 + channels4,921 entries. Recipe `7ed266c7ccf554627c83d6505d682306526f8659493329ac5931f2cebc68c8dd`. Corpus SHA-256 `93ce5b5ee873d83016e26ac2b82951f5154a47ba0fa88b2f28c00db29405ba33`. Local Pillow13.0.0 recipe identity is recorded separately from Windows. Receipt/corpus/cache stay together under the disposable data root.

## Visual review

- French PASS: `logs/shots/boot/boot-5s.bmp`; MENU PRINCIPAL, CHOISIR, RETOUR and the French Story description are readable.
- English subtitles PASS on targeted rerun: `logs/shots/subtitles-en/subtitles-en-16s.bmp` and `-24s.bmp`. Distinct readable English dialogue appears at those speech points. The8s image has no subtitle. Six timed images retained. Explicit all-cutscene enable overrides the owner’s disabled-subtitle preference for this test; the initial failed run is still retained.
- Review notes and capture hashes: `logs/rc-visual-review.json`.

## Controller results

| Trial | Refresh | Result | Maximum game-step gap ms | Retention discards | Reported trace loss | Applied / shadow / mismatches / read errors |
|---|---|---|---|---|---|---|
| regress-20261031-175510-late0 | 0 | PASS | 19.178 | 0 | 0 | 0 / 0 / 0 / 0 |
| regress-20261031-175510-late1 | 1 | FAIL | 164.907 | 2 | 0 | 5338 / 17856 / 0 / 0 |
| regress-20261031-180559-late0 | 0 | PASS | 17.9 | 0 | 0 | 0 / 0 / 0 / 0 |
| regress-20261031-180559-late1 | 1 | FAIL | None | 0 | 0 | 0 / 12637 / 0 / 0 |
| regress-20261031-203138-late0 | 0 | FAIL | 23.886 | 0 | 0 | 0 / 0 / 0 / 0 |
| regress-20261031-203138-late1 | 1 | PASS | 18.907 | 0 | 0 | 5198 / 17719 / 0 / 0 |

Required: all40presses through9stages, no retention discard, no reported trace loss and max gap≤100ms. Awake off observed39/40 from script_virtual/host_mapped onward (phase counts19+20); on observed40/40. Trace quality is checkpointed-window evidence; harness termination leaves uncheckpointed tail rows excluded, not a fully closed trace. Full metrics/CSV/checkpoints/reports: `logs/rc-controller-summary.json` and `logs/input-audit/regress-*`. No claim that the exact original164.907ms symptom was reproduced by the later missed-press failure.

## Memory comparison

Same combat fixture: `logs/fixtures/auto-20261031-173243`; all three comparison groups use seed20261031, same executable,16:9/cache2048MiB, `--result-tail 60 --hold-menu`. POSIX samples are RSS although the generic schema calls them private_bytes. Compare only within this host; decreases may reflect residency/paging and do not prove absence of allocation leaks. The64MiB gate remains unchanged.

Initial full-suite combat gate PASS: baseline738.77MiB, peak738.77MiB, final511.58MiB, growth-227.19MiB.

| Awake group | Scenario | Baseline MiB | Peak MiB | Final MiB | Growth MiB | Settled menu samples / growth MiB / slope MiB/min |
|---|---|---|---|---|---|---|
| both | PASS | 391.45 | 428.42 | 255.16 | -136.3 | 7 / -7.66 / -7.803 |
| lanczos | PASS | 259.72 | 368.5 | 210.44 | -49.28 | 8 / -13.77 / -20.483 |
| all | FAIL | 258.42 | 417.97 | 207.48 | -50.94 | 7 / -7.28 / -7.246 |

| Awake group | Fight growth MiB | Result/menu transition growth MiB | Fight RSS slope MiB/min | Failed assertions |
|---|---|---|---|---|
| both | -120.55 | -15.75 | -73.379 | none |
| lanczos | -30.33 | -18.95 | -11.185 | none |
| all | -32.16 | -18.78 | -1.339 | audio.dry_queue |

Awake memory reports: `logs/lanczos-memory-comparison-awake/{both,lanczos,all}/report.json` with HTML/JUnit/captures, `summary.json`, `plan.json`, `verified-packs.json`. Assertions retain independent failures.

| Sleep-affected group | Scenario result | Raw growth MiB | Limitation |
|---|---|---|---|
| both | PASS | -425.64 | missing settled-menu sampling due to long gap |
| lanczos | FAIL | -743.88 | no match result |
| all | FAIL | None | combat never reached; no valid growth measurement |

First matrix retained under `logs/lanczos-memory-comparison/`; qualified as sleep-affected, not replaced or called green.

## Combined-pack confirmation

The awake combined control failed only audio.dry_queue (one dry interval), while its memory-growth gate passed. One same-fixture/seed/budget/menu-hold confirmation returned PASS: growth-298.48MiB; settled menu-77.88MiB over8samples. Failed assertions: none. Both outcomes retained; the first failure is not waived.

Confirmation: `logs/lanczos-memory-all-confirm/{summary.json,all/report.json,all/index.html}`.

## Saves, hygiene and remaining gates

Save-restoration verdict: source unchanged=True; disposable unchanged=True; 21baseline files verified; unresolved guards=[]. Every scenario/input report checks complete restoration. Final verification: `logs/rc-save-verification.json`; baseline: `logs/save-baseline.json`.
- No original-image golden changed (`logs/rc-golden-identities.json`). No memory limit change. No unrelated code fix.
- Local generated textures, proprietary game data, saves, raw logs, captures/audio and the built executable are ignored and untracked. Original checkout preserved. No merge, tag, push, release publication, or setup install/Clean Update.
- Setup install/Clean Update and Linux/other release gates remain separate. Mac RC still has an input-registration failure; these observations do not authorize a release.

## Main local report paths

`logs/rc-macos-report.md` / `.json`; `logs/rc-identities.json`; `logs/release-profile.json`; `logs/rc-controller-summary.json`; `logs/rc-visual-review.json`; `logs/lanczos-memory-comparison-awake/summary.json`; `logs/regress-20261031-180020.txt`; `logs/regress-20261031-181550.txt`; `logs/rc-awake-input.stdout.txt`.
