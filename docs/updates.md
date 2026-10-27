# Updates

The launcher offers new builds itself. Because every PC builds the game from its own dump, an
update is a new **setup** (source and tools, no game data): the launcher downloads it and starts
it in update mode, which rebuilds the game from the game files the installation already
extracted, with a progress bar. Saves, settings and HD textures stay.

While it runs, the GAME card and the update window show the step ("Step 6 of 8 · Translating the
game code"; the narrow card heads with the name and puts "Step 6 of 8" under the bar), the setup's
current line, the percentage and the time so far. The setup runs hidden
with `--status-file <status>` and writes three small files the launcher reads every half second:
`<status>` (the current line), `<status>.progress` (0-1000) and `<status>.stage`
(`<index> <total> <key> <name>`, e.g. `5 7 lift Translating the game code`; the launcher counts its
download as step 1, so that is step 6 of 8). A launcher that does not read the stage file, or a setup
that does not write it, still shows the line and the bar.

## How it is trusted

- `latest.json` on the update server names the newest build, its version, release notes, the setup
  file, its size and its SHA-256.
- `latest.json.sig` is an ECDSA P-256 signature over the manifest's exact bytes. The launcher has
  the public key built in (`src/hooks/updater.c`) and ignores a manifest that does not verify.
- The setup is only started when its SHA-256 matches the signed manifest.
- So neither the server nor the connection has to be trusted, and plain HTTP is enough.
- The private key is `%USERPROFILE%\.defjam\update-signing-key.pem` on the publisher's PC, never
  in the repository or on the server. **Back it up.** Losing it means shipping a setup with a new
  public key by hand; leaking it lets someone else sign updates.

## Publishing a build

1. Raise the number in `config/remaster-version.txt` and commit everything.
2. `.\scripts\publish-update.ps1 -Notes "What changed, in a sentence or two."`
   It builds the setup, writes and signs `latest.json`, checks the signature and uploads the setup,
   the signature and then the manifest to `/opt/defjam-relay/updates` on the server
   (`-NoUpload` to stop before uploading).
3. Launchers offer the build at their next start (Install now / Later), and on the General page.

## The server side

The relay (`netplay/relay`) serves the update folder over HTTP on TCP **27778** when started with
`-updates-listen :27778 -updates-dir /opt/defjam-relay/updates` (the systemd unit does). It answers
only GET and HEAD for plain file names in that folder.

## A development checkout

A copy built from source has no `installed.json` above it; its launcher reports a newer build but
does not install it. Update the source and build as usual.
