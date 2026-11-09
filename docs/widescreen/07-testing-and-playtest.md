# 07 — Testing, beta release and play-test

## 1. Automated evidence

Use the [current acceptance policy](../02-test-plan.md), [harness reference](../09-testing-harness.md)
and release cadence. Quick/affected-area checks belong on a runtime PR; a full
run belongs on each platform's combined release candidate. Historical14-check
counts are not the present check inventory.

Compare stock4:3goldens with widescreen off. For a local wide smoke, pass the
supported numeric override through harness `--env`, for example
`python scripts/harness.py run fight --env RECOMP_WS_ASPECT=1.7777778 --png`.
Choose actual routes from `harness.py routes`; the old planned `--widescreen`
CLI and `regress.py --only widescreen` were not implemented. Report source,
settings, canvas, save restoration, faults and actual captured dimensions.
Frame-rate and view correctness remain separate claims.

## 2. Aspect test matrix

Run the smoke set at each, plus a window resize and Alt+Enter at each:

| Output | Canvas | Why |
|---|---|---|
| 4:3 window (1280x960) | off | baseline unchanged |
| 1920x1080 | 16:9 | main target |
| 1280x800 (Deck) | 16:10 | handheld; thin cutscene bars |
| 2560x1080 / 3440x1440 | 21:9 | ultrawide; HUD in16:9safe box; cinema full-height Hor+ |
| 5120x1440 | 32:9 | experimental; FOV near 97° in the widest venue |
| 1280x1024 | 5:4 | narrower than 4:3: widescreen inactive, bars |
| 16:9 canvas in a 16:10 window | 16:9 | present fit with bars |

A machine without those monitors can use window sizes (`window_width`/`window_height`); full-screen checks need the real
displays or the Deck.

## 3. Historical release context

The original October11anamorphic preview had stretched2D and is not the current
renderer. v0.9.0 shipped the implemented wider canvas/classifier; its platform
results remain in release notes and the Mac validation record. Later fresh-setting
Auto defaults do not retroactively validate unswept experimental aspects.

## 5. Play-test script (owner, by hand)

Tick each at 16:9 (desktop) and 16:10 (Deck); 21:9 if a monitor is available. Note anything stretched, cut off, misplaced,
flickering at the screen edges, or different from 4:3.

**Boot and menus**
- [ ] EA/THX logos, opening movie: centred, side fill as chosen, sound in sync.
- [ ] Title screen, main menu, options, memory-card prompts, user-ID and name entry.
- [ ] Battle setup: number of fighters, venue select, character select with 2 and 4 fighters (3D fighters inside their panels).
- [ ] Loading poster before a match.

**Fights**
- [ ] 1v1 at three venues incl. one small indoor (Crow's Office or Dragon House) and Inferno: walk both fighters to each wall.
- [ ] 4-way FFA: HUD in the four corners, all fighters visible, no edge pop-in of crowd or props.
- [ ] Tag/2v2 or 1vs2, one special match type (Cage, Window, Subway or Ringout).
- [ ] Two finishers and an environment attack (special cameras); a Blazin' move (filter covers the whole screen).
- [ ] Fight intro and victory outro.
- [ ] Pause menu, popups, a tutorial prompt; results and FFA results screens.
- [ ] Two pads (versus): nothing different from 1P.

**Story**
- [ ] Opening cutscenes and two later cutscenes vs the console video framing.
- [ ] Choose style, create-a-player (head preview), Crib, Map, Gym "Learn Moves" (preview inside the projector), one shop of
      each kind (barber, tattoo, clothes, jewellery), phone/messages, trophies, Unlock Fighters.
- [ ] One Story fight end to end and the cutscene after it.

**PC features**
- [ ] Launcher: change widescreen, HUD, cutscenes and side fill; restart picks them up.
- [ ] Overlay opens and draws over the wide picture; Alt+Enter, window resize.
- [ ] HD texture pack on, widescreen on: no conflict.
- [ ] Widescreen off: the game looks exactly as before.

## 6. Where findings go

Bugs from the play-test go into `docs/research/widescreen-beta-findings.md` (one line each: screen, aspect, what, capture
file name kept locally), then into GitHub issues for anything the public may also hit. Each fix names the element row in
[03](03-element-catalog.md) it touches, so the catalog stays the single list of what has been checked.

## 7. Steam Deck (Vulkan) scripted validation, 2026-10-12

Build: this branch merged with main at v0.7.3 (toolkit `872fbc3`), posix-release in the Deck build container,
HD packs on with `cache_mb = 2048`. Runs were `harness.py run <route>` inside `gamescope -W 1280 -H 800 -r 90`
(Deck panel shape), with `RECOMP_WS_ASPECT=1.6` (or 1.7778) and `RECOMP_TRANS_SHOT_WIDE=1` unless noted.

| Run | Result |
|---|---|
| One-on-one fight, 16:10, packs | 0 crashes, save identical, median 120 presents per 2 s (min 59) |
| Free For All, 4:3 vs 16:10, packs | median 120 vs 118 per 2 s; GPU fence about 2.9 ms vs 3.1 ms. Widescreen costs about nothing |
| Terrordome Free For All, 4 min, 16:10, packs | median 120 (min 59) |
| Story intro to the creator, 16:9 and 16:10 | clean; cutscenes fill the frame; the fighter-select menu is centred with black sides |
| Fight with `aspect = widescreen (auto)` in a settings file, full screen | `[WS] widescreen 1.6000`, swap chain 1280x800, median 120 |
| Widescreen off: m2, m3, m4a golden frames, a 120 s fight | match; median 120, min 110 |
| Unit tests (full checkout) | 239 passed, 50 skipped |

Captures at 16:10 show the 3D widened (FOV 30 to 35.65), the fight HUD in the corners (four corners in Free For All)
and the timer centred. Notes: a single 2 s window per run dips to about 59 presents (the cause was not investigated; it
appears at 4:3 too); the harness uses a 1280x960 window unless a settings file asks for full screen, so use
the settings-file run for the real output size. Not covered: pause and popups, shops, the Story hub, and the
by-hand checklist in section 5, which still needs a person on the Deck.


## Acceptance distinctions retained from the original packages

The original packages' remaining visual checks are acceptance criteria, not completed coverage: compare wide HUD bar width/height against 4:3 within 2%; check all four corners, pause/popups/tutorials, full-width tints/dimmers and hidden slide-ins; confirm 2Dpanels align with 3Dcharacter/head/gym previews; check cutscene→creator/menu transitions for framing flashes; confirm point glows remain round and movies retain audio sync. Resize/Alt+Enter must fit the fixed canvas, without implying live aspect switching. Capture-free fight pacing targets no more than 5%loss against a matched 4:3 baseline; debug-layer diagnostics and no ambient fill over active 3Dneed their own evidence. Existing routes do not prove every venue, finisher, language, late-Story screen or ultrawide camera.

Coverage work is tracked in [issues172–176](https://github.com/vyanhursky/DefJamFFNY-recomp/issues?q=is%3Aissue+172+OR+173+OR+174+OR+175+OR+176), rather than a second package backlog. Renderer/gameplay defects already tracked in 98/141/161/162/166 remain distinct from missing test evidence. Use the current [release cadence](../releasing.md) and supported harness `--env RECOMP_WS_ASPECT=...`/wide-capture options; the old `harness --widescreen` and `regress --only widescreen` examples were proposed interfaces, not demonstrated commands.
