# Startup skip: settings and verification integration

2026-10-17. Read-only source/log review at game source `9c17309`. This report does not establish that any proposed startup bypass works. It covers settings, build certification and safe play-test design; the game-flow investigation owns the actual bypass.

## Recommendation

Add `[debug] skip_intro = false`, overridden by `RECOMP_SKIP_INTRO`, as a restart setting beside the existing `unlock_all` setting. Label it “Skip startup movies and legal screen” and explain that it starts at Press Start on the next game launch. Keep the startup-only behavior in game-specific code under `src/patches/`, default off. Do not implement it as a global movie decoder/stream suppression: the Story route must retain its two opening movies and subsequent cutscenes.

## Exact source integration

| File / symbol | Change and reason |
|---|---|
| `src/hooks/pc_ui_settings.c`, `pc_ui_settings()` | Add `debug/skip_intro`, BOOL, default `0`, env name `RECOMP_SKIP_INTRO`, flags `RECOMP_SETTING_RESTART`, and helpful startup-only text beside `debug/unlock_all`. The function reserves eight entries and currently returns five, so one additional entry fits without changing its existing bounds. |
| Game-specific startup patch | Read `recomp_settings_get("debug", "skip_intro", 0)` on the first screen selection, after launcher completion. This uses the registry's boolean parsing and file/environment precedence directly; no additional export in `pc_settings.c` is needed. Explicit environment `0` or `false` remains authoritative. |
| `src/hooks/pc_ui.cpp`, `friendly()` | Add the human-readable label for `debug/skip_intro`. |
| `src/hooks/pc_ui.cpp`, `page_general()` | Include `skip_intro` in the existing Debug group, preferably with separate startup help from the unlock warning. The generic widget already locks environment-controlled settings and displays restart hints. |
| Game-specific startup patch | Consume the first selection even when skipping is off. Never re-read the live UI setting during Story or later returns to the front end. Overlay edits save for the next process. |

The settings library already handles default < INI < environment precedence, boolean parsing, commented-file generation and persistence. `pc_settings_init()` rewrites a complete settings file when a release adds registered settings. Its UI reset loop iterates the registry automatically. No additional custom parser or settings save path is needed.

Generic UI restart tracking snapshots fields with `RECOMP_SETTING_RESTART`; launcher completion clears those snapshots before the overlay, so a startup selection is correctly treated as applied. `setting_changed()` should not gain a skip callback: that would invite a mid-game behavior change.

## Build and certification

`CMakeLists.txt` uses `CONFIGURE_DEPENDS` globs for `src/hooks/*.c`, `src/hooks/*.cpp` and `src/patches/*.c`. A plain-C patch is included automatically. Existing settings/UI edits need no CMake change.

`scripts/pipeline-state.py:build_inputs()` hashes all project `.c`, `.cpp`, `.m`, `.h`, `.inc` and `.hlsl` source files plus CMake inputs. It also hashes runtime sources and the lift state. No certification file-format or allowlist change is necessary for ordinary C/C++ changes. `scripts/pipeline.py build` records and verifies those inputs; the harness refuses a stale executable.

If adding an existing `funccall_hook()` call or any entry hook in `src/recomp_manual.c`, its hash is also a lift input. The normal pipeline therefore requires re-lifting before certification/build. Do not hand-edit generated code or falsify the recorded hash to avoid the re-lift. The actual startup patch may require generated instrumentation; that is a separate dependency from the settings registration.

## What must be measured

The historical log `logs/run-20261009-122621.log.err` shows this startup order:

1. `Control.GetScreenInfo(intMain/legal1.swf)` (line 1827).
2. `Control.GetScreenInfo(intMain/bootLoad.swf)` (2683).
3. Startup movie opens: `eagames.mad`, `thx.mad`, `intro.mad` (2768, 3358, 4195).
4. `Control.GetScreenInfo(intMain/preStart.swf)` (22720), the Press Start screen.

Use the first `Control.GetScreenInfo(...)` from the full `[FUNCCALL]` stream as a positive startup assertion: with skipping enabled it must be `intMain/preStart.swf`, and legal/bootLoad screen requests and startup movie opens must be absent. Also capture and inspect an actual rendered Press Start frame. A log alone cannot prove that an intermediate image was never displayed. The loading crest may still be needed while resources initialize; the requested result should be described as the first front-end screen unless play-testing proves stronger behavior.

