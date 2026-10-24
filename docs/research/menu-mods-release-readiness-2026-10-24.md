# Integrated menu/mods release readiness — 2026-10-24

The owner accepts the revised native Mods layout, with two final corrections:
remove the mod-specific load-order recommendation and credit owned modpacks as
**vyanhursky**. The first is implemented and rebuilt in the isolated preview;
the second is in [catalogue PR2](https://github.com/vyanhursky/DefJamFFNY-mods/pull/2),
commit`c8940e2`. It changes only the author field, not archive bytes or hashes.
The source correction was merged and publication40100204475 succeeded. On
2026-10-24 the real client verified signed live revision2 and authors=vyanhursky
with unchanged project trust pins.

## Completed

- Installed and priority tables, Recomp HD grouping/details, missing legacy
  selection warning, and author display. Owner screenshot confirms the revised
  native layout; the extra recommendation is removed in`4f74f53`.
- Prior Windows embedded-helper and progress-file fixes, actual catalogue install,
  order/offline/Play acceptance and13applicable native gameplay checks are retained
  in the [Windows report](windows-integrated-menu-mods-rc-2026-10-23.md).
- PR91's three conflicts with main were resolved locally while preserving both
  CachyOS C++17/C++20/Unicode support and Mods grouping/priority coverage. No
  toolkit, lifter or gameplay rendering change.74focused native checks pass;
  native preview rebuild passes. Final fast-suite result is recorded in PROGRESS.
- Catalogue source validation passes;13applicable tests and22subtests pass.
  Symlink test requires a Windows privilege this machine lacks, so that check is
  left to catalogue CI; the initial WinError1314 result is not hidden.
- Select User ID, Choose Teams and Choose Venue are explicitly deferred to the
  owner's modpack1.1 backlog in PR91. They are not new1.0release blockers.

## Remaining before a stable release

1. **Final commit CI and setup artifacts.** Push the accepted UI/merge changes to
   PR91, require green platform CI, and validate a fresh game-free installer from
   that exact source. The previous clean Windows installer predates the tables.
   Verify fresh bundled install/update, catalogue/order persistence and Play on
   the final candidate; do not promote the old artifact as the new release.
2. **Native UI acceptance beyond the owner's normal-size Windows review.** Check
   large text/compact layout, grouped toggles and priority controls, plus shared
   Mac/Linux launcher/overlay smoke. Computer-use tool failure prevented those
   additional automated native observations here; model tests are not visual QA.
3. **Author metadata publication.** Review/merge catalogue PR2 and run a normal
   signed catalogue publication, then verify the client displays vyanhursky.
   The protected catalogue-release job requires owner approval. No signing key,
   cached signature or trust pin should be edited locally to fake the new credit.
4. **Rendering-defect disposition.** [Issue95](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/95)
   (HUD moves inward on pause) and the inherited fire-escape coverage gap are
   unresolved. Fix and validate them before stable release, or explicitly record
   an owner decision to ship them as known issues. The pause bug was requested as
   an issue; creating it did not constitute a fix or release waiver.

PR91 stays draft while these gates remain. No game PR merge, version tag, stable
release, catalogue merge or protected publication was performed by this follow-up.
The ordinary user installation/saves remain separate from preview fixtures.


## Preview authorization — 2026-10-24

D197 supersedes the earlier no-merge/no-release disposition for a preview only.
The owner authorizes merging PR91 and publishing v0.11.0 with prerelease=true
and latest=false, accepting issue #95, the fire-escape gap and the three
backgrounds deferred to modpack 1.1. The owner will playtest Mac/Linux.
v0.10.7 remains latest stable. Final head CI, provenance/hash verification and
isolated final Windows bundled-installer checks still gate publication.
