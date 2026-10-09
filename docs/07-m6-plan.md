# M6 plan: PC features

Approved by Vlad on 2026-10-05 (D63). Each slice is a public source release after his play-test;
the next slice does not start until he has tested the previous one.

## Design: one settings core, three front ends

- **Settings core.** One typed table of player-facing settings with defaults, so the executable starts
  without `run.ps1`. It loads and saves one file, `settings.ini`, in the data directory.
- **Front ends.** The file plus hotkeys (v0.2.0); a pad-friendly in-game overlay and a pad-friendly
  launcher window sharing one UI (v0.4.0). All three read and write through the core, so a change in one
  shows in the others.
- **Environment variables** keep working and win over the file. The harness, the goldens and every
  diagnostic are unchanged by M6.
- **Live changes.** Window size, fullscreen, aspect mode and vsync apply without a restart. A setting that
  cannot is marked "restart required" rather than silently ignored.

## Slices

| Release | Slice | What lands |
|---|---|---|
| v0.2.0 | Display and settings file (**released 2026-10-05**) | Settings core and `settings.ini`; direct executable launch; borderless full screen with Alt+Enter and F11 on a flip-model swap chain; resizable window with correct 4:3 pillarboxing; window size and render-scale choice; vsync that paces on the display at multiples of 60 Hz |
| v0.2.1 | Bug fix (**released 2026-10-05**) | Terrordome crash: seven stubbed function exits translated as code |
| v0.3.0 | Input | SDL3 gamepad input (DualSense, Switch Pro, DirectInput pads); keyboard gameplay with rebindable keys; pad remapping and deadzones; rumble to the host pad; 2-4 pad local multiplayer checked in a real match |
| v0.4.0 | Overlay and launcher | Pad-friendly in-game overlay; pre-boot launcher; mouse in both; live apply |
| v0.6.0 (release preparation authorized) | HD texture packs | Lossless content IDs, PNG/classic DDS packs, launcher/overlay controls, faithful Lanczos4x tooling and optional local Setup generation |
| Unscheduled | True 16:9 widescreen (**ToDo; D84**) | Wider 3D view in fights with the HUD placed correctly; menus and FMV stay 4:3 unless cheap |

Deferred out of M6 (not selected): music replacement, frame rates above 60. Exclusive full screen is
dropped (D64): it gains nothing under Proton or with the flip-model borderless window. Mouse means the launcher and
the overlay; gameplay is keyboard or pad.

## Where the code goes

- Generic pieces go on the toolkit fork, branch `defjam/m6` (started at `aa1a1b9`), one topic per commit,
  in new files with thin call-in points so a rebase conflicts on a few lines.
  D84's HD lane uses `defjam/hd-textures`, starting from the accepted v0.4.1 pin `c979c091`.
- Game-specific pieces (widescreen patch, default bindings, branding) go in `src/`.
- Upstream candidates from M6 are logged in the maintainer's private PR-maintenance notes, numbered
  from 20. Opening any upstream PR needs the owner's separate approval.

## Upstream drift

Check `gh api repos/sp00nznet/xboxrecomp/compare/<fork base>...main` at the start of each slice, before
each release tag, and when one of #167-171 merges or gets review changes. Rebase when upstream touches a
file we changed or one of our PRs merges; otherwise stay put. Texture packs hook the D3D11 texture cache,
the same code as upstream's backend-interface PR #162, so refresh that overlap before implementation.

## Testing

Each feature has a check in `docs/02-test-plan.md` and leaves `scripts/regress.py` green. The goldens run
at 4:3 with the gamma ramp off and an explicit render scale, so they stay comparable.

## Texture-pack investigation

The 2026-10-08 [HD texture and local upscaling plan](research/hd-texture-plan.md) records the renderer gaps,
asset categories, local GPU workflow, pilot and proposed expanded acceptance gates. Vlad chose faithful cleanup
and static assets first, with video extraction/workflow planning now and HD video playback later (D83).
On 2026-10-08 Vlad moved HD before widescreen (D84). A subsequent public-state refresh found native
macOS/Linux v0.5.0 already released (D85), so HD is proposed as v0.6.0 and widescreen remains unscheduled ToDo. C: is approved for the corpus and scratch; implementation uses an isolated lane.
The implementation and remaining gates are tracked in `docs/research/hd-textures-notes.md` in that lane;
player/tool instructions are `docs/hd-textures.md` there. Neither a prototype nor art coverage closes the release gate.
