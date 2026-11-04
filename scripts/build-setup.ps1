<# Build only the game-free Windows launcher, setup and app; never analyze or build a dump. #>
param([switch]$Development)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'env.ps1')
Push-Location $repo
try {
    cmake -S setup -B build/setup -G Ninja -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE) { throw 'Setup configure failed' }
    cmake --build build/setup --target DefJamLauncher
    if ($LASTEXITCODE) { throw 'Launcher build failed' }
    $packageArgs = @('scripts/package-setup.py', '--launcher', 'build/setup/DefJamLauncher.exe')
    if ($Development) { $packageArgs += '--development' }
    python @packageArgs
    if ($LASTEXITCODE) { throw 'Setup payload packaging failed' }
    $payload = Join-Path $repo 'build/setup-payload.zip'
    cmake -S setup -B build/setup -G Ninja -DCMAKE_BUILD_TYPE=Release "-DSETUP_PAYLOAD=$payload"
    if ($LASTEXITCODE) { throw 'Setup payload configure failed' }
    cmake --build build/setup --target DefJamSetup
    if ($LASTEXITCODE) { throw 'Setup build failed' }
    # The app (src/app): game-free, built on its own (preset win-x64-app) with the same payload inside
    # it as resource 101, as DefJamSetup.exe carries it (D215).
    cmake --preset win-x64-app "-DDEFJAM_APP_PAYLOAD=$payload"
    if ($LASTEXITCODE) { throw 'App configure failed' }
    cmake --build --preset win-x64-app
    if ($LASTEXITCODE) { throw 'App build failed' }
    $assetArgs = @('scripts/check-setup-assets.py', '--payload', 'build/setup-payload.zip',
                  '--installer', 'build/setup/DefJamSetup.exe', '--app', 'build/win-x64-app/DefJamRecompiled.exe',
                  '--output-dir', 'build/setup-assets')
    if ($Development) { $assetArgs += '--development' }
    python @assetArgs
    if ($LASTEXITCODE) { throw 'Setup asset validation failed' }
} finally { Pop-Location }
