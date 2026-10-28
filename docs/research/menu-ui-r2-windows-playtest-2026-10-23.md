# Menu RC2: R2 upload and Windows playtest — 2026-10-23

## Scope and authorization

Vlad supplied bucket `defjam-ffny-mods` and public development origin
`https://pub-9167b11f83f24f2a9e21af7f0e225057.r2.dev`, then explicitly requested
uploading the separate texture pack so he can test Windows. This authorizes the
exact RC2 artwork upload to that bucket, not a stable catalogue release or an
executable upload. Faithful HD stays optional and unbundled. No art bytes enter Git.

The local ZIP was rechecked before upload:421563157bytes,326files,423392524bytes
expanded,157texture mappings,62wide entries; all ZIP CRCs pass. SHA256:
`43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634`.

Cloudflare's dashboard is signed in and confirms this bucket, Standard storage
and public access. Its browser uploader explicitly limits files to300MB. Vlad
configured a bucket-scoped R2 S3 credential locally under AWS profile `defjam-r2`;
no credential values were printed, committed or sent in chat. A head-bucket probe
succeeds. Upload uses the existing AWS CLI and the R2 S3 endpoint. The private
profile is not part of this repository or the downloadable files.

## Transfer and integrity

The immutable object prefix is:
`menu-ui-hd-wide/1.0.0-rc2/43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634/`.

The operator uploads the exact original ZIP, a SHA256 sidecar and
`WINDOWS-PLAYTEST.txt`. Object metadata records each file's SHA256 and mod identity;
existing differing objects are refused rather than overwritten. The ZIP uses
`application/zip`, attachment disposition and immutable cache metadata.
Full public-download SHA256/length and a nonzero-offset HTTP Range206 response
must pass before recording this as uploaded. Local operational output/receipts:
`logs/menu-ui-r2-20261023/`; they contain no credential material. Final transfer
receipt and links are appended after successful verification.

## Windows runtime prerequisite

Checked the current public release **v0.10.7** on2026-10-23. Its toolkit pin is
`8589f8834e3c370e2def3c6757b0d4f6e3e89d70`. The published
`src/nv2a/texture_pack.c` contains neither `[wide]` parsing nor the
`texture_pack_wide_slabs` API. Its HD replacements alone do not establish
widescreen support for this pack. This is independent of hosting/download success.

Use a game built from the tested menu branch
[PR62](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/62), whose toolkit is
`dd63e17f0bd0a23f5a5b7218ee1c3bbb2ced994d`, or another build retaining that wide
loader. Build from the player's own supported dump; no game executable is shipped.
[PR73](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/73) remains open and fixes
long pack-list handling separately. Neither the texture upload nor
[catalogue-helper PR82](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/82)
automatically integrates the wide loader into the released game.

Extract the pack so `<data>/mods/menu-ui-hd-wide-v1-rc2/manifest.ini` exists.
Refresh installed mods, enable HD textures and this pack, and place it after
optional Faithful components. Disable old menu test packs; restart after order
changes. Preserve the old enabled list for comparison/rollback. Full Windows
instructions are in the uploaded text file, with the checksum command.

Test original4:3 and16:9/16:10/21:9/32:9 as available: title/fire-escape/Main Menu,
Options/Jukebox/Battle setup/player count, pre-fight loading, High Scores/Extras,
Story phone/character/available Crib. Record exact game/toolkit/settings/pack order
and screenshots. Existing transition-first-tile, conditional Mode lighting/native
branding, late Story and3-player/team acceptance gates are retained; this remains
RC2. No game was launched or ordinary save/settings changed for this upload task.

GitHub Pages still owns the future signed catalogue. R2 serves the versioned art
archive. The development `r2.dev` origin is for playtest traffic; production domain
and owner-approved signed catalogue publication remain separate work.


## Verified public delivery

Completed 2026-10-23T16:46:50Z. All three objects returned HTTP200 and their
full public-download bytes matched local SHA256/length. A64KiB request at a
nonzero1MiB offset returned HTTP206 with the exact Content-Range and original
ZIP bytes. This proves the development host serves the artifact and supports
range resume; it is not a native Windows/visual test.

