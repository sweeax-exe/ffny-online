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
| `src/hooks/pc_content.c` | Reads `content.ini` and the player's switches (`settings.ini [content] off`). At game start the synced texture packs go beneath the player's own packs in `RECOMP_TEXTURE_PACKS`; the online **content print** (FNV-1a over id, version and content hash of every `content` pack, in order) is mixed into the build fingerprint and sent after the build number in HELLO, so rooms only match PCs with the same `content` set. Replacements and language packs never count. |
| Launcher | Syncs at its start and with each update check (`pc_launcher_content.inc`); PLAY during a sync cancels the download and waits for it to stop. The **Content** button opens the page: installed packs in catalogue order, `content` packs marked ALWAYS ON, a switch for each `replacement`. Settings > General > Game language lists English, French and every language pack that is on. A hidden **FFNY Studio** button stub (`RECOMP_STUDIO_BUTTON=1` shows it). |
| Community tab | The Content page's second tab (`pc_launcher_community.inc`): every submitted pack from the account service's gallery (`GET /api/gallery`, sorted by votes or newest), also packs in review and rejected ones with the reason; the selected pack's page with its preview, description and changelog; Vote / Voted for signed-in players (one vote per account and pack). View only: nothing is downloaded from here. FFNY Studio's Community view reads the same endpoints. `RECOMP_CONTENT_TAB=community` opens it (screenshots). |
| `RECOMP_OFFLINE_ONLY=1` | Studio's Test in a fight: every online start is refused ("Online is off in this test session"). |

Kinds: `content` (new fighters, clothing, venues) is always on and part of online matching; `replacement`
(retextures, language packs) downloads too but can be switched off per PC. A language pack is used only
when chosen as the Game language.

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
python -m pytest -q tests/unit/test_content_sync.py
```

`FFNY_CONTENT_URL` may name a plain-HTTP catalogue on 127.0.0.1 only, with its key from
`FFNY_CONTENT_TEST_KEYS`; `RECOMP_TEST_CONTENT_PRINT=<hex>` fakes a content set (the mismatch test);
`RECOMP_CONTENT=0` turns synced packs off for a run.