`harness.summarise()` already collects the ordered `calls` list. A small pure parser can select GetScreenInfo calls and support meaningful synthetic tests for order, an earlier legal screen, missing target and misleading non-FUNCCALL text. Keep observations bounded to startup; rejecting every movie open in an entire Story run would encode the wrong behavior.

The existing `boot` route proves eventual main-menu entry, not first-screen identity. `regress.check_m3()` compares a captured title frame with `tests/golden/m3-title-screen.json`; reuse that golden without relaxing its thresholds. The existing `TITLE_ANCHOR = feflow.xml#2` assumes the flow is loaded twice. A bypass that omits the first legal flow can invalidate it. For focused skip runs use the explicit `Control.GetScreenInfo(intMain/preStart.swf)` anchor after confirming its observed spelling. Avoid changing all established routes until the bypass's actual load sequence is known.

Suggested checks:

- Default OFF and explicit environment `0`: preserve legal/movie order and the existing goldens.
- Environment `1`: first requested front-end screen is preStart, no startup movies, stable rendered title, Start reaches main menu, then a playable match.
- INI true with no override: same result. INI true plus env `0`: original startup. INI false plus env `1`: skip.
- Launcher checkbox: actual UI interaction saves `[debug] skip_intro = true`, Play applies it immediately. Overlay edit only changes the next launch.
- Skip ON with `intro`: create a fresh user ID, verify Story starts, both Story movies actually decode/render, and the creator appears. Continue an established Story profile into crib/gym or a fight as an additional transition check.

`scripts/regress.py:check_unlock_option()` is the closest existing UI/settings/run template. It clears the harness's unlock override before clicking its checkbox and checks file-only activation separately. A new skip check must likewise avoid inheriting `RECOMP_SKIP_INTRO` accidentally; `run_game(..., inherit_recomp=False)` or explicit per-run env values make cases reproducible. Current ordinary harness runs inherit arbitrary RECOMP variables unless told otherwise. Do not make the harness default skip intros: the baseline must continue covering the ordinary startup.

`tests/unit/test_input_native.py` compiles `pc_ui_settings.c` into a game-free Windows fixture and hardcodes the combined setting count `60`. Adding another registered field changes it to `61`; update that assertion, and add actual default/file/env checks for skip. This Windows-only fixture cannot establish native Mac settings behavior. Source fixtures and live Mac runs can cover that separately.

## Save isolation and existing fixtures

Every `harness.run_game()` enters `SaveGuard` on the effective child `DEFJAM_DATA/save`, covering UserData and caches; the original absence is restored too. It reads bytes directly for equality and retains failed backups. `RECOMP_SETTINGS=none` is the harness default, so player settings do not change normal regressions. UI checks use explicitly created local INI files. SaveGuard does not restore settings files outside the save tree, so tests must never point UI checks at the player's real INI.

Prefer a disposable complete data root and copied prepared profiles for this work even though the guard is available. The scenario suite reads a hashed fixture and copies it to a temporary run root, then asserts fixture identity and save restoration. `default_fixture()` only reads `save/UserData` and omits large caches. The standard `intro` route types AAA; remove AAA only from a disposable copy before calibrating that route, never from the owner's original saves.

Existing local fixture manifests include `logs/fixtures/auto-20261009-204754/fixture.json` (18 UserData files, profiles AAA/ABC/VY2/VY3 plus Options) and earlier `auto-*` entries. These are available metadata, not proof that an arbitrary route is calibrated. Existing Mac evaluation reports describe a prepared 15-file copy with AAA omitted and the third ID selecting VY2: `docs/research/apex-macos-compatibility-2026-10-11.md` and archived October hand-offs. Historical Windows rebase instructions use the ignored `logs/rebase-work/baseline-path.txt` to identify a preserved snapshot's `runtime-data` root (`docs/research/toolkit-rebase-acceptance.md`); availability on this Mac is not established. Do not substitute private absolute paths into published instructions.

No game was built or run for this report. No generated source, game data or capture was added.

## Implementation review addendum

2026-10-17, after implementation. Reviewed `src/patches/skip_intro.c`, its entry delegate in `src/recomp_manual.c`, settings/UI changes, `startup_skip_faults()` / `check_skip_intro()` and the compiled synthetic fixture in `tests/unit/test_skip_intro.py`. No correctness, ABI, default-setting or startup-scope blocker was found in the reviewed changes. This was a read-only review; live results are recorded by the coordinating play-test session.

The implementation reads `recomp_settings_get("debug", "skip_intro", 0)` once at the first screen-selector entry, after the launcher has finished. The real settings registry therefore supplies consistent file/environment boolean parsing, including explicit `false`, without an environment bridge. The first-entry latch is consumed even when disabled or the initial screen is unexpected, so later overlay edits and later front-end screen requests cannot activate the patch.

