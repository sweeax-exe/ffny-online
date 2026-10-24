# Windows owner feedback and Mods tables — 2026-10-23

Owner clarified the missing widescreen artwork backlog is Select User ID,
Choose Teams and Choose Venue. These are recorded in PR91 for the owner to tackle
in HD Widescreen Menus1.1. The repeated pause-menu close-ups are HUD evidence,
not additional artwork scope. No artwork was generated or uploaded.

[Issue95](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/95) records the
owner-reported widescreen pause bug: the fighter bars, names and scores move to
centered4:3positions during pause. It includes the four-player Foundation example,
reproduction, expected behavior and candidate provenance. It is logged, not fixed;
HUD-state classification remains an unverified investigation lead.

## Launcher changes

- Installed mods are a bordered table with On, Mod and Status columns.
- Owner chose one **Recomp HD textures** row. Generated components are recognized
  from their bounded builder ownership marker, recipe ID, folder and mip policy;
  lookalike custom names remain ordinary local mods. Details explain transparency
  and material-channel textures. The grouped checkbox controls the detected
  components together; partial selections remain unchanged until clicked.
- The bare `faithful-hd` default is a missing saved selection, separate from the
  generated pair. Missing selections appear under an explicitly labeled warning,
  outside the installed table. Nothing is silently removed or disabled. The actual
  installed test pair consists of channels and opacity directories; they are not
  missing. Legacy IDs and ownership metadata remain compatible.
- Load Order is a table sorted by priority descending, highest at the top, with
  Higher/Lower controls. Contiguous built-in components move together. If a custom
  mod is interleaved between them, their individual priorities remain visible with
  descriptive component names, preserving the original winner for every texture.
  Stored loader order remains low-to-high; the UI projects it high-to-low.
- Catalogue authors are passed from signed metadata through the helper's view and
  shown directly below the title. New generated pack manifests use the Recomp HD
  display name. Existing on-disk folder names and product markers stay unchanged.

## Validation and limitation

49focused native-model/manager tests pass. Full fast suite606passed,26skipped,
1slow deselected. Tests cover grouping, missing legacy selections, interleaved
priorities, whole-pair movement, partial selection, custom lookalikes and authors.
Native MSVC/D3D11 build passes after replacing an unavailable internal ImGui mixed
state flag with the explicit Partly enabled status. No guest/runtime rendering,
toolkit or lifter change. Gameplay regression was not repeated for these UI-only
changes; the earlier Windows runtime report remains the gameplay evidence.

A separate executable was built at
`C:/Users/Vlad/code/defjam-pr91-windows/mods-ui-preview/DefJamModsPreview.exe`;
`OpenModsPreview.cmd` starts it with disposable settings and cached catalogue.
The existing RC app executable and four temporarily used source inputs were
restored byte-for-byte (5files,0mismatches). The review-data fixture has its own
managed menu pack copy and manager state; local generated components are linked
for inventory. The original managed installation is not selected for mutations.

The Windows computer-use runtime failed before inspection with
`failed to write kernel assets: The system cannot find the path specified`,
including after a kernel reset. Consequently **visual layout, mouse/controller
interaction and large-text acceptance remain pending**, not claimed as passed.
Logs outside source: `../logs/mods-table-focused-tests.log`,
`mods-table-all-tests.log`, `mods-preview-build.log`, `mods-ui-preview-native.log`.
The original playtest launcher still opens the previously verified RC UI.

## Owner review follow-up — 2026-10-24

The owner reviewed the native preview, supplied its Load Order screenshot and
approved the revised layout. Removed the mod-specific "Put HD Widescreen Menus
above..." sentence; only the generic next-game-start notice remains. The preview
was rebuilt successfully. Automated computer-use acceptance remains distinct
from this owner visual approval; large-text and cross-platform checks remain.

Author credit is corrected in the authoritative catalogue source from `Vlad` to
`vyanhursky` on `fix/menu-author-github-name`, commit`c8940e2`. Source validation
and13applicable tests/22subtests pass; the symlink test remains inapplicable on
this Windows machine without the required privilege. The live signed catalogue
still contains the old credit until the metadata PR is merged and a new signed
revision is published. No client-side hardcoded author override is introduced.