- [menu-ui-hd-wide-v1-rc2.zip](https://pub-9167b11f83f24f2a9e21af7f0e225057.r2.dev/menu-ui-hd-wide/1.0.0-rc2/43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634/menu-ui-hd-wide-v1-rc2.zip) — 421563157bytes; SHA256 `43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634`.
- [menu-ui-hd-wide-v1-rc2.zip.sha256](https://pub-9167b11f83f24f2a9e21af7f0e225057.r2.dev/menu-ui-hd-wide/1.0.0-rc2/43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634/menu-ui-hd-wide-v1-rc2.zip.sha256) — 93bytes; SHA256 `b6cd7afbacddd50004fd646986e039c63f9112b287e55f4ba8d992452c79b33c`.
- [WINDOWS-PLAYTEST.txt](https://pub-9167b11f83f24f2a9e21af7f0e225057.r2.dev/menu-ui-hd-wide/1.0.0-rc2/43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634/WINDOWS-PLAYTEST.txt) — 2500bytes; SHA256 `98a6d6f4d5d00bd39929a73b0ea0d560c99f6f28c087049752be0b1fe106d814`.


## Compatible Windows setup reference and guide correction

The existing successful CI run39447027154 retains an unexpired
[windows-setup-assets artifact](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/39447027154/artifacts/12277037576).
Downloaded artifact SHA256 matches GitHub's digest:
`9e86316376a9c8a0d3051ba33e4e8bcb5a3e9eaf66207bf25b9ce384431f168d`.
Its `DefJamSetup-0.10.5-windows-x64.exe` independently matches the checksum and
provenance: `12e8e07f09d0300abfae5e4f8edd8f8bb68cd4b61aacc5e07036ce137a1c36f0`.
Source commit `bf967a316b8347a690908be320c0e8e8492a4f94`; toolkit
`dd63e17f0bd0a23f5a5b7218ee1c3bbb2ced994d`. This is the menu-branch CI candidate,
not the public v0.10.5 release or a new stable release. It compiles the player's
own dump; the existing artifact was read/verified, not executed or copied to R2.
Windows native/visual acceptance remains the owner's upcoming test.

Choose **Bundled release (offline)** in setup, with separate install/data folders,
so the wizard does not fetch the newer public release without the wide loader.
The older compatible launcher has a **Textures** tab, **Use HD texture packs**
and **Pack folders**. Start menu-only with `menu-ui-hd-wide-v1-rc2`; settings
fallback is `[textures] enabled=true`, `packs=menu-ui-hd-wide-v1-rc2`.
The first companion guide assumed the newer Mods page; a separate immutable v2
guide corrects this. The earlier object remains unchanged.

- [Superseded v2 Windows guide](https://pub-9167b11f83f24f2a9e21af7f0e225057.r2.dev/menu-ui-hd-wide/1.0.0-rc2/43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634/WINDOWS-PLAYTEST-v2.txt) — 3573bytes, public SHA256 `8f4e069d2aa8132f1cb33e9d70a2e35b708fe2d89142647f3321b06bebacce43`.

All four objects are public-byte verified. The v2 default Python-urllib request
received403; the same application User-Agent as the successful ZIP verification
returned200 and matched exact bytes. Actual helper User-Agent `DefJamMods/1` also
passes a ranged ZIP read. No Cloudflare security/access setting was changed.


### Final guide revision

The actual combined `dd63e17` header defines `RECOMP_SETTING_TEXT_MAX=1024`
(1023 usable bytes); its wider settings support was already integrated. v2's
older-limit sentence was incorrect. v3 corrects it and is the preferred guide.
The exact texture ZIP is unchanged. Earlier companion objects are retained
immutably; final delivery links only v3. Five public objects are verified in the
local receipt; only the ZIP, SHA256 sidecar and preferred v3 guide are current.

- [Final Windows playtest guide](https://pub-9167b11f83f24f2a9e21af7f0e225057.r2.dev/menu-ui-hd-wide/1.0.0-rc2/43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634/WINDOWS-PLAYTEST-v3.txt) — 3572bytes; public SHA256 `44e878b6ee1597fd6cbaabc563295307b828365435cf8f5254733cae5fe9ac7a`.
