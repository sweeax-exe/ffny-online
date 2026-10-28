# Launcher mod delivery readiness — 2026-10-23

Historical audit at10:18 local time. Subsequent integration and acceptance are
tracked in [the integrated RC record](integrated-menu-launcher-rc-2026-10-23.md).

The requested launcher-only experience is not ready end to end. The independent
menu RC2 archive is uploaded and publicly verified on R2. Direct download and
manual extraction are an interim test path, not fulfillment of launcher delivery.
No implementation, game, save or configuration changes were made during this audit.

## Four game PRs

| PR | Purpose | Current status |
| --- | --- | --- |
| [17](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/17) | Mod-system architecture/design: signed GitHub JSON, external archives, helper and future override types. | Open, docs only, conflicts with main. No runtime functionality. |
| [62](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/62) | Separate HD/widescreen menu builders, registrations, manifests and native test records; toolkit wide-slab support. Artwork stays outside Git. | Draft/open, conflicts with main; toolkit dd63e17 includes PR73's settings fix. Latest documentation head d447922 has no reported checks. |
| [73](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/73) | Fix pack lists longer than 95 characters; allow 1023 bytes and preserve long settings lines. | Open, conflicts with main; already included in PR62's combined toolkit. Integrate once. |
| [82](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/82) | Verified catalogue refresh/install/update/rollback, local/project discovery and shared enable/load order UI; bundled verification helper. | Draft/open, conflicts with main; toolkit 32aab4d does not include wide slabs. CI has two failed jobs. |

Latest release is v0.10.7, main toolkit8589f8834e3c370e2def3c6757b0d4f6e3e89d70.
Its texture_pack.c contains neither [wide] parsing nor texture_pack_wide_slabs.
The release does not include the PR82 downloader or the menu wide-slab runtime.
Do not resolve integration by replacing a newer toolkit pin with an older pin;
combine the required fixes on the current published toolkit and pin that result.

## Requirements and evidence

- Cloudflare archive: complete. RC2 is421563157bytes (421.6MB),326files,
  423392524bytes installed, SHA256
  43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634.
  Full public download/hash and nonzero-offset HTTP206 range verified.
  See [upload report](menu-ui-r2-windows-playtest-2026-10-23.md).
  Faithful is optional and not bundled.
- Hosted catalogue: not live. The exact helper URL
  https://vyanhursky.github.io/DefJamFFNY-mods/index.json and its .sig both
  return404. Pages API also returns404. Catalogue main is the foundation only;
  no menu version record has been added. Source publication implementation is
  [catalogue PR1](https://github.com/vyanhursky/DefJamFFNY-mods/pull/1), open,
  mergeable and validation green. Key pin, private backup and protected signing
  secret are provisioned; publication requires the owner's environment approval.
- Launcher install/enable/order: implemented in PR82, not integrated or released.
  Real RC2 ZIP format validation and synthetic native UI tours passed. An actual
  signed live catalogue -> R2 -> native launcher install -> enable/order -> game
  run has not been completed.
- Runtime: wide support exists/tested on the PR62 line, conditional on compatible
  loaded wide metadata/slabs. It is absent from current release and PR82's pin.
  Cross-platform and remaining menu-specific acceptance gates remain.

## Concrete blockers

1. Reconcile game PR conflicts and combine current-main launcher plus PR82 with
   published toolkit wide support and long settings handling. PR17 is reference
   design, not a runtime dependency; PR73 must not undo PR62's wide loader.
2. PR82 CI39626852537: Windows job119314999213 failed
   test_runtime_lock_interoperates_with_python_helper (PermissionError in
   setup/mods.py file_lock, one failed/62 passed). macOS packaging
   job119314998421 failed at hdiutil create with No space left on device.
   This is not a Windows compilation failure. Fix/verify the locking behavior
   and get a successful macOS package build.
3. Finish/review catalogue PR1, configure/deploy Pages, bootstrap signed history
   according to its publisher, add the exact menu record with R2 URL, size,
   SHA256, expansion bounds, runtime capability and load-after metadata, then
   obtain owner approval of the protected publishing job.
4. Current launcher view filters channel==stable and bridge passes no
   --prerelease; CLI supports it. RC2 needs an intentional testing-channel UI
   path, or a genuinely approved final release. Merely adding RC2 metadata will
   not make this version installable through the current native UI.
5. Test the real launcher-only workflow with and without Faithful, confirm final
   texture winners and persistent order, offline restart, cancellation/update/
   recovery, and native Windows acceptance plus 4:3/wide menu/transition checks.
   Prior full native suite15/18 and documented fixture/baseline failures remain
   visible; they are not an end-to-end hosted install pass.

No R2 credential/upload blocker remains. A production custom domain is advisable
for public distribution; r2.dev suffices for controlled testing and is not the
reason the integration is blocked. No merge, tag, signature publication, art edit,
new game run or credential exposure occurred in this audit.
