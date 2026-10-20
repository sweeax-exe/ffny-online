<#
.SYNOPSIS  Publish this checkout as an update the launchers will offer (docs/updates.md).
.DESCRIPTION
  1. Requires a clean, committed checkout (the setup is built from it).
  2. Builds the game-free setup (scripts/build-setup.ps1).
  3. Writes latest.json (build number from config/remaster-version.txt, version, notes,
     setup file name, SHA-256, size) and signs it with the private update key.
  4. Uploads the setup, then the signature and the manifest, to the update folder on the
     server, the manifest last so a launcher never sees a manifest without its setup.
  5. Pushes the checkout to GitHub (remote ffny, main) and makes the GitHub release
     v<version> with the setup and its .sha256. The Discord bot announces the build by itself
     from latest.json within five minutes.
  Raise config/remaster-version.txt and commit before publishing a new build.
.PARAMETER Notes   One or two sentences for the update dialog.
.PARAMETER Server  The ssh hosts (entries in ~/.ssh/config), comma-separated: the project's server,
                   and the first one for as long as older installs ask there.
.PARAMETER NoUpload  Build and sign only (build/update/).
.PARAMETER NoGitHub  Upload to the servers but make no GitHub release.
#>
param(
    [Parameter(Mandatory = $true)][string]$Notes,
    [string[]]$Server = @('ffny-online', 'hetzner-sweeax'),
    [string]$Key = "$env:USERPROFILE\.defjam\update-signing-key.pem",
    [switch]$NoUpload,
    [switch]$NoGitHub
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location $repo
if (git status --porcelain --untracked-files=no) { throw 'Commit your changes first: the setup is built from the committed checkout.' }
if (-not (Test-Path $Key)) { throw "No update signing key at $Key" }
$build = [int](Get-Content config\remaster-version.txt -TotalCount 1)
$base = (Select-String -Path CMakeLists.txt -Pattern 'project\(\S+\s+VERSION\s+([0-9.]+)' | Select-Object -First 1).Matches.Groups[1].Value
if (-not $base) { $base = '0.7.2' }
$version = "$base-remaster.$build"

& (Join-Path $PSScriptRoot 'build-setup.ps1')
$out = Join-Path $repo 'build\update'
New-Item -ItemType Directory -Force $out | Out-Null
$setupName = "DefJamSetup-remaster-$build.exe"
$setup = Join-Path $out $setupName
Copy-Item (Get-ChildItem build\setup-assets\DefJamSetup-*-windows-x64.exe | Select-Object -First 1).FullName $setup -Force
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item $setup).Length

$manifest = [ordered]@{ build = $build; version = $version; notes = $Notes; setup = $setupName; sha256 = $hash; size = $size }
$json = $manifest | ConvertTo-Json -Compress
[IO.File]::WriteAllText((Join-Path $out 'latest.json'), $json, (New-Object Text.UTF8Encoding($false)))

$sign = Join-Path $repo 'netplay\relay\dist\updatesign.exe'
if (-not (Test-Path $sign)) {
    Push-Location netplay\relay
    & "C:\Program Files\Go\bin\go.exe" build -o dist\updatesign.exe .\cmd\updatesign
    Pop-Location
}
& $sign sign -key $Key -in (Join-Path $out 'latest.json') -out (Join-Path $out 'latest.json.sig') | Out-Null
$pub = (Get-Content "$env:USERPROFILE\.defjam\update-signing-key.pub.txt" -TotalCount 1).Trim()
& $sign verify -pub $pub -in (Join-Path $out 'latest.json') -sig (Join-Path $out 'latest.json.sig')
if ($LASTEXITCODE) { throw 'The fresh signature does not verify' }
"Prepared $version ($setupName, $size bytes, sha256 $hash)"
if ($NoUpload) { return }

$remote = '/opt/defjam-relay/updates'
foreach ($host_ in $Server) {
scp -o BatchMode=yes $setup "$($host_):/tmp/$setupName"
scp -o BatchMode=yes (Join-Path $out 'latest.json') "$($host_):/tmp/defjam-latest.json"
scp -o BatchMode=yes (Join-Path $out 'latest.json.sig') "$($host_):/tmp/defjam-latest.json.sig"
ssh -o BatchMode=yes $host_ ("set -e; install -d -m 0755 $remote; " +
    "install -m 0644 /tmp/$setupName $remote/$setupName; " +
    "install -m 0644 /tmp/defjam-latest.json.sig $remote/latest.json.sig.new; " +
    "install -m 0644 /tmp/defjam-latest.json $remote/latest.json.new; " +
    "mv $remote/latest.json.sig.new $remote/latest.json.sig; mv $remote/latest.json.new $remote/latest.json; " +
    "rm -f /tmp/$setupName /tmp/defjam-latest.json /tmp/defjam-latest.json.sig; ls -l $remote")
if ($LASTEXITCODE) { throw "Upload to $host_ failed" }
}
"Published $version. Launchers will offer it at their next start."
if ($NoGitHub) { return }

# GitHub: the code first, then the release with the very setup the servers offer.
git push ffny HEAD:main
if ($LASTEXITCODE) { throw 'Push to GitHub failed; the servers already offer the build' }
$tag = "v$version"
$asset = Join-Path $out "DefJamSetup-$version-windows-x64.exe"
Copy-Item $setup $asset -Force
[IO.File]::WriteAllText("$asset.sha256", "$hash  $(Split-Path $asset -Leaf)`n", (New-Object Text.UTF8Encoding($false)))
$body = @"
**FFNY Online** build $build. Def Jam: Fight for NY on PC with online multiplayer. Unofficial fan project, built on [Vlad Yanhursky's DefJamFFNY-recomp](https://github.com/vyanhursky/DefJamFFNY-recomp).

### What's new
$Notes

### What you need
- Windows 10/11, 64-bit
- Your own **USA Xbox** copy of the game, dumped to your PC (see the [disc guide](https://github.com/sweeax-exe/ffny-online/blob/main/docs/dump-your-disc.md)). PS2 and GameCube versions do not work. This download contains no game data: the setup checks your dump and builds the game on your PC.

### Install
Download ``DefJamSetup-$version-windows-x64.exe``, run it, point it at your dump. The setup is unsigned, so Windows SmartScreen may warn; choose "More info" and "Run anyway". Installed copies update through the launcher.

Questions, bugs, opponents: [Discord](https://discord.gg/zCZKVDnKaP). SHA-256 of the setup is in the ``.sha256`` file next to it.
"@
$notesFile = Join-Path $out 'release-notes.md'
[IO.File]::WriteAllText($notesFile, $body, (New-Object Text.UTF8Encoding($false)))
gh release create $tag $asset "$asset.sha256" --repo sweeax-exe/ffny-online --target main `
    --title "FFNY Online - build $build" --notes-file $notesFile --latest
if ($LASTEXITCODE) { throw 'The GitHub release failed; the servers already offer the build' }
"GitHub release $tag is out. The Discord bot posts the build within five minutes."