The generated selector calls the entry hook before capturing its pending screen field. Changing only the pending index leaves normal loader initialization and selection intact. The direct ControllerBootup helper receives a synthetic return word plus two arguments; its generated `ret 8` consumes all twelve bytes. Its callee-saved register behavior and the hook's explicit restoration of EAX/ECX/EDX preserve the selector entry contract. No decoder or later movie operation is changed.

The new native fixture tests meaningful boundaries: effective setting precedence, one-time application, unexpected initial-screen refusal, stack/register restoration and leaving the current-screen field untouched. The startup parser requires positive ordered screen evidence and rejects earlier legal/bootLoad/movie activity. The live regression uses a clean RECOMP environment, local INI files, the existing title golden and a successful Start-to-menu transition.

One scope limit should remain visible when interpreting results: `startup_skip_faults()` ends its interval at the first main-menu request. If applied to a long idle-title log, it would reject the intentionally allowed attract `intro.mad` before any main menu has been reached. Its current `check_skip_intro()` caller presses Start before that point, so this is not a defect in that regression. Test idle attract playback separately and do not use this parser to classify that entire log. The new default/off and environment-off cases stop after observing legal1, so preservation of the rest of the ordinary startup is supplied by the existing normal-startup regression/play-test coverage.

## PR #25: fast regression startup policy

2026-10-17, read-only comparison requested after the owner authorized building the completed feature on PR #25 and running its full regression. The inspected PR checkout is exactly `0bb115fab155de0420db1af696f8857a14703d0f`. This policy supersedes the earlier recommendation to leave ordinary harness startup unchanged: the **player setting remains off by default**, while automated quick/full checks should now normally skip startup presentation. No source was changed and no build or game was run for this comparison.

### One complete normal presentation per set

Use the existing **m3 title-golden check** as the sole complete legal/movie boot in both QUICK and DEFAULT. Both lists already include it. Give that run explicit `RECOMP_SKIP_INTRO=0`, no pad script and a sanitized RECOMP environment. Keep the existing `m3-title-screen.json`; do not replace it or loosen its thresholds. Stop after the normal title capture, without a second normal main-menu boot.

Enhance this one run with positive ordered startup observations, all before the first title screen:

