<#
.SYNOPSIS  Publish this checkout as an update the launchers will offer (docs/updates.md).
.DESCRIPTION
  1. Requires a clean, committed checkout (the setup is built from it).
  2. Builds the game-free setup (scripts/build-setup.ps1).
  3. Writes latest.json (build number from config/remaster-version.txt, version, notes,
     setup file name, SHA-256, size) and signs it with the private update key.
  4. Uploads the setup, then the signature and the manifest, to the update folder on the
     server, the manifest last so a launcher never sees a manifest without its setup.
  Raise config/remaster-version.txt and commit before publishing a new build.
.PARAMETER Notes   One or two sentences for the update dialog.
.PARAMETER Server  The ssh host (an entry in ~/.ssh/config).
.PARAMETER NoUpload  Build and sign only (build/update/).
#>
param(
    [Parameter(Mandatory = $true)][string]$Notes,
    [string]$Server = 'hetzner-sweeax',
    [string]$Key = "$env:USERPROFILE\.defjam\update-signing-key.pem",
    [switch]$NoUpload
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
scp -o BatchMode=yes $setup "$($Server):/tmp/$setupName"
scp -o BatchMode=yes (Join-Path $out 'latest.json') "$($Server):/tmp/defjam-latest.json"
scp -o BatchMode=yes (Join-Path $out 'latest.json.sig') "$($Server):/tmp/defjam-latest.json.sig"
ssh -o BatchMode=yes $Server ("set -e; install -d -m 0755 $remote; " +
    "install -m 0644 /tmp/$setupName $remote/$setupName; " +
    "install -m 0644 /tmp/defjam-latest.json.sig $remote/latest.json.sig.new; " +
    "install -m 0644 /tmp/defjam-latest.json $remote/latest.json.new; " +
    "mv $remote/latest.json.sig.new $remote/latest.json.sig; mv $remote/latest.json.new $remote/latest.json; " +
    "rm -f /tmp/$setupName /tmp/defjam-latest.json /tmp/defjam-latest.json.sig; ls -l $remote")
if ($LASTEXITCODE) { throw 'Upload failed' }
"Published $version. Launchers will offer it at their next start."
