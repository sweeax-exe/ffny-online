<#
.SYNOPSIS  Publish this checkout as an update the launchers will offer (docs/updates.md).
.DESCRIPTION
  1. Requires a clean, committed checkout.
  2. Takes the game-free setup GitHub Actions built from this exact commit (docs/releasing.md,
     "FFNY Online builds"): pushes HEAD to ffny/main if it is not there yet, waits for the CI run
     of HEAD on that branch to finish green, downloads its windows-setup-assets artifact and checks
     it (provenance source_commit = HEAD, toolkit = the gitlink, not a development build, SHA-256
     of the setup = its .sha256 file = the provenance). Only -LocalBuild builds it here instead.
  3. Code signing (unless -NoCodeSign): scripts/codesign-setup.py Authenticode-signs the launcher
     inside the payload and then the setup with the Certum certificate (SimplySign Desktop), checks
     every signature with signtool verify /pa, and proves that signing is the only change from the
     CI build. The provenance then names the CI hashes and the signed ones. A probe signature at
     the start (and the signing itself) stops the script before anything is pushed or uploaded
     when the SimplySign session has expired.
  4. Writes latest.json (build number from config/remaster-version.txt, version, notes,
     setup file name, SHA-256, size of the signed setup) and signs it with the private update key.
  5. Uploads the setup, then the signature and the manifest, to the update folder on the
     server, the manifest last so a launcher never sees a manifest without its setup.
  6. Makes the GitHub release v<version> with the setup, its .sha256 and its provenance. The Discord bot announces the build by itself
     from latest.json within five minutes.
  Raise config/remaster-version.txt and commit before publishing a new build.
.PARAMETER Notes   One or two sentences for the update dialog.
.PARAMETER Server  The ssh hosts (entries in ~/.ssh/config), comma-separated: the project's server,
                   and the first one for as long as older installs ask there.
.PARAMETER RelayOnly Relays that serve no downloads but need the manifest for their build gate
                   (the regional relays, ssh aliases ffny-nj, ffny-na2, ffny-sg2, ffny-sa, ffny-dfw, ffny-lax): latest.json and its signature only.
.PARAMETER NoUpload  Fetch (or build), verify and sign only (build/update/).
.PARAMETER NoGitHub  Upload to the servers but make no GitHub release. With -NoUpload or
                   -NoGitHub nothing is pushed: HEAD must already be on ffny/<Branch>.
                   Both together are a dry run.
.PARAMETER Branch    The ffny branch whose push CI run built HEAD (default main; another branch
                   only for a dry run, e.g. -Branch ci/green -NoUpload -NoGitHub).
.PARAMETER CiTimeoutMinutes How long to wait for that CI run (default 120).
.PARAMETER LocalBuild Emergency only: build the setup on this PC (scripts/build-setup.ps1) instead
                   of taking the CI artifact. Players then get a binary CI did not build (it is
                   still code-signed); say why in the release notes.
