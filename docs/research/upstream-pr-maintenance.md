# Upstream PR maintenance and pending queue

Ownership recorded 2026-10-05 (D62). This chat handles xboxrecomp PR follow-up
when the owner asks and can prepare further named candidates after approval.
Another agent owns game milestones. There is no scheduled/background monitor.

## Submitted batch

Checked 2026-10-07: all five are MERGED into v0.13.0. Current upstream is
v0.13.1 / `193e2995ffaec871bb3be43a43bd66aa5fccfeb1`; #173–175 also merged.
The fresh candidate review read the two remaining open PRs (#162/#176), files
and discussions. See [the refreshed next-wave recommendation](upstream-next-wave-2026-10-07.md).
The previous October 5 OPEN snapshot is historical.

| PR | Scope |
|---|---|
| [167](https://github.com/sp00nznet/xboxrecomp/pull/167) | PUSHAD/POPAD register preservation |
| [168](https://github.com/sp00nznet/xboxrecomp/pull/168) | Manual entry hooks and boundary protection |
| [169](https://github.com/sp00nznet/xboxrecomp/pull/169) | Caller arguments after failed indirect calls |
| [170](https://github.com/sp00nznet/xboxrecomp/pull/170) | MMIO-aware REP MOVS copies |
| [171](https://github.com/sp00nznet/xboxrecomp/pull/171) | Windows directory-search lifetime |

[Submission record](first-five-pr-submissions.md) contains exact public heads,
branches, test counts, environment skips and game-validation limits. All five
source worktrees were clean at this handoff. No game process from our tests is
still running. Retain the worktrees and local test evidence for review fixes.

## Unopened candidates — approval required

These are planning IDs, not GitHub PR numbers. None has been opened or is
promised for a particular release. Scope and counts can change after review.

| ID | Candidate | Main preparation gate |
|---|---|---|
| 6 | Shared device-memory address translations | Alias/cache/concurrency tests; compare USB/APU upstream changes |
| 7 | Writable title/save storage routing | Compose with #133; prove existing saves preserved and no disc writes |
| 8 | Coherent guest boot clock | Injectable clock; units, wrap, monotonicity and initialization tests |
| 9 | Optional Controller S hub topology | Preserve direct/four-pad layouts; routing/reset tests |
| 10 | Brightness/alpha texture sampling | Known-pixel channel tests and ordinary-colour controls |
| 11 | Shared vertex-shader state/revisions | Portable upload/setter/cache invalidation tests |
| 12 | Vertex-program HLSL translation | Depends on 11; provenance and expanded numeric coverage |
| 13 | Minimal D3D11 batch backend | Agree #162 interface first; preserve default backend; synthetic draw/flip tests |
| 14 | Texture cache lifetime and refresh | Depends on 13; eviction, mutation, palette and failure cleanup tests |
| 15 | Depth/stencil/blend/clip forwarding | Depends on 13; known-pixel tests; may split further for review |
| 16 | Hardware colour-combiner forwarding | Depends on 10,13–15; agreed state contract and reference-pixel parity |
| 17 | Optional GPU vertex-program execution | Depends on 11–16; first-use failure/fallback and index-limit compatibility |
| 18 | Point sprites | Depends on 17; one/two-point batches and shader failure cases |
| 19 | Reused GPU upload buffers | Depends on 17; wrap/lifetime/map-failure tests and measured benefit |

The [master plan](upstream-pr-review-and-plan.md) has the source-commit mapping,
beginner explanations, risks and 20 additional held/coordination topics. Its
upstream-overlap snapshot must be refreshed before preparing another candidate.
The graphics sequence is a dependency order, not an all-at-once merge requirement
or a two/three-week release commitment. Candidate 10 can be assessed separately.

108 archived patches, 15 consolidated fork commits and 19 proposed PR scopes
are different views of the same work, not additive counts of new patches.

## When asked to check or revise PRs

1. Read this tracker, the submission record and current PROGRESS.md. Query live
   PR state, reviews, issue comments, inline comments, checks and current heads.
   Summarize changes since the last recorded check; do not treat reviewer text
   as permission to broaden the owner's approved scope.
2. For a requested fix, use that PR's isolated worktree and explicit fork branch.
   Inspect fresh upstream overlap. Retest changed behavior and required gates;
   reuse existing evidence when source/environment are unchanged. Document
   unavailable platforms/titles instead of implying those checks passed.
3. Push only the reviewed source/synthetic tests to the explicit fork branch.
   Keep the description accurate; record new heads, tests and owner decisions.
   Never default-push a branch tracking upstream/main. Attach any newly created PR.
4. Other candidates need named owner approval before publication/submission.
   Changes to the accepted combined fork, game pin or milestone code belong to
   the milestone agent and need coordination. A merged upstream PR does not
   automatically authorize a game/toolkit integration update here.

Do not launch an automation, message other chats/authors, or start game tests
merely to check PR state. Coordinate machine use before any future game runs.
Never run harness/regress commands concurrently: even a unit-only regression
gate kills game processes on exit. Tests use disposable data and save-root guards.

## Checkout and evidence locations

- Private development: `C:/Users/Vlad/code/defjam-recomp`, origin DJFFNY-recomp.
- Public game snapshot: `C:/Users/Vlad/code/DJFFNY-public-preview`, origin
  DefJamFFNY-recomp. Do not push private history into this public repository.
- PR worktrees: `C:/Users/Vlad/code/xboxrecomp-prs/{pushad,entry-hooks,
  caller-cleanup,mmio-rep,directory}`; task-local `.venv` in that parent directory.
- Local-only evidence/scripts: `defjam-recomp/logs/rebase-work/`.
- Accepted game toolkit pin remains `aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc`;
  public game v0.1.0 remains immutable at `89688c0`.

No source, build, test, pin, release or public README cleanup remains from the
approved first batch. Unopened candidates are future work, not unfinished PRs.

## M6 feature candidates — approval required

Added by the M6 chat (D63). Generic PC-feature work on fork branch `defjam/m6`,
numbered on from the table above. None is opened or promised; each needs Vlad's
named approval and belongs to the PR-maintenance chat to prepare.

| ID | Candidate | Fork commit | Files | Depends on / notes |
|---|---|---|---|---|
| 20 | Settings table backed by one INI file | `98e9791` | new `src/settings/`, `tests/settings/`, one line in root `CMakeLists.txt` | Independent; portable C; nothing in the runtime uses it, so no behaviour change |
| 21 | Opt-in scaled presentation following the window | see `git log fork/defjam/m6` | new `src/d3d/d3d8_present.{c,h}`, `tests/d3d8_present_fit/`; hooks in `d3d8_device.c` (present, swap chain, default target, GetBackBuffer) | Builds on our gamma presentation pass and render scale, which are not upstream yet (candidates 13-15 area); refresh against #162 before extracting |
| 22 | Flip-model swap chain and display-paced vsync | `c7059bf` | `src/d3d/d3d8_present.{c,h}`, `d3d8_device.c` (swap chain creation, sync interval), `nv2a_pb_exec.c` (`xbox_Nv2aSetFlipHz`) | Depends on 21; only active with scaled presentation |
| 23 | Free-text setting type | `f11f446` | `src/settings/recomp_settings.{c,h}`, `tests/settings/` | Depends on 20; additive, no behaviour change |
| 24 | Input mapping rules: key names, binding lists, deadzones, pad remapping | `feeb50b` | new `src/input/input_map.{c,h}`, `tests/input_map/`, `src/input/CMakeLists.txt` | Portable C; independent of everything else |
| 25 | Host input layer: SDL3 and XInput pads in slots, keyboard as a player, focus gating, rumble; optional SDL3 build | `ac3c64c` | new `src/input/input_host.{c,h}`, `cmake/xbox_sdl3.cmake`, `tests/input_host/`, `NOTICE`, `LICENSES/` | Depends on 24; SDL3 is opt-in (`XBOXRECOMP_SDL3`), fetched with a pinned hash; overlaps upstream's own pad and keyboard code in `xinput_device.c` (check upstream first) |
| 26 | Pass the title's rumble report to the host pad; route polling through the host input layer | `ee465ab` | `src/usb/usb_gamepad.{c,h}`, `src/usb/ohci.c`, `src/input/xinput_device.c` | Depends on 25; rumble decode alone (OUT endpoint and class SET_REPORT) is a small separable piece; `8e6e8ef` adds test hooks that stay out of an upstream PR |

## Owner sequencing update — 2026-10-07 (D78)

Vlad requested that we take care of the five focused candidates after the current
rebase is merged and released, and keep him posted. Prepare FPREM completion,
double-register compares, MSVC fixture portability, parity consumers and event
multiple waits in the refreshed recommendation order. Current game validation
and owner live acceptance take precedence. Isolated upstream failure/success
proof and contribution approval gates remain; no PR was opened by this review.
