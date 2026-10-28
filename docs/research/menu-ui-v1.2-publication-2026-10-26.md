# HD Widescreen Menus 1.2.0 publication — 2026-10-26

The owner approved the complete 1.2 text candidate and explicitly requested
packaging and publication. The stable package contains 1,140 Stronger edge
clarity text overrides, including 214 fighter-banner variants, with the approved
1.1 widescreen scenery unchanged. See the [text census and native validation](menu-ui-text-v1.2-2026-10-26.md)
and `config/menu-ui-text-v1.2-candidate.json` for treatment and per-texture pins.

## Stable package

| Field | Value |
|---|---|
| Version | 1.2.0 |
| Archive | `menu-ui-hd-wide-v1.2.0.zip` |
| Root | `menu-ui-hd-wide-v1-2-0` |
| SHA-256 | `bb9109ef1fa77f78796afd051c4d6c602f5dbfa0554e698f3b8746be4cbd3589` |
| Download bytes | 643,020,896 |
| Unpacked bytes | 646,222,232 |
| Files | 1,528 |
| Native texture mappings | 1,304 |
| Wide/stretch mappings | 80 |

The [immutable Cloudflare archive](https://pub-9167b11f83f24f2a9e21af7f0e225057.r2.dev/menu-ui-hd-wide/1.2.0/bb9109ef1fa77f78796afd051c4d6c602f5dbfa0554e698f3b8746be4cbd3589/menu-ui-hd-wide-v1.2.0.zip)
has adjacent `.sha256` and `RELEASE-NOTES.txt` objects. Upload preflight refuses
an existing object with different size/hash metadata. Full public download byte
count and SHA-256, plus HTTP range bytes, verified after upload. Every one of the
1,526 non-metadata files matches the approved RC1 exactly; only manifest naming,
receipt and archive-root metadata change. The stable ZIP passes the released
0.12.1 helper's strict unpack and image/manifest validation.

## Signed catalogue

[Catalogue PR #5](https://github.com/vyanhursky/DefJamFFNY-mods/pull/5), milestone
v0.12.2, appends 1.2.0 without changing any earlier version record. The 14 catalogue
tests, metadata validation and append-only comparison against trusted revision 4
pass; PR CI passes. Merge commit: `5c43ffc8bbac2a1a81ffc7a5e434716e8b3e8529`.

The [normal protected publication workflow](https://github.com/vyanhursky/DefJamFFNY-mods/actions/runs/40450457830)
runs with `bootstrap=false` and completed successfully: all download/content/ClamAV
checks, the existing environment approval, signing and Pages deployment. The
owner's explicit shipment approval authorized the normal deployment approval;
no trust pin, history reset or protection rule was changed.

The [live signed catalogue](https://vyanhursky.github.io/DefJamFFNY-mods/index.json)
is **revision 5**. Exact index SHA-256:
`28c145a4ea535b59b2ee59a8679cb72ee88e97acda78300f13da56bef0786918`.
An independent fetch verifies its Ed25519 signature against the unchanged
`4c7c170fa624aeb0aeac40f19c4fa75365eac700f61e9f9902ca42848b840ffd`
key, append-only revision-4 history and exact 1.2 archive descriptor.

## Released helper compatibility

Runtime minimum remains **0.12.1**, with `textures-v1` and `wide-v1`. The artwork
does not need a game rebuild or reinstall. Faithful HD remains optional and is
not bundled. Arbitrary player names, changing counters and unbounded live-font
variants retain the runtime font path; this release does not change that engine.

The unmodified helper files are byte-identical to tag v0.12.1. In a disposable
data root it first installed signed 1.1 from revision 4, refreshed to trusted
revision 5, selected and downloaded 1.2 over the network, and completed a real
managed update. All 1,528 installed files verify, all 404 previous-version files
remain exact in rollback, and an isolated save marker is unchanged. Evidence:
`logs/menu-ui-v1.2/publication/released-helper-update-result.json`.

Players can open Mods, select **Refresh catalogue**, then use **Update available**
for HD Widescreen Menus. A game update or reinstall is unnecessary for this pack.

The separate fire-escape transition geometry repair remains game PR #131,
milestone v0.12.2. Updating this texture pack cannot install that game-code fix.

Private publication evidence is under `logs/menu-ui-v1.2/publication/`. No artwork,
captures, archives, game data, keys or private recovery paths are tracked in Git.