.PARAMETER NoCodeSign Emergency only: publish the setup without Authenticode signatures (SmartScreen warns).
.PARAMETER CodeSignThumbprint The code-signing certificate in CurrentUser\My (default: the Certum one).
.PARAMETER SignTool  signtool.exe (default: the Windows 10 SDK one codesign-setup.py finds).
.PARAMETER TimestampUrl The RFC 3161 timestamp server (default Certum's).
#>
param(
    [Parameter(Mandatory = $true)][string]$Notes,
    [string[]]$Server = @('ffny-online', 'hetzner-sweeax'),
    [string[]]$RelayOnly = @('ffny-nj', 'ffny-na2', 'ffny-sg2', 'ffny-sa', 'ffny-dfw', 'ffny-lax'),
    [string]$Key = "$env:USERPROFILE\.defjam\update-signing-key.pem",
    [switch]$NoUpload,
    [switch]$NoGitHub,
    [string]$Branch = 'main',
    [int]$CiTimeoutMinutes = 120,
    [switch]$LocalBuild,
    [switch]$NoCodeSign,
    [string]$CodeSignThumbprint = '16E6B5882628B2F023AD98D546FA63AADA92D9D7',
    [string]$SignTool = '',
    [string]$TimestampUrl = 'http://time.certum.pl'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location $repo
if (git status --porcelain --untracked-files=no) { throw 'Commit your changes first: the setup is built from the committed checkout.' }
$ghRepo = 'sweeax-exe/ffny-online'
$commit = (git rev-parse HEAD).Trim()
if (-not (Test-Path $Key)) { throw "No update signing key at $Key" }

# Code signing: the certificate must be there, and a probe signature proves the SimplySign session
# is live before anything is pushed (the CI wait can take long; the signing itself checks again).
$codeSign = -not $NoCodeSign
$signLogin = 'Log in to SimplySign Desktop (tray) and run again'
$signArgs = @('--thumbprint', $CodeSignThumbprint, '--timestamp', $TimestampUrl)
if ($SignTool) { $signArgs += @('--signtool', $SignTool) }
function Assert-CodeSigned([string]$Path) {
    # The last gate before a file leaves this PC: a valid, timestamped signature by our certificate.
    $s = Get-AuthenticodeSignature -LiteralPath $Path
    if ($s.Status -ne 'Valid' -or $s.SignerCertificate.Thumbprint -ne $CodeSignThumbprint -or -not $s.TimeStamperCertificate) {
        throw "$Path is not validly signed by $CodeSignThumbprint ($($s.Status)); nothing was uploaded from here on"
    }
}
if ($codeSign) {
    $cert = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CodeSignThumbprint" -ErrorAction SilentlyContinue
    if (-not $cert) { throw "Code-signing certificate $CodeSignThumbprint is not in CurrentUser\My. Start SimplySign Desktop, or -NoCodeSign in an emergency." }
    if ($cert.NotAfter -lt (Get-Date).AddDays(1)) { throw "Code-signing certificate $CodeSignThumbprint expires $($cert.NotAfter); renew it" }
    if ($cert.NotAfter -lt (Get-Date).AddDays(30)) { Write-Warning "The code-signing certificate expires $($cert.NotAfter); renew it soon." }
    python (Join-Path $PSScriptRoot 'codesign-setup.py') probe @signArgs
    if ($LASTEXITCODE -eq 3) { throw "Code signing is not available: $signLogin" }
    if ($LASTEXITCODE) { throw 'The code-signing probe failed' }
} else {
    Write-Warning 'NO CODE SIGNING: players get an unsigned setup and launcher (SmartScreen warns). Use it only when signing cannot work and a fix cannot wait.'
}
$build = [int](Get-Content config\remaster-version.txt -TotalCount 1)
$base = (Select-String -Path CMakeLists.txt -Pattern 'project\(\S+\s+VERSION\s+([0-9.]+)' | Select-Object -First 1).Matches.Groups[1].Value
if (-not $base) { $base = '0.7.2' }
$version = "$base-remaster.$build"

$out = Join-Path $repo 'build\update'
New-Item -ItemType Directory -Force $out | Out-Null
$setupName = "DefJamSetup-remaster-$build.exe"
$setup = Join-Path $out $setupName
$provenance = $null
if ($LocalBuild) {
    Write-Warning 'LOCAL BUILD: this setup is built on this PC, not by CI from the public source. Its provenance is only this checkout. Use it only when CI is down and a fix cannot wait.'
    & (Join-Path $PSScriptRoot 'build-setup.ps1')
    $built = Get-ChildItem build\setup-assets\DefJamSetup-*-windows-x64.exe | Select-Object -First 1
    $provenance = Get-ChildItem build\setup-assets\DefJamSetup-*-windows-x64.provenance.json | Select-Object -First 1
    $builtBy = 'local'; $runUrl = $null
} else {
    # 1. The commit must be on GitHub: CI builds only what was pushed.
    git fetch -q ffny $Branch
    if ($LASTEXITCODE) { throw "Cannot fetch ffny/$Branch" }
    git merge-base --is-ancestor $commit "ffny/$Branch"
    if ($LASTEXITCODE) {
        if ($NoUpload -or $NoGitHub -or $Branch -ne 'main') { throw "HEAD $commit is not on ffny/$Branch; push it first (a dry run pushes nothing)" }
        "Pushing $commit to ffny/main so CI builds the setup"
        git push ffny HEAD:main
        if ($LASTEXITCODE) { throw 'Push to GitHub failed; nothing was published' }
    }
    # 2. The CI run of exactly this commit, pushed to that branch, finished and green.
    $deadline = (Get-Date).AddMinutes($CiTimeoutMinutes)
    $run = $null
    while ($true) {
        $runs = gh run list --repo $ghRepo --workflow ci.yml --branch $Branch --event push --commit $commit `
            --json databaseId,status,conclusion,headSha,url --limit 5 | ConvertFrom-Json
        if ($LASTEXITCODE) { throw 'gh run list failed' }
        $run = @($runs | Where-Object { $_.headSha -eq $commit }) | Select-Object -First 1
        if ($run -and $run.status -eq 'completed') { break }
        if ((Get-Date) -gt $deadline) { throw "No finished CI run for $commit on $Branch after $CiTimeoutMinutes minutes" }
        $state = if ($run) { "$($run.status) $($run.url)" } else { 'not started yet' }
        "Waiting for CI of $($commit.Substring(0, 12)) on ${Branch}: $state"
        Start-Sleep -Seconds 30
    }
    if ($run.conclusion -ne 'success') { throw "CI run $($run.url) ended '$($run.conclusion)'; fix it and push again (or -LocalBuild in an emergency)" }
    "CI run $($run.url) is green"
    # 3. Its Windows setup artifact, into a fresh folder.
    $ci = Join-Path $repo "build\ci-setup\$($run.databaseId)"
    if (Test-Path $ci) { Remove-Item -Recurse -Force $ci }
    New-Item -ItemType Directory -Force $ci | Out-Null
    gh run download $run.databaseId --repo $ghRepo --name windows-setup-assets --dir $ci
    if ($LASTEXITCODE) { throw "Run $($run.databaseId) has no windows-setup-assets artifact (a docs-only commit skips the setup build)" }
    $files = @(Get-ChildItem $ci -File)
    $exe = @($files | Where-Object { $_.Name -like 'DefJamSetup-*-windows-x64.exe' })
    if ($exe.Count -ne 1 -or $files.Count -ne 3) { throw "Unexpected artifact contents: $($files.Name -join ', ')" }
    $exe = $exe[0]
    $stem = $exe.BaseName
    $provenance = Get-Item (Join-Path $ci "$stem.provenance.json")
    $shaFile = Get-Item (Join-Path $ci "$stem.sha256")
    # 4. Provenance: built from HEAD and its toolkit pin, as a production (not development) payload.
    $p = Get-Content $provenance.FullName -Raw | ConvertFrom-Json
    $pin = ((git ls-tree HEAD tools/xboxrecomp) -split '\s+')[2]
    $actual = (Get-FileHash $exe.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $listed = ((Get-Content $shaFile.FullName -TotalCount 1) -split '\s+')[0].ToLowerInvariant()
    $problems = @()
    if ($p.source_commit -ne $commit) { $problems += "source_commit $($p.source_commit) is not HEAD $commit" }
    if ($p.toolkit_commit -ne $pin) { $problems += "toolkit_commit $($p.toolkit_commit) is not the gitlink $pin" }
    if ($p.platform -ne 'windows-x64') { $problems += "platform $($p.platform)" }
    if ($p.development) { $problems += 'a development payload' }
    if ($p.version -ne $base) { $problems += "version $($p.version) is not CMakeLists' $base" }
    if ($stem -ne "DefJamSetup-$base-windows-x64") { $problems += "file name $($exe.Name)" }
    if ($actual -ne $listed -or $actual -ne $p.installer_sha256) { $problems += "sha256 $actual, .sha256 says $listed, provenance $($p.installer_sha256)" }
    if ($problems) { throw ('The CI setup does not match this commit: ' + ($problems -join '; ')) }
    "Verified $($exe.Name) from CI run $($run.databaseId): source $commit, toolkit $pin, sha256 $actual"
    $built = $exe
    $builtBy = 'ci'; $runUrl = $run.url
}
# 5. Code signing here, on the publishing PC: the certificate's key is in Certum's cloud behind a
#    phone login, which CI cannot reach. codesign-setup.py signs the launcher inside the payload and
#    the setup, checks each signature and that nothing but signing changed, and writes the signed
#    setup with a provenance naming the CI hashes before signing and the hashes after.
if ($codeSign) {
    $signedDir = Join-Path $out 'signed'
    python (Join-Path $PSScriptRoot 'codesign-setup.py') sign --installer $built.FullName --provenance $provenance.FullName `
        --output-dir $signedDir --built-by $builtBy @(if ($runUrl) { '--run-url', $runUrl }) @signArgs
    if ($LASTEXITCODE -eq 3) { throw "Code signing stopped, nothing was uploaded: $signLogin" }
    if ($LASTEXITCODE) { throw 'Code signing failed; nothing was uploaded' }
    $signedExe = Get-Item (Join-Path $signedDir $built.Name)
    $provenance = Get-Item (Join-Path $signedDir "$($built.BaseName).provenance.json")
    $sp = Get-Content $provenance.FullName -Raw | ConvertFrom-Json
    if ($sp.source_commit -ne $commit) { throw 'The signed provenance names another commit' }
    if ($sp.installer_sha256 -ne (Get-FileHash $signedExe.FullName -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'The signed provenance does not describe the signed setup' }
    Copy-Item $signedExe.FullName $setup -Force
    Assert-CodeSigned $setup
    "Code-signed: CI sha256 $($sp.codesign.before_signing.installer_sha256) -> signed $($sp.installer_sha256); launcher in the payload $(($sp.codesign.signed_files | Where-Object { $_.path -eq 'payload/DefJamLauncher.exe' }).sha256_after)"
} else {
    Copy-Item $built.FullName $setup -Force
}
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item $setup).Length

$manifest = [ordered]@{ build = $build; version = $version; notes = $Notes; setup = $setupName; sha256 = $hash; size = $size }
$json = $manifest | ConvertTo-Json -Compress
# PowerShell 5.1 writes < > & ' as \u003c and so on; launchers before build 27 showed them literally
# ("Launcher u003e Settings"). They are plain characters in JSON strings.
$json = $json.Replace('\u003c', '<').Replace('\u003e', '>').Replace('\u0026', '&').Replace('\u0027', "'")
[IO.File]::WriteAllText((Join-Path $out 'latest.json'), $json, (New-Object Text.UTF8Encoding($false)))

# updatesign is in the private server repository, cloned as server\ (or where FFNY_SERVER_DIR points)
$relaySrc = if ($env:FFNY_SERVER_DIR) { Join-Path $env:FFNY_SERVER_DIR 'relay' } else { Join-Path $repo 'server\relay' }
if (-not (Test-Path (Join-Path $relaySrc 'go.mod'))) { throw "The server repository is missing: clone sweeax-exe/ffny-server as $repo\server" }
$sign = Join-Path $relaySrc 'dist\updatesign.exe'
if (-not (Test-Path $sign)) {
    Push-Location $relaySrc
    & "C:\Program Files\Go\bin\go.exe" build -o dist\updatesign.exe .\cmd\updatesign
    Pop-Location
}
& $sign sign -key $Key -in (Join-Path $out 'latest.json') -out (Join-Path $out 'latest.json.sig') | Out-Null
$pub = (Get-Content "$env:USERPROFILE\.defjam\update-signing-key.pub.txt" -TotalCount 1).Trim()
& $sign verify -pub $pub -in (Join-Path $out 'latest.json') -sig (Join-Path $out 'latest.json.sig')
if ($LASTEXITCODE) { throw 'The fresh signature does not verify' }
# The release notes (written now so a dry run shows them; only the GitHub release uses them).
if ($codeSign) {
    $signedText = 'The setup and the launcher are code-signed (Certum open-source code signing). While the certificate is new, Windows SmartScreen may still warn; choose "More info" and "Run anyway".'
    $signedProof = " Signing is the only change to the setup CI built: CI SHA-256 ``$($sp.codesign.before_signing.installer_sha256)``, signed ``$hash`` (the ``.provenance.json`` lists every signed file before and after)."
} else {
    $signedText = 'The setup is unsigned, so Windows SmartScreen may warn; choose "More info" and "Run anyway".'
    $signedProof = ''
}
$body = @"
**FFNY Online** build $build. Def Jam: Fight for NY on PC with online multiplayer. Unofficial fan project, built on [Vlad Yanhursky's DefJamFFNY-recomp](https://github.com/vyanhursky/DefJamFFNY-recomp).

### What's new
$Notes

### What you need
- Windows 10/11, 64-bit
- Your own **USA Xbox** copy of the game, dumped to your PC (see the [disc guide](https://github.com/sweeax-exe/ffny-online/blob/main/docs/dump-your-disc.md)). PS2 and GameCube versions do not work. This download contains no game data: the setup checks your dump and builds the game on your PC.

### Install
Download ``DefJamSetup-$version-windows-x64.exe``, run it, point it at your dump. $signedText Installed copies update through the launcher.

Questions, bugs, opponents: [Discord](https://discord.gg/ffnyonline). SHA-256 of the setup is in the ``.sha256`` file next to it.$signedProof
"@
$notesFile = Join-Path $out 'release-notes.md'
[IO.File]::WriteAllText($notesFile, $body, (New-Object Text.UTF8Encoding($false)))
"Prepared $version ($setupName, $size bytes, sha256 $hash)"
if ($NoUpload) { return }
if ($codeSign) { Assert-CodeSigned $setup }   # never upload an unsigned setup while signing is on

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
foreach ($host_ in $RelayOnly) {
scp -o BatchMode=yes (Join-Path $out 'latest.json') "$($host_):/tmp/defjam-latest.json"
scp -o BatchMode=yes (Join-Path $out 'latest.json.sig') "$($host_):/tmp/defjam-latest.json.sig"
ssh -o BatchMode=yes $host_ ("set -e; install -d -m 0755 $remote; " +
    "install -m 0644 /tmp/defjam-latest.json.sig $remote/latest.json.sig.new; " +
    "install -m 0644 /tmp/defjam-latest.json $remote/latest.json.new; " +
    "mv $remote/latest.json.sig.new $remote/latest.json.sig; mv $remote/latest.json.new $remote/latest.json; " +
    "rm -f /tmp/defjam-latest.json /tmp/defjam-latest.json.sig")
if ($LASTEXITCODE) { throw "Manifest upload to $host_ failed" }
}
"Published $version. Launchers will offer it at their next start."
if ($NoGitHub) { return }

# GitHub: the release with the very setup the servers offer. The code is on ffny/main already
# (CI built the setup from it); -LocalBuild pushes it here.
if ($LocalBuild) {
    git push ffny HEAD:main
    if ($LASTEXITCODE) { throw 'Push to GitHub failed; the servers already offer the build' }
}
$tag = "v$version"
$asset = Join-Path $out "DefJamSetup-$version-windows-x64.exe"
Copy-Item $setup $asset -Force
if ($codeSign) { Assert-CodeSigned $asset }
[IO.File]::WriteAllText("$asset.sha256", "$hash  $(Split-Path $asset -Leaf)`n", (New-Object Text.UTF8Encoding($false)))
$assets = @($asset, "$asset.sha256")
if ($provenance) {
    Copy-Item $provenance.FullName "$asset.provenance.json" -Force
    $assets += "$asset.provenance.json"
}
gh release create $tag @assets --repo sweeax-exe/ffny-online --target main `
    --title "FFNY Online - build $build" --notes-file $notesFile --latest
if ($LASTEXITCODE) { throw 'The GitHub release failed; the servers already offer the build' }
"GitHub release $tag is out. The Discord bot posts the build within five minutes."
