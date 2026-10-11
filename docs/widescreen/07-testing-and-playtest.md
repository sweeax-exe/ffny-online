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

## 3. Beta release

1. When WS-0, WS-1a, WS-2, WS-3, WS-4, WS-5, WS-6 and WS-7 are in (WS-8 can be `black` only), cut **`v0.8.0-beta.1`** from
   `feat/widescreen` merged to main, with `widescreen = off` by default and the setting visible in the launcher. Follow
   `docs/releasing.md`; confirm the release workflow handles a pre-release tag (mark it as a pre-release on GitHub, not
   "Latest").
2. Release notes: what to try (`widescreen = auto` in the launcher), what is known to be rough (32:9, unswept venues), how
   to report (issue form with the aspect and screen).
3. The owner plays through the script below at 16:9 and on the Deck. Fixes land as `beta.2`, `beta.3`.
4. When the script passes at 16:9 and 16:10 and the venue sweep (WS-9) is done at those aspects: `v0.8.0` with
   `widescreen = auto`. The 21:9/32:9 sweep and polish follow in a later release (owner, 2026-10-11).

## 4. Play-test script (owner, by hand)

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

## 5. Where findings go

Bugs from the play-test go into `docs/research/widescreen-beta-findings.md` (one line each: screen, aspect, what, capture
file name kept locally), then into GitHub issues for anything the public may also hit. Each fix names the element row in
[03](03-element-catalog.md) it touches, so the catalog stays the single list of what has been checked.
