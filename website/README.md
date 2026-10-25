# defjam.online

The start page of FFNY Online: one static page (`index.html`, `style.css`, `site.js`) with the
logo, a few screenshots, short texts and a download button for the newest Windows installer.
No framework, no external fonts or CDNs, no cookies, no trackers.

## Build

```
python website/build.py            # writes website/dist/
python website/build.py --out DIR  # or anywhere else
```

The pictures, the logo and the Anton font are not stored here a second time. `build.py` copies
them from `docs/media/` and `ffny-branding/` into `dist/`, next to the page, and stops if any of
them is not in the owner-approved list of `scripts/check-source-tree.py` or has changed since it
was approved. It also checks that every file the page refers to exists, and adds a content hash
to the CSS and JS links (`style.css?v=…`) so browsers never mix an old and a new upload.
`website/dist/` is ignored by git.

A new picture on the page needs the owner's approval and its fingerprint in
`scripts/check-source-tree.py` first; then add it to `ASSETS` in `build.py`.

To look at it locally, serve the folder:

```
python -m http.server 8000 --directory website/dist
```

## The download button

Without JavaScript the button opens the
[latest release](https://github.com/sweeax-exe/ffny-online/releases/latest). With it, `site.js`
asks `https://api.github.com/repos/sweeax-exe/ffny-online/releases/latest`, links the asset named
`DefJamSetup-*-windows-x64.exe` directly and shows the build, version, size and date. A
`*-macos-arm64.dmg` or `*-linux-x64.tar.gz` in the release appears as a small link below it, as
does the installer's `.sha256` file. The answer is kept in `sessionStorage` for 15 minutes,
because the API allows 60 requests an hour per address; if the API fails, the fallback link stays.
Nothing on the page needs changing for a new release.

## Deploy

The page is served by Caddy on the FFNY server, from `/var/www/defjam.online`. The site block is
`netplay/relay/deploy/Caddyfile.site-snippet` (`defjam.online`, with `www.defjam.online`
redirected to it; HTTPS certificates are automatic, a strict Content-Security-Policy allows only
the page's own files and the GitHub API).

Not set up yet. When the owner decides to go live:

1. DNS: A (and AAAA) records for `defjam.online` and `www.defjam.online` pointing at the server.
2. Upload the build:
   ```
   python website/build.py
   ssh root@SERVER mkdir -p /var/www/defjam.online
   scp -r website/dist/* root@SERVER:/var/www/defjam.online/
   ```
3. Add the site block to `/etc/caddy/Caddyfile`, then
   `caddy validate --config /etc/caddy/Caddyfile && systemctl reload caddy` (a graceful reload; the
   account service at `play.defjam.online` keeps running).

Note that `setup-accounts.sh` replaces `/etc/caddy/Caddyfile` with `deploy/Caddyfile` when the
installed one still carries its "managed by setup-accounts.sh" line. Either append the snippet to
`deploy/Caddyfile` in the repository, or remove that line from the server's copy, so a later
account-service deploy does not drop the site.

Later updates are step 2 only. The server is shared: ask before touching Caddy on it.
