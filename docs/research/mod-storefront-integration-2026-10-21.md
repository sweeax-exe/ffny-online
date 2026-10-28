# GameBanana, Thunderstore and Quiver alongside the curated catalogue

2026-10-21. Owner asks how those channels fit; he has not replaced the previously
approved R2 direction or asked for third-party listings/publication in this turn.

## Finding

Both services can host/list compatible texture mods. The game still needs its
own texture loader and enabled-pack order. A hosting service, catalogue and
installer perform different jobs; adding a storefront does not replace runtime
support or automatically make console/other-recomp mods compatible with this port.

| Component | Responsibility |
|---|---|
| GameBanana / Thunderstore | Public mod pages, discovery, version listings, downloadable packages and community |
| Project signed JSON catalogue | Curated reviewed versions, source identity, compatibility, pinned size/hash, revocation and suggested ordering |
| Def Jam launcher/helper | Verification, installation/update/rollback and shared local/downloaded pack order |
| R2 | Optional owner-controlled download origin or byte-identical mirror |

The existing generic HTTPS URL list can point at compatible direct ZIP downloads
from any host. A mod page belongs in `homepage`; the installer needs the exact
reviewed file URL in `archive.urls`. Do not resolve a page's latest file at install
time without an approved catalogue version and SHA-256. GameBanana submissions can
have several files/variants, so pin the selected file/version, not an ambiguous
latest-download link. Automatic provider browsing/API selection is additional UI
work; generic direct-download support is already the intended helper contract.

No broad storefront listing is implicitly project-approved. A locally/manual
installed pack remains Local unless a managed receipt proves catalogue ownership.
Faithful HD stays locally generated and separate from the menu download.

## GameBanana

[GameBanana](https://download.gamebanana.com/) advertises public downloads and
one-click integration with community installers. Its [API](https://api.gamebanana.com/)
exposes submission data and lists; Quiver has an actual provider for it. A release
page could supply screenshots, instructions, feedback and downloadable versions,
with our catalogue linking the reviewed file. Direct downloads must still be
verified against our resumable-download/hash rules before listing them as a tested
host. Neither a GameBanana page nor host moderation supplies our game's load-order
semantics. A dedicated category for this Xbox-based port should be confirmed;
mods for the console or another Def Jam recomp are not interchangeable by name.

### Relevant constraint for this particular menu pack

The rendered [site-wide AI policy](https://gamebanana.com/wikis/2175), read in the
browser because the static page omits its body, requires clear disclosure, a link
to the AI software and input/dataset/software credits. It also requires knowing
the training data used for generated artwork. Its AI-assisted-code addendum
explicitly does not exempt textures or other creative assets. Some game sections
impose stricter rules, so site-wide compatibility alone is insufficient.

Our menu restoration/outpainting used imggen. We cannot document its complete
training dataset, so we cannot claim the pack meets GameBanana's stated artwork
requirements. Do not upload and rely on a disclosure alone. Moderator clarification
would be needed before recommending a listing of this pack. None has been sought.
This changes the preliminary recommendation: R2 remains the primary route for
these images; GameBanana is conditional, despite the technical integration fitting.

## Thunderstore

Its [package specification](https://wiki.thunderstore.io/mods/creating-a-package)
requires `manifest.json`, `README.md`, and a 256x256 `icon.png` at ZIP root. Its
version field is three numeric components; dependency identities name a team,
package and version. [Install routing is game/loader-specific](https://wiki.thunderstore.io/mods/packaging-your-mods),
not an automatic property of recompilation.

The current menu ZIP has one versioned pack folder at root. A Thunderstore export
would wrap the same runtime payload with storefront metadata. That export is a
**different archive** with its own SHA-256/size, not a byte-identical mirror of the
R2 ZIP. The present helper deliberately accepts one pack root and rejects unknown
root metadata; support needs an explicit validated wrapper adapter or a separate
canonical download. Never relax archive validation globally to ignore arbitrary
content. Keep runtime `manifest.ini` and Thunderstore `manifest.json` distinct.

[New communities](https://wiki.thunderstore.io/ecosystem/adding-a-new-game) are
requested through Thunderstore's Discord and require existing mod developer
interest, ideally ready mods. Their process includes game metadata and mod-manager
routing. No community request or contact has been sent. This report does not
assert that a dedicated community already exists.

## Quiver

[Quiver's own README](https://github.com/tgeorgiadis/quiver-launcher#mods-thunderstore--gamebanana)
confirms browsing/install/update providers for both hosts. It requires `mods.path`
and `mods.sources`; `folderPerMod` wraps flat payloads and leaves existing
single-folder payloads intact. Its source confirms the configured mods path is
relative to the Quiver application install root, and source entries contain a
provider and source URL:

- [GameModsConfig.cs](https://github.com/tgeorgiadis/quiver-launcher/blob/main/Services/Mods/GameModsConfig.cs)
- [ModInstallService.cs](https://github.com/tgeorgiadis/quiver-launcher/blob/main/Services/Mods/ModInstallService.cs)

Our installed source/executable and external data folder are separate. Therefore
Quiver needs a deliberate path bridge/import workflow and coordination with our
`[textures] packs` list; setting only a provider URL will not enable packs or order
them. Files it installs into the correct data/mods location can be discovered as
Local packs, then enabled/ordered in Def Jam. Use one manager as the update owner
of each folder; two independent updaters must not overwrite each other's receipts
or local edits. A fully integrated one-click flow is not implemented or tested.

## Recommendation

Keep the host-independent signed catalogue and the game's shared load order.
For this imggen-restored menu pack, retain R2 as the primary download route until
GameBanana clarifies whether its artwork rules permit it. GameBanana remains a
technically suitable community channel for compatible submissions that meet its
rules. Pursue Thunderstore when a suitable community, content acceptance and
routing are arranged; no absence of an AI rule is inferred from search results. Keep canonical payload identity separate from provider
wrappers so future provider adapters preserve exact art and precedence. No new
hosting account, publication, manager integration or external message is implied
by this recommendation. Sources checked 2026-10-21.
