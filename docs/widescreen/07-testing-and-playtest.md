# 07 — Testing, beta release and play-test

## 1. Automated checks (agents run these)

| Check | When | How | Pass |
|---|---|---|---|
| Off is unchanged | every commit | `python scripts/regress.py --quick` (harness forces widescreen off) | green, goldens identical |
| Full regression | before merge | `python scripts/regress.py` | green (14 checks) |
| Maths | every commit touching placement or canvas | unit tests (WS-10) | green |
| Settings | WS-0 and later | settings and reference unit tests | green |
| Widescreen smoke | every game-side widescreen commit | `harness.py run fight --widescreen 16:9`, `run intro --widescreen 16:9`, `run ffa --widescreen 21:9` | no crash, `[WS]` lines, presents within 5% of the 4:3 run, save restored |
| Wide frame signatures | WS-10 onward | `regress.py --only widescreen` vs `<data>/test-baselines/widescreen/` | match (review and re-baseline intentionally) |
| Determinism | once per milestone | `replay` with widescreen on vs off | same fight stream; crowd-only differences documented |
| Side fill never on fights | WS-8 | request counter in the `fight` route's match | 0 |
| Debug layer | WS-1a | `win-x64-debug` fight at 16:9 | no D3D11 debug messages |
| Vulkan | WS-1b | Linux build on the Deck: quick regress + 16:10 fight | green |

## 2. Aspect test matrix

Run the smoke set at each, plus a window resize and Alt+Enter at each:

| Output | Canvas | Why |
|---|---|---|
| 4:3 window (1280x960) | off | baseline unchanged |
| 1920x1080 | 16:9 | main target |
| 1280x800 (Deck) | 16:10 | handheld; thin cutscene bars |
| 2560x1080 / 3440x1440 | 21:9 | ultrawide; HUD in 16:9 box; cutscene box |
| 5120x1440 | 32:9 | experimental; FOV near 97° in the widest venue |
| 1280x1024 | 5:4 | narrower than 4:3: widescreen inactive, bars |
| 16:9 canvas in a 16:10 window | 16:9 | present fit with bars |

A machine without those monitors can use window sizes (`window_width`/`window_height`); full-screen checks need the real
displays or the Deck.

## 3. Preview before the beta (2026-10-11)

The owner asked for an early look while the beta is built. The branch build with the experiment switched on is a
playable **preview**: run the game with `RECOMP_WS_ASPECT=1.7778` (16:9) or `1.6` (16:10). The 3D (fights, free-for-all,
Story hub, cutscenes) is the real Hor+ result, and the window presents at that aspect without touching
`settings.ini`. Everything 2D (menus, HUD, movies, loading posters) is **stretched**, because the 2D work packages are
not in yet; ignore that and judge the 3D: fight framing in 1v1 and free-for-all, venue edges, special cameras,
cutscenes. On Windows, from the worktree root:

```powershell
$env:RECOMP_WS_ASPECT = "1.7778"; .\build\win-x64-release\defjam_recomp.exe
```

## 4. Release plan (updated 2026-10-13)

Vlad requested the widescreen release after Windows, Linux and Mac validation,
then chose **v0.9.0** because v0.8.0 already shipped the setup updater.
Use the stable source-release workflow in `docs/releasing.md`; the widescreen
feature remains labelled **beta** and **off by default**. This version change
does not make unswept venues or experimental ultrawide shapes fully validated.

Release notes describe the tested routes, platform differences and retained
known issues. The Mac evidence is in [09-macos-validation.md](09-macos-validation.md).
The original beta sequence and eventual automatic-default proposal remain
research plans; changing the default requires a later decision and the venue
sweep. Continue the hand play-test below at 16:9 and 16:10, and record any gaps.

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