1. First `Control.GetScreenInfo` is `intMain/legal1.swf`.
2. `intMain/bootLoad.swf`, then `Game.StartMainMenu(0)` (allow the log's spaces).
3. Guest movie opens in order: `movies/eagames.mad`, `movies/thx.mad`, `movies/intro.mad`.
4. `intMain/preStart.swf`, followed by the existing positive rendered-title golden match.
5. No `[STARTUP] skip_intro` redirect occurred.

The run must reach the title by its ordinary flow, with no scripted button presses that could shorten the presentation. Match actual FUNCCALL/PATH events, not arbitrary mentions of movie names. Ordered movie opens plus native progression to a rendered title give the existing log's positive sequence evidence; they are not a separate per-frame decoder telemetry measurement. The current `startup_skip_faults(text, False)` alone is insufficient: it accepts a log ending immediately after legal1 and does not require any movie or title progress.

`golden()` currently inherits shell RECOMP variables. In particular, an inherited `RECOMP_PAD_SCRIPT` could shorten a supposed complete boot. Make this normal run deterministic with `inherit_recomp=False`, passing through only deliberate host controls such as the chosen `RECOMP_HEADLESS`. Keep all complete-save-root guards. `--only m4a` or `--only fight` need not add an unrelated full boot; the one-normal-boot promise applies to the defined quick/full sets. If a new separate startup row is preferred later, remove the normal behavior from m3 rather than retaining two complete presentations.

### Per-check policy

| Check | Startup policy / details |
|---|---|
| `unit` | No game. Preserve PR #25's `-m "not slow"` optimization; its slow branding check still belongs in CI/the explicit complete unit suite. |
| `m2` | Fast. The initial resource loading crest precedes the first front-end selector and is retained by the skip patch. Keep frame candidates 40/70/100/150/220 and the existing M2 golden. Recheck the same thresholds on the integrated build. It can finish before any first-screen event; do not demand preStart in this capture-only run. |
| `m3` | The one normal boot above, with the existing title golden and positive complete sequence. |
| `m4a` | Fast. Retain its menu golden, gamma0 and pad timing relative to the actual title anchor. Do not make it repeat legal/movies merely to reach mainMenu. |
| `fight`, `ffa`, `fight-terrordome`, `ffa-terrordome` | Fast. Keep calibrated post-title stages, existing fight durations, frame-rate floors and fault checks. |
| `combat`, `versus`, `ffa-result`, `two-matches` | Fast through the scenario suite; preserve disposable fixture saves, combat/result assertions, independent second-match events and strict audio checks. |
| `replay`, `repeat` | Fast. Keep `--no-unlock`, seed20261006, step-timed input and the exact existing fight-stream golden. Startup work/timer changes may still affect a seed-pinned stream; a mismatch needs investigation, not automatic rebaselining. |
| `intro`, `crib`, `gym`, `visual`, shared `story-tour` | Fast startup only. Preserve later Story/movie stages, the new-ID profile prerequisites and approved visual baselines. `intro` must still play the Story opening; `gym` must still open a move preview. Shared Story mode gets one fast launch, not a second boot for the gym row. |
| `soak` | All N boots fast. The m3 row already supplies this set's complete legal/movie boot. Keep N, hang detection, fault handling and byte-identical save restoration unchanged. Record that the soak now measures skipped-startup boot stability, rather than claiming N full movie presentations. |
| `launcher` | Inject fast startup for the game launched by Play. Preserve its real pointer interaction and launcher screenshot assertions. Its capture-only run can end before the guest reaches the selector; do not invent a first-screen assertion where the test intentionally checks only UI/Play. |
| `overlay` | Fast. Preserve real F1/pointer events and read-back frame proof. The existing20s host-clock trigger should be rechecked on PR #25; it now commonly opens over the title rather than over a startup movie. Do not reduce waits speculatively. |
| `unlockopt` | Fast for its UI launch and both file-only activation boots. Keep the existing empty `RECOMP_UNLOCK_ALL` sentinel so the checkbox stays editable and the file controls the unlock. The skip override is independent and may remain1 in these unlock tests. |
| `skipintro` combination matrix | Explicit startup policy per case, described below. It is an opt-in feature check, not an extra complete boot in every quick/full run. |

### Shared policy and anchor integration

Put the automatic fast policy at the harness environment-construction boundary rather than setting only `os.environ` in `regress.py`. PR #25's `scenario_suite.execute()` uses `inherit_recomp=False` for all three paths (ordinary routes, step input, live session plans), so it deliberately drops inherited `RECOMP_SKIP_INTRO`. A default supplied inside `run_game()` reaches all those paths and `scenarios.py` / `crowd-lab.py`, whose suite modules have their own imported harness instances. A mutation of only `regress.harness.BASE_ENV` does **not** reach a freshly imported `scenario_suite.harness`.

Use explicit per-run policy overrides for the one normal check and the startup matrix. The existing order is inherited environment → `BASE_ENV` → per-run `env` → default helpers. If `RECOMP_SKIP_INTRO=1` is added to `BASE_ENV`, per-run `0` still wins, but default/file-only matrix cases need a documented way to remove that injected value entirely. A shared default helper can support an empty sentinel like `unlock_default()`; the final child environment must omit the variable in genuine file/default cases. Never pass empty text to the registry and assume it is equivalent to absence. Keep the resulting effective child environment in `run.identity`, which the scenario report already embeds.

The current `TITLE_ANCHOR = "feflow.xml#2"` assumes two front-end-flow loads. The patch changes pending screen ID0 to1 at guest `sub_0006BC90_enter`; it avoids the first legal flow, so that second-load event does not occur. Use semantic `getscreeninfo(intmain/prestart` for all fast scripted/capture clocks. It also exists during normal startup, so a universal title anchor is simpler and does not need boolean-text parsing in Python. If preserving the exact old normal-title capture clock is necessary, retain `feflow.xml#2` as a separate **normal-only** constant for m3; fast m4a/routes must use preStart. The existing skip-enabled title5s captures already matched the unchanged golden using the semantic anchor. Recheck normal m3's clock once if switching it too.

Apply this consistently to `run_game(scripted=True)` and the explicit anchor passed by `check_m4a()` / capture helpers. Leave `title_presses()` and subsequent stage clocks intact: the title's own animation still needs its delay, and skipping the presentation is separate from shortening that animation. `scenario_suite.bootstrap_stages()` already reuses harness routes and closes setup input when the match begins; no rewrite of its combat schedule is needed.

Do not weaken `scenario_suite`'s environment sanitization or expand its restricted `--test-env` interface merely to carry this policy. `--test-env` intentionally accepts only `RECOMP_TEST_*`. Either the shared harness default supplies skip, or add a narrowly named startup option if manual normal-startup scenarios are needed; report the actual choice in the run identity.

### Startup combination matrix without duplicate full boots

The existing matrix covers default-off, INI-on, INI-on/env0, INI-off/env1 and skip-plus-unlock. After making automation fast by default:

- Default-off: explicitly remove the automation override, use settings-none, and stop after the positive legal1 event. This tests the game's real default; injecting env0 would instead test an override.
- INI-on: remove the automation override so the file alone controls startup; require preStart first, no startup movie opens, title golden and Start-to-menu.
- INI-on/env0: require legal1 first and stop there. m3 already supplies the set's complete presentation.
- INI-off/env1: require the fast behavior and menu transition.
- Skip-plus-unlock: skip1 or file-only skip with unlock enabled; require menu/profile progress and a real unlock execution. Do not clear the unlock alongside skip when preparing the file-only case.

If adding a real skip-checkbox interaction, clear the skip env override for that UI run so the widget is editable; preserve the screenshot/INI/Play/first-screen evidence already obtained for the feature. PR #25 adds a Debug tab and recorder settings; merge the new control into the existing page without moving/removing its recorder UI, and recheck pointer coordinates on the integrated UI. Recorder should stay off during ordinary regressions unless a named recorder test enables it.

### Integration risks and comparison limits

Preserve PR #25's existing `LAST_LOG`, `triage_hint()` and failing-row diagnostics. The root feature checkout differs in those areas and in the slow-unit exclusion; copying entire `harness.py` or `regress.py` files over PR #25 would regress its agent-tooling work. Port the focused startup changes onto the PR head instead. Its recorder-aware `scenario_suite.py` also contains more code than the root version; retain that code and all fixture/report assertions.

The important measurements after integration are unchanged frame goldens, the exact replay hash, Story/preview/attract behavior, live launcher/overlay controls and strict gameplay/audio gates. Fast startup changes elapsed boot work and thread/timer history, so the full regression is necessary even though post-title script timings stay anchored. Existing audio or non-code-call failures remain failures and must retain their logs; do not change their thresholds or suppress them as part of this optimization.

### Implemented fast-policy review

2026-10-17, after the root test-policy changes and before the PR #25 integrated run. No blocking environment-precedence, file-only-test, title-anchor or normal-boot assertion issue was found by read-only review. No source was changed and no build or game was run for this review.

`startup_default()` is called after the caller's environment merge. Explicit `0` remains `0`; explicit empty text removes the variable; only absence receives the automation default1. The matrix now initializes skip to the empty sentinel, replaces it only for the two environment-override cases, and sanitizes inherited RECOMP variables. Default-off and INI-only cases therefore exercise the real registry/default or file rather than the automation override. Unlock1 in the combined case remains independent.

The universal semantic title anchor is used by ordinary scripted routes and explicit golden-capture callers. It exists in both flows and avoids dependence on the omitted first flow load. M3 explicitly requests normal0, sanitizes inherited RECOMP variables (including pad scripts), and positively checks legal1, bootLoad, the movie controller, EA/THX/intro movie opens and preStart in order before accepting its unchanged title golden. QUICK and DEFAULT each include m3 once. Other check bodies contain no added complete normal startup; default/off matrix cases stop at legal1.

PR #25's ordinary scenario, step-input and live-session branches all ultimately call their own imported `harness.run_game()`, so porting this shared helper into that harness supplies fast startup after their deliberate environment sanitization. No scenario-suite sanitization exception is needed. File-only unlock tests remain valid: their empty unlock sentinel clears only unlock, while the separate startup helper makes those boots fast. M2/UI-only captures can still finish before a selector event and are not given an inappropriate missing-preStart failure.

One intentional precedence caveat applies to performance interpretation: direct harness checks still inherit shell RECOMP variables unless they explicitly sanitize. An inherited `RECOMP_SKIP_INTRO=0` or empty sentinel overrides the new fast default for those checks; sanitized scenario callers do not inherit it. Use a deliberate clean regression launch environment for the integrated run and read actual child environments from run identities before claiming that most checks used fast startup. This preserves explicit caller control rather than silently overriding it.

The source review does not replace measurement of the normal semantic-anchor golden, replay hash, PR #25 pointer coordinates, audio gates or the full integrated run. The ordered normal check positively verifies the available startup events and rendered title; it does not add per-frame decoder telemetry for each movie.
