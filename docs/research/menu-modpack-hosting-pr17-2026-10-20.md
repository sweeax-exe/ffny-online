# Menu-only artwork pack hosting discussion — 2026-10-20

**2026-10-21 follow-up:** Vlad chose Cloudflare R2, one new public JSON catalogue repository,
the helper initially in the game repository, and owner-approved GitHub Actions signing. The
catalogue source foundation and protected environment now exist; account/domain, key provisioning,
trusted publication and helper/launcher implementation remain. See the current
[implementation plan](launcher-mod-manager-implementation-2026-10-21.md). The report below preserves
the earlier investigation; its statements that the hosting preference/repository were undecided
describe the state on 2026-10-20.

## Finding

PR [#17](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/17) is an open, documentation-only proposal (D104, retitled after decision-number conflicts). Its described architecture is a signed `index.json` in a separate repository served by GitHub Pages; a separate `defjam-mods` helper performs fetch, resumable download, hash/size verification, install and rollback; the launcher would show available mods and updates while the game remains offline. Mod files are pinned by size and SHA-256. Submissions would arrive as reviewed pull requests and be scanned. The proposal says project hosting is for original content and recipe mods that generate from the player's own dump; derived assets should be linked from third-party hosts. It stages data mods before any code mods. PR #17 remains open and unscheduled. The API query `gh pr view 17 --comments` returned no comments; the PR diff and full design document were also retrieved from the PR head.

Provider choice, catalogue operations and helper implementation remain open. PR17 is an unscheduled design; the current menu artifact is a concrete local candidate that can be distributed as one independently versioned download when ready.

## Exact decisions and proposal details from PR #17

The proposal records Vlad’s 2026-10-11 answers as decisions for that design: derived game assets are only linked to third-party hosts; curation comes through reviewed pull requests plus security scans; data mods precede any code mods; network access belongs in a separate helper; hosting should be free or low-cost; updates are shown to the player and remain their choice; Faithful HD stays generated locally. These answers describe the intended general mod-system architecture.

The proposed implementation has three parts:

- A separate `vyanhursky/DefJamFFNY-mods` repository serves a signed `index.json` through GitHub Pages. CI would compile the index from one JSON file per mod. Each version records compatibility, dependencies/conflicts, reviewed/revoked status, one or more download URLs, exact byte size and SHA-256. `linked`, `original`, and `recipe` are separate content classes. The helper pins the signing public key; index signing key custody is open.
- `defjam-mods` is a headless CLI/JSON-progress helper, proposed in Python using Setup’s bundled runtime. It alone performs network requests: fetch/cache index, resumable `.part` downloads, verify size/hash, inspect/unpack to a temporary folder, install atomically, update, keep rollback copy, remove and run allow-listed recipes. Cached index supports offline use and never blocks Play. Launcher reads `state.json` for version and update badges.
- Launcher Mods page lists, enables, orders, and offers updates. Game stays offline. A generalized schema-2 manifest would unify precedence for textures, whole guest files and entries inside `.viv`, with later enabled mods winning and the original as fallback. Existing `[textures] packs` remains a compatibility fallback; this loader/Mods page/helper are proposals, not current features.

The review/security plan is specific: one mod per PR and append-only versions; no executables, scripts, links, ISO/disc content, absolute paths or `..`; allow-listed file extensions, per-file size cap and valid manifest; CI downloads every new file and checks its size/hash and every mirror; ClamAV scan, optional VirusTotal hash lookup; nightly link checks and automated revocation on changed hash. Recipes are declarative parameters using helper-pinned allow-listed tools and model weights with URL/hash/license; recipe output is an ordinary data mod. Code mods come only after data mods, with public source, CI-built releases, versioned API/ABI checks, an explicit warning and opt-in; native code has no sandbox.

The PR’s hosting comparison proposes GitHub Pages for the index; GitHub Releases for original assets/recipe tools (the PR text says under 2 GiB per file, up to 1,000 files, no bandwidth cap); Cloudflare R2 as a low-cost mirror for original content; Internet Archive as a free linked-content mirror (with torrent/web seed, slow and takedownable); GameBanana as modder-managed linked hosting; Thunderstore only after demonstrating community interest; Drive/MEGA/MediaFire only as mirrors because interstitials/quotas break unattended downloads; and no BitTorrent helper for v1. The document concludes that, under the owner’s answer, the project-hosted index, recipes and original work should fit free GitHub hosting. PR values are a dated design snapshot; current GitHub and R2 terms should be checked before any provider choice. Current official docs say each GitHub release file must be under 2 GiB, with up to 1,000 assets and no total release-size/bandwidth cap ([GitHub Releases docs](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)); Cloudflare currently lists 10 GB-month standard storage, 1M Class A and 10M Class B operations monthly free, then $0.015/GB-month standard storage and no R2 egress fee ([Cloudflare R2 pricing](https://developers.cloudflare.com/r2/pricing/)). These rates do not include account ownership, operations, availability, takedowns, rights, or actual traffic assumptions.

The broader proposal phases are: (1) local schema-2 mods and file/archive-entry overrides, (2) signed index/helper for texture packs, (3) recipes, (4) audio/movie substitution research, (5) fighter/stage spike and only then possible code mods, (6) extract a generic helper/index pattern if another recomp asks. Its six explicit unresolved owner questions are helper language and whether Setup retains Python; index repository name/owner; signing-key custody and automated vs manual signing; additional reviewers and turnaround; whether Textures page becomes Mods page; and whether interstitial-hosted linked mods should be manual-download browser entries.

## Fit for the menu-only pack

The first menu pack is a distinct product from Faithful HD. Faithful HD is built locally from the player's own dump; its generated pack is about 6.21 GB of PNGs, and the existing project policy keeps those files local. The menu work is sparse and source-identified, layered over Faithful HD, with current components such as restored, registered branding, conditional Mode, wide scenery, and the planned loading-screen component. Existing handover says packs are unpacked schema-1 directories under `<data>/mods`, ordered by semicolon-separated names; later packs override matching texture keys. Local review/approval records and exporter receipts provide useful provenance, but they are not a published runtime catalogue or a user-facing update protocol.

A menu pack should therefore have its own package identity and lifecycle, not be represented as a Faithful HD update. Keep Faithful HD generation/download state independent. Let the player enable/disable the menu component separately, state its compatible game/runtime range, and install it as a sparse override after the Faithful HD components. Conditional artwork remains clearly identified in the RC receipt and review; final owner native signoff is still required before promoting the candidate. Menu wide-scene work is still in integration/native acceptance; don't list it as a ready download until the active loaders, transforms and aspect checks pass.

The source pack contract is currently unpacked folders, schema 1, PNG straight alpha, safe relative paths, integer 1×–8× uniform scale and matching atlas aspect/layout. The established install path is `<data>/mods/<pack-name>`. There is no ZIP/archive installer contract yet. A downloadable archive would need an explicit extraction/atomic-install and rollback design in the helper; direct archive loading would require a runtime feature that does not exist today.

## First release recommendation

Keep the catalogue and downloads separate, as PR17 proposes. Give this pack its own stable ID,
version and update history. Faithful HD remains optional and independently generated/installed;
the menu pack loads after it. Ship one ZIP containing one unpackable schema-1 pack folder.
No runtime archive loader is needed. Initially manual extraction works with the current launcher;
a future helper can download, verify, stage and atomically install the same artifact.

Host small version/checksum/compatibility metadata in the proposed separate GitHub Pages catalogue.
Use an external mod host for the downloadable artwork, consistent with Vlad's recorded PR17 choice.
GameBanana is the mod-host option already named in that proposal; an owner-controlled object store
with immutable versioned URLs is more convenient for an automated downloader. A mirror can be added
without changing the package's pinned SHA-256. Do not replace an existing version in place.

Cloudflare R2 Standard is a practical object-storage option to discuss: current official pricing
includes 10 GB-month storage, 1 million writes and 10 million reads per month; additional storage is
$0.015/GB-month and Internet egress has no R2 bandwidth charge. This menu ZIP fits comfortably within
that storage tier even with several retained versions. Request counts and account/domain choices
still affect operation. At approximately 0.4 GB per download, 1,000 downloads move roughly 400 GB;
this is why egress pricing matters more than the single stored ZIP. These are sizing examples, not
a purchased service or a traffic forecast. [Current R2 pricing](https://developers.cloudflare.com/r2/pricing/)

GitHub Releases technically accepts individual files under 2 GiB and has no stated release bandwidth
cap, so this pack does not need splitting. PR17 assigns project-hosted releases to original work and
tools/recipes, however, and external linked hosting to derived artwork. Keep that existing distinction.
[Current GitHub release limits](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)

The approved imggen artwork cannot be reproduced exactly from the player's dump and the public
source recipes alone: the local approved masters are required build inputs. Do not advertise a
source-only one-click recipe as a substitute for a downloadable artwork pack. The public repository
can retain build scripts, registration tables, input/output hashes and prompts; artwork and captures
remain local under the repository's established hygiene rule.

For a future catalogue entry record the stable mod ID, version, compatible runtime range, archive
byte length/SHA-256, unpacked size, download/mirror URLs and precedence after Faithful HD. Preserve
one prior installed version for rollback. The current RC remains a local playtest; this investigation
has created no hosting account, bucket, catalogue repository or public artwork upload.

The useful next hosting choice is community mod-host downloads versus owner-controlled direct
object storage. Catalogue signing and the helper can follow PR17 as a separate implementation task;
there is no need to bundle that system into the menu artwork release.

## Local references

- [HD texture guide](../hd-textures.md): current folder format, ordering, limits and local-generation workflow.
- [HD texture release packaging](hd-texture-release-packaging.md): measured Faithful HD corpus size, local-build rationale and prior GitHub release asset limit check.
- [Menu/UI modpack handover](../menu-ui-modpack-handover.md): approved artwork scope, local pack export and integration gates.
- [Menu wide first-release integration audit](menu-wide-first-release-integration-audit-2026-10-20.md): current wide/loading acceptance gaps and local component composition.
- [Menu UI playtest pack report](menu-ui-playtest-pack-2026-10-19.md): concrete local component order and macOS test evidence.
- [PR #17](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/17): prior design discussion and visible proposal.

No hosting account, public asset, code, package, or game installation was changed for this report.
