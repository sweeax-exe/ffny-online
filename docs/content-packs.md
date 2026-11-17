# Community content packs

FFNY Online's mods pipeline: a modder builds a pack in FFNY Studio and submits it with their FFNY account;
the account service checks it and queues it; the team reviews it in Discord (Accept / Reject); on Accept the
service signs the pack and our catalogue with its **content-signing key** (never the update key, kept only on
the server); every launcher then installs it by itself. The server and bot side is in the private server
repository (`docs/content-catalogue.md` there); Studio's side is in the Studio repository.

## In the game repository

| Piece | What it does |
|---|---|
| `setup/mods.py sync` | Fetches `https://play.defjam.online/updates/content/index-v3.json` and `.sig` (below: which file for which launcher), checks the Ed25519 signature against `config/mods/catalogue-keys.json` (the content key's public half), the 30-day expiry and the revision (no rollback). Installs every listed pack in the catalogue's `order` (clothing IDs depend on it), downloading only packs whose version or archive changed; checks each pack's `content_sha256` and `pack_signature`; removes synced packs the catalogue dropped or revoked; keeps a folder the player installed by hand. Writes `<data>/mod-manager/content.ini`. |
| `setup/mod_catalogue.py` | The catalogue contract, with FFNY's optional fields: `kind` (`content` / `replacement`), `type`, `order`, `language`, `fighter_fe_id`, `bundle` with `bundle_name` (always together); per version `content_sha256`, `pack_signature`. |
| `setup/mods.py compose`, `setup/content_compose.py` | Composed packs (below): rebuilds the patched game files from the player's dump and each pack's deltas and ops. Runs at the end of every `sync`, as `mods.py compose` (the Content page's switch starts it), and is skipped when the packs, the switches and the bases are unchanged. `setup/ffnycontent/` is the part of FFNY Studio's library it needs (deltas, BIG archives, `djv2.sod`, `.loc`, the parts list, clothing ops, new venues). |
| `src/hooks/pc_content.c` | Reads `content.ini` and the player's switches (`settings.ini [content] off`). At game start the synced texture packs go beneath the player's own packs in `RECOMP_TEXTURE_PACKS`, and the composed files are served through the game-file redirect (ahead of `<data>/override`; the language and song redirects chain to it; `[CONTENT] n composed file(s) served` in the log). The online **content print** (FNV-1a over id, version and content hash of every `content` pack, in order) is mixed into the build fingerprint and sent after the build number in HELLO, so rooms only match PCs with the same `content` set; a composed `content` pack counts only when `files.ini` lists it as applied. Replacements and language packs never count. |
| Launcher | Syncs at its start and with each update check (`pc_launcher_content.inc`); PLAY during a sync cancels the download and waits for it to stop. The **Content** button opens the page: installed packs in catalogue order, `content` packs marked ALWAYS ON, a switch for each `replacement`. Settings > General > Game language lists English, French and every language pack that is on. |
| Community tab | The Content page's second tab (`pc_launcher_community.inc`): every submitted pack from the account service's gallery (`GET /api/gallery`, sorted by votes or newest), also packs in review and rejected ones with the reason; the selected pack's page with its preview, description and changelog; Vote / Voted for signed-in players (one vote per account and pack). View only: nothing is downloaded from here. FFNY Studio's Community view reads the same endpoints. `RECOMP_CONTENT_TAB=community` opens it (screenshots). |
| FFNY Studio button | The home screen's **FFNY STUDIO** card between the logo and PLAY (`pc_launcher_studio.inc`, `studio_app.cpp`, `studio_unzip.cpp`; Windows only). **Closed test (2026-11-10):** Studio goes to testers first, by the Discord role `FFNY Studio Beta` on the FFNY Online server (the server's owner and the pack admins always have access; server repository `docs/studio-closed-test.md`). At the launcher's start, whenever the player signs in or out, on hover (at most every two minutes) and every ten minutes while the answer is no, it asks the account service `GET /api/studio/access` with the player's session. The card is hidden when nobody is signed in or the account has no access (an installed Studio stays reachable while the service does not answer; Studio checks the access itself). With access it reads `GET /api/studio/latest` (`{"manifest": the signed text, "signature"}`) and believes the manifest only with a valid signature by the update key (`update_sig.h`, as `latest.json`); the card then shows once such a manifest exists or Studio is installed. The public folder (`studio/studio-latest.json` and its `.sig` on the update port, `https://play.defjam.online/updates/studio/`) is no longer read, except behind the test switches below. Its button: **Get FFNY Studio (<size>)**, the download's progress, **Open FFNY Studio**, or **Update Studio** when the published `build` is newer; a failure is one line in the card. The zip is `GET /api/studio/download/<setup>` with the session (`account_download` in `account.c`; on the public test path the manifest's `setup` beside the manifest, an absolute `url` only on the same host), resumed with a Range request, checked against `size` and `sha256`, unpacked (deflate, CRC-checked, only names under `FFNYStudio/`) into `<studio>\versions\<version>\`, then `<studio>\current.txt` is switched; the previous version stays, older ones go. `<studio>` = `RECOMP_STUDIO_DIR`, else `studio` beside the data folder (`%LOCALAPPDATA%\DefJamRecompiled\studio`). Open starts `FFNYStudio.exe --install-dir <install> --data-root <data> --account-file <data>\account.dat` in the exe's folder (`--install-dir` only for an installed copy). Tests: `RECOMP_STUDIO_SERVER=127.0.0.1:<port>` (the public path on a local server: plain HTTP, `/studio/...`, no access check), `RECOMP_STUDIO_PUBLIC=1` (the public path on the update servers), `RECOMP_STUDIO_TEST_KEY=<128 hex>` (only with that local server, or with `RECOMP_ACCOUNT_SERVER` on this PC for the account path), `RECOMP_TEST_STUDIO=<frame>:press;...`, `RECOMP_STUDIO_BUTTON=1` (show the card without access or a manifest); `scripts/test-studio-launcher.py` runs both paths: the public one, then the closed test against a local account service with a fake Discord API (signed out, refused, allowed, install with a resumed download, a wrong signature). |
| `RECOMP_OFFLINE_ONLY=1` | Studio's Test in a fight: every online start is refused ("Online is off in this test session"). |

Kinds: `content` (new fighters, clothing, venues) is always on and part of online matching; `replacement`
(retextures, language packs) downloads too but can be switched off per PC. A language pack is used only
when chosen as the Game language.

## Composed packs (fighter-body, clothing, venue, new-fighter)

These packs never carry the game's bytes. Their `install.json` (schema 1; the shared contract with the
server and Studio) lists **entries**, each a delta (`deltas/*.ffdelta`, FFDL v1: copy ranges from the player's
own entry plus the modder's bytes) that `replace`s or `add`s one archive entry or loose file, and **ops**
(clothing `colour` / `item`, venue `new-venue`) that append to the player's `djv2.sod`, parts list, name tables,
`venue.xml` and `entity.xml`. The catalogue lists them with the capability `content-v1`; content.ini marks them
`composed=1`.

A new venue may carry its own far crowd (the rows drawn at the bones of a locator file in `assets/icrowd.viv`):
the op's optional `"crowd_locator": "icl_<code>"` and exactly one entry, the `add` of `assets/icrowd.viv|icl_<code>.o`
from a retail locator (`icl_<template code>.o`), an ELF file of the same size. The new entity's
`<instancecrowdlocator>` names it; the template's stays untouched. Anything else in `icrowd.viv` from a new-venue
pack, or one of the two without the other, skips the pack (`ffnycontent/newvenue.py`, `check_crowd`). Launchers
up to build 79 ignore the field: the venue installs and the copy keeps the template's far crowd (drawn only,
never in the fight).

A new venue may carry breakable objects (FFNY Studio beta.24): the op's optional `"models": ["V2BG_<code>_Z", ...]`
(1 to 12, letters O..Z that the template's own stage models do not use) and, for each, the `add` of
`assets/v2bg.viv|<model>.o` from the game's own breakable model `V2BG_13A_J.o` (its retail SHA-256), an ELF file
renamed to the model. The new entity gets a `<model>` line for each, after the template's (the game draws no stage
model its entity does not list). Any other `v2bg.viv` add named `V2BG_<code>_<O..Z>.o`, except the template's own
stage model copied under the new code, skips the pack (`check_models`, `check_model`). The catalogue lists such a
pack with `venue-models-v1` as well, which launchers from build 83 pass (`pc_mods.cpp`); older ones skip it (they
ignored the field and would not draw the objects). Composer 5.

On the player's PC the base of a path is `<data>/override/<path>` when it exists, else the dump
(`--game-dir`, default `<data>/extracted`, else `./game`; the launcher passes its `game` junction resolved).
Packs apply in catalogue order, minus replacements switched off; ops first, then entries. A pack whose delta
does not match the player's file, whose added name exists already (in the dump or from an earlier pack), whose
venue feID or filename is taken, or whose op fails is skipped whole and the rest go on; of two replacements of
one entry the later wins. The result is `<data>/mod-manager/content-files/<path>` and `files.ini`:

```
[content-files]
schema=1
set=<sha256 over "id\0version\0content_sha256\n" of the applied packs, then "off:" + the switched-off composed packs, sorted, comma-separated>
applied=<id>,<id>
skipped=<id>: <reason>; <id>: <reason>
file=assets/v2ip.viv
```

It is built in `content-files.new` and swapped in; with nothing applied the folder is removed, so switching a
pack off or the catalogue dropping it restores the originals. `content-compose.json` beside it records the
inputs and the bases' size and mtime for the unchanged check. Added entries are stored as literal RefPack (raw
past 16 MiB), replacements as the original entry was stored.

**With a language pack.** A language pack rebuilds some of the same files from the dump at game start
(`assets/misc.viv` for its string tables, `screens/screens.viv` for its menu art), and the game serves its
copies ahead of the composed ones, which would lose a clothing pack's item names (`cmusen.loc`) and shop
pictures (the thumbnails added to `screens.viv`) while the language is on. So the composer applies the same
packs once more for every installed language pack that is not switched off (`<data>/mods/<id>/language.ini`,
at most 8, by id): the files the pack covers among those the packs change are rebuilt first as the game would
(`scripts/langpack.py`, the reference the game's rebuild is tested against; from the dump, never the override
copy, as the game does), the packs' ops and entries go on top (the deltas' originals stay the player's own
files; of a language edit and a pack's `replace` of one entry the pack wins), and the results go to
`content-files/lang/<id>/<path>`, listed as `lang=<id>|<path>`. Byte-identical on two PCs with the same packs
and language pack; the `set` and the online print are the packs alone. At game start, once the chosen language
pack has loaded, `pc_language.c` asks `pc_content_language_files` for those paths: the content redirect serves
the `lang/<id>/` copies from then on, and the pack rebuilds only its other files (`[CONTENT] n composed file(s)
built on the language pack <id>`). A language pack whose rebuild fails here, or on whose files the packs do not
apply alike, is left out (`language-skipped=<id>: <reason>`): the game then rebuilds that pack's files itself,
as before, without the packs' names. The language packs' `language.ini` and deltas are part of the unchanged
check, so a pack installed by hand is taken up at the next sync or `compose`.

**New fighters (type `new-fighter`, kind content).** FFNY Studio's "Add as new fighter" makes a pack whose
entries add the new model's own files to `assets/v2ip.viv` (`V2IP_7xxA.o/.xsh/_F.o`, deltas against the
template's, the model's symbols renamed) and whose one op `{"op": "new-fighter", "template_fe_id", "template_model",
"model", "name"}` the composer turns into the rest from the player's own files (`setup/ffnycontent/newfighter.py`,
the same text as Studio's `ffnystudio/newfighter.py`): a Character row in `djv2.sod` (the template's row with its
own front-end ID, name and model; the template's coreID, so it fights like the template), the model's
`entity.xml` entity, its `IntroExtro.xml` entry in `nis.viv`, its `CE_`/`CI_` camera banks in `v2ta.viv` and
placeholder menu pictures in `screens.viv`. The front-end ID (one of 48 places: 100..127 and, from build 85, the 20
low IDs 5 15 29 37 39 42 49 53 63 66 79 86 87 88 89 90 94 95 96 97; `newfighter.PLACES`, the game's
`src/hooks/new_fighter_places.h`) is the **account service's place for the pack**, carried in the signed catalogue as the mod's `fighter_fe_id` and kept in the receipt: two players with the
same packs build the same files whatever else they have. A synced new fighter without a place, or whose place or
model is taken, is skipped. A pack installed by hand (or a Studio test) names its place in the op (`fe_id`).
The catalogue with new fighters is `index-v2.json`; `index.json` is the same catalogue without them for launchers
up to build 78, whose strict check would refuse the type and the key. Launchers of build 79 read `index-v2.json`;
from the mod-bundles branch on the launcher reads `index-v3.json` (see "Packs of several mods" below), and from build 85
`index-v4.json`: the only one that lists a new fighter at a low place (its version has the capability
`fighter-places-v2`, which the build-85 game offers, `src/hooks/pc_mods.cpp`); a launcher up to build 84 refuses a
`fighter_fe_id` outside 100..127 and with it the whole catalogue. The account service gives low places only once
build 85 is out (its `-fighter-places-switch`), in the order 100..123, the low places, 124..127. At place 37 the
retail screens hold an unused Crow 2's pictures (`thumb37`, `l_MS_37_*`, `38_2p/3p`): the new fighter's copies
replace them, and `scripts/apt-patch.py` takes Crow 2's baked grid picture out of frames 37 and 186. The files share their
revisions: a cached revision of another file is not "changed without a new revision".

**Skins (build 85).** At review the team decides whether a new-fighter pack goes live as its own character (as
above) or as a **skin**: an extra outfit of its template, the same gameplay with a new look. A skin takes no place:
its catalogue entry has `"fighter_mode": "skin"` and `"skin_of": <the template's front-end ID>` and no
`fighter_fe_id`, and the place the submission held is free again. Only `index-v4.json` lists skins (launchers up to
build 84 read `index-v3.json`, which leaves them out). The launcher composes a skin like a new fighter without the
Character row and the menu pictures (`content_compose.apply_skin`: the model files, entity, IntroExtro entry, camera
banks, animation tags) and lists it in `content-files/files.ini` as `skin=<pack id>|<template feID>|<model>|<name>`,
in catalogue order; its picture for the skin window is `<data>/online-art/skins/<pack id>.png` (from `art/body.png`).
`content.ini` marks it `fighter_mode=skin` and `skin_of=<feID>`, and the content print counts a skin with its mode.
In the game (`src/hooks/fighter_skins.c`) picking the template opens the skin window (Default plus each skin), on the
hub's CHOOSE FIGHTER and over the game's own select screens; the slot's model is swapped at match start, everything
else is the template's. A template's skins are numbered from 1 in catalogue order; online the number travels as
READY's third byte and in START's skins trailer (CAPS bit 22), so the opponent and spectators load the same model.
CPU fighters and random picks play Default. The last skin per fighter is kept in `<data>/skin-choices.ini`.
A skin shows exactly the pack's model files: its look is the maker's painted textures (the model's own `.xsh`) or
an imported model. A pack that is its template's files with only the model code renamed looks like the template, as a
skin and as its own character (FFNY Studio 0.2.0 built such packs while its view still showed another project's
paint: studio-scorcha and studio-cyborg-crow, 2026-11-16). The account service measures the look from the deltas
(`checks.own_look`: the model's `.o` or `.xsh` at least 2 % changed bytes) and refuses such a pack as a skin; Studio
from fix/skin-looks no longer builds one. A pack without `art/body.png` has no skin picture: the window shows the
template's portrait tinted, under the skin's name.

A relay before this branch refuses the longer HELLO; it is sent only by a PC with `content` packs, so the relay
goes out first. The relay answers a code join between different content sets with ERROR 15 (14 is the relay's "client not supported").

## The player's own packs (FFNY Studio, "Use in my game")

Modders play their packs in Story and Battle, not only in Studio's Test in a fight: new clothing, venue edits,
and also retextures of standard fighters and edits of standard venues, which are never submitted (online packs
only add new content) but are fine on their own PC. Studio's **Use in my game** (the Packs page and the editors'
headers) builds the project's package and unpacks it into `<data>/mods/my-<pack id>` with a `local.json`
(`{"schema": 1, "source": "ffny-studio", "pack_id", "name", "kind", "type", "version", "author", "description",
"project", "fighter_fe_id"?}`), then runs this install's `setup/mods.py compose`; **Remove from my game** deletes
the folder and composes again. The `my-` prefix never takes a community pack's id.

- `mods.py` (`local_packs`): every folder of `<data>/mods` with a `local.json` that `state.json` does not own.
  They apply after every community pack (order from `LOCAL_ORDER`), are left out while switched off
  (`settings.ini [content] off`, as a replacement, whatever their kind), and their content hash is computed from
  their files. `compose` (also the `compose` command, which now rewrites content.ini first, and a sync without a
  catalogue) takes them; `content.ini` lists them with `local=1` and `online_ok`.
- **Online.** A local pack never counts in the online content print. `online_ok=1` only for a pack that changes
  only how things look: no `install.json` (a texture pack through the texture loader; a language pack, whose
  rebuilt fight files would change the fight-files print anyway), or an `install.json` with no ops whose every
  entry replaces a `*.xsh` (texture) entry of a `.viv` with its own base (`local_online_safe`). Everything else
  (new clothing, venues, fighters; models, collision, placements, lights) is offline only: while one is on,
  `netplay.c`'s start refuses every online start (hub, room, code) with `PC_CONTENT_LOCAL_LINE`, "Your own packs
  are on: they work offline only. Turn them off in the launcher (Content) to play online.", and logs
  `[NETPLAY] refused: the player's own packs are on (<names>)`. No silent mismatch: the PC never meets others with
  files they lack. Retextures may stay on online: textures never reach the simulation, and the fight files
  (`k_fight_files`) are printed separately anyway.
- Launcher, Content page, Installed tab: **MY PACKS** below the community packs, one card each with Offline only /
  Online too and an On/Off switch (composes again at once); while an offline-only pack is on, the line above and
  **Turn them off**, which switches every offline-only pack off.
- New fighters: Studio picks a free place (100..127, not used by community or own packs or the game) and writes it
  as `fighter_fe_id` in local.json; a new venue keeps the fe_id of its op (a clash with a community venue skips the
  own pack, listed as not applied).

## Packs of several mods (bundles)

A modder can submit several mods as one **pack** ("Pack (N mods)", e.g. three new fighters, a venue and clothing
colours); everything else stays a **single** ("Single"). The account service checks each mod of a pack like a
single and, on Accept, lists every one of them as an ordinary catalogue mod (own id, type, kind, order, archive,
content hash and pack signature, all at the pack's version) with two more fields: `bundle` (the pack's id) and
`bundle_name` (its title). Only **`index-v3.json`** lists pack items; `index-v2.json` (build 79) and `index.json`
(up to build 78) leave them out, so older launchers never see them and refuse nothing.

The launcher treats a pack as one unit:

- **sync**: after installing, a pack listed in the catalogue whose mods are not all installed at the catalogue's
  version (one failed its download, hash or signature, needs a capability this launcher lacks, or a hand-installed
  folder uses its id) loses every installed mod of it, and the sync reports `<pack name>: not installed (<first
  reason>)` once. A sync that PLAY cancelled half way removes a pack whose installed mods are not one whole
  version of it (the receipts keep the pack's mod list, `bundle_items`); the next sync installs it again.
- **compose**: when `content_compose` skips any mod of a pack, the packs are applied again without any mod of that
  pack, so the composed files hold nothing of it; its other mods are listed as skipped with `another mod of the
  pack <name> could not be applied`. Singles compose exactly as before.
- **remove**: `mods.py remove --id <pack id or one of its mods' ids>` removes every mod of the pack.
- `content.ini` has `bundle=` and `bundle_name=` in each `[pack.<id>]` (empty for a single); `view.ini` carries
  them too. The game counts a pack's mods one by one, like singles: the switches (a language in a pack keeps its
  own On/Off) and the online content print are per mod, unchanged.

What players see: the Content page shows one card per pack, titled with its name, tagged **Pack (N mods)** and
listing its mods (name and type, and an On/Off switch for a replacement such as a language); a single's card is
tagged **Single**. The Community tab tags each gallery card **Single** or **Pack (N mods)** (the gallery's `form`
and `item_count`; a missing `form` is a single), and a pack's page lists its mods (`items`: title, type).

## Official maps (build 86)

The FFNY team's own maps (first **Knox and Jam**, the Run the Streets map) are not baked into the game build. The
account service publishes them into the same signed catalogue (server repo `docs/content-catalogue.md`, "Official
maps"; only the guild owner and the pack admins can, from FFNY Studio's Packs > Publish official map), and every
player gets them like a content pack: always on, never switched off, updated by a new catalogue version without a
game build.

- **Catalogue.** Only `index-v5.json` lists them (build 86 reads it, falling back to `index-v4.json` and then
  `index-v3.json` on a 404). Entry: `"kind": "official-map"`, `"type": "venue"`, an id starting `official-`,
  `"venue_fe_id"` (the new venue's front-end ID, 28..63, unique among official maps) and `"rts_map": true` for a Run
  the Streets map (absent: false). Versions need `official-map-v1` (offered by `pc_mods.cpp` from build 86). The
  pack signature covers the kind `official-map`. `mod_catalogue.py` refuses `rts_map`/`venue_fe_id` on any other
  mod.
- **Install and compose.** `mods.py sync` installs it like any synced pack (receipt `kind` `official-map`,
  `official`, `rts_map`, `venue_fe_id`); `content_compose` composes its install.json (a new-venue op) into the venue
  slot the op names, like a community venue, and never switches it off (`ALWAYS_ON`). The account service's claims
  keep its venue slot free of community packs; a player's own venue at the same slot comes later and is skipped.
- **content.ini** (the game's view, `pc_content.c`): the map's section says `kind=content` (so the game of build
  85 and later counts it as always-on content, in the content print), plus

  ```
  [pack.official-knox-and-jam]
  kind=content
  type=venue
  official=1
  rts_map=1
  composed=1
  venue_fe_id=28
  venue_code=28A
  bigarena=1
  ```

  **The interface for Run the Streets:** the map to load is the `[pack.*]` section with `official=1` and
  `rts_map=1` whose `composed=1` pack is applied (`files.ini`); `venue_fe_id` and `venue_code` name the venue
  (`V2BG_<venue_code>`); `bigarena=1` says the pack folder `<data>/mods/<id>/bigarena.json` holds its layout
  (`{"version": 1, "mode": "bigarena", "size": [w, d], "spawns": [[x, z], ...]}`, Studio's `SPAWN_*` points; read
  it defensively). With several, the lowest `order` wins. None: Run the Streets has no map on this PC.
- **Original fighters only / STANDARD ONLY.** An official map is a venue, never a fighter or a skin, so it never
  makes a pick "community". Its venue is a new venue, which STANDARD ONLY rooms leave out of their vote, and that is
  wanted: it is a Run the Streets map, loaded by that mode whatever the room's setting.

## Community content in offline play (build 90)

`settings.ini [content] community_offline` (default 1; the launcher's Content page and Basics, env
`RECOMP_CONTENT_OFFLINE`) lets a player keep their own offline mods away from the always-on community packs.

- **Off:** the game starts with the original data plus the player's own packs and the language packs. The synced
  community packs (fighters, clothing, venues, skins, official maps, retextures) are not mounted: `pc_content_list`
  leaves them out, the composed files come from `<data>/mod-manager/own-packs/content-files` (`mods.py compose` builds
  that composition of the player's own packs alone next to `content-files`), and offline Battle and Story show the
  standard roster and venues. Nothing is deleted or re-downloaded; the sync keeps running, so turning it on needs
  nothing. Run the Streets needs its official map, so it is not offered offline while the content is off.
- **Online needs it on.** With the setting off the game never connects (`netplay.c start()` refuses). ONLINE on the main
  menu opens the prompt "Online needs FFNY Online's community content. Restart with it now?": YES closes the game
  and the launcher in UPDATE NOW mode syncs, composes and starts it again with `--content-update`, which has the content
  on for that run only (the saved setting stays off) and opens the online hub; NO goes back.
- The log's `[CONTENT] community content in offline play:` line and F9's `[REPORT] community content in offline play:`
  line say which. Tests: `tests/unit/test_content_offline.py`.

## Testing

```
# a local account service with a throwaway test key (server repo relay/: go build -o accounts.exe ./cmd/accounts)
accounts.exe -content-keygen <scratch>\test-content-key.pem          (prints the test public key)
accounts.exe -listen 127.0.0.1:8195 -internal-listen 127.0.0.1:8196 -data <scratch>\accounts.json \
  -public-url http://127.0.0.1:8195 -client-id 0 -client-secret-file <file> -test-accounts owner \
  -packs-dir <scratch>\packs -content-key-file <scratch>\test-content-key.pem -content-dir <scratch>\content \
  -content-url http://127.0.0.1:8195/updates/content -content-serve -pack-admins 4242 -studio-open
  (-studio-open, tests only: every test account may submit; without it only accounts with the FFNY Studio Beta role do)
# submit (Studio), post and accept (the bot's ffny_packs against :8196), then:
python tools/defjam-mcp/dj.py lock run -- python scripts/test-content-launcher.py \
  --catalogue http://127.0.0.1:8195/updates/content/index.json --keys <keys.json with the test key>
python -m pytest -q tests/unit/test_content_sync.py tests/unit/test_content_compose.py
python setup/mods.py compose --data-dir <data> [--game-dir <dump>]     (compose by hand)
python tools/defjam-mcp/dj.py scenario offline --content-root <data>    (a fight with that folder's packs;
  online: --content-root "<data>," gives copy 0 the packs and copy 1 none, for ERROR 15)
```

`FFNY_CONTENT_URL` may name a plain-HTTP catalogue on 127.0.0.1 only, with its key from
`FFNY_CONTENT_TEST_KEYS`; `RECOMP_TEST_CONTENT_PRINT=<hex>` fakes a content set (the mismatch test);
`RECOMP_CONTENT=0` turns synced packs off for a run.

## Publishing FFNY Studio (closed test)

The Studio repository's `scripts/package.ps1` writes `dist\FFNYStudio-<version>-windows-x64.zip` and
`dist\studio-latest.json` (`build` = its commit count, `version`, `notes`, `setup`, `url`, `sha256`, `size`). The
publisher signs the manifest's exact bytes with the update key, as for `latest.json`
(`updatesign` from the server repository; the private key stays on the publisher's PC), and uploads three files to
the account service's Studio folder on the EU server, `/var/lib/ffny-accounts/studio/` (its `-studio-dir`; owned
by `ffny-accounts`). **Not** to the public update folder (`/opt/defjam-relay/updates/studio/`) while the test is
closed: the service hands the files only to accounts with access. The manifest's `url` is not used on this path.
Who gets access (the owner creates the Discord role `FFNY Studio Beta` and gives it to testers) is in the server
repository's `docs/studio-closed-test.md`.

```
studio-latest.json          exactly the bytes that were signed
studio-latest.json.sig      r||s, 128 hex digits
FFNYStudio-<version>-windows-x64.zip
```

Upload the zip first and the manifest and `.sig` last, so a launcher never sees a manifest whose zip is missing. A
launcher offers **Update Studio** when `build` is higher than the installed one. Older zips can be removed once the
new manifest is up.

## Why a PC lacks a pack (build 83)

- `setup/mods.py sync` writes `<data>/mod-manager/sync-result.ini` (`result` ok/failed/cancelled, `kind`
  clock/network/disk/catalogue/pack, a plain `reason`, `revision`, `packs`, `ids`, `skipped` for packs this
  build cannot take yet). A refresh that failed and fell back on the cached catalogue is `failed`.
- `mods.py check` (the running game's news check) writes `error=` and `kind=` into `news.ini` when it fails.
- The game logs both at its start (`[CONTENT] last sync ok, revision 5, 1 pack` / `[CONTENT] last sync FAILED:
  <reason>`, `[CONTENT] installed: ...`), F9 adds `[REPORT] content: ...`, and the online hub shows one line.
  Launcher PLAY syncs once more when the last sync failed.
- Every HTTPS call of `mods.py` retries once with `config/mods/isrg-roots.pem` (ISRG Root X1 and X2) after a
  certificate failure: Python's `ssl` reads the Windows root store as it is and never fetches a missing root as
  WinHTTP does. Signatures are checked as before.
