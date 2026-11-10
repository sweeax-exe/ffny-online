# Community content packs

FFNY Online's mods pipeline: a modder builds a pack in FFNY Studio and submits it with their FFNY account;
the account service checks it and queues it; the team reviews it in Discord (Accept / Reject); on Accept the
service signs the pack and our catalogue with its **content-signing key** (never the update key, kept only on
the server); every launcher then installs it by itself. The server and bot side is in the private server
repository (`docs/content-catalogue.md` there); Studio's side is in the Studio repository.

## In the game repository

| Piece | What it does |
|---|---|
| `setup/mods.py sync` | Fetches `https://play.defjam.online/updates/content/index.json` and `.sig`, checks the Ed25519 signature against `config/mods/catalogue-keys.json` (the content key's public half), the 30-day expiry and the revision (no rollback). Installs every listed pack in the catalogue's `order` (clothing IDs depend on it), downloading only packs whose version or archive changed; checks each pack's `content_sha256` and `pack_signature`; removes synced packs the catalogue dropped or revoked; keeps a folder the player installed by hand. Writes `<data>/mod-manager/content.ini`. |
| `setup/mod_catalogue.py` | The catalogue contract, with FFNY's optional fields: `kind` (`content` / `replacement`), `type`, `order`, `language`; per version `content_sha256`, `pack_signature`. |
| `setup/mods.py compose`, `setup/content_compose.py` | Composed packs (below): rebuilds the patched game files from the player's dump and each pack's deltas and ops. Runs at the end of every `sync`, as `mods.py compose` (the Content page's switch starts it), and is skipped when the packs, the switches and the bases are unchanged. `setup/ffnycontent/` is the part of FFNY Studio's library it needs (deltas, BIG archives, `djv2.sod`, `.loc`, the parts list, clothing ops, new venues). |
| `src/hooks/pc_content.c` | Reads `content.ini` and the player's switches (`settings.ini [content] off`). At game start the synced texture packs go beneath the player's own packs in `RECOMP_TEXTURE_PACKS`, and the composed files are served through the game-file redirect (ahead of `<data>/override`; the language and song redirects chain to it; `[CONTENT] n composed file(s) served` in the log). The online **content print** (FNV-1a over id, version and content hash of every `content` pack, in order) is mixed into the build fingerprint and sent after the build number in HELLO, so rooms only match PCs with the same `content` set; a composed `content` pack counts only when `files.ini` lists it as applied. Replacements and language packs never count. |
| Launcher | Syncs at its start and with each update check (`pc_launcher_content.inc`); PLAY during a sync cancels the download and waits for it to stop. The **Content** button opens the page: installed packs in catalogue order, `content` packs marked ALWAYS ON, a switch for each `replacement`. Settings > General > Game language lists English, French and every language pack that is on. |
| Community tab | The Content page's second tab (`pc_launcher_community.inc`): every submitted pack from the account service's gallery (`GET /api/gallery`, sorted by votes or newest), also packs in review and rejected ones with the reason; the selected pack's page with its preview, description and changelog; Vote / Voted for signed-in players (one vote per account and pack). View only: nothing is downloaded from here. FFNY Studio's Community view reads the same endpoints. `RECOMP_CONTENT_TAB=community` opens it (screenshots). |
| FFNY Studio button | The home screen's **FFNY STUDIO** card between the logo and PLAY (`pc_launcher_studio.inc`, `studio_app.cpp`, `studio_unzip.cpp`; Windows only). At the launcher's start (and on hover, at most every two minutes) it reads `studio/studio-latest.json` and its `.sig` from the update port, falling back to `https://play.defjam.online/updates/studio/`, and believes it only with a valid signature by the update key (`update_sig.h`, as `latest.json`). The card stays hidden until such a manifest exists or Studio is installed. Its button: **Get FFNY Studio (<size>)**, the download's progress, **Open FFNY Studio**, or **Update Studio** when the published `build` is newer; a failure is one line in the card. The zip is the manifest's `setup` beside the manifest (an absolute `url` only on the same host), resumed, checked against `size` and `sha256`, unpacked (deflate, CRC-checked, only names under `FFNYStudio/`) into `<studio>\versions\<version>\`, then `<studio>\current.txt` is switched; the previous version stays, older ones go. `<studio>` = `RECOMP_STUDIO_DIR`, else `studio` beside the data folder (`%LOCALAPPDATA%\DefJamRecompiled\studio`). Open starts `FFNYStudio.exe --install-dir <install> --data-root <data> --account-file <data>\account.dat` in the exe's folder (`--install-dir` only for an installed copy). Tests: `RECOMP_STUDIO_SERVER=127.0.0.1:<port>` (plain HTTP, `/studio/...`), `RECOMP_STUDIO_TEST_KEY=<128 hex>` (only with that local server), `RECOMP_TEST_STUDIO=<frame>:press;...`, `RECOMP_STUDIO_BUTTON=1` (show the card without a manifest); `scripts/test-studio-launcher.py` runs the whole path. |
| `RECOMP_OFFLINE_ONLY=1` | Studio's Test in a fight: every online start is refused ("Online is off in this test session"). |

Kinds: `content` (new fighters, clothing, venues) is always on and part of online matching; `replacement`
(retextures, language packs) downloads too but can be switched off per PC. A language pack is used only
when chosen as the Game language.

## Composed packs (fighter-body, clothing, venue)

These packs never carry the game's bytes. Their `install.json` (schema 1; the shared contract with the
server and Studio) lists **entries**, each a delta (`deltas/*.ffdelta`, FFDL v1: copy ranges from the player's
own entry plus the modder's bytes) that `replace`s or `add`s one archive entry or loose file, and **ops**
(clothing `colour` / `item`, venue `new-venue`) that append to the player's `djv2.sod`, parts list, name tables,
`venue.xml` and `entity.xml`. The catalogue lists them with the capability `content-v1`; content.ini marks them
`composed=1`.

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

A relay before this branch refuses the longer HELLO; it is sent only by a PC with `content` packs, so the relay
goes out first. The relay answers a code join between different content sets with ERROR 15 (14 is the relay's "client not supported").

## Testing

```
# a local account service with a throwaway test key (server repo relay/: go build -o accounts.exe ./cmd/accounts)
accounts.exe -content-keygen <scratch>\test-content-key.pem          (prints the test public key)
accounts.exe -listen 127.0.0.1:8195 -internal-listen 127.0.0.1:8196 -data <scratch>\accounts.json \
  -public-url http://127.0.0.1:8195 -client-id 0 -client-secret-file <file> -test-accounts owner \
  -packs-dir <scratch>\packs -content-key-file <scratch>\test-content-key.pem -content-dir <scratch>\content \
  -content-url http://127.0.0.1:8195/updates/content -content-serve -pack-admins 4242
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

## Publishing FFNY Studio

The Studio repository's `scripts/package.ps1` writes `dist\FFNYStudio-<version>-windows-x64.zip` and
`dist\studio-latest.json` (`build` = its commit count, `version`, `notes`, `setup`, `url`, `sha256`, `size`). The
publisher signs the manifest's exact bytes with the update key, as for `latest.json`
(`updatesign` from the server repository; the private key stays on the publisher's PC), and uploads three files to
`/srv/ffny-updates/studio/` on each update server (EU, and the NA copy as for `latest.json`):

```
studio-latest.json          exactly the bytes that were signed
studio-latest.json.sig      r||s, 128 hex digits
FFNYStudio-<version>-windows-x64.zip
```

Upload the zip first and the manifest and `.sig` last, so a launcher never sees a manifest whose zip is missing. A
launcher offers **Update Studio** when `build` is higher than the installed one. Older zips can be removed once the
new manifest is up.
